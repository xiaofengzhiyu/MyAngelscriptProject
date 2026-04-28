# FunctionLibraries 测试覆盖缺口

---

## 测试审查 (2026-04-08 13:13)

### 一、现有测试问题

#### Issue-1：`NativeActorMethods` 没有验证 `GetActorLocation` / `GetActorRotation` 的返回语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeActorMethods` |
| 行号范围 | 33-54 |
| 问题描述 | 脚本体虽然调用了 `GetActorLocation()` 和 `GetActorRotation()`，但随后既不比较返回值，也不把它们参与任何可失败断言。真正决定失败的条件只有 `Path.Len() < 0`、`FullName.Len() < 0` 和 `!bActorType`；前两个长度判断在运行时恒为假，因此即使 actor transform getter 绑定错了、始终返回零值或默认旋转，测试仍会返回 `1`。 |
| 影响 | `AngelscriptActorLibrary` 当前仅有的 getter 覆盖会产生误报绿灯，`GetActorLocation` / `GetActorRotation` 的返回语义回归无法被该测试拦住。 |
| 修复建议 | 不要对 CDO 做“只调用不验证”的 smoke test。改为在独立 world 中创建一个非零 transform 的 actor 实例，先由 C++ 设定 `Location` 和 `Rotation`，再让脚本显式比较 `GetActorLocation()` / `GetActorRotation()` 与期望值；建议把 `GetClass` / `GetPathName` 的 UObject 断言拆到单独测试，避免继续稀释 actor transform 断言。 |

#### Issue-2：`GameplayTagQueryCompat` 名称覆盖了 Query 兼容性，但实际没有验证 `GameplayTagQueryMixinLibrary` 的关键方法

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagQueryCompat` |
| 行号范围 | 191-236 |
| 问题描述 | 该用例只验证了 `FGameplayTagQuery` 默认值、若干 `MakeQuery_*` 工厂和 `Tags.MatchesQuery(...)` 这条 container 路径。它没有调用 `FGameplayTagQuery::Matches(...)` 实例方法，也没有读取 `GetDescription()`，连 `MatchTag` 也只是检查 “非空” 后就丢弃。这样一来，即使 `GameplayTagQueryMixinLibrary::Matches` 或 `GetDescription` 没有正确绑定、返回值语义漂移，测试仍然会通过。 |
| 影响 | `GameplayTagQueryMixinLibrary` 现有覆盖基本停留在“能创建 query 对象”，对真正暴露给脚本的 query 成员方法没有防线，回归会直接漏检。 |
| 修复建议 | 在当前用例里补一组正反路径：对 `MatchAny` / `MatchAll` / `MatchTag` 直接调用 `Query.Matches(Tags)`，再准备一个不含目标 tag 的容器验证 false 分支；同时断言 `GetDescription()` 返回非空并包含已构造的 tag 名，确保 query 成员方法而不是 container helper 真正被覆盖。 |

#### Issue-3：`LevelStreamingCompile` 只检查方法注册，不验证 `GetShouldBeVisibleInEditor()` 的实际返回值

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.LevelStreamingCompile` |
| 行号范围 | 515-531 |
| 问题描述 | 用例唯一断言是 `TypeInfo->GetMethodByDecl("bool GetShouldBeVisibleInEditor() const")` 非空，等价于“方法名出现在类型信息里”。它没有构造 `ULevelStreaming` 实例，也没有执行脚本去比较 helper 返回值与底层 `ULevelStreaming::GetShouldBeVisibleInEditor()` 的一致性，所以实现层即使始终返回常量、绑定到错误函数，测试也照样为绿。 |
| 影响 | `AngelscriptLevelStreamingLibrary` 目前唯一导出函数只有注册级覆盖，没有行为级覆盖，最容易被“签名还在、语义已坏”的回归绕过。 |
| 修复建议 | 保留类型存在性断言，但新增一个可执行场景：创建可控的 `ULevelStreaming` 实例，分别在 editor-visible 为 true / false 的状态下从脚本调用 `GetShouldBeVisibleInEditor()`，并把脚本返回值与 native 直接调用结果逐一比对。推荐单独新建一个 `LevelStreaming` 专用测试文件，不要继续堆在 parity megafile 中。 |

#### Issue-4：`RuntimeCurveLinearColorCompile` 只验证 `AddDefaultKey()` 能编译，完全没有验证四条颜色曲线是否被正确写入

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.RuntimeCurveLinearColorCompile` |
| 行号范围 | 534-570 |
| 问题描述 | 当前测试先检查 `FRuntimeCurveLinearColor` 上存在 `AddDefaultKey`，随后只编译一段调用 `URuntimeCurveLinearColorMixinLibrary::AddDefaultKey(...)` 和 `Curve.AddDefaultKey(...)` 的脚本，但从未执行脚本，也没有读取 `ColorCurves[0..3]` 的 key 数量、时间和值。`URuntimeCurveLinearColorMixinLibrary::AddDefaultKey` 的真实工作是向 RGBA 四条曲线各加一个 key；如果其中任一通道漏写、顺序写错或 alpha 被忽略，这个测试都不会失败。 |
| 影响 | `RuntimeCurveLinearColorMixinLibrary` 的现有覆盖只能发现“函数名消失/脚本无法编译”，发现不了最关键的颜色通道语义回归。 |
| 修复建议 | 改成编译并执行脚本，或在脚本执行后回到 C++ 检查 `ColorCurves[0..3]`：断言四条曲线都新增了 1 个 key，`InTime` 一致，`R/G/B/A` 分别等于传入颜色分量；再补一个第二次调用场景，确认不会只写第一通道或覆盖前一条 key。 |

#### Issue-5：`HitResultCompile` 只测公开字段读写，`AngelscriptHitResultLibrary` 的 helper 实际上没有被执行

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.HitResultCompile` |
| 行号范围 | 573-622 |
| 问题描述 | 脚本片段只是在 `FHitResult` 上写 `FaceIndex`、`ElementIndex`、`Item`、`MyItem`、`BoneName`、`MyBoneName` 这些公开字段，然后把整数求和返回。它没有调用 `SetActor`、`SetComponent`、`Reset`、`GetActor`、`GetComponent`、`SetbBlockingHit`、`SetbStartPenetrating` 等任何 `UAngelscriptHitResultLibrary` helper。换言之，这个 “HitResult parity” 用例通过时，只能说明基础 struct 字段可访问，完全说明不了函数库 wrapper 是否仍然工作。 |
| 影响 | `AngelscriptHitResultLibrary` 当前看似“有 parity 测试”，实际关键 helper 仍处于无行为覆盖状态；actor/component 句柄和命中标志相关回归会直接漏掉。 |
| 修复建议 | 新增一个真正执行 helper 的场景：在 world 里创建 actor 和 primitive component，脚本对 `FHitResult` 依次调用 `SetActor`、`SetComponent`、`SetBlockingHit` / `SetbStartPenetrating`，随后用 `GetActor`、`GetComponent`、`GetbBlockingHit`、`GetbStartPenetrating` 验证写入结果；最后调用 `Reset()`，断言句柄被清空且命中标志恢复默认值。 |

#### Issue-6：`WidgetUMG` 用编译型示例 helper 覆盖 `CreateWidget`，测试工具选型错误

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleWidgetUmgTest.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptExamples.WidgetUMG` |
| 行号范围 | 44-59 |
| 问题描述 | 示例脚本里确实调用了 `WidgetBlueprint::CreateWidget(WidgetClass, OwningPlayer)`，但测试主体只做 `RunScriptExampleCompileTest(...)`。对应 helper 在 `AngelscriptScriptExampleTestSupport.cpp` 里只负责拼脚本并调用 `CompileAnnotatedModuleFromMemory(...)`，不会执行 `Example_AddExampleWidgetToHUD`，也不会创建 widget blueprint、player controller 或 viewport。这样一来，`CreateWidget` 的 world-context、参数顺序、返回对象类型乃至运行时 null 路径都完全没有被验证。 |
| 影响 | `WidgetBlueprintStatics::CreateWidget` 当前只有“示例可编译”覆盖，没有任何运行时语义保障；一旦 wrapper 改坏，示例测试仍会给出绿灯。 |
| 修复建议 | 把 `CreateWidget` 从示例编译测试中拆成独立 runtime 用例。使用 `ASTEST_*` world 相关 helper 或 `FScopedTestWorldContextScope` 创建 `APlayerController` 与最小 widget class，执行脚本调用后断言返回值非空、类匹配、`GetOwningPlayer()` 正确；同时补一个 `WidgetClass == null` 的错误路径，验证脚本侧得到可预期的失败结果而不是静默成功。 |

#### Issue-7：`BindConfig` 里对 `RuntimeFloatCurve` 的覆盖只停留在 direct-bind 注册，完全不验证返回值和 out-ref 语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.InlineDefinitionFunctionsCanRecoverDirectBind` / `Angelscript.TestModule.Engine.BindConfig.InlineOutRefFunctionsCanRecoverDirectBind` |
| 行号范围 | 773-847 |
| 问题描述 | 两个用例分别对 `URuntimeFloatCurveMixinLibrary::GetNumKeys` 和 `GetTimeRange` 做的全部检查，就是确认 `ClassFuncMaps` 里存在条目且 `FFuncEntry` 不是 `ERASE_NO_FUNCTION`。它们既没有创建 `FRuntimeFloatCurve`，也没有执行脚本或 native 调用去检查 `GetNumKeys()==实际 key 数量`、`GetTimeRange(out Min, out Max)` 是否正确回填。只要绑定层留下了一个“已绑定”的入口，即使目标函数签名错配、返回值错、out-ref 顺序反了，这两个测试仍会通过。 |
| 影响 | `RuntimeFloatCurveMixinLibrary` 现在看似有 `GetNumKeys` / `GetTimeRange` 覆盖，实际上只有注册面 smoke test，没有任何行为防线。 |
| 修复建议 | 保留 direct-bind 恢复断言，但补一个真正执行函数的最小场景：构造含两个 key 的 `FRuntimeFloatCurve`，脚本侧断言 `GetNumKeys()` 返回 2，`GetTimeRange()` 输出最小/最大时间正确；最好再顺手覆盖 `GetTimeRange_Double`，确认 double overload 没有把输出精度或参数顺序弄错。建议把这类运行时断言迁到单独的 `RuntimeFloatCurve` 测试文件，避免继续堆进 700+ 行的 bind-config 汇总文件。 |

### 二、需要新增的测试

#### NewTest-1：Actor transform / sweep / attach 行为回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` |
| 关联函数 | `SetActorLocation` / `SetActorLocationAdvanced` / `GetActorLocation` / `SetActorRotation` / `GetActorRotation` / `AttachToActor` / `AttachToComponent` |
| 现有测试覆盖 | 有测试但仅 `NativeActorMethods` 弱覆盖 `GetActorLocation` / `GetActorRotation`，其余高频函数完全无测试 |
| 风险评估 | Actor transform helper 一旦参数顺序、sweep 结果或附着规则回归，脚本层移动和附着逻辑会直接失真，且现有测试不会报警 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 场景描述 | 在独立 world 中生成 parent actor、child actor 和一个 blocking primitive。脚本先做 location/rotation round-trip，再执行 `SetActorLocationAdvanced(..., bSweep=true, ...)`，最后验证 `AttachToActor` / `AttachToComponent` 后 child 的 parent 关系与相对 transform。 |
| 输入/前置 | `FAngelscriptTestFixture` 创建 world；C++ 预先设置 non-zero transform；阻挡体放在目标路径上；脚本暴露 distinct error code 以区分 getter、setter、sweep、attach 失败点。 |
| 期望行为 | `GetActorLocation()` / `GetActorRotation()` 与 native 设定值一致；`SetActorLocationAdvanced` 的 bool 返回值、`SweepHitResult.bBlockingHit` 和最终位置与 native 同步调用结果一致；附着后 `GetAttachParentActor()` 或 root component parent 与预期对象一致。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P0 |

#### NewTest-2：Math 边界条件与数值稳定性回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` |
| 关联函数 | `WrapInt` / `WrapFloat` / `WrapDouble` / `WrapIndex` / `WrapIndexUInt` / `AngularDistance` / `AngularDistanceForNormals` / `ConstrainToPlane` / `ConstrainToDirection` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 这些 helper 被脚本高频复用，一旦边界值处理或数值稳定性出错，会把移动、插值、导航和索引逻辑整体带偏 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.MathBoundaries` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` |
| 场景描述 | 纯脚本断言整数/浮点 wrap、signed/unsigned index wrap、零向量角距离、近似单位向量 `AngularDistanceForNormals`、以及非单位方向输入下的 `ConstrainToPlane` / `ConstrainToDirection`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本里构造固定输入，例如 `WrapIndex(-1,0,4)`、`WrapIndex(5,0,4)`、`WrapIndexUInt(6,0,4)`、零向量与单位向量、长度为 2 的方向向量。 |
| 期望行为 | wrap 系列返回可预期的循环结果；角距离 helper 返回有限数值而不是 `NaN`；`ConstrainToDirection(FVector(1,0,0), FVector(2,0,0))` 与按单位方向投影的结果一致；`ConstrainToPlane` 对非单位法线输入不会发生额外缩放。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P0 |

#### NewTest-3：`UWorld` / `ULevelStreaming` helper 运行时一致性

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h` |
| 关联函数 | `GetStreamingLevels` / `GetShouldBeVisibleInEditor` |
| 现有测试覆盖 | `GetStreamingLevels` 完全无测试；`GetShouldBeVisibleInEditor` 只有方法存在性 smoke test |
| 风险评估 | world/level streaming helper 一旦返回空数组、错误对象或错误 editor-visible 状态，关卡流送脚本会在运行时静默失效 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WorldStreamingAccess` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldFunctionLibraryTests.cpp` |
| 场景描述 | 在测试 world 中挂接至少一个 `ULevelStreaming` 实例，脚本同时读取 `World.GetStreamingLevels()` 和 `Level.GetShouldBeVisibleInEditor()`，再由 C++ 对照 native 结果校验。 |
| 输入/前置 | `FAngelscriptTestFixture` 创建 world；向 `World->GetStreamingLevels()` 插入已知顺序的 streaming level；显式切换 `bShouldBeVisibleInEditor` 对应状态。 |
| 期望行为 | 脚本拿到的 streaming level 数量、顺序和对象标识与 native 完全一致；`GetShouldBeVisibleInEditor()` 在 true/false 两种状态下都与 native 返回值一致。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-4：`FHitResult` helper 的句柄与标志位 round-trip

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` |
| 关联函数 | `SetActor` / `GetActor` / `SetComponent` / `GetComponent` / `SetBlockingHit` / `SetbBlockingHit` / `GetbBlockingHit` / `SetbStartPenetrating` / `GetbStartPenetrating` / `Reset` |
| 现有测试覆盖 | 有 `HitResultCompile`，但只测公开字段读写，没有任何 helper 行为覆盖 |
| 风险评估 | 命中对象句柄和 blocking/penetrating 标志一旦不同步，碰撞脚本会把命中解释错位，现有测试无法发现 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.HitResultAccessors` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptHitResultFunctionLibraryTests.cpp` |
| 场景描述 | 创建 actor 与 primitive component，脚本对 `FHitResult` 依次执行 setter/getter round-trip，再调用 `Reset()` 检查清理效果。 |
| 输入/前置 | world 中创建可识别名称的 actor/component；脚本返回错误码区分 actor 句柄、component 句柄、blocking 标志、start penetrating 标志、reset 后默认值。 |
| 期望行为 | `GetActor()` / `GetComponent()` 返回刚写入的对象；`GetbBlockingHit()` 与 `GetbStartPenetrating()` 能反映 setter 写入；`Reset()` 后对象句柄为空且两个标志恢复默认 false。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-5：classic input 映射管理与上下文获取

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` |
| 关联函数 | `GetPlayerInput` / `PushInputComponent` / `PopInputComponent` / `AddActionMapping` / `RemoveActionMapping` / `AddAxisMapping` / `RemoveAxisMapping` / `GetKeysForAction` / `GetKeysForAxis` / `ForceRebuildingKeyMaps` / `SetMouseSensitivity` / `GetMouseSensitivityX` / `GetMouseSensitivityY` |
| 现有测试覆盖 | 基本无 dedicated 测试，只有零散字符串命中，不能证明任何函数语义 |
| 风险评估 | classic input helper 是脚本侧接入旧输入系统的唯一入口，映射增删、查询和上下文访问一旦错位会直接影响按键绑定与设置界面 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.InputMappingRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp` |
| 场景描述 | 创建 `APlayerController`、`UInputComponent` 和 `UPlayerInput`，脚本先做 `PushInputComponent` / `GetPlayerInput`，再添加 action/axis mapping、`ForceRebuildingKeyMaps()`、查询 key 列表与鼠标灵敏度，最后删除 mapping 并确认回退。 |
| 输入/前置 | 用明确的 `ActionName`、`AxisName` 和 `FKey` 作为测试数据；额外准备一个 null `PlayerController` / null `PlayerInput` 场景，验证错误路径契约。 |
| 期望行为 | `GetPlayerInput()` 返回控制器当前 `PlayerInput`；添加 mapping 后 `GetKeysForAction()` / `GetKeysForAxis()` 数量和 key 与 native 一致；删除后查询结果恢复为空；`SetMouseSensitivity()` 后 X/Y getter 返回更新值；null 输入路径应返回 `nullptr` 或安全失败而不是崩溃。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P0 |

#### NewTest-6：UMG helper 的创建与 render transform 读取

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` |
| 关联函数 | `CreateWidget` / `GetRenderTransform` |
| 现有测试覆盖 | `CreateWidget` 只有示例 compile test；`GetRenderTransform` 完全无测试 |
| 风险评估 | widget 创建参数顺序、world-context 绑定或 render transform 读取一旦出错，UI 脚本会直接创建失败或拿到错误布局数据 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WidgetCreateAndTransform` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp` |
| 场景描述 | 在测试 world 中创建最小 `UUserWidget` 子类和 `APlayerController`，脚本调用 `WidgetBlueprint::CreateWidget` 得到实例后，由 native 预置一个非默认 `RenderTransform`，再从脚本读取 `GetRenderTransform()`。 |
| 输入/前置 | `FAngelscriptTestFixture` 提供 world/context；native 创建 widget class 和 owning player；另补一个 `WidgetClass == null` 的错误路径。 |
| 期望行为 | `CreateWidget` 返回对象非空、类匹配、`GetOwningPlayer()` 正确；`GetRenderTransform()` 返回的 translation / scale / angle 与 native 预设完全一致；传入 null widget class 时脚本得到可预期失败结果而不是静默成功。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-7：`FRuntimeCurveLinearColor.AddDefaultKey` 的四通道写入语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeCurveLinearColorMixinLibrary.h` |
| 关联函数 | `AddDefaultKey` |
| 现有测试覆盖 | 只有 compile smoke test，没有任何执行与结果断言 |
| 风险评估 | RGBA 任一通道漏写、写反或时间不一致都会让颜色曲线在运行时失真，而现有测试完全捕不到 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.RuntimeCurveLinearColorAddDefaultKey` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp` |
| 场景描述 | 脚本函数接收 `FRuntimeCurveLinearColor&`，连续两次调用 `AddDefaultKey` 写入不同时间和颜色；执行结束后由 C++ 检查四条 `ColorCurves` 的 keys。 |
| 输入/前置 | 使用红色和自定义 alpha 颜色作为第一组输入，再追加第二个时间点；curve 初始为空。 |
| 期望行为 | 四条颜色曲线都新增相同数量的 key；每个时间点的 R/G/B/A 值分别等于传入颜色分量；不会出现只写单通道或把 alpha 丢掉的情况。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-8：`FRuntimeFloatCurve` 的 key helper 与查询函数联动

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` |
| 关联函数 | `AddDefaultKey` / `AddLinearCurveKey` / `AddAutoCurveKey` / `GetNumKeys` / `GetTimeRange` / `GetFloatValue` / `Equals` |
| 现有测试覆盖 | `GetNumKeys` / `GetTimeRange` 只有 bind-config 注册覆盖，其余基本无测试 |
| 风险评估 | curve helper 一旦 key 类型、时间范围或求值逻辑回归，脚本动画和曲线驱动逻辑会直接失真 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveRoundTrip` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp` |
| 场景描述 | 脚本在空 `FRuntimeFloatCurve` 上添加 default key 与 curve key，查询 key 数量与时间范围，再在给定时间做插值求值；另构造一份相同 curve 验证 `Equals`。 |
| 输入/前置 | 使用两个已知时间点和值，例如 `(0,0)`、`(1,10)`；准备一份内容相同和一份内容不同的对照 curve。 |
| 期望行为 | `GetNumKeys()` 返回实际 key 数量；`GetTimeRange()` / `GetTimeRange_Double()` 返回 `0` 和 `1`；`GetFloatValue(0.5)` 落在预期插值区间；`Equals()` 仅对完全相同的 curve 返回 true。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-9：异步存档 helper 的回调载荷与失败路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h` |
| 关联函数 | `AsyncSaveGameToSlot` / `AsyncLoadGameFromSlot` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | slot 名、user index、回调线程或失败路径一旦错位，脚本侧存档流程会出现“保存完成但回调不对”这类高成本问题 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.AsyncSaveLoadDelegates` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayFunctionLibraryTests.cpp` |
| 场景描述 | 定义最小 `USaveGame` 派生类型，脚本调用 `AsyncSaveGameToSlot` 保存到唯一 slot，再调用 `AsyncLoadGameFromSlot` 读取；native 或脚本侧 recorder 记录回调参数。 |
| 输入/前置 | 唯一 `SlotName`、固定 `UserIndex`、带可识别字段的 save object；另补一个不存在 slot 的 load 失败路径。 |
| 期望行为 | save 回调收到原始 `SlotName` / `UserIndex` 且 `bSuccess == true`；load 回调收到同样的 `SlotName` / `UserIndex` 且 `SaveGameObject` 非空、字段值正确；读取不存在 slot 时回调仍会触发，但 `SaveGameObject == nullptr`。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `ASTEST_BUILD_MODULE` + latent automation wait |
| 优先级 | P1 |

#### NewTest-10：Subsystem helper 的上下文解析与无效输入

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h` |
| 关联函数 | `GetEngineSubsystem` / `GetGameInstanceSubsystem` / `GetLocalPlayerSubsystem` / `GetWorldSubsystem` / `GetLocalPlayerSubsystemFromPlayerController` / `GetLocalPlayerSubsystemFromLocalPlayer` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | subsystem lookup 是脚本访问引擎全局状态的基础入口，一旦 world/local player 解析错位，脚本会拿到错误实例或空对象 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.SubsystemLookup` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemFunctionLibraryTests.cpp` |
| 场景描述 | 基于现有 `UScriptEngineSubsystem` / `UScriptGameInstanceSubsystem` / `UScriptLocalPlayerSubsystem` / `UScriptWorldSubsystem` 基类生成最小测试子类，脚本分别通过六个 helper 查找实例，并覆盖无 local player 的 controller 路径。 |
| 输入/前置 | `FAngelscriptTestFixture` 创建 engine/world/game instance/local player；一个绑定到 local player 的 controller；一个不绑定 local player 的 controller。 |
| 期望行为 | 六个 helper 返回的对象与 native `GetSubsystem<>()` / `GetLocalPlayerSubSystemFromPlayerController()` 结果一致；不带 local player 的 controller 路径返回 `nullptr`；world-context 为空时应安全失败。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-11：`UAssetManager` 查询与 initial-scan 回调

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` |
| 关联函数 | `GetPrimaryAssetData` / `GetPrimaryAssetDataList` / `GetPrimaryAssetObject` / `GetPrimaryAssetIdForObject` / `GetPrimaryAssetIdList` / `GetPrimaryAssetRules` / `GetPrimaryAssetTypeInfo` / `GetPrimaryAssetTypeInfoList` / `CallOrRegister_OnCompletedInitialScan` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 资产查询与 initial scan 回调一旦绑定错位，资源预热、资产枚举和启动阶段脚本逻辑都会失效，而且现在没有任何自动化信号 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.AssetManagerQueryAndScan` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp` |
| 场景描述 | 获取已初始化的 `UAssetManager`，先用无效 `FPrimaryAssetId` / 无效 `FPrimaryAssetType` 覆盖失败路径，再通过一个 native receiver 验证 `CallOrRegister_OnCompletedInitialScan` 只触发一次；如果项目配置了可用 primary asset type，再补一条 happy path 对比 list/data/type-info 一致性。 |
| 输入/前置 | 依赖测试引擎初始化后的 `UAssetManager::GetIfInitialized()`；准备 `FPrimaryAssetId("Invalid:Missing")`、空 `FPrimaryAssetType` 和一个记录调用次数的 receiver UObject。 |
| 期望行为 | 无效 asset id 的 `GetPrimaryAssetData()` / `GetPrimaryAssetObject()` 返回失败或 null；无效 type 的 list 查询返回 false 且输出数组为空；`CallOrRegister_OnCompletedInitialScan` 在引擎完成初始扫描后只调用一次 receiver；若存在已配置 type，则 `GetPrimaryAssetIdList`、`GetPrimaryAssetDataList`、`GetPrimaryAssetTypeInfo` 的数量和标识彼此一致。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `ASTEST_BUILD_MODULE` + receiver UObject |
| 优先级 | P1 |

#### NewTest-12：SceneComponent transform / bounds / attach helper 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` |
| 关联函数 | `SetWorldLocation` / `SetWorldRotation` / `GetRelativeLocation` / `GetRelativeRotation` / `GetBounds` / `GetShapeCenter` / `AttachToComponent` / `IsAttachedTo` / `IsAttachedTo_Actor` |
| 现有测试覆盖 | 仅 `GetRelativeLocation` / `SetRelativeLocation` / `GetNumChildrenComponents` 有弱覆盖，其余大部分函数完全无测试 |
| 风险评估 | 组件层 transform 和附着 helper 一旦失真，会直接影响脚本驱动的层级移动、socket 对齐和 bounds 查询 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ComponentTransformAndAttach` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp` |
| 场景描述 | 创建 parent / child `USceneComponent` 层级，脚本执行 world/relative transform 设置、附着和 bounds 查询，再与 native 结果对照。 |
| 输入/前置 | 在 actor 上创建已注册 component 层级；为 child 设置非零 relative transform；必要时添加带 shape 的 primitive component 以验证 `GetShapeCenter()` / `GetBounds()`。 |
| 期望行为 | world/relative getter 返回与 native 设置一致；附着后 `IsAttachedTo()` / `IsAttachedTo_Actor()` 为 true；`GetBounds()` / `GetShapeCenter()` 与 native 调用结果一致；detach 前后的子组件数量变化符合预期。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-13：soft reference async 回调类型与失败路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h` |
| 关联函数 | `FOnSoftObjectLoaded` / `FOnSoftClassLoaded` |
| 现有测试覆盖 | 只有 `TSoftObjectPtr.Get()` / `TSoftClassPtr.Get()` 的 compile smoke，delegate 完全无测试 |
| 风险评估 | async load 回调如果 payload 类型退化或失败时不触发，脚本侧资源异步加载链路会直接卡死或丢类型信息 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.SoftReferenceAsyncDelegates` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp` |
| 场景描述 | 脚本分别对一个存在的 `TSoftObjectPtr<UTexture2D>` 和 `TSoftClassPtr<AActor>` 发起 `LoadAsync(...)`，再对一个不存在的 soft path 发起失败加载；native 或脚本 recorder 捕获 delegate 参数。 |
| 输入/前置 | 使用项目内稳定存在的 engine asset path 和 actor class path；另准备一个明确不存在的 soft path；测试需要等待异步完成。 |
| 期望行为 | 成功路径回调只触发一次，object/class 参数非空且类型匹配；失败路径回调也会触发，但 payload 为 null；若脚本 API 设计要求保留模板类型信息，则回调中应能无额外 cast 地消费目标类型。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `ASTEST_BUILD_MODULE` + latent automation wait |
| 优先级 | P1 |

#### NewTest-14：WorldCollision async delegate 的 hits/overlaps 载荷

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WorldCollisionStatics.h` |
| 关联函数 | `FScriptTraceDelegate` / `FScriptOverlapDelegate` |
| 现有测试覆盖 | 只有 `System::*` 调用可编译的 smoke test，没有任何 async callback 行为断言 |
| 风险评估 | trace/overlap 的 user data、返回数组或回调触发时机一旦错位，脚本侧异步碰撞逻辑会误判结果且难以定位 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WorldCollisionAsyncCallbacks` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp` |
| 场景描述 | 在测试 world 放置一个可命中的阻挡体，脚本发起 `AsyncLineTrace*` 与 `AsyncOverlap*` 请求，并把 `TraceHandle`、`UserData`、`OutHits` / `OutOverlaps` 记录到 receiver。 |
| 输入/前置 | 已知位置的 blocking actor / component；固定 `UserData`；单次 trace 与单次 overlap 各一条。 |
| 期望行为 | trace delegate 收到非零 `TraceHandle`、正确的 `UserData` 和至少一个命中结果；overlap delegate 收到正确的 `UserData` 和包含目标对象的 overlap 数组；两条回调都只触发一次。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + latent automation wait |
| 优先级 | P1 |

#### NewTest-15：全局变量初始化上下文 helper

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` |
| 关联函数 | `GetNameOfGlobalVariableBeingInitialized` / `GetNamespaceOfGlobalVariableBeingInitialized` / `GetModuleNameOfGlobalVariableBeingInitialized` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 这组三个 helper 主要服务脚本模块初始化与调试，一旦名字、命名空间或模块名错位，排查全局初始化问题会直接失真 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GlobalInitContext` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp` |
| 场景描述 | 构造一个带 namespace 的脚本模块，在全局变量初始化表达式里调用 `Script::Get*BeingInitialized()` 并把结果存入可查询的脚本全局；模块加载完成后通过导出函数验证三元组。 |
| 输入/前置 | 模块名使用确定值；至少一个带 namespace 的全局变量和一个非初始化时机调用场景。 |
| 期望行为 | 初始化期间采集到的变量名、命名空间和模块名分别等于声明值；在非初始化时机再次调用三函数时返回空字符串。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` |
| 优先级 | P2 |

