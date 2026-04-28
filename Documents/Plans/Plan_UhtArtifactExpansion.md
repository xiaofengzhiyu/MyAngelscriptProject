# UHT 产物扩展计划：加速 AS 构建与 Bind

## 背景与目标

### 立项动机

`Plan_UhtPlugin.md` 已落地一期目标：让 `AngelscriptUHTTool` 自动为 BlueprintCallable / BlueprintPure 函数生成 `AS_FunctionTable_<Module>_<###>.cpp` 分片，配合 `Late+50` 的 `FBind` 在静态构造期把 native 指针塞进 `ClassFuncMaps`，使 `Bind_BlueprintCallable` 走直绑路径。一期阶段验收记录显示：13469 个候选函数中重构成功 9017、生成 14 个模块文件、覆盖代表类与端到端调用，其余 4452 条因签名重构失败退化为 `ERASE_NO_FUNCTION()` Stub。

但 UHT 阶段当前**只产出"函数指针 + 三份诊断 CSV/JSON"**。它在内存里能看到的元数据远多于此（`MetaData`、`PropertyFlags`、所有 UProperty / UEnum / UInterface / UDelegate），且当前运行时启动期 bind 仍存在多处可前移的工作：

- `BlueprintCallableReflectiveFallback.cpp::IsScriptDeclarationAlreadyBound` 在无 TLS 缓存时是 O(全局函数数) 扫描。
- Cook 路径下 `Bind_Defaults` 仍要对每条 `FAngelscriptMethodBind` 调 `Class->FindFunctionByName`，属性侧大量走 `FindPropertyByName`。
- `GetSortedBindArray()` 每次取用都重排，`TMap` 启动期没有任何 reserve 提示。
- `Plan_BindParallelization.md` 已证明并行执行 `CallBinds` 不可行（AS `Register*` API 非线程安全、`defaultNamespace` 单例、`PreviouslyBoundFunction` 静态串行），因此**减少 CallBinds 工作量比并行执行更有价值**。

### 目标

通过扩展 UHT 阶段的产物，把"运行时反射查找/全局扫描/重复字符串解析"这类启动期工作前移到编译期，并为 `Plan_StaticJITOfflineGeneration.md`、`Plan_ScriptAPIDocGeneration.md` 等下游 Plan 提供它们需要的输入：

1. **不动引擎、不破坏 ABI**：保持 Hazelight 那种"改 UHT + 改 FProperty"的方案在范围外，所有扩展都走 `UhtExporter` + 插件侧入口。
2. **零运行时风险路径优先**：先扩"只产出不消费"的产物（Phase 1），再决定哪些接入运行时（Phase 2/3）。
3. **与现有产物互补、不重复**：与 `FAngelscriptBindDatabase`（cook 期 method/property 描述）、`AS_JITAbiSignature_*`（StaticJIT ABI 校验）字段层互补，不重新做一份 source-of-truth。

## 范围与边界

### 在范围内

- 在 `AngelscriptUHTTool` 中扩展 Exporter，遍历 `UhtFunction` / `UhtProperty` / `UhtEnum` / `UhtClass` / `UhtScriptStruct` 收集已有但未导出的元数据。
- 新增产物文件（JSON / 二进制清单 / `.cpp` 表驱动注册），编译/构建路径接 `factory.CommitOutput()` 与现有 stale 清理逻辑。
- 在 `AngelscriptRuntime` 中按需添加只读消费入口（如 `FAngelscriptBinds::AddPropertyEntry` / `AddEnumEntry` / `LoadDeclarationManifest`），所有入口默认关闭、灰度开启。
- 与 `Plan_StaticJITOfflineGeneration.md`、`Plan_ScriptAPIDocGeneration.md`、`Plan_BindRegistrationProfiler.md` 共享部分计算结果（共享指纹、共享元数据 JSON）。

### 不在范围内

