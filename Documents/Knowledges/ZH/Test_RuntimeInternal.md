# Test_RuntimeInternal — Runtime 内部测试边界

> **所属前缀**: Test_（测试架构族）
> **关注层面**: `Angelscript.CppTests.*` / `Angelscript.TestModule.CppTests.*` 这一族 Runtime 内部 C++ 探针测试的语义、实际物理位置、与集成测试 `Angelscript.TestModule.<Theme>.*` 的边界、当前覆盖矩阵与缺口（不重述模块分层、不展开主题簇映射、不重复 Helper 大类拆解）
> **关键源码**:
> `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineHooksTests.cpp` (~330 行)
> · `Core/AngelscriptEngineExtensionRegistryTests.cpp` (~230 行)
> · `Core/AngelscriptEngineTypeInteropTests.cpp` (~169 行)
> · `Core/AngelscriptMultiEngineLifecycleTests.cpp` (~600+ 行；对照族)
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`（约定中的"理想落点"，当前为空）
> · `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngine.{h,cpp}` (~50 / 144 行)
> **关联文档**:
> `Documents/Knowledges/ZH/Test_Layering.md` — 三层骨架与 Build.cs 边界（本文是其 §四.3 的延伸）
> · `Documents/Knowledges/ZH/Test_Infrastructure.md` — Helper 层；本文复用其"四档引擎"概念但只取 IsolatedFull 一档
> · `Documents/Knowledges/ZH/Test_TopicClusters.md` — 主题簇映射；本文给出与 `Core/` 簇的边界
> · `Documents/Knowledges/ZH/Note_CQTest.md` — CQTest 框架使用笔记（CppTests 同样走 CQTest）
> · `Documents/Knowledges/ZH/RT_GlobalState.md` — 引擎隔离 / Reset 协议（CppTests 是其 RT 视角的对偶探针）
> · `Documents/Knowledges/ZH/Arch_RuntimeLifecycle.md` — Runtime 生命周期（hooks / extension registry 在此被订阅）
> **外部参考**:
> `Documents/Guides/TestConventions.md §1` — 测试层级矩阵
> · `Documents/Guides/TestCatalog.md §10 / §15` — CppTests 编目情况
> · `AGENTS.md` 行 151 — `Angelscript.CppTests.*` 的官方口径

---

## 概览

本文聚焦一个核心问题：**`Angelscript.CppTests.*` 与 `Angelscript.TestModule.CppTests.*` 这两个同义前缀下的测试，究竟在做什么、为什么是"Runtime 内部测试"而不是普通集成测试、它们与 `Angelscript.TestModule.<Theme>.*` 的边界画在哪、当前实际覆盖到哪、还差哪些？**

这是 P5 Test_ 族的最后一篇。它**不**重述模块分层（去 `Test_Layering.md`），**不**重述 Helper 工具大类（去 `Test_Infrastructure.md`），**不**重述每个主题簇测什么（去 `Test_TopicClusters.md`），**不**展开 CQTest 宏机制（去 `Note_CQTest.md`）。它只回答"`CppTests` 这条线本身是什么、当前落地到什么程度"。

```text
+----------------------------------------------------------+
| AGENTS.md 行 151 + TestConventions.md §1 给出的"理想口径" |
|                                                          |
|   AngelscriptRuntime/Tests/                              |
|       └── Angelscript.TestModule.CppTests.*              |
|             ★ 不依赖 Engine.Initialize、不依赖 .as       |
|             ★ 纯 C++ 数据结构 / 内部 API 验证            |
+--------------------▲-------------------------------------+
                     │
                     │  目录当前为空 / 占位
                     │  (Test_Layering §四.3 已点名)
                     │
+--------------------┴-------------------------------------+
|              当前实际落点：AngelscriptTest/Core/         |
|                                                          |
|  3 个 CppTests 文件（截至本文）                          |
|   ├── Engine.Hooks       (TestModule.CppTests.*)         |
|   ├── Engine.Extension   (TestModule.CppTests.*)         |
|   └── Engine.TypeInterop (Angelscript.CppTests.*  legacy)|
|                                                          |
|  31 个 TestModule.Engine.* / TestModule.Core.* 兄弟      |
|   ├── Engine.MultiEngine (lifecycle 四引擎隔离)          |
|   ├── Engine.BindConfig / BindString / BindModuleCache   |
|   ├── Engine.TypeDatabase / Docs / RuntimeModule         |
|   └── ...                                                |
+----------------------------------------------------------+

边界对比
========
                CppTests / TestModule.CppTests        TestModule.<Theme>
观察对象        Runtime 内部 C++ 数据结构 / 公开 API   脚本级行为（编译→执行→断言）
引擎档位        IsolatedFull (CreateFullTestEngine)   SharedClone (默认共享 + reset)
脚本依赖        极少（多为 0~1 个最小模块）            高（每 case 编一段 .as）
状态隔离        显式 ContextStackGuard + DestroyGlobal 默认 FCoverageModuleScope 增量回滚
访问深度        通过 friend / Test 专用访问点          只用公开 API + Helper 层
失败定位        指向某条 Runtime 内部不变量            指向某条 AS 行为/绑定
```

后续章节按"概念定位 → 命名约定与物理位置（理想 vs 现实）→ 当前覆盖矩阵 → 典型测试形态拆解 → 与集成测试的边界 → CQTest 框架与 Spec 命名 → BlueprintImpact / Cooker 关系 → 编写决策树 → 当前缺口"的顺序推进，最后用三张速查表收口。

---

## 一、概念定位：CppTests 是什么、不是什么

### 1.1 一句话定义

`Angelscript.CppTests.*`（含 `Angelscript.TestModule.CppTests.*` 同义形式）是 `AngelscriptTest` 模块下**专门用 C++ 直接探针 Runtime 内部数据结构 / 公开 API 的测试族**。它的核心特征是：

- **以验证 Runtime 私有/受限内部状态为目标**——不是脚本级别的"输入 .as → 看返回值"那种集成验证；
- **倾向极小化脚本依赖**——多数 case 不喂 .as，少数喂一个空模块或一行函数仅为打开类型表；
- **要求强引擎隔离**——靠 `CreateFullTestEngine()` 拿 IsolatedFull 引擎、靠 `FAngelscriptEngineContextStack` 显式快照/恢复，避免共享 engine 污染探针；
- **走 CQTest 标准模式**——`TEST_CLASS_WITH_FLAGS` + `TEST_METHOD`，不再用 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`。

### 1.2 不是什么

`CppTests` **不是**："纯静态的、零依赖的 C++ 单元测试"。

`AGENTS.md` 行 151 的官方表述是 "`Angelscript.CppTests.*` for runtime C++ unit tests"，理想口径是"不依赖 Engine.Initialize、不依赖 .as scripts、纯 C++ 数据结构验证"。但现实是：