#### NewTest-16：`FQualifiedFrameTime.AsSeconds` 的整数帧与分数帧转换

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h` |
| 关联函数 | `AsSeconds` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 时间换算 helper 一旦精度或分数帧处理出错，会让 timeline、sequencer 和媒体同步脚本出现稳定偏差 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.FrameTimeAsSeconds` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFrameTimeFunctionLibraryTests.cpp` |
| 场景描述 | 脚本构造整数帧和分数帧 `FQualifiedFrameTime`，调用 `AsSeconds()`，并与手工计算结果比较。 |
| 输入/前置 | 例如 `48 @ 24fps`、`90 @ 30fps` 以及带 sub-frame 的 `12.5 @ 25fps`。 |
| 期望行为 | `48 @ 24fps` 返回 `2.0`，`90 @ 30fps` 返回 `3.0`，分数帧 case 返回 `0.5`；断言使用明确容差，避免浮点误判。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 6 | Issue-1 |
| WrongHelper | 1 | Issue-6 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 3 | MissingScenario: 1, NoTestForSource: 2 |
| P1 | 11 | MissingScenario: 7, NoTestForSource: 4 |
| P2 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-08 13:29)

### 一、现有测试问题

#### Issue-8：`NativeComponentMethods` 只在未注册、无父节点的孤立组件上验证相对位移，掩盖了层级语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeComponentMethods` |
| 行号范围 | 93-206 |
| 问题描述 | 用例创建的是 `NewObject<USceneComponent>(OuterActor, ...)` 得到的瞬态组件，只执行 `OuterActor->AddOwnedComponent(RuntimeComponent)`，既没有 `RegisterComponent()`，也没有建立 parent/child 层级。脚本里随后用 `SetRelativeLocation(...)`、`GetRelativeLocation()`、`GetComponentTransform()` 和 `GetNumChildrenComponents()` 做断言，但在这种“未注册且无父节点”的形态下，relative translation 与 world translation 天然重合，`GetNumChildrenComponents()` 也只会恒为 `0`。因此即使 `SetRelativeLocation` 错绑成 world-space setter，或子组件计数逻辑在真实层级下回归，这个测试仍会返回 `1`。 |
| 影响 | `AngelscriptComponentLibrary` 当前唯一涉及 `GetRelativeLocation` / `SetRelativeLocation` / `GetNumChildrenComponents` 的现有覆盖，只证明了孤立组件 smoke path，不足以拦截层级变换和子组件计数的真实行为回归。 |
| 修复建议 | 把 transform 与 hierarchy 断言拆成独立场景：创建已注册的 parent / child `USceneComponent`，先附着 child，再让脚本分别验证 `GetRelativeLocation()`、`GetComponentTransform().GetTranslation()` 和 parent world transform 的差异；同时在附着前后断言 `GetNumChildrenComponents()` 从 `0` 变为 `1`。如果继续保留当前文件，至少要补 `RegisterComponent()`、`SetupAttachment()` 和一个非零 parent transform，避免 detached smoke test 冒充层级语义覆盖。 |

#### Issue-9：`GameplayTagContainerCompat` 用单标签正路径把 `HasAny` / `HasAll` / exact 语义全部压成同一个结果

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagContainerCompat` |
| 行号范围 | 98-181 |
| 问题描述 | 用例只从 `UGameplayTagsManager` 取一个 `ValidTag`，然后把同一个 tag 同时放进 `Tags` 和 `Others`。在这种数据形态下，`HasTag` / `HasTagExact`、`HasAny` / `HasAnyExact`、`HasAll` / `HasAllExact` 全都会得到相同的 `true`，根本区分不出 exact 与非 exact、any 与 all 的语义差别；同时全程没有任何负路径，也没有 parent/child tag 组合。结果是，只要这些 helper 被错误地绑到同一个实现，当前测试依然会通过。 |
| 影响 | `GameplayTagContainerMixinLibrary` 现有覆盖会给人“核心 container 查询都测到了”的错觉，但对最容易退化成同实现的匹配语义几乎没有鉴别力，层级标签或多标签组合回归会直接漏检。 |
| 修复建议 | 至少补三组可区分数据：1）父标签与子标签，验证 exact / non-exact 差异；2）包含两个 tag 的 container，对 `HasAny` true 且 `HasAll` false 的场景做断言；3）完全不相交的负路径。再把 `RemoveTag` 之外的 `RemoveTags`、`Filter`、`FilterExact`、`GetGameplayTagParents` 拆成独立断言，避免继续把过多语义挤在单个单标签 smoke test 里。 |

#### Issue-10：`SoftReferenceCompile` 只检查同步 getter 能编译，完全没碰 `SoftReferenceStatics` 的异步 delegate 契约

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.SoftReferenceCompile` |
| 行号范围 | 454-485 |
| 问题描述 | 当前 parity 用例只编译 `TSoftObjectPtr::Get()`、`EditorOnlyLoadSynchronous()` 和 `TSoftClassPtr::Get()` 三个同步路径，再确认脚本函数声明存在。它既不执行加载，也没有声明或绑定 `FOnSoftObjectLoaded` / `FOnSoftClassLoaded`，因此 `SoftReferenceStatics.h` 中专门为异步加载暴露的 delegate payload 类型、成功/失败回调时机和 null 路径都没有任何验证。即使 `LoadAsync(...)` 回调参数退化、失败路径不触发，测试仍然会全绿。 |
| 影响 | `SoftReferenceStatics` 当前看似“有 parity 测试”，实际只有同步 compile surface 的 smoke；最容易在运行时出问题的异步回调载荷和失败分支仍然处于无防线状态。 |
| 修复建议 | 保留现有同步 compile smoke，但新增 runtime 用例专门覆盖 `LoadAsync(...)`：准备一个存在的 soft object/class path 和一个明确不存在的 path，分别断言成功回调 payload 类型正确、失败回调仍会触发且 payload 为 null；如果项目 API 期望保留模板子类型，还应在脚本侧直接消费目标类型而不是额外 cast。 |

#### Issue-11：`WorldCollisionCompile` 只做 API 面编译探测，没有任何 trace / overlap 结果与异步回调断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.WorldCollisionCompile` |
| 行号范围 | 264-306 |
| 问题描述 | 用例把 `System::LineTraceTestByChannel`、`SweepSingleByObjectType`、`OverlapMultiByProfile`、`ComponentSweepMulti`、`ComponentOverlapMulti`、`AsyncOverlapByProfile` 拼进一个脚本字符串后，仅检查 `CompileFunction(...)` 成功并产生函数对象。它没有执行脚本，没有放置任何可命中的 world 几何体，也没有声明 `FScriptTraceDelegate` / `FScriptOverlapDelegate` 去观察 `TraceHandle`、`UserData`、`OutHits` / `OutOverlaps`。因此即使 `WorldCollisionStatics` 的异步 delegate 载荷顺序错了、回调从不触发，或者同步查询始终返回空结果，这个测试都不会失败。 |
| 影响 | `WorldCollisionStatics` 现有覆盖停留在“符号能编译”，对真正决定脚本碰撞逻辑正确性的命中结果和异步回调契约完全没有保护；一旦回归，自动化只能继续给出误报绿灯。 |
| 修复建议 | 把 compile smoke 留给 API 面回归，但另起 world 场景测试：在独立 world 里放一个已知阻挡体，脚本发起同步 trace/overlap 并断言命中数组包含目标对象；再补 `AsyncLineTrace*` / `AsyncOverlap*`，通过 receiver 记录 `TraceHandle`、`UserData` 和结果数组，验证回调只触发一次且 payload 与 native 预期一致。 |

### 二、需要新增的测试

#### NewTest-17：`FGameplayTag` 层级匹配与父标签 helper

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h` |
| 关联函数 | `MatchesTag` / `MatchesTagExact` / `MatchesTagDepth` / `MatchesAny` / `MatchesAnyExact` / `GetSingleTagContainer` / `RequestDirectParent` / `GetGameplayTagParents` |
| 现有测试覆盖 | `GameplayTagCompat` 只覆盖 `IsValid`、默认值和 `RequestGameplayTag` 路径，其余 hierarchy helper 无直接测试 |
| 风险评估 | tag 层级匹配一旦把 exact / non-exact、parent / child 关系处理错，权限、能力和状态标签脚本都会在运行时静默误判 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GameplayTagHierarchySemantics` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | 在 C++ 侧从 `UGameplayTagsManager` 找到一个存在直接父标签的 `ChildTag`，再找一个与该父标签无关的 `UnrelatedTag`。脚本对 `ChildTag` 依次调用 hierarchy helper，验证 parent/child 语义与负路径。 |
| 输入/前置 | 用 `UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false)` 扫描出 `ChildTag.RequestDirectParent().IsValid()` 的标签，并额外选择一个 `!UnrelatedTag.MatchesTag(ParentTag)` 的无关标签；把三个 tag name 注入脚本。 |
| 期望行为 | `ChildTag.MatchesTag(ParentTag)` 为 `true`，`ChildTag.MatchesTagExact(ParentTag)` 为 `false`，`ChildTag.MatchesTagDepth(ParentTag) >= 1`；`ChildTag.MatchesAny(FGameplayTagContainer(ParentTag))` 为 `true` 且 `MatchesAnyExact(...)` 为 `false`；`RequestDirectParent()` 返回 `ParentTag`；`GetSingleTagContainer()` 只对 `ChildTag` 返回 exact true；`GetGameplayTagParents()` 同时包含 `ChildTag` 与 `ParentTag` 的 exact 命中；对 `UnrelatedTag` 的匹配全部返回 false 或 0。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE` + `ASTEST_BEGIN_SHARE` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-18：`FGameplayTagContainer` 的 `AddLeafTag` / `Filter` / `RemoveTags` 语义分化

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` |
| 关联函数 | `AddTagFast` / `AddLeafTag` / `RemoveTags` / `GetGameplayTagParents` / `Filter` / `FilterExact` |
| 现有测试覆盖 | `GameplayTagContainerCompat` 只有单标签 happy path，无法区分 exact / non-exact 过滤，也没有覆盖批量移除和父标签展开 |
| 风险评估 | container helper 如果把 parent/child 归并、精确过滤或批量删除做错，脚本侧标签集合运算会持续给出“看起来合理但其实错位”的结果 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GameplayTagContainerHierarchyAndFilters` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | 复用一组 `ChildTag` / `ParentTag` / `UnrelatedTag`。脚本先用 `AddTagFast(UnrelatedTag)` 做 smoke，再在另一个 container 上先加 `ParentTag` 后执行 `AddLeafTag(ChildTag)`，随后对两个 container 执行 `Filter`、`FilterExact`、`GetGameplayTagParents` 与 `RemoveTags`。 |
| 输入/前置 | 与 NewTest-17 相同的 tag 选择 helper；准备 `ContainerA = { ChildTag, UnrelatedTag }`、`ContainerB = { ParentTag, UnrelatedTag }` 和 `RemoveContainer = { ChildTag }`。 |
| 期望行为 | `AddTagFast` 后 container 对 `UnrelatedTag` 的 exact 命中为 true；`AddLeafTag(ChildTag)` 会移除显式 `ParentTag`，因此 container 对 `ChildTag` 的 exact 命中为 true、对 `ParentTag` 的 exact 命中为 false，但 `HasTag(ParentTag)` 仍为 true；`ContainerA.Filter(ContainerB)` 返回同时含 `ChildTag` 与 `UnrelatedTag` 的结果，而 `FilterExact(ContainerB)` 只保留 `UnrelatedTag`；`GetGameplayTagParents()` 结果包含 `ParentTag`；`RemoveTags(RemoveContainer)` 后 `ChildTag` 被移除，`UnrelatedTag` 保留。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE` + `ASTEST_BEGIN_SHARE` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-19：`FGameplayTagQuery` 成员方法与描述字符串

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h` |
| 关联函数 | `Matches` / `GetDescription` |
| 现有测试覆盖 | `GameplayTagQueryCompat` 只覆盖 query 工厂和 `Tags.MatchesQuery(...)` 这条 container 路径，query 成员方法仍无 dedicated 测试 |
| 风险评估 | 一旦 `FGameplayTagQuery::Matches` 或 `GetDescription` 绑定漂移，脚本调试信息和 query 复用路径都会直接失真，而现有测试给不出信号 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GameplayTagQueryMembers` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | 用一个 `PositiveTag` 和一个 `NegativeTag` 构造 `PositiveTags` / `NegativeTags` 两个 container，脚本分别构造 `MatchAny`、`MatchAll`、`MatchTag`、`MatchNone` query，并直接调用 `Query.Matches(Tags)` 与 `Query.GetDescription()`。 |
| 输入/前置 | 从 `UGameplayTagsManager` 取两个不同 tag；`PositiveTags` 包含 `PositiveTag`，`NegativeTags` 只包含 `NegativeTag`；把 tag name 注入脚本字符串。 |
| 期望行为 | `MatchAny.Matches(PositiveTags)`、`MatchAll.Matches(PositiveTags)`、`MatchTag.Matches(PositiveTags)` 为 true，三者对 `NegativeTags` 为 false；`MatchNone.Matches(PositiveTags)` 为 false 且对 `NegativeTags` 为 true；`PositiveTags.MatchesQuery(MatchAny)` 与 `MatchAny.Matches(PositiveTags)` 结果一致；`GetDescription()` 返回非空字符串，并至少包含构造 query 时使用的 tag name。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE` + `ASTEST_BEGIN_SHARE` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-11 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 3 |

---

## 测试审查 (2026-04-08 13:44)

### 一、现有测试问题

#### Issue-12：`GameplayTagCompat` 只验证默认值与字符串表示，`GameplayTagMixinLibrary` 的层级 helper 完全未触达

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagCompat` |
| 行号范围 | 25-95 |
| 问题描述 | 脚本体只执行了 `RequestGameplayTag`、`IsValid`、与 `FGameplayTag::EmptyTag` 的等值比较、`GetTagName()` 和 `ToString()`。而 `GameplayTagMixinLibrary.h` 中真正需要保护的 `MatchesTag`、`MatchesTagExact`、`MatchesTagDepth`、`MatchesAny`、`MatchesAnyExact`、`GetSingleTagContainer`、`RequestDirectParent`、`GetGameplayTagParents` 在现有用例里一次都没有被调用。测试还直接取 `AllTags.First()` 作为唯一样本，没有 parent/child 或 unrelated tag 组合，因此即使 hierarchy helper 缺绑、exact / non-exact 语义漂移，当前用例也会稳定返回 `1`。 |
| 影响 | `GameplayTagMixinLibrary` 现在会被误判成“已有 compat 覆盖”，但最关键的层级匹配和父标签展开路径实际上仍然没有任何行为级防线。 |
| 修复建议 | 把默认值 smoke path 与 hierarchy 语义拆开。新增 dedicated 用例先在 native 侧挑选 `RequestDirectParent().IsValid()` 的 `ChildTag` 和一个 `UnrelatedTag`，再让脚本显式断言 `MatchesTag` / `MatchesTagExact` / `MatchesTagDepth` / `MatchesAny` / `MatchesAnyExact` / `GetSingleTagContainer` / `GetGameplayTagParents`；如果继续放在现有文件，至少不要再用 `AllTags.First()` 这种无法形成层级关系的数据。 |

#### Issue-13：`GameplayTagQueryCompat` 用单标签数据同时验证 `MatchAny` / `MatchAll` / exact query，无法区分工厂语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagQueryCompat` |
| 行号范围 | 191-287 |
| 问题描述 | 当前脚本只构造了一个 `ValidTag`，随后 `Tags` 容器里也只放这一枚 tag。在这种单元素数据形态下，`MakeQuery_MatchAnyTags`、`MakeQuery_MatchAllTags`、`MakeQuery_ExactMatchAnyTags`、`MakeQuery_ExactMatchAllTags` 对 `Tags` 的求值天然都会得到同一结果；也就是说，即使 any / all factory 或 exact / non-exact query 被错误地绑定到同一个实现，`if (!Tags.MatchesQuery(...))` 这一串断言仍然会全部通过。再加上没有部分匹配、缺标签和 parent/child 组合，现有用例几乎无法区分 query 工厂的真实语义。 |
| 影响 | `GameplayTagQueryMixinLibrary` 与 container query 互操作会给出过强的绿灯信号，factory 接错、exact 语义丢失或 all / any 退化都可能直接漏检。 |
| 修复建议 | 把 query 语义测试改成至少两标签输入：准备 `{TagA, TagB}`、`{TagA}`、`{TagC}` 三组 container，分别对 `MakeQuery_MatchAnyTags`、`MakeQuery_MatchAllTags`、`MakeQuery_ExactMatchAnyTags`、`MakeQuery_ExactMatchAllTags` 断言 true / false 组合；再补一个 parent/child 组合，明确区分 exact 与 non-exact。为避免现有文件继续膨胀，建议拆到新的 GameplayTagQuery 专用测试文件。 |

### 二、需要新增的测试

#### NewTest-20：input delegate 绑定与 engine-defined mapping 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` |
| 关联函数 | `BindAction` / `BindKey` / `BindChord` / `BindAxis` / `BindAxisKey` / `BindVectorAxis` / `GetEngineDefinedActionMappings` / `GetEngineDefinedAxisMappings` / `InvertAxis` |
| 现有测试覆盖 | 完全无测试；当前文档里的 `InputMappingRoundTrip` 只覆盖 controller / player-input 上下文和 mapping 增删查询，不覆盖 delegate 绑定与 engine-defined helper |
| 风险评估 | delegate 绑定一旦没有写进正确的 binding array、事件类型错位或 payload 类型退化，脚本输入回调会静默失效；同时 `GetEngineDefinedActionMappings` / `GetEngineDefinedAxisMappings` 当前 wrapper 带 `ActionName` / `AxisName` 形参却未参与实现，不写专门用例就无法固定真实契约。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.InputDelegateBindingsAndEngineDefaults` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp` |
| 场景描述 | 创建 `UInputComponent`、`APlayerController`、`UPlayerInput` 和脚本 receiver UObject。脚本分别调用 `BindAction`、`BindKey`、`BindChord`、`BindAxis`、`BindAxisKey`、`BindVectorAxis` 注册 delegate；随后由 C++ 直接检查 `ActionBindings` / `KeyBindings` / `AxisBindings` / `AxisKeyBindings` / `VectorAxisBindings` 的数量、事件类型和绑定对象。另准备 engine-defined action / axis mapping，调用 `GetEngineDefinedActionMappings()` / `GetEngineDefinedAxisMappings()` 与 `InvertAxis()` 校验契约。 |
| 输入/前置 | 使用固定 `FName("Jump")`、`EKeys::SpaceBar`、`FInputChord(EKeys::SpaceBar, true, false, false, false)`、`FName("MoveForward")`、`EKeys::Gamepad_LeftX` 等数据；native 在调用前记录 `UPlayerInput::GetEngineDefined*` 的 baseline，并为一条 axis mapping 准备可反转的 scale。 |
| 期望行为 | 每种 bind helper 都向对应 binding array 追加 1 条记录，`KeyEvent`、`bConsumeInput`、delegate 所属对象 / 函数名与脚本输入一致；`GetEngineDefinedActionMappings()` / `GetEngineDefinedAxisMappings()` 的返回值与 native `UPlayerInput::GetEngineDefined*` 完全一致，并明确记录它们当前不会按 `ActionName` / `AxisName` 过滤；`InvertAxis()` 后目标 axis 的 `Scale` 符号翻转，非目标 axis 保持不变。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + native receiver UObject |
| 优先级 | P0 |

#### NewTest-21：`FRuntimeFloatCurve` key-handle 变更 helper 与 invalid-handle guard

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` |
| 关联函数 | `GetValueRange` / `GetValueRange_Double` / `AddCurveKey` / `AutoSetTangents` / `SetDefaultValue` / `SetPreInfinityExtrap` / `SetPostInfinityExtrap` / `SetKeyInterpMode` / `SetKeyTangentMode` / `SetKeyTangentWeightMode` / `SetKeyUserTangents` / `SetKeyUserTangentWeights` / `AddConstantCurveKey` / `AddCurveKeyTangent` / `AddCurveKeyBrokenTangent` / `AddCurveKeyWeightedArriveTangent` / `AddCurveKeyWeightedLeaveTangent` / `AddCurveKeyWeightedBothTangent` |
| 现有测试覆盖 | 只有 `GetNumKeys` / `GetTimeRange` 的 bind-config 注册 smoke，实际 key-handle 变更、weighted tangent 和 invalid-handle 分支完全没有执行级覆盖 |
| 风险评估 | 一旦 tangent mode、weight mode、infinity extrapolation 或 invalid key-handle guard 回归，曲线编辑脚本会生成“可运行但形状错误”的数据，现有测试完全看不到。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveKeyMutation` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp` |
| 场景描述 | 脚本创建 `UCurveFloat`，分别用 `AddConstantCurveKey`、`AddCurveKeyTangent`、`AddCurveKeyBrokenTangent`、`AddCurveKeyWeightedBothTangent` 等 helper 返回 `FCurveKeyHandle`，随后调用 `SetKeyInterpMode`、`SetKeyTangentMode`、`SetKeyTangentWeightMode`、`SetKeyUserTangents`、`SetKeyUserTangentWeights`、`AutoSetTangents`、`SetDefaultValue`、`SetPreInfinityExtrap`、`SetPostInfinityExtrap`。再额外传入一个默认构造的无效 `FCurveKeyHandle`，验证 guard 分支。 |
| 输入/前置 | 使用 2 到 3 个已知时间点和值，例如 `(0,0)`、`(1,10)`、`(2,5)`；为 weighted tangent 提供可识别的 arrive / leave tangent 与 weight；单独准备 `FCurveKeyHandle InvalidHandle;`。 |
| 期望行为 | 有效 key 的 `InterpMode`、`TangentMode`、`TangentWeightMode`、arrive / leave tangent、arrive / leave tangent weight 与脚本设定一致；`SetDefaultValue` 与 pre / post infinity extrapolation 修改后能在 native `FloatCurve` 上直接读到；`GetValueRange()` / `GetValueRange_Double()` 返回与实际 key 值一致；对 `InvalidHandle` 调用 `SetKeyUserTangents()` / `SetKeyUserTangentWeights()` 不崩溃且不会改写已有 key。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-22：Actor 相对变换 fallback 与 local / world 增量语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` |
| 关联函数 | `SetActorRelativeLocation` / `GetActorRelativeLocation` / `SetActorRelativeRotation` / `GetActorRelativeRotation` / `SetActorRelativeTransform` / `GetActorRelativeTransform` / `SetActorQuat` / `GetActorQuat` / `AddActorLocalOffset` / `AddActorLocalRotation` / `AddActorWorldOffset` / `AddActorWorldRotation` |
| 现有测试覆盖 | 完全无测试；当前 actor 建议只覆盖 location / rotation round-trip、sweep 和 attach，没有触达相对变换、quat 和 local / world delta 语义 |
| 风险评估 | 这些 helper 属于高频 Actor API，一旦 relative / local / world 空间被混淆，脚本驱动的移动和挂点逻辑会稳定产生错误结果；同时 `GetActorRelative*` 在无 root component 时的 zero / identity fallback 目前完全没有防线。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ActorRelativeTransformSemantics` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 场景描述 | 在独立 world 中创建 parent actor 与 child actor，并给 child 配置已注册 root component 后先附着到 parent，使 relative 与 world transform 明确分离。脚本先做 relative location / rotation / transform round-trip，再执行 `SetActorQuat` / `GetActorQuat` 和 `AddActorLocalOffset` / `AddActorLocalRotation` / `AddActorWorldOffset` / `AddActorWorldRotation`；最后再对一个明确没有 root component 的 actor 调用 `GetActorRelativeLocation()` / `GetActorRelativeRotation()` / `GetActorRelativeTransform()`。 |
| 输入/前置 | parent 使用非零 location 与 yaw；child 初始相对 transform 也使用非零值；另准备一个移除 root component 的测试 actor 作为 fallback case。 |
| 期望行为 | `GetActorRelativeLocation()` / `GetActorRelativeRotation()` / `GetActorRelativeTransform()` 返回与 native 直接读取的 root 相对值一致；`GetActorQuat()` 与 `GetActorRotation().Quaternion()` 一致；`AddActorLocalOffset()` 按 actor 局部轴移动，而 `AddActorWorldOffset()` 按世界轴移动，两者最终 world position 不同且与 native 调用结果一致；无 root actor 的三个 relative getter 分别返回 `FVector::ZeroVector`、`FRotator::ZeroRotator`、`FTransform::Identity`。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-13 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-08 13:59)

### 一、现有测试问题

#### Issue-14：`MathExtendedCompat` 名称覆盖了“扩展 Math helper”，但完全没有触达 `AngelscriptMathLibrary.h` 的高频 API

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MathExtendedCompat` |
| 行号范围 | 26-155 |
| 问题描述 | 该用例的脚本体只覆盖了 `Math::RandHelper`、`Math::IsPowerOfTwo`、`Math::VRand`、`Math::ClampAngle`、`Math::CubicInterp`、`Math::LinePlaneIntersection` 等另一批 helper，却没有调用 `AngelscriptMathLibrary.h` 中本轮更关键的 `Math::Wrap`、`Math::SinCos`、`Math::Modf`、`Math::LerpShortestPath`、`Math::RInterpShortestPathTo`、`Math::RInterpConstantShortestPathTo`、`Math::TInterpTo`、`Math::LineBoxIntersection`、`Math::MoveTowards`，也没有触达 `FTransform::TransformRotation` / `InverseTransformRotation` 这组同头文件导出的 API。结果是，这个名字很宽的 compat 测试会给人“Math 函数库已有行为覆盖”的错觉，但对真正高频、最容易出数值语义回归的 helper 没有任何保护。 |
| 影响 | `AngelscriptMathLibrary` 目前会被一个无关 helper 聚合测试“借名覆盖”，搜索测试名或粗看模块统计时都容易误判为已有 math 回归防线；一旦 shortest-path、wrap、modf、transform rotation 等函数回归，当前自动化仍会稳定给出绿灯。 |
| 修复建议 | 不要继续让 `MathExtendedCompat` 充当“大而全” smoke file。新增 dedicated `AngelscriptMathFunctionLibraryTests.cpp`，把 `Wrap` / `SinCos` / `Modf` / shortest-path interp / `TInterpTo` / `MoveTowards` / transform rotation 分成 2 到 3 个单职责用例，分别断言精确返回值或与 native 结果一致；同时在现有 `MathExtendedCompat` 注释或测试名里明确它只覆盖随机数和通用 utility，避免继续误导 coverage 判断。 |

### 二、需要新增的测试

#### NewTest-23：shortest-path 插值与 `FTransform` 旋转 helper 语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` |
| 关联函数 | `LerpShortestPath` / `RInterpShortestPathTo` / `RInterpConstantShortestPathTo` / `TInterpTo` / `MoveTowards` / `TransformRotation` / `InverseTransformRotation` |
| 现有测试覆盖 | `MathExtendedCompat` 只覆盖随机数和通用 utility helper，这组 shortest-path / transform rotation / fixed-step helper 完全无直接测试 |
| 风险评估 | 一旦插值走成长弧、`InterpSpeed <= 0` 的特殊语义回归，或 `TransformRotation` / `InverseTransformRotation` 不再互逆，脚本里的摄像机、朝向、插值移动和动画过渡会持续给出“能跑但方向错误”的结果 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.MathShortestPathAndTransformSemantics` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` |
| 场景描述 | 脚本用跨越 `+180/-180` 边界的 rotator 调用 `LerpShortestPath`、`RInterpShortestPathTo` 和 `RInterpConstantShortestPathTo`，再对一个带非零 rotation 的 `FTransform` 做 `TransformRotation` / `InverseTransformRotation` round-trip；最后用 `MoveTowards` 和 `TInterpTo` 验证 fixed-step 与零速度分支。 |
| 输入/前置 | 使用 `A = FRotator(0, 170, 0)`、`B = FRotator(0, -170, 0)` 区分短弧/长弧；准备 `CurrentTransform = FTransform(FRotator(0, 90, 0), FVector(10, 0, 0))`、`TargetTransform = FTransform(FRotator(0, 180, 0), FVector(20, 0, 0))`；`MoveTowards` 采用 `StepSize = 3` 与 `StepSize = 20` 两组数据。 |
| 期望行为 | `LerpShortestPath(A, B, 0.5)` 的 yaw 应接近 `180/-180` 而不是 `0`；两个 shortest-path interp helper 的结果与 native `FQuat::Slerp` / `FMath::QInterp*` 一致；`TInterpTo(..., InterpSpeed = 0)` 直接返回 `TargetTransform`，正速度分支与 native blend 结果一致；`InverseTransformRotation(Transform, TransformRotation(Transform, R))` 回到原始 rotator；`MoveTowards` 小步长前进固定距离、大步长夹到目标点。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` |
| 优先级 | P0 |

#### NewTest-24：`AttachToComponent` 默认语句 guard 与推荐 attach 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` |
| 关联函数 | `AttachToComponent` |
| 现有测试覆盖 | 仅有运行时附着语义建议；`FUObjectThreadContext::IsInConstructor` 这条显式报错分支完全无测试 |
| 风险评估 | 如果 guard 被删掉、错误消息漂移，脚本作者会在 `default` 语句里静默建立非法层级，直到 CDO 构建或 editor 预览阶段才以更难定位的方式爆炸 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ComponentAttachDefaultStatementGuard` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp` |
| 场景描述 | 先用一份故意错误的脚本在 `default` 语句中调用 `Child.AttachToComponent(RootScene)`，再用一份对照脚本改成 `UPROPERTY(DefaultComponent, Attach = RootScene)` 的推荐写法。前者应产生稳定错误，后者应成功生成并保持层级。 |
| 输入/前置 | 通过 `CompileModuleWithResult` 编译错误脚本与正确脚本；正确脚本在 actor 上声明 root component 和 child component；错误脚本触发 `AttachToComponent` 的 constructor path。 |
| 期望行为 | 错误脚本返回 `ECompileResult::Error`，并出现包含 `Calling AttachToComponent in a default statement is invalid.` 与 `Attach =` / `AttachSocket =` 指引的诊断；正确脚本编译成功，spawn 后 child 的 `GetAttachParent()` 等于 `RootScene`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `CompileModuleWithResult` + `SpawnScriptActor` |
| 优先级 | P1 |