- 引擎级修改（`FProperty::AngelscriptPropertyFlags`、`FUNC_RuntimeGenerated`、`UClass::ASReflectedFunctionPointers`），由 `Plan_HazelightCapabilityGap.md` 承接。
- 并行化 `CallBinds`（已由 `Plan_BindParallelization.md` 论证不可行，本计划不重启该方向）。
- AS 脚本侧的 import / preprocessor / 模块依赖图导出（属于 `Plan_ModuleDependencyVisualization` 范围）。
- 函数 trampoline / 调用 wrapper 的预生成（需引擎修改才能落地，已由 `Plan_UhtPlugin.md` 风险章节明确归类为引擎侧差距）。
- `Bind_*.cpp` 手写绑定的注册风格统一（属于 `Plan_BindRegistrationUnification` 范围）。

### 与已有 Plan 的关系

- **`Plan_UhtPlugin.md`**：本 Plan 是其"二期扩展"，前置依赖一期的 `AS_FunctionTable_*.cpp` 生成路径稳定。
- **`Plan_StaticJITOfflineGeneration.md`**：Phase 1 的 ABI 指纹与其 `AS_JITAbiSignature_*` 共享同一份计算输入；本 Plan 先落"只产出不消费"，由 StaticJIT Plan 决定何时纳入 manifest 校验。
- **`Plan_BindParallelization.md`**：本 Plan 是其结论"无法并行 → 减少绑定工作量"的具体落地路径之一，重点解决方向 A（预计算与缓存）。
- **`Plan_ScriptAPIDocGeneration.md`**：Phase 1 的元数据 JSON 透传是其上游输入，可避免脚本 API 文档生成器再做一次 UE 反射扫描。
- **`Plan_BindRegistrationProfiler.md`**：本 Plan 不替代它，反而提供"每个 bind 注册了多少条目、哪些走直绑、哪些走 reflective fallback" 的源头数据。

## 当前事实状态快照

1. **UHT 插件输出现状**（来自 `AngelscriptFunctionTableCodeGenerator.cs` 与 `AngelscriptFunctionTableExporter.cs`）：
   - 每模块按 `MaxEntriesPerShard = 256` 切片，文件名 `AS_FunctionTable_<Module>_<###>.cpp`，注册顺序 `Late+50`。
   - 汇总：`AS_FunctionTable_Summary.json` / `ModuleSummary.csv` / `Entries.csv` 含 `directBindRate` / `stubRate` / `ShardCount`。
   - 跳过：`SkippedEntries.csv` / `SkippedReasonSummary.csv`，仅有计数，无修复指引列。
   - 排序键：`(ClassName, FunctionName)` 字符串序，**不是** FName 哈希序。
   - 元数据消费仅限三键：`NotInAngelscript`、`BlueprintInternalUseOnly`、`UsableInAngelscript`。
   - 无指纹/哈希文件、无清单文件。
2. **运行时启动期 bind 链路**（来自 `AngelscriptEngine.cpp::Initialize_AnyThread` 与 `AngelscriptBinds.cpp::CallBinds`）：
   - 顺序：`(可选) Binds.Cache 加载 → LoadBindModules → BindScriptTypes → CallBinds (单线程) → InitialCompile`。
   - 性能可观测点：`AS_PERF_SCOPE_STARTUP_BIND_SCRIPT_TYPES` / `AS_PERF_SCOPE_BINDS_CALL_BINDS` / `RecordGeneratedFunctionTableShardTiming` / `LogGeneratedFunctionTableTimingSummary`。
   - cook 路径走 `AS_USE_BIND_DB`（`!WITH_EDITOR`），编辑器路径走实时反射。
3. **`FAngelscriptBindDatabase` 现有字段**（来自 `AngelscriptBindDatabase.h`）：
   - `FAngelscriptMethodBind`：`Declaration` / `UnrealPath` / `ClassName` / `ScriptName` / `WorldContextArgument` / `DeterminesOutputTypeArgument` / 4 个 bool 标记。
   - `FAngelscriptPropertyBind`：`Declaration` / `UnrealPath` / `GeneratedName` / 6 个 bool 标记。
   - **没有**：参数类型签名指纹、UFunction/UProperty 偏移、metadata 完整透传、UEnum 表、Interface / Delegate 索引。

## 分阶段执行计划

### Phase 1：编译期信息扩展（只产出不消费，零运行时风险）

