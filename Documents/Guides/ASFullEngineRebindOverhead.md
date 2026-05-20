# Full Engine 重建时的 Bind 重做开销分析

> 关联文档：
> - `Documents/Guides/ASTestSuiteMemoryPeakRootCause.md`（峰值根因）
> - `Documents/Guides/ASBindAllocationInventory.md`（bind 阶段所有分配点清单）
> - `Documents/Guides/ASEngineMemoryAnalysis.md`（早期内存分析）

## 0. 问题陈述（一句话版）

> **Bind 函数本身（`FAngelscriptBinds::FBind` 注册表）确实是进程级 static、一次性注册、永远不变。但 bind 的"执行结果"（5000+ 个 `asCObjectType` / `asCScriptFunction` / 函数签名 / 属性 / 枚举……）必须落在某个具体的 `asCScriptEngine` 上，而每个 `FAngelscriptEngine`（Full 模式）持有一个独立的 `asCScriptEngine`。**
>
> 测试套件每发一次 `ASTEST_CREATE_ENGINE_FULL()`，就要把所有 lambda **再跑一遍**，再造一份 SDK 对象图、再分配 ≈ 1.2 GB 工作内存（实测，见 `ASTestSuiteMemoryPeakRootCause.md` 附 B）。

用户视角的"很不合理"准确命中了真问题：**全量重做的不是注册"指令"，是注册"产物"**。

---

## 1. 现状架构：注册表 vs 注册产物

### 1.1 注册表（process-scoped, 永久不变）

```128:144:Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
struct FBindFunction
{
	FName BindName;
	int32 BindOrder;
	TFunction<void()> Function;
	...
};

static TArray<FBindFunction>& GetBindArray()
{
	static TArray<FBindFunction> BindArray;
	return BindArray;
}
```

`AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_XXX(...)` 这种全局 static 变量，在 **C++ 全局初始化阶段**就把所有 `~150+ lambda` 塞进进程级 `BindArray` 里。一旦塞满，进程生命周期内**永不重建**。

这部分对应用户说的"全局不变" —— **完全成立**。

### 1.2 注册产物（per-`asCScriptEngine`, 全量重建）

每个 `FAngelscriptEngine::InitializeWithoutInitialCompile()` 都要做：

```1052:1058:Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
{
	FAngelscriptEngineScope ScopedTestingEngine(*this);
	BindScriptTypes();
}
GameThreadTLD->primaryContext = CreateContext();
bIsInitialCompileFinished = true;
```

`BindScriptTypes()` 内部 `CallBinds()` 把 `BindArray` 中**每一个 lambda 重新执行一次**，每个 lambda 都通过 `this->Engine->RegisterObjectType / RegisterObjectMethod / ...` 在**当前 asCScriptEngine 实例**上分配：

| 产物 | 量级 | 归属 | 来源类别（见 `ASBindAllocationInventory.md`） |
|------|------|------|------|
| `asCObjectType` | ~5000+（每个 UClass/UStruct/Delegate）| AS SDK 内部，挂在 `asCScriptEngine::registeredTypes` | A |
| `asCScriptFunction` | ~30000+（每个方法 / 行为）| AS SDK 内部，挂在 `asCScriptEngine::scriptFunctions` | A |
| `asCDataType / asCObjectProperty` | ~50000+ | AS SDK 内部 | A |
| `FUObjectType` shared | ~5000+ | plugin 适配层（per-engine 容器） | B1 |
| `FBlueprintEventSignature` | ~3000+ × ≈1.5 KB | plugin 适配层（永久存活，写入 `GBlueprintEventsByScriptName`，**process-scoped 但每 cycle 重建+丢弃旧条目**）| B5, C2 |
| `FInterfaceMethodSignature` | ~1000+ | plugin 适配层 | B7 |
| Phase 2A/2B 临时 array（`ClassesToBind`, `FunctionPreps`） | ~50-80 MB 峰值 | bind 期临时 | E |

**实测瞬时增长**：单次完整 bind 拉高进程 WS ≈ **1248 MB**（`ASTestSuiteMemoryPeakRootCause.md` 附 B.3，t=7.4s → t=22.3s）。

> 关键洞察：这 1.2 GB 的 99% 是"用 lambda 执行结果填充一个全新的 `asCScriptEngine`"，本质上是把同一份元数据**反复物化一次**，原始来源（UClass 反射、Bind_*.cpp 静态代码）**完全没动**。

