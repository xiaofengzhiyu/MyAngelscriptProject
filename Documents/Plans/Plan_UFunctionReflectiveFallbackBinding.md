# UFunction 反射回退绑定计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 按任务执行本计划；所有步骤使用 checkbox 追踪。

**Goal:** 在保留现有 direct native bind 主路径的前提下，为当前 `ERASE_NO_FUNCTION()` 的一部分 `BlueprintCallable/BlueprintPure` UFunction 建立分层可调用模型，优先补齐调用能力，再按热度逐步逼近更轻的插件化桥接路径。

**Architecture:** 采用三层渐进模型：Tier 1 保留 `FGenericFuncPtr + ASAutoCaller::FunctionCaller` 的原生直绑热路径；Tier 2 为经过专项筛选的 unresolved 热点提供 plugin-owned generated thunk bridge；Tier 3 使用 `UFunction`/`ProcessEvent` 驱动的 reflective fallback 作为通用正确性后路。该模型明确把 `UFunction*` 视为调用描述符的一部分，而不是原始 C++ 成员函数指针替代品。

**Tech Stack:** `AngelscriptRuntime`（`AngelscriptBinds.h/.cpp`、`Bind_BlueprintCallable.cpp`、`Bind_BlueprintEvent.cpp`、`ClassGenerator/AngelscriptClassGenerator.cpp`）、UE 反射系统（`UFunction`、`FProperty`、`ProcessEvent`、`FFrame`）、`AngelscriptTest` 自动化测试、`Documents/Knowledges/` 架构文档。

---

## 快速设计摘要

```text
Call descriptor
├─ Tier 1: Direct native bind
│  ├─ FGenericFuncPtr
│  └─ ASAutoCaller::FunctionCaller
├─ Tier 2: Plugin-owned generated thunk bridge
│  ├─ UFunction*
│  ├─ BridgeThunkPointer / BridgeThunkId
│  └─ Cached layout metadata
└─ Tier 3: Reflective fallback
   ├─ UFunction*
   ├─ Param/return FProperty layout cache
   └─ ProcessEvent-based invocation helper
```

- **Tier 1**：面向已恢复原始 native ABI 的函数，性能最佳，不走 `ProcessEvent`。
- **Tier 2**：面向热点 unresolved 函数，通过插件自生成 thunk/wrapper 降低对完整反射路径的依赖。
- **Tier 3**：面向通用 correctness fallback，依赖 `UFunction` 元数据与 `ProcessEvent` 封送。
- **关键边界**：`UFunction*` 可以作为 Tier 2/3 的中心元数据键，但不能单独替代 Tier 1 所需的原始 native pointer + typed caller。

## 背景与目标

### 背景

当前 UHT 生成链已经能为大量 `BlueprintCallable/BlueprintPure` 函数生成 `AddFunctionEntry(...)` 产物，并通过 `ClassFuncMaps` 把它们交给 `Bind_BlueprintCallable.cpp` 自动发现。但这条路径的核心前提，是 `FFuncEntry` 中必须存在一组可直接用于 AngelScript 原生 ABI 的数据：

- `FGenericFuncPtr`：类型擦除后的原始 C++ 方法/函数指针
- `ASAutoCaller::FunctionCaller`：知道真实签名并按 `asCALL_THISCALL` / `asCALL_CDECL` 调用的 caller

这套模型对 `ERASE_METHOD_PTR` / `ERASE_AUTO_METHOD_PTR` 很高效，但它解决的是 **“如何直接调用原始 native ABI”**，不是 **“如何复用 UE 反射调用路径”**。因此当 UHT 无法恢复原始 C++ 方法指针时，当前系统只能生成 `ERASE_NO_FUNCTION()`，`Bind_BlueprintCallable.cpp` 也会在发现 `FuncPtr` 未绑定时直接跳过。

与此同时，UE 蓝图运行时并不是靠原始 C++ 成员函数指针调用 `BlueprintCallable`。根据当前 UE5 本地源码与 `knot` 检索交叉确认：

- `UFunction::Func` 存储的是 `FNativeFuncPtr`，定义于 `CoreNative.h`，签名为 `void (*)(UObject*, FFrame&, RESULT_DECL)`
- `UClass::AddNativeFunction()` / `NativeFunctionLookupTable` 注册的是 `execXxx` thunk，而不是原始 `&Class::Method`
- `UFunction::Bind()` 将函数名映射到 thunk 指针，再由 `UFunction::Invoke()` / `ProcessEvent()` 走 `FFrame + FProperty` 封送路径执行

这意味着“蓝图能调用”并不自动推出“当前 direct bind 体系就能拿到原始 native method pointer”；但它也说明：**对于当前 unresolved 的一部分 UFunction，仍然存在一条可靠的反射调用后路。**

另一个必须明确写清的现实约束是：**`ProcessEvent` 不是便宜的“近原生直调”替代品。** 从 UE5 当前 `ScriptCore.cpp` 调用链可以直接看到，反射路径至少会做以下工作：

- 为当前函数分配/复用执行帧内存，并初始化局部变量区
- 把 `Parms` 复制到 `FFrame` / 本地 buffer
- 为 `out` 参数建立 `FOutParmRec` 链
- 在调用后对局部变量做析构，并对非 `out` 参数执行必要的拷回

这些步骤使 `ProcessEvent` 更适合作为**覆盖率后路**，而不是 direct bind 的性能等价替代。与之相对，当前仓库自己的 `UASFunctionNativeThunk` 路径已经明确在追求“薄 thunk + 特化调用器 + JIT/非 JIT 分流”的热路径优化，因此本计划必须把 reflective fallback 定位成 **coverage fallback, not hot path replacement**。

公开资料与源码分析也一致把这条调用族群描述为“反射分发 + 参数/返回值封送”，而不是可与手写 native ABI 直调等价的路径；但在没有本仓专项 benchmark 之前，本计划不会写死某个固定 slowdown 倍数，只会把它表述为**按机制可预期地更重、且不应占据热路径**。

### 本计划目标

1. 在不破坏现有 direct bind 性能与语义的前提下，引入 **UFunction 反射回退后端**。
2. 将当前一部分 `ERASE_NO_FUNCTION()` 条目从“完全不可绑定”提升为“可通过反射调用绑定”。
3. 明确分层：
   - Tier 1：direct native bind（当前 `ERASE_*` 主路径）
   - Tier 2：reflective fallback bind（`UFunction` / `ProcessEvent`）
   - Tier 3：仍 unresolved（接口类、`CustomThunk`、参数形态超界等）
4. 为后续持续改进覆盖率建立统一的统计、测试和文档入口，而不是继续把“蓝图可调”与“当前 AS 可直绑”混成一个概念。

## 范围与边界

### 在范围内

- 为 `Bind_BlueprintCallable.cpp` 增加基于 `UFunction` 的 reflective fallback 绑定路径。
- 从现有 `ProcessEvent`/`FProperty` 参数封送逻辑中抽取共享 helper，避免重复实现一套新的反射 marshalling。
- 为 fallback 路径建立运行时资格判定：哪些 unresolved 条目可回退、哪些仍必须拒绝。
- 为 `GeneratedFunctionTable` / bindings 自动化测试增加直绑 / 回退 / unresolved 三类统计与代表性验证。
- 同步更新与绑定生成相关的知识文档，明确 direct bind 与 reflective fallback 的职责边界。

### 不在范围内

- 把 `UFunction::Func` 当成原始 C++ 成员函数指针直接塞进现有 `FGenericFuncPtr`/`ASAutoCaller` 体系。
- 第一阶段就尝试支持所有 unresolved 情况（例如 `UInterface`、`NativeInterface`、`CustomThunk`、latent/custom K2 thunk、复杂 `FFrame` 特殊语义）。
- 直接改写 UE 引擎反射主链（`ProcessEvent`、`UFunction::Invoke`、`FFrame` 内部行为）。
- 把现有 direct bind 路径替换成统一的反射调用路径。
- 把本计划扩展成另一轮大范围 UHT 生成器重构；UHT 侧显式元数据优化最多作为后续增强项。

