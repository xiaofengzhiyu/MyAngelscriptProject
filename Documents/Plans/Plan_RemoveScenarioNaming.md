# Plan: 移除测试框架中的 Scenario 命名

## 状态

- **优先级**: P2
- **状态**: 未开始
- **关联**: TestConventions.md、TESTING_GUIDE.md、Plugin AGENTS.md

## 背景

"Scenario" 不是业界标准的测试术语。项目中将需要 UE 对象生命周期（World/Actor/Component）的功能性测试统称为 Scenario，但这层命名是冗余的——目录本身（`Actor/`、`Interface/`、`Component/`）已经表达了测试主题，每个测试就是该模块的功能性测试。

移除 "Scenario" 关键词后，测试直接按模块做功能性测试（Functional Test），命名更简洁、更符合行业惯例。

## 影响范围审计

| 类别 | 数量 | 示例 |
|------|------|------|
| **文件名含 Scenario** | 6 个 .cpp + 1 个 .h | `AngelscriptComponentScenarioTests.cpp`、`AngelscriptScenarioTestUtils.h` |
| **脚本模块名（ModuleName）** | ~100+ 处 | `ScenarioActorBeginPlay`、`ScenarioInterfaceImplementBasic` |
| **工具命名空间** | 1 处 | `AngelscriptScenarioTestUtils` |
| **C++ 类名/变量名** | ~50+ 处 | `FAngelscriptScenarioActorBeginPlayTest`、`CompileScriptModule` 参数 |
| **文档引用** | 3+ 文件 | `TestConventions.md`、`TESTING_GUIDE.md`、`AGENTS.md` |

## 迁移策略

### 核心原则

- **不改 Automation 测试路径前缀**（`Angelscript.TestModule.Actor.BeginPlay` 不含 Scenario，无需改）
- **文件名重命名**: `*ScenarioTests.cpp` → `*Tests.cpp`（如已有同名则合并或加更精确的后缀）
- **模块名简化**: `ScenarioActorBeginPlay` → `ActorBeginPlay` 或 `TestActorBeginPlay`
- **命名空间**: `AngelscriptScenarioTestUtils` → `AngelscriptTestUtils` 或直接合入 `AngelscriptTestSupport`
- **分批执行**，每批一个主题目录，确保测试全过后再进下一批

## Phase 1: 工具层重命名（最小影响，最大收益）

### P1.1 重命名 `AngelscriptScenarioTestUtils.h`

- [ ] `AngelscriptScenarioTestUtils.h` → `AngelscriptFunctionalTestUtils.h`
- [ ] `namespace AngelscriptScenarioTestUtils` → `namespace AngelscriptFunctionalTestUtils`
- [ ] 在旧文件位置放一个 `#include` 转发头，避免外部分支冲突
- [ ] 更新以下 **66 个文件**中的 `#include` 和 `using namespace` 引用：

**Template (3)**
- `Template/Template_WorldTick.cpp`
- `Template/Template_BlueprintWorldTick.cpp`
- `Template/Template_Blueprint.cpp`

**Actor (4)**
- `Actor/AngelscriptScriptSpawnedActorOverrideTests.cpp`
- `Actor/AngelscriptActorPropertyTests.cpp`
- `Actor/AngelscriptActorLifecycleTests.cpp`
- `Actor/AngelscriptActorInteractionTests.cpp`

**Interface (9)**
- `Interface/AngelscriptInterfaceNativeTests.cpp`
- `Interface/AngelscriptInterfaceNativeInheritedChildSurfaceTests.cpp`
- `Interface/AngelscriptInterfaceNativeBridgeTests.cpp`
- `Interface/AngelscriptInterfaceLifecycleTests.cpp`
- `Interface/AngelscriptInterfaceImplementTests.cpp`
- `Interface/AngelscriptInterfaceDeclareTests.cpp`
- `Interface/AngelscriptInterfaceCppBridgeTests.cpp`
- `Interface/AngelscriptInterfaceCastTests.cpp`
- `Interface/AngelscriptInterfaceAdvancedTests.cpp`

**Inheritance (1)**
- `Inheritance/AngelscriptInheritanceScenarioTests.cpp`

**HotReload (4)**
- `HotReload/AngelscriptHotReloadVersionChainTests.cpp`
- `HotReload/AngelscriptHotReloadScenarioTests.cpp`
- `HotReload/AngelscriptHotReloadLiteralAssetTests.cpp`
- `HotReload/AngelscriptHotReloadLifecycleTests.cpp`

**GC (1)**
- `GC/AngelscriptGCScenarioTests.cpp`

**Examples (1)**
- `Examples/AngelscriptScriptExampleCoverageTests.cpp`

**Delegate (1)**
- `Delegate/AngelscriptDelegateScenarioTests.cpp`

**Learning/Runtime (8)**
- `Learning/Runtime/AngelscriptLearningUEBridgeTraceTests.cpp`
- `Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp`
- `Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp`
- `Learning/Runtime/AngelscriptLearningInterfaceDispatchTraceTests.cpp`
- `Learning/Runtime/AngelscriptLearningGCTraceTests.cpp`
- `Learning/Runtime/AngelscriptLearningDelegateBridgeTraceTests.cpp`
- `Learning/Runtime/AngelscriptLearningComponentHierarchyTraceTests.cpp`
- `Learning/Runtime/AngelscriptLearningBlueprintSubclassTraceTests.cpp`