#### NewTest-25：Actor editor construction helper 的重跑与标志位同步

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` |
| 关联函数 | `SetbRunConstructionScriptOnDrag` / `RerunConstructionScripts` |
| 现有测试覆盖 | 完全无测试；现有 actor 建议都集中在 transform / attach，editor-only construction helper 仍是空白 |
| 风险评估 | editor 中脚本 actor 的构造结果一旦不能重跑或拖拽标志不同步，组件预览、派生属性和设计期调试都会长期失真，而且回归只会在人工编辑流程里暴露 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ActorConstructionEditorHelpers` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 场景描述 | 脚本 actor 在 `ConstructionScript()` 中递增 `ConstructionCount` 并写一个可观察的派生属性；另提供 `RunEditorHelpers()`，先调用 `SetbRunConstructionScriptOnDrag(false)`，再调用 `RerunConstructionScripts()` 并返回本次 construction 增量。 |
| 输入/前置 | 在 editor world 中生成脚本 actor，记录初始 `ConstructionCount`；通过 reflected call 执行 `RunEditorHelpers()`；必要时在 `ConstructionScript()` 中同时更新一个组件名或派生整数，保证能观测到重跑确实发生。 |
| 期望行为 | 初次 spawn 后 `ConstructionCount >= 1`；`RunEditorHelpers()` 返回的增量为 `1`，说明 `RerunConstructionScripts()` 只额外重跑一次；native 侧 `Actor->bRunConstructionScriptOnDrag` 变为 `false`，且 construction 中写入的派生属性与最新计数一致。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + reflected `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-14 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingScenario: 1 |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-08 14:14)

### 一、现有测试问题

#### Issue-15：`AngelscriptEngineParityTests.cpp` 已经膨胀成跨域 parity megafile，违反单文件单职责约束

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.WorldCollisionCompile` / `Angelscript.TestModule.Parity.SoftReferenceCompile` / `Angelscript.TestModule.Parity.LevelStreamingCompile` / `Angelscript.TestModule.Parity.RuntimeCurveLinearColorCompile` / `Angelscript.TestModule.Parity.HitResultCompile` |
| 行号范围 | 1-589 |
| 问题描述 | 这个文件已经堆到 589 行，把 collision、soft reference、widget、level streaming、curve、hit result 等互不相干的 parity case 全塞在一起，直接超过规则要求的单文件 300-500 行。更糟的是，本轮前面已经记录过的多条弱断言都集中在这一个文件里，说明“把不同函数库都塞进 parity smoke file”已经变成结构性反模式：作者更容易继续补 compile-only 片段，而不是给每个函数库写带 world fixture、断言和 cleanup 的专用行为测试。 |
| 影响 | 文件过大且职责混杂会显著提高审查成本和冲突概率，也会持续诱导新覆盖沿用 compile smoke 路线，导致函数库真正的运行时语义测试长期缺位；单看文件存在与测试数量时，还会误判成这些函数库已经有成体系覆盖。 |
| 修复建议 | 把该文件按函数库域拆开，至少拆成 `WorldCollision`、`SoftReference`、`LevelStreaming`、`Curve/HitResult` 等专用测试文件，并把每个文件控制在 300-500 行以内；compile-surface smoke 可以保留，但要紧邻对应的 runtime 行为用例，而不是继续塞进统一 parity megafile。对需要 world / async / delegate 的 helper，直接迁到 `Bindings/` 或对应功能目录下的 scenario 文件。 |

### 二、需要新增的测试

#### NewTest-26：异步存档回调的 game-thread 契约与单次交付

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h` |
| 关联函数 | `AsyncSaveGameToSlot` / `AsyncLoadGameFromSlot` |
| 现有测试覆盖 | 完全无测试；源码注释明确承诺 completion delegate 在 game thread 回调，但当前没有任何自动化去固定这个线程契约 |
| 风险评估 | 如果 wrapper 把回调落到 worker thread、重复触发，脚本侧 receiver 很容易在错误线程改 UObject 状态，或者把存档副作用执行两次；这类问题通常只会在高并发或真机流程里暴露，定位成本很高 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.AsyncSaveLoadCallbackThreadAffinity` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayFunctionLibraryTests.cpp` |
| 场景描述 | 定义一个 native receiver UObject，暴露 `OnSaveComplete` / `OnLoadComplete` 两个 `UFUNCTION()`，在函数体里记录 `SlotName`、`UserIndex`、调用次数以及 `IsInGameThread()`。脚本先调用 `AsyncSaveGameToSlot`，在 save callback 成功后再调用 `AsyncLoadGameFromSlot`，形成可确定的串行链路。 |
| 输入/前置 | 使用唯一 `SlotName`、固定 `UserIndex`、带可识别字段的 `USaveGame` 派生对象；receiver 持有 `SaveCount`、`LoadCount`、`bSaveOnGameThread`、`bLoadOnGameThread`、`LoadedValue`；测试主体使用 `FEvent` 或等价同步手段等待 load callback 完成。 |
| 期望行为 | `OnSaveComplete` 与 `OnLoadComplete` 都只触发一次，且两个回调内部 `IsInGameThread()` 都为 `true`；save/load 回调拿到的 `SlotName` 与 `UserIndex` 与输入一致；load 回调收到的 `SaveGameObject` 非空且字段值与保存前完全一致。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `ASTEST_BUILD_MODULE` + native receiver UObject + `FEvent` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-15 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 01:00)

### 一、现有测试问题

#### Issue-42：`ScriptExamples.Math` 只是编译型示例 smoke，既不执行脚本也没有覆盖 `AngelscriptMathLibrary` 的目标高频 API

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleMathTest.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptExamples.Math` |
| 行号范围 | 9-45 |
| 问题描述 | 这条示例测试把 `Example_Math.as` 交给 `RunScriptExampleCompileTest(...)`，而该 helper 在 `AngelscriptScriptExampleTestSupport.cpp` 16-59 行只会把脚本文本喂给 `CompileAnnotatedModuleFromMemory(...)`，不会执行 `ExecuteExampleMath()`。结果是脚本里的 `check(...)` 从未运行，`Math::Abs` / `Min` / `Max` / `Clamp` / `Sin` / `RandRange` 只是“能编译”；与此同时，示例正文也完全没有触达本轮目标 `AngelscriptMathLibrary.h` 中更关键的 `Wrap*`、`SinCos`、`Modf`、shortest-path interp、`MoveTowards`、`TransformRotation` 等 API。换言之，这条名字很宽的 `Math` 示例用例既没有提供运行时语义断言，也没有给本轮优先审查的 Math function library 带来实质覆盖。 |
| 影响 | 结合现有 `MathExtendedCompat` 等宽口径用例，仓库表面上会出现多条“Math”测试同时为绿的错觉，但真正高频的函数库 API 仍可能没有被执行级断言保护；一旦维护者按测试名或目录粗看覆盖度，很容易误把示例编译 smoke 当成函数库行为覆盖。 |
| 修复建议 | 保留这条示例测试作为文档/示例编译 smoke 可以，但不要再让它承担 Math function library 覆盖职责。至少应在测试名或注释中明确“只验证示例可编译”；函数库语义覆盖则应落到 dedicated runtime 用例，按现有 math 计划拆成 `Wrap/Modf`、shortest-path interp、transform rotation 等单职责测试，并在这些用例里真正执行脚本结果与 native/reference 值对照。 |

### 二、需要新增的测试

#### NewTest-74：`PushInputComponent` / `PopInputComponent` 的优先级排序与去重契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` |
| 关联函数 | `PushInputComponent` / `PopInputComponent` |
| 现有测试覆盖 | 现有 `InputMappingRoundTrip` 建议只把 `PushInputComponent` / `PopInputComponent` 当成 setup 步骤，`InputBindingAndEngineDefinedMappings` 聚焦 binding array，`InputNullInputGuards` 只覆盖 null path；`APlayerController` input stack 的优先级排序、重复 push 去重和 pop 后剩余顺序仍完全没有被固定 |
| 风险评估 | 这两个 wrapper 直接转发 `APlayerController::PushInputComponent` / `PopInputComponent`。如果脚本桥在参数传递、对象身份或宿主分派上回归，最先出问题的不是“组件在不在栈里”，而是输入处理顺序被悄悄打乱或重复 push 生成重复条目，最终表现为高优先级 UI/调试输入被游戏输入吞掉，且现有建议很难定位到 stack 语义本身 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.InputComponentStackOrdering` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp` |
| 场景描述 | 在带 `APlayerController` 的 fixture 中创建三个 `UInputComponent`：`LowPriority`、`MidPriority`、`HighPriority`，分别设置不同 `Priority`。脚本按“先低后高、再重复 push `LowPriority`”的顺序调用 `PushInputComponent`，native 侧每一步都调用 `PlayerController->BuildInputStack(InputStack)` 和 `IsInputComponentInStack(...)` 取快照；随后脚本对中间组件执行 `PopInputComponent`，再次读取 stack。 |
| 输入/前置 | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 创建 world / controller；native 侧显式关闭无关 pawn / level-script 输入干扰，或在断言中只截取这三枚测试 component；三枚 `UInputComponent` 分别给出稳定 `Priority` 值，例如 `0 / 5 / 10`；脚本侧使用同一 controller 句柄重复 push / pop。 |
| 期望行为 | 每次 `PushInputComponent` 后，`IsInputComponentInStack` 对目标组件返回 `true`；`BuildInputStack` 结果中三枚测试 component 只各出现一次，证明重复 push 不会制造重复条目；排序上高优先级 component 必须位于低优先级 component 之后，从而在 `ProcessInputStack` 自顶向下处理时拥有更高优先级；`PopInputComponent(MidPriority)` 后仅该组件消失，其余两枚保持原有相对顺序不变。若项目希望进一步固定“相同优先级保持 stable order”，可再补两枚同优先级 component，断言 `BuildInputStack` 顺序与 native `Algo::StableSort` 行为一致。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + native `BuildInputStack` / `IsInputComponentInStack` 快照 |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-42 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:41 真正尾部定位-EOF6)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-10 00:41)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧尾部定位段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-39 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:32)

### 一、现有测试问题

#### Issue-40：`SoftObjectPtrCompat` / `TSoftClassPtrCompat` 在无 reset 的 shared engine 上复用固定模块名，soft-reference compat 结果带顺序相关风险

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SoftObjectPtrCompat`；`Angelscript.TestModule.Bindings.TSoftClassPtrCompat` |
| 行号范围 | 84-193；186-273 |
| 问题描述 | 两条 dedicated soft-reference compat 用例都使用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE`，并把模块名固定成 `ASSoftObjectPtrCompat` / `ASTSoftClassPtrCompat`。而测试宏明确写着 shared engine 是“`reused across tests, no reset`”，`ASTEST_BEGIN_SHARE` 也会“`leave shared-engine module state intact`”（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` 42-49、97-104 行）。与此同时，`BuildModule(...)` 只负责编译并返回 `asIScriptModule*`，不会自动 `DiscardModule(...)`（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` 535-593 行）。这意味着同进程重复运行或相邻 case 复用 shared engine 时，soft-reference compat 可能命中前一轮残留的模块 / 类型状态，而不是本轮刚编译的脚本。 |
| 影响 | soft-reference 现有覆盖本来就偏弱，再叠加共享模块残留后，`SoftObjectPtrCompat` / `TSoftClassPtrCompat` 的通过信号会混入顺序相关噪声：真正的绑定回归可能被旧模块掩盖，反过来旧状态污染也可能制造假红灯。 |
| 修复建议 | 把两条用例统一切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `ASTEST_CREATE_ENGINE_CLONE()`；如果必须保留 shared engine，则至少在 case 结束前显式 `Engine.DiscardModule(TEXT("ASSoftObjectPtrCompat"))` / `Engine.DiscardModule(TEXT("ASTSoftClassPtrCompat"))`，并给模块名附唯一 suffix，确保每次执行都绑定到本轮 fresh module。 |

### 二、需要新增的测试

#### NewTest-71：`TSoftObjectPtr.LoadAsync` 对 actor/component 子类型的拒绝分支

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` |
| 关联函数 | `FOnSoftObjectLoaded` / `TSoftObjectPtr<T>.LoadAsync` |
| 现有测试覆盖 | 现有 `SoftObjectPtrCompat` 只覆盖 `UTexture2D` happy path，已有 `SoftReferenceAsyncDelegates` 建议也只覆盖成功/失败加载；`Bind_TSoftObjectPtr.cpp` 488-499 行对 `AActor` / `UActorComponent` 子类型的显式 `Throw(...)` 分支完全没有测试 |
| 风险评估 | 这条分支是 soft object async load 的明确安全护栏。若绑定回归为“不再抛错”“仍触发 callback”或抛错文案漂移，脚本侧会把本应被拒绝的 actor/component 软引用当成可加载资源处理，最常见结果就是等待永不触发的 callback，或在错误场景里静默继续执行。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.SoftReferenceRejectsActorAndComponentAsyncLoad` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp` |
| 场景描述 | 在测试 world 中创建一个 `AActor` 和一个已注册的 `USceneComponent`，脚本分别构造 `TSoftObjectPtr<AActor>` 与 `TSoftObjectPtr<USceneComponent>`，给两条 `LoadAsync(...)` 都绑定 native receiver 的 `FOnSoftObjectLoaded`。两条调用分别放进独立脚本入口，避免一条 runtime exception 吞掉另一条断言。 |
| 输入/前置 | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 创建 world；native receiver 记录 callback 次数和最后一次 payload；测试主体对两条调用分别 `AddExpectedError("Actor soft references cannot be loaded, stream the level in instead.")`、`AddExpectedError("Component soft references cannot be loaded, stream the level in instead.")`，并按 `Native/AngelscriptASSDKRuntimeTests.cpp` 的 runtime-exception 模式执行脚本入口，固定异常结果。 |
| 期望行为 | 两条调用都必须以可诊断的脚本异常结束，而不是静默成功；异常文本分别精确包含 actor/component 对应文案；receiver 的 callback 次数始终保持 `0`，确认拒绝分支不会在抛错后又错误执行 `OnLoaded.ExecuteIfBound(...)`；测试进程本身不得崩溃。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + native receiver UObject + `AddExpectedError` + runtime-exception 执行 helper（参照 `Native/AngelscriptASSDKRuntimeTests.cpp`） |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-40 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-10 00:34)

### 一、现有测试问题

本轮无新增。

### 二、需要新增的测试

#### NewTest-72：`LoadAsync` 对已加载 soft reference 的“立即且仅一次”回调契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` |
| 关联函数 | `FOnSoftObjectLoaded` / `FOnSoftClassLoaded` / `TSoftObjectPtr<T>.LoadAsync` / `TSoftClassPtr<T>.LoadAsync` |
| 现有测试覆盖 | 现有 `SoftReferenceAsyncDelegates` 建议聚焦“存在路径成功加载”和“缺失路径失败加载”，但没有任何 case 固定 502-509、627-631 行的 already-loaded 立即回调分支 |
| 风险评估 | 绑定实现当前对已加载对象/类会直接 `OnLoaded.ExecuteIfBound(...)` 后返回。如果回归成延后回调、重复回调或 payload 被清空，脚本里依赖“调用后立即可见副作用”的资源缓存、初始化链和 UI 预热逻辑会出现时序漂移，而普通 latent success-path 用例未必能拦住。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.SoftReferenceAsyncAlreadyLoadedCallbacks` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp` |
| 场景描述 | native 侧先创建一个已加载的 transient `UTexture2D`，并准备 `AActor::StaticClass()` / `ACameraActor::StaticClass()` 这种已解析 class。脚本分别构造 `TSoftObjectPtr<UTexture2D>` 与 `TSoftClassPtr<AActor>`，绑定两个 receiver 的 `OnObjectLoaded` / `OnClassLoaded`，然后在同一个脚本函数里调用 `LoadAsync(...)` 后立刻检查 receiver 的 `CallCount`、payload 和错误码。函数返回后 native 再等待一个短窗口，确认不会发生第二次迟到回调。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN`；native receiver UObject 记录 `CallCount`、最后一次 payload 指针和 `bCalledBeforeFunctionReturn`；对象路径与类路径都来自当前已加载对象，而不是额外触发 package load。 |
| 期望行为 | `TSoftObjectPtr<UTexture2D>.LoadAsync(...)` 与 `TSoftClassPtr<AActor>.LoadAsync(...)` 在脚本函数返回前都已恰好触发一次 callback；object/class payload 分别精确等于输入的已加载对象和类；短窗口 latent wait 后 `CallCount` 仍保持 `1`，确认不会“立即回调一次后再异步补一次”。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` + native receiver UObject + 短窗口 latent wait |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:41)

### 一、现有测试问题

#### Issue-39：`DeprecationsMetadata` 只验证外部 Niagara API，`FunctionLibraries` 自身的 deprecated helper 完全无覆盖

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.DeprecationsMetadata` |
| 行号范围 | 629-643 |
| 问题描述 | 当前用例唯一读取的对象是 `/Script/Niagara.NiagaraComponent:SetNiagaraVariableLinearColor`，随后只断言这条外部引擎 API 带有 `DeprecatedFunction` 和固定的 `DeprecationMessage`。它没有检查本模块真正需要守住的 `FunctionLibraries` deprecation surface：`UAngelscriptMathLibrary::WrapInt`、`UAngelscriptComponentLibrary::GetShapeCenter`、`UAngelscriptHitResultLibrary::SetBlockingHit` 在源码中都明确声明了 `DeprecatedFunction` 元数据与迁移文案，但当前 parity 用例完全不读取这些 `UFunction`。结果是，只要 Niagara 那条无关样本还在，哪怕插件自己的 deprecated helper 丢掉标记、文案漂移，测试也会继续稳定为绿。 |
| 影响 | `FunctionLibraries` 的弃用提示属于脚本 authoring contract 的一部分；一旦这些 metadata 回退，调用方会失去 API 迁移提示和 IDE/文档层的警告信号，而当前测试套件不会给出任何回归定位。 |
| 修复建议 | 保留 Niagara 样本作为“外部 UE 元数据仍可读”的基线也可以，但不要再让它代表整个 deprecation contract。新增 dedicated FunctionLibrary metadata 用例，直接通过 `UAngelscriptMathLibrary::StaticClass()->FindFunctionByName(TEXT("WrapInt"))`、`UAngelscriptComponentLibrary::StaticClass()->FindFunctionByName(TEXT("GetShapeCenter"))`、`UAngelscriptHitResultLibrary::StaticClass()->FindFunctionByName(TEXT("SetBlockingHit"))` 逐一断言 `HasMetaData(DeprecatedFunction)` 与准确的 `DeprecationMessage`；同时把这条检查从 parity megafile 拆到独立 metadata 测试文件，避免继续加重 `AngelscriptEngineParityTests.cpp`。 |

### 二、需要新增的测试

#### NewTest-70：`FunctionLibraries` deprecated helper 的 metadata 合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` |
| 关联函数 | `WrapInt` / `GetShapeCenter` / `SetBlockingHit` |
| 现有测试覆盖 | 只有 `Angelscript.TestModule.Parity.DeprecationsMetadata`，且它只检查 `/Script/Niagara.NiagaraComponent:SetNiagaraVariableLinearColor`；FunctionLibrary 自身的 deprecated helper 完全无 dedicated 断言 |
| 风险评估 | 如果 wrapper 上的 `DeprecatedFunction` 或 `DeprecationMessage` 丢失，脚本侧会静默失去迁移提示和 IDE 级警告；这类回归通常不会在运行时立刻炸掉，但会持续放大错误 API 的使用面 |
| 建议测试名 | `Angelscript.TestModule.Parity.FunctionLibraryDeprecationsMetadata` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryMetadataParityTests.cpp` |
| 场景描述 | 直接反射 `UAngelscriptMathLibrary`、`UAngelscriptComponentLibrary`、`UAngelscriptHitResultLibrary` 三个类，分别定位 `WrapInt`、`GetShapeCenter`、`SetBlockingHit`，检查这三条 helper 都带有 deprecated 标记和准确的迁移文案。 |
| 输入/前置 | 仅需常规 automation test 反射环境；准备 `DeprecatedFunction`、`DeprecationMessage` 两个 `FName` 常量，并通过 `StaticClass()->FindFunctionByName(...)` 获取目标 `UFunction`。 |
| 期望行为 | `WrapInt` 必须带 `DeprecatedFunction`，且 message 精确等于 `Wrapping integers is inclusive, and returns unintuitive values. Use Math::WrapIndex for the natural behavior.`；`GetShapeCenter` 的 message 必须是 `Get Bounds.Origin instead`；`SetBlockingHit` 的 message 必须是 `Assign bBlockingHit instead`。三条 helper 任一若丢失 deprecated flag、message 漂移或函数无法反射定位，测试必须直接失败。 |
| 使用的 Helper | `StaticClass()->FindFunctionByName` / `TestTrue` / `TestEqual` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-39 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:04)

### 一、现有测试问题

#### Issue-37：`FunctionLevelScriptMethodUsesFirstParameterAsMixin` 只验证 synthetic coverage library，没有锁定真实 FunctionLibrary mixin 的签名语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.FunctionLevelScriptMethodUsesFirstParameterAsMixin` |
| 行号范围 | 603-644 |
| 问题描述 | 这条用例只从 `UAngelscriptUhtCoverageTestLibrary` 取一个专门造出来的 `GetCoverageValue`，然后检查 `FAngelscriptFunctionSignature` 是否把首参移出公开签名、把函数变成脚本成员、并保留 `const` 与脚本名。整个断言链没有把任何生产中的 FunctionLibrary mixin 送进同一条路径，例如 `UAngelscriptFrameTimeMixinLibrary::AsSeconds`、`UAngelscriptWidgetMixinLibrary::GetRenderTransform` 或各类 `GameplayTag*MixinLibrary`。结果是，只要 synthetic fixture 仍然满足理想化 `ScriptMethod` 规则，真实函数库一旦在 class-level `ScriptMixin`、宿主类型、`const` 成员声明或暴露脚本名上发生漂移，这条测试仍会稳定为绿。 |
| 影响 | 当前 bind-config 套件会给“首参转成员方法”这条函数库注册链路提供过强的绿灯信号，却拦不住真实 mixin API 的签名回退；一旦 `FQualifiedFrameTime.AsSeconds()`、`UWidget.GetRenderTransform()` 这类生产入口不再以预期成员方法形态暴露，CI 仍可能全部通过。 |
| 修复建议 | 保留 synthetic coverage library 作为最小机制 smoke，但追加至少 2 个真实 FunctionLibrary mixin 样本进入同一断言链。建议直接为 `UAngelscriptFrameTimeMixinLibrary::AsSeconds` 和 `UAngelscriptWidgetMixinLibrary::GetRenderTransform` 构造 `FAngelscriptFunctionSignature`，分别断言它们在脚本侧表现为 `FQualifiedFrameTime` / `UWidget` 的成员方法、首参不再出现在公开参数列表、`const` 与返回类型保持正确；如果项目还要继续依赖 class-level `ScriptMixin`，再补一条 `GameplayTagQueryMixinLibrary::Matches` 作为结构体 mixin 对照。 |

---

## 测试审查 (2026-04-09 12:20 补记)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 12:19)` 段落；这里仅补一个尾部定位，避免重复抄写相同内容。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-28 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 01:26)

### 一、现有测试问题

#### Issue-44：`ActorTransformRoundTrip` 只比较 sweep 布尔值和终点位置，没有钉住 `SweepHitResult` 的 payload 语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 测试名 | `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| 行号范围 | 170-190；285-313 |
| 问题描述 | 脚本阶段只把 `SetActorLocation(..., true, SweepHit, false)` 的 `bool` 返回值、`SweepHit.bBlockingHit` 和 `TargetActor.GetActorLocation()` 存进 `bScriptSweepReturn`、`bScriptSweepBlockingHit`、`ScriptSweepLocation` 三个属性；C++ 侧也只把这三项与 native lane 对照。整个用例没有保存或断言 `SweepHit.GetActor()`、`SweepHit.GetComponent()`、`ImpactPoint`、`ImpactNormal`、`Time`、`bStartPenetrating` 等真正驱动命中后续逻辑的字段。只要 wrapper 仍返回正确的 bool、命中标志和停止位置，即使 `SweepHitResult` 指向了错误 blocker、漏填 component 或 penetration 元数据，这条测试仍会全绿。 |
| 影响 | `SetActorLocationAdvanced` 当前看似已有 dedicated runtime 覆盖，实际上只固定了“能挡住且停在正确位置”，没有固定脚本最常消费的 hit payload。脚本若依赖返回的 actor/component、impact 数据做交互、伤害或表面判断，这类回归会直接漏检。 |
| 修复建议 | 在现有 driver 上继续追加 `UPROPERTY()`，把 `SweepHit.GetActor()`、`SweepHit.GetComponent()`、`ImpactPoint`、`ImpactNormal` 和 `bStartPenetrating` 回传给 C++；native mirror lane 同步执行一次 `K2_SetActorLocation(...)`，逐项比对 blocker actor/component identity 与向量字段。若担心单 case 继续膨胀，可把这部分拆成同文件下第二条 `ActorSweepHitPayloadMirror` 用例，但优先保持在当前 328 行文件内补齐行为断言，而不是再退回 compile smoke。 |

#### Issue-45：`ActorTransformRoundTrip` 的附着断言让 `KeepWorld` scale 规则处于不可观测状态

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 测试名 | `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| 行号范围 | 200-210；315-353 |
| 问题描述 | `UAngelscriptActorLibrary::AttachToActor` / `AttachToComponent` 的实现不是简单把一个 `AttachmentRule` 传给全部轴，而是明确把 location/rotation 设为传入 `AttachmentRule`，同时把 scale 固定成 `EAttachmentRule::KeepWorld`。但当前测试对 `AttachToActor` / `AttachToComponent` 的断言只有“父子关系正确”和“child location 被 snap 到 parent”，并且 parent/child 都沿用默认单位 scale。这样即使 wrapper 把 scale 规则错误改成 `SnapToTarget`、`KeepRelative`，或者附着后 world scale 被悄悄改写，现有 case 也很可能继续通过，因为 scale 从头到尾没有被制造出可见差异。 |
| 影响 | actor attach helper 当前最容易出错、又最难凭肉眼发现的就是 scale 轴契约。脚本一旦依赖 attach 后保持 child world scale 不变，现有 dedicated 测试不会为此提供任何红灯，UI 挂件、特效挂点和运行时组合 actor 的尺寸回归都可能直接漏过去。 |
| 修复建议 | 在同一文件里把 parent 和 child 的 root scale 设成明显不同的非单位值，例如 parent `(2,3,4)`、child `(1.5,0.5,2)`，并在 `AttachToActor` / `AttachToComponent` 前后同时记录 world scale 与 relative scale；C++ 侧再用 mirror actor/native attach 做对照，明确断言“location/rotation 遵守传入 rule，但 scale 保持 world 值不变”。如果还要进一步固定规则语义，可在同文件追加一小段 `AttachmentRule = KeepRelative` 的子场景，确认只有 location/rotation 跟随 rule 变化，scale 仍保持 `KeepWorld`。 |

### 二、需要新增的测试

#### NewTest-76：`SetActorLocationAdvanced` 的 `SweepHitResult` payload mirror

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` |
| 关联函数 | `SetActorLocationAdvanced` |
| 现有测试覆盖 | `ActorTransformRoundTrip` 只固定 bool 返回值、`bBlockingHit` 和最终位置；`SweepHitResult` 的 actor/component identity、impact 数据和 penetration 标志都没有任何 dedicated 断言 |
| 风险评估 | 脚本最常把 `SweepHitResult` 继续传给交互、伤害、表面材质或碰撞恢复逻辑。若 hit payload 漂移到错误对象或丢失 penetration 信息，现有自动化仍会全绿，线上才会暴露出“命中了，但命中对象不对”的高成本问题 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ActorSweepHitPayloadMirror` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 场景描述 | 复用当前 actor mirror-lane 夹具，但在 script lane 与 native lane 都执行两段 sweep：1）从 blocker 外侧扫入，命中明确的 `UBoxComponent`；2）从已重叠状态再次 sweep，覆盖 `bStartPenetrating`。脚本把 `SweepHit.GetActor()`、`GetComponent()`、`ImpactPoint`、`ImpactNormal`、`Time`、`bStartPenetrating` 写回 `UPROPERTY()`，C++ 逐项与 native mirror lane 对照。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `FActorTestSpawner`；为 blocker 指定稳定名字并保留 root `UBoxComponent` 指针；第二段 initial-overlap 子场景让 mover 起始位置与 blocker 明确重叠，避免依赖随机物理结果。 |
| 期望行为 | 命中 sweep 上 script/native 的 `GetActor()` 都指向 blocker actor，`GetComponent()` 都指向 blocker root，`ImpactPoint` / `ImpactNormal` / `Time` 在容差内一致；initial-overlap 子场景里 `bStartPenetrating` 必须与 native 一致，且不会因为 payload 丢失而退化成只剩 `bBlockingHit=true`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `FActorTestSpawner` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-77：附着 helper 的 `KeepWorld` scale 契约与非单位缩放场景

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` |
| 关联函数 | `AttachToActor` / `AttachToComponent` |
| 现有测试覆盖 | `ActorTransformRoundTrip` 只验证父子关系和 snap 后 world location；parent/child 都是单位 scale，完全看不出 wrapper 把 scale 轴固定成 `EAttachmentRule::KeepWorld` 的真实契约 |
| 风险评估 | 一旦附着后 world scale 被错误改成 parent scale、relative scale 或 snap 结果，脚本挂点对象会在运行时悄悄变形。现有用例因为 scale 全是 1，无法给这种回归任何信号 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ActorAttachmentKeepsWorldScale` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 场景描述 | 在 script lane 与 native mirror lane 中分别准备 non-uniform scaled parent 和 child：parent root 设 `(2,3,4)`，child root 设 `(1.5,0.5,2)`。脚本先执行 `AttachToActor(ParentActor)`，再重置 child 后执行 `AttachToComponent(ParentComponent)`；两个阶段都记录 attach 前后的 child world scale、relative scale、world rotation，并与 native mirror lane 对照。可选地追加一个 `AttachmentRule = KeepRelative` 子场景，固定“location/rotation 随 rule 变化，但 scale 仍保持 world”。 |
| 输入/前置 | 继续使用 `FActorTestSpawner` 创建带已注册 root `UBoxComponent` 的 actor；attach 前明确写入 non-unit scale 和 non-zero rotation，避免 scale/rotation 回归被单位值掩盖；若加入 `KeepRelative` 子场景，需在 attach 前缓存 child 相对 transform 供 native/script 双侧对照。 |
| 期望行为 | `AttachToActor` 与 `AttachToComponent` 后，script lane 的 child world scale 必须与 attach 前 world scale及 native mirror 结果一致，而不是被 parent scale 吞掉；parent 关系、world location 和 world rotation 仍与当前 snap 语义一致；`KeepRelative` 子场景下 relative location/rotation 保持不变，但 scale 仍按 `KeepWorld` 保持 world 值。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `FActorTestSpawner` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-44 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 12:34 补记)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 12:34)` 段落；这里仅补一个尾部定位，避免重复抄写相同内容。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:49 尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 12:49)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文被插入到旧定位段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-29 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |

---

## 测试审查 (2026-04-09 13:01 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:01)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文再次被插入到旧定位段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-30 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingEdgeCase: 1 |
 
---

## 测试审查 (2026-04-09 13:16 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:14)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧汇总段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 13:27 真正尾部定位-3)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:27)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧段落之前。

### 二、需要新增的测试

本轮无新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 2 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:36)

### 一、现有测试问题

本轮无新增。

### 二、需要新增的测试

#### NewTest-69：production `ScriptMixin` 签名与成员暴露形态

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h` |
| 关联函数 | `AsSeconds` / `GetRenderTransform` / `Matches` |
| 现有测试覆盖 | `FunctionLevelScriptMethodUsesFirstParameterAsMixin` 只覆盖 synthetic `GetCoverageValue`；现有 `FrameTimeAsSeconds`、`Widget`、`GameplayTagQuery` 建议都偏运行时语义，没有一条 dedicated bind-config 用例固定真实 production mixin 的 declaration 形态 |
| 风险评估 | 如果 class-level `ScriptMixin`、首参裁剪、成员 `const` 或 world-context 处理只在真实函数库上回归，synthetic bind-config 测试仍会全绿，而脚本 API 面会静默从成员方法退化成静态 helper 或错误签名 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.ProductionScriptMixinSignatures` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibrarySignatureTests.cpp` |
| 场景描述 | 创建 testing engine 后，分别解析 `UAngelscriptFrameTimeMixinLibrary::AsSeconds`、`UAngelscriptWidgetMixinLibrary::GetRenderTransform`、`UGameplayTagQueryMixinLibrary::Matches` 三个 `UFunction`，逐个构造 `FAngelscriptFunctionSignature`，再通过 `FAngelscriptBinds::BindGlobalGenericFunction` + `ModifyScriptFunction` 取回 `asCScriptFunction` 检查 declaration、`bStaticInScript`、公开参数数量、`hiddenArgumentIndex` 和 trait。 |
| 输入/前置 | `CreateTestingFullEngine` + `FAngelscriptEngineScope`；`HostType` 分别取 `FQualifiedFrameTime`、`UWidget`、`FGameplayTagQuery` 对应 `FAngelscriptType`；使用 no-op generic 绑定，避免依赖实际执行逻辑。 |
| 期望行为 | 三条 production 函数都必须表现为脚本成员方法而不是静态函数，即 `bStaticInUnreal == true` 且 `bStaticInScript == false`；`AsSeconds` 的公开参数列表为空、declaration 包含 `AsSeconds` 且带 `const`；`GetRenderTransform` 的公开参数列表为空，且不应带 hidden world-context 或 `asTRAIT_USES_WORLDCONTEXT`；`Matches` 只保留一个显式 `FGameplayTagContainer` 参数、declaration 带 `Matches` 和 `const`。任一函数若退回静态 helper 形态、首参仍暴露在公开签名里，或 world-context/trait 漂移，都必须直接失败。 |
| 使用的 Helper | `FAngelscriptEngineScope` / `FAngelscriptFunctionSignature` / `FAngelscriptBinds::BindGlobalGenericFunction` / `asCScriptFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:24)

### 一、现有测试问题

#### Issue-38：`CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` 没有覆盖 `SubsystemLibrary` 里最容易漂移的 production world-context 签名

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` |
| 行号范围 | 647-703 |
| 问题描述 | 这条用例只对 `UAngelscriptUhtCoverageTestLibrary::RequiresWorldContext` 和 `CallableWithoutWorldContext` 两个 synthetic fixture 构造 `FAngelscriptFunctionSignature`，再检查 `hiddenArgumentIndex` 与 `asTRAIT_USES_WORLDCONTEXT`。它没有把任何真实 FunctionLibrary 送进相同断言链，尤其没有覆盖 `SubsystemLibrary.h` 中的 `GetLocalPlayerSubsystemFromLocalPlayer`。该生产函数同时声明了 `Meta = (WorldContext = "WorldContextObject")`、显式 `UObject* WorldContextObject` 形参和真正参与实现的 `ULocalPlayer* LocalPlayer` 形参，但函数体只把 `LocalPlayer` 传给 `USubsystemBlueprintLibrary::GetLocalPlayerSubsystem(LocalPlayer, Class)`。也就是说，这是 production 里最容易出现 “错误隐藏 world-context 参数” 或 “trait 没清干净” 的场景；一旦签名生成对这条真实函数回归，当前 synthetic 测试仍会稳定通过。 |
| 影响 | bind-config 套件会错误地放行 `SubsystemLibrary` 的关键签名漂移，导致脚本侧 `GetLocalPlayerSubsystemFromLocalPlayer(...)` 可能带着多余 hidden 参数、错误 world-context trait 或错误公开参数列表进入发布分支；这类问题通常只会在脚本 API 生成后才暴露，定位成本明显更高。 |
| 修复建议 | 不要继续只用 synthetic coverage library 代表 production world-context 规则。保留现有两条 fixture 作为基线，同时把 `USubsystemLibrary::GetLocalPlayerSubsystemFromLocalPlayer` 纳入同一测试：直接构造 `FAngelscriptFunctionSignature`，再用 `BindGlobalGenericFunction` + `ModifyScriptFunction` 检查最终 declaration、`hiddenArgumentIndex` 和 `asTRAIT_USES_WORLDCONTEXT`。至少要固定三点：`ULocalPlayer` 仍作为显式公开参数保留；`WorldContextObject` 不应被错误隐藏成脚本必填前置；最终 script function 不应继续带 `asTRAIT_USES_WORLDCONTEXT`。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-38 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |
 
---

## 测试审查 (2026-04-09 13:27 真正尾部定位-2)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:27)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧段落之前。

### 二、需要新增的测试

本轮无新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 2 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:27 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:27)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧段落之前。

### 二、需要新增的测试

本轮无新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 2 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:27)

### 一、现有测试问题

#### Issue-32：`GameplayTagBindings` 三个 compat 用例在无 reset 的 shared engine 上复用固定模块名，存在跨测试泄漏风险

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagCompat`；`Angelscript.TestModule.Bindings.GameplayTagContainerCompat`；`Angelscript.TestModule.Bindings.GameplayTagQueryCompat` |
| 行号范围 | 25-95；98-188；191-287 |
| 问题描述 | 三条用例都使用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE`（27-28、100-101、193-194 行）编译并执行脚本模块，而 `ASTEST_CREATE_ENGINE_SHARE` 在宏定义里被明确标注为“`reused across tests, no reset`”，`ASTEST_BEGIN_SHARE` 也写明会“`leave shared-engine module state intact`”（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` 42-49、97-104 行）。与此同时，它们分别把模块名固定成 `ASGameplayTagCompat`、`ASGameplayTagContainerCompat`、`ASGameplayTagQueryCompat`（74、167、266 行）。`BuildModule(...)` 虽然会写唯一文件名并删除旧的 automation 文件，但不会在共享引擎生命周期结束前自动 `DiscardModule(...)`（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` 535-593 行）。这意味着同一进程内的重复运行、前后相邻 case 或未来继续往该文件追加的用例，都有机会读到上一次残留的 module/type/function 状态，而不是本轮脚本刚编译出的结果。 |
| 影响 | 这组三个 GameplayTag compat 用例看起来是 compile+execute 的运行时断言，实际却建立在共享模块状态不会污染的假设上。一旦脚本模块缓存、生成类型或函数查找受前一次运行影响，测试可能出现假绿或难复现的顺序相关失败，尤其会掩盖 GameplayTag/Container/Query 这组三个固定 module name 的真实回归。 |
| 修复建议 | 不要继续把会生成脚本模块的 compat 用例放在 “no reset” 的 shared engine 上。把三条测试统一切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `ASTEST_CREATE_ENGINE_CLONE()`；若必须继续复用 shared engine，则至少在每条用例结束前显式 `Engine.DiscardModule(...)`，并给 module name 附唯一 suffix，确保 `BuildModule` / `GetFunctionByDecl` 不会命中前一次运行留下的模块与类型。 |

#### Issue-33：`MathExtendedCompat` 在无 reset 的 shared engine 上复用固定模块名，导致高频 Math helper 覆盖带有顺序相关风险

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MathExtendedCompat` |
| 行号范围 | 25-149 |
| 问题描述 | 这条用例同样使用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE`（28-29 行）编译并执行脚本模块，并把 module name 固定成 `ASMathExtendedCompat`（33 行）。而测试宏明确说明 shared engine 是“`reused across tests, no reset`”，`ASTEST_BEGIN_SHARE` 也不会清理共享模块状态（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` 42-49、97-104 行）。`BuildModule(...)` 本身只负责写临时 `.as` 文件、编译并返回 `asIScriptModule*`，不会在结束时自动 `DiscardModule(...)`（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` 535-593 行）。这意味着当前已经被前置 error-code 链塞满的 `MathExtendedCompat`，还额外依赖共享引擎里不存在旧版 `ASMathExtendedCompat` 模块、旧函数声明和旧 script state 的前提；一旦同进程内重复运行或相邻测试复用该引擎，结果就可能掺入上一次残留。 |
| 影响 | `MathExtendedCompat` 本来就承担了高频 Math helper 的 smoke coverage；若再叠加共享模块泄漏，CI 看到的通过/失败信号会同时混入“这次脚本逻辑是否正确”和“有没有命中旧模块缓存”两层不确定性。任何 `Math` helper 的真实回归都可能被顺序相关状态污染放大成噪声，或者反过来被残留模块掩盖。 |
| 修复建议 | 把 `MathExtendedCompat` 从 `ASTEST_CREATE_ENGINE_SHARE()` 切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `ASTEST_CREATE_ENGINE_CLONE()`；如果确实需要 shared engine 省成本，则在结束前显式 `Engine.DiscardModule(TEXT("ASMathExtendedCompat"))`，并给 module name 增加唯一后缀，确保每次编译/执行都命中本轮刚生成的模块，而不是共享引擎里已有的旧对象。 |

---

## 测试审查 (2026-04-09 13:16 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:14)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧汇总段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 13:16 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:14)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧汇总段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 13:14)

### 一、现有测试问题

#### Issue-31：`OverloadedExportedFunctionsCanRecoverDirectBind` 只验证 synthetic coverage library，真实 FunctionLibrary overload/alias 仍然没有 bind-config 防线

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.OverloadedExportedFunctionsCanRecoverDirectBind` |
| 行号范围 | 706-752 |
| 问题描述 | 当前用例唯一检查的是 `UAngelscriptUhtOverloadCoverageLibrary::ResolveCoverageOverload` 是否在 `ClassFuncMaps` 中恢复成 direct bind。它完全没有触达生产中的 FunctionLibrary overload/alias：例如 `UAngelscriptMathLibrary` 同时导出 `SinCos_32/64`、`Modf_32/64`、`WrapDouble/WrapFloat/WrapInt`，并依赖 `ScriptName = "Modf"` / `ScriptName = "Wrap"` 归并脚本名；`URuntimeFloatCurveMixinLibrary` 还把 `GetTimeRange_Double` / `GetValueRange_Double` 通过 `ScriptName = "GetTimeRange"` / `ScriptName = "GetValueRange"` 暴露给脚本。换言之，只要 synthetic fixture 还能过，生产 overload 在真实 FunctionLibrary 上即使发生 direct-bind 丢失、alias 归并错位或某个 overload 被吞掉，这条测试仍然会稳定为绿。 |
| 影响 | bind-config 套件目前只能证明“框架支持某个理想化 overload fixture”，却不能证明真实 FunctionLibraries 的 overload surface 仍然完好。高频数学 helper 和曲线 helper 一旦在 script alias、out-ref overload 或 direct bind 恢复链路上回归，CI 不会给出对应红灯。 |
| 修复建议 | 保留 synthetic fixture 作为最小机制 smoke，但必须补一条 production-oriented overload 测试：至少抽查 `UAngelscriptMathLibrary` 的 `Modf_32/64`、`WrapDouble/WrapFloat/WrapInt` 和 `URuntimeFloatCurveMixinLibrary` 的 `GetTimeRange` / `GetTimeRange_Double`。断言不只检查 `ClassFuncMaps` 条目已绑定，还要同时验证 `FAngelscriptFunctionSignature` 生成后的 script declaration 仍然正确归并到 `Modf` / `Wrap` / `GetTimeRange`，避免继续只测 synthetic 覆盖库。 |

### 二、需要新增的测试

#### NewTest-64：真实 FunctionLibrary overload/alias 的 bind-config 恢复测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` |
| 关联函数 | `SinCos_32` / `SinCos_64` / `Modf_32` / `Modf_64` / `WrapDouble` / `WrapFloat` / `WrapInt` / `GetTimeRange` / `GetTimeRange_Double` |
| 现有测试覆盖 | `OverloadedExportedFunctionsCanRecoverDirectBind` 只覆盖 `UAngelscriptUhtOverloadCoverageLibrary::ResolveCoverageOverload` 这个 synthetic fixture；真实 FunctionLibrary overload 与 `ScriptName` alias 完全没有 bind-config 级断言 |
| 风险评估 | 生产 overload 一旦在 direct bind 恢复、alias 归并或 out-ref 参数签名上回归，脚本 surface 会先静默缩水或重名冲突，再在运行时表现成错误调用；当前 bind-config 套件不会给出任何定位信号 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.FunctionLibraryProductionOverloadAliasesRecoverDirectBind` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibrarySignatureTests.cpp` |
| 场景描述 | 在测试引擎里分别解析上述 9 个 `UFunction`，一方面检查它们在各自宿主类的 `ClassFuncMaps` 中是否恢复成已绑定 `FFuncEntry`，另一方面为每个函数构造 `FAngelscriptFunctionSignature` 并绑定临时 generic script function，读取最终 declaration。重点验证 `Modf_32/64` 统一暴露为 `Modf`、`WrapDouble/WrapFloat/WrapInt` 统一暴露为 `Wrap`、`GetTimeRange_Double` 与 `GetTimeRange` 统一暴露为 `GetTimeRange`，且同 alias 下多个 overload 都能共存。 |
| 输入/前置 | `CreateTestingFullEngine` + `FAngelscriptEngineScope`；按 `StaticClass()->FindFunctionByName(...)` 获取真实生产 `UFunction`；使用 no-op generic bind 生成可检查的 `asCScriptFunction`。必要时统计同名 overload 个数，避免只验证第一个注册成功。 |
| 期望行为 | 9 个函数在 `ClassFuncMaps` 中的 `FFuncEntry` 全部 `IsFunctionEntryBound()` 为 `true`；`FAngelscriptFunctionSignature::Declaration` 中 `Modf` / `Wrap` / `GetTimeRange` 的 alias 正确归并，且不同参数列表仍可区分；任何一个 overload 若回退成 `ERASE_NO_FUNCTION`、alias 丢失或 declaration 冲突，测试必须直接失败。 |
| 使用的 Helper | `FAngelscriptEngineScope` / `FAngelscriptFunctionSignature` / `FAngelscriptBinds::BindGlobalGenericFunction` / `asCScriptFunction` |
| 优先级 | P1 |

#### NewTest-65：真实 FunctionLibrary 代表条目的 class-map 注册钉桩

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` |
| 关联函数 | `GetRelativeLocation` / `GetShouldBeVisibleInEditor` / `GetStreamingLevels` / `GetRenderTransform` |
| 现有测试覆盖 | `GeneratedBlueprintCallableEntriesPopulateClassMaps` 只抽查 `AActor::K2_DestroyActor`、`UGameplayStatics::GetPlayerController`、`UASClass::IsDeveloperOnly`，没有任何真实 FunctionLibrary 代表条目 |
| 风险评估 | mixin/FunctionLibrary 到宿主类型的注册链一旦断掉，脚本行为测试往往只会表现成“方法不存在”或在更晚阶段失败；没有一个专门的 class-map 钉桩测试，就无法在 bind-config 层第一时间指出到底是哪个生产入口没挂上宿主类型 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.FunctionLibraryRepresentativeClassMapEntries` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，或若该文件继续控长则新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryBindConfigTests.cpp` |
| 场景描述 | 在测试引擎启动后直接读取 `FAngelscriptBinds::GetClassFuncMaps()`，分别检查 `USceneComponent`、`ULevelStreaming`、`UWorld`、`UWidget` 四个宿主类型上的代表条目。测试既要确认条目存在，也要确认 key 出现在正确宿主类，而不是错误地留在 FunctionLibrary 自身类表里。 |
| 输入/前置 | `CreateTestingFullEngine` + `FAngelscriptEngineScope`；准备 `USceneComponent::StaticClass()`、`ULevelStreaming::StaticClass()`、`UWorld::StaticClass()`、`UWidget::StaticClass()` 四个宿主类型。若现有 `ClassFuncMaps` key 使用 `UFunction->GetName()`，则按反射函数名查表；若使用 script alias，则同步校验 alias 名称。 |
| 期望行为 | `USceneComponent` 表中存在并绑定 `GetRelativeLocation`；`ULevelStreaming` 表中存在并绑定 `GetShouldBeVisibleInEditor`；`UWorld` 表中存在并绑定 `GetStreamingLevels`；`UWidget` 表中存在并绑定 `GetRenderTransform`。四个入口都必须出现在目标宿主类型的表中，而不是只存在于 FunctionLibrary 自身 `StaticClass()` 的条目里。 |
| 使用的 Helper | `FAngelscriptEngineScope` / `FAngelscriptBinds::GetClassFuncMaps()` / `IsFunctionEntryBound` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 13:01 尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:01)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文再次被插入到旧定位段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-30 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 13:01)

### 一、现有测试问题

#### Issue-30：`NativeActorMethods` / `NativeComponentMethods` 用无 reset 的 shared engine 承载会生成模块的用例，存在跨测试泄漏风险

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeActorMethods`；`Angelscript.TestModule.Bindings.NativeComponentMethods` |
| 行号范围 | 28-90；93-211 |
| 问题描述 | 两条用例都用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE`（31-32，96-97 行）去编译带 `UCLASS()` 的脚本模块，并且复用固定模块名 `ASNativeActorBindingTest` / `ASNativeComponentBindingTest`。但 `ASTEST_CREATE_ENGINE_SHARE` 在宏定义里被明确标注为“`reused across tests, no reset`”，`ASTEST_BEGIN_SHARE` 也写明会“`leave shared-engine module state intact`”（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` 42-49、97-104 行）。这意味着这两条会生成类和模块的测试把产物留在共享引擎里，后续 case 或同一 case 的重复运行可能读到前一次残留的 module/class/function，而不是当前这轮刚编译出的状态。 |
| 影响 | 这类 compile-and-reflect 测试会出现隐蔽的顺序依赖和假绿风险：前一次运行残留的 `ABindingExampleActor` / `UBindingSceneComponent` 可能让 `FindGeneratedClass()`、`FindGeneratedFunction()` 继续命中旧对象，导致当前编译链路、注册链路或生成链路已经退化时测试仍通过。问题还会随着 shared-engine 内累计的模块和 `UASClass` 数量增加而变得更难定位。 |
| 修复建议 | 不要再把会生成模块/类的测试放在 “no reset” 的 shared engine 上。把这两条用例切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `ASTEST_CREATE_ENGINE_CLONE()`，并在需要时给 module/class 名附唯一 suffix，确保每次运行前都清掉共享状态；如果仍坚持复用 shared engine，则至少在 `ASTEST_END_*` 前显式 `DiscardModule(...)` 并验证不会从旧类对象读到缓存结果。 |

### 二、需要新增的测试

#### NewTest-62：`Math` 纯返回值 helper 的 `no_discard` / trivial 声明契约

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` |
| 关联函数 | `LerpShortestPath` / `RInterpShortestPathTo` / `RInterpConstantShortestPathTo` / `TInterpTo` / `Modf_32` / `Modf_64` / `WrapDouble` / `WrapFloat` / `WrapInt` / `WrapIndex` |
| 现有测试覆盖 | 现有 `MathExtendedCompat`、`MathRotationInterpolation` 等建议只覆盖运行时数值语义，没有任何 case 锁定这些纯返回值 helper 在脚本声明层的 `no_discard` 与 trivial contract |
| 风险评估 | 这些 helper 是 `Math` 库里最常见的“返回新值、不应静默丢弃”的入口。若 `ScriptNoDiscard` / `ScriptTrivial` 元数据继续漂移，脚本会在没有任何编译期提示的情况下丢弃返回值，同时绑定路径也可能从 trivial native bind 退回普通 `UFUNCTION` 调用；现有行为测试对这类声明退化完全没有信号 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.MathReturnValueHelperMetadata` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibrarySignatureTests.cpp` |
| 场景描述 | 为上述 10 个 helper 逐个解析 `UFunction`，构造 `FAngelscriptFunctionSignature`，再把 declaration 绑定成 script function。测试同时检查 signature 的 `Declaration`、`bTrivial` 和最终 script function 形态，覆盖 `ScriptName = "Modf"` / `ScriptName = "Wrap"` 这类别名场景。 |
| 输入/前置 | `CreateTestingFullEngine` + `FAngelscriptEngineScope`；从 `UAngelscriptMathLibrary::StaticClass()` 解析对应 `UFunction`；使用 no-op generic 绑定生成可检查的 `asCScriptFunction`。 |
| 期望行为 | 10 条 declaration 都应包含 `no_discard`；`Signature.bTrivial` 对这些 helper 均为 `true`；`Modf_32/64` 的 script 名应统一为 `Modf`，`WrapDouble/WrapFloat/WrapInt` 的 script 名应统一为 `Wrap`；它们都不应携带 hidden world-context 或 property accessor 语义。任何一个 helper 丢掉 `no_discard`、`bTrivial` 或 script alias，测试都必须直接红灯。 |
| 使用的 Helper | `FAngelscriptEngineScope` / `FAngelscriptFunctionSignature` / `FAngelscriptBinds::BindGlobalGenericFunction` / `asCScriptFunction` |
| 优先级 | P1 |

#### NewTest-63：`WrapIndexUInt` 的非零下界 underflow 语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` |
| 关联函数 | `WrapIndexUInt` |
| 现有测试覆盖 | 现有 `MathBoundaries` 建议只点到 `WrapIndexUInt(6, 0, 4)` 这类 `Min == 0` 场景；真正容易暴露 unsigned 下溢错误的 `Value < Min 且 Min > 0` 分支还没有任何专门断言 |
| 风险评估 | `WrapIndexUInt` 被用来做索引循环时，只要区间不是从 `0` 开始，`Value < Min` 就会进入 unsigned underflow 语义。若这条分支返回错误值，脚本侧缓存环、窗口索引和资源分页逻辑会只在“非零起点”场景里静默算错，而现有建议还没有专门信号 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.MathWrapIndexUIntUnderflow` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` |
| 场景描述 | 脚本分别调用 `Math::WrapIndex(uint(8), uint(10), uint(20))`、`Math::WrapIndex(uint(9), uint(10), uint(20))`、`Math::WrapIndex(uint(21), uint(10), uint(20))` 和 `Math::WrapIndex(uint(10), uint(10), uint(20))`，再把结果与 native `WrapIndex(int32, int32, int32)` 的 mirror 期望值或手工预期做对照。为了确认走到的是 unsigned overload，脚本输入显式使用 `uint` 字面量或 `uint(...)` 转换。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN`；测试脚本里固定 4 组 `uint` 输入，并保留一条 `int32` 版 `WrapIndex(8, 10, 20)` 作为对照；如果当前脚本面还没导出 unsigned overload，编译阶段就应直接失败，从而把可见性问题一并暴露出来。 |
| 期望行为 | `Math::WrapIndex(uint(8), uint(10), uint(20))` 应返回 `18`，`uint(9)` 返回 `19`，`uint(21)` 返回 `10`，`uint(10)` 返回 `10`；unsigned 路径的结果必须与同区间的 signed mirror 契约一致，不能因为 underflow 产出区间内但错误的数值。若 unsigned overload 不可见或别名解析错到 signed overload，测试也必须在编译/执行阶段红灯。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_COMPILE_RUN_INT`（或等价 `BuildModule` + `ExecuteIntFunction`） |
| 优先级 | P1 |

---

## 测试审查 (2026-04-09 12:49)

### 一、现有测试问题

#### Issue-29：`Subsystem` 场景文件把“不支持脚本子系统生成”的编译失败回归伪装成 helper 覆盖

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.WorldSubsystem.Lifecycle` / `Angelscript.TestModule.WorldSubsystem.Tick` / `Angelscript.TestModule.WorldSubsystem.ActorAccess` / `Angelscript.TestModule.GameInstanceSubsystem.Lifecycle` |
| 行号范围 | 46-245 |
| 问题描述 | 这 4 条测试都在 `CompileModuleWithResult(...)` 后显式断言 `bCompiled == false` 且 `CompileResult == ECompileResult::Error`，正文只是在验证 `UScriptWorldSubsystem` / `UScriptGameInstanceSubsystem` 当前仍然“不支持脚本生成”。它们既没有调用 `GetEngineSubsystem`、`GetGameInstanceSubsystem`、`GetLocalPlayerSubsystem`、`GetWorldSubsystem`、`GetLocalPlayerSubsystemFromPlayerController`、`GetLocalPlayerSubsystemFromLocalPlayer`，也没有构造任何 subsystem lookup 的有效 / 无效上下文。由于文件路径在 `Subsystem/`、测试名又直接写成 `WorldSubsystem.*` / `GameInstanceSubsystem.*`，函数库覆盖盘点时非常容易把它们误当成 `SubsystemLibrary` 的现有行为测试。 |
| 影响 | `SubsystemLibrary` 在当前仓库里会产生“目录里明明已经有 subsystem 测试”的假象，导致 6 个 helper 的真实执行覆盖缺口被掩盖；同时把 compile-failure 回归和函数库行为覆盖混放在同一主题目录里，也会让后续维护者更难看出到底缺的是“不支持分支”回归，还是查找 helper 的正向/错误路径测试。 |
| 修复建议 | 如果这些“不支持脚本子系统生成”的回归仍然需要，保留它们，但请改名并移到更明确的 compile-failure 归档位置，例如 `Compiler/`、`Unsupported/` 或 `Angelscript.TestModule.SubsystemCompileFailure.*` 组；同时按已有建议单独新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemFunctionLibraryTests.cpp`，专门覆盖 6 个 `SubsystemLibrary` helper 的 lookup / world-context / null-input 语义，避免今后再靠目录名误判覆盖度。 |

### 二、需要新增的测试

#### NewTest-60：`AActor` 高频 helper 的 `null` self / parent 输入契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` |
| 关联函数 | `GetActorLocation` / `GetActorRotation` / `SetActorLocation` / `SetActorLocationAdvanced` / `SetActorRotation` / `AttachToActor` / `AttachToComponent` |
| 现有测试覆盖 | `NativeActorMethods` 只在有效 CDO 上 weak smoke `GetActorLocation` / `GetActorRotation`；其余搜索命中如 `Actor/AngelscriptActorInteractionTests.cpp` 只是 C++ 场景代码里读取 `Actor->GetActorLocation()`，并不是脚本 helper 覆盖 |
| 风险评估 | 这些 wrapper 都是高频 Actor API，且实现直接解引用 `Actor` / `ParentActor` / `Parent`。脚本在 spawn 失败、对象销毁、热重载悬空引用或 attach 目标缺失时最容易走到这里；如果没有固定错误路径，结果通常不是“可诊断失败”，而是进程级崩溃或脏状态泄漏。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ActorNullInputContracts` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 场景描述 | 在同一 world 里先建立一条有效 baseline：创建 actor、parent actor 和 scene component parent，确认 getter / setter / attach 正路径工作。随后脚本分别构造 `AActor NullActor = null`、`AActor NullParent = null`、`USceneComponent NullParentComponent = null`，逐个调用上述 helper，每个 helper 独立返回 error code，避免单个失败把后续分支一起短路。 |
| 输入/前置 | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 创建 world；C++ 预先记录 baseline actor 的 location、rotation、attach parent；错误路径只替换 self 或 parent 句柄，其他输入保持与 baseline 一致。若当前实现走日志/脚本异常契约，使用 `AddExpectedError` 固定诊断文本。 |
| 期望行为 | `null` self / parent 路径必须以稳定、可诊断方式失败，而不是 access violation：如果运行时采用安全返回契约，则 `GetActorLocation()` 返回 `FVector::ZeroVector`、`GetActorRotation()` 返回 `FRotator::ZeroRotator`、`SetActorLocationAdvanced()` 返回 `false`，且 setter / attach 调用不会改写 baseline actor 的 transform 或 attach parent；如果当前实现选择脚本异常/日志失败，则测试必须把异常或日志文本固定下来。无论采用哪一种契约，都不得崩溃，也不得污染后续有效 baseline。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + `AddExpectedError`（如实现走日志/异常契约） |
| 优先级 | P1 |

#### NewTest-61：`USceneComponent` transform / attach helper 的 `null` self / parent 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` |
| 关联函数 | `GetRelativeLocation` / `GetRelativeRotation` / `SetRelativeLocation` / `SetWorldLocation` / `GetNumChildrenComponents` / `AttachToComponent` / `IsAttachedTo` / `IsAttachedTo_Actor` / `GetBounds` / `GetShapeCenter` |
| 现有测试覆盖 | `NativeComponentMethods` 只覆盖有效孤立组件，且没有任何 `null USceneComponent` / `null Parent` / `null CheckActor` 输入；文件内也没有 `null` 字样或错误路径断言 |
| 风险评估 | `USceneComponent` helper 是脚本侧处理默认组件、运行时附着和 bounds 读取的核心入口。对象 teardown、默认组件绑定失败或 attach parent 缺失时，这批直接解引用的 wrapper 极易把普通脚本错误升级成崩溃，尤其是在 editor 构造和热重载场景下。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ComponentNullInputContracts` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp` |
| 场景描述 | 复用 component transform 测试夹具，先建立一条已注册 parent / child component 的有效 baseline。随后脚本分别对 `USceneComponent NullComponent = null` 调用 getter / setter / bounds helper，再对有效 child 调用 `AttachToComponent(NullParent)`、`IsAttachedTo(NullParent)`、`IsAttachedTo(null Actor)`；每个 helper 分开执行并回传 distinct error code。 |
| 输入/前置 | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 创建 actor 和已注册 component 层级；基线阶段记录 child 的 relative/world transform、attach parent 和 bounds；错误路径阶段只把 self / parent / check actor 替换成 `null`。若当前实现通过日志或脚本异常报告失败，用 `AddExpectedError` 固定文本。 |
| 期望行为 | `null` self / parent 路径不得崩溃，也不得改写 baseline component 状态：如果运行时采用安全返回契约，则 `GetRelativeLocation()` / `GetShapeCenter()` 返回零向量，`GetRelativeRotation()` 返回零旋转，`GetNumChildrenComponents()` 返回 `0`，`GetBounds()` 返回零初始化 bounds，`IsAttachedTo*()` 返回 `false`，`AttachToComponent(NullParent)` 不应改变原有 attach parent；如果实现选择脚本异常/日志失败，则测试需固定对应诊断文本。无论采用哪种契约，基线 component 的 transform、bounds 和层级都必须在错误路径后保持不变。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + `AddExpectedError`（如实现走日志/异常契约） |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-29 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |

---

## 测试审查 (2026-04-09 12:34 补记)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 12:34)` 段落；这里仅补一个尾部定位，避免重复抄写相同内容。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:34 补记)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 12:34)` 段落；这里仅补一个尾部定位，避免重复抄写相同内容。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:34)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-57：`CallOrRegister_OnCompletedInitialScan` 的 `null UAssetManager` 直接调用守卫

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` |
| 关联函数 | `CallOrRegister_OnCompletedInitialScan` |
| 现有测试覆盖 | 现有 `AssetManagerQueryAndScan` / `AssetManagerNullAndInvalidCallbackGuards` / `AssetManagerInitialScanImmediateCallback` 已覆盖有效 manager、无效 receiver / callback 名和“已完成时立即回调”，但 `UAssetManager AssetManager = null` 时直接调用 callback helper 仍没有任何契约测试 |
| 风险评估 | 当前 wrapper 直接执行 `AssetManager->CallOrRegister_OnCompletedInitialScan(...)`。脚本一旦在启动时序、热重载或工具命令里拿到空 manager，就最容易从“安全失败”升级成空指针崩溃，而现有计划还没有单独信号把这条路径钉住 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.AssetManagerInitialScanNullManagerGuard` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp` |
| 场景描述 | 在同一测试里保留一条有效 `UAssetManager::GetIfInitialized()` baseline，然后脚本显式构造 `UAssetManager AssetManager = null` 并调用 `AssetManager.CallOrRegister_OnCompletedInitialScan(Receiver, n"OnInitialScanComplete")`。native receiver 继续记录 `CallCount`，同时如果当前实现会抛脚本异常或写错误日志，用 `AddExpectedError` 固定诊断文本。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN`；准备一个合法 receiver UObject 和一个已初始化 manager baseline；空 manager 路径与 baseline 使用相同 receiver / function name，避免把失败归因到 callback 本身。 |
| 期望行为 | `null` manager 路径不应导致进程崩溃，也不应偷偷触发 receiver；若实现选择安全返回，`Receiver.CallCount` 保持 `0`；若实现选择脚本异常/日志失败，测试必须把错误文本固定下来并确认 failure 可诊断、可重复。随后同一 receiver 在有效 manager baseline 上仍应恰好回调一次，证明前面的空 manager 路径没有污染后续状态。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + native receiver UObject + `ASTEST_BUILD_MODULE` + `AddExpectedError`（如实现走日志/异常契约） |
| 优先级 | P1 |

#### NewTest-58：`GetLocalPlayerSubsystemFromLocalPlayer` 的 `null LocalPlayer` 直接输入契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h` |
| 关联函数 | `GetLocalPlayerSubsystemFromLocalPlayer` |
| 现有测试覆盖 | 现有 `SubsystemLookup` 覆盖有效 subsystem baseline，`SubsystemLocalPlayerContextResolution` 覆盖六种正向 context-object，`SubsystemNullClassContracts` 覆盖 `Class == null`；但 `ULocalPlayer LocalPlayer = null` 这一条最直接的输入分支仍没有任何自动化 |
| 风险评估 | 该 wrapper 的 `WorldContextObject` 形参实际上未参与 native 调用，真正决定行为的是显式传入的 `LocalPlayer`。如果脚本在 UI 构建前、player 切换或 teardown 期间传入空 `LocalPlayer`，helper 很可能直接返回脏结果或崩溃，而现有测试只会继续证明 happy path 正常 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.SubsystemNullLocalPlayerContracts` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemFunctionLibraryTests.cpp` |
| 场景描述 | 复用已有 local player / controller / subsystem 夹具，脚本先走一条有效 `GetLocalPlayerSubsystemFromLocalPlayer(WorldContext, LocalPlayer, Class)` baseline，再显式构造 `ULocalPlayer NullLocalPlayer = null` 并重复调用；必要时再保留 `WorldContextObject` 非空但无关对象的版本，证明返回值只由 `LocalPlayer` 决定。 |
| 输入/前置 | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 创建 world、game instance、local player 和测试 `ULocalPlayerSubsystem` class；baseline 使用真实 `LocalPlayer`，错误路径只把 `LocalPlayer` 改成 `null`，其余参数完全一致。 |
| 期望行为 | baseline 返回与 native `LocalPlayer->GetSubsystemBase(Class)` 相同的实例；`null LocalPlayer` 路径应安全返回 `nullptr`，不得崩溃、不得复用上一次有效结果，也不得因为 `WorldContextObject` 仍然有效而“意外成功”；若再把 `WorldContextObject` 换成另一对象，结果仍应保持 `nullptr`，从而固定该 helper 的真实输入主导关系。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + native subsystem test classes + `ASTEST_BUILD_MODULE` |
| 优先级 | P2 |

#### NewTest-59：全局初始化上下文 helper 在间接函数调用中的保持语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` |
| 关联函数 | `GetNameOfGlobalVariableBeingInitialized` / `GetNamespaceOfGlobalVariableBeingInitialized` / `GetModuleNameOfGlobalVariableBeingInitialized` |
| 现有测试覆盖 | 现有 `GlobalInitContext` / `GlobalInitContextHotReloadName` 建议都以“在全局变量初始化表达式里直接调用 helper”为前提；helper 经由脚本中转函数、再从中转函数读取初始化上下文的场景仍未被固定 |
| 风险评估 | 当前实现直接读取 `asCModule::InitializingGlobalProperty`。如果 Angelscript 在函数调用层级、辅助封装或 initializer 内部再次 dispatch 时丢失这份上下文，脚本作者最常见的封装写法会从“能诊断初始化来源”退化成空字符串，而现有建议只覆盖最表层 direct call |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GlobalInitContextIndirectHelperCall` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp` |
| 场景描述 | 在脚本模块中先定义 1 到 2 个中转函数，例如 `FString CaptureName() { return Script::GetNameOfGlobalVariableBeingInitialized(); }`、`FString CaptureNamespace() { return Script::GetNamespaceOfGlobalVariableBeingInitialized(); }`。随后让命名空间内和全局命名空间里的变量初始化表达式都通过这些中转函数采样上下文，并在模块加载完成后由导出 getter 把采样结果回传给 C++。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN`；模块名使用稳定字符串；准备一组无 namespace 全局变量和一组带 namespace 的全局变量；导出 getter 同时返回 direct-call baseline 与 indirect-call 采样值，避免把失败归因到其他模块状态。 |
| 期望行为 | 间接调用得到的 name / namespace / module 三元组必须与 direct-call baseline 完全一致：无 namespace 变量返回变量名和空 namespace，带 namespace 变量返回对应 namespace 名，模块名保持当前模块值；模块加载完成后的普通函数调用仍应返回空字符串。这样才能把“helper 可以被封装在脚本函数里复用”这一真实使用场景固定下来。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:19)

### 一、现有测试问题

#### Issue-28：`MathExtendedCompat` 把 20+ 个 helper 串成单个错误码脚本，失败定位和执行覆盖都会被前置分支短路

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MathExtendedCompat` |
| 行号范围 | 26-154 |
| 问题描述 | `Entry()` 从 `Math::RandHelper` 一直串到 `Math::LinePlaneIntersection`，把随机 helper、插值 helper、整数 helper、`FVector2f` utility 和几何 helper 全塞进同一个脚本函数，只靠返回整数错误码区分失败点；宿主侧唯一断言仍然只是 `Result == 1`。这意味着一旦前面的某一条检查先失败，后面十几条 helper 根本不会执行，测试既不知道后续 API 是否也已回归，也无法在 Automation 输出里直接看到哪一组语义出了问题。结合该文件里已经存在的宽断言与随机断言问题，这种“大串行 smoke script”会继续放大误报绿灯和排障成本。 |
| 影响 | 单个 helper 回归会把后续所有 helper 的实际执行覆盖一起吞掉，导致 `MathExtendedCompat` 的通过/失败信号既粗又不稳定；后续再往同一入口里继续加 helper，只会让定位更慢、覆盖更虚。 |
| 修复建议 | 把当前用例拆成 3 到 4 个单职责测试，例如 `MathRandomInvariants`、`MathAngleClampAndPulse`、`MathInterpAndCubic`、`MathIntegerAndVector2fUtilities`。每组只覆盖一类 helper，并改成独立 `TestEqual` / `TestTrue` 或独立脚本入口，而不是继续共用单个 `int Entry()` 错误码协议；随机 helper 保留不变量断言，确定性 helper 则与 native mirror 或手算值逐项比较。 |

### 二、需要新增的测试

#### NewTest-55：`CallOrRegister_OnCompletedInitialScan` 的“已完成时立即回调”契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` |
| 关联函数 | `CallOrRegister_OnCompletedInitialScan` |
| 现有测试覆盖 | 现有 `AssetManagerQueryAndScan` / `AssetManagerNullAndInvalidCallbackGuards` 建议覆盖有效 receiver、空 receiver 和错误函数名，但还没有把头文件注释里“if this has already happened call immediately”这条最关键的时序契约固定下来 |
| 风险评估 | 如果 wrapper 在 initial scan 已完成后仍走“延后注册”而不是立即执行，启动期脚本会错过依赖扫描完成的初始化窗口；反过来如果回调既立即触发又在后续再次补触发一次，又会把资产预热、注册和缓存逻辑执行两遍 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.AssetManagerInitialScanImmediateCallback` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp` |
| 场景描述 | 在 `UAssetManager::GetIfInitialized()` 已可用且 initial scan 已完成的环境下，准备一个 native receiver UObject，暴露 `OnInitialScanComplete()` 并记录 `CallCount`。脚本调用 `AssetManager.CallOrRegister_OnCompletedInitialScan(Receiver, n"OnInitialScanComplete")` 后，立即把 `Receiver.CallCount` 回传给 C++；测试主体随后再等待一个短且确定的窗口，确认不会发生延后第二次触发。 |
| 输入/前置 | 已初始化的 `UAssetManager`；一个只记录调用次数与最后调用帧号的 receiver UObject；若需要显式确认“已扫描完成”，先在 native 侧调用对应状态查询或借助现有 engine 初始化前提固定此条件。 |
| 期望行为 | 第一次脚本调用返回时 `CallCount` 已经是 `1`，证明“已完成时立即回调”契约成立；之后短暂等待期间 `CallCount` 保持 `1`，不应出现补触发的第二次回调；若同一 receiver 再次显式调用一次 helper，则新的调用至多再带来一次新的立即回调，而不是把旧注册重放成多次。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + native receiver UObject + `ASTEST_BUILD_MODULE` + 短窗口 latent wait |
| 优先级 | P1 |

#### NewTest-56：`FQualifiedFrameTime.AsSeconds` 在零帧率下的 mirror 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h` |
| 关联函数 | `AsSeconds` |
| 现有测试覆盖 | 现有 `FrameTimeAsSeconds` 和 `FrameTimeAsSecondsNtscAndNegative` 建议只覆盖正常非零帧率；`FFrameRate` 为零或默认无效值时的返回分类完全没有被固定 |
| 风险评估 | 该 wrapper 直接转发 `Target.AsSeconds()`。如果 Angelscript 在零帧率下把 native 的 `double` 结果错误地钳成 `0`、抛成脚本异常，或把非有限值序列化成其他数字，Sequencer / 媒体同步脚本会在最难排查的“坏配置帧率”场景里得到与 native 不一致的时间值 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.FrameTimeAsSecondsZeroRateContracts` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFrameTimeFunctionLibraryTests.cpp` |
| 场景描述 | 脚本分别构造 `FQualifiedFrameTime(FFrameTime(12), FFrameRate(0, 1))` 和 `FQualifiedFrameTime(FFrameTime(0), FFrameRate(0, 1))`，调用 `AsSeconds()` 后把结果回传给 C++；native 侧对同样输入直接调用 `FQualifiedFrameTime::AsSeconds()` 做 mirror，对照两边的有限/非有限分类和值。 |
| 输入/前置 | 使用 share-clean engine；C++ 侧准备 `NativeNonZeroZeroRate` 与 `NativeZeroZeroRate` 两组参考结果；若 `ExecuteDoubleFunction` 不便比较非有限值，可让脚本同时回传 `FMath::IsFinite(Result)` 风格的布尔分类或通过整数错误码编码分类结果。 |
| 期望行为 | 脚本结果与 native mirror 完全一致：对零帧率的非零帧输入，若 native 返回非有限值，则脚本也必须保持相同的非有限分类；对零帧率的零帧输入，同样要与 native 当前结果一致，不得私自钳成其他值或触发进程级错误。测试应明确固定“值”和“finite 分类”两层契约，而不是只做近似相等。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` + `ExecuteDoubleFunction`（或等价 finite-classification helper） |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-28 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-08 17:40)

### 一、现有测试问题

#### Issue-16：`MathExtendedCompat` 依赖随机结果的“非零”断言，存在稀有但真实的红灯噪声

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MathExtendedCompat` |
| 行号范围 | 46-96 |
| 问题描述 | 用例把 `Math::VRand()`、`Math::VRandCone(...)` 和 `Math::RandomRotator(false)` 都当成“随机返回一个不是零的值”来断言，其中最危险的是 `RandomRotator(false)`：当前断言只有 `if (RandomRot.IsNearlyZero()) return 160;`，而底层 `UKismetMathLibrary::RandomRotator(false)` 本身就是用 `FRand() * 360.f` 生成 pitch / yaw，`Roll` 固定为 0，所以返回接近 `FRotator::ZeroRotator` 在实现上是合法输出，只是概率低。这样一来，测试会偶发地因为随机样本而失败，而不是因为绑定或语义真的回归；与此同时，`VRand` / `VRandCone` 也只检查“非零”，没有验证单位长度或锥体约束。 |
| 影响 | 该用例会把随机采样噪声混入函数库回归信号，造成偶发红灯；即使测试通过，`VRand` / `VRandCone` / `RandomRotator` 的真正契约仍然没有被稳定验证，容易继续掩盖语义回归。 |
| 修复建议 | 把随机 helper 的断言改成稳定不变量，而不是“非零”。例如：`VRand()` 断言 `Size()` 约等于 `1`；`VRandCone(Forward, HalfAngle)` 断言与 `Forward` 的夹角不超过 `HalfAngle`；`RandomRotator(false)` 断言 `Roll == 0` 且 `Pitch` / `Yaw` 落在合法区间，而不要把接近零视为失败。如果需要固定样本，改用带固定 seed 的 stream 版本或 native 侧预先生成参考值。 |

### 二、需要新增的测试

#### NewTest-27：`SinCos` / `Modf` / `LineBoxIntersection` 的精确返回值与 out-ref 语义

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` |
| 关联函数 | `SinCos_32` / `SinCos_64` / `Modf_32` / `Modf_64` / `LineBoxIntersection` |
| 现有测试覆盖 | `MathExtendedCompat` 只覆盖另一批随机与通用 utility helper；这五个函数当前没有任何 dedicated 行为测试 |
| 风险评估 | `SinCos` 与 `Modf` 同时涉及返回值和 out-ref，最容易出现参数顺序或符号语义漂移；`LineBoxIntersection` 一旦把 segment delta、边界接触或起点在盒内的语义绑错，碰撞前置判断会稳定误判 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.MathTrigDecomposeAndBoxIntersection` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` |
| 场景描述 | 纯脚本分别调用 float/double 版本 `Math::SinCos` 与 `Math::Modf`，再对一个已知 `FBox` 执行三组 `Math::LineBoxIntersection`：穿盒命中、完全 miss、以及起点在盒内的路径。 |
| 输入/前置 | 使用确定输入，例如 `PI * 0.5`、`PI`、`-3.75`、`2.0`；盒体使用 `FBox(FVector(-1,-1,-1), FVector(1,1,1))`，线段分别设为 `(-2,0,0)->(2,0,0)`、`(-2,2,0)->(2,2,0)`、`(0,0,0)->(2,0,0)`；C++ 侧同步用 `FMath::SinCos`、`FMath::Modf`、`FMath::LineBoxIntersection` 生成 reference，脚本结果逐一比对。 |
| 期望行为 | `SinCos(PI/2)` 得到 `sin≈1`、`cos≈0`，float / double 版本都满足容差；`Modf(-3.75)` 返回小数部分 `-0.75` 且 `OutIntPart == -3.0`，`Modf(2.0)` 返回 `0` 且 `OutIntPart == 2.0`；三组 `LineBoxIntersection` 的 bool 结果与 native reference 完全一致，确保 wrapper 没有把参数顺序或 segment delta 绑错。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-16 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-08 17:55)

### 一、现有测试问题

#### Issue-17：`SoftObjectPtrCompat` 的 path round-trip 建立在已加载 transient 对象上，根本没有验证 pending soft path 语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SoftObjectPtrCompat` |
| 行号范围 | 93-167 |
| 问题描述 | 用例先用 `NewObject(GetTransientPackage(), UTexture2D::StaticClass())` 创建一个已经在内存里的 transient texture，再从这个 live object 生成 `ConstructedPath`，随后断言 `TSoftObjectPtr<UTexture2D> FromPath(ConstructedPath)` 和 `AssignedFromPath = ConstructedPath` 都能直接等于同一个 `Texture`。这只证明“已解析对象 -> path -> 仍然指向原对象”的内存内 round-trip，完全没有进入真正的 unresolved soft path 状态；`IsPending()` 也只在 `Constructed` 这个已加载对象上断言 false，从未对 path 构造出的 soft pointer 验证 `IsPending()` / `Get() == null` / load 前后状态转换。 |
| 影响 | 现有 runtime compat 用例会给出“soft object path 已覆盖”的错觉，但真正高风险的 pending/null 语义和路径解析状态机仍然是空白；一旦 soft object path 绑定退化成始终 valid、始终 null 或状态位错乱，这个测试仍可能全绿。 |
| 修复建议 | 把 path 场景改成稳定 engine asset 的 `FSoftObjectPath` 或其他未预加载路径：先只从 path 构造 `TSoftObjectPtr`，断言 `!IsNull()`、`!IsValid()`、`IsPending()`、`Get() == null`，然后再执行 `LoadSynchronous()` 或异步加载并校验状态从 pending 变为 valid；另补一个无效 path 分支，确认失败时不会错误解析成已有对象。 |

#### Issue-18：`TSoftClassPtrCompat` 只验证已加载 native class，`TSoftClassPtr` 的 soft-path / pending 分支完全没被执行

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.TSoftClassPtrCompat` |
| 行号范围 | 205-248 |
| 问题描述 | 用例对 `TSoftClassPtr<AActor>` 的全部构造都来自 `AActor::StaticClass()` 或 `ACameraActor::StaticClass()` 这类已经加载的 native class，并且 216-219 行还重复断言了同一条 `Constructed.Get() == AActor::StaticClass()`。它没有从 `FSoftObjectPath` 或字符串类路径构造 soft class，也没有验证 `IsPending()`、`Get() == null` 的未解析状态，因此 `TSoftClassPtr` 最关键的“路径存在但类尚未加载”分支根本没被触达。 |
| 影响 | 当前测试名看起来像是覆盖了 `TSoftClassPtr` 的完整兼容性，实际只覆盖了最容易通过的已加载 native class 路径；soft class path 解析、pending 状态和加载前后语义一旦回归，现有自动化不会报警。 |
| 修复建议 | 把重复的 `Constructed.Get()` 断言替换成真正的 path 场景：使用稳定 class asset path 或可控脚本/蓝图类路径先构造 `TSoftClassPtr`，断言 `!IsNull()`、`!IsValid()`、`IsPending()`、`Get() == null`，随后加载并校验返回类；再补一个无效类路径，验证失败时保持 null 而不是误解析到默认类。 |

### 二、需要新增的测试

#### NewTest-28：Actor 组合变换与 quaternion overload 的结果一致性

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` |
| 关联函数 | `SetActorRelativeRotationQuat` / `SetActorRotationQuat` / `SetActorLocationAndRotation` / `SetActorLocationAndRotationQuat` / `SetActorTransform` / `AddActorLocalRotationQuat` / `AddActorLocalTransform` / `AddActorWorldRotationQuat` / `AddActorWorldTransform` |
| 现有测试覆盖 | 当前 actor 建议只覆盖 getter/setter 基础 round-trip、sweep、attach，以及 relative/local-world offset 语义；这组组合 setter、transform delta 和 quat overload 仍完全没有专门测试 |
| 风险评估 | 一旦 quat overload 绑到错误脚本名、local/world transform delta 空间混淆，或 `SetActorLocationAndRotation*` 的位置与旋转更新次序漂移，脚本侧高频移动逻辑会稳定出错且现有测试无法区分 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ActorCompositeTransformAndQuatOverloads` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 场景描述 | 在独立 world 中准备一对镜像 actor。脚本 actor 依次调用 `SetActorRelativeRotation(FQuat)`、`SetActorRotation(FQuat)`、`SetActorLocationAndRotation` 的 rotator/quat 两个 overload、`SetActorTransform`、`AddActorLocalTransform`、`AddActorWorldTransform`；mirror actor 由 native 直接调用对应 UE API。最后逐步对照两者的 location、rotator、quat 和完整 transform。 |
| 输入/前置 | 两个 actor 都带已注册 root component；初始使用非零 location、rotation、scale；为 `SetActorLocationAndRotation*` 准备 `bTeleport = false/true` 两组输入，保证能覆盖可选参数；每一步前记录前态，避免后续断言掩盖前一步错误。 |
| 期望行为 | 每个脚本调用后的 actor world transform 与 mirror actor native 结果完全一致；rotator 和 quat 读回结果彼此一致；local transform delta 只沿 actor 局部轴累加，而 world transform delta 直接作用于世界坐标；两个 `SetActorLocationAndRotation*` overload 在相同目标输入下得到同样最终姿态。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P0 |

#### NewTest-29：SceneComponent quat / socket / 组合变换 helper

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` |
| 关联函数 | `GetRelativeScale3D` / `SetRelativeRotationQuat` / `SetRelativeTransform` / `SetRelativeLocationAndRotation` / `SetRelativeLocationAndRotationQuat` / `GetSocketQuaternion` / `SetComponentQuat` / `GetComponentQuat` / `AddRelativeRotationQuat` / `AddLocalOffset` / `AddLocalRotation` / `AddLocalRotationQuat` / `AddLocalTransform` / `SetWorldRotationQuat` / `SetWorldTransform` / `SetWorldLocationAndRotation` / `SetWorldLocationAndRotationQuat` / `AddWorldRotationQuat` / `AddWorldTransform` / `SetbVisualizeComponent` |
| 现有测试覆盖 | 现有问题和建议只覆盖基础 relative/world 位置、bounds、attach；这批 quat overload、socket quaternion、组合 transform setter 和 editor visualize 标志仍无 dedicated 测试 |
| 风险评估 | 组件 helper 一旦把 local/world 空间或 rotator/quat overload 搞混，角色挂点、相机 rig、socket 对齐和 editor 可视化都会表现错误，而且基础位置断言无法暴露这种回归 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ComponentQuatAndCompositeTransform` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp` |
| 场景描述 | 创建已注册的 parent/child `USceneComponent` 层级，并准备一个 mirror child component。脚本分别调用 relative/world 组合 setter、quat overload、`AddLocal*` / `AddWorld*` transform helper，再读取 `GetComponentQuat()`、`GetRelativeScale3D()` 和 `GetSocketQuaternion(NAME_None)`；mirror component 由 native 逐步执行相同操作后对照。最后在 editor build 下切换 `SetbVisualizeComponent()`。 |
| 输入/前置 | child component 初始使用非零 relative location、quat 和非单位 scale；`GetSocketQuaternion()` 先用 `NAME_None` 验证默认 socket 路径，避免外部资产依赖；必要时单独保留一步 world transform 写入前的 baseline，确保 local/world 累加差异可观测。 |
| 期望行为 | script component 与 mirror component 的 relative/world transform、`GetComponentQuat()`、`GetRelativeScale3D()` 完全一致；`GetSocketQuaternion(NAME_None)` 与组件当前 world quaternion 一致；rotator/quat overload 对同一目标姿态得到等价结果；`SetbVisualizeComponent(true/false)` 后 native `bVisualizeComponent` 与脚本输入同步。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-30：`FHitResult.GetPhysMaterial()` 的物理材质访问与 reset 后清理

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` |
| 关联函数 | `GetPhysMaterial` |
| 现有测试覆盖 | 现有 `HitResultAccessors` 建议只覆盖 actor/component/flag round-trip；`PhysMaterial` 访问器仍未被任何现有测试或新增建议触达 |
| 风险评估 | 命中结果里的物理材质常被脚本用来决定 surface type、音效和特效；如果 `GetPhysMaterial()` 绑错字段或 reset 后仍残留旧引用，运行时表现会持续错误且难以从 actor/component 断言中看出来 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.HitResultPhysMaterialAccess` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptHitResultFunctionLibraryTests.cpp` |
| 场景描述 | C++ 预先构造 `FHitResult` 并写入一个可识别的 `UPhysicalMaterial`，脚本调用 `GetPhysMaterial()` 验证返回对象；随后脚本或 native 调用 `Reset()`，再次读取确认已清空。 |
| 输入/前置 | 使用 `NewObject<UPhysicalMaterial>(GetTransientPackage())` 创建 transient material，并给它设置稳定名字；准备一份带 material 的 `FHitResult` 和一份默认构造的空 `FHitResult` 作为对照。 |
| 期望行为 | `GetPhysMaterial()` 在有值时返回同一个 `UPhysicalMaterial` 指针，在默认 `FHitResult` 上返回 null；`Reset()` 后再次读取也应返回 null，确保不会保留旧引用。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-17 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingScenario: 1 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-08 18:07)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-31：Math 姿态构造、组合旋转与 transform mutator 语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` |
| 关联函数 | `MakeFromAxes` / `GetForwardVector` / `GetRightVector` / `GetUpVector` / `Compose` / `MakeFromX` / `MakeFromY` / `MakeFromZ` / `MakeFromXY` / `MakeFromXZ` / `MakeFromYX` / `MakeFromYZ` / `MakeFromZX` / `MakeFromZY` / `Blend` / `BlendWith` / `SetRotation` |
| 现有测试覆盖 | `MathExtendedCompat`、`MathBoundaries`、`MathTrigDecomposeAndBoxIntersection`、`MathShortestPathAndTransformSemantics` 只覆盖随机工具、wrap/trig/interp 和 rotation round-trip，这组姿态构造与 transform mutator 仍完全无 dedicated 测试 |
| 风险评估 | 一旦 basis factory 的轴顺序、`Compose` 的乘法次序、`Blend` / `BlendWith` 的插值结果或 `SetRotation` 的“只改旋转不动平移/缩放”语义回归，脚本侧相机朝向、挂点朝向和动画过渡会稳定偏转，而且现有 math 自动化给不出任何信号 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.MathOrientationFactoriesAndTransformMutators` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` |
| 场景描述 | C++ 预先准备一组可手算的姿态输入：canonical axes、一个带 yaw/pitch 的 `FRotator` 组合对、以及两个带非零 translation/scale 的 `FTransform`。脚本分别调用 `FRotator::MakeFromAxes`、`GetForwardVector` / `GetRightVector` / `GetUpVector`、`FRotator::Compose`、`FQuat::MakeFrom*`、`FTransform::Blend`、`BlendWith`、`SetRotation`，把结果写回原生可读的返回结构或属性。 |
| 输入/前置 | 使用 `Forward=(1,0,0)`、`Right=(0,1,0)`、`Up=(0,0,1)` 作为基础轴；再使用 `A=FRotator(0,90,0)`、`B=FRotator(45,0,0)` 验证 `Compose` 次序；`TransformA` / `TransformB` 采用不同的 translation、rotation、scale，`Alpha=0.25` 与 `0.5` 两组数据；额外保留一份原始 translation/scale，用于 `SetRotation` 后比对未被污染。 |
| 期望行为 | `MakeFromAxes` 生成的 rotator 经 `GetForwardVector` / `GetRightVector` / `GetUpVector` 读回后与输入轴方向一致；`Compose(A,B)` 与 native `FRotator(FQuat(B) * FQuat(A))` 完全一致，能固定乘法顺序；各个 `FQuat::MakeFrom*` 返回的 quaternion 转回矩阵后，其主轴与 native `FRotationMatrix::MakeFrom*` 一致；`Blend` 与 `BlendWith` 对同一输入得到与 native `FTransform::Blend` / `BlendWith` 相同的 translation、rotation、scale；`SetRotation` 后 transform 的 rotation 更新为目标 rotator，而 translation 与 scale 保持调用前的值不变。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` + native reference 对照 |
| 优先级 | P0 |

#### NewTest-32：WorldCollision 同步 trace / overlap 与 component 查询结果语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WorldCollisionStatics.h` |
| 关联函数 | `LineTraceSingleByChannel` / `LineTraceMultiByChannel` / `SweepSingleByObjectType` / `OverlapMultiByProfile` / `ComponentSweepMulti` / `ComponentOverlapMulti` |
| 现有测试覆盖 | 当前只有 `WorldCollisionCompile` 的 compile smoke，以及已记录的异步 callback 建议；同步查询返回值、`OutHit` / `OutHits` / `OutOverlaps` 语义和 component query 行为仍完全没有执行级覆盖 |
| 风险评估 | 如果同步 trace/overlap 的 bool 返回值、命中数组顺序、`OutHit` 载荷或 component query 参数转发回归，脚本侧移动、瞄准、交互检测会持续误判，而 compile smoke 与 async-only 用例都拦不住 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WorldCollisionSyncQueries` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp` |
| 场景描述 | 在独立 world 中放置一个带已知 collision profile 的 blocking cube 和一个可重叠目标组件，再准备一个已注册的 `UPrimitiveComponent` 作为 sweep/overlap 发起者。脚本依次执行 `System::LineTraceSingleByChannel`、`LineTraceMultiByChannel`、`SweepSingleByObjectType`、`OverlapMultiByProfile`、`ComponentSweepMulti`、`ComponentOverlapMulti`，并把 bool 结果、首个命中 actor/component 名称和数组数量返回给 C++。 |
| 输入/前置 | 把 blocker 放在 `Start=(0,0,0)` 到 `End=(200,0,0)` 路径中间，确保 line trace 与 sweep 必命中；重叠目标放在已知半径球形 `FCollisionShape` 内；为 component query 使用已注册的 `USphereComponent` 或 `UBoxComponent`，同时准备一条完全 miss 的空路径做负样例。 |
| 期望行为 | 命中路径上 `LineTraceSingleByChannel` 与 `SweepSingleByObjectType` 返回 `true`，`OutHit.GetActor()` / `GetComponent()` 指向 blocker，impact/location 落在预期区间；`LineTraceMultiByChannel` 返回 `true` 且 `OutHits.Num() >= 1`，首个 blocking hit 与 native 直接调用结果一致；`OverlapMultiByProfile` 与 `ComponentOverlapMulti` 返回 `true` 且结果数组包含目标组件；`ComponentSweepMulti` 返回至少一个 hit，并与 native `UWorld::ComponentSweepMulti` 的数量和目标对象一致；空路径负样例应返回 `false` 且输出数组保持空。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + native world geometry 搭建 |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-08 18:20)

