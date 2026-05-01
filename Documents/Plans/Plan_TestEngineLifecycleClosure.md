# 测试引擎生命周期闭环治理计划

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复测试模块中引擎创建/获取/重置的生命周期闭环问题——消除 TEST_METHOD 中冗余的引擎 reset、统一使用 CREATE/GET/RESET 三分宏模式、清理绕过宏的直接函数调用。

**Architecture:** 三步推进——先批量修复 AFTER_ALL 中的 double-reset 和绕过宏调用，再批量修复 TEST_METHOD 中的冗余 CREATE，最后评估 FULL 引擎的必要性并降级不必要的。

**Tech Stack:** PowerShell 批量替换、`ASTEST_CREATE_ENGINE()` / `ASTEST_GET_ENGINE()` / `ASTEST_RESET_ENGINE()` 宏体系

---

## 现状审计快照（2026-04-30）

### 宏使用统计

| 宏 | 当前使用次数 | 应有使用模式 |
|---|---|---|
| `ASTEST_CREATE_ENGINE()` | **~1101 次** | 仅在 BEFORE_ALL 中用一次（~120 个文件各一次） |
| `ASTEST_GET_ENGINE()` | **2 次** | 应在每个 TEST_METHOD 中用（应约 ~470 次） |
| `ASTEST_RESET_ENGINE()` | **2 次** | 应在每个 AFTER_ALL 中用（应约 ~120 次） |

### 影响面精确统计

| 分类 | 文件数 | 冗余调用数 | 说明 |
|---|---|---|---|
| CQTest 文件 TEST_METHOD 中冗余 CREATE | **112** | **470** | 应改为 GET |
| AFTER_ALL 中 double-reset（CREATE + Reset） | **~124** | **~248** | CREATE 改 GET + Reset 改宏 |
| 直接调 ResetSharedCloneEngine 绕过宏 | **~124** | **~124** | 改为 ASTEST_RESET_ENGINE |
| 无 BEFORE_ALL 的 Legacy 文件（合理的 CREATE） | **140** | — | 不改，等 CQTest 迁移时处理 |

### 问题一：TEST_METHOD 中冗余 reset（112 个文件，470 处）

当前绝大多数 CQTest 文件的每个 TEST_METHOD 都调用 `ASTEST_CREATE_ENGINE()`（内部执行 reset），而 BEFORE_ALL 已经 reset 过一次。这意味着：
- 每个测试方法启动时都在做一次**完全不需要的**引擎清理
- `FCoverageModuleScope` 本身就负责模块隔离，不需要每次都 reset 整个引擎
- 浪费运行时开销（每次 reset 涉及 DiscardModule + GC）

