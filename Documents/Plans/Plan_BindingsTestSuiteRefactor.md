# Bindings 测试套件重构计划

## 背景与目标

`Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 目前有 **60+ 个 `*BindingsTests.cpp`、66+ 个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 节点、55+ 段 `int Entry()` 内联脚本**，是测试模块体量最大、命名最混乱、维护成本最高的目录。其中：

- 仅 `AngelscriptTArrayBindingsTests.cpp`（2275 行）+ 配套 `AngelscriptTArrayBindingCoverage.h` 采用了**Coverage Section + Syntax Profile** 的工程化架构，并被 `AngelscriptArraySyntaxCompatBindingsTests.cpp`（37 行）作为孪生测试**复用同一份覆盖矩阵**。
- 仅 `Template/Template_GlobalFunctions.cpp` 与 `TArrayBindingsTests` 在使用 `FASGlobalFunctionInvoker`（基于 `asIScriptContext::SetArgXxx + Execute` 的现代调用基座，覆盖 bool/byte/int/int64/float/double/FString&in/FName&in/FVector&in/`double&out`/struct return 全参数矩阵）。
- 其余 50+ 个 `*BindingsTests.cpp` 仍沿用旧式扁平模式：单文件内 N 个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`，每个 `RunTest` 跑一段大 `int Entry()` 脚本，靠 `if (xxx) return 10; ... if (yyy) return 20;` 区分失败原因。

旧模式的问题：

1. **失败信号差**：`return 80` 一个数字看不出是什么 case 失败，需要回到脚本源码数 `if`。
2. **覆盖面很难加增量用例**：每加一个 case 都要在大脚本里挑空位插 `if/return` 再补一行 `TestEqual`，碰撞概率高。
3. **重复样板**：每个文件都在重写 `BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` + `DiscardModule` 四件套，且模块名命名风格不统一（`ASOptionalCompat` / `ASIntVectorValueTypesCompat` / `ASObjectPtrCompat` …）。
4. **无法多语法/多场景复用**：例如同一份 `TMap` 覆盖矩阵不能像 TArray 那样自动跑两遍（如 const & non-const、handle & value 元素等），都得 copy-paste 重写。
5. **测试 ID 颗粒度极不一致**：`ContainerBindingsTests.cpp` 一个文件 14 个 ID 平铺，`TArrayBindingsTests.cpp` 一个文件只有 1 个 ID 内含 8 段，新人无法判断该按哪种风格扩展。

**目标**：

1. **建立"Bindings Coverage Section + Profile"成为目录默认模式**——把 TArray 工程化架构里通用的部分抽到 `Bindings/Shared/`，使新增 `*BindingsTests.cpp` 直接复用，降低样板。
2. **以 `FASGlobalFunctionInvoker` 替代 `int Entry()` + return code 模式**——失败信号定位到具体 case 名 + 具体参数，而不是 `return 80`。
3. **用更少的测试节点覆盖更全的面**：把"一族能力"合并为单一 Automation ID 内驱动多 Section，断言数量和覆盖矩阵不缩水（实际是扩大），但顶层 ID 不再泛滥。
4. **不破坏现有覆盖**：所有旧脚本段中的具体断言都要在新结构里有对位用例，且整体 `Angelscript.TestModule.Bindings.*` 通过率持平或提升。

## 范围与边界

- **本次 Plan 范围**：
  - 抽取 `Bindings/Shared/` 通用基座（Profile / Section runner / Coverage helpers / 模块名约定）。
  - 改造 **8 个高价值文件**为新模式，并合并相邻同主题文件（详见"影响范围"）。
  - 同步删除/归并冗余 Automation ID，保持 ID 名向后兼容（保留旧 ID 作为别名时另开任务，不在本 Plan 内）。
  - 文档：`Documents/Guides/TestConventions.md` 增补"Bindings Coverage 模式"小节。
- **不在范围**：
  - 不改 `WorldCollisionAsync*BindingsTests` / `WorldCollisionFunctionLibrary*Tests`（涉及 Trace/异步/真实 World，重构需独立调研，单列后续 Plan）。
  - 不改 `*FunctionLibraryTests.cpp` 系列（17 个文件，属 FunctionLibrary 主题，应在 `Plan_FunctionLibrariesCleanup` 完成后再统一）。
  - 不改 `BlueprintCallableReflectiveFallbackTests`（已被 `Plan_UFunctionReflectiveFallbackBinding` 归档覆盖，不重复治理）。
  - 不动 `AngelscriptTArrayBindingsTests` 与 `AngelscriptArraySyntaxCompatBindingsTests`（它们就是参照原型，仅在 P1 抽取 Shared 时按需提升 helper 至公共目录）。
  - 不调整 Automation ID 名（只增不删；旧 ID 名继续可用，避免破坏 `RunTestSuite.ps1` 与 CI 调用）。