### 一、现有测试问题

#### Issue-19：`MathExtendedCompat` 对确定性 helper 只做“非零/大于零”断言，错误结果也容易蒙混过关

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MathExtendedCompat` |
| 行号范围 | 74-108 |
| 问题描述 | 这段脚本对一批确定性 helper 的断言明显过弱：`Math::GetReflectionVector(FVector(1,0,0), FVector(-1,0,0))` 按语义应精确得到 `(-1,0,0)`，但测试只检查“不是零向量”；`Math::VInterpTo`、`Math::RInterpTo` 只检查结果“不接近零”；`Math::FInterpTo`、`Math::CubicInterp`、`Math::CubicInterpDerivative` 只检查结果“大于零”。这意味着实现即使返回了错误方向、错误插值幅度，甚至与 native reference 相差很大，只要结果保持非零/正数，测试依然会返回 `1`。 |
| 影响 | `MathExtendedCompat` 当前不仅遗漏了大量 `AngelscriptMathLibrary` API，对它实际覆盖到的确定性 helper 也没有足够的语义保护；反射、插值和 cubic 曲线计算的回归很容易在现有自动化里漏检。 |
| 修复建议 | 把随机 helper 和确定性 helper 拆开处理。对 `GetReflectionVector`、`VInterpTo`、`RInterpTo`、`FInterpTo`、`CubicInterp`、`CubicInterpDerivative` 改成与 hand-written expected value 或 native `FMath` / `UKismetMathLibrary` reference 做逐项比较，并使用显式容差；不要再用“非零”“大于零”这类宽断言替代具体数值语义。 |

### 二、需要新增的测试

#### NewTest-33：`FVector` / `FVector3f` 平面投影、二维距离与颜色字符串 helper

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` |
| 关联函数 | `Size2D` / `SizeSquared2D` / `PointPlaneProject` / `Dist2D` / `DistSquared2D` / `ToColorString` |
| 现有测试覆盖 | 完全无测试；当前已记录的 Math 建议覆盖 wrap、trig、interp、orientation 等函数，但 `FVector` / `FVector3f` 这组 planar helper 与格式化 helper 仍没有任何 dedicated 用例 |
| 风险评估 | 如果 plane projection 的 up-axis 语义、二维距离计算或 `ToColorString()` 的格式化契约回归，脚本侧移动投影、平面导航和调试 HUD 会持续给出错误结果，而现有 Math 自动化完全没有信号 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.MathPlanarProjectionAndColorFormatting` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` |
| 场景描述 | 纯脚本分别对 `FVector` 和 `FVector3f` 调用 `Size2D`、`SizeSquared2D`、`PointPlaneProject`、`Dist2D`、`DistSquared2D` 与 `ToColorString`。使用可手算输入，验证平面投影长度、投影点位置、二维距离以及字符串格式。 |
| 输入/前置 | 使用 `Vector=(3,4,12)`、`Other=(0,0,12)`、`UpDirection=(0,0,1)`、`PlaneBase=(0,0,2)`、`PlaneNormal=(0,0,1)`；`FVector3f` 使用同值的 32-bit 版本；颜色字符串样本使用 `Vector=(1.0,0.5,0.25)`，避免舍入歧义。 |
| 期望行为 | `Size2D` 返回 `5`，`SizeSquared2D` 返回 `25`；`PointPlaneProject` 返回 `(3,4,2)`；`Dist2D(Vector, Other, UpDirection)` 返回 `5`，`DistSquared2D` 返回 `25`；`ToColorString()` 精确返回 `<Red>X=1.000 </><Green>Y=0.500 </><Blue>Z=0.250 </>`；`FVector` 与 `FVector3f` 两组结果都与 native reference 一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` + native reference 对照 |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-19 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
---

## 测试审查 (2026-04-08 18:37)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-34：`USceneComponent` 相对/世界增量位移与 rotator overload 语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` |
| 关联函数 | `AddRelativeLocation` / `SetRelativeRotation` / `AddRelativeRotation` / `AddWorldOffset` / `AddWorldRotation` |
| 现有测试覆盖 | 当前组件相关问题与建议已覆盖 relative/world setter、bounds、attach、quat overload、`AddLocal*` 与 `AddWorldRotationQuat`，但这组最常用的 rotator/offset 增量 helper 仍没有 dedicated 测试 |
| 风险评估 | 一旦 `AddRelativeLocation` / `AddWorldOffset` 把 parent 空间与世界空间混淆，或 `SetRelativeRotation` / `AddRelativeRotation` / `AddWorldRotation` 的 rotator overload 与 native 累加顺序漂移，脚本侧相机 rig、挂点偏移和组件动画会持续出现“能动但方向不对”的静默回归 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.ComponentIncrementalOffsetAndRotatorOverloads` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp` |
| 场景描述 | 创建带非零 world transform 的 parent / child `USceneComponent`，并再准备一个 native mirror child。脚本 child 依次调用 `AddRelativeLocation`、`SetRelativeRotation(FRotator)`、`AddRelativeRotation(FRotator)`、`AddWorldOffset`、`AddWorldRotation(FRotator)`；mirror child 由 C++ 逐步执行对应原生 API，最后对照两者的 relative/world location 与 rotation。 |
| 输入/前置 | parent 先设置 `Location=(100,50,0)`、`Rotation=(0,90,0)`，child 初始 relative transform 设为非零值，确保 relative 轴与世界轴可区分；对 `AddWorldOffset` 使用 `Delta=(10,0,0)`，对 `AddRelativeLocation` 使用 `Delta=(0,20,0)`；rotator 增量使用可手算的 yaw / pitch 组合，并在每一步后立即记录 native reference。 |
| 期望行为 | `AddRelativeLocation` 只改 child 的 relative translation，并通过 parent transform 反映到新的 world position；`AddWorldOffset` 直接按世界轴平移，最终 world location 与 native `AddWorldOffset` 完全一致；`SetRelativeRotation` / `AddRelativeRotation` 后 `GetRelativeRotation()` 与 mirror child 的 native relative rotator 一致；`AddWorldRotation` 后 child 的 world rotator 与 native `AddWorldRotation` 结果一致，且不会错误污染 relative location。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + native mirror component 对照 |
| 优先级 | P1 |

#### NewTest-35：`AddSmartAutoCurveKey` 与 `AddAutoCurveKey` 的 tangent mode 区分

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` |
| 关联函数 | `AddSmartAutoCurveKey` |
| 现有测试覆盖 | 当前 `RuntimeFloatCurve` 相关现有问题与新增建议已覆盖 `AddAutoCurveKey`、`AddLinearCurveKey`、weighted tangent helper、`GetNumKeys` / `GetTimeRange` / `GetValueRange` 等路径，但 `AddSmartAutoCurveKey` 仍是唯一没有 dedicated 断言的 key-creation helper |
| 风险评估 | 如果 `AddSmartAutoCurveKey` 退化成和 `AddAutoCurveKey` 完全相同的 `RCTM_Auto` 路径，脚本侧曲线编辑会静默丢失 `SmartAuto` 平滑策略，运行时插值形状会在不报错的情况下漂移 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveSmartAutoKey` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp` |
| 场景描述 | 创建两份独立 `UCurveFloat`。脚本对第一份调用 `AddSmartAutoCurveKey`，对第二份调用 `AddAutoCurveKey`，并把返回的 `FCurveKeyHandle` 保留下来。执行结束后由 C++ 直接读取两条曲线中对应 key 的 `InterpMode` 与 `TangentMode`，比较 smart-auto 与 auto 两条路径的差异。 |
| 输入/前置 | 两份曲线都从空状态开始，使用相同输入 `(InTime=0.5f, InValue=10.0f)`；必要时再添加第二个 key 以触发切线重算；native 侧准备 `ERichCurveInterpMode::RCIM_Cubic`、`ERichCurveTangentMode::RCTM_SmartAuto`、`ERichCurveTangentMode::RCTM_Auto` 作为 reference。 |
| 期望行为 | `AddSmartAutoCurveKey` 与 `AddAutoCurveKey` 都返回有效 `FCurveKeyHandle`，并各自把 key 数量增到 `1`；smart-auto 曲线中新 key 的 `InterpMode == RCIM_Cubic` 且 `TangentMode == RCTM_SmartAuto`；auto 曲线中新 key 的 `TangentMode == RCTM_Auto`；两条路径的 tangent mode 必须可区分，避免 `AddSmartAutoCurveKey` 静默退化成普通 auto key。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` + native `FRichCurveKey` 检查 |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 1, NoTestForSource: 1 |

---

## 测试审查 (2026-04-08 18:54)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-36：全局初始化模块名 helper 的 hotreload 后缀与非命名空间场景

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` |
| 关联函数 | `GetNameOfGlobalVariableBeingInitialized` / `GetNamespaceOfGlobalVariableBeingInitialized` / `GetModuleNameOfGlobalVariableBeingInitialized` |
| 现有测试覆盖 | 当前仅有 `GlobalInitContext` 建议覆盖“带 namespace 的正常初始化”和“非初始化时机返回空字符串”；没有任何用例固定 hotreload/后缀模块名或无 namespace 全局变量的语义 |
| 风险评估 | 这组三函数主要服务脚本初始化期调试；如果模块名被错误折叠成 base name，或无 namespace 场景返回值漂移，排查热重载与多模块初始化问题时会直接误导定位 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GlobalInitContextHotReloadName` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp` |
| 场景描述 | 构造一个模块名显式带后缀的脚本模块，例如 `ScriptInitContext_HotReload_42`，在一个无 namespace 全局变量和一个带 namespace 全局变量的初始化表达式里分别调用三组 `Script::Get*BeingInitialized()`，把结果写入独立全局或导出 getter。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 创建独立引擎；模块名包含稳定 suffix；准备两组全局变量：一组在全局命名空间，一组在自定义 namespace 下；模块加载完成后通过导出函数读回采样结果。 |
| 期望行为 | 对无 namespace 全局变量，`GetNameOfGlobalVariableBeingInitialized()` 返回变量名，`GetNamespaceOfGlobalVariableBeingInitialized()` 返回空字符串；对带 namespace 全局变量，返回对应 namespace 名；`GetModuleNameOfGlobalVariableBeingInitialized()` 应返回当前真实模块名字符串并保留传入 suffix，而不是被折叠成去后缀 base name；在非初始化时机再次调用三函数时仍返回空字符串。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-37：classic input bind helper 的 null 输入错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` |
| 关联函数 | `BindAction` / `BindKey` / `BindChord` / `BindAxis` / `BindAxisKey` / `BindVectorAxis` / `PushInputComponent` / `PopInputComponent` / `GetPlayerInput` |
| 现有测试覆盖 | `InputMappingRoundTrip` 只覆盖 null `APlayerController` / `UPlayerInput` 的 mapping 路径，`InputDelegateBindingsAndEngineDefaults` 只覆盖有效对象注册；最危险的 null `UInputComponent` / null controller bind 调用仍无任何断言 |
| 风险评估 | 这些 wrapper 当前都是直接解引用输入对象；脚本一旦在未初始化 input 组件、销毁后的 controller 或错误 world context 下调用，最可能出现的结果不是“返回失败”而是直接崩溃，且现有自动化没有保护 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.InputBindNullGuards` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp` |
| 场景描述 | 复用已有 input receiver UObject，但分别传入 `null` `UInputComponent`、`null` `APlayerController` 和 `null` `UPlayerInput`。脚本逐个调用 `BindAction`、`BindKey`、`BindChord`、`BindAxis`、`BindAxisKey`、`BindVectorAxis`、`PushInputComponent`、`PopInputComponent`、`GetPlayerInput`，并通过错误码区分每个 helper 的返回/失败点。 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 建立最小 world 和 receiver；准备一组有效 delegate/function name 作为对照，再单独构造 null component、null controller、controller 但 `PlayerInput == null` 三种输入；必要时借助 `AddExpectedError` 固定日志或异常消息。 |
| 期望行为 | 所有 null 输入路径都应以可诊断方式安全失败而不是崩溃：bind helper 不应向任何 binding array 写入记录，`PushInputComponent` / `PopInputComponent` 不应污染 controller 状态，`GetPlayerInput(null)` 返回 `nullptr`；如果当前实现选择抛脚本异常或记录特定错误日志，测试需把该契约固定下来。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + `AddExpectedError`（如实现走日志/异常契约） |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |
| P1 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 23:49 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 23:49)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧段落之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-08 18:57)

### 一、现有测试问题

#### Issue-20：`NativeComponentMethods` 把多类无关绑定揉进单个 error-code 脚本，失败定位和覆盖颗粒度都过粗

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeComponentMethods` |
| 行号范围 | 103-208 |
| 问题描述 | `ReadComponentBindings()` 一次性把 `Deactivate/Activate`、`SetRelativeLocation/GetRelativeLocation`、`SetComponentVelocity/GetComponentVelocity`、`GetNumChildrenComponents`、`GetOwner().GetComponent(...)`、`USceneComponent::Get(...)`、`GetComponentsByClass(...)` 和 `ComponentHasTag(NAME_None)` 全塞进同一个脚本函数里，只靠返回的整数错误码区分失败点；宿主侧真正的断言只有 `Result == 1`。这会导致一旦前面的某个检查失败，后面所有绑定都直接短路，既看不出究竟是哪一类 binding 退化，也无法知道其余分支是否仍然正确。 |
| 影响 | 该用例表面上覆盖了大量 component/native 入口，实际上是把生命周期、transform、owner lookup、容器查询和 tag 语义混成一个难以维护的 smoke test。回归定位成本高，新增层级/无效输入/注册状态等场景时也会继续被迫往同一个整数协议里堆逻辑，进一步稀释断言质量。 |
| 修复建议 | 把当前脚本拆成 3 到 4 个单职责用例，至少分开 `ComponentTransformAndVelocity`、`ComponentOwnerLookup`、`ComponentArrayQueries`、`ComponentLifecycleAndTag` 四组行为；每组只覆盖一类 API，并用独立 `TestEqual` / `TestTrue` 验证具体结果，而不是共享一个 `Result` 错误码。拆分时顺手把公共建模提到 helper，继续沿用已注册/有层级的 component fixture，避免再把 unrelated binding 全塞回同一个 smoke case。 |

### 二、需要新增的测试

#### NewTest-38：`UWorld` / `ULevelStreaming` helper 的 null 输入守卫

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h` |
| 关联函数 | `GetStreamingLevels` / `GetShouldBeVisibleInEditor` |
| 现有测试覆盖 | 已有 `WorldStreamingAccess` 建议只覆盖有效 world / valid level 的正路径；`null UWorld` 和 `null ULevelStreaming` 仍完全没有契约测试 |
| 风险评估 | 两个 wrapper 当前都是直接解引用输入对象。脚本一旦在 world teardown、streaming level 已销毁或显式空句柄场景下调用，就最容易出现 access violation，而现有计划还没有固定“应当安全失败”的约束。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WorldStreamingNullGuards` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldFunctionLibraryTests.cpp` |
| 场景描述 | 在同一测试模块里保留一条有效 world/access 正路径作对照，然后脚本显式对 `UWorld World = null` 调用 `World.GetStreamingLevels()`，并对 `ULevelStreaming Level = null` 调用 `Level.GetShouldBeVisibleInEditor()`。 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 创建正常 world 作为 baseline；另在脚本里构造 `null` world handle 和 `null` streaming-level handle；如当前实现会记录脚本异常或 `ensure`，用 `AddExpectedError` 固定日志。 |
| 期望行为 | 两条 null 输入路径都不应导致崩溃；`GetStreamingLevels()` 对空 world 返回空数组，`GetShouldBeVisibleInEditor()` 对空 level 返回 `false`；如果实现选择抛脚本异常而非安全返回，测试需把异常文本固定下来并确认 failure 是可诊断、可重复的，而不是进程级崩溃。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + `AddExpectedError`（如实现走日志/异常契约） |
| 优先级 | P0 |

#### NewTest-39：`UAssetManager` query / initial-scan wrapper 的 null receiver 与无效 callback 绑定

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` |
| 关联函数 | `GetPrimaryAssetData` / `GetPrimaryAssetDataList` / `GetPrimaryAssetObject` / `GetPrimaryAssetIdForObject` / `GetPrimaryAssetIdList` / `GetPrimaryAssetTypeInfo` / `GetPrimaryAssetTypeInfoList` / `GetPrimaryAssetRules` / `CallOrRegister_OnCompletedInitialScan` |
| 现有测试覆盖 | 已有 `AssetManagerQueryAndScan` 建议只覆盖已初始化 manager 上的无效 id/type 和正常 scan callback；`null UAssetManager`、`Object == null`、`FunctionName` 无效这些最危险的错误路径仍未建模 |
| 风险评估 | 这组 wrapper 基本都是直接转发成员调用；一旦脚本在 startup 时序、热重载或 editor 工具里传入空 manager / 空 receiver / 错函数名，最可能出现的不是“返回失败”而是直接崩溃或留下悬空 delegate，且目前没有任何自动化把这种行为钉住。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.AssetManagerNullAndInvalidCallbackGuards` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp` |
| 场景描述 | 脚本先对 `null` `UAssetManager` 逐个调用 query helper，再对真实 `UAssetManager::GetIfInitialized()` 调用 `CallOrRegister_OnCompletedInitialScan`，分别传入 `null` receiver、缺失 `UFUNCTION()` 的 receiver 和错误 `FunctionName`。最后再用一个有效 receiver 做 baseline，对照 callback 是否只在有效输入下触发。 |
| 输入/前置 | 准备已初始化的 `UAssetManager`、一个带合法 `OnScanComplete` 的 receiver UObject、一个不包含目标函数的 receiver、以及空 `AssetManager` 句柄；为 out 参数准备可观察的默认值和空数组；如当前实现会记录 `CreateUFunction` 或 script exception 错误，使用 `AddExpectedError` 固定。 |
| 期望行为 | `null` manager 查询路径都应安全失败而不是崩溃：bool 返回值为 `false`、对象返回值为 `nullptr`、数组保持空、`FPrimaryAssetId` / `FPrimaryAssetRules` / `FPrimaryAssetTypeInfo` 保持无效默认值；`CallOrRegister_OnCompletedInitialScan` 在 `null` receiver 或错误 `FunctionName` 下不应触发任何回调，也不应污染有效 receiver 的调用次数；有效 receiver 路径仍应按既有契约只触发一次。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + native receiver UObject + `AddExpectedError`（如实现走日志/异常契约） |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-20 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-08 19:17)

### 一、现有测试问题

#### Issue-21：`GameplayTagCompat` 对成功的 `RequestGameplayTag` 只检查 `IsValid()`，没有校验返回的具体 tag

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagCompat` |
| 行号范围 | 25-70 |
| 问题描述 | 用例先把 `AllTags.First()` 注入脚本，再调用 `FGameplayTag::RequestGameplayTag(FName("%s"), true)`；但后续对 `GlobalTag` 的唯一断言只有 `IsValid()`。测试既没有比较 `GlobalTag.GetTagName()` 是否等于请求的名字，也没有比较 `ToString()` 或 `==` 的结果。这样一来，只要绑定层返回“任意一个有效 tag”而不是被请求的 tag，当前用例仍会稳定返回 `1`。 |
| 影响 | `GameplayTagCompat` 目前连最基本的“按名称请求到正确 tag”都没有钉死，`RequestGameplayTag` 如果忽略输入参数、错误复用缓存或返回错 tag，现有自动化不会报警。 |
| 修复建议 | 在现有脚本里把成功路径的返回语义补完整：新增 `if (!(GlobalTag.GetTagName() == FName("%s"))) return ...;`、`if (!(GlobalTag.ToString() == "%s")) return ...;`，必要时再和由 native 侧预先选中的 `AllTags.First()` 做 `==` 对比。若继续保留无效路径，建议再补一个明确不存在的 tag name，而不只是 `NAME_None`，这样能同时覆盖“查找失败”和“成功查找返回正确对象”两条契约。 |

### 二、需要新增的测试

本轮未新增新的测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-21 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:15)

### 一、现有测试问题

#### Issue-25：`RuntimeFloatCurve` 的 bind-config 覆盖只检查 helper class，没验证 `FRuntimeFloatCurve` / `UCurveFloat` 的实例方法面

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.InlineDefinitionFunctionsCanRecoverDirectBind` / `Angelscript.TestModule.Engine.BindConfig.InlineOutRefFunctionsCanRecoverDirectBind` |
| 行号范围 | 754-847 |
| 问题描述 | 两个用例都从 `URuntimeFloatCurveMixinLibrary::StaticClass()` 取 `GetNumKeys` / `GetTimeRange`，再只检查 `ClassFuncMaps` 里对应 `FFuncEntry` 已绑定。它们没有编译任何 `FRuntimeFloatCurve` 或 `UCurveFloat` 脚本，也没有断言 `Curve.GetNumKeys()`、`Curve.GetTimeRange(...)`、`CurveAsset.AddAutoCurveKey(...)` 这类目标类型实例方法还能被脚本解析。考虑到 `RuntimeFloatCurveMixinLibrary.h` 已把 `ScriptMixin = "FRuntimeFloatCurve UCurveFloat"` 注释掉，现有测试实际上把“helper class 自身有 direct bind”误当成“曲线实例扩展面仍然存在”。 |
| 影响 | 一旦 `FRuntimeFloatCurve` / `UCurveFloat` 的实例方法 surface 已经回退成只能通过 `URuntimeFloatCurveMixinLibrary::...` 静态入口调用，当前 bind-config 套件仍会全绿，无法拦住真正的脚本 API 形态回退。 |
| 修复建议 | 保留 direct-bind 恢复断言，但追加一条 compile+execute 级验证：脚本同时对 `FRuntimeFloatCurve` 调用 `Curve.GetNumKeys()` / `Curve.GetTimeRange(...)`，并对 `UCurveFloat` 调用 `CurveAsset.AddAutoCurveKey(...)` 或 `AddDefaultKey(...)`；至少确认实例方法 declaration 仍存在、能够成功执行，并把结果与 native mirror 对照。若当前设计已决定退回静态 helper，则应把测试名和断言显式改成“只保证 helper class 入口存在”，不要继续宣称它覆盖了目标类型扩展面。 |

### 二、需要新增的测试

#### NewTest-46：`RuntimeFloatCurve` 实例方法 surface 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` |
| 关联函数 | `GetNumKeys` / `GetTimeRange` / `AddDefaultKey` / `AddAutoCurveKey` / `SetKeyInterpMode` |
| 现有测试覆盖 | 只有 `InlineDefinitionFunctionsCanRecoverDirectBind` / `InlineOutRefFunctionsCanRecoverDirectBind` 两条 helper-class direct-bind smoke，没有任何 compile/execute 用例覆盖 `FRuntimeFloatCurve` / `UCurveFloat` 实例写法 |
| 风险评估 | 曲线 helper 一旦从实例方法退回静态 helper，现有 bind-config 测试仍会全绿，脚本真实 API 面会静默缩水；后续即便补 runtime 行为测试，也可能只覆盖 `URuntimeFloatCurveMixinLibrary::...` 路径。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveInstanceSurface` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp` |
| 场景描述 | 脚本分别创建 `FRuntimeFloatCurve` 和 transient `UCurveFloat`，直接以实例方法形式调用 `Curve.AddDefaultKey(...)`、`Curve.GetNumKeys()`、`Curve.GetTimeRange(...)`、`CurveAsset.AddAutoCurveKey(...)`、`CurveAsset.SetKeyInterpMode(...)`，再由 native mirror 读取 key 数量、时间范围和 key 插值模式。 |
| 输入/前置 | `FRuntimeFloatCurve` 从空 curve 开始；`UCurveFloat` 使用 transient 对象；保存 `FCurveKeyHandle` 供后续 `SetKeyInterpMode(...)` 使用；脚本返回 distinct error code 区分 compile surface、key count、time range、interp mode 失败。 |
| 期望行为 | `Curve.AddDefaultKey(...)` 后 `Curve.GetNumKeys()` 返回 `1`，`GetTimeRange()` 回填传入时间；`CurveAsset.AddAutoCurveKey(...)` 返回有效 `FCurveKeyHandle`，native 侧 `FloatCurve.GetNumKeys()==1`，且 `SetKeyInterpMode(...)` 后对应 key 的 `InterpMode` 与脚本传入一致；如果实例方法 surface 丢失，测试应在 compile 阶段直接失败，而不是静默退回 helper-class 路径。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` / `ASTEST_BEGIN_SHARE_CLEAN` / `ASTEST_BUILD_MODULE` / transient `UCurveFloat` |
| 优先级 | P1 |

#### NewTest-47：`Script` 全局初始化 helper 的声明元数据

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h` |
| 关联函数 | `GetNameOfGlobalVariableBeingInitialized` / `GetNamespaceOfGlobalVariableBeingInitialized` / `GetModuleNameOfGlobalVariableBeingInitialized` |
| 现有测试覆盖 | 完全无测试；现有 `GlobalInitContext` 建议只聚焦运行时返回值，没有锁定 `NotAngelscriptProperty` / `no_discard` 声明语义 |
| 风险评估 | 这组三个 introspection helper 一旦继续被当成 property accessor 或允许静默丢弃返回值，脚本侧最需要诊断价值的调试 API 会先退化成“语法还在、约束没了”的灰区。 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.ScriptGlobalInitHelperMetadata` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibrarySignatureTests.cpp` |
| 场景描述 | 直接为 `GetNameOfGlobalVariableBeingInitialized` / `GetNamespaceOfGlobalVariableBeingInitialized` / `GetModuleNameOfGlobalVariableBeingInitialized` 构造 `FAngelscriptFunctionSignature`，再通过 `BindGlobalGenericFunction` + `ModifyScriptFunction` 观察最终 script declaration 与 trait。 |
| 输入/前置 | `CreateTestingFullEngine` + `FAngelscriptEngineScope`；解析 `UAngelscriptScriptLibrary::StaticClass()` 上 3 个 `UFunction`；使用 no-op generic 绑定生成可检查的 `asCScriptFunction`。 |
| 期望行为 | 3 条 declaration 都包含原始函数名和 `no_discard`；修改后的 `asCScriptFunction` 都不是 property accessor，即 `!ScriptFunction->IsProperty()`；3 条 helper 都不应携带 hidden world-context 参数。 |
| 使用的 Helper | `FAngelscriptEngineScope` / `FAngelscriptFunctionSignature` / `FAngelscriptBinds::BindGlobalGenericFunction` / `asCScriptFunction` |
| 优先级 | P2 |

#### NewTest-48：`Subsystem` getter 的 `no_discard` 与 world-context trait

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h` |
| 关联函数 | `GetEngineSubsystem` / `GetGameInstanceSubsystem` / `GetLocalPlayerSubsystem` / `GetWorldSubsystem` / `GetLocalPlayerSubsystemFromPlayerController` / `GetLocalPlayerSubsystemFromLocalPlayer` |
| 现有测试覆盖 | 完全无测试；现有 `SubsystemLookup` / `GetLocalPlayerSubsystem` 行为建议只覆盖返回实例和 context 解析，不覆盖 declaration metadata |
| 风险评估 | subsystem 查询入口如果可以被静默丢弃，或 `GetLocalPlayerSubsystemFromLocalPlayer` 继续带着多余的 world-context trait，生成代码和手写脚本都会被错误签名误导。 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.SubsystemGetterMetadata` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibrarySignatureTests.cpp` |
| 场景描述 | 为 6 个 getter 逐个构造 `FAngelscriptFunctionSignature`，再把生成的 declaration 绑定成 script function，检查 `no_discard`、hidden argument 和 `asTRAIT_USES_WORLDCONTEXT`。 |
| 输入/前置 | testing engine；从 `USubsystemLibrary::StaticClass()` 解析 6 个 `UFunction`；用 no-op generic 绑定生成 script function；按函数名记录期望的 world-context 行为。 |
| 期望行为 | 6 条 declaration 都包含 `no_discard`；`GetGameInstanceSubsystem` / `GetLocalPlayerSubsystem` / `GetWorldSubsystem` 保留 hidden world-context 参数并带 `asTRAIT_USES_WORLDCONTEXT`；`GetEngineSubsystem` 与 `GetLocalPlayerSubsystemFromPlayerController` 不带 hidden world-context；`GetLocalPlayerSubsystemFromLocalPlayer` 也不应隐藏 `WorldContextObject`、不应带 `asTRAIT_USES_WORLDCONTEXT`，而应把 `ULocalPlayer` 作为显式输入保留下来。 |
| 使用的 Helper | `FAngelscriptEngineScope` / `FAngelscriptFunctionSignature` / `FAngelscriptBinds::BindGlobalGenericFunction` / `asCScriptFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-25 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |
 