---

## 2. 为什么不能直接 "share `asCScriptEngine`" —— SDK 的限制

理想方案是：**一次 bind，多个 wrapper 共用同一个 `asCScriptEngine`，每个 wrapper 隔离自己的 `asIScriptModule` / `asIScriptContext` / 用户态状态**。

AS SDK 是否原生支持？

| 能力 | SDK 是否原生支持 |
|------|------------------|
| `asCreateScriptEngine()` | 创建新引擎，**无 `Clone()` / `DeepCopy()` API** |
| `asIScriptEngine::RegisterObjectType(...)` | 只能在**单个引擎实例**上累积注册，无导出 / 导入 |
| `asIScriptModule` 隔离 | ✅ 一个引擎可挂 N 个 module，互不干扰 |
| `asIScriptContext` 隔离 | ✅ 多 context 跑同一引擎是 SDK 第一公民设计 |
| 全局变量隔离 | ⚠ 跟 module 走，但跨 module 的 namespace / 单例需要自管 |

也就是说：**多个 wrapper 共享 ScriptEngine 在 AS SDK 层完全可行，只是需要在 plugin 层管好"什么应该 per-wrapper、什么应该 per-asCScriptEngine"。**

---

## 3. 项目已经实现的两种 Reuse 路径

仔细看 `FAngelscriptEngine` 的创建模式枚举：

```757:805:Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
EngineInstance->CreationMode = EAngelscriptEngineCreationMode::Full;
...
EngineInstance->bOwnsEngine = true;
EngineInstance->InitializeWithoutInitialCompile();   // ← 这里跑 BindScriptTypes
...

// Clone 路径：
EngineInstance->CreationMode = EAngelscriptEngineCreationMode::Clone;
EngineInstance->SourceEngine = Source.GetSourceEngine() != nullptr ? Source.GetSourceEngine() : &Source;
EngineInstance->bOwnsEngine = false;
EngineInstance->SharedState = Source.SharedState;
...
```

### 3.1 Clone Engine（**已存在、可用、未完全推广**）

- `CreateCloneFrom(Source, ...)` 完全**跳过** `asCreateScriptEngine` 和 `BindScriptTypes`
- 新 wrapper 直接复用 source 的 `Engine` 指针 + `PrimaryContext` + `PrecompiledData` + `StaticJIT` + `DebugServer`
- 仅自身拥有：`SharedState->ActiveCloneCount` 计数、wrapper 内部 wrapper 状态、对其上挂的 module 隔离
- `bOwnsEngine = false` —— 析构时**不删 ScriptEngine**，引用计数回落到 source

**内存成本**：≈ 0（一个 wrapper 几 KB），完全没有 SDK 对象分配。

### 3.2 Shared Test Engine（已存在、用于 CQTest 主路径）

```208:229:Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h
inline FAngelscriptEngine& GetOrCreateSharedCloneEngine()
{
	TUniquePtr<FAngelscriptEngine>& SharedCloneEngine = GetSharedTestEngineStorage();
	...
	if (!SharedCloneEngine.IsValid())
	{
		SharedScope.Reset();
		SharedCloneEngine = CreateIsolatedFullEngine();   // ← 首次仍是 Full
		...
	}
	...
}
```

- 第一次 `ASTEST_CREATE_ENGINE()` → `AcquireCleanSharedCloneEngine()` 创建一个 `thread_local` 全引擎单例（仍走 Full 路径 bind 一次）
- 之后所有 `ASTEST_CREATE_ENGINE() / ASTEST_GET_ENGINE()` **复用同一个单例**，配合 `ResetSharedCloneEngine` 清掉 module 状态
- 进程内只 bind **1 次**，后续 N 个测试零 bind 开销

> 命名误导：变量叫 `SharedCloneEngine`，但实际创建用的是 `CreateIsolatedFullEngine`，因为这是 reuse Full 实例 wrapper、而不是 wrapper 共享 ScriptEngine 的 Clone 模式。这是历史命名遗留。

### 3.3 现状对比表