> 目标：先把 UHT 在内存里能看到、但当前没写出来的元数据全部沉淀为构建产物，让下游 Plan（StaticJIT、API Doc、Profiler）有稳定输入；本阶段不动 `AngelscriptRuntime` 的任何运行时代码。

- [ ] **P1.1** 实现 ABI / 布局指纹产物 `AS_FunctionTable_AbiFingerprint.json`
  - `Plan_StaticJITOfflineGeneration.md` 已识别"C++ 迭代导致 JIT 静默崩溃"是当前 StaticJIT 主要风险源；ABI 指纹是该 Plan 的前置输入，也可独立用作启动期 reflective 数据校验。
  - 产物结构：每条 `(Module, Class, Function)` 写入 `(参数类型 token 序列, 返回类型 token, PropertyFlags 列表, ArrayDim 列表)` 的 SHA-1（或 FNV-1a 64bit）；模块级再聚合一次得到模块指纹；最终输出根级 `aggregateFingerprint`。
  - 实现要点：在 `AngelscriptFunctionSignatureBuilder.cs` 现有签名重构通路里插入指纹累加；与 `EraseMacro` 字段并列写入 `AS_FunctionTable_Entries.csv` 末尾新增列 `AbiFingerprint`，并在 `AS_FunctionTable_Summary.json` 增加 `abi` 子节点。
  - 不消费：本任务只生成文件；StaticJIT manifest 校验、运行时降级策略由 `Plan_StaticJITOfflineGeneration.md` 决定何时启用。
- [ ] **P1.1** 📦 Git 提交：`[Plugin/UHT] Feat: emit ABI fingerprint for generated function table`

- [ ] **P1.2** 实现元数据透传产物 `AS_BindingMetadata_<Module>.json`
  - 当前 UHT 只读 `NotInAngelscript` / `BlueprintInternalUseOnly` / `UsableInAngelscript`，其他元数据全部丢弃；下游 `Plan_ScriptAPIDocGeneration.md` 与 IntelliSense 导出又得重新做一次反射扫描，重复浪费。
  - 透传字段（来自 `UhtFunction.MetaData` / `UhtProperty.MetaData`）：`Category`、`DisplayName`、`ScriptName`、`ToolTip`、`AutoCreateRefTerm`、`HidePin`、`DefaultToSelf`、`EditCondition`、`ClampMin`、`ClampMax`、`AdvancedDisplay`、`DeprecationMessage`；以及函数的 `WorldContext`、`DeterminesOutputType`、`CallableWithoutWorldContext`。
  - 产物结构：每模块一份 JSON，按 `Class → { Functions: { Name → MetaMap }, Properties: { Name → MetaMap } }` 分层，路径与 `AS_FunctionTable_<Module>_<###>.cpp` 同目录便于一并打包。
  - 实现要点：在 `AngelscriptFunctionTableCodeGenerator.cs::CollectEntries` 同一遍遍历内顺手收集 Property 元数据；不向 `AngelscriptRuntime` 注入新字段，仅作为旁路文件。
- [ ] **P1.2** 📦 Git 提交：`[Plugin/UHT] Feat: emit binding metadata json per module`

- [ ] **P1.3** 强化诊断产物：清单 + SkippedReason 修复指引
  - 当前 `SkippedEntries.csv` 只有失败原因计数，无法快速找到"哪些是签名重构能力缺口、哪些是 UE API 限制、哪些是手写 wrapper 已经覆盖"；这直接制约 stub 比例下降速度。
  - 新增 `AS_FunctionTable_Manifest.json`：列出本次生成的所有分片文件名 + 内容哈希（不是 ABI 指纹，是"文件字节"哈希），用于 CI 校验"这次 UHT 跑完到底改了哪些"。
  - 在 `AS_FunctionTable_SkippedEntries.csv` 新增 `Recommendation` 列，按 `FailureReason` 映射到三类标签之一：`HandwrittenWrapperRequired`、`AwaitASTypeExtension`、`Unbindable`；映射规则在 `AngelscriptFunctionSignatureBuilder.cs` 中以查找表方式落地，可被 `BindGapAuditMatrix.md` 引用。
  - 不修改现有 CSV 列顺序，只在末尾追加新列，避免破坏现有消费者。