## 当前事实状态快照

1. **当前 direct bind 存储模型已固定。**
   - `FunctionCallers.h` 中 `FFuncEntry` 只存 `FGenericFuncPtr + ASAutoCaller::FunctionCaller`。
   - `Bind_BlueprintCallable.cpp` 在 `FuncPtr` 未绑定时直接返回，不存在 fallback。

2. **UE 原生 `BlueprintCallable` 调用链已经证明 thunk 可执行，但 ABI 与当前 direct bind 不同。**
   - `UFunction::Func` 存的是 `FNativeFuncPtr` thunk。
   - `PlayerCameraManager.gen.cpp` 中 `SetGameCameraCutThisFrame` 实际注册的是 `&APlayerCameraManager::execSetGameCameraCutThisFrame`。
   - `execSetGameCameraCutThisFrame` 内部再调用 `P_THIS->SetGameCameraCutThisFrame()`。

3. **仓库内已有两条可复用的反射参数封送参考。**
   - `ClassGenerator/AngelscriptClassGenerator.cpp` 中 `CallInterfaceMethod()` 已展示：从 AS generic 调用取参 → 填充 `UFunction` 参数 buffer → `ProcessEvent()` → 拷回返回值。
   - `Bind_BlueprintEvent.cpp` 中 `FScriptCall` 已系统处理 `FProperty` 校验、buffer 构造、引用回写与 `ProcessEvent`/delegate 执行。

4. **当前 unresolved 并不全是“理论上可 fallback”。**
   - `AngelscriptFunctionTableCodeGenerator.cs` 直接把 `UhtClassType.Interface / NativeInterface` 生成成 `ERASE_NO_FUNCTION()`。
   - `CustomThunk` 在 `ShouldGenerate()` 阶段直接被排除，不会进入当前生成条目。
   - 仍有一批 unresolved 更接近“解析器/重载恢复仍不完整”，与“需要 fallback”不是同一个问题域。

5. **测试基线已经具备统计入口。**
   - `AngelscriptGeneratedFunctionTableTests.cpp` 已能输出 direct / stub 比例，适合扩展为 direct / reflective / unresolved 三分统计。

6. **仓库内已经有一条与 `ProcessEvent` 相对照的轻量调用基线。**
   - `UASFunctionNativeThunk` 只是极薄跳板：从 `Stack.Node` 取 `UASFunction` 后立即转交给 `RuntimeCallFunction()`。
   - `UASFunction` 体系再按参数/返回值形状与 JIT 可用性走特化分流，目标是压平热路径里的参数提取、返回值搬运和分支判断。
   - 这说明当前仓库不是“没有反射以外的调用策略”，而是已经有一个明确的性能导向基线，因此 fallback 必须围绕“尽量不侵蚀现有快路径”来设计。

## 原理与设计原则

### 原理 1：`UFunction::Func` 是 thunk 指针，不是原始成员函数地址

- 反射层存的是 `execXxx` 入口，负责 `FFrame`/`FProperty` 语义。
- direct bind 路径存的是原始 native ABI 指针，负责零反射开销调用。
- 两者都“能调用 native”，但不是同一类 callable，不应混装到同一个 ABI 假设里。

### 原理 2：reflective fallback 必须是显式第二后端，而不是 direct bind 的偷偷降级

- direct bind 的价值是性能与原生调用语义清晰。
- fallback 的价值是覆盖率，而不是伪装成“同样快”。
- 调试、统计、测试和文档都必须能看出函数最终走的是哪一层。

### 原理 3：优先复用现有 `ProcessEvent`/`FProperty` 封送逻辑，不重复发明一套 marshalling

- `CallInterfaceMethod()` 与 `Bind_BlueprintEvent.cpp` 已经覆盖了参数 buffer 分配、属性复制、返回值回写、销毁清理等关键细节。
- 第一版应先抽共享 helper，再在 `Bind_BlueprintCallable.cpp` 接入，减少行为漂移和隐藏 bug。

### 原理 4：第一版只覆盖“反射路径清晰且风险可控”的 unresolved 函数

- 优先支持：`FUNC_Native`、非接口类、非 `CustomThunk`、非 latent/custom K2 thunk、参数与返回值可由当前 `FAngelscriptType` / `FProperty` 明确封送的函数。
- 暂不承诺：`UInterface` / `NativeInterface`、复杂 wildcard/custom thunk、显著依赖 Blueprint VM 特殊流程的函数。

### 原理 5：统计口径必须升级为“三分类”

- 不能再把所有非 direct 都归为“stub”。
- 实施后至少需要能回答：
  - 当前有多少 direct bind
  - 有多少 reflective fallback
  - 还有多少 unresolved

### 原理 6：direct bind 是热路径，reflective fallback 是冷路径

- `ProcessEvent`/`UFunction::Invoke` 路径会显式构造 `FFrame`、复制 `Parms`、组织 `out` 参数链、执行 `FProperty` 初始化/析构与必要回写，这些都是 direct bind 不需要承担的成本。
- 当前 `ERASE_* + ASAutoCaller` 路径的价值，正是绕开这套反射封送，直接进入原始 native ABI；这也是它必须继续保留为第一优先级后端的根本原因。
- 因此本计划的目标不是“让所有 unresolved 都能用 `ProcessEvent` 模拟成一样快”，而是“在保持 direct bind 作为主快路径的前提下，用 reflective fallback 补功能覆盖”。
- 在缺少本仓专项基准前，可以明确的是“机制上更重”，但不应直接写成某个固定倍数，也不应把 Blueprint VM 的公开 benchmark 数字直接等价套用到本计划的 fallback 路径上。

### 原理 7：第一版 fallback 要优先减少固定开销，而不是追求机制完美

- 即使 fallback 本质上要走反射封送，也应尽量避免把重复工作放到每次调用都重新做一次。
- 第一版至少要评估并规划以下轻量缓存点：`UFunction*` 定位结果、参数/返回值 `FProperty` 布局摘要、是否存在 `out` 参数、是否可安全走当前 helper 的资格结论。
- 这类缓存不会把 reflective fallback 变成 direct bind，但可以避免它退化成“每次调用都重新做全量反射扫描”。

## 影响范围

本次计划涉及以下操作（按需组合）：

- **回退调用槽位扩展**：为当前仅支持 direct bind 的绑定态增加 reflective fallback 表达方式。
- **共享封送层抽取**：从现有 `ProcessEvent`/generic 调用逻辑中提炼共享 helper。
- **BlueprintCallable 路由改造**：在 direct bind 失败时尝试 reflective fallback，而不是直接跳过。
- **统计口径升级**：把现有 direct/stub 统计升级成 direct/reflective/unresolved。
- **热路径保护**：确保已有 direct bind 仍优先走原生 ABI，不因引入 fallback 而被反射路径抢占。
- **自动化测试补强**：补充 representative unresolved 函数的正/负例验证。
- **性能取舍说明**：把 `ProcessEvent` 的额外成本来源、fallback 的适用位置、以及 direct bind 的性能优势写入计划与知识文档。
- **知识文档同步**：把新的双后端边界和限制写入知识文档与计划索引。

### 按目录分组的文件清单

`Plugins/Angelscript/Source/AngelscriptRuntime/Core/`（2 个）
- `AngelscriptBinds.h` — 回退调用槽位扩展 + `FFuncEntry`/状态访问调整
- `AngelscriptBinds.cpp` — 绑定态辅助函数与统计/注册支持