## 当前事实状态

| 指标 | 值 |
|------|-----|
| `Bindings/` 下 `*BindingsTests.cpp` 文件数 | **60+**（包含 FunctionLibrary 类一同计入则约 90） |
| `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 节点数 | **66+**（仅按 grep 命中，实际略多） |
| `int Entry()` / `BuildModule` 旧模式文件数 | **55+** |
| 采用 Coverage Section + Profile 架构的文件数 | **1**（`AngelscriptTArrayBindingsTests.cpp`） |
| 使用 `FASGlobalFunctionInvoker` 的文件数 | **2**（`TArrayBindingsTests` + `Template_GlobalFunctions`） |
| 配有 `*Coverage.h` 头文件的文件数 | **1**（`AngelscriptTArrayBindingCoverage.h`） |
| `Bindings/Shared/` 目录是否存在 | **否**（共享 helper 当前散在 `AngelscriptTest/Shared/`） |

### 标杆参照
- 架构标杆：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTArrayBindingsTests.cpp`（Section/Profile/CoverageHelpers）+ `AngelscriptTArrayBindingCoverage.h`
- 调用基座标杆：`Plugins/Angelscript/Source/AngelscriptTest/Template/Template_GlobalFunctions.cpp`（`FASGlobalFunctionInvoker` 全参数矩阵示范）
- Helper 当前位置：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGlobalFunctionInvoker.h`（已通用，无需移动）
- 测试规范文档：`Documents/Guides/TestConventions.md`（Phase 7.1 需要补章节）

## 设计要点

### Bindings Coverage 公共基座（拟落点 `Bindings/Shared/`）

```
Bindings/Shared/
  AngelscriptBindingsCoverage.h         // EBindingsProfile + RunBindingsCoverageSections 总入口
  AngelscriptBindingsCoverage.cpp       // Profile 注册、文本替换、模块名生成、Section 调度
  AngelscriptBindingsAssertions.h       // ExpectGlobalInt / ExpectGlobalIntsAtLeast / ExecuteFunctionExpectingScriptException 等通用断言
  AngelscriptBindingsModuleBuilder.h    // BuildCoverageModule / 自动 ModulePrefix / 自动 DiscardModule on scope exit
```

`EBindingsProfile` 不锁死语法二选一（如 TArray 的 Explicit/Shorthand），而是开放为"主题 + 变体"二维：

```cpp
struct FBindingsCoverageProfile {
    const TCHAR* Theme;       // "Container" / "Console" / "GameplayTag" ...
    const TCHAR* Variant;     // "" / "ConstRef" / "Shorthand" ...
    const TCHAR* ModulePrefix;// "ASContainer" / "ASContainer_ConstRef"
    const TCHAR* CasePrefix;  // 进 AddInfo / Module 名的可读前缀
    const TCHAR* LogCategory;
};

bool RunBindingsCoverageSections(
    FAutomationTestBase& Test,
    FAngelscriptEngine& Engine,
    const FBindingsCoverageProfile& Profile,
    TArrayView<const TFunction<bool(...)>> Sections);
```

新文件的标准结构：

```cpp
// AngelscriptXxxBindingsTests.cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAngelscriptXxxBindingsTest,
    "Angelscript.TestModule.Bindings.Xxx",
    EditorContext | EngineFilter)