| 宏 | 函数 | 引擎实例数 | bind 次数（进程内） | 实测开销 |
|------|------|----------|-------------|---------|
| `ASTEST_CREATE_ENGINE()` | `AcquireCleanSharedCloneEngine` | 1（thread_local 单例） | **1**（首次） | 启动一次性 ≈ 1.2 GB；后续测试 ~0 |
| `ASTEST_GET_ENGINE()` | `GetOrCreateSharedCloneEngine` | 同上 | 同上 | 同上 |
| `ASTEST_CREATE_ENGINE_FULL()` | `AcquireTransientFullTestEngine` | **每次 new、销毁旧的** | **每次都 bind 一遍** | 每次 ≈ 1.2 GB 瞬时增长 |
| `ASTEST_CREATE_ENGINE_NATIVE()` | 直接 `asCreateScriptEngine` | 1 | **0**（裸 SDK，不跑 plugin bind） | KB 级 |
| 程序代码内 `CreateCloneFrom(Source)` | `CreateCloneFrom` | wrapper N 个 + ScriptEngine 1 个 | **0** | KB 级（wrapper 自身） |

---

## 4. 用户的"很不合理"到底指向哪里

把 `ASTestSuiteMemoryPeakRootCause.md` 的 12 GB 峰值拆开：

```
12.9 GB 进程峰值
 ├─ 4-5 GB UE Editor 自身（FNamePool / UObject / 反射元数据 / RHI ...）
 │     └─ 一次性、process-static
 ├─ 1.2 GB AS SDK + 适配层 bind 产物（首个 Full engine）
 │     └─ 一次性 OK
 └─ ≈ 6-7 GB「重复 bind 累积 + mimalloc 不归还」
       ├─ 这是 ASTEST_CREATE_ENGINE_FULL() 在套件里被反复调用导致的
       └─ 用户认为"不合理"的就是这部分
```

**真问题不是"bind 慢"，是"bind 在不该重做的地方被重做了"。**

具体来说，套件里仍走 `ASTEST_CREATE_ENGINE_FULL()` 的测试主要分两类：

| 类别 | 代表测试 | 真的需要 Full 吗？ |
|------|----------|-------------------|
| **真正需要全新 SDK 状态** | `Angelscript.TestModule.Engine.Isolation` 的 lifecycle/bind 自测、HotReload self-test、bind 顺序回归 | ✅ 需要（测的就是 bind 行为本身）|
| **历史惯性继续用 Full** | 一些独立性测试套件、特定 GC / 类型注册场景 | ⚠ 大多数可以切到 Shared+Reset 或 Clone | 

第 1 类无法消除，但 **第 2 类是优化空间**。

---

## 5. 优化方案分级

### 5.1 第一级：把可以切的 Full 改成 Shared/Clone（短期、零风险）

> 收益：套件峰值有望从 12.9 GB → ~6-8 GB；测试时长可能下降（少几次 bind）

**做法**：审计所有 `ASTEST_CREATE_ENGINE_FULL` 调用点。判断准则：

| 该测试做什么 | 推荐宏 |
|------------|--------|
| 编译并执行脚本、看类型注册查询、调 `RegisterFor*Bindings` 之外的功能 | `ASTEST_CREATE_ENGINE()` + `ResetSharedCloneEngine` |
| 需要在多个 wrapper 内并发跑 module / 看 wrapper 级状态隔离 | `CreateIsolatedCloneEngine()` |
| 测 bind 流程本身、`asCreateScriptEngine` 全新行为、`bGeneratePrecompiledData` 路径 | 保留 `ASTEST_CREATE_ENGINE_FULL()` |
| 测 SDK 原生 API（不需要 plugin bind） | `ASTEST_CREATE_ENGINE_NATIVE()` |

需要审计的入口（粗略 grep 即可）：

```powershell
rg "ASTEST_CREATE_ENGINE_FULL\(\)" Plugins/Angelscript/Source/AngelscriptTest --files-with-matches
```

**任何切换都要配 `ResetSharedCloneEngine` 验证**（确认 module / FBlueprintEventSignature 残留得到清理）。

### 5.2 第二级：让 Clone Engine 替代更多 Full（中期、需测试基础设施改造）

> 收益：bind 次数降到全套件 ≤ 2-3 次

把"创建独立 Full 引擎"的语义放进 `CreateIsolatedCloneEngine` —— 多个 wrapper 共享同一个底层 `asCScriptEngine`、但每个 wrapper 看到独立的 module 命名空间。

依赖项：
- 确认 plugin 内所有 "per-wrapper" 状态确实没漏写到 "per-engine"（已基本做到，`SharedState` 与 wrapper 字段已分离）
- `ResetSharedCloneEngine` 现有的清理逻辑（detached UASClass / module discard / GC）需要再覆盖 clone 间的交叉污染场景

### 5.3 第三级：减少 SDK 内部"每次都新分配"的对象（长期、改 SDK fork）

仅当一二级被推满仍有诉求时考虑。代表性手术：

| 改造 | 收益 | 风险 |
|------|------|------|
| 把 `FBlueprintEventSignature` 缓存到 process-scoped 池，按 (UFunction*, UClass*) 复用 | 节省 B5 部分（每 cycle ~3-5 MB 长存）| 中 — 需要 UClass GC 时 invalidate |
| 把 Phase 2A/2B 临时 `ClassesToBind` / `FunctionPreps` 改成 thread_local pool，bind 结束 `Reset()` 而非释放 | 减少 50-80 MB 峰值短期 churn | 低 |
| 让 SDK fork 暴露 `asIScriptEngine::CloneRegistrations(SourceEngine)` API，把 source 的 registeredTypes / scriptFunctions 浅复制到新引擎 | 真正消除"每个 Full engine 重 bind 1.2 GB"的根因 | **高** — 涉及 SDK 内部 string 池、accessMask、refcount，需要审慎 |
| 走 `bGeneratePrecompiledData` + 加载 precompiled 路径，跳过 `BindScriptTypes` 的核心循环 | 跳过 bind 的可能性（看 SDK 是否真的能仅靠 precompiled data 复活类型表）| 高 — 项目内目前 precompiled data 主要用于脚本 module 加速，不取代 bind |

### 5.4 第四级：mimalloc 配置（已被实测验证，建议作为常态运行配置）

> 实测见 `ASTestSuiteMemoryPeakRootCause.md` 附 B：`mi.MemoryResetDelay=0` 让进程末态 WS 从 2148 MB → 269 MB（**-87.5%**），峰值无影响。

把 `Tools/RunTestSuite.ps1` 默认追加 `-ini:Engine:[ConsoleVariables]:mi.MemoryResetDelay=0`，对**外部观察到的工作集**会有显著改善（峰值不变，但活动期之外内存能尽快归 OS）。这是和 bind 重做正交的一项独立优化。

---

## 6. 设计原则上的建议

| 原则 | 当前是否符合 |
|------|-----------|
| Bind 注册表（lambda 数组） process-static、**一次构建** | ✅ 已成立 |
| Bind 产物（SDK 对象图）per-`asCScriptEngine`、**按需 reset 而非按需重建** | ⚠ 部分（Shared 路径已做到，Full 路径每次重建）|
| 测试需要"干净状态"应优先用 reset，而非 reinit | ⚠ 大量测试仍走 reinit |
| `ASTEST_CREATE_ENGINE_FULL()` 留给真正测试 bind 流程的少数测试 | ⚠ 当前散落使用，缺少标注 |
| 测试套件总 bind 次数 ≤ 3 | ❌ 当前是几十次 |

---

## 7. 结论

1. **你的直觉是对的**：bind 函数本身是全局静态、一次性的；但 bind **结果**确实在每次 Full engine 创建时被全量重物化，这是 ≈ 1.2 GB / cycle 的真实成本。
2. **架构上其实已经留好出路**：`Clone Engine` 和 `Shared Test Engine` 两条 reuse 路径都已存在并稳定运转 —— 主路径 `ASTEST_CREATE_ENGINE()` 已经做到全套件只 bind 1 次。
3. **痛点是"测试侧分流"**：仍走 `ASTEST_CREATE_ENGINE_FULL()` 的测试里有相当一部分是历史惯性，**纯粹从架构上没必要走 Full**。
4. **最有性价比的下一步**（不需要碰 AS SDK fork）：
   - 审计 `ASTEST_CREATE_ENGINE_FULL` 调用点，按 §5.1 表分类，把可以切的切到 Shared/Clone
   - 套件默认追加 `mi.MemoryResetDelay=0`
   - 把"何时该用 Full / Clone / Shared / Native"列入 `Documents/Guides/TestConventions.md`，加 code review 检查
5. **更深一级的优化**（SDK fork 改动）才是真正的"零 bind"路径，但收益边际，且和现有 fork 策略冲突，按需推迟。