- [ ] **P1.3** 📦 Git 提交：`[Plugin/UHT] Feat: add manifest and skipped-reason recommendation`

- [ ] **P1.4** 在 `Documents/Guides/Test.md` 与 `Documents/Plans/Plan_OpportunityIndex.md` 同步新产物路径
  - `Test.md` 当前已列出 `AS_FunctionTable_Summary.json` 等三份汇总文件路径；新增的 AbiFingerprint / BindingMetadata / Manifest 必须同步登记，否则下游 Plan 拿不到稳定挂点。
  - `Plan_OpportunityIndex.md` 在"4.1 已有 Plan"中把本 Plan 列为 `Plan_UhtPlugin.md` 的伴生二期（不放进新建议列）。
  - 这一步是文档同步，不动产物本体。
- [ ] **P1.4** 📦 Git 提交：`[Docs] Docs: register UHT artifact expansion outputs`

### Phase 2：启动期消费——直接服务于 bind 提速

> 目标：在 Phase 1 产物稳定后，新增运行时入口消费它们，直接降低启动期 bind 的实际耗时。本阶段引入运行时改动，必须配合性能基准对比验证。

- [ ] **P2.1** 实现全局声明清单 `AS_DeclarationManifest.bin` 与运行时一次性查询
  - `BlueprintCallableReflectiveFallback.cpp::IsScriptDeclarationAlreadyBound` 在 `FScopedBindCaches` TLS 缓存未命中时退化为遍历 AS engine 的 `GlobalDecls` / `ClassFuncNames`；编辑器冷启动每个 reflective fallback 候选都触发一次。
  - UHT 侧：新增 `AS_DeclarationManifest.bin`，按 `(NamespaceHash, ClassNameHash, DeclarationHash)` 三元组排序写入；同步生成 `AS_DeclarationManifest.idx.json` 给调试与诊断使用。
  - 运行时侧：`FAngelscriptBinds` 增加 `LoadDeclarationManifest(const FString&)`，启动期由 `BindScriptTypes()` 入口调用一次；`IsScriptDeclarationAlreadyBound` 增加 manifest 路径，命中后直接返回，未命中再退回原逻辑。
  - 必须由 console var `as.UseDeclarationManifest`（默认 1）开关；提供启动期对比开关方便回归基准。
- [ ] **P2.1** 📦 Git 提交：`[Plugin/Runtime] Feat: declaration manifest fast-path for reflective fallback`

- [ ] **P2.2** 实现预排序 / 预分桶的 `FBind` 索引
  - `AngelscriptBinds.cpp::GetSortedBindArray()` 每次取用都做一次 `Sort`，且启动期 `ClassFuncMaps` 没有任何 reserve；当 UHT 生成的条目数稳定在数万级别时，TMap rehash 成本不可忽略。
  - UHT 侧：新增 `AS_FunctionTable_Reservation.json`，列出每模块、每 `BindOrder` 档位的条目数与每 `UClass` 的函数数估算。
  - 运行时侧：`FAngelscriptBinds::ResetGeneratedFunctionTableTiming()` 同位点新增 `ReserveFromUhtHints()`，按 reservation 文件预先 `ClassFuncMaps.Reserve()` 与按 BindOrder 分桶预排序；`GetSortedBindArray` 改为 lazy 缓存（首次排序后缓存，新增 bind 时失效）。
  - 与 P2.1 共用同一个 manifest 加载入口，避免启动期多次磁盘 IO。
- [ ] **P2.2** 📦 Git 提交：`[Plugin/Runtime] Perf: prereserve class func maps and cache sorted binds`