`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`（4 个）
- `Bind_BlueprintCallable.cpp` — direct/fallback 双路绑定路由
- `Bind_BlueprintEvent.cpp` — 共享 `UFunction` 参数封送逻辑抽取 / 复用
- `BlueprintCallableReflectiveFallback.h`（新建） — 统一的 `UFunction` 资格检查、参数 buffer 构建、`ProcessEvent` 调用与返回值回写 helper 声明
- `BlueprintCallableReflectiveFallback.cpp`（新建） — reflective fallback helper 的具体实现与共享封送逻辑

`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/`（1 个）
- `AngelscriptClassGenerator.cpp` — `CallInterfaceMethod()` 迁移到共享 helper 或对齐同一套封送逻辑

`Plugins/Angelscript/Source/AngelscriptTest/`（2 个）
- `Core/AngelscriptGeneratedFunctionTableTests.cpp` — 统计口径升级 + reflective fallback 覆盖样本
- `Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp`（新建） — unresolved → reflective fallback 的正例/负例验证

`Documents/`（2 个）
- `Documents/Knowledges/02_04_Bind_System_And_Native_Binding_Generation.md` — 记录双后端模型与边界
- `Documents/Guides/Test.md`（如新增测试入口需要） — 记录 reflective fallback 相关测试前缀/运行方式

`Documents/Plans/`（2 个）
- `Plan_UFunctionReflectiveFallbackBinding.md` — 本计划
- `Plan_OpportunityIndex.md` — 新计划入口与优先级导航

## 分阶段执行计划

### Phase 0：定型后端边界与资格矩阵

> 目标：在动代码前先把“什么是 reflective fallback、它不是什么、第一版支持哪些 unresolved 条目”写成明确契约，避免实现期不断膨胀。

- [ ] **P0.1** 固化 direct bind 与 reflective fallback 的职责边界
  - 现状里 `FFuncEntry` 的 ABI 假设是“原始 native method/function pointer + ASAutoCaller caller”，这条路不能被 `UFunction::Func` 直接替换。
  - 在实现前先把两层模型写成显式约束：Tier 1 只服务原始 native ABI；Tier 2 明确是 `UFunction`/`ProcessEvent` 驱动的反射回退。
  - 同时决定第一版是否沿用现有 `ERASE_NO_FUNCTION` 作为 fallback candidate 信号，还是额外补 runtime-side eligibility 标记；默认优先不改 UHT 生成器，只在 runtime 按 `UFunction` 资格动态判定。
- [ ] **P0.1** 📦 Git 提交：`[Plugin/UHT] Docs: freeze direct-vs-reflective binding contract`

- [ ] **P0.2** 建立第一版资格矩阵与明确排除项
  - 为当前 unresolved 函数建立最小分类：`可直接 fallback`、`明确排除`、`后续再评估`。
  - 第一版默认排除 `UInterface` / `NativeInterface`、`CustomThunk`、latent/custom K2 thunk、以及当前封送层无法安全表达的参数/返回值形态。
  - 将代表性模块与样本函数写入计划附录或测试 TODO，避免后续实现只盯着单一模块。
- [ ] **P0.2** 📦 Git 提交：`[Plugin/UHT] Docs: record reflective fallback eligibility matrix`

- [ ] **P0.3** 固化性能定位与热路径保护原则
  - 在进入实现前先把本计划的性能口径写成显式约束：direct bind 是热路径，reflective fallback 是覆盖率后路；任何实现都不能把已有 direct bind 函数重新路由到 `ProcessEvent`。
  - 同时把 `ProcessEvent` 的主要固定成本来源写清楚：`FFrame` 构造、`Parms` 复制、`out` 参数链组织、`FProperty` 初始化/析构与回写。
  - 这一步不要求给出新的 benchmark 数字，但要求明确哪些性能结论是当前源码事实、哪些仍需要后续测量验证，避免计划里出现“fallback 也基本一样快”的误导表述。
- [ ] **P0.3** 📦 Git 提交：`[Plugin/UHT] Docs: freeze ProcessEvent performance positioning for fallback design`

### Phase 1：抽取共享 `UFunction` 调用封送层

> 目标：不要在 `Bind_BlueprintCallable.cpp` 里再写第三套 `ProcessEvent` 参数封送；先把现有事件/接口调用中可复用的逻辑收敛到共享 helper。

- [ ] **P1.1** 抽取共享的 `UFunction` 参数 buffer / 返回值回写 helper
  - 以 `AngelscriptClassGenerator.cpp` 的 `CallInterfaceMethod()` 和 `Bind_BlueprintEvent.cpp` 的 `FScriptCall` 为基础，抽出一套面向 `UFunction` 的共享 helper。
  - helper 至少负责：参数 buffer 分配、`FProperty` 逐项复制、`ProcessEvent` 执行、返回值拷回、引用参数回写与销毁清理。
  - 这一步不接 BlueprintCallable，只做逻辑抽取和现有调用点对齐，确保行为一致性优先于新功能推进。
- [ ] **P1.1** 📦 Git 提交：`[AngelscriptRuntime] Refactor: extract shared UFunction reflective invocation helper`

- [ ] **P1.2** 用共享 helper 回填现有反射调用点
  - 让 `CallInterfaceMethod()` 与现有 event/delegate 路径尽量复用同一套 helper，而不是保留两份长期分叉实现。
  - 对齐过程中补足错误信息、参数校验与引用回写的一致性，确保新 helper 不是“能跑”而是“行为等价”。
  - 这一步的价值是先把 reflective backend 的基础设施稳定下来，再让 `Bind_BlueprintCallable` 接入。
- [ ] **P1.2** 📦 Git 提交：`[AngelscriptRuntime] Refactor: align interface and event ProcessEvent marshaling on shared helper`

- [ ] **P1.3** 把可缓存的 reflective metadata 与不可缓存的执行态分离
  - 共享 helper 抽取完成后，补出最小缓存边界：哪些内容可以在绑定阶段或首次调用时缓存（例如 `UFunction*`、参数/返回值 `FProperty` 序列与资格结论），哪些内容仍必须每次调用按当前对象/参数构造。
  - 目标不是提前做复杂优化框架，而是先防止 fallback 的第一版在每次调用里重复做完全相同的反射扫描工作。
  - 这一步只建立轻量缓存策略和数据结构，不引入新的“全局性能系统”。
- [ ] **P1.3** 📦 Git 提交：`[AngelscriptRuntime] Refactor: separate reflective metadata caching from per-call marshaling state`

### Phase 2：在 `Bind_BlueprintCallable` 接入 reflective fallback

> 目标：让当前 direct bind 不再是唯一出路；当 `ClassFuncMaps` 中存在条目但 `FuncPtr` 未绑定时，尝试走 `UFunction` 反射后端。

- [ ] **P2.1** 为绑定态增加 reflective fallback 表达能力
  - 评估 `FFuncEntry` 是否需要扩展额外字段（例如 fallback kind / eligibility cache / 统计标签），或是否保持 `FFuncEntry` 不变、由 binder 当场基于 `UFunction` 判定。
  - 无论实现细节如何，都必须保证 direct bind 与 reflective fallback 在统计和测试上可区分，而不是都被归为“成功绑定”。
  - 保持与现有手写 bind 优先级兼容：已有 direct bind 的函数不得因 fallback 引入行为倒退。
- [ ] **P2.1** 📦 Git 提交：`[AngelscriptRuntime] Feat: add reflective fallback state to BlueprintCallable binding flow`

- [ ] **P2.2** 在 `Bind_BlueprintCallable.cpp` 接入回退路由
  - 当前 `Entry == nullptr` 或 `FuncPtr` 未绑定就直接返回；本阶段要把“有 UHT 条目但无 direct pointer”的分支升级成 reflective fallback 尝试。
  - 新路径必须显式区分：direct bind 仍走 `BindMethodDirect`/`BindGlobalFunction`；fallback 则走新的 generic/reflective binder，不伪装成同一套 callconv。
  - 对第一版不支持的函数给出可诊断的拒绝理由，而不是继续默默跳过。
