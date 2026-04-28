# RuntimeCore 测试覆盖缺口

---

## 测试审查 (2026-04-08 13:10)

### 一、现有测试问题

#### Issue-01：`PartialFailurePreservesGoodModules` 实际没有触发失败分支，测试名与断言目标脱节

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.PartialFailurePreservesGoodModules` |
| 行号范围 | 173-231 |
| 问题描述 | 用例名声称验证“另一个模块编译失败后保留好模块”，但 `BadSource` 实际上是合法脚本：`int BrokenEntry() { return 0; }`。测试在 219 行还显式断言第二个模块“compile should succeed”，随后却在 225 行用“after a failed compile”描述断言。整个用例从头到尾都没有制造 compile error、没有检查 `ECompileResult`，也没有验证失败回滚路径。 |
| 影响 | 文件系统热重载最关键的错误恢复合同完全未被覆盖；一旦“坏模块失败时保留旧模块/好模块”的逻辑回归，当前测试仍会稳定绿灯，形成明显误报。 |
| 修复建议 | 把第二个脚本改成明确的非法脚本，例如引用未定义类型；改用 `CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)` 断言 `bCompiled == false` 且 `CompileResult` 为 `Error` 或 `ErrorNeedFullReload`；补充 `AddExpectedError` 捕获日志；失败后同时断言好模块仍可执行、返回旧值，坏模块不会以新版本注册到 `GetModuleByFilenameOrModuleName()`。 |

#### Issue-02：`EngineParityTests` 直接污染共享 production engine，既绕过 `FAngelscriptEngineScope` 也不清理临时模块

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.*` |
| 行号范围 | 19-695 |
| 问题描述 | 文件通过 `GetProductionEngineForParity()` 直接拿全局 production engine（19-27 行），后续多个用例在这台共享引擎上调用 `GetModule(..., asGM_ALWAYS_CREATE)` 创建 `CollisionProfileParity`、`WorldCollisionParity`、`FIntPointParity`、`FVector2fParity`、`RuntimeCurveLinearColorParity`、`HitResultParity`、`StartupBindRegistryParity` 等临时模块，但整个文件没有任何 `FAngelscriptEngineScope`，也没有 `DiscardModule` 或统一 teardown。 |
| 影响 | 这些 parity 用例依赖编辑器启动顺序和进程级单例状态，测试之间会共享并残留模块；一旦某个用例改动模块表、当前 engine 解析或后续脚本环境，其他用例会在同一台 production engine 上继承污染，隔离性失真。 |
| 修复建议 | 不再直接复用 production engine 作为测试沙箱；优先改成 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`/`ASTEST_BEGIN_SHARE_CLEAN`，或者至少在每个用例内显式创建 `FAngelscriptEngineScope` 并用 `ON_SCOPE_EXIT` 调用 `DiscardModule` 清掉临时模块。若确实必须对 production engine 做 parity 观察，测试也应只读，不应在共享实例上动态编译脚本模块。 |

#### Issue-03：`GCScenarioTests` 虽然调用了 `CollectGarbage()`，但没有验证任何 Angelscript 引用存活/释放语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.GC.ActorDestroy` / `Angelscript.TestModule.GC.ComponentDestroy` / `Angelscript.TestModule.GC.WorldTeardown` |
| 行号范围 | 73-239 |
| 问题描述 | 三个用例都只做 UE 层销毁路径：`Actor->Destroy()`、`Component->DestroyComponent()` 或离开 `Spawner` 作用域，然后执行 `CollectGarbage(RF_NoFlags, true)`，最后只检查 `TWeakObjectPtr` 失效。测试中没有任何 script 侧持有引用、没有对象存活期断言、没有在释放 script 引用前后各跑一次 GC 对比，因此并不能证明 Angelscript GC 集成是否正确处理对象保活和回收。 |
| 影响 | 即使 RuntimeCore 把 script reference graph、GC callback、对象保活语义全部实现错了，这三个用例仍可能通过，因为它们验证的只是“UE 对象销毁后弱指针最终失效”这一通用行为。真正的脚本对象泄漏、提前回收、循环引用等回归不会被发现。 |
| 修复建议 | 为每个场景补上 script 持有引用的中间状态：先让脚本字段或脚本组件引用 actor/component/world，第一次 GC 后断言对象仍存活；再显式清空 script 引用并再次 GC，断言对象被回收。建议新增 shared helper 封装“创建脚本引用者 + 双阶段 GC + 弱指针/脚本返回值断言”，并把现有三个用例升级成真正的 Angelscript GC 场景测试。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-01 |
| BadIsolation | 1 | Issue-02 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 16:17)

### 二、需要新增的测试

#### NewTest-20：给 `UAngelscriptAbilityTaskLibrary` 补齐代表性 wrapper 的返回类型与 owning ability 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `WaitDelay(UGameplayAbility*, float)` / `WaitGameplayEvent(...)` / `WaitGameplayTagAdd(...)` / `WaitGameplayTagRemove(...)` / `WaitGameplayTagQuery(...)` / `WaitInputPress(...)` / `WaitInputRelease(...)` / `WaitTargetDataUsingActor(...)` |
| 现有测试覆盖 | 完全无测试；当前 RuntimeCore 只覆盖了 `UAngelscriptAbilityAsyncLibrary` 的 handwritten entry 存在性和 `UAngelscriptAbilityTask` 生命周期，没有任何用例直接保护 `UAngelscriptAbilityTaskLibrary` 这层脚本可见 wrapper |
| 风险评估 | 一旦 wrapper 指到错误的 native task 工厂、返回错 task class、owner ability 丢失，或某个公开函数从脚本面静默消失，当前自动化不会报警；GAS 脚本看到的 API 仍可能“有声明但运行时错对象” |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.RepresentativeWrappersReturnExpectedTaskTypes` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryTests.cpp` |
| 场景描述 | 搭一个最小 GAS fixture：测试 actor + `UAngelscriptAbilitySystemComponent` + 轻量 `UGameplayAbility` test double。分别直接调用 `WaitDelay`、`WaitGameplayEvent`、`WaitGameplayTagAdd`、`WaitGameplayTagRemove`、`WaitGameplayTagQuery`、`WaitInputPress`、`WaitInputRelease`、`WaitTargetDataUsingActor` 八个代表 wrapper，覆盖时间参数、tag、query、bool flag 和 target-actor overload 几类签名 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；一个可初始化 actor info 的 ASC/ability fixture；`FGameplayTag`、`FGameplayTagQuery`、`AGameplayAbilityTargetActor` test double；必要时增加一个小型 access shim 读取 task 上公开前置状态 |
| 期望行为 | 每个 wrapper 都返回非空 task，且 `GetClass()` 分别等于 `UAbilityTask_WaitDelay`、`UAbilityTask_WaitGameplayEvent`、`UAbilityTask_WaitGameplayTagAdded`、`UAbilityTask_WaitGameplayTagRemoved`、`UAbilityTask_WaitGameplayTagQuery`、`UAbilityTask_WaitInputPress`、`UAbilityTask_WaitInputRelease`、`UAbilityTask_WaitTargetData`；所有 task 的 outer/owning ability 都等于传入 ability；代表性输入如 tag/query、`bTriggerOnce`、`TaskInstanceName`、target actor 在 task 创建后保持一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + GAS fixture helper（ASC + ability actor info）+ 小型 access shim |
| 优先级 | P1 |

#### NewTest-21：覆盖 `UAngelscriptTestCommandlet::Main()` 的初始编译失败短路返回码

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp` |
| 关联函数 | `UAngelscriptTestCommandlet::Main(const FString& Params)` |
| 现有测试覆盖 | 完全无测试；当前 `RuntimeCore` 文档里只覆盖了 `AllScriptRootsCommandlet` 的建议，没有任何用例直接保护 `AngelscriptTestCommandlet` 的返回码合同 |
| 风险评估 | 如果 commandlet 在 `bDidInitialCompileSucceed == false` 时不再返回 `1`，CI/BuildGraph 很容易把“脚本初始编译失败”误判成测试通过或错误地落到后续 unit-test 执行路径 |
| 建议测试名 | `Angelscript.TestModule.Core.Commandlet.TestCommandlet.InitialCompileFailureReturnsOne` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestCommandletTests.cpp` |
| 场景描述 | 在 production-like 或 isolated full engine 下创建 `UAngelscriptTestCommandlet`，记录当前 `bDidInitialCompileSucceed` 和 active module 数量，临时把 `bDidInitialCompileSucceed` 置为 `false` 后调用 `Main(TEXT(""))`，结束时恢复原值 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::ProductionLike)` 或 `CreateFullTestEngine()` + `FAngelscriptEngineScope`；一个 scoped bool restore helper；`NewObject<UAngelscriptTestCommandlet>()` |
| 期望行为 | `Main()` 必须直接返回 `1`；调用前后的 active module 数量保持不变；当前 engine 指针不发生切换，证明命中了“初始编译失败立即短路”而不是继续跑 unit tests |
| 使用的 Helper | `FAngelscriptTestFixture` + scoped value restore helper |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:39:02)

### 二、需要新增的测试

#### NewTest-69：覆盖 `FAngelscriptRuntimeModule::StartupModule()` 的 editor/commandlet 启动门控与 fallback ticker 注册合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 关联函数 | `FAngelscriptRuntimeModule::StartupModule()` |
| 现有测试覆盖 | 当前文档里已记录的 `NewTest-22`、`NewTest-65`、`NewTest-66` 只分别覆盖 `InitializeAngelscript()`、`TickFallbackPrimaryEngine(...)` 和 `ShutdownModule()`；目标测试目录与已记录建议都没有任何一条直接调用 `StartupModule()`，也没有验证 `GIsEditor || IsRunningCommandlet()` 的初始化门控，以及 `GIsEditor` 下 fallback ticker 的注册分支。 |
| 风险评估 | `StartupModule()` 是 runtime module 生命周期的真正入口。若回归成“commandlet 不再初始化 Angelscript”“非 editor/game runtime 仍错误注册 fallback ticker”，或 editor 启动时漏掉 ticker handle，后果会是 commandlet 模式脚本环境静默失效、普通运行时出现多余 core ticker、或 editor 下 fallback tick 根本不工作，而当前自动化没有任何定点报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.RuntimeModule.StartupModuleHonorsEditorAndCommandletGates` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp` |
| 场景描述 | 在测试文件里扩展 `FAngelscriptRuntimeModuleTickTestAccess`，并给 `StartupModule()` 增加一个仅 `WITH_DEV_AUTOMATION_TESTS` 可用的环境 seam，例如可覆写的 `IsEditorForTesting()` / `IsRunningCommandletForTesting()` 谓词，或抽出可白盒调用的 `ShouldInitializeOnStartup()` / `ShouldRegisterFallbackTicker()` helper。测试使用 `SetInitializeOverrideForTesting(...)` 记录 initialize 调用次数，并分别驱动三段环境：1. `bEditor=true, bCommandlet=false`；2. `bEditor=false, bCommandlet=true`；3. `bEditor=false, bCommandlet=false`。每段都创建局部 `FAngelscriptRuntimeModule Module`，调用 `StartupModule()` 后立即检查 initialize 次数与 `FallbackTickHandle` 状态，再以 `ShutdownModule()` 收尾。 |
| 输入/前置 | `FAngelscriptRuntimeModuleTickTestAccess`；`SetInitializeOverrideForTesting(...)` / `ResetInitializeStateForTesting()`；一个计数型 override lambda；需要时用 `FAngelscriptEngineContextStack::SnapshotAndClear()` 做 scoped guard，确保 override push 的 engine 不泄漏到其他测试；`ON_SCOPE_EXIT` 恢复环境 seam 与 initialize state。 |
| 期望行为 | editor case 下 `StartupModule()` 必须恰好触发 1 次 initialize，且 `FallbackTickHandle.IsValid() == true`；commandlet-only case 下也必须触发 1 次 initialize，但 `FallbackTickHandle.IsValid() == false`；普通非 editor 且非 commandlet case 下 initialize 次数必须保持 `0`，并且 `FallbackTickHandle` 仍无效。三段在 `ShutdownModule()` 后都不应残留有效 ticker handle 或脏 context stack，证明 startup gate 与 teardown 配对正确。 |
| 使用的 Helper | `FAngelscriptRuntimeModuleTickTestAccess` + startup-environment test seam + `SetInitializeOverrideForTesting(...)` + `ResetInitializeStateForTesting()` + `FAngelscriptEngineContextStack` scoped guard + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 13:18:55)

### 记录校正

本轮有效新增发现为：
- `NewTest-81`
- `NewTest-82`

上述 2 条完整正文已写入本文前段，且前两次续写都未落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:36:10)

### 记录校正

本轮有效新增发现为：
- `NewTest-83`
- `NewTest-84`

上述 2 条完整正文已写入本文前段，但本次续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正的文末续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:51:53)

### 记录校正

本轮继续回扫 `AngelscriptEngine.cpp` 时，`NewTest-85` 的完整正文已写入本文前段，但上一轮续写再次命中了前文重复汇总锚点，没有落在真实文件尾；本段从真实文末继续追加，供后续轮次从这里去重与续写。

### 一、现有测试问题

本轮未新增现有测试质量问题；新增发现集中在当前目标测试与全仓测试都没有建立直接合同的 runtime type-interoperability helper。

### 二、需要新增的测试

#### NewTest-86：覆盖 `GetUnrealStructFromAngelscriptTypeId(...)` 的 struct/enum/delegate/template 分流合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::GetUnrealStructFromAngelscriptTypeId(int)` |
| 现有测试覆盖 | 全仓测试与当前文档都对 `GetUnrealStructFromAngelscriptTypeId` 为 0 直接命中；虽然 `Bind_FInstancedStruct.cpp` 与 `Bind_FAngelscriptDelegateWithPayload.cpp` 会在运行时调用它，但当前 `Core/Subsystem/GC/FileSystem` 目标测试、`AngelscriptRuntime/Tests` 和已记录建议都没有任何一条直接验证它对 plain struct、enum、delegate/multicast delegate、template subtype 和非法 type id 的分流结果。 |
| 风险评估 | 这是 wildcard struct、`FInstancedStruct` 和 delegate payload boxing 共享的类型映射入口。若 helper 把 enum/模板/委托误当成 `UStruct*` 返回，或 plain struct 映射到错误 `UScriptStruct`，脚本侧会表现成“看似拿到了类型，但运行时装箱/解箱和反射全部错位”，而当前自动化没有定点报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.TypeInterop.GetUnrealStructFromTypeIdRejectsNonStructAndPreservesPlainStructs` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineTypeInteropTests.cpp` |
| 场景描述 | 在 isolated full engine 下先编译一个最小脚本模块，声明 `delegate void FAutomationSingleCast(int32 Value);` 与 `event void FAutomationMultiCast(int32 Value);`，确保 delegate/event type info 可查询。随后从 script engine 取 5 组 type id：1. `FIntPoint` plain struct；2. `ECollisionChannel` enum；3. `TArray<FIntPoint>` 模板类型；4. `FAutomationSingleCast` delegate；5. `FAutomationMultiCast` event。最后再额外构造一个非法 type id（如 `-1`）作为 error-path control，并逐个调用 `GetUnrealStructFromAngelscriptTypeId(...)`。 |
| 输入/前置 | `AngelscriptTestSupport::CreateFullTestEngine()` + `FAngelscriptEngineScope`；`AngelscriptTestSupport::BuildModule(...)` 编译包含 delegate/event 声明的测试模块；`Engine.GetScriptEngine()->GetTypeIdByDecl(...)` 或 `GetTypeInfoByName(...)->GetTypeId()`；`TBaseStructure<FIntPoint>::Get()` 作为 plain-struct 对照；必要时用 `TestNotNull` 先确认 5 组 type info/type id 都能解析。 |
| 期望行为 | `FIntPoint` 的 type id 必须映射到 `TBaseStructure<FIntPoint>::Get()`；`ECollisionChannel`、`TArray<FIntPoint>`、`FAutomationSingleCast`、`FAutomationMultiCast` 和非法 type id 都必须返回 `nullptr`。这样才能证明 helper 既能保留 plain struct 的 `UStruct*`，也不会把 enum、模板实例、single-cast delegate、multicast event 或非法 id 误判成可直接暴露给 Unreal 反射层的 struct。 |
| 使用的 Helper | `AngelscriptTestSupport::CreateFullTestEngine()` + `FAngelscriptEngineScope` + `BuildModule(...)` + `GetTypeIdByDecl(...)` / `GetTypeInfoByName(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:49:18)

### 一、现有测试问题

本轮未新增现有测试质量问题；已完成对 `AngelscriptEngine.cpp` helper 层的补充回扫，新增发现集中在当前目标测试与全仓测试检索都未建立直接合同的 engine execution helper。

### 二、需要新增的测试

#### NewTest-85：覆盖 `PrepareAngelscriptContextWithLog(...)` 的跨引擎失败路径与日志合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `PrepareAngelscriptContextWithLog(asIScriptContext*, asIScriptFunction*, const TCHAR*)` |
| 现有测试覆盖 | 全仓测试检索 `PrepareAngelscriptContextWithLog` 为 0 命中；虽然 `AngelscriptTest.cpp`、`ASClass.cpp`、`ASStruct.cpp`、`Bind_Console.cpp`、`AngelscriptType.cpp` 等 runtime 路径都依赖这个 helper，但当前 `Core/Subsystem/GC/FileSystem` 目标测试以及 `AngelscriptRuntime/Tests` 都没有任何一条主动制造 `Context->Prepare(...) < 0` 的失败场景来验证它的返回值和日志合同。 |
| 风险评估 | 这是脚本执行、类生成、结构体构造、测试发现等多条路径共用的 prepare 防线。一旦跨引擎/失效函数 prepare 失败时不再返回 `false`、日志丢失 callsite、或失败后污染后续 context 复用，当前自动化不会给出任何定点报警，只会在更高层以“脚本没跑起来”形式迟到暴露。 |
| 建议测试名 | `Angelscript.TestModule.Engine.Context.PrepareContextLogsCrossEngineMismatch` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineExecutionGuardTests.cpp` |
| 场景描述 | 在测试文件里创建两台 isolated full engine：`EngineA` 与 `EngineB`。先在 `EngineA` 编译最小模块 `int Entry() { return 1; }` 并拿到 `asIScriptFunction* EntryA`；随后从 `EngineB` 创建 `asIScriptContext* ContextB`，并用 `AddExpectedError(...)` 捕获 `Failed to prepare Angelscript context for 'Automation.PrepareMismatch'`。调用 `PrepareAngelscriptContextWithLog(ContextB, EntryA, TEXT("Automation.PrepareMismatch"))` 后，再在 `EngineB` 自己编译同签名 control 模块、拿到 `EntryB`，用新的 `ContextB2` 再调一次 helper 作为成功对照。 |
| 输入/前置 | `AngelscriptTestSupport::CreateFullTestEngine()` 两次；局部 `FScopedContextStackGuard`（仿 `FCoreTestContextStackGuard`）清空/恢复 `FAngelscriptEngineContextStack`，避免跨引擎 current-engine 污染；`AngelscriptTestSupport::BuildModule(...)` / `GetFunctionByDecl(...)`；`AddExpectedError(...)`；`ON_SCOPE_EXIT` 释放 `asIScriptContext` 并 reset 两台 engine。 |
| 期望行为 | mismatch case 下 `PrepareAngelscriptContextWithLog(...)` 必须返回 `false`，且精确命中包含 callsite `Automation.PrepareMismatch` 的 error log；同一轮 control case 下 `PrepareAngelscriptContextWithLog(ContextB2, EntryB, TEXT("Automation.PrepareControl"))` 必须返回 `true`，证明失败路径不会把另一台 engine 的 context prepare 永久污染。若可读状态，可再补一条断言确认 mismatch 后 `ContextB` 没有进入 active/suspended 状态。 |
| 使用的 Helper | `AngelscriptTestSupport::CreateFullTestEngine()` + 本地 `FScopedContextStackGuard` + `BuildModule(...)` / `GetFunctionByDecl(...)` + `AddExpectedError(...)` + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:33:51)

### 一、现有测试问题

本轮未新增现有测试质量问题；已完成对 `Core/`、`Subsystem/`、`GC/`、`FileSystem/` 目标测试文件的逐文件复核，本轮新增发现集中在 `AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` 的未覆盖错误路径。

### 二、需要新增的测试

#### NewTest-83：给 `UAngelscriptAbilitySystemComponent` 补齐 null ability-class wrapper 的防御性错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `GiveAbility_Internal(...)` / `GiveAbilityAndActivateOnce_Internal(...)` / `CancelAbility(...)` / `IsAbilityActive(...)` / `HasAbility(...)` / `CanActivateAbilityByClass(...)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-03`、`NewTest-31`、`NewTest-48` 只覆盖 valid ability class / valid handle 的正向路径；目标测试目录和已记录建议都没有任何一条直接把 `TSubclassOf<UGameplayAbility>()` 作为空输入传给这些 public wrapper，验证 “记录 ensure 且不改状态” 这条显式防御合同。 |
| 风险评估 | 这些函数都是脚本/蓝图可直接调用的公共 API。若 null ability class 路径回归成“静默写入半成品 spec”“错误触发 `OnAbilityGiven` / `OnAbilityRemoved`”“把已有 active ability 状态清掉”或 simply 崩溃，当前自动化不会给出任何定位点，最终只会在运行时以难复现的空引用输入事故暴露。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.NullAbilityClassWrappersFailDeterministicallyWithoutMutatingState` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemErrorPathTests.cpp` |
| 场景描述 | 建一个最小 GAS fixture：owner actor + `UAngelscriptAbilitySystemComponent` + 一条 valid native test ability 作为 control，并绑定 `OnAbilityGiven` / `OnAbilityRemoved` recorder。先用 valid ability 授予一条 baseline spec，记录 handle、source object、当前 ability 数量。随后构造 `TSubclassOf<UGameplayAbility> NullAbilityClass`，依次调用 `BP_GiveAbility(NullAbilityClass)`、`BP_GiveAbilityAndActivateOnce(NullAbilityClass)`、`CanActivateAbilityByClass(NullAbilityClass)`、`CancelAbility(NullAbilityClass)`、`IsAbilityActive(NullAbilityClass)`、`HasAbility(NullAbilityClass)`。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或等价 ASC fixture；一条 valid test ability + `SourceObject` baseline；`AddExpectedError(...)` 精确捕获 `Please provide a valid InAbilityClass to GiveAbility()`、`...GiveAbilityAndActivateOnce()`、`...CancelAbility()`、`...IsAbilityActive()`、`...HasAbility()`；若 `CanActivateAbilityByClass(nullptr)` 走 warning 而非 ensure，则补 log capture helper。 |
| 期望行为 | 两条 give wrapper 都必须返回 invalid `FGameplayAbilitySpecHandle`；`CanActivateAbilityByClass(NullAbilityClass)`、`IsAbilityActive(NullAbilityClass)`、`HasAbility(NullAbilityClass)` 都返回 `false`；`CancelAbility(NullAbilityClass)` 不崩溃且不改变任何现有 spec。调用前后的 valid baseline ability handle、`SourceObject`、`HasAbility(ValidAbility)`、`OnAbilityGiven` 次数、`OnAbilityRemoved` 次数都保持不变，证明 null 输入只走防御路径而不会污染活跃 ASC 状态。 |
| 使用的 Helper | `FAngelscriptTestFixture` + ASC/ability fixture + delegate recorder + `AddExpectedError(...)` / warning log capture helper |
| 优先级 | P2 |

#### NewTest-84：给 checked attribute getter 补齐“缺失 attribute set”错误路径，防止返回未定义值

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `GetAttributeCurrentValueChecked(...) const` / `GetAttributeBaseValueChecked(...) const` |
| 现有测试覆盖 | 当前文档里的 `NewTest-32` 只覆盖 `TrySetAttributeBaseValue(...)`、`TryGetAttributeCurrentValue(...)`、`TryGetAttributeBaseValue(...)` 的正向 owner-delegation；`NewTest-62` 只覆盖 non-checked getter 在 set 缺失时返回调用方 `DefaultValue`。checked getter 的缺失-set 分支在目标测试目录和已记录建议里仍是 0 命中。 |
| 风险评估 | 这两个 wrapper 目前先声明局部 `float Out...Value;`，再把失败与否交给 `ensureMsgf(TryGet...)`。如果缺失-set 路径没有被测试锁住，就很容易把 “记录错误后返回确定 sentinel” 退化成读取未初始化栈值，最终在脚本侧表现为随机数值、平台相关抖动，甚至把错误定位彻底带偏。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.CheckedAttributeGettersReportMissingSetDeterministically` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemQueryTests.cpp` |
| 场景描述 | 创建最小 ASC fixture，但故意不注册目标 `UAngelscriptAttributeSet`。准备一个声明了 `Health` 属性的 test `UAngelscriptAttributeSet` class，先对缺失 set 调 `GetAttributeCurrentValueChecked(TestSetClass, TEXT("Health"))` 与 `GetAttributeBaseValueChecked(TestSetClass, TEXT("Health"))`；随后再注册真实 set，并把 `Health` 设成已知值做 control。为避免偶然读到相同栈垃圾，缺失-path 建议各调用两次，并在两次调用之间插入额外局部变量/不同分支，确认返回仍然稳定。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或等价 ASC fixture；一个含 `Health` 的 test `UAngelscriptAttributeSet` class；`AddExpectedError(...)` 捕获 `Could not set attribute base value for attribute <Health>` 两次；control 段使用 `RegisterAttributeSet(...)` + `TrySetAttributeBaseValue(...)` 把 `Health` 设成例如 `25.f`。 |
| 期望行为 | 缺失-set 阶段，两条 checked getter 都必须命中预期错误日志，且各自两次调用返回相同的确定值；建议把该值固定成 `0.f`，避免返回未初始化栈数据。整个缺失-path 不得隐式创建新的 attribute set，也不得改变 `GetSpawnedAttributes()` 数量。control 阶段在注册真实 set 后，`GetAttributeCurrentValueChecked(...)` 与 `GetAttributeBaseValueChecked(...)` 都应稳定返回 `25.f`，证明测试验证的是错误路径合同，而不是 fixture 本身失效。 |
| 使用的 Helper | `FAngelscriptTestFixture` + ASC fixture + test attribute set + `AddExpectedError(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |
*** End of File
---

## 测试审查 (2026-04-09 13:15:34)

### 一、现有测试问题

本轮未新增现有测试质量问题；继续聚焦 `AngelscriptRuntime/Core/` 中文档尚未覆盖的基础 bind contract。

### 二、需要新增的测试

#### NewTest-81：给 `FAngelscriptBinds::FNamespace` / `FEnumBind` 补齐 namespace 恢复与 enum 去重合同

| 字段 | 内容 |
|------|------|
| 优先级 | P1 |
| 缺口类型 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` |
| 关联函数 | `FAngelscriptBinds::FNamespace::FNamespace(FBindString)` / `~FNamespace()` / `FAngelscriptBinds::FEnumBind::FEnumBind(FBindString)` / `FEnumBind::FEnumElement::operator=(int32)` |
| 现有测试覆盖 | 当前目标测试目录与 `AngelscriptRuntime/Tests` 中没有任何一条直接实例化 `FAngelscriptBinds::FNamespace` 或 `FEnumBind`；仓库内现有 namespace 相关用例都是直接调用 `asIScriptEngine::SetDefaultNamespace(...)`，并不保护这个 RAII 封装和 enum value 去重逻辑。 |
| 风险评估 | 这两段代码属于最底层 bind 注册基础设施。若 `FNamespace` 析构不恢复默认 namespace，后续 bind 会被静默注册进错误命名空间；若 `FEnumBind` 重复赋值不去重、或 `asALREADY_REGISTERED` 路径退化，脚本侧 enum 类型会出现值重复、命名冲突或跨模块注册不稳定，而当前自动化不会给出定点报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.Binds.NamespaceGuardRestoresDefaultNamespaceAndEnumBindDeduplicatesValues` |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindsRegistrationTests.cpp` |
| 场景描述 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 建一个干净 engine。先记录 `ScriptEngine->GetDefaultNamespace()` 基线，然后进入局部作用域创建 `FAngelscriptBinds::FNamespace NamespaceGuard(TEXT("Automation.BindEnum"))`；在该作用域内用 `FAngelscriptBinds::Enum(TEXT("EAutomationBindEnum"))` 注册一个 enum，并对同一元素名重复赋值两次，例如 `["Ready"] = 1`、再次 `["Ready"] = 1`，再追加 `["Done"] = 2`。离开作用域后验证默认 namespace 已恢复到基线。随后分别从全局 namespace 与 `Automation.BindEnum` namespace 查询 `EAutomationBindEnum`，并检查 enum value 列表。必要时再用第二个 `FNamespace` guard 嵌套一层，确认恢复顺序遵循栈语义。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；`asIScriptEngine* ScriptEngine`；一个小型 helper `FindEnumValueCount(asITypeInfo*)` 用于读取 enum value 数；必要时用 `ON_SCOPE_EXIT` 恢复 default namespace 到进入测试前的原值。 |
| 期望行为 | guard 作用域内 `ScriptEngine->GetDefaultNamespace()` 精确等于 `Automation.BindEnum`，离开后恢复为进入测试前的 namespace；`EAutomationBindEnum` 只能在 `Automation.BindEnum` 下被找到，全局 namespace 查询应为空；enum value 总数应为 `2` 而不是 `3`，且 `Ready == 1`、`Done == 2`。如果再重复构造同名 `FEnumBind`，应复用现有 type 而不是生成第二份 enum。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + 轻量 enum-inspection helper + `ON_SCOPE_EXIT` |

#### NewTest-82：给 `ReferenceClass` / `ValueClass` / `ExistingClass` 补齐 size、alignment 与复用合同

| 字段 | 内容 |
|------|------|
| 优先级 | P1 |
| 缺口类型 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` |
| 关联函数 | `FAngelscriptBinds::ReferenceClass(FBindString, UClass*)` / `ExistingClass(FBindString)` / `ValueClass(FBindString, FBindFlags, int32)` / `GetTypeInfo()` |
| 现有测试覆盖 | 全仓库当前只有 `Interface/AngelscriptInterfaceNativeTests.cpp` 在测试夹具里间接调用过 `ReferenceClass(...)`，但没有任何断言验证注册后的 `asITypeInfo` size、alignment、flags 或重复注册复用语义；`ValueClass(...)` / `ExistingClass(...)` 在目标测试目录和 `AngelscriptRuntime/Tests` 中完全无直接命中。 |
| 风险评估 | 这组 API 是所有手写 native/value bind 的基础入口。若 `ReferenceClass` 没把 `GetStructureSize()` / `GetMinAlignment()` 传进 script type，或 `ValueClass` 在重复注册时 size/alignment 失配、`ExistingClass` 拿到空 type，后续脚本对象布局与 native marshalling 会直接失真，严重时表现为错误 field offset、ABI 崩溃或“类型存在但不可安全调用”。 |
| 建议测试名 | `Angelscript.TestModule.Engine.Binds.ReferenceAndValueClassPreserveLayoutAndReuseExistingTypeInfo` |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindsRegistrationTests.cpp` |
| 场景描述 | 在 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 下先定义一个测试名，例如 `AutomationBindRefActor` 和 `AutomationBindValuePoint`。第一段调用 `FAngelscriptBinds::ReferenceClass(TEXT("AutomationBindRefActor"), AActor::StaticClass())`，拿到 `asITypeInfo*` 后读取 `GetSize()` 与底层 `alignment`。第二段用 `FBindFlags Flags; Flags.Alignment = alignof(FIntPoint);` 调 `FAngelscriptBinds::ValueClass(TEXT("AutomationBindValuePoint"), TBaseStructure<FIntPoint>::Get(), Flags)`；第三段再调用 `ExistingClass(TEXT("AutomationBindValuePoint"))` 和第二次同名 `ValueClass(...)`，确认拿到的是同一个 type id / type info。必要时对 `FBindFlags.bPOD` 再加一个 control case，检查 value type traits 没被丢掉。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；`asIScriptEngine* ScriptEngine`；`TBaseStructure<FIntPoint>::Get()`；一个 access helper 读取 `asCObjectType` 上的 `alignment` 或通过公开 API 间接校验；必要时 `ON_SCOPE_EXIT` 丢弃测试 module/namespace 相关临时状态。 |
| 期望行为 | `ReferenceClass(...)` 返回的 type info 非空，`GetSize()` 等于 `AActor::StaticClass()->GetStructureSize()`，alignment 等于 `AActor::StaticClass()->GetMinAlignment()`；`ValueClass(...)` 返回的 type info 非空，`GetSize()` 等于 `FIntPoint` 实际 size，alignment 等于 `alignof(FIntPoint)`；后续 `ExistingClass(...)` 与第二次同名 `ValueClass(...)` 必须返回同一个 type id/同一底层 type，而不是重新注册出重复类型。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `TBaseStructure<FIntPoint>::Get()` + 轻量 type-info inspection helper |

---

## 测试审查 (2026-04-09 13:10:31)

### 一、现有测试问题

#### Issue-84：`AngelscriptFileSystemTests.cpp` 已超 500 行且混装多个主题，违背单文件单职责规范

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.ModuleLookupByFilename` / `CompileFromDisk` / `PartialFailurePreservesGoodModules` / `Discovery` / `SkipRules` / `RenameUpdatesModuleLookup` / `PathNormalizationLookup` / `MixedSuccessFailureRecoveryAndRemap` |
| 行号范围 | 1-547 |
| 问题描述 | 该文件当前约 550 行，已经超过规则要求的单文件 300-500 行上限，而且同时承载了至少 4 类不同职责：1. module lookup/path normalization；2. disk compile 与 mixed failure recovery；3. script root discovery/skip rules；4. rename/remap 行为。前面多轮已经在同一文件里记录出 helper 误用、cleanup 缺失、断言粒度不足等多种问题，说明这个“大杂烩”文件已经让不同主题的 fixture 和清理策略相互缠绕。继续往这一个文件里补 case，只会进一步放大串测和维护成本。 |
| 影响 | 文件系统回归定位会越来越依赖人工分辨“这次坏的是 lookup、discovery 还是 recovery”，并且后续补充你要求的新增场景时很容易再次把不相干的测试塞进同一文件，持续违反项目自己的测试组织规范。对于需要严格控制 helper 和 cleanup 的 RuntimeCore，这种结构会直接增加误用概率。 |
| 修复建议 | 按主题拆分文件并把每个文件控制在 300-500 行：建议至少拆成 `AngelscriptFileSystemLookupTests.cpp`（lookup/path normalization/rename identity）、`AngelscriptFileSystemDiscoveryTests.cpp`（root discovery/skip rules）、`AngelscriptFileSystemRecoveryTests.cpp`（compile-from-disk/partial failure/mixed recovery）。共享的磁盘 helper 和目录清理逻辑提炼到 `Shared/`，让每个文件只保留一种 fixture 语义。 |

---

## 测试审查 (2026-04-09 12:55:22)

### 一、现有测试问题

#### Issue-83：`LastFullDestroyClearsTypeState` 把 “type state” 收缩成 `GetTypes().Num()`，遗漏 alias/type-finder/current-engine 级 teardown

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.LastFullDestroyClearsTypeState` |
| 行号范围 | 156-189 |
| 问题描述 | 用例在 full-engine epoch 内唯一的正向断言是 `FAngelscriptType::GetTypes().Num() > 0`，在 `FullEngine.Reset()` 后唯一的负向断言是 `FAngelscriptType::GetTypes().Num() == 0`。它没有验证 `FAngelscriptType` 的 alias/type-finder 状态是否一起回到基线，也没有检查 `TryGetCurrentEngine()` 或 current-engine 解析是否随着最后一个 full owner 销毁而清空。对于一条名字就叫 “ClearsTypeState” 的核心生命周期用例，这实际只保护了一个聚合计数器。 |
| 影响 | 只要裸 `Types` 容器数量归零，这条用例就会给绿灯；即便 alias 查找、property type-finder、current-engine 解析或其它与 type database 相邻的 teardown 合同仍有残留，RuntimeCore 生命周期回归也不会被它发现。 |
| 修复建议 | 保留当前 `GetTypes().Num()` 断言作为最外层 smoke，但应补一组更精确的 teardown 合同：在 epoch 内注册一个轻量 alias/type-finder 或显式解析一个 property type，确认状态真的建立；`FullEngine.Reset()` 后再断言 `GetByAngelscriptTypeName(...)` / `GetByProperty(...)` 返回空或 invalid，同时 `FAngelscriptEngine::TryGetCurrentEngine() == nullptr`。如果不想把测试做得过宽，建议把更细的 type-database teardown 检查拆到专门的 `FAngelscriptType` 生命周期测试文件。 |

### 二、需要新增的测试

#### NewTest-80：覆盖 `SaveBindModules` / `LoadBindModules` 的 round-trip、顺序保持与 missing-file 清空合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptBinds::SaveBindModules(FString)` / `LoadBindModules(FString)` / `GetBindModuleNames()` |
| 现有测试覆盖 | 完全无测试；当前 `Plugins/Angelscript/Source/AngelscriptTest/` 下检索 `BindModules.Cache` / `SaveBindModules` / `LoadBindModules` 没有任何命中，现有 `BindConfig` / `GeneratedFunctionTable` 只覆盖 bind entry 和 bind state，不覆盖 bind module cache 持久化。 |
| 风险评估 | `AngelscriptEngine.cpp` 在启动时会先 `LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache")`，然后直接按 `GetBindModuleNames()` 的结果 `LoadModule(...)`。如果 cache round-trip 丢失顺序、残留旧模块名，或 missing-file 路径没有把旧列表清空，runtime 就可能在启动阶段加载错误/过期的 bind module，而现有自动化没有任何定点报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.BindModuleCache.RoundTripsOrderAndClearsOnMissingFile` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindModuleCacheTests.cpp` |
| 场景描述 | 在纯 native 单元测试里直接操作 `FAngelscriptBinds::GetBindModuleNames()`。第一段先 `ResetBindState()`，把列表设为 `ASRuntimeBind_Alpha`、`ASEditorBind_Beta`、`ASRuntimeBind_Gamma` 三个有序模块名，调用 `SaveBindModules(...)` 写到 `Saved/Automation/BindModulesCache/BindModules.Cache`。第二段再次 `ResetBindState()`，调用 `LoadBindModules(...)` 读回同一路径并检查顺序与内容。第三段故意先把内存列表填成脏数据，再对一个不存在的 cache 路径执行 `LoadBindModules(...)`，定义并验证“missing-file 时必须清空列表而不是沿用旧值”的安全合同。 |
| 输入/前置 | `FAngelscriptBinds::ResetBindState()`；`TArray<FString>& BindModuleNames = FAngelscriptBinds::GetBindModuleNames()`；临时目录 `Saved/Automation/BindModulesCache/`；`ON_SCOPE_EXIT` 删除临时 cache 文件与目录并再次 `ResetBindState()` |
| 期望行为 | round-trip case 下，读回后的 `BindModuleNames` 数量必须为 `3`，顺序严格保持 `ASRuntimeBind_Alpha`、`ASEditorBind_Beta`、`ASRuntimeBind_Gamma`，且不应被去重、排序或截断；missing-file case 下，`BindModuleNames.Num()` 必须变为 `0`，从而保证 runtime 启动不会沿用上一轮内存中的脏 bind module 列表。 |
| 使用的 Helper | 其他（纯 native `FAngelscriptBinds` 状态读写 + 临时文件 cleanup helper，无需 `FAngelscriptEngine`） |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-83 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:39:58)

### 二、需要新增的测试

#### NewTest-70：覆盖 `FAngelscriptRuntimeModule` 各类 delegate accessor 的“稳定单例引用”合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` / `AngelscriptRuntimeModule.h` |
| 关联函数 | `GetDynamicSpawnLevel()` / `GetDebugCheckBreakOptions()` / `GetDebugBreakFilters()` / `GetDebugObjectSuffix()` / `GetComponentCreated()` / `GetPreCompile()` / `GetPostCompile()` / `GetOnInitialCompileFinished()` / `GetClassAnalyze()` / `GetPreGenerateClasses()` / `GetPostCompileClassCollection()` / `GetOnLiteralAssetCreated()` / `GetPostLiteralAssetSetup()` / `GetDebugListAssets()` / `GetEditorCreateBlueprint()` / `GetEditorGetCreateBlueprintDefaultAssetPath()` |
| 现有测试覆盖 | 当前文档对 `AngelscriptRuntimeModule.cpp` 的建议只覆盖 startup/init/tick/shutdown 生命周期；目标测试目录和已记录建议都没有任何一条直接验证这些 accessor 是否始终返回同一个 delegate 实例，也没有检查通过一次 getter 绑定的 listener 能否通过下一次 getter 正常触发。 |
| 风险评估 | 这组 accessor 是 runtime、editor 和 compile pipeline 对外暴露的全局钩子入口。若后续 copy-paste 回归让某个 getter 返回了错误 delegate、临时对象或不同静态实例，订阅方会表现成“绑定成功但永远收不到广播”或“绑定到了错误事件”，而当前自动化只会在更高层功能上看到迟到故障，无法快速定位到 module accessor 本身。 |
| 建议测试名 | `Angelscript.TestModule.Engine.RuntimeModule.DelegateAccessorsReturnStableSharedInstances` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp` |
| 场景描述 | 在一个纯 native 测试里，按 delegate 形态分三组做代表性验证。第一组针对单播 ret-val delegate：对 `GetDynamicSpawnLevel()` 和 `GetEditorGetCreateBlueprintDefaultAssetPath()` 先通过第一次 getter 绑定 lambda 返回哨兵值，再通过第二次 getter 执行，验证返回值一致。第二组针对单播/多播 void delegate：分别对 `GetComponentCreated()`、`GetPreCompile()`、`GetPostCompile()`、`GetOnInitialCompileFinished()`、`GetEditorCreateBlueprint()` 绑定计数器，再通过重新获取的 accessor 广播/执行一次，检查计数器递增。第三组针对带参数输出的 delegate：对 `GetDebugBreakFilters()`、`GetDebugObjectSuffix()`、`GetDebugCheckBreakOptions()`、`GetClassAnalyze()`、`GetDebugListAssets()` 绑定会改写输出参数或返回布尔值的 lambda，并通过重新获取的 accessor 调用验证输出被同一 listener 修改。最后补一条地址级合同：同一 getter 前后两次取地址 `&FAngelscriptRuntimeModule::GetPreCompile()` 等应保持相等。 |
| 输入/前置 | 无需 engine；少量本地 recorder struct；对需要参数的 delegate 可传 `nullptr`、空数组或 dummy `FString` / `TArray<FString>` / `bool`；必要时准备一个可为空的 `UObject*` / `UActorComponent*` 占位；`ON_SCOPE_EXIT` 统一 `Unbind()` / `Clear()`，避免污染后续测试。 |
| 期望行为 | 每个 accessor 都必须表现成稳定单例：通过第一次 getter 绑定的 listener，必须能被第二次 getter 的 `Execute` / `Broadcast` 命中；返回值和输出参数与绑定 lambda 的哨兵逻辑一致；同一 getter 的地址前后相等；清理后再次查询不应残留旧 listener。这样才能证明外部系统拿到的确实是同一条全局事件通道，而不是错误或分裂的 delegate 实例。 |
| 使用的 Helper | 纯 native recorder helper + `ON_SCOPE_EXIT`；可选一个小型模板 helper 统一做“bind once, reacquire, execute/broadcast, assert hit count”的重复模式 |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:22:20)

### 记录校正

本轮有效新增发现为：
- `Issue-101`
- `Issue-102`
- `NewTest-102`

上述 3 条完整正文已写入本文前段（约 586-631 行），但上一次续写命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重。

对 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的本轮文件名回扫中，除已记录的 `Issue-101` / `Issue-102` / `NewTest-102` 外，未发现比既有 backlog 更高优先级、且尚未记录的 Core 关键无测文件；当前仍未进入 backlog 的 `.cpp` 只剩 `AngelscriptThirdPartyLib.cpp`，其内容是注释掉的旧 third-party 聚合入口，不作为本轮补测目标。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-101 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:35:41)

### 记录校正

本轮有效新增发现为：
- `Issue-103`
- `Issue-104`

上述 2 条完整正文已误写入本文前段（约 659-682 行），此前两次续写都命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的校正锚点，供后续轮次从文末继续追加。

对 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的本轮快速回扫未发现比既有 backlog 更高优先级、且尚未记录的关键无测文件；当前文件级空白结论不变，仍只有 `AngelscriptThirdPartyLib.cpp` 属于注释掉的旧 third-party 聚合入口，不作为补测目标。因此本轮没有新增 `NewTest-*`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-103 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:35:41)

### 记录校正

本轮有效新增发现为：
- `Issue-103`
- `Issue-104`

上述 2 条完整正文已误写入本文前段（约 659-682 行），此前两次续写都命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的校正锚点，供后续轮次从文末继续追加。

对 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的本轮快速回扫未发现比既有 backlog 更高优先级、且尚未记录的关键无测文件；当前文件级空白结论不变，仍只有 `AngelscriptThirdPartyLib.cpp` 属于注释掉的旧 third-party 聚合入口，不作为补测目标。因此本轮没有新增 `NewTest-*`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-103 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:34:58)

### 记录校正

本轮有效新增发现为：
- `Issue-103`
- `Issue-104`

上述 2 条完整正文已误写入本文前段（约 619-646 行），本次续写补齐真正文件尾部的校正锚点，供后续轮次继续从文末追加。

对 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的本轮快速回扫未发现比既有 backlog 更高优先级、且尚未记录的关键无测文件；当前文件级空白结论不变，仍只有 `AngelscriptThirdPartyLib.cpp` 属于注释掉的旧 third-party 聚合入口，不作为补测目标。因此本轮没有新增 `NewTest-*`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-103 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:32:25)

### 一、现有测试问题

#### Issue-103：`AddFunctionEntryPreservesFirstRegistration` 的去重核心断言全部停留在旁路 `TestTrue` / `TestFalse`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.AddFunctionEntryPreservesFirstRegistration` |
| 行号范围 | 582-585 |
| 问题描述 | 这条用例真正要保护的是“第二次 `AddFunctionEntry(...)` 不得覆盖第一次注册的 direct entry”。但 582-584 行对 `IsFunctionEntryBound(*StoredEntry)`、`AreFunctionEntriesEqual(*StoredEntry, FirstEntry)`、`!AreFunctionEntriesEqual(*StoredEntry, SecondEntry)` 的 3 个核心去重断言全部只是旁路 `TestTrue` / `TestFalse`，随后 585 行直接 `return true;`。代码结构上，最终显式通过条件并没有把“仍然保留 first registration、且没有被 `ERASE_NO_FUNCTION()` 覆盖”纳入硬门槛。 |
| 影响 | `AddFunctionEntryPreservesFirstRegistration` 是当前 `BindConfig` 文件里唯一直接命中 `FAngelscriptBinds::AddFunctionEntry(...)` 去重合同的用例。若后续回归让 duplicate registration 覆盖掉 direct bind，或者把 `Caller` / `FuncPtr` 其中一半替换成第二份 entry，当前测试在结构上仍然表现成“前置存在性检查通过后就返回成功”，削弱了 dedup 合同的可读性和防回归强度。 |
| 修复建议 | 把 3 个核心去重断言收进显式返回路径，例如先保存 `bStillBound`、`bMatchesFirstEntry`、`bDoesNotMatchSecondEntry`，最后 `return bStillBound && bMatchesFirstEntry && bDoesNotMatchSecondEntry;`；或者任一断言失败时直接 `return false`。若顺手增强覆盖，可再补一条 map-size/control 断言，证明相同 `Class + Name` 的重复注册没有额外生成第二个可见 entry。 |

#### Issue-104：`SummaryOutput` 的顶层 rate 校验仍是尾部旁路断言，summary 百分比漂移缺少显式硬闸

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.SummaryOutput` |
| 行号范围 | 523-527 |
| 问题描述 | 这条用例前半段会严密读取 `totalGeneratedEntries`、`totalDirectBindEntries`、`totalStubEntries`，也会显式验证 `TotalGeneratedEntries == TotalDirectBindEntries + TotalStubEntries`。但最接近 summary 百分比合同的 3 条断言 `directBindRate` 对齐、`stubRate` 对齐、`directBindRate + stubRate == 1.0` 都写成 525-527 行的尾部 `TestTrue(...)`，随后测试继续执行 per-module 逻辑并最终 `return true;`。从代码结构看，顶层 rate 字段并没有被纳入显式通过条件。 |
| 影响 | `SummaryOutput` 已经承担了生成报表数值合同的主要入口。如果后续 JSON 里 entry 计数仍然正确，但 `directBindRate` / `stubRate` 写错、归一化失衡或精度处理退化，当前测试会留下日志级失败记录，却没有把“summary 百分比字段必须正确”表达成清晰的最终门槛；在这个文件已经混杂多类工件检查的前提下，这会继续稀释 rate 回归的可读性。 |
| 修复建议 | 把 3 条顶层 rate 断言并入显式返回路径，例如保存 `bDirectRateMatches`、`bStubRateMatches`、`bRatesNormalized`，并在进入 per-module 校验前先 `if (!(...)) return false;`；或者把顶层 rate 校验抽成小 helper 返回布尔值。这样可让 `SummaryOutput` 明确把 summary 百分比视为第一层正式合同，而不是附带检查。 |

---

## 测试审查 (2026-04-10 02:22:20)

### 一、现有测试问题

#### Issue-101：`FullDestroyAllowsAnnotatedRecreate` 两个 epoch 用了不同的类名和模块名，根本没有覆盖“同名重建”这条真正危险的 recreate 路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.FullDestroyAllowsAnnotatedRecreate` |
| 行号范围 | 298-351 |
| 问题描述 | 第一轮编译的是 `RecreateAnnotatedActorA` / `ARecreateAnnotatedActorA`，第二轮却改成了 `RecreateAnnotatedActorB` / `ARecreateAnnotatedActorB`。这样一来，用例只证明“full destroy 后还能再编译一个新名字的 annotated class”，并没有真正触发最容易出问题的同名重建路径。即使旧 epoch 的 `UClass` / `UPackage` / type registry 还残留着原始名字，只要第二轮换个名字，当前测试仍会稳定通过。 |
| 影响 | `FullDestroyAllowsAnnotatedRecreate` 最应该保护的是“同一脚本类在 full destroy 后能否干净地重新生成”。如果 teardown 残留导致同名 `UClass` 冲突、旧 package 复用或旧反射布局被误拿，当前测试不会报警，因为它完全绕开了 name-collision 这条路径。 |
| 修复建议 | 把两轮统一成同一个 module/class/filename，例如都用 `RecreateAnnotatedActor` / `ARecreateAnnotatedActor`，并让第二轮脚本把默认值从 `11` 改成 `22` 之类的哨兵值。第一轮保存旧 `UClass`/outer package 的弱引用，`DiscardModule + CollectGarbage + Engine.Reset()` 后断言旧对象失效；第二轮再用同名脚本重编译，并验证拿到的新类不是旧指针、CDO 属性值命中第二轮哨兵，才能真正锁住同名 recreate 合同。 |

#### Issue-102：`GC.ActorDestroy` / `GC.ComponentDestroy` 只看 GC 之后的最终失效状态，没有证明“是 GC 完成了回收”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.GC.ActorDestroy` / `Angelscript.TestModule.GC.ComponentDestroy` |
| 行号范围 | 110-115, 158-163 |
| 问题描述 | 两条用例在 `Destroy()` / `DestroyComponent()` 和一次 `TickWorld(...)` 之后，立刻调用 `CollectGarbage(...)`，最后只断言弱指针失效。它们没有在 GC 之前记录对象是否仍处于“待 GC 回收”的过渡状态，因此无法区分两种情况：一是 GC 真正完成了最后回收；二是对象其实在 destroy/tick 阶段就已经完全失效，GC 只是路过。当前断言只观察终态，看不出 GC 这一步是否真的参与了行为。 |
| 影响 | 这会把 `GC.ActorDestroy` / `GC.ComponentDestroy` 退化成普通 UE destroy smoke。哪怕 Angelscript 侧的 GC 集成、引用断链或 script-object 回收时序完全没有起作用，只要 UE 自身销毁路径最终让弱指针失效，这两条测试仍会绿灯，无法回答“是否真正触发了 GC 并验证回收”这个审查重点。 |
| 修复建议 | 把断言拆成两阶段。先在 `Destroy()` / `DestroyComponent()` + `TickWorld(...)` 之后、`CollectGarbage(...)` 之前补一条过渡态断言，明确对象尚未被最终回收，或至少处于预期的 pending-kill/待清理状态；再执行 `CollectGarbage(...)` 并断言从“过渡态”转成无效。若当前引擎版本会在 destroy 阶段直接让弱指针失效，就不要继续依赖 plain destroy 路径，而应像 `Issue-03` 的修复方向那样加入 script 持有引用，做出“GC 前存活、清引用后二次 GC 才回收”的 staged contract。 |

### 二、需要新增的测试

#### NewTest-102：补一条“同名 annotated class full-destroy 后可重建”的真实 recreate 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::Shutdown()` / full-owner teardown 路径 |
| 现有测试覆盖 | `AngelscriptEngineCoreTests.cpp` 里的 `FullDestroyAllowsAnnotatedRecreate` 目前只覆盖“第二轮换个新名字还能编译”，没有任何一条用例真正复用同一个 annotated module/class 名称来验证 full destroy 之后的同名重建 |
| 风险评估 | 如果旧 epoch 的 generated `UClass`、outer package、type metadata 或 filename/module 映射没有被彻底清掉，最容易出错的就是“重新生成同名类”这条路径：可能命中旧类、类名冲突、反射布局错位，或者默默复用旧 CDO，而当前自动化完全不会定点报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.FullDestroyAllowsAnnotatedSameNameRecreate` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 场景描述 | 复用当前 `EngineCore` 的 full-engine fixture，但两轮都使用同一个 module 名、filename 和类名，例如 `RecreateAnnotatedActor` / `RecreateAnnotatedActor.as` / `ARecreateAnnotatedActor`。第一轮脚本把 `Value` 默认值设为 `11`，成功编译后保存 generated class 与 outer package 的弱引用；随后 `DiscardModule(...)`、`CollectGarbage(...)`、`Engine.Reset()`，断言旧类与旧 package 都失效。第二轮在新的 full engine 上用完全相同的名字重编译，但把 `Value` 默认值改成 `22`，再解析 generated class 和 CDO。 |
| 输入/前置 | `FCoreTestContextStackGuard`；`AngelscriptTestSupport::CreateFullTestEngine()`；`AngelscriptTestSupport::CompileAnnotatedModuleFromMemory(...)`；`AngelscriptTestSupport::FindGeneratedClass(...)`；`CollectGarbage(RF_NoFlags, true)`；读取 CDO `Value` 的轻量 property helper；`ON_SCOPE_EXIT` 统一销毁 shared/global engine 并恢复基线。 |
| 期望行为 | 第一轮 generated class 必须存在且默认值为 `11`；`DiscardModule + CollectGarbage + Engine.Reset()` 后，旧 `UClass` 与 outer package 的弱引用必须失效，且 `FindObject<UClass>(nullptr, TEXT(\"/Script/...ARecreateAnnotatedActor\"))` 或等价查找不能再命中旧类；第二轮同名重编译必须成功，解析到的新 `UClass*` 不能等于第一轮指针，CDO 默认值必须变成 `22`，证明真正生成了新 epoch 的同名类，而不是沿用了旧对象。 |
| 使用的 Helper | `FCoreTestContextStackGuard` + `CreateFullTestEngine()` + `CompileAnnotatedModuleFromMemory(...)` + `FindGeneratedClass(...)` + `CollectGarbage(...)` + property-read helper |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-101 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 01:52:05)

### 一、现有测试问题

#### Issue-98：`EngineParityTests` 自定义 production-engine helper 只认 `TryGetCurrentEngine()`，会在缺少 ambient context 时误判无引擎

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.*` |
| 行号范围 | 19-27 |
| 问题描述 | 文件顶部自定义的 `GetProductionEngineForParity()` 只通过 `FAngelscriptEngine::IsInitialized()` / `FAngelscriptEngine::Get()` 取引擎。RuntimeCore 里这条路径最终只看 `FAngelscriptEngineContextStack::Peek()` 和 `UAngelscriptGameInstanceSubsystem::GetCurrent()`，如果当前没有 ambient world context，就直接返回 `nullptr`（`AngelscriptEngine.cpp:718-733`）。而共享测试 helper `TryGetRunningProductionSubsystem()` / `TryGetRunningProductionEngine()` 已经提供了更强的 fallback，会继续扫描 `GEngine->GetWorldContexts()` 找到 live `UGameInstanceSubsystem`。结果是：只要 editor 里确实有 production subsystem/engine，但 ambient current-engine 没有建立，这整组 parity 用例就会误报“production engine should already be initialized”。 |
| 影响 | 这让 `Parity` 文件对环境状态过度敏感：同样存在的 runtime engine，在“ambient context 已建立”的批次能通过，在“只有 live world/subsystem、但没有 current-engine scope”的批次会整组假失败；更糟的是，它还会把错误定位成“引擎未初始化”，掩盖真实的解析层级问题。 |
| 修复建议 | 删除本地 `GetProductionEngineForParity()`，统一改用 `AngelscriptTestSupport::AcquireProductionLikeEngine(...)` 或至少 `TryGetRunningProductionEngine()`。如果这些 parity 用例仍必须观察 live production engine，也应在 helper 内显式建立 `FAngelscriptEngineScope`，并把“找不到 engine”与“当前没有 ambient context”分开报错，避免继续把 resolver 层级问题折叠成初始化失败。 |

#### Issue-99：`DeprecationsMetadata` 绑定 parity 直接依赖 Niagara 原生符号，外部模块裁剪时会产生假失败

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.DeprecationsMetadata` |
| 行号范围 | 629-641 |
| 问题描述 | 用例把被测对象硬编码成 `FindObject<UFunction>(nullptr, TEXT("/Script/Niagara.NiagaraComponent:SetNiagaraVariableLinearColor"))`。这意味着测试是否能开始，首先取决于 Niagara 模块和对应 `UFunction` 是否在当前 editor/automation 配置里可用，而不是取决于 Angelscript 绑定层是否正确转发了弃用 metadata。只要运行环境关闭了 Niagara、裁掉相关模块，或者该 API 在引擎版本中移动/更名，测试就会在 `TestNotNull` 处直接失败。 |
| 影响 | `DeprecationsMetadata` 本应保护 RuntimeCore 的弃用元数据透传合同，但现在先被外部插件可用性绑死。结果是：构建裁剪、模块禁用或引擎版本波动会把这条 parity 测试变成环境失败，既降低稳定性，也让真正的绑定层回归信号被第三方依赖噪声掩盖。 |
| 修复建议 | 不要再依赖 Niagara 真实 API 作为唯一样本。把弃用元数据 fixture 收口到仓内可控对象，例如在 `AngelscriptUhtCoverageTestLibrary` 或单独的 test-only `UBlueprintFunctionLibrary` 上定义一条带 `DeprecatedFunction`/`DeprecationMessage` 的 `UFUNCTION`，然后在 parity 测试里读取该本地符号并继续验证脚本侧弃用提示。若仍想保留 Niagara 作为 integration sample，也应先检测模块可用性并在不可用时 `AddWarning`/跳过，而不是直接把环境差异记成失败。 |

### 二、需要新增的测试

本轮未新增测试建议。对 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的二次回扫后，GAS、`GameInstanceSubsystem`、`RuntimeModule`、`BindDatabase`、`Settings`、`Docs`、`Commandlet` 等高优先级空白点在前文都已有更直接的测试提案，本轮没有发现新的更高优先级未记录项。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 1 | Issue-99 |
| WrongHelper | 1 | Issue-98 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:30:03)

### 记录校正

本轮有效新增发现为：
- `NewTest-77`
- `NewTest-78`

上述 2 条完整正文已写入本文前段，但本次追加再次命中了前文重复汇总锚点，没有落在真实文末；此处补齐真正的文末续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:44:25)

### 记录校正

本轮有效新增发现为：
- `Issue-81`
- `Issue-82`
- `NewTest-79`

上述 3 条完整正文已写入本文前段，但本次追加再次命中了前文重复汇总锚点，没有落在真实文末；此处补齐真正的文末续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 1 | Issue-81 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 1 | Issue-82 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:57:51)

### 记录校正

本轮有效新增发现为：
- `Issue-83`
- `NewTest-80`

上述 2 条完整正文已写入本文前段，但上一轮追加再次命中了前文汇总表，未落在真实文末；此处补齐真正的文末续写标记，供后续轮次从真实文件尾继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-83 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 13:17:32)

### 记录校正

本轮有效新增发现为：
- `NewTest-81`
- `NewTest-82`

上述 2 条完整正文已写入本文前段，但本次续写再次命中了前文汇总锚点，没有落在真实文末；此处补齐真正的文末续写标记，供后续轮次从真实文件尾继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:39:18)

### 一、现有测试问题

#### Issue-81：`ObserveStartupBindPass` 会销毁进入前的 global/shared engine，却不恢复原有基线

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.StartupBindInfoPreservesOrder` / `Angelscript.TestModule.Engine.BindConfig.StartupPathMergesDisabledBindNames` |
| 行号范围 | 125-146, 420-490 |
| 问题描述 | 两条 startup-path 用例都依赖 `ObserveStartupBindPass(...)`。这个 helper 在 127-131 行和 141-144 行会无条件执行 `DestroySharedTestEngine()` 与 `DestroyGlobalEngine()`，先把当前进程里可能已经存在的 shared/global engine 清掉，再创建临时 full engine 取 snapshot，最后只做“再次销毁”，没有任何进入前状态快照或恢复。结果是：如果测试进入前已经存在 production-like engine、runtime-module owned engine，或其他测试建立的 shared engine，这两条用例会把它们直接抹掉，并把“无 engine”的环境留给后续测试。 |
| 影响 | 这是实打实的串测污染，而不是单纯的 helper 局部副作用。后续依赖 `FAngelscriptEngine::IsInitialized()`、`FAngelscriptEngine::Get()`、production parity、runtime module initialize override 或 shared engine 基线的测试，都会因为这两条 bind startup 用例提前销毁了原有 engine 而出现顺序相关失败。 |
| 修复建议 | 不要在 helper 里直接破坏进程级 engine 基线。应新增 scoped snapshot/restore helper：进入前记录 shared/global engine 与 `FAngelscriptEngineContextStack` 状态，必要时在独立 isolated engine 上观测 startup bind pass，退出时完整恢复原有 engine；如果无法恢复真实 engine 实例，就把这两条测试迁到专用隔离进程/批次，避免和依赖 production/shared engine 的文件混跑。至少也要在 `ObserveStartupBindPass(...)` 注释和实现里显式声明“会销毁全局 engine”，并在 `RunTest()` 末尾重建所需基线，而不是静默留空。 |

#### Issue-82：`CleanFileSystemTestRoot` 忽略目录删除结果，固定根目录一旦清理失败就会把旧脚本带进后续用例

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.*` |
| 行号范围 | 16-30, 81-547 |
| 问题描述 | 所有 `FileSystem` 用例都依赖 `CleanFileSystemTestRoot()` 清空 `Saved/Automation/FileSystem` 和 legacy `Script/Automation/FileSystem`。但 helper 在 26-29 行直接调用 `IFileManager::Get().DeleteDirectory(...)` 后丢弃返回值，既不验证删除是否成功，也不在失败时中止当前测试。由于整文件共用固定目录而不是每用例独立 GUID 子目录，只要某次删除因为文件句柄、权限、索引器或前序失败残留而没清干净，后续 discovery / compile / remap 用例就会在陈旧脚本上继续运行。 |
| 影响 | 这会把文件系统测试变成明显的环境依赖：`Discovery` 可能多扫到旧 `.as` 文件，`CompileFromDisk`/`ModuleLookupByFilename` 可能误用上一轮残留内容，`MixedSuccessFailureRecoveryAndRemap` 也可能在“旧文件仍在”的前提下得到假绿灯或假失败。因为 helper 本身不报错，定位时只会看到后面的断言数量飘忽。 |
| 修复建议 | 把 `CleanFileSystemTestRoot()` 改成返回 `bool` 或结构化结果，并在每条测试开头/结尾用 `TestTrue` 明确断言两个目录都已成功删除；更稳妥的做法是为每个用例生成独立 `FGuid` 子目录，避免跨用例共享根路径。若必须保留共享根，还应在清理失败时打印未删掉的路径列表并立即 `return false`，不要带着脏目录继续执行主体断言。 |

### 二、需要新增的测试

#### NewTest-79：给 AngelScript 头文件封装层补一条独立的原生 C API 回归，保护 `AngelscriptInclude.h` 与 warning shim

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptInclude.h` / `StartAngelscriptHeaders.h` / `EndAngelscriptHeaders.h` |
| 关联函数 | `asGetLibraryVersion()` / `asCreateScriptEngine(asDWORD)` / `asIScriptModule::CompileFunction(...)` / `asIScriptContext::Prepare(...)` / `Execute()` |
| 现有测试覆盖 | 完全无测试；当前 RuntimeCore 的所有 automation 都默认这组 header shim 与链接边界已经可用，但文档里没有任何一条建议或现有测试直接包含 `AngelscriptInclude.h` 并通过这层封装调用原生 AngelScript C API。 |
| 风险评估 | 一旦 warning push/pop shim、头文件包含顺序、`ANGELSCRIPT_VERSION` 对齐或第三方库链接边界回归，现有高层 runtime 测试往往会在更后面以“engine 创建失败”“脚本上下文为空”之类的间接症状暴露，定位成本很高；而这组三方封装本身目前没有任何定点报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.HeaderShim.RawAngelscriptApiRoundTrip` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptHeaderShimTests.cpp` |
| 场景描述 | 在一个纯 native automation test 里只通过 `#include "AngelscriptInclude.h"` 访问 AngelScript C API，不经过 `FAngelscriptEngine`。先读取 `asGetLibraryVersion()`；随后用 `asCreateScriptEngine(ANGELSCRIPT_VERSION)` 创建原生 script engine，直接拿 `asIScriptModule` 编译 `int Entry() { return 5; }`，再创建 `asIScriptContext` 做 `Prepare/Execute`。 |
| 输入/前置 | 无需 UE world 或 `FAngelscriptEngine`；一个本地 RAII helper 负责 `asIScriptFunction` / `asIScriptContext` / `asIScriptEngine` 的 `Release()`；必要时加一个简单 message callback 收集编译错误文本，保证失败时诊断清楚。 |
| 期望行为 | `asGetLibraryVersion()` 返回非空字符串；`asCreateScriptEngine(ANGELSCRIPT_VERSION)` 返回非空 engine；`CompileFunction(...)` 返回 `asSUCCESS` 且产出非空 `asIScriptFunction*`；`Prepare(...)` 返回 `asSUCCESS`，`Execute()` 返回 `asEXECUTION_FINISHED`，最终 `GetReturnDWord() == 5`。这样可以直接证明 `AngelscriptInclude.h` 与 `Start/EndAngelscriptHeaders.h` 组合下的原生 C API 头文件、warning shim 和链接边界仍然完好。 |
| 使用的 Helper | 其他（纯 native AngelScript C API RAII helper + 可选 message callback） |
| 优先级 | P1 |

---

## 测试审查 (2026-04-09 12:28:42)

### 二、需要新增的测试

#### NewTest-77：给 `UAngelscriptAbilityTaskLibrary` 补齐 actor spawn / visualize wrapper 的创建与完成合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `SpawnActor(...)` / `VisualizeTargeting(...)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-71` 只覆盖 `VisualizeTargetingUsingActor(...)`、`WaitTargetData(...)` 和 move-to wrapper；目标测试目录与已记录建议里都还没有任何一条用例直接命中 `SpawnActor(...)` 或 class-based `VisualizeTargeting(...)`。 |
| 风险评估 | 这两组 wrapper 一旦丢失 `TargetData`、`TargetClass`、`OwningAbility` 或 finalize 路径，脚本侧会表现成“task 能创建，但 actor 不按目标数据落点生成 / targeting actor 没被初始化进 ASC / 销毁清理失效”；这是典型运行时回归，而当前自动化没有任何定位锚点。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.ActorSpawnWrappersPreserveTaskStateAndFinalizeActor` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryActorLifecycleTests.cpp` |
| 场景描述 | 建立完整 GAS fixture：`FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` + world + local `APlayerController` + avatar `AAngelscriptGASCharacter`（或等价带 `UAngelscriptAbilitySystemComponent` 的 `ACharacter`）+ native `UGameplayAbility` test double。先构造一个带 `FGameplayAbilityTargetData_SingleTargetHit` 的 `FGameplayAbilityTargetDataHandle`，命中点固定为 `FVector(120.f, 30.f, 50.f)`；调用 `UAngelscriptAbilityTaskLibrary::SpawnActor(Ability, TargetData, AAutomationSpawnProbeActor::StaticClass())`，再通过 `BeginSpawningActor(...)` / `FinishSpawningActor(...)` 驱动完整 spawn 流程。第二段调用 `UAngelscriptAbilityTaskLibrary::VisualizeTargeting(Ability, AAutomationAbilityTargetActor::StaticClass(), TEXT(\"TargetViz\"), 0.05f)`，同样走 `BeginSpawningActor(...)` / `FinishSpawningActor(...)`，最后 `EndTask()` 验证 cleanup。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；一个最小 GAS actor/ability fixture；`AAutomationSpawnProbeActor` 与 `AAutomationAbilityTargetActor` 测试类；`UAutomationSpawnActorTaskAccess` 读取 `CachedTargetDataHandle`；`UAutomationVisualizeTargetingTaskAccess` 读取 `TargetClass` / `TargetActor`；`TWeakObjectPtr<AGameplayAbilityTargetActor>` 用于销毁后校验；`ON_SCOPE_EXIT` 统一销毁 spawned actor / task。 |
| 期望行为 | `SpawnActor(...)` 返回 `UAbilityTask_SpawnActor`，`GetOuter()` / owning ability 等于传入 ability，`CachedTargetDataHandle.Num() == 1`；`BeginSpawningActor(...)` 在 authority/local fixture 下返回 `true` 且生成 `AAutomationSpawnProbeActor`；`FinishSpawningActor(...)` 后 success delegate 只广播一次，spawned actor 的世界位置精确等于 target data 里的 hit location。`VisualizeTargeting(...)` 返回 `UAbilityTask_VisualizeTargeting`，且 access shim 读到 `TargetClass == AAutomationAbilityTargetActor::StaticClass()`；`FinishSpawningActor(...)` 后 spawned target actor 的 `PrimaryPC` 等于当前 `PlayerController`，`ASC->SpawnedTargetActors.Last()` 等于该 actor，且 target actor 已收到 `StartTargeting(Ability)`；`EndTask()` 并 tick world 后，`TWeakObjectPtr` 失效，证明 `OnDestroy()` 确实清掉了 spawned target actor。 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS actor/ability fixture + actor/task access shims + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

#### NewTest-78：给 `UAngelscriptAbilityTaskLibrary` 补齐 ability-state / overlap wrapper 的激活、广播与解绑合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `StartAbilityState(...)` / `WaitForOverlap(...)` |
| 现有测试覆盖 | 当前目标测试目录里没有任何一条用例直接创建 `UAbilityTask_StartAbilityState` 或 `UAbilityTask_WaitOverlap`；`NewTest-20`/`71`/`73` 只覆盖 wait/tag/input/confirm/target-data/move-to 类 wrapper，没有覆盖 state task 或 overlap task。 |
| 风险评估 | `StartAbilityState(...)` 如果把 `StateName` 或 `bEndCurrentState` 透传错，脚本能力的 end/interrupted 分支会悄悄失效；`WaitForOverlap(...)` 如果没正确绑定 avatar primitive、构造 `TargetDataHandle` 或在结束时解绑 `OnComponentHit`，近战/触发类能力会出现“第一次能触发、后续泄漏或完全不触发”的高影响回归。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.StateAndOverlapWrappersDriveDelegatesCorrectly` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryStateOverlapTests.cpp` |
| 场景描述 | 继续复用完整 GAS world fixture。第一段创建 `StartAbilityState(Ability, TEXT(\"Casting\"), false)` task，绑定 `OnStateEnded` / `OnStateInterrupted` 计数器，调用 `Activate()` 后手工广播 `Ability->OnGameplayAbilityStateEnded.Broadcast(TEXT(\"Casting\"))`，再创建第二个 `StartAbilityState(Ability, TEXT(\"Interrupted\"), true)` task 并调用 `ExternalCancel()`。第二段创建 `WaitForOverlap(Ability)` task，绑定 `OnOverlap` 计数器和捕获到的 `FGameplayAbilityTargetDataHandle`，`Activate()` 后让 avatar root `UCapsuleComponent` 对另一个测试 actor 的 primitive 触发一次 `OnComponentHit.Broadcast(...)`，随后再次广播第二次命中验证 task 已经结束且不再重复触发。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；带 `UCapsuleComponent` root 的 avatar actor；native `UGameplayAbility` test double；一个带 primitive component 的 `AAutomationOverlapTargetActor`；必要时用 test-local helper 查询 `OnComponentHit` 是否仍绑定 `UAbilityTask_WaitOverlap::OnHitCallback`；`TWeakObjectPtr` 与 `ON_SCOPE_EXIT` 负责 actor/task 清理。 |
| 期望行为 | `StartAbilityState(...)` 返回 `UAbilityTask_StartAbilityState`，`GetOuter()` / owning ability 等于传入 ability，`GetDebugString()` 精确等于 `Casting (AbilityState)`；广播匹配 `StateName` 后，`OnStateEnded` 只触发一次且 `OnStateInterrupted` 仍为 `0`；`ExternalCancel()` 路径会触发一次 `OnStateInterrupted`，之后再次广播 ability 的 ended/cancelled delegate 不会命中已销毁 task。`WaitForOverlap(...)` 返回 `UAbilityTask_WaitOverlap`，`Activate()` 后 task 进入 waiting-on-avatar 状态，并在 avatar primitive 上挂上 hit callback；第一次 hit 只广播一次 `OnOverlap`，捕获到的 `TargetDataHandle` 恰好有一个 `FGameplayAbilityTargetData_SingleTargetHit`，其 `HitResult.GetActor()` 等于测试 target actor；task 结束后第二次 hit 不再增加广播次数，同时 primitive 上不再残留该 task 的动态绑定。 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS world/ability fixture + overlap target actor + optional delegate-binding helper + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:41:14)

### 记录校正

本轮新增发现为 `NewTest-69` 与 `NewTest-70`。
两条正文已写入本文前段，但上一轮追加时命中了错误锚点，没有落在文末；此处仅补齐文末追加标记，供后续轮次按文末继续去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:38)

### 一、现有测试问题

#### Issue-59：`ExecuteIsolatedBinds` 把 `Angelscript` 日志级别硬切到 `Fatal/Log`，却没有恢复进入前的全局配置

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.GlobalDisabledBindNames` / `EngineDisabledBindNames` / `UnnamedBindBackwardCompatibility` |
| 行号范围 | 118-123, 287-302, 347-362, 414-416 |
| 问题描述 | 这些用例都会经过 `ExecuteIsolatedBinds(...)`。helper 在 120 行把 `Angelscript` category 提到 `Fatal`，调用 `FAngelscriptBinds::CallBinds(...)` 后又在 122 行无条件写回 `Log`。这里既没有保存调用前的 verbosity，也没有用 scoped RAII 恢复；因此测试退出后，`Angelscript` 的全局日志级别不再是进入前的真实值，而是被硬编码成 `Log`。这属于典型的“修改全局设置但未恢复”反模式。 |
| 影响 | 一旦套件其他部分、开发者本地环境或前序测试把 `Angelscript` 日志级别设成 `Warning` / `Verbose` / `NoLogging`，这几条 `BindConfig` 用例跑完后会把全局配置永久改掉，影响后续测试的诊断密度和日志匹配行为；与日志相关的测试会因此出现顺序依赖。 |
| 修复建议 | 不要手写 `UE_SET_LOG_VERBOSITY(Angelscript, Fatal); ...; UE_SET_LOG_VERBOSITY(Angelscript, Log);`。改用 `FLogScopedVerbosityOverride` / `LOG_SCOPE_VERBOSITY_OVERRIDE(Angelscript, ELogVerbosity::Fatal)` 包住 `CallBinds(...)`，或至少先保存 `Angelscript.GetVerbosity()` 再在 `ON_SCOPE_EXIT` 中恢复原值。这样 helper 才不会把全局日志状态泄漏给后续用例。 |

#### Issue-60：`SubsystemScenarioTests` 4 条用例都把日志级别写死恢复为 `Log`，会污染后续测试的全局日志状态

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `WorldSubsystem.Tick` / `WorldSubsystem.ActorAccess` / `GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 77-100, 124-142, 166-190, 214-237 |
| 问题描述 | 四个用例在调用 `CompileModuleWithResult(...)` 前都执行 `UE_SET_LOG_VERBOSITY(Angelscript, Fatal)`，随后直接用 `UE_SET_LOG_VERBOSITY(Angelscript, Log)` 收尾。它们和 `BindConfig` helper 一样，没有保存进入前的 verbosity；即便测试最初运行在 `Warning`、`Verbose` 或自定义 suppress 配置下，退出后也会被强制改成 `Log`。这不是局部噪音控制，而是对全局 category 状态的永久改写。 |
| 影响 | 当前文件本身就是失败路径场景测试，后续 suite 往往还会依赖日志捕获或 suppress 规则。若这些 subsystem 用例先执行，后面的测试将继承被篡改过的 `Angelscript` 日志级别，导致日志基线、错误匹配次数和噪音水平随执行顺序波动。 |
| 修复建议 | 把每个 compile 调用都改成 scoped verbosity override，而不是 `Fatal -> Log` 硬编码恢复。最直接的做法是在用例里引入 `Logging/LogScopedVerbosityOverride.h`，用 `LOG_SCOPE_VERBOSITY_OVERRIDE(Angelscript, ELogVerbosity::Fatal)` 包住 `CompileModuleWithResult(...)`；如果继续手写宏，也必须先读取原始 verbosity，再通过 `ON_SCOPE_EXIT` 恢复，而不是假定基线一定是 `Log`。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 2 | Issue-59 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-10 00:23:56)

### 二、需要新增的测试

#### NewTest-94：给 `CaptureGameplayAttribute(...)` 补齐 `AttributeSetType == nullptr` 的防御性错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameplayEffectUtils.h` |
| 关联函数 | `UAngelscriptGameplayEffectUtils::CaptureGameplayAttribute(UStruct*, FName, EGameplayEffectAttributeCaptureSource, bool)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-07` 只覆盖 valid attribute 与“属性名不存在”路径；没有任何一条建议或现有测试直接命中 `ensureMsgf(AttributeSetType, TEXT("Struct from Angelscript should never be null!"))` 这一条 null-struct guard。 |
| 风险评估 | 这条 helper 是脚本侧 gameplay effect capture 的入口之一。若未来把 null `AttributeSetType` 误放行、错误地产生半有效 `FGameplayEffectAttributeCaptureDefinition`，或直接在防御路径上崩溃，execution calculation 会在运行时以难定位的方式失效，而当前自动化没有任何定点报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.GameplayEffectUtils.CaptureGameplayAttributeRejectsNullStructType` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGameplayEffectUtilsTests.cpp` |
| 场景描述 | 在纯 native 测试里直接调用 `UAngelscriptGameplayEffectUtils::CaptureGameplayAttribute(nullptr, TEXT("Health"), EGameplayEffectAttributeCaptureSource::Source, false)`。使用 `AddExpectedError(...)` 精确捕获一次 `Struct from Angelscript should never be null!` ensure；随后读取返回的 `FGameplayEffectAttributeCaptureDefinition`。可再做一条重复调用 control，确认同一 null 输入不会产生不同结果或污染静态状态。 |
| 输入/前置 | `AddExpectedError(TEXT("Struct from Angelscript should never be null!"), EAutomationExpectedErrorFlags::Contains, 1)`；`FName(TEXT("Health"))`；`EGameplayEffectAttributeCaptureSource::Source`；无需完整 `FAngelscriptEngine`。 |
| 期望行为 | 调用不得崩溃，且必须命中一次预期 ensure；返回的 capture definition 必须保持 default/invalid 状态，例如 `AttributeToCapture.IsValid() == false` 或等价空 property 合同为真；重复调用同一 null 输入时结果保持一致，不得留下任何可被后续 valid case 观察到的脏状态。 |
| 使用的 Helper | `AddExpectedError(...)` + 纯 native assertion helper |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:43:57)

### 记录校正

本轮有效新增发现为：
- `Issue-90`
- `NewTest-95`

上述 2 条完整正文已写入本文前段，但本次续写再次命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-90 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:47:27)

### 记录校正

本轮有效新增发现为：
- `Issue-91`
- `Issue-92`
- `Issue-93`
- `NewTest-96`
- `NewTest-97`
- `NewTest-98`

上述 6 条完整正文已写入本文前段，但本次续写再次命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 3 | Issue-92 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingErrorPath: 1, MissingEdgeCase: 1 |
| P2 | 0 | 无 |
| P3 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 01:16:49)

### 记录校正

本轮有效新增发现为：
- `Issue-94`
- `NewTest-99`
- `Issue-95`

上述 3 条完整正文已写入本文前段（约 1032-1119 行），但两次续写都命中了旧锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-94 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:18:41)

### 记录校正

本轮有效新增发现为：
- `Issue-96`
- `Issue-97`
- `NewTest-100`

上述 3 条完整正文已写入本文前段（约 1067-1128 行），本次续写命中了前文重复的汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-96 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:09:00)

### 一、现有测试问题

#### Issue-96：3 条执行型 parity 用例把返回值断言写成旁路检查，脚本语义回归仍可能留在绿灯内

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.CollisionProfileCompile` / `Angelscript.TestModule.Parity.FIntPointCompile` / `Angelscript.TestModule.Parity.HitResultCompile` |
| 行号范围 | 192-226 / 341-355 / 611-625 |
| 问题描述 | 这 3 条用例都会真正 `Prepare/Execute` 脚本，也分别对语义结果做了关键断言：`CollisionProfileCompile` 断言 `Compare(...) == 0`，`FIntPointCompile` 断言求和值为 `13`，`HitResultCompile` 断言字段回写结果为 `10`。但三条测试最后都统一以 `return PrepareResult == asSUCCESS && ExecuteResult == asEXECUTION_FINISHED;` 结束，没有把返回值断言纳入显式通过条件。结果是：只要脚本还能执行完，哪怕常量映射、运算符语义或 `FHitResult` 字段桥接已经漂移，测试尾部的“最终通过条件”仍然把它们视为成功。 |
| 影响 | 这三条用例名义上是 parity 回归，但当前尾部返回更接近 execution smoke。若后续有人沿用这些模式做重构，或者只看 `RunTest()` 的最终返回条件进行二次封装，真正的脚本语义回归会被弱化成“日志里有失败断言，但通过条件没有把它列为核心合同”，不利于把 parity 要保护的结果值固定下来。 |
| 修复建议 | 把语义结果也折回最终判定，例如分别引入 `const bool bReturnedExpected = TestEqual(...);`，并令返回条件变成 `Prepare/Execute/ReturnedExpected` 三者同时成立；或者更直接地改成每个关键 `TestEqual(...)` 失败即 `return false`。若后续按既有拆分建议把 parity 用例迁到 `ASTEST_COMPILE_RUN_*` helper，也要保留“结果值参与最终通过条件”的合同，而不是只包装 execution 成功。 |

#### Issue-97：`UnnamedBindBackwardCompatibility` 的关键状态断言都停留在旁路，unnamed bind 默认语义回归缺少硬闸

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.UnnamedBindBackwardCompatibility` |
| 行号范围 | 409-417 |
| 问题描述 | 用例在拿到 `UnnamedBindInfo` 之后，连续写了 3 条真正有语义价值的断言：默认 `BindOrder == 0`、默认 `bEnabled == true`、startup bind 执行次数为 `1`。但这些断言后面直接跟着 `return true;`，没有把它们纳入显式通过条件。也就是说，只要前面的“找到了一个 unnamed bind”与基础 setup 没出错，后半段关于默认顺序、启用状态和执行次数的核心 backward-compatibility 合同，在尾部返回里都只是旁路检查。 |
| 影响 | 当前这条用例是 unnamed bind backward compatibility 的唯一集中入口；如果后续回归把 unnamed bind 默认顺序改掉、错误标成 disabled，或执行次数出现漂移，而前半段 auto-name 注册仍然成功，测试就更像“registry 里出现了 unnamed bind smoke”，而不是“默认语义被完整固定”。这会让 query 视图和执行语义的退化比应有更晚暴露。 |
| 修复建议 | 把 409-416 行的关键断言全部折回最终结果，例如 `const bool bOrderDefaulted = TestEqual(...); const bool bEnabledByDefault = TestTrue(...); const bool bExecutedOnce = TestEqual(...); return bOrderDefaulted && bEnabledByDefault && bExecutedOnce;`；或者逐条改成失败即 `return false`。若后续按既有建议把 unnamed bind 测试拆到独立文件，也应把“默认 order / enabled / executed-once”保留成同一条用例的硬合同，而不是留在日志级附带检查。 |

### 二、需要新增的测试

#### NewTest-100：给 `Bind_CollisionProfile.cpp` 补齐 sanitize / empty / duplicate 三条 identifier 生成合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_CollisionProfile.cpp` |
| 关联函数 | `CollisionProfileBind::MakeIdentifier(const FName&)` / `Bind_CollisionProfile` |
| 现有测试覆盖 | 当前目标测试里只有 `Angelscript.TestModule.Parity.CollisionProfileCompile` 一条 happy-path smoke，而且它只拿一个现成 profile 做常量访问；对 `Bind_CollisionProfile.cpp` 里的真正关键分支完全没有定点覆盖，包括“非法字符替换”“数字前缀补 `_`”“sanitized 结果为空时跳过绑定”“两个 profile sanitize 后重名时去重跳过”。 |
| 风险评估 | 自定义 collision profile 是项目级常见扩展点。若 profile 名里带空格、横杠、数字前缀，或两个 profile sanitize 后碰撞成同名 identifier，脚本侧会静默拿不到常量，甚至把错误 profile 暴露成可访问项；当前自动化只验证了一个普通 profile 的 happy path，无法提前发现这些真正容易在项目配置里出现的回归。 |
| 建议测试名 | `Angelscript.TestModule.Engine.CollisionProfileBinding.SanitizesAndDeduplicatesIdentifiers` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCollisionProfileBindTests.cpp` |
| 场景描述 | 先把 `Bind_CollisionProfile.cpp` 中的 identifier 生成与 profile 列表遍历提炼成可测试 helper，例如 `MakeIdentifierForTesting(...)` 与 `BuildCollisionProfileDeclarationsForTesting(...)`，仅在 `WITH_DEV_AUTOMATION_TESTS` 下暴露。测试喂入 4 组 synthetic profile 名：`Block All`、`2D-Profile`、`!!!`、以及一组会碰撞的 `Foo Bar` / `Foo-Bar`。第一段直接验证 sanitize 结果；第二段通过 fake bind sink 或 declaration collector 执行一次“模拟注册”，检查哪些 profile 最终产出声明、哪些被跳过。 |
| 输入/前置 | `ASTEST_*` 宏建立最小 test shell；一个从 `Bind_CollisionProfile.cpp` 提炼出的纯函数 helper；一个本地 `FCollisionProfileBindSink` 记录最终生成的 declaration / identifier / source profile；必要时 `TArray<FName>` synthetic 输入与 `ON_SCOPE_EXIT` 恢复任何测试 seam。 |
| 期望行为 | `Block All` 必须生成 `Block_All`；`2D-Profile` 必须生成 `_2D_Profile`；`!!!` 必须得到空 identifier 并在模拟注册阶段被跳过；`Foo Bar` 与 `Foo-Bar` 必须映射到同一个 sanitized identifier，且最终只允许 1 条 declaration 被收录，另一条作为 duplicate 被拒绝。生成的 declaration 还应保持 `const FName <Identifier>` 形式，确保脚本常量名与 native profile 名映射稳定。 |
| 使用的 Helper | `ASTEST_*` 宏 + `MakeIdentifierForTesting(...)` / `BuildCollisionProfileDeclarationsForTesting(...)` + 本地 `FCollisionProfileBindSink` + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-96 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 01:12:41)

### 一、现有测试问题

#### Issue-94：`ExecuteSnippet` 的最终通过条件没有纳入返回值断言，脚本语义可在绿灯下漂移

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.ExecuteSnippet` |
| 行号范围 | 143-150 |
| 问题描述 | 用例在 145-147 行分别调用 `TestEqual(...)` 校验 `PrepareResult`、`ExecuteResult` 和脚本返回值 `42`，但 150 行的 `bPassed` 只取决于 `PrepareResult == asSUCCESS && ExecuteResult == asEXECUTION_FINISHED`。也就是说，`ReturnFortyTwo()` 若静默回归成返回其他值，只要仍能成功执行，这条测试的显式通过条件就不会把“返回值错误”纳入失败判定。 |
| 影响 | `ExecuteSnippet` 是 `EngineCore` 里少数真的执行脚本的基础用例，但当前最终只保护“能跑完”，没有把结果语义牢固绑定到 pass/fail。脚本返回寄存器、context 取值或调用桥接一旦回归，测试信号会弱化成日志级断言，生命周期 smoke 仍可能表现为通过。 |
| 修复建议 | 把返回值断言显式纳入最终结果，例如 `const bool bReturnedExpected = TestEqual(..., 42);`，并令 `bPassed = PrepareResult == asSUCCESS && ExecuteResult == asEXECUTION_FINISHED && bReturnedExpected`；更直接的做法是改成遇到任一关键断言失败就 `return false`。若后续按既有建议迁到 `ExecuteIntFunction(...)` / `ASTEST_COMPILE_RUN_INT`，也要保留“返回值参与最终通过条件”的合同。 |

### 二、需要新增的测试

#### NewTest-99：覆盖 `UAngelscriptGameInstanceSubsystem` 多实例共享 primary engine 与 tick-owner 计数合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 关联函数 | `UAngelscriptGameInstanceSubsystem::Initialize(FSubsystemCollectionBase&)` / `Deinitialize()` / `HasAnyTickOwner()` |
| 现有测试覆盖 | 当前文档里的 `NewTest-10` 只覆盖单个 subsystem 的 adopt/own 生命周期，`NewTest-67` 只覆盖 tickability gate，`NewTest-88` 只覆盖 `GetCurrent()` null guard；没有任何一条建议直接固定“第一个 subsystem 自建并 push primary engine，第二个 subsystem 复用该 engine，且 `HasAnyTickOwner()` 要一直保持为 true 直到最后一个 owner 释放”的多实例场景。 |
| 风险评估 | `ActiveTickOwners` 是 `RuntimeModule` fallback tick 的全局门控。若多 `UGameInstance` / 多会话场景下计数提前归零、第二个 subsystem 错误自建新 engine，或非 owner 的 `Deinitialize()` 提前把共享 engine 弹栈，editor/PIE 中的 tick、热重载和 unit-test 运行都会出现隐蔽串线，而当前自动化没有定点保护。 |
| 建议测试名 | `Angelscript.TestModule.GameInstanceSubsystem.MultiOwnerLifecycle.SharedPrimaryEngineKeepsTickOwnershipUntilLastShutdown` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptGameInstanceSubsystemRuntimeTests.cpp` |
| 场景描述 | 复用 `NewTest-10` 计划中的 live `UGameInstance` / `UWorld` fixture，但一次创建两个独立 game-instance 容器。第一段在无 ambient engine 的基线下初始化 `SubsystemA`，记录 `EngineA = SubsystemA->GetEngine()`，并确认它走 own-engine 路径。第二段在 `SubsystemA` 仍存活时初始化 `SubsystemB`，记录 `EngineB = SubsystemB->GetEngine()`；随后依次执行 `SubsystemB->Deinitialize()`、`SubsystemA->Deinitialize()`，在每个阶段检查 `HasAnyTickOwner()` 和 current-engine 状态。必要时在两个 subsystem 之间各调用一次 `Tick(0.016f)`，证明共享的 `EngineA` 仍然可推进。 |
| 输入/前置 | `UGameInstance` / `UWorld` 双实例 runtime fixture；从 `AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` 复用或提炼 `FAngelscriptTickBehaviorTestAccess`，用来读取 `PrimaryEngine`、`bOwnsPrimaryEngine` 与 `ActiveTickOwners`；`FCoreTestContextStackGuard` 或等价 scoped context guard；`ON_SCOPE_EXIT` 确保两个 subsystem 全部 `Deinitialize()` 并恢复 `ActiveTickOwners`。 |
| 期望行为 | `SubsystemA` 初始化后，`GetEngine()` 非空、`bOwnsPrimaryEngine == true`、`HasAnyTickOwner() == true`。`SubsystemB` 初始化后，`GetEngine()` 必须与 `EngineA` 同址，且 `bOwnsPrimaryEngine == false`，证明第二个实例复用共享 primary engine 而不是新建一台；此时 `ActiveTickOwners` 应为 `2`。先 `Deinitialize()` `SubsystemB` 后，`HasAnyTickOwner()` 仍必须为 `true`，`EngineA` 仍保持为 current engine 且可继续 `Tick()`；只有在最后 `SubsystemA` 也 `Deinitialize()` 后，`HasAnyTickOwner()` 才变为 `false`，共享 engine 被正确弹栈/关闭，不允许提前丢失 tick owner 或残留脏 engine。 |
| 使用的 Helper | live `UGameInstance` / `UWorld` 双实例 fixture + `FAngelscriptTickBehaviorTestAccess` + `FCoreTestContextStackGuard` + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-94 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 01:15:10)

### 一、现有测试问题

#### Issue-95：`GlobalDisabledBindNames` 的最终返回没有纳入 `bEnabled == false` 断言，settings 视图回归可能被放过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.GlobalDisabledBindNames` |
| 行号范围 | 304-314 |
| 问题描述 | 用例前半段已经正确验证“settings-level disabled bind 不会执行”，后半段也会取 `GetBindInfoList(MergedDisabledBindNames)` 并在 313 行写出关键断言 `TestFalse(..., NamedBindInfo->bEnabled)`。但这条断言之后紧跟着 314 行 `return true`，没有把 `bEnabled == false` 的结果折回最终通过条件。也就是说，一旦 query 视图回归成仍把该 bind 标成 enabled，而执行路径恰好仍被禁用，这条测试的显式返回仍然是通过。 |
| 影响 | `GlobalDisabledBindNames` 目前是 settings-level disabled merge 的唯一 query-view 合同；如果 registry 可见性与执行路径发生分叉，当前测试会更偏向保护“执行被禁用”，却弱化“查询结果也必须显示 disabled”。这会让后续依赖 `GetBindInfoList(...)` 的工具链在错误状态下继续拿到绿灯。 |
| 修复建议 | 把 313 行改成参与最终判定的布尔值，例如 `const bool bReportedDisabled = TestFalse(..., NamedBindInfo->bEnabled); return bReportedDisabled;`；或者直接写成 `if (!TestFalse(...)) { return false; }`。顺手再补一个 control 断言，确认某个未禁用 bind 仍保持 `bEnabled == true`，避免只验证单侧状态。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-95 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:47:27)

### 记录校正

本轮有效新增发现为：
- `Issue-91`
- `Issue-92`
- `Issue-93`
- `NewTest-96`
- `NewTest-97`
- `NewTest-98`

上述 6 条完整正文已写入本文前段，但本次续写再次命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 3 | Issue-92 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingErrorPath: 1, MissingEdgeCase: 1 |
| P2 | 0 | 无 |
| P3 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 00:47:27)

### 一、现有测试问题

#### Issue-91：`AngelscriptEngineParityTests.cpp` 已超 500 行且混装 15 个 parity 主题，违背单文件单职责规范

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.*` |
| 行号范围 | 1-697 |
| 问题描述 | 文件长度已到 697 行，内部同时承载 `SkinnedMesh`、`DelegateWithPayload`、`CollisionProfile`、`WorldCollision`、`FIntPoint`、`FVector2f`、`SoftReference`、`UserWidgetPaint`、`LevelStreaming`、`RuntimeCurveLinearColor`、`FHitResult`、`DeprecationsMetadata`、`StartupBindRegistry` 等 15 个互不相干的 parity family。当前文件既包含纯 type-info 检查，也包含 compile smoke、execute smoke 和 metadata 检查，审查、定位回归和后续补断言时都需要反复在同一大文件里切换上下文。 |
| 影响 | 这已经明显偏离项目要求的“单文件 300-500 行、单文件单职责”。后续再补更多 parity 断言时，文件会继续膨胀，merge 冲突、误改相邻测试和遗漏 cleanup 的概率都会上升；同时 reviewer 很难快速判断某一类 parity 是否已形成完整合同。 |
| 修复建议 | 按主题拆分到多个 300-500 行以内的文件，例如 `AngelscriptParityTypeSurfaceTests.cpp`、`AngelscriptParityMathStructTests.cpp`、`AngelscriptParityCollisionTests.cpp`、`AngelscriptParitySoftReferenceAndWidgetTests.cpp`、`AngelscriptParityRegistryMetadataTests.cpp`。拆分时保持 `ASTEST_*` / shared helper 复用，但不要再把 compile parity、execute parity、metadata parity 长期混在一个文件里。 |

#### Issue-92：`AngelscriptBindConfigTests.cpp` 已膨胀到 850 行，bind registry 与 UHT metadata 回归被混装在同一文件

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.*` |
| 行号范围 | 1-850 |
| 问题描述 | 文件长度达到 850 行，内部同时覆盖 settings/global disabled merge、unnamed bind backward compatibility、startup bind order、startup disabled merge、generated blueprint callable entries、`BlueprintInternalUseOnly` override、`ScriptMethod` metadata、world-context trait、overload recovery、inline direct-bind recovery 等多种方向。前半段是全局 bind registry 状态操作，后半段又切到 full-engine + `FAngelscriptFunctionSignature` / generated entry 语义，主题跨度非常大。 |
| 影响 | 同一文件里同时存在“改全局 bind registry 的 native 单元测试”和“依赖 full engine / generated bindings 的集成测试”，会放大上下文切换成本，也更容易让后续修复把某一侧的隔离策略误带到另一侧。持续在这个文件上追加 case，只会进一步恶化可维护性和代码审查效率。 |
| 修复建议 | 至少拆成三类文件：`AngelscriptBindConfigRegistryTests.cpp`（global/engine/startup bind registry 行为）、`AngelscriptBindConfigMetadataTests.cpp`（`BlueprintInternalUseOnly`、`ScriptMethod`、world-context trait）、`AngelscriptBindConfigDirectBindRecoveryTests.cpp`（generated entries、overload、inline direct-bind recovery）。每个文件控制在 300-500 行，并把 registry snapshot/restore helper 与 full-engine fixture helper 放到 `Shared/`，避免继续在单文件里混用多套隔离模型。 |

#### Issue-93：`AngelscriptGeneratedFunctionTableTests.cpp` 已超 500 行，运行时注册回归与磁盘报表回归没有分层

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.*` |
| 行号范围 | 1-778 |
| 问题描述 | 文件长度已到 778 行，同时混放了 runtime `ClassFuncMaps` 断言、handwritten GAS entry 保留、`WITH_EDITOR` 产物 guard、summary json 自洽、entries/module csv、skipped csv、reason summary csv，以及 macro-qualified direct-binding 文本扫描。前半段依赖当前进程内的运行时 registry，后半段依赖磁盘上的 UHT generated artifact，失败来源完全不同，却都塞在一个文件里。 |
| 影响 | 这会让“运行时注册回归”和“报表导出回归”互相污染审查语境，也让任何小改动都更容易引发大文件冲突。随着后续继续补 runtime-vs-artifact 交叉断言，这个文件只会更难维护，而且不利于把不同前置条件的测试拆到合适的 suite。 |
| 修复建议 | 把文件拆成至少三层：`AngelscriptGeneratedFunctionTableRuntimeTests.cpp`（`ClassFuncMaps` / direct-vs-fallback / handwritten GAS entry）、`AngelscriptGeneratedFunctionTableArtifactTests.cpp`（summary/json/csv/skipped 报表）、`AngelscriptGeneratedFunctionTableCodegenTextTests.cpp`（`WITH_EDITOR` guard、macro-qualified codegen 文本）。每个文件控制在 300-500 行，并共享一个 `ResolveGeneratedFunctionTableOutputDir()` helper，避免重复硬编码 UHT 目录。 |

### 二、需要新增的测试

#### NewTest-96：覆盖 `FAngelscriptBindDatabase::Load(..., true)` 在缺失 `.Headers` sidecar 时的降级合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` |
| 关联函数 | `FAngelscriptBindDatabase::Load(const FString& Path, bool bGeneratingPrecompiledData)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-11` 只覆盖 `Binds.Cache` 与 `Binds.Cache.Headers` 都存在时的正向 round-trip；没有任何建议直接命中“cache 主文件存在，但 `.Headers` sidecar 缺失”这一 editor/cook 常见降级分支。 |
| 风险评估 | 如果 `Load(Path, true)` 在缺失 `.Headers` 文件时污染旧 `HeaderLinks`、误保留上一次缓存，或把主 `Classes/Structs` 也一起读坏，cooked/static-JIT 场景会出现“绑定数据恢复了，但头文件映射串台/残留”的隐蔽回归，当前自动化没有定位点。 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindDatabase.LoadWithoutHeadersSidecarLeavesHeaderLinksEmptyButRestoresBinds` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindDatabaseTests.cpp` |
| 场景描述 | 在 isolated full engine 下构造一份最小 `FAngelscriptBindDatabase` 内容，至少填 1 个 `FAngelscriptClassBind` 和 1 个 `FAngelscriptStructBind`，调用 `Save(CachePath)` 生成 `Binds.Cache` 与 `.Headers`。随后先向 `HeaderLinks` 注入一个哨兵条目，再删除 `CachePath + ".Headers"`，执行 `Clear()`，最后调用 `Load(CachePath, true)`。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；临时目录 helper；样本 `FAngelscriptClassBind` / `FAngelscriptStructBind`；一个可作为 `HeaderLinks` key 的已存在 `UClass*` 或 `UScriptStruct*`；`IFileManager::Get().Delete(...)` 删除 `.Headers`；`ON_SCOPE_EXIT` 清理缓存目录并再次 `Clear()`。 |
| 期望行为 | `Load(CachePath, true)` 后 `Classes.Num()` 与 `Structs.Num()` 仍应恢复到保存前数量，字段内容与原始样本一致；`HeaderLinks.Num()` 必须为 `0`，且不允许保留删除 `.Headers` 前手工塞入的哨兵映射。这样才能证明 sidecar 缺失只影响 header link 恢复，不会把旧映射静默带入新一轮加载。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 临时目录 helper + `IFileManager` 文件删除 helper |
| 优先级 | P1 |

#### NewTest-97：给 `FAngelscriptBindDatabase::Clear()` 补齐 `BoundEnums` / `BoundDelegateFunctions` 清空合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` |
| 关联函数 | `FAngelscriptBindDatabase::Clear()` |
| 现有测试覆盖 | `NewTest-11` 只计划断言 `Classes` / `Structs` / `HeaderLinks` 清空；当前文档和现有目标测试都没有任何一条直接保护 `BoundEnums` 与 `BoundDelegateFunctions` 这两组 editor header sidecar 依赖状态。 |
| 风险评估 | 若 `Clear()` 未来只清 class/struct，而把 `BoundEnums` 或 `BoundDelegateFunctions` 留下来，后续 `Save()` 会继续把旧枚举/委托头文件写进 `.Headers` sidecar，形成“主缓存已刷新、头文件映射却来自旧 epoch”的错配，静态 JIT 和文档/导航链路都可能被污染。 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindDatabase.ClearPurgesEnumsDelegatesAndHeaderLinks` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindDatabaseTests.cpp` |
| 场景描述 | 在 isolated full engine 下取得 engine-owned bind database，手工填充 `Classes`、`Structs`、`HeaderLinks`，再额外把一个已存在 `UEnum*`（如 `ECollisionChannel::StaticEnum()`）和一个已存在 `UDelegateFunction*`（从 runtime 里找一个具体 delegate function）分别塞进 `BoundEnums` 与 `BoundDelegateFunctions`。随后调用 `Clear()`。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；一个样本 `UEnum*`；一个样本 `UDelegateFunction*`；轻量 class/struct bind 哨兵；`ON_SCOPE_EXIT` 再次 `Clear()`。 |
| 期望行为 | `Clear()` 后 `Classes.Num()`、`Structs.Num()`、`HeaderLinks.Num()`、`BoundEnums.Num()`、`BoundDelegateFunctions.Num()` 都必须为 `0`。必要时再补一条 control：清空后重新 `Save()`，断言新的 `.Headers` sidecar 不会再包含前一轮塞入的 enum/delegate 头文件路径。 |
| 使用的 Helper | `FAngelscriptTestFixture` + runtime enum/delegate lookup helper |
| 优先级 | P1 |

#### NewTest-98：覆盖 `FAngelscriptDocs` 对 `@see` / `@note` / `@returns` 的归一化输出合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` |
| 关联函数 | `GetFunctionTooltip(...)` / `GetParamTooltip(...)` / `FAngelscriptDocs::DumpDocumentation(asIScriptEngine*)` |
| 现有测试覆盖 | `NewTest-15` 只覆盖 documentation cache 读写；`NewTest-68` 只覆盖 `DumpDocumentation()` 能生成 `Parameters:` / `Returns:` 与 accessor section，但没有任何建议直接验证 `@see`、`@note` 文本会被归一化，或 `@returns` 别名会被当成返回值说明处理。 |
| 风险评估 | 如果 docs parser 回归成原样透传 doxygen tag、丢失 `@returns` 别名支持，或多行参数/说明文本不再被规整，离线导出文档会出现原始 tag 泄漏、返回说明缺失和格式错乱；问题不会影响编译，但会直接降低插件对外可用性。 |
| 建议测试名 | `Angelscript.TestModule.Engine.Docs.DumpDocumentationNormalizesSeeNoteAndReturnsAliases` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocsTests.cpp` |
| 场景描述 | 在 isolated full engine 下编译一个唯一 GUID 后缀的最小脚本类型，包含一个带参数和返回值的函数，例如 `int EvaluateScore(int InValue) const`。向该函数 id 写入一段包含 `@see RelatedScoreType`、`@note Keep integer only`、`@param InValue - first line` + 下一行续写、以及 `@returns final computed score` 的 tooltip，再调用 `DumpDocumentation()` 并读取生成的 `.hpp`。 |
| 输入/前置 | 复用 `NewTest-68` 的 compile + dump helper；唯一 module/type 名；`FAngelscriptDocs::AddUnrealDocumentation(...)`；`ON_SCOPE_EXIT` 清理生成的 `.hpp` 与临时模块。 |
| 期望行为 | 生成文档里必须出现 `See: RelatedScoreType`、`Note: Keep integer only`、`Parameters:` 段和 `Returns:` 段；`Returns:` 必须来自 `@returns` 别名而不是只有 `@return` 才生效；多行参数说明应被折叠成单段可读文本；文档中不应残留原始 `@see`、`@note`、`@returns` tag。 |
| 使用的 Helper | `CompileModuleFromMemory(...)` + script function lookup helper + `FAngelscriptDocs::AddUnrealDocumentation(...)` / `DumpDocumentation(...)` + 文件读取 helper |
| 优先级 | P3 |

---

## 测试审查 (2026-04-10 00:41:42)

### 一、现有测试问题

#### Issue-90：`Discovery` / `SkipRules` 只验证 `RelativePath`，没有保护 `AbsolutePath` 是否仍指向注入测试根

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.Discovery` / `Angelscript.TestModule.FileSystem.SkipRules` |
| 行号范围 | 237-317 |
| 问题描述 | 两条用例都只消费 `FFilenamePair::RelativePath`。`Discovery` 在 263-273 行只断言 `Files.Num()` 和 `FoundRelativePaths` 集合；`SkipRules` 在 307-310 行也只比较 `Files[0].RelativePath`。整个文件从未读取 `FFilenamePair::AbsolutePath`，因此即便 `FindAllScriptFilenames(...)` 错把 legacy 根目录、错误 root，或其他同相对路径文件塞进结果，只要 `RelativePath` 仍然长得像 `Gameplay/Main.as` / `Game/AI/Patrol.as`，当前测试就会继续绿灯。 |
| 影响 | 文件发现测试目前只能证明“枚举出来了某个相对路径”，不能证明 runtime 真正把它绑定到了当前测试注入的磁盘根。若 `FindScriptFiles(...)` / `FindAllScriptFilenames(...)` 在多 root、legacy 残留或路径拼接回归时返回了错误 `AbsolutePath`，后续 compile、hot reload 和 debugger 都可能吃到错文件，而当前 `Discovery` / `SkipRules` 不会定点报警。 |
| 修复建议 | 在现有两条用例里补强 `AbsolutePath` 断言，而不是只看 `RelativePath`。`Discovery` 应构造 `RelativePath -> AbsolutePath` 映射，逐项断言 `AbsolutePath.StartsWith(GetFileSystemTestRoot())` 且等于测试刚写入的完整路径；`SkipRules` 也应对保留下来的唯一条目断言 `AbsolutePath == FPaths::Combine(GetFileSystemTestRoot(), TEXT("Gameplay/Main.as"))`，并显式验证没有任何返回项落到 legacy root。若后续需要覆盖多 root 语义，单独补 focused discovery 测试，而不是继续把绝对路径合同留空。 |

### 二、需要新增的测试

#### NewTest-95：覆盖 `FindAllScriptFilenames(...)` 在多 root 同相对路径下的绝对路径身份合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::FindScriptFiles(...)` / `FAngelscriptEngine::FindAllScriptFilenames(...)` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 的 `Discovery` / `SkipRules` 只覆盖单 root happy path，并且只校验 `RelativePath`；当前文档里也没有任何一条建议直接命中“两个 script root 下存在同名相对路径文件时，`AbsolutePath` 是否仍保持区分和 root 顺序” |
| 风险评估 | 项目根与插件根、多个插件根或 legacy 根共存时，经常会出现相同 `RelativePath`。如果 runtime 在 discovery 阶段把不同 root 的文件折叠成同一条、重写成错误 `AbsolutePath`，或打乱 root 优先级，编译和热重载会静默绑定错脚本源，而当前自动化没有任何直接保护。 |
| 建议测试名 | `Angelscript.TestModule.FileSystem.Discovery.MultiRootDuplicateRelativePathsKeepDistinctAbsolutePaths` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemDiscoveryTests.cpp` |
| 场景描述 | 建两个独立临时 root，例如 `RootA` 与 `RootB`，并在两边都写入 `Game/Shared.as`，内容可不同以便调试。把 `Engine.AllRootPaths` 临时设为 `{ RootA, RootB }`，`bUseEditorScripts` 设为 `true`，随后调用 `FindAllScriptFilenames(Files)`。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；一个可显式指定根目录的写文件 helper（可从现有 `WriteFileSystemTestFile(...)` 提炼成 `WriteFileSystemTestFileAtRoot(...)`）；`TGuardValue<bool>` 守护 `bUseEditorScripts`；`ON_SCOPE_EXIT` 恢复 `AllRootPaths` 并清理 `RootA` / `RootB`。 |
| 期望行为 | `Files.Num()` 必须等于 `2`；两条记录的 `RelativePath` 都应是 `Game/Shared.as`；`AbsolutePath` 必须分别精确等于 `FPaths::Combine(RootA, TEXT("Game/Shared.as"))` 与 `FPaths::Combine(RootB, TEXT("Game/Shared.as"))`，且不能被 legacy root 或其他目录替换；若实现承诺沿 `AllRootPaths` 顺序追加结果，还应断言 `Files[0].AbsolutePath` 来自 `RootA`、`Files[1].AbsolutePath` 来自 `RootB`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + 文件系统临时 root helper + `TGuardValue` + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-90 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:05:59)

### 一、现有测试问题

### 二、需要新增的测试

#### NewTest-93：覆盖 `GetModuleByFilename(...)` 的大小写无关绝对路径 lookup 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::GetModuleByFilename(const FString&)` / `GetModuleByFilenameOrModuleName(const FString&, const FString&)` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 目前只覆盖 exact-case absolute path、slash normalization、rename remap 和 filename/module-name precedence；全仓已记录问题与新增建议都没有任何一条直接命中 `Section.AbsoluteFilename.Equals(Filename, ESearchCase::IgnoreCase)` 这条大小写无关分支，也没有验证大小写变化后的 filename lookup 仍然优先于错误的 module-name fallback。 |
| 风险评估 | `GetModuleByFilename(...)` 在 3031-3037 行把已编译 code section 的 `AbsoluteFilename` 与输入路径做 `IgnoreCase` 比较。若这条分支未来回归成区分大小写，Windows/editor 路径大小写漂移、外部工具回传的 drive-letter 大小写变化，或 debugger/commandlet 传回的不同 casing 都可能让 runtime 找不到已编译模块；当前 `FileSystem` 套件不会给出定点报警。 |
| 建议测试名 | `Angelscript.TestModule.FileSystem.ModuleLookupByFilename.CaseInsensitiveAbsolutePathResolvesBeforeFallback` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemLookupCaseTests.cpp` |
| 场景描述 | 在 clean shared engine 下写入并编译 `Game/AI/Patrol.as` 到模块 `Game.AI.Patrol`。拿到编译时的 `AbsolutePath` 后，构造一个仅改变字母大小写的 `CasedPath`，例如把 drive letter 与若干目录名切成另一种大小写，但保留同一物理路径。随后分别调用 `GetModuleByFilename(AbsolutePath)`、`GetModuleByFilename(CasedPath)` 和 `GetModuleByFilenameOrModuleName(CasedPath, TEXT("Game.Wrong.Module"))`。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；复用 `WriteFileSystemTestFile(...)` / `CleanFileSystemTestRoot()`；`CompileModuleFromMemory(...)`；一个本地 helper `MakeCaseVariantPath(...)` 只翻转字母大小写、不改动路径结构；`ON_SCOPE_EXIT` 中 `Engine.DiscardModule(TEXT("Game.AI.Patrol"))`、`ResetSharedCloneEngine(Engine)`、清理磁盘目录。 |
| 期望行为 | `GetModuleByFilename(AbsolutePath)` 与 `GetModuleByFilename(CasedPath)` 都必须返回有效 `ModuleDesc`，且两者是同一 `TSharedPtr`；`GetModuleByFilenameOrModuleName(CasedPath, TEXT("Game.Wrong.Module"))` 也必须返回同一个 `ModuleDesc`，证明命中的是 case-insensitive filename branch 而不是 module-name fallback；返回的 `ModuleDesc->Code` 至少有一个 section，且 `Code[0].AbsoluteFilename` 仍等于原始 `AbsolutePath`，不能被 lookup 输入的大小写变体污染。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleFromMemory(...)` + `WriteFileSystemTestFile(...)` + 本地 `MakeCaseVariantPath(...)` + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-10 00:20:04)

### 一、现有测试问题

#### Issue-89：`FindGeneratedBindingLine(...)` 只做首个子串命中，`CsvOutput` / `MacroQualifiedDirectBindings` 可能抓到错误行

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.CsvOutput` / `Angelscript.TestModule.Engine.GeneratedFunctionTable.MacroQualifiedDirectBindings` |
| 行号范围 | 104-129, 647-649, 759-770 |
| 问题描述 | 这两条用例共享的 `FindGeneratedBindingLine(...)` helper 会递归扫描全部 `AS_FunctionTable_*.cpp`，然后在遇到第一条 `Line.Contains(FunctionName)` 时立即返回。它既不要求该行真的是 `FAngelscriptBinds::AddFunctionEntry(...)` 注册行，也不验证命中的是精确函数名 token，更没有固定文件遍历顺序。结果是，只要任意 shard 里先出现了包含 `RunBehaviorTree` / `ReportPerceptionEvent` 子串的其他文本，例如别的注册行、注释、字符串字面量或同名前后缀函数名，这两个测试就可能拿错行继续断言，形成“看起来找到了目标 binding，实际上比对的是别的文本”的假绿灯。 |
| 影响 | `CsvOutput` 与 `MacroQualifiedDirectBindings` 已经分别存在“工件来源用错”和“只看 codegen 文本不看运行时 entry”的问题；这个 helper 的宽松首命中又进一步放大了误判空间，使得测试结果还会受 shard 布局和文件遍历顺序影响。即便将来修正工件来源，这个 helper 仍可能让测试在错误行上通过，无法稳定保护指定函数的 direct-binding 合同。 |
| 修复建议 | 不要再用裸 `Line.Contains(...)`。`CsvOutput` 应直接在 `EntryLines` 上按列精确匹配 `FunctionName == RunBehaviorTree`；`MacroQualifiedDirectBindings` 则应把 helper 收紧为“只匹配 `AddFunctionEntry` 注册行 + 精确函数名 token + 目标类名/模块名”，必要时用正则或先解析出参数列。无论保留哪种文本扫描方式，都应先对文件列表排序，并在命中多行时显式失败，而不是默认取第一条。 |

### 二、需要新增的测试

本轮未新增需要新增的测试；`AngelscriptRuntime/Core/` 回扫到的关键公开路径要么已在前文有明确建议，要么属于第三方/壳文件，不适合重复登记。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-89 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:07:07)

### 记录校正

本轮有效新增发现为：
- `Issue-88`
- `NewTest-93`

上述 2 条完整正文已写入本文前段，但续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 1 | Issue-88 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-10 00:25:13)

### 记录校正

本轮有效新增发现为：
- `Issue-89`
- `NewTest-94`

上述 2 条完整正文已写入本文前段，但上一轮续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-89 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:18:17)

### 二、需要新增的测试

#### NewTest-73：给 `UAngelscriptAbilityTaskLibrary` 补齐 control/action wrapper 的工厂参数合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `WaitNetSync(...)` / `PlayMontageAndWait(...)` / `RepeatAction(...)` / `WaitForCancelInput(...)` / `WaitForConfirmInput(...)` / `WaitConfirmCancel(...)` |
| 现有测试覆盖 | 当前文档里已记录的 `NewTest-20`、`NewTest-71`、`NewTest-72` 只覆盖 event/tag、targeting/root-motion move-to、gameplay-effect watcher 这几组 wrapper；上述 control/action wrappers 在目标测试目录和已记录建议里仍然完全无直接覆盖。 |
| 风险评估 | 这批 API 都是脚本 ability 常用的控制流入口。若 wrapper 返回错 task class、丢失 `SyncType` / `Montage` / `Rate` / `StartSection` / `TimeBetweenActions` 等种子参数，脚本面会表现成“节点存在但行为错位”，而当前自动化没有任何定点报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.ControlWrappersSeedExpectedTasks` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryControlTests.cpp` |
| 场景描述 | 建最小 GAS fixture：owner actor、avatar actor、`UAngelscriptAbilitySystemComponent`、native `UGameplayAbility` test double，以及一个 `UAnimMontage`。分别调用 `WaitNetSync(OnlyServerWait)`、`PlayMontageAndWait(TaskName, Montage, 1.25f, StartSection, true, 0.5f, 0.2f)`、`RepeatAction(0.15f, 3)`、`WaitForCancelInput()`、`WaitForConfirmInput()`、`WaitConfirmCancel()`。对 `UAbilityTask_NetworkSyncPoint`、`UAbilityTask_PlayMontageAndWait`、`UAbilityTask_Repeat` 增加本地 access shim 读取 `SyncType`、`MontageToPlay`、`Rate`、`StartSection`、`StartTimeSeconds`、`bStopWhenAbilityEnds`、`ActionPerformancesDesired`、`TimeBetweenActions`。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或等价 GAS fixture；native `UGameplayAbility` test double；测试 montage；`FName TaskInstanceName = TEXT("AutomationMontageTask")`；本地 access shim 暴露 protected 字段；`ON_SCOPE_EXIT` 结束并销毁所有创建的 task。 |
| 期望行为 | `WaitNetSync` 返回 `UAbilityTask_NetworkSyncPoint`，且 access shim 读到 `SyncType == EAbilityTaskNetSyncType::OnlyServerWait`；`PlayMontageAndWait` 返回 `UAbilityTask_PlayMontageAndWait`，并保留传入的 montage、rate、start section、start time、stop-when-ability-ends；`RepeatAction` 返回 `UAbilityTask_Repeat`，其期望次数为 `3`、间隔为 `0.15f`、初始 action counter 为 `0`；三个 confirm/cancel wrapper 分别返回 `UAbilityTask_WaitCancel`、`UAbilityTask_WaitConfirm`、`UAbilityTask_WaitConfirmCancel`。所有 task 的 `GetOuter()` / owning ability 都必须等于传入 ability。 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS fixture helper + native access shims（`UAutomationWaitNetSyncTaskAccess` / `UAutomationPlayMontageTaskAccess` / `UAutomationRepeatTaskAccess`）+ `ON_SCOPE_EXIT` |
| 优先级 | P2 |

#### NewTest-74：给 `UAngelscriptAbilityTaskLibrary` 补齐 ability activate/commit filter wrapper 的运行时合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `WaitForAbilityActivate(...)` / `WaitForAbilityActivateQuery(...)` / `WaitForAbilityActivateWithTagRequirements(...)` / `WaitForNewAbilityCommit(...)` / `WaitForNewAbilityCommitQuery(...)` |
| 现有测试覆盖 | 当前目标测试目录和已记录建议只覆盖 `AbilityTaskLibrary` 的 event/tag/gameplay-effect wrappers；这 5 个 ability activate/commit filter wrapper 既没有直接测试，也没有任何建议去验证 tag/query/requirements 过滤是否真正生效。 |
| 风险评估 | 如果 wrapper 把 `WithTag` / `WithoutTag` / `TagRequirements` / `Query` / `bTriggerOnce` / `bIncludeTriggeredAbilities` 透传错，脚本 ability 监听器会在错误 ability 上触发，或根本不触发；这会直接破坏 GAS ability 编排，但当前自动化不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.ActivateAndCommitWrappersHonorFilters` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryObserverTests.cpp` |
| 场景描述 | 准备一个最小 GAS fixture：同一个 `UAngelscriptAbilitySystemComponent` 上挂两条 native test ability，一条带 `Ability.Automation.Match` tag，另一条带 `Ability.Automation.Other` tag。创建 5 个 task：1. `WaitForAbilityActivate(Match, None, false, true)`；2. `WaitForAbilityActivateQuery(Query(Match), false, true)`；3. `WaitForAbilityActivateWithTagRequirements(Require Match, Ignore Blocked, false, true)`；4. `WaitForNewAbilityCommit(Match, None, true)`；5. `WaitForNewAbilityCommitQuery(Query(Match), true)`。给 `OnActivate` / `OnCommit` 绑定 recorder object，依次驱动 non-match ability 与 match ability 的 activate/commit。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` + GAS ASC/ability fixture；两个 ability tag 集合；`FGameplayTagQuery` builder；可记录回调 ability 指针与调用次数的 `UAutomationAbilityTaskRecorder`；必要时增加 access shim 读取 `WithTag`、`WithoutTag`、`TagRequirements`、`Query`、`TriggerOnce`、`IncludeTriggeredAbilities`。 |
| 期望行为 | non-match ability 触发时，5 个 task 都不得回调 recorder；match ability activate 时，前三个 `WaitAbilityActivate` task 各触发 1 次且传回的 `ActivatedAbility` 就是 match ability；match ability commit 时，两个 `WaitAbilityCommit` task 各触发 1 次；`bTriggerOnce == true` 时再次重复 activate/commit 不应追加第二次回调；若读取 config 字段，wrapper 写入的 tag/query/requirements 必须与输入完全一致。 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS ability fixture + `UAutomationAbilityTaskRecorder` + 可选 access shim |
| 优先级 | P1 |

#### NewTest-75：给 `UAngelscriptAbilityTaskLibrary` 补齐 attribute threshold wrapper 的阈值与 external-owner 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `WaitForAttributeChange(...)` / `WaitForAttributeChangeWithComparison(...)` / `WaitForAttributeChangeRatioThreshold(...)` / `WaitForAttributeChangeThreshold(...)` |
| 现有测试覆盖 | 当前已记录建议只覆盖 `AbilityAsyncLibrary.WaitForAttributeChanged(...)` 和 `AbilityTaskLibrary` 的 gameplay-effect watcher wrappers，没有任何一条直接命中 attribute change / threshold 这 4 个 task wrapper。 |
| 风险评估 | 一旦 wrapper 把 `ComparisonType`、`ComparisonValue`、`AttributeNumerator` / `AttributeDenominator`、`ExternalOwner` 或 `bTriggerOnce` 透传错，脚本 ability 会在错误阈值或错误 ASC 上触发属性监听，属于高频运行时回归而现有自动化完全失明。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.AttributeWrappersHonorThresholdsAndExternalOwner` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryAttributeTests.cpp` |
| 场景描述 | 搭一个最小 GAS fixture：`UAngelscriptAbilitySystemComponent` + 含 `Health` / `MaxHealth` 的 test `UAngelscriptAttributeSet` + owner actor / external owner actor。创建 4 个 task：1. `WaitForAttributeChange(Health, None, None, true, ExternalOwner)`；2. `WaitForAttributeChangeWithComparison(Health, None, None, GreaterThan, 50.f, true, ExternalOwner)`；3. `WaitForAttributeChangeRatioThreshold(Health, MaxHealth, LessThanOrEqualTo, 0.5f, true, ExternalOwner)`；4. `WaitForAttributeChangeThreshold(MaxHealth, GreaterThanOrEqualTo, 100.f, true, ExternalOwner)`。给各 task 的 `OnChange` 绑定 recorder，然后依次把 `Health` 从 `40 -> 45 -> 60`，把 `MaxHealth` 从 `80 -> 120`，并在需要时同步更新 `Health/MaxHealth` 比例。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` + GAS ASC/attribute fixture；`FGameplayAttribute` 通过 test set class 提取；`UAutomationAttributeTaskRecorder` 分别记录 plain-change 次数、comparison 命中次数、threshold delegate 的 `bMatchesComparison` 与 `CurrentValue/CurrentRatio`；必要时 access shim 读取 `ComparisonType`、`ComparisonValue`、`ExternalOwner`。 |
| 期望行为 | plain `WaitForAttributeChange` 在 `Health` 任何一次变化时触发 1 次；comparison task 在 `Health=45` 时不触发、到 `Health=60` 时首次触发；ratio task 在 `Health/MaxHealth` 下降到 `<= 0.5` 时触发，且 delegate 回报的 `CurrentRatio` 与实际比例一致；threshold task 在 `MaxHealth=80` 时不触发、到 `120` 时触发，并回报 `bMatchesComparison == true` 与 `CurrentValue == 120.f`；所有 task 使用的 focused ASC/owner 都必须解析到传入 `ExternalOwner` 对应的 ASC。 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS ASC/attribute fixture + `UAutomationAttributeTaskRecorder` + 可选 access shim |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:59:30)

### 记录校正

本轮新增发现为 `Issue-79`、`NewTest-71` 与 `NewTest-72`。
三条正文已写入本文前段，但本次追加仍命中了重复汇总锚点，没有落到真正文末；此处仅补齐有效文末标记，供后续轮次按文末继续去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-79 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:20:12)

### 记录校正

本轮有效新增发现为：
- `Issue-80`
- `NewTest-73`
- `NewTest-74`
- `NewTest-75`

上述 4 条完整正文已写入本文前段，但上一轮追加时命中了错误锚点，没有落在真实文末；此处仅补齐真正的文末续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 1 | Issue-80 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:12:35)

### 一、现有测试问题

#### Issue-80：`ExecuteSnippet` 在 `CreateContext()` 失败分支泄漏已编译函数引用

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.ExecuteSnippet` |
| 行号范围 | 132-141 |
| 问题描述 | 用例在 132-135 行确认 `Function` 非空后，继续调用 `Engine.CreateContext()`。如果 138 行这条 `TestNotNull` 失败，会在 140 行直接 `return false`，但前面拿到的 `asIScriptFunction* Function` 没有执行 `Release()`。由于该测试运行在 `ASTEST_CREATE_ENGINE_SHARE()` 的共享 engine 上，这个泄漏不会随着局部变量销毁自动回收。 |
| 影响 | 一旦 `CreateContext()` 因初始化回归、上下文池异常或共享 engine 污染而返回空指针，测试不仅会失败，还会把已编译函数引用留在共享 VM 里，增加后续用例的模块残留和资源泄漏噪音；这会把一次单测失败扩大成跨测试污染。 |
| 修复建议 | 在拿到 `Function` 后立即加 `ON_SCOPE_EXIT` 或等价 RAII guard 统一 `Release()`，并对 `Context` 也采用同样模式；或者至少在 `CreateContext()` 失败返回前显式 `Function->Release()`。若顺手修复 `Issue-09`，可以直接改成 `BuildModule`/`ASTEST_COMPILE_RUN_*` 这类 wrapper-aware helper，避免手写引用管理。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 1 | Issue-80 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:58:03)

### 一、现有测试问题

#### Issue-79：`SubsystemScenarioTests` 把失败类型锁死为 `ECompileResult::Error`，对 `SoftReloadOnly` 过于脆弱

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `WorldSubsystem.Tick` / `WorldSubsystem.ActorAccess` / `GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 77-107, 124-150, 166-198, 214-245 |
| 问题描述 | 四条用例都通过 `CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)` 走软重载编译，然后把失败结果精确锁定成 `CompileResult == ECompileResult::Error`。这比“当前分支不支持 subsystem 脚本，应当编译失败”的真实合同更窄：一旦编译器把同类失败重新分类为 `ECompileResult::ErrorNeedFullReload`，测试虽然仍遇到合法失败，却会被误判成红灯。当前文件又没有补充错误文本断言，因此它既没有验证正确失败原因，也把内部分类细节绑成了外部合同。 |
| 影响 | subsystem feature gate、UHT/class-generation 相关错误类别只要发生实现层调整，4 条 scenario 用例就会因为枚举值漂移而失败，形成对编译器内部分类的脆弱耦合。这类失败并不代表用户面行为回归，却会给维护者制造噪音。 |
| 修复建议 | 把结果断言放宽为“失败且属于预期错误族”：保留 `bCompiled == false`，并把 compile-result 断言改成 `CompileResult == ECompileResult::Error || CompileResult == ECompileResult::ErrorNeedFullReload`。如果希望维持强度，应再配合 `AddExpectedError(...)` 或结构化错误码断言 subsystem 不支持的诊断文本，把合同从内部枚举细节迁回用户可观察的失败原因。 |

### 二、需要新增的测试

#### NewTest-71：给 `UAngelscriptAbilityTaskLibrary` 补齐 root-motion / targeting wrapper 的参数透传合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `ApplyRootMotionMoveToActorForce(...)` / `ApplyRootMotionMoveToTargetDataActorForce(...)` / `ApplyRootMotionMoveToForce(...)` / `MoveToLocation(...)` / `VisualizeTargetingUsingActor(...)` / `WaitTargetData(...)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-20` 只覆盖 `WaitDelay`、`WaitGameplayEvent`、`WaitGameplayTag*`、`WaitInput*`、`WaitTargetDataUsingActor` 等 wait/tag/input 类 wrapper；`AbilityTaskLibrary.h` 里整块 root-motion、move-to 和 targeting wrapper 仍然完全无测试命中。 |
| 风险评估 | 这些 wrapper 一旦把 `TaskInstanceName`、目标 actor、target-data 索引、movement mode 或 duration 透传错位，脚本端会表现成“task 创建成功但行为跑偏”，而现有自动化只会在更高层 gameplay 集成里迟到暴露，难以定位到 wrapper 本身。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.RootMotionAndTargetingWrappersPreserveRepresentativeArguments` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryTests.cpp` |
| 场景描述 | 建最小 GAS fixture：owner actor + `UAngelscriptAbilitySystemComponent` + native `UGameplayAbility` test double + 一个 `AGameplayAbilityTargetActor` test actor。分别调用 `ApplyRootMotionMoveToActorForce`、`ApplyRootMotionMoveToTargetDataActorForce`、`ApplyRootMotionMoveToForce`、`MoveToLocation`、`VisualizeTargetingUsingActor`、`WaitTargetData` 六个代表 wrapper，覆盖 actor-target、target-data actor、纯 location、可视化 targeting 和 confirmation-type 几类签名。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或现有 GAS fixture；`FGameplayAbilityTargetDataHandle` 样本；`AGameplayAbilityTargetActor` test double；一个 native access shim 读取任务上的 `InstanceName`、目标 actor、target-data index、location/duration/confirmation type 等关键参数。 |
| 期望行为 | 每个 wrapper 都必须返回非空 task，且 `GetClass()` 分别等于对应 native task 类；所有 task 的 owning ability 都等于传入 ability；`TaskInstanceName`、target actor、target-data 索引、目标位置、duration、movement-mode/confirmation-type 等关键参数必须与输入完全一致；`ApplyRootMotionMoveToTargetDataActorForce` 还应验证它消费的是 target-data 中指定的 actor，而不是错误索引或空目标。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / GAS fixture + target-data builder helper + `AGameplayAbilityTargetActor` test double + task access shim |
| 优先级 | P1 |

#### NewTest-72：给 `UAngelscriptAbilityTaskLibrary` 补齐 gameplay-effect watcher wrapper 的 self/target/query 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `WaitGameplayEffectAppliedToSelf(...)` / `WaitGameplayEffectAppliedToSelfQuery(...)` / `WaitGameplayEffectAppliedToTarget(...)` / `WaitGameplayEffectAppliedToTargetQuery(...)` / `WaitGameplayEffectBlockedByImmunity(...)` / `WaitForGameplayEffectRemoved(...)` / `WaitForGameplayEffectStackChange(...)` |
| 现有测试覆盖 | 目标测试目录和已记录建议里没有任何一条直接命中 `AbilityTaskLibrary` 的 gameplay-effect watcher wrapper；现有 GAS 建议更多聚焦 ASC 自身 API 与 `AbilityAsyncLibrary`，没有覆盖这组 task 工厂。 |
| 风险评估 | 这批 wrapper 如果把 self/target 方向、query/tag-requirements、`ExternalOwner`、`bTriggerOnce` 或 `FActiveGameplayEffectHandle` 路由错了，脚本端会创建到错误 task 或错误监听对象，最终表现成“效果事件不触发/触发到别的 ASC”，但当前自动化没有 wrapper 级保护。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.GameplayEffectWatchWrappersRouteOwnerFiltersAndHandles` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryTests.cpp` |
| 场景描述 | 搭一个双 ASC GAS fixture：source actor、target actor、owner ability、可选 immunity target，以及一个已应用的 active gameplay effect handle。分别调用 self/target 两组 `WaitGameplayEffectApplied*` wrapper、`WaitGameplayEffectBlockedByImmunity`、`WaitForGameplayEffectRemoved`、`WaitForGameplayEffectStackChange`。对 self/target wrapper 传入不同 `FGameplayTagRequirements` / `FGameplayTagQuery` / `ExternalOwner` / `bTriggerOnce`，对 removed/stack-change wrapper 传入同一个已知 active handle。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或现有 ASC fixture；一个能创建 `FActiveGameplayEffectHandle` 的最小 gameplay effect；source/target tag/query 样本；必要时引入 task access shim 读取 `ExternalOwner`、trigger-once、filter/query 与 handle 字段。 |
| 期望行为 | self/target 两组 wrapper 都必须返回对应的 native task class，owning ability 正确，且内部记录的 `ExternalOwner`、`bTriggerOnce`、tag requirements/query 与传入值一致；self 与 target 任务不得互相交换监听方向；`WaitForGameplayEffectRemoved` / `WaitForGameplayEffectStackChange` 必须保留原始 `FActiveGameplayEffectHandle`，后续驱动 handle 移除或 stack 变化时应只命中对应 task，不应误触发其他 watcher。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / 双 ASC GAS fixture + gameplay effect handle builder + task access shim |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-79 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:47:12)

### 记录校正

上一轮文末追加中的 `Issue-` 编号为空，属于本轮写入时的格式化失误，不作为有效编号。
对应有效发现以下述 `Issue-78` 为准，供后续轮次继续去重。

### 一、现有测试问题

#### Issue-78：`SoftReferenceCppForm` 只要求 `CppHeader` 非空，抓不住错误 include 映射

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.SoftReferenceCppForm` |
| 行号范围 | 417-449 |
| 问题描述 | 这条用例对 `TSoftObjectPtr<UTexture2D>` / `TSoftClassPtr<AActor>` 的 `GetCppForm()` 做了多项字符串断言，但落到 include 关键信息时只检查 `SoftObjectForm.CppHeader.IsEmpty() == false` 和 `SoftClassForm.CppHeader.IsEmpty() == false`。也就是说，只要返回任意一个非空 header 字符串，测试就会通过；即便回归把 `UTexture2D` 映射成错误的头文件、把 `AActor` 指到无关 include，当前用例也抓不住。 |
| 影响 | `SoftReferenceCppForm` 的核心价值是保护 StaticJIT/生成代码所依赖的 native type 形态。如果 `CppHeader` 错而不空，运行时 parity 测试仍是绿灯，但真正的 generated C++ 代码会在编译阶段才暴露问题，报警既晚也难定位。 |
| 修复建议 | 把 header 断言升级为精确匹配：至少对 `TSoftObjectPtr<UTexture2D>` 断言 `CppHeader` 包含或等于 `Engine/Texture2D.h`，对 `TSoftClassPtr<AActor>` 断言 `CppHeader` 包含或等于 `GameFramework/Actor.h`。如果 `GetCppForm()` 允许多头文件格式，建议把 `CppHeader` 解析成条目列表后按集合比较，而不是只看“非空”。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-78 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:02)

### 二、需要新增的测试

#### NewTest-52：给 `UAngelscriptGASAbility` 补齐 AS override 检测标志的构造期合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGASAbility.cpp` |
| 关联函数 | `UAngelscriptGASAbility::UAngelscriptGASAbility(...)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-09` 只覆盖 gameplay cue wrapper 转发；现有目标测试目录与 `AngelscriptRuntime/Tests` 中没有任何用例直接命中构造函数里对 `K2_ShouldAbilityRespondToEvent` / `K2_CanActivateAbility` / `K2_ActivateAbility` / `K2_ActivateAbilityFromEvent` 的 `ImplementedInAS(...)` 探测逻辑 |
| 风险评估 | 如果这四个 `bHasBlueprint*` 标志没有在 script-generated ability class 上正确置位，GAS runtime 会把真正由 Angelscript 覆写的 ability hook 当成“未实现”，导致事件响应、可激活判定或 activate 路径静默走错。这个问题通常不是编译时报错，而是 ability 明明有 override 却在运行时完全不被调度。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.Ability.OverrideDetectionFlagsTrackASImplementedHooks` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilityTests.cpp` |
| 场景描述 | 在测试文件里新增一个 native access shim `UAutomationGASAbilityTestAccess : UAngelscriptGASAbility`，暴露四个 `bHasBlueprint*` 标志的只读 getter。第一段用这个 native shim 自身做 control，验证纯 native class 默认四个标志都为 `false`。第二段在 clean engine 下编译一个脚本类 `UAutomationASGASAbility : UAutomationGASAbilityTestAccess`，分别在脚本中实现 `K2_ShouldAbilityRespondToEvent`、`K2_CanActivateAbility`、`K2_ActivateAbility`、`K2_ActivateAbilityFromEvent` 四个 override；拿到 generated class 的 CDO 或新实例后，通过 access shim getter 读取四个标志。第三段再编译一个只实现其中一部分 hook 的脚本类，验证 flags 只对已实现的 override 置位。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；`CompileAnnotatedModuleFromMemory(...)`；一个 native access shim class；必要时用 `FindGeneratedClass(...)` + `GetDefaultObject()` 读取 script-generated ability class |
| 期望行为 | native control class 的 `HasRespondToEventFlag()` / `HasCanActivateFlag()` / `HasActivateFlag()` / `HasActivateFromEventFlag()` 全为 `false`；实现全部四个 script hook 的 generated class 上四个标志全为 `true`；只实现部分 hook 的 generated class 只置位对应标志，其余保持 `false`；重复取 CDO/实例时标志保持稳定，证明构造期探测没有依赖一次性脏状态。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / `CompileAnnotatedModuleFromMemory(...)` / native access shim `UAutomationGASAbilityTestAccess` / `FindGeneratedClass(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |



---

## 测试审查 (2026-04-09 00:28:55)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 00:24:47` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-63` / `Issue-64` |
| 新增测试建议 | `NewTest-53` / `NewTest-54` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-64 |
| BadIsolation | 1 | Issue-63 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 00:48:01)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 00:43:19` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-65` / `Issue-66` |
| 新增测试建议 | `NewTest-55` / `NewTest-56` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-65 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 01:05:30)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 01:03:42` / `2026-04-09 00:56:13` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-67` |
| 新增测试建议 | `NewTest-57` / `NewTest-58` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-67 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 2 | MissingScenario: 1, MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 01:27:33)

### 一、现有测试问题

#### Issue-69：`GC` 组件场景只验证 `UActorComponent` 基类，没把 `UAngelscriptComponent` 身份建成合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.GC.ComponentDestroy` / `WorldTeardown` |
| 行号范围 | 25-54, 152-163, 223-236 |
| 问题描述 | `CreateGCScenarioScriptComponent(...)` 的模板默认参数是 `UActorComponent`，而 `ComponentDestroy` 与 `WorldTeardown` 两条用例都直接走这个默认类型。helper 最终只断言 `Cast<UActorComponent>(Component)` 成功；对于脚本里明确定义成 `UAngelscriptComponent` 的 `UScenarioGCComponentDestroy` / `UScenarioGCWorldTeardownComponent`，测试从未验证返回对象仍然具备 `UAngelscriptComponent` 身份，也没有检查任何 script-component 特有语义。这样一来，只要生成类还继承自 `UActorComponent` 并能被 UE GC 回收，即便 `UAngelscriptComponent` 相关的 runtime hook、引用处理或事件桥接已经退化，这两条 GC 场景仍可能继续绿灯。 |
| 影响 | 这会把 `GC` 目录本应保护的 “Angelscript component 生命周期” 退化成普通 UE component 销毁 smoke。若脚本组件回归成缺少 `UAngelscriptComponent` 关键行为的 generic component subclass，当前自动化无法发现，用户只会在脚本事件、引用收集或 RPC 等集成路径上遇到延迟故障。 |
| 修复建议 | 把 helper 的默认模板参数改成 `UAngelscriptComponent`，并让 `ComponentDestroy` / `WorldTeardown` 显式以 `CreateGCScenarioScriptComponent<UAngelscriptComponent>(...)` 调用；随后补一条强断言验证 `Component->IsA(UAngelscriptComponent::StaticClass())`。如果担心未来复用 helper 给非脚本 component，建议拆成专用 `CreateGCAngelscriptComponent(...)` helper，避免当前测试继续以过宽基类掩盖 script-component 回归。 |

### 二、需要新增的测试

#### NewTest-59：给 `FAngelscriptEngine` 的 current-context 静态 helper 补齐“优先 scoped engine、无 current 时回退 ambient”合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::TryGetCurrentWorldContextObject()` / `ShouldUseEditorScriptsForCurrentContext()` / `ShouldUseAutomaticImportMethodForCurrentContext()` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` 只在 `EngineScopeWorldContextRestore` 里顺带断言了一次 `TryGetCurrentWorldContextObject()` 能看到 scope 里的 dummy context；当前目标测试目录 `Core/Subsystem/GC/FileSystem/` 没有任何用例直接覆盖这 3 个静态 helper 的嵌套 scope 切换，也没有验证“无 current engine 时回退 ambient world context 且两个 bool helper 返回 false”的分支 |
| 风险评估 | 这 3 个 helper 是 runtime 随处可见的上下文入口。一旦 current-engine 栈恢复出错、ambient world context 漂移，或 `bUseEditorScripts` / automatic-import 的上下文选择拿错 engine，文件发现、热重载和脚本执行路径就会在 editor 与 runtime 间悄悄走错分支，当前 RuntimeCore 自动化没有任何定点报警 |
| 建议测试名 | `Angelscript.TestModule.Engine.CurrentContext.StaticHelpersPreferScopedEngineAndAmbientFallback` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCurrentContextTests.cpp` |
| 场景描述 | 在 `FCoreTestContextStackGuard` 下创建两台 isolated engine：outer engine 设 `SetUseEditorScriptsForTesting(true)`、`SetAutomaticImportMethodForTesting(true)` 并绑定 `OuterContext`；inner engine 设两项 flag 为 `false` 并绑定 `InnerContext`。先进入 outer scope，断言 3 个静态 helper 返回 outer world/true/true；再进入 inner nested scope，断言 helper 改为返回 inner world/false/false；退出 inner scope 后再次断言已恢复 outer 值。最后完全离开 engine scope，再用 `FAngelscriptGameThreadScopeWorldContext AmbientScope(FallbackContext)` 验证 `TryGetCurrentWorldContextObject()` 回退到 ambient context，而两个 bool helper 都返回 `false` |
| 输入/前置 | 复用 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` 里的 `FCoreTestContextStackGuard`；`AngelscriptTestSupport::CreateFullTestEngine()` 或等价 isolated engine helper；`SetUseEditorScriptsForTesting(...)` / `SetAutomaticImportMethodForTesting(...)`；`FAngelscriptEngineScope`；`FAngelscriptGameThreadScopeWorldContext`；`UAngelscriptNativeScriptTestObject` 作为 dummy world context |
| 期望行为 | outer scope 内 3 个 helper 必须命中 outer engine 与 `OuterContext`；nested scope 内必须完整切到 inner engine 与 `InnerContext`；退出 nested scope 后必须恢复 outer 结果，不能残留 inner flag 或 context；无 current engine 且 ambient scope 有效时，`TryGetCurrentWorldContextObject()` 必须返回 `FallbackContext`，而 `ShouldUseEditorScriptsForCurrentContext()` / `ShouldUseAutomaticImportMethodForCurrentContext()` 都必须为 `false` |
| 使用的 Helper | `FCoreTestContextStackGuard` + isolated engine helper + `FAngelscriptEngineScope` + `FAngelscriptGameThreadScopeWorldContext` + `UAngelscriptNativeScriptTestObject` |
| 优先级 | P2 |

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-69` |
| 新增测试建议 | `NewTest-59` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-69 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 14:09:34)

### 记录校正

本轮有效新增发现为：
- `Issue-85`
- `NewTest-88`

上述 2 条完整正文已写入本文前段，且前两次“文末补录”都命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-85 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 23:34:19)

### 记录校正

本轮有效新增发现为：
- `NewTest-89`
- `NewTest-90`
- `NewTest-91`

上述 3 条完整正文已写入本文前段，但第一次续写命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正的文末续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingScenario: 1, NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 23:51:11)

### 记录校正

本轮有效新增发现为：
- `Issue-86`
- `Issue-87`
- `NewTest-92`

上述 3 条完整正文已写入本文前段，但本次续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-87 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-86 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:07:07)

### 记录校正

本轮有效新增发现为：
- `Issue-88`
- `NewTest-93`

上述 2 条完整正文已写入本文前段，但续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 1 | Issue-88 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-10 00:25:13)

### 记录校正

本轮有效新增发现为：
- `Issue-89`
- `NewTest-94`

上述 2 条完整正文已写入本文前段，但此前两次续写都命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-89 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:07:07)

### 记录校正

本轮有效新增发现为：
- `Issue-88`
- `NewTest-93`

上述 2 条完整正文已写入本文前段，但续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 1 | Issue-88 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:03:30)

### 一、现有测试问题

#### Issue-88：多条 RuntimeCore 测试把 `DestroyGlobalEngine()` 当成基线清理，但它当前只是 no-op

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.LastFullDestroyClearsTypeState` / `FullDestroyAllowsCleanRecreate` / `FullDestroyAllowsAnnotatedRecreate` / `Angelscript.TestModule.Core.Performance.Startup.*` |
| 行号范围 | 156-279, 42-48 |
| 问题描述 | `EngineCoreTests` 三条 full-destroy/lifecycle 用例在 setup 和 `ON_SCOPE_EXIT` 中反复调用 `AngelscriptTestSupport::FAngelscriptTestEngineScopeAccess::DestroyGlobalEngine()`，`EnginePerformanceTests` 的 `ResetPerformanceEngineState()` 也在 42-48 行用同一 helper 试图清理基线。但它最终落到 `FAngelscriptEngine::DestroyGlobal()`；而 runtime 现实现于 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:776-779`，函数体只有 `return false;`，没有执行任何销毁动作。也就是说，这批测试表面上“先清 global engine 再开始”，实际上根本没有移除任何 global/current-engine 状态，只是在无条件继续跑后续断言。 |
| 影响 | 这些用例对隔离前提的判断是假的：如果前序测试或 runtime module 留下了 ambient current engine、subsystem-attached engine 或其他 global-like 状态，`DestroyGlobalEngine()` 不会把它们清掉。结果就是 lifecycle/performance 测试可能在脏环境上运行，同时还误以为自己已经建立了 clean baseline；一旦发生跨测试污染，当前失败信号会非常难归因。 |
| 修复建议 | 不要继续把 `DestroyGlobalEngine()` 当作已生效的清理动作。短期修复是先在测试里显式断言该 helper 的返回值，并在返回 `false` 时停止把“global 已清空”写进测试流程；同时统一改用真实可控的隔离手段，例如 `FCoreTestContextStackGuard` + `DestroySharedTestEngine()` + 专门的 runtime-module/override teardown helper。长期应补一个 `WITH_DEV_AUTOMATION_TESTS` 下可用的真实销毁 seam，或重构这些测试，使它们完全不依赖不存在的 global-engine destroy 语义。 |

### 二、需要新增的测试

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 1 | Issue-88 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 23:51:11)

### 记录校正

本轮有效新增发现为：
- `Issue-86`
- `Issue-87`
- `NewTest-92`

上述 3 条完整正文已写入本文前段，但本次续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-87 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-86 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 23:51:11)

### 一、现有测试问题

#### Issue-86：`CompileSnippet` / `ExecuteSnippet` 直接走 `asIScriptModule::CompileFunction(...)`，没有真正覆盖 `FAngelscriptEngine` 的编译管线

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.CompileSnippet` / `Angelscript.TestModule.Engine.ExecuteSnippet` |
| 行号范围 | 75-153 |
| 问题描述 | 两条用例虽然名义上属于 `EngineCore` 生命周期测试，但实际编译路径都直接调用 `Engine.GetScriptEngine()->GetModule(..., asGM_ALWAYS_CREATE)` 与 `Module->CompileFunction(...)`。这条路径只覆盖底层 AngelScript VM 的 raw function compile/execute，绕过了 `FAngelscriptEngine::CompileModules(...)`、`FAngelscriptPreprocessor`、module desc/filename 追踪、diagnostics 汇总以及 runtime helper 的标准入口。结果是：就算 RuntimeCore 自己的编译接线、预处理或 module registry 回归，只要底层 VM 还能编译一段裸函数，这两条测试仍会绿灯。 |
| 影响 | `EngineCoreTests` 看起来覆盖了“编译/执行 snippet”，但实际上没有保护插件真正对外暴露的编译管线。涉及 preprocessor、`CompileModuleFromMemory(...)`、module bookkeeping 或 compile diagnostics 的缺陷会直接漏报，误导对 `FAngelscriptEngine` 生命周期覆盖度的判断。 |
| 修复建议 | 把两条用例改成通过 runtime helper 驱动：优先用 `AngelscriptTestSupport::BuildModule(...)` / `CompileModuleFromMemory(...)` 或 `ASTEST_COMPILE_RUN_*` 宏，让脚本先进入 `FAngelscriptEngine` 的标准 compile pipeline，再取函数执行。`CompileSnippet` 至少要断言 module 可被 runtime helper 找回；`ExecuteSnippet` 则继续执行并断言返回值。若还想保留 raw `CompileFunction(...)` smoke，应单独迁到测试底层头文件/原生 API 边界的文件，不要继续冒充 `EngineCore` 编译合同。 |

#### Issue-87：`UnnamedBindBackwardCompatibility` 只要求“至少出现一个 `UnnamedBind_`”，抓不住重复注册或多余 auto-name

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.UnnamedBindBackwardCompatibility` |
| 行号范围 | 368-418 |
| 问题描述 | 这条用例先取 `BaselineBindNames`，再注册 1 个 unnamed bind，然后只做两层宽松判断：`NewBindNames` 非空，以及其中“存在某个名字以 `UnnamedBind_` 开头”。它没有断言本次注册恰好新增 1 个 bind，也没有验证 auto-generated name 与 `BindInfo` 是一一对应关系。若回归导致 unnamed bind 一次注册出多个条目、重复追加多份 `UnnamedBind_*`，甚至额外制造无关的 auto-name，当前测试仍会通过，因为它只需要在新增集合里找到任意 1 个前缀匹配项。 |
| 影响 | unnamed bind 的 backward-compatibility 合同会被放宽成“有东西长得像 unnamed bind 就行”。真正会污染 registry 的重复注册、计数膨胀或 auto-name 生成异常不会被定点拦截，后续只会以 bind 数量漂移或排序噪音的形式在别处间接暴露。 |
| 修复建议 | 把断言收紧到 cardinality 合同：先要求 `NewBindNames.Num() == 1`，再断言唯一新增名就是 `GeneratedUnnamedBindName` 且其前缀为 `UnnamedBind_`；随后补一条 `BindInfos` 计数断言，确保该名字在 `GetBindInfoList()` 中也只出现 1 次。若项目允许，再加一条 control，确认第二次再注册 unnamed bind 时只新增 1 个新的唯一名字，而不是复用或批量追加旧名字。 |

### 二、需要新增的测试

#### NewTest-92：给 `AAngelscriptGASCharacter::GetOwnedGameplayTags(...)` 补齐 `AbilitySystem == nullptr` 的 no-op 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGASCharacter.cpp` |
| 关联函数 | `AAngelscriptGASCharacter::GetOwnedGameplayTags(FGameplayTagContainer&) const` |
| 现有测试覆盖 | 当前文档里的 `NewTest-12` 只覆盖正向路径：`AbilitySystem` 存在、可复制 tag、`GetIsReplicated()==true`。现有目标测试目录和已记录建议都没有任何一条直接命中 `if (AbilitySystem)` 失败分支，也没有验证 ASC 缺席时该接口是否保持 no-op。 |
| 风险评估 | `AAngelscriptGASCharacter` 同时实现 `IGameplayTagAssetInterface` 和 ASC 桥接，`GetOwnedGameplayTags(...)` 常被外部 ability/query 代码在 actor 尚未完全初始化、组件被替换，或 teardown 末期调用。如果 null 分支未来误写入脏数据、清空调用方已有容器或直接解引用空 ASC，当前自动化不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASActorBase.CharacterOwnedTagsNoopWhenAbilitySystemMissing` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASActorBaseTests.cpp` |
| 场景描述 | 在 `GASActorBase` 测试文件里新增一个 native concrete probe class `AAutomationGASCharacterProbe : public AAngelscriptGASCharacter`。先用 `FActorTestSpawner` spawn probe character，给 `Character->AbilitySystem` 加一个 loose tag 作为正向 control，并确认 `GetOwnedGameplayTags(...)` 能返回该 tag。随后保存原始 `AbilitySystem` 指针，把 `Character->AbilitySystem = nullptr`，并预先往 `FGameplayTagContainer PreservedTags` 中塞入一个哨兵 tag；再次调用 `GetOwnedGameplayTags(PreservedTags)`。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或现有 `FActorTestSpawner` fixture；native probe character；正向 control tag `GameplayTag.Tests.GASCharacterOwned`；哨兵 tag `GameplayTag.Tests.Preserved`；`ON_SCOPE_EXIT` 恢复原始 `AbilitySystem` 指针。 |
| 期望行为 | 正向 control 段里，`GetOwnedGameplayTags(...)` 必须返回包含 `GameplayTag.Tests.GASCharacterOwned` 的容器。将 `AbilitySystem` 置空后再次调用时，不得崩溃，不得把 `GameplayTag.Tests.GASCharacterOwned` 误写入结果，也不得清空或改写调用前已存在的 `GameplayTag.Tests.Preserved`；换句话说，null-ASC 分支应保持严格 no-op。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `FActorTestSpawner` + native `AAutomationGASCharacterProbe` + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-87 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-86 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 23:34:19)

### 一、现有测试问题

本轮未新增现有测试质量问题；继续扫描 `AngelscriptRuntime/Core/` 时，新发现集中在当前文档尚未覆盖的 runtime-module ambient-engine 路径与 bind-database / subsystem world 入口合同。

### 二、需要新增的测试

#### NewTest-89：覆盖 `InitializeAngelscript()` 采用 ambient current engine、且不偷建 `OwnedPrimaryEngine` 的路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 关联函数 | `FAngelscriptRuntimeModule::InitializeAngelscript()` |
| 现有测试覆盖 | 当前文档里的 `NewTest-22` 只覆盖 testing override push/reset，`NewTest-69` 覆盖 `StartupModule()` 启动门控，`NewTest-66` 覆盖 `ShutdownModule()` 清理；没有任何一条直接验证“已有 ambient current engine 时，runtime module 应初始化它而不是新建 owned engine”这条常规初始化分支。 |
| 风险评估 | 如果 `InitializeAngelscript()` 忽略了 `TryGetCurrentEngine()`、错误创建 `OwnedPrimaryEngine`，或在已有 ambient engine 时重复 push context stack，runtime module 会把 subsystem/外层 fixture 挂着的主引擎静默替换成另一台 owned engine；后续 `TryGetCurrentEngine()`、fallback tick 和 shutdown 清理都会在错误的引擎对象上运行，属于高影响生命周期回归。 |
| 建议测试名 | `Angelscript.TestModule.Engine.RuntimeModule.InitializeAdoptsAmbientEngineWithoutOwningIt` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp` |
| 场景描述 | 先用 `AngelscriptTestSupport::CreateFullTestEngine()` 创建一台 isolated full engine，并在局部 `FAngelscriptEngineScope` 中把它设成 ambient current engine。随后调用 `FAngelscriptRuntimeModule::ResetInitializeStateForTesting()` 清空基线，再对白盒 access shim 暴露的 `InitializeAngelscript()` 与 `OwnedPrimaryEngine` 做检查：在 ambient engine 仍存活的同一作用域里执行一次初始化，然后立刻快照 `FAngelscriptEngineContextStack`。最后退出作用域并再次 reset。 |
| 输入/前置 | `AngelscriptTestSupport::CreateFullTestEngine()`；`FAngelscriptEngineScope`；`FAngelscriptRuntimeModuleTickTestAccess` 或等价 friend access shim，允许读取 `OwnedPrimaryEngine` 与 `bInitializeAngelscriptCalled`；`FAngelscriptEngineContextStack::SnapshotAndClear()` scoped guard；`ON_SCOPE_EXIT` 恢复 initialize state 与 context stack。 |
| 期望行为 | 初始化后 `FAngelscriptEngine::TryGetCurrentEngine()` 仍必须等于 ambient full engine；`OwnedPrimaryEngine` 必须保持 `nullptr`；`bInitializeAngelscriptCalled == true`；`SnapshotAndClear()` 看到的栈深度不能比进入前多出额外 runtime-module push。若随后调用 `ShutdownModule()` 或 `ResetInitializeStateForTesting()`，也不应把 ambient engine 从外层 scope 中意外弹栈。这样才能证明 runtime module 在“已有 current engine”场景下真正采用了现有引擎，而不是偷偷切到 owned-engine 路径。 |
| 使用的 Helper | `AngelscriptTestSupport::CreateFullTestEngine()` + `FAngelscriptEngineScope` + `FAngelscriptRuntimeModuleTickTestAccess` + `FAngelscriptEngineContextStack` scoped guard + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

#### NewTest-90：给 `FAngelscriptBindDatabase::Get()` 建立“当前 engine 优先、无 current engine 时回退 legacy singleton”的地址级合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` |
| 关联函数 | `FAngelscriptBindDatabase::Get()` |
| 现有测试覆盖 | 当前文档里的 `NewTest-11` 只覆盖 `Save(...)` / `Load(...)` / `Clear()` / `GetSourceHeader(...)` 的持久化行为；目标测试目录和已记录建议都没有任何一条直接验证 `Get()` 在 engine-owned shared state、clone-shared state 与“无 current engine” legacy fallback 三种上下文下究竟返回哪一份数据库实例。 |
| 风险评估 | `FAngelscriptBindDatabase::Get()` 是 cooked bind cache 的全局入口。若它在 current-engine 场景下错误回退到 legacy singleton，或在 clone/full engine 切换时返回错 shared database，脚本绑定会静默读取到别的引擎 epoch 留下的缓存；表现出来通常是 class/struct bind 串台、跨测试污染或 cooked/runtime 绑定错位，而且很难定位到数据库选择逻辑。 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindDatabase.GetPrefersCurrentEngineSharedDatabaseAndFallsBackToLegacySingleton` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindDatabaseTests.cpp` |
| 场景描述 | 第一段在无 current engine 的基线下调用两次 `FAngelscriptBindDatabase::Get()`，记录 legacy 地址并清空其内容。第二段创建 isolated full engine `EngineA`，在 `FAngelscriptEngineScope ScopeA(*EngineA)` 内同时取 `&FAngelscriptBindDatabase::Get()` 与 `&EngineA->GetBindDatabaseForTesting()`，要求两者同址，并写入一个哨兵 `FAngelscriptClassBind`。第三段在 `ScopeA` 仍生效时创建 clone engine `EngineB`，进入 `ScopeB(*EngineB)` 后再次取 `Get()`，要求地址仍等于 `EngineA` 的 database，证明 clone 共享同一份 bind database。第四段销毁 A/B 后新建另一台 isolated full engine `EngineC`，要求 `Get()` 地址与 `EngineA` 不同且不继承 A 写入的哨兵。最后回到无 current engine 基线，再次验证 `Get()` 回到最初的 legacy 地址。 |
| 输入/前置 | `AngelscriptTestSupport::CreateFullTestEngine()`；`FAngelscriptEngine::CreateForTesting(..., EAngelscriptEngineCreationMode::Clone)` 或等价 clone helper；`FAngelscriptEngineScope`；一个轻量哨兵 `FAngelscriptClassBind`；`ON_SCOPE_EXIT` 对 legacy / engine-owned database 全部执行 `Clear()`，避免跨测试残留。 |
| 期望行为 | 无 current engine 时两次 `Get()` 必须返回同一 legacy singleton 地址；`ScopeA` 内的 `Get()` 必须与 `EngineA->GetBindDatabaseForTesting()` 同址，且可观察到写入的哨兵；`ScopeB` 内的 `Get()` 必须与 `EngineA` 地址一致，证明 clone 共享 source engine 的 bind database；`EngineC` 内的 `Get()` 必须是另一份新地址，且 `Classes/Structs` 不继承 A/B 的哨兵；退出所有 scope 后 `Get()` 必须回到最初的 legacy 地址。这样才能把 bind database 选择逻辑的 ambient-engine 合同固定下来。 |
| 使用的 Helper | `CreateFullTestEngine()` + clone engine helper + `FAngelscriptEngineScope` + 轻量 `FAngelscriptClassBind` sentinel + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

#### NewTest-91：覆盖 `GetTickableGameObjectWorld()` 的 owning-GameInstance world 解析合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 关联函数 | `UAngelscriptGameInstanceSubsystem::GetTickableGameObjectWorld() const` |
| 现有测试覆盖 | 当前文档里的 `NewTest-10` / `NewTest-67` / `NewTest-88` 分别覆盖 `Initialize/Deinitialize/Tick/GetCurrent()` 正向生命周期、tickability gate 和 `GetCurrent()` null guard；没有任何一条直接固定 `GetTickableGameObjectWorld()` 是否始终返回 owning `UGameInstance` 的 world，以及在 CDO / 无 `GameInstance` 上是否稳定返回 `nullptr`。 |
| 风险评估 | `GetTickableGameObjectWorld()` 是 tickable subsystem 向 UE tick 框架声明“我属于哪一个 world”的入口。若它开始返回错误 world、在脱离 `GameInstance` 后仍返回悬空 world，或 CDO 上不再为 null，多世界 PIE / editor 场景下的 tick 路由与统计归属会悄悄漂移，而当前自动化没有任何定点保护。 |
| 建议测试名 | `Angelscript.TestModule.GameInstanceSubsystem.TickableWorldTracksOwningGameInstance` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptGameInstanceSubsystemRuntimeTests.cpp` |
| 场景描述 | 复用 `NewTest-10` 计划中的真实 `UGameInstance` / `UWorld` fixture。第一段直接检查 `GetDefault<UAngelscriptGameInstanceSubsystem>()`，确认 CDO 的 `GetTickableGameObjectWorld()` 为 `nullptr`。第二段创建 live `UGameInstance` + `UWorld`，初始化 subsystem 后调用 `GetTickableGameObjectWorld()`，并与 `Subsystem->GetGameInstance()->GetWorld()` 做同址比较。第三段在 `Deinitialize()` 或 fixture teardown 后再次读取，确认不会继续返回旧 world。 |
| 输入/前置 | `UGameInstance` / `UWorld` runtime fixture；`UAngelscriptGameInstanceSubsystem` live instance；必要时一个 access shim 允许在 teardown 后安全读取状态；`ON_SCOPE_EXIT` 恢复 ambient world context 与任何 ActiveTickOwners 改动。 |
| 期望行为 | CDO case 必须返回 `nullptr`；live subsystem case 必须返回与 `GameInstance->GetWorld()` 完全相同的 `UWorld*`；`Deinitialize()` 或 fixture 释放后，再次读取必须返回 `nullptr` 或不可再解析出旧 world，不能保留悬空指针。这样才能把 tickable world 归属合同单独固定下来，而不是只在更大的生命周期测试里间接碰到。 |
| 使用的 Helper | `NewTest-10` 的 `UGameInstance` / `UWorld` fixture + 可选 subsystem access shim + `ON_SCOPE_EXIT` |
| 优先级 | P3 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingScenario: 1, NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 14:07:49)

### 记录校正

本轮有效新增发现为：
- `Issue-85`
- `NewTest-88`

上述 2 条完整正文已写入本文前段，但本次续写命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-85 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 14:04:31)

### 一、现有测试问题

#### Issue-85：`SubsystemScenarioTests` 只检查“编译失败”，没有验证失败后不会残留 module/class

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `WorldSubsystem.Tick` / `WorldSubsystem.ActorAccess` / `GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 77-108, 124-150, 166-198, 214-245 |
| 问题描述 | 四个用例在 `CompileModuleWithResult(...)` 返回 `false` 后，只断言 `CompileResult == ECompileResult::Error`。它们没有检查失败编译后 `Engine.GetModule(...)` / `GetModuleByFilenameOrModuleName(...)` 是否仍为空，也没有验证 `FindGeneratedClass(...)` 对 `UScenarioWorldLifecycleTracker`、`UScenarioWorldTicker`、`UScenarioWorldActorWatcher`、`AScenarioWorldSubsystemActorAccessActor`、`UScenarioGameInstanceLifecycleTracker` 都返回空。若 class generator 或 module registry 在报错前已经留下半注册状态，这组测试仍会全部绿灯。 |
| 影响 | subsystem 当前本来就在验证“不支持的脚本 subsystem 应该失败”。如果失败路径开始残留 ghost module、generated class 或 filename 映射，后续测试会被污染，但当前文件完全发现不了；它只能证明“最终返回了 error”，不能证明“error 之前没有泄漏运行时状态”。 |
| 修复建议 | 在每条失败断言后追加负向 cleanup 合同：1. `TestFalse(..., Engine.GetModule(*ModuleName.ToString()).IsValid())`；2. `TestFalse(..., Engine.GetModuleByFilenameOrModuleName(Filename, *ModuleName.ToString()).IsValid())`；3. 对脚本里声明的每个预期 generated type 调 `FindGeneratedClass(&Engine, TEXT("..."))` 并断言为空。必要时再检查 `Engine.GetActiveModules()` 不包含该 module，确保失败路径不会留下可观察的半成品。 |

### 二、需要新增的测试

#### NewTest-88：给 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 补齐 ambient-world / no-GameInstance null guard 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 关联函数 | `UAngelscriptGameInstanceSubsystem::GetCurrent()` |
| 现有测试覆盖 | 当前文档里的 `NewTest-10` 只覆盖 `Initialize(...)` / `Deinitialize()` / `Tick(...)` / `GetCurrent()` 的正向生命周期场景；目标测试目录的现有 `Subsystem` 测试仍然全部停在 compile-fail smoke，没有任何一条直接命中 `GetCurrent()` 的 `World == nullptr` 与 `GameInstance == nullptr` 两个 early-return 分支。 |
| 风险评估 | `GetCurrent()` 是 RuntimeCore 里多处“有没有 subsystem owner”判断的基础入口。若 ambient world context 解析错误、world 没有 `UGameInstance` 时仍返回悬空 subsystem，或 null guard 被破坏，后续 `TryGetCurrentEngine()`、fallback tick 门控和脚本运行时世界解析都会在错误 subsystem 上继续执行，问题通常表现成偶发串测或 editor 下脏 current-context。 |
| 建议测试名 | `Angelscript.TestModule.GameInstanceSubsystem.GetCurrentReturnsNullWithoutAmbientWorldOrGameInstance` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptGameInstanceSubsystemRuntimeTests.cpp` |
| 场景描述 | 复用 `NewTest-10` 计划中的 world/game-instance runtime fixture。第一段用 `FAngelscriptGameThreadScopeWorldContext` 或 `FScopedTestWorldContextScope` 把 ambient world context 设成 `nullptr` 或一个不解析出 `UWorld` 的 dummy `UObject`，直接调用 `GetCurrent()`；第二段创建一个临时 `UWorld` 但不挂 `UGameInstance`，把该 world 设为 ambient context 后再次调用 `GetCurrent()`；第三段作为 control，创建带 `UGameInstance` 的有效 world，并显式获取 `UAngelscriptGameInstanceSubsystem` 后再次调用 `GetCurrent()`。 |
| 输入/前置 | `FCoreTestContextStackGuard` 或等价 scoped guard；`FAngelscriptGameThreadScopeWorldContext` / `AngelscriptTestSupport::FScopedTestWorldContextScope`；一个 dummy `UAngelscriptNativeScriptTestObject`；一个无 `UGameInstance` 的临时 `UWorld`；一个正常 world + `UGameInstance` fixture。 |
| 期望行为 | dummy/null ambient context case 下 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 必须返回 `nullptr`；world 存在但 `World->GetGameInstance() == nullptr` 的 case 也必须返回 `nullptr`；只有在有效 world + game instance + subsystem 已创建的 control case 下，`GetCurrent()` 才返回与 `GameInstance->GetSubsystem<UAngelscriptGameInstanceSubsystem>()` 相同的指针。这样才能把 `GetCurrent()` 的 ambient-world 解析和 null guard 合同独立固定下来。 |
| 使用的 Helper | world/game-instance runtime fixture + `FAngelscriptGameThreadScopeWorldContext` / `FScopedTestWorldContextScope` + `UAngelscriptNativeScriptTestObject` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-85 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 01:49:50)

### 二、需要新增的测试

#### NewTest-61：给 `UAngelscriptAbilityTask` 补 waiting flag 与 `AbilitySystemComponent` proxy 的 round-trip 场景

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTask.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTask.cpp` |
| 关联函数 | `UAngelscriptAbilityTask::BP_SetAbilitySystemComponent(UAbilitySystemComponent*)` / `BP_GetAbilitySystemComponent()` / `BP_SetWaitingOnRemotePlayerData()` / `BP_ClearWaitingOnRemotePlayerData()` / `BP_SetWaitingOnAvatar()` / `BP_ClearWaitingOnAvatar()` / `IsWaitingOnRemotePlayerdata()` / `IsWaitingOnAvatar()` |
| 现有测试覆盖 | 当前文档里的 `NewTest-08` 只覆盖 `CreateAbilityTask(...)` / `CreateAbilityTaskAndRunIt(...)` 生命周期，以及 `SetIsTickingTask` / `SetIsPausable` / `SetIsSimulatedTask` 的基本 getter round-trip；目标测试目录源码检索也没有任何现有用例直接命中 waiting flag 或 `BP_GetAbilitySystemComponent()` 这组公开 proxy。 |
| 风险评估 | 这些 API 是脚本侧直接可见的 `UAngelscriptAbilityTask` 合同；一旦 waiting 状态不再透传到 `UAbilityTask` 基类，或者 `BP_SetAbilitySystemComponent(...)` / `BP_GetAbilitySystemComponent()` 不再保持同一个 ASC，脚本 task 会表现成“看起来创建成功，但网络等待状态错误、delegate 广播条件异常或使用了错误 ASC”，当前自动化没有定位点。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilityTask.WaitingFlagsAndAbilitySystemProxyRoundTrip` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilityTaskTests.cpp` |
| 场景描述 | 基于已有 GAS fixture 创建 source ASC、secondary ASC、native `UGameplayAbility` 实例和一个普通 `UAngelscriptAbilityTask`。先调用 `BP_SetAbilitySystemComponent(SecondaryAsc)` 并验证 `BP_GetAbilitySystemComponent()` 返回同一指针。然后分别走 remote-player-data 与 avatar 两组 waiting flag：初始应为 false；调用 `BP_SetWaitingOnRemotePlayerData()` 后，`BP_IsWaitingOnRemotePlayerdata()` 与虚函数 `IsWaitingOnRemotePlayerdata()` 都变为 true；`BP_ClearWaitingOnRemotePlayerData()` 后恢复 false。avatar 标志重复同样流程，并额外断言清理 remote flag 不会意外影响 avatar flag，反之亦然。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或同级 GAS fixture；一个可完成 actor info 初始化的 source ASC/ability；一个额外创建的 `UAbilitySystemComponent` 作为 secondary ASC；必要时加一个轻量 access shim 暴露 `IsWaitingOnRemotePlayerdata()` / `IsWaitingOnAvatar()` 以便 native 断言。 |
| 期望行为 | `BP_SetAbilitySystemComponent(SecondaryAsc)` 后 `BP_GetAbilitySystemComponent() == SecondaryAsc`；两组 waiting flag 在初始状态均为 false，`Set` 后仅对应 flag 变 true，`Clear` 后恢复 false；`BP_IsWaitingOnRemotePlayerdata()` 与 `IsWaitingOnRemotePlayerdata()`、`BP_IsWaitingOnAvatar()` 与 `IsWaitingOnAvatar()` 的结果始终一致；remote/avatar 两个状态位互不串扰。 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS ASC/ability fixture helper + 必要的 native access shim |
| 优先级 | P2 |

### 本轮汇总（真正文件尾部补录；承接上方 `2026-04-09 01:42:03` 区块并补充本节 `NewTest-61`）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-70` |
| 新增测试建议 | `NewTest-60` / `NewTest-61` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-70 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 2 | MissingErrorPath: 1, MissingScenario: 1 |
| P3 | 0 | 无 |

## 测试审查 (2026-04-09 01:42:03)

### 一、现有测试问题

#### Issue-70：`BindConfig` 两条 disabled-name 用例把未初始化的临时 engine 包进 `FAngelscriptEngineScope`，helper 选择与场景不匹配

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.GlobalDisabledBindNames` / `EngineDisabledBindNames` |
| 行号范围 | 293-297, 352-357 |
| 问题描述 | 两条用例在验证 `CollectDisabledBindNames()` 时，都先栈上构造 `FAngelscriptEngine Engine(Config, Dependencies)`，随后立刻创建 `FAngelscriptEngineScope EngineScope(Engine)`。但 runtime 实现里 `CollectDisabledBindNames()` 只是读取 `ConfigSettings->DisabledBindNames` 与 `RuntimeConfig.DisabledBindNames` 做集合合并，并不依赖 current engine；相反，`FAngelscriptEngineScope` 会在构造时把这台未初始化 engine `Push` 到 `FAngelscriptEngineContextStack`，并同步 ambient world context。也就是说，测试为了调用一个纯配置 merge helper，额外篡改了全局 current-engine 解析链。 |
| 影响 | 这让本来应是纯数据断言的测试，平白依赖 `FAngelscriptEngineScope` 的生命周期副作用；若该区间内新增任何 helper、日志或错误路径去查询 `TryGetCurrentEngine()` / world context，就可能读到一台从未初始化的临时 engine，制造与 bind-config 合同无关的串测风险。更直接地说，这两条用例并没有正确体现“什么时候该用 `FAngelscriptEngineScope`”。 |
| 修复建议 | 删掉这两处 `FAngelscriptEngineScope EngineScope(Engine)`，直接对局部 `Engine` 调 `CollectDisabledBindNames()` 即可；因为当前断言只关心 config merge，不需要 current-engine 语义。若后续确实需要带 current-engine 的 bind 行为，应改成创建完整、已初始化的 clean test engine 再入 scope，而不是把未初始化 wrapper 推进 `ContextStack`。 |

### 二、需要新增的测试

#### NewTest-60：覆盖 `OnAttributeSetRegistered(...)` 的 null-listener / missing-UFUNCTION 错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `UAngelscriptAbilitySystemComponent::OnAttributeSetRegistered(UObject*, FName)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-02` 只覆盖 `RegisterAttributeSet(...)` 的“去重 + 已有 set 立即回放”正向场景；`NewTest-29` 只覆盖 attribute-change callback。目标测试目录和已记录建议里，没有任何一条直接触发 `OnAttributeSetRegistered(...)` 的 `InObject == nullptr` 或 `FindFunction(...) == nullptr` 分支。 |
| 风险评估 | 这两个分支当前只打 `LogTemp Error` 后返回；若回归成“错误 listener 仍被加入 delegate”“立即回放时对坏对象 `ProcessEvent`”或文案静默变化，用户只会在运行时遇到重复回调、隐性 no-op 或崩溃，而自动化没有定位点。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.OnAttributeSetRegisteredRejectsInvalidListeners` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemCallbackTests.cpp` |
| 场景描述 | 创建最小 ASC fixture 与一个 test `UAngelscriptAttributeSet`。第一段直接调用 `OnAttributeSetRegistered(nullptr, TEXT("HandleRegisteredSet"))`；第二段传入一个存在但不含对应 `UFUNCTION` 的 dummy `UObject`；两段都用 `AddExpectedError(...)` 精确匹配 `Null object passed to AddUFunction.` 和 `Could not find function in object with this name. Is it declared UFUNCTION()?`。随后再绑定一个带有效 `UFUNCTION void HandleRegisteredSet(UAngelscriptAttributeSet* Set)` 的 recorder object，并执行一次 `RegisterAttributeSet(...)` 作为 control。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或现有 GAS ASC fixture；一个 `UAutomationAttributeSetRegisteredRecorder` 记录 `CallCount` 与最后一次 `UAngelscriptAttributeSet*`；一个没有目标 `UFUNCTION` 的 dummy `UObject`；`AddExpectedError(...)` 捕获两条错误日志。 |
| 期望行为 | null-listener 与 missing-function 两条调用都必须仅记录 1 次预期错误、不能崩溃，也不能让后续有效 listener 多收到回调；control 阶段第一次 `RegisterAttributeSet(TestSetClass)` 后，valid recorder 的 `CallCount == 1` 且 `LastSet` 等于返回的 attribute set；重复执行错误路径后再次注册同类 set，不会产生额外重复绑定或脏 delegate。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / 现有 GAS ASC fixture + `AddExpectedError(...)` + `UAutomationAttributeSetRegisteredRecorder` |
| 优先级 | P2 |

---

## 测试审查 (2026-04-09 01:15)

### 一、现有测试问题

#### Issue-68：6 条 `GeneratedFunctionTable` 报表/产物测试把 UHT 目录硬编码成 `Win64/UnrealEditor`，对 host 平台和目标配置产生无关耦合

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.EditorOutputsUseWithEditorGuard` / `SummaryOutput` / `CsvOutput` / `SkippedCsvOutput` / `SkippedReasonSummaryCsvOutput` / `MacroQualifiedDirectBindings` |
| 行号范围 | 244-247, 461-464, 596-599, 671-674, 708-711, 754-757 |
| 问题描述 | 这 6 条用例都通过 `FPaths::Combine(..., TEXT("Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT"))` 直接拼出生成目录。测试目标本来是验证 UHT 产物内容与报表一致性，但这里把 host 架构和 target 名称写死成 `Win64/UnrealEditor`。一旦本地/CI 改到其他平台、其他 editor target 名或不同 build 布局，测试会因为找不到目录直接失败，即便 generated function table 本身完全正确。 |
| 影响 | 这会把“产物位置变化”误报成“generated function table 回归”，降低报表类测试的可移植性和可信度；后续若引入多平台 CI、切换到非 `Win64` 主机，或中间目录布局调整，这 6 条用例会整批假红。 |
| 修复建议 | 把 UHT 目录解析抽成共享 helper，不要硬编码 `Win64/UnrealEditor`。优先从 `ProjectPluginsDir()/Angelscript/Intermediate/Build` 下递归搜索包含 `AS_FunctionTable_Summary.json` 或 `AS_FunctionTable_*.cpp` 的实际 UHT 目录；如果项目已有 build-target helper，则改为基于当前 platform/target name 组装路径。所有报表类用例统一复用这一个 resolver，并在找不到目录时输出“当前 build layout 未生成 UHT 产物”的精确信息，而不是把平台路径写死在每条测试里。 |

---

## 测试审查 (2026-04-09 01:05:30)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 01:03:42` / `2026-04-09 00:56:13` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-67` |
| 新增测试建议 | `NewTest-57` / `NewTest-58` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-67 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 2 | MissingScenario: 1, MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 01:03:42)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 00:56:13` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-67` |
| 新增测试建议 | `NewTest-57` / `NewTest-58` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-67 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 2 | MissingScenario: 1, MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:56:13)

### 一、现有测试问题

#### Issue-67：`WorldTeardown` 把 world scope 析构与 `CollectGarbage()` 混在同一个断言里，无法证明 GC 真正负责回收

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.GC.WorldTeardown` |
| 行号范围 | 208-236 |
| 问题描述 | 这条用例在 211-231 行把 `FActorTestSpawner`、world、actor 和 component 都包在局部作用域里，离开作用域后才执行一次 `CollectGarbage(RF_NoFlags, true)`。也就是说，`WeakWorld` / `WeakActor` / `WeakComponent` 失效的直接前置条件既可能是 world teardown、`FActorTestSpawner` 析构里的销毁逻辑，也可能是后续 GC；测试本身没有在 GC 前做一次中间断言，因此根本分不清“对象是在 scope teardown 阶段就已经被释放”还是“确实要靠 GC 才完成回收”。 |
| 影响 | 这条用例名义上在验证 `GC.WorldTeardown`，实际上只能证明“离开 spawner 作用域后这些弱指针最终失效”。如果 world teardown 路径已经提前销毁对象、而 GC callback 或 Angelscript 引用图完全没起作用，当前测试仍会稳定绿灯，无法回答“是否真正触发了 GC 并验证其回收行为”这个核心问题。 |
| 修复建议 | 把 world teardown 场景拆成两阶段断言：先在离开 `FActorTestSpawner` 作用域后、调用 `CollectGarbage()` 之前记录一次对象状态，明确验证哪些对象仍因 script 引用或 pending-kill 状态存在；随后再执行 `CollectGarbage()` 并比较前后变化。若当前设计预期 world teardown 本身就应立即清空对象，也应把测试名和断言改成 teardown 合同，不要继续把它包装成 GC 回收测试。 |

### 二、需要新增的测试

#### NewTest-57：覆盖 `UAngelscriptTestCommandlet::Main()` 的成功返回码与 passing module 选择合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp` |
| 关联函数 | `UAngelscriptTestCommandlet::Main(const FString& Params)` |
| 现有测试覆盖 | 当前文档只在 `NewTest-21` / `NewTest-51` 建议覆盖返回 `1` 和 `2` 的失败分支；正常情况下 unit tests 通过后返回 `0` 的主路径仍完全无覆盖 |
| 风险评估 | 如果 commandlet 在“初始编译成功 + unit tests 全绿”时仍错误返回非 `0`，CI 会把成功轮次误判成失败；反过来，如果 `RunAngelscriptUnitTests(...)` 根本没有跑到目标 passing module 就直接返回 `0`，当前也没有测试能发现成功路径实际上漏跑了脚本单测 |
| 建议测试名 | `Angelscript.TestModule.Core.Commandlet.TestCommandlet.PassingUnitTestsReturnZero` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestCommandletTests.cpp` |
| 场景描述 | 在 isolated full 或 production-like engine 下编译一个最小 passing module，例如声明 `void Test_CommandletPass(FUnitTest T) {}` 或等价不会调用 `Fail(...)` 的 unit test 入口；确认该 module 已进入 `GetActiveModules()` 且带有 1 条 unit-test 描述后，创建 `UAngelscriptTestCommandlet` 调用 `Main(TEXT(""))` |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::ProductionLike)` 或 `CreateFullTestEngine()` + `FAngelscriptEngineScope`；一个最小 passing script test module；必要时 scoped helper 暂存并恢复 unit-test 命名配置 |
| 期望行为 | `Main()` 返回 `0`；调用前后的 active module 集合保持同一 module 仍在；passing module 的 unit-test 描述数保持为 `1`，证明 commandlet 没有在成功路径上跳过模块发现或提前 short-circuit；若环境开启 editor-only struct 检查，本用例也应保持该检查返回 `0` |
| 使用的 Helper | `FAngelscriptTestFixture` + `CompileModuleFromMemory(...)` / `CompileModuleWithSummary(...)` + scoped unit-test settings restore |
| 优先级 | P2 |

#### NewTest-58：覆盖 `UAngelscriptTestCommandlet::Main()` 的 editor-only 未初始化 struct 返回码 `3`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp` |
| 关联函数 | `UAngelscriptTestCommandlet::Main(const FString& Params)` |
| 现有测试覆盖 | 当前文档只建议覆盖 `return 1` 和 `return 2`；`#if WITH_EDITOR` 下 `FStructUtils::AttemptToFindUninitializedScriptStructMembers() != 0` 时的 `return 3` 分支没有任何测试计划 |
| 风险评估 | 如果 editor-only 未初始化 struct 检查被静默绕过，commandlet 会在存在明显脚本 struct 初始化缺陷时仍返回成功；这类问题往往只在编辑器验证链上暴露，当前没有自动化出口码保护 |
| 建议测试名 | `Angelscript.TestModule.Core.Commandlet.TestCommandlet.UninitializedStructCheckReturnsThree` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestCommandletTests.cpp` |
| 场景描述 | 在 editor automation 下给 `UAngelscriptTestCommandlet` 增加一个测试 seam：优先通过 `WITH_DEV_AUTOMATION_TESTS` access shim 或静态 override helper 替换 `FStructUtils::AttemptToFindUninitializedScriptStructMembers()` 的返回值。测试里保持 `bDidInitialCompileSucceed = true`，并让 `RunAngelscriptUnitTests(...)` 走 passing module 路径，然后把 struct-check override 设成 `1` 再调用 `Main(TEXT(""))` |
| 输入/前置 | editor-only 环境；`FAngelscriptTestFixture` 或 isolated full engine；一个最小 passing unit-test module；`FAngelscriptTestCommandletAccess` 或等价的 struct-check override helper；`ON_SCOPE_EXIT` 恢复 override |
| 期望行为 | 在 passing unit-test 前置成立的情况下，`Main()` 必须返回 `3`；恢复 override 后同一 fixture 再调用一次 `Main(TEXT(""))` 应返回 `0`，证明 `return 3` 真正来自 editor-only struct 检查而不是其他失败分支；整个过程中 active modules 与 initial-compile 标志保持不变 |
| 使用的 Helper | `FAngelscriptTestFixture` + 最小 passing unit-test module + `FAngelscriptTestCommandletAccess`/测试 override helper + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

---

## 测试审查 (2026-04-09 00:48:01)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 00:43:19` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-65` / `Issue-66` |
| 新增测试建议 | `NewTest-55` / `NewTest-56` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-65 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:43:19)

### 一、现有测试问题

#### Issue-65：`FullDestroyAllowsCleanRecreate` 只检查第一次 full-destroy 的清理结果，没有验证重建后的第二个 epoch 也能干净收尾

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.FullDestroyAllowsCleanRecreate` |
| 行号范围 | 210-261 |
| 问题描述 | 这条用例会在第一轮 `FirstEngine.Reset()` 后显式断言 `FAngelscriptType::GetTypes().Num() == 0`，然后创建 `SecondEngine`、编译并执行 `RecreateCoreSnippet` 就直接 `return true`。第二个 full-engine 的析构发生在函数返回之后，但测试没有任何收尾断言去确认“重建后的新 epoch”在退出时也会再次清空 type metadata、恢复 current-engine 基线。也就是说，它证明了“第一次 full destroy 后可以再建一个 engine 并跑脚本”，却没有证明“第二次 engine 同样能正常 teardown”。 |
| 影响 | 如果 recreate 之后的 engine epoch 在销毁时残留 `FAngelscriptType`、context stack 或其他全局状态，这条测试仍会绿灯，因为它唯一验证的 cleanup 只发生在第一轮。对于你要求重点审查的 `FAngelscriptEngine` 生命周期，这会把“可重建但不可重复销毁”的回归遗漏掉。 |
| 修复建议 | 在第二轮正向编译/执行断言后，不要直接返回；先显式 `SecondEngine.Reset()`，再追加和第一轮同等级的 teardown 断言，例如 `FAngelscriptType::GetTypes().Num() == 0`，必要时再对 `FAngelscriptEngine::TryGetCurrentEngine()` / `FAngelscriptEngineContextStack` 做基线校验。若担心与 `Issue-63` 的 ambient stack 冲突，可把“无 ambient engine 的 isolated full-epoch”封装成专用 helper，再让两轮 cleanup 都落在 helper 返回前。 |

#### Issue-66：`FullDestroyAllowsAnnotatedRecreate` 证明了第二轮还能重新生成类，但没有验证第二轮 annotated epoch 自己的 teardown

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.FullDestroyAllowsAnnotatedRecreate` |
| 行号范围 | 298-352 |
| 问题描述 | 该用例第一轮会编译 `ARecreateAnnotatedActorA`、做一次 `DiscardModule + CollectGarbage + FirstEngine.Reset()`，然后检查 `FAngelscriptType::GetTypes().Num() == 0`。但第二轮只调用 `CompileAnnotatedActor(SecondEngine, ..., "ARecreateAnnotatedActorB")` 并把返回值直接作为最终结果，没有显式 `SecondEngine.Reset()`、没有保存 `ARecreateAnnotatedActorB` 的弱引用，也没有验证第二个 annotated 类和其 package 在退出时会被回收。换句话说，它验证的是“第二轮还能再编译一个 annotated 类”，不是“第二轮 annotated 生命周期同样能完整收尾”。 |
| 影响 | 如果 annotated recreate 只在第一次 epoch teardown 时表现正确，而第二次及之后会残留 generated class、package 或 type metadata，当前测试仍然会通过。这会让 `EngineCore` 最关键的“重复 annotated epoch”回归长期潜伏，尤其容易在多轮 hot-reload / full reload 之后才暴露。 |
| 修复建议 | 参照第一轮 cleanup 逻辑，把第二轮也变成显式收尾：在成功解析 `ARecreateAnnotatedActorB` 后保存 `TWeakObjectPtr<UClass>` 与 outer package 弱引用，随后 `SecondEngine->DiscardModule(TEXT("RecreateAnnotatedActorB"))`、`CollectGarbage(...)`、`SecondEngine.Reset()`，再断言类/package 弱引用失效、`FAngelscriptType::GetTypes().Num() == 0`。若测试希望压缩样板，可把“编译 annotated 类并验证 teardown”提炼成 helper，对两轮 epoch 复用同一套强断言。 |

### 二、需要新增的测试

#### NewTest-55：覆盖 `UAngelscriptAttributeChangedDataMixinLibrary` 的 null-path 与 gameplay-effect callback data 访问合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.h` |
| 关联函数 | `UAngelscriptAttributeChangedDataMixinLibrary::GetGameplayAttribute(...)` / `GetNewValue(...)` / `GetOldValue(...)` / `GetEffectSpec(...)` / `GetGameplayModifierEvaluatedData(...)` / `GetTargetAbilitySystemComponent(...)` |
| 现有测试覆盖 | 完全无直接测试；当前目标测试目录和已记录建议只覆盖 `UAngelscriptAbilitySystemComponent` 的 attribute-change callback 注册与 trampoline 广播，没有任何用例直接保护 `FAngelscriptAttributeChangedData` 这层脚本可见 mixin accessor 的有效/无效路径 |
| 风险评估 | 一旦 wrapper 把 `WrappedData` 的字段映射错位，或在 `GEModData == nullptr` 时返回了错误的 valid 标记/悬空引用，脚本侧读取 attribute-change 回调就会表现成“回调触发了但数据错了”。这类问题通常不会在 bind surface smoke 中暴露，只会在 GAS 运行时以错误属性名、错误旧值/新值或空指针崩溃出现 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AttributeChangedDataMixin.AccessorsExposeWrappedCallbackDataAndNullPaths` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAttributeChangedDataMixinTests.cpp` |
| 场景描述 | 准备一个最小 GAS fixture：`UAngelscriptAbilitySystemComponent` + 测试 `UAngelscriptAttributeSet`（至少包含 `Health` 属性）+ `UAttributeChangeRecorder` listener，listener 用 `UFUNCTION` 接收 `const FAngelscriptAttributeChangedData&` 并缓存最后一次数据。先构造一个默认/手工 `FAngelscriptAttributeChangedData` control case，验证 `GEModData == nullptr` 时的 accessor 行为；随后通过 `RegisterAttributeChangedCallback(...)` 挂 listener，并应用一个会修改 `Health` 的 gameplay effect 或等价 modifier，拿到真实 callback data 后逐个调用 mixin accessor |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；最小 ASC + attribute-set fixture；一个 test gameplay effect 或本地 helper 用于触发带 `GEModData` 的属性变化；`UAttributeChangeRecorder` 保存最后一次 `FAngelscriptAttributeChangedData`；必要时复用 `NewTest-29` / `NewTest-54` 中建议的 ASC/attribute-set helper 形态，但本用例必须独立断言 mixin accessor 返回值 |
| 期望行为 | control case 下，`GetEffectSpec(..., bIsValid)` 和 `GetGameplayModifierEvaluatedData(..., bIsValid)` 都必须把 `bIsValid` 置为 `false`，`GetTargetAbilitySystemComponent(...)` 返回 `nullptr`，同时 `GetOldValue(...)` / `GetNewValue(...)` 保持与手工写入数据一致；真实 callback case 下，`GetGameplayAttribute(...)` 必须等于 `Health`，`GetOldValue(...)` / `GetNewValue(...)` 与 listener 缓存值一致，`GetEffectSpec(..., bIsValid)` 与 `GetGameplayModifierEvaluatedData(..., bIsValid)` 都返回 `bIsValid == true`，且 `GetTargetAbilitySystemComponent(...)` 等于触发回调的 ASC。这样才能证明脚本侧 mixin 既能处理 null-path，也能正确暴露 gameplay-effect 驱动的 attribute-change 数据 |
| 使用的 Helper | `FAngelscriptTestFixture` + 最小 ASC/attribute-set fixture + `UAttributeChangeRecorder` + 触发 attribute change 的 gameplay effect helper |
| 优先级 | P1 |

#### NewTest-56：覆盖 `UAngelscriptAttributeSet::BP_GetActorInfo()` 对 owning ASC actor-info 的透传与刷新

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAttributeSet.cpp` |
| 关联函数 | `UAngelscriptAttributeSet::BP_GetActorInfo() const` |
| 现有测试覆盖 | 当前文档里关于 `AngelscriptAttributeSet.cpp` 的建议只覆盖 owner delegation、attribute helper、replication 和 hook 转发；目标测试目录与已记录建议都没有任何一条直接断言 `BP_GetActorInfo()` 返回的 `FGameplayAbilityActorInfo` 是否与 owning ASC 的当前上下文一致 |
| 风险评估 | 如果 attribute set 到 ASC actor-info 的桥接回归，脚本侧读取 owner/avatar/player-controller 时会拿到空引用或旧上下文，但现有 `RegisterAttributeSet(...)`、attribute helper、replication 用例都不一定会立刻报警，因为它们多数只依赖 owning ASC 自身，而不是 set 侧暴露的 actor-info wrapper |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AttributeSet.ActorInfoWrapperTracksOwningASCContext` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAttributeSetRuntimeTests.cpp` |
| 场景描述 | 创建最小 GAS fixture：owner actor、avatar actor、可选 `APlayerController`、`UAngelscriptAbilitySystemComponent` 和一个注册到该 ASC 的 test `UAngelscriptAttributeSet`。先调用 `InitAbilityActorInfo(OwnerA, AvatarA)`，从 attribute set 侧读取 `BP_GetActorInfo()`；随后把 avatar 切到 `AvatarB`（必要时同步 controller/world context），再次调用 `InitAbilityActorInfo(OwnerA, AvatarB)` 并重新读取 wrapper |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或 share-clean engine + `FActorTestSpawner`；owner/avatar/controller fixture；一个最小 test attribute set class；如果测试中需要稳定读取 `PlayerController`，准备带 controller 的 actor-info 初始化 helper |
| 期望行为 | 第一次初始化后，`BP_GetActorInfo().OwnerActor`、`AvatarActor`、`AbilitySystemComponent`、`PlayerController` 必须分别等于 fixture 中的 owner/avatar/ASC/controller；第二次 re-init 后，同一 attribute set 上的 `BP_GetActorInfo().AvatarActor` 必须切到 `AvatarB`，其余上下文字段保持与 ASC 当前 `AbilityActorInfo` 一致。这样才能证明 `BP_GetActorInfo()` 返回的是 live actor-info，而不是一次性快照或空引用 |
| 使用的 Helper | `FAngelscriptTestFixture` / share-clean engine + `FActorTestSpawner` + 最小 ASC/attribute-set fixture + actor-info 初始化 helper |
| 优先级 | P2 |

---

## 测试审查 (2026-04-09 00:24:47)

### 一、现有测试问题

#### Issue-63：三条 full-destroy 核心用例会清空并丢弃进入前的 `ContextStack`，对 ambient engine 形成串测污染

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.LastFullDestroyClearsTypeState` / `FullDestroyAllowsCleanRecreate` / `FullDestroyAllowsAnnotatedRecreate` |
| 行号范围 | 158-172, 194-208, 266-280 |
| 问题描述 | 这三条用例都会先用 `FCoreTestContextStackGuard` 把整个 `FAngelscriptEngineContextStack` 快照并清空，然后在销毁 shared/global engine 之后立刻调用 `ContextGuard.DiscardSavedStack()`。`DiscardSavedStack()` 会让析构阶段不再恢复进入前的栈内容，但用例后续只清理了 `DestroySharedTestEngine()` 和 `DestroyGlobalEngine()`，并没有恢复任何原本就存在、且并非本测试创建的 ambient engine。结果是：凡是进入测试前已经在 `ContextStack` 里的其它 engine（例如 subsystem-owned primary engine 或 testing override engine），都会被这些 full-destroy 用例静默从当前栈里抹掉。 |
| 影响 | 这不是单纯的“隔离测试”，而是对进程级 current-engine 环境做了不可逆改写。后续依赖 `TryGetCurrentEngine()` / `GetCurrent()` / production-like ambient context 的测试，会因为前面跑过这三条 `EngineCore` 用例而拿到不同环境，形成顺序依赖和难以定位的假失败。 |
| 修复建议 | 不要无条件 `DiscardSavedStack()`。把 helper 改成“清空栈但在退出时恢复未被本测试销毁的既有 engine”，或至少在进入用例前筛出真正要丢弃的 test-owned engine，再在 `ON_SCOPE_EXIT` 中恢复其余快照项。如果这些用例确实必须在无 ambient engine 的环境里运行，应把这种隔离封装成专用 helper，并在收尾阶段显式恢复进入前的非测试栈状态。 |

#### Issue-64：`MinimalApiFunctionLevelExports` 只检查 `FuncPtr` 存在，没把 direct-call 可调度性和脚本可见性建成合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.MinimalApiFunctionLevelExports` |
| 行号范围 | 321-356 |
| 问题描述 | 这条回归只在 `ClassFuncMaps` 里定位 `APlayerCameraManager` 的三条 entry，然后断言 `Entry != nullptr` 且 `Entry->FuncPtr.IsBound()`。它没有检查 `Entry->Caller.IsBound()`，也没有断言 `bReflectiveFallbackBound == false`，更没有编译一个最小脚本去验证 `SetManualCameraFade` / `StartCameraFade` / `StopCameraFade` 的声明真的能从脚本侧解析。换句话说，只要运行时 map 里还留着一个函数指针，这条“function-level exports”测试就会给绿灯。 |
| 影响 | 如果 MinimalAPI function-level export 回归成“map 里有 entry，但 caller 丢了 / 退化到 fallback / 脚本声明不可见”，当前测试不会报警。真正会影响脚本调用 `APlayerCameraManager` fade API 的回归，会被这个现有用例误判成通过。 |
| 修复建议 | 把断言升级成完整 `FFuncEntry` 合同：对三条样本函数同时检查 `FuncPtr.IsBound()`、`Caller.IsBound()` 且 `bReflectiveFallbackBound == false`。再补一个代表性 compile smoke，例如在 clean engine 下编译 `void Check(APlayerCameraManager Camera) { Camera.StartCameraFade(...); }`，确认这三条 function-level export 不只是注册到了 map，而是真正从脚本面可见。 |

### 二、需要新增的测试

#### NewTest-53：覆盖 `UAngelscriptAttributeSet` 的 metadata-init fallback 与 attribute/base-change hook 转发

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAttributeSet.cpp` |
| 关联函数 | `InitFromMetaDataTable(...)` / `BP_OnInitFromMetaDataTable(...)` / `PreAttributeChange(...)` / `PostAttributeChange(...)` / `PreAttributeBaseChange(...)` / `PostAttributeBaseChange(...)` |
| 现有测试覆盖 | 当前文档里关于 `AngelscriptAttributeSet.cpp` 的建议只覆盖 `PostInitProperties()`、`GetLifetimeReplicatedProps(...)`、`OnRep_Attribute(...)` 和 owner delegation；现有目标测试目录与已记录建议都没有直接命中 metadata-table fallback 和四个 attribute-change hook 的脚本转发 |
| 风险评估 | 如果 script override 不再收到 attribute/base-change 回调、`NewValue` 改写失效，或 `BP_OnInitFromMetaDataTable` 返回 `true` 仍错误落回 `Super::InitFromMetaDataTable`，脚本属性集会在初始化和数值钳制阶段静默跑错，当前自动化没有任何定点保护 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AttributeSet.AttributeHooksAndMetadataInitForwardCorrectly` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAttributeSetTests.cpp` |
| 场景描述 | 在测试文件里定义一个 native access shim `UAutomationAttributeSetHookAccess`，公开调用 `InitFromMetaDataTable`、`PreAttributeChange`、`PostAttributeChange`、`PreAttributeBaseChange`、`PostAttributeBaseChange` 的包装函数，并带一个 `Health` 属性。再编译一个 script 子类，覆写 `BP_OnInitFromMetaDataTable`、`BP_PreAttributeChange`、`BP_PostAttributeChange`、`BP_PreAttributeBaseChange`、`BP_PostAttributeBaseChange`，把收到的 attribute 名称、old/new 值记录到可读字段；其中 `BP_PreAttributeChange` 把 `NewValue` 改成 `33.f`，`BP_PreAttributeBaseChange` 改成 `44.f`，`BP_OnInitFromMetaDataTable` 通过 script bool 控制返回 `true/false` |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；最小 ASC/actor fixture；一个包含 `Health` 行的 `UDataTable`；native access shim `UAutomationAttributeSetHookAccess`；`CompileAnnotatedModuleFromMemory(...)` 编译 script 子类 |
| 期望行为 | 当 script 让 `BP_OnInitFromMetaDataTable` 返回 `true` 时，hook 计数变为 1 且 `Health` 保持默认值，证明 fallback 没有继续执行；当返回 `false` 时，`Super::InitFromMetaDataTable` 会把 `Health` 初始化成表里的数值。`PreAttributeChange` 之后传出值必须变成 `33.f`，`PostAttributeChange` 记录到的 `OldValue/NewValue` 必须与调用值一致；`PreAttributeBaseChange` 之后传出值必须变成 `44.f`，`PostAttributeBaseChange` 同样要记录正确的 old/new 值与 attribute 名称 |
| 使用的 Helper | `FAngelscriptTestFixture` + native access shim `UAutomationAttributeSetHookAccess` + `CompileAnnotatedModuleFromMemory(...)` + 最小 ASC fixture |
| 优先级 | P1 |

#### NewTest-54：覆盖 `UAngelscriptAttributeSet` 的 gameplay-effect execute hook 转发与 veto 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAttributeSet.cpp` |
| 关联函数 | `PreGameplayEffectExecute(...)` / `BP_PreGameplayEffectExecute(...)` / `PostGameplayEffectExecute(...)` / `BP_PostGameplayEffectExecute(...)` |
| 现有测试覆盖 | 现有目标测试目录和已记录建议都没有任何一条用例直接触发 gameplay-effect execute hooks；`UAngelscriptAttributeSet` 当前只有 replication/owner/helper 方向的建议，没有保护 `FGameplayEffectSpec`、`FGameplayModifierEvaluatedData` 和 owning ASC 向 script hook 的转发 |
| 风险评估 | 若 `PreGameplayEffectExecute` 不再把 ASC/spec/evaluated data 传到 script、返回 `false` 的 veto 被忽略，或 `PostGameplayEffectExecute` 在允许执行时不再触发，脚本属性集对 GameplayEffect 的核心控制点会静默失效 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AttributeSet.GameplayEffectExecuteHooksForwardAndHonorVeto` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAttributeSetTests.cpp` |
| 场景描述 | 基于同一个 native access shim，再加一个 `InvokeGameplayEffectHooks(UAngelscriptAbilitySystemComponent*, float InMagnitude, bool bAllowExecute)` helper：它构造最小 `FGameplayEffectSpec` 和 `FGameplayModifierEvaluatedData`（目标属性为 `Health`），先调用 `PreGameplayEffectExecute`，仅当返回 `true` 时再继续调用 `PostGameplayEffectExecute`。脚本子类覆写 `BP_PreGameplayEffectExecute` 和 `BP_PostGameplayEffectExecute`，记录收到的 ASC 指针、输入 magnitude、调用次数，并通过 script bool 控制 pre-hook 返回值 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；最小 ASC/actor fixture；注册后的 script attribute set；native access shim `UAutomationAttributeSetHookAccess`；一个能构造 `FGameplayEffectSpec` / `FGameplayModifierEvaluatedData` 的本地 helper |
| 期望行为 | allow case 下，`BP_PreGameplayEffectExecute` 收到的 ASC 必须等于注册该 set 的 `UAngelscriptAbilitySystemComponent`，记录的 magnitude 等于输入值，返回 `true` 后 `BP_PostGameplayEffectExecute` 调用次数变为 1；veto case 下，`BP_PreGameplayEffectExecute` 仍调用一次，但返回 `false` 后 `BP_PostGameplayEffectExecute` 次数保持不变。这样才能证明 runtime 既把 callback data 正确转发给 script，也尊重 pre-hook 的执行许可结果 |
| 使用的 Helper | `FAngelscriptTestFixture` + native access shim `UAutomationAttributeSetHookAccess` + ASC/attribute-set fixture + 本地 `InvokeGameplayEffectHooks(...)` helper |
| 优先级 | P1 |

---

## 测试审查 (2026-04-08 20:42)

### 二、需要新增的测试

#### NewTest-47：覆盖 `AbilitySpec` 查询/修改 API 的 invalid-handle 错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `CanActivateAbilitySpec(...)` / `SetAbilitySpecSourceObject(...)` / `GetAbilitySpecSourceObject(...)` / `CancelAbilityByHandle(...)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-31` 只覆盖有效 handle 下的 source-object 与 cooldown 查询；目标测试目录和 `AngelscriptRuntime/Tests` 里没有任何用例直接传入 default-constructed / stale `FGameplayAbilitySpecHandle` 验证这组 wrapper 的错误路径 |
| 风险评估 | 脚本层很容易持有过期 handle。若 invalid-handle guard 退化成错误返回值、误改其他 spec，或直接解引用空 `Spec`，运行时会表现成偶发崩溃或错误 ability 被篡改，而当前自动化没有定点保护 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.InvalidSpecHandleGuardsDoNotCrashOrMutateState` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemQueryTests.cpp` |
| 场景描述 | 创建最小 ASC fixture，先授予一个有效 ability 并记录 `ValidHandle` 与 `SourceA`。随后构造 `FGameplayAbilitySpecHandle InvalidHandle`，依次调用 `CanActivateAbilitySpec(InvalidHandle)`、`GetAbilitySpecSourceObject(InvalidHandle)`、`SetAbilitySpecSourceObject(InvalidHandle, RogueSource)`、`CancelAbilityByHandle(InvalidHandle)`；最后重新读取 `ValidHandle` 对应的 spec/source-object 与 ability presence |
| 输入/前置 | `FAngelscriptTestFixture`；native `UGameplayAbility` test double；`SourceA` / `RogueSource` 测试对象；一个 `ValidHandle` 和一个默认构造的 `InvalidHandle`；如项目已有日志捕获 helper，可选地记录 warning |
| 期望行为 | `CanActivateAbilitySpec(InvalidHandle)` 返回 `false`；`GetAbilitySpecSourceObject(InvalidHandle)` 返回 `nullptr`；调用 `SetAbilitySpecSourceObject(InvalidHandle, RogueSource)` 与 `CancelAbilityByHandle(InvalidHandle)` 不应崩溃，也不能影响 `ValidHandle` 对应 spec；最终 `GetAbilitySpecSourceObject(ValidHandle)` 仍等于 `SourceA`，`HasAbility(TestAbility)` 维持原始状态。若日志捕获可用，还应命中对应 invalid-handle warning |
| 使用的 Helper | `FAngelscriptTestFixture` + native `UGameplayAbility` test double + 可选 scoped log capture helper |
| 优先级 | P1 |

#### NewTest-48：给 `BP_SetRemoveAbilityOnEnd` / `OnAbilityRemoved` 补齐移除生命周期合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `BP_GiveAbility(...)` / `BP_GiveAbilityAndActivateOnce(...)` / `BP_SetRemoveAbilityOnEnd(...)` / `OnRemoveAbility(...)` / `HasAbility(...)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-03` 只覆盖 `GiveAbility`、激活、取消和 `OnAbilityGiven`；现有目标测试与 `AngelscriptRuntime/Tests` 都没有任何一条回归直接验证 remove-on-end 标志会真正移除 spec，或 `OnAbilityRemoved` 会向脚本面广播 |
| 风险评估 | 一次性 ability 若不会在结束后移除，或 removal delegate 不广播，脚本层会长期持有失效 spec，后续 `HasAbility` / stale handle / cleanup 逻辑都会漂移；这类问题常表现成“看似能用，但越跑越脏”的 GAS 生命周期故障 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.RemoveAbilityOnEndBroadcastsRemovalAndPrunesSpec` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemTests.cpp` |
| 场景描述 | 创建 ASC fixture 与 native test ability。先授予 ability、绑定一个记录 `OnAbilityRemoved` 的 listener，然后对返回 handle 调 `BP_SetRemoveAbilityOnEnd(...)`；接着让 ability 进入结束路径，例如调用 `BP_GiveAbilityAndActivateOnce(...)` 的 one-shot 流程，或让 test ability 在 `ActivateAbility()` 中立即 `EndAbility()`。结束后再重复触发一次取消/查询作为 control |
| 输入/前置 | `FAngelscriptTestFixture`；可立即结束的 native `UGameplayAbility` test double；delegate recorder object；必要时一个 access shim 读取 spec 是否仍存在 |
| 期望行为 | ability 结束后 `OnAbilityRemoved` 只广播一次，广播中的 ability class/handle 与授予结果一致；`HasAbility(TestAbility)` 变为 `false`；`CanActivateAbilitySpec(Handle)` 返回 `false` 或 access shim 证明 spec 已被移除；`GetAbilitySpecSourceObject(Handle)` 返回 `nullptr`；随后再次调用取消/查询不会重复广播 removal |
| 使用的 Helper | `FAngelscriptTestFixture` + native `UGameplayAbility` test double + delegate recorder/access shim |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingErrorPath: 1, MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:46)

### 二、需要新增的测试

#### NewTest-49：给 `BindInput(...)` 补齐 command/enum-path/input-id 转发合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `BindInput(UInputComponent*, FAngelscriptInputBindData)` |
| 现有测试覆盖 | 当前目标测试目录、`AngelscriptRuntime/Tests` 和现有 `RuntimeCore_TestGaps.md` 都没有任何一条用例命中 `FAngelscriptInputBindData` 或 `BindInput(...)`；现有 GAS 建议只覆盖 ability 给与、tag mirror、task/cue wrapper，不涉及输入绑定桥接 |
| 风险评估 | 这是脚本 ability 输入接线的唯一包装层。若 `ConfirmTargetCommand`、`CancelTargetCommand`、`EnumPathName` 或 confirm/cancel input id 传错，脚本面会表现成“ASC 存在但输入永远不触发”，而当前自动化无法给出任何定位 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.BindInputForwardsCommandsEnumPathAndIds` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemInputTests.cpp` |
| 场景描述 | 在测试文件里定义一个 `URecordingAngelscriptAbilitySystemComponent`，覆写 `BindAbilityActivationToInputComponent(...)` 把收到的 `UInputComponent*` 和 `FGameplayAbilityInputBinds` 缓存下来。准备一个 native test enum（如 `EAutomationAbilityInput`）、一个 `UInputComponent`，以及填好 `ConfirmTargetCommand`、`CancelTargetCommand`、`EnumName`、`ConfirmTargetInputID`、`CancelTargetInputID` 的 `FAngelscriptInputBindData`，然后调用 `BindInput(...)` |
| 输入/前置 | 纯 native test subclass；一个 native `UENUM(BlueprintType)` test enum；`NewObject<UInputComponent>()`；`FAngelscriptInputBindData` 样本值，例如 `"ConfirmTarget"` / `"CancelTarget"` / test enum path / `3` / `4` |
| 期望行为 | 覆写的 `BindAbilityActivationToInputComponent(...)` 必须被调用一次；缓存下来的 `InputComponent` 与传入对象同一地址；`FGameplayAbilityInputBinds` 内的 `ConfirmTargetCommand`、`CancelTargetCommand`、`EnumPathName`、`ConfirmTargetInputID`、`CancelTargetInputID` 与 `FAngelscriptInputBindData` 完全一致；`GetBindEnum()` 能解析回测试 enum |
| 使用的 Helper | 其他（recording ASC subclass + native test enum + `UInputComponent` fixture） |
| 优先级 | P2 |

#### NewTest-50：覆盖 tag-based ability 查询、激活与取消包装器

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `GetActiveAbilitiesWithTags(...)` / `ActivateAbilitiesUsingTags(...)` / `CancelAbilitiesByTags(...)` |
| 现有测试覆盖 | 当前现有提议只覆盖 `GiveAbility`、`CanActivateAbility*`、cooldown、attribute/tag mirror 和 callback；目标测试目录与 `AngelscriptRuntime/Tests` 中没有任何用例直接验证按 tag 激活/查询/取消 ability 的脚本包装器 |
| 风险评估 | GAS 脚本常通过 gameplay tags 驱动一组 ability。若这些 wrapper 过滤条件错用、忽略 `WithoutTags`、或返回的 active instance 列表不准确，运行时会表现成“按 tag 激活了错的技能”或“取消逻辑失效”，这类回归目前完全无保护 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.TagQueriesActivateAndCancelMatchingAbilities` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemTagTests.cpp` |
| 场景描述 | 创建 ASC fixture 与两个 native test ability：`PrimaryAbility` 持有 `Ability.Tests.Primary`，`SecondaryAbility` 持有 `Ability.Tests.Secondary`，两者都保持激活直到显式取消。先授予两条 ability；调用 `ActivateAbilitiesUsingTags({Primary})`，随后用 `GetActiveAbilitiesWithTags({Primary})` 和 `GetActiveAbilitiesWithTags({Secondary})` 读取活动实例；再激活 secondary，并调用 `CancelAbilitiesByTags({Primary}, {}, nullptr)` |
| 输入/前置 | `FAngelscriptTestFixture`；两个 native `UGameplayAbility` test double；`FGameplayTagContainer`：`Ability.Tests.Primary` 与 `Ability.Tests.Secondary`；能够读取 active state 的 ASC fixture |
| 期望行为 | 第一次 `ActivateAbilitiesUsingTags({Primary})` 返回 `true`，且只有 `PrimaryAbility` 进入 active；`GetActiveAbilitiesWithTags({Primary})` 返回 1 个 `PrimaryAbility` 实例，`GetActiveAbilitiesWithTags({Secondary})` 初始为空；激活 secondary 后两者都 active；调用 `CancelAbilitiesByTags({Primary}, {}, nullptr)` 后 primary 变为 inactive、secondary 仍保持 active，证明 tag 过滤与取消包装器都走对了路径 |
| 使用的 Helper | `FAngelscriptTestFixture` + native `UGameplayAbility` test double + gameplay tag fixture |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:08)

### 二、需要新增的测试

#### NewTest-44：补齐 `GameplayEffectExecutionParameters` mixin accessor 的 live-state 引用合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameplayEffectUtils.h` |
| 关联函数 | `UGameplayEffectExecutionParametersMixinLibrary::GetIgnoreHandles(...)` / `GetAppliedSourceTagFilter(...)` / `GetAppliedTargetTagFilter(...)` / `GetIncludePredictiveMods(...)` / `SetIncludePredictiveMods(...)` / `UAngelscriptGameplayEffectUtils::MakeGameplayEffectExecutionScopedModifierInfo(...)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-07` 只覆盖 `CaptureGameplayAttribute(...)`、`MakeGameplayModifierEvaluationData(...)` 和 `SetCapturedSourceTagsFromSpec(...)`；上述 accessor 和 scoped-modifier helper 在现有测试目录与 `AngelscriptRuntime/Tests` 中均无任何命中 |
| 风险评估 | 如果这些 mixin 不再返回底层 `WrappedParams` 的 live reference，或 `SetIncludePredictiveMods(...)` 写到了错误字段，脚本 execution calculation 会在忽略句柄、source/target tag filter、predictive mod 配置上静默偏离；这类错误往往不会在 compile smoke 中暴露 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.GameplayEffectUtils.ExecutionParameterMixinsExposeLiveState` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGameplayEffectUtilsTests.cpp` |
| 场景描述 | 在纯 native 测试里构造 `FGameplayEffectExecutionParameters Data`，预填一条 `FActiveGameplayEffectHandle` 到 `IgnoreHandles`，并准备 source/target `FGameplayTagContainer`。通过 mixin getter 取出数组/容器引用后原地修改，再调用 `SetIncludePredictiveMods(Data, true)` 与 `GetIncludePredictiveMods(Data)`；最后基于一个已知有效的 `FGameplayEffectAttributeCaptureDefinition` 调 `MakeGameplayEffectExecutionScopedModifierInfo(...)` |
| 输入/前置 | 一个最小 test attribute set class（可复用 `NewTest-07` 的 `Health` 属性）；测试 handle；source/target tags；无需完整 `FAngelscriptEngine` |
| 期望行为 | `GetIgnoreHandles(...)` 返回的数组引用追加新 handle 后，`Data.WrappedParams.IgnoreHandles` 立即包含相同元素；`GetAppliedSourceTagFilter(...)` 和 `GetAppliedTargetTagFilter(...)` 返回的容器引用修改后，底层 `WrappedParams` 同步可见；`GetIncludePredictiveMods(...)` 初始值与 `WrappedParams.IncludePredictiveMods` 一致，调用 `SetIncludePredictiveMods(Data, true)` 后 getter 返回 true；`MakeGameplayEffectExecutionScopedModifierInfo(...)` 产物中的 capture definition 必须与输入 capture 完全一致 |
| 使用的 Helper | 其他（纯 native `FGameplayEffectExecutionParameters` fixture，可复用 `NewTest-07` 的 test attribute set） |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

## 测试审查 (2026-04-08 15:51)

### 一、现有测试问题

#### Issue-34：`PopulatesClassFuncMaps` 只要求 `AActor` “存在任意可调用 entry”，没有保护它自己点名的 `GetActorTimeDilation`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.PopulatesClassFuncMaps` |
| 行号范围 | 150-196 |
| 问题描述 | 用例先显式挑出 `AActor::GetActorTimeDilation` 并断言条目存在（178-181 行），但后续真正的“可调用”断言却退化成遍历整个 `ActorFunctionMap`，只要任意别的 `AActor` entry 满足 `FuncPtr.IsBound()` 或 `bReflectiveFallbackBound` 就通过（184-195 行）。这意味着即便 `GetActorTimeDilation` 自己退化成 unresolved stub、caller 丢失或 direct-bind 回退，当前测试仍然可能因为 `AActor` 里别的函数可调用而绿灯。 |
| 影响 | 该用例名声称保护“generated function table 启动后已把关键 class map 填好”，但对真正点名的代表 entry 没有建 callable 合同；一旦 UHT 只把键注册出来却没把 `GetActorTimeDilation` 绑定成可用入口，测试无法精准报警。 |
| 修复建议 | 不要把代表样本和兜底统计混在一起。对 `ActorTimeDilationEntry` 直接做强断言：至少检查 `Entry->FuncPtr.IsBound() || Entry->bReflectiveFallbackBound`，若期望完整可调用，再改用共享 helper 同时验证 `FuncPtr` 与 `Caller`；“AActor 至少有一个 callable entry”这种宽泛统计可保留，但应降为附加健康度断言，不能替代代表 API 的合同。 |

#### Issue-35：`MixedSuccessFailureRecoveryAndRemap` 在坏模块编译失败后没有验证“坏模块未被注册/不可执行”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.MixedSuccessFailureRecoveryAndRemap` |
| 行号范围 | 415-540 |
| 问题描述 | 用例在 478-486 行只断言 `Game.Mixed.Bad` 的编译返回失败和 `CompileResult` 为 error，然后就直接转去验证 `Game.Mixed.Good` 仍能执行，以及后续“修复坏脚本后可以恢复”。它从未检查失败那一刻 `Engine.GetModuleByFilename(BadPath)` / `GetModuleByFilenameOrModuleName(BadPath, "Game.Mixed.Bad")` 是否仍为空，也没有验证 `ExecuteIntFunction(&Engine, "Game.Mixed.Bad", "int BrokenEntry()")` 会失败。若 runtime 在失败路径里错误留下半注册 module desc、旧 raw `asIScriptModule` 或脏 filename 映射，这条测试仍可能完全绿灯。 |
| 影响 | 该用例名声称覆盖 success/failure recovery 与 remap，但对最关键的“失败模块不能以坏状态污染运行时索引”没有合同；一旦 file-system 热重载把失败模块残留到 lookup/execute 路径，当前测试只会看到好模块仍然正常，无法发现 ghost module 回归。 |
| 修复建议 | 在坏模块编译失败之后立刻补两组负向断言：1. `GetModuleByFilename(BadPath)` 和 `GetModuleByFilenameOrModuleName(BadPath, TEXT("Game.Mixed.Bad"))` 都应无效；2. 直接执行 `Game.Mixed.Bad::BrokenEntry()` 必须失败并返回 lookup/prepare error。只有在这些断言通过后，再继续验证“修复脚本后重新注册成功并返回 23”，这样才能真正覆盖 failure recovery 的隔离合同。 |

#### Issue-36：`SummaryOutput` 主要在比对同一批 UHT 产物的内部自洽，缺少独立参考系

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.SummaryOutput` |
| 行号范围 | 459-591 |
| 问题描述 | 这条用例读取 `AS_FunctionTable_Summary.json` 后，主要做三类校验：字段存在、JSON 内部加总关系、以及 `totalGeneratedEntries` 等于 `CountGeneratedBindingRegistrations(GeneratedDirectory)` 的结果（510-590 行）。但 `CountGeneratedBindingRegistrations` 也是从同一目录下的 `AS_FunctionTable_*.cpp` 数 `AddFunctionEntry(` 行得到的（76-97 行），属于和 summary 同源的产物。换言之，测试并没有拿 runtime `ClassFuncMaps`、`AS_FunctionTable_Entries.csv` 或任何独立数据源交叉验证 summary；只要 generator 同时把 JSON 和 cpp 一起写错、一起漏模块或一起读到旧产物，这条测试仍可能完全通过。 |
| 影响 | `SummaryOutput` 当前更像“生成器输出自检”，而不是对外合同测试。它无法发现 summary 与真实 runtime 注册结果不一致、与 entries.csv 不一致，或整批产物其实来自旧构建轮次的情况；一旦 UHT 报表整体漂移但保持内部自洽，自动化会误报绿灯。 |
| 修复建议 | 给 summary 增加至少一个独立参照：优先把 `totalGeneratedEntries` / per-module totals 与 `AS_FunctionTable_Entries.csv` 的真实行数与模块分布做交叉校验，再抽一两个关键模块（如 `Engine`、`GameplayTasks`、`AngelscriptRuntime`）与运行时 `FAngelscriptBinds::GetClassFuncMaps()` 或已知代表 entry 数做对应检查；如果只能读取离线产物，也应验证 summary/entries/csv 三者互相一致，而不是只让 summary 去对照同源 cpp 文件。 |


---

## 测试审查 (2026-04-08 16:09)

### 一、现有测试问题

#### Issue-31：多数 `FileSystem` 用例只清磁盘目录，不清共享 engine 里的模块和 filename 索引

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.ModuleLookupByFilename` / `CompileFromDisk` / `PartialFailurePreservesGoodModules` / `RenameUpdatesModuleLookup` / `PathNormalizationLookup` |
| 行号范围 | 81-235, 319-413 |
| 问题描述 | 这 5 个用例都用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 在进入前拿到干净 shared engine，但成功路径退出时只调用 `CleanFileSystemTestRoot()` 删除临时脚本文件，没有 `DiscardModule(...)`、没有 `ResetSharedCloneEngine(Engine)`，也没有统一 `ON_SCOPE_EXIT` 清理 runtime 模块状态。结果是共享 engine 会继续保留刚编译出的 `FAngelscriptModuleDesc`、底层 `asIScriptModule` 和 filename->module 映射，只是这些映射现在指向已经被测试删掉的磁盘路径。 |
| 影响 | 这些用例把“脏模块 + 已删除磁盘文件”的状态留给后续共享 engine 测试；如果下一条用例不是再次走 `SHARE_CLEAN`，而是复用 `ASTEST_CREATE_ENGINE_SHARE()` 或直接检查 module/path 索引，就会继承上一条 file-system 测试的残留。自动化执行顺序一变，就可能出现 lookup 误报、旧模块命中和跨用例串线。 |
| 修复建议 | 把 file-system 编译类用例统一改成 scope-based cleanup：进入测试后立刻 `ON_SCOPE_EXIT` 调用 `Engine.DiscardModule(...)` 或直接 `ResetSharedCloneEngine(Engine)`，并把 `CleanFileSystemTestRoot()` 也放进同一个退出块；对需要保留多个模块名的场景，显式枚举要清理的 module name，避免只删磁盘不删 runtime 索引。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 15:18)

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-25` / `Issue-26` / `Issue-27` / `Issue-28` / `Issue-29` |
| 新增测试建议 | `NewTest-18` / `NewTest-19` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-27 |
| WrongHelper | 1 | Issue-25 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 16:22)

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-30` / `Issue-31` |
| 新增测试建议 | `NewTest-20` / `NewTest-21` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-30 |
| MissingCleanup | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 16:22)

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-30` / `Issue-31` |
| 新增测试建议 | `NewTest-20` / `NewTest-21` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-30 |
| MissingCleanup | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 16:02)

### 一、现有测试问题

#### Issue-30：`SubsystemScenarioTests` 只断言“编译失败”，没有验证失败原因真的是 subsystem 不支持

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `WorldSubsystem.Tick` / `WorldSubsystem.ActorAccess` / `GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 66-245 |
| 问题描述 | 四个用例都把 `Angelscript` 日志压到 `Fatal`，随后调用 `CompileModuleWithResult(...)`，最后只检查 `bCompiled == false` 和 `CompileResult == ECompileResult::Error`。它们既没有用 `CompileModuleWithSummary(...)` 读取 diagnostics，也没有 `AddExpectedError(...)` 去匹配“subsystem script generation remains unsupported”这一具体失败原因。结果是：哪怕脚本里出现了无关语法错误、helper/preprocessor 接线坏掉、基础类名解析失败，测试仍会把任何 `Error` 当成预期通过。 |
| 影响 | 这组用例无法区分“当前分支明确不支持 subsystem script generation”与“测试输入或编译基础设施随机坏了”。一旦真实回归把错误原因换成别的问题，测试仍会继续绿灯，既掩盖根因，也让后续功能落地时缺少可迁移的诊断合同。 |
| 修复建议 | 保留“当前应编译失败”的总断言，但把失败原因也建成合同：改用 `CompileModuleWithSummary(&Engine, ECompileType::SoftReloadOnly, ModuleName, ..., true, Summary, true)` 读取 diagnostics，或补 `AddExpectedError(...)` 精确匹配 subsystem 不支持的关键报错片段；同时断言 diagnostics 至少命中目标 section/消息，避免把任意 compile error 误判成预期行为。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-30 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 15:12)

### 二、需要新增的测试

#### NewTest-18：给 `FAngelscriptDelegateWithPayload` 补齐 bind/execute/reset 与 primitive payload 装箱回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDelegateWithPayload.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp` |
| 关联函数 | `FAngelscriptDelegateWithPayload::IsBound()` / `ExecuteIfBound()` / `BindUFunction(...)` / `BindUFunctionWithPayload(...)` / `Reset()` / `GetBoxedPrimitiveStructFromTypeId(int)` |
| 现有测试覆盖 | 只有 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` 中的 `DelegateWithPayloadCompile`，仅验证类型存在且暴露 `IsBound` / `ExecuteIfBound` 方法；没有任何真实绑定、执行或 payload 装箱断言 |
| 风险评估 | 一旦 payload boxing、`ProcessEvent` 参数传递、`Reset()` 清理或无 payload/有 payload 两条 bind 路径回归，当前自动化不会报警；脚本侧 delegate 看起来“能编译”，但运行时会静默不执行或传错参数 |
| 建议测试名 | `Angelscript.TestModule.Engine.DelegateWithPayload.BindExecuteAndResetPrimitivePayloads` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDelegateWithPayloadTests.cpp` |
| 场景描述 | 在 clean engine 下创建 `UAngelscriptNativeScriptTestObject`：第一段直接 `BindUFunction(..., "MarkNativeFlagFromDelegate")`，断言初始 `IsBound()==false`、绑定后为 `true`、`ExecuteIfBound()` 会把 `bNativeFlag` 置为 `true`；第二段获取 `float` 的 Angelscript type id，调用 `BindUFunctionWithPayload(..., "SetPreciseValueFromDelegate", &PayloadValue, FloatTypeId)`，执行后断言 `PreciseValue == 3.25f`；最后 `Reset()` 后再次执行，断言状态不再变化且 `IsBound()==false` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；`UAngelscriptNativeScriptTestObject` 作为 receiver；通过 `Engine.GetScriptEngine()->GetTypeIdByDecl("float")` 或等价 helper 获取 primitive payload type id；必要时对浮点断言使用容差 |
| 期望行为 | 无 payload 路径会触发目标 `UFUNCTION` 且不残留 payload；float payload 路径会把 `3.25f` 正确装箱并传给 `SetPreciseValueFromDelegate`；`Reset()` 后对象弱引用、函数名和 payload 都被清空，`ExecuteIfBound()` 成为 no-op |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `ASTEST_BEGIN_SHARE_CLEAN` + `UAngelscriptNativeScriptTestObject` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 15:15)

### 二、需要新增的测试

#### NewTest-19：覆盖 `AngelscriptAllScriptRootsCommandlet` 的 JSON 输出合同与 root 顺序透传

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp` |
| 关联函数 | `UAngelscriptAllScriptRootsCommandlet::Main(const FString& Params)` |
| 现有测试覆盖 | `Runtime/Tests` 里只看到 `DiscoverScriptRoots(...)` 级别的 engine 测试，没有任何用例直接调用 commandlet 并验证其日志输出的 JSON 结构、字段名和顺序 |
| 风险评估 | 一旦 commandlet 输出格式、字段名、引号规则或 root 顺序回归，下游脚本/CI 若把它当 JSON 消费会直接失效；当前自动化不会在接口层报警 |
| 建议测试名 | `Angelscript.TestModule.FileSystem.AllScriptRootsCommandlet.EmitsStableJsonArray` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptAllScriptRootsCommandletTests.cpp` |
| 场景描述 | 创建 `UAngelscriptAllScriptRootsCommandlet` 实例并调用 `Main(TEXT(""))`，用 scoped log capture 拦截 `Angelscript` 类别的 `Display` 输出；同时在测试内调用 `FAngelscriptEngine::MakeAllScriptRoots()` 取得基线 roots，随后比较 commandlet 输出中的 `AngelscriptScriptRoots` 数组与基线顺序、元素个数和每个字符串值 |
| 输入/前置 | clean runtime 环境；`UAngelscriptAllScriptRootsCommandlet` 实例；一个可复用的 `FOutputDevice`/scoped log capture helper；基线 roots 来自 `FAngelscriptEngine::MakeAllScriptRoots()` |
| 期望行为 | `Main()` 返回 `0`；日志中存在且只存在一个 JSON 对象，字段名为 `AngelscriptScriptRoots`；数组长度与 `MakeAllScriptRoots()` 完全一致，顺序一致，每个 root 都带引号且没有多余尾逗号或重复项 |
| 使用的 Helper | `FAngelscriptEngine::MakeAllScriptRoots()` + 新增 scoped log capture helper |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 15:05)

### 一、现有测试问题

#### Issue-29：`StartupBindRegistrySmoke` 只比较同一 registry 的两个视图，再加一条 compile smoke，抓不住关键 bind 缺失或污染

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.StartupBindRegistrySmoke` |
| 行号范围 | 646-695 |
| 问题描述 | 用例先取 `FAngelscriptBinds::GetAllRegisteredBindNames()` 和 `GetBindInfoList()`，只断言“数量大于 0”“两个数组长度一致”“BindOrder 单调递增”。这三条其实都来自同一份全局 bind registry，即使前序测试往 registry 里泄漏了额外 bind、或者关键 bind 家族整体缺失，只要两个视图同步变化，前半段仍会通过。后半段又只是编译一个混合片段，从头到尾没有执行脚本返回值，也没有检查某个特定 startup bind 名称/家族确实存在。 |
| 影响 | 这条 smoke 无法区分“registry 完整且干净”与“registry 已被测试污染但内部自洽”，也无法发现关键 startup bind 面被静默删掉的回归。它当前更像在验证“bind registry 不是空的”，而不是保护真正的 startup bind surface。 |
| 修复建议 | 先把 registry 断言升级成代表性合同：至少验证一组关键家族对应的 bind info 或脚本表面存在，不要只比总数；然后把后半段 compile smoke 升级成 execute parity，显式执行 `CheckStartupBindSurface()` 并断言返回值为预期常量。若继续依赖全局 registry，测试前还应配合快照/restore 或 clean engine fixture，避免被其他 `BindConfig` 用例注入的 bind 污染。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-29 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 15:02)

### 一、现有测试问题

#### Issue-28：`MinimalApiFunctionLevelExport` 只检查 `FuncPtr`，没有验证 direct entry 的 `Caller` 和 fallback 状态

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.MinimalApiFunctionLevelExport` |
| 行号范围 | 321-357 |
| 问题描述 | 这条 MinimalAPI 回归会定位 `APlayerCameraManager` 的 `SetManualCameraFade`、`StartCameraFade`、`StopCameraFade` 三个 entry，但强断言只落在 `Entry->FuncPtr.IsBound()`。它没有像 `BindConfigTests.cpp` 里的 `IsFunctionEntryBound(...)` 那样同时验证 `Caller.IsBound()`，也没有确认这些 entry 没有被重新归类成 reflective fallback。对于 generated function table，只有裸 `FuncPtr` 在并不等于完整 direct bind 仍然可调度。 |
| 影响 | 一旦 function-level export 逻辑只恢复了函数指针、却把 caller 丢失，或者条目悄悄退化成 fallback/stub，这条回归测试仍可能继续为绿。最需要保护的“MinimalAPI 函数保持 direct callable”合同因此存在漏检。 |
| 修复建议 | 把 3 个样本入口统一改成完整 entry 校验：复用 `IsFunctionEntryBound(*Entry)` 或等价 helper，同时断言 `!Entry->bReflectiveFallbackBound`；如果测试基础设施允许，再补一个最小 compile/execute smoke 调用其中一个导出函数，证明不只是 map 条目存在，而是脚本调度真的可用。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-28 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 14:59)

### 一、现有测试问题

#### Issue-27：`Startup.Clone` / `CreateForTestingClone` 只看两个耗时字段为零，没有证明 clone 路径真的复用了 source engine

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.Core.Performance.Startup.Clone` / `Angelscript.TestModule.Core.Performance.Startup.CreateForTestingClone` |
| 行号范围 | 35-40, 114-157, 168-195 |
| 问题描述 | 两条 clone 性能测试的断言只有 `BindScriptTypesSeconds == 0.0` 和 `CallBindsSeconds == 0.0`。`FStartupPerformanceSample` 也只保存了 3 个时长字段，没有记录 `FAngelscriptBindExecutionSnapshot` 里的 `InvocationCount`、`ExecutedBindNames`、`DisabledBindNames`，更没有记录 `CreateForTesting` 最终 creation mode、`GetSourceEngine()` 或是否真的引用了当前 scoped source engine。结果是：只要计时字段恰好为零，哪怕 clone 路径其实走错了 source、startup bind 被重放但 observation 没记到时长、或者 `CreateForTesting(..., Clone)` 悄悄回退成别的模式，测试依然会通过。 |
| 影响 | clone 启动最关键的“共享 source、不重放 startup binds、保留 clone creation contract”没有被这两条性能用例真正保护；当前绿色结果只能说明 artifact 写出来且两个时长值为零，不能说明 clone 语义本身正确。 |
| 修复建议 | 扩展 `FStartupPerformanceSample`，把 `InvocationCount`、`ExecutedBindCount`、creation mode 和 `SourceEngine` 身份一起采样；`Startup.Clone` 至少断言 `InvocationCount == 0`、`ExecutedBindCount == 0` 且 clone 的 source 指向刚创建的 full engine，`CreateForTestingClone` 还应显式断言 `GetCreationMode() == Clone`、`GetSourceEngine() == SourceEngine.Get()`。耗时断言可以保留，但只能作为附加性能信号，不能代替路径语义验证。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-27 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 14:56)

### 一、现有测试问题

#### Issue-26：`DeprecationsMetadata` 只检查 native `UFunction` metadata，完全没有验证 Angelscript 暴露面的弃用提示

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.DeprecationsMetadata` |
| 行号范围 | 629-643 |
| 问题描述 | 用例直接 `FindObject<UFunction>(nullptr, TEXT("/Script/Niagara.NiagaraComponent:SetNiagaraVariableLinearColor"))`，然后只断言 native `UFunction` 仍带有 `DeprecatedFunction` 和 `DeprecationMessage` metadata。它没有查询 Angelscript type system，也没有编译脚本调用该 API 来验证脚本侧是否会暴露同样的弃用提示。换句话说，这条“binding parity”测试验证的是 UE 原生元数据本身，而不是绑定层有没有把弃用信息正确传到脚本表面。 |
| 影响 | 如果绑定生成或 metadata 转写逻辑回归，导致脚本侧不再标记 deprecated、提示文案丢失或提示对象换成别的方法，而 native `UFunction` metadata 仍然存在，这条测试仍会稳定绿灯。真正的脚本用户体验退化因此不会被当前 parity 套件发现。 |
| 修复建议 | 把断言升级到脚本表面：优先编译一个最小 Angelscript snippet 调用 `SetNiagaraVariableLinearColor`，断言编译诊断里出现预期的弃用 warning/message；若当前测试基础设施能读 script metadata，也应同时检查对应 script declaration 上的 deprecated 标记和 message，而不是只看 native `UFunction`。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-26 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

## 测试审查 (2026-04-08 13:50)

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-13` / `Issue-14` / `Issue-15` / `Issue-16` |
| 新增测试建议 | `NewTest-11` / `NewTest-12` / `NewTest-13` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-13 |
| AntiPattern | 1 | Issue-15 |
| FlakyRisk | 1 | Issue-16 |
| MissingCleanup | 1 | Issue-14 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:55) 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-17` / `Issue-18` / `Issue-19` / `Issue-20` |
| 新增测试建议 | `NewTest-14` / `NewTest-15` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-20 |
| AntiPattern | 1 | Issue-19 |
| MissingCleanup | 1 | Issue-17 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-08 13:55)

### 一、现有测试问题

#### Issue-17：前半组 `BindConfig` 用例持续向全局 bind registry 注入 GUID bind，却没有任何恢复或快照

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.GlobalDisabledBindNames` / `EngineDisabledBindNames` / `UnnamedBindBackwardCompatibility` / `StartupBindInfoPreservesOrder` / `StartupPathMergesDisabledBindNames` |
| 行号范围 | 258-490 |
| 问题描述 | 这 5 个用例都通过 `FAngelscriptBinds::FBind` 注册新的全局 bind（272-279、336-339、374-377、424-425、472-474 行），而 `FBind` 构造只调用 `RegisterBinds(...)`，没有对应析构反注册。测试为了避免重名，甚至故意用 `FGuid` 生成唯一 bind 名；但文件前半段没有 `ResetBindState()`、也没有 baseline snapshot/restore，所以每跑一次都会把新 bind 永久追加到进程级 registry 和 `GetBindInfoList()`。 |
| 影响 | 这些用例会把执行环境越跑越脏，后续 `GetAllRegisteredBindNames()` / `GetBindInfoList()` / startup bind 顺序与耗时都会被测试自身改写。跨轮 RalphLoop、本地反复运行或同进程多套测试时，bind 数量和排序持续膨胀，导致顺序相关误报、性能失真和定位困难。 |
| 修复建议 | 不要依赖 GUID 名称“绕开冲突”。给这组测试补专用 bind-state fixture：进入前 snapshot `FAngelscriptBindState`，退出时完整 restore；或者把这些用例改到 isolated helper 中，并在每条测试结束时恢复原始 registry，而不是让 `FBind` 注册结果泄漏到全进程后续测试。 |

#### Issue-18：`LevelStreamingCompile` 写了 helper 可见性断言却完全不参与 pass/fail

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.LevelStreamingCompile` |
| 行号范围 | 513-531 |
| 问题描述 | 用例在 527-528 行读取 `UAngelscriptLevelStreamingLibrary` 的 `TypeInfo` 并调用 `TestNotNull("... should be visible ...", HelperTypeInfo)`，但返回值随后被直接忽略；函数最终只返回 `bHasMethod`。这意味着即使 helper library 整体从脚本类型系统里消失，只要 `ULevelStreaming::GetShouldBeVisibleInEditor()` 还在，测试仍然会通过。 |
| 影响 | `LevelStreaming` 相关 helper 暴露面回归会被静默漏掉，当前用例给出的只是 `ULevelStreaming` 单一方法存在性，而不是它声称的“binding parity”。这会让脚本侧真正依赖 helper library 的调用在生产中断掉后仍保持绿灯。 |
| 修复建议 | 如果 helper library 可见性是正式合同，就把 `HelperTypeInfo` 纳入最终返回条件，并进一步编译一个最小 snippet 调用 helper 方法验证声明可用；如果它只是调试输出，就删掉这条伪断言，避免制造“看起来测了、实际上不影响结果”的假覆盖。 |

#### Issue-19：`PopulatesClassFuncMaps` 用固定总量阈值 `> 1000` 代替真实生成合同，既粗又脆

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.PopulatesClassFuncMaps` |
| 行号范围 | 150-196 |
| 问题描述 | 用例先把整个 `ClassFuncMaps` 的 entry 总数累计成 `TotalFunctionEntryCount`，然后只要求它“大于 1000”。这个阈值没有和当前引擎版本、启用模块集或生成策略绑定；它既不能定位具体哪一类 generated entry 缺失，也会把正常的 API 收缩/模块裁剪误判成失败。后面的细化断言只落在 `AActor::GetActorTimeDilation` 和“至少有一条 callable actor entry”上，覆盖面仍然非常窄。 |
| 影响 | 一方面，大批关键 class/function 退化时只要总数还高于 1000，该测试仍会给绿灯；另一方面，只要 UE 升级、插件裁剪或生成策略优化让总数自然下降到阈值附近，测试就会因为快照漂移而转红，难以区分真实回归和正常演进。 |
| 修复建议 | 删除魔法阈值，改成显式样本合同：按模块/类挑一组 generated entry，分别断言 map 存在、entry 存在、`FuncPtr`/`Caller` 或 reflective fallback 状态符合预期；若确实要保留聚合校验，应改为与 `AS_FunctionTable_Summary.json` 的结构化字段对账，而不是写死一个裸总数。 |

#### Issue-20：`PerformanceArtifactGeneration` 只做字符串包含检查，没有验证 metrics artifact 的 JSON 结构

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerformanceArtifactTests.cpp` |
| 测试名 | `Angelscript.TestModule.Core.Performance.ArtifactGeneration` |
| 行号范围 | 15-28 |
| 问题描述 | 用例写完 `metrics.json` 后，只检查文件存在、目录存在、能读取文本，以及文本里包含 `artifact.generation.seconds` 和 `RunId` 两个子串。它没有解析 JSON，也没有验证 `metrics` 数组、`median`、sample 数量、notes 或 test-group 字段。只要文件里恰好残留这两个字符串，哪怕 JSON 结构损坏、字段缺失或数值写错，测试仍会通过。 |
| 影响 | 性能产物生成一旦回归成“格式上可读文件但 schema 不兼容”，下游聚合脚本和报表会先坏掉，而这条回归测试仍然给绿灯。它当前只能发现“文件完全没写出来”，发现不了“写出来但内容不可消费”。 |
| 修复建议 | 把读取后的文本改为 JSON 解析断言：验证根对象可反序列化，`runId` 与 test group 匹配，`metrics` 数组长度为 1，`metricName == artifact.generation.seconds`，`samples == [0.1, 0.2, 0.3]`，`median == 0.2`，并校验 notes 包含预期文案。 |

### 二、需要新增的测试

#### NewTest-14：给 `AngelscriptSkipBinds.cpp` 补齐默认 skip list 启动回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSkipBinds.cpp` |
| 关联函数 | `Bind_Skip` startup bind lambda（`FAngelscriptBinds::AddSkipEntry(...)` / `AddSkipClass(...)`） |
| 现有测试覆盖 | 现有测试只覆盖动态 `AddSkipEntry` / clone 隔离；没有任何用例验证 runtime 默认 skip list 是否在 startup bind 后真实注册 |
| 风险评估 | 一旦默认 skip 项漏注册，之前被明确屏蔽的函数和类会重新暴露到 binding surface，生成表可能重新产出不支持的 API，进而引入编译错误、反射 fallback 漏洞或错误脚本入口 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.SkipBinds.DefaultSkipListIsRegistered` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSkipBindsTests.cpp` |
| 场景描述 | 在 clean full engine 启动后，直接检查默认 skip registry：函数级验证 `StaticMesh/GetMinLODForQualityLevels`、`StaticMesh/SetMinLODForQualityLevels`、`SkeletalMesh/GetMinLODForQualityLevels`、`SkeletalMesh/SetMinLODForQualityLevels`、`SourceEffectEQPreset/SetSettings`，类级验证 `ClothingSimulationInteractorNv`、`NiagaraPreviewGrid`、`GameplayCamerasSubsystem`、`AsyncAction_PerformTargeting`；再补一组 control 名称证明不是“所有条目都被跳过” |
| 输入/前置 | `ASTEST_CREATE_ENGINE_FULL()`；control case 可用 `StaticMesh/BuildNanite` 与 `Actor` 之类未在 skip list 中的名称 |
| 期望行为 | 所有 `Bind_Skip` 中列出的 entry/class 都返回 `true`；control entry 与 control class 返回 `false`；重复读取不会新增额外 skip 项或改变查询结果 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL()` + `ASTEST_BEGIN_FULL` + `FAngelscriptBinds::CheckForSkipEntry` / `CheckForSkipClass` |
| 优先级 | P1 |

#### NewTest-15：覆盖 `FAngelscriptDocs` 的缓存写入、缺省查询与 `UFunction` 反查

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` |
| 关联函数 | `FAngelscriptDocs::AddUnrealDocumentation(...)` / `AddUnrealDocumentationForType(...)` / `AddUnrealDocumentationForProperty(...)` / `AddDocumentationForGlobalVariable(...)` / `GetUnrealDocumentation(...)` / `GetFullUnrealDocumentation(...)` / `LookupAngelscriptFunction(...)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 文档缓存一旦把 tooltip/category/UFunction 指针或 type/property/global-variable 索引写错，编辑器内 tooltip、文档导出和脚本帮助查询都会静默退化，当前自动化没有任何报警 |
| 建议测试名 | `Angelscript.TestModule.Engine.Docs.DocumentationCachesRoundTripAndMissingIdsReturnEmpty` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocsTests.cpp` |
| 场景描述 | 记录四类 documentation count 的基线后，使用一组不会与现有条目冲突的高位 id，分别写入 function/type/property/global-variable 文档；function case 绑定已存在的 `UFunction*`（例如 `AActor::K2_DestroyActor`），再读取 tooltip、full-doc、lookup 和 count；同时对未写入的 id 做缺省查询 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；唯一 `FunctionId` / `TypeId` / `GlobalVariableId`；property offset 可用固定整数；已存在的 native `UFunction*` |
| 期望行为 | 四个 count 都相对基线各增加 1；`GetUnrealDocumentation()` 返回原始 tooltip；`GetFullUnrealDocumentation()` 返回原始 tooltip/category 且 `Function` 指针与写入值同址；`LookupAngelscriptFunction()` 返回同一 `UFunction*`；type/property/global-variable getter 都返回写入文案；未命中的 id 返回空字符串或 `nullptr` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `ASTEST_BEGIN_SHARE_CLEAN` + baseline-count snapshot |
| 优先级 | P3 |

---

## 测试审查 (2026-04-08 13:47)

### 二、需要新增的测试

#### NewTest-11：覆盖 `FAngelscriptBindDatabase` 的 Save/Load/Clear 持久化合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` |
| 关联函数 | `FAngelscriptBindDatabase::Save(...)` / `Load(...)` / `Clear()` / `GetSourceHeader(...)` |
| 现有测试覆盖 | `Runtime/Tests` 只覆盖 engine-local bind database 隔离计数，没有任何持久化、header sidecar 或 clear 行为验证 |
| 风险评估 | `Binds.Cache` round-trip 一旦漏字段、header sidecar 丢失或 `Clear()` 残留旧数据，cooked/runtime bind 恢复会静默失真，最终表现为找不到绑定或错误头文件路径，当前自动化不会报警 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindDatabase.SaveLoadRoundTripsClassesAndHeaders` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindDatabaseTests.cpp` |
| 场景描述 | 在 `FAngelscriptEngineScope` 下取得 `Engine.GetBindDatabaseForTesting()`，手工填充一个 `FAngelscriptClassBind`（例如 `AActor`）、一个 `FAngelscriptStructBind`（例如 `FHitResult`）和带方法/属性元数据的样本；保存到 `Saved/Automation/BindDatabase/<Guid>/Binds.Cache`，随后执行 `Clear()`、`Load(Path, false)`，最后再执行 `Clear()`、`Load(Path, true)` |
| 输入/前置 | temp cache 路径、样本 class/struct bind、样本 `FAngelscriptMethodBind` 与 `FAngelscriptPropertyBind`、已存在的 `UClass`/`UScriptStruct` 对象路径 |
| 期望行为 | `Save()` 后 `Binds.Cache` 与 `Binds.Cache.Headers` 都存在；第一次 `Clear()` 后 `Classes`/`Structs`/`HeaderLinks` 全空；`Load(Path, false)` 后 class/struct 数量、`Declaration`、`GeneratedName`、`bStaticInScript` 等字段完整 round-trip，且 `HeaderLinks` 仍为空；第二次 `Load(Path, true)` 后 `HeaderLinks` 包含样本 `UClass`/`UScriptStruct`，映射值为非空头文件路径 |
| 使用的 Helper | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` + 临时目录 helper |
| 优先级 | P0 |

#### NewTest-12：给 `AAngelscriptGASCharacter` 补齐 input bridge 与 gameplay tag 转发回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGASCharacter.cpp` |
| 关联函数 | `AAngelscriptGASCharacter::AAngelscriptGASCharacter(...)` / `SetupPlayerInputComponent(...)` / `GetAbilitySystemComponent()` / `GetOwnedGameplayTags(...)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 角色基类若不创建 replicated ASC、`SetupCharacterInput` 不被转发，或 gameplay tag 查询不再透传到 ASC，脚本角色会表面可继承但输入与 GAS tag 逻辑在运行时失效 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.Character.ForwardsInputSetupAndOwnedTags` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASActorBaseTests.cpp` |
| 场景描述 | 编译一个脚本子类 `AAutomationGASCharacter` 继承 `AAngelscriptGASCharacter`，增加 `int SetupCalls` 属性并在 `BlueprintOverride void SetupCharacterInput(UInputComponent PlayerInputComponent)` 中自增；生成后用 `FActorTestSpawner` spawn 角色，构造 `UInputComponent` 并向 `AbilitySystem` 添加一个 loose gameplay tag |
| 输入/前置 | share-clean engine、脚本角色类、`UInputComponent`、测试 tag `GameplayTag.Tests.GASCharacterOwned` |
| 期望行为 | 角色上的 `AbilitySystem` 非空且 `GetIsReplicated()` 为 true；`GetAbilitySystemComponent()` 返回与成员同一地址；调用 `SetupPlayerInputComponent()` 后 `SetupCalls == 1`；调用 `GetOwnedGameplayTags()` 返回的容器包含 `GameplayTag.Tests.GASCharacterOwned` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileScriptModule` + `FActorTestSpawner` + `ReadPropertyValue` |
| 优先级 | P1 |

#### NewTest-13：覆盖 `AAngelscriptGASActor` / `AAngelscriptGASPawn` 的 ASC 创建与 pawn input 转发

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGASActor.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGASPawn.cpp` |
| 关联函数 | `AAngelscriptGASActor::AAngelscriptGASActor(...)` / `AAngelscriptGASActor::GetAbilitySystemComponent()` / `AAngelscriptGASPawn::AAngelscriptGASPawn(...)` / `AAngelscriptGASPawn::SetupPlayerInputComponent(...)` / `AAngelscriptGASPawn::GetAbilitySystemComponent()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 若 actor/pawn 基类没有稳定创建 `AbilitySystem` 子对象，或 pawn 没有把 `SetupPlayerInputComponent()` 转发到脚本侧 `SetupPawnInput`，所有继承这些基类的脚本能力角色都会在初始化阶段埋下隐性故障 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.ActorBase.CreatesReplicatedAbilitySystemAndPawnForwardsInput` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASActorBaseTests.cpp` |
| 场景描述 | 先用 native 或最小脚本子类 spawn `AAngelscriptGASActor`，再编译一个继承 `AAngelscriptGASPawn` 的脚本类，覆写 `SetupPawnInput` 把 `SetupCalls` 自增；随后为 pawn 构造输入组件并触发 `SetupPlayerInputComponent()` |
| 输入/前置 | share-clean engine、actor/pawn test class、`UInputComponent`、可读取 `SetupCalls` 的脚本属性 |
| 期望行为 | actor 与 pawn 都自动拥有非空 `UAngelscriptAbilitySystemComponent`，且 `GetAbilitySystemComponent()` 返回成员本身；两个 ASC 都处于 replicated 状态；pawn 调用 `SetupPlayerInputComponent()` 后脚本侧 `SetupCalls == 1`，重复调用时计数按调用次数递增 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileScriptModule` + `FActorTestSpawner` + `ReadPropertyValue` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:42)

### 一、现有测试问题

#### Issue-16：`FileSystem` 全套用例共用同一物理目录并递归删除 legacy 路径，存在明显互相踩文件风险

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.*` |
| 行号范围 | 16-37, 81-544 |
| 问题描述 | 辅助函数把所有文件系统测试都固定到 `Saved/Automation/FileSystem` 和 `ProjectDir()/Script/Automation/FileSystem` 两个共享根目录；每个用例开头/结尾都直接 `DeleteDirectory(..., true)` 递归清空这两个目录。目录名不带 test id，也没有 scoped temp root，因此任何并发运行、失败中断或上一次残留都会影响下一条用例；更糟的是，legacy 清理路径位于项目树下，不是一次性的临时目录。 |
| 影响 | 一条测试可能删除另一条测试刚写入的脚本文件，导致 discovery 计数、rename/remap 和 mixed-recovery 行为出现顺序相关失败。若开发者本地恰好在 `Script/Automation/FileSystem` 下放了调试脚本，这套测试还会直接把它们删掉。 |
| 修复建议 | 改成每个用例独立的 GUID 子目录，例如 `Saved/Automation/FileSystem/<TestName>/<Guid>/...`，并通过 helper 返回该次运行的 root path；legacy 路径如需覆盖，只在临时沙箱中显式注入给 `AllRootPaths`，不要直接删 `ProjectDir()` 下的共享目录。再配一个 `ON_SCOPE_EXIT` 式 scoped cleanup，避免依赖手写多处分支清理。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-16 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:40)

### 一、现有测试问题

#### Issue-15：`ReflectiveFallbackStats` 把当前 fallback 分布写死成测试合同，会把“实现改进”误判成失败

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.ReflectiveFallbackStats` |
| 行号范围 | 360-456 |
| 问题描述 | 用例统计完整 `ClassFuncMaps` 后，强制断言 `ReflectiveCount > 0`、`UnresolvedCount > 0`，并进一步要求 `AIModule`、`GameplayTags`、`UMG` 三个模块都必须存在 reflective fallback。它验证的是“当前生成器还有多少 fallback、分布在哪些模块”，而不是“fallback 机制是否正确”。一旦某个模块后来补齐 direct bind、把 reflective fallback 消掉，测试就会因为实现变好而转红。 |
| 影响 | 该用例把生成质量演进锁死在当前快照，阻碍 direct-bind 覆盖率提升；测试失败也很难区分是真回归还是正常重构。长期看，这类断言会让团队为了维持绿灯而保留旧 fallback 分布，而不是继续消除 stub/reflective path。 |
| 修复建议 | 把统计分布从 pass/fail 合同降级为诊断输出。正式断言应改为基于精选样本的行为合同：例如验证至少一条已知 reflective entry 仍可通过 reflective path 调用、至少一条 handwritten GAS entry 仍保持 direct bind、以及总体 direct/stub/fallback 统计字段自洽；不要再要求固定模块名必须保留 fallback 或 `UnresolvedCount` 必须大于零。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-15 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:39)

### 一、现有测试问题

#### Issue-14：多条 `BindConfig` 用例直接清空全局 bind registry，却没有恢复测试前基线

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.GeneratedBlueprintCallableEntriesPopulateClassMaps` / `AddFunctionEntryPreservesFirstRegistration` / `FunctionLevelScriptMethodUsesFirstParameterAsMixin` / `CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` / `OverloadedExportedFunctionsCanRecoverDirectBind` / `InlineDefinitionFunctionsCanRecoverDirectBind` / `InlineOutRefFunctionsCanRecoverDirectBind` |
| 行号范围 | 493-847 |
| 问题描述 | 这组用例在测试开始和 `ON_SCOPE_EXIT` 中反复调用 `FAngelscriptBinds::ResetBindState()`。实现侧 `ResetBindState()` 会直接执行 `GetBindState() = FAngelscriptBindState();`，即清空进程级 bind registry、class function map 和排序后的 bind 数组。文件里没有任何快照/恢复逻辑；退出时只会再次清空，而不是恢复测试前的 production bind 状态。 |
| 影响 | 这些测试结束后，后续依赖 `FAngelscriptBinds::GetClassFuncMaps()`、`GetBindInfoList()` 或已注册 startup binds 的用例会在被污染的全局状态上运行，产生顺序相关失败或假绿。当前文件里看似做了 cleanup，实际留下的是“空 registry”而不是原始基线。 |
| 修复建议 | 不要把 `ResetBindState()` 当作可逆 cleanup。新增 test helper 对 `FAngelscriptBindState` 做显式 snapshot/restore，或在测试结束时重新执行完整 startup bind 初始化以恢复基线；更稳妥的做法是把需要改写 bind registry 的测试放进专用 isolated helper 中，确保进入前保存原始 registry，离开时完整还原，而不是只做二次清空。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-14 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:37)

### 一、现有测试问题

#### Issue-13：`FullDestroyAllowsAnnotatedRecreate` 只验证第二轮能重新生成类，没有证明第一轮类/包已被真正回收

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.FullDestroyAllowsAnnotatedRecreate` |
| 行号范围 | 282-351 |
| 问题描述 | 用例先编译 `ARecreateAnnotatedActorA`，随后只调用 `DiscardModule("RecreateAnnotatedActorA")`、`CollectGarbage()`、`FirstEngine.Reset()`，然后断言 `FAngelscriptType::GetTypes().Num() == 0` 并继续创建 `ARecreateAnnotatedActorB`。整个过程没有保留第一轮 `UClass`/`UPackage` 的弱引用，也没有检查 `FindGeneratedClass(Engine, "ARecreateAnnotatedActorA")`、`FindObject<UClass>()` 或包对象在销毁后是否真的失效，因此它验证的是“第二次还能再编译一个不同名字的类”，不是“第一轮 annotated 产物已被完整清理”。 |
| 影响 | 即使 full-engine teardown 仍然把旧 generated class、package 或 UObject 残留在内存里，这个用例也会继续绿灯，因为第二轮使用的是全新的类名 `ARecreateAnnotatedActorB`。这会让 annotated 生成路径最关键的 cleanup 回归长期潜伏。 |
| 修复建议 | 在第一次编译后保存 `TWeakObjectPtr<UClass>` 与对应 outer package 的弱引用；`DiscardModule + CollectGarbage + FirstEngine.Reset()` 后显式断言旧类弱引用失效、`FindObject<UClass>(ANY_PACKAGE, TEXT(\"ARecreateAnnotatedActorA\")) == nullptr`，并验证旧 package 不再 rooted/可解析。第二轮仍可保留“重新生成新类成功”的正向断言，但必须把 cleanup 断言升级成用例核心。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-13 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:18)

### 二、需要新增的测试

#### NewTest-01：补齐 `CreateForTesting` clone/fallback 生命周期正确性

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CreateForTesting(...)` / `CreateCloneFrom(...)` / `CreateTestingFullEngine(...)` |
| 现有测试覆盖 | 有测试但缺少 create-mode 正确性；`AngelscriptEngineCoreTests.cpp` 只做指针级 smoke，`AngelscriptEnginePerformanceTests.cpp` 只测耗时 |
| 风险评估 | clone/full 选择、source engine 关联或 shared-state adoption 回归时，现有测试只能看到性能变化，看不到功能错误 |
| 建议测试名 | `Angelscript.TestModule.Engine.Lifecycle.CreateForTestingUsesScopedSourceOrFallsBackToFull` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineLifecycleModeTests.cpp` |
| 场景描述 | 先在 `FAngelscriptEngineScope` 内创建一台 full engine，再调用 `CreateForTesting(..., Clone)`；随后离开 scope，在无 current engine 的情况下再次调用同一入口 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 建立 source engine；第一段显式安装 `FAngelscriptEngineScope`，第二段确保 `FAngelscriptEngine::TryGetCurrentEngine() == nullptr` |
| 期望行为 | scoped case 断言返回实例 `GetCreationMode() == Clone`、`OwnsEngine() == false`、`GetSourceEngine() == &SourceEngine`、`GetScriptEngine() == SourceEngine.GetScriptEngine()`；no-current-engine case 断言返回实例 `GetCreationMode() == Full`、`OwnsEngine() == true`、`GetSourceEngine() == nullptr`、`GetScriptEngine() != nullptr` |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` |
| 优先级 | P1 |

#### NewTest-02：给 `UAngelscriptAbilitySystemComponent::RegisterAttributeSet` 建立“去重 + 立即回放”回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `RegisterAttributeSet(...)` / `OnAttributeSetRegistered(...)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | attribute set 重复注册、回调对象注册失败、已有 set 不回放等问题会直接破坏 GAS 初始化合同，但当前不会被任何自动化发现 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.RegisterAttributeSetReplaysExistingSets` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemTests.cpp` |
| 场景描述 | 创建带 `UAngelscriptAbilitySystemComponent` 的 native test actor，先注册一个 test `UAngelscriptAttributeSet`，再把带 `UFUNCTION` 计数器的 listener object 通过 `OnAttributeSetRegistered` 挂上，最后重复注册同一 attribute set class |
| 输入/前置 | native test actor、native test attribute set、listener UObject；必要时用 `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 建立运行环境 |
| 期望行为 | 第一次 `RegisterAttributeSet` 返回非空 set；第二次返回同一对象地址；listener 在注册时立即收到一次已有 set 回放；`GetSpawnedAttributes()` 中同类 attribute set 仍只有一份 |
| 使用的 Helper | `FAngelscriptTestFixture` + native listener test double |
| 优先级 | P0 |

#### NewTest-03：给 `GiveAbility`/`OnAbilityGiven`/取消路径补齐 spec 内容和委托断言

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `BP_GiveAbility(...)` / `GiveAbility_Internal(...)` / `TryActivateAbilitySpec(...)` / `CancelAbility(...)` / `CancelAbilityByHandle(...)` / `OnGiveAbility(...)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | ability spec 的 `Level`、`InputID`、`SourceObject` 或委托广播一旦错绑，脚本能力系统会表面可用但运行时行为错乱 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.GiveAbilityPopulatesSpecAndBroadcastsDelegates` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemTests.cpp` |
| 场景描述 | 创建轻量 native `UGameplayAbility` test class，给 ASC 授予 ability 并尝试激活，再通过 handle/class 取消 |
| 输入/前置 | `UAngelscriptAbilitySystemComponent`、native test ability、`OptionalInputID = 7`、`OptionalSourceObject = TestSourceObject`、绑定 `OnAbilityGiven` 监听器 |
| 期望行为 | `BP_GiveAbility` 返回有效 handle；`HasAbility(TestAbility)` 为真；从 `ActivatableAbilities` 读出的 spec `Level == 1/指定值`、`InputID == 7`、`SourceObject == TestSourceObject`；`OnAbilityGiven` 只广播一次且 ability class 匹配；激活后 `IsAbilityActive(TestAbility)` 为真，调用 `CancelAbilityByHandle` 或 `CancelAbility(TestAbility)` 后变为假 |
| 使用的 Helper | `FAngelscriptTestFixture` + native `UGameplayAbility` test double |
| 优先级 | P0 |

#### NewTest-04：覆盖 `UAngelscriptAttributeSet` 的属性名初始化与 replication blacklist

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAttributeSet.cpp` |
| 关联函数 | `PostInitProperties()` / `GetLifetimeReplicatedProps(...)` / `TryGetGameplayAttribute(...)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | attribute 名字填充错误、黑名单失效或属性查找回归时，复制和脚本属性访问会静默出错 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AttributeSet.InitializesAttributeNamesAndHonorsReplicationBlacklist` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAttributeSetTests.cpp` |
| 场景描述 | 声明 native test attribute set，包含两个 `FAngelscriptGameplayAttributeData` net 属性 `Health` 和 `Mana`，并把 `Mana` 加入 `ReplicatedAttributeBlackList` |
| 输入/前置 | 构造 test attribute set 实例，准备 `TArray<FLifetimeProperty>` 接收复制属性列表 |
| 期望行为 | 构造后 `Health.AttributeName == "Health"`、`Mana.AttributeName == "Mana"`；`TryGetGameplayAttribute(TestSetClass, "Health", OutAttr)` 返回 true，缺失属性返回 false；`GetLifetimeReplicatedProps` 只包含 `Health`，不包含 `Mana` 和 `ReplicatedAttributeBlackList` |
| 使用的 Helper | `FAngelscriptTestFixture` + native attribute-set test double |
| 优先级 | P1 |

#### NewTest-05：给 `WaitGameplayTagQueryOnActor` 建立运行时回归，阻止“公开 API 永远返回 nullptr”

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h` |
| 关联函数 | `UAngelscriptAbilityAsyncLibrary::WaitGameplayTagQueryOnActor(...)` |
| 现有测试覆盖 | `GeneratedFunctionTable` 只检查部分 async wrapper 的函数表可见性；`WaitGameplayTagQueryOnActor` 无运行时覆盖 |
| 风险评估 | 当前公开 wrapper 看起来可调用，但如果内部保持 stub/nullptr，脚本和蓝图侧会在运行时静默失效，现有测试无法报警 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.Async.WaitGameplayTagQueryCreatesTask` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAsyncLibraryTests.cpp` |
| 场景描述 | 创建带 ASC 的 native test actor，构造简单 `FGameplayTagQuery`，调用 `WaitGameplayTagQueryOnActor` 后再给 actor 添加匹配 tag |
| 输入/前置 | target actor、单标签 `FGameplayTagQuery`、`TriggerCondition = WhenTrue`、`bTriggerOnce = true` |
| 期望行为 | 调用后立即返回非空 async task；任务应绑定到目标 actor/ASC；添加匹配 tag 后触发一次完成回调，不匹配 tag 不触发。若设计上暂不支持，也应至少以显式错误/ensure 表达，而不是静默返回 `nullptr` |
| 使用的 Helper | `FAngelscriptTestFixture(ETestEngineMode::ProductionLike)` + native ASC actor fixture |
| 优先级 | P0 |

#### NewTest-06：为本地 gameplay cue wrapper 补齐事件顺序和空指针保护测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameplayCueUtils.h` |
| 关联函数 | `AddLocalGameplayCue(...)` / `RemoveLocalGameplayCue(...)` / `ExecuteLocalGameplayCue(...)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | `OnActive`/`WhileActive` 顺序、`Removed` 分发或 null target guard 回归后，cue 生命周期会错乱且难以从普通绑定 smoke 中看出来 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.GameplayCueUtils.DispatchesExpectedCueEvents` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGameplayCueUtilsTests.cpp` |
| 场景描述 | 注册 native cue recorder，分别调用 `AddLocalGameplayCue`、`ExecuteLocalGameplayCue`、`RemoveLocalGameplayCue`，并额外验证 `TargetActor == nullptr` 时 no-op |
| 输入/前置 | target actor、固定 gameplay cue tag、可记录事件顺序的 native notify/manager test double |
| 期望行为 | `AddLocalGameplayCue` 依次记录 `OnActive`、`WhileActive`；`ExecuteLocalGameplayCue` 记录 `Executed`；`RemoveLocalGameplayCue` 记录 `Removed`；null target 不崩溃且不会新增记录 |
| 使用的 Helper | `FAngelscriptTestFixture(ETestEngineMode::ProductionLike)` + native cue recorder |
| 优先级 | P1 |

#### NewTest-07：覆盖 `GameplayEffectUtils` 的属性捕获与 tag 拷贝 helper

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameplayEffectUtils.h` |
| 关联函数 | `CaptureGameplayAttribute(...)` / `MakeGameplayModifierEvaluationData(...)` / `UGameplayEffectExecutionParametersMixinLibrary::SetCapturedSourceTagsFromSpec(...)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | gameplay effect helper 一旦把属性解析错、invalid name 不兜底、source/target tag 拷贝断裂，执行计算会在运行时产生难追踪的错误数值 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.GameplayEffectUtils.CapturesAttributesAndTags` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGameplayEffectUtilsTests.cpp` |
| 场景描述 | 使用带 `Health` 属性的 test attribute set class 调 `CaptureGameplayAttribute`，再构造带 source/target captured tags 的 `FGameplayEffectSpec` 调 `SetCapturedSourceTagsFromSpec` |
| 输入/前置 | valid attribute 名 `Health`、invalid attribute 名 `MissingAttr`、带 source/target tags 的 `FGameplayEffectSpec` |
| 期望行为 | valid case 返回的 `FGameplayEffectAttributeCaptureDefinition.AttributeToCapture.IsValid()` 为真且属性名为 `Health`；invalid case 返回 default/invalid capture；`SetCapturedSourceTagsFromSpec` 后 `WrappedParams.SourceTags` 与 `WrappedParams.TargetTags` 都包含 spec 中的聚合 tags；`MakeGameplayModifierEvaluationData` 生成的 magnitude/op 与输入一致 |
| 使用的 Helper | `FAngelscriptTestFixture` + native test attribute-set class |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 3 | NoTestForSource: 2, MissingErrorPath: 1 |
| P1 | 3 | MissingScenario: 1, NoTestForSource: 2 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:18)

### 一、现有测试问题

#### Issue-07：`AngelscriptEngineCoreTests` 没有覆盖 `FAngelscriptEngine` 的关键创建模式与生命周期合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.CreateDestroy` / `CompileSnippet` / `ExecuteSnippet` / `LastFullDestroyClearsTypeState` / `FullDestroyAllowsCleanRecreate` / `FullDestroyAllowsAnnotatedRecreate` |
| 行号范围 | 58-352 |
| 问题描述 | 6 个用例只覆盖了“能拿到 engine 指针”“能编译/执行一个最小片段”“最后一个 full engine 销毁后类型表清空”和“重新创建后还能编译模块”。它们没有验证 `CreateForTesting(Clone)` 在有 scoped source engine 时是否真的走 clone 路径、无 current engine 时是否 fallback full，也没有断言 `GetCreationMode()`、`OwnsEngine()`、`GetSourceEngine()`、shared `asIScriptEngine*` adoption 等关键生命周期合同。 |
| 影响 | `FAngelscriptEngine` 最容易回归的 create-mode 选择、clone/full owner 语义和 source-engine 关联都处于测试盲区；只要最小编译执行还能跑，生命周期实现就可能带着严重缺陷通过现有套件。 |
| 修复建议 | 保留现有 smoke 用例，但把 create-mode/owner/source-contract 单独拆成 focused lifecycle tests；至少补一条 scoped clone + no-current fallback 的功能回归，并对 `GetCreationMode()`、`OwnsEngine()`、`GetSourceEngine()`、`GetScriptEngine()` 做显式断言。可直接按本轮 `NewTest-01` 落地。 |

#### Issue-08：多份核心测试文件超过 500 行，已经违反单文件单职责规范

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.*` / `Angelscript.TestModule.Engine.GeneratedFunctionTable.*` / `Angelscript.TestModule.Parity.*` |
| 行号范围 | 1-850 / 1-778 / 1-695 |
| 问题描述 | 这三份文件分别约 850、778、695 行，明显超出规则要求的 300-500 行。并且每个文件都混合了多个主题：`BindConfig` 同时测 disabled bind、UHT 元数据、direct bind 恢复；`GeneratedFunctionTable` 同时测 runtime map、UHT cpp、json/csv 产物和 GAS 兼容；`EngineParity` 同时混放 type lookup、compile smoke 和运行时 parity。 |
| 影响 | 文件过长会放大 shared helper 污染、降低发现 cleanup/隔离问题的难度，并让后续补测试时更倾向继续堆积到同一文件里，维护成本持续恶化。 |
| 修复建议 | 按主题拆文件并控制到 300-500 行：`BindConfig` 至少拆成 disabled bind、signature metadata、direct-bind recovery 三组；`GeneratedFunctionTable` 拆成 runtime map、generated artifact、csv/report 三组；`EngineParity` 拆成 native type lookup、compile parity、execute parity 三组。公共 native test double 和文件系统 helper 放到 `Shared/`。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-07 |
| AntiPattern | 1 | Issue-08 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:13)

### 一、现有测试问题

#### Issue-04：`EngineParityTests` 多个用例只验证“能编译/有符号”，没有做到真正的 parity 断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.CollisionQueryParamsCompile` / `WorldCollisionCompile` / `SoftReferenceCompile` / `UserWidgetPaintCompile` / `RuntimeCurveLinearColorCompile` / `FVector2fCompile` 等 |
| 行号范围 | 230-570 |
| 问题描述 | 这组 parity 测试里大量用例只停留在 compile smoke。`CollisionQueryParamsCompile`、`WorldCollisionCompile`、`UserWidgetPaintCompile`、`RuntimeCurveLinearColorCompile` 只断言脚本能编译并拿到 `asIScriptFunction*`；`SoftReferenceCompile` 也只检查声明能生成；`FVector2fCompile` 虽然执行了脚本，但把 `Context->GetReturnFloat()` 读出来后直接 `(void)ReturnValue` 丢弃，没有任何数值断言。文件名和测试名都强调 parity，但断言粒度多数停在“表面可见”。 |
| 影响 | 绑定签名只要足够让编译器通过，运行时行为、默认值、数值结果和调用语义出错都不会被发现；这会让“绑定存在”和“绑定正确”被错误地等同。 |
| 修复建议 | 把 compile smoke 升级成 execute parity。优先把这几类测试改为 `ASTEST_COMPILE_RUN_*` 或显式 `Prepare/Execute` 后断言具体结果：例如 `FVector2fCompile` 断言返回值为 `5.0f`（带容差），`CollisionQueryParamsCompile` 断言 `Bits` 等于 `QueryOnly` 对应值，`RuntimeCurveLinearColorCompile` 断言 `AddDefaultKey` 后 key 数变化，`SoftReferenceCompile` 至少执行 `Get()` / `EditorOnlyLoadSynchronous()` 的 null-path 合同并验证返回类型。 |

#### Issue-05：`GeneratedFunctionTableTests` 把 UHT 产物目录硬编码为 `Win64/UnrealEditor`，存在明显平台/配置假设

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.EditorOutputsUseWithEditorGuard` / `SummaryOutput` / `CsvOutput` / `SkippedCsvOutput` / `SkippedReasonSummaryCsvOutput` / `MacroQualifiedDirectBindings` |
| 行号范围 | 242-266, 461-777 |
| 问题描述 | 多个用例直接把生成目录拼成 `Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT`，随后读取 `AS_FunctionTable_*.cpp`、`AS_FunctionTable_Summary.json` 和若干 csv。路径里同时硬编码了平台 `Win64` 和 target `UnrealEditor`，并假定测试运行前这些产物已经在该目录生成。 |
| 影响 | 这批测试对平台、target 配置和构建目录布局高度敏感；一旦在非 Win64、不同 target、不同中间目录布局或清理产物后的环境里运行，就会因为找不到文件而失败，或者更糟地误读旧产物导致假绿。 |
| 修复建议 | 改成基于当前构建环境解析 UHT 输出目录，而不是写死 `Win64/UnrealEditor`；至少应从模块/target 信息、`FPaths`、平台宏或实际文件搜索结果推导目录，并在读取前验证产物是当前轮生成。若无法保证构建前置条件，应把“需要现成 UHT 产物”显式建成 helper 和前置断言。 |

#### Issue-06：`SubsystemScenarioTests` 全部锁定“当前编译失败”，没有任何正向子系统场景验证

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `WorldSubsystem.Tick` / `WorldSubsystem.ActorAccess` / `GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 66-245 |
| 问题描述 | 四个“Scenario”用例都走同一模式：构造一个最小 `UScriptWorldSubsystem` 或 `UScriptGameInstanceSubsystem` 脚本，调用 `CompileModuleWithResult(...)`，然后断言 `bCompiled == false` 且 `CompileResult == ECompileResult::Error`。文件里没有 world/game instance 初始化，没有 `BP_Initialize`、`BP_Deinitialize`、`BP_Tick` 的运行时断言，也没有 actor/world 访问行为验证。 |
| 影响 | 该文件实际只是在回归“这个分支目前不支持 subsystem script generation”，而不是验证 subsystem 行为本身。当前测试套件因此对真正的 subsystem 生命周期、tick 接线和 world 访问零覆盖；一旦功能落地，这些用例也只会整体转红，却不能指出运行时合同哪里坏了。 |
| 修复建议 | 把“当前不支持、应编译失败”的检查拆到单独的 compile-guard 文件，避免占用 scenario 目录；当分支支持 subsystem 后，改用 `FActorTestSpawner`/`UGameInstance`/`UWorld` 真实初始化子系统，断言 `BP_Initialize`、`BP_Deinitialize`、`BP_Tick`、`GetWorld()`/actor 访问结果，并通过 native recorder 或 test object 记录回调次数。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-04 |
| FlakyRisk | 1 | Issue-05 |
| AntiPattern | 1 | Issue-06 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:26)

### 一、现有测试问题

#### Issue-09：`CompileSnippet` / `ExecuteSnippet` 在共享 engine 上直接创建 raw `asIScriptModule`，绕过 helper 且不做清理

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.CompileSnippet` / `Angelscript.TestModule.Engine.ExecuteSnippet` |
| 行号范围 | 75-151 |
| 问题描述 | 两个用例都用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE` 进入共享 engine，然后又手动创建第二层 `FAngelscriptEngineScope`，并直接通过 `Engine.GetScriptEngine()->GetModule(..., asGM_ALWAYS_CREATE)` 在底层 AngelScript engine 上建立 raw module。这样既绕过了 `BuildModule` / `ASTEST_COMPILE_RUN_*` 的统一 cleanup，也没有 `DiscardModule` 或 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 的 reset；`CompileSnippet` 与 `ExecuteSnippet` 生成的 raw module 因为不在 `Engine.GetActiveModules()` 里，测试框架后续也无法按 wrapper 级别清理。 |
| 影响 | 这两个最基础的 engine smoke test 会把未跟踪的 raw module 留在共享 VM 中，造成跨用例状态残留；一旦后续测试再次使用同名 raw module、枚举 module 数量，或依赖“共享 engine 只有 wrapper 跟踪模块”的假设，就可能出现顺序相关的误报。 |
| 修复建议 | 不要在共享 engine 上手写 raw `GetModule(..., asGM_ALWAYS_CREATE)`。改成 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `ASTEST_BEGIN_SHARE_CLEAN`，并优先使用 `ASTEST_BUILD_MODULE` / `ASTEST_COMPILE_RUN_INT` / `AngelscriptTestSupport::BuildModule` 这类 wrapper-aware helper；如果必须走 raw module，也要在 `ON_SCOPE_EXIT` 里显式 `DiscardModule` 并删除底层 discarded modules，同时删掉宏外多余的第二层 `FAngelscriptEngineScope`。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-09 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:27)

### 一、现有测试问题

#### Issue-11：`GeneratedBlueprintCallableEntriesPopulateClassMaps` 对两个代表性 generated entry 只验“存在”，没有验“可调用”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.GeneratedBlueprintCallableEntriesPopulateClassMaps` |
| 行号范围 | 493-552 |
| 问题描述 | 该用例先取 `AActor::K2_DestroyActor`、`UGameplayStatics::GetPlayerController`、`UASClass::IsDeveloperOnly` 三个代表函数，但最终只有 `IsDeveloperOnlyEntry` 使用 `IsFunctionEntryBound(...)` 做了强断言。`DestroyActorEntry` 和 `GetPlayerControllerEntry` 仅检查 map 中存在条目，没有检查 `FuncPtr` / `Caller` 是否真的绑定，也没有判断是否退化成 reflective fallback 或 unresolved stub。 |
| 影响 | 只要 UHT 仍然生成了键名，这个测试就会通过；即使 `K2_DestroyActor` 或 `GetPlayerController` 已经退化成 `ERASE_NO_FUNCTION()`、空 caller 或错误绑定，当前用例仍会给出绿灯，削弱了它对 generated blueprint-callable 回归的价值。 |
| 修复建议 | 对三个样本入口统一做强断言：至少分别检查 `IsFunctionEntryBound(*Entry)` 或预期的 `bReflectiveFallbackBound` 状态；如果设计上要求 direct bind，建议进一步比较 `FuncPtr.IsBound()` 与 `Caller.IsBound()`，并在可能时补一个最小调用 smoke，确认条目不只是“注册了名字”。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-11 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:32)

### 二、需要新增的测试

#### NewTest-08：给 `UAngelscriptAbilityTask` 补齐创建/激活/销毁生命周期回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTask.cpp` |
| 关联函数 | `UAngelscriptAbilityTask::CreateAbilityTask(...)` / `CreateAbilityTaskAndRunIt(...)` / `Activate()` / `TickTask(...)` / `OnDestroy(...)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | task 创建若没有正确绑定 `Ability`、`InstanceName`、等待标记或生命周期回调，脚本 GAS task 会表现成“对象创建成功但永远不激活/不 tick/不清理”，这类问题当前不会被任何自动化发现 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilityTask.CreateAndRunTaskInitializesLifecycle` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilityTaskTests.cpp` |
| 场景描述 | 定义 native `UGameplayAbility` test double 和 native `UAngelscriptAbilityTask` recorder subclass，分别调用 `CreateAbilityTask` 与 `CreateAbilityTaskAndRunIt`；前者在 `ReadyForActivation()` 前后比较激活状态，后者验证立即激活；随后手动调用 `TickTask(0.25f)` 和 `EndTask()`/`OnDestroy(false)` 路径 |
| 输入/前置 | native ASC owner actor、native ability instance、recording task subclass；fixture 采用 `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 保证 GAS 类型系统完整初始化 |
| 期望行为 | `CreateAbilityTask` 返回非空 task，`BP_GetAbility()`/`GetAbility()` 指向源 ability，`InstanceName` 等于输入值，未 `ReadyForActivation()` 前不会记录激活；`CreateAbilityTaskAndRunIt` 会立即记录一次激活；`SetIsTickingTask` / `SetIsPausable` / `SetIsSimulatedTask` 经 getter round-trip 正确；`TickTask(0.25f)` 记录同样的 delta；销毁路径只回调一次并保留 `bInOwnerFinished` 值 |
| 使用的 Helper | `FAngelscriptTestFixture` + native GAS ability/task recorder doubles |
| 优先级 | P1 |

#### NewTest-09：覆盖 `UAngelscriptGASAbility` 的 gameplay cue wrapper 转发与空输入保护

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGASAbility.cpp` |
| 关联函数 | `K2_ExecuteGameplayCue_Actor(...)` / `K2_ExecuteGameplayCue_Static(...)` / `K2_AddGameplayCue_*` / `K2_AddGameplayCueWithParams_*` / `K2_RemoveGameplayCue_*` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | `_Actor` / `_Static` 包装器一旦把 `GameplayCueTag`、`bRemoveOnAbilityEnd` 或 `FGameplayCueParameters` 转发错，脚本 ability 会表面可编译但运行时 cue 触发到错误 tag；null cue 输入若缺少 guard，还会把错误拖到运行期才暴露 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.Ability.CueWrappersForwardTagAndGuardNull` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilityTests.cpp` |
| 场景描述 | 定义 native test ability，覆写 tag-based `K2_ExecuteGameplayCue` / `K2_AddGameplayCue` / `K2_RemoveGameplayCue` 记录收到的 tag、参数和 remove-on-end 标志；再定义一个 `AGameplayCueNotify_Actor` test class 与一个 `UGameplayCueNotify_Static` test class，分别配置固定 `GameplayCueTag`，调用所有 `_Actor` / `_Static` wrapper |
| 输入/前置 | actor cue tag `GameplayCue.Tests.ActorCue`、static cue tag `GameplayCue.Tests.StaticCue`、一份带 `Instigator`/`RawMagnitude` 的 `FGameplayCueParameters`、`AddExpectedError` 捕获 null cue ensure |
| 期望行为 | valid actor/static case 都把 default-object 上的 `GameplayCueTag` 原样转发到 tag-based override；`AddGameplayCueWithParams_*` 保留原始 `FGameplayCueParameters`，`AddGameplayCue_*` 保留 `bRemoveOnAbilityEnd`；`RemoveGameplayCue_*` 记录正确 tag；传 `nullptr` cue class 时记录一次预期 ensure，且不会追加任何新的转发记录 |
| 使用的 Helper | native ability/cue recorder doubles + `AddExpectedError` |
| 优先级 | P1 |

#### NewTest-10：把 `Subsystem/` 目录从“编译失败 smoke”升级为真实 `GameInstanceSubsystem` 生命周期场景

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 关联函数 | `Initialize(...)` / `Deinitialize()` / `Tick(float)` / `GetCurrent()` / `HasAnyTickOwner()` |
| 现有测试覆盖 | 有测试但缺少正向运行场景；`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` 目前只验证脚本 subsystem 编译失败 |
| 风险评估 | subsystem adopt/own primary engine、tick owner 计数和 `GetCurrent()` 解析如果回归，当前 `Subsystem/` 目录不会给任何正向行为保护，最核心的生命周期合同只能靠其他低层测试间接发现 |
| 建议测试名 | `Angelscript.TestModule.GameInstanceSubsystem.InitializeAdoptsOrOwnsEngineAndTicksIt` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptGameInstanceSubsystemRuntimeTests.cpp` |
| 场景描述 | 用真实 `UGameInstance` / `UWorld` 构建 subsystem：第一段在外层先安装 `FAngelscriptEngineScope`，验证 `Initialize()` adopt 当前 engine；第二段不提供当前 engine，验证 subsystem 创建并拥有 `OwnedEngine`；两段都调用 `Tick()` 与 `Deinitialize()` 检查 tick owner 和 current-engine 恢复 |
| 输入/前置 | `FTestWorldWrapper` 或现有 world/game-instance helper、一个能记录 `Tick()` 次数的 engine test seam 或 `FAngelscriptTestEngineHelper` 观察点、必要时 `FCoreTestContextStackGuard` 隔离全局栈 |
| 期望行为 | adopt case 中 `Subsystem->GetEngine()` 等于外层 engine、`HasAnyTickOwner()` 变为 true、`GetCurrent()` 在有效 ambient/world 下返回该 subsystem，`Deinitialize()` 后恢复为 false；own case 中 `GetEngine()` 非空且不等于外层空值，`Tick()` 只在 `ShouldTick()==true` 时推进，`Deinitialize()` 会释放 owner engine 并清空 `PrimaryEngine` |
| 使用的 Helper | `FAngelscriptTestFixture` + world/game-instance fixture + context-stack guard |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 3 | NoTestForSource: 2, MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:27)

### 一、现有测试问题

#### Issue-12：`GeneratedFunctionTable` 的 CSV 用例用逗号硬拆列，遇到带逗号的 failure reason 会误报

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.SkippedCsvOutput` / `SkippedReasonSummaryCsvOutput` |
| 行号范围 | 669-748 |
| 问题描述 | 两个用例都把 CSV 行直接 `ParseIntoArray(Columns, TEXT(\",\"), false)`，然后强断言 skipped-entry 行必须正好 4 列、reason-summary 行必须正好 2 列。`FailureReason` 本身来自生成器的失败原因字符串，这类文本很容易包含逗号、模板参数列表或路径片段；一旦生成器开始对字段做合法 CSV quoting，或者 failure reason 本身出现逗号，当前测试会把有效 CSV 误判成列数错误。 |
| 影响 | 这些用例对产物格式变化非常脆弱，会因为 failure reason 文本内容而不是生成逻辑错误转红；结果是测试更像在锁死“当前恰好不含逗号的样本文本”，而不是验证 CSV 产物语义。 |
| 修复建议 | 不要手写逗号分割。改用项目内统一 CSV reader，或者至少实现支持引号与转义的最小 parser；断言也应转向字段语义，例如检查 header、记录数、`FailureReason` 非空，以及汇总计数是否与 skipped-entry 数量一致，而不是依赖“逗号数恰好固定”。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-12 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:26)

### 一、现有测试问题

#### Issue-10：`CreateForTestingFallbackFull` 性能用例没有清空 `ContextStack`，可能在脏 scope 下误测 clone 路径

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.Core.Performance.Startup.CreateForTestingFallbackFull` |
| 行号范围 | 42-49, 130-141, 180-184 |
| 问题描述 | `ResetPerformanceEngineState()` 只销毁 shared/global engine，却没有像 `AngelscriptEngineCoreTests.cpp` 那样用 `FAngelscriptEngineContextStack::SnapshotAndClear()` 清掉当前 engine scope。随后 `MeasureCreateForTestingFallbackStartup()` 直接调用 `CreateForTesting(..., Clone)` 并假定“无 current engine 时会 fallback full”。如果前序测试留下了 `FAngelscriptEngineScope`、subsystem-owned engine 或 runtime-module current engine，这个性能用例就会偷偷复用脏 `ContextStack` 走 clone 路径，而当前断言只检查 metrics artifact，完全看不出测量对象已经变了。 |
| 影响 | 该用例名声称覆盖 “fallback full”，实际却可能在跨用例污染下测到 clone 启动时间，导致性能基线和路径语义同时失真；一旦 `CreateForTesting` 的 fallback 合同回归，当前测试仍可能稳定绿灯。 |
| 修复建议 | 在 `CollectStartupSamples()` 每次 warmup / measurement 前后都加 `FCoreTestContextStackGuard` 或等价 helper，显式清空并恢复 `ContextStack`；同时给 fallback 用例补一个最小功能断言，至少验证 `TryGetCurrentEngine() == nullptr` 后创建结果 `GetCreationMode() == Full`、`OwnsEngine() == true`，避免只靠 timing 猜测路径。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-10 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 13:50)

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-13` / `Issue-14` / `Issue-15` / `Issue-16` |
| 新增测试建议 | `NewTest-11` / `NewTest-12` / `NewTest-13` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-13 |
| AntiPattern | 1 | Issue-15 |
| FlakyRisk | 1 | Issue-16 |
| MissingCleanup | 1 | Issue-14 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 14:23)

### 一、现有测试问题

#### Issue-21：`CompileFromDisk` 先手动读文件再走 `CompileModuleFromMemory`，实际绕过了磁盘编译入口

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.CompileFromDisk` |
| 行号范围 | 127-170 |
| 问题描述 | 用例名声称验证“从磁盘编译”，但 141-153 行的实际流程是：先 `WriteFileSystemTestFile(...)` 写盘，再用 `FFileHelper::LoadFileToString(...)` 把文件内容手动读回 `LoadedSource`，最后调用 `CompileModuleFromMemory(&Engine, ..., AbsolutePath, LoadedSource)`。而 `CompileModuleFromMemory` 在 `Shared/AngelscriptTestEngineHelper.cpp` 353-356 行只是把传入的字符串交给内存编译 helper，`AbsolutePath` 只作为 filename 元数据传下去，并没有重新走 engine/preprocessor 的磁盘读取逻辑。这个测试因此验证的是“UE 文件读成功 + 内存编译成功”，不是 runtime 的真正 file-backed compile path。 |
| 影响 | 只要真实的磁盘编译入口、文件预处理、路径到 module 的读取契约回归，但手动 `LoadFileToString` 仍然成功，这条用例就会继续绿灯。文件系统测试里最直观的一条“从磁盘编译”合同实际上没有被覆盖。 |
| 修复建议 | 把该用例改成真正的 disk-path compile smoke，不要先把脚本内容读回内存。建议新增专用 helper，例如 `CompileModuleFromDiskPath(...)`，内部只接受 root/path/module 名并让 `FAngelscriptPreprocessor` 或等价 engine 入口自己读取磁盘文件；同时补一条断言验证脚本修改后重新走同一 disk helper 会读到新内容，而不是沿用测试内缓存的 `LoadedSource`。 |

#### Issue-22：`Startup.Full` / `CreateForTestingFallbackFull` 只写性能 artifact，没有断言“full startup 语义”真的发生

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.Core.Performance.Startup.Full` / `Angelscript.TestModule.Core.Performance.Startup.CreateForTestingFallbackFull` |
| 行号范围 | 35-40, 73-99, 101-141, 161-184 |
| 问题描述 | `MeasureFullStartup()` 和 `MeasureCreateForTestingFallbackStartup()` 都会先 `FAngelscriptBindExecutionObservation::Reset()`，再拿一次 `GetLastSnapshot()`；`FAngelscriptBindExecutionSnapshot` 本身还带有 `InvocationCount`、`DisabledBindNames`、`ExecutedBindNames`、`BindScriptTypesDurationSeconds`、`CallBindsDurationSeconds`。但 `FStartupPerformanceSample` 只保留了三个时长字段，两个 `RunTest()` 也只是把样本交给 `ValidateAndWriteStartupMetrics(...)` 写 artifact，然后直接 `return true`。也就是说，这两条测试没有任何断言去证明 full startup 确实执行了一次 startup bind pass、没有断言 fallback case 真的是 full 模式，更没有检查执行 bind 列表是否非空。 |
| 影响 | 只要 instrumentation 断了、startup bind pass 被意外跳过，或者 `CreateForTesting(..., Clone)` 在 fallback 场景里偷偷走了错误路径，但最后还能写出 `metrics.json`，这两条用例就会稳定绿灯。性能测试名看起来在保护启动路径，实际只保护了“能产出一个文件”。 |
| 修复建议 | 扩展 `FStartupPerformanceSample` 把 `InvocationCount`、`ExecutedBindCount` 和 creation-mode 信息一起记录下来；`Startup.Full` 至少断言每个样本 `InvocationCount == 1` 且 `ExecutedBindCount > 0`，`CreateForTestingFallbackFull` 还应额外断言创建结果为 full/owner engine，而不是 clone。artifact 写入可以保留，但应放在这些语义断言之后。 |

#### Issue-23：`RepresentativeCoverage` 只验证每个类“至少有一条 entry”，对代表 API 的回归几乎没有辨识力

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.RepresentativeCoverage` |
| 行号范围 | 269-318 |
| 问题描述 | 这条用例挑了 `AActor`、`UWorld`、`UGameplayStatics`、`UUserWidget`、`UAngelscriptAbilityAsyncLibrary` 等 10 个 representative class，但循环里唯一的强断言只是 `FunctionMap->Num() > 0`。它没有检查任何具体函数名，也没有验证 entry 是 direct bind、reflective fallback 还是根本不可调用的残缺占位。换句话说，只要某个类在 `ClassFuncMaps` 里还剩下一条任意 entry，这个“代表性覆盖”就会继续通过。 |
| 影响 | 生成表如果对单个大类丢掉了大部分关键 API，只剩一条不相关的 entry，当前测试仍然绿灯。它无法区分“类真的有代表性覆盖”与“类只是没有被完全清空”，对 UHT 生成回归的定位价值非常有限。 |
| 修复建议 | 把“类存在且 map 非空”的 smoke 保留成前置检查，但应为每个代表类补一条明确的代表 API 合同，例如 `AActor` 检查 `GetActorTimeDilation`、`UGameplayStatics` 检查 `GetPlayerController`、`UUserWidget` 检查 `RemoveFromParent`、`UAngelscriptAbilityAsyncLibrary` 检查 `WaitForAttributeChanged`；同时断言这些 entry 至少处于 direct bind 或明确允许的 reflective fallback 状态，而不是只看 `Num() > 0`。 |

#### Issue-24：`PreservesHandwrittenGASEntries` 只验证 `FuncPtr`，没有验证 entry 的 `Caller` 仍可调用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.PreservesHandwrittenGASEntries` |
| 行号范围 | 199-239 |
| 问题描述 | 这条 GAS 兼容回归只检查 `WaitForAttributeChanged` 和 `WaitGameplayTagRemoveFromActor` 两个 handwritten entry 仍存在，并且 `Entry->FuncPtr.IsBound()` 为真。它没有像 `BindConfig` 文件里的 `IsFunctionEntryBound(...)` 那样同时验证 `Caller.IsBound()`，也没有明确断言这两条 entry 没有退化成 reflective fallback。对 `FFuncEntry` 来说，只有函数指针还在并不等于脚本调度仍然完整可用。 |
| 影响 | 一旦 generated/handwritten 合并逻辑把 GAS entry 的 caller 擦掉、替换成错误 caller，或者悄悄退化到 fallback/stub，但 `FuncPtr` 恰好仍保留，这条测试仍会给绿灯。最需要被保护的“手写 GAS helper 仍能被完整调用”合同因此存在漏检。 |
| 修复建议 | 把两个样本入口都升级成完整 entry 断言：使用共享 helper 同时检查 `FuncPtr.IsBound()` 和 `Caller.IsBound()`，并明确断言 `bReflectiveFallbackBound == false`；如果项目允许，更进一步编译一个最小 snippet 实际调用 `WaitForAttributeChanged`/`WaitGameplayTagRemoveFromActor` 的声明，确认注册的不只是 map 条目。 |

### 二、需要新增的测试

#### NewTest-16：给 `DiscoverScriptRoots` / `MakeAllScriptRoots` 补齐“项目根优先 + 插件根去重”合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::DiscoverScriptRoots(bool)` / `FAngelscriptEngine::MakeAllScriptRoots(bool)` |
| 现有测试覆盖 | `FileSystem.Discovery` / `SkipRules` 只验证给定 `AllRootPaths` 后的文件枚举；没有任何用例直接覆盖 root discovery 本身，也没有验证项目根插到首位、插件根去重和缺失目录过滤 |
| 风险评估 | 一旦 script root 发现顺序漂移、插件根重复、或缺失目录没有被滤掉，`GetModuleByFilename`、初始脚本扫描和 commandlet 输出都会在错误 root 集合上运行，当前目标测试集不会直接报警 |
| 建议测试名 | `Angelscript.TestModule.FileSystem.RootDiscovery.ProjectRootFirstAndPluginRootsDeduped` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptScriptRootDiscoveryTests.cpp` |
| 场景描述 | 构造带 fake `FAngelscriptEngineDependencies` 的 test engine：`GetProjectDir()` 返回固定项目路径，`GetEnabledPluginScriptRoots()` 返回“一个有效插件根、一个重复项目根、一个不存在的插件根、一个需要排序的第二插件根”；随后分别调用 `DiscoverScriptRoots(false)` 和 `DiscoverScriptRoots(true)` |
| 输入/前置 | `FAngelscriptEngineConfig` 设置 `bIsEditor=true`；fake dependency 的 `ConvertRelativePathToFull` / `DirectoryExists` 用 in-memory lambda 控制返回；无需真实磁盘目录 |
| 期望行为 | `DiscoverScriptRoots(false)` 的结果中项目 `.../Script` 永远位于索引 0；重复的项目根不会被二次加入；不存在的插件根不会出现在结果里；其余有效插件根按源码约定排序；`DiscoverScriptRoots(true)` 只返回项目根；若再通过 `MakeAllScriptRoots()` 或等价 wrapper 取值，结果与 `DiscoverScriptRoots(false)` 保持一致 |
| 使用的 Helper | `FAngelscriptEngine` + 自定义 `FAngelscriptEngineDependencies` test double；不需要 world fixture |
| 优先级 | P2 |

#### NewTest-17：补一条真正走磁盘读取的 `disk compile` 回归，避免继续被 `CompileModuleFromMemory` 伪覆盖

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::InitialCompile()` 中的磁盘脚本加载流程（`FindAllScriptFilenames(...)` / `AllRootPaths` / `FAngelscriptPreprocessor::AddFile(...)`） |
| 现有测试覆盖 | `FileSystem.CompileFromDisk` 目前只做“手动读文件 + CompileModuleFromMemory”，没有一条用例让 runtime 自己从磁盘路径读取脚本文本 |
| 风险评估 | 一旦磁盘脚本读取、路径映射或预处理入口回归，现有 `FileSystem` 套件不会直接报警，最容易让“启动扫描/磁盘重编译坏了，但内存编译仍可用”的缺陷漏过 |
| 建议测试名 | `Angelscript.TestModule.FileSystem.DiskCompileReadsUpdatedSourceFromPath` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptDiskCompileTests.cpp` |
| 场景描述 | 在独立 temp root 下写入 `RuntimeDiskModule.as`，第一次通过新的 `CompileModuleFromDiskPath(...)` helper 让 engine/preprocessor 自己从路径读取并执行 `Entry()`；随后覆写同一路径上的脚本返回值，再次调用同一 disk helper 重新编译并执行 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；GUID temp root；新增 shared helper `CompileModuleFromDiskPath(FAngelscriptEngine*, FName ModuleName, const FString& AbsolutePath)`，helper 内部不得先 `LoadFileToString`，而应把路径交给 runtime/preprocessor 自己读取 |
| 期望行为 | 第一次执行 `int Entry()` 返回初始值，例如 `42`；覆写磁盘文件后第二次编译返回更新值，例如 `17`；`Engine.GetModuleByFilename(AbsolutePath)` 始终能解析到目标模块；若 helper 内部错误缓存了旧文本，第二次返回值会暴露回归 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + 新增 `CompileModuleFromDiskPath(...)` + `ExecuteIntFunction(...)` |
| 优先级 | P1 |

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-21` / `Issue-22` / `Issue-23` / `Issue-24` |
| 新增测试建议 | `NewTest-16` / `NewTest-17` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-22 |
| WrongHelper | 1 | Issue-21 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 14:53)

### 一、现有测试问题

#### Issue-25：`PathNormalizationLookup` 用 `GetModuleByFilenameOrModuleName` 兜底，实际上没有证明反斜杠 filename lookup 可用

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.PathNormalizationLookup` |
| 行号范围 | 370-412 |
| 问题描述 | 用例名声称验证“path normalization lookup”，但真正被测的第二条路径是 `Engine.GetModuleByFilenameOrModuleName(BackslashPath, TEXT("Game.Path.Normalize"))`。这个 API 会在 filename 规范化失败时继续回退到 module-name lookup，因此当前断言只能证明“模块名兜底能找到模块”，不能证明 `GetModuleByFilename()` 对 `\\` 路径真的做了规范化。也就是说，只要 filename 索引坏了、但 module name 仍然存在，这条测试仍会通过。 |
| 影响 | `GetModuleByFilename()` 的反斜杠/标准化合同一旦回归，当前用例会继续给绿灯；真正依赖磁盘路径查 module 的代码路径会在生产中失效，但测试报告只会显示“一切正常”。 |
| 修复建议 | 把核心断言改成直接验证 filename 分支：补 `Engine.GetModuleByFilename(BackslashPath)` 必须有效且与 `AbsolutePath` 返回同一模块；若还想保留 `GetModuleByFilenameOrModuleName`，应传一个不存在的 module name 作为 control，证明命中的是 normalized filename 分支而不是名称兜底。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-25 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 15:19)

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-25` / `Issue-26` / `Issue-27` / `Issue-28` / `Issue-29` |
| 新增测试建议 | `NewTest-18` / `NewTest-19` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-27 |
| WrongHelper | 1 | Issue-25 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-08 16:22)

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-30` / `Issue-31` |
| 新增测试建议 | `NewTest-20` / `NewTest-21` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-30 |
| MissingCleanup | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-08 15:24)

### 一、现有测试问题

#### Issue-32：`CreateDestroy` 没有清空 `ContextStack`，实际创建模式会被前序 current engine 污染

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.CreateDestroy` |
| 行号范围 | 58-72 |
| 问题描述 | 用例只调用了 `DestroySharedTestEngine()`，随后直接执行 `FAngelscriptEngine::CreateForTesting(Config, Dependencies)`。但 `CreateForTesting(..., Clone)` 的默认实现会先查 `TryGetCurrentEngine()`，若 `ContextStack` 里仍有 ambient engine 就走 `CreateCloneFrom(...)`，否则才 fallback 到 `CreateTestingFullEngine()`（`AngelscriptEngine.cpp` 654-666 行）。这条测试既没有像同文件后半段那样使用 `FCoreTestContextStackGuard` 清空上下文，也没有销毁 global engine 或断言 `GetCreationMode()`，因此它实际创建的是 clone 还是 full 完全取决于前序测试是否留下 current engine。 |
| 影响 | 最基础的 create/destroy smoke 本身就不是稳定的生命周期合同；如果前序测试残留 `FAngelscriptEngineScope`、subsystem-owned engine 或其他 current engine，这条用例会悄悄改成 clone 路径，依然稳定绿灯，既掩盖创建模式污染，也无法证明“销毁 owner engine 后资源正确释放”。 |
| 修复建议 | 进入用例前先用 `FCoreTestContextStackGuard` 清空并恢复 `ContextStack`，同时销毁 shared/global engine，确保环境真的无 current engine；随后至少断言 `Engine->GetCreationMode() == EAngelscriptEngineCreationMode::Full`、`Engine->OwnsEngine() == true`、`Engine->GetSourceEngine() == nullptr`。如果只想验证 clone/fallback 逻辑，直接删掉这条泛化 smoke，改用现有 `NewTest-01` 建议的 focused lifecycle mode tests。 |

#### Issue-33：`CsvOutput` 只核对行数和单个样本函数，未验证大多数 CSV 字段与 summary/生成产物一致

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.CsvOutput` |
| 行号范围 | 594-666 |
| 问题描述 | 这条用例对 `AS_FunctionTable_ModuleSummary.csv` 的校验只有 header 和“行数等于 summary.json 的 module 数”；对 `AS_FunctionTable_Entries.csv` 也只校验 header、总行数和 `RunBehaviorTree` 这一条样本 entry 是否带有 `,Direct,`。它没有解析任何 module row 去核对 `ModuleName`、`EditorOnly`、`TotalEntries`、`DirectBindEntries`、`StubEntries`、`ShardCount` 是否与 `summary.json` 对齐，也没有检查 entry csv 里其他函数的 `EntryKind` / `EraseMacro` / `ShardIndex` 是否匹配生成产物。结果是：只要 exporter 还能写出同样数量的行，并保住 `RunBehaviorTree` 这一个样本，绝大多数列值错位、模块统计错误或 entry 分类漂移都不会被发现。 |
| 影响 | CSV 产物一旦出现“总行数没变但列值错了”的回归，当前测试仍会绿灯。依赖 module summary / entry csv 做诊断或后处理的流水线会拿到错误数据，而自动化无法指出具体哪一列、哪一类 entry 出错。 |
| 修复建议 | 把 CSV 校验升级成结构化对账：解析 `ModuleSummary.csv` 的每一行，按 `ModuleName` 与 `summary.json.modules[]` 建立映射，逐项断言 `EditorOnly`、`TotalEntries`、`DirectBindEntries`、`StubEntries`、`DirectBindRate`、`StubRate`、`ShardCount` 一致；对 `Entries.csv` 至少抽样多类 entry（direct / stub / editor-only / runtime）并断言 `EntryKind`、`EraseMacro`、`ShardIndex` 与 `AS_FunctionTable_*.cpp` / summary 数据吻合。若继续保留单样本 smoke，也应作为附加断言，而不是唯一的语义校验。 |

### 二、需要新增的测试

#### NewTest-22：覆盖 `FAngelscriptRuntimeModule` 的 testing override 初始化与 reset 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 关联函数 | `FAngelscriptRuntimeModule::InitializeAngelscript()` / `SetInitializeOverrideForTesting(...)` / `ResetInitializeStateForTesting()` |
| 现有测试覆盖 | 完全无测试；当前 `RuntimeCore` 目录里的 `Core/Subsystem/GC/FileSystem` 目标测试没有任何用例直接保护 runtime module 的初始化入口、testing override 或 reset 行为 |
| 风险评估 | 如果 module 初始化重复 push engine、忽略 testing override、或 reset 后残留 current engine，后续所有依赖 `TryGetCurrentEngine()` 的 runtime 行为都会在脏上下文上运行，问题会表现为大面积串测和生命周期错乱，但现有套件没有一个定位点 |
| 建议测试名 | `Angelscript.TestModule.Engine.RuntimeModule.InitializeOverrideIsIdempotentAndRestorable` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp` |
| 场景描述 | 在测试文件里定义 `FAngelscriptRuntimeModuleTickTestAccess` 访问 private testing hook。先清空 shared/global engine 与 `ContextStack`，创建一个 isolated full engine 作为 override engine；调用 `SetInitializeOverrideForTesting([&] { return OverrideEngine.Get(); })` 后连续两次执行 `InitializeAngelscript()`，再通过 `FAngelscriptEngineContextStack::SnapshotAndClear()` 检查当前栈内容；最后调用 `ResetInitializeStateForTesting()` 并确认上下文被完整恢复 |
| 输入/前置 | `AngelscriptTestSupport::DestroySharedTestEngine()`；销毁 global engine；一个本地 `FScopedContextStackGuard`（仿 `EngineCoreTests` 的 `FCoreTestContextStackGuard`）用于快照/恢复 `ContextStack`；`TUniquePtr<FAngelscriptEngine> OverrideEngine = AngelscriptTestSupport::CreateFullTestEngine()` |
| 期望行为 | 第一次 `InitializeAngelscript()` 后 `FAngelscriptEngine::TryGetCurrentEngine()` 返回 `OverrideEngine.Get()`；第二次调用不会重复 push，同一次 `SnapshotAndClear()` 看到的栈大小仍为 `1` 且唯一元素就是 `OverrideEngine.Get()`；`ResetInitializeStateForTesting()` 后 `TryGetCurrentEngine() == nullptr` 且新的 `SnapshotAndClear()` 为空，证明 testing override 和 initialize state 都被完整清理 |
| 使用的 Helper | `AngelscriptTestSupport::CreateFullTestEngine()` + 自定义 `FScopedContextStackGuard` + `FAngelscriptRuntimeModuleTickTestAccess` |
| 优先级 | P1 |

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-32` / `Issue-33` |
| 新增测试建议 | `NewTest-22` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-32 |
| WeakAssertion | 1 | Issue-33 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 16:00)

### 二、需要新增的测试

#### NewTest-23：给 `FBindString` 补齐空串判定与 constant/dynamic/FString round-trip 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindString.h` |
| 关联函数 | `IsEmpty()` / `ToFString()` / `ToCString()` / `ToCString_EnsureConstant()` / `SetDynamic(const ANSICHAR*)` / `operator=(const FString&)` / `operator=(const ANSICHAR*)` |
| 现有测试覆盖 | 完全无测试；当前 RuntimeCore 的 `BindConfig` / `GeneratedFunctionTable` / `Parity` 只间接走 bind 注册流程，没有任何用例直接保护 `FBindString` 的缓存失效、空串语义和常量/动态字符串切换 |
| 风险评估 | `FBindString` 被 `FAngelscriptBinds` 广泛用于 class 名、method signature、namespace 和 enum 名注册；一旦空串判断或 ANSI/FString 缓存切换回归，runtime 可能把“名字为空/名字错了/沿用旧缓存”的错误静默带进 bind 注册，现有自动化很难定位根因 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindString.EmptyAndRoundTripAcrossConstantDynamicAndUnrealSources` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindStringTests.cpp` |
| 场景描述 | 在纯 native unit 测试里覆盖三组输入源：1. 常量 `ANSICHAR*` 的空串与非空串；2. `FString` 的空串与非空串；3. `SetDynamic("")` 与 `SetDynamic("Namespace::Value")`。每组都交叉调用 `IsEmpty()`、`ToCString()`、`ToFString()`；随后再覆盖“先 constant -> 再 dynamic -> 再 FString”的连续赋值路径，验证缓存切换后不会沿用旧内容 |
| 输入/前置 | 无需 engine；一个轻量 `CheckBindStringState(FBindString&, bool bExpectedEmpty, const TCHAR* ExpectedUnreal, const ANSICHAR* ExpectedAnsi)` helper；测试字符串限定 ASCII，符合该类型注释约束 |
| 期望行为 | constant empty / `FString()` / `SetDynamic("")` 都返回 `IsEmpty()==true`；constant non-empty / non-empty `FString` / `SetDynamic("Namespace::Value")` 都返回 `IsEmpty()==false`；`ToCString()` 与 `ToFString()` 的结果逐次稳定且保持相同文本；`ToCString_EnsureConstant()` 仅在 constant-string case 可用；经过 constant/dynamic/FString 多次切换后，`ToCString()` 和 `ToFString()` 都不应返回前一次缓存内容 |
| 使用的 Helper | 其他（纯 native unit helper，无需 `FAngelscriptEngine`；可在文件内放一个小型 `CheckBindStringState` 共享断言） |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 16:41)

### 一、现有测试问题

#### Issue-37：三条 `BindConfig`“recover direct bind”用例实际上接受 reflective fallback 绿灯

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.OverloadedExportedFunctionsCanRecoverDirectBind` / `InlineDefinitionFunctionsCanRecoverDirectBind` / `InlineOutRefFunctionsCanRecoverDirectBind` |
| 行号范围 | 727-847 |
| 问题描述 | 这三条用例的最终断言都只调用 `IsFunctionEntryBound(*Entry)`。但 `FFuncEntry` 同时还带有 `bReflectiveFallbackBound`，而 reflective fallback 路径本身也会把 `FuncPtr`/`Caller` 绑定起来；也就是说，只要入口退化成 reflective fallback 而不是预期的 direct bind，这三条“CanRecoverDirectBind”测试仍可能稳定通过。测试名声明要保护“direct bind 恢复”，现有断言却只能证明“条目可调度”。 |
| 影响 | 一旦 UHT / bind 恢复逻辑回归，导致 overload、inline 定义或 out-ref 场景不再恢复 direct bind，而是静默落回 reflective fallback，当前测试不会报警。生成表面继续可用，但 direct bind 回归带来的性能、可调试性和导出语义退化会被误报成绿灯。 |
| 修复建议 | 把这三条用例的合同改成“direct bind 而不是 fallback”：在 `IsFunctionEntryBound(*Entry)` 之外，追加 `TestFalse(..., Entry->bReflectiveFallbackBound)`；如需更强保护，再补一个 helper 比较导出 entry 的 erase-macro/生成来源，或编译最小脚本实际调用对应函数并确认命中 direct path，而不是只验证 map 条目存在。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-37 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:30)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 20:27` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | 无 |
| 新增测试建议 | `NewTest-45` / `NewTest-46` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:48)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 20:38` / `2026-04-08 20:42` / `2026-04-08 20:46` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-59` / `Issue-60` |
| 新增测试建议 | `NewTest-47` / `NewTest-48` / `NewTest-49` / `NewTest-50` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 2 | Issue-59 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 3 | MissingErrorPath: 1, MissingScenario: 2 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:10)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 23:57` / `2026-04-09 00:02` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-62` |
| 新增测试建议 | `NewTest-52` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-62 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:09)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 23:57` / `2026-04-09 00:02` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-62` |
| 新增测试建议 | `NewTest-52` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-62 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:07)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 23:57` / `2026-04-09 00:02` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-62` |
| 新增测试建议 | `NewTest-52` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-62 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:05)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 23:57` / `2026-04-09 00:02` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-62` |
| 新增测试建议 | `NewTest-52` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-62 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 23:57)

### 一、现有测试问题

#### Issue-62：前五条 `FileSystem` 用例同时调用 `ASTEST_CREATE_ENGINE_SHARE()` 和 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，属于互相冲突的 helper 组合

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.ModuleLookupByFilename` / `CompileFromDisk` / `PartialFailurePreservesGoodModules` / `Discovery` / `SkipRules` |
| 行号范围 | 85-87, 131-133, 177-179, 241-243, 285-287 |
| 问题描述 | 这五条用例在进入 `ASTEST_BEGIN_SHARE_CLEAN` 之前，都会先拿一个未使用的 `EngineOwner = ASTEST_CREATE_ENGINE_SHARE()`，随后又立刻再拿 `Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN()`。根据 `TESTING_GUIDE.md` 与 `AngelscriptTestMacros.h`，`SHARE` 表示“复用现有 shared engine、不做 reset”，而 `SHARE_CLEAN` 表示“获取 shared engine 并先执行 clean reset”；两者是互斥的 fixture 选择，不应在同一用例里叠加。当前写法既让 `EngineOwner` 完全不参与断言，也把“测试到底依赖脏 shared engine 还是 clean reacquire”混成一段多余 setup。 |
| 影响 | 这组用例表面上是 clean shared-engine 测试，实际却在前一行先触发一次 `GetOrCreateSharedCloneEngine()`。当前实现下这通常只是多余创建/复用，但它会把 fixture 语义写得模糊，增加未来 helper 语义调整后的误用风险，也让读代码的人误以为需要两个 engine handle 才能维持生命周期。你要求重点检查 helper 使用是否正确，这里就是明显的 macro 误用。 |
| 修复建议 | 删掉未使用的 `EngineOwner = ASTEST_CREATE_ENGINE_SHARE()`，统一只保留 `FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();` + `ASTEST_BEGIN_SHARE_CLEAN`。如果这些用例其实想测试“共享但不 reset”的累积行为，就应反过来只保留 `SHARE` 宏并补显式 cleanup；不要在同一用例里同时调用两套语义不同的 engine fixture。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-62 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 23:32)

### 二、需要新增的测试

#### NewTest-51：覆盖 `UAngelscriptTestCommandlet::Main()` 在 unit test 失败时返回 `2`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp` |
| 关联函数 | `UAngelscriptTestCommandlet::Main(const FString& Params)` |
| 现有测试覆盖 | 当前文档只在 `NewTest-21` 建议覆盖 `bDidInitialCompileSucceed == false` 的短路返回；`RunAngelscriptUnitTests(...)` 返回 `false` 时的 `return 2` 分支仍完全无覆盖 |
| 风险评估 | 一旦 commandlet 在脚本单测失败时错误返回 `0` 或吞掉失败，CI / BuildGraph 会把“脚本单测已红”的构建误判成成功，直接削弱 runtime 回归链路 |
| 建议测试名 | `Angelscript.TestModule.Core.Commandlet.TestCommandlet.UnitTestFailureReturnsTwo` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestCommandletTests.cpp` |
| 场景描述 | 在 isolated full 或 production-like engine 下编译一个最小脚本模块，模块内声明 `void Test_CommandletFailure(FUnitTest& T) { T.Fail("CommandletShouldFail"); }`。调用前先确认该 module 已进入 `GetActiveModules()` 且 `UnitTestFunctions.Num() == 1`；必要时临时把 `UAngelscriptTestSettings::UnitTestNamingConvention` 设成 `*`，然后创建 `UAngelscriptTestCommandlet` 调 `Main(TEXT(""))` |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或 `ProductionLike`；`CompileModuleFromMemory(...)` / `CompileModuleWithResult(...)`；一个 failing `FUnitTest` 脚本；可选 `AddExpectedError(TEXT("CommandletShouldFail"), Contains, 1)` 或 scoped log capture；scoped settings restore helper |
| 期望行为 | `Main()` 必须返回 `2`；日志中命中一次 failing unit test 名或 `CommandletShouldFail` 文本；active module 数量在执行前后保持一致；调用结束后 `bDidInitialCompileSucceed` 仍保持 `true`，证明命中的是 unit-test failure 分支而不是 initial-compile short路 |
| 使用的 Helper | `FAngelscriptTestFixture` + `CompileModuleFromMemory(...)` + scoped settings restore/log capture helper |
| 优先级 | P1 |

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-61` |
| 新增测试建议 | `NewTest-51` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-61 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 23:32)

### 一、现有测试问题

#### Issue-61：`SummaryOutput` 忽略 `moduleCount` / `totalShardCount` 与 per-module 标识字段，summary 结构合同仍有明显空洞

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.SummaryOutput` |
| 行号范围 | 459-590 |
| 问题描述 | 这条用例会解析 `AS_FunctionTable_Summary.json`，但实际只校验 `totalGeneratedEntries`、`totalDirectBindEntries`、`totalStubEntries`、`directBindRate`、`stubRate`，以及每个 module 的 `totalEntries` / `directBindEntries` / `stubEntries` / rate。当前 summary 产物还明确包含顶层 `moduleCount`、`totalShardCount`，以及每个 module 的 `moduleName`、`editorOnly`、`shardCount` 字段；测试从头到尾都没有读取或断言这些值。结果是：只要 entry 计数仍自洽，哪怕模块名错位、editor-only 标记漂移、shard 数统计错误，`SummaryOutput` 依然会绿灯。 |
| 影响 | generated function table 的 summary 已经不只是“计数报表”，还承担模块身份和 shard 布局合同。当前测试遗漏这些字段后，UHT 若把 `UMGEditor`/`UnrealEd` 的 `editorOnly` 标记写错、模块名映射错位，或 `totalShardCount`/`shardCount` 与真实产物脱节，自动化都不会给出定点报警。 |
| 修复建议 | 在现有 self-consistency 校验之外，补结构字段断言：1. 顶层读取并验证 `moduleCount == Modules->Num()`、`totalShardCount ==` 实际 `AS_FunctionTable_*.cpp` 文件数；2. 遍历 `modules[]` 时显式读取 `moduleName`、`editorOnly`、`shardCount`，至少断言 `moduleName` 非空且唯一、`shardCount > 0`，并把 `editorOnly`/`shardCount` 与 `ModuleSummary.csv` 或磁盘 shard 文件前缀做交叉核对；3. 对 `UnrealEd` / `UMGEditor` / `Engine` 这类代表模块补精确布尔断言，避免 editor/runtime 模块身份漂移。 |

---

## 测试审查 (2026-04-08 20:29)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 20:27` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | 无 |
| 新增测试建议 | `NewTest-45` / `NewTest-46` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:27)

### 二、需要新增的测试

#### NewTest-45：给 `asIScriptObject::GetObjectType()` 建立 script/native 对照合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp` |
| 关联函数 | `asIScriptObject::GetObjectType() const` |
| 现有测试覆盖 | 当前目标测试目录 `Plugins/Angelscript/Source/AngelscriptTest/Core/`、`Subsystem/`、`GC/`、`FileSystem/` 以及 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 中，对 `GetObjectType(` / `asIScriptObject` 都没有任何直接命中；`EngineCoreTests` 只验证 full-engine teardown 和 annotated class 可重建，没有一条用例直接保护 script object 到 `UASClass::ScriptTypePtr` 的映射 |
| 风险评估 | 这个入口被 `Docs`、type/stack introspection 等路径复用；如果 `UASClass::GetFirstASClass((UObject*)this)` 的查找回归、script instance 被错误当成 native object、或返回了过期 `ScriptTypePtr`，现有自动化不会给出任何定点报警，表现会是“脚本对象存在，但 object-type 反射丢失” |
| 建议测试名 | `Angelscript.TestModule.Engine.ObjectModel.ScriptObjectGetObjectTypeMatchesGeneratedASClass` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 场景描述 | 在 full engine 下编译一个最小 annotated `UObject` script class（例如 `UObject` 派生、带一个简单 `UPROPERTY`），通过 `FindGeneratedClass(...)` 找到生成类并实例化；把实例转成 `asIScriptObject*` 后调用 `GetObjectType()`。同时创建一个 native `UObject` control case，验证 native object 不会误返回 script type |
| 输入/前置 | `ASTEST_CREATE_ENGINE_FULL()` + `ASTEST_BEGIN_FULL`；`CompileAnnotatedModuleFromMemory(...)`；`FindGeneratedClass(...)`；一个 generated `UObject` instance 和一个 native `UObject` instance |
| 期望行为 | generated script object 的 `GetObjectType()` 返回非空，且与 generated `UASClass` 上的 `ScriptTypePtr` 指向同一个 `asITypeInfo`，其 type name 与脚本类名一致；native `UObject` 的 `GetObjectType()` 返回 `nullptr`；如先 `DiscardModule(...)` 再重新编译同名/新名 script class，新的实例仍应返回当前 epoch 的 type info，而不是旧指针 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL()` + `CompileAnnotatedModuleFromMemory(...)` + `FindGeneratedClass(...)` + `DiscardModule(...)` |
| 优先级 | P1 |

#### NewTest-46：给 `FAngelscriptBinds` 的 previous-bind metadata / compile-out helper 建立原生回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` |
| 关联函数 | `MarkAsImplicitConstructor()` / `DeprecatePreviousBind(...)` / `SetPreviousBindIsPropertyAccessor(...)` / `SetPreviousBindIsGeneratedAccessor(...)` / `SetPreviousBindIsEditorOnly(...)` / `SetPreviousBindRequiresWorldContext(...)` / `SetPreviousBindIsCallable(...)` / `SetPreviousBindNoDiscard(...)` / `SetPreviousBindArgumentDeterminesOutputType(...)` / `SetPreviousBindForceConstArgumentExpressions(...)` / `PreviousBindPassScriptFunctionAsFirstParam()` / `PreviousBindPassScriptObjectTypeAsFirstParam()` / `CompileOutPreviousBind()` / `CompileOutPreviousBindAsMethodChain()` |
| 现有测试覆盖 | `AngelscriptBindConfigTests.cpp` 目前只覆盖 bind registry merge、`ShouldSkipBlueprintCallableFunction(...)`、`AddFunctionEntry(...)`、`ModifyScriptFunction(...)` 等路径；当前目标测试目录与 `AngelscriptRuntime/Tests` 对上述 previous-bind mutator / compile-out helper 都没有任何直接命中 |
| 风险评估 | 这些 helper 负责把 bind metadata 写进 `asCScriptFunction`：trait bit、`compileOutType`、`determinesOutputTypeArgumentIndex`、`passFirstParamMetaData` 等。如果其中任一写错或丢失，startup bind smoke、generated entry 数量、甚至 direct-bind 恢复测试都可能继续绿灯，但脚本编译期和文档导出语义会静默漂移 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindMetadata.PreviousBindMutatorsPersistTraitsAndCompileOutFlags` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindMetadataTests.cpp` |
| 场景描述 | 在 isolated full engine 下注册一组最小 native bind：一个 object constructor/方法、一个 global generic function。每次 bind 后立刻调用对应 previous-bind helper，再直接读取 `asCScriptFunction` / `asCObjectType` 内部状态，覆盖 implicit-constructor、deprecated、property/generated/editor-only/world-context/not-callable/no-discard、output-type argument index、force-const、first-param metadata，以及两种 compile-out 模式 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_FULL()` + `ASTEST_BEGIN_FULL`；最小 `NoOpGeneric(asIScriptGeneric*)` helper；必要时一个简易 native value/object type 用于 constructor case；直接包含 `source/as_scriptfunction.h` / `source/as_objecttype.h` |
| 期望行为 | `MarkAsImplicitConstructor()` 之后目标函数的 `asTRAIT_IMPLICITCONSTRUCTOR` 为 true，且宿主 `objectType->hasImplicitConstructors` 被置位；`DeprecatePreviousBind(...)` 之后 `asTRAIT_DEPRECATED` 为 true，并在 `WITH_EDITOR` 下保留 deprecation message；其余 setter 分别把对应 trait 或字段写到 `asCScriptFunction`；`PreviousBindPassScriptFunctionAsFirstParam()` / `PreviousBindPassScriptObjectTypeAsFirstParam()` 分别把 `sysFuncIntf->passFirstParamMetaData` 设到正确枚举值；`CompileOutPreviousBind()` / `CompileOutPreviousBindAsMethodChain()` 分别写入预期 `compileOutType` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL()` + 最小 native bind helper + `source/as_scriptfunction.h` / `source/as_objecttype.h` 直接读回状态 |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:05)

### 二、需要新增的测试

#### NewTest-42：给 `UAngelscriptAbilityAsyncLibrary` 的 actor wrapper 补齐真实运行时 task 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h` |
| 关联函数 | `WaitForAttributeChanged(...)` / `WaitGameplayEventToActor(...)` / `WaitGameplayTagAddToActor(...)` / `WaitGameplayTagRemoveFromActor(...)` |
| 现有测试覆盖 | 当前目标测试目录里只有 `GeneratedFunctionTable` 对 `WaitForAttributeChanged`、`WaitGameplayTagRemoveFromActor` 做 entry 存在性检查；没有任何运行时用例真正创建这些 async task、绑定 delegate 并驱动 ASC/tag/event/attribute 变化 |
| 风险评估 | 只要 wrapper 指到错误的 native async class、参数如 `bTriggerOnce` / `bMatchExact` 被丢失，或 task 能创建但永远不触发，现有自动化都会继续绿灯；脚本侧 GAS async API 会表现成“可见但不可用” |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.Async.ActorWrappersCreateWorkingTasks` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAsyncLibraryTests.cpp` |
| 场景描述 | 搭一个最小 GAS fixture：带 `UAngelscriptAbilitySystemComponent` 的 native test actor、一个最小 `UAngelscriptAttributeSet`（如 `Health`）、以及可记录 delegate 次数/参数的 listener object。分别调用 `WaitForAttributeChanged`、`WaitGameplayEventToActor`、`WaitGameplayTagAddToActor`、`WaitGameplayTagRemoveFromActor`，随后通过修改 attribute、发送 gameplay event、添加 loose gameplay tag、移除 loose gameplay tag 驱动 task |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或等价 GAS fixture；测试 attribute `Health`；event tag `GameplayEvent.Tests.Async`；owned tag `GameplayTag.Tests.Async`；一个 delegate recorder helper；必要时 actor/ASC 初始化 helper |
| 期望行为 | 四个 wrapper 都返回非空 task，`GetClass()` 分别等于 `UAbilityAsync_WaitAttributeChanged`、`UAbilityAsync_WaitGameplayEvent`、`UAbilityAsync_WaitGameplayTagAdded`、`UAbilityAsync_WaitGameplayTagRemoved`；修改 `Health` 后 attribute task 记录一次旧值/新值变化；发送 `GameplayEvent.Tests.Async` 后 event task 只在匹配 tag 时触发；添加 tag 后 add-task 触发一次，移除同一 tag 后 remove-task 触发一次；`bTriggerOnce=true` 的 case 第二次相同变化不再重复广播 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS actor/ASC fixture + delegate recorder helper |
| 优先级 | P1 |

#### NewTest-43：覆盖 `FindCueLoadedClassInEditor()` 的已缓存/已加载/缺失 tag 三条分支

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameplayCueUtils.h` |
| 关联函数 | `UAngelscriptGameplayCueUtils::FindCueLoadedClassInEditor(FGameplayTag)` |
| 现有测试覆盖 | 现有文档只建议给 `AddLocalGameplayCue` / `ExecuteLocalGameplayCue` / `RemoveLocalGameplayCue` 建运行时回归；`FindCueLoadedClassInEditor()` 这个 editor-only 查找入口在当前 4 个测试目录和 `AngelscriptRuntime/Tests` 中都没有任何命中 |
| 风险评估 | 如果 cue set 查表、`FindObject<UClass>` fallback 或同步 load/caching 路径回归，编辑器脚本会在合法 gameplay cue tag 上得到 `nullptr`，或每次都重复 load 同一个 cue class；这种 editor-only 问题目前没有任何自动化定位点 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.GameplayCueUtils.FindCueLoadedClassInEditorResolvesAndCachesCueClass` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGameplayCueUtilsTests.cpp` |
| 场景描述 | 构造一个临时 `UGameplayCueSet`，向其中注入一条测试 tag（如 `GameplayCue.Tests.EditorLookup`）对应的 `FGameplayCueNotifyData`，目标类使用 native test cue class。通过测试 access shim 暂时把该 cue set 挂到 `UGameplayCueManager::GetGlobalCueSets()` 的返回集合中，覆盖三种情况：1. `LoadedGameplayCueClass` 预先已填；2. `LoadedGameplayCueClass` 为空，但 `GameplayCueNotifyObj` 指向一个已加载类；3. 查找不存在的 tag |
| 输入/前置 | editor automation 环境；一个 native `UGameplayCueNotify_Static` 或 `AGameplayCueNotify_Actor` test class；临时 cue set/cue manager access shim；`ON_SCOPE_EXIT` 恢复原始 global cue sets 与 `LoadedGameplayCueClass` 缓存 |
| 期望行为 | 情况 1 返回预填的 `LoadedGameplayCueClass`；情况 2 返回同一个 test cue class，且调用后 `CueData.LoadedGameplayCueClass` 被回填缓存；情况 3 返回 `nullptr` 且不修改其他 cue data；整个测试期间不应对非测试 tag 产生额外加载副作用 |
| 使用的 Helper | 其他（临时 `UGameplayCueSet` fixture + cue manager access shim + `ON_SCOPE_EXIT` 恢复 helper） |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:02)

### 本轮汇总（承接上方 `NewTest-29` / `NewTest-30` / `NewTest-31` / `NewTest-32`）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | 无 |
| 新增测试建议 | `NewTest-29` / `NewTest-30` / `NewTest-31` / `NewTest-32` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 4 | NoTestForSource: 4 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:22)

### 本轮汇总（本轮详细新增条目已记录于上方 `2026-04-08 18:21` 区块；此处仅补末尾索引，不重复描述）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-43` / `Issue-44` |
| 新增测试建议 | `NewTest-33` / `NewTest-34` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-44 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-43 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:36)

### 本轮汇总（补末尾索引；本轮详细新增条目已记录于上方 `2026-04-08 18:35` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-45` / `Issue-46` / `Issue-47` / `Issue-48` |
| 新增测试建议 | `NewTest-35` / `NewTest-36` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-46 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-45 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:19)

### 本轮汇总（补末尾索引；本轮详细新增条目已记录于上方 `2026-04-08 19:17` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | 无 |
| 新增测试建议 | `NewTest-38` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:35)

### 本轮汇总（补真正文件尾部索引；本轮详细新增条目已记录于上方 `2026-04-08 19:34` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-51` / `Issue-52` |
| 新增测试建议 | `NewTest-39` / `NewTest-40` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-51 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:52)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 19:48` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-53` / `Issue-54` / `Issue-55` / `Issue-56` |
| 新增测试建议 | `NewTest-41` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-54 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-55 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-08 20:10)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 20:04` / `2026-04-08 20:05` / `2026-04-08 20:08` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-57` |
| 新增测试建议 | `NewTest-42` / `NewTest-43` / `NewTest-44` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 1 | Issue-57 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 2 | NoTestForSource: 2 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:04)

### 一、现有测试问题

#### Issue-57：性能 artifact 测试复用固定 `RunId` 且不清旧文件，当前写入失败时可能直接吃到上一次结果

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerformanceArtifactTests.cpp` |
| 测试名 | `Angelscript.TestModule.Core.Performance.Startup.Full` / `Startup.Clone` / `Startup.CreateForTestingFallbackFull` / `Startup.CreateForTestingClone` / `ArtifactGeneration` |
| 行号范围 | 161-197 / 15-28 |
| 问题描述 | 这 5 条用例都把结果写到固定 `RunId` 目录，例如 `P3_1_StartupPerformance_Full`、`P3_4_PerformanceArtifactGeneration`，但进入测试前没有删除 `Saved/Automation/AngelscriptPerformance/<RunId>` 旧目录。与此同时，共享 helper `WritePerformanceMetricsArtifact(...)` 只是调用 `FFileHelper::SaveStringToFile(Output, *MetricsPath)` 后直接返回路径（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h` 40-80 行），完全没有检查写入是否成功。结果是：只要当前轮写失败而上一次 `metrics.json` 还在，测试里的 `FileExists`、`LoadFileToString`、字符串包含检查就仍可能全部通过。 |
| 影响 | 这批性能用例会对磁盘残留状态敏感，形成“当前回归但沿用旧 artifact 仍绿灯”的假阳性窗口。尤其 `ArtifactGeneration` 和两个只关心 artifact 的 startup 用例，会把真正的写入失败、目录权限问题或 helper 失效直接吞掉。 |
| 修复建议 | 在每个用例开始时先删除对应 `GetPerformanceRunDirectory(RunId)`，或给 `RunId` 增加 `FGuid`/时间戳后缀，确保本轮一定写到新目录；同时把 `WritePerformanceMetricsArtifact(...)` 改成返回 `bool bSaved` 或 `TOptional<FString>`，对 `SaveStringToFile(...)` 做强断言。为了证明读到的是本轮产物，建议在 JSON 里再写一个本轮唯一 nonce（例如 GUID note），测试结束时解析文件并验证该 nonce，而不是只看固定 `run_id` 和 metric 名称。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 1 | Issue-57 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:48)

### 一、现有测试问题

#### Issue-53：`CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` 只检查内部 flag，没有验证脚本可见签名与调用合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` |
| 行号范围 | 647-703 |
| 问题描述 | 这条用例先构造 `FAngelscriptFunctionSignature`，再用 `BindGlobalGenericFunction(...)` 绑上 `NoOpGeneric`，最后直接把 `GetFunctionById(...)` 取回的 `asCScriptFunction` 强转出来，只检查 `hiddenArgumentIndex` 和 `asTRAIT_USES_WORLDCONTEXT`。它没有验证最终脚本声明是否真的隐藏了 world-context 参数，也没有编译/执行任何脚本调用点来证明 `CallableWithoutWorldContext` 可以在不显式传 world 的情况下正常调用；由于底层 generic body 还是 no-op，就算隐藏参数注入、调用桥接或脚本可见签名已经回归，当前用例仍可能通过。 |
| 影响 | 这条测试只能证明内部 metadata “看起来像对了”，不能证明脚本侧真实 API 合同仍成立。若 `ModifyScriptFunction(...)` 只改了 trait/hidden index，却没有把最终可调用声明或运行时注入路径修正到位，自动化会误报绿灯。 |
| 修复建议 | 保留当前 internal flag 检查作为补充，但应新增用户面断言：1. 直接比对 `RequiredSignature.Declaration` / `OptionalSignature.Declaration`，确认隐藏参数后的声明精确符合预期；2. 在 clean engine 下编译一个最小脚本模块，分别调用 `RequiresWorldContext(...)` 与 `CallableWithoutWorldContext(...)`，验证前者仍通过 hidden world-context 注入正常执行、后者在脚本面无需显式 world 参数也能成功调用；3. 对 required/optional 两条路径各补一条失败断言，防止 trait 与实际调用语义再次脱节。 |

#### Issue-54：`ReflectiveFallbackStats` 用 `FuncPtr` 单独统计 direct entry，会把缺失 `Caller` 的残缺条目误算为通过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.ReflectiveFallbackStats` |
| 行号范围 | 360-455 |
| 问题描述 | 这条统计回归在 381-398 行把 `FuncPtr.IsBound()` 直接当成 “direct binding” 判定条件，只要函数指针还在就计入 `DirectCount`；它既不检查 `Entry.Caller.IsBound()`，也没有把“函数指针还在、caller 已丢”的半残 `FFuncEntry` 单独分类。后面的 GAS sample 断言同样只验证 `WaitForAttributeChangedEntry->FuncPtr.IsBound()` 和 `!bReflectiveFallbackBound`，仍未确认 caller 仍然可调度。 |
| 影响 | 如果 generated entry 在回归后只剩下裸 `FuncPtr`、`Caller` 丢失或 direct dispatch 已不可用，这条测试仍可能继续把它统计成 direct，并满足 “DirectCount > 0 / GAS handwritten entry 仍是 direct” 的断言。结果是 report 看上去健康，但真正的脚本调度已破损。 |
| 修复建议 | 把 direct 谓词统一切到完整合同：复用 `BindConfigTests.cpp` 里的 `IsFunctionEntryBound(...)` 等价 helper，同时要求 `FuncPtr.IsBound()` 与 `Caller.IsBound()` 都为 true；把 “只有 `FuncPtr`、没有 `Caller`” 的 entry 单独计成 unresolved 或新增 `HalfBoundCount`，防止被吞进 direct 统计；GAS sample 也要同步补 `Caller.IsBound()` 断言，确保 handwritten entry 仍是完整 direct path。 |

#### Issue-55：`SkippedCsvOutput` / `SkippedReasonSummaryCsvOutput` 用裸字符串拆 CSV，报表字段一旦带逗号就会误报失败

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.SkippedCsvOutput` / `Angelscript.TestModule.Engine.GeneratedFunctionTable.SkippedReasonSummaryCsvOutput` |
| 行号范围 | 669-748 |
| 问题描述 | 两条 CSV 用例在 689-690 行和 733-734 行都用 `ParseIntoArray(TEXT(","), false)` 直接按逗号切分，然后把列数硬断言为 `4` 或 `2`。这不是 CSV-aware 解析：只要 `FailureReason` 或后续新增字段采用标准 CSV quoting，包含逗号、引号或转义内容，测试就会把合法报表误解析成多列并直接失败。尤其 `FailureReason` 本身就是生成器输出的自由文本，最容易随着错误文案演进出现逗号。 |
| 影响 | 当前测试把“文案里是否含逗号”错误地提升成格式回归，导致 artifact 生成器稍微改善说明文字或开始输出 quoted CSV 时，自动化会给出与真实功能无关的假红灯；同时也没有真正保护 CSV 语义，只保护了当前这版最简字符串布局。 |
| 修复建议 | 改用真正的 CSV parser（如 `FCsvParser` 或项目内等价 helper）读取 header 与行列，再按列名验证字段；`SkippedReasonSummaryCsvOutput` 还应把 summary 中每个 `FailureReason -> Count` 与 `SkippedEntries.csv` 明细按 reason 分桶后做精确比对，而不是只做总和相等。这样才能同时覆盖 quoting 合法性和 reason 汇总正确性。 |

#### Issue-56：`CollisionProfileCompile` 只取第一个 profile 做 happy-path 对比，几乎没有保护 identifier sanitization 映射

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.CollisionProfileCompile` |
| 行号范围 | 170-227 |
| 问题描述 | 这条 parity 用例虽然专门定义了 `SanitizeCollisionProfileIdentifier(...)`，但实际只取 `CollisionProfiles[0]`，随后唯一与 sanitize 相关的断言只是“结果非空”。它既没有确认这个 profile 名真的包含非法 identifier 字符，也没有断言 `SanitizedIdentifier` 相比原始 `ProfileName` 发生过任何替换。换句话说，只要数组第一个 profile 本来就是合法标识符，这条测试就只是在验证最普通的 `CollisionProfile::<Name>` happy path，而不是它名字里暗示的“profile 名 -> sanitized 常量名”映射合同。 |
| 影响 | 一旦带空格、横杠、数字前缀等特殊 profile 名不再生成可用的 `CollisionProfile::<Sanitized>` 常量，当前测试仍可能长期绿灯，因为它只覆盖了第一个简单 profile 的情况。真正容易回归的 identifier sanitization 分支没有被正式保护。 |
| 修复建议 | 把样本选择改成“优先找一个确实需要 sanitize 的 profile 名”；若默认 profile 集合里没有，就在测试 fixture 中临时注入一个含空格/横杠或数字前缀的 collision profile。补充强断言：`SanitizedIdentifier != ProfileName.ToString()`，再编译/执行脚本验证 `CollisionProfile::<Sanitized>` 仍能映射回原始 `FName`。当前第一个 profile 的 smoke 可以保留，但应降为附加健康度检查。 |

### 二、需要新增的测试

#### NewTest-41：给 `AngelscriptDebugValue.h` / `Helper_Reification.h` 补齐 offset helper 与 reify type-map 的原生单元测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/Helper_Reification.h` |
| 关联函数 | `TDebug<NativeType>::Instantiate(void*)` / `FDebugValueHandle::Get(...)` / `GetReifyType<T>()` |
| 现有测试覆盖 | 完全无测试；在本轮目标测试目录 `Core/`、`Subsystem/`、`GC/`、`FileSystem/` 以及 `AngelscriptRuntime/Tests` 中搜索 `FDebugValueHandle`、`TDebug<`、`GetReifyType<` 均无命中 |
| 风险评估 | 这层代码负责 debugger/local-variable 可视化时的基础偏移换算和 type reification；一旦指针算术或 type-id 映射回归，表面症状通常是“调试值错位、数组/struct 监视显示错对象、甚至访问越界”，但当前自动化没有任何独立定位点 |
| 建议测试名 | `Angelscript.TestModule.Engine.DebugValues.OffsetHelpersAndReifyTypeMapStayStable` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDebugValueTests.cpp` |
| 场景描述 | 写纯 native 单元测试，不依赖 `FAngelscriptEngine`。准备一块 `uint8 Buffer[16]`，在已知偏移写入哨兵值；先用 `FDebugValueHandle{4}` 验证 `Get<int32>(Buffer)` 指向正确偏移，再构造 `TDebug<int32>(4)` 并调用 `Instantiate(Buffer)`，断言 `Value` 指针与 handle 结果一致且能读到哨兵值。随后对 `Helper_Reification.h` 分 build 分支校验：`#if ((!UE_BUILD_TEST && !UE_BUILD_SHIPPING) && UE_BUILD_DEBUG)` 下断言 `GetReifyType<int32>()`、`GetReifyType<FString>()`、`GetReifyType<UObject*>()` 都非零且与 `GetReifyType<void>()` 不同；`#else` 下断言这些查询稳定返回 `0`，证明非 debug 构建不会误暴露伪 reify id |
| 输入/前置 | 无需 engine；文件内定义一个小型哨兵 buffer helper 与 `ReadSentinelAtOffset(...)` 即可 |
| 期望行为 | `FDebugValueHandle` 与 `TDebug::Instantiate` 必须把偏移 `4` 精确映射到同一内存位置；支持的 reified type 在 debug 分支下返回稳定非零 id，未知类型返回 `Unknown/0`；非 debug 分支下所有 reify 查询都应回落为 `0`，避免工具链错误地认为有可用 debug-value 实例化路径 |
| 使用的 Helper | 其他（纯 native buffer helper，无需 `FAngelscriptEngine`） |
| 优先级 | P3 |

---

## 测试审查 (2026-04-08 19:34)

### 一、现有测试问题

#### Issue-51：`EngineDisabledBindNames` 只验证执行被禁用，没有验证 `GetBindInfoList()` 也反映 engine-level disabled 状态

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.EngineDisabledBindNames` |
| 行号范围 | 317-365 |
| 问题描述 | 这条用例前半段会验证 named bind 在未禁用时执行一次，后半段通过 `Config.DisabledBindNames.Add(NamedBindName)` 和 `CollectDisabledBindNames(Engine)` 证明 engine config 能让执行路径跳过该 bind。但它在 357-364 行之后就直接结束，没有像前一个 `GlobalDisabledBindNames` 用例那样继续调用 `FAngelscriptBinds::GetBindInfoList(MergedDisabledBindNames)`，也没有检查对应 `FBindInfo->bEnabled == false`。因此，测试目前只保护了“engine-level disabled 会阻止执行”，没有保护“registry/query 视图也把该 bind 标成 disabled”。 |
| 影响 | 如果后续回归让 engine-level disabled 只影响 `CallBinds(...)`，却没有同步到 `GetBindInfoList(...)` 的 UI/调试视图，这条测试仍会稳定绿灯。结果是 settings-level 与 engine-level 两条禁用路径的外部可观测合同不一致，而当前自动化不会给出定位。 |
| 修复建议 | 直接复用 `GlobalDisabledBindNames` 的后半段断言：在 `MergedDisabledBindNames` 计算完成后，调用 `GetBindInfoList(MergedDisabledBindNames)`，查出 `NamedBindInfo`，并显式断言 `NamedBindInfo != nullptr` 且 `NamedBindInfo->bEnabled == false`。最好再补一个 control 断言，确认未禁用的邻近 bind 仍保持 `bEnabled == true`，把 engine-level merge 的查询语义完整建成合同。 |

#### Issue-52：`FunctionLevelScriptMethodUsesFirstParameterAsMixin` 只做子串检查，没有把最终脚本声明建成精确合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.FunctionLevelScriptMethodUsesFirstParameterAsMixin` |
| 行号范围 | 604-644 |
| 问题描述 | 用例在构造 `FAngelscriptFunctionSignature Signature(...)` 后，只断言了 4 个宽泛条件：`bStaticInUnreal == true`、`bStaticInScript == false`、`ArgumentTypes.Num() == 0`，以及 `Signature.Declaration` 同时 `Contains("const")` 和 `Contains("GetCoverageValue")`。它没有验证最终导出的脚本声明是否精确等于预期成员签名，也没有检查返回类型是否仍是 `int`、host mixin 是否仍绑定到 `UObject` 这一侧。换句话说，只要声明里还残留 `const` 和函数名，这条测试就可能继续通过。 |
| 影响 | 如果 `ScriptMethod` 元数据回归成错误的返回类型、错误的成员声明格式、额外限定符，或把 mixin 绑定到错误宿主，但声明字符串里仍包含 `GetCoverageValue` 与 `const`，当前测试不会精准报警。这样 `FunctionLevelScriptMethodUsesFirstParameterAsMixin` 就只能证明“看起来像成员函数”，不能证明脚本面实际拿到的是正确 API。 |
| 修复建议 | 把宽泛的 `Contains(...)` 升级成精确声明合同：直接断言 `Signature.Declaration == "int GetCoverageValue() const"` 或等价的完整预期字符串；同时补 `Signature.ReturnType` / `Signature.HostType` / `Signature.ScriptName` 的显式检查，确保 first-parameter-as-mixin 不只是“去掉了一个参数”，而是生成了正确的宿主、返回类型和 const 成员声明。 |

### 二、需要新增的测试

#### NewTest-39：给 `GC/` 目录补齐“脚本字段保活直到显式清空”的双阶段回收合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptComponent.cpp` |
| 关联函数 | runtime script UObject reference handling during manual `CollectGarbage()`（script-generated `UPROPERTY()` object references） |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp` 目前 3 条用例都只验证“销毁后弱指针最终失效”；没有任何场景让脚本字段先持有 `AActor`/`UActorComponent`，也没有“先保活、后清引用、再回收”的双阶段断言 |
| 风险评估 | 如果 Angelscript 生成类的对象引用没有正确参与 UE GC 引用图，当前自动化既抓不到“提前被收掉”，也抓不到“引用清空后仍泄漏”。这类回归会直接表现成 script 侧悬空引用或长生命周期泄漏，但现在没有定点回归。 |
| 建议测试名 | `Angelscript.TestModule.GC.ScriptReferences.DestroyedActorIsRetainedUntilFieldCleared` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScriptReferenceTests.cpp` |
| 场景描述 | 编译一个 `UScenarioGCHoldingComponent : UAngelscriptComponent`，其中包含 `UPROPERTY() AActor HeldActor;`、`SetHeldActor(AActor InActor)` 和 `ClearHeldActor()`。测试中创建 host actor + 该脚本组件，再 spawn 一个 target script actor；先通过 generated function 把 `HeldActor` 指向 target，再执行 `Destroy()` + `TickWorld()` + 第一次 `CollectGarbage()`；随后调用 `ClearHeldActor()` 并执行第二次 `CollectGarbage()`。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；`FActorTestSpawner`；复用当前 `GC` 文件里的 `CreateGCScenarioScriptComponent(...)`；通过 `FindGeneratedFunction(...)` + `ProcessEvent(...)` 或等价 helper 调用 `SetHeldActor/ClearHeldActor`；保留 `TWeakObjectPtr<AActor>` 指向 target actor |
| 期望行为 | 第一次 GC 后 `WeakTargetActor.IsValid()` 仍为真，且脚本组件里的 `HeldActor` 仍能解析到同一对象，证明 script 字段确实参与保活；第二次 GC（清空字段后）`WeakTargetActor.IsValid()` 必须变为假，组件中的 `HeldActor` 也应为 `nullptr`，证明回收会在 script 引用释放后发生而不是永久泄漏。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / `FActorTestSpawner` / `CreateGCScenarioScriptComponent(...)` / `FindGeneratedFunction(...)` |
| 优先级 | P1 |

#### NewTest-40：覆盖 `GetModuleByFilenameOrModuleName()` 在 filename 与 module name 冲突时的优先级合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::GetModuleByFilenameOrModuleName(const FString&, const FString&)` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 现有 lookup 用例只传“匹配的 filename + 匹配的 module name”，以及 `PathNormalizationLookup` 的 module-name fallback；没有任何用例构造“filename 指向模块 A，但 module name 参数指向模块 B”的冲突场景 |
| 风险评估 | 一旦 lookup 优先级回归，热重载、重命名和 remap 路径就可能在 filename 命中失败时静默落到错误的 module-name 结果上，表现成“找到的是错模块但名字还合法”，这类问题很容易绕过当前全绿测试。 |
| 建议测试名 | `Angelscript.TestModule.FileSystem.ModuleLookupByFilenameOrModuleName.PrefersFilenameOverMismatchedModuleName` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemLookupPrecedenceTests.cpp` |
| 场景描述 | 在独立临时 root 下分别写入并编译 `Lookup/A.as -> Game.Lookup.A` 与 `Lookup/B.as -> Game.Lookup.B`。随后调用 `GetModuleByFilenameOrModuleName(PathA, "Game.Lookup.B")` 和 `GetModuleByFilenameOrModuleName(PathB, "Game.Lookup.A")`；最后再加一个 control：传入不存在的 filename + 有效 module name，确认 API 仍会回退到 module-name 分支。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；复用 `WriteFileSystemTestFile(...)` / `CleanFileSystemTestRoot()`；`CompileModuleFromMemory(...)` 编译两个模块；`ON_SCOPE_EXIT` 里 `DiscardModule("Game.Lookup.A")`、`DiscardModule("Game.Lookup.B")` 并清理磁盘目录 |
| 期望行为 | 冲突场景下，`PathA` 查询必须返回 `Game.Lookup.A`，`PathB` 查询必须返回 `Game.Lookup.B`，且 `ModuleDesc->Code[0].AbsoluteFilename` 与传入 filename 完全一致；只有在 filename 不存在时，API 才允许回退到 module-name 命中。这样才能把“filename 优先、module name 兜底”的合同固定下来。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / `CompileModuleFromMemory(...)` / `WriteFileSystemTestFile(...)` / `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-51 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:17)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-38：给 `FAngelscriptType::GetDebuggerValueFromFunction(...)` 补齐自动求值黑名单与源属性追踪合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptType::GetDebuggerValueFromFunction(asIScriptFunction*, void*, FDebuggerValue&, asITypeInfo*, UStruct*, const FString&)` |
| 现有测试覆盖 | 有零散 debugger value 覆盖，但没有任何用例命中这个自动求值入口；全仓库测试与当前报告中搜索 `GetDebuggerValueFromFunction` 均无命中，现有 `AngelscriptTypeTests` / `ContainerCompareBindingsTests` 只验证 `GetDebuggerValue` / `GetDebuggerMember` 的基础格式化，不验证 auto-evaluate、blacklist 拒绝或 `NonTemporaryAddress` 追踪 |
| 风险评估 | 一旦 debugger auto-evaluation 无视 `DebuggerBlacklistAutomaticFunctionEvaluation(WithoutWorldContext)`、错误执行无 world 的函数，或返回临时值时无法把 watch 重新挂回真实属性地址，调试器就可能在 watch 展开时触发副作用、给出不可刷新的假值，甚至持续监控错误内存；当前 RuntimeCore 没有任何定点回归能提前发现 |
| 建议测试名 | `Angelscript.TestModule.Engine.Debugger.AutoEvaluate.RespectsBlacklistAndTracksSourceProperty` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDebuggerAutoEvaluationTests.cpp` |
| 场景描述 | 在 isolated/full engine 下复用 `UAngelscriptUhtCoverageTestLibrary::GetCoverageValue` 这条 `ScriptMethod` 绑定路径做正向验证：通过 `FAngelscriptFunctionSignature` 生成声明，向真实已绑定的 `UObject` script type 取出 `GetCoverageValue` 对应的 `asIScriptFunction`；创建 `UAngelscriptUhtCoverageTestObject` 实例并把 `StoredValue` 设为 `42`，调用 `FAngelscriptType::GetDebuggerValueFromFunction(...)`，传入 `ContainerClass = UAngelscriptUhtCoverageTestObject::StaticClass()` 与 `PropertyAddrToSearchFor = "StoredValue"`。随后再把 `Signature.ClassName + "." + Signature.ScriptName` 临时加入 `DebuggerBlacklistAutomaticFunctionEvaluationWithoutWorldContext`，在同一个无 world 的 `UObject` 实例上重跑一次，验证 blacklist 拒绝生效 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或等价 full-engine helper；`UAngelscriptUhtCoverageTestLibrary::StaticClass()->FindFunctionByName(TEXT("GetCoverageValue"))`；`FAngelscriptFunctionSignature`；`asITypeInfo::GetMethodByDecl(...)`；`FProperty* StoredValueProperty = FindFProperty<FProperty>(UAngelscriptUhtCoverageTestObject::StaticClass(), TEXT("StoredValue"))`；用 `ON_SCOPE_EXIT` 保存并恢复 `UAngelscriptSettings::Get()->DebuggerBlacklistAutomaticFunctionEvaluationWithoutWorldContext` |
| 期望行为 | 正向 case 中 `GetDebuggerValueFromFunction(...)` 必须返回 `true`，`OutValue.Value == TEXT("42")`，`OutValue.bTemporaryValue == true`，且 `OutValue.GetNonTemporaryAddress()` 与 `StoredValueProperty->ContainerPtrToValuePtr<void>(Target)` 完全一致，`OutValue.GetAddressToMonitorValueSize() == sizeof(int32)`；blacklist case 中同一 `ScriptFunction` 在 worldless object 上必须返回 `false`，证明 without-world-context blacklist 真正阻止了 auto-evaluate，而不是只在配置层存在 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptFunctionSignature` + `FindFProperty` + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | 无 |
| 新增测试建议 | `NewTest-38` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:36)

### 本轮汇总（补末尾索引；本轮详细新增条目已记录于上方 `2026-04-08 18:35` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-45` / `Issue-46` / `Issue-47` / `Issue-48` |
| 新增测试建议 | `NewTest-35` / `NewTest-36` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-46 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-45 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:51)

### 一、现有测试问题

#### Issue-49：`HitResultCompile` 写入 `BoneName` / `MyBoneName` 却从未读回，`FHitResult` 名称字段回归会被直接漏掉

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.HitResultCompile` |
| 行号范围 | 590-622 |
| 问题描述 | 脚本片段先后写入 `Hit.FaceIndex`、`Hit.ElementIndex`、`Hit.Item`、`Hit.MyItem`，同时还显式给 `Hit.BoneName` 和 `Hit.MyBoneName` 赋了 `FName("Bone")` / `FName("MyBone")`。但最终返回值只把四个整数字段求和，测试端也只断言 `Context->GetReturnDWord() == 10`。这意味着该用例虽然表面覆盖了 `FHitResult` 的 `FName` 字段写入语法，实际上完全没有验证 `BoneName` / `MyBoneName` 的绑定、存储和读回语义。 |
| 影响 | 一旦 `FHitResult` 的 `FName` 属性桥接回归，例如字段名映射错位、setter/getter 丢失或名称值被静默截断，当前 parity 用例仍会稳定绿灯，因为四个整数字段路径依然足以返回 10。真正会影响命中骨骼/命名骨节点逻辑的回归因此没有测试保护。 |
| 修复建议 | 把返回值改成同时验证名称字段，例如返回 `Hit.BoneName.Compare(FName("Bone")) == 0 && Hit.MyBoneName.Compare(FName("MyBone")) == 0 ? 10 : -1`，或拆成额外 helper 函数分别返回两个 `Compare(...)` 结果；若想继续保留整数字段覆盖，也应把 `FaceIndex` / `ElementIndex` / `Item` / `MyItem` 与 `BoneName` / `MyBoneName` 两类断言同时纳入 pass/fail，而不是只验证前者。 |

#### Issue-50：`ModuleLookupByFilename` / `RenameUpdatesModuleLookup` 只看“能找到”和 `ModuleName`，没有证明 filename lookup 命中的就是目标路径对应的 `ModuleDesc`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.ModuleLookupByFilename` / `RenameUpdatesModuleLookup` |
| 行号范围 | 106-119, 360-362 |
| 问题描述 | `ModuleLookupByFilename` 在成功编译后同时取 `ModuleByName`、`ModuleByFilename`、`ModuleByEither`，但只断言三者 `IsValid()` 且 `ModuleName == "Game.AI.Patrol"`；`RenameUpdatesModuleLookup` 在重编译 renamed 路径后，也只检查新路径 lookup 有效、旧路径 lookup 无效，以及 module-name lookup 还能执行。两条用例都没有比较这些 lookup 是否返回同一个 `TSharedPtr<FAngelscriptModuleDesc>`，也没有检查 `ModuleDesc->Code[0].AbsoluteFilename` 已切到当前目标路径。 |
| 影响 | 如果 runtime 在 filename remap 或 path-index 更新时留下了“同名但不同 descriptor”的脏状态，或者新路径 lookup 命中的是一个 `ModuleName` 正确但 `Code` 里仍指向旧文件的残留 desc，当前测试仍可能全部通过。这样 filename lookup 最关键的“路径索引与模块实体一一对应”合同就没有被真正保护。 |
| 修复建议 | 在 `ModuleLookupByFilename` 中补强 identity/path 断言：`TestTrue(..., ModuleByName == ModuleByFilename && ModuleByFilename == ModuleByEither)`，并验证 `ModuleByFilename->Code.Num() > 0` 且 `ModuleByFilename->Code[0].AbsoluteFilename` 与 `AbsolutePath` 完全一致。`RenameUpdatesModuleLookup` 也应对 renamed 后返回的 `ModuleDesc` 做同样检查，确认 `Code[0].AbsoluteFilename == NewAbsolutePath`，而不是只看 `ModuleName` 和执行结果。 |

### 二、需要新增的测试

#### NewTest-37：给 `FCpuProfilerTraceScoped` 补齐脚本可见构造/析构的 compile-execute 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FCpuProfilerTraceScoped.h` |
| 关联函数 | `FCpuProfilerTraceScoped::FCpuProfilerTraceScoped(const FName&)` / `~FCpuProfilerTraceScoped()` |
| 现有测试覆盖 | 完全无测试；在当前 `Core/Subsystem/GC/FileSystem` 目标测试与 `AngelscriptRuntime/Tests` 中搜索 `FCpuProfilerTraceScoped` 均无命中 |
| 风险评估 | 一旦 struct 绑定、构造签名或析构路径回归，脚本里的 scoped CPU profiler 事件会从“静默不生效”到“直接编译失败/执行崩溃”不等；当前自动化没有任何覆盖点能提示这类开发期 profile helper 已失效 |
| 建议测试名 | `Angelscript.TestModule.Engine.Profiling.CpuProfilerTraceScoped.CompilesAndExecutesAsLocalScope` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCpuProfilerTraceScopedTests.cpp` |
| 场景描述 | 在 clean engine 下编译两个最小脚本：1. `int CheckSingleScope() { FCpuProfilerTraceScoped Scope(FName("Automation.Scope")); return 17; }`；2. `int CheckNestedScopes(bool bEarlyReturn) { FCpuProfilerTraceScoped Outer(FName("Automation.Outer")); { FCpuProfilerTraceScoped Inner(FName("Automation.Inner")); if (bEarlyReturn) return 23; } return 29; }`。分别执行普通返回和 early-return 分支，验证脚本局部对象的构造/析构路径都能走通 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；`ASTEST_COMPILE_RUN_INT` 或等价 compile+execute helper；两个唯一 module name，避免与其他 profiling 用例冲突 |
| 期望行为 | 两段脚本都必须编译成功并返回预期常量 `17` / `23` / `29`；`FCpuProfilerTraceScoped` 作为局部变量和嵌套局部变量都不应导致 prepare/execute 失败；如果将来补到可观测 trace helper，还应在 `CPUPROFILERTRACE_ENABLED` 下进一步断言 begin/end 事件数量平衡 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-49` / `Issue-50` |
| 新增测试建议 | `NewTest-37` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-49 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:35)

### 一、现有测试问题

#### Issue-45：`ObserveStartupBindPass` 在 helper 里用 `check()`，会把 bind 启动回归升级成整批崩溃

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.StartupBindInfoPreservesOrder` / `StartupPathMergesDisabledBindNames` |
| 行号范围 | 125-146, 420-490 |
| 问题描述 | 两条 startup-path 用例都依赖 `ObserveStartupBindPass(...)`。这个 helper 在 135-136 行创建 full engine 后直接 `check(Engine.IsValid())`，随后才返回 `FAngelscriptBindExecutionSnapshot`。也就是说，一旦 startup bind 初始化因为 bind 排序、disabled-merge、engine 创建或更早的全局状态污染而失败，测试不会留下常规 `TestTrue`/`TestNotNull` 诊断，而是直接触发 fatal assert 中断本轮自动化。 |
| 影响 | `StartupBindInfoPreservesOrder` 和 `StartupPathMergesDisabledBindNames` 本应提供 bind 启动路径的定点回归信息，但现在任何 engine 创建失败都会把问题扩大成整批测试崩溃，既丢失单测级上下文，也会掩盖后续文件里的真实缺口。 |
| 修复建议 | 把 `ObserveStartupBindPass(...)` 改成返回 `TOptional<FAngelscriptBindExecutionSnapshot>` 或带 `bValid`/`FailureReason` 的结构；在 `RunTest()` 内用 `TestTrue`/`TestNotNull` 报告 engine 创建是否成功，再继续做 invocation/order/disabled-set 断言。测试代码不应使用 `check()` 处理可预期的回归路径。 |

#### Issue-46：`BlueprintInternalUseOnlyCanBeOverriddenForAngelscript` 只看 metadata 和 skip predicate，没有证明 override 函数真的暴露到脚本面

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.BlueprintInternalUseOnlyCanBeOverriddenForAngelscript` |
| 行号范围 | 588-601 |
| 问题描述 | 这条用例只验证两件事：`InternalCallableWithOverride` 带 `UsableInAngelscript` metadata，以及 `FAngelscriptBinds::ShouldSkipBlueprintCallableFunction(...)` 对 override/control 两个函数给出相反结果。它没有创建 engine，也没有检查 `UAngelscriptUhtCoverageTestLibrary` 的 `ClassFuncMaps`/script type surface 是否真的注册了 `InternalCallableWithOverride` 并排除了 `InternalCallableWithoutOverride`。如果后续注册阶段、generated function table 或 script-signature 生成仍然把 override 函数漏掉，当前测试仍会绿灯。 |
| 影响 | 该用例名声称“可以为 Angelscript 覆盖 BlueprintInternalUseOnly”，但现在只保护了前置 predicate，不保护最终脚本暴露合同。真正用户可见的退化会发生在 bind 注册或脚本编译阶段，而现有断言对此完全失明。 |
| 修复建议 | 在 clean engine 下补最终暴露面的断言：定位 `UAngelscriptUhtCoverageTestLibrary` 的 `ClassFuncMaps` 或 script type，确认 `InternalCallableWithOverride` 被注册、`InternalCallableWithoutOverride` 仍被过滤；再编译一个最小脚本直接调用 override 函数并断言返回值，证明不只是 metadata 正确，而是脚本面确实可见且可调用。 |

#### Issue-47：`EditorOutputsUseWithEditorGuard` 只检查文件前缀，没验证 `WITH_EDITOR` guard 的完整结构

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.EditorOutputsUseWithEditorGuard` |
| 行号范围 | 242-266 |
| 问题描述 | 用例对 editor/runtime 两个生成文件只做了 `StartsWith(TEXT("#if WITH_EDITOR"))` 和“不以该前缀开头”两条断言。它没有验证 editor 文件是否存在匹配的尾部 `#endif`，没有确认 guard 是否包住整份文件，也没有检查 runtime 文件里是否意外混入顶层 `WITH_EDITOR` 包裹。只要 editor 产物恰好以 `#if WITH_EDITOR` 开头，这条测试就会通过，即便后续 preprocessor 结构已经损坏。 |
| 影响 | UHT 产物最关键的是“editor-only shard 被完整包在正确 guard 中”。当前测试只能发现前缀缺失，不能发现缺少闭合、guard 范围漂移或 runtime 输出被错误包裹等结构性错误，容易在真正编译阶段才暴露问题。 |
| 修复建议 | 把断言升级为结构检查：读取首个非空行和末个非空行，要求 editor 文件分别是 `#if WITH_EDITOR` / `#endif`；同时确认 runtime 文件不存在顶层 `WITH_EDITOR` 包裹。若项目内有轻量 preprocessor/brace helper，可进一步校验 guard 成对出现且覆盖整份 shard。 |

#### Issue-48：`SkinnedMeshCompile` 只验证 type info 上有两个方法名，不能证明脚本声明和调用桥接真的可用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.SkinnedMeshCompile` |
| 行号范围 | 132-148 |
| 问题描述 | 这条 parity 用例只通过 `GetTypeInfoByName("USkinnedMeshComponent")` 拿到 type info，再用 `GetMethodByDecl("void UpdateLODStatus()")` 和 `GetMethodByDecl("void InvalidateCachedBounds()")` 做两个非空检查。它没有编译任何脚本，也没有验证这两个声明在真实脚本上下文里可解析、可调用。若 type info 仍保留方法名，但签名桥接、call thunk 或脚本可见性发生回归，当前用例仍会稳定绿灯。 |
| 影响 | 该用例名为 `SkinnedMeshCompile`，但实际没有 compile 行为，只保护了“类型表面存在方法名”。一旦 parity 问题出在 declaration 生成、参数适配或 direct/fallback 调用路径，当前测试不会提供任何报警。 |
| 修复建议 | 把它升级成真正的 compile parity：改用 clean clone engine 编译最小脚本 `void Check(USkinnedMeshComponent Comp) { Comp.UpdateLODStatus(); Comp.InvalidateCachedBounds(); }`，至少断言 `CompileFunction(...) == asSUCCESS` 且函数可取到；如果能方便构造组件实例，再补一次执行 smoke，证明方法不仅可见，而且调用路径完整。 |

### 二、需要新增的测试

#### NewTest-35：给 `FunctionCallers.h` 补齐 direct-call marshalling 的 value/reference/pointer 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h` |
| 关联函数 | `FGenericFuncPtr::Make(...)` / `MakeAutoFunctionPtr(...)` / `MakeAutoMethodPtr(...)` / `ASAutoCaller::MakeFunctionCaller(...)` / `RedirectFunctionCaller(...)` / `RedirectMethodCaller(...)` |
| 现有测试覆盖 | 完全无测试；当前 RuntimeCore 只在 `BindConfig` / `GeneratedFunctionTable` 间接检查 `FFuncEntry` 是否“看起来已绑定”，没有任何用例直接验证 type-erased function pointer 与 `ASAutoCaller` 的参数转发、引用回传和 method dispatch |
| 风险评估 | 这是 direct bind 调用链的基础设施。一旦 value/reference/pointer marshalling、const method dispatch 或 return-value reification 出错，海量 generated bind 会在运行时静默返回错值、写坏 out/ref 参数，现有测试只能看到高层症状，无法快速定位到 caller 层 |
| 建议测试名 | `Angelscript.TestModule.Engine.FunctionCallers.DirectCallersRoundTripValueReferenceAndPointerArguments` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionCallersTests.cpp` |
| 场景描述 | 在纯 native 测试里定义一个 `FFunctionCallerHarness`，提供 1. 一个全局函数 `int32 GlobalAddAndBump(int32 Value, int32& InOut)`；2. 一个成员函数 `int32 AddToBias(int32 Value, int32& InOut)`；3. 一个 `const` 成员函数 `const int32& GetBiasRef() const`；4. 一个带指针参数的函数 `void CopyFromPtr(const int32* InValue, int32& OutValue)`。分别通过 `ERASE_AUTO_FUNCTION_PTR` / `ERASE_AUTO_METHOD_PTR` 和 `ASAutoCaller::MakeFunctionCaller(...)` 组装 `FFuncEntry`，再用 `void*` 参数数组直接走 caller thunk |
| 输入/前置 | 无需 `FAngelscriptEngine`；一个文件内 `InvokeCaller(const FFuncEntry&, void** Args, void* ReturnValue)` helper，按 `Caller.type` 分发到 `Caller.FuncPtr`/`Caller.MethodPtr`；若参数为引用，按 `PassArgument(...)` 约定准备二级指针 |
| 期望行为 | 全局函数 case 返回值必须等于输入求和值且 `InOut` 被正确回写；成员函数 case 必须读取到 `Args[0]` 提供的对象并基于对象状态返回结果；`const` 成员函数必须返回对对象内部字段的引用而非错误副本；指针参数 case 必须正确读取 `const int32*` 并写回 `OutValue`。同时断言各 `FFuncEntry` 的 `FuncPtr.IsBound()` 与 `Caller.IsBound()` 都为真 |
| 使用的 Helper | 其他（纯 native invoke helper，无需 engine/world） |
| 优先级 | P1 |

#### NewTest-36：给 `UAngelscriptSettings` 补齐默认 namespace-strip 与 debugger blacklist 的配置合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` |
| 关联函数 | `UAngelscriptSettings::Get()` 及默认字段：`BlueprintLibraryNamespacePrefixesToStrip` / `BlueprintLibraryNamespaceSuffixesToStrip` / `DebuggerBlacklistAutomaticFunctionEvaluationWithoutWorldContext` / `EditorMaximumScriptExecutionTime` |
| 现有测试覆盖 | 完全无直接测试；现有 RuntimeCore 只间接读取 `DisabledBindNames`、`bUseEditorScripts` 等个别字段，没有任何用例保护其余默认配置是否仍满足当前 script surface 和 debugger 假设 |
| 风险评估 | 一旦默认前后缀 strip 列表或 debugger world-context blacklist 被意外改动，脚本暴露名和自动求值安全边界会静默漂移；很多高层 parity/调试问题会以“脚本名变了”或“无 world context 下 debugger 触发副作用”形式出现，但当前没有定点回归 |
| 建议测试名 | `Angelscript.TestModule.Engine.Settings.DefaultSurfaceAndDebuggerContractsRemainStable` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSettingsTests.cpp` |
| 场景描述 | 直接读取 `UAngelscriptSettings::Get()` 默认对象，验证当前 runtime 明确依赖的默认列表和值：namespace prefix strip 至少包含 `UKismet`、`UBlueprint`；suffix strip 至少包含 `Statics`、`Library`、`BlueprintFunctionLibrary`；without-world-context debugger blacklist 至少包含 `AActor.GetWorldTimerManager`、`AActor.GetGameInstance`、`AActor.GetPhysicsVolume`、`AActor.GetActorTimeDilation`；`EditorMaximumScriptExecutionTime` 保持正数 |
| 输入/前置 | 无需 engine；如担心用户本地 ini 覆盖，可在 helper 中同时读取 CDO 当前值并明确记录“这是默认配置合同测试”，必要时在测试前临时保存/恢复相关 config 数组 |
| 期望行为 | 上述前缀、后缀和 blacklist 项都必须存在；`EditorMaximumScriptExecutionTime > 0.f`；如果项目约定 `bAllowImplicitPropertyAccessors`、`bAutomaticImports`、`bUseScriptNameForBlueprintLibraryNamespaces` 等默认值不可漂移，也应一并断言其布尔默认值 |
| 使用的 Helper | 其他（纯 config/CDO 读取 helper，无需 `FAngelscriptEngine`） |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-46 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-45 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:21)

### 一、现有测试问题

#### Issue-43：`EnginePerformanceTests` 在测量 helper 里使用 `check()`，回归时会直接打断整批自动化

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.Core.Performance.Startup.Full` / `Startup.Clone` / `Startup.CreateForTestingFallbackFull` / `Startup.CreateForTestingClone` |
| 行号范围 | 101-157 |
| 问题描述 | `MeasureFullStartup()`、`MeasureCloneStartup()`、`MeasureCreateForTestingFallbackStartup()`、`MeasureCreateForTestingCloneStartup()` 在创建 `FAngelscriptEngine` 或 clone/source engine 后，统一用 `check(Engine.IsValid())`、`check(SourceEngine.IsValid())`、`check(CloneEngine.IsValid())` 做保护。这样一旦启动路径本身发生回归，性能测试不会留下常规 `TestTrue`/`TestNotNull` 失败信息，而是直接触发 fatal assert 终止进程。 |
| 影响 | 启动失败这类本应定位到单条用例的回归，会把整批 automation 提前打断，后续测试与 artifact 全部失去执行机会；同时日志只剩 assert 现场，缺少“哪条性能合同失效”的测试级上下文。 |
| 修复建议 | 把四个 `Measure*` helper 改成返回 `TOptional<FStartupPerformanceSample>` 或带 `bValid`/`FailureReason` 的结果结构，在 `RunTest()` 里用 `TestTrue`/`TestNotNull` 报告 engine/source/clone 创建失败；只有在样本有效时才继续写性能 artifact。测试代码应避免用 `check()` 把可预期回归升级成进程级崩溃。 |

#### Issue-44：`DelegateWithPayloadCompile` 只检查类型表面存在，没有保护 delegate bind/payload 语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.DelegateWithPayloadCompile` |
| 行号范围 | 151-167 |
| 问题描述 | 这条 parity 用例只取 `FAngelscriptDelegateWithPayload` 的 `TypeInfo`，然后断言脚本类型系统里能找到 `IsBound()` 和 `ExecuteIfBound()` 两个方法。它既没有编译/执行任何脚本，也没有在 native 侧实例化 delegate、调用 `BindUFunction(...)` / `BindUFunctionWithPayload(...)`、验证 payload boxing，或检查 `Reset()` 后状态是否清空。 |
| 影响 | 只要绑定表面还保留这两个方法名，delegate 真实行为即使已经回归为“永远不触发 / payload 传错 / reset 无效”，当前 parity 用例仍会稳定绿灯。对于 `FAngelscriptDelegateWithPayload` 这种运行时语义远重于类型可见性的接口，这属于明显的假覆盖。 |
| 修复建议 | 把这条 parity smoke 升级成真实行为合同：至少在 clean engine 下创建一个 receiver object，验证 `IsBound()` 初始为 false、`BindUFunction(...)` 后变 true、`ExecuteIfBound()` 能触发目标 `UFUNCTION`；再补一段 `BindUFunctionWithPayload(...)` 的 primitive payload round-trip，最后 `Reset()` 后再次执行应为 no-op。若要保留当前 type-surface 检查，也应降为前置 smoke，而不是唯一断言。 |

### 二、需要新增的测试

#### NewTest-33：给 `FAngelscriptAnyStructParameter` 补齐 implicit constructor 的类型和值保真合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAnyStructParameter.h` |
| 关联函数 | `FAngelscriptInstancedStructHelpers::ImplicitConstructAnyStruct(...)` / `ImplicitConstructAnyStructFromInstancedStruct(...)` |
| 现有测试覆盖 | 完全无测试；在本轮目标测试目录中搜索 `FAngelscriptAnyStructParameter`、`ImplicitConstructAnyStruct`、`FInstancedStruct` 的 wildcard/any-struct 构造路径均无命中 |
| 风险评估 | 一旦 any-struct implicit constructor 丢失 struct type、复制错数据，或从 `FInstancedStruct` 转入时把 payload 清空，脚本侧所有依赖 `?&in`/任意 struct 参数的 API 都可能“能编译但拿错值”，当前 RuntimeCore 自动化没有任何定位点 |
| 建议测试名 | `Angelscript.TestModule.Engine.AnyStructParameter.ImplicitConstructorsPreserveStructTypeAndValue` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAnyStructParameterTests.cpp` |
| 场景描述 | 在 isolated full engine 下准备一个 `FIntPoint(3, 4)` 和一个已初始化的 `FInstancedStruct`。第一段调用 `ImplicitConstructAnyStruct(...)` 把 `FIntPoint` 直接装入 `FAngelscriptAnyStructParameter`；第二段调用 `ImplicitConstructAnyStructFromInstancedStruct(...)` 把现成 `FInstancedStruct` 装入同类参数 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；通过 `Engine.GetScriptEngine()->GetTypeIdByDecl("FIntPoint")` 取得 type id；`TBaseStructure<FIntPoint>::Get()` 作为期望 script struct；一个小 helper 负责从 `InstancedStruct.GetMemory()` 读回 `FIntPoint` |
| 期望行为 | 第一段构造后 `Param.InstancedStruct.IsValid()==true`，`GetScriptStruct()` 等于 `TBaseStructure<FIntPoint>::Get()`，且读回的 `X==3`、`Y==4`；第二段构造后 `Param.InstancedStruct` 的类型和值都与源 `FInstancedStruct` 完全一致，不允许因为 implicit constructor 路径把内容重置或降级为空结构 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptInstancedStructHelpers` + `TBaseStructure<FIntPoint>` |
| 优先级 | P2 |

#### NewTest-34：给 `FAngelscriptType` 建立 alias/type-finder 注册与 reset 的生命周期合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` / `AngelscriptType.h` |
| 关联函数 | `FAngelscriptType::RegisterAlias(...)` / `RegisterTypeFinder(...)` / `GetByProperty(...)` / `ResetTypeDatabase()` |
| 现有测试覆盖 | 有零散覆盖但没有直接命中核心合同：当前目标测试只用 `FAngelscriptType::GetTypes().Num()` 验证 full-engine teardown、在 `BindConfig` 中用 `GetByClass(UObject)` 构造 signature，并通过 `SoftReferenceCppForm` 触发 `GetCppForm()`；没有任何用例直接保护 alias 查找、property type-finder 优先级或 reset 清理 |
| 风险评估 | 如果 alias/type-finder 在测试或运行时 epoch 之间残留，property 解析和 script 类型名查找会静默漂移，表现成“编译走错类型映射”或“上一轮注册污染下一轮”；现有 lifecycle smoke 只看 type count，捕不到这类更细的状态泄漏 |
| 建议测试名 | `Angelscript.TestModule.Engine.TypeDatabase.AliasAndTypeFindersResetCleanly` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTypeDatabaseTests.cpp` |
| 场景描述 | 在无 current-engine 的隔离上下文里定义一个本地 fake `FAngelscriptType`，给它注册 base type、alias `AutomationAlias`，并注册一个 type finder，把 `UAngelscriptUhtCoverageTestObject::StoredValue` 映射到该 fake type。先验证 alias 查找、`GetByProperty(...)` 和 `FAngelscriptTypeUsage::FromProperty(...)` 都命中 fake type；随后调用 `ResetTypeDatabase()` 再重新查询 |
| 输入/前置 | `FCoreTestContextStackGuard` 或等价 helper 清空 `ContextStack`，确保 `FAngelscriptType` 落在 legacy test database；`ON_SCOPE_EXIT` 二次调用 `ResetTypeDatabase()`；`FProperty* StoredValueProperty = UAngelscriptUhtCoverageTestObject::StaticClass()->FindPropertyByName(TEXT("StoredValue"))`；一个本地 fake type subclass |
| 期望行为 | reset 前：`GetByAngelscriptTypeName(TEXT("AutomationAlias"))` 返回 fake type，`GetByProperty(StoredValueProperty)` 与 `FromProperty(StoredValueProperty)` 也解析到同一类型；reset 后：`GetTypes().Num()==0`，alias 查找返回 null，`GetByProperty(...)` 返回 null，`FromProperty(...)` 变为 invalid usage，证明 alias/type-finder 状态没有跨 epoch 泄漏 |
| 使用的 Helper | `FCoreTestContextStackGuard` + 本地 fake `FAngelscriptType` helper，无需完整 `FAngelscriptEngine` |
| 优先级 | P1 |

### 本轮汇总

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-43` / `Issue-44` |
| 新增测试建议 | `NewTest-33` / `NewTest-34` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-44 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-43 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:08)

### 二、需要新增的测试

#### NewTest-29：覆盖 `UAngelscriptAbilitySystemComponent` 的 attribute-change callback 注册、去重与 trampoline 广播

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `RegisterAttributeChangedCallback(...)` / `GetAndRegisterAttributeChangedCallback(...)` / `RegisterCallbackForAttribute(...)` / `GetAndRegisterCallbackForAttribute(...)` / `OnAttributeChangedTrampoline(...)` |
| 现有测试覆盖 | 当前 `RuntimeCore_TestGaps.md` 已建议覆盖 `RegisterAttributeSet(...)` 和 `GiveAbility(...)`，但对 attribute-change callback 这组 API 仍是 0 命中；在目标测试目录和 `AngelscriptRuntime/Tests` 里搜索上述函数名同样没有任何现有用例 |
| 风险评估 | 一旦 callback 重复注册、初始值回填错误，或 deprecated trampoline 不再把 `OldValue/NewValue` 正确转成 `FAngelscriptModifiedAttribute`，脚本侧属性监听会表现成重复回调、漏回调或错误数值，而当前自动化没有定位点 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.AttributeCallbacksDeduplicateAndReportValues` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemCallbackTests.cpp` |
| 场景描述 | 创建带 `UAngelscriptAbilitySystemComponent` 的 native test actor 和一个最小 `UAngelscriptAttributeSet`（例如 `Health` 属性）。先给 attribute 设置初值，再用带 `UFUNCTION` 计数器的 listener object 调 `GetAndRegisterAttributeChangedCallback(...)` 两次，随后通过 `SetAttributeBaseValue(...)` 或 `ApplyModToAttributeUnsafe(...)` 触发值变化；同一用例里再走一遍 deprecated `GetAndRegisterCallbackForAttribute(...)`，观察 `OnAttributeChanged` multicast 是否收到 trampoline 广播 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或等价 ASC fixture；native test actor；native test attribute set；listener UObject；`Health` 初值 `10.f`，更新后值 `25.f` |
| 期望行为 | 第一次 `GetAndRegisterAttributeChangedCallback(...)` 返回的 `OutCurrentValue == 10.f`；同一 listener 重复注册后，属性变化只触发一次 UFUNCTION 回调；回调载荷中的 attribute 名称为 `Health`，`OldValue == 10.f`、`NewValue == 25.f`；deprecated trampoline 路径会广播一次 `OnAttributeChanged`，并产生同样的 `FAngelscriptModifiedAttribute` 三元组 |
| 使用的 Helper | `FAngelscriptTestFixture` + native ASC/attribute-set fixture + listener test double |
| 优先级 | P1 |

#### NewTest-30：给 `InitAbilityActorInfo`、tag mirror API 和 `OnOwnedTagUpdated` 建立运行时合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `GetAbilityActorInfo()` / `GetAvatar()` / `GetPlayerController()` / `HasGameplayTag(...)` / `HasAllGameplayTags(...)` / `HasAnyGameplayTags(...)` / `InitAbilityActorInfo(...)` / `OnTagUpdated(...)` |
| 现有测试覆盖 | 当前文档里的 GAS 提议还没有直接命中这些 actor-info 与 gameplay-tag wrapper；目标测试目录和 `AngelscriptRuntime/Tests` 中也没有搜索命中 |
| 风险评估 | 如果 actor info 初始化没广播、avatar/player-controller wrapper 失配，或 tag mirror API / `OnOwnedTagUpdated` 委托回归，脚本和蓝图看到的 ASC 状态会与底层 GAS 状态脱节，但现有自动化不会报警 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.ActorInfoAndOwnedTagMirrorsStayInSync` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemLifecycleTests.cpp` |
| 场景描述 | 创建一个带 `UAngelscriptAbilitySystemComponent` 的 actor fixture，并准备可选 `APlayerController`。先绑定 `OnInitAbilityActorInfo` 与 `OnOwnedTagUpdated` 监听器，然后调用 `InitAbilityActorInfo(Owner, Avatar)`；接着向 ASC 添加和移除两个 loose gameplay tags，并分别通过 `HasGameplayTag`、`HasAllGameplayTags`、`HasAnyGameplayTags` 查询 |
| 输入/前置 | ASC fixture；owner/avatar actor；可选 player controller；测试 tags `GameplayTag.Tests.Primary` 与 `GameplayTag.Tests.Secondary`；delegate recorder helper |
| 期望行为 | `InitAbilityActorInfo(...)` 后 `GetAbilityActorInfo()` 有效，`GetAvatar()` 返回传入 avatar，若 fixture 带 controller 则 `GetPlayerController()` 返回同一实例；`OnInitAbilityActorInfo` 只广播一次且参数等于传入 owner/avatar；添加两个 tag 后 `HasGameplayTag(Primary)`、`HasAllGameplayTags({Primary,Secondary})`、`HasAnyGameplayTags({Missing,Secondary})` 都返回 true；移除 `Secondary` 后 `OnOwnedTagUpdated` 记录一次 `TagExists=false`，同时 `HasAllGameplayTags({Primary,Secondary})` 变为 false |
| 使用的 Helper | `FAngelscriptTestFixture` + native actor/controller fixture + delegate recorder |
| 优先级 | P1 |

#### NewTest-31：补齐 `AbilitySpec` source-object 变更与 cooldown 查询回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `CanActivateAbilityByClass(...)` / `CanActivateAbilitySpec(...)` / `SetAbilitySpecSourceObject(...)` / `GetAbilitySpecSourceObject(...)` / `GetCooldownTimeRemaining(...)` |
| 现有测试覆盖 | 现有文档仅建议验证 `GiveAbility` 会填充初始 `SourceObject`，但还没有任何用例覆盖后续 mutation、按 class/spec 查询可激活性，以及 cooldown tag 查询逻辑 |
| 风险评估 | 一旦 spec source object 更新不到位，或 cooldown 查询没有按 owning tags 取最大剩余时间，脚本 ability 会出现 UI/判定与真实状态不一致的问题；这类错误通常只有在运行时多能力并存时才暴露 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.SpecSourceObjectAndCooldownQueriesReflectLiveState` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemQueryTests.cpp` |
| 场景描述 | 创建一个 native test ability class，覆写 cooldown tags 为固定 tag（如 `Cooldown.Tests.Primary`）且 `CanActivateAbility(...)` 恒返回 true；把它授予 ASC，记录返回 handle。先断言 `CanActivateAbilityByClass(...)` 和 `CanActivateAbilitySpec(...)` 为 true，再把 spec 的 `SourceObject` 从 `SourceA` 改为 `SourceB`。随后向 ASC 应用一个拥有同一 cooldown tag、持续时间为正值的 test gameplay effect，并调用 `GetCooldownTimeRemaining(...)` |
| 输入/前置 | `FAngelscriptTestFixture`；native ASC fixture；native gameplay ability test double；`SourceA` / `SourceB` 两个 `UObject`；一个带 `Cooldown.Tests.Primary` owning tag 的 duration gameplay effect |
| 期望行为 | 初始授予后 `GetAbilitySpecSourceObject(Handle) == SourceA`，调用 `SetAbilitySpecSourceObject(Handle, SourceB)` 后 getter 返回 `SourceB`；`CanActivateAbilityByClass(TestAbility)` 与 `CanActivateAbilitySpec(Handle)` 在 ability 未阻塞时都为 true；应用 cooldown effect 后 `GetCooldownTimeRemaining(TestAbility) > 0.f`，且若同 tag 下有多条 active effect，则返回值等于最大的剩余时间；对未授予的 ability class 调该函数应返回 `-1.f` |
| 使用的 Helper | `FAngelscriptTestFixture` + native `UGameplayAbility` / `UGameplayEffect` test double |
| 优先级 | P1 |

#### NewTest-32：覆盖 `UAngelscriptAttributeSet` 的 owner delegation、attribute helper 与 replication 回推

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAttributeSet.cpp` |
| 关联函数 | `OnRep_Attribute(...)` / `BP_GetOwningActor()` / `BP_GetOwningAbilitySystemComponent()` / `TrySetAttributeBaseValue(...)` / `TryGetAttributeCurrentValue(...)` / `TryGetAttributeBaseValue(...)` / `CompareGameplayAttributes(...)` |
| 现有测试覆盖 | 文档中已有 `PostInitProperties()` / `GetLifetimeReplicatedProps(...)` / `TryGetGameplayAttribute(...)` 建议，但对 attribute set 实例与 owning ASC 的联动、`OnRep_Attribute`、以及 set-level helper 仍没有任何现有测试或建议 |
| 风险评估 | 如果 attribute set 无法把 helper 调用正确转发给 owning ASC，或 `OnRep_Attribute(...)` 没有把复制值推回 GAS 基础值，脚本读取到的属性会与 replication 后端长期偏离 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AttributeSet.OwnerDelegationAndReplicationStayConsistent` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAttributeSetRuntimeTests.cpp` |
| 场景描述 | 定义一个 native test attribute set，至少包含一个 `UPROPERTY(Replicated)` 的 `FAngelscriptGameplayAttributeData Health`。把它注册到 `UAngelscriptAbilitySystemComponent` 后，先通过 set-level `TrySetAttributeBaseValue("Health", 30.f)` 和 `TryGetAttributeBaseValue/CurrentValue(...)` 验证 owner delegation；再构造一份旧的 `FAngelscriptGameplayAttributeData OldHealth`，把当前 `Health` 值改成 `55.f` 后手动调用 `OnRep_Attribute(OldHealth)` |
| 输入/前置 | `FAngelscriptTestFixture`；native ASC fixture；native replicated attribute set；`Health` 初始值 `10.f`，更新后的 replicated 值 `55.f` |
| 期望行为 | `BP_GetOwningActor()` 返回 ASC owner，`BP_GetOwningAbilitySystemComponent()` 返回同一 ASC；`TrySetAttributeBaseValue("Health", 30.f)` 返回 true，随后 `TryGetAttributeBaseValue("Health", OutBase)` 和 `TryGetAttributeCurrentValue("Health", OutCurrent)` 至少有一条与新的数值同步；`CompareGameplayAttributes(...)` 对同一 `Health` attribute 返回 true、对不同 attribute 返回 false；调用 `OnRep_Attribute(OldHealth)` 后，通过 ASC 再次读取 `Health` base value 应变成 `55.f`，证明复制回推已生效 |
| 使用的 Helper | `FAngelscriptTestFixture` + native replicated attribute-set fixture |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 4 | NoTestForSource: 4 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 17:51)

### 一、现有测试问题

#### Issue-40：`EngineCore` 的 `CreateDestroy` 只检查裸指针 reset，没有覆盖 `FAngelscriptEngine` 的关键生命周期合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.CreateDestroy` |
| 行号范围 | 58-72 |
| 问题描述 | 这条用例是 `EngineCoreTests` 里唯一直接命名为 `CreateDestroy` 的生命周期测试，但它只做了两件事：`CreateForTesting(...)` 返回非空，以及 `Engine.Reset()` 后 `TUniquePtr` 变空。它没有验证 `CreateForTesting()` 产物是否真正完成 `InitializeForTesting()` 所需状态，也没有验证销毁后 `FAngelscriptEngineContextStack`、`FAngelscriptType::GetTypes()`、current engine 解析、script engine 句柄或其他全局副作用是否回到基线。结果是最关键的“创建/销毁合同”被退化成了 C++ 指针层面的 smoke test。 |
| 影响 | 一旦 `FAngelscriptEngine` 在创建时漏做初始化、在销毁时残留 global/current-engine 状态，当前 `CreateDestroy` 仍会稳定绿灯。`EngineCoreTests` 虽然名义上有 6 条生命周期相关用例，但这条最基础的 create/destroy 合同并没有真正保护 RuntimeCore 的生命周期边界。 |
| 修复建议 | 把该用例升级成真正的 lifecycle contract test：创建后至少断言 `Engine->GetScriptEngine()` 有效、可在 `FAngelscriptEngineScope` 下解析成 current engine，必要的类型/包状态已建立；销毁后断言 `FAngelscriptEngineContextStack` 恢复到进入前快照，`TryGetCurrentEngine()` 不再返回已销毁实例，且测试模式下应清掉的全局状态回到稳定基线。若这些断言过多，建议拆成 `CreateInitializesCoreState` 与 `DestroyClearsCurrentEngineState` 两条更聚焦的 `ASTEST_CREATE_ENGINE_FULL()` 用例。 |

#### Issue-41：`EngineParityTests` 里多条 parity 用例只做 compile smoke，没有验证运行结果或绑定语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.CollisionQueryParamsCompile` / `WorldCollisionCompile` / `SoftReferenceCompile` / `UserWidgetPaintCompile` / `RuntimeCurveLinearColorCompile` / `StartupBindRegistrySmoke` |
| 行号范围 | 230-306, 454-510, 534-570, 646-695 |
| 问题描述 | 这组用例大量停留在“模块可编译、函数对象非空”级别。例如 `CollisionQueryParamsCompile`、`WorldCollisionCompile`、`RuntimeCurveLinearColorCompile` 和 `StartupBindRegistrySmoke` 都在 `CompileFunction(...) == asSUCCESS` 后直接返回；`SoftReferenceCompile` 只检查三个函数声明能被取到；`UserWidgetPaintCompile` 甚至只断言 `BuildModule(...)` 返回非空。它们没有执行脚本，也没有验证字段写回、返回类型、helper 绑定路径或 representative API 的真实运行结果，因此更像 codegen surface smoke，而不是 parity contract。 |
| 影响 | 只要绑定层还能勉强通过编译，这些测试就会给绿灯；即便运行时调度错了、返回值类型漂移、绑定落到错误 overload、或某些 API 只能“编译通过但执行出错”，当前 parity 套件也无法报警。用户要求重点审查的 15 条 parity 用例中，这几条属于明显的粗粒度检查。 |
| 修复建议 | 保留 compile smoke 作为最低层保护，但应给每个 family 至少补一条 representative execute/assert 合同：`CollisionQueryParams` 要执行脚本并断言 `Bits` 等于 `ECollisionEnabled::QueryOnly` 对应值；`SoftReferenceCompile` 要实际调用 `Get()`/`EditorOnlyLoadSynchronous()` 的签名并校验返回 type info；`RuntimeCurveLinearColor` 要在执行后验证 key 被加入；`StartupBindRegistrySmoke` 要执行脚本并断言组合返回值，而不是止步于 `Function != nullptr`。如果担心 production engine 污染，先把这些用例迁到 clean clone engine。 |

#### Issue-42：`SubsystemScenarioTests` 只要求“编译失败”，不验证失败原因，导致任何无关错误都能通过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `WorldSubsystem.Tick` / `WorldSubsystem.ActorAccess` / `GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 77-107, 124-149, 166-197, 214-244 |
| 问题描述 | 四个用例都把日志级别临时提到 `Fatal`，然后只断言 `bCompiled == false` 且 `CompileResult == ECompileResult::Error`。它们没有 `AddExpectedError(...)`，也没有检查编译器报错是否真的是“subsystem 脚本生成当前分支不支持”。因此，只要脚本因为任意其他原因失败，例如 parser 回归、基类名拼写变化、无关 bind 缺失或日志路径变化，测试仍会当作“符合预期的 unsupported 行为”而通过。 |
| 影响 | 这组 scenario test 现在只能证明“某处出错了”，却不能证明“失败的正是 subsystem unsupported 这条合同”。一旦编译器或 binding 出现与 subsystem 无关的回归，自动化会误报成稳定绿灯，直接掩盖真正的退化点。 |
| 修复建议 | 给每条用例补精确的错误合同：用 `AddExpectedError(...)` 或测试 helper 捕获包含 `UScriptWorldSubsystem` / `UScriptGameInstanceSubsystem` 不支持信息的诊断文本，并断言命中次数；若编译器提供结构化错误码，优先断言具体错误类别，而不是泛化成 `ECompileResult::Error`。同时保留现有 `bCompiled == false` 断言，形成“失败且因为正确原因失败”的双重保护。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-40 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 17:45)

### 本轮汇总（承接上方 `Issue-39` / `NewTest-26` / `NewTest-27` / `NewTest-28`）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-39` |
| 新增测试建议 | `NewTest-26` / `NewTest-27` / `NewTest-28` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-39 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 2 | MissingScenario: 1, NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 17:20)

### 一、现有测试问题

#### Issue-39：`MacroQualifiedDirectBindings` 只检查生成源码里的 erase macro 文本，没有验证运行时 entry 真的落在 direct path

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.MacroQualifiedDirectBindings` |
| 行号范围 | 752-776 |
| 问题描述 | 这条用例只调用 `FindGeneratedBindingLine(...)` 在 `AS_FunctionTable_*.cpp` 里搜 `RunBehaviorTree` 和 `ReportPerceptionEvent` 两行，然后断言字符串里包含 `ERASE_AUTO_METHOD_PTR` / `ERASE_METHOD_PTR` 或 `ERASE_AUTO_FUNCTION_PTR` / `ERASE_FUNCTION_PTR`，并且不包含 `ERASE_NO_FUNCTION()`。它从头到尾都没有检查 `FAngelscriptBinds::GetClassFuncMaps()` 里的最终 `FFuncEntry`，也没有确认生成产物真的被编进运行时并注册成 direct entry。换句话说，只要磁盘上的 UHT 文本还长得像 direct macro，这条测试就会通过，即便最终运行时条目已经因为集成、编译或注册阶段的问题退化成 fallback / unresolved。 |
| 影响 | 这条测试把“生成器写出了正确宏文本”和“运行时真的拥有 direct binding”混为一谈。若 codegen 文本正确但生成 cpp 没有被正确消费、`ClassFuncMaps` 注册丢失，或最终 entry 仍走 reflective fallback，当前自动化不会报警；开发者会看到 artifact 绿灯，却在真实运行时拿到错误绑定路径。 |
| 修复建议 | 保留当前生成文本 smoke，但把运行时交叉验证补成正式合同：对 `RunBehaviorTree`、`ReportPerceptionEvent` 各自定位到对应 `UClass` 的 `FFuncEntry`，断言 entry 存在、`FuncPtr.IsBound()` 与 `Caller.IsBound()` 都为 true，且 `bReflectiveFallbackBound == false`；必要时再编译一个最小脚本实际调用这两个 API，证明最终走的是 direct path 而不是只在磁盘 artifact 上看起来像 direct。 |

### 二、需要新增的测试

#### NewTest-26：补齐 `FindAllScriptFilenames()` 在 editor-scripts enabled 时保留 `Examples` / `Dev` / `Editor` 目录的分支

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::FindAllScriptFilenames(...)` / `FAngelscriptEngine::FindScriptFiles(...)` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 的 `Discovery` / `SkipRules` 只覆盖“给定 root 后能枚举文件”和 `bUseEditorScripts == false` 时过滤 `Examples` / `Dev` / `Editor`；完全没有命中 `ShouldUseEditorScripts()==true` 时三类目录应被保留的分支 |
| 风险评估 | 如果 editor/dev/example 脚本在 editor 环境里被错误过滤，当前自动化不会报警；结果会表现成编辑器下脚本缺失、开发脚本和 editor-only 工具脚本静默不参与编译，问题只能在人工使用时暴露 |
| 建议测试名 | `Angelscript.TestModule.FileSystem.SkipRules.EditorScriptsEnabledKeepsExamplesDevAndEditor` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemDiscoveryTests.cpp` |
| 场景描述 | 在独立临时 root 下写入 `Gameplay/Main.as`、`Examples/ExampleOnly.as`、`Dev/DevOnly.as`、`Editor/EditorOnly.as` 四个脚本；把 `Engine.bUseEditorScripts` 置为 `true`，`AllRootPaths` 临时替换成该 root，然后调用 `FindAllScriptFilenames(...)` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；复用或抽取现有 `WriteFileSystemTestFile(...)`/临时 root helper；用 `TGuardValue<bool>` 守护 `bUseEditorScripts`，用 `ON_SCOPE_EXIT` 恢复 `AllRootPaths` 并清理磁盘目录 |
| 期望行为 | `Files.Num()` 必须等于 `4`；返回的 `RelativePath` 集合必须同时包含 `Gameplay/Main.as`、`Examples/ExampleOnly.as`、`Dev/DevOnly.as`、`Editor/EditorOnly.as`；不允许因为 skip-rule 逻辑把后三类目录误删 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + 文件系统临时 root helper + `TGuardValue<bool>` + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

#### NewTest-27：给 `UnversionedPropertySerialization` 建立 round-trip 与 schema-cache reset 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/UnversionedPropertySerialization.cpp` / `UnversionedPropertySerialization.h` |
| 关联函数 | `SerializeUnversionedProperties(...)` / `DestroyAngelscriptUnversionedSchema(...)` / `GetSchemaHash(...)` |
| 现有测试覆盖 | 本轮目标测试目录 `Core/`、`Subsystem/`、`GC/`、`FileSystem/` 完全无覆盖；源码虽然自带 `UnversionedPropertySerializationTest.cpp` 的内部 harness，但它不是当前自动化套件中的正式 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 合同 |
| 风险评估 | 一旦 unversioned round-trip、zero/default 分支、或 schema cache reset 回归，生成类/结构体的序列化结果会在运行时静默漂移；当前 RuntimeCore 自动化没有任何定位点，class-generator teardown 后的 stale schema 也不会被发现 |
| 建议测试名 | `Angelscript.TestModule.Engine.UnversionedPropertySerialization.RoundTripsAndRebuildsSchemaCache` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUnversionedPropertySerializationTests.cpp` |
| 场景描述 | 在测试文件中定义一个小型 native `USTRUCT`，覆盖 `int32`、`bool`、`FName`、`TArray<int32>` 等字段；先用 versioned 路径和 `SerializeUnversionedProperties(...)` 各做一次 save/load round-trip，再显式调用 `DestroyAngelscriptUnversionedSchema(TestStruct)` 清缓存后重跑 unversioned round-trip |
| 输入/前置 | 复用并精简 `Core/UnversionedPropertySerializationTest.cpp` 里的本地 save/load harness，抽一个只做单 struct round-trip 的 helper 到测试文件或 `Shared/`；准备一份非默认值实例和一份 defaults 实例 |
| 期望行为 | versioned 与 unversioned 两条路径 load 回来的实例字段必须逐项等于原始实例；`DestroyAngelscriptUnversionedSchema(...)` 后再次执行 unversioned round-trip 仍然得到相同结果；若在 `WITH_EDITORONLY_DATA` 下可用，还应断言 `GetSchemaHash(TestStruct, false)` 在清缓存前后保持一致 |
| 使用的 Helper | 其他（从 `UnversionedPropertySerializationTest.cpp` 提炼的最小 round-trip harness） |
| 优先级 | P1 |

#### NewTest-28：给 `AngelscriptSort::QuickSort` 补齐空数组、重复值和降序排序回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSort.h` |
| 关联函数 | `AngelscriptSort::QuickSort(...)` / `Partition(...)` |
| 现有测试覆盖 | 本轮目标测试集完全无覆盖；仓库内也没有任何自动化直接命中 `AngelscriptSort::QuickSort`，而它已被 `Bind_TArray.cpp` 的脚本数组排序路径直接使用 |
| 风险评估 | 一旦 partition 边界、空数组处理、重复值比较或降序 comparator 回归，脚本侧 `TArray::Sort` 会出现顺序错误甚至越界风险，但当前 RuntimeCore 自动化不会给出独立定位 |
| 建议测试名 | `Angelscript.TestModule.Engine.Sort.QuickSortHandlesDuplicatesAndDescendingOrder` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSortTests.cpp` |
| 场景描述 | 在纯 native 测试里定义一个简单 POD 数组和 comparator context，分别覆盖长度 `0`、长度 `1`、已排序输入、包含重复值的乱序输入，以及 descending 排序；直接调用 `AngelscriptSort::QuickSort<...>(...)` |
| 输入/前置 | 一个本地 `struct FSortValue { int32 Key; int32 Tag; };`，一个 comparator userdata 用于控制升序/降序，外加 `CheckSortedKeys(...)` helper |
| 期望行为 | 空数组和单元素数组调用后保持不变且不崩溃；升序 case 的 `Key` 序列必须单调不降；降序 case 必须单调不升；重复值 case 中元素总数与 key 计数保持不变，证明 `Swap/Partition` 没有丢元素或越界覆盖 |
| 使用的 Helper | 其他（纯 native comparator + `CheckSortedKeys` helper，无需 `FAngelscriptEngine`） |
| 优先级 | P2 |

---

## 测试审查 (2026-04-08 16:56)

### 二、需要新增的测试

#### NewTest-24：覆盖 `UAngelscriptComponent::ProcessEvent()` 的 RPC validate 成功/失败分流

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptComponent.cpp` |
| 关联函数 | `UAngelscriptComponent::ProcessEvent(UFunction*, void*)` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp` 只覆盖 `BeginPlay` / `Tick` / `EndPlay` / owner 读取；当前 RuntimeCore 目标测试集以及全仓库现有测试都没有触发 `FUNC_NetValidate` 分支、`GetRuntimeValidateFunction()` 调度和 `RPC_ValidateFailed(...)` 路径 |
| 风险评估 | 一旦 `ProcessEvent()` 参数拷贝错位、validate 返回值被忽略、或失败时仍继续执行 RPC body，脚本 component 的 replicated RPC 会表现成“表面能注册，运行时 validation 失效或 body 被错误执行”，当前自动化没有任何定位点 |
| 建议测试名 | `Angelscript.TestModule.Engine.Component.ProcessEvent.WithValidationRoutesValidateBeforeRpcBody` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptComponentProcessEventTests.cpp` |
| 场景描述 | 编译一个 `UAngelscriptComponent` 脚本子类，声明一个 `UFUNCTION(Server, Reliable, WithValidation)` 的 RPC，例如 `Server_RecordValue(int Value)`，并在脚本里通过 `_Validate` 或等价 validate 入口把 `Value >= 0` 视为通过、`Value < 0` 视为失败；组件同时暴露 `ValidateCalls`、`BodyCalls`、`LastAcceptedValue` 三个 `UPROPERTY` 计数器。测试中把组件挂到 host actor 上，分别用正值和负值两次 `ProcessEvent()` 调同一个 generated `UFunction` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；`FActorTestSpawner`；一个共享 helper `CreateComponentScenarioScriptComponent(...)`（可从 `Component/AngelscriptComponentScenarioTests.cpp` 提炼到 `Shared/`）；必要时 `AddExpectedError` 捕获一次 `RPC_ValidateFailed` 日志 |
| 期望行为 | 正值调用后 `ValidateCalls == 1`、`BodyCalls == 1`、`LastAcceptedValue` 等于输入值；负值调用后 `ValidateCalls == 2`，但 `BodyCalls` 仍保持 `1`、`LastAcceptedValue` 不变；如果 runtime 会记录 validation failure，日志命中次数应为 1。这样才能证明 `ProcessEvent()` 真正先跑 validate，再决定是否转发到 `RuntimeCallEvent` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `FActorTestSpawner` + `CompileScriptModule` + 共享 `CreateComponentScenarioScriptComponent(...)` |
| 优先级 | P1 |

#### NewTest-25：给 `TAngelscriptSharedPtr` 补齐 copy/assign/self-assign 的引用计数合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSharedPtr.h` |
| 关联函数 | `TAngelscriptSharedPtr<T>::TAngelscriptSharedPtr(T*)` / copy ctor / `operator=(T*)` / `operator=(const TAngelscriptSharedPtr<T>&)` / `Release()` / destructor |
| 现有测试覆盖 | 完全无测试；仓库内没有任何用例直接保护这个手写 AddRef/Release wrapper 的构造、复制、重赋值和析构行为 |
| 风险评估 | 该模板当前用于把 `asIScriptFunction` 保活到 console-command lambda 等异步路径；如果 copy/self-assign 处理错了，很容易引入双重 `Release()`、引用泄漏或悬空函数指针，而这些问题通常只会在控制台命令或延迟回调里以偶发崩溃出现 |
| 建议测试名 | `Angelscript.TestModule.Engine.SharedPtr.CopyAndReleaseBalanceReferenceCounts` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSharedPtrTests.cpp` |
| 场景描述 | 在测试文件里定义一个最小 fake ref-counted type，提供 `AddRef()` / `Release()` / `RefCount` / `ReleaseCalls` 计数。分别覆盖 1. 从裸指针构造；2. copy construct；3. 从另一个 wrapper copy assign；4. 赋成新的裸指针释放旧对象；5. self-assign；6. 手动 `Release()` 后析构 |
| 输入/前置 | 无需 engine；一个文件内 helper `MakeFakeRefCounted()` 和 `CheckRefState(...)` 即可 |
| 期望行为 | 裸指针构造会执行一次 `AddRef()`；copy construct 和 copy assign 会再增加一次引用；把 wrapper 改指向新对象时旧对象 `Release()` 恰好执行一次；self-assign 不会导致额外 `Release()` 或把指针清空；显式 `Release()` 后 `IsValid()==false` 且随后的析构不会再次释放同一对象 |
| 使用的 Helper | 其他（纯 native unit helper，无需 `FAngelscriptEngine`） |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 16:46)

### 一、现有测试问题

#### Issue-38：`SubsystemScenarioTests` 对失败编译后的 module/class 残留完全没有负向断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `WorldSubsystem.Tick` / `WorldSubsystem.ActorAccess` / `GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 66-245 |
| 问题描述 | 四个用例在 `CompileModuleWithResult(...)` 返回失败后就直接结束，只检查 `bCompiled == false` 和 `CompileResult == Error`。它们没有验证 `Engine.GetModule(ModuleName)` 是否仍为空，也没有检查失败脚本里声明的 `UScenarioWorldLifecycleTracker`、`UScenarioWorldTicker`、`UScenarioWorldActorWatcher`、`UScenarioGameInstanceLifecycleTracker` 等 generated class 是否根本没有注册。若 subsystem 编译失败路径错误地留下半注册 `FAngelscriptModuleDesc`、旧类对象或脏反射条目，当前测试仍会绿灯。 |
| 影响 | 这组用例现在只能证明“编译失败了”，不能证明“失败回滚是干净的”。一旦 subsystem 不支持路径开始污染 module lookup、class lookup 或后续重编译环境，问题会在别的测试里以串测形式出现，而当前最接近的失败场景测试不会给出定位。 |
| 修复建议 | 在失败断言之后立即补负向检查：`TestFalse(..., Engine.GetModule(*ModuleName.ToString()).IsValid())`，并对脚本里声明的代表类调用 `FindGeneratedClass(&Engine, TEXT(\"...\"))` 或 `FindObject<UClass>` 断言为空；如担心名字冲突，可在每个用例里用 GUID 后缀生成唯一类名，再把“失败后无 module / 无 generated class / 无可执行入口”建成正式合同。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-38 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:12)

### 本轮汇总（承接上方 `NewTest-29` / `NewTest-30` / `NewTest-31` / `NewTest-32`）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | 无 |
| 新增测试建议 | `NewTest-29` / `NewTest-30` / `NewTest-31` / `NewTest-32` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 4 | NoTestForSource: 4 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:23)

### 本轮汇总（本轮详细新增条目已记录于上方 `2026-04-08 18:21` 区块；此处仅补末尾索引，不重复描述）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-43` / `Issue-44` |
| 新增测试建议 | `NewTest-33` / `NewTest-34` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-44 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-43 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 18:36)

### 本轮汇总（补末尾索引；本轮详细新增条目已记录于上方 `2026-04-08 18:35` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-45` / `Issue-46` / `Issue-47` / `Issue-48` |
| 新增测试建议 | `NewTest-35` / `NewTest-36` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-46 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-45 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-08 19:21)

### 本轮汇总（补真正文件尾部索引；本轮详细新增条目已记录于上方 `2026-04-08 19:17` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | 无 |
| 新增测试建议 | `NewTest-38` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:36)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 19:34` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-51` / `Issue-52` |
| 新增测试建议 | `NewTest-39` / `NewTest-40` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-51 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:54)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 19:48` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-53` / `Issue-54` / `Issue-55` / `Issue-56` |
| 新增测试建议 | `NewTest-41` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-54 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-55 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 1 | NoTestForSource: 1 |
---

## 测试审查 (2026-04-08 20:11)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 20:04` / `2026-04-08 20:05` / `2026-04-08 20:08` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-57` |
| 新增测试建议 | `NewTest-42` / `NewTest-43` / `NewTest-44` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 1 | Issue-57 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 2 | NoTestForSource: 2 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:22)

### 一、现有测试问题

#### Issue-58：`EditorOutputsUseWithEditorGuard` 把具体 shard 文件名 `_000.cpp` 写死，generator 重新分片时会误报失败

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.EditorOutputsUseWithEditorGuard` |
| 行号范围 | 244-260 |
| 问题描述 | 这条用例除了把 UHT 输出目录写死外，还进一步把 sample 文件写成 `AS_FunctionTable_UMGEditor_000.cpp` 和 `AS_FunctionTable_Engine_000.cpp`。但 shard 序号 `000` 只是当前 generator 的分片结果，不是正式合同；一旦某个模块前面新增/删除 exported functions、shard 阈值调整、或同模块输出被重新切分，首个 shard 文件名、是否仍存在 `000`、甚至目标函数是否还落在这份 shard 里都可能变化。此时即便 editor/runtime guard 逻辑完全正确，测试也会因为 sample 文件名变化直接转红。 |
| 影响 | generated function table 正常 reshard 会被误判成 guard 回归，制造与功能无关的假红灯；同时测试只覆盖当前恰好命中的两个 shard，无法稳定表达“任一 editor-only shard 应带 `WITH_EDITOR`，runtime shard 不应带顶层 guard”这一真正合同。 |
| 修复建议 | 不要锁死具体 shard 文件名。改为在生成目录内按前缀搜索 `AS_FunctionTable_UMGEditor_*.cpp` 和 `AS_FunctionTable_Engine_*.cpp`，至少各取一份实际存在的 shard 做断言；更稳妥的做法是遍历所有 editor-only shards 验证都以 `#if WITH_EDITOR` 包裹，并遍历一组 runtime shards 验证不带顶层 guard。这样测试跟随 generator reshard 仍保持稳定。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 1 | Issue-58 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:31)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 20:27` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | 无 |
| 新增测试建议 | `NewTest-45` / `NewTest-46` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:48)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 20:38` / `2026-04-08 20:42` / `2026-04-08 20:46` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-59` / `Issue-60` |
| 新增测试建议 | `NewTest-47` / `NewTest-48` / `NewTest-49` / `NewTest-50` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 2 | Issue-59 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 3 | MissingErrorPath: 1, MissingScenario: 2 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:11:37)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-08 23:57` / `2026-04-09 00:02` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-62` |
| 新增测试建议 | `NewTest-52` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-62 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 00:28:55)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 00:24:47` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-63` / `Issue-64` |
| 新增测试建议 | `NewTest-53` / `NewTest-54` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-64 |
| BadIsolation | 1 | Issue-63 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 00:48:01)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 00:43:19` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-65` / `Issue-66` |
| 新增测试建议 | `NewTest-55` / `NewTest-56` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-65 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 01:08:40)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 00:56:13` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-67` |
| 新增测试建议 | `NewTest-57` / `NewTest-58` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-67 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 2 | MissingScenario: 1, MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 01:30:48)

### 本轮汇总（真正文件尾部补录；本轮详细新增条目已记录于上方 `2026-04-09 01:27:33` 区块）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-69` |
| 新增测试建议 | `NewTest-59` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-69 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 01:49:50)

### 二、需要新增的测试

#### NewTest-61：给 `UAngelscriptAbilityTask` 补 waiting flag 与 `AbilitySystemComponent` proxy 的 round-trip 场景

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTask.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTask.cpp` |
| 关联函数 | `UAngelscriptAbilityTask::BP_SetAbilitySystemComponent(UAbilitySystemComponent*)` / `BP_GetAbilitySystemComponent()` / `BP_SetWaitingOnRemotePlayerData()` / `BP_ClearWaitingOnRemotePlayerData()` / `BP_SetWaitingOnAvatar()` / `BP_ClearWaitingOnAvatar()` / `IsWaitingOnRemotePlayerdata()` / `IsWaitingOnAvatar()` |
| 现有测试覆盖 | 当前文档里的 `NewTest-08` 只覆盖 `CreateAbilityTask(...)` / `CreateAbilityTaskAndRunIt(...)` 生命周期，以及 `SetIsTickingTask` / `SetIsPausable` / `SetIsSimulatedTask` 的基本 getter round-trip；目标测试目录源码检索也没有任何现有用例直接命中 waiting flag 或 `BP_GetAbilitySystemComponent()` 这组公开 proxy。 |
| 风险评估 | 这些 API 是脚本侧直接可见的 `UAngelscriptAbilityTask` 合同；一旦 waiting 状态不再透传到 `UAbilityTask` 基类，或者 `BP_SetAbilitySystemComponent(...)` / `BP_GetAbilitySystemComponent()` 不再保持同一个 ASC，脚本 task 会表现成“看起来创建成功，但网络等待状态错误、delegate 广播条件异常或使用了错误 ASC”，当前自动化没有定位点。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilityTask.WaitingFlagsAndAbilitySystemProxyRoundTrip` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilityTaskTests.cpp` |
| 场景描述 | 基于已有 GAS fixture 创建 source ASC、secondary ASC、native `UGameplayAbility` 实例和一个普通 `UAngelscriptAbilityTask`。先调用 `BP_SetAbilitySystemComponent(SecondaryAsc)` 并验证 `BP_GetAbilitySystemComponent()` 返回同一指针。然后分别走 remote-player-data 与 avatar 两组 waiting flag：初始应为 false；调用 `BP_SetWaitingOnRemotePlayerData()` 后，`BP_IsWaitingOnRemotePlayerdata()` 与虚函数 `IsWaitingOnRemotePlayerdata()` 都变为 true；`BP_ClearWaitingOnRemotePlayerData()` 后恢复 false。avatar 标志重复同样流程，并额外断言清理 remote flag 不会意外影响 avatar flag，反之亦然。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或同级 GAS fixture；一个可完成 actor info 初始化的 source ASC/ability；一个额外创建的 `UAbilitySystemComponent` 作为 secondary ASC；必要时加一个轻量 access shim 暴露 `IsWaitingOnRemotePlayerdata()` / `IsWaitingOnAvatar()` 以便 native 断言。 |
| 期望行为 | `BP_SetAbilitySystemComponent(SecondaryAsc)` 后 `BP_GetAbilitySystemComponent() == SecondaryAsc`；两组 waiting flag 在初始状态均为 false，`Set` 后仅对应 flag 变 true，`Clear` 后恢复 false；`BP_IsWaitingOnRemotePlayerdata()` 与 `IsWaitingOnRemotePlayerdata()`、`BP_IsWaitingOnAvatar()` 与 `IsWaitingOnAvatar()` 的结果始终一致；remote/avatar 两个状态位互不串扰。 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS ASC/ability fixture helper + 必要的 native access shim |
| 优先级 | P2 |

### 本轮汇总（真正文件尾部补录；承接上方 `2026-04-09 01:42:03` 区块并补充本节 `NewTest-61`）

**本轮新增条目索引**

| 类别 | 编号 |
|------|------|
| 现有测试问题 | `Issue-70` |
| 新增测试建议 | `NewTest-60` / `NewTest-61` |

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-70 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 2 | MissingErrorPath: 1, MissingScenario: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 02:07:46)

### 一、现有测试问题

#### Issue-71：`SubsystemScenarioTests` 保留整套 scenario fixture 死代码，却没有任何一条测试真正进入 scenario 执行阶段

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `WorldSubsystem.Tick` / `WorldSubsystem.ActorAccess` / `GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 1-15, 22-43 |
| 问题描述 | 文件头部引入了 `ActorTestSpawner`、`Engine/GameInstance`、`Engine/World`、`TestGameInstance` 和 `UAngelscriptNativeScriptTestObject`，并额外定义了 `SubsystemScenarioDeltaTime`、`InitializeSubsystemScenarioSpawner(...)`、`GetScenarioNativeRecorder(...)` 三个 scenario helper；但四个用例从头到尾都没有创建 `FActorTestSpawner`、没有初始化 game subsystem、没有读取 native recorder，也没有推进 world/game-instance 生命周期。也就是说，这些 fixture 代码完全处于未使用状态，文件真实行为只是“编译失败 smoke”，却保留了一整套看似会执行 runtime scenario 的脚手架。 |
| 影响 | 这会把当前覆盖面伪装成“已有 scenario 基础，只差少量断言”，实际却没有任何运行期驱动，容易误导后续维护者继续在同一文件堆叠编译失败 case。死 helper 还会增加阅读成本，让人误判 subsystem 生命周期已经有 world/game-instance 级别的测试夹具。 |
| 修复建议 | 如果当前文件继续只承担 compile-failure 合同，就删除未使用的 include 和 helper，把文件收敛成明确的 compile-failure smoke；若计划承接真实 runtime scenario，则应把这些 helper 挪到独立的 `...SubsystemRuntimeTests.cpp` 中，并在该文件里实际使用 `FActorTestSpawner`、native recorder 和 tick/world fixture。不要让“未使用的 scenario 脚手架”长期留在 compile-failure 测试里。 |

### 二、需要新增的测试

#### NewTest-62：给 `ModAttributeUnsafe(...)` 补齐“值已更新但 attribute callback 不广播”的危险路径合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `UAngelscriptAbilitySystemComponent::ModAttributeUnsafe(FGameplayAttribute, EGameplayModOp::Type, float)` |
| 现有测试覆盖 | 当前文档里关于 `UAngelscriptAbilitySystemComponent` 的建议已经覆盖了 safe setter、attribute-change callback、tag/ability/query 路径，但没有任何一条直接命中 `ModAttributeUnsafe(...)` 这个脚本可见 wrapper；现有建议只把 `ApplyModToAttributeUnsafe(...)` 当成触发 callback 的驱动手段之一，没有把“不走完整 callback 链”的合同建成测试。 |
| 风险评估 | 这是一个带明确危险注释的公开 API。如果它不再调用 unsafe path、反而错误地广播 attribute callback，或根本没有更新属性值，脚本侧用于 clamp / internal correction 的逻辑会出现隐蔽回归，而当前自动化无法区分“safe 修改”与“unsafe 修改”是否仍然遵守各自合同。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.ModAttributeUnsafeUpdatesValueWithoutBroadcastingCallbacks` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemCallbackTests.cpp` |
| 场景描述 | 创建最小 GAS fixture：带 `UAngelscriptAbilitySystemComponent` 的 owner actor、一个包含 `Health` 属性的 test `UAngelscriptAttributeSet`，以及两个 recorder：1 个通过 `RegisterAttributeChangedCallback(...)` 监听 `FAngelscriptAttributeChangedData`，1 个通过 deprecated `RegisterCallbackForAttribute(...)` 监听 `OnAttributeChanged` multicast。先把 `Health` 设为 `10.f` 作为基线，然后调用 `ModAttributeUnsafe(HealthAttribute, EGameplayModOp::Additive, 5.f)`；最后再用一次 `SetAttributeBaseValue(...)` 做 control，证明 safe path 仍会广播。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或现有 GAS ASC fixture；一个最小 `UAutomationAttributeSet`（含 `Health`）；`UAutomationAttributeChangeRecorder` 和 deprecated trampoline recorder；`FGameplayAttribute` 通过 `UAngelscriptAttributeSet::GetGameplayAttribute(...)` 获取。 |
| 期望行为 | 调用 `ModAttributeUnsafe(...)` 后，`GetAttributeCurrentValueChecked(...)` 或等价读取应变为 `15.f`；unsafe 调用阶段两个 recorder 的计数都保持 `0`，证明没有错误触发高层 callback；随后 control 阶段调用 `SetAttributeBaseValue(...)` 时，至少会触发一次预期 callback，证明测试不是因为 fixture 失效才“全程无广播”。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 现有 GAS ASC/attribute-set fixture + attribute-change recorder helper |
| 优先级 | P1 |

#### NewTest-63：覆盖 `GetAttributeCurrentValue` / `GetAttributeBaseValue` 在缺失 attribute set 时回落到调用方默认值

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.cpp` |
| 关联函数 | `UAngelscriptAbilitySystemComponent::GetAttributeCurrentValue(...) const` / `GetAttributeBaseValue(...) const` |
| 现有测试覆盖 | 已记录建议主要覆盖 `TrySetAttributeBaseValue(...)`、`TryGetAttributeCurrentValue(...)`、`TryGetAttributeBaseValue(...)` 的正向 owner-delegation 场景，以及 callback/query/ability 路径；没有任何一条专门验证 non-checked getter 在 attribute set 缺失时是否按注释返回调用方提供的 `DefaultValue`。 |
| 风险评估 | 这两个 fallback getter 是脚本侧最容易被直接调用的容错入口。如果缺失 attribute set 时返回值不再采用 `DefaultValue`，而是泄漏旧值、0 值或触发额外错误路径，脚本逻辑会在“可选属性未装配”的场景里静默跑错，且现有自动化不会给出明确定位。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilitySystem.AttributeValueFallbackUsesProvidedDefaultsWhenSetMissing` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilitySystemQueryTests.cpp` |
| 场景描述 | 创建一个最小 `UAngelscriptAbilitySystemComponent` fixture，但故意不注册目标 `UAngelscriptAttributeSet`。随后对同一个 `Health` 名称分别调用 `GetAttributeCurrentValue(TestSetClass, TEXT("Health"), 123.f)` 与 `GetAttributeBaseValue(TestSetClass, TEXT("Health"), 456.f)`；同一用例里再调用 `TryGetAttributeCurrentValue(...)` / `TryGetAttributeBaseValue(...)` 作为 control，观察缺失 set 时的返回布尔和值是否保持调用前哨兵。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 或等价 ASC fixture；一个含 `Health` 属性声明但本用例不注册到 ASC 的 test `UAngelscriptAttributeSet` class；哨兵默认值 `123.f` / `456.f` / `789.f`。 |
| 期望行为 | 在目标 attribute set 未注册时，`GetAttributeCurrentValue(..., 123.f)` 必须返回 `123.f`，`GetAttributeBaseValue(..., 456.f)` 必须返回 `456.f`；`TryGetAttributeCurrentValue(...)` 与 `TryGetAttributeBaseValue(...)` 都返回 `false`，且输出参数保持调用前的哨兵值不变；整个过程不应向 ASC 注入新的 spawned attribute set，也不应改变后续注册真实 set 的状态。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 现有 GAS ASC fixture + 最小 test attribute-set class |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-71 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 02:23:25)

### 一、现有测试问题

#### Issue-72：`RenameUpdatesModuleLookup` 先手动 `DiscardModule`，实际绕开了“同模块改名后原地 remap”的核心路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 测试名 | `Angelscript.TestModule.FileSystem.RenameUpdatesModuleLookup` |
| 行号范围 | 323-362 |
| 问题描述 | 用例先以 `OldPatrol.as` 编译 `Game.AI.Patrol`，随后在 345 行直接 `Engine.DiscardModule(TEXT("Game.AI.Patrol"))`，再用 `NewPatrol.as` 做一次全新编译。这样测试覆盖到的其实是“discard 旧模块后重新编译一个新模块”，而不是“同一个 module 在 filename 改名后，runtime 的 filename->module 映射会不会被原地更新”。即便 rename/remap 逻辑本身坏了，只要 `DiscardModule(...)` 能把旧索引清掉、后续 fresh compile 能注册新索引，这条测试仍然会通过。 |
| 影响 | 当前用例名声称验证 rename 更新 lookup，但真正最容易回归的“模块未先 discard、直接因文件改名而重绑 filename 映射”路径处于裸奔状态。文件系统重载若把旧路径残留在 `ModuleDesc` 或 lookup 索引里，现有测试不会报警。 |
| 修复建议 | 不要在 rename 场景前手动 `DiscardModule`。改成保留已加载模块，先把磁盘文件从 `OldPatrol.as` 改到 `NewPatrol.as`，再直接对同一 `ModuleName` 调用 compile/reload；之后同时断言 1. `GetModuleByFilename(NewAbsolutePath)` 命中新模块，2. `GetModuleByFilename(OldAbsolutePath)` 失效，3. `GetModuleByFilenameOrModuleName(NewAbsolutePath, TEXT("Game.AI.Patrol"))` 与 module-name lookup 返回同一 `ModuleDesc`，4. `Code[0].AbsoluteFilename` 已切到新路径。若现有 helper 不支持这条路径，应新增一个专门的 rename-remap 测试 helper，而不是用 `DiscardModule` 把场景提前简化掉。 |

#### Issue-73：`SkippedReasonSummaryCsvOutput` 只校验汇总总和，没验证每个 `FailureReason` 的分桶是否正确

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.SkippedReasonSummaryCsvOutput` |
| 行号范围 | 708-749 |
| 问题描述 | 这条用例读取 `AS_FunctionTable_SkippedEntries.csv` 和 `AS_FunctionTable_SkippedReasonSummary.csv` 后，只做了三类检查：header 正确、每行 reason 非空、所有 `SkippedCount` 求和后等于明细行数（730-748 行）。它从头到尾都没有把 summary 里的 `FailureReason -> Count` 与明细 csv 按 reason 分桶做逐项比对。结果是：即便生成器把多个 reason 错误合并成一个桶、把 count 记到错误 reason 上，或者 reason 文本被统一改坏，只要总和仍然相等，这条测试就会继续绿灯。 |
| 影响 | `SkippedReasonSummaryCsvOutput` 现在保护的只是“总共有多少条 skipped entry”，而不是“为什么被 skipped 的统计是否准确”。一旦报表消费者依赖 per-reason 聚合诊断热点，当前自动化无法发现 reason 归类漂移，只会在人工查看 csv 时才暴露。 |
| 修复建议 | 在解析 csv 时把两份产物都收敛成 `TMap<FString, int32>`：一边遍历 `SkippedEntries.csv` 按 `FailureReason` 计数，另一边读取 `SkippedReasonSummary.csv` 的聚合值，然后对 key 集合与每个 count 做精确相等断言。若同时修复 `Issue-55` 的 parser 问题，应先换成支持 quoting 的 CSV reader，再把“按桶精确对账”作为主断言，而不是只比较总和。 |

### 二、需要新增的测试

#### NewTest-64：覆盖同一 `ModuleName` 在文件改名后无需手动 `DiscardModule` 的 filename remap 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::GetModuleByFilename(...)` / `GetModuleByFilenameOrModuleName(...)` / 触发重编译后更新 `FAngelscriptModuleDesc::Code[].AbsoluteFilename` 的 compile/hot-reload 路径 |
| 现有测试覆盖 | 现有 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 里的 `RenameUpdatesModuleLookup` 会先 `DiscardModule(TEXT("Game.AI.Patrol"))` 再重新编译，因此只覆盖“删旧模块 + fresh compile”路径，没有任何一条测试直接命中“同一模块改名后原地 remap filename 索引”的真实场景 |
| 风险评估 | 如果 runtime 在文件改名后残留旧 `AbsoluteFilename`、旧 filename lookup 索引或执行的仍是旧源码，当前自动化不会报警。用户会在实际脚本文件 rename、重命名目录或 source-control move 之后遭遇 lookup 混乱和热重载错绑。 |
| 建议测试名 | `Angelscript.TestModule.FileSystem.RenameUpdatesModuleLookup.InPlaceRenameRemapsFilenameWithoutDiscard` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemRenameTests.cpp` |
| 场景描述 | 在 clean shared engine 下先写入 `Game/AI/OldPatrol.as`，内容为 `int PatrolEntry() { return 7; }`，并以 `ModuleName = Game.AI.Patrol` 编译；确认旧路径 lookup 命中且执行结果为 `7`。随后用 `IFileManager::Get().Move(...)` 把文件改名为 `Game/AI/NewPatrol.as`，同时把源码内容更新为 `int PatrolEntry() { return 13; }`，但不调用 `DiscardModule(...)`；直接对同一 `ModuleName` 用新路径再次编译。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；复用现有文件系统临时 root helper；`ON_SCOPE_EXIT` 中统一 `Engine.DiscardModule(TEXT("Game.AI.Patrol"))`、`ResetSharedCloneEngine(Engine)`、清理磁盘目录；必要时用 `ExecuteIntFunction(...)` 验证 recompile 后实际执行结果 |
| 期望行为 | 第二次编译必须成功；`GetModuleByFilename(NewAbsolutePath)` 与 `GetModuleByFilenameOrModuleName(NewAbsolutePath, TEXT("Game.AI.Patrol"))` 都应命中有效模块；`GetModuleByFilename(OldAbsolutePath)` 必须失效；返回的 `ModuleDesc` 至少有一段 `Code`，且 `Code[0].AbsoluteFilename == NewAbsolutePath`；重新执行 `PatrolEntry()` 时结果必须是 `13`，证明 runtime 已真正切到 renamed 文件而不是沿用旧路径/旧代码。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + 文件系统临时 root helper + `ExecuteIntFunction(...)` + `ON_SCOPE_EXIT` cleanup |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-72 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 02:37:58)

### 二、需要新增的测试

#### NewTest-65：覆盖 `FAngelscriptRuntimeModule` 的 fallback tick 门控合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 关联函数 | `FAngelscriptRuntimeModule::TickFallbackPrimaryEngine(float)` |
| 现有测试覆盖 | 当前文档里仅有 `NewTest-22` 覆盖 `InitializeAngelscript()` / `SetInitializeOverrideForTesting(...)` / `ResetInitializeStateForTesting()`；目标测试目录和已记录建议都没有任何一条直接命中 `TickFallbackPrimaryEngine(...)`，也没有验证 `UAngelscriptGameInstanceSubsystem::HasAnyTickOwner()` 对 fallback tick 的门控 |
| 风险评估 | 这是 editor 下没有 `GameInstanceSubsystem` tick owner 时维持 Angelscript runtime 前进的兜底路径。若回归成“有 subsystem owner 仍重复 tick”或“无 owner 时根本不 tick”，会造成脚本热重载、unit-test runner、debug server 等 editor 空闲路径静默失效或双重推进，当前自动化没有定点报警 |
| 建议测试名 | `Angelscript.TestModule.Engine.RuntimeModule.FallbackTickRespectsSubsystemOwnership` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp` |
| 场景描述 | 在测试文件中定义 `FAngelscriptRuntimeModuleTickTestAccess` 与 `FAngelscriptTickBehaviorTestAccess`，利用现有 friend access 调用 private `TickFallbackPrimaryEngine(...)` 并暂时改写 `UAngelscriptGameInstanceSubsystem::ActiveTickOwners`。准备一个 isolated full engine，把它压入 `FAngelscriptEngineContextStack`，并通过 engine access shim 把 `bScriptDevelopmentMode = true`、`bUseHotReloadCheckerThread = true`、`NextHotReloadCheck = -1.0`。第一段把 `ActiveTickOwners` 设为 `0` 后调用 fallback tick；第二段把 `ActiveTickOwners` 设为 `1` 后再次调用同一入口；必要时第三段再把 current engine 切到一个 `ShouldTick()==false` 的 dummy engine 作为 control |
| 输入/前置 | `AngelscriptTestSupport::CreateFullTestEngine()`；`FAngelscriptEngineScope` 或直接 `FAngelscriptEngineContextStack::Push/Pop`；runtime-module private access shim；`UAngelscriptGameInstanceSubsystem` tick-owner access shim；一个 engine access shim 允许读写 `NextHotReloadCheck` / `bUseHotReloadCheckerThread` / `bScriptDevelopmentMode` |
| 期望行为 | `TickFallbackPrimaryEngine(0.016f)` 始终返回 `true`；在 `ActiveTickOwners == 0` 且 engine 可 tick 时，`NextHotReloadCheck` 必须从 `-1.0` 变成大于 `0.0` 的值，证明 fallback 路径实际进入了 `FAngelscriptEngine::Tick(...)`；在 `ActiveTickOwners == 1` 时，同样调用后 `NextHotReloadCheck` 必须保持不变，证明 subsystem owner 成功抑制 fallback tick；若增加 `ShouldTick()==false` control，则 `NextHotReloadCheck` 也必须保持不变 |
| 使用的 Helper | `AngelscriptTestSupport::CreateFullTestEngine()` + `FAngelscriptRuntimeModuleTickTestAccess` + `FAngelscriptTickBehaviorTestAccess` + engine access shim + `ON_SCOPE_EXIT` 恢复 `ContextStack` 与 `ActiveTickOwners` |
| 优先级 | P1 |

#### NewTest-66：覆盖 `FAngelscriptRuntimeModule::ShutdownModule()` 的 owned-engine 与 ticker 清理合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 关联函数 | `FAngelscriptRuntimeModule::ShutdownModule()` |
| 现有测试覆盖 | 已记录的 `NewTest-22` 只覆盖 testing override 初始化与 reset；当前目标测试目录和已记录建议都没有任何一条直接调用 `ShutdownModule()`，因此 `OwnedPrimaryEngine` 出栈、`FallbackTickHandle` 释放与重复 shutdown 的幂等性仍是空白 |
| 风险评估 | 如果 module shutdown 不再移除 core ticker，editor 自动化或长生命周期会话会残留悬空 delegate；如果 `OwnedPrimaryEngine` 没有从 `FAngelscriptEngineContextStack` 弹出，后续 `TryGetCurrentEngine()` / `CreateForTesting(Clone)` 等路径会继承脏 ambient engine，造成跨测试污染且很难定位 |
| 建议测试名 | `Angelscript.TestModule.Engine.RuntimeModule.ShutdownReleasesOwnedPrimaryEngineAndTickerHandle` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp` |
| 场景描述 | 在测试文件里复用 `FAngelscriptRuntimeModuleTickTestAccess` 访问 private static/member 状态。先通过 `ResetInitializeStateForTesting()` 清空基线，并确保 `ContextStack` 为空。然后让 `InitializeAngelscript()` 在“无 current engine、无 testing override”条件下创建 `OwnedPrimaryEngine`；再通过 access shim 给一个局部 `FAngelscriptRuntimeModule Module` 注入真实 `FallbackTickHandle`（例如向 `FTSTicker::GetCoreTicker()` 注册一个 no-op ticker）。第一段直接调用 `Module.ShutdownModule()`；第二段在已 shutdown 状态下再次调用一次作为幂等 control |
| 输入/前置 | `FAngelscriptRuntimeModuleTickTestAccess`；`FAngelscriptEngineContextStack::SnapshotAndClear()` 或等价 scoped guard；`FTSTicker::GetCoreTicker().AddTicker(...)` 生成测试 handle；`ON_SCOPE_EXIT` 确保最终恢复 initialize state 与 context stack |
| 期望行为 | 首次 `ShutdownModule()` 后 `FallbackTickHandle.IsValid() == false`；`OwnedPrimaryEngine == nullptr`；`FAngelscriptEngine::TryGetCurrentEngine()` 返回 `nullptr` 或进入前快照中的原始 engine，而不是刚创建的 owned engine；第二次重复 `ShutdownModule()` 不应崩溃、也不应重新压栈或重新生成 handle，证明 shutdown 路径具备幂等性 |
| 使用的 Helper | `FAngelscriptRuntimeModuleTickTestAccess` + `FTSTicker::GetCoreTicker()` + `FAngelscriptEngineContextStack` scoped guard + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:14:29)

### 一、现有测试问题

#### Issue-74：`SkippedCsvOutput` 只看 `FailureReason`，没有验证被跳过条目的身份列是否完整

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.SkippedCsvOutput` |
| 行号范围 | 683-702 |
| 问题描述 | 这条用例在读完 `AS_FunctionTable_SkippedEntries.csv` 后，只检查 header 正确、行数大于 1、每行切成 4 列，以及 `FailureReason` 非空。它完全没有断言 `ModuleName`、`ClassName`、`FunctionName` 这三列本身是否非空，也没有确认每条 skipped row 仍然能唯一指回某个被跳过的导出项。结果是：只要生成器还会写出一个 reason 字符串，即便前三列因为回归被写成空串或默认值，这条测试仍会绿灯。 |
| 影响 | `SkippedEntries.csv` 的核心价值是给开发者和工具链定位“哪一个 module/class/function 被跳过”。如果身份列被清空而测试没报警，报表就会退化成一堆不可追踪的 reason 文本，排查 generated-function-table 回归的成本会显著上升。 |
| 修复建议 | 在修复 `Issue-55` 的 CSV 解析方式后，逐行补强 identity 断言：`ModuleName`、`ClassName`、`FunctionName` 都必须非空；最好再对 `(ModuleName, ClassName, FunctionName, FailureReason)` 建唯一键，防止同一条 skipped entry 被重复写出。若已有代表性 skipped 函数样本，可再补一条“已知 skipped 行仍能精确定位到具体函数”的定点断言。 |

#### Issue-75：`CsvOutput` 用扫描 generated `.cpp` 的 helper 验证 entry csv，helper 选型与场景不匹配

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.CsvOutput` |
| 行号范围 | 647-650 |
| 问题描述 | 这条 CSV 用例在读取 `AS_FunctionTable_Entries.csv` 之后，先调用 `FindGeneratedBindingLine(GeneratedDirectory, TEXT("\"RunBehaviorTree\""), RunBehaviorTreeCsvLine)`，断言文本却写成“should include RunBehaviorTree in the entry csv”。但 `FindGeneratedBindingLine(...)` 实际扫描的是 `AS_FunctionTable_*.cpp` generated source，不是 `AS_FunctionTable_Entries.csv`。也就是说，这一步用错了 helper 和工件来源；它验证的是 codegen `.cpp` 文本里有没有 `RunBehaviorTree`，不是 entry csv 里有没有这行。 |
| 影响 | 该断言把 CSV 报表测试错误耦合到了 generated `.cpp` 工件上，失败时很难判断问题到底出在 CSV 输出还是 C++ 代码生成；同时它对 CSV 覆盖没有新增价值，因为下面 653-665 行已经再次遍历 `EntryLines` 去找 `RunBehaviorTree`。helper 选错会增加误报噪音，也会掩盖真正的 CSV 合同缺口。 |
| 修复建议 | 删除这段 `FindGeneratedBindingLine(...)` 调用，改为直接在 `EntryLines` 上做基于列名/列值的查找；若修复 `Issue-33` 时引入结构化 CSV parser，可按 `FunctionName == RunBehaviorTree` 精确取行，再断言 `EntryKind == Direct`、`EraseMacro != ERASE_NO_FUNCTION()` 以及对应 `ModuleName` / `ClassName`。如果确实需要 cross-artifact 校验 generated `.cpp` 与 csv 一致性，应单独拆成另一条测试，而不是混在 `CsvOutput` 里。 |

### 二、需要新增的测试

#### NewTest-67：覆盖 `UAngelscriptGameInstanceSubsystem` 的 tickability gate，防止 CDO/未初始化实例误进入 tick

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 关联函数 | `UAngelscriptGameInstanceSubsystem::GetTickableTickType() const` / `IsAllowedToTick() const` / `IsTickableInEditor() const` / `IsTickableWhenPaused() const` |
| 现有测试覆盖 | 当前目标目录 `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/` 只有 4 条 compile-fail smoke；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` 也只在白盒注入的正向路径里断言过一次 `Subsystem->IsAllowedToTick()`，没有任何用例保护 CDO/template、`bInitialized == false`、`PrimaryEngine == nullptr` 这几条 gate，也没有检查 editor/paused tick policy |
| 风险评估 | 如果 `GetTickableTickType()` 不再把 template/CDO 压成 `Never`，或 `IsAllowedToTick()` 在未初始化/无 engine 时仍返回 `true`，UE ticker 可能会让模板对象、半初始化 subsystem 或已释放 engine 的实例继续参与 tick，造成 editor 下双重推进、悬空 `PrimaryEngine` 访问或极难复现的顺序依赖 |
| 建议测试名 | `Angelscript.TestModule.GameInstanceSubsystem.TickPolicy.GatesTemplateAndInitializationState` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptGameInstanceSubsystemRuntimeTests.cpp` |
| 场景描述 | 在新文件里引入一个本地 access shim，复用或移植 `AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` 中的 `FAngelscriptTickBehaviorTestAccess` 思路，用来临时改写 `bInitialized`、`PrimaryEngine` 与 `ActiveTickOwners`。第一段直接检查 `GetDefault<UAngelscriptGameInstanceSubsystem>()`：`GetTickableTickType()` 必须是 `ETickableTickType::Never`，`IsAllowedToTick()` 必须是 `false`。第二段创建一个挂到临时 `UGameInstance` 上的 live subsystem 实例，在 `bInitialized == false`、`PrimaryEngine == nullptr` 时检查 `IsAllowedToTick() == false`。第三段注入一个测试 engine 并把 `bInitialized` 设为 `true`，随后验证 `IsAllowedToTick() == true`、`IsTickableInEditor() == true`、`IsTickableWhenPaused() == true`；最后再分别清空 `PrimaryEngine` 或把 `bInitialized` 改回 `false`，确认 gate 会重新关闭。 |
| 输入/前置 | `AngelscriptTestSupport::CreateFullTestEngine()` 或 `CreateForTesting(..., Clone)`；临时 `UGameInstance`；从 `AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` 提炼到 `Shared/` 或本文件内的 `FAngelscriptTickBehaviorTestAccess`；`ON_SCOPE_EXIT` 恢复 `ActiveTickOwners` 与任何被改写的 `ContextStack` |
| 期望行为 | CDO/template 上 `GetTickableTickType() == ETickableTickType::Never` 且 `IsAllowedToTick() == false`；live instance 在 `PrimaryEngine == nullptr` 或 `bInitialized == false` 时始终 `IsAllowedToTick() == false`；只有当两者同时满足时 `IsAllowedToTick() == true`；`IsTickableInEditor()` 与 `IsTickableWhenPaused()` 对 live instance 始终返回 `true`；重新关掉任一 gate 后 `IsAllowedToTick()` 立即恢复 `false` |
| 使用的 Helper | `FAngelscriptTickBehaviorTestAccess`（从 `AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` 复用/提炼）+ `AngelscriptTestSupport::CreateFullTestEngine()` + 临时 `UGameInstance` + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-74 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-75 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:29:29)

### 一、现有测试问题

#### Issue-76：`FullDestroyAllowsAnnotatedRecreate` 只验证 generated class 能找到，没有证明重建后的 annotated class 仍然可用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.FullDestroyAllowsAnnotatedRecreate` |
| 行号范围 | 282-295, 304-351 |
| 问题描述 | 该用例的正向 helper `CompileAnnotatedActor(...)` 在 282-295 行只做两件事：`CompileAnnotatedModuleFromMemory(...)` 成功，以及 `FindGeneratedClass(...) != nullptr`。两轮脚本都故意声明了 `UPROPERTY() int Value = 11/22;`，但测试从未检查 `Value` 属性是否真的出现在 generated class 上，也没有读取 CDO/default instance 的默认值。结果是：只要类名被注册进类型系统、哪怕生成类已经退化成“能找到但属性布局/默认值错误”的半成品，这条 recreate 用例仍会绿灯。 |
| 影响 | `FullDestroyAllowsAnnotatedRecreate` 目前只能证明“full destroy 后还能再次拿到一个同名 generated class”，不能证明 class generator 在第二个 epoch 里仍然产出了可实例化、属性正确的 annotated class。涉及默认属性初始化、反射布局或 generated class 组装的回归会被直接漏掉。 |
| 修复建议 | 在 `FindGeneratedClass(...)` 之后补强正向断言：读取 generated class 的 CDO 或创建一个临时对象，用 `FindFProperty<FIntProperty>(GeneratedClass, TEXT("Value"))` 断言属性存在，再读取默认值并分别验证第一轮为 `11`、第二轮为 `22`。如果希望更贴近运行时，还可以在 helper 里额外验证 class 可实例化且 `Value` 在实例上保持同样默认值，而不是只停留在 `UClass*` 可解析。 |

#### Issue-77：`AngelscriptUhtCoverageTestTypes.cpp` 作为 helper-only fixture 混在 `Core/` 测试目录里，会误导覆盖盘点

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.cpp` |
| 测试名 | `无（helper-only fixture file）` |
| 行号范围 | 1-27 |
| 问题描述 | 该文件位于 `Plugins/Angelscript/Source/AngelscriptTest/Core/`，文件名也呈现出典型“测试文件”命名，但全文只有 `UAngelscriptUhtCoverageTestLibrary` 的 fixture/fixture-support 实现，没有任何 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`。这会让基于文件数的审查、目录盘点或人工浏览产生“Core 目录多了一份测试覆盖”的错觉，而实际这里承载的是 `BindConfig` 用例的测试支撑类型。 |
| 影响 | RuntimeCore 覆盖盘点容易把这个 helper 文件误算成一份现有测试，导致对 `Core/` 真实测试密度和责任边界的判断偏乐观；后续维护者也更难快速分辨“这是可执行测试”还是“只是测试夹具实现”。 |
| 修复建议 | 把该文件移到 `Plugins/Angelscript/Source/AngelscriptTest/Shared/`，或至少重命名为更明确的 `AngelscriptUhtCoverageTestFixtures.cpp` / `...Support.cpp`，并让 `Core/` 目录只保留真正包含 automation case 的测试文件。如果短期内不移动，至少在文件头增加 `fixture only, no automation cases` 说明，并在测试目录统计脚本里把它从“测试文件数”中排除。 |

### 二、需要新增的测试

#### NewTest-68：覆盖 `FAngelscriptDocs::DumpDocumentation()` 的生成产物合同，防止文档导出静默退化

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` |
| 关联函数 | `FAngelscriptDocs::DumpDocumentation(asIScriptEngine*)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-15` 只覆盖 `AddUnrealDocumentation(...)` / `GetUnrealDocumentation(...)` / `LookupAngelscriptFunction(...)` 这组缓存读写 API；目标测试目录与 `AngelscriptRuntime/Tests` 中都没有任何用例直接调用 `DumpDocumentation()`，更没有验证 `Docs/angelscript/generated/*.hpp` 的生成内容 |
| 风险评估 | 一旦 `DumpDocumentation()` 在 accessor 收敛、参数/返回值 tooltip 提取、文件写盘或 `Docs/angelscript/generated` 目录输出上回归，当前自动化不会报警；开发者只会在离线导出文档后看到空文件、缺少 `Parameters/Returns` 段落或 accessor property 消失 |
| 建议测试名 | `Angelscript.TestModule.Engine.Docs.DumpDocumentationEmitsGeneratedHeaderWithTooltipAndAccessorSections` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocsTests.cpp` |
| 场景描述 | 在 isolated full engine 下编译一个带唯一 GUID 后缀的最小脚本类型，例如 `class FAutomationDocs_<Guid> { int GetScore() const { return 7; } void SetScore(int InScore) {} }`。解析该类型的 `GetScore` / `SetScore` script function id 后，用 `FAngelscriptDocs::AddUnrealDocumentation(...)` 分别注入含 `@param InScore` 与 `@return` 的 tooltip/category，然后调用 `FAngelscriptDocs::DumpDocumentation(Engine.GetScriptEngine())`。最后读取 `Project/Docs/angelscript/generated/FAutomationDocs_<Guid>.hpp`。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)`；唯一 type/module 名；`CompileModuleFromMemory(...)` 或等价 compile helper；可读取 script type/method id 的小型 helper；`ON_SCOPE_EXIT` 清理生成的 `.hpp` 文件与临时模块 |
| 期望行为 | 目标 `.hpp` 文件必须被创建；文件内容至少包含 `class FAutomationDocs_<Guid>`、生成的 accessor/property 名 `Score`、`Parameters:` 段里的 `InScore` 说明，以及 `Returns:` 段里的返回说明；若 category 被写入，还应出现对应 `// Group:` 分组文本。这样才能证明 `DumpDocumentation()` 不只是缓存存在，而是实际把 tooltip/accessor 结构落成文档产物。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / `CompileModuleFromMemory(...)` + script function lookup helper + 文件读取/清理 helper |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-76 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-77 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:41:14)

### 记录校正

本轮新增发现为 `NewTest-69` 与 `NewTest-70`。
两条正文已写入本文前段，但上一轮追加时命中了错误锚点，没有落在文末；此处补齐真正的文末追加标记，供后续轮次按文末继续去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 11:46:56)

### 一、现有测试问题

#### Issue-：SoftReferenceCppForm 只要求 CppHeader 非空，抓不住错误 include 映射

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp |
| 测试名 | Angelscript.TestModule.Parity.SoftReferenceCppForm |
| 行号范围 | 417-449 |
| 问题描述 | 这条用例对 TSoftObjectPtr<UTexture2D> / TSoftClassPtr<AActor> 的 GetCppForm() 做了多项字符串断言，但落到 include 关键信息时只检查 SoftObjectForm.CppHeader.IsEmpty() == false 和 SoftClassForm.CppHeader.IsEmpty() == false。也就是说，只要返回任意一个非空 header 字符串，测试就会通过；即便回归把 UTexture2D 映射成错误的头文件、把 AActor 指到无关 include，当前用例也抓不住。 |
| 影响 | SoftReferenceCppForm 的核心价值是保护 StaticJIT/生成代码所依赖的 native type 形态。如果 CppHeader 错而不空，运行时 parity 测试仍是绿灯，但真正的 generated C++ 代码会在编译阶段才暴露问题，报警既晚也难定位。 |
| 修复建议 | 把 header 断言升级为精确匹配：至少对 TSoftObjectPtr<UTexture2D> 断言 CppHeader 包含或等于 Engine/Texture2D.h，对 TSoftClassPtr<AActor> 断言 CppHeader 包含或等于 GameFramework/Actor.h。如果 GetCppForm() 允许多头文件格式，建议把 CppHeader 解析成条目列表后按集合比较，而不是只看“非空”。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue- |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:01:27)

### 记录校正

截至本段，当前轮有效新增发现为：
- `Issue-78`
- `Issue-79`
- `NewTest-71`
- `NewTest-72`

其中 `Issue-78`、`Issue-79`、`NewTest-71`、`NewTest-72` 的完整正文均已写入本文前段；本段仅作为真正的文末续写标记，供后续轮次从文末继续追加与去重。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-78 |
| BadIsolation | 0 | 无 |
| AntiPattern | 1 | Issue-79 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:20:47)

### 记录校正

本轮有效新增发现为：
- `Issue-80`
- `NewTest-73`
- `NewTest-74`
- `NewTest-75`

上述 4 条完整正文已写入本文前段，但前两次追加都命中了错误锚点，没有落在真实文末；此处补齐真正的文末续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 1 | Issue-80 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:22:29)

### 二、需要新增的测试

#### NewTest-76：给 `UAngelscriptAbilityTaskLibrary` 补齐 root-motion / movement wrapper 的参数透传合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` |
| 关联函数 | `ApplyRootMotionConstantForce(...)` / `ApplyRootMotionJumpForce(...)` / `ApplyRootMotionRadialForce(...)` / `WaitMovementModeChange(...)` / `WaitVelocityChange(...)` |
| 现有测试覆盖 | 当前文档里的 `NewTest-71` 只覆盖 `ApplyRootMotionMoveToActorForce` / `ApplyRootMotionMoveToTargetDataActorForce` / `ApplyRootMotionMoveToForce` 这组 move-to wrapper；constant/jump/radial force 与 movement/velocity watcher wrappers 在目标测试目录和已记录建议里仍完全无直接覆盖。 |
| 风险评估 | 这批 wrapper 一旦把 direction、strength、distance、height、radius、finish flags、movement mode 或 minimum magnitude 透传错，脚本能力会表现成“节点能创建但角色运动完全不对”，属于高影响运行时回归，而现有自动化没有任何定位点。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GASAbilityTaskLibrary.MovementWrappersSeedExpectedTaskConfig` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTaskLibraryMovementTests.cpp` |
| 场景描述 | 建立最小 GAS fixture：带 `UAngelscriptAbilitySystemComponent` 的 `ACharacter` avatar、native `UGameplayAbility` test double，以及必要的 `UCurveFloat` / `UCurveVector` 占位。分别调用 `ApplyRootMotionConstantForce(TaskConstant, FVector(1,2,3), 900.f, 0.75f, true, nullptr, MaintainLastRootMotionVelocity, FVector::ZeroVector, 0.f, true)`、`ApplyRootMotionJumpForce(TaskJump, FRotator(0,45,0), 600.f, 220.f, 0.8f, 0.1f, false, MaintainLastRootMotionVelocity, FVector::ZeroVector, 0.f, nullptr, nullptr)`、`ApplyRootMotionRadialForce(TaskRadial, FVector(10,20,30), nullptr, 1200.f, 0.6f, 350.f, true, false, true, nullptr, nullptr, true, FRotator(0,90,0), MaintainLastRootMotionVelocity, FVector::ZeroVector, 0.f)`、`WaitMovementModeChange(MOVE_Falling)`、`WaitVelocityChange(FVector::ForwardVector, 220.f)`。用本地 access shim 读取各 task 的公开/受保护字段。 |
| 输入/前置 | `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` + GAS character/ability fixture；本地 access shim：`UAutomationRootMotionConstantTaskAccess`、`UAutomationRootMotionJumpTaskAccess`、`UAutomationRootMotionRadialTaskAccess`、`UAutomationVelocityChangeTaskAccess`；`ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity`；`ON_SCOPE_EXIT` 销毁所有 task。 |
| 期望行为 | constant-force task 返回 `UAbilityTask_ApplyRootMotionConstantForce`，且 `WorldDirection == FVector(1,2,3)`、`Strength == 900.f`、`Duration == 0.75f`、`bIsAdditive == true`、`bEnableGravity == true`；jump-force task 返回 `UAbilityTask_ApplyRootMotionJumpForce`，并保留 `Rotation.Yaw == 45`、`Distance == 600.f`、`Height == 220.f`、`Duration == 0.8f`、`MinimumLandedTriggerTime == 0.1f`、`bFinishOnLanded == false`；radial-force task 返回 `UAbilityTask_ApplyRootMotionRadialForce`，并保留 `Location == FVector(10,20,30)`、`Strength == 1200.f`、`Radius == 350.f`、`bIsPush == true`、`bIsAdditive == false`、`bNoZForce == true`、`bUseFixedWorldDirection == true`、`FixedWorldDirection.Yaw == 90`；movement-mode task 的 `RequiredMode == MOVE_Falling`；velocity-change task 的 `Direction == FVector::ForwardVector` 且 `MinimumMagnitude == 220.f`。所有 task 的 outer/owning ability 都必须等于传入 ability。 |
| 使用的 Helper | `FAngelscriptTestFixture` + GAS character/ability fixture + root-motion / velocity access shims + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:30:43)

### 记录校正

本轮有效新增发现为：
- `NewTest-77`
- `NewTest-78`

上述 2 条完整正文已写入本文前段，但前两次追加都命中了前文重复汇总锚点，没有落在真实文末；此处补齐真正的文末续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:44:25)

### 记录校正

本轮有效新增发现为：
- `Issue-81`
- `Issue-82`
- `NewTest-79`

上述 3 条完整正文已写入本文前段，但本次追加再次命中了前文重复汇总锚点，没有落在真实文末；此处补齐真正的文末续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 1 | Issue-81 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 1 | Issue-82 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 12:57:51)

### 记录校正

本轮有效新增发现为：
- `Issue-83`
- `NewTest-80`

上述 2 条完整正文已写入本文前段，但上一轮追加再次命中了前文汇总表，未落在真实文末；此处补齐真正的文末续写标记，供后续轮次从真实文件尾继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-83 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 13:20:11)

### 记录校正

本轮有效新增发现为：
- `NewTest-81`
- `NewTest-82`

上述 2 条完整正文已写入本文前段，且此前两次续写都没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:37:17)

### 记录校正

本轮有效新增发现为：
- `NewTest-83`
- `NewTest-84`

上述 2 条完整正文已写入本文前段；此前两次“续写”都命中了前文重复汇总锚点，没有落在真实文件尾。此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:53:35)

### 记录校正

本轮有效新增发现为：
- `NewTest-85`
- `NewTest-86`

上述 2 条完整正文已写入本文前段，但前两次续写都命中了前文重复汇总锚点，没有落在真实文件尾；本段补齐真实文末续写锚点，并继续从文末追加新的 source-scan 发现。

### 一、现有测试问题

本轮未新增现有测试质量问题；新增发现继续集中在 `AngelscriptEngine.cpp` 的 direct helper 合同缺口。

### 二、需要新增的测试

#### NewTest-87：给 `CanCastScriptObjectToUnrealInterface(...)` 建立 implementing/non-implementing/null guard 的 native fast-path 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CanCastScriptObjectToUnrealInterface(asITypeInfo*, asITypeInfo*, void*)` |
| 现有测试覆盖 | 推断：`Plugins/Angelscript/Source/AngelscriptTest/Interface/` 下的脚本 cast 场景可能会间接经过该 helper；但全仓测试检索 `CanCastScriptObjectToUnrealInterface` 为 0 直接命中，且没有任何一条测试把 `RuntimeType` / `TargetType` / `ObjectPtr` 的 native fast-path guard 合同独立固定下来，更没有覆盖 null 输入和“target 不是 interface”这两个显式 early-return 分支。 |
| 风险评估 | 这个 quick-cast helper 被第三方 AngelScript cast 路径直接调用。一旦它把非 interface target 错判为可转换、把未实现 interface 的 script object 错判成 `true`，或 null 输入不再稳定返回 `false`，高层 interface 场景只会在特定 cast 组合下偶发回归，定位会落到第三方 cast 栈而不是 RuntimeCore 自己。 |
| 建议测试名 | `Angelscript.TestModule.Engine.InterfaceInterop.CanCastScriptObjectToUnrealInterfaceHonorsImplementingAndGuardCases` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineTypeInteropTests.cpp` |
| 场景描述 | 复用 `Shared/AngelscriptNativeInterfaceTestTypes.h`，并在测试文件里移植一份最小 `EnsureNativeInterfaceBoundForTests(...)` helper（可参考 `Interface/AngelscriptInterfaceNativeTests.cpp` 的绑定方式）。随后在 fresh/isolated engine 下编译两个最小脚本 `UObject` 子类：`UAutomationInterfaceCastOk : UObject, UAngelscriptNativeParentInterface` 实现 `GetNativeValue/SetNativeMarker/AdjustNativeValue`，以及 `UAutomationInterfaceCastNoImpl : UObject` 不实现该 interface。拿到两个 generated `UClass` 后，用 `CastChecked<UASClass>(Class)->ScriptTypePtr` 作为 `RuntimeType`，再通过 `Engine.GetScriptEngine()->GetTypeInfoByName(TCHAR_TO_ANSI(*FAngelscriptType::GetBoundClassName(UAngelscriptNativeParentInterface::StaticClass())))` 取 interface `TargetType`；另外再取一个普通非 interface type info 作为 negative control。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()` 或 `AngelscriptTestSupport::CreateFullTestEngine()`；`Shared/AngelscriptNativeInterfaceTestTypes.h`；局部 native-interface bind helper；`CompileScriptModule(...)` / `FindGeneratedClass(...)`；`UASClass::ScriptTypePtr`；`NewObject<UObject>(GetTransientPackage(), ImplementingClass)` 与 non-implementing control object。 |
| 期望行为 | implementing object case 必须返回 `true`；non-implementing object case 必须返回 `false`；把 `TargetType` 换成普通 non-interface type info 时必须返回 `false`；任意一项输入为 `nullptr` 时也都必须返回 `false`。若同一 fixture 中再编译一个实现 child interface 的 class，还应补一条断言确认它对 parent interface target 仍返回 `true`，证明 helper 与 `UClass::ImplementsInterface(...)` 的继承语义保持一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH()` / `CreateFullTestEngine()` + `CompileScriptModule(...)` + `FindGeneratedClass(...)` + 本地 `EnsureNativeInterfaceBoundForTests(...)` + `UASClass::ScriptTypePtr` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-09 14:10:46)

### 记录校正

本轮有效新增发现为：
- `Issue-85`
- `NewTest-88`

上述 2 条完整正文已写入本文前段，且前三次“文末补录”都命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-85 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-09 23:34:19)

### 记录校正

本轮有效新增发现为：
- `NewTest-89`
- `NewTest-90`
- `NewTest-91`

上述 3 条完整正文已写入本文前段；此前两次“补录文末”都命中了中段重复汇总锚点，没有落在真实文件尾。此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingScenario: 1, NoTestForSource: 1 |
| P2 | 0 | 无 |
| P3 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 23:51:11)

### 记录校正

本轮有效新增发现为：
- `Issue-86`
- `Issue-87`
- `NewTest-92`

上述 3 条完整正文已写入本文前段，但本次续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-87 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 1 | Issue-86 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-10 00:07:07)

### 记录校正

本轮有效新增发现为：
- `Issue-88`
- `NewTest-93`

上述 2 条完整正文已写入本文前段，但续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 1 | Issue-88 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingEdgeCase: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-10 00:25:13)

### 记录校正

本轮有效新增发现为：
- `Issue-89`
- `NewTest-94`

上述 2 条完整正文已写入本文前段，但此前多次续写都命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-89 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingErrorPath: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:43:57)

### 记录校正

本轮有效新增发现为：
- `Issue-90`
- `NewTest-95`

上述 2 条完整正文已写入本文前段，但本次续写再次命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-90 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | MissingScenario: 1 |
| P3 | 0 | 无 |
 
---
 
## 测试审查 (2026-04-10 00:47:27)
 
### 记录校正
 
本轮有效新增发现为：
- `Issue-91`
- `Issue-92`
- `Issue-93`
- `NewTest-96`
- `NewTest-97`
- `NewTest-98`
 
上述 6 条完整正文已写入本文前段，但本次续写再次命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。
 
### 本轮汇总
 
**现有测试问题统计**
 
| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 3 | Issue-92 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |
 
**新增测试建议统计**
 
| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 2 | MissingErrorPath: 1, MissingEdgeCase: 1 |
| P2 | 0 | 无 |
| P3 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 01:16:50)

### 记录校正

本轮有效新增发现为：
- `Issue-94`
- `NewTest-99`
- `Issue-95`

上述 3 条完整正文已写入本文前段（约 1071-1154 行），此前两次“补录文末”都命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-94 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:22:12)

### 记录校正

本轮有效新增发现为：
- `Issue-96`
- `Issue-97`
- `NewTest-100`

上述 3 条完整正文已写入本文前段（约 1106-1163 行），此前两次“补录文末”都命中了前文重复锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重，不再重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-96 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 01:52:05)

### 记录校正

本轮有效新增发现为：
- `Issue-98`
- `Issue-99`

上述 2 条完整正文已写入本文前段（约 582-631 行），但续写再次命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记。对 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的本轮二次回扫未发现比前文更高优先级的新测试空白点，因此本轮没有新增 `NewTest-*`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 0 | 无 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 1 | Issue-99 |
| WrongHelper | 1 | Issue-98 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-10 02:10:01)

### 一、现有测试问题

#### Issue-100：两条 `GeneratedFunctionTable` 手写 GAS 回归把关键尾部断言留在旁路 `TestTrue`，显式通过条件没有锁住最终合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.GeneratedFunctionTable.PreservesHandwrittenGASEntries` / `Angelscript.TestModule.Engine.GeneratedFunctionTable.ReflectiveFallbackStats` |
| 行号范围 | 231-239, 443-456 |
| 问题描述 | `PreservesHandwrittenGASEntries` 在 231-238 行把第二个代表样本 `WaitGameplayTagRemoveFromActor` 的 `FuncPtr.IsBound()` 断言写成尾部 `TestTrue(...)`，随后 239 行直接 `return true;`；`ReflectiveFallbackStats` 也在 443-455 行先验证 `WaitForAttributeChanged` 样本，再把“handwritten GAS entry 不应被重分类为 reflective fallback”的关键断言留在 455 行的 `TestTrue(...)`，456 行同样直接 `return true;`。这两条测试各自最接近真实 GAS 回归点的最后一道断言，都没有被折回显式返回路径，代码结构上仍把更早的 smoke check 当成唯一硬门槛。 |
| 影响 | 手写 GAS entry 的关键兼容合同在代码层面被弱化成“尾部附带检查”。即使自动化框架当前会记录 `TestTrue` 失败，这种写法仍容易在后续重构里被误解成非关键断言，也让读代码的人误以为用例的最终通过条件只依赖前半段 entry 存在性检查。对 `GeneratedFunctionTable` 这类已经大量依赖 smoke check 的文件，这会继续稀释真正的回归信号。 |
| 修复建议 | 把尾部 GAS 断言显式纳入返回路径：例如为 `WaitGameplayTagRemovePointer.IsBound()` 和 `!WaitForAttributeChangedEntry->bReflectiveFallbackBound` 分别保存布尔值，并令 `return` 合并这些结果；或者改成失败即 `return false`。同时建议把手写 GAS compatibility 断言集中到同一个 helper，避免今后再出现“关键第二样本/最终样本只做日志级检查”的模式。 |

### 二、需要新增的测试

#### NewTest-101：覆盖 `UAngelscriptAbilityTask` 的 spec/context proxy 与 simulated-task 初始化合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTask.cpp` |
| 关联函数 | `InitSimulatedTask(UGameplayTasksComponent&)` / `GetIsSimulating()` / `BP_GetAbilitySpecHandle(...)` / `BP_IsPredictingClient()` / `BP_IsForRemoteClient()` / `BP_IsLocallyControlled()` / `BP_ShouldBroadcastAbilityTaskDelegates()` |
| 现有测试覆盖 | 当前文档里的 `NewTest-08` 只覆盖 `CreateAbilityTask(...)` / `CreateAbilityTaskAndRunIt(...)` 与 tick/pause/simulated flag 的基本 round-trip，`NewTest-66` 只覆盖 waiting flag 和 `BP_GetAbilitySystemComponent()`；目标测试目录与已记录建议里还没有任何一条直接命中 spec-handle/context proxy 或 `InitSimulatedTask(...)` 这组公开脚本入口。 |
| 风险评估 | 这些 proxy 是脚本 task 读取本地/远端上下文与 simulated state 的唯一现成入口。若 `BP_GetAbilitySpecHandle(...)` 不再返回当前 spec、`BP_IsLocallyControlled()` / `BP_ShouldBroadcastAbilityTaskDelegates()` 与基类状态脱节，或 `InitSimulatedTask(...)` 不再把 `bIsSimulating` 与 `BP_InitSimulatedTask` 正确联动，脚本侧 task 会表现成“能创建但网络/预测语义错位”，而现有自动化完全不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Engine.GAS.AbilityTask.ProxyGettersAndSimulatedInitMirrorBaseState` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGASAbilityTaskTests.cpp` |
| 场景描述 | 在测试文件里新增 native access shim `UAutomationAbilityTaskProxyAccess : UAngelscriptAbilityTask`，提供 `CallInitSimulatedTask(UGameplayTasksComponent&)` 公有包装，并在 `BP_InitSimulatedTask(...)` 中记录收到的 component 指针与调用次数。基于 `FAngelscriptTestFixture(ETestEngineMode::IsolatedFull)` 创建最小 GAS fixture：owner actor、`UAngelscriptAbilitySystemComponent`、native `UGameplayAbility` test double 和一个已授予的 valid spec handle。先用 `CreateAbilityTask(...)` 创建 task，再分别读取 `BP_GetAbilitySpecHandle(false/true)`、`BP_IsPredictingClient()`、`BP_IsForRemoteClient()`、`BP_IsLocallyControlled()`、`BP_ShouldBroadcastAbilityTaskDelegates()`；随后把 task 标成 simulated，并对一个真实 `UGameplayTasksComponent` 调 `CallInitSimulatedTask(...)`。 |
| 输入/前置 | `FAngelscriptTestFixture`；native `UGameplayAbility` test double；`UAutomationAbilityTaskProxyAccess`；授予 ability 后得到的有效 `FGameplayAbilitySpecHandle`；一个 `NewObject<UGameplayTasksComponent>(OwnerActor)` 并完成最小注册；必要时 access shim 暴露 `BP_InitSimulatedTask` 记录字段。 |
| 期望行为 | `BP_GetAbilitySpecHandle(false)` 与 `BP_GetAbilitySpecHandle(true)` 都必须等于授予结果的 valid handle；`BP_IsPredictingClient()`、`BP_IsForRemoteClient()`、`BP_IsLocallyControlled()`、`BP_ShouldBroadcastAbilityTaskDelegates()` 必须与同一 task 上对应 native 基类查询结果一致；调用 `SetIsSimulatedTask(true)` 后 `GetIsSimulatedTask()` 为 true，而在 `CallInitSimulatedTask(...)` 之前 `GetIsSimulating()` 仍保持 false；执行 `CallInitSimulatedTask(...)` 后 `GetIsSimulating()` 必须变为 true，且 recorder 恰好收到 1 次 `BP_InitSimulatedTask`，参数指针与传入 `UGameplayTasksComponent` 同址。 |
| 使用的 Helper | `FAngelscriptTestFixture` + native `UGameplayAbility` test double + `UAutomationAbilityTaskProxyAccess` + `UGameplayTasksComponent` fixture |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-100 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | 无 |
---

## 测试审查 (2026-04-10 02:22:20)

### 记录校正

本轮有效新增发现为：
- `Issue-101`
- `Issue-102`
- `NewTest-102`

上述 3 条完整正文已写入本文前段（约 623-670 行），此前两次续写都命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的续写标记，供后续轮次从文末继续追加与去重。

对 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的本轮文件名回扫中，除已记录的 `Issue-101` / `Issue-102` / `NewTest-102` 外，未发现比既有 backlog 更高优先级、且尚未记录的 Core 关键无测文件；当前仍未进入 backlog 的 `.cpp` 只剩 `AngelscriptThirdPartyLib.cpp`，其内容是注释掉的旧 third-party 聚合入口，不作为本轮补测目标。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-101 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |

---

## 测试审查 (2026-04-10 02:37:40)

### 记录校正

本轮有效新增发现为：
- `Issue-103`
- `Issue-104`

上述 2 条完整正文已误写入本文前段（约 695-718 行），此前三次续写都命中了前文重复汇总锚点，没有落在真实文件尾；此处补齐真正文件尾部的校正锚点，供后续轮次从文末继续追加。

对 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的本轮快速回扫未发现比既有 backlog 更高优先级、且尚未记录的关键无测文件；当前文件级空白结论不变，仍只有 `AngelscriptThirdPartyLib.cpp` 属于注释掉的旧 third-party 聚合入口，不作为补测目标。因此本轮没有新增 `NewTest-*`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-103 |
| BadIsolation | 0 | 无 |
| AntiPattern | 0 | 无 |
| FlakyRisk | 0 | 无 |
| WrongHelper | 0 | 无 |
| MissingCleanup | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |
| P1 | 0 | 无 |
| P2 | 0 | 无 |
| P3 | 0 | 无 |