bool FAngelscriptXxxBindingsTest::RunTest(const FString&)
{
    auto& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
    ASTEST_BEGIN_SHARE_CLEAN
    ON_SCOPE_EXIT { ResetSharedCloneEngine(Engine); };

    static const FBindingsCoverageProfile Profile{ TEXT("Xxx"), TEXT(""),
        TEXT("ASXxx"), TEXT("Xxx"), TEXT("XxxBindings") };

    bool bPassed = true;
    bPassed &= RunXxxConstructionSection (*this, Engine, Profile);
    bPassed &= RunXxxArithmeticSection   (*this, Engine, Profile);
    bPassed &= RunXxxErrorPathSection    (*this, Engine, Profile);
    ASTEST_END_SHARE_CLEAN
    return bPassed;
}
```

每个 `Section` 内部使用 `FASGlobalFunctionInvoker` 而不是 `int Entry()` 模式，**每个 case 一次小调用 + 立即一次 `TestEqual`/`TestTrue`**，保证失败信号包含具体函数声明和参数。

### "覆盖面不缩水"的硬约束

为避免重构中漏掉旧断言，**每个被改造的文件都要先做 case 清单 dump**：把旧脚本里所有 `if (...) return N;` 与 `TestEqual(..., Result, M)` 全部列成 markdown 表格放进伴侣目录 `Plan_BindingsTestSuiteRefactor/`。改造完成后逐项打勾覆盖。

### 命名一致性

- Automation ID：保留旧 ID，以"主入口 ID"形式驱动多 Section；如旧文件有多个 ID（如 `ContainerBindingsTests` 14 个），保留全部 ID 名，但每个 ID 内部都改用新基座，仅"该 ID 关心的 Section"会被调用——即 ID 名是"当前 Profile/Section 集合"的**别名**而非**节点**。
- 模块名：统一 `AS<Theme>[_<Variant>][_<Section>]`，全部由 `BuildCoverageModule` 自动拼装，禁止文件内裸写 `BuildModule(..., "ASFooBar", ...)`。

## 影响范围

本计划属于**测试体系重构**，受影响文件清单按操作类型 + 主题分组列出。

### 操作类型定义

- **基座抽取**：把 `AngelscriptTArrayBindingsTests.cpp` 中可复用的 helper（`BuildCoverageModule` / `TraceCase` / `ExpectGlobalInt*` / `ExecuteFunctionExpectingScriptException` / `ExecuteFunctionReturningScriptArray` / `CompileSummaryContainsDiagnosticMessage`）泛化为不绑定 `EArraySyntaxCoverage` 的形态，落到 `Bindings/Shared/`。
- **文件改造**：把旧的 `int Entry() + ExecuteIntFunction` 模式重写为 `Section + FASGlobalFunctionInvoker`，原 case 清单 100% 翻新，可补增量 case（例如 ToString / Format / 边界异常）。
- **文件合并**：相邻同主题、且每个文件 < 100 行的小测试合并为同一文件的多个 Section（不删 ID，仅合并实现）。
- **TArray 复用回写**：`AngelscriptTArrayBindingCoverage.h` 改为 include `AngelscriptBindingsCoverage.h`，保留 TArray 专用 `EArraySyntaxCoverage` 在自身目录或迁移到 Shared，由 P1 决定。

### 按优先级分组的目标文件清单

**Phase 2（高密度文件，单文件 ≥ 5 个 ID 或 ≥ 15KB，价值最高）**：

- `AngelscriptContainerBindingsTests.cpp`（21KB，14 个 ID，38 段 `int Entry()`）—— 文件改造 + ID 内部 Section 化
- `AngelscriptGameplayTagBindingsTests.cpp`（30KB，8 个 ID，24 段）—— 文件改造
- `AngelscriptFileAndDelegateBindingsTests.cpp`（23KB，7 个 ID，20 段）—— 文件改造（拆分 File 与 Delegate 两组 Section，仍同文件）
- `AngelscriptClassBindingsTests.cpp`（21KB，7 个 ID，20 段）—— 文件改造
- `AngelscriptCompatBindingsTests.cpp`（13KB，5 个 ID，16 段）—— 文件改造
- `AngelscriptConsoleBindingsTests.cpp`（16KB，5 个 ID，19 段）—— 文件改造
- `AngelscriptEnhancedInputBindingsTests.cpp`（13KB，8 个 ID，21 段）—— 文件改造
- `AngelscriptUserWidgetBindingsTests.cpp`（15KB，4 个 ID）—— 文件改造

**Phase 3（中密度文件，2~4 个 ID，6~13KB）**：

- `AngelscriptUtilityBindingsTests.cpp`、`AngelscriptCoreMiscBindingsTests.cpp`、`AngelscriptMathAndPlatformBindingsTests.cpp`、`AngelscriptEngineBindingsTests.cpp`、`AngelscriptDataTableBindingsTests.cpp`、`AngelscriptDateTimeBindingsTests.cpp`、`AngelscriptTimespanBindingsTests.cpp`、`AngelscriptCollisionParamsBindingsTests.cpp`、`AngelscriptCollisionValueBindingsTests.cpp`、`AngelscriptAssetRegistryBindingsTests.cpp`、`AngelscriptContainerCompareBindingsTests.cpp`、`AngelscriptIteratorBindingsTests.cpp`、`AngelscriptJsonBindingsTests.cpp`、`AngelscriptJsonObjectConverterBindingsTests.cpp`、`AngelscriptGuidBindingsTests.cpp`、`AngelscriptQuatBindingsTests.cpp`、`AngelscriptTransformBindingsTests.cpp`、`AngelscriptRandomStreamBindingsTests.cpp`

**Phase 4（小文件，1 个 ID 或 < 6KB，候选合并）**：

- 合并组 A — Console 周边：`ConsoleCommandArgumentBindingsTests` + `ConsoleCommandErrorBindingsTests` + `ConsoleCommandLifecycleBindingsTests` + `ConsoleVariableIdentityTests`（如未在 Bindings/，跳过）→ 主入口 `ConsoleBindingsTests`，前者 4 个文件改为同一文件多 Section（保留各自原 ID 名）
- 合并组 B — Collision 值类：`CollisionProfileBindingsTests` + `CollisionValueBindingsTests` 已在 Phase 3，组 B 仅在 Phase 3 后回看是否再合并
- 单文件改造（不合并）：`ObjectBindingsTests`、`IntVectorBindingsTests`、`PrimitiveComponentBindingsTests`、`MemoryReaderBindingsTests`、`StringTableBindingsTests`、`TextFormattingBindingsTests`、`EnumBindingsTests`、`GASValueBindingsTests`、`GameInstanceLocalPlayerBindingsTests`、`GameplayTagEmptyContractTests`、`NativeEngineBindingsTests`、`WorldBindingsTests`、`GlobalBindingsTests`、`DebugBindingsTests`

**Phase 5（暂不改造，单列说明）**：

- `WorldCollisionBindingsTests.cpp` / `WorldCollisionAsyncBindingsTests.cpp` / `WorldCollisionAsyncSweepBindingsTests.cpp` —— 涉及 World/异步/Trace，需独立 Plan
- `*FunctionLibraryTests.cpp`（17 个）—— 等 `Plan_FunctionLibrariesCleanup` 推进后再统一
- `BlueprintCallableReflectiveFallbackTests.cpp` —— 已由归档 Plan 治理，不重复

## 分阶段执行计划

### Phase 1 — 抽取 Bindings Coverage 公共基座

> 目标：把 TArray 文件里可复用的工程化架构提升到 `Bindings/Shared/`，让后续 Phase 2-4 改造可以"include 即用"，并把 TArray 自身切到新基座以做"自举"验证。

- [ ] **P1.1** 调研并整理 TArray 文件中可复用的公共部分
  - 通读 `AngelscriptTArrayBindingsTests.cpp` 第 28-450 行的 `Private` 命名空间，把"与 `EArraySyntaxCoverage` 解耦"的部分（`MakeModuleName`、`BuildCoverageModule`、`TraceTArrayCase`、`ExpectGlobalInt*`、`ExecuteFunctionExpectingScriptException`、`ExecuteFunctionReturningScriptArray`、`CompileSummaryContainsDiagnosticMessage`、`FExpectedGlobalInt`、`FExpectedGlobalIntAtLeast`）列出来，分清哪些是"TArray 专属"哪些是"Bindings 通用"
  - 输出 `Plan_BindingsTestSuiteRefactor/SharedExtractMatrix.md` 一张表，列每个 helper 的复用层级（Bindings 通用 / TArray 专属 / 边界），用作 P1.2 的执行清单
- [ ] **P1.1** 📦 Git 提交：`[Docs/Plans] Docs: dump TArray bindings shared extract matrix`

- [ ] **P1.2** 新增 `Bindings/Shared/AngelscriptBindingsCoverage.{h,cpp}`、`AngelscriptBindingsAssertions.h`、`AngelscriptBindingsModuleBuilder.h`
  - 引入 `FBindingsCoverageProfile`（Theme/Variant/ModulePrefix/CasePrefix/LogCategory 五字段），不绑定具体语法枚举
  - 把 P1.1 标记为"通用"的 helper 全部以模板/普通函数形式落入新文件，签名以 `const FBindingsCoverageProfile&` 替代 `EArraySyntaxCoverage`
  - 提供 `FCoverageModuleScope` RAII：构造时 `BuildModule`，析构时 `Engine.DiscardModule(ModuleName)`，避免每个 Section 末尾散写 `DiscardModule`
  - 不引入 TArray 专属枚举（`EArraySyntaxCoverage` 留在 TArray 自身的头文件中，由 TArray 文件 include Shared 后自行特化）
  - 在 `Bindings/Shared/README.md` 写明用法、最小示例、与旧 `int Entry()` 模式的对照
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings/Shared] Feat: extract Bindings coverage base from TArray tests`