> 当前所有挂 `CppTests` 前缀的 case 都会调 `AngelscriptTestSupport::CreateFullTestEngine()`——这个函数内部就是 `FAngelscriptTestEngine::Create(Config{bSkipInitialCompile=true}, ...)`，它**仍然走完整的引擎初始化**（类型表 / Bind 数据库 / VM 都建起来），只是跳过磁盘脚本扫描与 cooked-style 编译。

也就是说，CppTests **依赖 Engine 实例存在**，但**不依赖它的"启动期 .as 编译完成"**。这点比 `TestModule.<Theme>.*` 集成测试轻一个量级，但还远没到"零依赖单元测试"的程度。详见本文 §五。

### 1.3 与 `Angelscript.TestModule.<Theme>.*` 的语义差别

```text
              ┌────────────────────────────────┐
              │  目标：保证脚本端某行为 X        │
              │  路径：写 .as → 编译 → 执行 → 断言│
              │  失败 → 上游链上某节失常        │
              │  例：HotReload.PropertyPreserved│
              └─────────▲──────────────────────┘
                        │ TestModule.<Theme>
                        │
                        │ public API 上层
─────────────────────────┼─────────────────────────
                        │ 直接戳"内部抽屉"
                        ▼ TestModule.CppTests / CppTests
              ┌────────────────────────────────┐
              │  目标：保证内部数据结构不变量 Y │
              │  路径：起 IsolatedFull 引擎 →   │
              │        直接调 Runtime 内部 API  │
              │        → 比对内部状态 / 计数器  │
              │  例：Engine.Extension.Late-     │
              │       RegistrationReplaysToCur- │
              │       rentEngine               │
              └────────────────────────────────┘
```

举例：

- **集成测试（TestModule.HotReload.PropertyPreserved）**：编一段 v1 脚本 → reload 成 v2 → spawn actor → tick → 反射读 property → 断言保留。
- **CppTests（TestModule.CppTests.Engine.Extension.LateRegistrationReplaysToCurrentEngine）**：起两个 IsolatedFull 引擎 → 注册一个 `IAngelscriptExtension` → 断言 `AttachCount == 1` 且 `AttachedEngineIds[0]` 等于当前 engine 指针——脚本完全没参与。

后者对维护 Runtime 私有契约更"贴脸"；前者对回归用户可见行为更直接。两条线缺一不可。

---

## 二、命名约定与物理位置：理想 vs 现实

### 2.1 命名约定（Test_Layering 已覆盖，本文只摘要）

`Test_Layering.md` §四.3 已经详细对比了两种写法，这里只摘要重点：

| 前缀变体 | 来源 | 当前规范 | 现状 |
|---------|------|---------|------|
| `Angelscript.TestModule.CppTests.*` | TestConventions.md §3 推荐写法 | **新写一律带 `TestModule.` 段** | 2 个文件（Engine.Hooks / Engine.Extension） |
| `Angelscript.CppTests.*` | 早期遗留（无 TestModule. 段） | 历史保留，不强制改名 | 1 个文件（Engine.TypeInterop） |

这 2 + 1 = **3 个文件**就是当前 `CppTests` 族的全部。详见 §三 覆盖矩阵。

### 2.2 物理位置：理想 vs 现实

`Documents/Guides/TestConventions.md §1` 把"理想落点"明确写出：

```text
| Runtime 内部 C++ | Plugins/Angelscript/Source/AngelscriptRuntime/Tests/
|                  | Angelscript.TestModule.CppTests.*
|                  | 运行时内部状态、共享引擎、隔离、覆盖率、预编译数据等
|                  | Runtime 私有头 + StartAngelscriptHeaders.h
```

但现实是——

```bash
$ ls Plugins/Angelscript/Source/AngelscriptRuntime/Tests/
<目录不存在>
```

`Test_Layering.md` §四.3 已经点过名：

> `AngelscriptRuntime/Tests/` 在仓库当前 baseline 里**不存在**实体目录。`Documents/Guides/TestConventions.md §1` 把它标为 `Angelscript.TestModule.CppTests.*` 的"理想落点"，但实际历史 case 都在 `AngelscriptTest/Core/` 里。

含义有三层：

1. **AngelscriptRuntime 模块不带任何测试**——它是 Runtime 模块（`PostDefault` phase），按 UE 模块约定本不应链接 `WITH_DEV_AUTOMATION_TESTS` 代码；要写 Runtime 内部测试，要么把 Runtime 模块拆出 Developer 子模块，要么继续放在 `AngelscriptTest/Core/`。
2. **当前选择是放在 `AngelscriptTest/Core/`**——这避开了拆模块的成本，缺点是"内部"测试需要走 `AngelscriptTest` 的依赖（即仍然需要 Editor 守卫块下的 `CQTest` / `Sockets` / `BlueprintGraph` 等）。
3. **TestConventions.md §1 表中"理想落点"短期不会落实**——拆 Runtime 模块需要专项 OpenSpec 提案；目前的取舍是"落点错位但不强制迁"。

### 2.3 为什么不在 Runtime 模块里写

抛开"模块拆分成本"，本质原因有两个：