---

## 测试审查 (2026-04-09 13:27 真正尾部定位-EOF)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:27)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧段落之前。

### 二、需要新增的测试

本轮无新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 2 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-08 19:44)

### 一、现有测试问题

#### Issue-24：`AngelscriptBindConfigTests.cpp` 已膨胀成 700+ 行的 bind-config 汇总文件，FunctionLibrary 相关断言被埋没

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.*` |
| 行号范围 | 1-712 |
| 问题描述 | 该文件目前包含 disabled-bind 合并、startup 顺序、重复注册、BlueprintInternalUseOnly 覆盖、ScriptMethod metadata、world-context metadata、overload 恢复，以及 `RuntimeFloatCurveMixinLibrary` 的 direct-bind 恢复等多类完全不同层面的断言，总长度已到 712 行，明显超过规则要求的单文件 300-500 行。和 `AngelscriptEngineParityTests.cpp` 的 megafile 问题类似，这种“通用 bind-config + FunctionLibrary 专项”混装结构会把函数库相关失败埋进一串无关测试里，继续诱导后续补丁往同一个汇总文件里堆更多 smoke 断言。 |
| 影响 | FunctionLibraries 相关的 metadata / direct-bind 回归即使被发现，也很难快速定位到是 `RuntimeFloatCurve`、`WidgetBlueprintStatics` 还是通用 bind pipeline 退化；测试文件继续膨胀后，维护者更容易补“存在即可”的表项断言，而不是把函数库拆成可执行、可清理、单职责的专用测试。 |
| 修复建议 | 把该文件按职责拆开：保留纯 bind-config 基建测试（disabled bind、顺序、去重、override）在一个 300-500 行以内文件；把真实 FunctionLibrary 相关断言拆到专用文件，例如 `BindConfig.FunctionLibraryMetadata`（`CreateWidget`、`GetRenderTransform`、`GetLocalPlayerSubsystemFromLocalPlayer` 等 metadata/signature 场景）和 `BindConfig.FunctionLibraryDirectBind`（`RuntimeFloatCurve` 等 direct-bind 恢复场景）。拆分后每个测试名继续保持 `Angelscript.TestModule.Engine.BindConfig.*` 前缀，但按领域分文件，避免 FunctionLibrary 断言继续被 bind-config 汇总文件吞掉。 |

### 二、需要新增的测试

#### NewTest-43：`GameplayLibrary` invalid input 下的立即失败回调契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h` |
| 关联函数 | `AsyncSaveGameToSlot` / `AsyncLoadGameFromSlot` |
| 现有测试覆盖 | 现有建议只覆盖正常 save/load 链路与 callback 在 game thread 上触发；`SaveGameObject == null`、`SlotName == ""`、不存在 save system / 无效 slot 的立即失败分支仍没有测试 |
| 风险评估 | 当前 wrapper 直接转发到 `UGameplayStatics::AsyncSaveGameToSlot` / `AsyncLoadGameFromSlot`。一旦参数校验或 delegate 转发被改坏，脚本侧最常见的失败路径就会从“同步触发失败回调”退化成静默不回调或崩溃，自动化目前没有任何信号。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GameplayAsyncImmediateFailureCallbacks` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayFunctionLibraryTests.cpp` |
| 场景描述 | 准备一个 native receiver UObject，记录 save/load callback 的调用次数、`SlotName`、`UserIndex`、成功位和收到的 `USaveGame*`。脚本分别调用 `AsyncSaveGameToSlot(null, "", UserIndex, Delegate)`、`AsyncSaveGameToSlot(ValidSaveGame, "", UserIndex, Delegate)`、`AsyncLoadGameFromSlot("", UserIndex, Delegate)` 和 `AsyncLoadGameFromSlot("DefinitelyMissingSlot", UserIndex, Delegate)`。 |
| 输入/前置 | 使用唯一 `UserIndex`，一个带可识别字段的最小 `USaveGame` 派生对象，以及四个独立 callback 计数槽；测试主体需要等待一小段确定窗口，确保“立即失败分支”已经有机会在 game thread 上执行。UE 原生 `GameplayStatics.cpp` 当前明确在 `SaveSystem` 不可用、`SlotName.Len()==0`、`SaveGameToMemory` 失败或 load 前置无效时直接 `ExecuteIfBound(...)`。 |
| 期望行为 | 两条 invalid save 路径都应恰好触发一次 callback，且 `bSuccess == false`、`SlotName` / `UserIndex` 与输入一致；`AsyncLoadGameFromSlot("", ...)` 与 `AsyncLoadGameFromSlot("DefinitelyMissingSlot", ...)` 也都应恰好触发一次 callback，且 `SaveGameObject == nullptr`；四条路径都不应崩溃，也不应出现“参数非法时不回调”的静默失败。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + native receiver UObject + `FEvent`/latent wait |
| 优先级 | P1 |

#### NewTest-44：`GetRenderTransform` 的 null widget 错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` |
| 关联函数 | `GetRenderTransform` |
| 现有测试覆盖 | 现有 `UMG helper` 建议只覆盖 `CreateWidget` 正路径、`GetRenderTransform` 正路径和 `null widget class`；对 `UWidget Widget = null` 直接调用 `GetRenderTransform()` 仍完全没有契约测试 |
| 风险评估 | `UAngelscriptWidgetMixinLibrary::GetRenderTransform` 当前实现直接解引用 `Widget`。如果脚本在 widget 已销毁、绑定失败或显式空句柄场景下调用，这条路径最容易从脚本错误升级成 access violation，而现有计划还没有任何自动化把它钉成可诊断行为。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WidgetRenderTransformNullGuard` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp` |
| 场景描述 | 在同一模块里保留一条有效 widget 的 `GetRenderTransform()` baseline，然后脚本显式构造 `UWidget Widget = null` 并调用 `Widget.GetRenderTransform()`；如果项目脚本桥支持捕获运行时异常，再把错误文本导回测试。 |
| 输入/前置 | 使用已有 widget fixture 或最小 `UUserWidget` 子类作为正路径；另准备空 `UWidget` 句柄。若当前实现会记录日志、脚本异常或 `ensure`，用 `AddExpectedError` 把诊断固定下来，避免测试依赖进程级崩溃。 |
| 期望行为 | 有效 widget 路径返回与 native 设定一致的 `FWidgetTransform`；null widget 路径不应导致进程崩溃，而应表现为稳定、可诊断的失败契约: 要么返回固定默认值并记录错误，要么抛出脚本异常并带可匹配文本。无论最终契约选择哪一种，都需要由测试明确固定下来。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + `AddExpectedError`（如实现走日志/异常契约） |
| 优先级 | P0 |

#### NewTest-45：`GetLocalPlayerSubsystem` 的多种 context-object 解析分支

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h` |
| 关联函数 | `GetLocalPlayerSubsystem` / `GetLocalPlayerSubsystemFromPlayerController` / `GetLocalPlayerSubsystemFromLocalPlayer` |
| 现有测试覆盖 | 现有 `SubsystemLookup` 建议主要覆盖“给定上下文能否拿到 subsystem”与无 local player 的 controller 负路径，但没有把 `USubsystemBlueprintLibrary::GetLocalPlayerSubsystem` 内部针对 `UUserWidget`、`APlayerController`、`APawn`、`AActor`、`UActorComponent`、`ULocalPlayer` 六种 context-object 的解析分支固定下来 |
| 风险评估 | 这组 helper 一旦在某个 context 分支上解析错 `LocalPlayer`，脚本 UI、pawn、actor component 或 instigator 驱动的本地玩家逻辑会只在特定宿主类型下失效，且基础“controller 能拿到 subsystem”测试无法发现 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.SubsystemLocalPlayerContextResolution` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemFunctionLibraryTests.cpp` |
| 场景描述 | 在同一 local player 环境下准备六种 context object：`UUserWidget`（设置 owning local player）、`APlayerController`、被该 controller possession 的 `APawn`、`InstigatorController` 指向该 controller 的 `AActor`、owner/instigator 链接到该 actor 的 `UActorComponent`、以及 `ULocalPlayer` 本身。脚本分别调用 `GetLocalPlayerSubsystem(Context, TestSubsystemClass)`，再和 `GetLocalPlayerSubsystemFromPlayerController` / `GetLocalPlayerSubsystemFromLocalPlayer` 的结果做交叉对照。 |
| 输入/前置 | 创建最小 `ULocalPlayerSubsystem` 测试子类与本地 player/controller/pawn；actor 需要显式设置 `Instigator` / `InstigatorController`，component 需要挂到该 actor；widget 需要设置 `SetOwningLocalPlayer(LocalPlayer)`；另准备一个不关联任何 local player 的 actor/component 负样例。 |
| 期望行为 | 六条正路径都应返回同一个 `ULocalPlayerSubsystem` 实例，且与 native `LocalPlayer->GetSubsystemBase(Class)` 一致；`GetLocalPlayerSubsystemFromPlayerController` 和 `GetLocalPlayerSubsystemFromLocalPlayer` 的返回值也应与上述实例相同；不关联 local player 的 actor/component 负路径返回 `nullptr`。这样才能把各 context 分支而不是单一 controller happy path 真正固定下来。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + native subsystem test classes + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

## 测试审查 (2026-04-08 19:18)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-40：`EmptyTag` 在 `GameplayTagMixinLibrary` helper 上的无效输入契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h` |
| 关联函数 | `MatchesTag` / `MatchesTagExact` / `MatchesTagDepth` / `MatchesAny` / `MatchesAnyExact` / `GetSingleTagContainer` / `RequestDirectParent` / `GetGameplayTagParents` |
| 现有测试覆盖 | `GameplayTagCompat` 只验证 `EmptyDefault.IsValid()`、`GetTagName().IsNone()` 与 `ToString()`；空 tag 输入到 hierarchy / container helper 的返回语义完全没有测试 |
| 风险评估 | 如果 `EmptyTag` 在这些 helper 上返回脏数据、错误匹配已注册 tag，或父级/容器展开行为漂移，脚本侧标签过滤会在最常见的“默认未初始化 tag”场景里静默出错 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayTagEmptyTagContracts` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | 复用已有 gameplay tag 测试基建，挑选一个已注册 `ValidTag`，再在脚本里构造 `FGameplayTag EmptyTag`。分别对 `EmptyTag` 调用 `MatchesTag`、`MatchesTagExact`、`MatchesTagDepth`、`MatchesAny`、`MatchesAnyExact`、`GetSingleTagContainer`、`RequestDirectParent`、`GetGameplayTagParents`，并和 `ValidTag`、包含 `ValidTag` 的 container 做对照。 |
| 输入/前置 | `UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false)` 选出一个已注册 tag；脚本里准备 `FGameplayTag EmptyTag;`、`FGameplayTag ValidTag = FGameplayTag::RequestGameplayTag(...)`、`FGameplayTagContainer ValidContainer`。使用独立 share-clean engine，避免 tag 测试之间互相污染。 |
| 期望行为 | `EmptyTag.MatchesTag(ValidTag)`、`MatchesTagExact(ValidTag)`、`MatchesAny(ValidContainer)`、`MatchesAnyExact(ValidContainer)` 全部返回 `false`；`MatchesTagDepth(ValidTag)` 返回 `0`；`GetSingleTagContainer()` 返回空 container；`RequestDirectParent()` 返回 invalid/empty tag；`GetGameplayTagParents()` 不应包含 `ValidTag`，且结果保持空或只包含 empty 默认值，具体以 native reference 为准并在测试中固定。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
 
---

## 测试审查 (2026-04-09 23:49)

### 一、现有测试问题

本轮无新增。

### 二、需要新增的测试

#### NewTest-68：`GameplayLibrary` 并发异步请求的 callback 隔离契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h` |
| 关联函数 | `AsyncSaveGameToSlot` / `AsyncLoadGameFromSlot` |
| 现有测试覆盖 | 现有 `AsyncSaveLoadDelegates` / `AsyncSaveLoadCallbackThreadAffinity` / `GameplayAsyncUnboundDelegates` 建议都只覆盖单条或串行请求，没有固定“两条异步请求同时在途”时的 delegate payload 隔离 |
| 风险评估 | 这两个 wrapper 会把 dynamic delegate 拷贝进异步 lambda。若并发请求下 delegate、`SlotName`、`SaveGameObject` 或回调记录发生串线，脚本存档系统会只在高并发或多存档界面里静默把 A 槽结果写到 B 槽回调，现有建议无法拦住这种回归 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GameplayAsyncConcurrentCallbacks` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayFunctionLibraryTests.cpp` |
| 场景描述 | 准备两份带不同 marker 值的 `USaveGame` 派生对象和两个唯一 `SlotName`。脚本先在同一帧内连续发起两条 `AsyncSaveGameToSlot` 请求，并把 save callback 都绑定到稳定的 native recorder；待两条 save callback 都完成后，再对两个 slot 连续发起 `AsyncLoadGameFromSlot`，同样记录各自 callback 的 `SlotName`、调用次数和加载出的 marker。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN`；`SlotA` / `SlotB` 使用唯一字符串，`UserIndex` 统一固定为 `0` 以降低平台差异；native recorder UObject 至少记录 `SaveCallCountBySlot`、`LoadCallCountBySlot`、`LoadedMarkerBySlot`、`bAllCallbacksOnGameThread`；测试主体用 `FEvent` 或轮询等待 4 条 callback 全部完成，并在收尾删除两个 save slot。 |
| 期望行为 | 两条 save callback 都恰好触发一次，且各自收到的 `SlotName` 与发起请求时一致；随后两条 load callback 也都恰好触发一次，`LoadedMarkerBySlot[SlotA]` 与 `LoadedMarkerBySlot[SlotB]` 分别等于各自写入值，不能互换或复用同一对象内容；4 条 callback 都必须运行在 game thread，且任一槽位若出现丢回调、重复回调或 payload 串线，测试必须直接失败。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + native recorder UObject + `BuildModule` + `ExecuteIntFunction` + `FEvent`/轮询等待 |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
 
---

## 测试审查 (2026-04-08 19:18)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-41：空 `FGameplayTagQuery` 对 `Matches` / `GetDescription` 的默认契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h` |
| 关联函数 | `Matches` / `GetDescription` |
| 现有测试覆盖 | `GameplayTagQueryCompat` 只检查空 query 的 `IsEmpty()` 和与 `EmptyQuery` 的相等性，没有执行 `EmptyQuery.Matches(...)` 或读取 `GetDescription()` |
| 风险评估 | 如果默认 query 在脚本侧错误地“匹配任意容器”或生成非空描述文本，标签查询分支和调试 UI 会在未初始化 query 场景里持续误判，且现有自动化没有信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayTagQueryEmptyQueryContracts` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | 选取一个已注册 `ValidTag`，在脚本里准备 `FGameplayTagQuery EmptyQuery;`、空 `FGameplayTagContainer EmptyContainer;` 和只含 `ValidTag` 的 `NonEmptyContainer`。分别调用 `EmptyQuery.Matches(EmptyContainer)`、`EmptyQuery.Matches(NonEmptyContainer)` 与 `EmptyQuery.GetDescription()`。 |
| 输入/前置 | 复用 `UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false)` 选出 `ValidTag`；测试前用 native reference 固定 UE 语义，当前引擎自带 `GameplayTagQueryTests.cpp` 明确要求空 query 对空/非空 container 都返回 `false`。 |
| 期望行为 | `EmptyQuery.IsEmpty()` 为 `true`；`EmptyQuery.Matches(EmptyContainer)` 与 `EmptyQuery.Matches(NonEmptyContainer)` 都返回 `false`；`EmptyQuery.GetDescription()` 返回空字符串；对照 `NonEmptyContainer.MatchesQuery(EmptyQuery)` 也应保持 `false`，确保 query 成员方法和 container helper 在空 query 场景下一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |

### 本轮汇总（对应 `2026-04-08 19:44` 轮次）

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-24 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |
| P1 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

### 本轮汇总（对应 `2026-04-08 19:44` 轮次）

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-24 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |
| P1 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-08 19:31)

### 一、现有测试问题

#### Issue-22：`GeneratedBlueprintCallableEntriesPopulateClassMaps` 只抽查引擎原生 callable，FunctionLibrary 的 class-map 路径仍是盲区

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.GeneratedBlueprintCallableEntriesPopulateClassMaps` |
| 行号范围 | 493-552 |
| 问题描述 | 用例只检查了 `AActor::K2_DestroyActor`、`UGameplayStatics::GetPlayerController` 和 `UASClass::IsDeveloperOnly` 三个条目是否出现在 `FAngelscriptBinds::GetClassFuncMaps()` 里；这三者都不是 `FunctionLibraries/` 下通过 mixin/辅助库挂到宿主类型上的 API。测试没有抽查任何真正依赖 FunctionLibrary 注册链路的条目，例如 `USceneComponent::GetRelativeLocation()`、`ULevelStreaming::GetShouldBeVisibleInEditor()`、`UWidget::GetRenderTransform()` 或 `UWorld::GetStreamingLevels()`。因此即使 FunctionLibrary 生成的 class-map 入口整体漏注册、挂到了错误宿主类型，当前测试仍会稳定通过。 |
| 影响 | bind-config 套件会对 FunctionLibraries 模块给出误报绿灯，尤其无法拦住 “反射条目仍存在，但 mixin 入口没有进入目标类型 class map” 这类回归。 |
| 修复建议 | 在当前用例里保留原生 callable 的 smoke 断言，但至少再补 2 到 3 个来自生产 FunctionLibrary 的代表性条目：例如验证 `UAngelscriptComponentLibrary` 生成的 `GetRelativeLocation` 出现在 `USceneComponent` 的 class map，`UAngelscriptLevelStreamingLibrary` 生成的 `GetShouldBeVisibleInEditor` 出现在 `ULevelStreaming`，以及 `UWidgetBlueprintStatics`/`UAngelscriptWidgetMixinLibrary` 的入口出现在对应宿主类型。断言不仅要检查 key 存在，还要确认宿主类正确、`FFuncEntry` 已绑定，避免继续只测“引擎原生 callable 本身能进表”。 |

#### Issue-23：`CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` 只验证理想化 coverage library，没覆盖真实函数库上的 metadata 漂移

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` |
| 行号范围 | 647-703 |
| 问题描述 | 用例只从 `UAngelscriptUhtCoverageTestLibrary` 取 `RequiresWorldContext` 和 `CallableWithoutWorldContext` 两个专门设计的覆盖函数，随后断言 `hiddenArgumentIndex` 和 `asTRAIT_USES_WORLDCONTEXT`。它没有把任何真实 FunctionLibrary 函数送进 `FAngelscriptFunctionSignature`，尤其没有覆盖 `WidgetBlueprintStatics.h` 里的 `UAngelscriptWidgetMixinLibrary::GetRenderTransform`。该生产函数当前带有 `WorldContext = "WorldContextObject"` 元数据，但签名里根本不存在这个参数；由于测试只覆盖理想化 fixture，这类 production metadata 漂移会被静默放过。 |
| 影响 | 绑定元数据测试虽然是绿的，但真实函数库仍可能带着错误或失配的 world-context 声明进入发布分支；文档生成、静态检查和脚本签名行为会继续和生产 API 脱节。 |
| 修复建议 | 把当前 synthetic coverage 保留为基线，同时新增对真实函数库的断言：直接为 `UWidgetBlueprintStatics::CreateWidget` 和 `UAngelscriptWidgetMixinLibrary::GetRenderTransform` 构造 `FAngelscriptFunctionSignature`。前者应隐藏 `WorldContextObject` 并保留 world-context trait；后者应暴露单一 `UWidget*` 参数且不带 hidden world context / trait。这样才能把“理想 UHT 样例”和“生产函数库实际元数据”同时钉住。 |

### 二、需要新增的测试

本轮未新增新的测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-22 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:21)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-42：空 `FGameplayTagContainer` 在 `HasAll` / `HasAny` / `HasTag` 上的默认语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` |
| 关联函数 | `HasTag` / `HasTagExact` / `HasAny` / `HasAnyExact` / `HasAll` / `HasAllExact` |
| 现有测试覆盖 | `GameplayTagContainerCompat` 只有单标签非空正路径，没有任何空 container / empty tag 输入断言 |
| 风险评估 | 如果空 container 的 `HasAll` / `HasAny` 语义在脚本层和 UE 原生漂移，标签前置条件、默认配置和未初始化容器判断会持续出现“空集合也匹配”或“所有空集都不匹配”的静默逻辑错误 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayTagContainerEmptyContracts` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | 选取一个已注册 `ValidTag`，在脚本里构造 `NonEmptyContainer`、`EmptyContainer` 和 `FGameplayTag EmptyTag`。分别断言 `NonEmptyContainer.HasAll(EmptyContainer)`、`NonEmptyContainer.HasAny(EmptyContainer)`、`EmptyContainer.HasAll(EmptyContainer)`、`EmptyContainer.HasAny(EmptyContainer)`、`EmptyContainer.HasAll(NonEmptyContainer)`、`EmptyContainer.HasAny(NonEmptyContainer)` 以及 `NonEmptyContainer.HasTag(EmptyTag)` / `HasTagExact(EmptyTag)`。 |
| 输入/前置 | 用 `UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false)` 选出一个稳定的 `ValidTag`；脚本构造 `NonEmptyContainer.AddTag(ValidTag)`。测试前可引用 UE 本地 `GameplayTagTests.cpp` 中的空容器断言作为 native reference，固定项目当前语义。 |
| 期望行为 | `NonEmptyContainer.HasAll(EmptyContainer)` 与 `HasAllExact(EmptyContainer)` 返回 `true`；对应的 `HasAny` / `HasAnyExact` 返回 `false`；`EmptyContainer.HasAll(EmptyContainer)` 与 `HasAllExact(EmptyContainer)` 返回 `true`；`EmptyContainer.HasAny(EmptyContainer)` 与 `HasAnyExact(EmptyContainer)` 返回 `false`；`EmptyContainer.HasAll(NonEmptyContainer)`、`HasAllExact(NonEmptyContainer)`、`HasAny(NonEmptyContainer)`、`HasAnyExact(NonEmptyContainer)` 全部返回 `false`；`NonEmptyContainer.HasTag(EmptyTag)` 与 `HasTagExact(EmptyTag)` 返回 `false`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 11:23)

### 一、现有测试问题

本轮未发现新的现有测试问题。

### 二、需要新增的测试

#### NewTest-49：`CreateWidget` 的隐式 owning-player / world-context fallback

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` |
| 关联函数 | `CreateWidget` |
| 现有测试覆盖 | 现有 `UMG helper` 建议只覆盖“显式传入 `OwningPlayer` 的正路径 + `WidgetClass == null` 负路径”，没有固定 `OwningPlayer == null` 时 `WorldContextObject` 的三条 fallback 分支 |
| 风险评估 | `UWidgetBlueprintLibrary::Create(...)` 当前会按 `OwningPlayer` → `WorldContextObject as APlayerController` → `WorldContextObject as UUserWidget` → `WorldContextObject as UWorld` 的顺序推导创建路径。若 Angelscript wrapper 在参数顺序、world-context 隐藏或 null-owner 分支上漂移，脚本 UI 会只在“没显式传 player”这一高频用法下静默失败，而现有建议还拦不住 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WidgetCreateWidgetWorldContextFallback` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp` |
| 场景描述 | 在同一 world 中准备 `APlayerController`、最小 `UUserWidget` 子类和一个已创建的 owner widget。脚本分三次调用 `WidgetBlueprint::CreateWidget`：1）`OwningPlayer == null` 且 `WorldContextObject` 传 `PlayerController`；2）`OwningPlayer == null` 且 `WorldContextObject` 传 `World`；3）`OwningPlayer == null` 且 `WorldContextObject == null`。必要时再补 `WorldContextObject` 为 `UUserWidget` 的 owner-widget 分支，对照 native `UWidgetBlueprintLibrary::Create`。 |
| 输入/前置 | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 创建 world / player controller；准备 transient widget class；native 侧直接调用 `UWidgetBlueprintLibrary::Create(WorldContextObject, WidgetClass, nullptr)` 生成 mirror 结果，用来固定各分支当前契约。 |
| 期望行为 | `WorldContextObject == PlayerController` 时脚本返回的 widget 非空，且 `GetOwningPlayer()` 与该 controller 一致；`WorldContextObject == World` 时脚本仍返回非空 widget，但 `GetOwningPlayer()` 保持 `nullptr`；`WorldContextObject == null` 时返回 `nullptr` 且不崩溃；若补 `UUserWidget` 分支，则其结果必须与 native mirror 一致。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P1 |

#### NewTest-50：`FQualifiedFrameTime.AsSeconds` 的负帧与 NTSC 精度

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h` |
| 关联函数 | `AsSeconds` |
| 现有测试覆盖 | 现有 `FrameTimeAsSeconds` 建议只覆盖整数帧和普通分数帧，没有固定负帧值与 `24000/1001`、`30000/1001` 这类非整数帧率下的秒数精度 |
| 风险评估 | `AsSeconds()` 直接执行 `Time / Rate`。如果 Angelscript 对 `FFrameTime` 的负值、subframe 或非整数 `FFrameRate` 处理有符号/精度漂移，Sequencer、Timecode 和媒体同步脚本会在最常见的 NTSC 速率下稳定算错时间，而当前计划还没有专门信号 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.FrameTimeAsSecondsNtscAndNegative` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFrameTimeFunctionLibraryTests.cpp` |
| 场景描述 | 脚本分别构造三组 `FQualifiedFrameTime`：1）`FFrameTime(-10)` + `FFrameRate(24000, 1001)`；2）`FFrameTime(10, 0.5f)` + `FFrameRate(24000, 1001)`；3）`FFrameTime(0)` + `FFrameRate(30000, 1001)`。调用 `AsSeconds()` 后把结果回传给 C++，再与 native `FQualifiedFrameTime(...).AsSeconds()` 对照。 |
| 输入/前置 | 使用确定性 frame/rate 组合，避免依赖外部时钟；C++ 侧准备 mirror reference，并用 `KINDA_SMALL_NUMBER` 或更严格容差比较 double 结果。若脚本 helper 只能返回整数错误码，可把 double 差值比较留在 native 侧。 |
| 期望行为 | 三组输入的脚本结果都应与 native `AsSeconds()` 一致；负帧值保持负号，不得被截断成 0 或绝对值；`10.5` 帧在 `24000/1001` 下的结果应保留 subframe 精度；零帧输入始终返回 `0.0`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteDoubleFunction`（或等价 native mirror 比对 helper） |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 11:55)

### 一、现有测试问题

#### Issue-26：`GameplayTagContainerCompat` 只验证 `First()`，完全漏掉 `Last()` 的尾元素契约

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagContainerCompat` |
| 行号范围 | 111-165 |
| 问题描述 | 这条 dedicated container compat 用例在 `Tags` 上断言了 `Num()`、`HasTag*()` 和 `First()`，但从头到尾没有调用 `Last()`。`GameplayTagContainerMixinLibrary.h` 同时导出了 `First` 和 `Last` 两个访问器，而当前脚本数据始终只有单个 `ValidTag`，所以即使 `Last()` 没有正确绑定、返回了错误元素，或者在 `AppendTags` / `RemoveTag` 后尾元素顺序漂移，这个测试也仍然会返回 `1`。 |
| 影响 | `GameplayTagContainerMixinLibrary::Last()` 目前处于“有 dedicated compat 文件却无任何断言”的状态，搜索现有覆盖时很容易被误判成已被 `GameplayTagContainerCompat` 顺手保护；一旦尾元素访问在脚本 UI、tag 栈展示或容器遍历逻辑里回归，自动化不会给出信号。 |
| 修复建议 | 不要继续用单标签容器做“只测 `First()`”的 smoke。扩展当前文件或新增单独用例，挑选两个稳定且不相等的已注册 tag，分别在原始顺序、`AppendTags` 后、`RemoveTag` 后对 `First()` / `Last()` 做成对断言，并与 native `FGameplayTagContainer` mirror 结果逐步对照；如果引擎当前并不保证插入顺序，也要把 script/native 的实际 `Last()` 契约固定下来，而不是让它继续完全无测试。 |

### 二、需要新增的测试

#### NewTest-51：`FGameplayTagContainer.Last()` 的多元素尾访问契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` |
| 关联函数 | `First` / `Last` / `AddTag` / `AppendTags` / `RemoveTag` |
| 现有测试覆盖 | `GameplayTagContainerCompat` 只在单标签容器上断言 `First()`，`Last()` 完全没有 dedicated 断言 |
| 风险评估 | 一旦 `Last()` 绑定错位、尾元素顺序在 append/remove 后漂移，脚本侧 tag 栈展示、最近标签读取和容器遍历逻辑会静默读到错误元素，而现有测试仍然全绿 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayTagContainerFirstLastOrdering` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | 从 `UGameplayTagsManager` 选出两个稳定且不相等的已注册 tag，原生侧先用同样的 `AddTag` / `AppendTags` / `RemoveTag` 序列构造 mirror container，再让脚本容器执行同样操作并返回每一步的 `First()` / `Last()` 结果。至少覆盖：1）单标签 baseline；2）两个标签并存；3）移除尾元素后只剩一个标签；4）`Reset()` 后两者都回到 empty/default。 |
| 输入/前置 | 选择 `TagA != TagB`；native mirror container 与脚本容器使用完全相同的变更顺序；必要时把 `First()` / `Last()` 的 `ToString()` 结果导回 C++，避免仅做 object identity 比较。 |
| 期望行为 | 在每一步上，脚本 `First()` / `Last()` 与 native mirror `FGameplayTagContainer::First()` / `Last()` 完全一致；双标签阶段 `Last()` 必须能区分于 `First()`；移除尾元素后 `Last()` 与剩余元素同步回退；`Reset()` 后 `First()` / `Last()` 都返回 empty/invalid tag。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction`（或等价 tag-string 回传 helper） |
| 优先级 | P2 |