- [ ] **P2.3** 性能基准对比与验收证据
  - 不允许"凭感觉提速"。在 `Plan_PerformanceBenchmarkFramework.md` 提供的基线就绪前，本任务先用临时方案：以 `AS_PERF_SCOPE_STARTUP_BIND_SCRIPT_TYPES` 与 `AS_PERF_SCOPE_BINDS_CALL_BINDS` 在固定机器跑 5 次取中位数；同时记录 `RecordGeneratedFunctionTableShardTiming` 的总和与 reflective fallback 路径条目数。
  - 输出：`Saved/Tests/uht-artifact-bench/<RunId>/before.json` 与 `after.json`，并在本 Plan 末尾"阶段验收记录"小节写入对比百分比与日志路径。
  - 若 P2.1 / P2.2 单项收益不足以覆盖增加的运行时复杂度（例如启动期 bind 总耗时下降 < 5%），允许把对应任务标记为"已实现但默认关闭"，由后续基准框架决定是否启用，避免回滚成本。
- [ ] **P2.3** 📦 Git 提交：`[Plugin/Runtime] Test: benchmark startup bind before vs after manifest`

### Phase 3：扩展直绑覆盖面（中等侵入度，先解 stub 比例）

> 目标：当前 stub 比例约 33%（4452/13469），主要由签名重构失败、接口类、属性类元素未导出等组成；本阶段通过新增 `AddPropertyEntry` / `AddEnumEntry` / `AddInterfaceEntry` 入口，把更多元数据从反射回退路径转到直绑路径。每项任务必须可独立合并、可独立回滚。

- [ ] **P3.1** UProperty 直接绑定表 `AS_PropertyTable_<Module>_<###>.cpp`
  - 当前 cook 路径下 `Bind_Defaults` 对每条 property 都要 `Class->FindPropertyByName`，编辑器路径同样存在反射查找；`FAngelscriptPropertyBind` 已有 `Declaration` / `UnrealPath`，但运行时仍要再做一次反射解析 offset。
  - UHT 侧：对 `BlueprintReadOnly` / `BlueprintReadWrite` 字段生成 `FAngelscriptBinds::AddPropertyEntry(UClass::StaticClass(), "Name", { TypeToken, OffsetGetter, bWritable })`；offset 通过 `STRUCT_OFFSET` / `__builtin_offsetof` 在生成代码里解析（避免在 UHT C# 侧写死偏移，否则 ABI 一变就脏）。
  - 运行时侧：`FAngelscriptBinds` 增加 `AddPropertyEntry` 与对应 `ClassPropMaps`；`Bind_Defaults` 优先消费表；表无命中时退回原 `FindPropertyByName`。
  - 必须配合 P1.1 的 ABI 指纹做防御：启动期校验失败时整个属性表降级为反射路径并打 warning。
- [ ] **P3.1** 📦 Git 提交：`[Plugin/UHT+Runtime] Feat: property direct-bind table`

- [ ] **P3.2** UEnum 字符串↔数值表 `AS_EnumTable_<Module>.cpp`
  - 当前 `Bind_UEnum_*` 中每个 BlueprintType 枚举都要 AS `RegisterEnumValue` 解析字符串；手写绑定还需要逐个枚举写一份。
  - UHT 侧：对 `UENUM(BlueprintType)` 生成单一 `AS_EnumTable_<Module>.cpp`，调用新接口 `FAngelscriptBinds::AddEnumEntry("Namespace", "EnumName", {{"Name", Value}, ...})`，并附带 enum 元数据（`DisplayName`、`Hidden`）。
  - 运行时侧：`FAngelscriptBinds::AddEnumEntry` 在 `Late+45`（早于 `Late+50` 函数表）档位批量注册；与现有 `Bind_UEnum.cpp` 手写注册做去重（手写优先，类似现有 `AddFunctionEntry` 的 `Contains` 检查）。
  - 验收必须包含：枚举数 ≥ 100 的回归 + 至少 5 个手写 `Bind_UEnum_*` 的兼容性回归。
- [ ] **P3.2** 📦 Git 提交：`[Plugin/UHT+Runtime] Feat: enum direct-bind table`

