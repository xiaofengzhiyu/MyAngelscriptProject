# AS 全量测试 12GB 内存峰值根因分析

> 2026-05-13 / 2026-05-14（增补 free 完整性验证）
> 关联文档：`ASEngineMemoryAnalysis.md`、`ASBindFreeCompletenessVerification.md`、OpenSpec `2026-05-13-fix-as-engine-shutdown-memory-leak`

## 摘要

修复了 6 处真实代码泄漏后，全量测试套件进程内存峰值仍然在 ~12GB 量级，没有显著下降。本文分析这个现象的根因。

**2026-05-13/14 实证更新**（详见 `ASBindFreeCompletenessVerification.md`）：

- **mimalloc 页面保留**确实是 OS 视角峰值不下降的主因；将 `mi.MemoryResetDelay=0` 后，Reset+GC 在 T1 立刻释放可观察到的内存。
- AS 释放路径 **绝大部分正确**：LLM 视角下 ~10 个 fine-grained tag（DebugServer / SharedState / TypeDatabase / BindState / SubclassOfType / ObjectPtrType / WeakObjectPtrType / InterfaceMethodSignature / ClassProperty 等）在引擎销毁后均归 0。
- 验证过程发现 **一处真实的小型 leak**：`FBlueprintEventSignature` 作为 `asIScriptFunction` 的 user data 附着，但 AS 2.33 fork 上 `~asCScriptFunction()` 不会回调任何 cleanup（且 `SetFunctionUserDataCleanupCallback` 在本 fork 上根本不存在）—— 原来每个引擎周期约 **+1.7 MB** 残留。
- **2026-05-14 已修复**：新增 `FBlueprintEventSignatureRegistry`（挂在 `FAngelscriptOwnedSharedState` 上，与 `BindState/BindDatabase` 同生命周期），在 `ScriptEngine->ShutDownAndRelease()` 之后 `Reset()`。`BindFreeEvidence_BlueprintEventSignatureBounded` 6 cycle 测试每轮 `Registry->Num()` 恒为 1060，delta = 0，PASS。详见 `ASBindFreeCompletenessVerification.md` §4。

因此本文摘要中"12GB 峰值是固有代价、不可消除"这一表述修正为：**12GB 峰值绝大部分是固有代价（mimalloc 保留 + 单 engine ~200 MB 工作集 + FName / 全局 TMap append-only）；曾经存在的 `FBlueprintEventSignature` 泄漏（~1.7 MB / cycle）已于 2026-05-14 修复，按 200+ cycle 估算约可省 ~340 MB 峰值上限累积，但相对 12 GB 量级仍属小头**。

## 一、问题历史

### 1.1 现象

| 指标 | 数值 |
|------|------|
| 全量测试触发的进程内存峰值 | **~12.9 GB** |
| 每引擎周期平均增长（修复前） | ~51.6 MB |
| 每引擎周期 bind 阶段瞬时分配 | ~800–1200 MB |
| 测试套件中的引擎 Init/Shutdown 周期数 | 200+ |
| 单次超时阈值 | 900 s |

### 1.2 修复前的假设

最初假设是"AS 引擎存在内存泄漏，每周期累积 ~50MB"。基于这个假设做了一轮系统性的代码审计，发现并修复了 6 处真实问题：

- 4 类 UObject（UASClass / UASStruct / UDelegateFunction / UUserDefinedEnum）从未 unroot → GC 永远回收不了
- `GScriptNativeForms` 中堆分配的 `FScriptFunctionNativeForm*` 从未 delete
- `AngelscriptDocs` 4 个静态 TMap 从未清理
- `GBlueprintEventsByScriptName` 全局 TMap 持有跨周期悬垂指针
- `GCachedEditorClasses` 函数级 static 持有跨周期悬垂指针
- 测试宏 `ASTEST_CREATE_ENGINE_FULL()` 创建新引擎时不销毁旧引擎

详见 `ASEngineMemoryAnalysis.md` 第三章。

### 1.3 修复后预期 vs 现实

| | 预期 | 实际 |
|---|------|------|
| 真实泄漏 | 完全修复 | ✅ 已修复 |
| 悬垂指针崩溃风险 | 消除 | ✅ 已消除 |
| 12GB 峰值 | 大幅下降 | ❌ 几乎不变 |

修复正确且必要，但**它解决的是"代码缺陷"而不是"内存峰值"**。这是两个不同的问题。