#### NewTest-52：Subsystem 查询 helper 的 `null TSubclassOf` 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h` |
| 关联函数 | `GetEngineSubsystem` / `GetGameInstanceSubsystem` / `GetLocalPlayerSubsystem` / `GetWorldSubsystem` / `GetLocalPlayerSubsystemFromPlayerController` / `GetLocalPlayerSubsystemFromLocalPlayer` |
| 现有测试覆盖 | 现有 `SubsystemLookup` / `GetLocalPlayerSubsystem` 建议覆盖有效上下文和部分空 world/local-player 路径，但没有固定 `Class == null` 这一组最基础的无效输入 |
| 风险评估 | 如果脚本把空 `TSubclassOf` 传进 subsystem helper 时发生崩溃、返回脏对象或错误沿用上一次 class 结果，运行时服务定位会在最难诊断的“配置没给 class”场景里静默失真 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.SubsystemNullClassContracts` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemFunctionLibraryTests.cpp` |
| 场景描述 | 在已有 engine/world/game instance/local player 夹具上，同时准备有效 context object 和 `TSubclassOf<...> NullClass = null`。脚本分别调用六个 subsystem helper，把返回对象是否为空导回 C++；保留一条有效 class baseline，确认“null class 返回空”不是因为环境本身没建好。 |
| 输入/前置 | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 创建 world、game instance、local player、player controller；准备一个可用的测试 subsystem class 作为 baseline，再额外传入 `null` class。 |
| 期望行为 | 六个 helper 在 `Class == null` 时都应安全返回 `nullptr`，且不崩溃、不污染有效 baseline 结果；同一上下文下改用有效 class 时仍应返回正确 subsystem 实例，证明 null-class 路径只是安全失败而不是环境初始化失败。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` |
| 优先级 | P2 |

#### NewTest-53：WorldCollision component query 的 null 发起组件错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WorldCollisionStatics.h` |
| 关联函数 | `ComponentSweepMulti` / `ComponentOverlapMulti` |
| 现有测试覆盖 | 已有建议覆盖命中 world 几何体的正路径和 async callback payload，但还没有固定 `UPrimitiveComponent Source == null` 时 component query helper 的失败契约 |
| 风险评估 | 组件查询通常从脚本持有的运行时组件直接发起；一旦组件已销毁、尚未创建或传错句柄，helper 如果直接崩溃或残留旧结果，交互检测会在最常见的失效路径里变成进程级错误 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WorldCollisionNullComponentQueries` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp` |
| 场景描述 | 复用 world collision 夹具中的 blocker/profile 配置，但把 `UPrimitiveComponent SourceComponent = null` 传给 `System::ComponentSweepMulti` 和 `System::ComponentOverlapMulti`。脚本回传 bool 结果、输出数组数量以及是否出现脚本异常；必要时再保留一条有效组件 baseline，对照真正的命中路径。 |
| 输入/前置 | 有效 world、query params、object query params 与 `FCollisionShape`；一条有效已注册组件路径作 baseline；另一条 null component 路径使用完全相同的 world/query 设置。若当前实现记录日志或抛脚本异常，使用 `AddExpectedError` 固定错误文本。 |
| 期望行为 | null component 路径应以稳定、可诊断的方式失败：bool 返回 `false`，`OutHits` / `OutOverlaps` 保持空，不得崩溃，也不得把 baseline 命中结果泄漏到本次输出；有效组件 baseline 仍应命中目标，证明失败来自 null 输入而非场景搭建错误。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + `AddExpectedError`（如实现走日志/异常契约） |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-26 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 2 | MissingScenario: 1, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 12:09)

### 一、现有测试问题

#### Issue-27：`WorldCollisionCompile` 连 compile surface 都只抽到少量样例，整组 async sweep / by-channel overload 仍处于盲区

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.WorldCollisionCompile` |
| 行号范围 | 264-300 |
| 问题描述 | 当前脚本片段只编译了 `LineTraceTestByChannel`、`SweepSingleByObjectType`、`OverlapMultiByProfile`、`ComponentSweepMulti`、`ComponentOverlapMulti` 和 `AsyncOverlapByProfile` 六个调用。对照 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` 221-452 行，运行时还额外注册了 `SweepMultiByChannel/ObjectType/Profile`、`ComponentSweepMultiByChannel`、`ComponentOverlapMultiByChannel`、`AsyncLineTraceByObjectType/Profile`、`AsyncSweepByChannel/ObjectType/Profile`、`AsyncOverlapByChannel/ObjectType` 等整组 overload。也就是说，这条“WorldCollision parity” 用例不仅不执行行为，连 compile smoke 都没有覆盖到多数高风险入口；这些签名即使整体丢失、参数顺序漂移或默认参数回退，当前测试也仍会稳定通过。 |
| 影响 | `WorldCollisionCompile` 现在会给人一种“System:: collision API 已有广覆盖 compile 守卫”的错觉，实际上 async sweep、by-channel component query 和 multi-sweep 家族都没有任何现有红灯信号，回归能直接滑过 CI。 |
| 修复建议 | 不要继续把单个脚本片段当成整组 WorldCollision overload 的 compile coverage。至少新增一个专门的 overload-matrix smoke，把 `SweepMultiBy*`、`Component*ByChannel`、`AsyncLineTraceByObjectType/Profile`、`AsyncSweepBy*`、`AsyncOverlapByChannel/ObjectType` 全部编译进同一组或拆成 2 到 3 个单职责 case；随后让这些 compile smoke 紧邻 dedicated runtime 场景测试，避免再次出现“只抽一两个样例就宣称整组 API 已覆盖”。 |

### 二、需要新增的测试

#### NewTest-54：`AsyncSweepBy*` 的 callback payload 与 overload 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WorldCollisionStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` |
| 关联函数 | `AsyncSweepByChannel` / `AsyncSweepByObjectType` / `AsyncSweepByProfile` / `FScriptTraceDelegate` |
| 现有测试覆盖 | `WorldCollisionCompile` 只编译 `AsyncOverlapByProfile`；已有 `WorldCollisionAsyncCallbacks` 建议主要覆盖 `AsyncLineTrace*` / `AsyncOverlap*`，`AsyncSweepBy*` 仍没有 dedicated 行为级断言 |
| 风险评估 | async sweep 既走独立 overload，又复用 `FScriptTraceDelegate` 把 `TraceHandle`、`OutHits`、`UserData` 回传脚本。若 sweep 专属参数、delegate 桥接或回调分发回归，角色前探检测、体积扫掠和预测碰撞会只在异步路径下静默失真，而当前计划还没有单独信号。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.WorldCollisionAsyncSweepCallbacks` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp` |
| 场景描述 | 在独立 world 中放置一个带已知 collision channel / object type / profile 的 blocking cube，并准备 native receiver UObject 记录 `TraceHandle`、`UserData`、callback 次数和首个 `Hit` 的 actor/component。脚本分别发起 `AsyncSweepByChannel`、`AsyncSweepByObjectType`、`AsyncSweepByProfile` 三条请求，三次都使用可区分的 `UserData` 与同一 `FCollisionShape`；必要时再用 `QueryTraceData` 对返回 handle 做 native mirror 对照。 |
| 输入/前置 | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 创建 world；阻挡体配置成同时满足测试 channel、object query 和 profile；native receiver 暴露 `UFUNCTION()` 形式的 trace callback，并用 `FEvent` 或轮询等待三条异步回调全部完成；脚本为三条请求返回 distinct error code，区分 handle 无效、callback 超时、命中对象错误和 `UserData` 不匹配。 |
| 期望行为 | 三条 `AsyncSweepBy*` 都返回有效 `FTraceHandle`，且各自 callback 恰好触发一次；`UserData` 与发起请求时的值逐一对应，不能串线；`OutHits.Num() >= 1`，首个 blocking hit 指向预期 blocker，且与 native `QueryTraceData` 或同步 mirror 结果一致；任何一条 overload 若未回调、payload 为空或命中对象错误，测试必须直接失败。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `ASTEST_BUILD_MODULE` + native receiver UObject + `FEvent`/轮询等待 |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-27 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:20 补记)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 12:19)` 段落；这里仅补一个尾部定位，避免重复抄写相同内容。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-28 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 12:34 尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 12:34)` 段落；这里补一个真正位于文件末尾的定位，避免再次重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:49 尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 12:49)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文被插入到旧定位段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-29 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |
 
---

## 测试审查 (2026-04-09 13:01 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:01)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文再次被插入到旧定位段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-30 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 13:16 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:14)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧汇总段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 13:27 真正尾部定位-EOF2)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 13:27)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧段落之前。

### 二、需要新增的测试

本轮无新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 2 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |

---

## 测试审查 (2026-04-09 23:27)

### 一、现有测试问题

#### Issue-34：`SoftObjectPtrCompat` 这个 dedicated soft-reference 用例仍然完全绕开了 `FOnSoftObjectLoaded`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SoftObjectPtrCompat` |
| 行号范围 | 84-190 |
| 问题描述 | 这条用例从 95-167 行一路只覆盖 `TSoftObjectPtr` 的 `IsNull()`、`IsValid()`、`Get()`、`ToSoftObjectPath()`、数组存取和 `Reset()`，并且样本始终来自 `NewObject(...)` 创建的已加载 transient `UTexture2D`。脚本里没有声明 `FOnSoftObjectLoaded`，也没有对任何 soft path 调用 `LoadAsync(...)`。但本轮目标中的 `SoftReferenceStatics.h` 实际只导出了 `FOnSoftObjectLoaded` / `FOnSoftClassLoaded` 两个异步 delegate 类型；换句话说，这个名为 `SoftObjectPtrCompat` 的 dedicated 文件仍然把最关键的 function-library 契约完全留在测试外。即使 `LoadAsync(...)` 的 object payload 类型退化、成功回调不触发，或者失败路径不回调，当前用例也仍会稳定返回 `1`。 |
| 影响 | 当前仓库看起来既有 parity smoke，又有 dedicated `SoftObjectPtrCompat`，很容易让人误判 soft-reference runtime 已经有双重保护；实际上 `SoftReferenceStatics` 的 object async delegate 仍然没有任何执行级断言，回归会直接滑过 CI。 |
| 修复建议 | 保留这条同步 compat 用例，但不要再让它承担 function-library 覆盖的错觉。新增或拆出 `Angelscript.TestModule.FunctionLibraries.SoftReferenceAsyncDelegates`：准备一个存在的 `TSoftObjectPtr<UTexture2D>`、一个明确不存在的 path 和一个 native/script receiver，脚本显式绑定 `FOnSoftObjectLoaded` 调用 `LoadAsync(...)`；断言成功路径只回调一次、payload 指向预期对象，失败路径也会回调且 payload 为 `null`。如果项目希望回调保留模板子类型，还要在脚本里直接把参数当 `UTexture2D` 消费，而不是仅在 native 侧做宽类型 smoke。 |

#### Issue-35：`TSoftClassPtrCompat` 也没有任何 `FOnSoftClassLoaded` / `LoadAsync` 执行路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.TSoftClassPtrCompat` |
| 行号范围 | 186-271 |
| 问题描述 | 205-248 行的脚本体只验证了 `TSoftClassPtr` 在已加载 native class 上的 `Get()`、`IsValid()`、`ToString()`、赋值、数组存取和 `Reset()`；整个文件没有声明 `FOnSoftClassLoaded`，也没有让 `TSoftClassPtr` 走一次 `LoadAsync(...)`。这意味着 class 侧现有 dedicated compat 测试仍停留在同步/已解析对象表面，和 `SoftReferenceStatics.h` 中真正需要防守的异步 class delegate 契约完全脱节。即使 class async callback 的 payload 顺序错了、模板子类型退化成宽 `UClass*` 后不可用，或者缺失路径根本不触发回调，当前测试依然会全绿。 |
| 影响 | `SoftReferenceStatics` 的 class delegate 现在既没有 dedicated runtime 断言，也没有从 `TSoftClassPtrCompat` 获得任何旁证保护；soft class 异步加载一旦回归，只有用户运行时踩到路径才会暴露问题。 |
| 修复建议 | 参考 object 侧，把 class async 合并进专用 soft-reference 测试文件，或在当前文件旁边新增单职责 case：准备一个可解析的 class path、一个缺失 class path 和 receiver，脚本绑定 `FOnSoftClassLoaded` 调 `LoadAsync(...)`；成功路径断言回调只触发一次且拿到预期 class，失败路径断言仍会回调且 payload 为 `null`。如果当前脚本 API 宣称 class 回调可直接作为 `TSubclassOf<T>`/目标子类使用，测试必须在脚本侧固定这层类型语义，而不是只比较裸 `UClass*` 非空。 |

### 二、需要新增的测试

#### NewTest-66：`GameplayLibrary` 的未绑定 delegate fire-and-forget 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h` |
| 关联函数 | `AsyncSaveGameToSlot` / `AsyncLoadGameFromSlot` |
| 现有测试覆盖 | 现有 `SaveLoadCallbacks` / `SaveLoadCallbacksOnGameThread` / `SaveLoadInvalidInputs` 建议都假定传入了已绑定 delegate，没有任何用例固定默认构造 delegate 走 `ExecuteIfBound` 的 fire-and-forget 路径 |
| 风险评估 | 这两个 wrapper 在 lambda 中直接复制动态 delegate 并调用 `ExecuteIfBound(...)`。如果未绑定 delegate 的复制、跨线程转发或完成时机回归，脚本里最常见的“只想异步保存，不关心回调”用法会从静默成功退化成崩溃、异常或根本不落盘，而现有建议都捕不到这条边界。 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GameplayAsyncUnboundDelegates` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayFunctionLibraryTests.cpp` |
| 场景描述 | 脚本构造带可识别字段的 `USaveGame` 派生对象，同时默认构造 `FAsyncSaveGameToSlotDynamicDelegate` 和 `FAsyncLoadGameFromSlotDynamicDelegate`，不绑定任何 receiver，直接调用 `AsyncSaveGameToSlot` 和 `AsyncLoadGameFromSlot`。native 测试体对 save 路径轮询 `UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex)`，再用同步 `LoadGameFromSlot` 校验保存内容；load 路径至少需要固定“未绑定 delegate 也不会崩溃/抛异常”。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN`；唯一 `SlotName`、固定 `UserIndex`、带标记字段的最小 `USaveGame` 类型；native 侧准备有限时长的轮询窗口和收尾删除 slot；如果当前实现会在未绑定 delegate 时记录 warning，用 `AddExpectedError` 固定日志。 |
| 期望行为 | `AsyncSaveGameToSlot` 在未绑定 delegate 时仍能成功完成：脚本调用本身不报错，save slot 在超时前出现，随后同步读回的对象字段与写入值一致；`AsyncLoadGameFromSlot` 在同样的未绑定 delegate 条件下也不应崩溃或挂起测试。如果实现会输出特定 warning，测试需把 warning 文本固定下来；不允许出现“未绑定就不保存”或“未绑定导致崩溃”的回归。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` + native save-slot 轮询 |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 23:38)

### 一、现有测试问题

#### Issue-36：多条 FunctionLibrary parity compile 用例直接污染 production engine，全局脚本状态没有隔离

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Parity.WorldCollisionCompile`；`Angelscript.TestModule.Parity.SoftReferenceCompile`；`Angelscript.TestModule.Parity.RuntimeCurveLinearColorCompile`；`Angelscript.TestModule.Parity.HitResultCompile` |
| 行号范围 | 264-304；454-485；534-570；573-622 |
| 问题描述 | 这 4 条用例都先通过 `GetProductionEngineForParity(...)` 拿到全局 `FAngelscriptEngine::Get()`，随后直接在这台 production engine 上创建或编译临时模块：`WorldCollisionCompile` / `RuntimeCurveLinearColorCompile` / `HitResultCompile` 分别调用 `GetModule("WorldCollisionParity" / "RuntimeCurveLinearColorParity" / "HitResultParity", asGM_ALWAYS_CREATE)` 再 `CompileFunction(...)`，`SoftReferenceCompile` 则通过 `BuildModule(*this, *Engine, "Editor.SoftReferenceParity", Source)` 走 `AngelscriptTestUtilities.h` 535-588 行的 `Engine.CompileModules(...)` 路径。它们最后只 `Release()` 了 `asIScriptFunction`，没有任何 `ON_SCOPE_EXIT`、`DiscardModule(...)` 或引擎 reset。结果是“只想做 compile smoke”的测试把临时脚本模块永久注入了全局脚本引擎，后续同进程测试看到的已经不是干净启动态。 |
| 影响 | 这会把本应纯观察的 parity smoke 变成顺序相关的全局状态修改。后续 bind/parity/metadata 测试可能意外读取到这些临时模块里的类型和函数，造成误报绿灯、重复注册噪声或难以复现的前后依赖；一旦有人继续沿用这个模式补更多 FunctionLibrary smoke，production engine 污染面只会扩大。 |
| 修复建议 | 不要在 production engine 上承载会编译模块的 FunctionLibrary parity case。把这 4 条测试迁到 `FAngelscriptTestFixture` / `CreateTestingFullEngine` 这样的隔离引擎，或至少在每条 case 里用 `ON_SCOPE_EXIT` 显式 `Engine.DiscardModule(TEXT(\"...\"))` 清理模块；如果必须保留全局引擎，只允许做只读 type-info 断言，不允许继续往里面编译临时脚本。 |

### 二、需要新增的测试

#### NewTest-67：`FGameplayTagContainer.RemoveTag` 的未命中返回值与容器不变契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` |
| 关联函数 | `RemoveTag` |
| 现有测试覆盖 | `GameplayTagContainerCompat` 只调用 `Combined.RemoveTag(ValidTag)` 的成功路径，随后只看容器是否为空；对“不存在的 tag”与 `EmptyTag` 输入的 bool 返回值和容器保持不变契约完全没有断言 |
| 风险评估 | `RemoveTag` 是高频容器 helper。若脚本桥把“未命中”也错误返回 `true`，或在 miss 路径意外改写容器内容，调用方的条件分支、UI tag 栈和 gameplay gating 都会静默漂移，而现有自动化不会报警 |
| 建议测试名 | `Angelscript.TestModule.FunctionLibraries.GameplayTagContainerRemoveTagMiss` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagContainerFunctionLibraryTests.cpp` |
| 场景描述 | native 侧挑选两个稳定且不相等的已注册 tag：`PresentTag` 与 `MissingTag`。脚本先创建只包含 `PresentTag` 的 `FGameplayTagContainer`，依次执行 `RemoveTag(MissingTag)`、`RemoveTag(FGameplayTag::EmptyTag)`、`RemoveTag(PresentTag)`，把每一步的返回值、`Num()` 和 `HasTagExact(PresentTag)` 状态回传给 C++。 |
| 输入/前置 | `UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false)` 选择两枚不同 tag；`ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN`；脚本用明确错误码区分 “miss 返回值错误”、“EmptyTag 路径错误” 和 “成功移除后容器状态错误”。 |
| 期望行为 | `RemoveTag(MissingTag)` 返回 `false`，且容器仍 `Num()==1`、`HasTagExact(PresentTag)==true`；`RemoveTag(FGameplayTag::EmptyTag)` 同样返回 `false` 且容器保持不变；最后 `RemoveTag(PresentTag)` 返回 `true`，容器变为空。所有路径都要与 native `FGameplayTagContainer` mirror 行为一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-36 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
 
---

## 测试审查 (2026-04-09 23:49 真正尾部定位-EOF3)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-09 23:49)` 段落；这里补一个真正位于文件末尾的定位，避免本轮正文继续插入到旧段落之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:16 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-10 00:04)` 段落；这里补一个真正位于文件末尾的定位，并给出本轮汇总，避免正文再次插入到旧定位段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-37 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:30 真正尾部定位-EOF4)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-10 00:04)` 和 `## 测试审查 (2026-04-10 00:24)` 段落；这里补一个真正位于文件末尾的总定位，避免本轮正文继续插入到旧定位段之前。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-37 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | 无 |

---

## 测试审查 (2026-04-10 00:40 真正尾部定位-EOF5)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-10 00:04)`、`## 测试审查 (2026-04-10 00:24)` 和 `## 测试审查 (2026-04-10 00:36)` 段落；这里补一个真正位于文件末尾的最终定位，固定本轮总汇总。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-37 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:41 真正尾部定位-EOF7)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-10 00:41)` 段落；这里补一个真正位于文件末尾的定位，固定本轮新增 issue 与 new test 的尾部归档位置。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-39 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:35 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-10 00:32)` 与 `## 测试审查 (2026-04-10 00:34)` 段落；这里补一个真正位于文件末尾的定位，固定本轮新增 issue 与 new test 的尾部归档位置。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-40 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:37)

### 一、现有测试问题

#### Issue-41：`BindConfig` 元数据套件完全没钉住 `SubsystemLibrary` 的 `DeterminesOutputType` 契约

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 测试名 | `Angelscript.TestModule.Engine.BindConfig.GeneratedBlueprintCallableEntriesPopulateClassMaps` / `Angelscript.TestModule.Engine.BindConfig.FunctionLevelScriptMethodUsesFirstParameterAsMixin` / `Angelscript.TestModule.Engine.BindConfig.CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` |
| 行号范围 | 515-551；633-644；677-703 |
| 问题描述 | 这三条现有 bind-config 断言分别只检查 class-map 里“有条目”、synthetic `ScriptMethod` 首参转成员、以及 synthetic world-context 的 hidden argument / trait；整套文件里没有任何一处提到 `DeterminesOutputType`、`determinesOutputTypeArgumentIndex`，也没有把 `USubsystemLibrary` 的真实函数送进断言链。可 `SubsystemLibrary.h` 18-62 行的 6 个 getter 全都声明了 `Meta = (DeterminesOutputType = "Class")`，而绑定层会在 `Helper_FunctionSignature` 中单独记录并写回 `ScriptFunction->determinesOutputTypeArgumentIndex`。也就是说，只要 `Class` 参数不再驱动返回类型特化，或在 world-context/mixin 参数调整后索引漂移，当前整套 bind-config 测试仍会全部通过。 |
| 影响 | `SubsystemLibrary` 的静态类型面目前没有自动化防线。脚本一旦从 `GetWorldSubsystem` / `GetLocalPlayerSubsystemFromLocalPlayer` 等入口退化为只能看到基类 `UWorldSubsystem` / `ULocalPlayerSubsystem`，或者 compiler 不再允许无 cast 访问子类成员，CI 仍可能给出绿灯。 |
| 修复建议 | 不要再让当前 suite 只覆盖 class-map、mixin 和 world-context 三个维度。新增 production bind-config 用例，至少把 `GetWorldSubsystem` 与 `GetLocalPlayerSubsystemFromLocalPlayer` 纳入同一链路：1）构造 `FAngelscriptFunctionSignature` 后断言 `DeterminesOutputTypeArgument` 指向 `Class` 形参；2）`ModifyScriptFunction` 后检查 `determinesOutputTypeArgumentIndex` 与隐藏/保留的 world-context 参数共同成立；3）再编译一段脚本，把返回值直接赋给具体测试子类 subsystem 并访问子类独有字段/方法，确保 typed return 是 end-to-end 生效的，而不是只在 metadata 层存在。 |

### 二、需要新增的测试

#### NewTest-73：`SubsystemLibrary` 的 `DeterminesOutputType` end-to-end typed return 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h` |
| 关联函数 | `GetEngineSubsystem` / `GetGameInstanceSubsystem` / `GetLocalPlayerSubsystem` / `GetWorldSubsystem` |
| 现有测试覆盖 | 现有建议与现有 issue 已覆盖 world-context、`no_discard`、null 输入和 context 解析，但完全没有 dedicated 测试固定 `DeterminesOutputType` 对返回静态类型的影响 |
| 风险评估 | 一旦 `Class` 形参不再驱动返回类型特化，脚本会静默退化成基类 subsystem 视图，子类 API 访问需要额外 cast，甚至直接编译失败；这类回归只看运行时对象 identity 很难提前发现 |
| 建议测试名 | `Angelscript.TestModule.Engine.BindConfig.SubsystemDeterminesOutputTypePreservesTypedReturn` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryBindConfigTests.cpp` |
| 场景描述 | 在测试模块里定义最小 native `UEngineSubsystem` / `UGameInstanceSubsystem` / `ULocalPlayerSubsystem` / `UWorldSubsystem` 子类，每个子类暴露一个独有 `Marker` getter。测试先构造 `FAngelscriptFunctionSignature` 并对 `GetEngineSubsystem`、`GetWorldSubsystem` 断言 `DeterminesOutputTypeArgument` / `determinesOutputTypeArgumentIndex`；随后在带 `GameInstance + LocalPlayer + World` 的 fixture 中编译脚本，把 `USubsystemLibrary` 返回值直接赋给具体测试子类变量，再无 cast 调用子类独有 getter。 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` 建立 world/local player/controller 上下文；native 侧确保 4 个测试 subsystem 已初始化并写入不同 marker 值；脚本分别传入 `UTestEngineSubsystem::StaticClass()`、`UTestWorldSubsystem::StaticClass()` 等 class 形参，确保既覆盖无 world-context 也覆盖隐藏 world-context 的形态。 |
| 期望行为 | metadata 断言层面，`GetEngineSubsystem` 与 `GetWorldSubsystem` 的 `Class` 参数都必须被标记为 determines-output-type 参数；执行层面，脚本应能把返回值直接当成 `UTest*Subsystem` 使用，并正确读取各自 marker，而不是只能接成基类后再 cast；如果 typed return 退化，模块编译应直接失败并暴露在此测试，而不是等运行时才发现。 |
| 使用的 Helper | `FAngelscriptEngineScope` + `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-41 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 01:04 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-10 01:00)` 段落；这里补一个真正位于文件末尾的定位，固定本轮新增 issue 与 new test 的尾部归档位置。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-42 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 01:13)

### 一、现有测试问题

#### Issue-43：`GameplayTagContainerCompat` 对 `IsValid()` 只测非空正路径，空容器契约仍然是盲区

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagContainerCompat` |
| 行号范围 | 112-163 |
| 问题描述 | 这条 dedicated container compat 用例在 `EmptyDefault` 上只检查了 `IsEmpty()` 和 `== FGameplayTagContainer::EmptyContainer`，随后对 `Tags` 只在 `AddTag(ValidTag)` 之后做了一次 `if (!Tags.IsValid()) return 30;`。也就是说，`GameplayTagContainerMixinLibrary::IsValid()` 当前只被 happy path 的“非空且含有效 tag”间接覆盖；如果脚本桥把 `IsValid()` 错绑成“容器非空即 true”甚至“永远 true”，现有脚本仍会稳定返回 `1`，因为它从未在空容器、`Reset()` 后容器或 `EmptyContainer` 上断言 `IsValid()==false`。 |
| 影响 | `GameplayTagContainerCompat` 目前会给人一种“container 有效性已经有 dedicated 覆盖”的错觉，但真正最容易回归的默认空容器语义仍未被固定；一旦 `IsValid()` 退化，脚本侧用它区分“有内容的 tag set”与“默认空值”的逻辑会静默失真，而现有自动化不会报警。 |
| 修复建议 | 不要再只在 `Tags.AddTag(ValidTag)` 后 smoke 一次 `IsValid()`。把 `EmptyDefault.IsValid()`、`FGameplayTagContainer::EmptyContainer.IsValid()`、`Combined.Reset()` 之后的 `Combined.IsValid()` 一并拉进同一条或独立用例，明确固定“空容器为 false、含有效 tag 的容器为 true”的契约；若 native reference 当前并非如此，也要让脚本结果与 native `FGameplayTagContainer::IsValid()` 逐项对照，而不是继续只测正路径。 |

### 二、需要新增的测试

#### NewTest-75：`FGameplayTagContainer.IsValid()` 的空容器与 `Reset()` 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` |
| 关联函数 | `IsValid` |
| 现有测试覆盖 | `GameplayTagContainerCompat` 只在 `Tags.AddTag(ValidTag)` 之后检查一次 `IsValid()`；空容器、`EmptyContainer` 常量和 `Reset()` 后容器的有效性都没有任何 dedicated 断言 |
| 风险评估 | 一旦 `IsValid()` 退化成“永远 true”或把 `Reset()` 后容器错误地视为仍有效，脚本侧根据 tag container 是否有效决定分支、缓存或 UI 展示的逻辑会静默跑偏，而现有测试仍会全绿 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayTagContainerIsValidContracts` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` 或新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagContainerFunctionLibraryTests.cpp` |
| 场景描述 | native 侧先选取一个稳定已注册 `ValidTag`，同时用 native `FGameplayTagContainer` 生成 4 份 mirror：`EmptyDefault`、`FGameplayTagContainer::EmptyContainer`、`ValidContainer(AddTag)`、`ResetAfterValid(AddTag 后 Reset)`。脚本按完全相同顺序构造 4 份 container，分别调用 `IsValid()`，再把布尔结果回传给 C++ 与 native mirror 对照。 |
| 输入/前置 | 使用 `UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false)` 选出一枚稳定 tag；采用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN`，避免 gameplay tag 模块残留影响结果；native 侧在测试开始前直接调用 `FGameplayTagContainer::IsValid()` 记录 reference，而不是手写假设。 |
| 期望行为 | 脚本 `EmptyDefault.IsValid()`、`FGameplayTagContainer::EmptyContainer.IsValid()`、`ValidContainer.IsValid()`、`ResetAfterValid.IsValid()` 与 native mirror 逐项一致；至少要固定 `Reset()` 后结果不会沿用 reset 前状态。如果当前 UE/native 语义是“空容器无效、含有效 tag 的容器有效”，则脚本侧必须完全一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_BEGIN_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-43 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 01:30 真正尾部定位)

本轮新的正文发现已记录在文中现有的 `## 测试审查 (2026-04-10 01:26)` 段落；这里补一个真正位于文件末尾的定位，固定本轮新增 issue 与 new test 的尾部归档位置。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-44 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |
