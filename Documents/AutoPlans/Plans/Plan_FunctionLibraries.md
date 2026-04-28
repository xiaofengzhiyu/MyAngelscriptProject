# FunctionLibraries 改进计划

## 背景与目标

### 背景

`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/` 当前共有 `21` 个 helper / mixin library 文件，承担了大量“脚本友好包装层”职责；但本轮交叉核对 `FunctionLibraries_Analysis`、`FunctionLibraries_TestGaps`、`ModuleStructure_ArchReview`、`CrossComparison`、`Hazelight_Analysis` 与当前源码后，可以确认该目录仍有几类高优先级问题：

- 一部分 wrapper 在从 `ScriptCallable` / `ScriptMixin` 迁到 `BlueprintCallable` 的过程中停在半完成状态，源码还保留着 helper，但脚本表面、返回值契约和真实 owner 已经漂移。
- 一部分高频 utility 仍存在明确的运行期正确性问题，例如 `NaN`、错误投影、参数被忽略、类型退化或空输入直接解引用。
- 现有测试大量停留在 compile smoke、class-map presence 或单标签正路径，无法对 helper 的真实行为、边界条件和错误路径形成保护。

同时，`Documents/Plans/Plan_TestCoverageExpansion.md` 已经承接了“代表性 FunctionLibrary 覆盖基线”的主线，`Documents/Plans/Plan_HazelightCapabilityGap.md` 已经承接了 engine / plugin / workflow 的 parity 分层；因此本计划只补 **FunctionLibraries 目录内、具备明确源码锚点的 plugin-side 运行期改进项**，避免重复主线计划。

### 范围与边界

- 只纳入 `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/` 内仍可在当前源码中复核的问题。
- 每个条目至少引用 `2` 个分析维度；优先使用被 `3+` 个维度交叉确认的问题。
- `Documents/AutoPlans/DiscoveryPlans/FunctionLibraries_Plan.md` 在 `2026-04-09` 核验时不存在，因此本轮不伪造维度 B 来源；仅在 A / C / D / E 已形成足够交叉证据时立项。
- 不重复拆写 `Plan_TestCoverageExpansion.md` 中已经存在的泛化 smoke / representative test 任务；本计划只写 bug-driven 或 contract-driven 的专项修复与回归测试。

### 目标

1. 收敛 `FunctionLibraries` 中最容易导致脚本 API 漂移或运行期错误的 `5` 个高价值问题域。
2. 为每个改进项补齐 dedicated 自动化测试，且测试都覆盖正常路径、边界条件、错误路径三类场景。
3. 把“仍应由 FunctionLibrary owner 维护的 helper”与“已经迁到 hand-written bind / 其他 owner 的能力”重新划清，减少双轨维护。
4. 让后续执行者可以直接按本计划落地，不必再次回看整批 AutoPlans 分析文档才能理解变更边界。

## 分析来源

| 分析文档 | 关键发现 |
| --- | --- |
| `Documents/AutoPlans/FunctionLibraries_Analysis.md` | 记录了 `GameplayTag` helper 双轨维护、classic input 契约退化、Math 数值边界错误、`FHitResult` helper 面缩水、`UAssetManager` 查询入口失联等直接源码问题。 |
| `Documents/AutoPlans/DiscoveryPlans/FunctionLibraries_Plan.md` | `2026-04-09` 核验时文件不存在；本轮不把维度 B 作为 primary evidence，避免伪造来源。 |
| `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` | 明确指出现有测试多停留在 compile smoke 或宽松断言，`GameplayTag`、Input、Math、HitResult、AssetManager 都缺 dedicated runtime contract coverage。 |
| `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` | `Arch-MS-40` 指出同一 UE 模块的脚本支持分散在 UHT function-table、hand-written bind 和 runtime helper 三条管线，owner 不清；对 `GameplayTags` 这类领域尤为关键。 |
| `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` | 对 input callback authority 的横向对比说明当前插件仍应保持 direct-bind / delegate-authority 路线，不应把 classic input helper 继续退化成“只剩最小静态包装”。 |
| `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` | `D2/D3` 直接指出 `ScriptMixin` 在当前插件里已从普遍规则退成选择性保留；`InputComponent` 与 `FHitResult` 是最明确的 wrapper 收缩样本。 |

## 分阶段执行计划

### Phase 1：正确性与运行期契约闭环

> 目标：先修正会直接导致 API 失真、返回值漂移、空输入崩溃或数值错误的条目，并为这些 helper 建立行为级回归。

- [ ] **P1.1** 收敛 `GameplayTag` / `GameplayTagQuery` / `GameplayTagContainer` 三组 helper 的 owner，并恢复缺失的实例契约
  - 当前 `GameplayTags` 领域同时存在“仍留在 `FunctionLibraries/` 头文件中的静态 wrapper”和“已经迁到 hand-written bind 的实例方法”两套 owner。执行时应先把基础值类型 direct bind 与真正需要 FunctionLibrary 承担的扩展 helper 分开，再决定保留哪些 wrapper、删除哪些重复入口、以及哪些 helper 需要恢复 member-style surface。
  - 这项工作至少要解决三类已确认问题：`GameplayTag` / `GameplayTagQuery` 类级 `ScriptMixin` 被注释后 helper 失联；`GameplayTagContainer` 继续保留大批 wrapper 但其中 `AddLeafTag` 被裁成 `void`；Query / hierarchy / empty-contract 目前完全缺少行为级保护。
  - 实施时优先保持稳定脚本名，能通过恢复 class-level mixin 或薄转发解决的，不要再引入第三套 owner；若某些 helper 已彻底由 hand-written bind 取代，则应同步清理失效 wrapper，避免继续双轨维护。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 8 / 10 / 85：`GameplayTag` / `GameplayTagQuery` helper 仍留在失效 wrapper 中，`GameplayTagContainer` wrapper 与 hand-written bind 双轨维护，`AddLeafTag` 被裁成 `void` 返回。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-2 / Issue-9 / Issue-21 / NewTest-17 / NewTest-40 / NewTest-41 / NewTest-42：现有 GameplayTag 测试只覆盖基础构造、单标签正路径和 `IsValid()`，没有钉住 hierarchy / query / empty-container / empty-query / exact 语义。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 UE 模块的脚本支持分散在 UHT function-table、hand-written bind 与 runtime helper 三条管线里，维护者无法一眼判断 owner。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h` L13-L79 — 类级 `ScriptMixin` 仍被注释成 `UCLASS(Meta = ())`，`MatchesTag*` / `MatchesAny*` / `RequestDirectParent` / `GetGameplayTagParents` 仍留在静态 wrapper 里。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h` L13-L37 — `Matches()` / `GetDescription()` 仍依赖已失效的 mixin wrapper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` L12-L155 — `AddLeafTag()` 仍声明为 `void`，同时 `Filter*` / `MatchesQuery` / `First` / `Last` 等 helper 继续滞留在 wrapper 中。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
- [ ] **P1.1** 📦 Git 提交：`[FunctionLibraries] Refactor: unify GameplayTag helper ownership and contracts`
- [ ] **P1.1-T** 单元测试：锁定 `GameplayTag` hierarchy / query / container helper 的真实契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
  - 测试场景：
    - 正常路径：父标签 / 子标签 / 多标签容器 / 非空 query 都能在脚本侧得到与 native 一致的 `MatchesTag*`、`MatchesAny*`、`MatchesQuery()` 和 `GetDescription()` 结果；`AddLeafTag()` 首次插入返回 `true`，重复插入返回 `false`。
    - 边界条件：空 `FGameplayTag`、空 `FGameplayTagContainer`、空 `FGameplayTagQuery` 分别覆盖 `HasAll` / `HasAny` / `Matches()` / `GetSingleTagContainer()` / `RequestDirectParent()` / `GetGameplayTagParents()` 的默认契约。
    - 错误路径：请求未注册 tag、向空容器 / 空 query 做匹配、或对默认构造 tag 调用 hierarchy helper 时，不得返回脏数据或误匹配结果。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.GameplayTagHierarchySemantics`、`Angelscript.TestModule.FunctionLibraries.GameplayTagQueryContracts`、`Angelscript.TestModule.FunctionLibraries.GameplayTagContainerContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.1-T** 📦 Git 提交：`[FunctionLibraries] Test: lock GameplayTag helper semantics`

- [ ] **P1.2** 修正 classic input helper 的类型契约、返回值与空输入守卫
  - `UInputComponentScriptMixinLibrary` 当前同时存在多类契约退化：`BindAction` / `BindChord` / `BindAxis*` 全部吞掉 binding 返回值，`BindAxisKey` 把引擎原生 `FKey` 退化成 `FName`，`GetEngineDefined*Mappings()` 明明声明了筛选参数却完全忽略，`PushInputComponent` / `GetPlayerInput` 等路径也没有任何空输入防御。
  - 本项应先固定 classic input 在插件内的 owner：保持 direct-bind / delegate-authority 路线，不和 `Bind_UEnhancedInputComponent.cpp` 混层；然后把 helper surface 恢复到“脚本仍能拿到 binding / handle、能配置 `bConsumeInput` / `bExecuteWhenPaused`、能用正确的 `FKey` 类型、能在 null 输入下安全失败”的最低可用契约。
  - 若决定不恢复 class-level `ScriptMixin` 语法，也必须显式统一为静态 helper 风格并补齐测试；不能继续保留注释里的 `ScriptMixin` / `ScriptCallable` 痕迹，让维护者误判这些 wrapper 仍会自动折叠成成员方法。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 2 / 5 / 87 / 88 / 94 / 95 / 96 / 97 / 98：筛选参数 ignored、null receiver 会直接崩溃、`BindAxisKey` 从 `FKey` 退化成 `FName`、bind helper 吞掉 binding 对象、Touch / Gesture / keystate / value-only overload 全部缺席。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-5 / NewTest-37：缺少 mapping round-trip、null input 守卫与 classic input helper 行为回归。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “D2 / D3：UEAS2 仍把 `InputComponent` / `PlayerController` / `PlayerInput` wrapper 作为 active `ScriptMixin`；当前快照已把 class-level `ScriptMixin` 注释掉，helper surface 明显收缩。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` L12-L105 — `UInputComponent` wrapper 仍是 `UCLASS(Meta = ())`；`BindAction()` / `BindKey()` / `BindChord()` / `BindAxis()` / `BindAxisKey()` / `BindVectorAxis()` 全部返回 `void`，且 `BindAxisKey()` 仍接收 `const FName& AxisKey`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` L129-L147 — `PushInputComponent()` / `PopInputComponent()` / `GetPlayerInput()` 直接解引用 `PlayerController`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` L211-L220 — `GetEngineDefinedActionMappings()` / `GetEngineDefinedAxisMappings()` 仍直接返回全量引擎映射，完全忽略形参里的 `ActionName` / `AxisName`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp`
- [ ] **P1.2** 📦 Git 提交：`[FunctionLibraries] Fix: restore classic input helper contracts`
- [ ] **P1.2-T** 单元测试：锁定 classic input helper 的 binding、过滤与空输入行为
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本创建 `BindAction` / `BindKey` / `BindAxis*` 后能拿到可继续配置的 binding/handle，`GetEngineDefinedActionMappings()` / `GetEngineDefinedAxisMappings()` 只返回匹配筛选参数的条目。
    - 边界条件：空 mapping 集、没有 `LocalPlayer` 的 controller、重复 add/remove input component、重复绑定同一 axis/key 时保持可诊断且稳定的结果。
    - 错误路径：`null UInputComponent`、`null APlayerController`、`null UPlayerInput`、无效 `FKey` 或缺失 delegate receiver 时必须安全失败，且不污染 binding array 或 controller 状态。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.InputBindingContracts`、`Angelscript.TestModule.FunctionLibraries.InputBindNullGuards`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add classic input helper regression coverage`

- [ ] **P1.3** 修正 `Math` helper 的边界值错误与 `FVector` / `FVector3f` 对称性
  - 当前 `Math` helper 同时存在导出与正确性两类问题：`WrapIndexUInt()` 仍没有 callable flag；`AngularDistance()` / `AngularDistanceForNormals()` 对零向量和浮点漂移没有保护；`ConstrainToPlane()` / `ConstrainToDirection()` 默认把非单位向量静默投进错误结果；`FVector3f` 的 `Dist2D()` / `DistSquared2D()` 仍退化成世界 XY 距离；`MoveTowards()` 只有 `FVector` 版本。
  - 本项的目标不是继续追加更多“非零 / 大于零”式 smoke 断言，而是把这些 helper 的数学契约明确下来：对 ill-defined 输入要么规约成安全结果，要么以显式失败方式被测试固定；对 `FVector` / `FVector3f` 要尽量保持对称 API 面。
  - 实施时优先参考当前引擎 native 行为和已有 `FunctionLibraries_TestGaps` 里的 expected semantics；若某些 helper 必须保留“需要单位法线 / 单位方向”的前提，应同步写入注释与测试，而不是继续 silent wrong result。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 4 / 7 / 86 / 91 / 93：`WrapIndexUInt` 不会暴露、角距离 helper 会产出 `NaN`、`FVector3f` 平面距离退化成 XY、plane/direction helper 对非单位向量给错结果、`MoveTowards` 缺失 `FVector3f` 对称 overload。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-2 / NewTest-33，以及 `MathExtendedCompat` 宽断言问题：现有测试没有覆盖 wrap、平面投影、二维距离、边界向量和 deterministic expected-value 对比。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L319-L332 — `WrapIndexUInt()` 仍只有 `UFUNCTION(Meta = (ScriptName = "WrapIndex"))`，没有任何 callable flag。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L383-L405、L474-L496 — `AngularDistance*()` 仍直接 `Acos(...)`，`ConstrainToPlane()` / `ConstrainToDirection()` 仍直接使用未归一化法线 / 方向。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L422-L427、L459-L468 — `MoveTowards()` 只有 `FVector` 版本，而 `FVector3f::Dist2D()` / `DistSquared2D()` 仍使用 `DistSquaredXY(VectorPlaneProject(...))`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
- [ ] **P1.3** 📦 Git 提交：`[FunctionLibraries] Fix: harden math helper edge cases and parity`
- [ ] **P1.3-T** 单元测试：用 deterministic expected-value 锁定 `Math` helper 的边界行为
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：`WrapIndex()` / `WrapIndexUInt()`、`FVector` / `FVector3f` 平面距离、`MoveTowards()` 与 native reference / 手工 expected value 一致。
    - 边界条件：`Min == Max`、`Min > Max`、零向量、近似单位向量、非单位法线 / 方向输入都要得到有限且可预测的结果。
    - 错误路径：对数学上未定义的输入不能继续返回 `NaN` 或 silent wrong result；若本项最终选择抛脚本异常、归一化后计算或返回默认值，测试需把该契约固定下来。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.MathBoundaries`、`Angelscript.TestModule.FunctionLibraries.MathPlanarProjectionAndParity`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.3-T** 📦 Git 提交：`[FunctionLibraries] Test: add deterministic math helper coverage`

### Phase 2：helper 面恢复与导出一致性

> 目标：处理仍停留在“wrapper 还在，但 API 面已经缩水或导出链路半完成”的条目，避免源码继续保留误导性的半失效 helper。

- [ ] **P2.1** 恢复 `FHitResult` helper surface，并明确 wrapper/member 语法的 owner
  - `FHitResult` 当前的底层 property/direct-bind surface 仍存在，但高层 helper wrapper 已明显缩水：类级 `ScriptMixin` 被注释，`Reset()` 只剩无参版本，`SetPhysMaterial()` 消失，只留下 getter 与少量 flag setter。继续保留这种“底层可用、helper 半失效”的状态，会让维护者和脚本用户都难以判断真正受支持的 API 面。
  - 本项应先明确 owner：如果 `FHitResult` 仍要保留 helper-style member sugar，就恢复 class-level mixin，并补回对等 overload / setter；如果决定完全依赖 property/direct bind，则应移除误导性的 wrapper，并同步调整文档和测试，不能继续让半残 helper 留在 `FunctionLibraries/` 里。
  - 执行时优先保证脚本调用点稳定；对 `Reset()` 这类签名变化，尽量通过 overload / deprecation 过渡，不直接制造不必要的 breaking change。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 14：`UAngelscriptHitResultLibrary` 同时丢失 `ScriptMixin`、`SetPhysMaterial` 与可控 `Reset` overload，helper 面明显缩水。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-5 / NewTest-4：现有 `HitResultCompile` 只测字段读写，完全没有 helper round-trip 覆盖。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “D3 wrapper-based mixin policy：UEAS2 仍把 `FHitResult` helper 作为 active `ScriptMixin`，当前快照则留下 wrapper 但关闭了 mixin。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` L8-L9 — `ScriptMixin = "FHitResult"` 仍被注释。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` L18-L34 — `SetComponent()` / `SetActor()` / `Reset()` 仍存在，但 `Reset()` 只有无参版本。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` L53-L88 — 只剩 `GetPhysMaterial()` getter 与 flag setter，没有 `SetPhysMaterial()` 和带参数的 reset overload。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptHitResultFunctionLibraryTests.cpp`
- [ ] **P2.1** 📦 Git 提交：`[FunctionLibraries] Fix: restore HitResult helper parity`
- [ ] **P2.1-T** 单元测试：验证 `FHitResult` helper 的 round-trip、reset 与失败路径
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptHitResultFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本对 `FHitResult` 依次执行 `SetActor()`、`SetComponent()`、`SetPhysMaterial()`、`SetbBlockingHit()`、`SetbStartPenetrating()`，再用 getter 做 round-trip 对照。
    - 边界条件：默认构造 `FHitResult`、部分字段已填充的 `FHitResult`、以及带 / 不带 trace data 的 `Reset()` 都应与 native reference 一致。
    - 错误路径：传入 `null Actor`、`null Component`、`null PhysMaterial` 或对空命中结果重复 reset 时必须保持安全且可预测。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.HitResultAccessors`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add HitResult helper round-trip coverage`

- [ ] **P2.2** 补齐 `UAssetManagerMixinLibrary` 的失联查询入口，并锁定 initial-scan callback 失败契约
  - `UAssetManagerMixinLibrary` 前半段 query API 已经迁成 `BlueprintCallable`，但 `GetPrimaryAssetTypeInfo()`、`GetPrimaryAssetTypeInfoList()`、`GetPrimaryAssetRules()` 仍停在裸 `UFUNCTION()`；这说明导出迁移只做了一半，脚本表面会出现“同文件内部分 query 可见、部分不可见”的不一致状态。
  - 同时，`CallOrRegister_OnCompletedInitialScan()` 仍直接 `CreateUFunction(Object, FunctionName)`，但当前没有任何 dedicated 测试去固定 `null receiver`、缺失 `UFUNCTION()`、错误函数名等失败语义。执行时应一并定义这些负路径契约，避免 callback helper 继续靠未定义行为存活。
  - 这项工作应保持 `AssetManager` query helper 作为 FunctionLibrary owner，不要把本来就是 wrapper 层的资产查询 API 随意迁去不相关 bind 文件；但若恢复 class-level mixin，则也要同步补齐对应 member-surface 测试。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 1：`GetPrimaryAssetTypeInfo`、`GetPrimaryAssetTypeInfoList`、`GetPrimaryAssetRules` 注释仍显示 `ScriptCallable`，实际却只有裸 `UFUNCTION()`，迁移停在半完成状态。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-11 / NewTest-39：`UAssetManager` query 与 initial-scan callback 缺少 dedicated 正/负路径测试。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` L6-L7 — `ScriptMixin = "UAssetManager"` 仍被注释。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` L55-L74 — `GetPrimaryAssetTypeInfo()`、`GetPrimaryAssetTypeInfoList()`、`GetPrimaryAssetRules()` 仍是 bare `UFUNCTION()`，与同文件前半段 `BlueprintCallable` 风格不一致。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` L79-L82 — `CallOrRegister_OnCompletedInitialScan()` 仍直接 `CreateUFunction(Object, FunctionName)`，没有任何本地 guard 或明示失败契约。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`
- [ ] **P2.2** 📦 Git 提交：`[FunctionLibraries] Fix: complete asset manager query exports`
- [ ] **P2.2-T** 单元测试：覆盖 `UAssetManager` 查询 helper 与 initial-scan callback 的正负路径
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本通过 `GetPrimaryAssetTypeInfo*()` / `GetPrimaryAssetRules()` 读到与 native `UAssetManager` 一致的数据；有效 callback receiver 在 initial scan 完成后只触发一次。
    - 边界条件：未扫描类型、空 asset type、无匹配 asset、已经完成 scan 的重复注册路径，都应返回稳定的 false / empty 结果而不是脏数据。
    - 错误路径：`null UAssetManager`、`null receiver`、缺失 `UFUNCTION()` 的 receiver、错误 `FunctionName` 必须安全失败，且不得误触发 callback。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.AssetManagerQueryAndScan`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add asset manager query and callback coverage`