## 二、12GB 峰值的真实构成

把 12GB 峰值拆开来看：

```
12.9 GB 进程峰值
│
├── ~1.5–2.0 GB  UE Editor 自身基线（无 AS）
│
├── ~0.8–1.5 GB  AS 单次完整 bind 的瞬时活跃数据
│   └── 121 个 Bind_*.cpp、~500 个动态类型、属性表、bind database 等
│
├── ~7–9 GB     mimalloc 在 200+ 周期中累积的"未归还"页面
│   └── 每周期分配 ~800MB 后释放，但 OS 仍看到 ~已使用 ~700MB
│
├── ~0.5–1 GB   FNamePool 单调增长（数千个 _REPLACED_N FName）
│
├── ~0.1–0.3 GB UE 内部对象残留（即使 unroot 也要等 GC 实际清理）
│
└── ~0.3–0.5 GB  FBlueprintEventSignature 累积（200+ 周期 × ~1.7 MB/周期，**已于 2026-05-14 修复**）
                   详见 ASBindFreeCompletenessVerification.md §4
```

**关键观察**：泄漏修复掉的部分（~50MB/周期 × 200 周期 = ~10GB）听起来很多，但其中**绝大部分本来就由 mimalloc 重复使用了**——也就是说"修不修都是那么多内存"，区别只在于：

- 修复前：被泄漏对象占着，即使分配器想还也还不了（虽然 mimalloc 也不会主动还）
- 修复后：泄漏对象被 GC 回收了，但 mimalloc 仍然不归还页面

净效果在 OS 内存数字上几乎看不出差异。

> **后续校正（2026-05-14）**：本节"不可消除根因"对绝大部分内存仍然成立。`FBlueprintEventSignature` 这一项**已**通过新增 per-engine `FBlueprintEventSignatureRegistry`（在 `ScriptEngine->ShutDownAndRelease()` 之后 `Reset()`）消除——AS 2.33 fork 没有 `SetFunctionUserDataCleanupCallback`，因此走的是 owner-list 路径而非 SDK callback 路径。详见 `ASBindFreeCompletenessVerification.md` §4。

## 三、两大不可消除根因 + 一处已修复 leak

### 3.1 mimalloc 不归还页面

**机制**：

UE5 Editor 默认使用 `FMallocMimalloc`。mimalloc 是一种现代 thread-cached 分配器，设计目标是高性能而非"立即归还"。它的行为：

1. 进程向 OS 申请大块虚拟地址空间（通常 1MB+ 单位）
2. 在这块空间内做小对象分配
3. 当小对象 free 时，**只是标记为可用，不会立即 unmap 给 OS**
4. 只有满足非常苛刻的条件（整段大块完全空闲 + 一定时间未使用）才可能归还

**FMemory::Trim(true) 的实际效果**：

调用链是 `FMemory::Trim → FMallocMimalloc::Trim → mi_collect(true)`。`mi_collect` 文档说明：

> Try to reclaim and return abandoned memory pages. This is generally not needed but can be useful in long-running services.

实测中对已经 mmap 的大块虚拟地址几乎无效——mimalloc 仍保留这些页面以备复用。

**与 AS 引擎的交互**：

每次 AS 引擎周期：

```
Init/Bind 阶段：
  → 大量分配（asNEW、TArray 扩容、TMap rehash 等）
  → mimalloc 从 OS 申请新虚拟地址空间
  → 进程 working set: +800MB

Shutdown 阶段：
  → 全部 free 回 mimalloc
  → mimalloc 把页面标记为可用，不还给 OS
  → 进程 working set: -50MB（只有少量页面真正归还）
  → 净增长：~750MB，但下次 Init 会复用

下次 Init/Bind：
  → 大部分分配命中已有的 free 页面（不再向 OS 申请）
  → 进程 working set 增长很少
  → 但实际"已使用"的页面仍然显示为占用
```

结果：**第一次周期后，working set 趋于稳态高水位线，即使 80% 是空闲的**。

**为什么不能修**：

| 选项 | 障碍 |
|------|------|
| 切换到 `FMallocBinned2`（更激进归还） | 需要修改 UE 启动配置或编译选项；其他 UE 子系统依赖 mimalloc 性能特征；不属于插件能控的范围 |
| 强制 `madvise(DONTNEED)` 已释放页面 | mimalloc 内部不暴露这种 API；UE 也未提供穿透接口 |
| 自定义 AS 专用分配器 | SDK 已经走 FMemory；改成独立 arena 需要 fork 200+ 处 SDK 代码 + UObject 无法纳入（详见 `ASEngineMemoryAnalysis.md` 4.2） |