- [ ] **P3.3** 接口与委托元数据透传到 `AS_InterfaceTable_<Module>.cpp` / `AS_DelegateTable_<Module>.cpp`
  - 当前 UInterface 上的 UFUNCTION 一律 Stub（见 `AngelscriptFunctionTableCodeGenerator.cs` 的 ClassType 判定）；UDelegate 完全不导出，`ClassGenerator` 在解析 `UPROPERTY` 时仍走反射查找委托签名。
  - UHT 侧：新增两类生成器，分别对 UINTERFACE 输出 `(InterfaceName, FunctionList, MetaData)`，对 UDelegate 输出 `(DelegateName, Signature, IsMulticast, BlueprintAssignable)`；不试图直接生成 native 调用指针（这部分仍需 `Plan_InterfaceBinding.md` 与 `Plan_CppInterfaceBinding.md` 决定运行时调度策略）。
  - 运行时侧：`FAngelscriptClassGenerator` / `Bind_Delegates` 在初始化阶段优先消费表；表无命中时退回原反射路径。
  - 本任务依赖 `Plan_InterfaceBinding.md` Phase 1 至少建立"接口在 AS 端的生成 wrapper" 方案，否则即便 UHT 把数据吐出来也没人消费；若该 Plan 仍未启动，本任务退化为只生成产物不接入运行时，等待对接。
- [ ] **P3.3** 📦 Git 提交：`[Plugin/UHT+Runtime] Feat: interface and delegate metadata tables`

### Phase 4：清理与长期维护

> 目标：把扩展产物纳入常规生命周期，避免"一次性输出后腐化"。

- [ ] **P4.1** stale 产物清理与 worktree 隔离
  - 当前 `AngelscriptFunctionTableCodeGenerator.cs::CleanStaleShards` 仅清理 `AS_FunctionTable_*.cpp`；新增的 JSON / 二进制 / `AS_PropertyTable_*` / `AS_EnumTable_*` / `AS_InterfaceTable_*` / `AS_DelegateTable_*` 必须同步纳入清理逻辑。
  - 多 worktree 并行时（`Tools/Bootstrap/powershell/BootstrapWorktree.ps1`），产物落点必须与 `Saved/Build/<Label>/<RunId>/` 隔离，不能踩当前共享路径。
- [ ] **P4.1** 📦 Git 提交：`[Plugin/UHT] Chore: extend stale-output cleanup for new artifacts`

- [ ] **P4.2** 在 `BindGapAuditMatrix.md` 与 `TechnicalDebtInventory.md` 中固化"stub 比例下降趋势"
  - 把 P1.3 新增的 `Recommendation` 列与 P3 阶段的实际覆盖率提升结果写入 `BindGapAuditMatrix.md`，作为"未来还能继续覆盖多少 stub"的滚动基线。
  - `TechnicalDebtInventory.md` 中如果出现 "BlueprintCallable reflective fallback 占比" 等条目，统一替换为 P1.1 指纹与 P1.3 Recommendation 提供的口径。
  - 不在本任务里启动新一轮 stub 减少行动，避免 scope 蔓延；下一轮覆盖率行动由后续具体 sibling plan 启动。
- [ ] **P4.2** 📦 Git 提交：`[Docs] Docs: update bind gap matrix with new uht artifact baselines`

## 验收标准

1. **构建通过**：完整构建 + 增量构建（仅头文件未变）下，UHT 处理时间增量不超过基线（一期 P2.3 记录的 `5.17s` 单次 / `120.57s` 完整构建）的 20%。
2. **产物完整性**：`AS_FunctionTable_AbiFingerprint.json`、`AS_BindingMetadata_<Module>.json`、`AS_FunctionTable_Manifest.json`、（如执行 P3）`AS_PropertyTable_*.cpp` / `AS_EnumTable_*.cpp` 全部存在，且能被 `Plan_StaticJITOfflineGeneration.md` 等下游 Plan 直接读取。
3. **运行时正确性**：所有现有 `Angelscript.TestModule.*` 自动化测试通过，新增 manifest fast-path 不引入回归；`Angelscript.TestModule.Engine.GeneratedFunctionTable.*` 系列继续通过。
4. **运行时性能**：在 P2.3 基准中，启动期 bind 总耗时（`STAT_AngelscriptStartupBindScriptTypes`）相对基线下降 ≥ 5%（采纳）或 ≥ 0% 但默认关闭（保留实现）。
5. **诊断可读性**：`AS_FunctionTable_SkippedReasonSummary.csv` 中每个 `FailureReason` 都有 `Recommendation` 列；`BindGapAuditMatrix.md` 同步引用。
6. **Stale 安全**：手动删除某个分片或 metadata 文件后再次构建，UHT 能重新生成且不留旧产物。
7. **回滚可控**：所有 Phase 2 / Phase 3 入口都有 console var 或 build define 开关，关闭后行为退回当前主干表现。