- [ ] **P2.2** 📦 Git 提交：`[AngelscriptRuntime] Feat: bind unresolved BlueprintCallable functions via UFunction reflective fallback`

- [ ] **P2.3** 建立 runtime 统计与可观测性
  - 每轮绑定后至少能统计 direct / reflective / unresolved 三类数量，并支持按模块输出。
  - 与现有 `GeneratedFunctionTable` 导出率统计口径对齐，避免后续报告里继续把 reflective fallback 误算成 direct bind。
  - 若某些模块（如 `GameplayTags`、`AIModule`）收益明显，记录为优先验证对象。
- [ ] **P2.3** 📦 Git 提交：`[AngelscriptRuntime] Feat: record direct-reflective-unresolved binding statistics`

- [ ] **P2.4** 建立 direct-path precedence 与 reflective cold-path 保护
  - 对 binder 路由加入显式护栏：只要已有可用 direct bind，就绝不允许 reflective fallback 介入。
  - 对 reflective fallback 的入口增加轻量保护，确保它只在确实缺少 native pointer 且资格校验通过时触发，而不是变成“更宽松但更慢”的默认路径。
  - 这一步的目标是把性能优先级写成代码路径约束，而不是只留在文档里。
- [ ] **P2.4** 📦 Git 提交：`[AngelscriptRuntime] Feat: enforce direct-bind precedence and reflective cold-path guards`

### Phase 3：测试驱动的 representative fallback 闭环

> 目标：不是只让统计数字变好，而是真正把一批此前 unresolved 的 `BlueprintCallable` 变成可从 AS 调用的能力闭环。

- [ ] **P3.1** 补 representative reflective fallback 正例测试
  - 在 `Bindings/` 主题下新增专项自动化测试文件，挑选 3 个不同模块的当前 unresolved 样本做最小正例，例如 `AIModule`、`GameplayTags`、`UMG` 中各 1 个。
  - 测试要验证的不只是“找到 `UFunction`”，而是“从 AS 发起调用 → 参数正确进入 `ProcessEvent` → 返回值/引用参数正确回写”。
  - 样本选择应优先避开接口类和 `CustomThunk`，确保第一版先打通反射后端的主闭环。
- [ ] **P3.1** 📦 Git 提交：`[Test/Bindings] Test: add representative reflective fallback coverage`

- [ ] **P3.2** 补 reflective fallback 负例与边界测试
  - 为当前明确排除的类别建立负例：至少覆盖 `UInterface`/`NativeInterface`、`CustomThunk` 或已知不支持参数形态中的 2 类。
  - 负例要验证系统给出清晰拒绝，而不是 silent skip 或崩溃。
  - 同时确保 direct bind 代表性用例仍走原路径，避免 fallback 把既有快路径吞掉。
- [ ] **P3.2** 📦 Git 提交：`[Test/Bindings] Test: lock reflective fallback exclusions and direct-path precedence`

- [ ] **P3.3** 升级 `GeneratedFunctionTable` 统计测试
  - 现有测试已输出 direct/stub 比例；本阶段把它升级为 direct/reflective/unresolved 三分类统计，并加入至少一个 reflective sample 的断言。
  - 该测试要继续保留手写 GAS 覆盖保护，确保“手写 direct bind 优先于 generated reflective fallback”的规则被锁住。
  - 这一步是后续持续追踪覆盖改进的统一入口，不再依赖临时脚本统计。
- [ ] **P3.3** 📦 Git 提交：`[Test/Core] Test: extend generated table stats with reflective fallback coverage`

- [ ] **P3.4** 建立最小性能验证与表述校准
  - 不把第一版计划扩大成完整 benchmark 框架，但至少补一组最小验证：同一主题下各选 1 个 direct bind 样本与 1 个 reflective fallback 样本，确认两者执行路径和统计分类符合预期。
  - 若当前自动化环境适合采集粗粒度时序，则记录为“定性验证 + 粗量级参考”；若噪声过大，则至少固定日志与验证口径，明确此阶段不做绝对性能承诺。
  - 这一步的重点是防止文档里把 reflective fallback 说成“接近原生”，而不是在没有稳定测量条件的前提下硬写脆弱性能门槛。
- [ ] **P3.4** 📦 Git 提交：`[Test/Bindings] Test: validate reflective fallback performance positioning and path classification`

### Phase 4：文档、知识沉淀与后续增强闸门

> 目标：把这次新增的第二后端明确写进知识库，并给未来“是否继续改 UHT 生成器 / 是否做 thunk 直调桥”留出清晰的后续决策点。

- [ ] **P4.1** 更新绑定系统知识文档
  - 在 `02_04_Bind_System_And_Native_Binding_Generation.md` 中补齐 direct bind 与 reflective fallback 的职责边界、调用链、优缺点与运行时统计口径。
  - 文档需要明确说明：`UFunction::Func` 存的是 thunk，不是原始成员函数指针；反射回退是 coverage 策略，不是性能等价替代。
  - 还要补一段单独的性能定位：`ProcessEvent`/`FFrame`/`FProperty` 路径为什么天然比 direct bind 更重，当前仓库已有的 `UASFunctionNativeThunk + 特化子类` 为什么能作为热路径对照基线。
  - 若测试/运行入口新增了固定前缀或常用命令，也同步到 `Documents/Guides/Test.md`。
- [ ] **P4.1** 📦 Git 提交：`[Docs/Binding] Docs: document reflective fallback backend and runtime boundaries`

- [ ] **P4.2** 记录第二阶段以后才值得讨论的增强项
  - 明确把下列方向留到本计划 closeout 之后再评估：
    - 是否为 unresolved 条目增加显式 UHT 元数据标记，而不是完全依赖 runtime eligibility
    - 是否对部分热点函数做 thunk-based trampoline/JIT bridge
    - 是否为 reflective fallback 增加更细粒度的缓存/性能观测
  - 这样可以防止实施过程中 scope 继续膨胀成“再做一轮 UHT 导出器重写”。
- [ ] **P4.2** 📦 Git 提交：`[Plugin/UHT] Docs: record post-fallback follow-up gates`

- [ ] **P4.3** 同步更新 `Plan_OpportunityIndex.md` 中的新计划入口与优先级摘要
  - 当前索引已记录本计划入口，但正式执行本计划时仍需要在 closeout 或阶段落地过程中同步更新状态、优先级说明和相关导航，避免 `Documents/Plans/` 根目录的路由再次失真。
  - 这一步只负责索引同步与摘要维护，不与其他实现任务混做，确保后续归档或状态切换时能直接定位到本计划的最新状态。
- [ ] **P4.3** 📦 Git 提交：`[Docs/Plans] Docs: sync opportunity index for UFunction reflective fallback plan`

## 阶段依赖关系

```text
Phase 0（边界与资格矩阵）
  -> Phase 1（共享 UFunction 封送层）
    -> Phase 2（BlueprintCallable fallback 接入）
      -> Phase 3（正负例与统计闭环）
        -> Phase 4（知识沉淀与后续闸门）
```

## 验收标准

1. **双后端边界明确**：direct bind 与 reflective fallback 在实现、统计和测试中均可区分。
2. **代表性 unresolved 函数可调用**：至少 3 个当前 unresolved 的真实 `BlueprintCallable` 样本经 AS 调用成功，并通过自动化测试保护。
3. **既有 direct bind 不回退**：现有 `GeneratedFunctionTable` / 手写 GAS 兼容测试继续通过，且 direct bind 代表性样本仍走原路径。
4. **排除项行为清晰**：接口类、`CustomThunk` 或当前未支持参数形态的函数不会 silent skip，而是有显式拒绝/统计。
5. **统计口径升级完成**：测试与日志能输出 direct / reflective / unresolved 三类数量与比例。
6. **性能定位明确**：计划、测试与知识文档都明确 reflective fallback 是 cold path/coverage fallback，direct bind 仍是性能优先主路径。
7. **文档同步**：绑定系统知识文档与测试入口文档完成更新，不再把 `UFunction::Func` 误写成“原始函数地址”，也不再把 `ProcessEvent` 路径误写成“接近原生直调”。