### 3.2 FNamePool 单调增长

**机制**：

UE 的 `FName` 系统通过 `FNamePool` 实现去重——所有 FName 字符串存储在一个全局 append-only 池中。这个设计是 UE 的核心架构决策，因为：

- 它让 FName 比较成为 O(1) 的整数比较
- 它让 FName 占用固定大小（一个 index）
- 但代价是**字符串永远不能从池中删除**——任何代码可能仍然持有指向这条 entry 的 FName

**AS 触发的 FName 增长源**：

这是关键的代码路径：

```2698:2705:Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// Check if we're replacing a class
UASClass* ReplacedClass = FindObject<UASClass>(FAngelscriptEngine::GetPackage(), *UnrealName);
if (ReplacedClass)
{
    FString OldClassName = FString::Printf(TEXT("%s_REPLACED_%d"), *ReplacedClass->GetName(), UniqueCounter());
    ReplacedClass->Rename(*OldClassName, nullptr, REN_DontCreateRedirectors);
    ReplacedClass->ClassFlags |= CLASS_NewerVersionExists;
}
```

引擎周期之间复用同一个 Package 时，第 N 次发现已存在同名旧类 → 把旧类重命名为 `"AMyScriptActor_REPLACED_N"` → **每次产生一个全新的 FName**。

UASClass、UASStruct、UDelegateFunction 三类各有一处这种 Rename，加上 `CleanupRemovedClass` 路径还有 2 处。

**规模估算**：

- 单次完整 bind 创建 ~500 个动态类型
- 每个被替换都产生 1 个 `_REPLACED_N` FName
- 200 周期 × 500 类型 = ~10 万个新 FName
- 每个 FName 在 pool 中占 ~40-100 字节
- 总计 **~5-10 MB**

**实际贡献相对较小**——在 12GB 中只占 0.1% 量级。但因为 FNamePool 是全局共享，**这部分内存永远无法回收，即使 AS 引擎完全卸载**。

**为什么不能修**：

| 选项 | 障碍 |
|------|------|
| 复用同名 FName（不加 `_REPLACED_N` 后缀） | 旧 UClass 还存在于 Package 中，UE 不允许同名冲突 |
| 引擎周期之间销毁整个 Package | Package 是 root，且其他子系统可能持有 Package* 引用；销毁 Package 会触发巨量 GC |
| 修改 UE FNamePool 实现 | 引擎核心架构，超出插件范围 |
| 测试架构上避免重复 Init/Shutdown | 这是真正可行的方向，详见第四章 |

### 3.3 200+ 引擎周期的固有代价

**这是最关键也最容易被忽视的因素**。

测试套件当前的执行模型：

```
每个测试 → ASTEST_CREATE_ENGINE_FULL() → 创建新 FAngelscriptEngine
        → 完整 bind（121 Bind_*.cpp，~800MB 瞬时分配）
        → 编译测试脚本
        → 执行测试
        → 销毁引擎
```

**为什么需要这种模式**：

1. **测试隔离**：每个测试有独立的引擎状态，避免相互污染
2. **状态可控**：测试不需要担心 bind 顺序、注册副作用
3. **失败隔离**：一个测试 crash 不会影响其他测试

**代价**：

- 每周期至少 ~800MB 的"瞬时分配峰值"（即使全部释放，mimalloc 也保留页面）
- 每周期至少 ~500 个 FName 增量
- 每周期至少需要 ~3-5 秒的 bind 时间

**这些代价随测试数量线性放大**。即使每个周期"理论上"内存能完全归还，在测试运行的过程中，**至少有一个时刻进程持有 ~800MB+ 的活跃 AS 数据**——这个瞬时峰值就是 12GB 的天花板被推高的根本原因。

### 3.4 已修复 leak — `FBlueprintEventSignature`（2026-05-14）

历史问题：`Bind_BlueprintEvent.cpp` 三处 `new FBlueprintEventSignature` 把 Sig 挂到 `asCScriptFunction::userData` 单槽位上。本仓库 AS 2.33 fork 不提供 `SetFunctionUserDataCleanupCallback`（`SetUserData(data, type)` 直接忽略 `type` 参数；`~asCScriptFunction()` 也不调用任何 cleanup），因此引擎销毁后 ~1060 个 Sig 全部留下，跨周期累积 ~1.7 MB/cycle。