- [ ] **P1.3** 让 `AngelscriptTArrayBindingsTests.cpp` 切到新基座（自举验证）
  - `AngelscriptTArrayBindingCoverage.h` 改为 include `AngelscriptBindingsCoverage.h`，保留 `EArraySyntaxCoverage` 与 `RunTArrayBindingCoverageSections`
  - TArray 文件内部把对 `Private` helper 的调用全部替换为 `Bindings::Shared::*` 同名 API，删除已被提升的 helper 副本
  - 验证 `Angelscript.TestModule.Bindings.TArray` 与 `Angelscript.TestModule.Bindings.ArraySyntaxCompat` 两个测试**全绿且断言数量与基线一致**（用 `RunTests.ps1 -ReportEvents` 对比）
- [ ] **P1.3** 📦 Git 提交：`[Tests/Bindings] Refactor: bootstrap TArray tests on shared bindings coverage base`

- [ ] **P1.4** 新增 `Bindings/Shared/AngelscriptBindingsExampleSection.h` 一段 50 行示例 Section
  - 用 `FASGlobalFunctionInvoker` 调用一段最简脚本（`int Echo(int)`），覆盖：模块构建、单 case 调用、断言、异常路径、struct return
  - 这是后续 Phase 2-4 抄改的参考骨架，等价于"Bindings 版的 Template_GlobalFunctions"