## 风险与注意事项

### 风险

1. **反射封送语义比 direct bind 复杂得多**
   - `FProperty` 构造、引用回写、返回值写回、buffer 生命周期一旦处理不完整，就会出现隐蔽的崩溃或数据损坏。
   - **缓解**：优先复用现有 `CallInterfaceMethod()` / `Bind_BlueprintEvent.cpp` 模式，不直接在 `Bind_BlueprintCallable.cpp` 手搓第三套封送逻辑。

2. **容易把“fallback 可用”误当成“性能已经等价”**
   - `ProcessEvent` / `UFunction::Invoke` / `FFrame` 路径与 direct bind 的成本模型不同。
   - **缓解**：文档、统计和测试中始终分开记 direct 与 reflective；不要让 reflective 覆盖提升被解释成 direct bind 覆盖提升。

3. **`ProcessEvent` 固定开销可能让热点调用成本明显高于 direct bind**
   - 当前 UE 反射调用链会创建 `FFrame`、复制 `Parms`、组织 `out` 参数记录，并在调用后执行局部变量析构与必要拷回；如果这条路径被误用于高频热路径，调用成本会明显高于 direct bind。
   - **缓解**：坚持 direct-path precedence，第一版只把 fallback 用在当前确实缺失 native pointer 的 coverage 场景；并在计划内加入最小性能定位验证与缓存边界设计。

4. **并非所有 unresolved 都适合第一版 fallback**
   - 接口类、`CustomThunk`、复杂 Blueprint VM 语义函数即使理论上能走某种桥接，也不适合作为第一版目标。
   - **缓解**：先建立资格矩阵和负例测试，避免 scope 蔓延。

5. **共享 helper 抽取可能牵动现有事件/接口调用行为**
   - 如果抽取方式不谨慎，可能把已有 `ProcessEvent` 路径的稳定行为带偏。
   - **缓解**：Phase 1 先做等价重构并验证现有调用点，Phase 2 再接新功能。

### 已知行为变化