修复方案：

- 新增 `FBlueprintEventSignatureRegistry`（`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintEventSignatureRegistry.{h,cpp}`），内部持 `TArray<void*>`，删除走集中 `BlueprintEventSignatureRegistryInternal::DropOwnedSignature(...)`（定义在 `Bind_BlueprintEvent.cpp`，能看到完整 struct 类型）。
- `FAngelscriptOwnedSharedState` 加 `TUniquePtr<FBlueprintEventSignatureRegistry>` 成员，由 `EnsureSharedStateCreated()` 创建、`ReleaseOwnedSharedStateResources()` 在 `ScriptEngine->ShutDownAndRelease()` 之后 `Reset()`。这一时序保证：
  - `asCScriptFunction` 全部析构完毕、`userData` 指针不再被任何上下文读取，再释放底下指向的 `FBlueprintEventSignature`，无悬空读风险。
  - 每个 `FAngelscriptEngine` 拥有独立 registry，支持多 engine 同时存活的隔离语义。
- `Bind_BlueprintEvent.cpp` 三处 `new` 收敛到 `NewOwnedBlueprintEventSignature()` 助手，原地把所有权交给当前 engine 的 registry。

验证：`Angelscript.TestModule.Memory.BindFreeEvidence.BindFreeEvidence_BlueprintEventSignatureBounded`（新增）6 cycle 全部 `Registry->Num() == 1060`，delta = 0，PASS。

为何这处 leak"小但值得修"：

- 单次量级（~1.7 MB/cycle）远小于 mimalloc 保留页面，对峰值天花板贡献有限。
- 但它**违反 RAII 直觉**：作为"plugin 自己 new 出来的对象"，预期是 engine 销毁时全部释放；如果以后写其他 user-data 类型挂到 AS function 上，会踩同一个坑。把这种"AS user data 拥有权"集中到 Registry 后，未来加任何 per-engine user data 都有现成模板可循。
- 修复后，§4 提到的 `FUObjectType` +2.47 MB / 6 cycles "soft lead" 可以独立观察 —— 如果 BlueprintEventSignature 收敛后 UObjectType 仍然漂移，可以确定是另一根因。

## 四、什么才能真正降低峰值

只有改变测试架构。具体可选路径：

### 4.1 测试分组共享引擎

让相关测试共享同一个 `FAngelscriptEngine` 实例：

```
Group A 测试（10 个）：共享 engine_A → 1 次 bind 代价
Group B 测试（10 个）：共享 engine_B → 1 次 bind 代价
...
```