- **`AngelscriptRuntime.Build.cs` 不挂 CQTest**。CQTest 是 Editor / Program 链接的 Developer 模块，挂到 Runtime 会污染 cooked target。这意味着 Runtime 内 case 只能用裸 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`——但 Spec 已经全面迁到 CQTest（见 `Note_CQTest.md` / `Test_Infrastructure.md` §四.2 已 archived "Test macro optimization"），不应再开倒车。
- **`AngelscriptRuntime` 没有 `bBuildEditor` 守卫块**——它是真 Runtime，即使写 `WITH_DEV_AUTOMATION_TESTS` 守卫，也得保证测试代码本身不引入 Editor-only 头文件。当前 CppTests 的探针（如 `FBlueprintActionDatabase` 缓存验证）会触碰 Editor 模块边界。

**短期对策**：在 `AngelscriptTest/Core/` 用 `Angelscript.{TestModule.}CppTests.*` 前缀**在路径上声明它是 Runtime 内部测试**，物理上仍住在 Test 模块。

---

## 三、当前覆盖矩阵

### 3.1 完整清单

按"前缀变体 + 测试类 + TEST_METHOD 主题"展开：

| Automation 前缀 | 文件（`AngelscriptTest/Core/` 下） | TEST_METHOD（节选） | 探针目标 |
|----------------|----------------------------------|--------------------|---------|
| `Angelscript.TestModule.CppTests.Engine.Extension` | `AngelscriptEngineExtensionRegistryTests.cpp` | `EmptyRegistryReplayIsANoop` / `LateRegistrationReplaysToCurrentEngine` / `UnregisterStopsFutureReplay` / ... | `FAngelscriptEngineExtensionRegistry` 的 attach / detach / replay 不变量 |
| `Angelscript.TestModule.CppTests.Engine.Hooks` | `AngelscriptEngineHooksTests.cpp` | `PreCompileHookDoesNotLeakBetweenEngines` / `RuntimeHookDelegatesAreEngineOwned` / `RuntimeBindCallSitesUseCurrentEngineHooks` / ... | `FAngelscriptEngine::GetHooks()` 返回的 13+ 委托表的 engine-owned 不变量 |
| `Angelscript.CppTests.Engine.TypeInterop` | `AngelscriptEngineTypeInteropTests.cpp` | `GetUnrealStructFromTypeIdRejectsNonStructAndPreservesPlainStructs` | `FAngelscriptEngine::GetUnrealStructFromAngelscriptTypeId` 对 enum / template / delegate / event / 非法 typeId 的拒绝逻辑 |

合计 **3 个文件**。文件总行数约 **730 行**（Hooks ~330 + Extension ~230 + TypeInterop ~169）。

### 3.2 被错认的"看起来像 CppTests"——其实是 `TestModule.Engine.*`

`AngelscriptTest/Core/` 下还有 31 个文件挂 `Angelscript.TestModule.Engine.*` / `Angelscript.TestModule.Core.*` 前缀。它们的目标也都是 Runtime 内部，但**没用 CppTests 段**：

| 文件 | 前缀 | 与 CppTests 的差别 |
|------|-----|------------------|
| `AngelscriptMultiEngineLifecycleTests.cpp` | `Angelscript.TestModule.Engine.MultiEngine` | 同样起 IsolatedFull 引擎，但文件内部代码注释明确说"Replacement coverage of the new single-owner contract belongs in `Angelscript.TestModule.CppTests.Engine.TestEngine.*` once added"——即它**应当**搬过去，目前未搬 |
| `AngelscriptEngineIsolationTests.cpp` | `Angelscript.TestModule.Engine.Isolation` | 测引擎隔离；按理也属 CppTests 范围 |
| `AngelscriptBindConfigTests.cpp` / `AngelscriptBindModuleCacheTests.cpp` / `AngelscriptBindStringTests.cpp` | `Angelscript.TestModule.Engine.BindConfig.*` | bind 配置 / 缓存——属于 Runtime 内部，但走 `Engine.BindConfig` 子前缀 |
| `AngelscriptTypeDatabaseTests.cpp` | `Angelscript.TestModule.Engine.TypeDatabase` | 类型数据库内部一致性 |
| `AngelscriptEngineCoreTests.cpp` | `Angelscript.TestModule.Engine` | 引擎核心 smoke |
| `AngelscriptCodeCoverageTests.cpp` | `Angelscript.TestModule.Core.CodeCoverage.*` | coverage 内部—— `Core.` 段未带 CppTests |
| ... | ... | ... |

**含义**：`CppTests` 段在当前代码里**没有被严格执行**——同类性质的 case 一部分在 `CppTests`、一部分在 `Engine.*` / `Core.*`，靠路径段名分辨"这一族在测什么"会失准。`TestConventions.md §3` 提出过"新写一律带 `TestModule.CppTests.` 段"的目标，但实际只 2 个文件做到了。

### 3.3 TestCatalog.md 的"已编目 vs 实际"

`TestCatalog.md` 表格里出现过另外几个 `CppTests` 子前缀：

```text
| Angelscript.TestModule.CppTests.Debug.Protocol.*    | AngelscriptDebugProtocolTests.cpp
| Angelscript.TestModule.CppTests.Debug.Transport.*   | AngelscriptDebugTransportTests.cpp
| Angelscript.TestModule.CppTests.MultiEngine         | (smoke 层引用)
| Angelscript.TestModule.CppTests.Engine.DependencyInjection
| Angelscript.TestModule.CppTests.Subsystem
| Angelscript.TestModule.CppTests.StaticJIT.PrecompiledData.*
```

实测 grep `AngelscriptDebugProtocolTests.cpp` / `AngelscriptDebugTransportTests.cpp` 在仓库内**不存在**；`PrecompiledData.*` 实际前缀是 `Angelscript.TestModule.StaticJIT.PrecompiledData.*`（无 CppTests 段）；`MultiEngine` / `DependencyInjection` / `Subsystem` 实际前缀也是 `TestModule.Engine.*`。

**结论**：TestCatalog.md 的相关表格是**部分计划态、部分历史漂移**的混合。本文以源码 grep 结果为准——当前 `CppTests` 段下只有 3 个文件。

---

## 四、典型测试形态：Engine 隔离 + 内部状态探针

### 4.1 标准 case 骨架

CppTests 的 TEST_METHOD 几乎都长成下面这个样子：

```cpp
// ============================================================================
// 文件: AngelscriptTest/Core/AngelscriptEngineExtensionRegistryTests.cpp
// 节选自: TEST_METHOD(LateRegistrationReplaysToCurrentEngine) 简化骨架
// 角色: CppTests 标准探针形态
// ============================================================================
TEST_METHOD(LateRegistrationReplaysToCurrentEngine)
{
    FExtensionRegistryContextGuard ContextGuard;             // ★ 1) 快照并清空 engine context stack
    AngelscriptTestSupport::DestroySharedTestEngine();        // ★ 2) 让出 SharedClone 名额
    if (FAngelscriptEngine::IsInitialized())
        FAngelscriptTestEngineScopeAccess::DestroyGlobalEngine();  // ★ 3) 清掉残留 global
    ContextGuard.DiscardSavedStack();                         // ★ 4) 不还原（清得最干净）

    ON_SCOPE_EXIT { /* 反向 cleanup：再次清空 + destroy global + destroy shared */ };

    TUniquePtr<FAngelscriptEngine> Engine = AngelscriptTestSupport::CreateFullTestEngine();
    FAngelscriptEngineScope EngineScope(*Engine);             // ★ 5) 显式声明 current engine

    TSharedRef<FRecordingEngineExtension> Ext = MakeShared<FRecordingEngineExtension>();
    FAngelscriptEngineExtensionRegistry::Get().RegisterExtension(Ext);

    // ★ 6) 直接断言"内部计数器"，不经任何 .as 脚本
    TestRunner->TestEqual(TEXT("..."), Ext->AttachCount, 1);
    TestRunner->TestEqual(TEXT("..."), Ext->AttachedEngineIds[0], MakeEngineIdentityString(*Engine));
}
```

六个标志性步骤：

1. **ContextStackGuard**：`FAngelscriptEngineContextStack::SnapshotAndClear()` 保存当前 engine 上下文栈并清空——否则共享 engine 的 scope 会污染探针。
2. **DestroySharedTestEngine**：让出 Helper 层维护的 SharedClone 名额。这一步会 `ResetModules` + `Reset()` 整个共享 engine。
3. **DestroyGlobalEngine**：调 `FAngelscriptEngine::DestroyGlobal()`——通过 `FAngelscriptTestEngineScopeAccess` 友元访问点。
4. **DiscardSavedStack**：不还原快照——既然进了 CppTests，就**完全清场**而不是临时借用。
5. **CreateFullTestEngine + EngineScope**：拿 IsolatedFull 引擎，显式起 scope。
6. **直接断言内部状态**：访问的是 Runtime API（`FAngelscriptEngineExtensionRegistry::Get()` / `Engine->GetHooks()` / `Engine->GetUnrealStructFromAngelscriptTypeId()`），完全不写 .as。

### 4.2 与共享 engine 的差别

`Test_Infrastructure.md` §二.1 给出过四档引擎：

```text
ProductionLike → 真实 GameInstance 引擎（最重）
SharedClone    → 默认；CQTest BEFORE_ALL 取一次（持久 scope）
IsolatedFull   → CppTests 唯一选项；每个 TEST_METHOD 独占
Native         → 仅 AngelScriptSDK/* 用，不涉 FAngelscriptEngine
```

CppTests **不能用 SharedClone**——SharedClone 全程持有 "持久 `FAngelscriptEngineScope`" 与 reset on acquire 语义，会让"内部状态计数器"在 case 之间偶尔脏。这就是为什么所有 CppTests 都做"先杀 SharedClone、再杀 GlobalEngine、再起 IsolatedFull、显式 ContextStackGuard"——这套 4 步开场白几乎一模一样。

### 4.3 一个特殊形态：FAngelscriptMultiEngineTestAccess

`AngelscriptMultiEngineLifecycleTests.cpp` 引出了一个 CppTests 风格独有的设计 — **friend 访问类**：

```cpp
// ============================================================================
// 文件: AngelscriptTest/Core/AngelscriptMultiEngineLifecycleTests.cpp
// 角色: CppTests 风格的"友元访问点"——直接戳 Runtime 私有字段
// ============================================================================
struct FAngelscriptMultiEngineTestAccess
{
    static void DestroyGlobalEngine() { FAngelscriptEngine::DestroyGlobal(); }
    static FAngelscriptEngine* GetGlobalEngine() { return FAngelscriptEngine::TryGetGlobalEngine(); }

    static asIScriptModule* CreateNamedModule(FAngelscriptEngine& Engine, const FString& ModuleName)
    { return Engine.Engine->GetModule(TCHAR_TO_ANSI(*Engine.MakeModuleName(ModuleName)), asGM_ALWAYS_CREATE); }

    // ★ 直接戳 ActiveModules 这种 private 字段，集成测试一律不允许
    static void TrackNamedModule(FAngelscriptEngine& Engine, const FString& ModuleName, asIScriptModule* ScriptModule)
    {
        TSharedRef<FAngelscriptModuleDesc> Desc = MakeShared<FAngelscriptModuleDesc>();
        Desc->ModuleName = ModuleName;
        Desc->ScriptModule = static_cast<asCModule*>(ScriptModule);
        Engine.ActiveModules.Add(Engine.MakeModuleName(ModuleName), Desc);
        Engine.ModulesByScriptModule.Add(ScriptModule, Desc);
    }

    static int32 GetLocalPooledContextCount(asIScriptEngine* ScriptEngine)
    { return FAngelscriptEngine::GetLocalPooledContextCountForTesting(ScriptEngine); }
};
```

> 这是 CppTests 与集成测试的**最大形态差**。集成测试一律走公开 API（`FAngelscriptEngine::CompileModules` / `GetActiveModules`）；CppTests 允许通过 `FAngelscriptMultiEngineTestAccess` 这类友元访问点直接读写私有字段。代价：Runtime 重构时 friend 访问点需要同步维护——见 §九 缺口。

### 4.4 脚本依赖：极少而非零

CppTests 不是"完全不喂脚本"——少数 case 仍然需要一点 .as 来打开类型表。例如 `Engine.TypeInterop` 测试 `GetUnrealStructFromAngelscriptTypeId` 时，必须先编一段含 `delegate void X(int)` 与 `event void Y(int)` 的脚本，让 Module 把 delegate / event 类型注册进 typeId 表，否则没法验证"这些 typeId 都被正确拒绝映射回 UStruct"：

```cpp
// 节选自: AngelscriptEngineTypeInteropTests.cpp
const FString ScriptSource = FString::Printf(
    TEXT("delegate void %s(int32 Value);\n")
    TEXT("event void %s(int32 Value);\n")
    TEXT("int Entry() { return 0; }\n"),
    *SingleCastTypeName, *MultiCastTypeName);
asIScriptModule* Module = BuildModule(*TestRunner, *TestEngine, ModuleNameAnsi.Get(), ScriptSource);
// ★ 编完只是拿 typeId，不执行 Entry() 函数
const int SingleCastTypeId = SingleCastTypeInfo->GetTypeId();
UStruct* SingleCastStruct = TestEngine->GetUnrealStructFromAngelscriptTypeId(SingleCastTypeId);
TestRunner->TestNull(TEXT("..."), SingleCastStruct);   // ★ 真正断言：内部映射 API 行为
```

这与"集成测试编一段脚本去执行 → 断言返回值"的模式完全不同：**CppTests 编脚本只是为了"产生数据"，断言对象是 Runtime API**。

---

## 五、为什么不是"纯 C++ 数据结构验证"——口径与现实的差距

### 5.1 AGENTS.md 与 TestConventions.md 的口径

```text
AGENTS.md 行 151:
  Tests use the Automation prefix convention
  Angelscript.TestModule.<Theme>.* for integration tests,
  Angelscript.CppTests.*           for runtime C++ unit tests, and
  Angelscript.Editor.*             for editor tests.

TestConventions.md §1 表格:
  Runtime 内部 C++  →  AngelscriptRuntime/Tests/  →  Angelscript.TestModule.CppTests.*
                       运行时内部状态、共享引擎、隔离、覆盖率、预编译数据等
                       Runtime 私有头 + StartAngelscriptHeaders.h
```

字面读"runtime C++ unit tests" / "纯 C++ 数据结构验证"——容易理解成 GoogleTest 风格的"零 Engine 单测"。

### 5.2 现实的三个让步

但实际落地有三个让步：

1. **没有零 Engine 路径**。`FAngelscriptEngine::Create` 是构造类型表 / Bind 数据库 / VM 的唯一入口，Runtime 内部 API（如 `GetHooks()` / `GetUnrealStructFromAngelscriptTypeId`）都挂在 Engine 实例上——绕开 Engine 就没法验证它们。
2. **CQTest 必然链接到 AngelscriptTest 模块**。Runtime 模块自身不挂 CQTest（见 §二.3）；放在 Test 模块里就一定是"挂在 Engine 之上的"测试。
3. **`bSkipInitialCompile=true` 是当前最轻的引擎档位**。它跳过磁盘扫描与 cooked 编译，但不能跳过类型表构造——这一档已经是 IsolatedFull 能给的最小开销。

### 5.3 重新对齐：CppTests 的工作定义

抛开字面"纯 C++ 单测"的误导，CppTests 在当前仓库的**工作定义**应是：

```text
CppTests 工作定义
=================
1. 起 IsolatedFull 引擎（bSkipInitialCompile=true，不扫盘、不编 .as）
2. 显式 ContextStackGuard / DestroyGlobalEngine 清场，避免共享 engine 污染
3. 通过 Runtime 公开 API 或 friend 访问点直接戳"内部数据结构 / 内部状态计数器"
4. 不走脚本编译→执行→反射断言的集成路径；脚本仅在"喂数据"角色时使用
5. 走 CQTest 标准模式（TEST_CLASS_WITH_FLAGS + TEST_METHOD）
6. 失败时指向某条 Runtime 内部不变量（如"Hook delegate 不应跨 engine 泄漏"）
```

下次有新需求时，对照这 6 条决定要不要落 CppTests——而不是机械套"纯 C++"字面意义。

### 5.4 为什么"零 Engine 单测"暂时也不必要

抛开当前实现，**真正的零 Engine 单测**应该测什么？候选：

- `FAngelscriptType` / `FAngelscriptTypeUsage` 的解析与 `==` / hash 行为；
- Preprocessor 的 token splitter（与 `FAngelscriptPreprocessor::PreparseTokens` 配套的纯算法层）；
- Hash 组合器（用于 Bind 数据库 cache key 的 hash 函数）。

但这几个候选都有现成的"间接验证"：

- `FAngelscriptType` 解析在 `Bindings/AngelscriptType*Tests.cpp` 中通过"喂一段 .as 看绑定结果"间接覆盖；
- Preprocessor token 行为在 `Preprocessor/AngelscriptPreprocessor*Tests.cpp` 中通过"输入文本 → 看展开结果"间接覆盖；
- Hash 组合器在 `Core/AngelscriptBindModuleCacheTests.cpp` 通过"造两个相似配置看 cache 是否命中"间接覆盖。

间接覆盖的代价是：失败时定位到具体哪行算法的成本变高。后续如果出现"间接覆盖反复掩盖某 token bug"的情况，就该补一批真正的零 Engine 单测；当下不是优先项。

---

## 六、CQTest 框架与 Spec 命名宏

### 6.1 CppTests 同样走 CQTest

历史上 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 在 CppTests 风格里出现过（典型如 `Validation/AngelscriptMacroValidationTests.cpp` 的 `ASTEST_COMPILE_RUN_INT` legacy macro 路径）。当前 `Core/` 下三个 CppTests 文件**全部**走 CQTest：

```cpp
TEST_CLASS_WITH_FLAGS(FAngelscriptEngineExtensionRegistryTests,
    "Angelscript.TestModule.CppTests.Engine.Extension",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
    TEST_METHOD(EmptyRegistryReplayIsANoop) { /* ... */ }
    TEST_METHOD(LateRegistrationReplaysToCurrentEngine) { /* ... */ }
    TEST_METHOD(UnregisterStopsFutureReplay) { /* ... */ }
};
```

特征：

- **没有 `BEFORE_ALL` / `AFTER_ALL`**——CppTests 的清场动作走"每 TEST_METHOD 自己开场 + ON_SCOPE_EXIT 收尾"，因为不同 TEST_METHOD 之间的 isolation 要求高。共享 `BEFORE_ALL` 反而让残留状态更难定位。
- **没有 `ASTEST_CREATE_ENGINE`**——直接 `CreateFullTestEngine()`。`ASTEST_CREATE_ENGINE_FULL` 宏（`Test_Infrastructure.md` §四）也可以用，但 CppTests 习惯直接调函数版本，配合 `TUniquePtr` 显式 own。
- **没有 `FCoverageModuleScope`**——这是 Bindings 主题的模块隔离 RAII；CppTests 偶尔用 `BuildModule` 但不挂模块 RAII，因为引擎本身就是 transient 的，TEST_METHOD 结束后整个 engine 都会被 `TUniquePtr::Reset()` 销毁。

### 6.2 与 `Note_CQTest.md` 的对接

`Note_CQTest.md` 详细描述了 CQTest 宏机制（`TEST_CLASS_WITH_FLAGS` 展开、`BEFORE_ALL` 静态成员语义、`TEST_METHOD` 注册路径）。CppTests 用其中**最小子集**：

```text
Note_CQTest 全集                         CppTests 用到的子集
========================================  ====================
TEST_CLASS_WITH_FLAGS                     ✓
TEST_METHOD                               ✓
BEFORE_ALL / AFTER_ALL                    ✗ (用 ON_SCOPE_EXIT 替代)
BEFORE_EACH / AFTER_EACH                  ✗
SECTION / SUBSECTION                      ✗
TEST_METHOD(...) WITH_TAGS(...)           ✗
ASSERT_THAT / TestRunner->TestXxx         ✓ (后者为主)
```

> **核心简化**：CppTests 的语义是"每条 TEST_METHOD 独占一个引擎实例"，没有"类级一次启动 / 多 case 共享"的需求。BEFORE_ALL 在这里反而是**反 pattern**——它会鼓励 case 之间共享状态。

---

## 七、与其他簇的边界

### 7.1 CppTests vs TestModule.<Theme>.*

```text
                            CppTests / TestModule.CppTests        TestModule.<Theme>.*