- [ ] **P1.4** 📦 Git 提交：`[Tests/Bindings/Shared] Test: add example section as bindings refactor template`

### Phase 2 — 高密度文件改造（8 个）

> 目标：把单文件 ≥ 5 个 ID 或 ≥ 15KB 的高价值 Bindings 测试全部切到新基座，覆盖面只增不减。每个文件先 dump 旧 case 清单做 baseline，改造后逐项打勾。

- [ ] **P2.1** 改造 `AngelscriptContainerBindingsTests.cpp`
  - 它是目录里 ID 最多（14 个）的旧式文件，覆盖 TOptional/TSet/TMap/Foreach/MapForeach 等，是验证"多 ID 共用一文件"重构能力的最佳用例
  - dump 现有 14 个 `RunTest` 的全部 `if (xxx) return N;` 到 `Plan_BindingsTestSuiteRefactor/ContainerCaseInventory.md`
  - 按 Theme 拆 5 组 Section：`OptionalCompat`、`SetCompat`、`MapCompat`、`Foreach{Array,Set,Map}`、`MapKeyValuePairing`
  - 每个旧 ID 的 `RunTest` 改为"创建对应 Profile + 调用对应 Section 子集"，旧 ID 名不变，旧 case 100% 在新 Section 中翻新
  - 用 `FASGlobalFunctionInvoker` 替换 `ExecuteIntFunction` + return code 模式
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild ContainerBindingsTests on shared coverage sections`

- [ ] **P2.2** 改造 `AngelscriptGameplayTagBindingsTests.cpp`
  - 8 个 ID，24 段 `int Entry()`，覆盖 Tag 创建/比较/容器/查询/序列化等
  - 切 6 组 Section：`Construction`、`Comparison`、`ContainerOps`、`Query`、`Serialization`、`ErrorPaths`
  - 注意 Tag 测试普遍依赖 `UGameplayTagsManager` 全局状态，重构时确认新基座的 `FCoverageModuleScope` 不会破坏单测顺序
- [ ] **P2.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild GameplayTagBindingsTests on shared coverage sections`

- [ ] **P2.3** 改造 `AngelscriptFileAndDelegateBindingsTests.cpp`
  - File 与 Delegate 两个完全不同的主题塞在同一文件，但都各 7 个 ID 共 20 段；保持单文件，但用两个 Profile（`FileBindings` + `DelegateBindings`）和两组 Section runner 分隔
  - File 组 Section：`PathOps`、`IO`、`ErrorPaths`；Delegate 组 Section：`SingleBind`、`Multicast`、`UnbindLifecycle`、`InvokeWithReturn`
  - 旧 ID 名分别映射到对应 Profile + Section 子集
- [ ] **P2.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild File/Delegate tests on shared coverage sections`

- [ ] **P2.4** 改造 `AngelscriptClassBindingsTests.cpp`
  - 7 个 ID，20 段；覆盖 `UClass` / `TSubclassOf` / `Cast` / `IsChildOf` / 类元数据访问
  - Section 拆分：`StaticClass`、`SubclassOf`、`Cast`、`Hierarchy`、`MetaAccess`
  - 注意 `UClass` 类测试涉及很多 reflective 边界，`FASGlobalFunctionInvoker.ReadReturnStruct<UClass*>` 路径需在 P1.4 示例里验证可行
- [ ] **P2.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild ClassBindingsTests on shared coverage sections`