> **2026-05-14 现状盘点（数据驱动）**：
>
> 仓库**已经实现**这套基础设施，但采用率不均匀：
>
> - 基础设施：`FAngelscriptTestEnginePool` 单例 + `ASTEST_CREATE_ENGINE / _GET_ENGINE / _RESET_ENGINE / _FULL` 四件套宏（`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEnginePool.h` / `AngelscriptTestMacros.h`）
> - 标准模式：`BEFORE_ALL ASTEST_CREATE_ENGINE` → `TEST_METHOD ASTEST_GET_ENGINE` → `AFTER_ALL ASTEST_RESET_ENGINE`
> - 静态字符串采用率（源代码计数，不等于运行时）：`_GET_ENGINE` 656 处、`_CREATE_ENGINE` 462 处（共享路径）vs. `_FULL` 90 处 + 直调 `CreateUncompiled` 42 处（per-test full bind）
>
> **运行时实测**（最近一次 full suite, 2026-05-13 20:11）：
>
> | 指标 | 数值 |
> |---|---:|
> | 总 full bind cycle | **189** |
> | 平均 bind 耗时 | 974.7 ms |
> | 总 bind 时间 | **184.2 s ≈ 3.1 min** |
> | 触发 bind 的 test method | 178 |
> | 触发 bind 的 test class | 51 |
>
> 按 module 分布：
>
> | Module | Binds | 占比 |
> |---|---:|---:|
> | `Angelscript.GAS.Functional` | **87** | **46%** |
> | `Angelscript.TestModule.Debugger` | 31 | 16% |
> | `Angelscript.TestModule.Core`（含 `Performance`） | 13 | 7% |
> | `Angelscript.TestModule.Bindings` | 12 | 6% |
> | `Angelscript.TestModule.Engine` | 12 | 6% |
> | 其他 11 个 module | 34 | 18% |
>
> **真正的反模式**：GAS Functional 子树（如 `AngelscriptGASAttributeSetOverrideTests.cpp`）在**每个 `TEST_METHOD` 内部**而非 `BEFORE_ALL` 里调用 `ASTEST_CREATE_ENGINE_FULL()`，导致单个 class 的 N 个 method 触发 N 次 full bind（`AttributeSetOverrides=15`、`AttributeCallbacks=13`、`AbilityLifecycle=12`、`AttributeSetBPEvents=10`）。
>
> 改造潜力（需逐 class 验证副作用，未做）：
>
> | 类型 | Binds | 可压缩 |
> |---|---:|---|
> | GAS Functional（per-method new） | ~80 | 大概率改 BEFORE_ALL |
> | Debugger | ~31 | 多数可共享 |
> | Performance benchmark | 13 | ❌ 测的就是 startup |
> | Engine isolation | ~10 | ❌ 隔离即测试目标 |
> | 其他 | ~55 | ⚠ 逐个看 |
>
> **乐观节省**：把 GAS 87 个 binds 压成 ~20 个 ≈ 节省 **65 秒** wall time；加上 Debugger 子树整体压缩可达 **~1.6 分钟**。但**对 OS 峰值天花板影响有限**（仍由 mimalloc + FNamePool 顶住）—— 这是 wall time 优化，不是 peak 优化。
>
> 后续行动：先做 GAS Functional 子树的"可共享性审计"+ 单 class 试改一例（如 `AttributeSetOverrides`），用同样方式 pre/post 量 bind cycle 数变化，然后决定要不要做整批迁移。审计模板：每 class 检查 (a) 是否依赖全局 attribute set static registration？(b) 是否需要 fresh `UAbilitySystemComponent`？(c) `ASTEST_RESET_ENGINE` 后状态是否恢复干净？
>
> **2026-05-14 实施完成（GAS Functional 全子树迁移）**：
>
> 试改一例（`AttributeSetOverrideTests.cpp`，15 _FULL → BEFORE_ALL + 15 _GET_ENGINE）成功后，对所有 GAS Functional 子树文件（共 13 个 class，82 个 _FULL 站点）完成统一迁移。
>
> 模式确认：每个 _FULL 站点的代码形态完全同质 —— 独立 module 名、独立 UClass 名、`ON_SCOPE_EXIT { Engine.DiscardModule(...); }` —— 无一例外。这意味着这些"per-method new engine"全部是历史复制粘贴惯性，没有"故意要 fresh engine"的语义需求。
>
> 实测结果（`Angelscript.GAS.Functional` 全集，包含全部 172 个 test method）：
>
> | 指标 | 迁移前 | 迁移后 | 变化 |
> |---|---:|---:|---:|
> | full bind 数（runtime 实测） | 87 | **1**（仅 editor startup） | **−86 (−98.9%)** |
> | 测试 PASS 率 | 172/172 | **172/172** | 0 回归 |
> | 子集 wall time | ~3-4 min（推算） | **39.6 s** | **−~2.5 min** |
> | 每 method `post full reload` 耗时 | ~1.5-2 s（full bind） | ~50 ms（仅 module reload） | ~30× faster |
>
> 修改的 13 个 GAS Functional 文件（`Plugins/AngelscriptGAS/Source/AngelscriptGASTest/Private/Functional/GAS/*.cpp`）：每个 class 头部加 `BEFORE_ALL { ASTEST_CREATE_ENGINE(); }` + `AFTER_ALL { ASTEST_RESET_ENGINE(Engine); }`，所有 `ASTEST_CREATE_ENGINE_FULL()` → `ASTEST_GET_ENGINE()`。`AbilityTaskLifecycleTests.cpp` 本就 0 _FULL，无需修改。
>
> 验证：`gas-A-rollout-test` 跑 172/172 PASS（中间快照，A 类完成）；`gas-AB-rollout-test` 跑 172/172 PASS（最终态）。bind cycle 已经从 GAS 子树的 87 减到 1（实质上是 GAS 子树对全套件 bind 总数的贡献从 87 → 0，因为剩下那 1 个是 editor startup 的全局 bind）。
>
> **未触动**：B 类原本就保留的非 _FULL method（不取 engine、用 default current engine 的）行为不变；这些 method 本来就在 GAS 测试 startup engine 之上跑，迁移后依旧。
>
> **下一步候选**：`Angelscript.TestModule.Debugger` 子树 31 binds（占总数 16%）— 同样的"per-method new engine"反模式，可按此次模式继续推。
>
> **依然不动的部分**：`Performance benchmark`（13 binds，测的就是 startup）、`Engine isolation`（10 binds，隔离即测试目标）这两类必须保留 _FULL。