## 单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` | hierarchy / query / empty tag / empty container / empty query / `AddLeafTag` 返回语义 | `P0` |
| `P1.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp` | binding return contract、mapping filtering、`null` receiver、安全失败 | `P0` |
| `P1.3` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` | deterministic expected-value、零向量、非单位法线 / 方向、`FVector3f` parity | `P1` |
| `P2.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptHitResultFunctionLibraryTests.cpp` | setter/getter round-trip、reset overload、`null` handle 输入 | `P1` |
| `P2.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp` | asset query、scan callback、无效 receiver / 函数名 | `P1` |

## 验收标准

1. `FunctionLibraries` 本轮新增条目全部具备源码锚点、来源追溯与 dedicated `-T` 测试任务，没有只停留在标题级描述的执行项。
2. `GameplayTag`、classic input、Math、HitResult、AssetManager 五个问题域都形成“代码修复 + 正常/边界/错误路径测试”的闭环。
3. 至少 `P1.1`、`P1.2`、`P1.3` 的测试不再依赖 compile-only 或宽松 truthy/non-zero 断言，而是与 native reference 或明确 expected value 做对比。
4. 计划执行完成后，`FunctionLibraries/` 中不再保留明显误导性的半失效 helper：例如被注释掉 `ScriptMixin` 但仍被当成实例语法依赖的 wrapper、参数被声明却完全忽略的 query helper、或返回值被静默裁剪的包装函数。
5. 本计划与 `Documents/Plans/Plan_TestCoverageExpansion.md`、`Documents/Plans/Plan_HazelightCapabilityGap.md` 的边界保持清晰：前者继续承接泛化测试扩张，后者继续承接大范围 parity 分层，而本计划只处理 plugin-side FunctionLibraries 具体改进。

## 风险与注意事项

### 风险

1. **`GameplayTag` owner 收敛可能引入脚本入口迁移风险**
   - 若把某些 helper 从 wrapper 切回 hand-written bind，或恢复 class-level mixin，脚本表面的宿主类型 / 声明形式可能发生变化。
   - 缓解：优先保留现有 `ScriptName` 与实例调用习惯，必要时提供 deprecation shim，而不是直接删除旧入口。

2. **classic input 契约修正最容易触发脚本调用点兼容性问题**
   - `FName -> FKey`、`void -> binding/handle` 之类修正会直接影响脚本签名。
   - 缓解：优先通过新增 overload、deprecated wrapper 或 thin adapter 过渡，避免一次性硬切。

3. **Math helper 的“修正”会改变此前未定义输入下的结果**
   - 对零向量、非单位法线 / 方向、越界 dot product 的处理，一旦从 `NaN` / silent wrong result 改成 deterministic contract，就会改变旧行为。
   - 缓解：所有这类改动都必须先用测试固定目标语义，并在变更说明里明确是“纠正未定义行为”。

4. **AssetManager callback 失败语义可能受引擎版本细节影响**
   - `CreateUFunction(Object, FunctionName)` 的失败表现可能受 UE 版本和对象状态影响。
   - 缓解：测试以当前仓库目标引擎版本的实际行为为准，只固定项目级 contract，不擅自推断跨版本保证。

### 已知行为变化

1. **`GameplayTagContainer::AddLeafTag` 若恢复 `bool` 返回值，会从“无返回值 wrapper”改回“可观察插入结果”的脚本契约**
   - 忽略返回值的调用点通常仍可兼容，但任何依赖旧静态声明的脚本签名都需要重新编译验证。

2. **classic input helper 可能从“只负责注册”升级为“返回 binding / handle 供进一步配置”**
   - 这会让脚本 API 更接近 UE 原生，但也意味着现有 helper 的声明文本会变化。

3. **`FHitResult.Reset()` 可能重新暴露带参数 overload**
   - 若脚本此前默认依赖当前无参 wrapper 的固定行为，需要在测试里同时覆盖旧默认路径与新 overload 路径。

---

## 第 2 轮补充（2026-04-09）

本轮只追加前两轮尚未覆盖、且与 `Plan_TestCoverageExpansion.md` 中“代表性 smoke / representative coverage”不重复的导出面缩水项。优先顺序仍按“当前源码仍保留 helper 文件，但脚本表面已经失联、重复、或契约明显退化”的标准筛选。

### Phase 3：mixin / wrapper 导出面收口

- [ ] **P3.1** 恢复 `AActor` helper 的真实导出面，并去掉只会制造双源维护的重复 getter
  - 当前 `UAngelscriptActorLibrary` 仍声明了一整组相对变换、sweep、attach 与 editor helper，但绝大多数入口停留在裸 `UFUNCTION()`；真正会被自动导出的只剩 `GetActorLocation()` / `GetActorRotation()`，而这两条又早已由 `Bind_AActor.cpp` 手写绑定。执行时应先把 actor helper 的 owner 收口成单轨：要么把独有 helper 正式迁成 `BlueprintCallable` / 显式 bind，要么删除已无价值的重复 wrapper，不能继续维持“真正缺的没导出，重复 getter 却导出了”这一状态。
  - 这项改动应优先覆盖 `SetActorRelative*`、`SetActorLocationAdvanced()`、`SetActorLocationAndRotation*()`、`AttachToComponent()`、`AttachToActor()` 与 editor-side `RerunConstructionScripts()` 这类当前文件里仍有实现、但脚本侧无法稳定依赖的 helper；同时把 `GetActorLocation()` / `GetActorRotation()` 的 authority 固定到单一 owner，避免后续再出现 metadata 或空指针策略漂移。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 6 / 66：`UAngelscriptActorLibrary` 大部分 transform / attachment helper 仍是裸 `UFUNCTION()`，真正自动导出的只剩 `GetActorLocation` / `GetActorRotation`，而这两条又与 hand-written bind 重复。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-1 / NewTest-1：`NativeActorMethods` 没有验证 actor transform 返回语义，`SetActorLocationAdvanced` / attach helper 完全无 dedicated scenario coverage。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 UE 模块的脚本支持同时分散在 UHT function-table、`Binds/*.cpp` 手写注册与 runtime helper 中，owner 不唯一。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L12-L72 — `SetActorRelativeLocation()`、`GetActorRelativeLocation()`、`SetActorRelativeRotation()`、`SetActorLocation()` 仍是裸 `UFUNCTION()`，没有 callable flag。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L74-L96 — 当前真正带 `BlueprintCallable` 的只剩 `GetActorLocation()` 与 `GetActorRotation()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L199-L208 — `AttachToComponent()` / `AttachToActor()` 仍是裸 `UFUNCTION()`，并把 attach 规则压成单一 `AttachmentRule`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L29-L33 — `GetActorLocation()` / `GetActorRotation()` 已经由 hand-written bind 注册，形成当前自动导出面唯一生效部分与手写 bind 的双源重叠。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp`
- [ ] **P3.1** 📦 Git 提交：`[FunctionLibraries] Fix: restore actor helper exports and dedupe getters`
- [ ] **P3.1-T** 单元测试：锁定 `AActor` transform / sweep / attach helper 的运行期契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本对非零 transform 的 actor 执行相对 / 世界变换 round-trip，调用 `SetActorLocationAdvanced()` 验证 `bSweep` 与 `SweepHitResult`，再验证 `AttachToActor()` / `AttachToComponent()` 后的 parent 与相对变换。
    - 边界条件：无 root component actor、重复 attach 到同一 parent、默认 `SocketName` / 默认 `AttachmentRule`、`bTeleport` true/false 两种路径都要固定结果。
    - 错误路径：`null AActor`、`null ParentActor`、`null ParentComponent` 与无效 attach/sweep 输入必须给出稳定脚本异常或安全失败结果，不能继续靠崩溃暴露问题。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip`、`Angelscript.TestModule.FunctionLibraries.ActorAttachAndSweepContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add actor helper regression coverage`

- [ ] **P3.2** 重新接通 `UWorld.GetStreamingLevels()` 的 mixin owner，并补齐 world / level 的空输入契约
  - `UAngelscriptWorldLibrary` 当前只剩一个 `GetStreamingLevels()` helper，但类级 `ScriptMixin = "UWorld"` 已被注释，`Bind_FunctionLibraryMixins.cpp` 也没有任何 `UWorld` 兜底绑定。这意味着文件仍然存在、函数体仍然存在，但脚本侧已经拿不到原本应属于 `UWorld` 的实例扩展方法。
  - 这项工作要做的不只是“把函数重新导出来”，还要把 null-world / null-level 的失败语义固定下来。当前 `GetStreamingLevels()` 直接解引用 `World`，而 `ULevelStreaming::GetShouldBeVisibleInEditor()` 又已经通过 hand-written bind 存在；执行时应把两者统一成同一组 world-streaming helper contract，而不是继续一边是有效 mixin、一边是失联 wrapper。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 9：`UAngelscriptWorldLibrary` 失去 `ScriptMixin` 后没有任何手写绑定兜底，`GetStreamingLevels` 不再扩展到 `UWorld`。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-3 / NewTest-38：`GetStreamingLevels` / `GetShouldBeVisibleInEditor` 缺少运行时一致性和 null 输入守卫测试。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 UE 模块的脚本支持 authority 分散，runtime helper 与 hand-written bind 并存时 owner 不清。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h` L6-L18 — `ScriptMixin = "UWorld"` 已被注释，唯一 helper `GetStreamingLevels()` 仍直接解引用 `World`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp` L7-L23 — 当前只显式补了 `ULevelStreaming::GetShouldBeVisibleInEditor()` 与 `FRuntimeCurveLinearColor::AddDefaultKey()`，没有 `UWorld` 相关兜底绑定。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldFunctionLibraryTests.cpp`
- [ ] **P3.2** 📦 Git 提交：`[FunctionLibraries] Fix: restore world streaming helper ownership`
- [ ] **P3.2-T** 单元测试：覆盖 `UWorld` / `ULevelStreaming` helper 的正路径与 null guard
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本通过 `World.GetStreamingLevels()` 读取已知顺序的 streaming levels，并对 `Level.GetShouldBeVisibleInEditor()` 做 native 对照。
    - 边界条件：空 streaming-level 列表、重复读取、editor-visible true/false 两种状态都要返回稳定结果。
    - 错误路径：`null UWorld`、`null ULevelStreaming` 与已销毁对象句柄场景必须表现为可诊断的安全失败，而不是 access violation。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.WorldStreamingAccess`、`Angelscript.TestModule.FunctionLibraries.WorldStreamingNullGuards`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add world streaming helper coverage`

- [ ] **P3.3** 收口 `WidgetBlueprint` / `UWidget` helper 的 owner，并修正 `GetRenderTransform()` 的 metadata / const 契约
  - 当前 UI helper 同时存在两类漂移：一是 `CreateWidget()` 还留在 `UWidgetBlueprintStatics` 里，而真正可用的 `WidgetBlueprint::CreateWidget` 又已经在 `Bind_UUserWidget.cpp` 手写绑定；二是 `GetRenderTransform()` 失去 `ScriptMixin = "UWidget"` 后退化成静态类函数，同时挂着一个找不到实参的 `WorldContext` 元数据，参数类型还从 reference/UE 原生的 `const UWidget*` 收窄成了可变 `UWidget*`。
  - 本项应先决定 UI helper 的唯一 owner：继续保留 `WidgetBlueprint::CreateWidget` 作为 namespace 入口，还是反向收回 FunctionLibrary，都可以；但 `GetRenderTransform()` 必须重新成为稳定的 `UWidget` helper，并清掉错误的 world-context metadata 与不必要的可变参数约束。否则文档、静态检查和脚本调用面会继续同时漂移。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 13 / 83 / 89：`GetRenderTransform` 的 mixin 绑定被切断，参数从 `const UWidget*` 退化成 `UWidget*`，且仍残留找不到实参的 `WorldContext` 元数据。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-6 / NewTest-44 / Issue-23：`CreateWidget` 只有 compile test，`GetRenderTransform` 缺少 runtime、null-widget 和真实 metadata 断言。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：单个功能域同时受 runtime helper 与 hand-written bind 两条 authority 影响，owner 不清。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` L19-L24 — `CreateWidget()` 仍保留在 `UWidgetBlueprintStatics` 里作为 class static wrapper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` L28-L40 — `UAngelscriptWidgetMixinLibrary` 的 `ScriptMixin = "UWidget"` 已被注释，`GetRenderTransform()` 仍带 `WorldContext = "WorldContextObject"` 元数据，但签名只有一个 `UWidget* Widget` 参数且直接解引用。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp` L286-L293 — `WidgetBlueprint::CreateWidget` 已经存在 hand-written namespace bind，进一步放大当前 class-helper / namespace-helper 双轨 owner。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
- [ ] **P3.3** 📦 Git 提交：`[FunctionLibraries] Fix: normalize widget helper ownership and metadata`
- [ ] **P3.3-T** 单元测试：锁定 `CreateWidget` / `GetRenderTransform` 的 runtime 与 metadata 契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
  - 测试场景：
    - 正常路径：脚本通过 `WidgetBlueprint::CreateWidget` 或最终收口后的唯一 owner 创建 widget，并从 `GetRenderTransform()` 读回 native 预设的 `FWidgetTransform`。
    - 边界条件：默认 transform、无 owning player、以及 metadata 检查中“只应隐藏真实 `WorldContextObject` 参数的函数”都要被固定。
    - 错误路径：`null WidgetClass`、`null UWidget`、错误 world-context metadata 不得再静默通过；测试需固定“安全失败 / 脚本异常 / 无 hidden world context trait”中的最终契约。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.WidgetCreateAndTransform`、`Angelscript.TestModule.FunctionLibraries.WidgetRenderTransformNullGuard`、`Angelscript.TestModule.Engine.BindConfig.WidgetFunctionLibraryMetadata`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.3-T** 📦 Git 提交：`[FunctionLibraries] Test: add widget helper runtime and metadata coverage`

- [ ] **P3.4** 恢复 `ComponentLibrary` 的脚本友好 query surface，并统一 `USceneComponent` helper 的 null / duplicate 契约
  - 当前组件库问题不是单点，而是一组互相关联的回退：类级 mixin 只剩 `USceneComponent`，`UPrimitiveComponent::GetOverlappingActorsOfClass()` 整体掉线；UEAS2 里返回数组的 `GetChildrenComponentsByClass()` helper 也消失，只剩 out-param 旧绑定；同时 `GetNumChildrenComponents()`、`SetRelativeLocation()` 这类 trivial helper 又与 hand-written bind 重复，`AttachToComponent()` 还把 `ScaleRule`、`bWeldSimulatedBodies` 和返回值一并压扁，空指针容错策略也与 `Bind_USceneComponent.cpp` 现有做法不一致。
  - 本项应先收口 owner：对已有 hand-written bind 负责的 trivial API，要么删掉 FunctionLibrary 重复入口，要么让 FunctionLibrary 成为唯一 authority；对真正缺失的脚本友好 helper，则恢复 `UPrimitiveComponent` overlap query、数组返回的 child query 和与 UE 原生一致的 attach / null-guard 契约。不能继续把“缺失、缩水、重复、无防御”四类问题混在同一库里。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 11 / 12 / 48 / 62 / 63 / 64：`ComponentLibrary` 只剩 `USceneComponent` mixin，丢失 `GetOverlappingActorsOfClass` 与数组返回 child query，`AttachToComponent` 压扁规则与返回值，quat overload 丢掉 `NotAngelscriptProperty`，多处 helper 缺少 null guard 且与 hand-written bind 重复。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-12 / NewTest-34 / Issue-22：component transform / bounds / attach helper 缺少专项回归，bind-config 也没有抽查真实 FunctionLibrary class-map 入口。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：当 runtime helper 与 hand-written bind 并存时，单个 UE 模块的 support owner 不唯一。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L7-L8 — 当前类级 mixin 只剩 `USceneComponent`，没有 `UPrimitiveComponent` owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L15-L39 — `GetRelativeLocation()` / `GetRelativeRotation()` / `SetRelativeLocation()` 这类高频 helper 仍直接解引用 `Component`，没有任何 null guard。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L49-L52、L98-L101、L225-L235 — quat overload 已丢失 `NotAngelscriptProperty`、`GetNumChildrenComponents()` 成为重复 trivial wrapper、`AttachToComponent()` 仍固定 `KeepWorld` + `false` 并丢弃返回值。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp` L25-L26、L83-L116、L131-L135 — hand-written bind 里已存在 `GetNumChildrenComponents()`、null guard 和 `SetRelativeLocation()` 的另一套实现，说明当前确实处于双源且语义不一致的状态。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp`
- [ ] **P3.4** 📦 Git 提交：`[FunctionLibraries] Fix: recover component helper parity and null contracts`
- [ ] **P3.4-T** 单元测试：覆盖 `ComponentLibrary` 查询、attach 与 null-guard 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本调用数组返回的 `GetChildrenComponentsByClass()`、`GetOverlappingActorsOfClass()`、显式 attach helper 和相对 / 世界位移 helper，并与 native 对照 child 集合、overlap 集合、attach 结果和 transform 更新。
    - 边界条件：空组件层级、无匹配 class、无 overlap actor、重复 attach、默认 attachment rule 与 quat / rotator overload 并存路径都要得到稳定结果。
    - 错误路径：`null USceneComponent`、`null UPrimitiveComponent`、`null ComponentClass`、无效 parent 输入必须遵循统一的脚本异常或安全返回契约，不能再出现同域 API 一部分可诊断、一部分直接崩溃。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.ComponentHierarchyAndOverlap`、`Angelscript.TestModule.FunctionLibraries.ComponentAttachContracts`、`Angelscript.TestModule.FunctionLibraries.SceneComponentNullGuards`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.4-T** 📦 Git 提交：`[FunctionLibraries] Test: add component helper contract coverage`

- [ ] **P3.5** 恢复 `RuntimeFloatCurve` 的目标类型 helper 面，并修正 `SmartAuto` / invalid-handle 语义
  - `URuntimeFloatCurveMixinLibrary` 当前同时存在三类回退：类级 `ScriptMixin = "FRuntimeFloatCurve UCurveFloat"` 已被注释，导致整组曲线 helper 从目标类型实例方法退回 helper class；`AddSmartAutoCurveKey()` 与 `AddAutoCurveKey()` 现在完全同义，`SmartAuto` 已经退化成 `Auto`；`SetKeyUserTangents()` / `SetKeyUserTangentWeights()` 对无效 `FCurveKeyHandle` 只做静默 `return`，脚本拿不到任何失败信号。
  - 本项应先恢复“曲线 helper 应该挂在谁身上”的 owner，再锁定关键曲线语义：`FRuntimeFloatCurve` / `UCurveFloat` 的实例 helper 必须可用，`AddSmartAutoCurveKey()` 要恢复 `RCTM_SmartAuto`，无效 key-handle 路径则必须变成 deterministic contract，而不是继续静默吞掉修改。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 3 / 16 / 49 / 52：`RuntimeFloatCurveMixinLibrary` 丢失 `ScriptMixin`、`AddSmartAutoCurveKey` 退化成普通 `Auto`、invalid key-handle 修改 helper 静默 no-op。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-7 / NewTest-8 / NewTest-21 / NewTest-35：当前只有 `GetNumKeys` / `GetTimeRange` 的 direct-bind smoke，没有 key helper、SmartAuto 或 invalid-handle dedicated coverage。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：helper class 注册成功并不等于目标类型 helper surface 仍然存在，owner 需要显式化。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “`URuntimeFloatCurveMixinLibrary` 对照源码片段仍保留 `ScriptMixin = "FRuntimeFloatCurve UCurveFloat"`，说明参考实现的目标类型 owner 与当前快照不同。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` L16-L18 — `ScriptMixin = "FRuntimeFloatCurve UCurveFloat"` 已被注释，helper class 当前只剩 `UCLASS(meta = ())`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` L151-L168 — `SetKeyUserTangents()` / `SetKeyUserTangentWeights()` 对无效 key-handle 仍是静默 `return`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` L190-L205 — `AddAutoCurveKey()` 与 `AddSmartAutoCurveKey()` 都仍写成 `RCTM_Auto`，没有 `SmartAuto` 分支。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp`
- [ ] **P3.5** 📦 Git 提交：`[FunctionLibraries] Fix: restore runtime float curve mixin semantics`
- [ ] **P3.5-T** 单元测试：固定 `RuntimeFloatCurve` 的实例 helper、`SmartAuto` 和 invalid-handle 行为
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本在 `FRuntimeFloatCurve` / `UCurveFloat` 上执行 `GetNumKeys()`、`GetTimeRange()`、`GetFloatValue()`、`AddAutoCurveKey()`、`AddSmartAutoCurveKey()`，并与 native rich-curve 结果对照。
    - 边界条件：空 curve、单 key curve、`float` / `double` out-ref overload、一致内容的 `Equals()` 与不同 tangent mode 的 key 形状都要固定下来。
    - 错误路径：无效 `FCurveKeyHandle` 输入到 tangent / weight 修改 helper 时，必须返回可诊断的失败契约或稳定脚本异常，不能继续静默吞掉变更。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveRoundTrip`、`Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveSmartAutoKey`、`Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveInvalidKeyHandle`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.5-T** 📦 Git 提交：`[FunctionLibraries] Test: add runtime float curve semantic coverage`

### 单元测试总览增补（Phase 3）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P3.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` | actor transform round-trip、sweep、attach、null parent / actor | `P1` |
| `P3.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldFunctionLibraryTests.cpp` | streaming level 读取、editor-visible 对照、null world / level | `P1` |
| `P3.3` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp` | widget 创建、render transform 读取、null widget / null class、metadata trait 对照 | `P1` |
| `P3.4` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp` | hierarchy / overlap query、attach 规则、null guard、duplicate bind contract | `P1` |
| `P3.5` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp` | curve round-trip、`SmartAuto` tangent、invalid key-handle | `P1` |

### 验收标准补充（Phase 3）

1. `Actor`、`World`、`Widget`、`Component`、`RuntimeFloatCurve` 五个领域不再保留“源码里有 helper，但脚本表面失联或只剩重复入口”的状态。
2. 所有新增 `Phase 3` 测试都同时覆盖正常路径、边界条件和错误路径，不再以 compile smoke 或 `ClassFuncMaps` presence 代替行为断言。
3. `ScriptMixin` / `BlueprintCallable` / hand-written bind 的 owner 关系在上述五个领域都能被唯一追踪，后续维护者无需再猜测“该改 helper class、bind 文件还是 metadata”。

### 风险与注意事项补充（Phase 3）

1. **去重会改变绑定覆盖顺序**
   - `AActor`、`USceneComponent` 与 `WidgetBlueprint` 当前都存在自动导出 / hand-written bind 双轨。执行时若直接删一条通路，可能暴露此前被覆盖顺序掩盖的问题。
   - 缓解：先补行为测试，再做 owner 收口；对外保持原脚本名不变。

2. **恢复缺失 helper 可能重新扩大脚本 API 面**
   - `GetStreamingLevels()`、`GetRenderTransform()`、`GetOverlappingActorsOfClass()`、`RuntimeFloatCurve` 实例 helper 一旦恢复，会把当前脚本可见表面重新拉回接近参考实现的状态。
   - 缓解：把新增或恢复的脚本入口全部列入测试与变更说明，避免“静默回归成功”却没有文档同步。

3. **错误路径契约会从“崩溃 / 静默 no-op”改成“可诊断失败”**
   - `null World`、`null Widget`、`null SceneComponent`、invalid `FCurveKeyHandle` 等路径修正后，旧行为可能从进程级崩溃或无声吞掉变更，变成脚本异常、布尔失败或默认返回。
   - 缓解：测试必须先把最终契约固定，再同步更新相关 helper 注释与调用方预期。

---

## 第 3 轮补充（2026-04-09）

### Phase 4：异步 utility、metadata 与查询 helper 契约补强

- [ ] **P4.1** 收口 `RuntimeCurveLinearColor.AddDefaultKey` 的双 owner，实现与测试都只保留单一 source-of-truth
  - `FRuntimeCurveLinearColor` 是当前仍明确保留 active mixin 的少数 wrapper 之一，但 `AddDefaultKey()` 现在同时存在于 `RuntimeCurveLinearColorMixinLibrary.h` 和 `Bind_FunctionLibraryMixins.cpp` 的手写 lambda 中，已经形成双源实现。执行时应先决定唯一 owner：优先让 `URuntimeCurveLinearColorMixinLibrary::AddDefaultKey()` 成为唯一业务实现，手写 bind 只做最薄转发或直接删除，避免两处 4 通道写入逻辑继续漂移。
  - 这项工作不应该把 `RuntimeCurveLinearColor` 误归类为“又一个需要恢复 mixin 的失效 helper”；相反，它是当前少数仍然 active 的 wrapper/mixin 样本，问题在于 owner 没有收口、测试又只停在 compile smoke。目标是既保住实例语法，又让 RGBA 四通道语义只由一处逻辑定义。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 23 / 39：`RuntimeCurveLinearColor` 的 `AddDefaultKey` 同时由 FunctionLibrary 与 hand-written bind 维护，现有测试只验证能编译，不验证两条路径语义一致。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-4 / NewTest-7：`RuntimeCurveLinearColorCompile` 没有读取四条 `ColorCurves` 的 keys，缺少 RGBA 写入语义测试。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “D3 wrapper-based mixin policy：`RuntimeCurveLinearColor` 仍是当前插件少数刻意保留的 active mixin，不应继续放任双实现并存。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeCurveLinearColorMixinLibrary.h` L8-L22 — `ScriptMixin = "FRuntimeCurveLinearColor"` 仍然 active，`AddDefaultKey()` 直接写入四条 `ColorCurves`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp` L14-L31 — 同名实例方法与 helper namespace 又手写了一遍完全相同的 RGBA `AddKey` 逻辑。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeCurveLinearColorMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp`
- [ ] **P4.1** 📦 Git 提交：`[FunctionLibraries] Refactor: dedupe runtime linear color curve helper ownership`
- [ ] **P4.1-T** 单元测试：锁定 `RuntimeCurveLinearColor.AddDefaultKey` 的单 owner 与四通道写入语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp`
  - 测试场景：
    - 正常路径：脚本通过最终保留的实例入口调用 `Curve.AddDefaultKey()` 两次后，`ColorCurves[0..3]` 的 key 数量、时间和值都与 native 预期一致。
    - 边界条件：重复时间点、`Alpha == 0` / `Alpha == 1`、两次不同颜色写入都要保证四个通道同步增长，不出现漏写或通道错位。
    - 错误路径：legacy helper namespace 若继续保留，则必须与实例入口保持完全一致；若决定删掉，则测试要把“旧入口稳定报错/不可见、实例入口仍然存在”的最终契约固定下来，防止 owner 收口时方法静默消失。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.RuntimeCurveLinearColorAddDefaultKey`、`Angelscript.TestModule.Engine.BindConfig.RuntimeCurveLinearColorOwner`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add runtime linear color curve ownership coverage`

- [ ] **P4.2** 对齐 `SubsystemLibrary` 与当前 UE 原生 helper 的入口、metadata 与文档契约
  - `SubsystemLibrary` 当前不是单个函数小 bug，而是一组同域漂移：缺少 `GetAudioEngineSubsystem()`；6 个 getter 全部丢失 `ScriptNoDiscard`；`GetLocalPlayerSubsystemFromLocalPlayer()` 仍挂着无用的 `WorldContext`；`GetEngineSubsystem()` 顶部注释还在描述 `GameInstance` 语义。执行时应把这些问题作为一组 query helper 契约一起收口，而不是分散成零碎修补。
  - 这项工作还需要明确 owner：当前插件已经在 `Bind_Subsystems.cpp` 里为 `ULocalPlayerSubsystem` 生成了 `ClassName Get(ULocalPlayer LocalPlayer)` 的直达路径，但预处理器仍会生成 `Subsystem::GetLocalPlayerSubsystemFromLocalPlayer(...)` 这一条带 hidden world-context trait 的 helper。应把 function library、preprocessor 生成静态函数和 hand-written bind 的语义对齐成单一规则。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 17 / 19 / 74 / 84：缺少 `GetAudioEngineSubsystem`、getter 批量丢失 `ScriptNoDiscard`、`GetLocalPlayerSubsystemFromLocalPlayer` 挂了无用 `WorldContext`、`GetEngineSubsystem` 注释与真实签名冲突。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-10 / NewTest-45：subsystem helper 的上下文解析、多种 `LocalPlayer` context 分支和无效输入都缺 dedicated coverage。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一功能域被 runtime helper、hand-written bind 与生成路径共同承载时，owner 不清会放大维护成本。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h` L16-L22 — `GetEngineSubsystem()` 仍只有 `BlueprintCallable`，且注释误写成 `Game Instance Subsystem` 语义。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h` L24-L65 — 6 个 getter 全部缺少 `ScriptNoDiscard`；`GetLocalPlayerSubsystemFromLocalPlayer()` 仍带 `WorldContext = "WorldContextObject"`，但函数体只使用 `LocalPlayer` 与 `Class`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` L97-L106 — 当前已经存在 `ClassName Get(ULocalPlayer LocalPlayer)` 的 hand-written direct path。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L1290-L1293 — 生成静态函数仍调用 `Subsystem::GetLocalPlayerSubsystemFromLocalPlayer(LocalPlayer, Class.Get())`，说明无用 world-context trait 还在向脚本侧扩散。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
- [ ] **P4.2** 📦 Git 提交：`[FunctionLibraries] Fix: align subsystem helper metadata and parity`
- [ ] **P4.2-T** 单元测试：覆盖 `SubsystemLibrary` 的 lookup、context resolution 与 metadata 契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
  - 测试场景：
    - 正常路径：`GetEngineSubsystem`、`GetGameInstanceSubsystem`、`GetLocalPlayerSubsystem`、`GetWorldSubsystem`、`GetLocalPlayerSubsystemFromPlayerController`、`GetLocalPlayerSubsystemFromLocalPlayer` 与新增的 `GetAudioEngineSubsystem` 都返回与 native `USubsystemBlueprintLibrary` / `GetSubsystemBase()` 一致的实例。
    - 边界条件：`UUserWidget`、`APlayerController`、`APawn`、`AActor`、`UActorComponent`、`ULocalPlayer` 六种 context object 都能解析到同一个 `ULocalPlayerSubsystem`；`GetEngineSubsystem` 注释与脚本声明同步更新，不再误导为 `GameInstance` helper。
    - 错误路径：`null WorldContextObject`、没有 `LocalPlayer` 的 controller、无关 actor/component、无效 subsystem class 都必须稳定返回 `nullptr` 或可诊断失败；`GetLocalPlayerSubsystemFromLocalPlayer` 不得再携带无用 hidden world-context trait。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.SubsystemLookup`、`Angelscript.TestModule.FunctionLibraries.SubsystemLocalPlayerContextResolution`、`Angelscript.TestModule.Engine.BindConfig.SubsystemFunctionLibraryMetadata`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add subsystem helper metadata coverage`

- [ ] **P4.3** 对齐 `GameplayLibrary` 的异步 save/load helper 签名，并锁定 invalid input 下的立即失败回调契约
  - `UGameplayLibrary` 当前对 `UGameplayStatics` 的包装过于机械：`AsyncSaveGameToSlot()` 把原生可选 completion delegate 收窄成了必填动态 delegate，脚本侧被迫传占位参数；同时现有测试完全没有覆盖 save/load callback 的载荷、线程与 invalid input 行为。执行时应优先恢复“可以 fire-and-forget save，也可以绑定回调”的脚本面，再把成功与失败回调语义固定下来。
  - 这里不建议再引入与 `UGameplayStatics` 脱节的新调用形态。更稳的做法是保留当前动态 delegate wrapper，但补一个可选 delegate overload 或默认空 delegate 路径，让脚本使用习惯与原生 UE 保持一致。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 20 / 35：`AsyncSaveGameToSlot` 把 UE 原生可选 completion delegate 收窄成必填参数，脚本被迫传占位 delegate。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-9 / NewTest-43：异步 save/load callback 的载荷、game-thread 执行和 invalid input 下的立即失败回调契约都没有测试。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h` L37-L45 — `AsyncSaveGameToSlot()` 当前要求必填 `FAsyncSaveGameToSlotDynamicDelegate Delegate`，没有任何 fire-and-forget overload。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h` L57-L64 — `AsyncLoadGameFromSlot()` 只做最薄的 `ExecuteIfBound` 转发，现有代码面看不到任何对 invalid input 契约的测试约束。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayFunctionLibraryTests.cpp`
- [ ] **P4.3** 📦 Git 提交：`[FunctionLibraries] Fix: align gameplay async save-load helper contracts`
- [ ] **P4.3-T** 单元测试：覆盖异步 save/load helper 的回调载荷、线程与 invalid input 失败路径
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本执行 `AsyncSaveGameToSlot()` 后再 `AsyncLoadGameFromSlot()`，save/load callback 各触发一次，且 `SlotName`、`UserIndex`、`USaveGame` 字段值与 native 预期一致，并都运行在 game thread。
    - 边界条件：空 delegate fire-and-forget save、缺失 slot 的 load、重复调用同一 slot 都要表现稳定；如果引入 overload，则旧签名与新签名都要覆盖。
    - 错误路径：`null SaveGameObject`、`SlotName == ""`、明显不存在的 slot 都必须安全触发失败回调或稳定 no-op，不得崩溃，也不得出现“参数非法时完全不回调”的静默失败。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.AsyncSaveLoadContracts`、`Angelscript.TestModule.FunctionLibraries.AsyncSaveLoadInvalidInputs`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.3-T** 📦 Git 提交：`[FunctionLibraries] Test: add gameplay async save-load coverage`

- [ ] **P4.4** 收紧 `SoftReference` async delegate 的类型面，并为 null/pending/invalid path 建立统一失败契约
  - 当前 `SoftReference` 的同步路径和异步路径已经分裂：`TSoftObjectPtr<T>::Get()` / `TSoftClassPtr<T>::Get()` 能保留 subtype，但 `SoftReferenceStatics.h` 里的异步 delegate 统一退化成 `UObject*` / `UClass*`；同时 `LoadAsync()` 直接把 `ObjectCopy.ToString()` 送进 `LoadPackageAsync()`，没有在空 soft reference 上先做 guard。执行时应优先决定最终 API 契约：最好让 async 回调至少和同步 `Get()` 一样保留 subtype，或者提供兼容旧 delegate 的 typed overload / adapter；并把 null/pending/invalid path 的行为固定成脚本异常或稳定失败回调，而不是继续让调用方靠底层日志猜测。
  - 这项工作虽然会触及 `Bind_TSoftObjectPtr.cpp`，但 primary evidence 仍落在 `FunctionLibraries/SoftReferenceStatics.h`：当前全局 delegate 类型本身就是模板类型信息被擦除的根源之一。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 92：`TSoftObjectPtr<T>` / `TSoftClassPtr<T>` 的 `LoadAsync` 回调把模板实参退化成了 `UObject*` / `UClass*`；发现 43 / 46：null soft reference 会直接走无效 package 加载路径。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-13 / Issue-10 / Issue-17 / Issue-18：异步 delegate 契约、pending soft path、invalid path 与失败回调现在都没有行为级覆盖。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h` L5-L6 — `FOnSoftObjectLoaded` / `FOnSoftClassLoaded` 仍固定为 `UObject*` / `UClass*` payload。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` L483-L526 — `TSoftObjectPtr<T>::LoadAsync()` 仍直接接受宽类型 delegate，并在未命中已加载短路时直接 `LoadPackageAsync(*FPackageName::ObjectPathToPackageName(ObjectCopy.ToString()), ...)`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` L622-L650 — `TSoftClassPtr<T>::LoadAsync()` 同样复用宽类型 delegate，并沿用同一条 package-load 路径。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp`
- [ ] **P4.4** 📦 Git 提交：`[FunctionLibraries] Fix: harden soft reference async delegate contracts`
- [ ] **P4.4-T** 单元测试：覆盖 `SoftReference` async delegate 的类型、pending 状态与失败路径
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本对有效 `TSoftObjectPtr<UTexture2D>` 和 `TSoftClassPtr<AActor>` 调 `LoadAsync()`，回调各只触发一次，且 payload 与期望 asset/class 类型一致。
    - 边界条件：从合法但尚未解析的 soft path 构造 pending pointer，覆盖 `IsPending()`、加载前 `Get() == null`、加载后变为 valid 的状态迁移。
    - 错误路径：`null soft reference`、明显不存在的 object/class path 都必须触发稳定失败契约，payload 为空或抛出可匹配脚本异常，但不能卡住回调链，也不能静默排队一个不可诊断的无效加载。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.SoftReferenceAsyncDelegates`、`Angelscript.TestModule.FunctionLibraries.SoftReferencePendingPaths`、`Angelscript.TestModule.FunctionLibraries.SoftReferenceInvalidPaths`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.4-T** 📦 Git 提交：`[FunctionLibraries] Test: add soft reference async contract coverage`

- [ ] **P4.5** 修正 `Script` introspection helper 的脚本声明形态，并返回真实模块名而非 hotreload 去后缀基名
  - `Script::GetNameOfGlobalVariableBeingInitialized()` / `GetNamespaceOfGlobalVariableBeingInitialized()` / `GetModuleNameOfGlobalVariableBeingInitialized()` 当前同时有两类漂移：声明上丢掉了 `NotAngelscriptProperty` 与 `ScriptNoDiscard`，运行期实现上又把“模块名”错误映射成了 `baseModuleName`。执行时应先恢复这组三个纯查询 helper 的脚本形态约束，再把 `GetModuleName...()` 对齐为真实 module name。这样至少能先修正脚本 API 面与返回值语义，避免继续把 introspection helper 暴露成可属性化、可静默丢弃且拿不到 hotreload suffix 的弱契约。
  - 这里先不把“`InitializingGlobalProperty` 是进程级 static”扩成更大的 engine-isolation 重构；本轮更适合先把当前可以精确验证的 metadata 与 module-name 语义钉住，再把多 engine 隔离列为后续风险。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 18 / 34：`Script` 命名空间的 3 个 helper 丢掉了 `NotAngelscriptProperty` 与 `ScriptNoDiscard`；发现 90：`GetModuleNameOfGlobalVariableBeingInitialized` 返回 `baseModuleName` 而不是实际模块名。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-15：全局变量初始化上下文 helper 目前完全无测试，模块名应保留真实 suffix，而不是折叠成 base name。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h` L17-L35 — 3 个 helper 现在都只剩 `UFUNCTION(BlueprintCallable)`，注释里保留的旧 `NotAngelscriptProperty` 只是注释，没有真实 metadata。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` L26-L32 — `GetModuleNameOfGlobalVariableBeingInitialized()` 仍直接返回 `module->baseModuleName`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
- [ ] **P4.5** 📦 Git 提交：`[FunctionLibraries] Fix: restore script introspection metadata and module naming`
- [ ] **P4.5-T** 单元测试：固定全局变量初始化上下文 helper 的名字、命名空间、模块名与 metadata 契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
  - 测试场景：
    - 正常路径：在带 namespace 的脚本模块全局变量初始化表达式里调用三条 `Script::Get*BeingInitialized()`，返回的变量名、namespace 与模块名都与声明值一致，模块名保留真实 hotreload suffix。
    - 边界条件：全局 namespace 变量、带 namespace 变量、以及非初始化时机再次调用三条 helper 时都返回稳定结果，不把合法的全局 namespace 与“没有初始化上下文”混淆。
    - 错误路径：metadata 层不得再把这三条 query helper 当成 property accessor，也不得允许返回值静默丢弃；若 helper 在无初始化上下文下被调用，必须稳定返回空字符串而不是脏值。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.GlobalInitContext`、`Angelscript.TestModule.Engine.BindConfig.ScriptFunctionLibraryMetadata`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.5-T** 📦 Git 提交：`[FunctionLibraries] Test: add script introspection helper coverage`

### 单元测试总览增补（Phase 4）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P4.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp` | linear color curve 四通道写入、owner 收口、legacy helper 行为 | `P1` |
| `P4.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp` | subsystem lookup、`LocalPlayer` context resolution、metadata/world-context trait | `P1` |
| `P4.3` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayFunctionLibraryTests.cpp` | async save/load callback 载荷、game-thread 执行、invalid input 失败回调 | `P1` |
| `P4.4` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp` | async delegate 类型、pending path、null/invalid path | `P1` |
| `P4.5` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp` | 全局初始化上下文、真实模块名、query-helper metadata | `P2` |

### 验收标准补充（Phase 4）

1. `RuntimeCurveLinearColor`、`SubsystemLibrary`、`GameplayLibrary`、`SoftReferenceStatics`、`AngelscriptScriptLibrary` 五个领域都补齐了行为级测试，不再只依赖 compile smoke、`ClassFuncMaps` presence 或示例脚本编译通过。
2. 所有新增 query / async helper 都明确了最终脚本契约：哪些入口保留 `no_discard`、哪些不该带 hidden world-context、哪些 async 失败路径应回调、哪些输入应返回空值或脚本异常。
3. `FunctionLibraries/` 中本轮新增覆盖的 helper 不再保留“active mixin 但双实现漂移”“query helper 只有 `BlueprintCallable` 没有脚本语义元数据”“同步保留类型信息而异步回调退化成宽类型”的不一致状态。

### 风险与注意事项补充（Phase 4）

1. **metadata 修正可能改变脚本声明与静态诊断**
   - `SubsystemLibrary` 与 `AngelscriptScriptLibrary` 一旦恢复 `ScriptNoDiscard` / `NotAngelscriptProperty` 或移除无用 world-context，脚本声明字符串和部分编译期诊断会发生变化。
   - 缓解：同时补 bind-config metadata 测试，并把变更视为契约修正而不是 incidental side effect。

2. **async helper 的失败路径一旦收紧，旧调用点可能暴露隐藏问题**
   - `GameplayLibrary`、`SoftReference` 的 invalid input 过去可能只是静默失败或完全未被测试；修正后，旧脚本可能首次观察到稳定失败回调或脚本异常。
   - 缓解：先用自动化固定最终契约，再同步更新调用示例和相关注释，避免外部把契约修正误判为随机回归。

3. **`Script::GetModuleName...()` 行为修正会改变 hotreload 场景下的字符串比较结果**
   - 从 `baseModuleName` 改成真实模块名后，依赖“忽略后缀”做字符串比较的旧逻辑可能需要同步改写。
   - 缓解：把“真实模块名保留 suffix”写进测试与变更说明；若调用方确实需要 base name，应显式自行规约，而不是继续让 helper 名不副实。

4. **本轮不会解决 `InitializingGlobalProperty` 的进程级 static owner**
   - `AngelscriptScriptLibrary` 仍然读取 AngelScript 内核的全局 static 状态，多 engine 隔离不是这轮的直接落点。
   - 缓解：先修正 metadata 与 module-name 语义，并在后续 engine-isolation 主题中单独评估是否需要把 introspection state 收进插件自己的 engine context。

---

## 深化（2026-04-09 01:05）

### Phase 5：classic input helper 面拆分补强

> 承接既有 `P1.2`。上一轮已经锁定 classic input 的 contract drift、null guard 与 filter ignored，本轮把同一文件里仍未拆开的“缺失 helper 面”拆成两个可执行切片，避免继续把 `UInputComponent` 与 `UPlayerInput` 的 parity debt 混成一条大任务。

- [ ] **P5.1** 补齐 `UInputComponent` 的缺失 bind surface，保持 direct-bind / delegate-authority 路线不变
  - 承接 `P1.2`，但范围收窄到 `UInputComponent` 自身：在不把 callback authority 回退到 transient `UFunction` / helper UObject 的前提下，恢复当前文件里完全缺失的无参 `BindAction` / `BindKey` / `BindChord` overload、`BindTouch` / `BindGesture`、以及 value-only `BindAxis` / `BindAxisKey` / `BindVectorAxis` 入口，同时保持和已有 helper 一致的 null-guard 与 binding-return contract。
  - 这项工作不应与 `Bind_UEnhancedInputComponent.cpp` 混层。更稳的做法是借用当前插件已经在 EnhancedInput 上验证过的“direct bind 返回 binding/handle，delegate authority 仍留在 UE delegate 本身”模式，把 classic input 的入口补齐，而不是再造一层 metadata rewrite 或 helper UObject。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 87 / 88 / 94 / 95 / 98：`BindAxisKey` 退化成 `FName`、六个 bind helper 全吞返回值、无参 action/key/chord handler 缺失、`BindTouch` / `BindGesture` 缺失、axis 只剩必须注册回调的版本。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-5 / NewTest-37：当前 classic input 只规划了 mapping/context round-trip 与 null 输入错误路径，仍没有 dedicated coverage 去锁定缺失 bind surface 的最终脚本契约。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一功能域被 runtime helper 与 hand-written bind 共同承接时，owner 不清会放大维护成本。”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “当前插件与 Hazelight 都应保持 direct bind / delegate-authority 路线，不需要把 callback slot 回写成额外 metadata owner。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “Hazelight 的 `InputComponent` wrapper 仍是 active class-level `ScriptMixin`；当前快照则退成 `BlueprintCallable` 静态 helper，member-style surface 明显收缩。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` L18-L105 — 当前 `UInputComponent` 区段只有 `BindAction` / `BindKey` / `BindChord` / `BindAxis` / `BindAxisKey` / `BindVectorAxis` 六条 helper，没有 `BindTouch` / `BindGesture`，也没有无参 handler 或 value-only axis overload。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` L25-L100 — 上述六条 helper 仍全部返回 `void`；`BindAxisKey()` 仍把原生 `FKey` 入口收窄成 `const FName& AxisKey`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp`
- [ ] **P5.1** 📦 Git 提交：`[FunctionLibraries] Feat: restore classic input binding surface parity`
- [ ] **P5.1-T** 单元测试：覆盖 classic input 缺失 bind surface 的 direct-bind 契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本可用无参 `BindAction` / `BindKey` / `BindChord` 绑定 handler，可注册 `BindTouch` / `BindGesture`，也可走 value-only `BindAxis` / `BindAxisKey` / `BindVectorAxis` 再配合轮询读取当前值；若本项恢复 binding 返回值，则新旧 overload 都能拿到可继续配置的 binding/handle。
    - 边界条件：重复绑定同一 action/key/chord、`bConsumeInput` 与 `bExecuteWhenPaused` true/false 组合、无触摸设备环境、以及同一 axis 同时存在 delegate 版与 value-only 版时，都要得到稳定且可诊断的结果。
    - 错误路径：`null UInputComponent`、无效 `FKey` / `FInputChord`、缺失 delegate receiver、或 `BindTouch` / `BindGesture` 传入非法参数时，必须安全失败，且不能污染 `ActionBindings` / `KeyBindings` / `AxisBindings`。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.ClassicInputBindingSurface`、`Angelscript.TestModule.FunctionLibraries.ClassicInputTouchAndGesture`、`Angelscript.TestModule.FunctionLibraries.ClassicInputValueOnlyAxisContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.1-T** 📦 Git 提交：`[FunctionLibraries] Test: cover classic input binding surface parity`

- [ ] **P5.2** 补齐 `UPlayerInput` 的运行时 keystate / axis configuration / discard helper 面
  - 承接 `P1.2` 的 mapping/filter/null-guard 收口，本项只处理 `UPlayerInput` 仍缺的 runtime query surface：`FlushPressedKeys()` / `DiscardPlayerInput()`、按键状态查询、axis/value 查询、axis property 读写与 invert 反查。现在的文件只覆盖“改 mapping + 改鼠标灵敏度”，还没有把 `UPlayerInput` 作为运行时输入状态容器那一半职责暴露出来。
  - 这项工作应与 `UInputComponent` 绑定恢复分开执行。前者解决“如何声明对某类输入感兴趣”，后者解决“如何查询当前输入状态和配置 axis 特性”。如果继续混在一个 task 里，执行时很容易只修 binding surface，遗漏 runtime query / config 这一半。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 96 / 97：`UPlayerInputScriptMixinLibrary` 只暴露 mapping 管理、`InvertAxis` 和鼠标灵敏度；`FlushPressedKeys` / `DiscardPlayerInput` / `IsPressed` / `WasJustPressed` / `GetKeyValue` / `GetAxisProperties` / `SetAxisProperties` / `GetInvertAxis` / `InvertAxisKey` / `ClearSmoothing` 等整组 runtime helper 缺席。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-5 / Issue-24：现有建议测试只覆盖 mapping/context round-trip，且 FunctionLibrary 断言不应继续埋在 bind-config megafile；`UPlayerInput` runtime helper 需要 dedicated 行为级测试文件。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一功能域被多条 owner 路径承接时，必须先明确哪一层负责完整脚本 surface。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “Hazelight 的 `PlayerInput` wrapper 仍属于 class-level `ScriptMixin` 覆盖面；当前快照只保留 mapping 子集，helper surface 收缩。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` L151-L249 — 当前 `UPlayerInput` 区段只包含 mapping 增删、`ForceRebuildingKeyMaps()`、`GetKeysFor*()`、`GetEngineDefined*Mappings()`、`InvertAxis()` 与鼠标灵敏度 getter/setter，没有任何 keystate、value query、axis property、invert query 或 discard helper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` L226-L249 — 文件在 `GetMouseSensitivityY()` 后直接结束，确认 `FlushPressedKeys()` / `DiscardPlayerInput()` / `IsPressed()` / `WasJustPressed()` / `GetTimeDown()` / `GetKeyValue()` / `GetAxisProperties()` 等入口在当前源码里仍不存在。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp`
- [ ] **P5.2** 📦 Git 提交：`[FunctionLibraries] Feat: expose player input runtime state and axis config helpers`
- [ ] **P5.2-T** 单元测试：锁定 `UPlayerInput` 运行时状态、axis 配置与 discard 契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本能对已注入输入状态执行 `IsPressed()` / `WasJustPressed()` / `WasJustReleased()` / `GetTimeDown()` / `GetKeyValue()` / `GetRawKeyValue()` / `GetProcessedVectorKeyValue()` / `GetRawVectorKeyValue()` 查询；`GetAxisProperties()` / `SetAxisProperties()` / `GetInvertAxis()` / `GetInvertAxisKey()` / `InvertAxisKey()` / `ClearSmoothing()` 与 native 结果一致。
    - 边界条件：未按下按键、未配置 axis property、重复 `FlushPressedKeys()` / `DiscardPlayerInput()`、默认 dead zone / sensitivity / exponent、以及没有任何 mapping 的 `UPlayerInput` 都要返回稳定默认值，不把“未配置”和“脏状态残留”混淆。
    - 错误路径：`null UPlayerInput`、无效 `FKey`、无效 axis name、缺失 axis property 目标时必须安全失败；`DiscardPlayerInput()` / `FlushPressedKeys()` 不得在错误路径下留下残余按键状态。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.PlayerInputRuntimeState`、`Angelscript.TestModule.FunctionLibraries.PlayerInputAxisProperties`、`Angelscript.TestModule.FunctionLibraries.PlayerInputDiscardAndFlush`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add player input runtime helper coverage`

### 单元测试总览增补（Phase 5）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P5.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp` | classic input 无参 binding、`BindTouch` / `BindGesture`、value-only axis bind、错误路径不污染 binding arrays | `P1` |
| `P5.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputFunctionLibraryTests.cpp` | `UPlayerInput` keystate、axis property、invert query、flush/discard 行为 | `P1` |

### 验收标准补充（Phase 5）

1. `InputComponentScriptMixinLibrary.h` 不再只停留在“mapping/filter/null-guard”子集，而是恢复 `UInputComponent` 与 `UPlayerInput` 两半职责的最小可用 helper 面。
2. classic input 的 callback authority 仍保持 direct-bind / delegate-owner 路线，不回退到额外 `UFunction` 注入或 helper UObject 中介；同时新恢复的 bind / query helper 都有 dedicated 行为级自动化覆盖。
3. `AngelscriptInputFunctionLibraryTests.cpp` 不再只验证 mapping round-trip，而是同时固定 binding surface、keystate、axis property 与 discard/flush 契约。

### 风险与注意事项补充（Phase 5）

1. **补入口会改变脚本 API 面，但不应改变既有 callback authority**
   - `UInputComponent` / `UPlayerInput` 一旦恢复缺失 helper，脚本自动补全与类型信息会明显增多；但这不等于允许把 callback owner 改写成额外 metadata 或 transient `UFunction`。
   - 缓解：实现时显式复用 current `Bind_UEnhancedInputComponent.cpp` 与 Hazelight direct-bind 路线，只新增 surface，不改 authority 模式。

2. **输入状态测试容易受宿主环境噪声影响**
   - `Touch` / `Gesture`、axis 值、`WasJustPressed()` 这类场景如果直接依赖真实平台输入，会让自动化不稳定。
   - 缓解：优先用 test fixture / synthetic input state 驱动 `UInputComponent` 与 `UPlayerInput`，让测试只验证 helper 契约，不依赖真实设备事件。

3. **`UPlayerInput` runtime helper 一旦补齐，旧脚本可能暴露隐藏的“手工缓存输入值”逻辑**
   - 过去脚本没有 `GetKeyValue()` / `FlushPressedKeys()` / `DiscardPlayerInput()` 等接口时，调用方可能自己维护了近似状态机；新 helper 上线后，旧 workaround 可能与原生状态查询冲突。
   - 缓解：把“推荐改用原生 helper、废弃手工缓存”的迁移注意事项写进后续文档或变更说明，避免把契约收口误判成行为回归。

---

## 深化（2026-04-09 01:14）

### Phase 6：补齐 `WorldCollision` 与 `FrameTime` 的剩余 FunctionLibrary 盲区

> 现有 `Phase 1-5` 已覆盖多数 `FunctionLibraries/` 文件，但 `WorldCollisionStatics.h` 与 `AngelscriptFrameTimeMixinLibrary.h` 仍未被纳入计划主体。本轮只补这两个仍有真实源码锚点、且在五维输入里有明确支撑的尾部缺口，避免继续让运行期 crash path、async 载荷退化和 frame-time utility 空白留在计划之外。

- [ ] **P6.1** 为 `WorldCollision` 全链路建立 null-world 失败契约，并把 compile-only coverage 翻转成可执行行为回归
  - 当前 `WorldCollision` 的同步 query、component query、async trace / overlap 以及 `QueryTraceData()` / `QueryOverlapData()` / `IsTraceHandleValid()` 都共享同一条空指针路径：`GetWorld()` 明明已经按 `LogAndReturnNull` 设计成“拿不到 world 就返回 `nullptr`”，但所有 helper 紧接着直接 `->` 解引用。这个问题继续留在现状里，脚本只要脱离 `__WorldContext()` 可解析区间，就会从“记录日志”升级成直接崩溃。
  - 本项应先统一失败契约，再谈测试细节：所有返回 `bool` 的同步 helper 与 query helper 在 `World == nullptr` 时都短路返回 `false`，同时显式清空/保持空 `OutHit`、`OutHits`、`OutOverlaps`；所有返回 `FTraceHandle` 的 async helper 在 `World == nullptr` 时都返回默认 invalid handle，不触发回调，也不把无效请求排进引擎异步队列。这样后续脚本才能把“没有 world context”当成可测试、可诊断的错误路径，而不是未定义行为。
  - 这一项不只是“补 if 判空”。执行时还应把现有 `WorldCollisionCompile` 的 compile smoke 从 parity megafile 中拆出来，和 runtime 行为测试落在同一个专用文件里，避免之后再次出现“符号能编译但运行时直接崩”的假绿灯。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 33：`WorldCollision` 全局 helper 统一走 `GetWorld()->...` 直解引用，脚本一旦脱离 world context 会直接崩溃。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-11 / NewTest-14 / NewTest-32：当前 `WorldCollisionCompile` 只做 API 面编译探测，没有执行同步 trace / overlap，也没有验证异步回调、命中结果与错误路径。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “[D4/D2] 当前插件的脚本 world-context contract 已经是 resolver `__WorldContext()` / `TryGetCurrentWorldContextObject()`，说明‘取不到 world’本来就是需要被 helper 正式处理的分支，不应再靠空指针崩溃兜底。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L13-L16 — `WorldCollision::GetWorld()` 仍使用 `EGetWorldErrorMode::LogAndReturnNull`，缺失 world 时明确会返回 `nullptr`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L149-L316 — 全部同步 trace / overlap / component query helper 仍直接调用 `WorldCollision::GetWorld()->...`，没有任何 null guard。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L328-L467 — 全部 async helper 与 `QueryTraceData()` / `QueryOverlapData()` / `IsTraceHandleValid()` 仍沿用同一条直解引用路径。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WorldCollisionStatics.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp`
- [ ] **P6.1** 📦 Git 提交：`[FunctionLibraries] Fix: harden world collision null-world failure contracts`
- [ ] **P6.1-T** 单元测试：覆盖 `WorldCollision` 同步/异步 helper 的正常路径、空 world 失败路径与 compile-smoke 拆分后的真实行为
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：在最小 world geometry 下执行 `LineTraceSingleByChannel`、`OverlapMultiByProfile`、`ComponentSweepMulti`、`AsyncOverlapByProfile`，验证命中 actor/component、`OutHit` / `OutHits` / `OutOverlaps` 以及 async callback 触发次数与 native 结果一致。
    - 边界条件：合法 world 但无命中、未绑定 async delegate、重复查询同一个 invalid handle、以及 component query 传入存在但未命中的 primitive component 时，返回值和 out 数据都保持稳定默认值。
    - 错误路径：没有当前 world context、`PrimComp == null`、或在 null-world 下发起 async trace 时，不得崩溃；同步 helper 返回 `false` 且 out 数据保持空，async helper 返回 invalid handle 且不触发回调。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.WorldCollisionSyncQueries`、`Angelscript.TestModule.FunctionLibraries.WorldCollisionAsyncCallbacks`、`Angelscript.TestModule.FunctionLibraries.WorldCollisionNullWorldGuards`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add world collision runtime contract coverage`

- [ ] **P6.2** 恢复 `WorldCollision` async handle / datum 的完整脚本载荷，避免 delegate surface 退化成残缺 carrier
  - 当前 `WorldCollision` async callback 和查询 API 已经出现了类型断裂：回调侧把 `FTraceHandle` 压扁成 `uint64`，查询侧却仍要求完整 `FTraceHandle`；同时 `FTraceHandle` 本身只暴露了最小字段，`FTraceDatum` / `FOverlapDatum` 又只暴露了极少数结果字段。结果是脚本能“收到回调”，却拿不到无损继续查询所需的 handle / context 语义，这和当前插件在其他 delegate surface 上强调的“脚本值类型是一等公民”路线相冲突。
  - 本项要先统一 carrier，而不是零散补字段。优先方案是让 async callback 直接携带脚本可消费的 typed handle；如果 dynamic delegate 受 UHT 反射限制不能直接承载原生 `FTraceHandle`，则在 `WorldCollisionStatics.h` 引入一个反射友好的 `FScriptTraceHandle` wrapper，至少无损保留 `_Handle`、`FrameNumber`、`Index`、`bTransactional`，并为它与 native `FTraceHandle` 提供明确转换。然后在 `Bind_WorldCollision.cpp` 里把 async callback、`QueryTraceData()`、`QueryOverlapData()`、`IsTraceHandleValid()`、`Invalidate()` / `IsTransactional()` 串成同一条 typed round-trip 链路。
  - 对 datum 结构体也要一并收口：既然脚本已经有 `QueryTraceData()` / `QueryOverlapData()` 入口，就不应继续只拿到删过字段的半成品。执行时应把 `FrameNumber`、`PhysWorld`、`CollisionParams` / `ObjectQueryParams` / `ResponseParam` / `CollisionShape` 这类后续排障和结果重放需要的上下文一起评估并补齐到脚本面，至少保证 callback 载荷与 query 结果不会再互相缺字段。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 44 / 70 / 76：async delegate 把 `FTraceHandle` 压扁成 `uint64`，`FTraceHandle` 缺少 `Invalidate()` / `IsTransactional()`，`FTraceDatum` / `FOverlapDatum` 只暴露残缺子集。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-14 / NewTest-32：缺少 async delegate 载荷、同步/异步查询结果、handle round-trip 与 datum 语义的执行级覆盖。”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “[D2-DelegateSurface] 当前 Angelscript 的优势是 delegate surface 仍以签名化值类型为 authority，而不是退回 helper/proxy carrier；`WorldCollision` 的 `uint64` handle 压扁违背了这条路线。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WorldCollisionStatics.h` L7-L8 — `FScriptTraceDelegate` / `FScriptOverlapDelegate` 仍把第一个参数声明为裸 `uint64 TraceHandle`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L44-L63 — `FTraceHandle` 当前只绑定了两个构造器、`opEquals`、`IsValid()` 以及 `_Handle` / `_FrameNumber` / `_Index` 三个字段。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L95-L105、L131-L139 — `FTraceDatum` / `FOverlapDatum` 当前只暴露少数结果字段，缺失基类上下文信息。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L328-L452 — async trace / overlap lambda 仍统一把 `TraceHandle._Handle` 传给脚本回调。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L455-L467 — `QueryTraceData()` / `QueryOverlapData()` / `IsTraceHandleValid()` 仍要求完整 `FTraceHandle`，回调载荷与查询入口已分裂成两套 carrier。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WorldCollisionStatics.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp`
- [ ] **P6.2** 📦 Git 提交：`[FunctionLibraries] Refactor: restore world collision handle and datum fidelity`
- [ ] **P6.2-T** 单元测试：锁定 `WorldCollision` async handle round-trip、datum payload 与 invalidation 契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：async trace / overlap callback 返回的 typed handle 可以直接进入 `QueryTraceData()` / `QueryOverlapData()` / `IsTraceHandleValid()` 链路，且 `UserData`、`OutHits` / `OutOverlaps` 与 query 返回的 datum 保持一致。
    - 边界条件：合法但无命中的 async query 仍返回结构完整的 handle / datum；若实现补了 `Invalidate()` / `IsTransactional()`，则要验证 default handle、已完成 handle 与手工 invalidated handle 的状态迁移。
    - 错误路径：invalid handle、synthetic handle、以及不能构造真实 transactional trace 的测试环境下，都必须得到稳定 `false` / 空 datum；若为保留 `bTransactional` 引入了 wrapper，还要单独验证 wrapper 与 native `FTraceHandle` 的 round-trip 不丢状态位。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.WorldCollisionAsyncHandleRoundTrip`、`Angelscript.TestModule.FunctionLibraries.WorldCollisionDatumPayloads`、`Angelscript.TestModule.FunctionLibraries.WorldCollisionHandleInvalidation`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add world collision handle fidelity coverage`

- [ ] **P6.3** 补齐 `FQualifiedFrameTime` 的常用 workflow helper，并把 `AsSeconds()` 的 deterministic 语义单独钉住
  - `UAngelscriptFrameTimeMixinLibrary` 当前只额外补了一个 `AsSeconds()`，这让脚本在 frame-accurate 时间处理上只能半手工工作：要么自己拼 `FFrameRate` 换算，要么自己做 `Timecode` 生成。既然当前文件已经明确以 `ScriptMixin = "FQualifiedFrameTime"` 的形式存在，就更适合把最常和 `AsSeconds()` 成对出现的 `ConvertTo(FFrameRate DesiredRate)` 与 `ToTimecode(...)` 补齐，而不是继续把 `FrameTime` 工作流停在一个孤立 helper 上。
  - 这一项应把“扩 API 面”和“补测试”一起做。`AsSeconds()` 本身虽然实现只是薄转发，但现在完全没有 dedicated coverage；执行时应先用 deterministic expected-value 固定整数帧、分数帧和 NTSC 分数帧率的语义，再补 `ConvertTo()` / `ToTimecode(...)`，避免后续 helper 面扩展后仍只有一个 compile smoke。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 51：`FQualifiedFrameTime` 的 FunctionLibrary 只补了 `AsSeconds()`，却漏掉帧率转换和 timecode 两组高频 utility。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-16：`FQualifiedFrameTime.AsSeconds` 当前没有 dedicated 行为级测试，应比较整数帧与分数帧的手工计算结果。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h` L7-L16 — 当前 `ScriptMixin = "FQualifiedFrameTime"` 的整份 helper 仍只包含一个 `AsSeconds(const FQualifiedFrameTime& Target)`，没有 `ConvertTo()` 或 `ToTimecode(...)`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFrameTimeFunctionLibraryTests.cpp`
- [ ] **P6.3** 📦 Git 提交：`[FunctionLibraries] Feat: extend qualified frame time helper workflow`
- [ ] **P6.3-T** 单元测试：固定 `FQualifiedFrameTime` 的秒数、帧率转换与 timecode 语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFrameTimeFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：整数帧与分数帧 `FQualifiedFrameTime` 的 `AsSeconds()` 与手工 expected value 一致；`ConvertTo(FFrameRate)` 与 native 参考结果一致；`ToTimecode(...)` 输出与原生 helper 对齐。
    - 边界条件：`0` 帧、NTSC 分数帧率、带 sub-frame 的 frame time、以及跨不同目标帧率转换时都保持稳定结果，不出现意外舍入漂移。
    - 错误路径：default-constructed `FQualifiedFrameTime`、无效/异常 `FFrameRate` 输入若原生会返回默认值、失败结果或断言前置条件，脚本 helper 也必须保持同样的 deterministic 契约，不能引入额外除零或静默错算。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.FrameTimeAsSeconds`、`Angelscript.TestModule.FunctionLibraries.FrameTimeConvertTo`、`Angelscript.TestModule.FunctionLibraries.FrameTimeToTimecode`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.3-T** 📦 Git 提交：`[FunctionLibraries] Test: add qualified frame time helper coverage`

### 单元测试总览增补（Phase 6）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P6.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp` | 同步 trace / overlap、async callback、null-world 与 invalid handle 失败路径 | `P1` |
| `P6.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionFunctionLibraryTests.cpp` | async handle round-trip、datum payload、`Invalidate()` / `IsTransactional()` / wrapper 保真 | `P1` |
| `P6.3` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFrameTimeFunctionLibraryTests.cpp` | `AsSeconds()`、`ConvertTo()`、`ToTimecode(...)` 的 deterministic 结果 | `P2` |

### 验收标准补充（Phase 6）

1. `WorldCollision` helper 在缺失 current world context 时不再通过空指针崩溃兜底，而是统一返回可测试的 `false` / invalid handle / 空结果契约。
2. async trace / overlap 的 callback payload、query API 与脚本值类型不再分裂成两套 carrier；脚本侧可以无损保留并复用 handle 语义，至少不会再因为 `uint64` 压扁而丢失必要状态。
3. `FQualifiedFrameTime` 不再只有孤立的 `AsSeconds()`；脚本侧能直接完成秒数、帧率转换和 timecode workflow，且三类 helper 都有 dedicated 行为级自动化保护。

### 风险与注意事项补充（Phase 6）

1. **`WorldCollision` 失败契约从“崩溃”改成“可诊断失败”后，旧脚本可能首次观察到稳定返回值**
   - 过去某些调用点可能隐式依赖“这里总有 world”；修正后会看到 `false`、empty out array 或 invalid handle，而不是直接崩溃。
   - 缓解：把 null-world 返回值写进测试与变更说明，明确这属于 crash-path 修复，不是随机行为变化。

2. **async handle fidelity 可能受 UHT / dynamic delegate 可承载类型限制**
   - 如果 `FTraceHandle` 不能直接进入 dynamic delegate，就必须引入反射友好的 wrapper；否则无法既保留 typed carrier，又维持蓝图/脚本可见性。
   - 缓解：优先做一层最小 wrapper，而不是继续传播裸 `uint64`；同时让 query API 和 callback payload 共用同一条转换链。

3. **`FrameTime` helper 一旦补齐，脚本 API 面会扩大到 Sequencer / Timecode 工作流**
   - 这会提升 discoverability，但也意味着 `AsSeconds()` 不再是该 mixin 的唯一入口，任何命名或 overload 选择都需要与 UE 原生语义严格对齐。
   - 缓解：实现前先对齐 native 方法名与参数顺序，测试里直接用 native reference 结果做对照，不自造第二套舍入规则。

---

## 深化（2026-04-09 01:24）

### Phase 7：补齐剩余 `Math` / `AssetManager` helper 面与脚本声明契约

> `Phase 1-6` 已覆盖运行期崩溃、classic input、GameplayTag、Widget、WorldCollision 与 FrameTime 等主线问题；本轮只补前面尚未展开的三组剩余高价值缺口：`Rotator/Transform` 的实例 helper 面、`Math/FQuat` 的 `ScriptNoDiscard` / `ScriptTrivial` 契约，以及 `AssetManager` 的 scan/load/id workflow。这样可以在不重复既有条目的前提下，把 `AngelscriptMathLibrary.h` 与 `UAssetManagerMixinLibrary.h` 仍然悬空的高频脚本工作流一起收口。

- [ ] **P7.1** 恢复 `FRotator` / `FRotator3f` / `FTransform` / `FTransform3f` 的实例 helper 面，并把 rotator-friendly overload 从 namespace/static wrapper 拉回目标类型
  - 当前 `AngelscriptMathLibrary.h` 里这四组 helper 仍保留着明确的 rotator/transform 业务逻辑，但 `ScriptMixin` 已被注释，结果是 `MakeFromAxes()` 这类 namespace 工厂还能留下来，`GetRightVector()` / `Compose()` / `AngularDistance()` 与 `TransformRotation(FRotator)` / `SetRotation(FRotator)` 这种真正依赖实例语法的 helper 则掉出目标类型实例面。执行时应把这组“姿态工作流”按单一 owner 收口：要么恢复 mixin 实例面，要么把缺失的实例方法显式迁到 hand-written bind；不能继续让脚本作者在 `Rotator.Vector()` 仍可用的同时，被迫为相邻 helper 回退到 `FRotator::GetRightVector(Rotator)` 或 `Transform.SetRotation(Rotator.Quaternion())` 这种样板写法。
  - 这一项还需要一并收紧 `SetRotation(FRotator)` / `SetRotation(FRotator3f)` 的声明形态。当前 FunctionLibrary 注释里保留了 `NotAngelscriptProperty`，但真实声明已经丢掉该 metadata；如果只恢复可见性、不恢复“它必须是方法而不是 property accessor”的脚本契约，最终 API 面仍会继续分裂。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 25 / 27：`FRotator` / `FRotator3f` 的方向与组合 helper 因 `ScriptMixin` 失效退化成 namespace 静态函数，`FTransform` / `FTransform3f` 的 rotator-friendly overload 只剩 FunctionLibrary wrapper，实例面只保住了 quaternion 版本。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-14 / NewTest-23 / NewTest-31：`MathExtendedCompat` 没有触达 shortest-path、姿态构造和 `FTransform` 旋转 helper，这组姿态 API 目前缺少 dedicated 行为回归。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一领域脚本支持可能同时分散在 hand-written bind 与 runtime helper 两条 owner 上，若不显式化 owner，维护者很容易只修一条通路。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L512-L561 — `UAngelscriptFRotatorLibrary` 仍只有 `ScriptName = "FRotator"`，`ScriptMixin` 已被注释，`GetForwardVector()` / `GetRightVector()` / `GetUpVector()` / `Compose()` / `AngularDistance()` 仍停留在静态 wrapper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRotator.cpp` L161-L180 — hand-written bind 目前只补了 namespace 下的 `MakeFrom*` 工厂和 `Vector()` / `RotateVector()` / `UnrotateVector()`，没有 `GetRightVector()` / `Compose()` / `AngularDistance()` 对应 method。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L781-L819、L877-L888 — `FTransform` / `FTransform3f` 仍保留 `TransformRotation(FRotator*)`、`InverseTransformRotation(FRotator*)`、`SetRotation(FRotator*)` wrapper，但 `ScriptMixin` 与 `NotAngelscriptProperty` 都只留在注释。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform.cpp` L163-L205 — 当前实例面只有 `FQuat` 版 `TransformRotation()` / `InverseTransformRotation()` / `SetRotation()`，确认 rotator overload 尚未回填到 hand-written bind。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRotator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRotator3f.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform3f.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
- [ ] **P7.1** 📦 Git 提交：`[FunctionLibraries] Fix: restore rotator and transform instance helper surface`
- [ ] **P7.1-T** 单元测试：锁定 `FRotator` / `FTransform` 姿态 helper 的实例调用面、overload 与 property 语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：`Rotator.GetRightVector()` / `GetUpVector()` / `Compose()` / `AngularDistance()` 与 native 结果一致；`Transform.TransformRotation(Rotator)` / `InverseTransformRotation()` / `SetRotation(Rotator)` 的 round-trip 与 native `FQuat` 中转结果一致。
    - 边界条件：default / identity `FRotator`、`FTransform`、`FRotator3f`、`FTransform3f`，以及跨 `+180/-180` 边界的姿态组合都保持稳定结果，不因 float/double 版本切换而出现语义漂移。
    - 错误路径：`SetRotation(FRotator)` / `SetRotation(FRotator3f)` 必须继续以普通方法而非 property accessor 形态暴露；对错误的 property-style 脚本写法给出稳定 compile failure，而不是静默绑定到错误入口。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.MathRotatorInstanceHelpers`、`Angelscript.TestModule.FunctionLibraries.MathTransformRotatorOverloads`、`Angelscript.TestModule.FunctionLibraries.MathTransformPropertyContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add rotator and transform helper coverage`

- [ ] **P7.2** 恢复 `Math` / `FQuat` 纯值 helper 的 `ScriptNoDiscard` / `ScriptTrivial` 契约，消除同域 API 的声明与调用路径分裂
  - 这一组 helper 的问题不再是“有没有函数”，而是“函数虽然还在，但脚本声明 contract 已经分裂”。`LerpShortestPath()`、`RInterpShortestPathTo()`、`TInterpTo()`、`Modf_*()`、`Wrap*()`、`WrapIndex*()` 的旧 `ScriptTrivial + ScriptNoDiscard` 仍清楚地留在注释里，真实声明却退化成普通 `BlueprintCallable`；`FQuat::MakeFromX/Y/Z/.../MakeFromAxes` 也一样失去 `ScriptNoDiscard`。与此同时，同一命名空间里的 hand-written `FQuat::MakeFromEuler()` / `FindBetween*()` 仍显式保留 `no_discard`。继续维持这种“同一语义域里一半 helper 走轻量 native bind、另一半走普通 UFUNCTION；一半丢返回值会报错、另一半不会”的状态，会让脚本作者无法预测 API 规则，也让高频数值 helper 无端回到更重的调用路径。
  - 本项应把“纯值 helper 的脚本声明 contract”当成一等能力回填，而不是只补运行期 happy path。执行时要把 `ScriptNoDiscard` 拉回纯返回值/纯工厂 helper，把 `ScriptTrivial` 拉回这批高频 numeric helper，再用 signature/compile 级测试固定 `no_discard` 与 trivial 注册链，避免以后再被静默回退。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 30 / 72 / 73：`FQuat` / `FQuat4f` 轴向构造 helper 丢掉 `no_discard`，`Math` 纯返回值 helper 批量丢失 `ScriptNoDiscard` 与 `ScriptTrivial`，同域 API 的诊断和绑定路径已分裂。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-14 / Issue-16 / Issue-19 / NewTest-27 / NewTest-31：当前 `MathExtendedCompat` 既没有稳定覆盖 deterministic helper，也没有任何 dedicated 测试去锁定 `Modf` / shortest-path / 姿态工厂的精确返回值与声明语义。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L27-L30、L92-L101、L112-L202、L296-L321 — `LerpShortestPath()`、`Modf_*()`、`Wrap*()`、`WrapIndex*()` 的旧 `ScriptTrivial` / `ScriptNoDiscard` 只剩注释，真实 `UFUNCTION` 已不再携带这两类 metadata。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L631-L696 — `FQuat MakeFromX()` 到 `MakeFromAxes()` 同样只剩 `BlueprintCallable`，`ScriptNoDiscard` 与 `ScriptTrivial` 都未保留。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FQuat.cpp` L145-L150 — 同一 `FQuat` 命名空间下 `MakeFromEuler()` / `MakeFromRotator()` / `FindBetween*()` 仍显式 `no_discard`，确认当前 contract 分裂客观存在。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FQuat.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptMathSignatureContractTests.cpp`
- [ ] **P7.2** 📦 Git 提交：`[FunctionLibraries] Fix: restore math and quat signature contracts`
- [ ] **P7.2-T** 单元测试：固定 `Math` / `FQuat` 纯值 helper 的 `no_discard`、`trivial` 与确定性返回值语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptMathSignatureContractTests.cpp`
  - 测试场景：
    - 正常路径：`Math::LerpShortestPath()`、`Modf_*()`、`Wrap*()`、`WrapIndex*()` 与 `FQuat::MakeFromX()` / `MakeFromAxes()` 的返回值与 native reference 一致，并通过签名/绑定检查确认带有预期的 `no_discard` / trivial 契约。
    - 边界条件：`Alpha = 0 / 1`、`Modf(-3.75)`、`WrapIndex(Min == Max)`、单位轴与近零轴输入都给出稳定结果，不因 float/double 版本切换而丢失诊断一致性。
    - 错误路径：脚本显式丢弃这些 pure-return helper 的返回值时，应产生稳定 compile diagnostic；不能继续无提示通过编译。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.MathSignatureContracts`、`Angelscript.TestModule.FunctionLibraries.MathDeterministicNumericHelpers`、`Angelscript.TestModule.FunctionLibraries.QuatFactoryNoDiscard`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add math signature contract coverage`

- [ ] **P7.3** 承接 `P2.2` 之外补齐 `UAssetManager` 的 scan/filter/load/id workflow，恢复统一的资产工作流入口
  - `P2.2` 已覆盖 `GetPrimaryAssetTypeInfo*` 导出与 initial-scan callback 的失败契约，本项不重复那部分，而是只补剩余 workflow 缺口：`UAssetManagerMixinLibrary` 仍关闭 `ScriptMixin`，查询 helper 继续停留在静态类；`ScanPathForPrimaryAssets()`、`GetPrimaryAssetObjectList()` 与带 `EAssetManagerFilter` 的 `GetPrimaryAssetIdList()` 完全缺席；hand-written bind 里只剩 path/id query 与 `LoadPrimaryAsset(s)` / `UnloadPrimaryAsset(s)`，`LoadPrimaryAssetsWithType()` 和安全 singleton 入口还停在注释；`FPrimaryAssetId` 也只剩字符串构造，而且当前 lambda 首参类型仍写错。这样一来，脚本虽然“能查一点东西”，却拿不到完整的资源发现、类型安全构造与按 type 批量加载工作流。
  - 本项应把 `AssetManager` 重新收口成统一的脚本工作流 owner：脚本可以安全获取 singleton、按 type/id 枚举资产、按路径扫描、类型安全构造 `FPrimaryAssetId`，并在 batch/type load 路径上保持与 native 一致的 callback 与失败语义。否则资产 API 仍会停留在“查询、加载、ID 组三段各自半完成”的状态。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 21 / 24 / 41 / 61 / 67 / 68 / 69：`UAssetManagerMixinLibrary` 缺少目录扫描、过滤版枚举与 `GetPrimaryAssetObjectList()`，helper 脱离 `UAssetManager` 实例面，`LoadPrimaryAssetsWithType()` 与安全 singleton 入口被删，`FPrimaryAssetId` 仅剩字符串构造且实现首参类型错误。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-11 / NewTest-39：`UAssetManager` 当前只有 query / initial-scan dedicated 测试建议，null receiver、无效 callback 与资产工作流其它入口都没有回归保护。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一领域脚本支持若分散在 runtime helper 与 hand-written bind 两条 owner 上，维护时很容易漏掉另一条注册通路。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` L6-L50 — `ScriptMixin = "UAssetManager"` 仍被注释，当前只暴露 `GetPrimaryAssetData*` / `GetPrimaryAssetObject` / `GetPrimaryAssetIdForObject` / `GetPrimaryAssetIdList` 子集，没有 `ScanPathForPrimaryAssets()`、`GetPrimaryAssetObjectList()` 或 filter 形参。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L29-L33 — `FPrimaryAssetId` 仍只绑定 `const FString&` 构造，且 lambda 首参仍写成 `FPrimaryAssetType* Address`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L87-L110 — hand-written bind 只剩 path/id query 与 `LoadPrimaryAsset(s)` / `UnloadPrimaryAsset(s)`；`LoadPrimaryAssetsWithType()` 缺失，`IsInitialized()` / `GetIfInitialized()` 仍处于注释状态。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`
- [ ] **P7.3** 📦 Git 提交：`[FunctionLibraries] Feat: complete asset manager workflow helpers`
- [ ] **P7.3-T** 单元测试：覆盖 `UAssetManager` scan/filter/load/id workflow 的正负路径
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本通过安全 singleton 获取 `UAssetManager`，使用 `FPrimaryAssetType + FName` 构造 `FPrimaryAssetId`，验证 `GetPrimaryAssetIdList(filter)` / `GetPrimaryAssetObjectList()` / `LoadPrimaryAssetsWithType()` / `ScanPathForPrimaryAssets()` 与 native reference 一致。
    - 边界条件：空 scan path、空或不存在的 asset type、filter 命中 `0` 条、already-initialized singleton、以及没有实际资产命中的 type-load 都返回稳定空结果或一次性完成回调。
    - 错误路径：`null UAssetManager`、无效 `FPrimaryAssetId`、未初始化 singleton、无效 callback receiver 或错误 asset type 输入时，必须安全失败，不得 crash，也不得遗漏约定的失败回调或默认返回。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.AssetManagerWorkflowParity`、`Angelscript.TestModule.FunctionLibraries.AssetManagerPrimaryAssetIdConstruction`、`Angelscript.TestModule.FunctionLibraries.AssetManagerWorkflowNullAndInvalidInputs`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.3-T** 📦 Git 提交：`[FunctionLibraries] Test: add asset manager workflow coverage`

### 单元测试总览增补（Phase 7）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P7.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` | `FRotator` / `FTransform` 实例 helper、rotator overload、property contract compile negative | `P1` |
| `P7.2` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptMathSignatureContractTests.cpp` | `Math` / `FQuat` 的 `no_discard`、trivial 与 deterministic numeric helper | `P1` |
| `P7.3` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp` | `AssetManager` singleton、scan/filter/load、`FPrimaryAssetId` 构造与无效输入失败路径 | `P1` |

### 验收标准补充（Phase 7）

1. `FRotator` / `FRotator3f` / `FTransform` / `FTransform3f` 不再出现“工厂函数还在、实例 helper 掉线”的 API 断层；脚本可直接走实例语法完成方向、组合与 rotator-friendly transform workflow。
2. `Math` / `FQuat` 纯值 helper 的 `ScriptNoDiscard` / `ScriptTrivial` 契约重新一致化；脚本声明、编译诊断与实际绑定路径不再在同一命名空间内分裂。
3. `UAssetManager` 脚本工作流重新闭环：脚本既能安全获取 manager，又能完成 primary asset 扫描、过滤枚举、类型安全 ID 构造与按 type 批量加载，并且这条 workflow 有 dedicated 自动化保护。

### 风险与注意事项补充（Phase 7）

1. **恢复 `Rotator/Transform` 实例 helper 可能改变现有补全顺序与 overload 解析**
   - 某些脚本如果已显式写成 `FRotator::GetRightVector(Rotator)` 或手工 `Quaternion()` 中转，恢复实例面后补全列表与首选调用形态会变化。
   - 缓解：优先保持旧静态入口兼容一轮，只新增/恢复实例面，不在同一提交里强制移除旧名字。

2. **把 `ScriptNoDiscard` / `ScriptTrivial` 拉回后，部分旧脚本会从“静默通过”升级成编译期报错或更快调用路径**
   - 这是 contract 收紧，不是运行期功能新增；旧脚本若曾丢弃返回值，会第一次看到稳定诊断。
   - 缓解：测试里先固定 diagnostic 文本和函数声明，再在变更说明里明确这是脚本声明契约修复。

3. **`AssetManager` workflow 补齐会把更多项目配置差异暴露到自动化里**
   - `PrimaryAssetType`、scan path 与 callback 语义都依赖项目当前 `AssetManager` 配置；如果测试直接绑定真实项目资产，容易造成不稳定。
   - 缓解：优先使用 test-local asset type、synthetic receiver 和最小配置 fixture；把“无资产命中”与“配置存在时的 happy path”分成独立断言，避免测试既依赖资产内容又依赖接口契约。

---

## 深化（2026-04-09 01:34）

### Phase 8：Vector helper surface 与 bind-config 守卫补强

> `Phase 1-7` 已经覆盖了 `Math` 正确性、`Widget/World/Component` owner 恢复、以及 `AssetManager/Input` 等高频 workflow；本轮只补三类仍未写进计划的 FunctionLibraries 余项：真实 FunctionLibrary 生产入口在 bind-config 层仍无守卫、`FVector/FVector3f` 的 helper 实例面仍未收口、以及 UEAS2 曾提供但当前快照已缺失的任意平面 `GetSafeNormal2D` utility。

- [ ] **P8.1** 把真实 FunctionLibrary 生产入口纳入 bind-config guardrail，补齐 class-map 与 production metadata 断言
  - 当前 `GeneratedBlueprintCallableEntriesPopulateClassMaps` 与 `CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` 仍主要验证引擎原生 callable 或 synthetic coverage library，无法提前证明 `FunctionLibraries/` 下真正依赖 helper/mixin 注册链的生产入口已经挂到正确宿主类型，也无法证明 production metadata 没有继续漂移。
  - 承接 `P3.2` / `P3.3` / `P3.4` 的 owner 收口，本项要把三类代表性真实入口拉进 bind-config：`USceneComponent::GetRelativeLocation()`、`UWorld::GetStreamingLevels()`、`UWidget::GetRenderTransform()`，并同时补 `CreateWidget()` / `GetRenderTransform()` 这组 production world-context 断言，防止以后只修 synthetic fixture 而漏掉真实函数库。
  - 这不是重复 runtime scenario 测试，而是把“入口到底挂到谁身上、是否还保留正确 metadata”前移到绑定配置阶段。只有把这层守卫补上，`Arch-MS-40` 所说的多 owner 漂移才会在更早阶段暴露出来。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 9 / 11 / 13：`GetStreamingLevels`、`GetRelativeLocation`、`GetRenderTransform` 这类真实 FunctionLibrary helper 都出现过 owner / mixin 漂移。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-22 / Issue-23：现有 bind-config 用例只抽查引擎原生 callable 与 synthetic coverage library，没有覆盖真实 FunctionLibrary class-map 和 production metadata 漂移。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一领域 API 可能同时受 UHT stub、手写 bind、runtime helper 三条通路影响，owner 不唯一时很容易漏掉另一条注册链。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` L515-L551 — `GeneratedBlueprintCallableEntriesPopulateClassMaps` 目前只断言 `AActor::K2_DestroyActor`、`UGameplayStatics::GetPlayerController`、`UASClass::IsDeveloperOnly`，仍未抽查任何 FunctionLibrary 生产入口。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L7-L18 — `USceneComponent` 侧仍通过 FunctionLibrary 暴露 `GetRelativeLocation()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h` L6-L18 — `UWorld` 侧仍保留 `GetStreamingLevels()` helper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` L19-L23、L37-L40 — `CreateWidget()` 与 `GetRenderTransform()` 仍是 production FunctionLibrary 入口，且 `GetRenderTransform()` 仍带敏感的 world-context metadata。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`
- [ ] **P8.1** 📦 Git 提交：`[FunctionLibraries] Test: add production function library bind-config guards`
- [ ] **P8.1-T** 单元测试：给真实 FunctionLibrary 入口补 class-map 与 production metadata 守卫
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：
    - 正常路径：`USceneComponent::GetRelativeLocation()`、`UWorld::GetStreamingLevels()`、`UWidget::GetRenderTransform()` 都能在正确宿主类型的 `ClassFuncMaps` 中找到绑定后的 `FFuncEntry`；`CreateWidget()` 继续隐藏 `WorldContextObject` 且保留 world-context trait。
    - 边界条件：同一轮断言同时覆盖“仍是 FunctionLibrary owner 的 helper”和“恢复后重新折叠为 member surface 的 helper”；`GetRenderTransform()` 必须隐藏 `WorldContext` 参数但不再带 `asTRAIT_USES_WORLDCONTEXT`，与 `CreateWidget()` 的 required-world-context 行为形成对照。
    - 错误路径：错误宿主类型、源 library class 或不相关 class-map 上不得出现这些入口；任何找到的 `FFuncEntry` 都不能退化成未绑定占位实现。
  - 测试命名：`Angelscript.TestModule.Engine.BindConfig.FunctionLibraryClassMapCoverage`、`Angelscript.TestModule.Engine.BindConfig.FunctionLibraryWorldContextMetadata`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P8.1-T** 📦 Git 提交：`[FunctionLibraries] Test: lock real function library bind-config coverage`

- [ ] **P8.2** 收口 `FVector` / `FVector3f` helper 的唯一 owner，恢复任意平面向量 utility 的实例调用面
  - `P1.3` 已处理 `AngularDistance` / `ConstrainToPlane` / `MoveTowards` 的数值契约，但还没有解决更基础的 API surface 问题：`UAngelscriptFVectorMixinLibrary` / `UAngelscriptFVector3fMixinLibrary` 仍然只是 `UCLASS(Meta = ())` 的静态 helper class，`Size2D(UpDirection)`、`Dist2D(Other, UpDirection)`、`AngularDistance*()`、`ConstrainToPlane()`、`ConstrainToDirection()`、`ToColorString()`、`MoveTowards()` 虽然还在源码里，却没有重新收口成 `FVector` / `FVector3f` 的唯一实例入口。
  - 本项需要在“恢复 class-level `ScriptMixin`”与“迁到 hand-written member bind”之间选一个单一 owner；不能继续让 `Bind_FVector*.cpp` 只保留原生成员，而把脚本便利 helper 永久留在孤立的 static library surface。否则即便数值逻辑修正了，脚本作者仍会继续面对“native method 是 member，utility helper 却掉回 static wrapper”的断层。
  - 参考对比层面，这正是 `Hazelight_Analysis` 所说的 `ScriptMixin` 已从“普遍规则”退成“选择性保留”的具体样本；执行时应优先让 vector helper 和 `Rotator/Transform` 一样回到单一且可预测的实例 surface，而不是再造第三条 helper 通路。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 37：`FVector` / `FVector3f` 的高频扩展 helper 仍留在 FunctionLibrary 中，但实例方法绑定已整体掉线。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-33：`FVector` / `FVector3f` 的平面投影、二维距离与 `ToColorString()` 仍无 dedicated 行为级测试；`MathExtendedCompat` 也没有锁定这批 helper surface。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一领域 API 若同时分散在手写 bind 与 runtime helper，维护者很容易只修其中一条注册链。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “维度 D3 深化：当前插件的 class-level `ScriptMixin` 覆盖面已从普遍规则退成选择性保留，失去 metadata 的 wrapper 会退回 namespace / global static surface。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L338-L422 — `UAngelscriptFVectorMixinLibrary` 的 `ScriptMixin = "FVector"` 仍被注释，`Size2D(UpDirection)`、`Dist2D(Other, UpDirection)`、`AngularDistance*()`、`ConstrainToPlane()`、`ConstrainToDirection()`、`ToColorString()`、`MoveTowards()` 仍停留在静态 wrapper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L429-L503 — `UAngelscriptFVector3fMixinLibrary` 同样只剩 `UCLASS(Meta = ())`，对应 `FVector3f` helper 仍停留在静态 wrapper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp` L257-L262 — hand-written `FVector` 当前只补了原生 `VectorPlaneProject()` / `GetSafeNormal*()` 一类成员，没有把上面的 helper 迁回 member surface。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector3f.cpp` L248-L253 — `FVector3f` hand-written bind 同样只保留原生投影 / 安全法线成员，没有补回 `FunctionLibraries` 里的扩展 helper。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector3f.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
- [ ] **P8.2** 📦 Git 提交：`[FunctionLibraries] Fix: restore vector instance helper surface`
- [ ] **P8.2-T** 单元测试：锁定 `FVector` / `FVector3f` helper 的实例 surface 与单一 owner 契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本可直接以实例语法调用 `Vector.Size2D(UpDirection)`、`Vector.Dist2D(Other, UpDirection)`、`Vector.ConstrainToPlane(...)`、`Vector.ToColorString()`、`Vector.MoveTowards(...)` 以及 `FVector3f` 对应 helper，并与 native 复合表达式结果一致。
    - 边界条件：零向量、default-constructed `FVector` / `FVector3f`、非单位 `UpDirection`、float/double 交叉对照都给出稳定结果，且 `FVector` / `FVector3f` 两套 surface 保持对称。
    - 错误路径：同名 helper 不得继续同时以错误 host、library class 或第二套 static surface 暴露；若保留过渡 alias，也必须保证 type info / binding 只存在单一 authoritative owner，不产生重载歧义。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.VectorInstanceHelperSurface`、`Angelscript.TestModule.FunctionLibraries.Vector3fInstanceHelperSurface`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P8.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add vector instance helper coverage`

- [ ] **P8.3** 补回 `FVector.GetSafeNormal2D(UpDirection, Tolerance, ResultIfZero)`，恢复任意平面二维安全法线 utility
  - 即便 `P8.2` 把 vector helper surface 重新收口，当前 API 里仍少了一条 UEAS2 曾明确提供的高频 utility：按任意 `UpDirection` 先投影到平面，再求二维安全法线的 overload。现在脚本只能手工写 `VectorPlaneProject(...).GetSafeNormal(...)`，而 `Bind_FVector.cpp` 暴露的原生 `GetSafeNormal2D()` 仍固定按世界 XY 平面工作。
  - 这项工作应把“任意平面二维法线”重新定义成正式 helper，而不是让它继续隐身在脚本作者各自拼出来的复合表达式里。实现时要把 zero-vector、与 `UpDirection` 平行、以及接近容差边界的行为一次性钉死，避免未来出现“恢复了 API 名字，但又在边界输入下回退到 XY 语义或产出 `NaN`”的新漂移。
  - 由于当前分析产出只直接定位到 `FVector` 版本，本项先不凭空扩张到 `FVector3f`；若执行阶段发现参考源或现有 helper 已需要对称 float32 版本，再在 `P8.2` 的 vector owner 收口里一并决策。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 38：UEAS2 中按任意 `UpDirection` 取二维安全法线的 `FVector` helper 已被整个删除，当前只剩固定 XY 平面的原生 `GetSafeNormal2D()`。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-2 / NewTest-33：当前 `Math` 自动化只覆盖平面投影、二维距离等一部分向量 utility，仍没有 dedicated 覆盖去锁定任意平面二维法线这一类组合 helper。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “维度 D3 深化：当前插件的 class-level `ScriptMixin` richer helper surface 已出现选择性收缩，vector utility 属于应显式恢复而不是继续隐形缺失的范畴。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L347-L422 — 当前 `FVector` helper 区段已经保留 `Size2D(UpDirection)`、`Dist2D(Other, UpDirection)`、`ConstrainToPlane()` 等任意平面 utility，但没有对应的 `GetSafeNormal2D(UpDirection, ...)` overload。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp` L261-L262 — hand-written bind 目前只暴露原生 `GetSafeNormal()` 与固定 XY 平面的 `GetSafeNormal2D()`，确认任意平面版本仍不存在。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
- [ ] **P8.3** 📦 Git 提交：`[FunctionLibraries] Feat: restore vector up-direction safe-normal helper`
- [ ] **P8.3-T** 单元测试：固定任意平面 `GetSafeNormal2D` 的结果与边界契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本调用 `Vector.GetSafeNormal2D(UpDirection, Tolerance, ResultIfZero)` 后，结果与 `VectorPlaneProject(Vector, UpDirection).GetSafeNormal(Tolerance, ResultIfZero)` 的 native reference 一致。
    - 边界条件：零向量、与 `UpDirection` 平行的向量、自定义 `Tolerance`、自定义 `ResultIfZero` 都返回稳定且可预测的结果，不因 `UpDirection` 是否单位向量而退化成世界 XY 语义。
    - 错误路径：无效或近零 `UpDirection`、以及接近容差边界的输入不能返回 `NaN`、不能悄悄退回错误平面；若最终契约选择显式失败或安全返回默认值，测试必须把该行为固定下来。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.VectorSafeNormal2DWithUpDirection`、`Angelscript.TestModule.FunctionLibraries.VectorSafeNormal2DZeroContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P8.3-T** 📦 Git 提交：`[FunctionLibraries] Test: add arbitrary-plane safe-normal coverage`

### 单元测试总览增补（Phase 8）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P8.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` | 真实 FunctionLibrary class-map、production world-context metadata、错误 host 防回归 | `P1` |
| `P8.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` | `FVector` / `FVector3f` 实例 helper surface、single-owner contract、float/double parity | `P1` |
| `P8.3` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` | 任意平面 `GetSafeNormal2D`、zero/tolerance 契约、非 XY 平面结果 | `P2` |

### 验收标准补充（Phase 8）

1. `BindConfig` 套件不再只验证引擎原生 callable；至少能提前拦住 `FunctionLibraries/` 生产入口的 class-map 漂移与 production metadata 漂移。
2. `FVector` / `FVector3f` 不再出现“native method 是 member、FunctionLibrary utility 却掉回 static helper”的 API 断层；向量 utility 有且只有一条 authoritative owner。
3. `FVector.GetSafeNormal2D(UpDirection, Tolerance, ResultIfZero)` 重新成为正式脚本能力，并且其任意平面语义、zero-contract 与容差行为都有 dedicated 自动化保护。

### 风险与注意事项补充（Phase 8）

1. **把 vector utility 拉回实例 surface 可能改变旧脚本的补全顺序与调用习惯**
   - 某些脚本如果已手工写成 helper-class / static wrapper 形式，恢复 member surface 后可能第一次看到新的首选入口。
   - 缓解：优先保持一轮兼容别名或显式 deprecation，再用 type-info / class-map 测试防止双 owner 长期并存。

2. **任意平面 `GetSafeNormal2D` 的边界契约必须与现有投影 helper 一起定义**
   - 如果不先决定非单位 `UpDirection`、近零 `UpDirection`、零向量的统一语义，新 helper 很容易重新引入“API 恢复了，但边界行为不稳定”的回归。
   - 缓解：直接以 `VectorPlaneProject(...).GetSafeNormal(...)` 或等价明确公式作为 reference，并把 `Tolerance` / `ResultIfZero` 行为固定到测试里。

3. **bind-config 代表性守卫与前面 owner 收口条目存在顺序依赖**
   - 如果 `P3.2` / `P3.3` / `P3.4` 还没落地，过早写死最终 host/metadata 预期会让新守卫先对当前基线报红。
   - 缓解：先在计划执行顺序上把 `Phase 8` 放到相应 owner 修复之后，或在首轮测试中明确区分“当前基线断言”和“owner 收口后的目标断言”。

---

## 深化（2026-04-09 01:48）

本轮补充说明：

- 再次核验 `Documents/AutoPlans/DiscoveryPlans/FunctionLibraries_Plan.md` 仍不存在；以下新增条目继续以维度 A/C 为主交叉，在 owner 分裂明确处补充维度 D。
- 只补前面 `Phase 1-8` 尚未细化的尾项，不重写既有 `P1.1`、`P3.1`、`P4.4`、`P4.5` 的主体范围。

### Phase 9：剩余 contract 尾项与 deferred 风险收口

> `Phase 1-8` 已覆盖大部分 owner 恢复、运行期 crash、metadata 与主 workflow；本轮只补四类仍未被前文完整承接的尾项：soft reference 的严格类型/utility contract、`Script` 全局初始化 helper 的 engine-safe truth-source、`GameplayTagContainer` 的 bulk helper 语义，以及 `AActor` 组合变换 / quaternion overload 的专项回归。

- [ ] **P9.1** 在 `SoftReference` 上恢复严格 subtype/property contract，并补齐 editor/tooling utility surface
  - 承接 `P4.4` 已锁住的 async delegate/null path，本项只补当前仍悬空的另一半 contract：`MatchesProperty()` 现在完全不比较 `PropertyClass` / `MetaClass`，`BindSoftPtrBaseMethods()` 也仍缺 `GetUniqueID()` / `GetLongPackageFName()` / `ResetWeakPtr()`；同时 `TSoftClassPtr<T>` 只有 `LoadAsync()` 没有任何同步/editor-only load，整批构造器也还丢着 `no_discard`。继续放任这组问题存在，soft reference 会在“脚本值类型”“原生属性匹配”“编辑器工具链”三条路径上同时弱于 native。
  - 实施时把 soft reference contract 拆成两层收口：先在 `Bind_TSoftObjectPtr.cpp` 的 `FSoftObjectPtrType::MatchesProperty()` / `FSoftClassPtrType::MatchesProperty()` 恢复 subtype 守卫，让错误模板实参在 property-match 阶段就失败；再在 `BindSoftPtrBaseMethods()` 与 `TSoftClassPtr<T>` 绑定区补回 native 常用 path/cache utility、editor/tooling 同步加载入口，以及构造器 / 临时值构造的 `no_discard` 诊断，避免脚本继续退回更重的字符串路径 workflow 和 silent temporary misuse。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 75 / 77 / 78 / 79：soft reference 属性匹配擦掉 subtype；缺少 `GetUniqueID` / `GetLongPackageFName` / `ResetWeakPtr`；`TSoftClassPtr` 缺同步加载；构造器丢掉 `no_discard`。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-10 / Issue-17 / Issue-18：现有 soft reference 测试只覆盖 `Get()` / `EditorOnlyLoadSynchronous()` 等 smoke，pending path、soft-path class/object 状态迁移与更严格 contract 没有 dedicated 覆盖。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` L195-L200 — `FSoftObjectPtrType::MatchesProperty()` 仍只检查 `FSoftObjectProperty` 类型，不比较 `PropertyClass`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` L251-L256 — `FSoftClassPtrType::MatchesProperty()` 仍只检查 `FSoftClassProperty` 类型，不比较 `MetaClass`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` L373-L435 — `BindSoftPtrBaseMethods()` 当前只有 `ToSoftObjectPath()`、`ToString()`、`GetLongPackageName()`、`GetAssetName()`、`IsValid/IsPending/IsNull()`、`Reset()`，没有 `GetUniqueID()` / `GetLongPackageFName()` / `ResetWeakPtr()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` L530-L553、L556-L652 — 只有 `TSoftObjectPtr<T>` 暴露 `EditorOnlyLoadSynchronous()`；`TSoftClassPtr<T>` 仍只有 `Get()` 与 `LoadAsync()`，相关构造器后也没有任何 `SetPreviousBindNoDiscard(true)`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
- [ ] **P9.1** 📦 Git 提交：`[FunctionLibraries] Fix: restore soft reference subtype and utility contracts`
- [ ] **P9.1-T** 单元测试：覆盖 `SoftReference` 的 subtype/property、utility parity 与 pending/sync-load contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`TSoftObjectPtr<UTexture2D>` / `TSoftClassPtr<AActor>` 的正确 subtype property 仍可匹配；脚本可调用 `GetUniqueID()` / `GetLongPackageFName()` / `ResetWeakPtr()`，`TSoftClassPtr` 的 editor/tooling 同步加载结果与 native 一致。
    - 边界条件：从稳定 soft path 构造 pending object/class pointer，覆盖 `IsPending()`、加载前 `Get() == null`、加载后转为 valid 的状态迁移；`ResetWeakPtr()` 只清弱缓存、不破坏 soft path。
    - 错误路径：错误 subtype property、无效 class/object path、以及显式丢弃 soft reference 构造临时值时，必须得到稳定 compile/property-match 失败或安全空结果，不能继续把错配拖到更晚的运行期。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.SoftReferenceSubtypeContracts`、`Angelscript.TestModule.FunctionLibraries.SoftReferenceUtilityParity`、`Angelscript.TestModule.FunctionLibraries.SoftReferencePendingAndSyncLoad`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add soft reference subtype and utility coverage`

- [ ] **P9.2** 承接 `P4.5`，把 `Script::Get*GlobalVariableBeingInitialized()` 的 truth-source 收紧到当前 engine/module 上下文
  - `P4.5` 已经把 metadata 与真实 module name 纳入计划，但当时刻意延后了更底层的 truth-source 问题：`AngelscriptScriptLibrary.cpp` 目前仍直接读取 `asCModule::InitializingGlobalProperty` 这颗进程级 static，`GetNamespaceOfGlobalVariableBeingInitialized()` 也继续用空字符串同时承载“合法全局 namespace”与“无初始化上下文”两种状态。只要不再把这层 runtime truth-source 收口，多 engine scope / hot-reload /并行编译场景里仍可能读到 stale context，而现有测试也只能证明 happy path。
  - 实施时不扩大成新的公共 API，而是在 `AngelscriptScriptLibrary.cpp` 内收敛一条共享查询路径：先校验当前 Angelscript engine/module 上下文是否仍与被初始化的 global property 对齐，再读取 `asCModule::InitializingGlobalProperty`；并把 contract 固定成“合法全局 namespace 仍可返回空 namespace，但必须伴随非空 name/module；超出当前上下文则三者统一返回空字符串”。这样既不强行引入新脚本 surface，也能把 `P4.5` 当时明确 deferred 的 stale-state 风险补进计划。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 45 / 50：`Script::Get*GlobalVariableBeingInitialized` 直接读取第三方进程级 static 指针，且 namespace helper 用空字符串同时表示‘无变量正在初始化’与‘变量位于全局命名空间’。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-15：全局变量初始化 helper 目前只有单一 happy path 建议，缺少对非初始化时机、全局 namespace 与上下文切换后 stale-state 的 dedicated 覆盖。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` L9-L13 — `GetNameOfGlobalVariableBeingInitialized()` 仍直接读取 `asCModule::InitializingGlobalProperty` 并返回字符串。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` L16-L23 — `GetNamespaceOfGlobalVariableBeingInitialized()` 目前只要 `nameSpace == nullptr` 就返回空字符串，未区分“全局 namespace”与“无初始化上下文”。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` L26-L33 — `GetModuleNameOfGlobalVariableBeingInitialized()` 同样直接从同一颗进程级 static 取值，没有任何当前 engine/module guard。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp`
- [ ] **P9.2** 📦 Git 提交：`[FunctionLibraries] Fix: isolate script init context queries to active scope`
- [ ] **P9.2-T** 单元测试：给 `Script` 全局初始化 helper 增加 stale-state 与上下文切换回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：当前 engine/module 中带 namespace 的全局变量初始化表达式里，三条 helper 返回的变量名、namespace、模块名与声明一致。
    - 边界条件：全局 namespace 变量在初始化期仍允许返回空 namespace，但 `GetName...()` / `GetModuleName...()` 必须非空；退出初始化期后，三条 helper 一律回到空字符串。
    - 错误路径：切换到另一个 `FAngelscriptEngineScope`、或在没有当前初始化上下文的作用域再次调用时，helper 不得泄露上一次模块/变量信息，必须稳定返回空字符串而不是 stale data。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.GlobalInitContextScopeIsolation`、`Angelscript.TestModule.FunctionLibraries.GlobalInitContextGlobalNamespaceContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add script init scope-isolation coverage`

- [ ] **P9.3** 深化 `P1.1`，把 `GameplayTagContainer` 的 bulk helper 语义与 by-ref contract 单独锁死
  - `P1.1` 已经把 owner 收口、`AddLeafTag` 返回语义与 hierarchy/query 主干纳入计划，但它还没有单独承接 `GameplayTagContainer` 的 bulk helper 尾项：当前 wrapper 仍把 `RemoveTags()` 的第二个参数退化成 by-value，`Filter()` / `FilterExact()` / `GetGameplayTagParents()` 仍停在 static wrapper，而 `AppendTags()` / `Reset()` 又只存在于 hand-written bind。继续让这组 bulk 操作依赖单标签 smoke 或间接 query 覆盖，会把“参数拷贝”“精确过滤”“父标签展开”“批量移除”这些最容易悄悄漂移的 container 行为留成盲区。
  - 本项不重复讨论 `GameplayTag` / `GameplayTagQuery` 的 owner 主线，而是把 `GameplayTagContainer` 的 bulk mutation / bulk query 当成单独 contract：`RemoveTags` 应恢复 `const FGameplayTagContainer&`；`AddLeafTag`、`Filter/FilterExact`、`GetGameplayTagParents`、`AppendTags`、`Reset` 需要在同一组测试里和 native 对齐，防止 `P1.1` 落地后只修主路径、却把高频容器工作流继续留在薄弱地带。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 56 / 80 / 85：`RemoveTags` 被退化成 by-value；`GameplayTagContainerMixinLibrary` 不再反映完整真实 API 面；`AddLeafTag` 当前被 wrapper 裁成 `void`。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-9 / NewTest-18 / NewTest-19：现有测试把 exact/non-exact、filter、parents、批量移除与 query 成员方法压在单标签 happy path 之外，container bulk helper 仍无 dedicated 覆盖。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一领域 API 同时分散在 runtime helper 与 hand-written bind 时，很容易只修一条注册链。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` L32-L50 — `AddLeafTag()` 仍声明为 `void`，`RemoveTags()` 的 `TagsToRemove` 仍按值传递。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` L116-L134 — `GetGameplayTagParents()`、`Filter()`、`FilterExact()` 仍都停在 wrapper 内，没有独立行为保护。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp` L158-L165 — `AppendTags()` / `Reset()` 仍只在 hand-written bind 侧存在，`GameplayTagContainer` 实际 API 面仍是 split-owner。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
- [ ] **P9.3** 📦 Git 提交：`[FunctionLibraries] Fix: lock gameplay tag container bulk helper contracts`
- [ ] **P9.3-T** 单元测试：覆盖 `GameplayTagContainer` 的批量增删、过滤与 parent 展开契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
  - 测试场景：
    - 正常路径：对 parent/child/unrelated tag 组合执行 `AddLeafTag()`、`AppendTags()`、`Filter()`、`FilterExact()`、`GetGameplayTagParents()`、`RemoveTags()`，脚本结果与 native container 操作逐项一致。
    - 边界条件：空移除容器、重复 `AddLeafTag()`、对不存在 tag 的 `RemoveTags()`、以及 `Reset(0)` 后的空容器语义都保持稳定，不多删、不少删，也不残留脏 parents/filter 结果。
    - 错误路径：default-constructed tag、空 container、以及 bulk helper 的 split-owner/错误 host 不得继续产生重载歧义或误匹配；若 owner 收口后移除了旧 wrapper，错误入口必须给出稳定 compile failure。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.GameplayTagContainerBulkMutation`、`Angelscript.TestModule.FunctionLibraries.GameplayTagContainerFilterContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.3-T** 📦 Git 提交：`[FunctionLibraries] Test: add gameplay tag container bulk helper coverage`

- [ ] **P9.4** 深化 `P3.1`，给 `AActor` 组合变换与 quaternion overload 建立 mirror-style 行为回归
  - `P3.1` 已经覆盖了 transform round-trip、sweep、attach 与导出 owner，但当前 actor helper 仍有一大段完全没有 dedicated 行为保护的组合入口：`SetActorRelativeRotation(FQuat)`、`SetActorRotation(FQuat)`、`SetActorLocationAndRotation` 的 rotator/quat 双 overload、`SetActorTransform()`、`AddActorLocalTransform()`、`AddActorWorldTransform()` 以及相对 getter 的 rootless fallback。它们现在都还停在裸 `UFUNCTION()`，现有计划也没有把“同名 overload 是否命中正确脚本入口”“local/world delta 是否混空间”“quat/rotator 是否结果一致”单独钉住。
  - 本项不是重写 `P3.1`，而是把 actor 侧剩余的高频组合工作流补齐成 mirror-style 对照测试：让脚本 actor 与 native mirror actor 按相同步骤逐条执行 composite/quat API，逐步对比 location、rotator、quat 和完整 transform。只有这样，后续收口 actor helper surface 时，才不会继续把最容易悄悄漂移的 overload 与 delta 语义留在“能编译、但没有强行为断言”的状态。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 6 / 66：`UAngelscriptActorLibrary` 仍保留整组 transform helper，但大多数入口停留在裸 `UFUNCTION()`，真正自动导出的只剩已由 hand-written bind 覆盖的 getter。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-22 / NewTest-28：actor 目前只有基础 getter/setter、sweep、attach 建议，相对变换 fallback、local/world delta、组合 setter 与 quaternion overload 仍无 dedicated 覆盖。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L12-L65 — `GetActorRelativeLocation()` / `GetActorRelativeRotation()` / `GetActorRelativeTransform()` 仍通过 root component fallback 返回 `ZeroVector` / `ZeroRotator` / `Identity`，但当前没有专项测试锁这条语义。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L35-L40、L99-L118 — `SetActorRelativeRotation(FQuat)` 与 `SetActorRotation(FQuat)`、`SetActorLocationAndRotation(FQuat)` 仍是同名 overload 的裸 `UFUNCTION()` 包装。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L127-L194 — `SetActorQuat()`、`GetActorQuat()`、`AddActorLocalTransform()`、`AddActorWorldTransform()` 以及 world/local delta 入口仍全部存在，但还没有 dedicated 行为回归。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp`
- [ ] **P9.4** 📦 Git 提交：`[FunctionLibraries] Test: extend actor composite transform contracts`
- [ ] **P9.4-T** 单元测试：覆盖 `AActor` 的 quaternion overload、组合 setter 与 local/world delta 语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本 actor 与 native mirror actor 逐步执行 `SetActorRelativeRotation(FQuat)`、`SetActorRotation(FQuat)`、`SetActorLocationAndRotation` 的 rotator/quat 两个 overload、`SetActorTransform()`、`AddActorLocalTransform()`、`AddActorWorldTransform()`，最终 world transform 完全一致。
    - 边界条件：`bTeleport = false/true`、identity delta transform、以及无 root component actor 的 relative getter fallback 都保持稳定，quat 读回结果与 rotator 中转结果一致。
    - 错误路径：同名 overload 不能继续解析到错误脚本入口；若显式使用错误 property-style/host 调用形式，必须稳定 compile failure 或安全失败，不能静默落到另一条 overload。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.ActorCompositeTransformAndQuatOverloads`、`Angelscript.TestModule.FunctionLibraries.ActorRelativeFallbackContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.4-T** 📦 Git 提交：`[FunctionLibraries] Test: add actor composite transform coverage`

### 单元测试总览增补（Phase 9）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P9.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` | soft reference subtype/property、utility parity、pending/sync-load、`no_discard` / wrong subtype 负路径 | `P1` |
| `P9.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp` | 当前 scope 命中、全局 namespace 边界、stale-state 与跨 scope 泄漏防回归 | `P2` |
| `P9.3` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` | `AddLeafTag` / `AppendTags` / `Filter*` / `RemoveTags` / `Reset` / parent 展开 | `P1` |
| `P9.4` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` | actor quaternion overload、组合 transform setter、local/world delta、rootless fallback | `P1` |

### 验收标准补充（Phase 9）

1. `TSoftObjectPtr<T>` / `TSoftClassPtr<T>` 不再把 subtype 擦除到 property-match 阶段；脚本侧也能直接使用 native 常见的 path/cache utility，而不是被迫退回字符串路径工作流。
2. `Script::Get*GlobalVariableBeingInitialized()` 不再把进程级 static 当成无条件 truth-source；超出当前 engine/module 上下文时不会泄露 stale 变量或模块信息。
3. `GameplayTagContainer` 的 bulk helper 不再依赖单标签 smoke 侧面覆盖；`AddLeafTag`、`RemoveTags`、`Filter*`、`AppendTags`、`Reset` 与 parent 展开语义都有 dedicated 自动化保护。
4. `AActor` 的 quaternion overload、组合 setter 与 local/world delta helper 都有 mirror-style 行为回归，后续再收口 actor helper surface 时不会继续出现“签名还在、空间语义已经漂了”的假绿灯。

### 风险与注意事项补充（Phase 9）

1. **soft reference 的 subtype/property matching 一旦收紧，部分旧脚本或旧属性桥会从“晚失败”提前变成“早失败”**
   - 这属于 contract 变严，不是新功能回退。
   - 缓解：优先补 compile/property-match negative test，再在变更说明里明确这是把 runtime 错配前移到更早阶段。

2. **`TSoftClassPtr` 的同步加载入口要严格限制在 editor/tooling 语境，避免误把 gameplay hitch 风险重新暴露给脚本**
   - 如果直接照搬 object soft pointer 的同步 load 而不保留 editor-only 边界，会把旧的性能约束重新打穿。
   - 缓解：复用现有 `EditorOnlyLoadSynchronous` 约束思路，只为 `TSoftClassPtr` 补 editor/tooling 对等入口，不在同一轮开放 runtime 同步加载。

3. **`Script` 全局初始化 helper 的 stale-state 风险部分来自第三方 AngelScript 的进程级 static，完全 per-engine 化可能超出本轮范围**
   - 这意味着本轮更适合先实现“只在当前上下文可见、否则安全返回空”的最小 contract，而不是承诺一次性消灭所有跨 engine 污染。
   - 缓解：先把共享查询函数和 negative test 落地，再根据执行结果决定是否需要更深的 core-side 隔离设计。

4. **`GameplayTagContainer` 与 `AActor` 这两组 contract 收紧都可能影响旧脚本的 overload 解析与补全顺序**
   - `RemoveTags(const&)`、`AddLeafTag(bool)`、quat/rotator 同名 overload 一旦回到单一 authoritative contract，脚本侧可见签名会更明确，但也可能暴露之前被宽松解析掩盖的问题。
   - 缓解：优先保持一轮兼容别名或明确 compile diagnostic，再让新测试锁住“最终 authoritative 入口”而不是继续放任双义语法。

---

## 深化（2026-04-09 02:00）

本轮补充说明：

- `P3.1` 与 `P7.3` 已覆盖 `AActor` 的 transform / attach 主路径、以及 `UAssetManager` 的 scan / load / id workflow，但再次交叉核对五维输入与当前源码后，仍有两组高频 contract 没有被单独立项：`AActor` 的 script-friendly collection helper，以及 `UAssetManager` 的 safe-singleton / `no_discard` query guard。
- `Documents/AutoPlans/DiscoveryPlans/FunctionLibraries_Plan.md` 在 `2026-04-09 02:00` 仍不存在；以下条目继续以维度 A / C / D / E 交叉取证，并以当前源码为准补尾项，不重写前面已经成型的 `P3.1` / `P7.3` 主体。

### Phase 10：剩余 query surface 与 guard contract 收口

> 本轮只补两个执行面清晰、测试可落地、且不会与 `P3.1` / `P7.3` 重复的尾项：`AActor` 高层集合 helper，以及 `UAssetManager` 的 guard / query 诊断面。

- [ ] **P10.1** 深化 `P3.1`，恢复 `AActor` 的 script-friendly collection helper，而不是只剩 out-param `GetComponentsByClass()`
  - `P3.1` 已经覆盖 transform / sweep / attach / owner 收口，但 `AActor` 仍缺一组更贴近脚本日常工作流的高层集合 helper：`GetComponents()`、`GetAttachedActors()`、`GetAttachedActorsOfClass()`、`GetOverlappingActorsOfClass()`。当前脚本只能退回 `GetComponentsByClass(?& OutComponents)` 这种更底层的 out-param 枚举接口，既不对齐参考实现，也让 actor 侧 query surface 与 `ComponentLibrary` 已规划恢复的数组返回 helper 继续割裂。
  - 本项不重复处理 `P3.1` 的 transform/attach 主线，而是把 actor 侧“集合查询”定义成独立 contract：若最终决定继续挂在 `AActor` member surface，就应直接恢复这组高频 helper；若改为 hand-written bind，也必须保持返回数组、class-filter 与 null/error-path 语义一致，不能再让脚本作者自己循环 `GetComponentsByClass(?&)` 拼装高层查询。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “当前分支已丢失 `GetComponents()`、`GetAttachedActors()`、`GetAttachedActorsOfClass()`、`GetOverlappingActorsOfClass()` 这组 `AActor` 高层查询 helper，脚本只能退回更笨重的低层枚举接口。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一领域 API 同时分散在 runtime helper 与 hand-written bind 两条通路上时，很容易只修主路径、遗漏剩余 surface。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “当前插件的 wrapper-based member sugar 已改成按类型 opt-in，helper surface 出现选择性收缩；高频 richer helper 需要显式决定是否恢复为正式 surface。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L189-L228 — 文件在 `AttachToActor()`、`SetbRunConstructionScriptOnDrag()`、`RerunConstructionScripts()` 后直接结束，当前没有 `GetComponents()`、`GetAttachedActors()`、`GetAttachedActorsOfClass()`、`GetOverlappingActorsOfClass()` 声明。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L29-L37、L39-L149 — hand-written bind 目前只补了基础 trivial method 与两个 out-param `GetComponentsByClass()` overload，没有任何 actor-level attached / overlap / array-return collection helper。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp`
- [ ] **P10.1** 📦 Git 提交：`[FunctionLibraries] Feat: restore actor collection helper surface`
- [ ] **P10.1-T** 单元测试：覆盖 `AActor` 的 collection helper 与 class-filter query contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本对带多个 component、attached child actor 和 overlap target 的 actor 调用 `GetComponents()`、`GetAttachedActors()`、`GetAttachedActorsOfClass()`、`GetOverlappingActorsOfClass()`，结果与 native `AActor` 查询逐项一致。
    - 边界条件：无 component、无 attached actor、无 overlap、以及 class filter 命中 `0` 条时都返回稳定空数组，不回退到旧 out-param 语义，也不制造顺序漂移。
    - 错误路径：`null AActor`、`null` class filter、以及错误 host / 旧接口误用时，必须稳定抛脚本异常或 compile failure，不能继续靠访问越界或静默空结果掩盖问题。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.ActorCollectionHelpers`、`Angelscript.TestModule.FunctionLibraries.ActorAttachedAndOverlapQueries`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P10.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add actor collection helper coverage`

- [ ] **P10.2** 深化 `P7.3`，恢复 `UAssetManager` 的 safe-singleton / `no_discard` query contract，而不是只补 workflow helper
  - `P7.3` 已承接 scan/filter/load/id workflow，但 `UAssetManager` 仍有一层更基础的 guard/query contract 处于裸奔状态：`GetPrimaryAssetIdForObject()` 丢掉了 `ScriptNoDiscard`，脚本可以无提示忽略返回的 `FPrimaryAssetId`；`UAssetManager::IsInitialized()` / `Get()` 这组安全 singleton helper 仍整行注释，脚本侧没有 guard-style 入口判断 manager 是否已就绪。这不是“缺几个便利函数”而已，而是 query 诊断面和生命周期防御面一起变薄。
  - 本项不重复 `P7.3` 的扫描、批量加载与类型安全构造主线，只把“query 结果必须消费”和“singleton 必须可安全探测”收成单独 contract：`GetPrimaryAssetIdForObject()` 要重新回到 `no_discard` 语义；`UAssetManager::IsInitialized()` / `Get()` 或等价 safe entrypoint 必须恢复，并在未初始化环境下返回稳定 guard 结果，而不是逼脚本假定全局 singleton 永远可用。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 65 / 68：`GetPrimaryAssetIdForObject` 丢失 `ScriptNoDiscard`；`UAssetManager::IsInitialized()` / `Get()` 安全 singleton helper 被整体注释掉。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-11：`UAssetManager` 查询与 initial-scan 回调当前完全无 dedicated coverage，guard/query 语义没有任何自动化保护。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一领域脚本支持若分散在 runtime helper 与 hand-written bind 两条 owner 上，维护时很容易漏掉另一条 contract。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` L37-L42 — `GetPrimaryAssetIdForObject()` 当前只有 `UFUNCTION(BlueprintCallable)`，没有任何 `ScriptNoDiscard` 元数据。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L107-L110 — `UAssetManager` namespace 下的 `IsInitialized()` / `Get()` 仍整行注释，safe singleton 入口没有替代实现。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` L325-L329 — 最终脚本声明只有在函数保留 `ScriptNoDiscard` 元数据时才会追加 `no_discard`，说明当前 query contract 的缺口会直接反映到脚本签名。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
- [ ] **P10.2** 📦 Git 提交：`[FunctionLibraries] Fix: restore asset manager guard and query contracts`
- [ ] **P10.2-T** 单元测试：锁定 `UAssetManager` 的 singleton guard 与 query `no_discard` 契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
  - 测试场景：
    - 正常路径：manager 已初始化时，脚本可通过 `UAssetManager::IsInitialized()` / `Get()` 或最终 authoritative safe entrypoint 取得实例；`GetPrimaryAssetIdForObject()` 返回值与 native 一致，且脚本签名保留 `no_discard`。
    - 边界条件：无效 object、invalid `FPrimaryAssetId`、以及测试环境中 manager 尚未可用的 guard 路径都返回稳定的 invalid/null 结果，不误报成功，也不要求脚本提前持有外部注入实例。
    - 错误路径：显式丢弃 `GetPrimaryAssetIdForObject()` 返回值、或在 unavailable context 下直接取 singleton 时，必须得到稳定 compile diagnostic / metadata 断言或安全失败结果，不能继续静默通过或 crash。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.AssetManagerSingletonGuards`、`Angelscript.TestModule.FunctionLibraries.AssetManagerQueryNoDiscardContracts`、`Angelscript.TestModule.Engine.BindConfig.AssetManagerFunctionLibraryMetadata`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P10.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add asset manager guard and query coverage`

### 单元测试总览增补（Phase 10）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P10.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` | actor collection helper、attached/overlap/class-filter query、null/error-path | `P1` |
| `P10.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp` | `UAssetManager` safe singleton、query `no_discard`、metadata/compile diagnostic | `P1` |

### 验收标准补充（Phase 10）

1. `AActor` 不再只剩 transform / attach helper；脚本可直接使用 collection-style query helper，并且 attached / overlap / class-filter 语义有 dedicated 自动化保护。
2. `UAssetManager` 不再要求脚本无条件假定 singleton 已初始化；safe singleton 入口与 `GetPrimaryAssetIdForObject()` 的 `no_discard` query contract 都重新成为正式脚本约束。

### 风险与注意事项补充（Phase 10）

1. **恢复 `AActor` collection helper 可能与现有 `GetComponentsByClass(?&)` 形成短期双轨**
   - 如果直接新增数组返回 helper 而不定义 authority，旧脚本与新脚本可能在补全顺序和错误提示上继续分裂。
   - 缓解：优先把新 helper 定义为 authoritative surface，并明确旧 out-param 入口是兼容层还是继续保留的低层 API。

2. **把 `UAssetManager` guard/query contract 收紧后，旧脚本可能从“静默通过”升级成编译或 metadata 断言失败**
   - 这类失败通常暴露的是本来就存在的误用：丢弃 query 结果、假设 singleton 总可用、或把 guard helper 当成普通副作用函数。
   - 缓解：先补 metadata / compile negative test，再让运行期 helper 返回稳定 null/invalid 结果，避免 contract 收紧和运行期行为变化一起发生。

---

## 深化（2026-04-09 02:07）

本轮补充说明：

- `P3.2` 已经覆盖 `UWorld` / `ULevelStreaming` 的运行期 contract，但还没有把 `GetShouldBeVisibleInEditor()` 的双源注册单独收口成 single-owner 任务；该问题在 `FunctionLibraries_Analysis` 与 `TestGaps` 里仍有独立证据。
- `P1.2` / `P3.3` 已经覆盖 `Input` / `Widget` helper 的行为与 metadata，但 `ModuleStructure_ArchReview` 额外指出这两组 FunctionLibrary 头文件本身还在把 `InputCore` / `UMG` 作为 accidental public contract 暴露；当前 Plan 尚未把这个架构尾项单独立项。
- `Documents/AutoPlans/DiscoveryPlans/FunctionLibraries_Plan.md` 在 `2026-04-09 02:07` 复核时仍不存在，以下新增条目继续只用 A / C / D 交叉取证，并以当前源码为准补尾项。

### Phase 11：single-owner 收口与 public-header contract 尾项

> 目标：补两类仍未被前文单独承接的结构尾项：`ULevelStreaming` helper 的双源 owner，以及 `FunctionLibraries` 公共头对 `InputCore` / `UMG` 的 accidental public contract。

- [ ] **P11.1** 深化 `P3.2`，把 `ULevelStreaming::GetShouldBeVisibleInEditor()` 从“双源注册”收口成单一 authoritative owner
  - `P3.2` 已经处理 `UWorld.GetStreamingLevels()` 的失联与 world/level null guard，但 `ULevelStreaming` 这半边仍然没有单独收口 owner：`UAngelscriptLevelStreamingLibrary` 继续通过 `ScriptMixin = "ULevelStreaming"` 提供 `BlueprintCallable` wrapper，而 `Bind_FunctionLibraryMixins.cpp` 又对同名同签名方法手写注册了一次。继续保留这两条并行来源，会让后续修正 editor-only 语义、class-map 抽查或 host 归属时始终分不清谁才是 authority。
  - 本项不重复 `P3.2` 的运行期 true/false / null contract，而是专门要求把 `GetShouldBeVisibleInEditor()` 压成一个 source-of-truth：要么保留 FunctionLibrary 自动收集并删除手写 bind，要么反过来保留手写 bind 并把 FunctionLibrary 降为薄转发或兼容层，但不能继续同时活着。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 31：`ULevelStreaming::GetShouldBeVisibleInEditor()` 同时走自动 FunctionLibrary 路径和 `Bind_FunctionLibraryMixins.cpp` 手写注册，形成双源 owner。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-3 / NewTest-3 / Issue-22：现有 `LevelStreamingCompile` 只断言方法存在，没有验证 true/false 返回值；bind-config 也没有抽查真实 FunctionLibrary 的 class-map 入口。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 UE 模块被 UHT function-table 与 hand-written bind 双轨共同承接时，owner 不显式会放大维护成本。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h` L6-L18 — `UAngelscriptLevelStreamingLibrary` 仍以 `ScriptMixin = "ULevelStreaming"` 暴露 `GetShouldBeVisibleInEditor()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp` L9-L12 — 同名 `GetShouldBeVisibleInEditor() const` 仍被再次手写注册到 `ULevelStreaming`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
- [ ] **P11.1** 📦 Git 提交：`[FunctionLibraries] Refactor: dedupe level streaming helper ownership`
- [ ] **P11.1-T** 单元测试：锁定 `ULevelStreaming` helper 的 single-owner 与 editor-visible contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：
    - 正常路径：脚本对 editor-visible 为 `true/false` 的 `ULevelStreaming` 实例调用 `GetShouldBeVisibleInEditor()`，结果与 native 直接调用一致；`ClassFuncMaps` 中该入口只挂在 `ULevelStreaming` 的最终 authoritative owner 上。
    - 边界条件：空 streaming-level 列表、重复模块初始化/重复 bind 注册，以及 `WITH_EDITOR` gating 下的可见/不可见两条路径都保持稳定，不再出现“双源都在但行为一致只是碰巧”的假绿灯。
    - 错误路径：`null ULevelStreaming`、错误 host type、或再次引入重复 owner 时，测试必须通过运行期异常 / metadata 断言 / class-map 检查稳定报错，而不是继续静默通过。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.LevelStreamingVisibilityContracts`、`Angelscript.TestModule.Engine.BindConfig.LevelStreamingFunctionLibraryOwner`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P11.1-T** 📦 Git 提交：`[FunctionLibraries] Test: lock level streaming single-owner contract`

- [ ] **P11.2** 补齐 `FunctionLibraries` public-header contract，避免 `Input` / `Widget` helper 继续依赖 accidental public deps
  - 当前 `P1.2` 与 `P3.3` 已经处理了 `Input` / `Widget` helper 的行为、metadata 与 owner 漂移，但 `ModuleStructure_ArchReview` 额外指出了另一层更底的结构问题：`InputComponentScriptMixinLibrary.h` 和 `WidgetBlueprintStatics.h` 仍位于 runtime 可见头路径下，并直接 include `InputComponent` / `PlayerController` / `PlayerInput` / `WidgetBlueprintLibrary` 等模块头；与此同时 `AngelscriptRuntime.Build.cs` 仍把 `InputCore`、`UMG` 放在 `PrivateDependencyModuleNames`。这意味着这些 helper header 当前处于“外部能 accidental include，但声明依赖并不匹配”的状态。
  - 本项不展开整个 runtime 公共依赖重构，只收口这两个 FunctionLibrary 热点：要么把它们变成明确的 public facade 并同步提升所需 public deps，要么迁到 private/leaf module 并用更窄的 bridge header 承接脚本注册。不能继续一边在 Plan 里修行为契约，一边让 C++ public contract 仍然靠宽 `PublicIncludePaths` 漏出。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 2 / 13 / 15：`InputComponentScriptMixinLibrary.h`、`WidgetBlueprintStatics.h` 仍是 active owner 漂移热点，helper surface 尚未稳定，不适合继续作为 accidental public contract 裸露。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-22 / Issue-23 / Issue-24：现有 bind-config 只抽查 synthetic 或引擎原生 callable，真实 FunctionLibrary production entry 和 metadata 漂移仍缺专用 contract-check，且 FunctionLibrary 断言不应继续埋在 megafile 中。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime public deps 不含 `UMG` / `InputCore`，但 `FunctionLibraries/InputComponentScriptMixinLibrary.h`、`FunctionLibraries/WidgetBlueprintStatics.h` 已经位于 public 可 include 面；第一批优先处理对象就包含这两个 header，并建议用 dummy consumer 验证真实 public contract。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` L2-L4、L12-L24 — public FunctionLibrary header 仍直接 include `Components/Widget.h` 与 `Blueprint/WidgetBlueprintLibrary.h`，并暴露 `CreateWidget()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` L2-L6、L12-L25 — public FunctionLibrary header 仍直接 include `InputComponent` / `PlayerController` / `PlayerInput` 相关头，并暴露 `BindAction()` 等 classic input helper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` L45-L56 — `InputCore`、`UMG` 仍只在 `PrivateDependencyModuleNames`，说明当前 public header 可见面和模块依赖声明仍不对齐。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryHeaderContractTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
- [ ] **P11.2** 📦 Git 提交：`[FunctionLibraries] Refactor: align function-library header contract and module deps`
- [ ] **P11.2-T** 单元测试：为 `FunctionLibraries` public-header boundary 增加 contract-check，而不是继续依赖 accidental include
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryHeaderContractTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
  - 测试场景：
    - 正常路径：最小 contract-check / dummy consumer 只依赖 `AngelscriptRuntime` 时，能够编译保留下来的 public facade；脚本侧 `WidgetBlueprint::CreateWidget` 与 classic input helper 经过 header 收口后仍能生成正确 metadata / host entry。
    - 边界条件：game target 与 editor target 都要验证同一组 FunctionLibrary public header；若这两组 helper 最终被拆到 leaf module，则测试需同时固定“runtime 默认 surface”和“editor / feature 扩展 surface”的合法 include 方式。
    - 错误路径：直接 include 不再允许公开的 internal helper 头、或再次让 `UMG` / `InputCore` 依赖通过 accidental public path 泄漏时，contract-check 必须稳定失败；production metadata 若挂到错误 host 或残留旧 hidden trait，也必须被专用断言拦截。
  - 测试命名：`Angelscript.TestModule.Engine.PublicHeader.FunctionLibraryBoundary`、`Angelscript.TestModule.Engine.BindConfig.FunctionLibraryProductionMetadata`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P11.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add function-library header contract coverage`

### 单元测试总览增补（Phase 11）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P11.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` | `ULevelStreaming` true/false 运行期语义、重复 owner 防回归、class-map authoritative entry | `P2` |
| `P11.2` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryHeaderContractTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp` | public-header boundary、dummy consumer contract、production metadata / host entry | `P2` |

### 验收标准补充（Phase 11）

1. `ULevelStreaming::GetShouldBeVisibleInEditor()` 只保留一个 authoritative 注册来源，运行期 true/false 语义与 class-map host 都有 dedicated 自动化保护。
2. `InputComponentScriptMixinLibrary.h` / `WidgetBlueprintStatics.h` 不再依赖 accidental public include 才能被外部消费；它们的 public / private 落点、模块依赖声明与 production metadata 至少在一个 contract-check 中被固定。

### 风险与注意事项补充（Phase 11）

1. **收紧 `ULevelStreaming` owner 可能暴露 editor-only surface 的历史兼容差异**
   - 旧脚本如果碰巧依赖了重复注册带来的补全 / 排序行为，single-owner 化后可能出现方法来源变化。
   - 缓解：先用 class-map / metadata 测试锁定最终 authoritative entry，再决定是否保留兼容 alias。

2. **FunctionLibrary public-header boundary 收口可能触发下游 C++ consumer 编译失败**
   - 这类失败通常不是新回归，而是 accidental public deps 被正式清理后暴露出的真实依赖。
   - 缓解：先补 dummy consumer / contract-check，再分阶段决定“提升 public deps”还是“迁入 private / leaf module”，避免一次性把行为修复和模块边界重排绑在一起。

---

## 深化（2026-04-09 06:34）

本轮补充说明：

- `P1.3` 已覆盖 `WrapIndexUInt()` 的可见性与一般数学边界，`P7.2` 已覆盖 `Wrap*()` / `WrapIndex*()` 的 `ScriptNoDiscard` / `ScriptTrivial` 契约，但再次交叉核对分析输入与当前源码后，unsigned wrap family 仍有一块没有被单独立项的 correctness 缺口：`Math::Wrap(uint32)` 整体失联，而 `WrapIndexUInt()` 在 `Value < Min` 时依然给出错误结果。
- `Documents/Plans/` 当前未见专门承接 `Math::Wrap(uint32)` / `Math::WrapIndex(uint32)` 运行期语义的活跃 Plan；以下只补这一条与现有 `P1.3` / `P7.2` 不重复的尾项，避免把“可见性修复”“metadata 契约修复”和“unsigned 算法修复”继续混成一个笼统 math 问题。

### Phase 12：unsigned wrap family 的正确性与对称 API 面

> 目标：把 `Math::Wrap(uint32)` / `Math::WrapIndex(uint32)` 从“一个失联、一个算错”的半完成状态收口成稳定的 unsigned helper contract，并补上 dedicated 回归测试。

- [ ] **P12.1** 深化 `P1.3` / `P7.2`，恢复 `Math::Wrap(uint32)` 并修正 `WrapIndexUInt()` 的 unsigned 算法，而不是只补导出与 metadata
  - 当前 unsigned wrap 家族的问题已经不是单点注解遗漏，而是 API 面和运行期语义同时断裂：`WrapUInt()` 从声明到实现整段被注释掉，`WrapIndexUInt()` 即使未来恢复完整 metadata / callable flag，也仍会在 `Value < Min` 时走错结果。继续只把它们归到“Math helper 可见性”里，会让 unsigned 调用面和真实返回值长期处于假修复状态。
  - 本项不重复 `P1.3` 的 `AngularDistance` / plane project 主线，也不重复 `P7.2` 的 `ScriptNoDiscard` / `ScriptTrivial` 契约主线，而是只收口 unsigned wrapping 自身：恢复 `Math::Wrap(uint32, uint32, uint32)` 的正式脚本入口；把 `WrapIndexUInt()` 改成与注释承诺一致的 `[>= Min, < Max)` 区间语义；同时锁定 `Min == Max`、`Min > Max`、`Value < Min`、`Value >= Max` 这四类边界，确保 unsigned 路径不再偷偷退回 signed 假设。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 26 / 71：`Math::Wrap(uint32)` 整段被注释掉，`WrapIndexUInt()` 把 signed 负余数逻辑直接复制到 `uint32`，导致 `Value < Min` 时返回错误索引。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-2：现有 `MathBoundaries` 建议虽然覆盖了 `WrapIndexUInt()`，但当前仓库仍没有任何 dedicated 测试去固定 unsigned wrap overload 的可见性、边界值与错误结果。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L245-L284 — `WrapUInt()` 当前从 `UFUNCTION` 到函数体全部仍是注释状态，确认 unsigned `Math::Wrap` 入口在当前源码里仍未正式暴露。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L319-L333 — `WrapIndexUInt()` 仍使用 `uint32 ModValue = (Value - Min) % Range; if (ModValue >= 0)`；对 `uint32` 来说该分支恒为真，说明 `Value < Min` 的“wrap 到 `Max` 以下”语义在当前实现中确实还未成立。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
- [ ] **P12.1** 📦 Git 提交：`[FunctionLibraries] Fix: restore unsigned wrap parity and correctness`
- [ ] **P12.1-T** 单元测试：锁定 unsigned wrap overload 的可见性、边界值与 `Value < Min` 纠偏语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本可直接调用 `Math::Wrap(uint(12), uint(10), uint(20))`、`Math::Wrap(uint(25), uint(10), uint(20))`、`Math::WrapIndex(uint(21), uint(10), uint(20))`，结果分别与 native unsigned reference 一致，证明 `uint32` overload 已重新成为正式 surface。
    - 边界条件：`Math::WrapIndex(uint(8), uint(10), uint(20))` 必须返回 `18` 而不是当前错误的中段值；`Min == Max`、`Min > Max`、以及恰好命中 `Min` / `Max` 的 inclusive 或 exclusive 边界都要被固定，避免后续再次把 signed 逻辑机械复制到 unsigned 路径。
    - 错误路径：显式传入大于 `INT32_MAX` 的 `uint32` 输入时，不得静默解析到 `int32` overload；若脚本误用旧 cast/workaround，测试应能稳定暴露 overload 选择或结果漂移，而不是继续让错误落在隐式窄化里。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.MathUnsignedWrapParity`、`Angelscript.TestModule.FunctionLibraries.MathUnsignedWrapIndexBoundaries`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P12.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add unsigned wrap regression coverage`

### 单元测试总览增补（Phase 12）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P12.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` | unsigned `Wrap` overload、`WrapIndexUInt` 的 `Value < Min` 边界、`Min == Max` / `Min > Max`、大值 overload 选择 | `P1` |

### 验收标准补充（Phase 12）

1. `Math::Wrap(uint32)` 不再是注释中的半删除 API，而是正式、可测试、与 `int32` 版并列存在的脚本 helper。
2. `WrapIndexUInt()` 在 `Value < Min` 时不再返回当前错误的中段索引；unsigned wrapping 的区间语义与头文件注释、测试期望和 native reference 保持一致。

### 风险与注意事项补充（Phase 12）

1. **恢复 unsigned overload 可能改变现有脚本的 overload 解析与自动补全排序**
   - 一些旧脚本可能依赖显式 cast 到 `int32` 或自写 helper 绕开当前缺口；unsigned overload 回来后，这些 workaround 可能开始与正式 API 竞争。
   - 缓解：优先用“大于 `INT32_MAX` 的 `uint32` 输入”补负向测试，确保真正走到 unsigned surface；必要时在迁移说明中标注“旧 cast-based workaround 可删除”。

2. **修正 `WrapIndexUInt()` 的错误结果会改变部分历史脚本的运行期输出**
   - 这不是新回归，而是把当前已确认的错误行为纠正回注释承诺的语义；但如果下游脚本曾围绕错误结果做补偿，修复后可能首次暴露差异。
   - 缓解：先补 dedicated 行为测试，再以 `Value < Min` 的代表性样例（如 `8,10,20 -> 18`）在执行说明中明确告知行为变化，避免后续把预期变更误判成回归。

---

## 深化（2026-04-09 06:41）

本轮补充说明：

- 复核 `Documents/Plans/Plan_TestCoverageExpansion.md`、`Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 与现有 `Plan_FunctionLibraries.md` 后，未见专门承接“剩余 FunctionLibrary 生产 bind-config 断言彻底迁出 megafile”与“`RuntimeFloatCurve` double/out-ref query contract 独立落地”的活跃条目；前者不等同于 generic bind startup/config 观察，后者也不等同于已有 `P3.5` 的 `SmartAuto` / invalid-handle 主线。
- `P3.5` 已经覆盖 `RuntimeFloatCurve` 的 owner、`SmartAuto` 与 invalid-key 语义，`P8.1` / `P11.1` 已经开始给真实 FunctionLibrary 入口补 bind-config guard；但当前源码仍把最后一批生产断言集中在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，同时 `GetTimeRange_Double()` / `GetValueRange_Double()` 仍没有 dedicated runtime/direct-bind 守卫。

### Phase 13：`RuntimeFloatCurve` double parity 与 FunctionLibrary bind-config megafile 收口

> 目标：把剩余 FunctionLibrary 生产 bind-config 守卫从 megafile 拆到单职责测试文件，并把 `RuntimeFloatCurve` 的 double/out-ref 查询契约补成独立执行单元，避免继续依赖“helper class 存在即可”的弱保护。

- [ ] **P13.1** 深化 `P8.1` / `P11.1`，把剩余 FunctionLibrary 生产 bind-config 断言完整迁出 `AngelscriptBindConfigTests.cpp`
  - 现有计划已经开始引入 `AngelscriptBindConfigFunctionLibraryMetadataTests.cpp` / `AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp` 这类专用文件名，但当前仓库 `Plugins/Angelscript/Source/AngelscriptTest/Core/` 实际仍只有 `AngelscriptBindConfigTests.cpp` 一份 `BindConfig` 测试文件。只要代表性 FunctionLibrary 断言还混在 generic startup/config、synthetic coverage library 和 inline direct-bind smoke 里，`Issue-24` 的 anti-pattern 就不会真正收口，后续回归仍会继续埋在大文件里。
  - 本项不重复 `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 已承接的 startup-path、disabled-bind、execution-order 观测能力；这里只处理 FunctionLibraries 自己的 production surface。执行时把 `USceneComponent::GetRelativeLocation()`、`UWorld::GetStreamingLevels()`、`ULevelStreaming::GetShouldBeVisibleInEditor()`、`UWidget::GetRenderTransform()` / `CreateWidget()` 和 `RuntimeFloatCurve` inline direct-bind 这批断言从 generic bind-config megafile 迁到 dedicated 文件，让原 megafile 回到框架级职责。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 9 / 13 / 16 / 40：`GetStreamingLevels`、`GetRenderTransform`、`RuntimeFloatCurve` 这类真实 FunctionLibrary 入口仍依赖 production owner，而 helper class / synthetic fixture 并不能代表目标类型 surface。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-22 / Issue-23 / Issue-24：`GeneratedBlueprintCallableEntriesPopulateClassMaps` 只抽查原生 callable，`CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` 只测 synthetic fixture，`AngelscriptBindConfigTests.cpp` 已膨胀成 700+ 行 megafile。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 UE 模块的脚本支持分散在 UHT function-table、hand-written bind 与 runtime helper 三条管线里，owner 不显式时就需要更贴近 production surface 的守卫。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` L493-L552 — `GeneratedBlueprintCallableEntriesPopulateClassMaps` 目前仍只验证 `AActor::K2_DestroyActor`、`UGameplayStatics::GetPlayerController`、`UASClass::IsDeveloperOnly`，没有任何真实 FunctionLibrary host coverage。
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` L647-L703 — `CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` 仍只使用 `UAngelscriptUhtCoverageTestLibrary` synthetic fixture，没有碰到 `GetRenderTransform()` 这类 production metadata。
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` L773-L847 — `RuntimeFloatCurve` 目前仍只有 `URuntimeFloatCurveMixinLibrary::StaticClass()` 上的 `GetNumKeys` / `GetTimeRange` direct-bind smoke，没有覆盖目标类型 surface 与 double query。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L7-L19 — `GetRelativeLocation()` 仍是 `USceneComponent` 的 production FunctionLibrary 入口。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h` L6-L19 — `GetStreamingLevels()` 仍是 `UWorld` 的 production helper。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h` L6-L18 — `GetShouldBeVisibleInEditor()` 仍是 `ULevelStreaming` 的 active helper 入口。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h` L19-L40 — `CreateWidget()` / `GetRenderTransform()` 仍是 production world-context / metadata 漂移样本。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`
- [ ] **P13.1** 📦 Git 提交：`[FunctionLibraries] Test: retire remaining function-library bind-config megafile coverage`
- [ ] **P13.1-T** 单元测试：把剩余 FunctionLibrary production bind-config 守卫拆到 dedicated metadata/direct-bind 文件
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp`
  - 测试场景：
    - 正常路径：`USceneComponent::GetRelativeLocation()`、`UWorld::GetStreamingLevels()`、`ULevelStreaming::GetShouldBeVisibleInEditor()` 都在正确 host type 的 `ClassFuncMaps` 上出现；`CreateWidget()` 与 `GetRenderTransform()` 的 production `FAngelscriptFunctionSignature` 分别保留“required world-context”与“hidden argument but no trait”的真实契约。
    - 边界条件：`WITH_EDITOR` 下 `ULevelStreaming` helper 的 host/class-map 检查、callable-without-world-context 与 required-world-context 对照、以及 `RuntimeFloatCurve` inline helper 的 helper-class entry 与 target-type usage 同时被固定，避免再退回 synthetic fixture。
    - 错误路径：helper class entry 存在但 target host entry 缺失、`GetRenderTransform()` 再次带回 `asTRAIT_USES_WORLDCONTEXT`、或 `RuntimeFloatCurve` direct-bind 只剩 helper-class smoke 时，专用断言必须明确失败，而不是继续被 generic bind-config 汇总文件掩盖。
  - 测试命名：`Angelscript.TestModule.Engine.BindConfig.FunctionLibraryProductionOwners`、`Angelscript.TestModule.Engine.BindConfig.FunctionLibraryProductionWorldContext`、`Angelscript.TestModule.Engine.BindConfig.FunctionLibraryDirectBind.RuntimeFloatCurveQueries`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P13.1-T** 📦 Git 提交：`[FunctionLibraries] Test: split production bind-config coverage into dedicated function-library files`

- [ ] **P13.2** 深化 `P3.5`，把 `RuntimeFloatCurve` 的 `GetTimeRange_Double()` / `GetValueRange_Double()` 与 `UCurveFloat` query parity 升级成独立执行单元
  - `P3.5` 已经承接了 `ScriptMixin` owner、`AddSmartAutoCurveKey()` 与 invalid-key-handle 主线，但当前真正悬空的还有一块更细的 query contract：`RuntimeFloatCurveMixinLibrary.h` 明确保留了 `GetTimeRange()` / `GetValueRange()` 与 `GetTimeRange_Double()` / `GetValueRange_Double()` 四个重载，且 double 版是“先读 float 再 widen”的显式适配；然而当前 bind-config smoke 只盯住 `GetNumKeys()` 与 `GetTimeRange()`，没有任何 dedicated runtime/direct-bind 断言去固定 `GetValueRange*()`、double out-ref 顺序，或 `UCurveFloat` / `FRuntimeFloatCurve` 之间的查询一致性。
  - 本项不重复 `P3.5` 已经列出的 `SmartAuto` / weighted tangent / invalid-key 变异路径，只把 query overload 单独切出来：一方面让 `FRuntimeFloatCurve` 上的 float/double `Min/Max` 结果保持严格一致；另一方面让 `UCurveFloat` 经由 key-mutation helper 修改后，`GetValueRange()` / `GetValueRange_Double()` 能和 native rich curve 返回同一组边界值，防止以后只修 mutation 不修 query，或只保住 float 不保住 double。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 16 / 40 / 49 / 52：`RuntimeFloatCurve` helper 仍滞留在去 mixin 的 helper class 上，invalid-key guard 仍静默失败，`SmartAuto` 语义已回退；说明这整组 curve contract 仍需要按更细颗粒拆开收口。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-7 / NewTest-8 / NewTest-21 / NewTest-35：当前只对 `GetNumKeys` / `GetTimeRange` 做 bind-config smoke，建议补 `GetTimeRange_Double()`、`GetValueRange()` / `_Double()`、`UCurveFloat` key-mutation 和 `SmartAuto` 的 dedicated runtime coverage。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同域 API 若同时分散在 helper class、target type 与 hand-written bind 上，必须把真正 authoritative contract 拆成可独立验证的单元。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` L34-L63 — `GetTimeRange()` / `GetValueRange()` 与 `GetTimeRange_Double()` / `GetValueRange_Double()` 仍是显式分开的 float/double overload，double 版当前仍通过中间 `float` 变量做 widening。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` L87-L205 — 同一 helper class 仍同时承接 `FRuntimeFloatCurve` query 与 `UCurveFloat` mutation/key-creation，说明 query parity 与 mutation parity 仍在同一文件内耦合。
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` L781-L847 — 当前 direct-bind smoke 仍只覆盖 `GetNumKeys()` 与 `GetTimeRange()`，没有 `GetValueRange*()`、`GetTimeRange_Double()` 或 `UCurveFloat` query 行为断言。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp`
- [ ] **P13.2** 📦 Git 提交：`[FunctionLibraries] Test: isolate runtime float curve double query contracts`
- [ ] **P13.2-T** 单元测试：固定 `RuntimeFloatCurve` float/double query overload、`UCurveFloat` range parity 与 direct-bind 解析
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp`
  - 测试场景：
    - 正常路径：两 key `FRuntimeFloatCurve` 同时走 `GetTimeRange()` / `GetTimeRange_Double()`、`GetValueRange()` / `GetValueRange_Double()`，`Min/Max` 与 native reference 完全一致；对经过 `AddCurveKey*` / `SetDefaultValue` / extrapolation 修改后的 `UCurveFloat`，float 与 double 查询返回同一组边界值。
    - 边界条件：空 curve、单 key curve、负时间/负值、以及需要 widen 到 `double` 的小数输入都保持稳定结果，不写反 `Min/Max`，也不因 float/double 路径切换而漂移。
    - 错误路径：脚本显式声明 `double` out-ref 时必须命中 `ScriptName = "GetTimeRange"` / `ScriptName = "GetValueRange"` 的正确 overload；若未来 direct-bind 只剩 `float` 版、helper surface 退回 helper class，或 `GetValueRange_Double()` 与 `GetValueRange()` 的结果不一致，测试必须明确失败。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveDoubleQueries`、`Angelscript.TestModule.FunctionLibraries.RuntimeFloatCurveUCurveFloatRangeParity`、`Angelscript.TestModule.Engine.BindConfig.FunctionLibraryDirectBind.RuntimeFloatCurveDoubleOutRefs`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P13.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add runtime float curve double overload coverage`

### 单元测试总览增补（Phase 13）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P13.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryMetadataTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp` | production host/class-map、world-context metadata、`RuntimeFloatCurve` direct-bind 守卫迁出 megafile | `P1` |
| `P13.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCurveFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryDirectBindTests.cpp` | float/double `GetTimeRange` / `GetValueRange`、`UCurveFloat` range parity、double out-ref overload 解析 | `P1` |

### 验收标准补充（Phase 13）

1. `AngelscriptBindConfigTests.cpp` 不再承担 FunctionLibrary production metadata/direct-bind 细项；真实 FunctionLibrary bind-config 守卫迁入 dedicated 文件后，generic bind-config 文件只保留框架级职责。
2. `RuntimeFloatCurve` 的 `GetTimeRange_Double()` / `GetValueRange_Double()` 不再只是“边界条件顺带覆盖”，而是有独立 runtime/direct-bind 断言固定 float/double 结果、out-ref 顺序与 `UCurveFloat` parity。

### 风险与注意事项补充（Phase 13）

1. **迁出 bind-config megafile 可能在过渡期引入短期重复覆盖或断言重名**
   - 如果不先定义“generic bind-config”与“FunctionLibrary production bind-config”的边界，迁移过程中容易出现一半旧断言还留在 megafile、一半新断言已落到 dedicated 文件的双写状态。
   - 缓解：先列清单，再按 `metadata -> direct-bind -> host/class-map` 三类一次性搬迁，最后再回删 megafile 中对应断言，避免长期双轨。

2. **`RuntimeFloatCurve` 的 double query 其实仍建立在 float rich-curve 数据之上**
   - 当前 `GetTimeRange_Double()` / `GetValueRange_Double()` 是显式 “float -> double” widening 适配，不应在本轮误写成“提供更高精度的数据源”。
   - 缓解：测试与验收标准都要把目标表述为“float/double 结果一致、顺序正确、surface 稳定”，而不是承诺超出当前 rich-curve 存储模型的额外精度。

---

## 深化（2026-04-09 06:53）

本轮补充说明：

- 再次复核 `Documents/Plans/Plan_TestCoverageExpansion.md` 与现有 `Plan_FunctionLibraries.md` 后，未见专门承接以下专项回归场景的活跃 Plan：`USceneComponent` 的 default-statement attach guard 与 quat/socket/composite transform、`AActor` 的 editor construction helper、`FHitResult` 的 `PhysMaterial` cleanup、`Script` 全局初始化 helper 的 hotreload suffix / 无 namespace 语义，以及 `GameplayTag` 非对称 query 数据集。
- `Documents/AutoPlans/DiscoveryPlans/FunctionLibraries_Plan.md` 本轮再次核验仍不存在，因此以下新增条目继续只使用 `[A]` 与 `[C]` 交叉来源，不伪造 `[B]`。
- 这批条目都不是重写既有 `P1-P13` 主线，而是给已立项的高优先级问题补齐“之前没有被单独固定”的高风险专项回归，避免执行时只收主 contract、漏掉最容易再次退化的边缘语义。

### Phase 14：给既有高优先级条目补齐专项回归场景

- [ ] **P14.1** 深化 `P3.4`，把 `USceneComponent` 的 default-statement attach guard 与 quat/socket/composite transform contract 从“顺带覆盖”提升为显式约束
  - `P3.4` 已经承接 `ComponentLibrary` 的 owner/null-guard/attach 主线，但本轮交叉核对后确认，还缺两组容易反复回退的细颗粒分支：一组是 `AttachToComponent()` 在 `default` 语句中的显式报错与推荐 `Attach =` / `AttachSocket =` 路径；另一组是 quat overload、socket quaternion、组合 transform setter 与 `SetbVisualizeComponent()` 这批 helper 的 local/world 镜像语义。继续只靠基础 relative/world 位置断言，会让这些高频分支再次在“能编译、主路径也过了”的情况下静默漂移。
  - 执行时不要再把这批场景塞回宽泛 parity 文件。优先新建专用 `Component` 行为测试文件，把 `AttachToComponent` 的 constructor-guard 文本、`SetRelativeRotationQuat()` / `SetWorldRotationQuat()` 的 method-style contract、`GetSocketQuaternion(NAME_None)` / `GetComponentQuat()` 的镜像结果，以及 `SetbVisualizeComponent()` 的 editor-only 行为收口到同一组场景里；若执行过程中发现 helper 本体的 metadata 或 guard 语义仍需微调，改动也只落在 `AngelscriptComponentLibrary.h`，避免再次扩散成跨域 megafile 修补。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 48 / 63 / 64：`AttachToComponent` 把 attach 语义压扁成单一策略，quat setter 丢失 `NotAngelscriptProperty`，并且 `USceneComponent` 高频 helper 的容错 / contract 仍与现有 bind 路径不一致。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-24 / NewTest-29 / Issue-20：`AttachToComponent` 的 default-statement guard、quat/socket/composite transform helper 以及 component 相关断言都还没有 dedicated 场景，继续混在粗粒度脚本里会掩盖失败定位。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L49-L75 — `SetRelativeRotationQuat()` 与 `SetRelativeLocationAndRotationQuat()` 仍只保留 `ScriptName`，没有把旧 `NotAngelscriptProperty` 契约恢复到真实声明。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L77-L95 — `GetSocketQuaternion()`、`SetComponentQuat()`、`GetComponentQuat()` 仍是 active helper，但当前计划尚未把它们列成独立回归场景。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L223-L244 — `AttachToComponent()` 仍保留 `FUObjectThreadContext::IsInConstructor` 的显式报错分支，`SetbVisualizeComponent()` 仍是 editor-only helper，说明这两条 contract 还真实存在于当前源码中。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`
- [ ] **P14.1** 📦 Git 提交：`[FunctionLibraries] Fix: tighten component guard and composite helper contracts`
- [ ] **P14.1-T** 单元测试：把 `AttachToComponent` constructor-guard 与 quat/socket/composite transform helper 升级成独立场景
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：在已注册的 parent/child `USceneComponent` 层级上，对脚本路径与 native mirror 路径分别执行 `SetRelativeRotationQuat()`、`SetRelativeLocationAndRotationQuat()`、`SetWorldTransform()`、`AddLocalTransform()`、`AddWorldRotationQuat()`，并对照 `GetComponentQuat()`、`GetRelativeScale3D()`、`GetSocketQuaternion(NAME_None)` 与最终 local/world transform。
    - 边界条件：`NAME_None` socket、identity quat、zero delta transform、editor build 下 `SetbVisualizeComponent(true/false)` 的切换都要有稳定结果；推荐 `UPROPERTY(DefaultComponent, Attach = ...)` 路径必须继续成功建立层级。
    - 错误路径：故意在 `default` 语句中调用 `AttachToComponent()` 时，必须抛出稳定且可匹配的诊断文本；`null` component / parent 若按最终 contract 应安全失败，也要在同一文件里固定下来，不能再落回 access violation。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.ComponentAttachDefaultStatementGuard`、`Angelscript.TestModule.FunctionLibraries.ComponentQuatAndCompositeTransform`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add component guard and composite helper coverage`

- [ ] **P14.2** 深化 `P3.1`，给 `AActor` 的 editor construction helper 建立单独的 editor-only 运行期契约
  - `P3.1` 已把 `AActor` helper 的导出面、sweep 与 attach 主线收进计划，但 `SetbRunConstructionScriptOnDrag()` / `RerunConstructionScripts()` 这条 editor-only 分支仍停留在“文件里有 helper、计划里顺带提到、自动化却没有 dedicated scenario”的状态。由于这两个入口只在 editor 设计期工作流里暴露问题，它们特别容易在日常 gameplay 测试全绿时悄悄回退。
  - 本项只补 editor construction 这一窄切片，不重复 `P3.1` 的位置/附着主线。执行时用单独的 script actor fixture 把 `ConstructionScript()` 的可观察副作用固定下来，再通过 helper 触发 `bRunConstructionScriptOnDrag` 变更与 `RerunConstructionScripts()`，确保未来如果 helper 消失、editor-only gating 漂移、或 construction rerun 不再同步脚本派生属性，都能在独立场景里直接失败。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 6 / 66：`UAngelscriptActorLibrary` 当前大量 helper 仍停在裸 `UFUNCTION()`，真正自动导出的只剩与 hand-written bind 重叠的 getter；`RerunConstructionScripts()` 等 editor helper 也仍然只存在于当前 library 文件中。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-25：actor editor construction helper 目前完全无 dedicated 覆盖，transform/attach 主线不会替它兜底。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L197-L208 — `AttachToComponent()` / `AttachToActor()` 之后，当前文件仍继续保留 editor helper 区段，说明这类 editor-only utility 仍由 `FunctionLibraries` 维护。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L211-L217 — `SetbRunConstructionScriptOnDrag()` 仍是 active helper，且当前只有 bare `UFUNCTION()` 声明。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L220-L226 — `RerunConstructionScripts()` 仍只在 `#if WITH_EDITOR` 区段下由当前 library 提供，没有现成 runtime 行为测试兜底。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp`
- [ ] **P14.2** 📦 Git 提交：`[FunctionLibraries] Fix: harden actor editor construction helper contract`
- [ ] **P14.2-T** 单元测试：把 actor editor construction helper 从主线 transform 测试中拆成独立 editor-only 场景
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：脚本 actor 在 `ConstructionScript()` 中递增 `ConstructionCount` 并更新可观察派生属性；测试脚本通过 helper 先设置 `bRunConstructionScriptOnDrag`，再调用 `RerunConstructionScripts()`，验证 construction 计数、派生属性和 editor 标志都与 native 参考一致。
    - 边界条件：重复 rerun、`bRunConstructionScriptOnDrag(true/false)` 双向切换、已有非零默认 transform / component 层级的 actor 都要保持稳定结果，不把 editor helper 和 transform 主线混成一条断言。
    - 错误路径：`null AActor`、非 editor gating 漂移或 helper 丢失时，测试必须得到稳定失败信号，而不是继续只靠人工拖拽 actor 才暴露回归。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.ActorConstructionEditorHelpers`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.2-T** 📦 Git 提交：`[FunctionLibraries] Test: add actor editor construction helper coverage`

- [ ] **P14.3** 深化 `P2.1`，把 `FHitResult.GetPhysMaterial()` 与 reset 后清理效果固定成独立 contract
  - `P2.1` 已经承接 `FHitResult` helper 面恢复，但当前计划仍主要围绕 actor/component/flag round-trip。`GetPhysMaterial()` 这条 getter 虽然在源码里还在，且正是当前残余 helper 里最容易被 gameplay 脚本直接消费的一条路径，可它既没有专门的测试建议落点，也没有被单独写成“reset 后必须清空旧引用”的回归断言。
  - 本项不重复 `P2.1` 的 owner 收口和 `SetPhysMaterial()` / `Reset` overload 恢复，只把 `PhysMaterial` 访问单独抽出来：一方面验证 getter 当前确实仍从 `HitResult.PhysMaterial` 读取；另一方面在 `Reset()` / 扩展 reset 契约收口后，明确要求旧物理材质引用必须被清空，避免未来只修 actor/component 句柄却遗漏材质载荷的生命周期问题。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 32 / 58：`AngelscriptHitResultLibrary` 当前缩成残缺子集，`SetPhysMaterial` 与更完整的 reset 语义已掉线，只剩 `GetPhysMaterial()` 这类残余访问器。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-30：现有 `HitResult` 计划尚未把 `GetPhysMaterial()` 与 reset 后清理效果单独固定，actor/component/flag round-trip 无法替代这条 contract。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` L31-L35 — `Reset(FHitResult&)` 当前仍只有无参版本，说明 reset 行为还没有更细颗粒的 dedicated contract。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` L51-L56 — `GetPhysMaterial()` 仍直接返回 `HitResult.PhysMaterial.Get()`，它是当前残余 helper 面里真实活跃的一条材质访问路径。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` L16-L56 — 当前文件中不存在 `SetPhysMaterial()`，进一步说明 getter / reset cleanup 必须在 `P2.1` 执行时被显式补测，而不是假定会随其它 helper 一起自然正确。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptHitResultFunctionLibraryTests.cpp`
- [ ] **P14.3** 📦 Git 提交：`[FunctionLibraries] Fix: clarify hit result phys material cleanup contract`
- [ ] **P14.3-T** 单元测试：锁定 `GetPhysMaterial()` 访问与 reset 后清理效果
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptHitResultFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：由 native 或恢复后的 script setter 向 `FHitResult` 注入一个可识别的 `UPhysicalMaterial`，脚本调用 `GetPhysMaterial()` 后必须返回同一对象。
    - 边界条件：default-constructed `FHitResult`、重复 `Reset()`、以及没有物理材质的命中结果都要稳定返回 `nullptr`，不把“未设置”和“残留旧引用”混淆。
    - 错误路径：一旦命中结果被 `Reset()` 或最终恢复的扩展 reset 清理，`GetPhysMaterial()` 不得继续泄露旧材质；若 `SetPhysMaterial()` 最终仍由其它 owner 提供，getter 也必须和该 authoritative 路径保持一致。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.HitResultPhysMaterialAccess`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.3-T** 📦 Git 提交：`[FunctionLibraries] Test: add hit result phys material cleanup coverage`

- [ ] **P14.4** 深化 `P4.5` / `P9.2`，把 `Script::Get*GlobalVariableBeingInitialized()` 的 hotreload suffix 与无 namespace 语义固定成显式边界
  - `P4.5` 已把 metadata / `baseModuleName` 问题纳入计划，`P9.2` 又把 stale context truth-source 加进来；但本轮对照 `TestGaps` 后确认，仍少一组最容易误导调试的专项场景：模块名带 hotreload suffix 时必须返回真实模块名而不是去后缀基名；无 namespace 全局变量在初始化期允许返回空 namespace，但此时 name/module 不能同步为空。少了这组边界断言，执行者即使修了主线，也仍可能留下“字符串看起来没错、但在热重载或全局 namespace 场景里继续误导人”的隐性回归。
  - 本项不新增脚本 surface，只要求把现有三条 helper 的字符串语义彻底钉死：header 侧继续恢复 `NotAngelscriptProperty` / `no_discard`，runtime 侧继续走当前 engine-safe truth-source；然后用带后缀模块名、带 namespace 全局和无 namespace 全局三组脚本模块，把返回值矩阵写成独立测试，不再让这组调试 helper 只靠 happy path。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 34 / 50 / 90：三条 global-init helper 丢失 `NotAngelscriptProperty` 与 `ScriptNoDiscard`，`GetNamespace...()` 用空字符串同时表示两种状态，`GetModuleName...()` 仍返回去 hotreload 后缀的 `baseModuleName`。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-36：当前还没有任何用例固定 hotreload 后缀模块名或无 namespace 全局变量在初始化期的返回语义。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h` L17-L35 — 三条 helper 当前仍只有 `UFUNCTION(BlueprintCallable)`，旧 `NotAngelscriptProperty` 仍停留在注释里。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` L16-L23 — `GetNamespaceOfGlobalVariableBeingInitialized()` 只要 `nameSpace == nullptr` 就直接返回空字符串，当前仍未区分“全局 namespace”与“没有初始化上下文”。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp` L26-L31 — `GetModuleNameOfGlobalVariableBeingInitialized()` 仍直接返回 `module->baseModuleName`，hotreload suffix 语义在当前源码里仍然错误。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp`
- [ ] **P14.4** 📦 Git 提交：`[FunctionLibraries] Fix: lock script global init name semantics`
- [ ] **P14.4-T** 单元测试：覆盖 hotreload suffix、带/无 namespace 变量与无上下文返回矩阵
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：构造模块名显式带 suffix 的脚本模块，在带 namespace 与无 namespace 的全局变量初始化表达式中分别调用三条 helper，验证 `GetModuleName...()` 返回完整后缀模块名，`GetName...()` 返回变量名，带 namespace 场景返回对应 namespace。
    - 边界条件：无 namespace 全局变量初始化期间，`GetNamespace...()` 可以为空，但 `GetName...()` / `GetModuleName...()` 必须非空；退出初始化期后三条 helper 一律回到空字符串。
    - 错误路径：切换 `FAngelscriptEngineScope`、没有当前初始化上下文、或 hotreload 场景里的旧模块上下文泄露时，helper 必须稳定返回空字符串而不是上一次缓存值。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.GlobalInitContextHotReloadName`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.4-T** 📦 Git 提交：`[FunctionLibraries] Test: add global init hotreload context coverage`

- [ ] **P14.5** 深化 `P1.1`，用非对称多标签数据集显式区分 hierarchy helper 与 query factory 语义
  - `P1.1` 已经把 `GameplayTag` / `GameplayTagQuery` / `GameplayTagContainer` 主 contract 拉进计划，但当前测试基线仍有一个未被单独承接的缺陷：现有 `GameplayTagQueryCompat` 基本用单标签数据同时验证 `MatchAny` / `MatchAll` / exact query，`GameplayTagCompat` 也几乎没有真正触发 hierarchy helper。这样即使 `MatchesTagDepth()`、`RequestDirectParent()`、`GetGameplayTagParents()`、`GameplayTagQuery.Matches()` 或 `GetDescription()` 只剩半有效实现，单标签 happy path 仍可能继续给出假绿灯。
  - 本项不重复 `P1.1` 的 owner 收口，而是把“如何测出 query/层级语义差异”单独固定下来：测试数据必须至少包含 parent/child tag 与另一个同层无关 tag，容器必须设计成能区分 `MatchAny` / `MatchAll` / exact query；并且同一轮要同时走 `GameplayTagQuery.Matches(Tags)` 与 `Tags.MatchesQuery(Query)` 两条路径，避免继续只测 container helper 而不测 query member helper。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 8 / 10 / 85：`GameplayTag` / `GameplayTagQuery` helper 仍留在 function library，`GameplayTagContainer` 维持双轨维护且 `AddLeafTag` 返回语义已被裁掉，说明这组 helper 的真实 contract 需要更细粒度场景来固定。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-12 / Issue-13：现有 `GameplayTagCompat` 几乎不触达 hierarchy helper，`GameplayTagQueryCompat` 用单标签数据同时验证 `MatchAny` / `MatchAll` / exact query，无法区分工厂语义。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h` L19-L79 — `MatchesTag*()`、`MatchesAny*()`、`GetSingleTagContainer()`、`RequestDirectParent()`、`GetGameplayTagParents()` 仍全部留在当前 runtime helper 中，说明 hierarchy helper 仍是 active contract。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h` L19-L37 — `Matches()` 与 `GetDescription()` 仍是当前 query helper 的核心 surface，需要独立于 container helper 被执行。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` L33-L141 — `AddLeafTag()`、`HasAny*()`、`HasAll*()`、`MatchesQuery()` 仍是当前 wrapper 面的一部分，进一步要求测试数据能区分 query 工厂和 container 语义。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
- [ ] **P14.5** 📦 Git 提交：`[FunctionLibraries] Fix: tighten gameplay tag hierarchy and query contracts`
- [ ] **P14.5-T** 单元测试：用 parent/child/peer tag 组合区分 `MatchAny` / `MatchAll` / exact 与 hierarchy helper
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
  - 测试场景：
    - 正常路径：准备 parent/child tag 与同层 peer tag 的非对称数据集，分别验证 `MatchesTag()`、`MatchesTagExact()`、`MatchesTagDepth()`、`RequestDirectParent()`、`GetGameplayTagParents()`、`GameplayTagQuery.Matches()`、`Tags.MatchesQuery()` 与 `GetDescription()` 的真实语义，并明确区分 `MatchAny` / `MatchAll` / `ExactMatch*` 三组工厂。
    - 边界条件：空 tag、空 container、空 query 继续沿用 `P1.1` 主线的默认 contract，但要与非空多标签数据集放在同一测试文件里，避免将来只保留某一半场景。
    - 错误路径：不能再只用单标签 happy path 通过所有 query 工厂；如果 query member helper、container helper 或 `AddLeafTag()` 的插入契约再次漂移，新的非对称数据集必须稳定把差异暴露出来。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.GameplayTagHierarchyDisambiguation`、`Angelscript.TestModule.FunctionLibraries.GameplayTagQueryFactoryDisambiguation`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.5-T** 📦 Git 提交：`[FunctionLibraries] Test: differentiate gameplay tag hierarchy and query factories`

### 单元测试总览增补（Phase 14）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P14.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp` | `AttachToComponent` default-statement guard、quat/socket/composite transform、editor visualize | `P1` |
| `P14.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` | editor construction rerun、`bRunConstructionScriptOnDrag`、editor-only helper contract | `P1` |
| `P14.3` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptHitResultFunctionLibraryTests.cpp` | `GetPhysMaterial()` 访问、default/null 材质、reset 后清理 | `P1` |
| `P14.4` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptScriptFunctionLibraryTests.cpp` | hotreload suffix 模块名、带/无 namespace 全局初始化、无上下文回空 | `P1` |
| `P14.5` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` | parent/child/peer tag 非对称数据集、query factory 区分、hierarchy helper 直测 | `P1` |

### 验收标准补充（Phase 14）

1. `USceneComponent` 的 constructor-guard、quat/socket/composite transform 不再只靠基础位置断言间接覆盖；这些分支有 dedicated 场景且能在 contract 漂移时直接失败。
2. `AActor` 的 `SetbRunConstructionScriptOnDrag()` / `RerunConstructionScripts()` 不再是只存在于 header 中的 editor helper；其 rerun 与标志位同步语义有独立 editor-only 自动化保护。
3. `FHitResult.GetPhysMaterial()` 在 reset 前后都具备 deterministic contract，不会因 `P2.1` 只修 actor/component/flag 主线而继续遗留旧材质引用。
4. `Script::Get*GlobalVariableBeingInitialized()` 对 hotreload suffix、带/无 namespace 场景和无上下文场景都返回稳定字符串矩阵，不再把 `baseModuleName`、空 namespace 和 stale context 混成一类。
5. `GameplayTag` / `GameplayTagQuery` 的 hierarchy helper 与 query factory 不再依赖单标签 happy path；`MatchAny` / `MatchAll` / exact 与 member/container 双路径差异都有 dedicated 回归。

### 风险与注意事项补充（Phase 14）

1. **`Component` / `Actor` 的 editor-only 场景对 fixture 与 build 配置更敏感**
   - `AttachToComponent` constructor-guard、`SetbVisualizeComponent()` 和 `RerunConstructionScripts()` 都依赖 editor 行为或 `WITH_EDITOR` gating，执行时必须把场景与普通 runtime helper 用例分开，避免因为 build 配置差异把行为回归误判成测试脆弱。

2. **全局初始化 helper 的 hotreload 命名测试不能把“当前后缀格式”误写成永久 ABI**
   - 本轮要固定的是“返回真实模块名，而不是去后缀基名”，不是把某个具体后缀格式永久写死。测试应比较“是否保留 suffix 与模块唯一性”，不要把实现细节变成过度严格的字符串模板。

3. **`GameplayTag` 非对称数据集需要稳定、可重复的 tag 选择策略**
   - 如果测试继续依赖“第一个注册 tag”或不保证 parent/child/peer 关系，新的 query factory 用例就会退回脆弱的偶然绿灯。执行时必须显式选取存在层级关系的一组 tag，必要时在 fixture 中预先注册测试 tag，避免把场景正确性再次交给运行环境偶然性。

---

## 深化 (2026-04-09 07:01:52)

- 复核 `Documents/Plans/Plan_TestCoverageExpansion.md` 与 `Documents/Plans/Plan_GlobalVariableAndCVarParity.md` 后，未见专门承接 `GameplayTag` helper 默认/空值语义矩阵的活跃条目：前者只覆盖代表性扩展测试，后者只处理 `EmptyTag` / `EmptyContainer` / `EmptyQuery` 作为 namespace global property 的可见性，不覆盖 `FunctionLibraries` helper 的运行期 contract。

### Phase 15：`GameplayTag` 默认值 contract 独立固化

> 目标：把 `FGameplayTag::EmptyTag`、`FGameplayTagContainer::EmptyContainer`、`FGameplayTagQuery::EmptyQuery` 从 `P1.1` / `P14.5` 的宽边界条件中拆出来，形成 member helper 与 container/query helper 共用的专门回归矩阵。

- [ ] **P15.1** 深化 `P1.1` / `P14.5`，把 `EmptyTag` / `EmptyQuery` / `EmptyContainer` 的 helper contract 从“顺带覆盖”升级成单独 authoritative 语义
  - `P1.1` 已要求恢复 owner 与主 contract，`P14.5` 又补了非对称 parent/child/peer 数据集；但当前计划还没有把默认/空值输入的三元语义拆成独立执行项。`FunctionLibraries_TestGaps` 已明确把这三条分别列为 `NewTest-40`、`NewTest-41`、`NewTest-42`，说明如果继续只把它们写在宽泛边界条件里，执行者仍可能优先完成非空 happy path，而把最容易在默认构造值上悄悄回归的 helper 留成隐性空白。
  - 本项不重复 `P1.1` 的 owner 收口，也不重复 `P14.5` 的 query factory 区分；它只固定三组“默认值仍应如何表现”的最终 contract：`EmptyTag` 在 hierarchy helper 上必须稳定返回 `false` / `0` / empty container / invalid parent；`EmptyQuery` 在 `Matches()` / `GetDescription()` 上必须和 container helper 保持一致的空值语义；`EmptyContainer` 在 `HasAll*` / `HasAny*` / `HasTag*` 上必须与 native 默认集合语义一致，不能因为 member/container 双路径或 owner 迁移而漂移。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 8 / 10 / 85：`GameplayTag` / `GameplayTagQuery` helper 仍留在 function library、`GameplayTagContainer` 维持双轨维护、`AddLeafTag` 返回语义已被裁掉，说明这组 helper 需要更细粒度的 contract 固定，而不是只靠非空 happy path。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-12 / Issue-13 / NewTest-40 / NewTest-41 / NewTest-42：现有测试不触达 hierarchy helper 的默认值语义，也没有任何 dedicated 用例固定 `EmptyTag`、`EmptyQuery`、`EmptyContainer` 的返回矩阵。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 `GameplayTags` 域同时被 UHT function-table 与 hand-written bind 承接；如果默认值语义不被单独固定，class-map 存在性并不能证明最终 public helper contract 正确。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h` L21-L79 — `MatchesTag()`、`MatchesTagExact()`、`MatchesTagDepth()`、`MatchesAny*()`、`GetSingleTagContainer()`、`RequestDirectParent()`、`GetGameplayTagParents()` 仍全部暴露，默认/空 tag 输入的真实 contract 仍由当前 helper 负责。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h` L21-L37 — `Matches()` 与 `GetDescription()` 仍是 active query helper，空 query 语义如果不单独钉住，`P14.5` 的非空数据集无法替它兜底。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h` L55-L141 — `HasTag*()`、`HasAny*()`、`HasAll*()` 与 `MatchesQuery()` 仍是 active container helper，空 container / empty tag / empty query 的默认集合语义仍可能在当前 wrapper 面漂移。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
- [ ] **P15.1** 📦 Git 提交：`[FunctionLibraries] Fix: lock gameplay tag empty-value contracts`
- [ ] **P15.1-T** 单元测试：把 `EmptyTag` / `EmptyQuery` / `EmptyContainer` 拆成独立回归矩阵
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
  - 测试场景：
    - 正常路径：以一个已注册 `ValidTag` 和其非空 container 作为对照，分别验证 `EmptyTag.MatchesTag(ValidTag)` / `MatchesTagExact(ValidTag)` / `MatchesAny(ValidContainer)` 返回 `false`，`EmptyTag.MatchesTagDepth(ValidTag)` 返回 `0`，`EmptyTag.GetSingleTagContainer()` 为空，`EmptyQuery.Matches(ValidContainer)` 返回 `false`，`EmptyContainer.HasAll(EmptyContainer)` / `HasAllExact(EmptyContainer)` 返回与 native 一致的默认结果。
    - 边界条件：同一轮同时覆盖 default-constructed `FGameplayTag` / `FGameplayTagContainer` / `FGameplayTagQuery` 与对应 namespace global 默认值，确认 helper 在“默认对象来源不同但语义相同”时结果一致；`EmptyQuery.GetDescription()` 必须固定为空字符串或与 native 当前空描述完全一致的结果，不再把空描述语义留给调用方猜测。
    - 错误路径：member helper 与 container/query helper 不能在空值输入上出现分裂结果；一旦 owner 收口后某条路径继续返回脏 parent、误匹配 `ValidTag`、把空 query 视为“匹配一切”，或在跨 `FAngelscriptEngineScope` 后泄露旧值，测试必须稳定失败。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.GameplayTagEmptyTagContracts`、`Angelscript.TestModule.FunctionLibraries.GameplayTagQueryEmptyQueryContracts`、`Angelscript.TestModule.FunctionLibraries.GameplayTagContainerEmptyContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P15.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add gameplay tag empty-value contract coverage`

### 单元测试总览增补（Phase 15）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P15.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` | `EmptyTag` / `EmptyQuery` / `EmptyContainer` 默认值矩阵、member 与 container/query 双路径一致性 | `P1` |

### 验收标准补充（Phase 15）

1. `FGameplayTag::EmptyTag`、`FGameplayTagContainer::EmptyContainer`、`FGameplayTagQuery::EmptyQuery` 都拥有 dedicated 自动化测试名，不再只作为 `P1.1` / `P14.5` 的附带边界条件存在。
2. `GameplayTag` hierarchy helper、`GameplayTagQuery` member helper、`GameplayTagContainer` helper 在空值输入上的返回值矩阵与 native 当前语义一致，不再出现 member path 与 container/query path 分裂。
3. 本轮新增的默认值 contract 不与 `Plan_GlobalVariableAndCVarParity.md` 重复：global property 可见性仍由原计划负责，而 helper 运行期语义由本计划负责。

### 风险与注意事项补充（Phase 15）

1. **空 query 的 description 断言不能把当前引擎实现的文案细节写死过头**
   - `GetDescription()` 更适合固定“空 query 返回空字符串或与 native 当前空描述完全一致”的 contract，而不是写死某段可能随引擎版本微调的提示文案。

2. **默认值来源要区分“helper contract”与“namespace global property contract”**
   - 本项可以复用 `FGameplayTag::EmptyTag` / `FGameplayTagContainer::EmptyContainer` / `FGameplayTagQuery::EmptyQuery` 作为输入来源，但不要把测试目标扩张成 global property 注册层；否则会与 `Plan_GlobalVariableAndCVarParity.md` 重叠。

3. **空值矩阵必须和 `P14.5` 的非对称数据集共存**
   - 如果后续只保留 `EmptyTag` / `EmptyQuery` / `EmptyContainer` contract，而把 parent/child/peer 非对称数据集删掉，就会重新失去 query factory 区分能力；执行时应把两组测试放在同一文件但不同测试名下，分别守住“默认值语义”和“非对称语义差异”。

---

## 深化 (2026-04-09 07:08:35)

- 复核 `Documents/Plans/` 后，未见专门承接 `GetDelta` / `ApplyDelta` / `GetRelative` / `ApplyRelative` / `MakeDeltaRotationFromAngularVelocity` / `MakeAngularVelocityFromDeltaRotation` 这组 `Math` utility 的活跃 Plan；现有 `P7.1` / `P7.2` / `P13.2` 只覆盖实例 helper 面、metadata 与 shortest-path/query contract，还没有把整组缺失 workflow utility 立成独立执行项。

### Phase 16：`Math` delta/relative utility 工作流补齐

> 目标：补回 `FRotator` / `FRotator3f`、`FQuat` / `FQuat4f`、`FTransform` / `FTransform3f` 的 delta/relative/ angular-velocity utility 家族，并用 round-trip 测试锁定乘法顺序与 parent/child 语义。

- [ ] **P16.1** 深化 `P7.1` / `P13.2`，补回 `Math` delta/relative utility 家族，而不是继续让脚本手写 quaternion / transform 组合
  - `P7.1` 已承接 `Rotator/Transform` 的实例 helper 面，`P13.2` 已承接 shortest-path 与 transform rotation query；但当前真正缺失的仍是更高层的工作流 helper：`GetDelta` / `ApplyDelta` / `GetRelative` / `ApplyRelative`、`MakeDeltaRotationFromAngularVelocity`、`MakeAngularVelocityFromDeltaRotation` 在当前源码与 hand-written bind 里都不存在。执行时应把这批 helper 继续收口在 `AngelscriptMathLibrary.h` 的 math facade 下，并与 `P7.1` 选定的 authoritative owner 保持一致，避免再拆成“基础 method 在 bind、工作流 helper 在零散 wrapper”的第三条通路。
  - 这项工作需要同时考虑 base/float 变体对称性。当前 `FRotator` / `FQuat` / `FTransform` 以及 `3f/4f` 对应 helper 都还活着，但脚本缺少“从 `Source/Target` 求 delta，再反向应用到 `Source` 得到 `Target`”“从 `Parent/Child` 求 relative，再还原世界姿态”“由角速度生成一帧 delta rotation，再反解回 angular velocity”的正式入口，只能手写乘法顺序与坐标系转换，最容易把 source/target 或 parent/child 顺序写反。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 54：`AngelscriptMathLibrary.h`、`Binds` 与 `AngelscriptTest` 当前都没有 `GetDelta` / `ApplyDelta` / `GetRelative` / `ApplyRelative` / `MakeDeltaRotationFromAngularVelocity` / `MakeAngularVelocityFromDeltaRotation`；UEAS2 已提供这组围绕世界空间 / 相对空间和角速度 / delta rotation 的基础 utility。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-14 / NewTest-23：现有 `MathExtendedCompat` 仍没有 dedicated 覆盖 `AngelscriptMathLibrary.h` 的高频姿态 / transform helper；shortest-path 与 `TransformRotation` 都缺独立行为回归，说明更高层的 delta/relative workflow 也处于无测试状态。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L512-L779 — `UAngelscriptFRotatorLibrary` / `UAngelscriptFRotator3fLibrary` / `UAngelscriptFQuatLibrary` / `UAngelscriptFQuat4fLibrary` 当前只保留 `MakeFromAxes`、方向向量、`Compose`、`AngularDistance` 与轴向构造 helper，没有任何 `GetDelta` / `ApplyDelta` / `GetRelative` / `ApplyRelative` 或角速度转换 utility。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` L781-L910 — `UAngelscriptFTransformLibrary` / `UAngelscriptFTransform3fLibrary` 当前只保留 `Blend*`、`TransformRotation`、`InverseTransformRotation`、`SetRotation` 与 `MakeFrom*` helper，仍没有 delta/relative utility 家族。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRotator.cpp` L159-L193、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FQuat.cpp` L142-L179、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform.cpp` L134-L205 — hand-written bind 当前只暴露基础 `MakeFrom*` / `Vector` / `Quaternion` / `RotateVector` / `GetRelativeTransform` / `TransformRotation` 等现有 native surface，依然没有 analysis 指向的统一 delta/relative workflow helper。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRotator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRotator3f.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FQuat.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FQuat4f.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform3f.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
- [ ] **P16.1** 📦 Git 提交：`[FunctionLibraries] Feature: restore math delta and relative utility workflows`
- [ ] **P16.1-T** 单元测试：为 delta/relative/ angular-velocity helper 建立 round-trip 与顺序保护
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：对一组固定 `Source/Target` rotator、quat 与 transform，分别验证 `GetDelta(Source, Target)` + `ApplyDelta(Source, Delta)` 能还原 `Target`；对一组固定 `Parent/Child` 姿态验证 `GetRelative(Parent, Child)` + `ApplyRelative(Parent, Relative)` 能还原 `Child`；`MakeDeltaRotationFromAngularVelocity` 与 `MakeAngularVelocityFromDeltaRotation` 对已知轴和时间步长形成可对照的 round-trip。
    - 边界条件：相同 `Source/Target` 必须产生 identity delta，identity parent 必须使 `GetRelative` 返回 world 姿态本身；跨 `+180/-180` 边界的 rotator、zero angular velocity、uniform / non-uniform scale 的 transform 都要保持有限且可预测的结果，并在 `FRotator/FRotator3f`、`FQuat/FQuat4f`、`FTransform/FTransform3f` 之间保持对称。
    - 错误路径：交换 `Source/Target` 或 `Parent/Child` 顺序后，`ApplyDelta` / `ApplyRelative` 不得仍然还原到同一目标姿态；专门用 mirror native reference 断言 helper 没有把乘法顺序、相对空间方向或角速度正负号绑定反。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.MathDeltaRoundTrip`、`Angelscript.TestModule.FunctionLibraries.MathRelativeRoundTrip`、`Angelscript.TestModule.FunctionLibraries.MathAngularVelocityDeltaUtilities`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P16.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add math delta and relative workflow coverage`

### 单元测试总览增补（Phase 16）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P16.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp` | delta/apply round-trip、relative/apply round-trip、angular-velocity 与 delta-rotation 双向换算、顺序错误保护 | `P1` |

### 验收标准补充（Phase 16）

1. `FRotator` / `FRotator3f`、`FQuat` / `FQuat4f`、`FTransform` / `FTransform3f` 至少补齐一套统一命名的 delta/relative workflow helper，不再要求脚本手写 quaternion / transform 组合公式。
2. `Source/Target` 与 `Parent/Child` 的顺序语义有 dedicated round-trip 自动化保护，后续一旦 helper 乘法顺序或空间方向漂移，会直接红灯。
3. `MathExtendedCompat` 不再继续“借名覆盖”高层姿态 workflow；新增用例在单独文件里对 `Math` delta/relative utility 给出 authoritative 行为基线。

### 风险与注意事项补充（Phase 16）

1. **新 helper 命名不能与现有 native `GetRelativeTransform()` / `SetToRelativeTransform()` 造成语义重叠**
   - 执行时要明确区分“原生成员函数已有的 transform primitive”和“脚本友好的 workflow facade”，避免把不同坐标系语义塞进过于接近的名字，导致补全与文档再次混淆。

2. **delta/relative helper 最容易在乘法顺序上 silently wrong**
   - 这组 API 的主要风险不是编译失败，而是“结果有数值但方向反了”。测试必须始终保留一组交换 `Source/Target`、交换 `Parent/Child` 的对照场景，专门防止未来把顺序绑反后仍拿到假绿灯。

3. **`3f/4f` 变体不能只补表面签名，不补行为对称**
   - 如果只给 `FRotator` / `FQuat` / `FTransform` 主类型补 helper，而让 `FRotator3f` / `FQuat4f` / `FTransform3f` 继续缺席，后续会再次把 float 变体切成另一套 workflow。执行时应把 base / float 变体放在同一轮一起验证。

---

## 深化 (2026-04-09 07:18:21)

- 复核 `Documents/Plans/`、现有 `Plan_FunctionLibraries.md`、`FunctionLibraries_TestGaps.md` 与当前 `AngelscriptGameplayTagBindingsTests.cpp` 后，未见专门承接“把 legacy `GameplayTagCompat` / `GameplayTagQueryCompat` 从弱断言 smoke 升级成 authoritative identity + member-path contract”的活跃 Plan；现有 `P1.1` / `P14.5` / `P15.1` 已覆盖 owner、非对称数据集和空值语义，但还没有单独钉住“成功请求到的 tag 必须就是请求名”以及“query member helper 必须真正走 `GameplayTagQueryMixinLibrary`”这两条 compat baseline。

### Phase 17：`GameplayTag` compat authority 基线补强

> 目标：把当前 `GameplayTagCompat` / `GameplayTagQueryCompat` 的 false-green 空间收口成可执行 contract，确保后续 `P1.1` / `P14.5` / `P15.1` 落地时，不会再被“只要返回任意 valid tag、只要 container path 还活着”掩盖真实回归。

- [ ] **P17.1** 深化 `P1.1` / `P14.5` / `P15.1`，把 `GameplayTagCompat` / `GameplayTagQueryCompat` 从 `IsValid()` / factory-only smoke 升级成 authoritative identity 与 member-path 合同
  - 当前 legacy compat 用例仍存在两处明确的假绿灯窗口：`GameplayTagCompat` 对成功 `RequestGameplayTag()` 只检查 `IsValid()`；`GameplayTagQueryCompat` 只走 `Tags.MatchesQuery(Query)` 的 container 路径，从未直接执行 `Query.Matches(Tags)` 或读取 `GetDescription()`。在 `GameplayTag` 域仍处于 FunctionLibrary wrapper 与 hand-written bind split-owner 的状态下，这两处弱断言会让“返回了错误 tag”“query member helper 失联”“member/helper 两条路径分裂”继续漏检。
  - 本项不重复 `P1.1` 的 owner 收口，也不重复 `P14.5` / `P15.1` 的非对称数据集与 empty-value 语义；它只把 compat baseline 收紧成两个必须独立守住的 authoritative contract：一是 `RequestGameplayTag(FName(RequestedName), true)` 返回对象的 `GetTagName()`、`ToString()`、`==` 都必须精确对应 `RequestedName`；二是 `MatchAny` / `MatchAll` / `MatchTag` 构造出来的 query 必须能通过 `Query.Matches(Tags)` 直接得到与 native reference 一致的正反结果，并且 `GetDescription()` 不再只是“可创建但没人读取”的悬空 helper。
  - 执行时优先复用现有 `AngelscriptGameplayTagBindingsTests.cpp`，但要把 legacy compat 脚本拆成独立错误码段或独立测试名，避免继续让 identity、factory、container helper、query member helper 混在一条 `return int` 链里互相掩盖。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 8 / 10：`GameplayTag` / `GameplayTagQuery` helper 仍留在 `FunctionLibraries`，类级 `ScriptMixin` 已注释，member helper 与 hand-written bind 处于 split-owner 状态；现有测试对这些 helper 仍是 0 命中或只走 container 主路径。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-2 / Issue-21：`GameplayTagQueryCompat` 没有验证 `Query.Matches(Tags)` / `GetDescription()`，`GameplayTagCompat` 对成功 `RequestGameplayTag` 只检查 `IsValid()`，没有比对请求名与返回 tag identity。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 UE 模块的脚本支持同时分散在 UHT function-table、`Binds/*.cpp` 与 runtime helper 多条管线里；若 compat baseline 只做弱断言，就无法识别究竟哪条 owner 回归了。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h` L13-L80 — `ScriptMixin = "FGameplayTag"` 仍被注释，`MatchesTag()`、`MatchesTagExact()`、`MatchesTagDepth()`、`MatchesAny*()`、`RequestDirectParent()`、`GetGameplayTagParents()` 仍全部停留在 FunctionLibrary helper 中。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h` L13-L38 — `ScriptMixin = "FGameplayTagQuery"` 仍被注释，`Matches()` 与 `GetDescription()` 仍只有 FunctionLibrary helper 这一条显式 surface。
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` L47-L52 — 当前 `GameplayTagCompat` 对成功 `RequestGameplayTag()` 仍只检查 `GlobalTag.IsValid()`。
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` L220-L257 — 当前 `GameplayTagQueryCompat` 只验证 factory + `Tags.MatchesQuery(Query)` container 路径，没有直接调用 `Query.Matches(Tags)` 或读取 `GetDescription()`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
- [ ] **P17.1** 📦 Git 提交：`[FunctionLibraries] Test: harden gameplay tag compat authority contracts`

- [ ] **P17.1-T** 单元测试：把 `GameplayTagCompat` / `GameplayTagQueryCompat` 升级成 identity + member-path authoritative 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
  - 测试场景：
    - 正常路径：native 侧先挑选一个已注册 `RequestedTag` 并把其原始 tag name 注入脚本；脚本调用 `FGameplayTag::RequestGameplayTag(FName(RequestedName), true)` 后，必须同时验证 `GetTagName() == FName(RequestedName)`、`ToString() == RequestedNameString`、以及与 native 侧同名 `RequestedTag` 的 `==` 比较为真。随后分别对 `MatchAny` / `MatchAll` / `MatchTag` 直接调用 `Query.Matches(PositiveContainer)`，并把 `GetDescription()` 与 native `FGameplayTagQuery::GetDescription()` 结果逐项对照。
    - 边界条件：同一轮保留 `FGameplayTag EmptyTag`、`FGameplayTagQuery EmptyQuery` 与空 container 作为对照，但不再让它们吞没成功路径断言；`MatchAny` / `MatchAll` / `MatchTag` 至少各覆盖一条 positive container 与一条 negative container，确保 member path 的 true/false 分支都被显式执行。
    - 错误路径：使用一个明确不存在且不同于 `NAME_None` 的 tag name 调 `RequestGameplayTag(MissingName, false)`，必须返回 invalid/empty tag；若 `Query.Matches(Tags)` member path 继续失联、错误退回 container 路径、或 `GetDescription()` 返回空/脏字符串而与 native reference 不一致，测试必须稳定失败而不是继续给出绿灯。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.GameplayTagRequestIdentity`、`Angelscript.TestModule.FunctionLibraries.GameplayTagQueryMemberContracts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P17.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add gameplay tag identity and member-path coverage`

### 单元测试总览增补（Phase 17）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P17.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` | `RequestGameplayTag` 精确 identity、`Query.Matches(Tags)` member path、`GetDescription()` 与 native reference 对照、missing tag 失败路径 | `P1` |

### 验收标准补充（Phase 17）

1. `GameplayTagCompat` 不再只证明“返回了某个 valid tag”，而是明确证明“返回的就是被请求的 tag name / string / object identity”。
2. `GameplayTagQueryCompat` 不再只证明 container helper 还能工作；`GameplayTagQueryMixinLibrary::Matches()` 与 `GetDescription()` 的 member path 有 dedicated 自动化保护。
3. 本轮新增的 compat authority baseline 不与 `P14.5` 的非对称数据集或 `P15.1` 的 empty-value matrix 重复：前者守住 query 语义差异，后者守住默认值语义，本项只守住 legacy compat smoke 最容易漏掉的 identity 与 member-path 入口。

### 风险与注意事项补充（Phase 17）

1. **`GetDescription()` 断言应优先对照 native reference，而不是写死文案细节**
   - `GameplayTagQuery` 的描述文本更适合作“与 native 当前结果一致”的对照项，不适合把整段说明写死成跨版本 ABI；否则后续引擎文案微调会制造无效噪音。
2. **成功 tag 与缺失 tag 需要显式分开，不能继续只拿 `NAME_None` 充当所有失败路径**
   - `NAME_None` 只能覆盖一类无效输入；要真正拦住“成功查到了错 tag”，必须同时使用一个已注册 tag name 和一个明确不存在的 `MissingName`。
3. **compat baseline 应拆成独立测试名，而不是继续堆进单个巨大 error-code 脚本**
   - 否则一旦 `RequestGameplayTag` identity 先失败，`Query.Matches(Tags)` 与 `GetDescription()` 分支仍会被短路，重新回到当前 `Issue-2` / `Issue-21` 的可见性问题。

---

## 深化 (2026-04-09 07:25:44)

- 复核 `Documents/Plans/Plan_UhtPlugin.md` 与当前活跃 Plans 后，未见专门承接“`FunctionLibraries` 头文件里残留 `//UFUNCTION(ScriptCallable)` / `//UCLASS(...ScriptMixin...)` 注释，但真实 callable surface 未被任何 build/runtime authority 记录”的活跃 Plan。`Plan_UhtPlugin.md` 当前只承接 exporter 落地、分片输出与 summary 产物，不负责 `FunctionLibraries` 迁移债账本。

### Phase 18：`FunctionLibraries` callable-surface 迁移债审计

> 目标：把 `FunctionLibraries/` 里“注释仍暗示脚本可见，但真实声明已掉出 UHT/runtime callable 面”的半迁移 helper 收口成显式账本与自动化守卫，避免后续继续只靠人工读头文件判断 owner。

- [ ] **P18.1** 为 `FunctionLibraries` 建立 callable-surface debt ledger，并让 `ScriptCallable` / `ScriptMixin` 注释残留不再处于账本外状态
  - 当前 `AngelscriptFunctionTableExporter.cs` 只把 `BlueprintCallable/Pure` 函数写入 `AS_FunctionTable_*` 与 skipped CSV，runtime `Bind_BlueprintType.cpp` 则额外接受真实 `NAME_ScriptCallable` metadata；但 `FunctionLibraries/` 里仍存在第三种“账本外”状态：源码保留 `//UFUNCTION(ScriptCallable)` 或 `//UCLASS(...ScriptMixin...)` 注释，真实声明却只是 bare `UFUNCTION()` 或 `UCLASS(Meta = ())`。这类 helper 既不会进入 UHT skipped 报表，也不会被 runtime 当成 script-callable，最终只能靠人工阅读头文件发现。
  - 本项不重复 `P2.2`、`P3.1`、`P1.1` 等单文件修复，而是新增一层跨文件 authority 守卫：先列出首批 `FunctionLibraries` 迁移债样本清单，再给每个样本明确归类为 `generated-callable`、`handwritten-owner`、`intentional-non-script-surface` 三类之一；任何仍带 `ScriptCallable/ScriptMixin` 注释残留、却没有 ledger 归类且不在 generated/runtime owner 内的符号，都应让自动化直接失败并输出精确文件/符号名。
  - 第一批样本不宜贪多，优先覆盖当前最典型且仍在源码中存在的三类漂移：`UAssetManagerMixinLibrary` 的 bare `UFUNCTION()` 查询链、`UAngelscriptActorLibrary` 的成片 bare `UFUNCTION()` mixin helper、以及 `GameplayTagMixinLibrary` 这类“函数仍是 `BlueprintCallable`，但类级 `ScriptMixin` 已被注释”的 member-surface 漂移。等 ledger 稳定后，再把其它 FunctionLibrary 分批纳入。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 1 / 6 / 8：`UAssetManagerMixinLibrary`、`UAngelscriptActorLibrary`、`GameplayTagMixinLibrary` 仍保留 `//UFUNCTION(ScriptCallable)` 或 `//UCLASS(...ScriptMixin...)` 注释，但真实声明已退化成 bare `UFUNCTION()` / `Meta = ()`，形成半迁移 callable surface。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-22 / Issue-23：现有 generated-function-table / bind-config 测试只抽查原生 callable 或 synthetic fixture，没有覆盖真实 FunctionLibrary 生产入口与 metadata/host 漂移。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 UE 模块的脚本支持同时分散在 UHT function-table、`Binds/*.cpp` 手写注册与 runtime helper 多条通路里，owner 不唯一。”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`ScriptCallable` 与 `BlueprintCallable` 在当前插件里只处于 `Partial` 同构状态，建议为 `ScriptCallable-only` surface 增加正式 skipped manifest / audit 输出。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` L53-L74 — `GetPrimaryAssetTypeInfo()`、`GetPrimaryAssetTypeInfoList()`、`GetPrimaryAssetRules()` 上方仍保留 `//UFUNCTION(ScriptCallable)` 注释，但真实声明仍只是 bare `UFUNCTION()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h` L12-L223 — `SetActorRelativeLocation()` 到 `AttachToActor()` / `RerunConstructionScripts()` 仍成片保留 `//UFUNCTION(ScriptCallable)` 注释，而真实声明大多仍是 bare `UFUNCTION()`；真正带 `BlueprintCallable` 的只剩 `GetActorLocation()` / `GetActorRotation()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h` L9-L20 — 类注释仍声明“bind functions on `FGameplayTag`”，但 `//UCLASS(Meta = (ScriptMixin = "FGameplayTag"))` 仍被注释成 `UCLASS(Meta = ())`。
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` L56-L63 — 当前 exporter 仍只统计 `BlueprintCallable/Pure`，不会把上述 bare `UFUNCTION()` half-migration surface 写入 skipped ledger。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L1311-L1314、L1403-L1406 — runtime 仍额外接受真实 `NAME_ScriptCallable` metadata，说明 build-time 与 runtime callable authority 现在仍是双轨。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/FunctionLibraryCallableAuditLedger.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryCallableAuditTests.cpp`
- [ ] **P18.1** 📦 Git 提交：`[FunctionLibraries] Test: add callable-surface migration debt audit`

- [ ] **P18.1-T** 单元测试：为 `FunctionLibraries` 的 callable-surface 迁移债建立 ledger 校验与 production owner 守卫
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryCallableAuditTests.cpp`
  - 测试场景：
    - 正常路径：ledger 中标记为 `generated-callable` 的样本必须能在 `AS_FunctionTable_*.cpp` / `AS_FunctionTable_SkippedEntries.csv` 中找到对应条目；标记为 `handwritten-owner` 的样本必须能在 `FAngelscriptBinds::GetClassFuncMaps()` 或既定 bind owner 上找到最终 authoritative entry。
    - 边界条件：像 `GameplayTagMixinLibrary` 这类“函数是 `BlueprintCallable`，但类级 `ScriptMixin` 已注释”的 header，必须通过 ledger 显式声明为 `handwritten-owner` 或 `intentional-non-script-surface`，不允许再靠注释与猜测隐式解释。
    - 错误路径：任何仍保留 `//UFUNCTION(ScriptCallable)` / `//UCLASS(...ScriptMixin...)` 注释残留、但既不在 ledger 中、也不在 generated/runtime owner 中的样本，都必须带着精确 `Header + Symbol + MissingOwnerKind` 失败；新增 bare `UFUNCTION()` half-migration surface 时，测试不得继续给出绿灯。
  - 测试命名：`Angelscript.TestModule.Engine.GeneratedFunctionTable.FunctionLibraryCallableAudit`、`Angelscript.TestModule.Engine.GeneratedFunctionTable.FunctionLibraryOwnerLedger`
  - 隔离方式：`FAngelscriptEngineScope` + 读取 UHT 生成产物 / `ClassFuncMaps`
- [ ] **P18.1-T** 📦 Git 提交：`[FunctionLibraries] Test: enforce function library callable audit ledger`

### 单元测试总览增补（Phase 18）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P18.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFunctionLibraryCallableAuditTests.cpp` | half-migration callable ledger、generated/runtime owner 对照、未归类 `ScriptCallable` / `ScriptMixin` 残留报错 | `P2` |

### 验收标准补充（Phase 18）

1. `FunctionLibraries/` 中首批高风险 half-migration 样本不再处于“源码注释看起来可用，但 build/runtime 都没有账本”的灰区；每个样本都能被明确归类为 `generated-callable`、`handwritten-owner` 或 `intentional-non-script-surface`。
2. generated-function-table / bind-config 自动化不再只会对原生 callable 和 synthetic fixture 给绿灯；真实 `FunctionLibraries` 迁移债会在新增时直接输出具体 header 与 symbol。
3. `Plan_UhtPlugin.md` 已有的 exporter 分片、summary 与 skipped CSV 继续保持通用工具链职责，本轮新增 ledger 只负责 `FunctionLibraries` 头文件迁移债审计，不与通用 UHT 生成主线重复。

### 风险与注意事项补充（Phase 18）

1. **ledger 不能退化成长期豁免名单**
   - 首批 registry 只应用于显式解释当前过渡态；执行时必须要求每个例外都写清 `owner_kind` 与后续收敛方向，避免把 half-migration debt 永久合法化。
2. **注释扫描只适合作为迁移债信号，不应替代真实 metadata / bind 判断**
   - 测试的失败条件应是“注释残留 + 无 ledger 归类 + 无 generated/runtime owner”三者同时成立；不能单凭存在旧注释就误报，否则正常历史注释也会被当成 bug。
3. **新增审计后，旧的灰色状态会从静默漏检升级成明确红灯**
   - 这不是回归，而是把原本账本外的问题显式化。执行时应先把首批样本范围控制在当前已经反复出现的 FunctionLibrary 热点，避免一次性扫全目录造成噪音。

---

## 深化 (2026-04-09 07:33:04)

- 复核 `Documents/Plans/Plan_HazelightBindModuleMigration.md`、`Documents/Plans/Plan_UEBindGapRoadmap.md`、`Documents/Plans/Plan_UhtPlugin.md`、现有 `Plan_FunctionLibraries.md`、`FunctionLibraries_TestGaps.md` 与当前 `AngelscriptComponentLibrary.h` / `Bind_USceneComponent.cpp` / `AngelscriptNativeEngineBindingsTests.cpp` 后，未见专门承接“把 `USceneComponent` 的 dual-owner 层级语义从 false-green smoke 升级成 registered parent/child authoritative contract”的活跃 Plan。现有 `P3.4` / `P14.1` 已覆盖 `ComponentLibrary` 主线、quat/socket/composite helper 与 null-guard，但还没有单独钉住 `SetRelativeLocation()` 在 FunctionLibrary 与 hand-written bind 之间的语义分叉，以及 `NativeComponentMethods` 当前 detached smoke 会把这类回归继续放过去。

### Phase 19：`USceneComponent` hierarchy authority 与 legacy smoke 退休

> 目标：把 `SetRelativeLocation()` / `GetComponentTransform()` / `GetNumChildrenComponents()` 的真实层级语义从“未注册孤立组件 smoke”升级成 registered parent/child authoritative baseline，并显式决定 `FunctionLibrary` wrapper 与 `Bind_USceneComponent.cpp` 谁是唯一 owner。

- [ ] **P19.1** 深化 `P3.4` / `P14.1`，收口 `USceneComponent::SetRelativeLocation()` 的 dual-owner，并退休 `NativeComponentMethods` 的 false-green 层级 smoke
  - 当前 `UAngelscriptComponentLibrary::SetRelativeLocation()` 与 `Bind_USceneComponent.cpp` 的 hand-written `SetRelativeLocation()` 并不是等价实现：前者只做 `Component->SetRelativeLocation(NewLocation)`，后者还会额外 `UpdateComponentToWorld()`。与此同时，唯一 legacy 覆盖 `NativeComponentMethods` 只在 `AddOwnedComponent()` 后的未注册、无父节点孤立组件上断言 `Relative == Transform.GetTranslation()` 且 `GetNumChildrenComponents() == 0`，这个场景天然无法区分 local/world hierarchy 语义，也看不出最终命中的是哪条 owner。
  - 本项不重复 `P3.4` 的整体 `ComponentLibrary` parity，也不重复 `P14.1` 的 quat/socket/composite helper。它只把最容易继续假绿的三条 contract 单独钉住：`SetRelativeLocation()` 在已注册 parent/child 层级上的 local/world 差异、`GetComponentTransform()` 对未注册但已挂 parent 的 fallback 组合语义、以及 `GetNumChildrenComponents()` 在真实 attach 前后的计数变化。执行时必须顺带裁决 owner：要么把 trivial transform helper 彻底留给 hand-written bind 并清理重复 wrapper，要么让 FunctionLibrary wrapper 在语义上与 hand-written 路径完全对齐，不能继续保留“两个入口都叫 `SetRelativeLocation()`、但 world-update 行为不一致”的状态。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 62 / 64 / 82：`GetNumChildrenComponents()` 与 `SetRelativeLocation()` 仍处于 FunctionLibrary 自动绑定和 hand-written bind 双源状态，`SetRelativeLocation()` 两条实现已在 `UpdateComponentToWorld()` 行为上分叉，且 `USceneComponent` 高频 helper 的 null / hierarchy contract 与现有 bind 容错策略不一致。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “Issue-8 / Issue-20：`NativeComponentMethods` 只在未注册、无父节点的孤立组件上验证相对位移，并把多类无关绑定揉进单个 error-code 脚本，无法拦截层级语义与 owner 分叉回归。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “Arch-MS-40：同一 UE 模块的脚本支持同时分散在 UHT function-table 与 `Binds/*.cpp` 手写注册两条管线，owner 需要显式化。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` L35-L40 — `SetRelativeLocation()` 仍只有 `Component->SetRelativeLocation(NewLocation)`，没有 `UpdateComponentToWorld()`、注册状态处理或 parent-aware 逻辑。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp` L112-L135 — hand-written `GetComponentTransform()` 对未注册组件会组合 `RelativeTransform * AttachParent->GetComponentTransform()`，而 `SetRelativeLocation()` 仍额外调用 `UpdateComponentToWorld()`，说明当前 `USceneComponent` transform helper 仍存在第二条 authoritative path。
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` L118-L130、L188-L207 — `NativeComponentMethods` 当前只在 `AddOwnedComponent()` 后直接执行脚本，没有 `RegisterComponent()`、`SetupAttachment()` 或非零 parent transform，仍然是 detached smoke 场景。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp`
- [ ] **P19.1** 📦 Git 提交：`[FunctionLibraries] Fix: lock scene component hierarchy owner and retire false-green smoke`
- [ ] **P19.1-T** 单元测试：把 `NativeComponentMethods` 的孤立组件 smoke 拆成 registered hierarchy / unregistered fallback / legacy compile-smoke 三个明确场景
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`
  - 测试场景：
    - 正常路径：创建已注册且具有非零 world transform 的 parent / child `USceneComponent`，脚本在 child 上调用 `SetRelativeLocation()` 后同时验证 `GetRelativeLocation()`、`GetComponentTransform().GetTranslation()`、parent world transform 与 native mirror 完全一致；attach 前后 `GetNumChildrenComponents()` 从 `0` 变为 `1`。
    - 边界条件：未注册但已 `SetupAttachment()` 的 child 应通过 hand-written fallback 组合出 `RelativeTransform * ParentTransform`；`SetRelativeLocation()` / `GetComponentTransform()` 在 `RegisterComponent()` 前后都必须保持可预测，不允许因为 owner 漂移出现一条路径更新 world、一条路径不更新 world。
    - 错误路径：`null USceneComponent`、已销毁 parent、以及 legacy `NativeComponentMethods` 继续在 detached smoke 场景里误报 green 时，专用断言必须明确失败；旧 megatest 应收缩为最小 compile/reflective smoke，不再承担 hierarchy 语义 authority。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.ComponentRegisteredHierarchyContracts`、`Angelscript.TestModule.FunctionLibraries.ComponentUnregisteredHierarchyFallback`、`Angelscript.TestModule.Bindings.NativeComponentMethodsSmoke`
  - 隔离方式：`FAngelscriptTestFixture` + `FScopedTestWorldContextScope`；legacy smoke 保持 `ASTEST_CREATE_ENGINE_SHARE`
- [ ] **P19.1-T** 📦 Git 提交：`[FunctionLibraries] Test: split native component smoke and lock hierarchy contracts`

### 单元测试总览增补（Phase 19）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P19.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptComponentFunctionLibraryTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` | registered parent/child 层级、未注册 fallback 组合、legacy smoke 收缩、dual-owner `SetRelativeLocation()` 一致性 | `P1` |

### 验收标准补充（Phase 19）

1. `USceneComponent::SetRelativeLocation()` 不再存在“FunctionLibrary 路径”和“hand-written bind 路径”在 world-update 语义上各算各的灰区；执行后的唯一 owner 与行为被测试固定。
2. `NativeComponentMethods` 不再以 detached / unregistered 孤立组件冒充层级语义覆盖；真实 hierarchy contract 在 dedicated 测试文件中由 registered parent/child 场景守住。
3. `GetComponentTransform()` 的未注册 fallback、已注册 runtime 路径和 child-count 行为都有独立断言，后续若因 owner 迁移或 helper 收口产生漂移，会直接红灯而不是继续假绿。

### 风险与注意事项补充（Phase 19）

1. **不要把 legacy smoke 完全删掉**
   - 旧 `NativeComponentMethods` 仍适合保留为最小 compile/reflective smoke；需要退休的是“拿它当 hierarchy authority”，不是把所有历史覆盖一起抹掉。
2. **registered 与 unregistered 语义必须分开建模**
   - `Bind_USceneComponent.cpp` 明确对未注册组件走 fallback 组合逻辑；如果把两种状态混在一条测试里，未来很容易再次把一条路径修好、另一条路径漏掉。
3. **owner 收口要与 `P3.4` / `P14.1` 保持一致**
   - 本项只解决 transform hierarchy false-green 与 trivial helper dual-owner，不应顺手扩成新的 component 大重构；否则会和已有 component 主线重新重叠。

---

## 深化 (2026-04-09 07:41:36)

- 复核 `Documents/Plans/`、现有 `Plan_FunctionLibraries.md`、`FunctionLibraries_Analysis.md`、`FunctionLibraries_TestGaps.md` 与当前 `Bind_UAssetManager.cpp` 后，未见专门承接“`UAssetManager.LoadPrimaryAsset*` 在 invalid `FPrimaryAssetId` 下提前返回，导致 complete/cancel callback 全部失联”的活跃 Plan。现有 `P2.2` / `P7.3` / `P10.2` 已覆盖 query export、scan/load workflow、safe singleton 与 `no_discard`，但还没有把这条 async failure contract 单独钉成可执行项。

### Phase 20：`UAssetManager` invalid-load callback 合同收口

> 目标：把 `LoadPrimaryAsset()` / `LoadPrimaryAssets()` 遇到 invalid `FPrimaryAssetId` 时的行为，从“silent early return + callback 永不触发”收口成显式、稳定、可回归的 callback-first 失败合同。

- [ ] **P20.1** 深化 `P7.3` / `P10.2`，收口 invalid `FPrimaryAssetId` 下 `LoadPrimaryAsset*` 的 callback 黑洞
  - `P7.3` 已承接 `AssetManager` 的 scan/filter/load/id workflow，`P10.2` 已承接 safe-singleton 与 query `no_discard`；但当前 `Bind_UAssetManager.cpp` 还留着另一条未被计划明确收口的 async failure path：`AssetManager_LoadPrimaryAssets()` 只要在输入数组里看到一个 invalid `FPrimaryAssetId`，就会在绑定 `CompleteDelegate` / `CancelDelegate` 之前直接 `return`。这会把脚本侧 `void + optional callbacks` 的公开 surface 退化成“调用看似成功、但失败没有任何脚本级反馈”的灰区。
  - 本项不重复 `P2.2` 的 initial-scan callback，也不重复 `P7.3` 的 `LoadPrimaryAssetsWithType()` / `ScanPathForPrimaryAssets()` / `FPrimaryAssetId` 构造主线。它只收口 invalid-load failure contract：优先保持现有 `void + optional callbacks` 形态，在 invalid-id 分支统一走一条显式 `ImmediateFail` 路径，至少保证 `OptionalCanceledCallbackName` 在提供时会被稳定触发一次；若调用侧没有提供 cancel callback，则必须同步落到稳定脚本异常或明示日志，而不能继续 silent early return。
  - 执行时要顺带处理两类边界分支，避免修一处再漏一处：一是 `LoadPrimaryAsset()` 的单 id 包装也必须复用同一套 invalid-id 失败逻辑；二是 mixed array（同时包含 valid/invalid id）与 empty array 需要有单一、可文档化的优先级规则，不能让一部分路径走引擎 immediate complete，另一部分路径继续静默返回。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 55：`UAssetManager.LoadPrimaryAsset*` 遇到无效 `FPrimaryAssetId` 时会直接返回，完成/取消回调都不会触发。”
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — “NewTest-11 / NewTest-39 目前只覆盖 query / initial-scan wrapper；可据此推断，`LoadPrimaryAsset*` 的 invalid-id callback 路径仍没有 dedicated 自动化保护。”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D3 / D8：当前 Angelscript 的 gameplay async public surface 仍是 callback/proxy-first；据此应把 `AssetManager` async helper 的失败分支也收口成显式 callback 合同，而不是 silent early return。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L49-L64 — `AssetManager_LoadPrimaryAssets()` 仍在 invalid asset 分支直接 `return`，没有任何脚本可观察的失败返回值或失败回调。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L66-L80 — `CompleteDelegate` / `CancelDelegate` 只有通过 invalid-id 检查后才会绑定，说明当前错误路径确实完全绕开了 callback 通知。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L93-L105 — `LoadPrimaryAsset()` / `LoadPrimaryAssets()` 对脚本暴露的仍是 `void` helper，调用者当前无法靠返回值判断 early return。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`
- [ ] **P20.1** 📦 Git 提交：`[FunctionLibraries] Fix: close invalid asset load callback gap`
- [ ] **P20.1-T** 单元测试：锁定 `LoadPrimaryAsset*` 在 invalid id、mixed array 与 no-work 分支下的回调合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp`
  - 测试场景：
    - 正常路径：测试夹具先动态找到一个当前项目里可用的 valid `FPrimaryAssetId`（必要时由 fixture 注册最小 test asset type），脚本分别调用 `LoadPrimaryAsset()` 与 `LoadPrimaryAssets()`；`OnFinished` 只触发一次，`OnCanceled` 不触发，且 receiver 记录到的调用顺序与 native handle 生命周期一致。
    - 边界条件：`LoadPrimaryAssets()` 传空数组时，要么稳定走“no work immediate complete”，要么稳定走“显式 no-op 不回调”，但必须由测试把最终规则钉死；mixed array（valid + invalid）必须遵循与 single invalid id 同一优先级，不允许一条路径 silent early return、另一条路径进入引擎 complete。
    - 错误路径：single invalid `FPrimaryAssetId`、mixed array 中首尾位置不同的 invalid id、以及提供/不提供 `OptionalCanceledCallbackName` 两种调用方式下，都不得 silent return；若最终合同选择 callback-first，则 `OnCanceled` 必须稳定触发且 `OnFinished` 不得触发；若最终合同选择脚本异常/日志，则测试需用 `AddExpectedError` 或等价断言把文本固定下来。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.AssetManagerInvalidPrimaryAssetLoadCallbacks`、`Angelscript.TestModule.FunctionLibraries.AssetManagerMixedPrimaryAssetLoadFailure`
  - 隔离方式：`FAngelscriptTestFixture` + `ASTEST_BUILD_MODULE` + native receiver UObject + `FEvent`/轮询同步等待
- [ ] **P20.1-T** 📦 Git 提交：`[FunctionLibraries] Test: lock asset manager invalid-load callbacks`

### 单元测试总览增补（Phase 20）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P20.1` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerFunctionLibraryTests.cpp` | invalid id 立即失败、mixed array 优先级、no-work immediate complete 或 no-op 规则、callback 单次交付 | `P1` |

### 验收标准补充（Phase 20）

1. `LoadPrimaryAsset()` / `LoadPrimaryAssets()` 在 invalid `FPrimaryAssetId` 下不再 silent early return；脚本调用者至少能收到稳定 cancel callback、脚本异常或固定日志中的一种显式失败信号。
2. single invalid id、mixed array 与 empty array 三类分支拥有统一且文档化的优先级规则，不再一部分走 callback、一部分静默返回。
3. `AngelscriptAssetManagerFunctionLibraryTests.cpp` 对 valid baseline、invalid id 与 mixed array 都有 dedicated 自动化保护，后续若再把 invalid-id 失败路径绕开 callback 绑定，测试会直接红灯。

### 风险与注意事项补充（Phase 20）

1. **把 silent failure 改成显式 callback/异常后，旧脚本可能首次观察到此前被吞掉的错误**
   - 这不是新回归，而是把原本灰区里的失败显式化；执行时要在测试与文档里把新合同写清，避免调用方误把“更容易看见失败”理解成行为变差。
2. **要避免 no-work immediate complete 与 invalid-id immediate fail 发生双重交付**
   - 当前代码注释已经说明 `LoadPrimaryAssets()` 在“没有工作可做”时可能立即触发 complete；invalid-id 修复必须先在 wrapper 层裁决失败优先级，防止同一调用既走失败信号又落到引擎 complete。
3. **mixed array 的规则必须固定为单一策略**
   - 只要数组里同时出现 valid/invalid id，就很容易在未来重构里出现“先过滤 invalid 再加载剩余 valid”与“整体失败”两种互相竞争的实现；本项要求测试把最终策略钉死，避免后续漂移。

---

## 深化 (2026-04-09 07:51:06)

- 复核 `Documents/Plans/Plan_TestCoverageExpansion.md`、`Documents/Plans/Plan_HazelightCapabilityGap.md`、现有 `Plan_FunctionLibraries.md`、`FunctionLibraries_Analysis.md` 与当前 `AngelscriptHitResultLibrary.h` / `Bind_FHitResult.cpp` / `AngelscriptDocs.h` 后，未见专门承接“把 `FHitResult` 的 property 文档 authoring surface 从静默回退项升级成 dedicated docs contract”的活跃 Plan。现有 `P2.1` / `P14.3` 已覆盖 helper surface、`Reset()` / `GetPhysMaterial()` 与 runtime 行为，但还没有单独钉住 `FaceIndex` / `PenetrationDepth` / `ImpactPoint` / `Location` / `Normal` / `BoneName` 这类高频字段的文档条目继续缺失时应如何报警。

### Phase 21：`FHitResult` property 文档元数据回归

> 目标：在不扩张新的运行时 helper surface 的前提下，恢复 `FHitResult` property docs 的 authoring path，并用 dedicated automation 固定 `ImpactPoint` / `Location` / `Time` / `Normal` 等字段的说明文本不再静默丢失。

- [ ] **P21.1** 深化 `P2.1` / `P14.3`，恢复 `FHitResult` property 文档条目与 `SCRIPT_PROPERTY_DOCUMENTATION` authoring seam
  - `P2.1` / `P14.3` 已处理 `FHitResult` helper surface 回退与 `PhysMaterial` cleanup，但当前更隐蔽的回归仍未被计划显式承接：`Bind_FHitResult.cpp` 还在注册 property，却没有再向 `FAngelscriptDocs` 喂任何 property 级说明；`AngelscriptDocs.h` 也仍缺少和 Hazelight 一样的 `SCRIPT_PROPERTY_DOCUMENTATION` 宏封装。这样一来，脚本层虽然还能访问 `ImpactPoint`、`Location`、`Normal`、`Time`、`PenetrationDepth` 等字段，却失去了区分语义的内联解释，docs dump / hover / editor 提示都会静默变弱。
  - 本项不重复 `P2.1` 的 helper owner 或 `P14.3` 的 `GetPhysMaterial()` 行为，只把文档 authoring surface 单独拉出来：优先恢复一个稳定、低摩擦的 property-doc 注入入口（可以是重新引入 `SCRIPT_PROPERTY_DOCUMENTATION` 宏，也可以是等价的局部 helper），再把 `FHitResult` 这组高频字段的说明文本重新挂回 docs 数据库；不能继续让 `FAngelscriptDocs` 具备 property API、但最常见的 manual bind 类型没有任何生产用例喂它。
  - 来源：
    - [A] `Documents/AutoPlans/FunctionLibraries_Analysis.md` — “发现 36 / 59：`Bind_FHitResult.cpp` 已不再包含 property 级文档注入，`ImpactPoint` / `Location` / `Normal` / `Time` / `PenetrationDepth` 等字段只剩裸名字，当前也没有任何自动化监控 property 文档条目是否持续产出。”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D10 小结：当前插件与 Hazelight 的差距不是没有 docs dump，而是 `SCRIPT_PROPERTY_DOCUMENTATION` / `SCRIPT_MANUAL_BIND_META` 这类 authoring surface 没有继续显式保留；若要提升 API 参考质量，应优先恢复这些入口。”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “D5/D6：参考实现仍保留 `SCRIPT_PROPERTY_DOCUMENTATION(Binds, Documentation)`，说明 property docs 仍被视为绑定 authoring surface，而不是可有可无的注释残留。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp` L1-L42 — 当前只注册 constructor 和 property，没有 `#include "AngelscriptDocs.h"`，也没有任何 property 文档注入调用。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h` L4-L12、L16-L34 — docs 系统仍保留 `AddUnrealDocumentationForProperty()` / `GetUnrealDocumentationForProperty()` / `GetUnrealPropertyDocumentationCount()`，但宏层只暴露 `AS_DOC` / `SCRIPT_BIND_DOCUMENTATION` / `SCRIPT_GLOBAL_DOCUMENTATION`，没有 property-doc authoring 入口。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h` L16-L91 — `FHitResult` helper surface 仍是当前脚本用户会接触的 active 域，说明 property 文档缺失仍会直接影响同域 API 的可理解性，而不是纯历史 dead code。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocumentationPropertyTests.cpp`
- [ ] **P21.1** 📦 Git 提交：`[FunctionLibraries] Docs: restore FHitResult property documentation`
- [ ] **P21.1-T** 单元测试：锁定 `FHitResult` property 文档条目与缺失项 fallback
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocumentationPropertyTests.cpp`
  - 测试场景：
    - 正常路径：在 editor/文档可用环境下解析 `FHitResult` 类型与 property offset，验证 `ImpactPoint`、`Location`、`Time`、`Normal`、`ImpactNormal`、`PenetrationDepth`、`BoneName` 的文档字符串非空，且至少能区分 `ImpactPoint` vs `Location`、`Normal` vs `ImpactNormal` 这类容易混淆的字段语义。
    - 边界条件：重复初始化 engine scope 或重复查询同一 property 时，`GetUnrealPropertyDocumentationCount()` 与单字段 docs 结果保持稳定，不因多次 bind/lookup 产生重复或丢失；如最终实现仍采用 editor-only 文档注入，测试需显式限定运行配置，避免把 `WITH_EDITOR` 差异误判成契约漂移。
    - 错误路径：对不存在的 property offset、未注入说明的字段、或 unrelated type 查询 `GetUnrealDocumentationForProperty()` 时，必须稳定返回空字符串而不是复用上一条 property 的 stale docs；如果 `Bind_FHitResult.cpp` 再次丢掉 property docs 注入，测试要直接红灯。
  - 测试命名：`Angelscript.TestModule.Engine.Docs.HitResultPropertyDocumentation`、`Angelscript.TestModule.Engine.Docs.PropertyDocumentationFallback`
  - 隔离方式：`FAngelscriptEngineScope` + editor-only docs lookup
- [ ] **P21.1-T** 📦 Git 提交：`[FunctionLibraries] Test: add hit result property documentation coverage`

### 单元测试总览增补（Phase 21）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P21.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocumentationPropertyTests.cpp` | `FHitResult` property docs 注入、字段语义区分、缺失 offset fallback、重复 lookup 稳定性 | `P2` |

### 验收标准补充（Phase 21）

1. `FHitResult` 的关键 property 不再只剩裸名字；`ImpactPoint`、`Location`、`Time`、`Normal`、`ImpactNormal`、`PenetrationDepth`、`BoneName` 至少恢复一组可导出的 property 文档说明。
2. 当前 docs 系统重新拥有可用于 manual bind property 的稳定 authoring seam，不再只能给 function/global variable 写文档，而无法对 `Bind_FHitResult.cpp` 这类 property-heavy bind 施加说明。
3. `AngelscriptDocumentationPropertyTests.cpp` 能在 property docs 再次被删掉、错误复用、或 stale fallback 时直接红灯，而不是继续让 docs dump / hover 质量静默回退。

### 风险与注意事项补充（Phase 21）

1. **不要把 property docs 回归误做成新 API 行为**
   - 本项只恢复文档元数据与导出质量，不应顺手改写 `FHitResult` 的字段名、helper 行为或 binding shape；否则会和 `P2.1` / `P14.3` 的运行时主线重叠。
2. **字段说明文本应固定“语义差异”，不要抄整段引擎文档**
   - 重点是把 `ImpactPoint` vs `Location`、`Normal` vs `ImpactNormal`、`Time` / `PenetrationDepth` 等易混淆点重新变成可搜索、可 hover 的短说明；避免把 docs 修复扩张成大段复制粘贴。
3. **editor-only 文档能力要与 runtime contract 分开验证**
   - 当前 docs 注入与 dump 仍受 `WITH_EDITOR` 影响；测试必须明确自己验证的是 editor 文档 contract，而不是 gameplay runtime 行为，避免配置差异造成假红灯。