- [ ] **P2.5** 改造 `AngelscriptCompatBindingsTests.cpp` + `AngelscriptConsoleBindingsTests.cpp`
  - 两个文件分别 5 个 ID / 16 段、5 个 ID / 19 段，结构对称，可放同一 Phase 子任务并行处理
  - Compat 文件覆盖 `TObjectPtr` / `TSoftObjectPtr` / `TWeakObjectPtr` / `TLazyObjectPtr` / 隐式转换；Section：`StrongHandles`、`SoftHandles`、`WeakHandles`、`Conversion`
  - Console 文件覆盖 `IConsoleManager` / `FConsoleVariable` / Find/Register/Unregister；Section：`Variable`、`Command`、`Lifecycle`、`ErrorPaths`
- [ ] **P2.5** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Compat and Console tests on shared coverage sections`

- [ ] **P2.6** 改造 `AngelscriptEnhancedInputBindingsTests.cpp` + `AngelscriptUserWidgetBindingsTests.cpp`
  - EnhancedInput：8 个 ID / 21 段，Section：`ActionValue`、`Mapping`、`Modifier`、`Trigger`、`PlayerInput`
  - UserWidget：4 个 ID，Section：`Construction`、`Layout`、`Lifecycle`、`InputHandling`
  - 两者都涉及 `UWorld`/`ULocalPlayer` 等较重对象，确认新基座的 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 路径仍能跑过（沿用 TArray 的 share-clean 模式）
- [ ] **P2.6** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild EnhancedInput and UserWidget tests on shared coverage sections`

- [ ] **P2.7** Phase 2 收尾验证
  - 运行 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings`，对比 P0 基线（断言数 / 失败数 / 用时）
  - 8 个文件改造后，整体 Bindings 测试套件断言数预期 +20% ~ +40%（来自旧"return code 合并断言"被拆开），失败数 0，用时浮动 < 10%
- [ ] **P2.7** 📦 Git 提交：`[Tests/Bindings] Test: phase-2 high-density file refactor regression`

### Phase 3 — 中密度文件改造（约 18 个）

> 目标：把 2~4 个 ID、6~13KB 的中等规模文件批量切换。这一阶段强调"流水化套用 P1.4 示例骨架"，每个文件预计 0.5~1 工时。

- [ ] **P3.1** 切换数学/几何相关：`MathAndPlatformBindingsTests`、`QuatBindingsTests`、`TransformBindingsTests`、`GuidBindingsTests`、`RandomStreamBindingsTests`
  - 数学类共性：纯值类型 + 大量 `FMath::IsNearlyEqual` 比较，最适合 `FASGlobalFunctionInvoker.CallAndReturn<double>` 模式
  - Section 拆分按类型：每类 1~2 个 Section（Construction / Arithmetic / Conversion）
- [ ] **P3.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild math-family bindings tests on shared coverage sections`

- [ ] **P3.2** 切换时间/资源相关：`DateTimeBindingsTests`、`TimespanBindingsTests`、`DataTableBindingsTests`、`AssetRegistryBindingsTests`
  - DataTable / AssetRegistry 涉及 UAssetManager，新基座下注意 `FAngelscriptEngineScope` 的取用边界
- [ ] **P3.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild time and asset bindings tests on shared coverage sections`

- [ ] **P3.3** 切换容器/迭代相关：`ContainerCompareBindingsTests`、`IteratorBindingsTests`、`JsonBindingsTests`、`JsonObjectConverterBindingsTests`
  - JSON 系列测试常见的"构造对象 → 序列化 → 反序列化 → 比较"的 4 步骤天然适合 4 个 Section
- [ ] **P3.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild container-compare and json bindings tests on shared coverage sections`

- [ ] **P3.4** 切换 Engine/Utility/Misc 相关：`EngineBindingsTests`、`UtilityBindingsTests`、`CoreMiscBindingsTests`、`CollisionParamsBindingsTests`、`CollisionValueBindingsTests`
  - Collision 值类型类（`FHitResult` / `FOverlapResult` 等）注意只覆盖**纯值构造与字段访问**，不动 World 物理 Trace（属 Phase 5 单独 Plan）