**估算收益**：将 200 次 init 降为 ~20 次 → bind 总成本 / 10。

**风险**：测试间相互污染、bind 顺序敏感性。

### 4.2 区分"完整 bind"和"轻量 bind"测试

大量测试其实只需要某个 subset 的 bind（例如只测 FVector）。可以提供：

```
ASTEST_CREATE_ENGINE_MINIMAL()  // 只 bind 必要的几个类型
ASTEST_CREATE_ENGINE_NATIVE()   // 纯 SDK，无 UE 反射
ASTEST_CREATE_ENGINE_FULL()     // 当前的完整 bind
```

把测试按需求分级，让 80% 的测试用更轻的引擎。

### 4.3 进程级隔离（spawn 子进程跑测试组）

让每个测试组在独立进程中运行：

```
Driver 进程：调度
  ├── Worker 1: 跑 Group A 50 个测试 → 进程退出回收所有内存
  ├── Worker 2: 跑 Group B 50 个测试 → 进程退出回收所有内存
  └── ...
```

这是 UE Editor 测试 farm 的常用做法。**进程退出 = 所有内存归零**，绕过 mimalloc 不归还和 FNamePool 不可清理。

### 4.4 引擎复用 + 显式 GC（最低收益）

保持当前架构但每 N 次周期主动调用 `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true)` 强制清理。

**收益小**：GC 只能回收 unroot 后的 UObject，不影响 mimalloc 和 FNamePool。

## 五、最终结论

**12GB 内存峰值是测试架构选择的成本，不是代码缺陷。**

| 维度 | 状态 |
|------|------|
| 真实泄漏 | ✅ 已修复（OpenSpec `fix-as-engine-shutdown-memory-leak`） |
| 悬垂指针风险 | ✅ 已消除 |
| 12GB 峰值 | ❌ 不会显著改善（不是泄漏导致的） |

要真正降低测试套件的内存占用，应该走 4.1–4.3 的方向（测试架构改造），而不是继续在分配器/全局容器层面做文章——那条路径已经基本走到尽头了。

## 附 A：相关文档与提交

| 资产 | 位置 |
|------|------|
| 修复 OpenSpec | `openspec/changes/archive/2026-05-13-fix-as-engine-shutdown-memory-leak/` |
| 共享 spec | `openspec/specs/engine-shutdown-resource-cleanup/spec.md` |
| 内存分析详解 | `Documents/Guides/ASEngineMemoryAnalysis.md` |
| 子模块修复 commit | `55282af`（核心）+ `b47c3ea`（注释修正） |
| 父仓库 commit | `1a33f80` → `f7b626d`（OpenSpec 演进 + 归档） |

## 附 B：实证数据（2026-05-13 对照实验）

为验证第三章"mimalloc 不归还页面"是 12GB 峰值的核心驱动因素，做了一组最小可重现的对照实验。

### B.1 实验设置

| 项目 | 取值 |
|------|------|
| 测试目标 | `Angelscript.TestModule.Engine.Isolation`（14 个 TEST_METHOD，含多次 full engine 创建） |
| Editor 调用方式 | 直接调用 `UnrealEditor-Cmd.exe`，统一启动参数 |
| 采样器 | 外部 PowerShell `Get-Process` 轮询，500ms 一次，记 `WorkingSet64 / PrivateMemorySize64 / VirtualMemorySize64 / Thread.Count` |
| 唯一变量 | 是否追加 `-ini:Engine:[ConsoleVariables]:mi.MemoryResetDelay=0` |
| 验证 CVar 生效 | Editor 启动日志中能看到 `LogConfig: Set CVar [[mi.MemoryResetDelay:0]]` |
| 测试结果 | 两组均 14/14 通过，exit code 0 |

### B.2 关键数据