**根因**：CQTest 迁移时批量替换 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` → `ASTEST_CREATE_ENGINE()`，但没有按新的三分模式区分 BEFORE_ALL / TEST_METHOD / AFTER_ALL 的不同语义。

#### 冗余 CREATE 热点文件（TOP 20）

| 文件 | 冗余次数 |
|---|---|
| `Syntax/AngelscriptSyntaxDelegateEventTests.cpp` | 25 |
| `Bindings/AngelscriptFStringBindingsTests.cpp` | 20 |
| `Syntax/AngelscriptSyntaxDefaultComponentTests.cpp` | 19 |
| `Syntax/AngelscriptSyntaxOperatorsTests.cpp` | 13 |
| `Bindings/AngelscriptUObjectBindingsTests.cpp` | 13 |
| `Syntax/AngelscriptSyntaxTypeDeclarationTests.cpp` | 12 |
| `Syntax/AngelscriptSyntaxControlFlowTests.cpp` | 12 |
| `Functional/Delegate/AngelscriptDelegateTests.cpp` | 12 |
| `Functional/Actor/AngelscriptActorScriptOverrideTests.cpp` | 10 |
| `Functional/Actor/AngelscriptActorLifecycleTests.cpp` | 10 |
| `Bindings/AngelscriptClassBindingsTests.cpp` | 10 |
| `Functional/Actor/AngelscriptActorInteractionTests.cpp` | 9 |
| `Bindings/AngelscriptForeachBindingsTests.cpp` | 9 |
| `Functional/Blueprint/AngelscriptBlueprintChildTests.cpp` | 8 |
| `Bindings/AngelscriptGameplayTagBindingsTests.cpp` | 8 |
| `Bindings/AngelscriptEnhancedInputBindingsTests.cpp` | 8 |
| `Syntax/AngelscriptSyntaxContainerTests.cpp` | 7 |
| `Syntax/AngelscriptSyntaxCastingTests.cpp` | 7 |
| `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` | 7 |
| 其余 93 个文件 | 各 3-6 |

**应有模式**：
```cpp
BEFORE_ALL()  { ASTEST_CREATE_ENGINE(); }          // reset 一次
TEST_METHOD() { FAngelscriptEngine& E = ASTEST_GET_ENGINE(); ... }  // 不 reset
AFTER_ALL()   { FAngelscriptEngine& E = ASTEST_GET_ENGINE(); ASTEST_RESET_ENGINE(E); }
```

### 问题二：AFTER_ALL 中 double-reset（~124 个文件）

存在两种子模式：

**模式 A — 内联单行**（16 个文件）：
```cpp
AFTER_ALL() { FAngelscriptEngine& E = ASTEST_CREATE_ENGINE(); AngelscriptTestSupport::ResetSharedCloneEngine(E); }
```
代表文件：`BodyInstance`、`Collision`、`Color`、`FName`、`GASExtended`、`HitResult`、`Input`、`InputComponentMixin`、`InstancedStruct`、`MeshComponent`、`MessageDialog`、`Paths`、`Volume`、`InterfaceNativeBridge`、`InterfaceNativePointerOffset`、`InterfaceNativeTests`

**模式 B — 多行展开**（~108 个文件）：
```cpp
AFTER_ALL()
{
    FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE();
    AngelscriptTestSupport::ResetSharedCloneEngine(Engine);
}
```
代表文件：大部分 Bindings/、Syntax/、Functional/ 的 CQTest 文件

### 问题三：直接调用底层函数绕过宏（124 处）

| 直接调用 | 次数 | 应改为 |
|---|---|---|
| `AngelscriptTestSupport::ResetSharedCloneEngine(Engine)` | **124** | `ASTEST_RESET_ENGINE(Engine)` |
| `AngelscriptTestSupport::GetOrCreateSharedCloneEngine()` | 少量 | `ASTEST_GET_ENGINE()` |
| `AngelscriptTestSupport::AcquireCleanSharedCloneEngine()` | 少量 | `ASTEST_CREATE_ENGINE()` |

---

## FULL 引擎使用审计（25 个文件）

### 确实需要 FULL 的（19 个文件）

| 分类 | 文件 | 理由 |
|---|---|---|
| **StaticJIT**（4） | PrimitiveConversion, NativeBridge, Exception, PrecompiledDataArchive | JIT 编译需要完整绑定环境和隔离 |
| **Bindings/World 系列**（5） | WorldFunction, WorldCollision×3, WorldCollisionAsyncSweep | 需要世界上下文、碰撞系统完整初始化 |
| **Bindings/其他**（3） | NativeEngine, GameInstanceLocalPlayer, ReflectiveFallback | 需要完整引擎环境或生产引擎行为 |
| **Core**（3） | Docs, SkipBinds, ScriptObjectType | 测试引擎核心行为（跳过绑定、类型系统、文档生成） |
| **Validation**（2） | MacroValidation, CompilerMacroValidation | 验证宏系统自身 |
| **AngelScriptSDK**（1） | TypeRegistry | SDK 类型注册需要隔离 |
| **Template**（1） | Template_ReflectionAccess | 教学模板演示 FULL 用法 |

### 可能不需要 FULL 的（6 个文件，待评估）

| 文件 | 当前用 FULL 的原因 | 降级可行性 |
|---|---|---|
| `Functional/ControlFlow/AngelscriptControlFlowTests.cpp` | Legacy 旧式测试，用 `ASTEST_COMPILE_RUN_INT` | 迁移 CQTest 后可降级为共享引擎 |
| `Functional/Execution/AngelscriptCoreExecutionTests.cpp` | Legacy，用 `ASTEST_COMPILE_RUN_INT` | 同上 |
| `Functional/Functions/AngelscriptFunctionTests.cpp` | Legacy，用 `ASTEST_COMPILE_RUN_INT` | 同上 |
| `Functional/Types/AngelscriptTypeTests.cpp` | Legacy，用 `ASTEST_COMPILE_RUN_INT` | 同上 |
| `Functional/Inheritance/AngelscriptInheritanceTests.cpp` | Legacy，部分用 FULL 部分用共享 | 分离后共享引擎部分可降级 |
| `Functional/Handles/AngelscriptHandleTests.cpp` | Legacy | 评估是否需要隔离 |

---

## 执行计划

### Phase 1: 修复 AFTER_ALL（~124 个文件）

这一步最安全——AFTER_ALL 的语义明确，不涉及测试逻辑变化。

**替换规则**：

| 搜索 | 替换为 | 文件范围 |
|---|---|---|
| `AFTER_ALL() { FAngelscriptEngine& E = ASTEST_CREATE_ENGINE(); AngelscriptTestSupport::ResetSharedCloneEngine(E); }` | `AFTER_ALL() { FAngelscriptEngine& E = ASTEST_GET_ENGINE(); ASTEST_RESET_ENGINE(E); }` | 模式 A（16 个文件） |
| `FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE();`（在 AFTER_ALL 块内） | `FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();` | 模式 B（~108 个文件） |
| `AngelscriptTestSupport::ResetSharedCloneEngine(Engine);`（在 AFTER_ALL 块内） | `ASTEST_RESET_ENGINE(Engine);` | 所有 AFTER_ALL |
| `AngelscriptTestSupport::ResetSharedCloneEngine(E);`（在 AFTER_ALL 块内） | `ASTEST_RESET_ENGINE(E);` | 所有 AFTER_ALL |

- [ ] 脚本批量替换模式 A（内联单行）
- [ ] 脚本批量替换模式 B（多行 AFTER_ALL 块内的 CREATE → GET + Reset → 宏）
- [ ] 编译验证
- [ ] 运行 `Angelscript.TestModule` 全量测试

### Phase 2: 修复 TEST_METHOD 中的冗余 CREATE（112 个文件，470 处）

**替换规则**：在有 `BEFORE_ALL { ASTEST_CREATE_ENGINE() }` 的 CQTest 文件中，将 TEST_METHOD 体内的 `ASTEST_CREATE_ENGINE()` → `ASTEST_GET_ENGINE()`。

**安全边界**：
- ✅ 替换范围：仅限有 BEFORE_ALL 的 CQTest 文件的 TEST_METHOD 体内
- ❌ 不替换：BEFORE_ALL 中的 CREATE（那是正确的）
- ❌ 不替换：AFTER_ALL 中的（Phase 1 已处理）
- ❌ 不替换：没有 BEFORE_ALL 的 140 个 Legacy 文件（它们的 CREATE 是必要的）
- ❌ 不替换：FULL / NATIVE 宏

**实现方式**：PowerShell 脚本遍历所有 .cpp 文件，检测是否有 BEFORE_ALL，如果有，则只替换 TEST_METHOD `{` 和 `}` 之间的 `ASTEST_CREATE_ENGINE()` → `ASTEST_GET_ENGINE()`。

- [ ] 编写替换脚本（带安全检查）
- [ ] 执行替换
- [ ] 编译验证
- [ ] 运行全量测试确认无回归

### Phase 3: 评估 FULL 引擎降级（6 个文件）

- [ ] 逐个评估上表中 6 个 Functional/ 文件，确认是否因为 Legacy `ASTEST_COMPILE_RUN_INT` 而被迫用 FULL
- [ ] 对可降级的文件，迁移到 CQTest 后改用 `ASTEST_CREATE_ENGINE()` + `ASTEST_GET_ENGINE()`
- [ ] 编译 + 对应测试验证

---

## 预期效果

| 指标 | 修复前 | 修复后 |
|---|---|---|
| `ASTEST_CREATE_ENGINE()` 调用 | ~1101 | ~120（仅 BEFORE_ALL + ~140 个 Legacy） |
| `ASTEST_GET_ENGINE()` 调用 | 2 | ~590（~470 个 TEST_METHOD + ~120 个 AFTER_ALL） |
| `ASTEST_RESET_ENGINE()` 调用 | 2 | ~124（每个 AFTER_ALL） |
| 直接调 `ResetSharedCloneEngine` | 124 | 0（全部改为宏） |
| 每次测试的冗余 reset 次数 | 2-3 次 | 0 次 |
| 受影响 CQTest 文件总数 | — | ~112 个（Phase 1+2 合计） |
| 不受影响的 Legacy 文件 | — | ~140 个（等 CQTest 迁移时处理） |
| 潜在测试速度提升 | — | 减少 ~470 次不必要的引擎 reset + ~124 次 double-reset |

---

## 关联文档

- `Plan_CQTestFullMigration.md` — CQTest 全面迁移计划
- `Plan_ExamplesTestConsolidation.md` — Examples 测试融合计划
- `TESTING_GUIDE.md` — 测试指南（已包含 CREATE/GET/RESET 标准用法）

## 验证清单

1. `RunBuild.ps1` 编译通过
2. `RunTests.ps1 -TestPrefix "Angelscript.TestModule"` 全量通过，无新增失败
3. `grep -r "AngelscriptTestSupport::ResetSharedCloneEngine" --include="*.cpp"` 仅在 Shared/ 基础设施文件中出现
4. `grep -r "ASTEST_CREATE_ENGINE()" --include="*.cpp"` 仅在 BEFORE_ALL 和 Legacy 文件中出现