## 风险与注意事项

### 风险

1. **ABI 指纹漂移导致 false positive**：UE 升级后 `PropertyFlags` 编码可能微调，若指纹算法过度敏感，会让所有模块在小版本升级后都失配并退化到反射路径。
   - **缓解**：指纹只覆盖 `(参数类型 token 序列, 返回类型 token, ArrayDim)`，不把 `PropertyFlags` 中可能频繁微调的位（如 `CPF_NativeAccessSpecifierPublic`）纳入；指纹失配时只打 warning 不报错，由 P3.1 的 fallback 兜底。

2. **Property 直接绑定表与多继承 / Sparse class data 冲突**：UE 的 `FProperty::GetOffset_ForUFunction` 在 sparse class data 路径下不等于编译期 `STRUCT_OFFSET`。
   - **缓解**：P3.1 的生成代码在编译期校验 `Class->IsChildOf(StaticClass)` 与 `!Class->HasAllClassFlags(CLASS_HasInstancedReference)` 等条件，命中风险路径时强制退化为 `FindPropertyByName`。

3. **多 worktree 并行写产物冲突**：`Saved/` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/` 是共享路径，并行 UHT 跑会互踩。
   - **缓解**：P4.1 强制把新增产物落入 `Plugins/Angelscript/Intermediate/Build/.../UHT/` 子目录，并在 `BootstrapWorktree.ps1` 的隔离矩阵中校验。

4. **Phase 2 / 3 的运行时入口绕过现有手写绑定**：手写 `AddFunctionEntry` 比 UHT 生成的 `Late+50` 早注册；新增 `AddPropertyEntry` / `AddEnumEntry` 必须保持同样的"手写优先"语义，否则 GAS 等手写 wrapper 会被覆盖。
   - **缓解**：所有新接口复用 `AddFunctionEntry` 的 `Contains` 检查模式；对 GAS 等关键路径加专项回归测试（参考一期 P3.2 的 `PreservesHandwrittenGASEntries`）。

5. **元数据透传文件体积膨胀**：Editor 模块的 metadata 在大型 UE 5.7 项目中可能达到几十 MB JSON。
   - **缓解**：P1.2 输出按模块分文件、UTF-8、无缩进；只透传白名单字段；提供 `--skip-metadata-export` 命令行开关供 CI 关闭。

### 已知行为变化

1. **`AS_FunctionTable_Entries.csv` 末尾新增 `AbiFingerprint` 列**（P1.1）
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、所有消费该 CSV 的 PowerShell 工具或 CI 脚本。

2. **`AS_FunctionTable_SkippedEntries.csv` 末尾新增 `Recommendation` 列**（P1.3）
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`BindGapAuditMatrix.md`。

3. **`FAngelscriptBinds` 公开新方法 `LoadDeclarationManifest` / `AddPropertyEntry` / `AddEnumEntry`**（P2.1 / P3.1 / P3.2）
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` / `.cpp`，对应单元测试在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增。

4. **新增 console var `as.UseDeclarationManifest` / `as.UsePropertyTable` / `as.UseEnumTable`**（Phase 2 / 3）
   - 默认 1（开启），但允许在 cook 配置中关闭以观察基线行为。
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 启动期入口。

5. **Stale 清理策略扩展到非 `.cpp` 产物**（P4.1）
   - 影响文件：`AngelscriptFunctionTableCodeGenerator.cs::CleanStaleShards`。

## 阶段验收记录

> 待执行后填写。每个 Phase 收尾时在此处补充：本次基线机器、构建/测试日志路径、性能对比百分比、stub 比例变化。