**ClassGenerator (16)**
- `ClassGenerator/AngelscriptScriptClassStructureTests.cpp`
- `ClassGenerator/AngelscriptScriptClassShapeTests.cpp`
- `ClassGenerator/AngelscriptScriptClassCreationTests.cpp`
- `ClassGenerator/AngelscriptLiteralAssetPostInitTests.cpp`
- `ClassGenerator/AngelscriptInterfaceDispatchBridgeTests.cpp`
- `ClassGenerator/AngelscriptASFunctionWorldContextTests.cpp`
- `ClassGenerator/AngelscriptASFunctionProcessEventTests.cpp`
- `ClassGenerator/AngelscriptASFunctionOptimizedCallTests.cpp`
- `ClassGenerator/AngelscriptASFunctionMetadataTests.cpp`
- `ClassGenerator/AngelscriptASFunctionDispatchTests.cpp`
- `ClassGenerator/AngelscriptASClassTickSettingsTests.cpp`
- `ClassGenerator/AngelscriptASClassReplicationTests.cpp`
- `ClassGenerator/AngelscriptASClassReferenceSchemaTests.cpp`
- `ClassGenerator/AngelscriptASClassObjectConstructionTests.cpp`
- `ClassGenerator/AngelscriptASClassHelperTests.cpp`
- `ClassGenerator/AngelscriptASClassConstructionContextTests.cpp`
- `ClassGenerator/AngelscriptASClassComponentMetadataTests.cpp`
- `ClassGenerator/AngelscriptASClassComponentConstructionTests.cpp`
- `ClassGenerator/AngelscriptASClassActorConstructionTests.cpp`

**Blueprint (2)**
- `Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp`
- `Blueprint/AngelscriptBlueprintSubclassActorTests.cpp`

**Component (1)**
- `Component/AngelscriptComponentScenarioTests.cpp`

**Subsystem (1)**
- `Subsystem/AngelscriptSubsystemScenarioTests.cpp`

**Shared (1)**
- `Shared/AngelscriptScenarioTestUtils.h` （源文件本身）

**文档 (1)**
- `TESTING_GUIDE.md`

> 注：以上共 **66 个文件**，P1.1 可通过批量 sed/replace-in-files 自动化完成，全部修改内容仅限 `#include` 路径和 `using namespace` 行。

### P1.2 更新文档

- [ ] `TestConventions.md`: 移除 "Scenario" 术语，改用 "Functional Test" / "功能性测试"
- [ ] `TESTING_GUIDE.md`: 同步更新示例和说明
- [ ] `Plugins/Angelscript/AGENTS.md`: 移除 "不要在 Automation 路径里重复追加 Scenario" 条目（因为 Scenario 不再存在）

### P1.3 构建 + 全量测试回归

## Phase 2: 文件名重命名（按目录分批）

每个子 Phase 处理一个目录的文件名：

| 子 Phase | 文件 | 新名称 |
|----------|------|--------|
| P2.1 | `Component/AngelscriptComponentScenarioTests.cpp` | `AngelscriptComponentTests.cpp` |
| P2.2 | `Delegate/AngelscriptDelegateScenarioTests.cpp` | `AngelscriptDelegateTests.cpp` |
| P2.3 | `GC/AngelscriptGCScenarioTests.cpp` | `AngelscriptGCTests.cpp` |
| P2.4 | `HotReload/AngelscriptHotReloadScenarioTests.cpp` | `AngelscriptHotReloadTests.cpp` |
| P2.5 | `Inheritance/AngelscriptInheritanceScenarioTests.cpp` | `AngelscriptInheritanceTests.cpp` |
| P2.6 | `Subsystem/AngelscriptSubsystemScenarioTests.cpp` | `AngelscriptSubsystemTests.cpp` |

每个子 Phase：
- [ ] `git mv` 重命名
- [ ] 更新文件内的 namespace 和类名（如 `AngelscriptTest_Component_AngelscriptComponentScenarioTests_Private`）
- [ ] 构建验证

## Phase 3: 脚本模块名清理（可选，低优先级）

内联脚本的 `ModuleName`（如 `ScenarioActorBeginPlay`）不影响外部接口，仅影响生成的临时 `.as` 文件名。可以在后续的日常维护中逐步替换：

- `ScenarioActorBeginPlay` → `TestActorBeginPlay` 或 `ActorBeginPlay`
- `ScenarioInterfaceImplementBasic` → `TestInterfaceImplementBasic`

**此 Phase 不阻塞前两个 Phase，可以在日常修改文件时顺手清理。**

## Phase 4: C++ 类名清理（可选，最低优先级）

`IMPLEMENT_SIMPLE_AUTOMATION_TEST` 中的 C++ 类名（如 `FAngelscriptScenarioActorBeginPlayTest`）是内部符号，不影响测试路径。可在触及文件时顺手重命名，不需要专项批量处理。

## 验收标准

- [ ] `grep -r "Scenario" Plugins/Angelscript/Source/AngelscriptTest/` 在 Phase 1-2 完成后，文件名和工具命名空间中不再出现 "Scenario"
- [ ] 全量 Interface + Actor + Component + Delegate + GC + HotReload + Subsystem 测试通过
- [ ] 文档中不再使用 "Scenario" 作为测试类别术语

## 风险

- **编译文件列表变更**: `git mv` 后 Unity Build 可能需要重新生成
- **外部分支冲突**: 如有未合并的分支引用旧文件名，需要 merge 时处理
- **临时 .as 文件名变化**: `Saved/Automation/ScenarioXxx.as` 文件名会变，不影响功能但可能影响日志搜索

## 备注

- Phase 3 和 Phase 4 是"渐进式清理"，不需要作为阻塞性工作，可以在日常开发中 opportunistic 地执行
- 如果后续决定用 `Functional` 替代 `Scenario`，Phase 1 的命名空间选择需要确定最终词汇