1. **绑定率统计口径会变化**
   - 现有 `direct/stub` 二分类在本计划落地后需要升级为 `direct/reflective/unresolved` 三分类。
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`

2. **“可绑定”不再等于“原生直绑”**
   - 部分此前 unresolved 的函数会变成“可通过反射调用绑定”，但这不是 direct bind 覆盖提升。
   - 影响文档：`Documents/Knowledges/02_04_Bind_System_And_Native_Binding_Generation.md`

## 相关资料与 sibling plan

- `Documents/Plans/Plan_UhtPlugin.md`：现有 UHT 生成函数表计划，说明 direct bind 生成链的起点与当前边界。
- `Documents/Plans/Plan_InterfaceBinding.md`：已存在的 `ProcessEvent`/接口调用复用思路参考。
- `Documents/Plans/Plan_UEBindGapRoadmap.md`：Bind gap 的主题化路线与避免重复注册原则。
- `Documents/Knowledges/02_04_Bind_System_And_Native_Binding_Generation.md`：当前 Bind 系统、生成器与运行时注册态边界。
- `Documents/Plans/Plan_HazelightCapabilityGap.md`：Hazelight 引擎侧改动与当前插件侧边界说明。

## 附录：Hazelight 相关机制学习与对比记录

> 本附录是学习/对比材料，用来解释 Hazelight 如何处理“可调用但未必能恢复为原始 native method pointer”的函数调用问题。它用于帮助理解本计划的架构选择，不直接构成当前计划的额外实施范围。

### A. 已确认的 Hazelight 处理方式

1. **Hazelight 不是继续追“统一恢复原始 C++ 成员函数指针”，而是给运行时生成函数开独立执行通道。**
   - 本仓对比文档显示，Hazelight 在引擎 `UFunction` 基类上新增了 `RuntimeCallFunction`、`RuntimeCallEvent`、`GetRuntimeValidateFunction` 等虚方法，并在 `ScriptCore.cpp` 中增加了 `FUNC_RuntimeGenerated` 分流。
   - 对应说明见 `Documents/Hazelight/ScriptClassImplementation.md` 中的 `3.2 UFunction 虚方法注入` 与 `3.3 ScriptCore.cpp 执行路径分流`。

2. **Hazelight 的 engine ref 里同时保留了“类型擦除后的 native 指针表”这条能力面。**
   - 本地参考仓 `J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\CoreNative.h` 定义了 `ASAutoCaller::FReflectedFunctionPointers` 和 `ERASE_METHOD_PTR` 一类宏；
   - `J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\Class.h` 给 `UClass` 增加了 `ASReflectedFunctionPointers`；
   - `J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Private\UObject\Class.cpp` 修改了 `FNativeFunctionRegistrar::RegisterFunctions`，在注册 native 函数时一并填充这张 reflected pointer 表。
   - 这说明 Hazelight 的能力不是“只有 ProcessEvent”，而是“引擎级 reflected pointer + runtime-generated dispatch”双轨并存。

3. **当函数不能按 raw native ABI 直接拿到合适的原始 method/function pointer 时，Hazelight 也不是强行把所有调用压成 raw pointer。**
   - 本仓对比文档和参考实现都指向同一结论：Hazelight 允许一部分函数作为 `FUNC_RuntimeGenerated`/script-backed UFunction 走 `RuntimeCallFunction` 分支。
   - 这意味着 Hazelight 解决的是“函数调用能力”问题，而不是“把所有 callable surface 都统一变成原始 native pointer”问题。

### B. Hazelight 与当前插件的根本差异

1. **Hazelight 是 engine-embedded，当前仓库是 plugin-only。**
   - `Documents/Hazelight/ScriptClassImplementation.md` 已明确写出：Hazelight 修改了 `UClass`、`UFunction`、`ScriptCore.cpp`、`UObjectGlobals.cpp` 等引擎核心路径；
   - 当前仓库则保持 `UASClass` / `UASFunction` / `NativeThunk` 在插件内闭合。

2. **Hazelight 通过引擎分流扩展“可调用面”，当前插件通过 direct bind 扩展“原始 native ABI 可调用面”。**
   - 当前仓库的 direct bind 存储模型是 `FGenericFuncPtr + ASAutoCaller::FunctionCaller`，核心价值是绕过反射封送，直接进入原始 native ABI。
   - Hazelight 除了保留 reflected native pointer 能力，还允许 runtime-generated/script-backed 函数在引擎执行路径中被直接调度，因此“可调用面”天然更宽。

3. **两边都不该把“能 invoke”误写成“拿到了原始 method pointer”。**
   - 对当前计划而言，这一点尤其重要：Hazelight 的引擎改造证明“调用能力”可以通过第二条执行通道补齐，但这不意味着它把所有函数都变成了 `ERASE_AUTO_METHOD_PTR` 那种 raw native pointer。

### C. 为什么这对本计划重要

1. **Hazelight 的做法验证了问题拆分方式。**
   - 真正需要拆开的，是：
     - “这个函数能不能被脚本/蓝图体系调用？”
     - “这个函数能不能恢复成原始 native ABI 的直绑目标？”
   - 这两个问题相关，但不等价。

2. **Hazelight 的能力更接近“引擎级 reflective/runtime-generated fallback”，而不是“万能 direct bind 恢复器”。**
   - 对当前仓库来说，最现实的插件化近似方案不是复制 Hazelight 的引擎补丁，而是在保留 direct bind 热路径的同时，引入 `UFunction` reflective fallback 冷路径。

3. **本计划的方向因此是合理且必要的。**
   - direct bind 继续负责高性能主路径；
   - reflective fallback 负责补掉当前 `ERASE_NO_FUNCTION()` 里“理论上可通过 UE 反射调用”的那一层覆盖缺口；
   - 而不是试图在插件内复制 Hazelight 全套 `FUNC_RuntimeGenerated` 引擎分流。

### D. 对后续实施的具体启示

1. **不要把 `UFunction::Func` 或 `ProcessEvent` 路径伪装成 raw native pointer。**
   - 这会混淆 ABI 假设，也会混淆性能口径。

2. **应当明确保留双后端边界。**
   - Tier 1：direct native bind
   - Tier 2：reflective fallback
   - Tier 3：仍 unresolved

3. **如果后续真的要进一步靠近 Hazelight，必须承认那会越过插件边界。**
   - 比如显式 UHT 元数据、运行时 generated flag、引擎分流入口、甚至 thunk-based trampoline/JIT bridge，这些都不应在本计划第一阶段被偷偷带入。

4. **本计划与 Hazelight 的关系，应该表述为“插件化近似解”，而不是“等价移植”。**
   - Hazelight 解决的是引擎级脚本/运行时生成函数接入问题；
   - 本计划解决的是在不改引擎的前提下，为 unresolved BlueprintCallable 函数补出一条可维护、可统计、可测试的调用后路。

## 附录：不修改引擎前提下，是否能逼近 Hazelight 做法

> 本附录是在前文 Hazelight 对比的基础上，专门回答一个进一步的问题：既然当前仓库已经把 Hazelight 的若干引擎侧改造通过插件化方式近似实现，那么在“运行时生成函数/反射调用补面”这一块，是否也能继续沿着插件化方向逼近 Hazelight。

### A. 已经明确可以插件化逼近的部分

1. **运行时元数据与调用策略本身可以继续插件化。**
   - 当前仓库已经证明，并非所有 Hazelight 的引擎改动都必须原样照搬：`ScriptStructImplementation.md` 已记录我们通过 UE5 现有 FakeVTable 机制，零引擎修改地逼近了 Hazelight 在 `ICppStructOps` 上的能力面。
   - 这说明“Hazelight 通过改引擎拿到的能力”里，至少有一类其实是“引擎里已有足够可扩展接口，只是我们需要重新组织插件实现”的问题。
   - 同样地，当前仓库自己的 `UASFunction` / `UASFunctionNativeThunk` / `RuntimeCallFunction` 体系，本身就是一套已经插件化成功的运行时调用层，说明“第二执行后端”并不是全新方向，而是现有模式的延伸。

2. **对 unresolved BlueprintCallable 的“第二执行后端”完全可以插件化。**
   - 当前仓库已经有 `CallInterfaceMethod()`、`Bind_BlueprintEvent.cpp` 等 `ProcessEvent`/`FProperty` 封送参考，说明在不改引擎的前提下，我们已经具备构造参数 buffer、调用 `ProcessEvent`、拷回返回值与引用参数的基础能力。
   - 从这个角度说，Hazelight 用 `FUNC_RuntimeGenerated + RuntimeCallFunction` 打通的“调用能力”，我们可以在插件侧以 **direct bind + reflective fallback** 的双后端近似达成。
   - 结合 `ScriptPlugin` 的 `AddUniqueNativeFunction` 与 `Verse` 的 `UVerseFunction::Bind()` 旁证，可以进一步确认：UE 体系本身允许“自生成函数 + 自定义 thunk + lookup table”这类扩展模式，但它们通常建立在**扩展方自己拥有的函数生成/注册链**上，而不是事后恢复任意引擎 native 函数的原始实现指针。

3. **注册期或绑定期的轻量缓存，同样可以插件化。**
   - 不改引擎并不等于每次都只能全量反射扫描。`UFunction*` 定位结果、`FProperty` 布局摘要、资格结论、是否存在 `out` 参数等信息，都可以在插件层缓存。
   - 这意味着我们虽然拿不到 Hazelight 那种“引擎内嵌字段 + 全局执行分流”，但仍可以通过插件层 metadata cache，把 reflective fallback 的固定开销压到更可控的水平。

### B. 无法原样插件化复制的部分

1. **不能在不改引擎的前提下，把 `UFunction` 基类普遍扩展成 Hazelight 那样的运行时分流入口。**
   - Hazelight 的关键能力之一，是在 `UFunction` 基类里新增 `RuntimeCallFunction` 等虚方法，并在 `ScriptCore.cpp` 的原生执行路径上增加 `FUNC_RuntimeGenerated` 分流。
   - 这类能力点之所以强，是因为它们位于引擎主执行链内部。插件无法在不改 `UFunction` / `ScriptCore.cpp` 的前提下，把所有 `ProcessEvent` 调用自动改走自己的新分支。

2. **不能在不改引擎的前提下，获得 Hazelight 那种“全局级 reflected native pointer 表”同等级的注册控制力。**
   - Hazelight 在 engine ref 里通过 `ASReflectedFunctionPointers` 和对 `FNativeFunctionRegistrar::RegisterFunctions` 的修改，把 reflected pointer 收集直接嵌进 `UClass` 注册期。
   - 插件层当然可以维护自己的映射表，但没法无缝插进 UE 原生所有类/函数的统一注册点，也无法让每个 `UClass` 天生都带上一份插件私有字段。

3. **不能把“可 invoke”偷换成“拿到了原始 native ABI 直绑目标”。**
   - 不改引擎时，我们最现实的逼近方式是：对 unresolved 函数提供一条插件层 reflective fallback。
   - 这与 Hazelight 的 engine-assisted invocation family 在能力上接近，但仍然不是“恢复出原始 method pointer”。

### C. 最现实的插件化近似路线

1. **保留 direct bind 作为主热路径。**
   - 任何已经能由 `FGenericFuncPtr + ASAutoCaller::FunctionCaller` 解决的函数，都不应被新的 reflective 路线覆盖。

2. **把当前 unresolved 分成三类，而不是一刀切。**
   - `可 direct bind`：继续走原始 native ABI
   - `可 reflective fallback`：通过 `UFunction`/`ProcessEvent` 提供调用能力
   - `仍需引擎支持或显式排除`：如接口类、`CustomThunk`、某些 runtime-generated 语义复杂场景

3. **引入“插件私有的类/函数元数据缓存层”，逼近 Hazelight 的注册期优势。**
   - 它不能替代 Hazelight 直接改 `UClass` 的能力，但可以在插件层维持一份按 `UClass` / `UFunction` 索引的资格表、布局表和调用后端选择结果。
   - 这会让我们的 reflective fallback 更像“预计算后的第二后端”，而不是“每次调用临时拼装的慢路径”。

4. **把“是否进一步靠近 Hazelight”拆成两个层次。**
   - 第一层：完全插件化近似，目标是覆盖率和维护性
   - 第二层：如果未来确实证明某些 reflective 热点需要进一步降成本，再评估是否值得引入更强的生成期桥接、专门 thunk bridge，甚至重新审视引擎边界

### D. 当前判断

1. **可以逼近，但不是等价复制。**
   - 我们可以在不改引擎的前提下，逼近 Hazelight 的“更广可调用面”目标；
   - 但这种逼近更接近“插件层双后端 + metadata/cache + reflective fallback”，而不是“把 Hazelight 的 `FUNC_RuntimeGenerated` 分流整套搬进来”。

2. **这个方向与当前仓库过往经验是一致的。**
   - `ScriptStructImplementation.md` 已经证明，一些 Hazelight 的引擎补丁本质上只是“它走了引擎分支最短路径”，并不代表插件侧没有替代实现。
   - 因此，把 unresolved BlueprintCallable 这块继续朝“插件化近似解”推进，是有历史先例和工程风格一致性的。

3. **但必须保留一条清醒边界。**
   - 只要问题触及 `UFunction`/`ScriptCore` 主执行链、统一注册期字段、或真正的 engine-wide runtime-generated dispatch，插件化近似就不再是“等价实现”，而是“能力接近、语义与性能口径不同”的替代方案。

4. **这正是本计划应该采用的表述。**
   - 本计划不是在“复制 Hazelight 引擎方案”；
   - 本计划是在“持续识别哪些 Hazelight 能力可以继续被插件化吸收，哪些仍然是明确的引擎边界”，并把这一判断落实到 direct bind / reflective fallback / unresolved 三层模型中。
   - 目前最值得保留为“边界候选”的，不是整个调用模型本身，而是那些已经在本仓测试中暴露出脚本生成/生命周期闭环不足的场景，例如 `WorldSubsystem` / `GameInstanceSubsystem` 这类子系统脚本生成问题；它们更像真正需要单独论证“是否触及引擎边界”的能力点。

### E. 来自 UE 现有机制的旁证

1. **UE 生态里已经存在“非完全引擎改造、但依赖 `UFunction`/native thunk/lookup table 扩展调用面”的模式。**
   - `ScriptPlugin` 的 `UScriptBlueprintGeneratedClass::AddUniqueNativeFunction()` 证明，类级别的 native function lookup 扩展与生成类配合是可行的。
   - `Verse` 的 `UVerseFunction::Bind()` / `InvokeCalleeThunk` 说明，即使在 UE 主线里，运行时或生成期函数也可以通过 `SetNativeFunc(...)` + 自定义 thunk 接入，而不必都还原成原始 C++ 成员函数指针。
   - 这些案例说明：**插件/扩展系统可以逼近调用能力扩展，但通常依赖“插件自己拥有的函数生成与注册路径”，而不是事后恢复引擎所有 native 函数的原始 ABI 指针。**

2. **这与当前仓库的可行空间是匹配的。**
   - 对我们来说，最现实的是：
     - 对插件自生成/自持有的函数，继续优化 native-thunk / direct-bind 路径；
     - 对引擎现有 `BlueprintCallable` 但无 raw pointer 的函数，提供 reflective fallback；
     - 不把“能接入 `NativeFunctionLookupTable`”误解成“已经拿到了原始 method pointer”。

### F. 建议写入本计划的决策、非目标与升级闸门

1. **决策：本计划目标应正式重述为“两层可调用模型”，不是“完整 Hazelight 调用模型对齐”。**
   - 层 1：原始 native ABI 的 direct bind，面向高频/性能敏感路径。
   - 层 2：基于 `UFunction` 的 reflective fallback，面向覆盖率与功能补面。
   - 只要某类问题的答案不是“恢复 raw pointer”，就不应继续把它挂在 direct bind 缺口名下。
   - 同时保留一个**受控增强方向**：如果后续验证表明某些 unresolved 热点确实不适合直接落到 `ProcessEvent`，可以在两层模型之间插入“插件自生成 thunk bridge”这一中间层，但它必须建立在专项验证之后，而不是提前扩成第一阶段承诺。

2. **非目标：不把以下事项伪装成本计划第一阶段目标。**
   - 通用的原始 C++ 成员函数指针恢复器。
   - 对任意引擎 native 函数统一构建 Hazelight 等价的引擎主执行链分流。
   - 依赖 `UFunction` 基类扩展、`ScriptCore.cpp` 分支、统一注册期字段注入的能力。
   - 用未验证的 wrapper/thunk bridge 假设替代当前明确可行的 fallback 路径。

3. **升级闸门：什么时候才该重新讨论引擎边界。**
   - 若存在一类函数，既无法 direct bind，又无法以 reflective fallback 达到正确语义，则标记为“引擎边界候选”。
   - 若后续实际使用证明 reflective fallback 覆盖虽足够，但在热点路径上成本不可接受，则先尝试插件内 wrapper/thunk bridge 或更强缓存；只有当这些方案都不能满足需求时，才进入“是否需要引擎改动”的讨论。
   - 若某项能力依赖引擎在注册期保留当前并未暴露的数据，则不再把它当插件待办，而是升级为明确的引擎侧差距。

### G. 插件化近似 Hazelight 的可选实现方向（学习记录，不是当前阶段承诺）

1. **Metadata cache 强化版 reflective fallback**
   - 在当前计划已经覆盖的 `UFunction*` / `FProperty` 布局缓存基础上，进一步把资格判断、参数摘要和返回值回写路径预计算到类/函数级 side table。
   - 这是最稳妥、最贴近当前计划的增强方向。

2. **生成期 wrapper/thunk bridge**
   - 对一部分热点 unresolved 函数，在 UHT 或插件侧生成薄 wrapper，使 fallback 不必每次做完全相同的反射扫描。
   - 这能逼近 Hazelight 的“注册期就准备好调用入口”的优势，但仍不是通用 raw pointer 恢复。
   - 如果后续要进一步减少 `ProcessEvent` 参与度，这一层就是最像 `FUNC_RuntimeGenerated + RuntimeCallFunction` 的**插件化近似解**：不是改 `ScriptCore.cpp` 主执行链，而是在我们可控的 UHT/注册链上预先生成并登记一组 plugin-owned thunk/wrapper，让部分 unresolved 热点先命中这组桥接入口，再决定是否还要落到完整 reflective fallback。

3. **插件私有的运行时生成函数注册层**
   - 对插件自生成函数，尽量像 `Verse`/`ScriptPlugin` 那样在自己可控的生成/注册链路中提前埋入 lookup 表与 thunk。
   - 这更适合“插件自己拥有的函数面”，不应误解成“能覆盖任意现有引擎 native 函数”。

4. **需要显式降级的高风险方向**
   - 试图在插件内模拟 `FUNC_RuntimeGenerated` 的全局执行分流。
   - 通过脆弱的全局 hook/vtable 截持来复制 Hazelight 的引擎主链语义。
   - 把未经过专项验证的实验性近似方案提前写进 Phase 0-Phase 2 的承诺列表。

### H. 对“能否做到类似 `FUNC_RuntimeGenerated + RuntimeCallFunction`、同时尽量避免 `ProcessEvent`”的专门回答

1. **对插件自生成/自控制的函数，答案基本是“可以逼近”。**
   - 当前仓库已经拥有 `UASFunctionNativeThunk + RuntimeCallFunction` 这类 plugin-owned 调用层；若继续扩展 UHT 与注册链，我们可以为部分 unresolved 热点生成专门的 thunk/wrapper，并在插件自己的 metadata/lookup 表中提前登记。
   - 这种做法在能力上会更接近 Hazelight 的“运行时生成函数有专门执行入口”，同时比每次都走完整 `ProcessEvent` 更轻。

2. **对任意现有引擎 native UFUNCTION，答案只能是“部分逼近，不能等价复制”。**
   - 不改引擎时，我们无法把 `UFunction` 基类普遍改成带 `RuntimeCallFunction` 的新分流入口，也无法统一接管 `ScriptCore.cpp` 的主执行链。
   - 因此我们最多做到：对一部分函数通过插件侧生成桥接 thunk 避开完整 reflective fallback；而不是让所有 unresolved 引擎函数都自动拥有 Hazelight 那种 engine-level dispatch path。

3. **所以当前最合理的思路应被表述为“三层渐进模型”，而不是直接把 `ProcessEvent` 当成唯一 fallback，也不是误以为能直接复制 Hazelight。**
   - Tier 1：direct bind（原始 native ABI）
   - Tier 2：plugin-owned generated thunk bridge（只覆盖经过专项筛选的热点 unresolved）
   - Tier 3：reflective fallback（`UFunction`/`ProcessEvent`，作为通用正确性后路）
   - 其中 Tier 2 是可选增强层，是否立项取决于 Phase 0/Phase 3 的验证结果，而不是当前计划默认承诺。

### I. 存储-调用设计细化：为什么不能“只存 `UFunction*` 然后直接调”

1. **Hazelight 示例成立的前提，是它已经改了引擎。**
   - 类似 `Bind_TDEvent.cpp` 里这段逻辑之所以能写成：
     - `if (Function->FunctionFlags & FUNC_RuntimeGenerated)`
     - `Function->RuntimeCallEvent(...)`
     - `else Listener->ProcessEvent(...)`
   - 前提是引擎已经拥有：
     - `FUNC_RuntimeGenerated`
     - `UFunction::RuntimeCallEvent(...)`
     - `ScriptCore.cpp` 对 runtime-generated 函数的额外执行语义
   - 在 vanilla UE + 当前插件模型下，单独存一个 `UFunction*`，并不能神奇地让 `UFunction` 具备新的运行时分流入口。

2. **`UFunction*` 只足以支撑 reflective 路径，不足以支撑 direct bind 路径。**
   - `UFunction*` 能提供：函数标志、参数/返回值 `FProperty` 布局、`ParmsSize`、名称查找、`ProcessEvent`/`Invoke` 所需元数据。
   - `UFunction*` 不能直接提供：
     - 原始 C++ 成员函数指针
     - 当前 `FGenericFuncPtr + ASAutoCaller::FunctionCaller` 所要求的原始 native ABI 调用目标
     - vanilla UE 中类似 `RuntimeCallEvent()` 的额外 plugin-owned 虚入口
   - 这就是为什么“只存 `UFunction*` 然后直接通过 `UFunction` 调用”在当前仓库里，实质上只会落回 reflective fallback，而不会自动变成 direct bind。

3. **因此，正确的存储模型必须按调用层级拆开。**
   - Tier 1（direct bind）至少要存：
     - `FGenericFuncPtr`
     - `ASAutoCaller::FunctionCaller`
     - 可选的 native signature / bind metadata
   - Tier 2（plugin-owned generated thunk bridge）至少要存：
     - `UFunction*`
     - 预生成/预登记的 plugin-owned thunk 或 wrapper 标识
     - 参数/返回值布局摘要与资格缓存
   - Tier 3（reflective fallback）至少要存：
     - `UFunction*`
     - `FProperty` 布局缓存
     - 参数 buffer 构造 / 引用回写 / 返回值处理所需的辅助描述

4. **换句话说，`UFunction*` 是 Tier 2/Tier 3 的核心元数据键，不是 Tier 1 的替代品。**
   - 它适合作为“查参数布局、找调用后端、做缓存索引”的中心句柄；
   - 但不能被误解为“它本身就是原始 native method pointer”。
   - 从数据结构设计上说，真正需要被缓存/传递的应当是 **call descriptor**，其中 `UFunction*` 只是它的一部分，而不是完整调用能力本身。

### J. 计划中的推荐存储模型

```text
Per function entry
├─ Common metadata
│  ├─ UFunction* ReflectedFunction
│  ├─ EligibilityKind (Direct / Bridge / Reflective / Unsupported)
│  ├─ ParamLayoutSummary
│  └─ ReturnLayoutSummary
├─ Tier 1 DirectBindPayload
│  ├─ FGenericFuncPtr NativePointer
│  └─ ASAutoCaller::FunctionCaller NativeCaller
├─ Tier 2 BridgePayload
│  ├─ BridgeThunkId or BridgeThunkPointer
│  ├─ Optional prebuilt native wrapper metadata
│  └─ Cached marshalling plan
└─ Tier 3 ReflectivePayload
   ├─ ProcessEvent invocation policy
   ├─ OutParm / ref writeback policy
   └─ Buffer allocation / reuse policy
```

### K. 计划中的推荐调用链（ASCII 树）

#### K.1 Tier 1：direct bind（原始 native ABI）

```text
AS call site
└─ FFuncEntry
   ├─ FGenericFuncPtr
   └─ ASAutoCaller::FunctionCaller
      └─ typed native ABI call
         └─ original C++ method / function
```

**特点**
- 不依赖 `UFunction` 封送
- 不构造 `FFrame`
- 不走 `ProcessEvent`
- 这是当前最轻、最接近原生的路径

#### K.2 Tier 2：plugin-owned generated thunk bridge

```text
AS call site
└─ FFuncEntry
   ├─ UFunction* ReflectedFunction
   ├─ BridgeThunkPointer / BridgeThunkId
   └─ Cached layout metadata
      └─ plugin-owned bridge thunk
         ├─ minimal argument marshalling
         ├─ optional direct/native wrapper handoff
         └─ fallback to reflective helper only if needed
```

**特点**
- 仍以 `UFunction*` 为元数据中心
- 但不一定每次都走完整 `ProcessEvent`
- 目标是把一部分热点 unresolved 从“完整反射路径”提升到“更轻的插件桥接路径”
- 这是最像 Hazelight 思路、但仍保持插件边界的中间层

#### K.3 Tier 3：reflective fallback（通用正确性后路）

```text
AS call site
└─ FFuncEntry
   ├─ UFunction* ReflectedFunction
   └─ Reflective invocation helper
      ├─ allocate / reuse parms buffer
      ├─ marshal arguments via FProperty metadata
      ├─ UObject::ProcessEvent(UFunction*, Parms)
      ├─ copy back return value
      └─ copy back out/ref params + cleanup