─────────────────────────  ────────────────────────────────────  ─────────────────────────────
观察对象                    Runtime 内部数据结构 / 公开 API        脚本侧业务行为
脚本依赖                    极少（多数 0~1 个最小模块）            高（每 case 编一段 .as）
引擎档位                    IsolatedFull (CreateFullTestEngine)    SharedClone (默认共享)
Reset 协议                  显式 DestroyGlobal + ContextStackGuard FCoverageModuleScope 增量回滚
访问深度                    可经 friend 访问点戳 private 字段      只用公开 API + Helper 层
失败定位                    某条 Runtime 不变量                    某条 AS 行为 / 绑定
回归触发                    Runtime 内部重构时                     Runtime 行为变更或新增
样板长度                    ~70-100 行/case（含 4 步开场白）       ~20-40 行/case（CQTest 简洁）
```

### 7.2 CppTests vs AngelScriptSDK/Native

`AngelScriptSDK/AngelscriptNativeSmokeTest.cpp` 也"不挂 FAngelscriptEngine"——表面上更接近"零 Engine 单测"。但两者目标完全不同：

| 维度 | CppTests | AngelScriptSDK/Native |
|------|---------|----------------------|
| 引擎类型 | `FAngelscriptEngine`（UE 桥接的 Runtime 引擎） | 原生 `asIScriptEngine`（直接 `asCreateScriptEngine`） |
| 验证目标 | UE 桥接层的内部 API / 数据结构 | AS ThirdParty 内核的"裸"行为 |
| 命名空间 | `Angelscript.{TestModule.}CppTests.*` | `Angelscript.TestModule.AngelScriptSDK.*` |
| Helper | `FAngelscriptTestEngine::Create` | `CreateNativeEngine(&Messages)` |
| 失败时排查方向 | UE Runtime 集成 | AS 内核 / fork 差异 |

**边界规则**：

- 如果某个 bug 只能在 UE bind 层之上重现 → CppTests
- 如果某个 bug 在裸 AS 内核就能重现 → AngelScriptSDK
- 如果某个 bug 只在 .as 脚本运行时才暴露 → TestModule.<Theme>

### 7.3 CppTests vs Editor 模块测试

`AngelscriptEditor/Tests/` 的 49 个 spec 走 `Angelscript.Editor.*` 前缀（`Test_Layering.md` §四.1 / `Test_TopicClusters.md` §五.5）。它们与 CppTests 的关系：

- **完全不重叠**——Editor 测试针对 `FClassReloadHelper` / `FAngelscriptDirectoryWatcher` / `FAngelscriptBlueprintImpactScanner` 等 Editor 模块的内部对象；CppTests 针对 Runtime 模块。
- **观察方向相反**——Editor 测试在 Editor 模块自己的 Build.cs 守卫块下运行；CppTests 在 Test 模块（Editor build only）。
- **Editor 不能反向依赖 Test 模块**——所以 Editor 测试不能用 CppTests 的 friend 访问点，反之亦然。

### 7.4 与 BlueprintImpact / cooker 的关系

CppTests **不依赖 Blueprint，不参与 cooker pipeline**：

- `BlueprintImpact` Commandlet 走 `AngelscriptEditor/BlueprintImpact/`（`Arch_EditorTestDumpCollaboration.md` §二），分析 BP 资产对 .as 脚本的依赖——CppTests 不直接覆盖这条链路（覆盖在 `AngelscriptEditor/Tests/AngelscriptBlueprintImpact*Tests.cpp` 与 `Functional/Blueprint/`）。
- cooker pipeline 的 StaticJIT AOT 输出在 `StaticJIT/AOT/` 主题——CppTests 不直接验证 cook 产物（Plan 中有 `Angelscript.TestModule.CppTests.StaticJIT.PrecompiledData.*` 计划，目前未落地，前缀实测仍是 `TestModule.StaticJIT.*`）。

> 抽象描述：**CppTests 关心 Engine 实例的"in-memory invariants"**；cook / BlueprintImpact 关心"out-of-process artifacts"。两者在测试通道上是正交的。

---

## 八、决策树：什么时候写 CppTests，什么时候写别的

### 8.1 主决策树

```text
新需求 → 想要在测试里做什么？
   │
   ├── 我要验证脚本写出来后某行为正确（如"AS class A 的 BeginPlay 触发"）
   │     ▼ TestModule.<Theme>.*  (Functional/* / Bindings/* / HotReload/* / ...)
   │     ▼ 用 SharedClone + CQTest + FCoverageModuleScope
   │
   ├── 我要验证 AS 内核的"裸"行为（不经 UE bind 层）
   │     ▼ AngelScriptSDK/Angelscript{Native,ASSDK}*Tests.cpp
   │     ▼ 用 ASTEST_CREATE_ENGINE_NATIVE
   │
   ├── 我要验证 Editor 模块自身的内部行为
   │     ▼ AngelscriptEditor/Tests/  (前缀 Angelscript.Editor.*)
   │     ▼ 不要反向写到 Test 模块
   │
   ├── 我要验证 Runtime 模块的内部 API / 数据结构 / 状态计数器
   │     ▼ 当前落点：AngelscriptTest/Core/
   │     ▼ 前缀：Angelscript.TestModule.CppTests.<Section>.<Topic>
   │     ▼ 不写 BEFORE_ALL；每个 TEST_METHOD 独立 IsolatedFull + ContextStackGuard
   │     ▼ 复制 Engine.Extension / Engine.Hooks 现有 case 的 4 步开场白
   │
   └── 我想做"零 Engine 纯算法"测试（如 hash 函数 / 字符串 splitter）
         ▼ 当前不存在专用通道（理由见 §五.4）
         ▼ 临时方案：仍走 CppTests 起 IsolatedFull，断言裸算法
         ▼ 长期：开 OpenSpec 提案，要么拆 Runtime/Tests/ 子模块，要么引入 GoogleTest
```

### 8.2 命名段决策

```text
确定要写 CppTests 后，前缀第三段（CppTests 之后的第一个段）选什么？
   │
   ├── 测对象是 Engine 整体行为？        → CppTests.Engine.<Topic>
   │     例: CppTests.Engine.Extension / Engine.Hooks / Engine.TypeInterop
   │
   ├── 测对象是某个 Subsystem？         → CppTests.Subsystem.<Topic>  (规划中)
   │
   ├── 测对象是 Debug 协议？           → CppTests.Debug.<Sub>.<Topic>  (TestCatalog 计划)
   │     例: CppTests.Debug.Protocol / CppTests.Debug.Transport
   │
   ├── 测对象是 StaticJIT 产物？       → CppTests.StaticJIT.<Topic>  (TestCatalog 计划)
   │
   ├── 测对象是其他 Runtime 子系统？   → CppTests.<SubsystemName>.<Topic>
   │
   └── 不确定 → 默认 CppTests.Engine.<Topic>，PR 评审时再讨论
```

### 8.3 旧 case 是否要迁移？

如果你看到 `AngelscriptTest/Core/` 下某个 `Angelscript.TestModule.Engine.*` case 实质上做的是 CppTests 的活（典型如 `AngelscriptMultiEngineLifecycleTests.cpp` 的内部代码注释自承），**短期不要"为了对齐而改名"**：

- Automation 路径变更会破坏 CI / TestCatalog 中的"已编目"基线；
- Test report archive 历史记录也会断；
- 重命名收益（命名一致性）小于成本（CI 配置 + 编目同步）。

合适的迁移时机：**为某个 Runtime 模块做大改造时，连同相关 CppTests 一起整理**——这是 OpenSpec change 的事情，不是 ad-hoc 改一改。

---

## 九、当前覆盖缺口

### 9.1 已编目但未实现

`TestCatalog.md` 中预留的子前缀：

```text
计划态                                                        实现态
========================================================     ======
Angelscript.TestModule.CppTests.Debug.Protocol.*              不存在
Angelscript.TestModule.CppTests.Debug.Transport.*             不存在
Angelscript.TestModule.CppTests.Engine.TestEngine.*           不存在 (MultiEngineLifecycleTests 注释建议落点)
Angelscript.TestModule.CppTests.MultiEngine                   不存在 (实测前缀 TestModule.Engine.MultiEngine)
Angelscript.TestModule.CppTests.Engine.DependencyInjection    不存在 (实测前缀 TestModule.Engine.DependencyInjection)
Angelscript.TestModule.CppTests.Subsystem                     不存在
Angelscript.TestModule.CppTests.StaticJIT.PrecompiledData.*   不存在 (实测前缀 TestModule.StaticJIT.PrecompiledData)
```

### 9.2 应该有但没有的 Runtime 内部探针

按 Runtime 子系统列开，按"已有 CppTests 覆盖" / "缺口"标记：

| Runtime 子系统 | 已有 CppTests 探针 | 缺口（应补） |
|--------------|-------------------|------------|
| `FAngelscriptEngineExtensionRegistry` | ✓ Engine.Extension | — |
| `FAngelscriptEngine::GetHooks()` 委托表 | ✓ Engine.Hooks | hook 覆盖目前停留在 13 个委托中的 5-6 个；其他需补 |
| `FAngelscriptEngine::GetUnrealStructFromAngelscriptTypeId` | ✓ Engine.TypeInterop（legacy 前缀） | 应迁到 `TestModule.CppTests.Engine.TypeInterop` |
| `FAngelscriptEngineContextStack` | 隐式覆盖（每个 CppTests 都用） | 没有专门验证 SnapshotAndClear / RestoreSnapshot 的不变量 |
| `FAngelscriptModuleDesc` 生命周期 | ✗ | 应补"模块在引擎销毁时正确解绑 generated UClass"探针 |
| `FAngelscriptBindDatabase` cache 键 | 间接通过 Bindings/* | 应补"hash 组合器输入相同→键相同、输入不同→键不同"探针 |
| `FAngelscriptStaticJITRegistry` 注册表 | ✗ | 应补 JIT 函数指针 attach / detach 不变量 |
| `FAngelscriptCodeCoverage` 钩子 | 间接通过 Core/AngelscriptCodeCoverageTests | 应补"AddTestFrameworkHooks 在 missing FAutomationTestFramework 时优雅退出"探针 |
| Debug 协议 envelope framing | 计划中 (CppTests.Debug.Transport) | TestCatalog.md 已列、源文件未落 |
| Debug 协议消息 round-trip | 计划中 (CppTests.Debug.Protocol) | 同上 |

### 9.3 长期方向

如果仓库要把 CppTests 真正"独立成一个模块"，需要走 OpenSpec 提案：

1. 在 `AngelscriptRuntime/` 下拆出 `AngelscriptRuntime.Build.cs` 的 Developer 分片（或者新建 `AngelscriptRuntimeTests` Developer 模块）；
2. 把 CQTest 依赖搬到该 Developer 模块；
3. 把 `AngelscriptTest/Core/` 下 3 个 CppTests 文件物理迁过去（不改前缀）；
4. 同步更新 `TestConventions.md §1` 表格的"理想落点"（届时变成"现实落点"）。

短期内**不建议做这件事**——拆模块的副作用大于收益。

---

## 附录 A：现存 CppTests 速查

### A.1 三个文件 + 测试方法清单

```text
AngelscriptTest/Core/
├── AngelscriptEngineExtensionRegistryTests.cpp
│   └── "Angelscript.TestModule.CppTests.Engine.Extension"
│       ├── EmptyRegistryReplayIsANoop
│       ├── LateRegistrationReplaysToCurrentEngine
│       ├── UnregisterStopsFutureReplay
│       └── (其他 method, ~5-7 个)
│
├── AngelscriptEngineHooksTests.cpp
│   └── "Angelscript.TestModule.CppTests.Engine.Hooks"
│       ├── PreCompileHookDoesNotLeakBetweenEngines
│       ├── RuntimeHookDelegatesAreEngineOwned
│       ├── RuntimeBindCallSitesUseCurrentEngineHooks
│       └── (其他 method, ~3-5 个)
│
└── AngelscriptEngineTypeInteropTests.cpp
    └── "Angelscript.CppTests.Engine.TypeInterop"   (legacy 前缀)
        └── GetUnrealStructFromTypeIdRejectsNonStructAndPreservesPlainStructs
```

### A.2 CppTests 标准开场白模板

```cpp
// ============================================================================
// 文件: AngelscriptTest/Core/Angelscript<Topic>Tests.cpp
// 角色: CppTests 标准开场白（每个 TEST_METHOD 复制粘贴）
// ============================================================================
namespace AngelscriptTest_Core_Angelscript<Topic>Tests_Private
{
    struct F<Topic>ContextStackGuard
    {
        TArray<FAngelscriptEngine*> SavedStack;
        F<Topic>ContextStackGuard()  { SavedStack = FAngelscriptEngineContextStack::SnapshotAndClear(); }
        ~F<Topic>ContextStackGuard() { FAngelscriptEngineContextStack::RestoreSnapshot(MoveTemp(SavedStack)); }
        void DiscardSavedStack() { SavedStack.Reset(); }
    };
}

TEST_CLASS_WITH_FLAGS(FAngelscript<Topic>Tests,
    "Angelscript.TestModule.CppTests.<Section>.<Topic>",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
    TEST_METHOD(SomeInvariant)
    {
        using namespace AngelscriptTest_Core_Angelscript<Topic>Tests_Private;
        F<Topic>ContextStackGuard ContextGuard;
        AngelscriptTestSupport::DestroySharedTestEngine();
        if (FAngelscriptEngine::IsInitialized())
            AngelscriptTestSupport::FAngelscriptTestEngineScopeAccess::DestroyGlobalEngine();
        ContextGuard.DiscardSavedStack();

        ON_SCOPE_EXIT
        {
            FAngelscriptEngineContextStack::SnapshotAndClear();
            if (FAngelscriptEngine::IsInitialized())
                AngelscriptTestSupport::FAngelscriptTestEngineScopeAccess::DestroyGlobalEngine();
            AngelscriptTestSupport::DestroySharedTestEngine();
        };

        TUniquePtr<FAngelscriptEngine> Engine = AngelscriptTestSupport::CreateFullTestEngine();
        if (!TestRunner->TestNotNull(TEXT("..."), Engine.Get())) return;
        FAngelscriptEngineScope EngineScope(*Engine);

        // ★ 开始探针
        // 1) 触发 Runtime 内部某条路径
        // 2) 直接断言内部计数器 / 内部对象
    }
};
```

### A.3 CppTests 常用的 Runtime 内部访问点

| 访问点 | 用途 | 暴露方式 |
|-------|------|--------|
| `FAngelscriptEngine::DestroyGlobal` | 摧毁 global engine | 公开 static |
| `FAngelscriptEngine::TryGetGlobalEngine` | 读 global engine | 公开 static |
| `FAngelscriptEngine::IsInitialized` | 全局 init 标志 | 公开 static |
| `FAngelscriptEngineContextStack::SnapshotAndClear` | 上下文栈快照 + 清空 | 公开 static |
| `FAngelscriptEngineContextStack::RestoreSnapshot` | 还原快照 | 公开 static |
| `FAngelscriptEngine::GetHooks()` | 13+ 委托表 | 公开 method |
| `FAngelscriptEngine::GetUnrealStructFromAngelscriptTypeId` | typeId → UStruct 映射 | 公开 method |
| `FAngelscriptEngineExtensionRegistry::Get()` | 全局 extension 注册表 | 单例 |
| `FAngelscriptTestEngineScopeAccess` | 友元访问点（destroy global） | Test 模块友元 |
| `FAngelscriptMultiEngineTestAccess` | Multi engine 友元（戳 ActiveModules / asCScriptEngine 内部） | Test 模块友元 |
| `FAngelscriptEngine::GetLocalPooledContextCountForTesting` | testing-only context pool 计数 | 公开 static for testing |

### A.4 不应该在 CppTests 里出现的写法

```text
✗ TEST_CLASS_WITH_FLAGS(..., "Angelscript.TestModule.CppTests.<X>") + BEFORE_ALL/AFTER_ALL
   理由：CppTests 不应在 case 之间共享 engine 状态。

✗ ASTEST_CREATE_ENGINE() 后跑业务断言
   理由：SharedClone 引擎不适合 internal probe；应改 CreateFullTestEngine。

✗ 用 FCoverageModuleScope + ExpectGlobalInt 做断言
   理由：CppTests 不验证 .as 脚本返回值；应直接戳 Runtime API。

✗ 在 .as 脚本里写复杂逻辑然后跑
   理由：脚本仅用于"喂数据"打开类型表；写 Entry() { return 0; } 即可。

✗ 跨多个 TEST_METHOD 共享一个 IsolatedFull 引擎
   理由：每个 TEST_METHOD 自带 ContextStackGuard + DestroyGlobal 是 contract。
```

---

## 附录 B：决策树（一页速查）

```text
要不要写 CppTests？
├── 你要验证什么？
│   ├── 脚本端业务行为 → TestModule.<Theme>.*
│   ├── AS 裸内核 → AngelScriptSDK/AngelscriptNative*Tests.cpp
│   ├── Editor 内部 → AngelscriptEditor/Tests/
│   └── Runtime 内部 API / 数据结构 → CppTests
│
├── 写 CppTests 时
│   ├── 引擎档位 → 一律 CreateFullTestEngine（IsolatedFull）
│   ├── 状态隔离 → ContextStackGuard + DestroySharedTestEngine + DestroyGlobalEngine 四步
│   ├── 框架   → CQTest TEST_CLASS_WITH_FLAGS + TEST_METHOD（不写 BEFORE_ALL）
│   ├── 脚本   → 极少，仅在喂数据时；Entry() { return 0; } 即可
│   └── 断言   → 直接戳 Runtime API 或 friend 访问点
│
├── 命名前缀
│   ├── 新写 → Angelscript.TestModule.CppTests.<Section>.<Topic>
│   ├── 段名 → Engine / Subsystem / Debug / StaticJIT / ...
│   └── 旧 case Angelscript.CppTests.<X>.<Y> → 不强制改名
│
└── 物理位置
    ├── 当前 → Plugins/Angelscript/Source/AngelscriptTest/Core/
    ├── 理想 → Plugins/Angelscript/Source/AngelscriptRuntime/Tests/（占位中）
    └── 短期不迁移：拆模块成本大于收益
```

---

## 附录 C：避坑清单

1. **不要把字面"runtime C++ unit tests"理解成"零 Engine 单测"**——当前所有 CppTests 都需要 IsolatedFull 引擎，详见 §五。
2. **不要在 CppTests 里写 `BEFORE_ALL` / `AFTER_ALL`**——CppTests 的 case 之间需要强隔离，类级共享 fixture 反而是反 pattern。
3. **不要用 `ASTEST_CREATE_ENGINE()`**——SharedClone 持久 scope 会让内部状态计数器在 case 间脏。一律 `CreateFullTestEngine()`。
4. **不要省略 `ContextStackGuard + DestroySharedTestEngine + DestroyGlobalEngine + DiscardSavedStack` 四步开场白**——少一步都可能让残留 engine 污染探针。
5. **不要直接调 `FAngelscriptEngine::DestroyGlobal()`**——通过 `FAngelscriptTestEngineScopeAccess::DestroyGlobalEngine()` 友元入口；前者目前 public 但语义脆弱，未来可能改 private。
6. **不要把 `Angelscript.TestModule.Engine.*` case 误认作 CppTests**——`AngelscriptTest/Core/` 下 31 个 `TestModule.Engine.*` case 不属于本族；只有显式带 `CppTests.` 段的 3 个文件算。
7. **不要为了"对齐前缀"重命名旧 case**——CI / TestCatalog / Test report archive 全部依赖现有 Automation 路径；改名是 OpenSpec change 级别的事。
8. **不要在 CppTests 里编大块脚本**——脚本仅在"喂数据"角色出现，`Entry() { return 0; }` 这种最小函数即可。验证脚本行为应去 `TestModule.<Theme>.*`。
9. **不要把 BlueprintImpact / cooker / CSV Dump 验证写进 CppTests**——它们是 Editor / commandlet 通道的事，与 in-memory invariants 正交。
10. **不要新建 "CppTests/" 物理目录**——当前 baseline 物理位置就是 `AngelscriptTest/Core/`；把 CppTests case 拆出来反而会破坏 Build.cs 的 PrivateIncludePaths 假设。
11. **不要混用 `Angelscript.CppTests.*` 与 `Angelscript.TestModule.CppTests.*`**——同义但 TestConventions 已规定新写带 `TestModule.` 段；旧 case 容忍不强制改名。
12. **不要把 `Angelscript.Editor.*` 的内部测试写到 Test 模块**——它们住在 `AngelscriptEditor/Tests/`，由 Editor 模块自己的 Build.cs 守卫；Test 模块对 Editor 是 private + bBuildEditor 双守卫依赖（`Test_Layering.md` §二.4）。

---

## 小结

- **CppTests 是 Runtime 内部测试通道**，目标是验证 Runtime 数据结构 / 内部 API / 状态计数器的不变量，与 `TestModule.<Theme>.*` 的脚本级集成测试形态正交。
- **现实 ≠ 理想**：AGENTS.md / TestConventions.md 的字面"runtime C++ unit tests" / "纯 C++ 数据结构验证"是简化口径，真正的 CppTests **仍依赖 IsolatedFull 引擎实例**——只是不依赖 `bSkipInitialCompile=false` 的启动期 .as 编译。
- **物理位置错位**：`TestConventions.md §1` 指明 `AngelscriptRuntime/Tests/` 是理想落点，但该目录当前不存在；3 个 CppTests 文件全部住在 `AngelscriptTest/Core/`。短期不会拆 Runtime 子模块。
- **当前覆盖矩阵很小**：3 个文件、2 种前缀变体、约 730 行代码——`Engine.Extension` / `Engine.Hooks` / `Engine.TypeInterop`。同目录另有 31 个 `TestModule.Engine.*` 实质上做 CppTests 的活但未带 `CppTests.` 段。
- **形态稳定**：CQTest `TEST_CLASS_WITH_FLAGS + TEST_METHOD`（不写 `BEFORE_ALL`）+ 四步开场白（ContextStackGuard / DestroySharedTestEngine / DestroyGlobalEngine / DiscardSavedStack）+ `CreateFullTestEngine` + 直接戳内部 API。
- **缺口很多**：StaticJIT 注册表、Bind 数据库 hash 键、Debug 协议 envelope、ModuleDesc 生命周期等都缺探针；`TestCatalog.md` 列出的 `Debug.Protocol` / `Debug.Transport` / `StaticJIT.PrecompiledData` 子前缀均未落地。
- 与 P5 Test_ 族其他三篇的关系：`Test_Layering` 给骨架、`Test_Infrastructure` 给 Helper、`Test_TopicClusters` 给主题簇映射、本文给"内部测试通道"的语义边界与现状对账，至此 P5 Test_ 族落幕。