- [ ] **P3.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild engine and utility bindings tests on shared coverage sections`

- [ ] **P3.5** Phase 3 收尾回归
  - 运行 `Angelscript.TestModule.Bindings.*` 全前缀，对比 Phase 2 收尾基线
- [ ] **P3.5** 📦 Git 提交：`[Tests/Bindings] Test: phase-3 mid-density refactor regression`

### Phase 4 — 小文件改造与必要合并

> 目标：单 ID / < 6KB 的小文件，按"流水化改造 + 必要时多文件合并"两种处理。合并仅做实现合并，**不删 Automation ID**。

- [ ] **P4.1** 合并组 A：Console 周边四文件
  - `ConsoleCommandArgumentBindingsTests.cpp`、`ConsoleCommandErrorBindingsTests.cpp`、`ConsoleCommandLifecycleBindingsTests.cpp`、`ConsoleVariableIdentityTests.cpp` 四个文件代码量都 < 9KB，且都属于 Console 主题
  - 实现合并到 `AngelscriptConsoleBindingsTests.cpp`（已在 P2.5 改造）作为新增 Section（`CommandArgument` / `CommandError` / `CommandLifecycle` / `VariableIdentity`），原四个文件**仅保留 1 行 `#include "AngelscriptConsoleBindingsTests_Sections.h"` 与 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + 调用对应 Section** 的薄壳，确保 ID 继续注册
  - 等价目录化复用：参考 `AngelscriptArraySyntaxCompatBindingsTests.cpp`（37 行薄壳）的写法
- [ ] **P4.1** 📦 Git 提交：`[Tests/Bindings] Refactor: merge console-family small tests into shared sections`

- [ ] **P4.2** 单文件流水化改造（约 14 个）
  - 文件清单：`ObjectBindingsTests`、`IntVectorBindingsTests`、`PrimitiveComponentBindingsTests`、`MemoryReaderBindingsTests`、`StringTableBindingsTests`、`TextFormattingBindingsTests`、`EnumBindingsTests`、`GASValueBindingsTests`、`GameInstanceLocalPlayerBindingsTests`、`GameplayTagEmptyContractTests`、`NativeEngineBindingsTests`、`WorldBindingsTests`、`GlobalBindingsTests`、`DebugBindingsTests`
  - 每个文件按 P1.4 示例骨架改写，单 Section 即可（这些文件目前就只有 1 个 ID）
  - 不合并到其他文件（主题独立或体量已合理）
- [ ] **P4.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild remaining single-id bindings tests on shared coverage sections`

- [ ] **P4.3** Phase 4 收尾回归
  - 全量 `Angelscript.TestModule.Bindings.*` 通过、断言数较 P0 基线非降低
- [ ] **P4.3** 📦 Git 提交：`[Tests/Bindings] Test: phase-4 small-file refactor regression`

### Phase 5 — 文档与索引同步

> 目标：让新基座对所有未来新增 Bindings 测试的作者可见，且本 Plan 在项目索引中可被检索。

- [ ] **P5.1** 在 `Documents/Guides/TestConventions.md` 新增"Bindings Coverage 模式"章节
  - 内容：何时使用 Section + Profile / `FASGlobalFunctionInvoker` 与 `int Entry()` 的取舍 / 模块名约定 / 失败信号怎么写 / 示例代码引用 `AngelscriptBindingsExampleSection.h`
  - 在 `Plugins/Angelscript/AGENTS.md` 与 `AngelscriptTest/AGENTS.md` 加一句"`Bindings/` 默认走 Coverage Section 基座，旧 `int Entry()` 模式仅维护期保留，不接受新增"
- [ ] **P5.1** 📦 Git 提交：`[Docs/Guides] Docs: document Bindings coverage section pattern`

- [ ] **P5.2** 把 `Plan_BindingsTestSuiteRefactor.md` 登记进 `Plan_OpportunityIndex.md`
  - 落入"二、测试增强 → 2.1 已有 Plan"表格，状态写"未开始"或对应阶段
  - 同时与 `Plan_NativeAngelScriptCoreTestRefactor.md`（同属测试重构）做一句"非冲突，主题不同（Native Core vs Bindings）"标注
- [ ] **P5.2** 📦 Git 提交：`[Docs/Plans] Docs: register bindings test refactor plan in opportunity index`

- [ ] **P5.3** 完工后按 `Plan.md` 归档规则评估归档
  - 所有 Phase 通过后，在文档顶部补 `归档状态` / `归档日期` / `完成判断` / `结果摘要`
  - 同步 `Documents/Plans/Archives/README.md`
- [ ] **P5.3** 📦 Git 提交：`[Docs/Plans] Chore: archive bindings test refactor plan`

## 验收标准

1. `Bindings/Shared/` 下基座 4 文件就位，且 `AngelscriptTArrayBindingsTests` 通过 include Shared 而不是自含 helper 形式工作（自举通过 = 设计自洽）。
2. Phase 2-4 的所有目标文件全部切到 `Section + FBindingsCoverageProfile + FASGlobalFunctionInvoker` 模式，**单文件内不再出现"`int Entry()` 内部用 `if/return N` 区分失败"模式**（grep `return 1[0-9][0-9]` 在改造后的文件应为 0）。
3. 整体 `Angelscript.TestModule.Bindings.*` 通过率 100%（与 P0 基线一致）；**断言总数较基线 ≥ +20%**（来自旧 return-code 合并被拆开），用时浮动 ≤ 10%。
4. 每个 Phase 收尾的回归测试以 `Tools\RunTestSuite.ps1` 或 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings` 形式跑通，输出 events 报告对比基线。
5. 新增/合并文件中，所有模块名都通过 `BuildCoverageModule` 自动派生，文件内裸写 `BuildModule(..., "ASXxx", ...)` 数量为 0。
6. `Documents/Guides/TestConventions.md` 与 `AGENTS.md` 已登记新模式，且 `AngelscriptBindingsExampleSection.h` 是文档引用的唯一示例点。
7. `Plan_OpportunityIndex.md` 中本 Plan 状态被准确标注；P5.3 归档后 `Archives/README.md` 同步更新。