```

**特点**
- 覆盖面最广
- 语义最稳妥
- 也是固定开销最大的一层
- 应明确作为 cold path/coverage fallback

### L. 三层路径的性能花费分析

1. **Tier 1（direct bind）**
   - 主要成本：
     - type-erased pointer 恢复
     - `ASAutoCaller` 的参数展开与调用
   - 不承担：
     - `FFrame` 构造
     - `FProperty` 逐项封送
     - `ProcessEvent`
   - 这是热路径基线。

2. **Tier 2（bridge thunk）**
   - 主要成本：
     - bridge 跳转
     - 轻量参数布局适配
     - 必要时的最小封送/包装
   - 目标是显著低于完整 reflective fallback，但仍高于 Tier 1。
   - 它的价值不在“做到和 Tier 1 一样快”，而在“避免所有 unresolved 都直接落到 Tier 3”。

3. **Tier 3（reflective fallback）**
   - 主要成本：
     - `UFunction*` 元数据驱动的参数/返回值布局处理
     - 参数 buffer 构造或复用
     - `FProperty` 复制、引用参数回写、析构清理
     - `ProcessEvent` / `Invoke` / `FFrame` 路径
   - 这是正确性后路，不应成为高频调用默认路径。

4. **性能判断的安全表述**
   - 可以明确说：Tier 3 按机制显著重于 Tier 1，Tier 2 设计目标是夹在两者之间。
   - 但在没有本仓专项 benchmark 之前，不应写死固定倍数。

### M. 对当前问题的直接回答

1. **“我们可以直接存 `UFunction*` 吗？”**
   - 可以，而且对于 Tier 2/Tier 3 这是必须存的核心元数据句柄。

2. **“然后调用时直接通过 `UFunction` 调呢？”**
   - 在 vanilla UE 里，若没有额外桥接层，这基本就意味着：
     - 你先拿 `UFunction*`
     - 再准备目标对象与参数 buffer
     - 最后走 `ProcessEvent` / `UFunction::Invoke` 语义
   - 也就是说，这天然落在 Tier 3，而不是 direct bind。

3. **“那是不是不行？”**
   - 不是不行，而是**不能把它误判成 direct bind**。
   - 如果目标是“比完整 `ProcessEvent` 更轻”，就需要在 `UFunction*` 之外，再存 bridge 所需的 thunk / wrapper / layout cache，这就是 Tier 2 的意义。

4. **“所以我们的真实思路是什么？”**
   - 不是“只存 `UFunction*`”；
   - 而是“以 `UFunction*` 作为 Tier 2/Tier 3 的中心元数据键，再按情况补上 direct payload 或 bridge payload，形成三层渐进调用模型”。