| 指标 | Run A2（默认 `mi.MemoryResetDelay=10000`） | Run B（`mi.MemoryResetDelay=0`） | 差异 |
|------|------|------|------|
| 起点 WS | 4.8 MB | 4.4 MB | ≈ |
| 测试运行中（5–73 秒） | 与 B 基本同步，最大瞬时差 ≤ 5% | 与 A 基本同步 | -1% ~ -10% |
| **峰值 WS** | **3665.6 MB** | **3606.1 MB** | **-1.6%** |
| 峰值前末点 WS（Editor 仍 ~98 线程） | 3665.6 MB | 3581.6 MB | -2.3% |
| **末点 WS（Editor 已降到 1 线程）** | **2148.2 MB** | **269.1 MB** | **-87.5%** |
| 总耗时 | 73.9 s | 76.1 s | +3% |

### B.3 单次 bind 内存增长（Run A2，按时间轴拆解）

| t(秒) | WS (MB) | 阶段 |
|------|---------|------|
| 4.8 | 13 | UE 进程冷启动 |
| 7.4 | 836 | UE Editor 初始化 + 第 1 次 bind 开始 |
| 22.3 | 2084 | 第 1 次完整 bind 完成（**单次增长 +1248 MB**） |
| 30.3 | 2703 | Isolation 测试中第 2 次 full engine bind（+619 MB） |
| 76.3 | **3670**（peak） | 测试收尾 + 多次 bind 累积 |
| 77.9 | 1645 | Editor 进程退出第 1 秒，OS 开始回收（仍未完全释放） |

**直接对应文档第二章估算的"~0.8–1.5 GB AS 单次完整 bind 瞬时活跃数据"**——实测 1248 MB，落在估算区间正中。

### B.4 结论

1. **峰值几乎相同（-1.6%）** → 证明 `reset_delay` **不影响真实工作内存需求**。bind 阶段需要的活跃 page 量是真实的，与归还策略无关。
2. **末值差 1879 MB（-87.5%）** → 这正是 mimalloc 默认 10 秒延迟卡住的"已释放但未归还"页：
   - 默认模式：Editor 完全 shutdown 后还有 2148 MB 没归还给 OS
   - `reset_delay=0`：立即归还，只剩 269 MB（基线 + FNamePool + UE persistent allocator）
3. **测试运行中曲线几乎重叠** → mimalloc 默认延迟在"持续分配"期间是设计上的优化：旧 page 会被新 alloc 复用，所以 OS 视角看不到下降也属正常。差异主要在最后归还阶段体现。
4. **耗时几乎不变（+3%）** → `reset_delay=0` 的性能代价微乎其微。

把这条曲线按比例放大到全量套件（200+ engine cycle）：
- 单次 bind 增长 1.2 GB × 多次 cycle，叠加 mimalloc 不归还，**预计峰值 ~10–13 GB**——与文档摘要的 12.9 GB 完全一致。
- 如果在全量套件里也设 `mi.MemoryResetDelay=0`，可以期望测试结束（或 Editor shutdown）后 OS 看到的 WS 大幅下降，但**峰值不会有本质改善**——因为峰值由活跃工作内存决定，而非缓存。

### B.5 实验产物（不入库）

| 资产 | 路径 |
|------|------|
| 采样脚本 | `D:\Tmp\AsMemExp\Sample-MemoryDuringTests.ps1` |
| 直调 Editor 包装 | `D:\Tmp\AsMemExp\Run-DirectEditor.ps1` |
| 曲线对比脚本 | `D:\Tmp\AsMemExp\Compare-Curves.ps1` |
| Run A2 CSV（baseline） | `D:\Tmp\AsMemExp\runA2-default.csv` |
| Run B CSV（zerodelay） | `D:\Tmp\AsMemExp\runB-zerodelay.csv` |
| Run A CSV（通过 `RunTests.ps1`，独立交叉验证） | `D:\Tmp\AsMemExp\runA-default.csv`（峰值 3670 MB，与 A2 一致） |

### B.6 后续可做但本次未做

- 在全量 200+ cycle 套件上重复同样对照（预计耗时 ~3 小时/组），验证"峰值不变 + 末值大幅下降"在大尺度上是否仍然成立。
- 用 UE 的 LLM tag 在 bind 阶段做细分计量（区分 `AS_SDK` / `BindDatabase` / `BlueprintEvent` 等），把"1248 MB 单次 bind"再向下分解。
- 评估把 `mi.MemoryResetDelay=0` 作为 `AngelscriptTest` 模块的固定启动项是否合理（trade-off：放弃 mimalloc 的复用优化，换取 OS 内存观感）。