## 风险与注意事项

### 风险

1. **Section 内大量小调用 vs 单段大脚本的性能差异**：把"一段 50 行 `int Entry()` 一次 Execute"拆成"50 个 case 各自一次 Execute"会触发 50 次 `asIScriptContext::Prepare/Execute`，单测耗时可能增加。
   - **缓解**：基座层提供"批量 case + 共享 Context"的可选模式（`FBatchInvoker`：一次 Prepare + 多次 SetArg/Execute/Reset，类型同质场景下使用）；性能样本以 P2.1 改造结果为准，若用时 > 基线 +10% 即在基座补 BatchInvoker 后回填。
2. **case 清单 dump 漏掉边界断言**：旧 `int Entry()` 里有大量隐式假设（如"上一行 `Add(x)` 后 `Num() == 1`" 等），P2 / P3 改造时若漏掉会造成"测试通过但覆盖面下降"。
   - **缓解**：每个改造文件强制先 dump `Plan_BindingsTestSuiteRefactor/<File>CaseInventory.md`，PR/提交里同时附 dump 文档；review 时按"旧文件 grep 行数 vs 新 Section 断言数"做下界校验（新 ≥ 旧）。
3. **`FASGlobalFunctionInvoker` 当前覆盖参数类型有限**：`Template_GlobalFunctions.cpp` 仅示范了 bool/byte/int/int64/float/double/FString&in/FName&in/FVector&in/`double&out`/struct return；遇到 UObject*/TArray&in/TMap&in 等可能要补 Invoker 能力。
   - **缓解**：P1.4 示例 Section 即把 `UObject*` / `TArray<int>&in` / `TMap<FName, int>&in` 三种典型补到 Invoker；如改造中遇到额外类型，在 Phase 2 子任务内"先补 Invoker 能力 → 再改文件"，不在文件内绕过基座。
4. **share-clean 引擎在 Section 之间的状态污染**：当前 TArray 测试在所有 Section 跑完后才 `ResetSharedCloneEngine`，如果某个新 Section 注册的 ASClass 与下一个 Section 同名就会冲突。
   - **缓解**：基座的 `FCoverageModuleScope` RAII 在析构时强制 `DiscardModule`；模块名一律带 `<Theme>_<Section>` 前缀，碰撞概率降至零。
5. **Phase 4.1 的"薄壳文件"是否反而增加阅读成本**：`AngelscriptArraySyntaxCompatBindingsTests.cpp` 已经是 37 行薄壳，新人第一眼可能困惑"实现在哪里"。
   - **缓解**：薄壳文件顶部强制注释一段 1~3 行说明 `// 测试实现在 AngelscriptConsoleBindingsTests_Sections.h，本文件仅注册 Automation ID`，并在 `TestConventions.md` 章节里图示该模式。

### 已知行为变化

1. **Automation ID 不删但行为重新分配**：旧 ID 名继续存在，但其实际跑的脚本已替换为新 Section。如有外部脚本/CI 通过 `AddExpectedError` 匹配旧 ID 内的特定错误消息，需要同步调整。
   - 影响范围：本仓内未发现；外部调用方需自行 sweep。
2. **断言数大幅上升导致 `RunAutomationTests` 报告变长**：`Tools\GetAutomationReportSummary.ps1` 当前以"通过/失败计数"为主，断言数变化不影响 summary，但 raw events 文件体积会增大。
   - 影响范围：CI artifact 容量统计需要更新阈值，无功能影响。
3. **测试单次运行时长上升 5%~10%**（来自更多 Prepare/Execute 调用）。
   - 影响文件：所有改造文件；P2.7 / P3.5 / P4.3 的回归任务负责采样实测，> 10% 触发风险 1 的 BatchInvoker 缓解路径。
