# Angelscript 跨领域架构改进计划

## 背景与目标

### 背景

`Plugins/Angelscript` 已经具备稳定的 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`、`AngelscriptUHTTool` 基础结构，但五维分析在多个模块中反复指出同一类跨领域问题：runtime identity 仍依赖 ambient world / current engine 的隐式回退，binding pipeline 同时保留 legacy cache 与 UHT shard 两套事实源，hot reload 的 session / version chain / tombstone owner 分散在 class generator、`UASClass` 和 editor helper 中，type/symbol/source metadata 缺少统一 canonical key，editor 侧的 file watch、content browser、menu extension、source navigation、BlueprintImpact 也还没有共享 workspace intelligence。

这些问题并不都表现为单点 crash；更多时候它们表现为扩展点难以外放、测试构造成本高、跨模块能力只能靠全局状态粘合、参考插件里的成熟模式无法按增量方式吸收。结合当前项目约束，本 Plan 只覆盖“不改引擎、兼容 AngelScript 2.33.0 WIP、以插件内部增量演进为主”的跨领域架构改进，不重复已有活跃 Plan 中已经单独立项的发布工程化、debug adapter、全量去全局化或 capability gap inventory。

### 目标

- 建立显式 `runtime identity + activation plane`，把 runtime 所属关系、boot 契约、world context 决议从 ambient/global 回退中抽离出来。
- 建立统一 `binding provider manifest + extension protocol`，让 runtime、editor、UHT、后续外部 provider 共享一套 artifact authority。
- 把 hot reload 提升为 engine-owned session，收口 version chain、tombstone、construct context 的 owner 和约束边界。
- 建立统一 `type/symbol identity + source metadata`，为 runtime lookup、editor navigation、debug/toolchain metadata 提供同一 canonical key。
- 建立 `EditorAssetSyncService + SurfaceAdapterRegistry + WorkspaceIntelligence`，让 editor 数据面与 runtime metadata 解耦，并把当前“因架构而难测”的区域转化为可隔离的 automation test。

## 与现有活跃 Plan 的边界

- `Documents/Plans/Plan_FullDeGlobalization.md`
  - 本 Plan 只定义 runtime identity、activation 和 owner 收口所需的抽象边界，不展开全仓“所有全局入口”的逐项清单替换。
- `Documents/Plans/Plan_PluginEngineeringHardening.md`
  - 本 Plan 不覆盖 README、打包、CI、版本发布与外部交付脚手架，只处理插件内部架构 seams。
- `Documents/Plans/Plan_DebugAdapter.md`
  - 本 Plan 只提供 debug/toolchain 会消费的 symbol/source metadata 基础契约，不实现 DAP 协议、VS Code 扩展或客户端交互层。
- `Documents/Plans/Plan_HazelightCapabilityGap.md`
  - 本 Plan 不重写 capability gap inventory；只有当 gap 已在当前源码中形成真实架构痛点时，才吸收参考插件做法进入条目。
- `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md`
  - 本 Plan 不是对参考仓库的完整“学习笔记”，只选取已经被 A/B/C/D 维度交叉确认、且能在当前代码落地验证的架构模式。

## 分析来源

| 维度 | 文档 | 对本 Plan 的贡献 |
| --- | --- | --- |
| A | `Documents/AutoPlans/RuntimeCore_Analysis.md`、`Documents/AutoPlans/ClassGenerator_Analysis.md`、`Documents/AutoPlans/BindSystem_Analysis.md` | 识别 runtime activation 的隐式回退、hot reload owner 分散、binding/type lookup 重复实现、editor/runtime 耦合面过宽等反复出现的设计气味。 |
| B | `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md`、`Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md`、`Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` | 给出 Architecture 类 issue 的可执行修正方向，帮助判断哪些问题已经从“设计味道”升级成结构性缺陷。 |
| C | `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` | 证明 runtime boot、fallback tick、bind artifact、symbol/source metadata 等区域因为 owner 与边界不清而难以稳定构造隔离测试。 |
| D | `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md`、`Documents/AutoPlans/ArchitectureReview/ExtensionPoints_ArchReview.md`、`Documents/AutoPlans/ArchitectureReview/EditorArch_ArchReview.md`、`Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md`、`Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md`、`Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md`、`Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md`、`Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` | 提供跨模块根因分析与分层改进方向，是本 Plan 的主输入。 |
| E | `Documents/AutoPlans/ReferenceComparison/CrossComparison.md`、`Documents/AutoPlans/ReferenceComparison/GapAnalysis.md`、`Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/sluaunreal_Analysis.md` | 用参考插件的 activation gate、manifest/catalog、workspace intelligence、reload impact 事实源等模式，帮助确定可吸收且适合增量迁移的方案。 |

## 影响范围

本次迁移涉及以下操作（按需组合）：

- **Runtime identity 收口**：引入 `FAngelscriptRuntimeIdentity`、`FAngelscriptExecutionContextResolver`、`FAngelscriptBootRequest` / `FAngelscriptBootResult`，替代 ambient world + push/pop side effect 驱动的 activation。
- **Binding artifact 统一**：引入 `BindingManifest`、`ProviderId`、`ShardKind`、`SchemaVersion`、`SymbolKey`，让 runtime、editor、UHT 和未来 provider 共用同一 artifact authority。
- **Hot reload session 收口**：引入 `FAngelscriptHotReloadSession`、`ASVersionChain`、tombstone owner，把 reload 过程中散落的全局状态变成 request-scoped / engine-owned 数据。
- **Symbol/source metadata canonicalization**：引入 `FAngelscriptSymbolKey`、`FAngelscriptNominalClassRef`、`FAngelscriptSourceMetadata`，替换 name-only lookup 和 `Module->Code[0]` 假设。
- **Editor workspace intelligence 抽离**：引入 `FAngelscriptEditorAssetSyncService`、`IAngelscriptEditorSurfaceAdapter`、`FAngelscriptWorkspaceIntelligence`，让 data source、menu、source navigation、impact scanner 共享同一 workspace 视图。
- **自动化验证补齐**：新增按主题组织的 `AngelscriptTest` automation tests，统一使用 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + `FAngelscriptEngineScope`。

按目录分组的文件清单：

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/`（7 个）
  - `AngelscriptEngine.cpp` — Runtime identity 收口 + Hot reload session 收口 + Symbol/source metadata canonicalization
  - `AngelscriptEngine.h` — Runtime identity 收口 + Hot reload session 收口
  - `AngelscriptGameInstanceSubsystem.cpp` — Runtime identity 收口
  - `AngelscriptRuntimeModule.cpp` — Runtime identity 收口
  - `AngelscriptBinds.cpp` — Binding artifact 统一
  - `AngelscriptBindDatabase.cpp` — Binding artifact 统一
  - `AngelscriptType.cpp` / `AngelscriptType.h` — Symbol/source metadata canonicalization
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/`（3 个）
  - `AngelscriptClassGenerator.cpp` — Hot reload session 收口 + Symbol/source metadata canonicalization
  - `ASClass.cpp` — Hot reload session 收口 + Symbol/source metadata canonicalization
  - `ASClass.h` — Hot reload session 收口
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`（2 个）
  - `Bind_UObject.cpp` — Symbol/source metadata canonicalization
  - `Bind_BlueprintType.cpp` — Symbol/source metadata canonicalization
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/`（6 个）
  - `AngelscriptEditorModule.cpp` — Binding artifact 统一 + Editor workspace intelligence 抽离
  - `ClassReloadHelper.cpp` — Hot reload session 收口
  - `AngelscriptContentBrowserDataSource.cpp` — Editor workspace intelligence 抽离
  - `AngelscriptContentBrowserDataSource.h` — Editor workspace intelligence 抽离
  - `ScriptEditorMenuExtension.cpp` — Editor workspace intelligence 抽离
  - `AngelscriptSourceCodeNavigation.cpp` — Symbol/source metadata canonicalization + Editor workspace intelligence 抽离
- `Plugins/Angelscript/Source/AngelscriptEditor/Public/`（2 个）
  - `BlueprintImpact/AngelscriptBlueprintImpactScanner.h` — Editor workspace intelligence 抽离
  - `BaseClasses/ScriptEditorSubsystem.h` — Editor workspace intelligence 抽离
- `Plugins/Angelscript/Source/AngelscriptUHTTool/`（1 个）
  - `AngelscriptFunctionTableCodeGenerator.cs` — Binding artifact 统一
- `Plugins/Angelscript/Source/AngelscriptTest/`（5 个新增测试文件）
  - `Core/AngelscriptRuntimeIdentityTests.cpp` — Runtime identity 收口验证
  - `Bindings/AngelscriptBindingManifestTests.cpp` — Binding artifact 统一验证
  - `HotReload/AngelscriptHotReloadCoordinatorTests.cpp` — Hot reload session 收口验证
  - `Bindings/AngelscriptSymbolIdentityTests.cpp` — Symbol/source metadata canonicalization 验证
  - `Editor/AngelscriptEditorWorkspaceTests.cpp` — Editor workspace intelligence 验证

## 分阶段执行计划

### Phase 1: Runtime activation 与 binding authority

- [ ] **P1.1** 建立显式 `runtime identity + activation plane`
  - 当前 runtime 生命周期同时依赖 `FAngelscriptEngineContextStack`、`UAngelscriptGameInstanceSubsystem`、`GAmbientWorldContext` 与 module startup 的 push/pop side effect；同一套“当前 engine”语义分别散落在 runtime module、subsystem、ambient world 和 boot 后置 hook 中，导致 editor、commandlet、game instance、automation test 很难以统一方式声明 owner、推导 boot 行为或验证 fallback tick。
  - 本条目只做边界重构，不追求一轮消灭所有 legacy global。先引入 `FAngelscriptRuntimeIdentity`、`FAngelscriptExecutionContextResolver`、`FAngelscriptBootRequest` / `FAngelscriptBootResult`，让 module/subsystem/automation test 明确传入 owner、purpose、world source、tick policy；再把 `PostInitFunctions`、`InitializeAngelscript()`、`AssignWorldContext()` 等入口改成消费 boot contract 的 shim，保留必要兼容层但不再让 ambient world 承担事实源。
  - 目标状态是：runtime activation 由显式 owner 与 boot request 决定，`TryGetCurrentEngine()` 只作为兼容入口转发到 resolver；subsystem 负责声明“谁拥有 runtime”，而不是通过 push/pop 顺带制造全局激活；test 可以不依赖真实 `GameInstance` 与全局 ambient world 就验证 boot、tick、shutdown 和 fallback 行为。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — 多处指出 current engine / ambient world / subsystem fallback 在同一条查询链上叠加，runtime identity 不显式。
    - [B] `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md` — Architecture 类发现要求把 runtime 启动、world context 绑定和 owner 生命周期从隐式 side effect 中拆开。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — 现有 runtime 初始化、fallback tick、world context 相关路径难以用隔离测试稳定复现。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-05`、`Arch-SL-12`、`Arch-SL-20` — 指出 activation、boot hook 与 context resolution 缺少统一契约。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — 参考实现以显式 activation gate 与 owner 生命周期驱动 runtime，而不是从 ambient world 反推。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L287 — `SyncAmbientWorldContextFromCurrentEngine()` 继续把 `current engine` 反写到全局 `GAmbientWorldContext`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L718 — `TryGetCurrentEngine()` 先查 stack，再回退 `UAngelscriptGameInstanceSubsystem::GetCurrent()`，说明 runtime identity 仍通过隐式查询拼接。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L746 — `AssignWorldContext()` 同时写 `WorldContextObject` 与 ambient global，world source 没有单一 owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L17 — `Initialize()` 通过 `Push(PrimaryEngine)` / `OwnedEngine.Initialize()` 隐式接管 primary runtime。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L138 — `InitializeAngelscript()` 通过 `OwnedPrimaryEngine` + `Push` 建立 fallback primary engine，boot 语义混在 module startup 中。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeIdentityTests.cpp`
- [ ] **P1.1** 📦 Git 提交：`[RuntimeIdentity] Refactor: introduce explicit engine resolver and boot plane`
- [ ] **P1.1-T** 单元测试：为显式 resolver 与 boot contract 建立隔离验证
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeIdentityTests.cpp`
  - 测试场景：
    - 正常路径：显式传入 owner / world source 时，resolver 选择对应 engine，并把 boot request 转换成稳定 `BootResult`。
    - 边界条件：没有 `GameInstance`、只有 module-owned fallback engine、或测试覆盖 `InitializeOverrideForTesting` 时，resolver 仍返回预期 runtime identity。
    - 错误路径：同时提供冲突 owner、失效 `WorldContextObject` 或重复 shutdown 时，boot contract 拒绝进入 active 状态并给出可断言诊断。
  - 测试命名：`Angelscript.TestModule.RuntimeIdentity.ResolverAndBootRequestPreferExplicitOwner`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.1-T** 📦 Git 提交：`[RuntimeIdentity] Test: cover resolver and boot contract`

- [ ] **P1.2** 建立 `binding provider manifest + extension protocol`
  - 当前 binding pipeline 同时存在 `FAngelscriptBinds` 的进程内注册数组、`FAngelscriptBindDatabase` 的 `Binds.Cache` / `.Headers` 二进制缓存、editor 侧 `BindModules.Cache`、以及 UHT 侧 `AS_FunctionTable_*` shard/json/csv 输出；这些 artifact 彼此没有统一 `ProviderId`、`SchemaVersion`、`SymbolKey` 或 authoritative manifest，导致 runtime、editor、UHT 都在消费“看起来像事实源”的不同文件与单例状态。
  - 本条目要引入 `BindingManifest` 作为单一 authority，先让 editor 生成、runtime 加载、UHT sidecar 输出都挂到统一 manifest schema，再保留 `Binds.Cache` / `.Headers` / `BindModules.Cache` 的双写双读兼容窗口；同时定义 `IAngelscriptBindingProvider` 扩展协议，允许后续外部 provider 以 manifest 为边界接入，而不是继续依赖 `RegisterBinds(int32, TFunction<void()>)` 这种只含顺序与 lambda 的隐式注册方式。
  - 目标状态是：每份 bind artifact 都能追溯 `ProviderId`、`ShardKind`、`SchemaVersion`、`SymbolKey` 与生成来源；runtime/editor/UHT 共享一套 manifest 解析与验证逻辑；legacy cache 只作为迁移兼容层存在，而不是继续承担 primary authority。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — 指出 bind database、class DB、UHT output 与 editor legacy generator 并存，artifact authority 不清。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — bind artifact 相关路径缺少 round-trip 与 schema 兼容测试，现有结构不利于隔离验证。
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-01`、`Arch-BP-02`、`Arch-BP-03`、`Arch-BP-07`、`Arch-BP-13` — 要求把 binding pipeline 从“代码生成副产物”升级为有 manifest、provider 和校验边界的正式协议。
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — 指出 provider/module authority 目前横跨 runtime、editor 与 UHT，缺少收口点。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — 建议建立统一 manifest/catalog，让多个消费者共享同一 binding 事实源。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — 参考实现都把元数据与生成物 authority 收口到可追溯协议，而不是仅凭散落 cache。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L23 — `GetBindState()` 仍回退到进程级 `LegacyBindState`，说明 bind authority 仍绑定在隐式全局状态上。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L151 — `RegisterBinds()` 只记录 `BindName`、`BindOrder` 与 `TFunction<void()>`，没有 provider metadata。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` L42 — `Save()` 继续写 `Binds.Cache`，并在 L98 追加 `.Headers`，runtime cache 与 header sidecar 是手工耦合的双文件协议。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L999 — `GenerateNativeBinds()` 仍维护 legacy editor-side bind generator，并在 L1077 输出 `BindModules.Cache`。
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` L174、L220、L246、L302 — UHT 已生成 json/csv/shard，但这些输出尚未纳入统一 manifest authority。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindingManifestTests.cpp`
- [ ] **P1.2** 📦 Git 提交：`[BindingManifest] Refactor: add provider manifest and artifact protocol`
- [ ] **P1.2-T** 单元测试：为 manifest round-trip、legacy shim 与 provider 校验建立测试
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindingManifestTests.cpp`
  - 测试场景：
    - 正常路径：editor/UHT 产出的 provider artifact 经 manifest round-trip 后，runtime 能按 `ProviderId`、`ShardKind`、`SymbolKey` 恢复同一组 entries。
    - 边界条件：只存在 legacy `Binds.Cache` / `.Headers`、manifest 缺少可选字段、或 schema 处于兼容版本时，加载逻辑走双读 shim 仍保持结果一致。
    - 错误路径：重复 `ProviderId + SymbolKey`、损坏 shard、schema major version 不兼容时，校验层拒绝导入并输出可断言错误。
  - 测试命名：`Angelscript.TestModule.BindingManifest.ProviderArtifactsRoundTrip`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.2-T** 📦 Git 提交：`[BindingManifest] Test: validate provider manifest round-trip`

### Phase 2: Reload ownership 与 symbol canonicalization

- [ ] **P2.1** 把 hot reload 提升为 engine-owned session，并为 version chain / tombstone / construct context 建立单一 owner
  - 当前 soft reload 需要先 `PrepareSoftReload()` 再 `DoSoftReload()`，但 request-scoped 状态并没有封装成明确 session：`GConstructASObjectWithoutDefaults`、`CurrentObjectInitializers`、`ReplaceHelper`、`NewerVersion`、`CleanupRemovedClass()` 等 reload 相关事实分别散落在 class generator、`UASClass` 和 editor helper 中；这让 full reload gate、removed class tombstone、构造期 defaults 抑制、旧类到新类链路都依赖隐式全局状态和调用顺序。
  - 本条目要引入 `FAngelscriptHotReloadSession`、`ASVersionChain`、request-scoped construct context，并把 tombstone owner、`FullReloadRequired` gate、replace helper 生命周期收口到 engine-owned reload coordinator；同时把 `GConstructASObjectWithoutDefaults` / `CurrentObjectInitializers` 从进程级状态改为 request-scoped 或 `thread_local`，把 `CLASS_NewerVersionExists`、`NewerVersion`、removed-class cleanup 统一为 session 产物，而不是每个子流程各自改一半。
  - 目标状态是：reload session 有明确开始、准备、commit、rollback、tombstone cleanup 边界；soft reload 不能绕过 full reload gate；version chain 有单一 owner 和可验证的 live head / tombstone 语义；tests 可以构造并断言“合法 soft reload”“必须 full reload”“removed class 变 tombstone”的不同路径。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — 多次指出 reload 流程依赖全局标志、调用顺序和原地 mutation，owner 不清。
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — runtime 生命周期分析也指出 reload 入口与 engine state 绑定松散，难以做隔离验证。
    - [B] `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` `Issue-6`、`Issue-7`、`Issue-30`、`Issue-31` — 明确把 hot reload 的全局状态、version link 与 cleanup 顺序列为 Architecture 类缺陷。
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` `Arch-HR-08`、`Arch-HR-15`、`Arch-HR-29` — 要求为 reload session、impact facts、version chain 建立单一 owner。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — 参考方案强调 `ReloadImpactManifest` / single impact fact，而不是多处散落事实。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2245 与 L2275 — soft reload 仍分成 prepare/do 两轮遍历，session 状态完全依赖外部顺序。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4094 — `PrepareSoftReload()` 直接写全局 `GConstructASObjectWithoutDefaults`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2578、L3230、L3699 — 旧 class/struct 通过原地写 `CLASS_NewerVersionExists` 与 `NewerVersion` 建 version chain。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L987 — `CurrentObjectInitializers` 是进程级静态数组，L988 的 `GConstructASObjectWithoutDefaults` 也是全局标志。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L917 — `GetMostUpToDateClass()` 直接走裸 `NewerVersion` 链，没有 tombstone/session owner 抽象。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` L25 — `ReplaceHelper` 仍是全局裸指针，在 L34 延迟创建。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadCoordinatorTests.cpp`
- [ ] **P2.1** 📦 Git 提交：`[HotReloadArchitecture] Refactor: move reload session and version chain under engine ownership`
- [ ] **P2.1-T** 单元测试：验证 reload session planner、tombstone owner 与 full-reload gate
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadCoordinatorTests.cpp`
  - 测试场景：
    - 正常路径：可软重载的 class 进入 session 后，planner 保持 live head 连续、soft reload 完成后旧版本转为 tombstone。
    - 边界条件：连续多次 reload、只有 struct 替换、或旧类已处于 `CLASS_NewerVersionExists` 状态时，version chain 仍能解析出唯一最新 head。
    - 错误路径：命中 `FullReloadRequired` 条件、removed class cleanup 顺序非法、或构造上下文跨 session 泄漏时，coordinator 拒绝 soft reload 并中止提交。
  - 测试命名：`Angelscript.TestModule.HotReload.SessionPlannerPreservesLiveHeadAndRejectsInvalidSoftReload`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.1-T** 📦 Git 提交：`[HotReloadArchitecture] Test: lock coordinator, tombstone, and full-reload gates`

- [ ] **P2.2** 建立统一 `type/symbol identity + source metadata canonicalization`
  - 当前 runtime lookup、bind exposure、debug/toolchain metadata 和 editor navigation 还没有共享 canonical symbol key：`FAngelscriptType` 主要以 name map + alias 组织，`FromTypeId()` 在无法精确匹配时退回 `ScriptType->GetName()`；`Bind_UObject.cpp` 同时保留两套 `FindClass` / `GetAllClasses` 路径；`Bind_BlueprintType.cpp` 多处通过去掉 `U` / `A` 前缀比对 class 名；`UASClass::GetSourceFilePath()` 与 `UASFunction::GetSourceFilePath()` 又都直接返回 `Module->Code[0]` 的绝对路径。
  - 本条目要引入 `FAngelscriptSymbolKey`、`FAngelscriptNominalClassRef`、`FAngelscriptSourceMetadata`，把 class/function/type/source span 的 canonical identity 抽成可复用结构；统一 `FindClass`、`__StaticClass`、`GetAllClasses`、`GetScriptTypeDeclaration`、`GetSourceFilePath`、`GetSourceLineNumber` 等入口，让 runtime/editor/debug/toolchain 都消费同一 symbol/source metadata。`RegisterAlias()` 继续保留做人类可读名字兼容层，但不再承担事实 identity。
  - 目标状态是：type lookup 不再依赖 stripped-name workaround 或 `Module->Code[0]` 假设；symbol identity 能稳定跨 hot reload、editor navigation、impact analysis 与 bind manifest 对齐；后续 debug/toolchain/IDE metadata 可以直接引用 canonical key，而不是重复再做一次 name-based 推导。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — 指出 class lookup、source metadata、名字兼容逻辑在 runtime/editor/bind 层重复实现。
    - [B] `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` `Issue-11`、`Issue-43`、`Issue-48`、`Issue-49`、`Issue-65` — 多个 Architecture/consistency 问题都指向 name-only identity 不可靠。
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-03`、`Arch-TS-07` — 要求建立 canonical type identity 与 nominal reference。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT-10`、`Arch-DT-17` — debug/toolchain metadata 需要共享统一 symbol/source key。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md`、`Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — 参考插件普遍提供 type catalog / IDE metadata / nominal symbol identity。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L331 与 L519 — 当前存在两套 `FindClass` 绑定逻辑，查找规则并不一致。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L280 与 L405 — class/function source metadata 分别转发到 `UASClass` / `UASFunction`，但没有共享 canonical source model。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1507 与 L1545 — class/function source path 都直接返回 `Module->Code[0].AbsoluteFilename`，缺少 canonical source span。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` L54、L108、L120 — type database 仍以名字注册与 alias 覆盖为主。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` L420 — `FromTypeId()` 在 script type 分支回退到 `GetByAngelscriptTypeName(ScriptType->GetName())`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L176、L1622、L1884、L2186 — 多处通过移除 `U` / `A` 前缀做 class identity workaround。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSymbolIdentityTests.cpp`
- [ ] **P2.2** 📦 Git 提交：`[TypeSystemArchitecture] Refactor: unify symbol identity and source metadata`
- [ ] **P2.2-T** 单元测试：验证 lookup、source metadata 与 reload 后 symbol identity 共享同一 canonical key
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSymbolIdentityTests.cpp`
  - 测试场景：
    - 正常路径：class/function/type lookup、source path、source line 与 script declaration 都能通过同一 `FAngelscriptSymbolKey` 解析。
    - 边界条件：alias 名、模板 subtype、脚本 struct / delegate / enum、以及 hot reload 后旧类 tombstone 仍能映射到稳定 nominal identity。
    - 错误路径：名字冲突、缺失 source metadata、或 stripped-name fallback 本应命中的场景在新 canonical key 体系下被显式拒绝并给出诊断。
  - 测试命名：`Angelscript.TestModule.SymbolIdentity.LookupAndSourceMetadataShareCanonicalKey`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.2-T** 📦 Git 提交：`[TypeSystemArchitecture] Test: cover lookup, source metadata, and tombstone identity`

### Phase 3: Editor workspace intelligence 与多表面适配

- [ ] **P3.1** 建立 `EditorAssetSyncService + SurfaceAdapterRegistry + WorkspaceIntelligence`
  - 当前 editor 侧对脚本文件变更、content browser 虚拟项、menu extension、source navigation、BlueprintImpact 的处理仍是多个入口各自直接碰 runtime state：目录监听回调直接把 `Changes` 推给 engine；content browser data source 把虚拟路径硬编码成 `/All/Angelscript/<Name>` 且没有实现 path/object 枚举；menu extension 通过遍历 `UASClass` 注册 extender，但对 selected paths/context 的传递仍然丢失；source navigation 与 BlueprintImpact 又各自从 runtime symbol 或 modules 拼接出自己要的 metadata。
  - 本条目要引入 `FAngelscriptEditorAssetSyncService`、`IAngelscriptEditorSurfaceAdapter`、`FAngelscriptWorkspaceIntelligence`，让 file watch、content browser、menu、source navigation、BlueprintImpact、后续 IDE metadata 都从同一 workspace snapshot 读取脚本目录、虚拟 folder tree、symbol/source metadata 与 impact facts。module startup 只负责装配 service 与 adapter registry，不再把每个 editor surface 都直接绑到 runtime 静态入口。
  - 目标状态是：editor 有一个可缓存、可增量刷新、可测试的 workspace intelligence 层；content browser 能按 folder/surface 增量枚举虚拟资产；menu extenders 能保留 selected path/context；source navigation、BlueprintImpact 和未来 IDE metadata 不再各自重造 symbol lookup；runtime/editor 之间只通过 manifest + canonical metadata 交换事实。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — 指出 editor surface 与 runtime metadata 耦合过深，lookup 逻辑重复。
    - [B] `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` `Issue-48`、`Issue-49` — 指出 editor 入口与 symbol/source data 的一致性问题已经影响行为与可维护性。
    - [D] `Documents/AutoPlans/ArchitectureReview/EditorArch_ArchReview.md` `Arch-06`、`Arch-07`、`Arch-51` — 要求为 editor data source、surface adapter、workspace intelligence 建正式边界。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — 建议“single impact fact, multiple consumers”的 editor/toolchain 共享事实模型。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` — 参考插件都把 editor listener、workspace data、IDE metadata 或 asset sync 组织成共享服务而不是每个表面各自实现。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L78 — `OnScriptFileChanges()` 直接从 engine roots 与 runtime state 计算变更，没有独立 asset sync service。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L111 — `OnEngineInitDone()` 直接 new/activate content browser data source，module startup 承担了 surface 装配与数据初始化两种职责。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L728 — 菜单中仍暴露 legacy binding generator，说明 editor surface authority 还没完全收口。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp` L28 — 虚拟项路径硬编码为 `/All/Angelscript/<Name>`；L124 与 L128 的 path/object 枚举尚未实现。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` L845 — `RegisterExtensions()` 通过遍历 `TObjectIterator<UASClass>` 注册 extender；L1020 与 L1032 的 lambda 忽略传入 `Paths`，selected context 没有进入 adapter 模型。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp` L123 — navigation 仍通过拼接 `PrefixCPP + Name` 调 runtime `GetClass()`。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h` L64 — `BuildImpactSymbols()` 与 `AnalyzeLoadedBlueprint()` 暴露了单独的 impact 符号路径，还未挂到共享 workspace intelligence。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorWorkspaceTests.cpp`
- [ ] **P3.1** 📦 Git 提交：`[EditorArchitecture] Refactor: add asset sync, surface adapters, and workspace intelligence`
- [ ] **P3.1-T** 单元测试：验证 asset sync、surface adapter 与 shared workspace metadata
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorWorkspaceTests.cpp`
  - 测试场景：
    - 正常路径：脚本文件变化经 `EditorAssetSyncService` 处理后，content browser、source navigation、BlueprintImpact 读取到同一份 workspace snapshot。
    - 边界条件：嵌套目录、空目录、selected paths/context、增量刷新与重复 file watch 事件不会破坏虚拟 folder tree 和 adapter state。
    - 错误路径：无效脚本路径、过期 workspace snapshot、或 surface adapter 请求未知 symbol/path 时，服务返回显式失败结果而不是直接回退 runtime 全局查询。
  - 测试命名：`Angelscript.TestModule.EditorWorkspace.AssetSyncAndSurfaceAdaptersShareIntelligence`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P3.1-T** 📦 Git 提交：`[EditorArchitecture] Test: verify content browser sync and shared workspace metadata`

## 阶段依赖关系

1. `P1.1` 是 `P2.1` 和 `P3.1` 的前置条件；没有显式 runtime identity，reload session 与 editor workspace 都会继续借用 ambient/global 回退。
2. `P1.2` 是 `P2.2` 与 `P3.1` 的前置条件；binding manifest 是 symbol/source metadata 和 editor workspace intelligence 的稳定事实源。
3. `P2.1` 应先于 `P2.2` 完成核心 owner 收口，再把 canonical symbol key 扩展到 reload tombstone / live head；否则 symbol identity 还会再次迁移。
4. `P3.1` 放在最后落地，以消费 `P1.1`、`P1.2`、`P2.2` 提供的 runtime identity、manifest 和 canonical metadata，避免 editor 表面先接入一套临时协议后再二次重写。

## 单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeIdentityTests.cpp` | 显式 owner 优先、fallback runtime、冲突 boot request 拒绝 | 高 |
| `P1.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindingManifestTests.cpp` | manifest round-trip、legacy shim、schema/provider 错误 | 高 |
| `P2.1` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadCoordinatorTests.cpp` | session planner、version chain、tombstone/full reload gate | 高 |
| `P2.2` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSymbolIdentityTests.cpp` | canonical symbol key、alias/template 边界、source metadata 错误 | 高 |
| `P3.1` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorWorkspaceTests.cpp` | asset sync、surface adapter、workspace snapshot 失效处理 | 中 |

## 验收标准

- runtime activation 不再要求通过 ambient world 反推出 primary engine；`module`、`subsystem`、`automation test` 三类启动入口都能构造显式 `BootRequest` 并通过自动化测试验证。
- runtime、editor、UHT 至少能围绕一套 `BindingManifest` 完成一次端到端 round-trip；legacy `Binds.Cache` / `.Headers` / `BindModules.Cache` 只作为兼容层保留，并有自动化测试覆盖。
- hot reload 至少具备可断言的 session owner、version chain owner、tombstone 语义；必须 full reload 的场景不能再被 soft reload 静默绕过。
- class/function/type/source metadata 对外暴露统一 canonical identity；重复 `FindClass` / stripped-name workaround 的调用链被收口到共享实现，自动化测试覆盖正常、边界、错误三类路径。
- editor 的 file watch、content browser、menu extension、source navigation、BlueprintImpact 能共享一份 workspace intelligence snapshot，至少有一条自动化测试验证多消费者读取同一事实源。

## 风险与注意事项

### 风险

1. **runtime boot 收口会触及 editor、commandlet、game instance 三种入口**
   - 缓解：`P1.1` 先保留兼容 shim，把 `TryGetCurrentEngine()` 和 ambient world 入口退化为 resolver facade，再逐步减少直接调用点。
2. **binding manifest 迁移期间需要同时兼容多种旧 artifact**
   - 缓解：`P1.2` 采用双写双读窗口，并把 legacy cache 回放与 schema 兼容性做成自动化测试，而不是靠手工验证。
3. **hot reload owner 收口可能改变现有 class replacement 顺序**
   - 缓解：`P2.1` 先锁定 `live head`、`tombstone`、`full reload gate` 三个最核心语义，再迁移构造上下文与 replace helper 生命周期。
4. **editor workspace intelligence 容易被目录监听时序问题污染**
   - 缓解：`P3.1` 采用 snapshot + adapter 的分层，让 file watch 只负责提交变更事件，消费者只读稳定快照。

### 已知行为变化

1. `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L718 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L101 这条“通过 ambient world 反推 current subsystem/runtime”的链路会被降级为兼容入口；后续新代码必须显式传入 runtime owner 或 `BootRequest`。
2. `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` L42-L103 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L999-L1077 当前生成的 `Binds.Cache`、`.Headers`、`BindModules.Cache` 会变成兼容产物，新的 authoritative artifact 是 `BindingManifest`。
3. `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1497-L1558 当前“类/函数 source path 等于 `Module->Code[0]`”的行为会改变；依赖绝对路径首项的 editor/debug/toolchain 逻辑需要改为消费 canonical `SourceMetadata`。
4. `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp` L28 当前固定 `/All/Angelscript/<Name>` 的虚拟路径模型会升级为基于 workspace intelligence 的 folder-aware 路径；任何依赖该硬编码路径的 editor 扩展都需要同步调整。

---

## 本轮深化追加（2026-04-09）

### Phase 1 补充：extension contract 与 compiler strategy

- [ ] **P1.3** 建立显式 `ExtensionRegistry + ContributorProvenance`，收口 bind/type/compile observer 的外部接入合同
  - 当前 `FAngelscriptRuntimeModule`、`FAngelscriptBinds`、`FAngelscriptType` 仍把扩展能力暴露成“public header + static 注册 + raw delegate”组合：宿主模块能接入，但没有 `IAngelscriptExtensionModule`、没有 contributor 身份、也没有 bind/type/provider provenance。结果是扩展“能工作”，却无法稳定回答“谁注册了什么”“哪个模块提供了哪个 bind/type”“owner teardown 后是否还残留 observer”。本条目作为既有 `P1.2` 的运行期 contract 补项，关注的是 contributor authority 与 provenance，不重复 `Plan_UnrealCSharpArchitectureAbsorption.md` 已覆盖的 generator artifact/cache 主体，也不扩张 watcher/file-manifest 主题。
  - 目标状态是：runtime 在初始化期能显式发现 extension module；bind/type/compile observer 统一走 `FAngelscriptExtensionRegistry` 注册；每个 contributor 都有 `SourceModule`、`ExtensionKind`、`ContributorId` 与可审计生命周期；`DisabledBindNames`、`StateDump` 与自动化测试都能按 contributor 维度断言，而不是只看全局数组“是否大致有东西”。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `A-07`、`A-10` — engine 级长期 delegate 没有 owner/解绑，说明当前扩展与事件面仍停留在静态全局回调模型。
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` `发现 10` — runtime 模块只保留零散 delegate seam（如 `GetDynamicSpawnLevel()`），缺少成体系的扩展 contract。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-29`、`NewTest-46` — 当前测试抓不住 bind 缺失/污染，也缺 previous-bind metadata 原生回归，反证 registry/provenance 仍不可测。
    - [D] `Documents/AutoPlans/ArchitectureReview/ExtensionPoints_ArchReview.md` `Arch-EP1`、`Arch-EP5` — 明确要求把隐式 bind/type 扩展改成显式 `extension registry/event hub`，并区分 observer 与 strategy authority。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — `SupportedModule/SupportedAssetPath/bEnableExport + OnBeginGenerator/OnEndGenerator` 把扩展范围和阶段做成显式 contract。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — `CustomLoadLuaFile` 与 `GetModuleName()` 展示了宿主可实现 protocol 比 raw static helper 更可发现、更可维护。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` L9-L21、L32-L47 — 只暴露 raw delegate 类型与 `GetXxx() -> Delegate&` 静态入口，没有 extension module/registry 协议。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L72-L135 — compile/editor/literal-asset hook 仍是 function-local static delegate，本体与 owner 生命周期未绑定。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L120-L153 — `FBindFunction` 只有 `BindName`、`BindOrder`、`TFunction<void()>`，`RegisterBinds()` 不记录 `SourceModule`/provider provenance。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` L54-L118 — type registration 仍是全局静态数据库与 finder 列表，没有 contributor identity 或 teardown contract。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionRegistry.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionRegistry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptExtensionRegistryTests.cpp`
- [ ] **P1.3** 📦 Git 提交：`[ExtensionArchitecture] Refactor: add explicit extension registry and contributor provenance`
- [ ] **P1.3-T** 单元测试：验证 extension registry、provenance 与 owner teardown
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptExtensionRegistryTests.cpp`
  - 测试场景：
    - 正常路径：宿主 contributor 同时注册 bind/type/compile observer 后，registry 能按 `ContributorId`、`SourceModule`、`ExtensionKind` 枚举，并驱动 bind/type 安装。
    - 边界条件：重复 `BindName`、同名 contributor、多 contributor 同时存在、以及 `DisabledBindNames` 过滤时，registry 仍能保留 provenance 并给出稳定排序/启用状态。
    - 错误路径：contributor 在 owner teardown 后仍残留 callback、provider 缺少必填 metadata、或重复 `ContributorId` 冲突时，registry 显式拒绝注册并输出可断言诊断。
  - 测试命名：`Angelscript.TestModule.Extension.RegistryTracksContributorProvenanceAndTeardown`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.3-T** 📦 Git 提交：`[ExtensionArchitecture] Test: cover registry provenance and owner teardown`

- [ ] **P1.4** 建立 `ImportResolver + CompileContext` 早期 compiler extension surface
  - 当前公开的 compile/reload hook 大多发生在“导入已解析、预处理已成形”之后：`OnProcessChunks` 与 `OnPostProcessCode` 都晚于 `ProcessImports()`；`FAngelscriptAdditionalCompileChecks` 又只是 `FAngelscriptEngine` 上一张公开 map，需要外部直接改 engine 实例状态。本条目要把“能订阅编译事件”推进到“能正式扩展编译策略”，补的是 `compiler/import` contract，而不是重复 `Plan_ScriptFileSystemRefactor.md` 的 script-root/file-scan 主体。
  - 目标状态是：`import/module resolution`、additional compile checks 与 compile/reload phase 都有显式注册入口；`BeforeResolveImports`、`IAngelscriptImportResolver`、`RegisterAdditionalCompileChecks(...)` 与统一 `FAngelscriptCompileContext`/`FAngelscriptReloadContext` 可以让宿主在不改插件源码的前提下改变编译语义，同时保持多 engine 隔离与旧 `OnProcessChunks` 时序兼容。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` `发现 83` — preprocessor 仍依赖公开 raw helper 名驱动 codegen，说明 compiler/preprocess 协议仍在通过文本约定外泄，而不是正式 strategy surface。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-46` — compile-out helper 与 previous-bind metadata 仍缺 focused regression，说明现有 compile/bind mutation seam 不可见、难验证。
    - [D] `Documents/AutoPlans/ArchitectureReview/ExtensionPoints_ArchReview.md` `Arch-EP2` — 直接指出 `ProcessImports()` 早于现有 hook，`AdditionalCompileChecks` 只是 engine map，缺少早期 compiler extension surface。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — `CustomLoadLuaFile` 把 resolver 放在真正的加载路径中，而不是后处理字符串 hook。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — `OnBeginGenerator`、`OnEndGenerator`、`OnCSharpEnvironmentInitialize` 展示了“命名化阶段 + settings 驱动 allowlist”的 compiler/toolchain contract。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L29-L30、L217 — 公开的只有 `OnProcessChunks`/`OnPostProcessCode`，`ProcessImports(...)` 仍是内部私有函数。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L232-L239、L265-L287 — 先执行 `ProcessImports()`，之后才广播 `OnProcessChunks` 与 `OnPostProcessCode`，现有 hook 介入时序过晚。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L357-L359 — `AdditionalCompileChecks` 仍是公开 `TMap<UClass*, TSharedPtr<FAngelscriptAdditionalCompileChecks>>`，没有正式注册 API。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h` L4-L8 — compile-check 扩展只是一组裸虚接口，没有 registry、context 或 engine-scope contract。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptImportResolver.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptImportResolver.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCompileContext.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCompileStrategyTests.cpp`
- [ ] **P1.4** 📦 Git 提交：`[CompilerArchitecture] Refactor: add import resolver and engine-scoped compile context`
- [ ] **P1.4-T** 单元测试：验证 import resolver 时序、engine-scope compile checks 与错误诊断
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCompileStrategyTests.cpp`
  - 测试场景：
    - 正常路径：自定义 `IAngelscriptImportResolver` 在 `ProcessImports()` 之前接管模块解析，且 `RegisterAdditionalCompileChecks(...)` 能对目标 `CodeSuperClass` 生效。
    - 边界条件：resolver 明确返回“未处理”时仍回退默认 import 逻辑；legacy `OnProcessChunks` 与 `OnPostProcessCode` 的触发顺序保持不变；两个 engine 并存时 compile checks 不串台。
    - 错误路径：resolver 返回循环依赖、非法模块名、或 compile-check provider 试图跨 engine 注册时，系统输出显式 diagnostics 并拒绝继续沿用脏状态。
  - 测试命名：`Angelscript.TestModule.Compiler.ImportResolverRunsBeforeChunkHooksAndStaysEngineScoped`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.4-T** 📦 Git 提交：`[CompilerArchitecture] Test: cover early import resolver and engine-scoped compile checks`

### Phase 2 补充：native reload bridge 与 session audit

- [ ] **P2.3** 建立 `NativeReloadBridge + ExternalStateAdapters + ReloadSessionAudit`
  - 当前热重载主链仍是“script-file change -> 流式 `OnClassReload/OnStructReload/...` 广播 -> editor helper 用静态状态拼出 repair 行为”：`ClassReloadHelper` 用全局 `ReloadState()` 与 rooted `ReplaceHelper` 收集本轮变更，却没有 owner/teardown 契约；runtime 侧也没有监听 `ReloadCompleteDelegate` / `OnHotReload()` 的 native reload bridge，`QueuedFullReloadFiles` 只是文件队列，不携带 session/native drift/audit 事实。本条目作为既有 `P2.1` 的深化补项，关注 `native reload -> script reload` 的桥接、外部状态适配与 session 级审计；不重复 `Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的 watcher 验证矩阵，也不重复 `Plan_ScriptFileSystemRefactor.md` 的 source-index/file-scan 主体。
  - 目标状态是：UE native reload 与 script reload 之间存在正式 `NativeReloadBridge`；bridge 能基于 native contract fingerprint 标记受影响 `UASClass`、收集 external state adapter（如 open editors、BlueprintImpact、workspace consumers）反馈，并把结果写进单一 `FAngelscriptHotReloadSession`/audit 对象；legacy `OnClassReload` 等旧 delegate 保留为派生事件，但不再是唯一事实源。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 48`、`发现 49` — `replace -> remove` 后旧版本链仍会把 `GetMostUpToDateClass()` 导向失效 head，且现有自动化抓不住这类链路。
    - [B] `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` `Issue-6`、`Issue-9` — 版本链缺少单一 owner，`NewerVersion` 是 GC 不安全的裸指针，`GetMostUpToDateClass()` 有真实 UAF 风险。
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` `Arch-HR-18`、`Arch-HR-25`、`Arch-HR-36`、`Arch-HR-38` — legacy reload 不发布标准 UE reload 信号、native reload 无桥接、observer 无 owner、对外缺少 session 级 delta 协议。
    - [E] `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` — puerts 明确把 native `ReloadCompleteDelegate/OnHotReload()` 与 source hot reload 分成两条路径，并用 `MakeSharedJsEnv()` / `HMR.prepare/HMR.finish` 组织生命周期事件。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` L27-L175 — `ReloadState()` 是进程级 static，`Init()` 通过一组 `AddLambda` 订阅脚本 reload 事件，没有 owner/teardown handle。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` L20-L37 — 源码注释直接承认当前路径是 hacked-together mess，`ReplaceHelper` 仍是 rooted 全局对象。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` L12-L38 — 只有 script reload 事件，没有 native reload bridge 或 session/audit 对象。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L912-L923 — `GetMostUpToDateClass()` 仍通过裸 `NewerVersion` 链手工追尾，没有 latest-resolve/audit 抽象。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L419、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4168-L4186 — full reload 延后仍只是往 `QueuedFullReloadFiles` 塞文件，没有 native drift/session context。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptNativeReloadBridge.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptNativeReloadBridge.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptReloadAudit.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptReloadAudit.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptNativeReloadBridgeTests.cpp`
- [ ] **P2.3** 📦 Git 提交：`[HotReloadBridge] Refactor: add native reload bridge, external state adapters, and session audit`
- [ ] **P2.3-T** 单元测试：验证 native reload bridge、external adapter 审计与重复批次保护
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptNativeReloadBridgeTests.cpp`
  - 测试场景：
    - 正常路径：模拟 native reload 完成后，bridge 能识别受影响 `UASClass`、写入 `FAngelscriptHotReloadSession`，并把 reload reason 升级为可断言的 audit 结果。
    - 边界条件：只有 open editor/BlueprintImpact/workspace consumer 持有旧引用时，external state adapter 能列出未替换项；`angelscript.UseUnrealReload=0/1` 两种 backend 都只生成一份 session/audit。
    - 错误路径：同一帧同时发生 script reload 与 native reload、fingerprint 缺失、或 stale `NewerVersion` 链导致 latest-resolve 失败时，bridge 去重/拒绝重复提交，并输出显式失败诊断而不是静默继续。
  - 测试命名：`Angelscript.TestModule.HotReload.NativeReloadBridgeAuditsAffectedScriptTypesAndExternalState`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.3-T** 📦 Git 提交：`[HotReloadBridge] Test: cover native bridge audit and duplicate-batch protection`

## 与既有主线的衔接

1. `P1.3` 与 `P1.4` 都是对既有 `P1.2` 的 contract 补强，优先用于收口“谁能扩展、何时扩展、如何被审计”，再让 `P2.*` 和 `P3.1` 消费更正式的 extension/compiler surface。
2. `P2.3` 以既有 `P2.1` 的 engine-owned reload session 为前置，但可以先以 `warn-only bridge + audit` 骨架落地，不必等待 watcher/source-index 主体重构完成。
3. 本轮明确排除三类重复范围：`Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的 watcher 验证矩阵、`Plan_ScriptFileSystemRefactor.md` 的 script root/file scan 重构、以及 `Plan_UnrealCSharpArchitectureAbsorption.md` 已单列的 manifest/cache 吸收项。

---

## 本轮深化追加（2026-04-09 续）

### Phase 1 补充：script annotation contract 与 compiler authority

- [ ] **P1.5** 建立 `specifier / metadata handler registry + trait catalog`
  - 当前 `UCLASS / UFUNCTION / UPROPERTY` 注解一方面仍由 `FAngelscriptPreprocessor` 的 `PP_NAME_* + if/else` 链硬编码解释，另一方面又在 reload 分析、`UClass/UFunction/FProperty` 落地与调试/文档导出时分散成多份事实源。`Config=`、`DefaultConfig`、`WithValidation`、`HideCategories`、`BlueprintSpawnableComponent` 这些语义并没有统一 annotation authority，导致 class generator、reload gate 与 tooling 只能各自猜测“哪类注解变化算结构变化、哪类只算 metadata 漂移”。
  - 本条目不把现有 built-in specifier 一次性脚本化，而是在 `P1.3` / `P1.4` 的扩展与 compile context 基础上补一层 `IAngelscriptSpecifierExtension`、`FAngelscriptAnnotationTraitCatalog` 与 `RegisterSpecifierExtension()`。core 继续以兼容 handler 解释现有 `PP_NAME_*`，但产出统一 trait view；未知或项目自定义 annotation 再走 registry。reload 分析、docs/debug/dump 统一消费 trait catalog，而不是继续分头读取 `Meta`、`Flags`、`FunctionDesc` 与运行时对象状态。
  - 这样既能吸收参考插件把 annotation 语义前移成正式 contract 的经验，也能把 A/C 维度里反复出现的 specifier 漂移问题转成可审计的 trait diff。优先级高于纯“开放扩展”诉求，因为当前源码已经证明 built-in annotation 自身就缺少统一 authority。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — `Config=` / `DefaultConfig`、`WithValidate`、property/class metadata 在 `SoftReloadOnly` 下反复漂移，暴露出 annotation 语义与 reload/state authority 分裂。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — previous-bind metadata、deprecated/script-side metadata parity 仍缺 focused regression，说明 annotation 输出面还不可稳定验证。
    - [D] `Documents/AutoPlans/ArchitectureReview/ExtensionPoints_ArchReview.md` `Arch-EP32` — 直接指出当前 specifier/metadata 语言仍是 closed-world parser，第三方无法在不改 core 的前提下扩展注解 contract。
    - [E] `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` — `UEMeta.ts` 把 specifier/meta 统一采集并在 authoring 阶段校验，说明 annotation 语义可以先成为显式工具链 contract。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — `UnLuaSettings` / locator / typed contract 展示了“先建模 provider，再消费语义”，而不是继续堆 parser 分支。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L29 — 当前对外仍只有 `OnProcessChunks` / `OnPostProcessCode` 两个整轮 hook，没有 annotation handler 注册面。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L237/L259/L263/L265 — `ProcessImports()`、`ProcessMacros()`、`ProcessDelegates()` 都在 `OnProcessChunks.Broadcast(*this)` 之前完成，外部代码无法介入 annotation 解释。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L2272 — `ProcessClassMacro()` 仍用 inline `if/else` 硬编码 `Config`、`DefaultConfig`、`Blueprintable` 等 class annotation。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L2395 — `ProcessPropertyMacro()` 内仍以 inline specifier 分派处理 property annotation，没有 trait/provider catalog。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L1032 — `FAngelscriptFunctionDesc::IsDefinitionEquivalent()` 仍未比较 `bNetValidate`，annotation diff 没有统一的结构变更 authority。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3294 — `ConfigName` / `DefaultConfig` 仍只在 full reload class creation 路径落地到 `UClass`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptSpecifierExtension.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Private/Extension/AngelscriptSpecifierExtension.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAnnotationTraitCatalog.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAnnotationTraitCatalog.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSpecifierExtensionTests.cpp`
- [ ] **P1.5** 📦 Git 提交：`[CompilerExtension] Refactor: add specifier handler registry and annotation trait catalog`
- [ ] **P1.5-T** 单元测试：验证 additive annotation handler、built-in trait 兼容与 reload trait diff
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSpecifierExtensionTests.cpp`
  - 测试场景：
    - 正常路径：外部模块注册自定义 class/property/function annotation handler 后，trait catalog 能记录 `SourceModule`、consumer 与产出的新增 meta/trait，且编译通过。
    - 边界条件：内建 `Config=`、`DefaultConfig`、`WithValidation` 继续由 core handler 解释，并在 trait catalog 中与自定义 handler 并存；修改这些 trait 时 reload 分析能看见差异而不是静默沿用旧状态。
    - 错误路径：extension handler 试图覆盖 built-in specifier、输出冲突 trait、或返回不完整 priority/diagnostics 时，编译阶段显式拒绝并保留旧脚本语义不变。
  - 测试命名：`Angelscript.TestModule.Compiler.SpecifierExtensions.PreserveBuiltInTraitsAndExposeCustomHandlers`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.5-T** 📦 Git 提交：`[CompilerExtension] Test: cover additive annotation handlers and trait diff visibility`

### Phase 3 补充：editor action 与 asset authoring workflow

- [ ] **P3.2** 建立 `EditorActionInvocation + InputPresenter` 合同，收口 editor action 的反射执行链
  - 当前 editor action 的主路径仍是“把上下文压扁成 `FExtenderSelection` -> 走一次通用 prompt -> 直接 `ProcessEvent()`”。这套模型扩展面很宽，但它把 typed validation、host-specific widget、selected path/blueprint context 与执行结果报告都挤进了一次性参数窗体里；一旦 action 需要 wizard、多阶段确认、或要在 stale function / invalid context 时给出结构化失败，现有 contract 就只能继续往 `ScriptEditorPrompts` 里塞更多特判。
  - 这不是单纯的 UI 体验问题。B/C 维度已经证明 runtime `ProcessEvent` 路径对 stale `UFunction`、validate 分流和错误传播本身就不够强，而 editor prompt 仍把所有 action 执行都落到裸 `ProcessEvent`。本条目要引入 `FAngelscriptEditorActionInvocation`、`IAngelscriptEditorActionInputPresenter`、`FAngelscriptEditorActionResult`，让 `Prepare -> Validate -> Present -> Execute -> Report` 成为显式生命周期；当前 `FStructOnScope + ProcessEvent` 只保留为默认 reflected presenter/fallback。
  - 本条目以 `P3.1` 的 typed surface context 为前置，不重做 surface adapter；重点是让“拿到上下文之后怎样执行 action”也成为正式 contract，而不是继续把所有语义压回 `ShowPromptToCallFunction()`。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` — stale `UASFunction` 通过 `ProcessEvent` / thunk 调用会静默 no-op，说明裸反射调用本身已经缺少显式失败合同。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-24` — `UAngelscriptComponent::ProcessEvent()` 仍需要单独补 validate-before-body 回归，说明执行链缺少统一的验证/报告层。
    - [D] `Documents/AutoPlans/ArchitectureReview/EditorArch_ArchReview.md` `Arch-27` — 当前脚本 action 输入仍是单一反射 prompt，难以承载 wizard、typed validation 和 host-specific widget。
    - [D] `Documents/AutoPlans/ArchitectureReview/EditorArch_ArchReview.md` `Arch-28` — surface 上下文在注册层已被压扁，进一步放大了 action 执行 contract 的脆弱性。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — UnrealCSharp 把常用 author path 前移到 Blueprint toolbar，说明 action 入口应更聚焦作者工作流而不是只提供“万能菜单”。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — 当前 `Angelscript` 的 `ScriptEditorPrompts` 更像通用执行基础设施，而 UnLua / puerts 已把高频 editor 动作建模成稳定 workflow object 或 toolbar chain。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` L98 — `FExtenderSelection` 仍只有 `SelectedObjects` / `SelectedAssets` 两类扁平上下文。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` L143 — `CallFunctionOnSelection()` 仍无条件转发到 `FScriptEditorPrompts::ShowPromptToCallFunction(...)`。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` L1020 — `ContentBrowser_AssetContextMenu` 收到 `Paths` 后仍直接丢弃。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` L1032 — `ContentBrowser_PathViewContextMenu` 同样没有把 `SelectedPaths` 带入 action 执行。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp` L192 — 当前执行前仍先构造一次性 `FStructOnScope` 并导入默认值。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp` L255/L260 — 最终执行仍是直接 `ProcessEvent(...)`，没有 invocation result / validator / presenter contract。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h` L43/L46/L49/L52 — `UScriptEditorSubsystem` 只暴露基础生命周期事件，没有 action presenter / validator / report API。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Actions/AngelscriptEditorActionInvocation.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Actions/AngelscriptEditorActionInputPresenter.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Actions/AngelscriptEditorActionInvocation.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Actions/AngelscriptReflectedActionPresenter.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorActionInvocationTests.cpp`
- [ ] **P3.2** 📦 Git 提交：`[EditorActions] Refactor: add invocation and presenter contract for editor actions`
- [ ] **P3.2-T** 单元测试：验证 typed context、presenter fallback 与执行失败诊断
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorActionInvocationTests.cpp`
  - 测试场景：
    - 正常路径：action 通过自定义 presenter 获取 typed context，先 `ValidateInputs()` 再执行，最终返回结构化 `ActionResult` 并完成调用。
    - 边界条件：未迁移 action 继续走 reflected presenter fallback；`SelectedPaths`、`UBlueprint` 上下文和 legacy `SelectedObjects` 可同时进入 invocation context 而不破坏旧菜单。
    - 错误路径：invalid input、stale `UFunction`、或 presenter/validator 明确拒绝执行时，系统在进入 `ProcessEvent()` 前返回显式失败报告，不留下静默 no-op。
  - 测试命名：`Angelscript.TestModule.Editor.ActionInvocation.ValidatesTypedContextBeforeExecution`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P3.2-T** 📦 Git 提交：`[EditorActions] Test: cover presenter fallback and explicit action failure contracts`

- [ ] **P3.3** 建立 `AssetWorkflowService + ContentBrowser authoring bridge`
  - 当前脚本资产 authoring 仍由 editor module 的静态 popup 驱动：`ShowCreateBlueprintPopup()` 负责默认路径推导、保存对话框、`CreateBlueprint` / `NewObject`、保存与打开；`ShowAssetListPopup()` 又在模块里临时拼一套 `AssetPicker`。与之相对，`UAngelscriptContentBrowserDataSource` 虽然已经把脚本资产接进 `/All/Angelscript/...`，但 `GetItemPhysicalPath()`、`EditItem()`、`BulkEditItems()`、`AppendItemReference()`、virtual-path bridge 仍全部返回 `false`。
  - 这说明当前插件已经具备 asset-centered editor truth，但 authoring 行为仍没有单一 owner。继续在模块静态函数和 data source 之间分摊“创建/打开/引用/路径回桥”，会让后续 `ContentBrowser.AddNewContextMenu`、项目自定义创建向导、PIE guard、批量打开/复制引用和 workspace snapshot 全都重复实现。应先抽成 `FAngelscriptAssetWorkflowService`，再让 data source、toolbar/menu 与 runtime bridge 都只调用同一条服务线。
  - 由于这条议题主要由 D/E 维度驱动、尚未在 A/B/C 里形成大量 crash 级证据，本轮把它放在 `P3` 收尾阶段，优先作为 authoring contract 收口，而不是抢在 runtime/hot reload 主线上游之前。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/EditorArch_ArchReview.md` `Arch-29` — 当前脚本资产 authoring 仍是模块级静态 popup，没有形成可替换的 workflow service。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — 当前插件已经是明显的 asset-centered editor model，但缺的是“更聚焦的作者路径”与单一 workflow authority。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — `DynamicDataSource` 已把 `AddNewContextMenu`、`EditItem()`、`AppendItemReference()` 等动作接回统一 data source/workflow。
    - [E] `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` — `UPEBlueprintAsset::LoadOrCreate()` 展示了稳定 workflow object + PIE guard 的收口方式。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — `GetModuleName + template/toolbar` 说明高频 authoring path 更适合围绕单一 asset truth/service 组织，而不是退回一次性 popup。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L418 — `ShowCreateBlueprintPopup()` 仍是模块级静态 authoring 入口。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L449 — 默认创建目录仍通过 `GetRelativeSourceFilePath()` + `AssetRegistry` 启发式倒推。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L541 — `ShowAssetListPopup()` 继续手工构建 `AssetPicker` 和“Create New Blueprint/DataAsset”按钮，再回调静态 popup。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp` L182 — `GetItemPhysicalPath()` 仍直接 `return false`。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp` L187 — `CanEditItem()` / `EditItem()` / `BulkEditItems()` / `AppendItemReference()` 仍全部未接通。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp` L249 — `Legacy_TryConvertPackagePathToVirtualPath()` / `Legacy_TryConvertAssetDataToVirtualPath()` 仍未实现 path bridge。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AssetWorkflow/AngelscriptAssetWorkflowService.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AssetWorkflow/AngelscriptAssetWorkflowService.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptAssetWorkflowServiceTests.cpp`
- [ ] **P3.3** 📦 Git 提交：`[AssetWorkflow] Refactor: extract asset workflow service and content browser bridge`
- [ ] **P3.3-T** 单元测试：验证创建/打开/引用/路径回桥共用同一 workflow service
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptAssetWorkflowServiceTests.cpp`
  - 测试场景：
    - 正常路径：从已选 `ContentBrowser` 目录触发创建时，workflow service 生成稳定默认路径，并能完成 `Blueprint` / `DataAsset` 创建后自动打开。
    - 边界条件：多资产列表、虚拟 `/All/Angelscript/...` 路径、legacy popup 入口与 data source `EditItem()` 同时存在时，仍共享同一解析与打开逻辑。
    - 错误路径：PIE 中禁止创建、无效虚拟路径、或缺失 backing asset 时，workflow service 返回显式失败并阻止继续打开/保存。
  - 测试命名：`Angelscript.TestModule.Editor.AssetWorkflowService.BridgesContentBrowserAndCreateOpenPaths`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P3.3-T** 📦 Git 提交：`[AssetWorkflow] Test: cover create open reference and path bridge behavior`

## 本轮补充的衔接与边界

1. `P1.5` 依赖 `P1.3` / `P1.4` 先把 extension registry 与 compile context 收口，再把 annotation handler 接进正式 contract；它不重复 `Plan_HazelightCapabilityGap.md` 的单点 bind gap 清单。
2. `P3.2` 以 `P3.1` 的 typed surface context 为前置，只解决 action invocation contract，不重复 `surface adapter + workspace intelligence` 本体。
3. `P3.3` 与 `P3.1` 共享 workspace/path snapshot，但不重做 script-root/file-scan 主体；`ShowCreateBlueprintPopup()` / `ShowAssetListPopup()` 在迁移期只保留为 compatibility shim。
4. `P3.2` / `P3.3` 与 `Documents/Plans/Plan_TestCoverageExpansion.md` 的关系是“owner contract 改造先行，覆盖率计划随后消费”，不重复其 `ContentBrowser` 注册 smoke 或 editor test 扩容条目。

## 本轮补充的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.5` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSpecifierExtensionTests.cpp` | additive annotation handler、built-in trait 兼容、reload trait diff 可见性 | 高 |
| `P3.2` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorActionInvocationTests.cpp` | typed context、presenter fallback、显式失败报告 | 中高 |
| `P3.3` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptAssetWorkflowServiceTests.cpp` | create/open/reference/path bridge 共线、PIE/无效路径错误 | 中 |

## 本轮补充的验收与风险

### 验收补充

- 至少一个外部模块可以在不修改 `AngelscriptPreprocessor.cpp` 的前提下注册自定义 annotation handler，并在 `StateDump`/诊断中看到其 contributor 与消费结果。
- 至少一个 editor action 从“通用 prompt + 直接 `ProcessEvent`”迁到显式 invocation/presenter contract，且无效输入会在执行前被阻止。
- `ContentBrowser` 的创建、打开、复制引用和路径回桥至少有一条统一 workflow service 骨架，旧 popup 入口继续可用但不再是唯一 owner。

### 风险补充

1. **annotation handler registry 容易与 built-in specifier 争优先级**
   - 缓解：`P1.5` 第一阶段限定为 additive-only，built-in consumer 永远先跑；extension 只能消费未知 annotation 或追加 trait，不能覆写 core 语义。
2. **action invocation 双轨期可能引入 presenter/旧 prompt 并存的重复注册和调试负担**
   - 缓解：先让 `FScriptEditorPrompts` 退化为 default reflected presenter，并通过 `ActionResult` 统一收口执行日志与失败报告。
3. **asset workflow service 若过早改动虚拟路径与物理路径映射，容易打破现有 `/All/Angelscript` 使用习惯**
   - 缓解：第一阶段只收口 owner 与 API；virtual-path/folder-aware 结构继续复用 `P3.1` snapshot，`ShowCreateBlueprintPopup()` 保留兼容壳直到 path bridge 稳定。

---

## 本轮深化追加（2026-04-09 再续）

### Phase 2 补充：module deactivation 与执行静默闸门

- [ ] **P2.4** 建立 `ModuleDeactivationContract + ExecutionQuiescenceBarrier`
  - 现状不是“reload session 不存在”，而是 `P2.1` / `P2.3` 之后，旧模块如何安全退场仍未成为 first-class contract：`DiscardModule()` 只回收 free pool、立刻断开脚本函数指针；运行层却仍允许 active context 直接 `PushState()` 嵌套执行。结果是调用面能继续拿到旧 `UASFunction` 外壳、`IsFunctionImplementedInScript()` 继续报 true、真正进入 thunk 时又静默 `return`。这已经不是单点 stale-function bug，而是缺少“准备退场 -> 等待静默 -> 正式 discard -> 对外宣告完成”的模块级生命周期。
  - 本条作为 `P2.1` engine-owned reload session 与 `P2.3` native reload bridge 的中段补口，目标不是重写 hot reload，而是把旧模块退场补成正式 phase：新增 `PrepareDeactivateModule` / `AwaitQuiescence` / `FinalizeDiscard` 三段式 contract，把 in-flight execution lease、stale function policy、deactivation diagnostics 与外部 observer 都挂进同一批 reload session。旧路径默认仍可走 destructive discard，但一旦启用新 contract，就不能再出现“函数句柄仍可见、调用时 silent no-op”的半失效态。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 59` — `IsFunctionImplementedInScript()` 只检查 `UASFunction` 外壳，module discard 后仍会把失效函数视为“已实现”。
    - [B] `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` `Issue-14` — 已明确要求统一失效 `UASFunction` 的发现与调用语义，避免 `FindFunctionByName()` 仍能找到旧函数而执行时 silent no-op。
    - [B] `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` `Issue-6` — 版本链生命周期缺少单一 owner，replace/remove/query 规则分散，说明 reload 退场本身尚未被建模为稳定阶段。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-56` — 模块退场/关闭缺少对称 `deactivation` 合约。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-57` — discard 默认假设 runtime 已静默，缺少 `in-flight execution` quiescence barrier。
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` `Arch-HR-40` — 当前扩展层拿到的是“已宣告完成”的 reload，说明 finish 边界本身还不可靠。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — `UnLua` 会显式修补 running stack，`puerts` 提供 `HMR.prepare / HMR.finish`，说明 reload 退场应先成为公开 contract，而不是埋在 discard 尾部。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1026-L1065` — `DiscardModule()` 只回收 thread-local/global free pool 后就直接 `Engine->DiscardModule()`，并把 `UASFunction::ScriptFunction` / `ValidateFunction` 置空，没有 deactivation callback 或 quiescence 等待。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1797-L1805` — 当前线程已有 active context 时，会直接 `PushState()` 进入嵌套执行，证明 discard 前并不存在“禁止继续进入旧模块执行”的显式闸门。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L979-L984` — `IsFunctionImplementedInScript()` 只要还能拿到 `UASFunction` 外壳就返回 true，不校验 `ScriptFunction` 是否仍有效。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L153-L156` 与 `L480-L481` — BPVM/parms 调用在 `ScriptFunction == nullptr` 时直接 `return`，当前 stale function 的最终表现仍是 silent no-op。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleLifecycle.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleLifecycle.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExecutionLease.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExecutionLease.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleQuiescenceTests.cpp`
- [ ] **P2.4** 📦 Git 提交：`[HotReloadLifecycle] Refactor: add module deactivation contract and execution quiescence barrier`
- [ ] **P2.4-T** 单元测试：验证 module deactivation、quiescence barrier 与 stale function 诊断统一收口
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleQuiescenceTests.cpp`
  - 测试场景：
    - 正常路径：module 进入 `PrepareDeactivate -> AwaitQuiescence -> FinalizeDiscard` 后，observer 能收到成对 lifecycle 事件，旧模块只在 quiescence 达成后才真正 discard。
    - 边界条件：同线程 active context 触发嵌套 `PushState()`、或已有旧 `UASFunction*` 句柄仍被外部持有时，reload session 会延迟 discard 并把函数状态显式标记为 `Deactivating/Stale`，而不是继续报告“已实现”。
    - 错误路径：deactivation participant 拒绝退场、quiescence 超时、或 stale function 在退场窗口内被再次调用时，系统输出结构化 diagnostics，并拒绝 silent no-op。
  - 测试命名：`Angelscript.TestModule.HotReload.ModuleDeactivationWaitsForInFlightExecutionAndRejectsStaleCalls`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.4-T** 📦 Git 提交：`[HotReloadLifecycle] Test: cover deactivation barrier and stale function diagnostics`

### Phase 4：模块拓扑与工具链产物合同

- [ ] **P4.1** 建立 `ModuleLayerContract + TopologyValidator + GeneratedShardGuardrail`
  - 当前 checked-in owner 只有 `AngelscriptRuntime / AngelscriptEditor / AngelscriptTest` 三个，但 public fan-out、generated shard、UHT tool 和 runtime module loader 仍然各自维护一套“我以为模块边界是什么”的规则：Build.cs 公开依赖很宽，generated shard 被当成 public module 写出，UHT 通过解析 `AngelscriptRuntime.Build.cs` 文本推导支持模块，runtime 再从 `BindModules.Cache` 的字符串列表直接 `LoadModule(...)`。这意味着“模块分层规则”仍主要存在于人工评审文档里，而不是可执行 guardrail。
  - 本条不直接做大规模模块拆分，也不重复 `Plan_UnrealCSharpArchitectureAbsorption.md` 的工具链外拆主线；它先补一份 machine-readable `ModuleLayerContract`，显式声明 `RuntimeCore / EditorShell / TestHarness / UHTTool / GeneratedRuntimeShard / GeneratedEditorShard` 六类模块的允许依赖、允许暴露的 public surface、target 约束与加载规则，然后让 shard generator、UHT tool、runtime bind loader 与 review/test 都消费同一份 contract。第一阶段先 report-only 校验，第二阶段再把 runtime->editor 漂移、非 editor target 混入 `ASEditorBind_*`、以及 generated shard 继续暴露 public ABI 变成 hard failure。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 C-13` — non-editor 默认 threaded 初始化会在 worker thread 上直接加载 bind modules，暴露出“模块名可加载”与“在哪个 layer/线程/target 上允许加载”仍未建模。
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-57` — `AngelscriptRuntime` / `AngelscriptEditor` 的 public fan-out 仍过宽。
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-58` — 当前分层规则仍停留在人工评审，没有可执行 guardrail。
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-59` — generated bind shard 被建模成公共库模块，但模板并没有真正产出可复用 ABI。
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-60` — `AngelscriptEditor.Build.cs` 的真实依赖拓扑仍部分建立在传递依赖泄漏上。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D1]` — 当前 Angelscript 把更多能力收进 `AngelscriptRuntime`，而 UnrealCSharp / puerts 会把 contract 前置到 build/codegen 阶段。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `[维度 D1] Build 图谱传递` — UnrealCSharp 会先把模块图谱序列化成 manifest，再给 generator/editor 复用，而不是按需解析 `Build.cs` 文本。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — 当前 `AngelscriptFunctionTableCodeGenerator` 仍靠 `Build.cs` 文本解析推导支持模块，说明模块图谱还没有稳定 handoff。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` `L15-L22, L30-L79` — runtime 公开模块根目录、`Core`、third-party include 与 editor 依赖，public surface 明显过宽。
    - `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs` `L12-L40` 与 `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` `L13-L49` — editor/test 仍继续暴露较宽 public include/dependency 面，缺少稳定 layer 分类。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L1187-L1206, L1214-L1276, L1300-L1325` — shard generator 会写出 `Public/<ModuleName>Module.h` 与 `PublicDependencyModuleNames`，但 public 头本体只有 `FDefaultModuleImpl` 壳类，说明 generated shard 被错误建模成公共 ABI。
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` `L334-L409` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1473-L1488` — UHT 仍靠 Build.cs 文本解析模块集合，runtime 仍靠 `BindModules.Cache` 的字符串名单直接加载模块，没有统一 topology contract。
  - 涉及文件：
    - `Plugins/Angelscript/Angelscript.uplugin`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Tools/ArchitectureReview/RunArchitectureReview.ps1`
    - 新增 `Plugins/Angelscript/Resources/Toolchain/AngelscriptModuleTopology.json`
    - 新增 `Tools/ArchitectureReview/ValidateAngelscriptModuleTopology.ps1`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleTopologyTests.cpp`
- [ ] **P4.1** 📦 Git 提交：`[ModuleTopology] Refactor: add executable layer contract and generated shard guardrail`
- [ ] **P4.1-T** 单元测试：验证模块层级 contract、generated shard target 约束与 runtime bind-load guard
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleTopologyTests.cpp`
  - 测试场景：
    - 正常路径：checked-in `Runtime / Editor / Test / UHTTool` 与 generated runtime/editor shard 都能通过 `ModuleLayerContract` 校验，runtime 仍能按 contract 加载允许的 bind modules。
    - 边界条件：同一批 bind shard 在 editor/game target 下分别应用不同 allowlist；generated shard 仍可注册绑定，但不再暴露伪 public ABI。
    - 错误路径：出现 `Runtime -> Editor` 反向依赖、未声明 public dependency、或非 editor target 混入 `ASEditorBind_*` 时，validator 与 runtime load guard 都输出显式失败而不是静默沿用字符串名单。
  - 测试命名：`Angelscript.TestModule.Architecture.ModuleTopologyRejectsUndeclaredShardEdgesAndWrongTargets`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.1-T** 📦 Git 提交：`[ModuleTopology] Test: validate topology contract and shard boundaries`

- [ ] **P4.2** 建立 `ArtifactReceipt + AuthoritativeSymbolGraph + RevisionHandshake`
  - 这条不是重复 `P2.2` 的 runtime symbol identity。`P2.2` 解决“运行时怎样 canonicalize type/symbol/source metadata”；本条解决“这些 canonical 事实怎样被 UHT / Docs / DebugDatabase / StaticJIT / PrecompiledData 共同发布与复用”。当前仓库已经有 `AS_FunctionTable_*`、`Docs/angelscript/generated/*.hpp`、`DebugDatabase`、`PrecompiledScript.Cache` 与 `AngelscriptJitInfo.jit.cpp`，但它们仍是五套分散 sidecar：producer 不共享 revision，consumer 也没有统一 receipt/symbol graph，测试只能硬编码目录名和 shard 名去猜产物。
  - 目标状态是新增 report-first 的 `ArtifactReceipt` 与 `AuthoritativeSymbolGraph`：UHT sidecar、docs dump、debug database、JIT/precompiled data 都挂同一 `artifactRevision` / `symbolRevision` / `SymbolId`，`RequestDebugDatabase` 前先做轻量 revision/capability 握手，source navigation / debug / docs 统一消费同一份 source-path/line contract。第一阶段先让 receipt 报告事实和 mismatch；第二阶段再把硬编码路径、硬编码 shard 文件名与“live engine 才能看见 symbol”的假设移除。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 73` — `UASFunction::GetSourceFilePath()` 无视真实 section，multi-section 模块的 source navigation 会固定指向 `Code[0]`。
    - [B] `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` `Issue-48` — 当前 source-path getter 已持有足够 section metadata，却完全没有接入它。
    - [B] `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` `Issue-49` — `GeneratedSourceLineNumber` 已被缓存，但 `GetSourceLineNumber()` 在 `ScriptFunction` 失效后仍退化成 `-1`。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-68` — `GeneratedFunctionTable` 测试把 UHT 目录硬编码成 `Win64/UnrealEditor`。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-58` — `EditorOutputsUseWithEditorGuard` 把具体 shard 文件名 `_000.cpp` 写死。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT4` — IDE 语义仍依赖 `WITH_EDITOR` 进程内 cache，缺少持久化 symbol contract。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT47` — `UHT` / `Docs` / `DebugDatabase` / `StaticJIT` 仍在各自重建同一批符号事实。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT53` — UHT 已有 dependency-aware 增量合同，但 Docs/StaticJIT/PrecompiledData 仍是进程副作用 producer。
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-05` — UHT 函数表与运行时 `FAngelscriptTypeUsage` 仍是双轨真相。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D6]` — 当前问题不是没有 IDE/toolchain 产物，而是它们还没有收束成单一 contract。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — 已明确建议新增 `AngelscriptArtifactManifest.json`、`symbolRevision` / `artifactRevision` 握手，并让 `DebugDatabase` 改用 `SymbolId` / `ArtifactManifest` 做 join key。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` `L174-L241, L334-L409` — generator 直接写 `Summary.json` / `ModuleSummary.csv` / `Entries.csv`，并继续靠 Build.cs 文本解析支持模块，没有统一 receipt。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` `L407-L430, L675-L755` — `DumpDocumentation()` 直接遍历 live script engine，再把结果落成 `Docs/angelscript/generated/*.hpp` 文本快照。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` `L1497-L1515` — `SendDebugDatabase()` 每次请求都从 live `FAngelscriptEngine::Get().Engine` 重新拼装数据库，没有持久化 `symbolRevision` / `artifactRevision`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L522-L528, L1433-L1447, L1575-L1608, L2224-L2227` — `dump-as-doc` / `as-generate-precompiled-data` 仍是进程启动参数驱动的副作用 producer，产物写完就 `RequestExit()`，没有共享 build receipt。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L1535-L1558` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L3416-L3417` — source file/path 与 source line 仍分裂在 live `ScriptFunction` 和 `GeneratedSourceLineNumber` 两套数据上。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp` `L25-L30`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` `L2620-L2644`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1550-L1555` — JIT 侧目前只靠全局 `PrecompiledDataGuid` / build identifier 对账，仍不是完整 artifact revision contract。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - 新增 `Plugins/Angelscript/Resources/Toolchain/AngelscriptArtifactReceipt.json`
    - 新增 `Plugins/Angelscript/Resources/Toolchain/AngelscriptSymbolGraph.json`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptToolchainReceiptTests.cpp`
- [ ] **P4.2** 📦 Git 提交：`[ToolchainReceipt] Refactor: publish shared artifact receipt and symbol graph`
- [ ] **P4.2-T** 单元测试：验证 receipt-based 输出发现、revision parity 与 DebugDatabase 握手
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptToolchainReceiptTests.cpp`
  - 测试场景：
    - 正常路径：同一轮 toolchain run 会产出统一 `artifactRevision` / `symbolRevision`，并让 `AS_FunctionTable_*`、docs dump、debug database、JIT/precompiled data 共享同一份 receipt/graph。
    - 边界条件：host 平台、target 名称或 shard 分片数量变化时，测试仍通过 receipt 定位产物，不再硬编码 `Win64/UnrealEditor` 或 `_000.cpp`。
    - 错误路径：docs/debug/JIT 任一产物 revision 与 receipt 不一致、或 `RequestDebugDatabase` 前缺少 revision/capability 握手时，系统返回显式 mismatch diagnostics，而不是继续按 live cache 拼临时真相。
  - 测试命名：`Angelscript.TestModule.Toolchain.ArtifactReceiptKeepsUhtDocsDebugAndJitInSync`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.2-T** 📦 Git 提交：`[ToolchainReceipt] Test: cover manifest-based output discovery and revision parity`

## 本轮新增条目的衔接与边界

1. `P2.4` 依赖 `P2.1` / `P2.3` 已建立的 session 与 bridge，但它只补“旧模块如何退场、何时允许 discard”的中段合同，不重复 native reload observer、外部状态 adapter 或 watcher 主线。
2. `P4.1` 的目标是把模块层级规则从文档约定升级为可执行 guardrail，并不在本轮直接推进大规模模块拆分；与 `Plan_UnrealCSharpArchitectureAbsorption.md` 的关系是“先收 contract，再决定是否外拆模块”。
3. `P4.2` 明确消费 `P2.2` 的 symbol identity / source metadata 成果，但它解决的是“如何把这份真相发布成持久化 receipt 与 symbol graph”，不是再次设计一套 runtime identity。
4. 本轮刻意避开两类重复范围：`Plan_DebugAdapter.md` 的前端/协议适配主线，以及 `Plan_PluginEngineeringHardening.md` 的通用构建硬化；这里补的是 architecture-level owner contract、topology guardrail 与 artifact authority。

---

## 本轮深化追加（2026-04-09 架构补充五项）

### Phase 1 补充：binding policy 与 trait authority

- [ ] **P1.7** 建立 `BindingPolicyCatalog + EligibilityReasonLedger`
  - 当前 hand-written bind、reflective bind 与 UHT lane 仍把“这个 surface 为什么暴露、带哪些 trait、为什么被跳过”拆成多条隐式支线：一条靠 `SetPreviousBind*()` 追写上一个函数，一条靠 `#if WITH_EDITOR` 和字符串声明补 `no_discard`，另一条再由 UHT/runtime 各自猜是否应生成。结果就是 `GetCurrentWorld()`、`System::LineTrace*`、editor-only helper、subsystem `Get()` 这类 API 反复出现 `no_discard` / `RequiresWorldContext` / `EditorOnly` 漏标。
  - 本条的目标不是一次性重写全部 `Bind_*.cpp`，而是先把 bind metadata 与准入原因提升成显式 catalog：手写 bind 在注册时直接提交 `FAngelscriptBindingPolicy`；reflective/UHT lane 统一产出 `EligibilityReason` 与 trait 集合；旧 `SetPreviousBind*()` 先降级为写入同一 catalog 的兼容层，并在 editor/test 下记录双写或漏写诊断。
  - 第一阶段优先覆盖反复被五维分析交叉命中的高风险 family：`GetCurrentWorld()` / `__WorldContext()`、`Bind_WorldCollision.cpp` 整组 world helper、`Bind_FunctionLibraryMixins.cpp` / `Bind_AssetRegistry.cpp` / `Bind_Subsystems.cpp` 的 editor-only surface，以及常见 `no_discard` value-factory。这样可以在不改引擎、不推翻既有 bind entrypoint 的前提下，把 trait authority 收回到单一入口。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` `发现 25 / 45 / 50` — “`GetCurrentWorld()`、collision `System::*`、subsystem `Class::Get()` 的 `no_discard` / `RequiresWorldContext` 合同持续回退，暴露出 trait 依赖人工追写。”
    - [B] `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` `Issue-52 / Issue-65` — “hand-written editor-only bind 与 `GetCurrentWorld()` 需要逐点补 trait，说明当前没有统一 policy 入口。”
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — “现有 bind 测试主要建议直接断言 previous-bind helper 写入结果，但对真实脚本可见合同与 skipped reason 缺少结构化守护。”
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-10 / Arch-BP-11` — “绑定准入规则分散在 editor/UHT/runtime，多条 lane 也没有共享 eligibility reason ledger。”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `D6` — “参考插件至少维护 exported registry 或 reasonful manifest；当前 Angelscript 仍缺统一的 eligibility / unsupported 账本。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L318-L389 — `DeprecatePreviousBind()`、`SetPreviousBindIsEditorOnly()`、`SetPreviousBindRequiresWorldContext()`、`SetPreviousBindNoDiscard()` 仍以“修改上一个 bind”的 process API 形态存在。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L33-L42 — `__WorldContext()` 与 `GetCurrentWorld()` 仍直接注册为普通全局函数，注册点旁没有统一 `RequiresWorldContext` / `no_discard` policy。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp` L7-L12 — `GetShouldBeVisibleInEditor()` 只包在 `#if WITH_EDITOR`，没有同一注册点上的 editor-only policy 封装。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` L43-L65 — `UEditorSubsystem` 与 `UEngineSubsystem` 的 `ClassName::Get()` 仍各自裸注册，没有显式 `EditorOnly` / `no_discard` 统一入口。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L146-L189 — `System::LineTrace*` 族函数持续直接调用 `WorldCollision::GetWorld()`，文件内看不到对应的 `RequiresWorldContext` policy 写入。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AssetRegistry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingPolicy.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingPolicy.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPolicyTests.cpp`
- [ ] **P1.7** 📦 Git 提交：`[BindingPolicy] Refactor: add binding policy catalog and eligibility reason ledger`
- [ ] **P1.7-T** 单元测试：验证 hand-written bind trait、reflective 准入结果与 skipped reason 进入同一 policy 账本
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPolicyTests.cpp`
  - 测试场景：
    - 正常路径：`GetCurrentWorld()`、`UEditorSubsystem::Get()`、代表性 trace helper 在注册后都能从同一 catalog 读回 `no_discard`、`EditorOnly`、`RequiresWorldContext` 与 `EligibilityReason`。
    - 边界条件：旧 `SetPreviousBind*()` 与新 policy API 混用时，最终 trait 只写一次且 ledger 仍保持单一 reason 来源，不出现双写漂移。
    - 错误路径：缺失 `EditorOnly` / `RequiresWorldContext` / `no_discard` 的高风险绑定在校验阶段输出显式 diagnostics，并拒绝静默进入正式 bind surface。
  - 测试命名：`Angelscript.TestModule.Core.BindingPolicy.KeepsTraitsAndEligibilityReasonsInSync`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.7-T** 📦 Git 提交：`[BindingPolicy] Test: cover handwritten bind traits and eligibility reasons`

### Phase 2 补充：module classification 与 test discovery ownership

- [ ] **P2.7** 建立 `ModuleClassificationService + EditorOnlyCapabilityCache`
  - 现在“模块是否 editor-only / developer-only”至少有三套事实源：编译阶段 `isEditorOnlyModule` 用 `StartsWith("Editor.") || Contains(".Editor.")`，`UASClass::IsDeveloperOnly()` 只认 `Dev.` 与 `Editor.` 前缀，reflective/UHT lane 又各自维护一套 editor/runtime 模块集合。`Game.Tools.Editor.Visualizers` 这类嵌套 `.Editor.` 模块于是会在 compiler、class generator、summary/artifact 上给出彼此矛盾的结论。
  - 本条的目标是把 module classification 提升成 engine/toolchain 共用服务：在 `FAngelscriptModuleDesc` 与 toolchain receipt 上一次性计算 `bIsEditorOnlyModule`、`bIsDeveloperOnlyModule`、`ClassificationReason`，随后 compiler、`UASClass`、`FinalizeActorClass()`、reflective bind 与 UHT summary 只读取这个缓存，不再各自做字符串猜测。
  - 第一阶段先统一 runtime/class generator；第二阶段再把结果喂给 UHT summary、editor-only shard、generated function table 与 docs/symbol graph。这样既不会抢跑更大的模块拆分，也能直接补齐当前最明显的命名相关假失败与报表身份漂移。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 71 / 72` — “`IsDeveloperOnly()` 与编译阶段 editor-only 判定不一致，而且现有自动化没有覆盖嵌套 `*.Editor.*` 命名。”
    - [B] `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` `Issue-21` — “需要提取单一 module classification helper，并把 editor-only 结论缓存到模块描述上。”
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-61` — “generated function table 的 `SummaryOutput` 没有验证 `moduleName` / `editorOnly` / `shardCount` 这些模块身份字段。”
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-10` — “editor/UHT/runtime 多条 lane 没有共享 eligibility policy 与模块分类事实源。”
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `[维度 D1] Build 图谱传递` — “Build graph 先落 manifest，再让 editor 与 generator 共享同一份模块图谱。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4353-L4357 — compiler 侧 `isEditorOnlyModule` 已经接受 `StartsWith("Editor.") || Contains(".Editor.")`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1523-L1532 — `UASClass::IsDeveloperOnly()` 仍只认 `Dev.` 与 `Editor.` 前缀。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L962-L980 — reflective bind lane 仍在 `ShouldBindEngineType()` 内自行做 editor/runtime 过滤。
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` L334-L384 — UHT lane 仍通过独立的 `allModules/editorOnlyModules` 集合维护模块分类。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleClassification.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleClassification.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleClassificationTests.cpp`
- [ ] **P2.7** 📦 Git 提交：`[ModuleClassification] Refactor: add shared editor-only classification service`
- [ ] **P2.7-T** 单元测试：验证 compiler、class generator 与 toolchain 对模块身份的结论保持一致
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleClassificationTests.cpp`
  - 测试场景：
    - 正常路径：`Editor.Foo`、`Game.Tools.Editor.Visualizers`、`Dev.Foo` 与普通 runtime 模块在 compiler、`UASClass::IsDeveloperOnly()`、summary `editorOnly` 字段上得到一致分类。
    - 边界条件：manifest 缺失而回退 legacy parser 时，classification 结果仍与共享 helper 一致，并在 summary 中明确标记 discovery source。
    - 错误路径：若 runtime/class generator/UHT 任一 lane 看到的分类与缓存不一致，系统输出显式 mismatch diagnostics，而不是继续生成互相矛盾的 `UASClass` 或 UHT 报表。
  - 测试命名：`Angelscript.TestModule.Core.ModuleClassification.KeepsCompilerClassGeneratorAndToolchainInSync`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.7-T** 📦 Git 提交：`[ModuleClassification] Test: cover nested editor module classification parity`

- [ ] **P2.8** 建立 `TestDiscoveryService + ReadinessMilestoneReport`
  - test discovery、coverage hook 与 hot reload test prepare 目前仍挂在 runtime compile/init 主链里：`OnPostEngineInit` 里接 coverage hook 与 `DiscoverTests()`，compile 成功后立即增量 discovery，复杂测试 discovery 还会在“发现阶段”真实执行 `*_GetTests` 脚本函数。late init 或 `AssetManager == nullptr` fallback 时，这条链又会卡在 `bCompletedAssetScan` 上，把 runtime 留在“初始化完成但 tests-ready 永远不完整”的半状态。
  - 本条的目标是把 test discovery 提升成独立 observer/service：runtime core 只发布 `ScriptsReady`、`AssetScanObserved`、`ModuleCommitted`、`InteractiveRuntimeReady` 等里程碑与 `ModuleCommitReport`；`TestDiscoveryService` 再决定何时做 discovery、何时准备 hot reload tests、何时把 discovery failure 记录到独立 report，而不是继续污染 core compile diagnostics。
  - 第一阶段保持今天 editor 下“自动发现测试”的体验不变，但把 `bCompletedAssetScan` 拆成事实状态与功能就绪状态两个概念；第二阶段再允许 analyze-only、toolchain-only、headless validate profile 显式禁用 discovery 执行脚本这一副作用。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 C-09 / C-11` — “late init 会错过 `OnPostEngineInit`，`AssetManager` fallback 又不会推进 tests-ready 所需状态。”
    - [B] `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md` `Issue-28 / Issue-53` — “需要把 coverage/test discovery 接线改成立即执行或延后执行统一 helper，并拆开 asset-scan 与 discovery-readiness。”
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-55` — “test discovery 仍嵌在 compile/commit 主链里，而且 discovery 阶段会执行脚本。”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “参考插件更倾向让 editor toolbar / commandlet 共享 generator 或 observer，而不是把工具侧 discovery 直接塞进 runtime compile 事务。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1628-L1633 — coverage hook 仍只在 `OnPostEngineInit` 回调里调用 `AddTestFrameworkHooks()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2201-L2218 — `DiscoverTests()` 与 `bCompletedAssetScan` 仍绑在 `OnPostEngineInit -> AssetManager` 回调链上。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2232-L2248 — `DiscoverTests()` 直接遍历 `GetActiveModules()` 做 unit/integration discovery。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2481-L2489 — hot reload test prepare 仍把 `bCompletedAssetScan` 当作硬门槛。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4143-L4153 — compile 成功后仍在 core 内立刻对 `CompiledModules` 做增量 discovery。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp` L125-L145、L172-L209 — complex test discovery 会真实执行 `*_GetTests`，unit/integration discovery 本身不是纯元数据扫描。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp` L22-L29 — coverage recorder 直接挂到 automation controller 生命周期。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestDiscoveryService.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestDiscoveryService.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestDiscoveryServiceTests.cpp`
- [ ] **P2.8** 📦 Git 提交：`[TestDiscovery] Refactor: extract discovery service and readiness milestone report`
- [ ] **P2.8-T** 单元测试：验证 test discovery 不再绑定 compile 主链与单一 asset-scan 布尔值
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestDiscoveryServiceTests.cpp`
  - 测试场景：
    - 正常路径：module commit 之后由 `TestDiscoveryService` 统一完成 unit/integration discovery 与 hot reload test prepare，editor 默认 observer 体验保持不变。
    - 边界条件：late init 发生在 `OnPostEngineInit` 之后，或 `AssetManager == nullptr` fallback 时，系统仍会推进 `TestsReady` 而不会伪装成“真实 asset scan 已发生”。
    - 错误路径：`*_GetTests` 执行失败、环境禁止执行 discovery script、或 readiness 先决条件未满足时，失败进入独立 discovery report，而不是污染 core compile diagnostics 或永久卡死后续 `PrepareTests()`。
  - 测试命名：`Angelscript.TestModule.Core.TestDiscoveryService.DecouplesReadinessAndModuleCommitFromCompile`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.8-T** 📦 Git 提交：`[TestDiscovery] Test: cover late-init readiness and discovery observer separation`

### Phase 4 补充：debug/toolchain packaging 与 host contract

- [ ] **P4.3** 建立 `DebugRuntimeFacade + OptionalDebugToolchainModules`
  - 当前 `AngelscriptRuntime` 仍同时承担 runtime kernel、socket transport、debug wire schema、StaticJIT/export job、editor asset create/list-assets bridge 等多种责任。结果是一份 Runtime 模块既公开 socket/public protocol header，又在初始化时直接创建 `DebugServer` 与 `StaticJIT`，导出模式还在 runtime 启动尾部直接写产物并 `RequestExitWithStatus()`。
  - 本条的目标是先做 packaging/ownership 拆层，而不是重做调试协议：runtime 只保留 `IAngelscriptDebugRuntime` / `IAngelscriptToolchainBridge` 之类的 façade；socket、wire schema、workspace write-back、StaticJIT/toolchain batch bridge 移到可选模块或 program host。第一阶段保留旧协议与旧 delegate，通过 forwarding 层兼容 `Plan_DebugAdapter.md` 已有主线，不额外改 UI/client。
  - 这样既能减小 `AngelscriptRuntime` 的公开依赖面，也能让 runtime-only、headless validate、shipping profile 明确不加载这些可选能力；同时 toolchain/debug lane 终于能拥有独立发版与测试边界，而不是继续绑在 Runtime 发版节奏上。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — “`DebugServer`、line callback 与 current-engine 路由仍深度绑在 runtime wrapper 生命周期上，说明 debug owner 尚未从 runtime 核心分离。”
    - [B] `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md` `Issue-47` — “export-only `bForcedExit` 路径请求退出后仍继续安装 hot reload/debug/global callback，暴露 batch job 混入 interactive runtime init。”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT36 / Arch-DT50 / Arch-DT51` — “runtime/debug/editor/toolchain 发布边界尚未拆清，`AngelscriptDebugServer.h` 也把 transport 与语义一起暴露成 public surface。”
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `[维度 D1]` — “Runtime / Editor / Generator / Compiler / Program 被明确拆成一等模块。”
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` `插件架构总览` — “`UnLuaEditor` 承担 Hot Reload / IntelliSense 等工具面，runtime 维持较薄职责边界。”
  - 源码验证：
    - `Plugins/Angelscript/Angelscript.uplugin` L18-L33 — 插件清单当前仍只有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个正式模块。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` L45-L79 — runtime 直接依赖 `Networking`、`Sockets`、`AssetRegistry`、`Projects`，editor target 下还额外拉入 `UnrealEd`、`EditorSubsystem`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` L40-L47 — runtime public surface 仍直接暴露 `GetDebugListAssets()`、`GetEditorCreateBlueprint()` 等 debug/editor hook。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L9-L34、L1438-L1455 — runtime kernel 直接 include `AngelscriptDebugServer.h` / `StaticJIT` 头，并在初始化阶段创建 `StaticJIT` 与 `DebugServer`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1575-L1608 — runtime 初始化尾部直接承担导出产物与 `RequestExitWithStatus()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L1-L23 — public debug header 直接暴露 `Sockets.h` 与 transport/public schema。
  - 涉及文件：
    - `Plugins/Angelscript/Angelscript.uplugin`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptDebugRuntime/`
    - 新增 `Plugins/Angelscript/Source/AngelscriptToolchainBridge/`
    - `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugPackagingTests.cpp`
- [ ] **P4.3** 📦 Git 提交：`[DebugPackaging] Refactor: split optional debug and toolchain modules from runtime`
- [ ] **P4.3-T** 单元测试：验证 runtime-only profile 不再被 debug/toolchain 公开依赖面拖重，同时 legacy bridge 仍兼容
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugPackagingTests.cpp`
  - 测试场景：
    - 正常路径：runtime-only profile 启动时不加载 socket/debug/toolchain bridge；启用可选 debug module 后，既有 pause/list-assets/create-blueprint 流程仍通过 façade 正常工作。
    - 边界条件：editor profile 与 headless validate profile 分别只加载各自需要的 bridge，legacy `GetDebugListAssets()` / `GetEditorCreateBlueprint()` forwarding 只执行一次且不改变现有协议。
    - 错误路径：缺失 debug/toolchain module、版本不匹配或在 shipping/runtime-only 下请求该能力时，系统返回显式 capability-unavailable diagnostics，而不是留下半初始化 `DebugServer` 指针或继续注册全局回调。
  - 测试命名：`Angelscript.TestModule.Debugger.DebugPackaging.LoadsOptionalBridgeWithoutInflatingRuntime`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.3-T** 📦 Git 提交：`[DebugPackaging] Test: cover optional bridge loading and runtime-only profile`

- [ ] **P4.4** 建立 `ToolchainProgramHost + ToolchainModuleManifest`
  - 当前 toolchain 仍有两类结构性问题叠在一起：一类是 UHT 通过 `LoadSupportedModules()` 直接解析 `AngelscriptRuntime.Build.cs` 文本推断哪些模块参与 `AS_FunctionTable_*`；另一类是 docs/JIT/precompiled-data 导出仍通过 runtime 初始化分支顺手完成，再靠 `RequestExitWithStatus()` 结束进程。于是 tests 不得不把生成目录写死成 `Win64/UnrealEditor`、把 shard 写死成 `_000.cpp`，summary 又缺少 authoritative module/editor-only/receipt 合同。
  - 本条的目标是补一层 first-class tool host：新增 `AngelscriptToolchainProgram` 或等价 commandlet/program module，统一承载 `emit-function-table`、`emit-docs`、`emit-precompiled-data`、`emit-jit`、`emit-symbol-graph`；同时引入 `ToolchainModuleManifest + BuildReceipt`，由 UHT/docs/JIT/precompiled 四类 producer 共同写入 receipt，再让 runtime/debugger/CI 只消费 receipt，而不是继续猜路径、猜平台、猜 shard。
  - 第一阶段保持现有 `-dump-as-doc`、`-as-generate-precompiled-data`、`AS_FunctionTable_*` 输出路径与 UHT exporter 不变，只新增 manifest/receipt 与 program host；`Build.cs` 文本解析保留为 fallback，并在 summary/日志中明确标识 `moduleDiscoverySource=buildcs-fallback`。这样既能兼容当前工程，又能为后续 authoritative symbol graph、toolchain coordinator 与 delivery baseline 提供稳定宿主。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md` `Issue-47` — “export-only job 仍嵌在 `Initialize_AnyThread()` 尾部，通过 `RequestExitWithStatus()` 假装成 tool host。”
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-58 / Issue-61 / Issue-68` — “generated function table 测试对 shard 名、平台目录与 summary 身份字段仍是脆弱硬编码。”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT36 / Arch-DT38 / Arch-DT47 / Arch-DT53` — “需要 first-class tool host、显式 module manifest、authoritative symbol graph 与统一 build receipt。”
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `[维度 D1] Build 图谱传递` — “模块图谱先序列化成 manifest，再由 editor/generator/program 共享。”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “参考链路更适合单目标 commandlet 与 `Intermediate` artifact，而不是把 docs/summary/manifest 继续塞进 runtime 启动侧效应。”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` L51-L67 — generator 入口启动时先调用 `LoadSupportedModules(factory)` 决定模块集。
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` L334-L409 — `LoadSupportedModules()` 仍通过 `ResolveRuntimeBuildCsPath()` 找到 `AngelscriptRuntime.Build.cs`，逐行解析 `DependencyModuleNames.AddRange`，找不到时直接抛异常。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1573-L1608 — StaticJIT / precompiled-data 输出仍在 runtime 初始化尾部直接写产物并退出。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2223-L2227 — docs dump 仍由 runtime `InitialCompile()` 成功后直接触发并退出进程。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptToolchainProgram/`
    - 新增 `Plugins/Angelscript/Config/AngelscriptToolchainModules.json`
    - 新增 `Plugins/Angelscript/Resources/Toolchain/AngelscriptBuildReceipt.json`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptToolchainHostTests.cpp`
- [ ] **P4.4** 📦 Git 提交：`[ToolchainHost] Refactor: add program host, module manifest, and unified build receipt`
- [ ] **P4.4-T** 单元测试：验证 UHT/docs/JIT/precompiled producer 通过同一 manifest 与 receipt 汇合
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptToolchainHostTests.cpp`
  - 测试场景：
    - 正常路径：program/commandlet host 运行后，receipt 同时记录 function table、docs、JIT、precompiled-data 节点，UHT generator 优先消费 `ToolchainModuleManifest` 而不是重新解析 `Build.cs`。
    - 边界条件：host 平台、target 名称或 shard 分片变化时，测试仍通过 receipt/manifest 定位产物，不再硬编码 `Win64/UnrealEditor` 或 `_000.cpp`。
    - 错误路径：manifest 缺失、receipt revision 不一致或某 producer 节点未完成时，summary/日志输出显式 failure source 或 `buildcs-fallback` 标识，而不是继续静默把旧产物当成当前事实。
  - 测试命名：`Angelscript.TestModule.Core.ToolchainHost.UsesManifestAndReceiptAcrossUhtDocsAndJit`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.4-T** 📦 Git 提交：`[ToolchainHost] Test: cover manifest-driven discovery and unified build receipt`

## 本次追加条目的衔接与边界

1. `P1.7` 不重复 `P1.2` 的 provider manifest 主线；它补的是“provider 已被发现之后，trait/eligibility/reason 由谁定义、谁负责审计”的 policy authority。
2. `P2.7` 只统一 module classification 与 editor-only capability cache，不在本轮直接推进更大的模块拆分；toolchain 侧的正式 manifest 发布由 `P4.4` 继续承接。
3. `P2.8` 只把 test discovery 从 compile/init 主链中抽成 observer/service，不改 `AngelscriptTestCommandlet` 的退出码合同，也不重写现有 test runner/operator 层。
4. `P4.3` 明确避开 `Plan_DebugAdapter.md` 的协议消息、前端 UI 与客户端兼容工作；这里只收口 packaging 边界、public surface 与可选模块装配。
5. `P4.4` 不替换现有 UHT/docs/JIT/precompiled producer，只新增 first-class host、manifest 与 receipt；旧命令行入口与旧路径先保留为兼容 alias。
6. 五项都按当前仓库约束设计成可增量推进的小阶段：不要求修改引擎，不提前挤占交付基线与 blocker 修复，也不把 AngelScript `2.38` 选择性吸收主线硬塞到本轮架构计划里。

## 本次追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.7` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPolicyTests.cpp` | hand-written bind trait、eligibility reason、旧新 API 混用对账 | 高 |
| `P2.7` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleClassificationTests.cpp` | nested `.Editor.` 命名、compiler/class generator/UHT 分类一致性 | 高 |
| `P2.8` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestDiscoveryServiceTests.cpp` | late-init、asset fallback、discovery observer 与 compile 主链解耦 | 高 |
| `P4.3` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugPackagingTests.cpp` | optional debug bridge、runtime-only profile、legacy forwarding | 中高 |
| `P4.4` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptToolchainHostTests.cpp` | manifest-driven module discovery、unified receipt、非硬编码产物定位 | 高 |

---

## 本轮深化追加（2026-04-09 类型支持、绑定阶段与写盘策略补充）

### Phase 1 补充：type surface 与 binding authority

- [ ] **P1.8** 建立 `SurfaceSupportProfile + CallableExposurePolicy`
  - 当前 `TypeSystem` 只提供 `CanCreateProperty`、`CanBeArgument`、`CanBeReturned`、`CanBeTemplateSubType` 这类粗粒度布尔位；一到真实使用面，`ClassGenerator`、runtime direct-bind、reflective fallback、`AngelscriptUHTTool` 又各自叠一层局部规则。结果是同一个 symbol 会在 runtime 里被标成 `RejectedInterfaceClass`，在 UHT 产物里只剩 `Stub`，在 skip ledger 里又可能完全没有对应 reason，调用面和报表面并不共享同一份支持面事实。
  - 本条的目标不是马上补齐所有 `UInterface/FInterfaceProperty/TScriptInterface<>` 能力，而是先把“某类型 family 在哪些 surface 可用、某 callable 在哪些 lane 可暴露、为什么被拒绝”收成单一 contract。第一阶段新增 `SurfaceSupportProfile` 与 `CallableExposurePolicy`，由 `TypeUsage` 派生出 `PropertyMaterialization / ScriptCallable / NativeDirectBind / ReflectiveFallback / UHTDirectBind / DebugDocsVisibility / TemplateEmbedding` 这些 lane 的状态与 `ReasonCode`；第二阶段再让 interface family、future wrapper 和 selective rollout 都通过同一 policy 开关推进，而不是继续在 runtime/UHT 各开一半。
  - 这样可以把 `P10` 与后续 type family 扩面从“碰到哪里补哪里”升级成“先声明支持面，再按 lane 渐进打开”。`ClassGenerator`、`Bind_BlueprintCallable`、`BlueprintCallableReflectiveFallback`、`AS_FunctionTable_*`、未来 `DebugDatabase/docs` 都只消费同一份 `SurfaceSupportProfile` / `CallableExposureDecision`，不再重复维护 `Stub/Rejected/Skipped` 三套口径。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 30 / 31 / 37 / 56` — script interface 的 `UFunction` 生成、实现校验与 dispatch 仍在 `name-only stub`、`name-only match` 和全局短名 type identity 之间分裂，说明 callable/type authority 还没收口。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-61 / Issue-73` — `GeneratedFunctionTable` 测试目前只保护 summary/csv 的算术自洽，不保护 `moduleName/editorOnly/shardCount` 与 `FailureReason` 的逐项语义，对“同一 callable 在不同 lane 的 decision parity”几乎没有守护。
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-55 / Arch-TS-56` — `TypeSystem` 缺少按使用面收敛的 support profile，runtime 与 UHT 的 callable 曝光规则已经分叉。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` `D6`、`Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `D2/D6` — 已明确指出当前 `RejectedInterfaceClass / Stub / 缺席` 是三套状态词汇，参考插件更接近“一类 property/type family 对应一份支持性 factory 或 decision ledger”。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` `L147-L250` — 类型核心仍只暴露 `CanCreateProperty`、`CanBeTemplateSubType`、`CanBeArgument`、`CanBeReturned` 等粗粒度 capability。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L575-L604` — script `UFUNCTION` 生成阶段只检查 `CanCreateProperty` / `CanBeReturned` / `CanBeArgument`，没有按 surface 维度区分支持面。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp` `L27-L31` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` `L94-L117` — runtime direct bind 仍靠 `FUNC_Native` + `ShouldSkipBlueprintCallableFunction()` 的本地规则决定是否暴露。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp` `L267-L287` — reflective fallback 又单独维护 `RejectedInterfaceClass / RejectedCustomThunk / RejectedTooManyArguments` 枚举。
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` `L56-L63` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` `L466-L514` — UHT 侧只按 `BlueprintCallable/Pure` 和局部 metadata 做判断，并对 `Interface/NativeInterface` blanket `ERASE_NO_FUNCTION()`，没有消费 runtime policy。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSurfaceSupportProfile.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSurfaceSupportProfile.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCallableExposurePolicy.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCallableExposurePolicy.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCallableExposurePolicyTests.cpp`
- [ ] **P1.8** 📦 Git 提交：`[TypeSurface] Refactor: add surface support profile and shared callable exposure policy`
- [ ] **P1.8-T** 单元测试：验证 type surface 与 callable exposure 在 runtime、reflective 与 UHT 三侧保持同源
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCallableExposurePolicyTests.cpp`
  - 测试场景：
    - 正常路径：代表性 native `BlueprintCallable`、普通 script `UFUNCTION`、以及可直接 materialize 的基础 type family，在 `ClassGenerator`、runtime direct bind、reflective fallback 与 UHT summary 上得到一致的 `SurfaceSupportProfile/ExposureMode`。
    - 边界条件：`UInterface/FInterfaceProperty/TScriptInterface<>` 一类当前只允许部分 lane 的 family，被明确标成 `PropertyOnly`、`ReflectiveOnly` 或等价受限状态，并带稳定 `ReasonCode`，而不是一侧 `Stub`、一侧静默缺席。
    - 错误路径：若 runtime 与 UHT 对同一 `SymbolKey` 给出不同 `ExposureMode/ReasonCode`，或 forced-stub symbol 缺失 decision ledger，测试直接报 parity failure。
  - 测试命名：`Angelscript.TestModule.Core.TypeSurface.KeepsRuntimeReflectiveAndUhtExposureInSync`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.8-T** 📦 Git 提交：`[TypeSurface] Test: cover surface profile and callable decision parity`

- [ ] **P1.9** 建立 `BindPhasePlan + BindingDecisionLedger`
  - 当前 binding pipeline 仍把真正的阶段依赖埋在 `BindOrder` 魔法数字和 append-only `TypeFinder` 顺序里：`Bind_UEnum` 占 `Early-1`，`Bind_BlueprintType_Declarations` 占 `Early`，`Bind_UObject_Base` 占 `Late-1`，`Bind_Defaults` 又跳到 `Late+100`，actor helper 再继续叠 `Late+150`。与此同时，editor 菜单已经明确把 `Legacy Native Bind Generator` 标成 debug-only，但 runtime 启动仍会默认读取 `BindModules.Cache` 并装载 `ASRuntimeBind_* / ASEditorBind_*`，说明“这一轮到底是哪条 bind lane 在提供 authority”仍不是显式 contract。
  - 本条的目标是把“谁在什么时候注册什么、哪条 generated lane 具有什么 authority、哪个 decision 进入 dump/summary/test”提升为可执行 plan，而不是继续靠整数插槽与隐式 auto-load。第一阶段新增 `BindPhasePlan`、`ResolvedBindStep` 与 `BindingDecisionLedger`：旧 `FBind(int32 BindOrder, ...)` 继续存在，但先映射到 `RegisterCoreTypes / RegisterTypeFinders / DeclareReflectedTypes / BindReflectedMembers / BindGlobals / PostBind` 等显式 phase；`TypeFinder` 也增加 `Priority/ProviderName`；runtime 在 `CallBinds()` 前先构造 resolved plan，再把 legacy shard、hand-written bind、future UHT-generated bind 统一标成 `Manual / GeneratedModule / UHTShard / Reflective / ExternalProvider` 等 lane。
  - 第二阶段再把 legacy bind module lane 从“默认 authority”降成“显式 legacy provider”，让 `BindModules.Cache`、`GetBindInfoList()`、`BindRegistrations.csv`、generated decision ledger 和测试都说同一套话。这样 `P1.3` 的 contributor provenance、`P1.7` 的 trait policy、`P4.4` 的 toolchain host 才有共同的执行语义基础，而不是继续一个负责“谁注册了”、一个负责“注册了什么 trait”、另一个负责“构建时产物从哪来”，却没人负责“绑定计划本身如何落地”。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` `发现 04` — 当前 bind core 的 trait 写入链路仍依赖 `SetPreviousBind*()` 这类 process API，暴露出 binding metadata 与执行计划仍不是显式对象。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-03 / Issue-51` — 现有 bind 测试只校验 `BindOrder` 单调、registry 数量与禁用执行，不保护具体 bind family、provider lane 与 query-view 语义，说明 binding plan 还不可测。
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-21 / Arch-BP-22` — 绑定阶段依赖被 `BindOrder` 与 `TypeFinder` 顺序隐式编码，legacy bind module lane 与 UHT 主线并存但 authority 不清晰。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` `D1/D6`、`Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `D1/D2` — 已明确建议让 runtime 在 `CallBinds()` 前先构造 resolved provider/bind plan，并让 exporter/generator/runtime 共享单一 decision owner，而不是继续 `BindModules.Cache + static FBind + UHT summary` 三套并行 authority。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` `L424-L476` — 公开阶段仍只有 `Early / Normal / Late`，`FBindInfo` 也只有 `BindName / BindOrder / bEnabled`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` `L120-L183` — registry 内部仍只保存 `BindName / BindOrder / TFunction<void()>`，`GetBindInfoList()` 无法回答 phase、lane 或 authority。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` `L114-L159` — `RegisterTypeFinder()` 只是 append 到数组，`GetByProperty()` 按先注册先命中的顺序返回，没有 phase/priority contract。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp` `L358-L402`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `L712-L725` 与 `L1317-L1380`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` `L37-L72`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` `L450-L467` — 真实阶段顺序仍由 `Early-1 / Early / Late-1 / Late+100 / Late+150` 等魔法槽位拼接。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L728-L730` 与 `L999-L1077`、`L1285-L1327` — editor UI 已声明 legacy bind generator 只是 debug-only，但它仍会生成 `ASRuntimeBind_* / ASEditorBind_*` 并写回 `BindModules.Cache`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1466-L1496` — runtime 初始化仍默认读取 `BindModules.Cache`、动态装载模块，再统一 `BindScriptTypes()`，没有显式 lane 或 phase plan。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindPhasePlan.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindPhasePlan.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingDecisionLedger.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingDecisionLedger.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPhasePlanTests.cpp`
- [ ] **P1.9** 📦 Git 提交：`[BindPhase] Refactor: add explicit bind phase plan and binding decision ledger`
- [ ] **P1.9-T** 单元测试：验证 legacy `BindOrder`、`TypeFinder` 优先级与 generated lane authority 被统一映射到同一执行计划
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPhasePlanTests.cpp`
  - 测试场景：
    - 正常路径：resolved plan 明确输出 `Enum finder -> Blueprint declarations -> UObject globals -> Blueprint reflective members -> Actor helpers` 的阶段顺序，并能标出每一步的 provider/lane。
    - 边界条件：legacy `ASRuntimeBind_* / ASEditorBind_*` 模块仍可通过兼容映射进入 `BindPhasePlan`，但在 `UHT-only`、`legacy-explicit` 与混合模式下都会被清楚标注 authority source，而不是继续匿名注册。
    - 错误路径：phase 依赖冲突、`TypeFinder` 优先级双写、或 runtime 同时消费重复 authority 时，validator 输出显式 diagnostics 并拒绝把有歧义的计划静默提交给 `CallBinds()`。
  - 测试命名：`Angelscript.TestModule.Core.BindPhase.ResolvesLegacyAndGeneratedAuthoritiesIntoSinglePlan`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.9-T** 📦 Git 提交：`[BindPhase] Test: cover phase mapping, lane authority, and decision parity`

### Phase 3 补充：editor save 与 writable policy

- [ ] **P3.4** 建立 `EditorFilePolicyService + SourceControlWriteGuard`
  - 当前 editor 侧已经有 `Create Blueprint / DataAsset` 的正式作者路径，但“保存 package”和“写文本文件”仍是两套完全独立的语义：`ShowCreateBlueprintPopup()` 最后走 `PromptForCheckoutAndSave()`，而 legacy bind/module 生成则直接 `SaveStringArrayToFile()`、`Delete()`。同时 `LevelViewport_SourceControlMenu` 只是一个菜单挂点，不代表插件真的拥有可复用的 source-control / writable policy。
  - 本条目的目标不是把 `SourceControl` 依赖强绑进 `AngelscriptEditor`，而是先把写盘 contract 收口。第一阶段新增 `EditorFilePolicyService`，统一承载 `WriteTextFile / DeleteFile / SavePackages / EnsureWritable / WriteIfChanged` 等入口；无源控环境下保持当前直接写盘行为，有源控环境则通过可选 adapter 在写前做 checkout/writable 检查。第二阶段再把 `P3.3` 的 asset workflow、legacy bind 生成、future workspace stub 生成、editor commandlet 输出都接到同一策略上，并优先吸收 `UnLua` 的 diff-save 降噪和 `puerts` 的可选 source-control bridge。
  - 这样可以让 editor 工具和未来脚本扩展真正复用“如何写包、如何写文本、如何避免无意义 churn”的统一 seam，而不是继续一处弹保存对话框、一处直接覆盖文件、另一处直接删旧产物。考虑到这项主要由 D/E 维度驱动、A/B/C 里的直接故障信号较少，本轮把它明确放在 `P3` 收尾，不抢占 runtime/hot reload/toolchain 主线。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/EditorArch_ArchReview.md` `Arch-63` — editor 写盘与保存路径缺少统一的 source-control / writable policy，`LevelViewport_SourceControlMenu` 目前只是 UI surface。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `D6/D10`、`Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — puerts 采用可选 source-control + 统一 file operation，UnLua 采用 diff-save 降低 source-control 噪音；这两种做法都优于当前分散式直接写盘。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` `L16-L24` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` `L958-L967` — `LevelViewport_SourceControlMenu` 目前只是普通 menu location + extender 注册点。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L471-L534` — asset 创建路径会弹 `CreateModalSaveAssetDialog()` 并在末尾调用 `PromptForCheckoutAndSave()`。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L1205-L1206`、`L1430-L1440`、`L1471-L1471` — legacy bind/module 生成仍直接 `SaveStringArrayToFile()` 和 `Delete()`，没有统一 file/write policy。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/IO/AngelscriptEditorFilePolicyService.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/IO/AngelscriptEditorFilePolicyService.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/IO/AngelscriptSourceControlWriteGuard.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/IO/AngelscriptSourceControlWriteGuard.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorFilePolicyTests.cpp`
- [ ] **P3.4** 📦 Git 提交：`[EditorFilePolicy] Refactor: add editor file policy service and optional source-control guard`
- [ ] **P3.4-T** 单元测试：验证 package save、text write 与 readonly/source-control 场景走同一写盘策略
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorFilePolicyTests.cpp`
  - 测试场景：
    - 正常路径：创建 Blueprint/Asset 与 legacy text 产物生成都通过 `EditorFilePolicyService` 落盘；无源控环境下行为与当前一致，且 `WriteIfChanged` 不会对未变化内容重复写盘。
    - 边界条件：source-control 模块不可用、文件已可写、或 `PromptUser / SilentCheckoutIfPossible / NoSave` 不同 policy 组合时，service 仍给出稳定且可复用的行为，不制造额外 modal 或无意义 churn。
    - 错误路径：readonly 文件无法 checkout、删除失败或 package save 被拒绝时，调用方收到显式 diagnostics，且不会留下“部分文件已写、部分文件未写”的半成功状态。
  - 测试命名：`Angelscript.TestModule.Editor.FilePolicy.UnifiesPackageSaveAndTextWrites`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P3.4-T** 📦 Git 提交：`[EditorFilePolicy] Test: cover unified save policy and optional source-control guard`

## 本次补充条目的衔接与边界

1. `P1.8` 不重复 `P2.2` 的 symbol/source canonicalization，也不重复 `P4.2` 的 artifact receipt；它补的是“这些 symbol/type 到底在哪些 surface 被支持、为何被拒绝”的统一 vocabulary。
2. `P1.9` 不重复 `P1.3` 的 contributor provenance、`P1.7` 的 bind trait policy、或 `P4.4` 的 toolchain host；它只处理 binding pipeline 的执行阶段、authority lane 与 decision ledger。
3. `P3.4` 以 `P3.3` 的 asset workflow 为前置，只收口 editor 写盘/保存策略；不重做 content browser authoring、本地模板入口或更大的 workspace bootstrap。

## 本次补充条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.8` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCallableExposurePolicyTests.cpp` | surface profile、direct/reflective/UHT callable decision parity、受限 family reason code | 高 |
| `P1.9` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPhasePlanTests.cpp` | bind phase mapping、legacy/UHT authority lane、resolved plan diagnostics | 高 |
| `P3.4` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorFilePolicyTests.cpp` | package save、text write、diff-save、readonly/source-control guard | 中高 |

---

## 深化 (2026-04-09)

### Phase 1 补充：settings profile 与 apply contract

- [ ] **P1.6** 建立 `EngineEffectiveSettingsProfile + SettingsApplyContract`
  - 当前 `UAngelscriptSettings` 在 runtime 内同时被当成“owner 初始化快照”和“运行时全局单例”两种来源使用：初始化阶段 `FAngelscriptEngine` 会把部分字段灌进 engine/static 状态，但 loop detection、primitive naming、math namespace、debugger blacklist 等路径仍会在运行期继续直接读 `UAngelscriptSettings::Get()` 或 `GetDefault<>()`。这使同一台 engine 在 owner 生命周期内混用了两套配置语义，也让 multi-owner / clone 场景无法拥有稳定的 engine-local settings 边界。
  - 本条的目标不是把所有设置都做成 live-reload，而是先把“当前 engine 实际生效的配置”显式化。第一阶段在 `FAngelscriptEngine` 内新增 `FAngelscriptEffectiveSettingsProfile`（或等价命名），统一承载 `automatic imports`、namespace policy、disabled binds、primitive naming、loop timeout、debugger blacklist 等运行期会消费的字段；`PreInitialize_GameThread()` 一次性从 `UAngelscriptSettings` 组装 profile，后续 runtime/bind/type/debugger 代码只读 engine profile，不再随手偷读全局默认对象。
  - 第二阶段补一条最小 `SettingsApplyContract`。只允许少数被明确定义为可热应用的字段通过 `ApplyUpdatedSettings(...)` 或等价 owner API 显式进入 engine；其余 `ConfigRestartRequired` 字段继续保持“新 owner / 新 engine 才生效”的合同。这样既能吸收参考插件把 settings 提升为 typed runtime contract 的经验，也能避免把当前插件拖回 process-global mutable singleton。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md` `Issue-50` — runtime settings 同时存在 snapshot 与 live-singleton 两套读取语义，owner 生命周期内会出现配置漂移。
    - [D] `Documents/AutoPlans/ArchitectureReview/ExtensionPoints_ArchReview.md` `Arch-EP23 / Arch-EP43` — 扩展/配置语义仍按 process-global settings 生效，缺少 engine-level profile 与正式 apply contract。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — `UnLua` 通过 `EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 这类 typed settings 驱动运行时行为，说明配置不应只是 editor UI 页，而应成为可消费的 runtime contract。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` `L41-L42`、`L51-L153`、`L221-L223` — `UAngelscriptSettings` 是 `Config=Engine` 的 mutable default object，且大量字段标记了 `ConfigRestartRequired`，`Get()` 仍直接返回 `GetMutableDefault<UAngelscriptSettings>()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1291-L1296` — `PreInitialize_GameThread()` 只把 `ConfigSettings` 指针挂到 engine 上，并同步部分 namespace policy 到 engine/static 状态。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L5568-L5580` — `AngelscriptLoopDetectionCallback()` 在运行期每次直接读取 `UAngelscriptSettings::Get().EditorMaximumScriptExecutionTime`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp` `L9-L13` — math namespace / double policy 直接读取 `UAngelscriptSettings::Get()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp` `L610-L639` — primitive type naming 直接依赖 `GetDefault<UAngelscriptSettings>()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` `L669-L688` — debugger blacklist 仍从 `FAngelscriptEngine::Get().ConfigSettings` 读取 live config。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp` `L262-L278` — 现有测试直接修改 `GetMutableDefault<UAngelscriptSettings>()`，证明 live singleton 仍被当作运行态输入。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEffectiveSettingsTests.cpp`
- [ ] **P1.6** 📦 Git 提交：`[SettingsProfile] Refactor: add engine effective settings profile and apply contract`
- [ ] **P1.6-T** 单元测试：验证 engine-local settings profile 与显式 apply contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEffectiveSettingsTests.cpp`
  - 测试场景：
    - 正常路径：默认 `UAngelscriptSettings` 组装出稳定的 engine-effective snapshot，`Bind_FMath`、primitive naming、loop timeout 与 debugger blacklist 都从当前 engine profile 取值。
    - 边界条件：owner A / owner B 用不同 settings snapshot 初始化后互不串线；clone 继承 profile 时保持与 owner 合同一致，不因全局默认对象变化漂移。
    - 错误路径：修改 `ConfigRestartRequired` 字段但未显式重建 engine 或未走 `ApplyUpdatedSettings(...)` 时，旧 engine 行为保持不变；对未允许热应用的字段调用 apply contract 时返回显式 diagnostics。
  - 测试命名：`Angelscript.TestModule.Core.SettingsProfile.KeepsOwnerScopedEffectiveSettingsStable`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.6-T** 📦 Git 提交：`[SettingsProfile] Test: cover owner-scoped effective settings and apply contract`

### Phase 2 补充：编译事务与模块驻留分层

- [ ] **P2.5** 建立 `CompileRequest + CompileTransaction + ModuleGraphSnapshot`
  - 当前 `InitialCompile()`、hot reload 与 future analyze/lint 场景都只能把输入临时拼成 `ModulesToCompile` 然后直接冲进 `CompileModules()`；而 `CompileModules()` 本身又在同一个函数里混合维护 `CompiledModules`、`ModulesToUpdateReferences`、`ScriptUpdateMap`、旧模块 availability 剥离、`ClassGenerator.Setup()`、`SwapInModules()` 与失败队列更新。这导致“分析得出的 reload 真值”和“真正 commit 到 live engine 的动作”没有 first-class transaction 边界，也就无法稳定支持 `AnalyzeOnly`、预检查、后台 warmup 或 request-local diagnostics。
  - 本条的第一阶段是显式化边界，而不是重写算法。新增 `FAngelscriptCompileRequest`、`FAngelscriptCompileTransaction` 与 `FAngelscriptModuleGraphSnapshot`：`InitialCompile()` / `PerformHotReload()` 只负责收集 dirty modules、profile 和 compile mode；`ExecuteCompileTransaction(Request)` 内部再分成 `PreparePlan()`、`BuildArtifacts()`、`ValidateReload()`、`Commit()`、`Rollback()`。现有 `CompileModules()` 先退化为兼容包装，默认行为保持不变。
  - 第二阶段补 `AnalyzeOnly` 与 request-scoped graph freeze。`Import`/dependency provider 查找、`CombinedDependencyHash` 聚合、旧模块 reference update 都先读 `ModuleGraphSnapshot`，不再在 request 中途回退 live `ActiveModules`。这样一来，`Issue-43 / 44 / 51` 这类“分析层已经知道应该升级 reload，但执行层又把真值吃掉”的问题，才有统一的承载对象与测试入口。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` `Issue-43 / Issue-44 / Issue-51` — 依赖传播与 reload 决策在分析阶段和执行阶段之间丢失真值，ClassGenerator 缺少统一执行计划层。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `Issue-43 / Issue-44 / Issue-51` 相关现有测试只覆盖 analysis helper 或弱断言，没有保护 transaction 执行层是否真的按同一结论 commit。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-10 / Arch-SL-11 / Arch-SL-30` — `FAngelscriptModuleDesc` 混装 source/artifact/live state，`CompileModules()` 没有 first-class request/transaction，且依赖 provider 仍直接读 live `ActiveModules`。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `D4` — 当前 Angelscript 的 safe point owner 仍是 compile transaction，这是应保留并显式化的强项，不应继续埋在大函数副作用里。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L176-L185` — 公开编译入口仍只有 `InitialCompile()`、`CompileModules()` 与四个 stage helper，没有 request/transaction 类型。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L1272-L1333` — `FAngelscriptModuleDesc` 仍同时承载 source、artifact 与 live/runtime 相关字段。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L2038-L2088` — `InitialCompile()` 现场构造 `ModulesToCompile` 后立即调用 `CompileModules()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3061-L3208` — `CompileModules()` 内部自建 `CompilingModulesByName`、`CompiledModules`、`ScriptUpdateMap`，并在同一条路径里混合 import provider 解析与旧模块 availability 剥离。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3134-L3147` — commit 之前就先把旧模块从 engine availability 中移走。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3177-L3183` — import provider 找不到本轮 compiling module 时会直接回退 `GetModuleByModuleName()` 读取 live graph。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3894-L4064` — 同一函数尾部继续串行执行 `ClassGenerator.Setup()`、reload 决策、`SwapInModules()`、rollback reverse-map 与后续 side effect。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` `L123-L161` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L75-L103` — 预处理阶段直接吐 `FAngelscriptModuleDesc` 数组，缺少 request-local source/artifact snapshot。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCompileTransactionTests.cpp`
- [ ] **P2.5** 📦 Git 提交：`[CompileTransaction] Refactor: add compile request, transaction, and graph snapshot`
- [ ] **P2.5-T** 单元测试：验证 analyze-only、rollback 与 request-scoped graph freeze
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCompileTransactionTests.cpp`
  - 测试场景：
    - 正常路径：`AnalyzeOnly` request 可以完成预处理、Stage1-4、`ClassGenerator.Setup()` 与 diagnostics 采集，但不会改动 `ActiveModules`、`ModulesByScriptModule` 或失败队列。
    - 边界条件：同一次 compile request 执行期间外部 live graph 发生无关变化，当前 transaction 仍坚持自己的 `ModuleGraphSnapshot`，得到稳定的 dependency hash 与 import 解析结果。
    - 错误路径：编译或 reload 校验失败后，rollback 会恢复旧 `ModulesByScriptModule`、旧 bytecode 与旧 provider availability，不留下半提交的 module swap / template replacement 污染。
  - 测试命名：`Angelscript.TestModule.Core.CompileTransaction.KeepsAnalyzeOnlyAndRollbackIsolated`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.5-T** 📦 Git 提交：`[CompileTransaction] Test: cover analyze-only snapshot and rollback isolation`

- [ ] **P2.6** 建立 `PersistentModuleCatalog + ModuleResidencyStore + CodeOnlyCommitLane`
  - 当前模块生命周期仍被 `ActiveModules` 一把抓住：预处理器只在本次请求里临时创建 `FAngelscriptModuleDesc`，成功编译后直接 `SwapInModules()` 进入 live map，失败或清理则通过 `DiscardModule()` 做 destructive hard-evict；fully precompiled 路径也会在恢复时直接清空 `ActiveModules`。这让系统缺少一个独立于 live runtime 的 `module catalog / residency layer`，也无法表达“同一个模块 identity 仍存在，但这次只做 code-only commit、不走 class generator 全链”的渐进 lane。
  - 本条把 `P2.5` 的 transaction 边界继续往后拆。第一阶段新增 `PersistentModuleCatalog` 与 `ModuleResidencyStore`：`catalog` 持久记录 module identity、source/artifact fingerprint、最近一次成功 compile artifact 与 dirty closure；`residency` 只描述当前 live `Active / Warm / Cold / Evicted` 状态。`GetModuleByFilename*()`、rename/update lookup 与 precompiled replay 优先对 catalog 说话，而不是把 `ActiveModules` 当唯一账本。
  - 第二阶段补 `CodeOnlyCommitLane`。当 transaction 明确判定模块只涉及 code/body 变化、没有 class/enum/delegate layout 变化时，允许该模块走不创建 `FAngelscriptClassGenerator` 或最小化 class-generator 参与的 fast lane；默认 eager/pinned 行为保持现状。这样既能吸收 `puerts/UnLua` 那种“缓存/目录/已加载态分层”的经验，又不牺牲当前插件对 build-bound snapshot 与严格 gate 的主线约束。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-50` — `ModuleLookupByFilename` / rename 相关测试只证明“能找到”和 `ModuleName` 一致，没有证明命中的就是稳定 module identity，对 catalog/identity 缺口没有保护。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-24 / Arch-SL-49 / Arch-SL-53 / Arch-SL-54` — 预处理仍是一次性 batch、source/precompiled artifact 缺少统一模块装载契约、系统只有 live `ActiveModules` 与 destructive `DiscardModule()` 两态，也没有纯脚本模块的轻量 commit lane。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` — `UnLua` 把 `loaded_modules / loaded_module_times` 与 reload 输入列表分开维护，`puerts` 则把 `moduleCache / localModuleCache / forceReload()` 分层，说明“目录账本、缓存账本、已加载态”不应全塞进一个 live module map。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L75-L103` — `GetModulesToCompile()` 只吐本次 batch 的模块数组，`AddFile()` 立即创建 `FAngelscriptModuleDesc`，没有持久 catalog。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L2046-L2056`、`L2068-L2082` — fully precompiled 与 source compile 是两条全局模式分支，而不是共享 module catalog 的同一请求链。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L2758-L2763` — `QueuedFullReloadFiles` 只是简单文件队列，没有 module residency / dirty-closure 账本。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L2907-L2939` — `SwapInModules()` 直接把模块换进 `ActiveModules`，commit 后没有独立 residency state。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3031-L3058`、`L4775-L4870` — filename/class/enum/delegate 查询都围绕 live `ActiveModules` 做线性或 name-based 查找。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1026-L1058`、`L1126-L1127` — `DiscardModule()` 直接从 script engine 和 `ActiveModules` 中硬删除模块。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` `L2603-L2610` — precompiled replay 也会直接清空 manager 的 live module 集合。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3894-L3907` — compile 成功后总是创建 `FAngelscriptClassGenerator`，缺少 code-only commit 分流。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L2126-L2288`、`L5865-L5884` — reload 主链与 full/code-only 判定仍全部围绕 class generator 决策。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleCatalogResidencyTests.cpp`
- [ ] **P2.6** 📦 Git 提交：`[ModuleCatalog] Refactor: add persistent catalog, residency store, and code-only commit lane`
- [ ] **P2.6-T** 单元测试：验证 module identity、residency 分层与 code-only fast lane
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleCatalogResidencyTests.cpp`
  - 测试场景：
    - 正常路径：同一模块在 persistent catalog 中拥有稳定 identity；code-only 变更命中 `CodeOnlyCommitLane` 时，不需要完整 class generator reload 也能完成 commit。
    - 边界条件：默认 eager/pinned 行为保持与现状一致；source/artifact mixed-mode 下，未脏模块继续沿既有 artifact/residency 状态运行，脏模块闭包单独升级。
    - 错误路径：rename、evict、artifact miss 或同名不同路径模块并存时，filename lookup 仍命中正确 `ModuleDesc`/catalog entry；错误模块不会被误映射到同名 live 条目。
  - 测试命名：`Angelscript.TestModule.Core.ModuleCatalog.KeepsIdentityStableAcrossResidencyAndCodeOnlyCommit`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.6-T** 📦 Git 提交：`[ModuleCatalog] Test: cover catalog identity, residency, and code-only fast lane`

### Phase 3 补充：editor diagnostics 与 snapshot provider

- [ ] **P3.5** 建立 `EditorDiagnosticsService + SnapshotProviderRegistry`
  - 当前 editor 诊断/分析能力分散在三条互不统一的调用链上：`Tools` 菜单只挂少量匿名 lambda，`BlueprintImpact` 的正式 headless 入口闭合在 `commandlet Main()`，`EditorStateDump` 又通过 `FAngelscriptStateDump::OnDumpExtensions` 私下接入 runtime dump。已有能力并不弱，但它们还没有沉成一层统一的 diagnostics service / feature snapshot provider，因此后续只要再加 workspace index、reload 审计或项目自定义检查，就会继续在菜单、commandlet、dump hook 之间重复接线。
  - 本条分两步做。第一步新增 `EditorDiagnosticsService`：把 `DiagnosticId`、headless/interactive request、summary/result model 统一起来，让 `BlueprintImpact` commandlet、菜单入口和未来 console/notification 都只做 surface adapter。第二步新增 `SnapshotProviderRegistry`：把现有 `EditorReloadState.csv` 与 `EditorMenuExtensions.csv` 改成 provider 产物，后续 `ContentBrowserDataSource`、watch roots、source navigation、runtime->editor bridge 都可以按 feature 注册只读 snapshot。
  - 这样既能保留当前 `BlueprintImpact` GUI/headless 共用同一分析内核的优点，也能吸收 `UnLua` 那种“先有 generator/service，再挂 toolbar 与 commandlet 两个 surface”的结构经验；而现有命令行参数、exit code 与 CSV 文件名都保持兼容，不与现有 editor automation 和外部脚本冲突。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/EditorArch_ArchReview.md` `Arch-60 / Arch-61` — editor 诊断能力分散在 menu、commandlet、dump hook 三条链，`EditorStateDump` 仍是硬编码两张表，缺少 feature-level snapshot provider contract。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `D7` — 工具 surface 应共享同一 semantic core，而不是每条 toolbar/commandlet/dump 链各写一套 orchestration。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` — `UnLua` 的 IntelliSense generator 同时被菜单和 commandlet 复用，说明 editor diagnostics 更适合先沉成 service/generator 再做 surface adapter。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L351-L415` — `StartupModule()` 仍 inline 注册 reload helper、source navigation、directory watcher、runtime->editor bridge、state dump 与 `ToolMenus` callback。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L701-L745` — `RegisterToolsMenuEntries()` 只注册 `ASOpenCode`、`ASGenerateBindings`、`Function Tests` 等菜单项，没有统一 diagnostics facade。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` `L55-L120` — `BlueprintImpact` 的参数解析、扫描执行、摘要输出与退出码都闭合在 commandlet 内部。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` `L111-L112` — 交互式 reload 路径直接调用 `AnalyzeLoadedBlueprint(...)`，没有 service adapter。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp` `L21-L118` — state dump 只硬编码导出 `EditorReloadState.csv` 与 `EditorMenuExtensions.csv`，再通过 `OnDumpExtensions` 私下注册。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp` `L186-L222` — runtime dump 主链仍假定 editor 扩展表就是这两张 CSV，没有 provider registry。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Diagnostics/AngelscriptEditorDiagnosticsService.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Diagnostics/AngelscriptEditorDiagnosticsService.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Diagnostics/AngelscriptEditorSnapshotProvider.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Diagnostics/AngelscriptEditorSnapshotRegistry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorDiagnosticsServiceTests.cpp`
- [ ] **P3.5** 📦 Git 提交：`[EditorDiagnostics] Refactor: add diagnostics service and snapshot provider registry`
- [ ] **P3.5-T** 单元测试：验证 commandlet、menu 与 dump 共享同一 diagnostics/service contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorDiagnosticsServiceTests.cpp`
  - 测试场景：
    - 正常路径：`BlueprintImpact`、editor state dump 与 menu-triggered diagnostics 都通过同一 `EditorDiagnosticsService` / provider registry 执行，且输出结果与当前一致。
    - 边界条件：现有 `BlueprintImpact` commandlet 参数、exit code 与 `EditorReloadState.csv` / `EditorMenuExtensions.csv` 文件名保持不变；新增 provider 不修改旧 dump 入口也能追加快照。
    - 错误路径：diagnostic provider 未注册、provider 执行失败或 headless-only/interactive-only surface 不匹配时，service 返回显式 diagnostics，而不是静默依赖某条私有 lambda / dump hook。
  - 测试命名：`Angelscript.TestModule.Editor.Diagnostics.UnifiesMenuCommandletAndSnapshotProviders`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P3.5-T** 📦 Git 提交：`[EditorDiagnostics] Test: cover unified diagnostics service and snapshot providers`

## 本轮追加条目的衔接与边界

1. `P1.6` 只处理 engine-effective settings 与显式 apply contract，不重复 `Plan_FullDeGlobalization.md` 的全仓去全局化，也不把 settings 扩大成新的扩展模块装配系统。
2. `P2.5` 先收口 request/transaction/snapshot 边界，`P2.6` 再处理持久 catalog、residency 与 code-only fast lane；两者互补，但不重复 `P2.1-P2.4` 已经覆盖的 reload owner、version chain 与 quiescence barrier。
3. `P2.6` 继续坚持当前项目的 `cache/snapshot + build gate` 主线，只补模块级目录与驻留分层，不把系统改成 raw script loader 或弱化 precompiled gate。
4. `P3.5` 只统一 diagnostics/service/provider contract，不改现有 `BlueprintImpact` commandlet 参数、exit code 与 `EditorReloadState.csv` / `EditorMenuExtensions.csv` 文件名，避免和既有 editor automation、外部脚本及 `P3.1-P3.4` 冲突。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.6` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEffectiveSettingsTests.cpp` | owner-scoped settings snapshot、显式 apply contract、禁止漂移回归 | 高 |
| `P2.5` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCompileTransactionTests.cpp` | analyze-only、request snapshot、rollback 不污染 live graph | 高 |
| `P2.6` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleCatalogResidencyTests.cpp` | stable module identity、residency 分层、code-only fast lane | 中高 |
| `P3.5` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorDiagnosticsServiceTests.cpp` | menu/commandlet/dump 统一 service、snapshot provider 扩展、兼容旧 CSV/exit code | 中高 |

---

## 深化 (2026-04-09 03:10)

### Phase 2 补充：逻辑模块身份与加载策略

- [ ] **P2.9** 建立 `LogicalModuleIdentity + ModuleResolver + InitialLoadPolicy`
  - 当前模块系统虽然已经有 `P1.4` 的 early import resolver 方向，也有 `P2.6` 的 persistent catalog/residency 方向，但“模块到底是谁”仍主要靠 path-derived string 临时拼出来：预处理器用 `FilenameToModuleName()` 把相对路径直接改写成 `Foo.Bar`，`import` 只保存裸字符串，启动阶段又一次性发现并编译全部 `.as`，运行期 filename lookup 失败后还会再从 root-relative path 反推模块名。这样一来，alias、entry file、variant、version、load policy 与逻辑模块失效都没有 first-class owner。
  - 本条目的目标不是重做整个 hot reload，而是把“逻辑模块身份”先独立出来。第一阶段新增 `FAngelscriptLogicalModuleId`、`IAngelscriptModuleResolver` 与可选 `ScriptModules.json`/manifest contract；未声明模块继续沿用当前路径映射，保证旧项目零迁移。第二阶段让 `ProcessImports()`、`GetModuleByFilename*()`、`InitialCompile()` 与失败/排队 reload 路径统一围绕这份 identity 工作：`import Foo.Bar;` 先解析成逻辑模块，再映射 entry/source 集合；启动时是否 eager compile 由显式 `InitialLoadPolicy` 决定，而不是默认把所有脚本都编进 live engine。
  - 这样可以把 `Arch-SL-02/03/04/05` 提到的“path 即模块、文件批次即加载策略、ActiveModules 即运行态真相”拆开：module identity 属于 resolver/manifest，compile 与 activation 属于后续 phase，catalog/residency 再消费同一 identity。它不重复 `P1.4` 的 hook 时序，也不重复 `P2.6` 的驻留账本，而是给两者补上共同的模块命名与加载前提。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — clone 包装层查询、测试发现与高层 helper 仍全部围绕 `ActiveModules` / filename lookup 运行，说明模块身份还没有从 live map 中抽离。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-25 / NewTest-40 / Issue-50` — 现有 filename 与 module-name lookup 测试仍主要在守“能不能找到”，没有保护“命中的是否是稳定逻辑模块身份”。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-02 / Arch-SL-03 / Arch-SL-04 / Arch-SL-05` — 明确指出当前仍缺 `FAngelscriptModuleIdentity`、resolver/manifest、按需加载策略与 activation plane。
    - [E] `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md`、`Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D4]` — `puerts` 把 `normalize -> search -> load -> moduleCache` 做成统一解析管线，说明模块名应升级为“模块解析协议”，而不是只保留 path-derived string。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L86-L89` — `FilenameToModuleName()` 仍直接把 `.as` 去掉并把 `/` 替换成 `.`，没有逻辑 `ModuleId`、alias 或 version/load policy。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` `L101-L108` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L3497-L3510` — `FImport` 及 `ProcessImports()` 只保存裸 `ModuleName` 字符串，没有 resolver 结果、entry file 或 policy 信息。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1999-L2015`、`L2038-L2083` — `InitialCompile()` 仍先递归发现全部脚本文件，再整体预处理并一次性编译所有已发现模块。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L2914-L2939`、`L3018-L3058` — swap-in 与 lookup 仍由 `ActiveModules` 和“按 root-relative path 反推模块名”的 fallback 驱动。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` `L138-L165` — runtime 初始化会立刻创建/初始化 primary engine，没有显式的 initial load policy 或 deferred module activation contract。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleResolver.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleResolver.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleManifest.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleManifest.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleResolverTests.cpp`
- [ ] **P2.9** 📦 Git 提交：`[ModuleResolver] Refactor: add logical module identity, resolver, and load policy`
- [ ] **P2.9-T** 单元测试：验证逻辑模块身份、alias/entry 解析与初始加载策略合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleResolverTests.cpp`
  - 测试场景：
    - 正常路径：manifest/resolver 可把 `import Foo.Bar;`、entry file 与 canonical `ModuleId` 统一到同一逻辑模块，且 eager/default 模块的启动编译结果与当前行为兼容。
    - 边界条件：未声明 manifest 的脚本继续沿用现有 `FilenameToModuleName()` 兼容路径；正斜杠、反斜杠、alias 名与真实 entry filename 都能归并到同一个逻辑模块身份。
    - 错误路径：重复 `ModuleId`、alias 冲突、entry file 缺失或非法 load policy 时，resolver 输出显式 diagnostics，且 runtime 不会留下半注册的 `ActiveModules`/filename lookup 脏状态。
  - 测试命名：`Angelscript.TestModule.Core.ModuleResolver.ResolvesLogicalIdentityAndLoadPolicy`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.9-T** 📦 Git 提交：`[ModuleResolver] Test: cover logical identity, alias resolution, and load policy`

### Phase 3 补充：多入口工具产物契约

- [ ] **P3.6** 建立 `ToolProducerContract + ToolRunResultSchema`
  - 当前 editor/runtime 侧已经有不少 operator surface，但“谁负责产生产物、谁负责给 menu/commandlet/CI 回结果”仍是分散的：`ToolMenus` 菜单直接调 `GenerateNativeBinds()`，legacy bind 生成函数内部直接写 `BindModules.Cache`；`BlueprintImpact` commandlet 自己解析参数、组装 JSON 摘要并决定 exit code；`AllScriptRootsCommandlet` 也手写自己的 JSON 输出。它们都有机器可消费结果，却没有共享的 producer/result contract。
  - 这条不重复 `P3.5` 的 diagnostics service。`P3.5` 解决的是“diagnostic capability 如何注册与复用”，而本条解决的是“产物型工具如何报告结果、产物路径、诊断与退出码”。第一阶段新增 `IAngelscriptToolProducer`、`FAngelscriptToolRunResult` 与稳定 `ToolId`/`SurfaceKind`/`ArtifactPaths`/`SummaryJson`/`Diagnostics` contract；菜单、commandlet、future console/CI 只做 adapter。第二阶段把 `GenerateNativeBinds()`、`AllScriptRoots`，以及已被 `P3.5` service 化的 `BlueprintImpact` 接到同一结果模型上，让“共享语义内核”继续延伸到共享输出契约。
  - 这样可以把现有工具从“各写各的 JSON / 日志 / 文件”收敛到一个 machine-readable result schema：GUI 可以弹 notification，commandlet 可以保持既有 exit code，CI 则直接消费 `ToolRunResult` 或产物 receipt。与 `P4.4` 的 toolchain program host 不冲突，因为这里针对的是 editor/runtime operator tool，而不是 UHT/docs/JIT/precompiled 主工具链。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-19 / Issue-20` — `AllScriptRoots` 的 JSON 合同与 `metrics.json` 的结构合同目前都缺少真正的接口级保护，说明 machine-readable output 还没有统一 schema owner。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT12 / Arch-DT16` — 当前多条 tool lane 缺少统一 coordinator/manifest，强调应先定义 producer、artifact root 与 freshness contract。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D7-SharedCore]` — 当前 `BlueprintImpact` 已经证明“多 surface 共享同一语义核”是可行方向，dump/docs/bind summary 仍值得继续朝公共内核 + 多 surface 收敛。
    - [E] `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` `[维度 D7]` — puerts 的菜单按钮与 console command 都回到同一个 `GenUeDts()`/`FTypeScriptDeclarationGenerator`，说明 shared producer contract 比复制 surface 逻辑更稳。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L701-L745` — `RegisterToolsMenuEntries()` 仍直接把菜单项绑到匿名 lambda 和 `GenerateNativeBinds()`，没有 command descriptor 或统一 result object。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L999-L1077` — `GenerateNativeBinds()` 直接分 shard、写 `ASRuntimeBind_* / ASEditorBind_*` 与 `BindModules.Cache`，没有独立 producer/result 层。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` `L55-L120` — 参数解析、扫描执行、JSON 摘要与 exit code 全部闭合在 `Main()` 内。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp` `L5-L21` — commandlet 直接拼接 JSON 数组并输出，没有共享 JSON/result schema。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Tooling/IAngelscriptToolProducer.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Tooling/AngelscriptToolRunResult.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tooling/AngelscriptToolProducerRegistry.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tooling/AngelscriptToolRunJson.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptToolRunContractTests.cpp`
- [ ] **P3.6** 📦 Git 提交：`[ToolRunContract] Refactor: add shared tool producer contract and result schema`
- [ ] **P3.6-T** 单元测试：验证 menu、commandlet 与 headless tool surface 共享同一产物结果合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptToolRunContractTests.cpp`
  - 测试场景：
    - 正常路径：legacy bind 生成、`AllScriptRoots` 与 `BlueprintImpact` 通过同一 `ToolProducer` 核心返回 `ToolId`、`ArtifactPaths`、`SummaryJson` 与稳定 exit/result code。
    - 边界条件：同一 producer 分别由 menu adapter、commandlet adapter 与最小 headless helper 调用时，输出字段名、JSON shape 与未变化内容的写盘行为保持一致。
    - 错误路径：参数非法、写盘失败或 producer 未注册时，调用方拿到结构化 diagnostics 与失败状态，而不是只依赖零散日志文本判断结果。
  - 测试命名：`Angelscript.TestModule.Editor.Tooling.SharedProducerReturnsStableRunResultAcrossSurfaces`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P3.6-T** 📦 Git 提交：`[ToolRunContract] Test: cover shared producer results across menu and commandlet surfaces`

### Phase 4 补充：性能归因账本

- [ ] **P4.5** 建立 `BindingCoverageId + DispatchAssumptionManifest + PerfCoverageSidecar`
  - 当前仓库已经能做 `PrecompiledData + StaticJIT + performance tests`，但“为什么某次回归发生、能不能把回归指回具体绑定/假设”仍缺一层 join contract。UHT summary 只统计 direct/stub 数量，JIT native-form 事实只挂在 `asIScriptFunction*` 上，dispatch/devirtualization assumption 还隐身在生成过程里；性能测试产物则只保留 total/median 和少量阶段耗时，无法对位到某个绑定或假设。
  - 这条的目标不是先做 profiler UI，而是先把账本补齐。第一阶段新增稳定 `BindingCoverageId`，让 `AS_FunctionTable_*`、runtime bind、JIT native form 与 future perf sidecar 都能 join 到同一函数级身份。第二阶段新增 `DispatchAssumptionManifest`，显式记录“为什么被视为 final / 为什么走 direct call / imported binding 当时绑定到了谁”。第三阶段让 `metrics.json` 或并列 `AS_PerfCoverage.json` 持有 `BindingCoverageId`、`FunctionId`、`AssumptionKinds`、`NativeFormKind`、`MetricNames`，把“能测量”提升到“可归因、可诊断”。
  - 这样可以直接回应维度 C 和 E 已经指出的短板：当前不是没有性能数据，而是缺 `CoverageId / AssumptionId` 这层 owner。`P4.2/P4.4` 负责 toolchain receipt/host，本条负责把这些 receipt 内的 UHT/JIT/runtime/perf 记录接到同一可对账键上。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-20` 及性能相关缺口 — 现有 `metrics.json` 测试主要守文件存在与字符串包含，startup/hot reload performance 并未保护函数级归因或 join key。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT26 / Arch-DT30` — 明确要求补 `BindingCoverageId` 与 `DispatchAssumptionManifest`，让 UHT coverage、JIT native form 与 dispatch assumption 成为可验证工件。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — 已将当前 D8 结论收束为“已能优化、已能测量，但缺 `BindingCoverageId / DispatchAssumption / perf sidecar` 的可归因合同”。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D8]` — 现有强项在于编译期、装载期、调用期都有优化 authority；下一步应把这些 authority 通过稳定 join key 对位，而不是继续停留在分散 sidecar。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` `L35-L40`、`L73-L98`、`L101-L157` — startup sample 只记录总耗时与两个阶段时间，写出的 artifact 不带函数/coverage/assumption 归因。
    - `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` `L36-L40`、`L60-L74` — hot reload performance 只写 `ReloadSeconds` 聚合，没有 join key。
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h` `L40-L80` — `metrics.json` schema 只有 `run_id`、`test_group`、`metrics`、`notes`。
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` `L166-L206`、`L244-L250` — summary/entry csv 仍只输出聚合字段与基础 entry 字段，没有稳定 `BindingCoverageId`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` `L27-L28`、`L112-L117`、`L530-L534` — native form 事实仍挂在 `GScriptNativeForms<asIScriptFunction*>` 上，且只在生成预编译数据时采集。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` `L299-L305`、`L432-L433` — 运行时 bind 侧仍主要依赖 ambient `GetPreviousBind()` 状态，没有稳定 coverage identity 旁路。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` `L3415-L3463` — `DevirtualizeFunction()` 与 `AnalyzeScriptFunction()` 直接把 assumption 物化成当前绑定与 `asTRAIT_FINAL` 副作用，没有单独 assumption ledger。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptBindingCoverage.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptBindingCoverage.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/DispatchAssumptionManifest.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/DispatchAssumptionManifest.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerfCoverageJoinTests.cpp`
- [ ] **P4.5** 📦 Git 提交：`[PerfCoverage] Refactor: add binding coverage ids, dispatch assumptions, and perf sidecar`
- [ ] **P4.5-T** 单元测试：验证 UHT、JIT、dispatch assumption 与性能工件可按同一 join key 对账
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerfCoverageJoinTests.cpp`
  - 测试场景：
    - 正常路径：同一函数在 `AS_FunctionTable_*`、coverage sidecar、`DispatchAssumptions` 与 performance artifact 中都能通过同一 `BindingCoverageId`/`FunctionId` 建立一对一映射。
    - 边界条件：stub、interface、legacy `GetPreviousBind()` fallback 或无 JIT native form 的函数，仍会得到稳定 coverage 记录，并明确标识 `jit=none/dynamic` 而不是整条记录缺失。
    - 错误路径：coverage id 冲突、assumption 失效或 metrics artifact 缺少 join 字段时，自动化输出显式 diagnostics，并拒绝把“只有 total/median 的旧工件”当成新合同通过。
  - 测试命名：`Angelscript.TestModule.Core.Performance.AttributionUsesJoinableCoverageAndAssumptionIds`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.5-T** 📦 Git 提交：`[PerfCoverage] Test: cover joinable coverage ids and dispatch assumptions`

## 本轮追加条目的衔接与边界

1. `P2.9` 不重复 `P1.4` 的 early import hook，也不重复 `P2.6` 的 persistent catalog/residency；它只解决逻辑模块身份、resolver/manifest 与 initial load policy 的统一 contract。
2. `P3.6` 以 `P3.5` 为前置，但不重复 diagnostics registry；`P3.5` 统一 capability，`P3.6` 统一产物型工具的 producer/result contract，重点补 legacy bind 生成与 commandlet JSON/operator 输出。
3. `P4.5` 不重复 `P4.2` 的 artifact receipt/symbol graph，也不重复 `P4.4` 的 toolchain host；它补的是 `UHT -> runtime bind -> JIT -> metrics` 之间的可归因 join key 和 assumption ledger。
4. 本轮条目刻意避开 `Documents/Plans/Plan_FullDeGlobalization.md` 的进程级状态收口，以及 `Documents/Plans/Plan_DebugAdapter.md` 的协议前端与 DAP/客户端兼容工作。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.9` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleResolverTests.cpp` | logical module id、alias/entry 解析、initial load policy、错误诊断 | 高 |
| `P3.6` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptToolRunContractTests.cpp` | menu/commandlet/headless 共用 producer、稳定 JSON/result schema、结构化失败返回 | 中高 |
| `P4.5` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerfCoverageJoinTests.cpp` | `BindingCoverageId` 对账、dispatch assumption ledger、perf sidecar join | 高 |

---

## 深化 (2026-04-09 06:38:02)

### Phase 1 补充：runtime 服务 owner 与 teardown 账本

- [ ] **P1.10** 建立 `OwnedRuntimeServiceHost + DelegateHandleLedger`
  - 当前 `FAngelscriptEngine` 已经把 `DebugServer`、`StaticJIT`、`PrecompiledData`、`PrimaryContext` 放进 owner/shared-state 清理链，但 `CodeCoverage`、automation hooks、`OnGetOnScreenMessages` 仍停留在“初始化时顺手注册”的 side-effect 形态。这样一来，runtime 生命周期账本并不完整，测试/编辑器长会话里的 service teardown 也没有统一 authority。
  - 第一阶段先把“可选 runtime service”收口到 `OwnedRuntimeServiceHost`：由 host 统一持有 `CodeCoverage` 这类 service 指针，以及 `FDelegateHandle`/late-init token；`FAngelscriptEngine` 只负责创建 host、把 host 接到 shared-state owner 生命周期，并在 `Shutdown()` / deferred release / module-owned engine shutdown 三条路径上调用同一 `Release()`。
  - 第二阶段再引入 `DelegateHandleLedger`，把 `OnPostEngineInit`、`OnGetOnScreenMessages`、coverage test hooks 这类注册动作改成显式登记和幂等解绑；`FAngelscriptCodeCoverage` 自己不再假定外部一定常驻进程，而是通过 host 暴露 `BindAutomationHooks()` / `UnbindAutomationHooks()`。整个实现限定在 `Plugins/Angelscript` 内，不依赖引擎改动，也不与 `Plan_FullDeGlobalization.md` 的更大范围状态收口重复。
  - 这样做的收益不是只修一处泄漏，而是给后续 runtime service 增量留出统一 owner 面。未来再引入新的 hot-reload observer、debug sidecar、test-only service 时，不需要继续把 `new + AddRaw` 散落进 `Initialize_AnyThread()` 和 `Shutdown()`。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `A-25 / D-14`
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-65 / NewTest-66`
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` 中对 delegate handle 显式持有的模式（`OnCompileDelegateHandle`、`OnBeginGeneratorDelegateHandle`、`OnEndGeneratorDelegateHandle`）
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1460-L1463` — `CodeCoverage` 仍以裸 `new` 创建，没有统一 service owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1628-L1634` — `OnPostEngineInit.AddLambda(...)` 注册后不保存 handle，无法在 teardown 时回收。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1638-L1638` — `OnGetOnScreenMessages.AddRaw(this, ...)` 也没有登记到任何 ledger。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1132-L1245` — `Shutdown()` 清理了多种 owned object，但没有 `delete CodeCoverage` 或 delegate unbind。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L327-L378` — `ReleaseOwnedSharedStateResources()` 同样没有覆盖 coverage/service hook。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp` `L22-L28` — automation hooks 仍使用 `AddRaw(this, ...)`，生命周期完全依赖外部手工解绑。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` `L27-L39` — module shutdown 只回收 ticker 与 owned engine，无法兜底悬挂 service hook。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeServiceHost.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeServiceHost.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeServiceLifecycleTests.cpp`
- [ ] **P1.10** 📦 Git 提交：`[RuntimeLifecycle] Refactor: add owned runtime service host and delegate ledger`
- [ ] **P1.10-T** 单元测试：验证 runtime service owner、late-init hook 与 teardown 幂等合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeServiceLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：创建 full engine 后，`OwnedRuntimeServiceHost` 能注册 `CodeCoverage` 与 late-init hooks；shutdown 时统一释放 service 与 delegate handle。
    - 边界条件：service disabled、late-init 尚未发生、module-owned engine / shared-state owner 路径下，host 仍能保持空对象幂等释放，不制造额外回调。
    - 错误路径：重复 shutdown、deferred owner release、engine recreate 后，不会留下重复 automation callback 或悬挂 on-screen message hook。
  - 测试命名：`Angelscript.TestModule.Engine.RuntimeServices.OwnerHostReleasesDelegatesAndOptionalServices`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.10-T** 📦 Git 提交：`[RuntimeLifecycle] Test: cover runtime service host teardown and delegate cleanup`

### Phase 2 补充：属性物化与 accessor owner

- [ ] **P2.10** 建立 `PropertyMaterializationPlan + PropertyAccessorDescriptor`
  - 当前 property 体系仍把两个本应独立的问题搅在一起：`ClassGenerator` 决定“成员是否落成真实 `FProperty` 以及带哪些 UE 语义”，`Bind_BlueprintType` 决定“native property 如何生成 getter/setter 和 bind-db replay”。两边都只拿到非常粗粒度的 `CreateProperty()` / `BindProperty()` 扩展点，于是新增 family 时需要同时改 class generation、native accessor、DB replay 三个面。
  - 第一阶段先补 `PropertyMaterializationPlan`：在 class analysis 阶段把 `GC / Replication / Serialization / EditorExposure / ConstructionSlot` reason 明文化，并让 hidden transient lane、exported reflected lane、function argument/return lane 都消费同一 plan。这样 `CreateProperty()` 不再只是“能不能造一个字段”，而是“按什么意图、以什么 storage kind 落成字段”。
  - 第二阶段再补 `PropertyAccessorDescriptor`：让 `Bind_BlueprintType.cpp` 不再直接手写 object/unresolved object/enum/POD family 分支，而是根据 `FProperty + TypeUsage + MaterializationPlan` 先构造 descriptor，再由 descriptor 统一生成 getter/setter、cook replay 与 docs/property metadata。这个 owner 提取属于类型系统与绑定管线的收口，不等于 `Plan_AS238NonLambdaPort.md` 里针对 AS 2.38 parity 的局部迁移；它的目标是把后续 parity 吸收点放到 descriptor/plan 层，而不是继续堆 if/else。
  - 这样可以和现有 `P1.8`、`P1.9` 形成前后衔接：前者解决 surface support/profile，后者解决 bind phase orchestration，而本条负责“property 为什么存在、accessor 为什么这样生成”的 owner。整条链仍限定在插件源码内，不需要引擎侧 `FProperty` 或 UHT 改造。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-34`
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-47 / Arch-TS-49`
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` 中对 UnLua / puerts / UnrealCSharp property descriptor / translator 模式的对比
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` `L147-L165` — `MatchesProperty / CanCreateProperty / CreateProperty` 只回答粗粒度 property 形态，没有 materialization reason。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` `L255-L265` — `BindProperty()` 是可选 override，默认返回 `false`，没有统一 accessor descriptor owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L2924-L2932` — 成员 property 物化仍直接调用 `PropertyType.CreateProperty(Params)`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L2945-L2958` — replication flags 在 property 落地后分散补写，没有独立 materialization plan。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L3942-L3949` 与 `L3970-L3977` — return/argument property 也各自直接调用 `CreateProperty()`，没有与成员字段共享的 plan owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `L870-L879` — cooked/native property bind 仍先把 `FProperty` 还原成 `TypeUsage`，再直接调用 `Usage.Type->BindProperty(...)`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `L1094-L1113` — property docs 与 DB replay 仍是 binder 自己拼装，不由类型系统统一提供 descriptor。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `L1564-L1596` — family-specific `CreateProperty()` 逻辑仍散落在具体 binder/type family 代码中。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptPropertyAccessorDescriptor.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptPropertyAccessorDescriptor.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptPropertyMaterializationPlan.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptPropertyMaterializationPlan.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptPropertyMaterializationTests.cpp`
- [ ] **P2.10** 📦 Git 提交：`[PropertySystem] Refactor: add property materialization plan and accessor descriptor`
- [ ] **P2.10-T** 单元测试：验证 property 物化理由、accessor 生成与 cooked replay 命中同一 owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptPropertyMaterializationTests.cpp`
  - 测试场景：
    - 正常路径：reflected property、hidden transient property、function arg/return property 都能产出稳定 `PropertyMaterializationPlan`，且 getter/setter descriptor 与当前脚本表面保持兼容。
    - 边界条件：alias/type-finder reset、`AS_USE_BIND_DB` replay、object/unresolved object/POD/enum family 都消费同一 descriptor，而不是 editor/runtime 双轨分派。
    - 错误路径：成员声明了 replication/editor/serialization 意图，但 plan 仍落到 `HiddenTransientProperty` 时，生成阶段输出稳定 diagnostics；family 声称支持 surface，却未提供一致 accessor descriptor 时测试失败。
  - 测试命名：`Angelscript.TestModule.Bindings.PropertyMaterialization.PlanAndAccessorDescriptorStayInSync`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.10-T** 📦 Git 提交：`[PropertySystem] Test: cover property materialization and accessor descriptor parity`

### Phase 4 补充：docs/source 工件生命周期

- [ ] **P4.6** 建立 `DocumentationCatalog + SourceSectionIndex + ArtifactLifecycleManifest`
  - 当前 docs/debug/source-navigation 仍是三条松耦合链：`FAngelscriptDocs` 用进程内静态 `TMap` 保存 live metadata，`.hpp docs dump` 只按当前引擎状态直接写盘，`DebugServer` 又即时从 live docs cache 回拼 JSON。对外看似都能工作，但缺少“符号来自哪个 script section、当前磁盘工件是否仍代表同一轮构建、陈旧产物怎么清理”的统一生命周期合同。
  - 第一阶段先补 `SourceSectionIndex`：把 `UASClass` / `UASFunction` 从当前 `Code[0]` 固定首文件策略升级为 section-level source identity，优先消费生成期或 script-function 已知的 section/line 信息；`GeneratedSourceLineNumber` 也应在 live `ScriptFunction` 缺席时作为稳定 fallback，而不是继续被闲置。
  - 第二阶段补 `DocumentationCatalog + ArtifactLifecycleManifest`：在保留 `Docs/angelscript/generated/*.hpp` 导出路径的前提下，为每次 docs dump 记录 catalog、source identity、artifact revision、stale/deleted entry 和输出文件清单；`DebugServer` 与未来 IDE/CI 入口优先读 catalog，再按 live runtime 做覆盖。这样既延续当前插件的 live-authoritative 优势，又把 `.hpp docs`、debug database、source navigation 拉回同一个可发现合同。该条目是 `P4.2` 的 docs/navigation 消费者收口，不替代 `P4.2` 的更广义 artifact receipt。
  - 这项工作放在 Phase 4，是为了与当前主线优先级保持一致：先收口 blocker 和运行时 owner，再把 delivery / IDE 工件变成稳定入口；同时全程限定在 `Plugins/Angelscript` 里完成，不要求引擎修改。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` `发现 47`；`Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 73`
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-15`
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT4`
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` 对 `D6 / D11` 中 docs、debug database 与 artifact manifest 分裂的总结
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L1497-L1507` — `UASClass::GetSourceFilePath()` 仍固定返回 `Module->Code[0].AbsoluteFilename`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L1535-L1545` — `UASFunction::GetSourceFilePath()` 同样只认 `Code[0]`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L1548-L1558` — `GetSourceLineNumber()` 只读 `scriptData->declaredAt`，没有消费 `GeneratedSourceLineNumber` 或 section fallback。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` `L26-L29` — docs cache 仍是 process-global static `TMap`，没有 catalog/revision owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` `L407-L470` — docs dump 基于当前 live script engine 临时收集，不存在独立的 source identity/catalog 中间层。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` `L682-L755` — `.hpp docs` 直接写到 `ProjectDir/Docs/angelscript/generated`，没有 manifest、stale cleanup 或 revision 账本。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L2224-L2227` — docs dump 仍是 `bDumpDocumentation` 路径下的一次性 side-effect。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` `L1575-L1583` 与 `L1751-L1756` — debug database 仍直接从 live docs cache/UFunction lookup 取 doc/property doc，没有稳定磁盘 catalog。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocumentationCatalog.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocumentationCatalog.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocumentationCatalogTests.cpp`
- [ ] **P4.6** 📦 Git 提交：`[DocsCatalog] Refactor: add documentation catalog, source section index, and artifact manifest`
- [ ] **P4.6-T** 单元测试：验证 docs/source identity 与 stale artifact 生命周期
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocumentationCatalogTests.cpp`
  - 测试场景：
    - 正常路径：multi-section module 生成 docs catalog 后，class/function/source line 都能指向真实 section，而不是固定 `Code[0]`。
    - 边界条件：live runtime docs cache 与离线 catalog 共存时，`DebugServer` 读取 catalog 基线并允许 live override；`.hpp docs` 导出路径保持不变。
    - 错误路径：删除 symbol、删除 section 或残留旧 `.hpp` 文件时，manifest 必须把条目标成 stale/deleted 并清理对应产物，不能继续保留 ghost docs。
  - 测试命名：`Angelscript.TestModule.Engine.Docs.CatalogPreservesSectionSourceIdentityAndCleansStaleArtifacts`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.6-T** 📦 Git 提交：`[DocsCatalog] Test: cover section source identity and stale artifact cleanup`

## 本轮追加条目的衔接与边界

1. `P1.10` 只补 runtime service owner 与 delegate teardown，不重复 `Documents/Plans/Plan_FullDeGlobalization.md` 的全局状态收口，也不触碰 `Plan_DebugAdapter.md` 的协议/客户端兼容面。
2. `P2.10` 处于 `P1.8 / P1.9` 之后，负责 property 物化与 accessor owner；它不是 `Documents/Plans/Plan_AS238NonLambdaPort.md` 的 parity patch 扩写，而是为后续 parity 吸收提供稳定 owner。
3. `P4.6` 是 `P4.2` 在 docs/source-navigation/debug consumer 方向上的下游收口，保留现有 `.hpp docs dump` 路径，不把它误判为技术债，也不单独另起一套 IDE 协议。
4. 本轮优先级与当前主线保持一致：先补 `P1` 生命周期 owner，随后推进 `P2` 类型系统/绑定收口，最后把 `P4` delivery 与 authoring 工件生命周期沉成稳定入口。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.10` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeServiceLifecycleTests.cpp` | owned service host、automation/on-screen delegate cleanup、重复 shutdown 幂等 | 高 |
| `P2.10` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptPropertyMaterializationTests.cpp` | property materialization plan、accessor descriptor、bind-db replay 一致性 | 高 |
| `P4.6` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDocumentationCatalogTests.cpp` | multi-section source identity、docs catalog/live override、stale artifact cleanup | 中高 |

---

## 深化 (2026-04-09 06:51:25)

### Phase 1 补充：startup 契约与 runtime 引用 owner

- [ ] **P1.11** 建立 `StartupPhaseRegistry + ThreadAffinityContract`
  - 当前 threaded initial compile 只把“是否差不多完成”暴露成 `bIsInitialCompileFinished` 与无参 `OnInitialCompileFinished`；任何带副作用的扩展都只能自己猜“现在是不是已经安全回到 game thread”。`Bind_Console` 已经被迫内联一套 late-init 自救逻辑，这说明问题不是某个 bind 的偶发现象，而是 runtime startup 缺少正式 phase/affinity 合同。
  - 本条目不重做 `Stage1-4` 或把所有 boot 流程一次性拆空，而是先引入 `EAngelscriptStartupPhase`、`EAngelscriptStartupThreadAffinity`、`FAngelscriptStartupParticipant` 与 `FAngelscriptStartupCoordinator`，把 `Initialize()` 对外显式分成 `BootstrapAnyThread`、`InitialCompileAnyThread`、`CommitGameThread`、`PostCompileGameThread` 四个最小阶段。第一阶段只迁移今天已经在猜线程边界的参与者，例如 `Bind_Console`；旧 `GetOnInitialCompileFinished()` 保留，但内部退化成 `PostCompileGameThread` 的兼容 adapter。
  - 这样可以把“扩展应该挂在哪个启动节点、要求哪个线程执行、失败后如何诊断”变成 runtime 自己维护的合同，而不是继续要求每个 bind/editor/tool helper 都复制一遍 `if (!IsInitialCompileFinished()) ... AddLambda(...)`。它也是后续 `P2.8` test discovery/readiness consumer 的 producer-side 基础，不与 `P2.8` 的 service 拆分重复。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 A-06 / A-09 / A-11 / D-02` — threaded 初始化仍靠 `volatile`、全局 `GameThreadTLD` 和单次广播拼接时序，且没有自动化锁住这条生命周期分支。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-40` — 当前只暴露粗粒度完成广播，扩展侧必须自行处理 thread affinity。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L825-L847` — threaded 初始化仍用 `volatile bool bInitializationDone` 轮询等待，同时 worker thread 临时改写全局 `GameThreadTLD`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L242-L249` — `CanUseGameThreadData()` 只围绕 “当前是否 game thread / 是否已完成初编译” 这个布尔判断，没有 phase/affinity 描述。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L481-L489` — 对外可见的 startup 状态仍只有 `bIsInitialCompileFinished` / `IsInitialCompileFinished()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1653-L1655` — `PostInitialize_GameThread()` 只做一次 `GetOnInitialCompileFinished().Broadcast()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` `L17-L20`、`L37-L39` — runtime module 仍只公开 coarse-grained compile/startup delegate，没有 startup participant registry。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h` `L18-L31` — console bind 需要自己判断 threaded init 并手工延后 `RegisterConsoleVariable()`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptStartupCoordinator.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptStartupCoordinator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupPhaseTests.cpp`
- [ ] **P1.11** 📦 Git 提交：`[StartupLifecycle] Refactor: add startup phase registry and thread affinity contract`
- [ ] **P1.11-T** 单元测试：验证 startup participant、线程亲和与 legacy 广播兼容层
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupPhaseTests.cpp`
  - 测试场景：
    - 正常路径：同时注册 `AnyThread` 与 `GameThread` participant，threaded init 下前者在后台阶段执行，后者只在 `PostCompileGameThread` 触发。
    - 边界条件：关闭 threaded init、或只注册 legacy `GetOnInitialCompileFinished()` listener 时，adapter 仍只触发一次，且顺序与旧行为兼容。
    - 错误路径：participant 声明的 thread affinity 与实际 drain 线程不符、或重复注册同一 `DebugName + Phase` 时，协调器必须给出稳定 diagnostics 并拒绝双执行。
  - 测试命名：`Angelscript.TestModule.Engine.Startup.ParticipantsHonorThreadAffinityAndLegacyBroadcastCompatibility`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.11-T** 📦 Git 提交：`[StartupLifecycle] Test: cover startup phases, thread affinity, and legacy adapter`

- [ ] **P1.12** 建立 `RuntimeReferenceBridge + RootedPackageLifecycle`
  - 当前 module-owned runtime 仍把 `FAngelscriptEngine` 放在 `FAngelscriptRuntimeModule::OwnedPrimaryEngine` 这个静态 `TUniquePtr` 里，而 `FAngelscriptEngine` 内部却依赖 `UPROPERTY()` 持有 `WorldContextObject`、`AngelscriptPackage`、`AssetsPackage` 和配置对象。对于 subsystem owner 路径，这些引用还能借宿主 UObject 被 GC 看到；但 module-owned 路径没有任何 `FGCObject`/bridge owner，结果是 `UPROPERTY()` 语义在这条路径上退化成“看起来安全的私有裸指针”。
  - 同时，engine 会把 `/Script/Angelscript` 与 `/Script/AngelscriptAssets` 创建成 `RF_MarkAsRootSet` package，但 `Shutdown()` 只清空指针，不显式解除 root。这样 runtime 既没有稳定的引用桥，也没有 package root 生命周期账本；编辑器 content browser、literal asset、annotated recreate 等外部消费者又已经把 `AssetsPackage` 当成长期事实源，导致问题跨越 runtime 与 editor surface。
  - 本条目的增量路线是引入 `FAngelscriptRuntimeReferenceBridge`：它作为 module-owned runtime 的显式 owner，负责 `AddReferencedObjects`/`FGCObject` 级 reachability、root package token 管理、以及 `Shutdown()`/`ShutdownModule()` 的对称 unroot。第一阶段先只把 `WorldContextObject`、`ConfigSettings`、`AngelscriptPackage`、`AssetsPackage` 接进 bridge；第二阶段再把 module-owned engine、testing override engine 与第二轮 annotated recreate cleanup 的弱引用验证收敛到同一 lifecycle contract。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 A-06 / A-08 / A-27` — module-owned engine 不在 GC 引用图里，rooted package 缺少显式释放，`WorldContextObject` 在 module owner 路径上会退化成未受追踪的引用。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-66 / NewTest-66` — 现有测试没有直接保护第二轮 annotated epoch cleanup，也没有覆盖 `ShutdownModule()` 对 owned engine / ticker / package 生命周期的完整收尾。
    - [D] `Documents/AutoPlans/ArchitectureReview/EditorArch_ArchReview.md` — content browser 与 editor workflow 仍直接消费 `FAngelscriptEngine::Get().AssetsPackage`，说明 package 生命周期已经外溢到 editor surface。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — 参考实现把对象/struct 清理、`RemoveFromRoot` 与 game-thread teardown 提升成正式 owner contract，而不是只清空持有指针。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L455-L456` — `WorldContextObject` 仍以 `UPROPERTY()` 形式存放在 `FAngelscriptEngine` 内部，默认假设存在 GC 可见 owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` `L59-L60` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` `L162-L164` — module-owned runtime 仍是静态 `TUniquePtr<FAngelscriptEngine> OwnedPrimaryEngine`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L878-L882` — `AngelscriptPackage` 与 `AssetsPackage` 仍以 `RF_MarkAsRootSet` 创建。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1245-L1251` — `Shutdown()` 只把 `AngelscriptPackage`、`AssetsPackage`、`WorldContextObject` 置空，没有显式 unroot/release 合同。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeReferenceBridge.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeReferenceBridge.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeReferenceBridgeTests.cpp`
- [ ] **P1.12** 📦 Git 提交：`[RuntimeReferences] Refactor: add GC bridge and rooted package lifecycle for module-owned runtime`
- [ ] **P1.12-T** 单元测试：验证 module-owned runtime 的引用桥、package unroot 与重复 epoch cleanup
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeReferenceBridgeTests.cpp`
  - 测试场景：
    - 正常路径：module-owned engine 挂上 `RuntimeReferenceBridge` 后，`WorldContextObject`、`ConfigSettings` 与两个 script package 在运行期可被稳定追踪；`ShutdownModule()` 后 package 被解除 root，弱引用失效。
    - 边界条件：testing override、第二轮 annotated recreate、以及 `CollectGarbage()` 穿插在 shutdown 前后时，bridge 仍保持对称 release，不留下脏 `OwnedPrimaryEngine` 或残留 package。
    - 错误路径：重复 `Shutdown()` / `ShutdownModule()`、bridge 未绑定 owner 就尝试 unroot、或 module-owned engine 已释放但旧 world/package 弱引用仍可解析时，测试必须失败并给出明确诊断。
  - 测试命名：`Angelscript.TestModule.Engine.RuntimeReferences.ModuleOwnedBridgeTracksWorldContextAndUnrootsPackages`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.12-T** 📦 Git 提交：`[RuntimeReferences] Test: cover GC bridge, package unroot, and repeated epoch cleanup`

### Phase 2 补充：值语义 owner 与容器扩展性

- [ ] **P2.11** 建立 `ValueOpsCatalog + ContainerCapabilityDescriptors`
  - 当前值生命周期合同同时散落在 `FAngelscriptType` 大虚表、container binder 的 `bNeedConstruct/bNeedDestruct/bNeedCopy` 缓存、debugger literal 的临时构造/析构，以及 `FASStructOps` 的 `Construct/Destruct/Copy/Identical/GetStructTypeHash` 路径里。这样一来，新增一个非 POD 值族、脚本 struct 或新的容器 family，就必须同时改 type bridge、container ops、debugger 和 script struct runtime；owner 不清，回归也更难定位。
  - 本条目不重复 `P2.10` 的 property 物化理由，也不触碰 `UHT` 生成入口；它只把 “值如何初始化、复制、销毁、比较、哈希” 收敛成统一 `FAngelscriptValueOps`/`FAngelscriptValueOpsCatalog`。第一阶段先让 `FDebuggerValue`、`Bind_TArray`、`Bind_TOptional` 共用同一 value-ops 入口；第二阶段再把 `Bind_TMap`/`Bind_TSet` 和 `FASStructOps` 接到同一来源，避免 script struct 的 `Identical/Hash` 与容器 helper 各自维护一套语义。
  - 这项工作可以直接吸收参考插件里 “descriptor/helper 持有 value lifecycle” 的经验，又不会和现有 `FAngelscriptType` 冲突：`FAngelscriptType` 继续表达语言级类型语义，`ValueOps` 则成为容器、debugger、struct ops 的共享执行层。这样后续新增 wrapper/container family 时，不再需要为每个 consumer 手动复制 `NeedConstruct()` / `NeedCopy()` / `NeedDestruct()` 分支。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-41` — debugger value/reify 基础设施缺少独立回归，暴露出临时值/类型元数据这条底层能力没有统一 owner。
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-08` — 值生命周期 trait owner 分散在 type、container binder、debugger 与 `ICppStructOps` 四处。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` 与 `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — UnLua / UnrealCSharp 都把 container/value 生命周期下沉到 descriptor/helper owner，而不是让每个容器 family 复制 `bNeed*` 缓存。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` `L202-L235` — `FAngelscriptType` 仍直接承载 `NeedCopy/CopyValue/NeedConstruct/ConstructValue/NeedDestruct/DestructValue`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` `L696-L705` — `FDebuggerValue` 仍直接调用 `NeedConstruct()` / `ConstructValue()` / `NeedDestruct()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` `L125-L173` 与 `L1769-L1775` — `TArray` 同时内联逐元素 lifecycle 逻辑，并额外缓存 `bNeedConstruct/bNeedDestruct/bNeedCopy`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.h` `L70-L76` — `TMap` 为 key/value 单独缓存 `bNeed*` 三元组。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp` `L324-L329` — `TOptional` 继续维护自己的 `bNeedConstruct/bNeedDestruct/bNeedCopy`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` `L127-L205` — `FASStructOps` 另外实现了一套 `Construct/Destruct/Copy/Identical/GetStructTypeHash`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptValueOps.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptValueOps.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptValueOpsTests.cpp`
- [ ] **P2.11** 📦 Git 提交：`[ValueOps] Refactor: add shared value-ops catalog for containers, debugger, and script structs`
- [ ] **P2.11-T** 单元测试：验证容器、debugger 临时值与 script struct ops 共享同一值语义合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptValueOpsTests.cpp`
  - 测试场景：
    - 正常路径：`TArray<ScriptStruct>`、`TOptional<ScriptStruct>`、debugger literal 与 `FASStructOps` 都从同一 `ValueOps` 得到构造/复制/析构/哈希行为，结果保持一致。
    - 边界条件：POD、object pointer、script struct、带 `Identical/Hash` 与不带 `Identical/Hash` 的类型都能通过同一 catalog 取到 capability，不再依赖容器 family 自己复制 `bNeed*`。
    - 错误路径：type reset/reload 后继续命中 stale ops、或容器 helper 与 `FASStructOps` 对同一类型给出不一致 `Identical/Hash` 结论时，测试必须稳定失败。
  - 测试命名：`Angelscript.TestModule.Bindings.ValueOps.ContainerDebuggerAndStructOpsShareOneLifecycleContract`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.11-T** 📦 Git 提交：`[ValueOps] Test: cover shared lifecycle ops across containers, debugger, and struct ops`

## 本轮追加条目的衔接与边界

1. `P1.11` 只补 startup producer-side 的 phase/affinity 合同，不重复 `P2.8` 的 `TestDiscoveryService` consumer 设计；`P2.8` 继续消费 milestone，`P1.11` 负责把 milestone/participant owner 正式化。
2. `P1.12` 不重复 `P1.1` 的 runtime identity，也不把工作扩张成 `Plan_TestEngineIsolation.md` 那类纯测试扩写；它只处理 module-owned runtime 的 GC/reference bridge 与 rooted package 生命周期。
3. `P2.11` 处于 `P2.10` 之后，负责值生命周期 owner；`P2.10` 仍聚焦 property materialization/accessor，二者前后衔接但不覆盖彼此。
4. 本轮优先级与当前主线保持一致：先补 `P1` 启动/生命周期 owner，再进入 `P2` 类型系统内部收口；没有额外拉高 editor/workflow 或外部交付主题的优先级。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.11` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupPhaseTests.cpp` | startup participant、线程亲和、legacy compile-finished adapter | 高 |
| `P1.12` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeReferenceBridgeTests.cpp` | module-owned GC bridge、package unroot、重复 annotated epoch cleanup | 高 |
| `P2.11` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptValueOpsTests.cpp` | container/debugger/script struct 共享 value ops、一致性与 stale-op 保护 | 中高 |

---

## 深化 (2026-04-09 07:18:53)

### Phase 1 补充：runtime-ready 里程碑与结构化生命周期状态

- [ ] **P1.13** 建立 `RuntimeReadyMilestone + LifecycleStateModel`
  - 当前 `P1.11` 已把 startup participant 和 thread affinity 抽成正式合同，但 runtime 对外仍然把“脚本编译完成”“`SharedState` 已提交”“testing/clone/full 路径是否真的可 tick”混在 `OnInitialCompileFinished` 与 `bIsInitialCompileFinished` 两个 legacy 表面里。结果是 startup observer、clone wrapper、tooling 和 tick gate 继续各自猜 readiness，生命周期真相源没有真正收口。
  - 本条目要把 readiness 拆成两个 owner：`RuntimeReadyMilestone` 负责里程碑广播，`LifecycleStateModel` 负责当前 runtime 快照。第一阶段不重排现有 compile 主链，只在 `InitializeOwnedSharedState()` 之后补 `SharedStateReady/RuntimeReady` 事件，并让 `Full/Test/Clone` 三条路径都显式写入 `FAngelscriptLifecycleState`；第二阶段再让 `ShouldTick()`、clone adopt、future lazy activation 和新测试 helper 统一读取结构化状态，而不是继续读 `Engine != nullptr` 或单一 bool。
  - 这条线是对 `P1.11` 的继续深化，不重开 `Documents/Plans/Plan_TestEngineIsolation.md` 或 `Documents/Plans/Plan_FullDeGlobalization.md` 的全量环境隔离改造；范围只限于 `AngelscriptRuntime` 自己的 lifecycle truth。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 B-15 / B-16 / B-21 / B-22` — clone wrapper 会把 `ShouldTick()`、automatic-import、`bIsInitialCompileFinished` 和 asset-scan 读成与 source runtime 不一致的生命周期事实。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-50 / Arch-SL-51` — `OnInitialCompileFinished` 早于 `SharedState` 提交，且 `bIsInitialCompileFinished` 无法稳定表达 full/test/clone 的 readiness。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L819-L857` — `Initialize()` 固定顺序仍是 `PreInitialize_GameThread -> Initialize_AnyThread -> PostInitialize_GameThread -> InitializeOwnedSharedState`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L922-L942` — `InitializeOwnedSharedState()` 才提交 `ScriptEngine`、`PrimaryContext`、`PrecompiledData`、`StaticJIT` 与 `DebugServer`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1653-L1655` — `PostInitialize_GameThread()` 仍只做一次 `GetOnInitialCompileFinished().Broadcast()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L859-L919` — `InitializeForTesting()` 在没有 `InitialCompile()` 的前提下直接把 `bIsInitialCompileFinished = true`，随后提交 `SharedState`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L481-L489` — 对外 readiness 查询仍只有 `bIsInitialCompileFinished` 与 `IsInitialCompileFinished()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L2843-L2857` — `ShouldTick()` 仍只检查 `Engine != nullptr`，`AdoptSharedStateFrom()` 只复制少量布尔而不是完整 lifecycle state。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeLifecycleState.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeLifecycleState.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptLifecycleStateTests.cpp`
- [ ] **P1.13** 📦 Git 提交：`[RuntimeLifecycle] Refactor: add runtime-ready milestones and structured lifecycle state`
- [ ] **P1.13-T** 单元测试：验证 full/test/clone 三路的 ready milestone 与 lifecycle state 同步推进
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptLifecycleStateTests.cpp`
  - 测试场景：
    - 正常路径：full engine 初始化时，`ScriptsCompiled` 先于 `SharedStateReady/RuntimeReady` 触发，且 ready report 可稳定读到 `ScriptEngine`、`PrimaryContext` 与 `LifecycleState`。
    - 边界条件：`InitializeForTesting()` 创建出的 testing engine 必须停在 `Bootstrapped + !HasCompiledScripts`，clone 必须继承 source runtime 的 compiled/ready 状态，而不是回落到默认 false。
    - 错误路径：重复 ready 广播、clone adopt 后 `ShouldTick()` 仍只因 `Engine != nullptr` 返回 true、或 legacy `IsInitialCompileFinished()` 与 `LifecycleState` 发生分裂时，测试必须失败并输出明确诊断。
  - 测试命名：`Angelscript.TestModule.Engine.Lifecycle.RuntimeReadyMilestoneAndStateStayConsistent`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.13-T** 📦 Git 提交：`[RuntimeLifecycle] Test: cover runtime-ready milestones and lifecycle state consistency`

### Phase 2 补充：模块运行态入口与依赖 fanout 账本

- [ ] **P2.12** 建立 `ModuleRuntimeEntry + ActivationReport`
  - 当前 `P2.5 / P2.6` 已经在 compile transaction 与模块目录账本层做收口，但“模块编译成功之后如何进入运行态”仍是隐式副作用：Stage4 直接 `ResetGlobalVars(0)`，reload 尾部无条件执行 `CallPostInitFunctions()` 与 `InitDefaultObjects()`，而 `PostInitFunctions` 只是由预处理器塞进去的一串短函数名。系统没有显式 `module runtime entry`、没有 activation result，也没有可缓存的 exports 视图。
  - 本条目要把“编译结果”和“运行态激活结果”拆开建模。第一阶段新增 `FAngelscriptModuleRuntimeEntry`、`FAngelscriptModuleActivationReport` 与轻量 `Exports` 视图，保留 `PostInitFunctions` 兼容路径，但把执行过程改成先生成 activation request、后提交 activation report；第二阶段再把 hot reload 切成“先准备新 runtime entry，成功后再切换当前 generation”，把 literal asset getter、startup hook 和未来 lazy activation 统一纳入同一个入口，而不是继续散落在 class generator 的 fire-and-forget side effect 中。
  - 这条线补的是 `P2.5 / P2.6` 之后的运行态 owner，不替换它们已有的 compile transaction、catalog 与 residency 范围。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 44 / 45` — literal asset `__Init_*` 会在 `InitDefaultObjects()` 之前以用户脚本形式执行，且 `PostInitFunctions` 只按短函数名分派，错误命中与未命中都不会升级成 reload 失败。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-47` — 当前 runtime 只有编译入口与全局副作用，没有显式 `module runtime entry / exports` 契约。
    - [E] `Documents/AutoPlans/ReferenceComparison/Puerts_Analysis.md` `7398-7428, 7548-7558` — puerts 把 `module.exports` 和 cache invalidation 做成显式 module object/key，reload 与缓存命中都围绕同一个 runtime entry 运转。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L160-L205` — runtime API 只有 `Initialize/InitialCompile/CompileModules/Tick`，没有显式模块激活或 exports 获取接口。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L1272-L1306` — `FAngelscriptModuleDesc` 只保存 `ImportedModules` 与 `PostInitFunctions`，没有 `RuntimeEntry`、`Exports` 或 `ActivationState`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L4403-L4410` — Stage4 仍直接 `ResetGlobalVars(0)` 初始化全局变量。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L2299-L2304` — reload 尾部仍是硬编码 `CallPostInitFunctions(); InitDefaultObjects();`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L5775-L5805` — `CallPostInitFunctions()` 只按短函数名遍历 `globalFunctionList` 并 `Context->Execute()`，没有结构化结果或错误聚合。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L4090-L4133` — literal asset lowering 仍把 `Get{Name}` 注入 `PostInitFunctions`，说明运行态入口还是“编译后副作用列表”。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleRuntimeEntry.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptModuleRuntimeEntry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleActivationTests.cpp`
- [ ] **P2.12** 📦 Git 提交：`[ModuleRuntime] Refactor: add module runtime entry and activation report`
- [ ] **P2.12-T** 单元测试：验证模块激活、legacy `PostInitFunctions` 兼容与 reload 回滚
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleActivationTests.cpp`
  - 测试场景：
    - 正常路径：普通模块 compile 后会生成稳定 `ModuleRuntimeEntry`，并可读取 activation report 与 exports 视图；重复激活在同一 generation 内应命中同一 entry。
    - 边界条件：未声明显式 startup/exports 的旧模块仍通过 legacy `PostInitFunctions` 完成激活，literal asset getter 继续工作，但 activation report 会记录兼容路径来源。
    - 错误路径：`PostInitFunctions` 缺失、短名碰撞命中错误函数、或 reload 新 generation 激活失败时，必须保留旧 runtime entry，不允许把部分副作用切成当前事实。
  - 测试命名：`Angelscript.TestModule.Engine.Modules.RuntimeEntryTracksActivationAndPreservesRollback`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.12-T** 📦 Git 提交：`[ModuleRuntime] Test: cover activation reports, legacy post-init, and rollback`

- [ ] **P2.13** 建立 `ImportEdgeLedger + ProviderConsumerIndex`
  - 当前 `P1.4` 更偏向 resolver 本身，而 runtime 仍然缺一份“哪些 import edge 存在、哪些 consumer 需要因 provider 变化而重绑”的正式账本。预处理器会把 inactive `#if/#ifdef` 分支直接抹成空白，只把当前可见 `import` 写入 `ImportedModules`；后续模块一旦发生变更，manual import 模式又直接 `ResolveAllDeclaredImports()` 全量扫全 engine，再对 provider/consumer 做第二轮大范围校验。结果是“图上不存在的边无法被分析，图上存在的边又只能用 whole-engine sweep 维护”。
  - 本条目要把 import 事实拆成两层：`ImportEdgeLedger` 记录 active + conditional edge，`ProviderConsumerIndex` 记录 declared import 的 provider-to-consumer fanout。第一阶段坚持 metadata-only，不改变现有 compile order，只让预处理器保留 conditional edge 并给 manual import/rebind 提供结构化索引；第二阶段再把 `ResolveDeclaredImports` 与 `CheckFunctionImportsForNewModules()` 收敛成 targeted rebind，只有真实受影响的 consumer 才进入验证与失败重试。
  - 这条线补的是 `P1.4` 之后的“谁依赖谁、谁该重绑”的账本层，不重复 `P1.4` 已有的模块发现和 resolver 语义；前者回答“去哪里找”，本条回答“图上有哪些边、哪一批 consumer 受影响”。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 B-16` — clone scope 下 `ShouldUseAutomaticImportMethodForCurrentContext()` 会被 wrapper 默认值读错，说明当前 import mode 与依赖投影仍依赖 ambient runtime 状态，而不是结构化图事实。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-31 / Arch-SL-46` — declared import 维护仍是 whole-engine sweep，且条件编译分支里的 `import` 在预处理期被直接抹除。
    - [E] `Documents/AutoPlans/ReferenceComparison/Puerts_Analysis.md` `7424-7428, 7548-7557` 与 `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` `5570-5799` — 参考实现把 reload/rebind 绑定到显式 module key 或 dependency graph，而不是每次 provider 变化都全量扫描全部 consumer。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L230-L238` — manual import 模式下仍先按 `ProcessImports()` 重排文件，依赖图来自预处理阶段投影。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L439-L498` — `ProcessImports()` 只遍历 `File.Imports`，随后把命中的模块名写入 `ImportedModules` 并把原 `import` 文本抹掉。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L3256-L3403` 与 `L3489-L3510` — inactive `#if/#ifdef` 分支会先被整体改成空白，只有当前可见的顶层 `import` 才会形成 `FImport`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3173-L3208` — Stage1 只消费 `Module->ImportedModules` 构造 provider 集，conditional edge 已经不可见。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L4056-L4064` 与 `L4426-L4430` — 模块变更后 manual import 模式仍直接 `ResolveAllDeclaredImports()` 扫过全部 `ActiveModules`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L4646-L4695` — `CheckFunctionImportsForNewModules()` 仍通过本地 `SwappingModules` + `GetImportedFunction*` 全量扫描 consumer import slot。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptImportEdgeLedger.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptImportEdgeLedger.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptImportEdgeLedgerTests.cpp`
- [ ] **P2.13** 📦 Git 提交：`[ImportGraph] Refactor: add import edge ledger and provider-consumer index`
- [ ] **P2.13-T** 单元测试：验证 conditional import 元数据、targeted rebind 与 fallback 全量 sweep
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptImportEdgeLedgerTests.cpp`
  - 测试场景：
    - 正常路径：provider A 改动后，只会重绑真正引用它的 consumer B/C；无关模块 D 不再被加入 rebind 或失败重试集合。
    - 边界条件：`#if EDITOR import Foo.Editor; #else import Foo.Runtime; #endif` 在当前 profile 下仍只编译激活边，但 ledger 必须同时记录 active 与 inactive edge；automatic/manual import 两种模式都能读到相同的边事实。
    - 错误路径：provider 缺失只在命中 active edge 或显式开启分析模式时报告；index 丢失或 stale 时必须自动回退 `ResolveAllDeclaredImports()`，保证正确性优先于优化。
  - 测试命名：`Angelscript.TestModule.Engine.Imports.ImportEdgeLedgerEnablesTargetedRebindAndFallbackSweep`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.13-T** 📦 Git 提交：`[ImportGraph] Test: cover conditional edges, targeted rebind, and fallback sweep`

### Phase 4 补充：authored source 定位与编译 backend 封装

- [ ] **P4.7** 建立 `SourceMapArtifact + DiagnosticsTranslator`
  - 当前 `P4.2 / P4.6` 已经在符号目录、docs catalog 与 source identity 生命周期上开始收口，但 authored source 和 generated/lowered code 之间仍缺少正式映射。预处理器把 chunk condense、range-based-for lowering 和 literal asset helper 全部折叠进单段 `ProcessedCode`，编译时又始终以原文件名和 `lineOffset=0` 入内核；之后 diagnostics、`UASFunction` 源码导航和模块级错误统一回落到 `Code[0]`。结果是 metadata-owned source identity 这条优势还在，但 authored span 已经被 rewrite/generate 过程悄悄打碎。
  - 本条目不再另起一套 symbol authority，而是补 `SourceMapArtifact + DiagnosticsTranslator` 这层侧车工件，让 `P4.6` 的 catalog、editor navigation、debug database 与 compile diagnostics 都能回到真实 authored section/line。第一阶段只做 sidecar map，不改变现有编译文本与 section 切分；第二阶段再评估是否对 generated helper 拆 section 或使用非零 `lineOffset`。
  - 这条线是对 `P4.2 / P4.6` 的 authored-span 深化，不重复 `Documents/Plans/Plan_DebugAdapter.md` 的协议面工作，也不把 source identity 从 metadata owner 改回 live frame 或离线 json-only 路线。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 73` — `UASFunction::GetSourceFilePath()` 无视真实 section，multi-section 模块会固定跳到 `Code[0]`。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-39` — `ProcessedCode` rewrite 后仍以 `AbsoluteFilename + lineOffset=0` 入引擎，diagnostics 无法稳定回溯到 authored span。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` 与 `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `[维度 D6]` — 当前仓库的优势是 editor/debugger 共享 metadata-owned source identity，因此更适合补 authored-span 翻译层，而不是退回到仅靠 live frame 或离线路径表的定位方式。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L276-L307` — 预处理完成后只把最终 `ProcessedCode` 写入 `Module->Code`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L3983-L4143` — range-based-for lowering 与 literal asset helper 都在整段 `ProcessedCode` 上改写或追加文本。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L4234-L4294` — 预处理 diagnostics 始终按 `File.AbsoluteFilename` + `Column = 1` 回报。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L1278-L1284` — `FCodeSection` 仍只有 `RelativeFilename`、`AbsoluteFilename`、`Code`、`CodeHash`，没有 authored/source-map 元数据。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L4343-L4345` — `AddScriptSection(... Section.AbsoluteFilename, Section.Code, 0, 0)` 仍固定 `lineOffset=0`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L4944-L4954` — 模块级 `ScriptCompileError()` 仍默认把错误回写到 `Module->Code[0].AbsoluteFilename`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L1497-L1558` — `UASClass/UASFunction` 源码路径仍固定取 `Module->Code[0]`，行号只读 `declaredAt`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSourceMap.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSourceMap.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDiagnosticsTranslator.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDiagnosticsTranslator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptSourceMapTests.cpp`
- [ ] **P4.7** 📦 Git 提交：`[SourceMap] Refactor: add authored source map artifact and diagnostics translator`
- [ ] **P4.7-T** 单元测试：验证 authored section/source line 能穿过 preprocessor rewrite 与 multi-section 导航
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptSourceMapTests.cpp`
  - 测试场景：
    - 正常路径：range-based-for 与 literal asset 生成 helper 报错时，diagnostics 能回到 authored `for` 或 `asset` 声明所在行，而不是生成代码尾部。
    - 边界条件：multi-section module 中 `UASFunction::GetSourceFilePath()`、`GetSourceLineNumber()` 与 docs/debug consumer 都指向真实 section，不再固定 `Code[0]`。
    - 错误路径：没有 `SourceMapArtifact` 的 legacy 模块仍按旧格式输出 diagnostics；一旦 translator 产出越界 fragment 或把模块级错误错误映射到首文件，测试必须失败。
  - 测试命名：`Angelscript.TestModule.Preprocessor.SourceMap.TranslatesDiagnosticsAndPreservesSectionIdentity`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.7-T** 📦 Git 提交：`[SourceMap] Test: cover authored diagnostics translation and section identity`

- [ ] **P4.8** 建立 `CompilationBackendAdapter + PrivateABISeal`
  - 当前 compile 主链仍把 `FAngelscriptEngine` 直接绑到 `asCModule/asCBuilder` 私有 ABI：`AngelscriptEngine.cpp` 顶部直接 include `source/as_builder.h`，Stage1-4 直接调用 `BuildParallelParseScripts()`、`BuildGenerateTypes()`、`BuildGenerateFunctions()`、`BuildCompileCode()`，并手工 `asDELETE(ScriptModule->builder, asCBuilder)`。这让 staged compile 虽然高性能，但也把 future analyze-only、兼容 smoke backend、selective 2.38 吸收和 callback ownership 修补都锁死在同一条私有实现里。
  - 本条目不要求切回引擎修改或一次性替换 fast path，而是先给插件内补一层 `CompilationBackendAdapter`。默认 backend 继续封装现有 private builder fast path；新增 `PublicABISmokeBackend` 只服务 analyze/smoke/upgrade 验证；`FAngelscriptEngine` 主链只编排 capability 和 phase，不再直接碰私有 builder 指针。这样既保留当前 `2.33 + selective 2.38`、不修改引擎的现实约束，也把未来升级面收敛到 backend 实现文件内。
  - 这项工作放在 Phase 4，是为了先完成 blocker、startup owner、运行态入口和 source artifact 这些更贴近当前主线的收口，再处理长线 backend 抽象；它属于 plugin-side 封装，不重复 `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md` 的跨插件经验吸收，也不要求重写现有 hot reload 语义。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` 中对 `BuildParallelParseScripts()` + `LogAngelscriptError()` owner 归属的分析指出，当前并行编译链与 message callback/diagnostics 仍依赖 ambient current-engine，而不是 backend 自带的实例边界。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-38` — 当前 compile orchestration 与 `asCBuilder` 私有阶段深度耦合，扩展能力被锁在 AngelScript 内核 ABI 之下。
    - [E] `Documents/AutoPlans/ReferenceComparison/Puerts_Analysis.md` `2825-2885` — puerts 通过 `pesapi_func_ptr` / `PesapiBackend` 先封装 backend-neutral ABI，再让上层 builder 消费统一 contract；对当前仓库可借鉴的是“先封印 ABI 边界，再扩演进能力”。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L46-L51` — runtime 仍直接 include `source/as_builder.h` 等私有 AngelScript 头文件。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3231-L3252` — Stage1 直接调用 `ScriptModule->builder->BuildParallelParseScripts()` 与 `BuildGenerateTypes()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L4376-L4397` — Stage2/3 直接调用 `BuildGenerateFunctions()`、`BuildCompileCode()`，随后手工删除 `ScriptModule->builder`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L3830-L3835` — 其他路径仍直接在 runtime 侧构造 `asCBuilder builder(ScriptEngine, nullptr)` 并操控其行为。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCompilationBackendAdapter.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCompilationBackendAdapter.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptPrivateBuilderBackend.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptPrivateBuilderBackend.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptPublicABISmokeBackend.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptPublicABISmokeBackend.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCompilationBackendTests.cpp`
- [ ] **P4.8** 📦 Git 提交：`[CompilationBackend] Refactor: seal AngelScript private ABI behind backend adapter`
- [ ] **P4.8-T** 单元测试：验证默认 private backend 行为不变，public smoke backend 可提供稳定对账面
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCompilationBackendTests.cpp`
  - 测试场景：
    - 正常路径：默认 `PrivateBuilder` backend 继续通过现有小模块 compile/reload 测试；同一简单模块在 `PublicABISmokeBackend` 下也能得到一致的 `ECompileResult` 与主要 diagnostics。
    - 边界条件：当 backend capability 不支持 staged reload、precompiled apply 或 private reload metadata 时，runtime 必须返回明确 capability report，而不是继续直接碰 `asCBuilder` 指针。
    - 错误路径：语法错误、import 缺失或 backend 切换失败时，两个 backend 都必须给出稳定 diagnostics；切换到 non-private backend 后不得留下悬挂 `builder` 指针或访问已删除的私有结构。
  - 测试命名：`Angelscript.TestModule.Engine.Compilation.BackendAdapterPreservesPrivateFastPathAndPublicSmokeParity`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.8-T** 📦 Git 提交：`[CompilationBackend] Test: cover private fast path and public smoke backend parity`

## 本轮追加条目的衔接与边界

1. `P1.13` 是对 `P1.11` 的 lifecycle truth 深化，不重复 `Documents/Plans/Plan_TestEngineIsolation.md` 或 `Documents/Plans/Plan_FullDeGlobalization.md` 的全局环境/测试隔离路线。
2. `P2.12` 补的是 compile 之后的模块运行态入口，和现有 `P2.5 / P2.6` 的 transaction/catalog/residency 收口互补，不替换其既有范围。
3. `P2.13` 补的是 import edge ledger 与 targeted rebind，不重做 `P1.4` 的 resolver，也不重复 `Documents/Plans/Plan_ScriptFileSystemRefactor.md` 的文件系统/VFS 主题。
4. `P4.7` 只补 authored span 到 metadata-owned source identity 的翻译层，和 `P4.2 / P4.6` 前后衔接；它不展开 `Documents/Plans/Plan_DebugAdapter.md` 的协议客户端工作。
5. `P4.8` 明确放在后期，只做 plugin-side backend 封装，遵守 `2.33 + selective 2.38`、不修改引擎的当前约束；它吸收参考实现的 ABI 封印思路，但不等价于整套多 VM 架构迁移。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.13` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptLifecycleStateTests.cpp` | runtime-ready milestone、full/test/clone 生命周期状态一致性 | 高 |
| `P2.12` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptModuleActivationTests.cpp` | module runtime entry、activation report、legacy post-init 与回滚 | 高 |
| `P2.13` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptImportEdgeLedgerTests.cpp` | conditional import 元数据、targeted rebind、fallback 全量 sweep | 高 |
| `P4.7` | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptSourceMapTests.cpp` | authored diagnostics 翻译、multi-section source identity、legacy fallback | 中高 |
| `P4.8` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCompilationBackendTests.cpp` | private fast path 保持、public smoke backend capability/diagnostics 对账 | 中 |

---

## 深化 (2026-04-09 07:30:50)

### Phase 1 补充：usable context 与 clone 投影

- [ ] **P1.14** 建立 `PrimaryContextLease + UsableContextBroker`
  - 当前 `primaryContext` 仍是“TLS 单槽 + wrapper 局部快照”的混合模型：threaded 初始化把 `GameThreadTLD` 临时借给 worker thread，退出时直接清空 `primaryContext`；多 full-engine 并存时，owner 销毁路径也只会把当前槽位改成 `nullptr`，不会恢复仍存活 owner 的 usable context。结果是 `asGetUsableContext()` 这条 VM 级 fallback 语义继续依赖偶然的槽位状态，而不是 runtime owner 的显式租约。
  - 本条目不是去改 AngelScript 2.33 内核，也不引入多 VM；第一阶段只在插件内补一层 `PrimaryContextLease`，让 full engine / testing full engine 在创建或 adopt shared state 时显式申请、转交、释放 usable-context 租约；第二阶段再让 pooled context、脚本对象构造/析构和 clone/full-engine teardown 都统一经 `UsableContextBroker` 读写 TLS 单槽。这样可以保留现有 `asGetUsableContext()` 契约，同时把 owner 真相从裸 `primaryContext` 指针提升成带 epoch 的插件侧合同。
  - 该条目优先级高于更晚的 toolchain 细化，因为它直接影响 multi-engine correctness、脚本对象生命周期与后续所有 clone/full-engine 测试的可信度；但它的实现仍限定在 `Plugins/Angelscript` 内，不需要引擎改动。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 A-09 / A-29 / D-17` — threaded 初始化会丢失 game-thread usable context，多 full-engine 又共享单个 `primaryContext` 槽，现有自动化没有直接守住这条合同。
    - [E] `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` — 参考实现把 shared runtime 资源与 owner/clone 生命周期显式建模；当前仓库虽然也有 `SharedState->PrimaryContext`，但还没有把它提升成可恢复的租约。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L831-L838` — threaded 初始化把 `GameThreadTLD` 指到 worker TLS，结束时直接把 `primaryContext` 清成 `nullptr`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L917-L935` — full/testing full engine 先写 `GameThreadTLD->primaryContext = CreateContext()`，再把该单槽快照进 `SharedState->PrimaryContext`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L354-L359` — owner 释放时若命中同一 TLS 槽，只会把 `primaryContext` 清空，不会恢复幸存 owner 的租约。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` `L150-L155` — `asGetUsableContext()` 在无 active context 时直接回退到 `tld->primaryContext`，说明当前单槽状态仍会影响真实执行路径。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptPrimaryContextLease.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptPrimaryContextLease.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPrimaryContextLeaseTests.cpp`
- [ ] **P1.14** 📦 Git 提交：`[PrimaryContext] Refactor: add lease broker for usable-context ownership`
- [ ] **P1.14-T** 单元测试：验证 usable context 租约在 threaded init、multi-owner 与 clone 场景下保持一致
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPrimaryContextLeaseTests.cpp`
  - 测试场景：
    - 正常路径：threaded/full/testing full 初始化完成后，game thread 上的 `asGetUsableContext()` 必须返回当前 owner 的 leased primary context，而不是 `nullptr`。
    - 边界条件：两台 full engine 先后创建并共存时，后创建 owner 释放后要恢复前一个仍存活 owner 的 lease；clone engine 不应额外抢占新的 primary-context lease。
    - 错误路径：重复 shutdown、owner 失配释放或 threaded init 中途失败时，broker 必须输出显式诊断并回退到安全状态，而不是静默遗留空槽或悬空 context。
  - 测试命名：`Angelscript.TestModule.Engine.Context.PrimaryContextLeasePreservesUsableContextAcrossOwners`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.14-T** 📦 Git 提交：`[PrimaryContext] Test: cover threaded init handoff and owner lease restore`

- [ ] **P1.15** 建立 `CloneProjectionCatalog + SharedStateObserverBus`
  - `P1.13` 已经开始收口 lifecycle truth，但 clone 仍只 adopt 了一小撮 wrapper-local 快照：module/type 索引、asset-scan 后续推进、`GetActiveModules()` 相关高层能力都没有正式投影。于是 clone 虽然共享同一个底层 VM，却可能继续把 source engine 已存在的 module graph、class graph 和后续异步状态读成空或旧值，测试又靠 `TrackNamedModule()` 之类 friend helper 手工补洞。
  - 本条目不是重做 `P1.13` 的 ready milestone，而是给 shared runtime 补一层只读投影账本。第一阶段新增 `CloneProjectionCatalog`，把 `ActiveModules / ModulesByScriptModule / ActiveClassesByName / ActiveEnumsByName / ActiveDelegatesByName` 与 `AssetScanReady/TestDiscoveryReady` 这类“shared runtime 事实”收进 `SharedState` 旁路；第二阶段再通过 `SharedStateObserverBus` 把 source wrapper 的 module swap、discard、asset-scan 完成和测试发现事件推给 clone，使 clone 不再靠一次性复制布尔值或测试后门修补。
  - 这样做可以保持“clone 不是第二个 VM”这一当前路线，同时让 clone 视图真正成为 source runtime 的受控 mirror，而不是时灵时不灵的局部快照；也避免和 `Plan_AngelscriptUnitTestExpansion.md`、`Plan_TestEngineIsolation.md` 的测试扩张主题重复。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 B-20 / B-21 / B-22 / D-19 / D-20 / D-21` — clone 不会继承 module/type 索引，`bIsInitialCompileFinished` / `bCompletedAssetScan` 只做快照复制，相关自动化也缺真实回归保护。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-27`、`NewTest-01`、`Issue-10` — 现有 clone/create-mode 测试更多只看耗时或最小 smoke，没有守住 source-engine 关联、fallback 模式和 clone 观察面合同。
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` `Arch-HR-8` — hot reload / watcher / state helper 仍与 engine clone / 多实例模型脱节。
    - [E] `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` — 参考实现虽然走的是多 VM 路线，但它把 shared resource owner 与 view lifecycle 分成显式层；当前仓库更需要把“单 VM 多视图”的投影层正式化。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L628-L648` — `CreateCloneFrom()` 仅共享 `SharedState` 并调用 `AdoptSharedStateFrom(Source)`，没有建立后续投影同步层。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L2850-L2856` — `AdoptSharedStateFrom()` 当前只复制 engine/package/root path 和 `bCompletedAssetScan` 等少量字段。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L385-L389` — `ActiveModules`、`ModulesByScriptModule`、`ActiveClassesByName` 仍是 wrapper-local 容器，不在 shared state 中。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L481-L484` — `bIsInitialCompileFinished` 与 `bCompletedAssetScan` 也仍是 wrapper-local bool。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` `L43-L48` — 测试通过手工写 `ActiveModules` / `ModulesByScriptModule` 来补 clone 视图，说明主路径仍缺正式 projection contract。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCloneProjectionCatalog.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCloneProjectionCatalog.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCloneProjectionTests.cpp`
- [ ] **P1.15** 📦 Git 提交：`[CloneProjection] Refactor: add shared projection catalog and observer bus`
- [ ] **P1.15-T** 单元测试：验证 clone 能无后门地观察 source runtime 的 module/type/lifecycle 投影
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCloneProjectionTests.cpp`
  - 测试场景：
    - 正常路径：source engine 编译并 swap-in 模块后，新建 clone 能直接看到相同 `ActiveModules` / `ModulesByScriptModule` / class lookup 结果，无需 `TrackNamedModule()`。
    - 边界条件：clone 在 source 完成 asset scan 或测试发现之前创建，后续 source 推进这些状态后，clone 也要收到同步更新。
    - 错误路径：projection revision 过期、source module 被 discard 或 clone 先于 source teardown 时，clone 必须收到显式失效/回退诊断，而不是静默返回空模块图。
  - 测试命名：`Angelscript.TestModule.Engine.Clone.CloneProjectionCatalogMirrorsModulesAndLifecycleWithoutManualBackfill`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.15-T** 📦 Git 提交：`[CloneProjection] Test: cover module graph and lifecycle mirroring for clones`

### Phase 2 补充：typed metadata owner 与结构化 type tree

- [ ] **P2.14** 建立 `TypedMetadataSlotRegistry + ContainerQueryService`
  - 当前 `plainUserData / GetUserData(type=0)` 仍被同时拿来承载 UE type bridge、template ops、delegate sentinel 和 module hash；与此同时，高层 gameplay helper 还直接解 `templateSubTypes[0].plainUserData`。这意味着“类型身份”“容器操作表”“helper 解析”没有各自 owner，接口删除、reload 清理和未来外部扩展都会继续踩在同一匿名槽位上。
  - 本条目第一阶段不推翻现有 `FAngelscriptTypeUsage` 与现有 binder，而是在插件内补 `TypedMetadataSlotRegistry` 与 `ContainerQueryService`：前者把 `TypeBridge / TemplateOps / DelegateTag / ModuleHash` 分到命名 slot，后者把高层 helper 对 `templateBaseType/plainUserData` 的手工解包收口成统一查询 API；第二阶段再逐步让 `Bind_AActor`、`Bind_USceneComponent`、`Bind_UDataTable`、StaticJIT cast 与 reload 清理都改读 registry/query，保留 slot `0` 镜像做迁移兼容。
  - 这条线与 `Plan_CppInterfaceBinding.md` 的区别是：它不直接承诺“补齐某一类 interface capability”，而是先把支撑 interface/container/wrapper 扩展的元数据 owner 收口。只有 slot/query owner 明确后，后续 interface value、外部 provider 和更多 wrapper family 才不会继续把 `plainUserData` 用成共享总线。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 52` — interface 删除/重载后 `ScriptType->GetUserData()` 残留，暴露出 live type bridge 仍缺统一 owner 和清理边界。
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-23`、`Arch-TS-31` — `plainUserData` 被多系统复用，高层 binder 还在自己解 `templateBaseType/templateSubTypes/plainUserData`。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` `6588-6779`、`7069-7191` — 当前 fork 已有 typed user-data API，问题在插件 owner 选择；高层 binder 应该读 resolver/query，而不是继续解 VM 内部布局。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `L660-L668` — native object type 仍直接写 `TypeInfo->plainUserData = (SIZE_T)Class`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L2590-L2610`、`L2701-L2736` — class/interface/struct/delegate 物化后继续把 live UE 反射对象写进默认 `SetUserData(...)`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` `L1735-L1753`、`Bind_TMap.cpp` `L1296-L1335`、`Bind_TOptional.cpp` `L334-L349`、`Bind_TSet.cpp` `L730-L763` — container ops 也都继续占用默认 user-data 槽。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` `L58-L67` 与 `Bind_UDataTable.cpp` `L60-L68` — 高层 helper 仍直接读取 `templateSubTypes[0].plainUserData` 取得 `UClass/UStruct*`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptMetadataSlots.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptMetadataSlots.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptContainerQuery.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptContainerQuery.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMetadataSlotRegistryTests.cpp`
- [ ] **P2.14** 📦 Git 提交：`[TypeMetadata] Refactor: add typed metadata slots and container query service`
- [ ] **P2.14-T** 单元测试：验证 type bridge、template ops 与高层 helper 不再争用匿名 slot
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMetadataSlotRegistryTests.cpp`
  - 测试场景：
    - 正常路径：同一 `asITypeInfo` 上能同时持有 `TypeBridge` 与 `TemplateOps`，`GetComponentsByClass` / `DataTable` 等 helper 通过 query service 得到正确 nominal type。
    - 边界条件：迁移窗口内保留 slot `0` 镜像时，legacy 调用仍可工作，但 registry/query 的结果必须与旧路径一致。
    - 错误路径：type reload/remove 后若 slot revision 失效、typed slot 缺失或 query 拿到陈旧 live pointer，测试必须收到显式失败而不是继续解 `plainUserData`。
  - 测试命名：`Angelscript.TestModule.TypeSystem.MetadataSlots.SeparateTypeBridgeTemplateOpsAndGameplayQueries`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.14-T** 📦 Git 提交：`[TypeMetadata] Test: cover typed slots, query service, and legacy mirror parity`

- [ ] **P2.15** 建立 `ReferenceFamilyKind + RoleTaggedTypeShape`
  - 当前 `FAngelscriptTypeUsage` 只有匿名 `SubTypes`，容器和 wrapper family 全靠约定 `SubTypes[0]` 是 element、`SubTypes[0/1]` 是 key/value、`SubTypes[0]` 是 nominal class。这让 object/interface/container/wrapper 的“角色”和“引用家族”都只能在调用点后置猜测，未来要补 `TScriptInterface<>`、复杂 wrapper 或更多 cross-tooling shape 时，仍会把扩展成本扩散到 binder、debugger、StaticJIT 和 UHT sidecar 多处。
  - 本条目先在现有 `FAngelscriptTypeUsage` 旁边补一层 sidecar `TypeShape`，而不是一次性替换所有 usage。第一阶段引入 `EAngelscriptReferenceFamilyKind` 与 `FAngelscriptTypeShapeNode`，显式标出 `Object / Interface / Delegate / ScriptObject` family，以及 `Element / Key / Value / ReferencedClass / InterfaceClass` 这些 role；第二阶段再让 `SurfaceSupportProfile`、`ContainerQueryService`、future interface/container rollout 统一消费 `TypeShape`。这样既能保持旧声明字符串与绝大部分现有 binder 不变，又能增量把“位置语义”升级成“结构化 shape”。
  - 这条线是对 `P1.8` 与 `P2.14` 的继续深化：`P1.8` 回答“某 family 在哪些 surface 上支持”，`P2.14` 回答“元数据挂在哪个 owner”，而本条回答“这个 family 的结构到底长什么样”。它也不重复 `Plan_CppInterfaceBinding.md` 的具体 capability 闭环，而是为那条主线提供统一 shape owner。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-53 / Arch-TS-54` — script-defined 引用类型在入口被压成单一 object family，`SubTypes` 又只有匿名位置语义，container/wrapper 缺少共享结构树。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `7454-7514` 与 `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` `5112-5150`、`7671-7679` — 参考实现把 `FInterfaceProperty`、container inner/key/value 和 `TScriptInterface<>` 的 family/role 都显式建模到 bridge/type graph 中。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` `L349-L351` — `FAngelscriptTypeUsage` 当前只有 `SubTypes`，没有 role/tag 字段。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` `L174-L189` — declaration printer 也是纯按 subtype 下标串接类型名。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` `L108-L156` 与 `L260-L264` — `TMap` 明确把 `SubTypes[0]` 当 key、`SubTypes[1]` 当 value。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp` `L108-L123` 与 `Bind_TSet.cpp` `L93-L107` — `TOptional`/`TSet` 都继续把 `SubTypes[0]` 当唯一 value/element。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `L1557-L1588` — `TSubclassOf/TObjectPtr/TWeakObjectPtr` 等 wrapper 也都把 `SubTypes[0]` 当 nominal class。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTypeShape.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTypeShape.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTypeShapeTests.cpp`
- [ ] **P2.15** 📦 Git 提交：`[TypeShape] Refactor: add reference family kind and role-tagged type shape`
- [ ] **P2.15-T** 单元测试：验证容器、wrapper 与 interface family 都能通过同一结构化 shape 被读取
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTypeShapeTests.cpp`
  - 测试场景：
    - 正常路径：`TArray<T>`、`TSet<T>`、`TMap<K, V>`、`TOptional<T>`、`TSubclassOf<T>` 的 shape 都能给出稳定的 role-tagged 结果。
    - 边界条件：object family 与 interface family 在 legacy declaration 相同或相近的情况下，shape 仍能区分 `ReferencedClass` 与 `InterfaceClass`。
    - 错误路径：role 缺失、位置顺序不合法或 legacy fallback 与新 shape 发生分裂时，binder/query 必须显式失败，而不是继续默默按下标解释。
  - 测试命名：`Angelscript.TestModule.TypeSystem.TypeShape.PreservesReferenceKindsAndRoleTaggedSubtypeSemantics`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.15-T** 📦 Git 提交：`[TypeShape] Test: cover reference family kinds and role-tagged subtype semantics`

### Phase 4 补充：JIT execution state 与调试桥

- [ ] **P4.9** 建立 `ExecutionStateProvider + JitBreakpointBridge`
  - 当前 runtime 实际已经有两套执行态事实：解释器侧 `asCContext` callstack，以及 JIT 侧 `Execution.debugCallStack`。但工具链主链还只消费前者，导致 `GetAngelscriptExecutionThisObject()`、`DebugBreak()`、`StepOver/StepOut` 和 `SendCallStack()` 在 JIT 路径上继续表现成“没有上下文”或直接退回原生 `UE_DEBUG_BREAK()`；甚至 `GetAngelscriptExecutionFileAndLine()` 的 JIT 分支还保留了确定性的空指针读。
  - 本条目第一阶段不动 DebugServer V2 协议形状，只引入 `ExecutionStateProvider` 抽象，把 interpreter/JIT 两路的 `CurrentLocation`、`ThisObject`、callstack 顶层 frame 和 breakpoint enter path 收到统一 provider；第二阶段再把 capability 结果显式暴露给调试器和脚本 helper，允许 JIT 路径先只交付 top frame/`this`，而不是伪装成“与解释器完全等价”。这样可以在不重开 `Plan_DebugAdapter.md` 的前提下，把 runtime 内部已经存在的 JIT debug state 接回正式协议。
  - 这条线应排在 blocker/lifecycle 真相收口之后，因为它不阻塞脚本执行主线；但它仍高于更晚的 UI/IDE 展示优化，因为当前实现已经存在 crash/path split，属于 runtime debugger contract 本身的破口。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 A-01 / C-10 / C-12 / D-21` — JIT 路径的 file/line、`this-object` 和 `DebugBreak()` 仍未走同一套执行态模型，自动化也没有覆盖。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT7` — `StaticJIT` 已维护独立 `debugCallStack`，但 `DebugServer` 仍主要消费解释器 context。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L5638-L5644` — `GetAngelscriptExecutionPosition()` 已经优先读 `tld->activeExecution->debugCallStack`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L5671-L5675` — `GetAngelscriptExecutionFileAndLine()` 在 `DebugStack == nullptr` 分支里仍解引用 `DebugStack`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L5700-L5715` — `GetAngelscriptExecutionThisObject()` 仍只从 `asGetActiveContext()` 取 `ThisPointer`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp` `L24-L28` 与 `Core/AngelscriptEngine.cpp` `L5718-L5731` — `DebugBreak()` 经过 `TryBreakpointAngelscriptDebugging()`，而后者只接受解释器 `Context`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` `L193-L216` — `FScopeJITDebugCallstack` 已经显式记录 `Filename`、`FunctionName`、`ThisObject` 和前一帧。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` `L862-L887`、`L1441-L1477` — `StepOver/StepOut` 与 `SendCallStack()` 仍直接围绕 `asGetActiveContext()` / `Context->GetLineNumber()` 组装调试状态。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/IAngelscriptExecutionStateProvider.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/InterpreterExecutionStateProvider.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/JitExecutionStateProvider.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptExecutionStateProviderTests.cpp`
- [ ] **P4.9** 📦 Git 提交：`[ExecutionState] Refactor: unify interpreter and JIT execution-state providers`
- [ ] **P4.9-T** 单元测试：验证解释器与 JIT 执行态都能通过统一 provider 暴露 callstack、location 与 `this`
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptExecutionStateProviderTests.cpp`
  - 测试场景：
    - 正常路径：解释器帧与 JIT 帧都能通过 provider 返回稳定的 `source/line`、top-frame `this` 与 breakpoint stop reason。
    - 边界条件：JIT 仅支持 top-frame 变量时，provider 必须显式报告 capability，而不是伪装成支持深层 locals。
    - 错误路径：`debugCallStack == nullptr`、JIT `DebugBreak()` 或 file/line 查询时，不得崩溃或退回原生 `UE_DEBUG_BREAK()`；必须返回显式 unavailable 结果或通过 debug server 正常暂停。
  - 测试命名：`Angelscript.TestModule.Debugger.ExecutionState.UnifiesInterpreterAndJitCallstackLocationAndThisObject`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.9-T** 📦 Git 提交：`[ExecutionState] Test: cover JIT and interpreter provider parity`

## 本轮追加条目的衔接与边界

1. `P1.14` 聚焦 usable-context 的 owner 与租约恢复，不重复 `P1.13` 的 lifecycle milestone 建模，也不把范围扩成 `Plan_AngelscriptUnitTestExpansion.md` 的测试矩阵扩张。
2. `P1.15` 是对 `P1.13` 的 clone 视图深化：前者解决 readiness truth，后者解决 module/type/lifecycle projection；两者互补，不是重复立项。
3. `P2.14` 先收口 metadata slot 与 query owner，不直接承诺 interface feature closeout，因此不替代 `Documents/Plans/Plan_CppInterfaceBinding.md`。
4. `P2.15` 解决 family/role 的结构化 type tree，补的是 `P1.8 SurfaceSupportProfile` 和 `P2.14 MetadataSlotRegistry` 之间缺失的“类型形状”层，而不是再次发明一套 capability policy。
5. `P4.9` 只收口 runtime 内已有的 JIT/interpreter 执行态模型，不展开 `Documents/Plans/Plan_DebugAdapter.md` 的 DAP/客户端协议工作，也不替代 `Plan_ASDebuggerUnitTest.md` 的现有测试补齐任务。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.14` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPrimaryContextLeaseTests.cpp` | threaded init handoff、多 owner 恢复、clone 不抢占 primary-context lease | 高 |
| `P1.15` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCloneProjectionTests.cpp` | clone module/type/lifecycle 投影、asset-scan 后续同步、stale projection 诊断 | 高 |
| `P2.14` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMetadataSlotRegistryTests.cpp` | typed slot 分槽、container query、legacy slot0 mirror 兼容 | 中高 |
| `P2.15` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTypeShapeTests.cpp` | reference family kind、role-tagged subtype shape、legacy declaration 一致性 | 中 |
| `P4.9` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptExecutionStateProviderTests.cpp` | interpreter/JIT callstack-location-`this` 一致性与 JIT `DebugBreak()` 桥接 | 中高 |

---

## 深化 (2026-04-09 07:41:32)

### Phase 4 补充：调试宿主准入与 hook 所有权

- [ ] **P4.10** 建立 `DebugAdmissionPolicy + EngineAttachRegistry + SessionBootstrapLedger`
  - 当前 `DebugServer` 仍把“发现服务、建立 attach、切换调试态、清空断点、推送 symbol/asset 数据库”压在同一条裸 TCP 流里：新连接先被无条件收下，`RequestDebugDatabase` 直接下发数据库，`StartDebugging` 直接改 `bIsDebugging` 并清空全局断点，而 runtime 侧又只提供 `DebugServerPort` 这一项配置。与此同时，runtime 已经支持 full engine、clone 和多 `GameInstanceSubsystem` 生命周期，但 attach 侧既没有 `EngineInstanceId`，也没有 `readonly/debug` 模式，更没有 “发现后再接管” 的最小准入面。
  - 本条目不重开 `Documents/Plans/Plan_DebugAdapter.md` 的 DAP 客户端实现，而是补 server-side 合同。第一阶段在现有 `V2` 二进制通道上追加 `Hello/HelloAck` 或等价 discovery 包，默认把 legacy 行为收缩到 localhost，并引入 `readonly` 与 `debug` 两类 session mode；第二阶段增加 `EngineAttachRegistry`，让 discovery 能稳定列出 `EngineInstanceId`、label、capability、attach url/session id，旧客户端继续落到 primary engine 兼容路径；第三阶段再引入 `SessionBootstrapLedger`，把 `PauseOnFirstScriptLine`、`WaitForDebugger`、`reattachSessionId`、`bResetBreakpoints` 等启动期语义从破坏性 `StartDebugging` 动作里拆出来。
  - 这样做的直接收益有三类：一是把 remote attach 的 trust boundary 从“谁连上谁能改状态”收口成显式 capability；二是让 multi-engine/clone 场景能按宿主实例精确 attach，而不是共享进程级 `DebugAdapterVersion` 与断点状态；三是为后续可选 `DebugAdapter` bridge、多前端或 remote proxy 留出稳定 discovery 面，而不是继续让客户端靠 today 的副作用顺序猜协议。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 B-11 / B-12` — clone/shared-state 与 current-engine 解析已经证明“调试宿主是谁”不能再靠 wrapper 偶然状态反推。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT8 / Arch-DT11 / Arch-DT29 / Arch-DT42` — 远程调试缺少 admission/discovery、multi-engine attach 路由、trust boundary 与稳定 bootstrap policy。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` `1622-1628` 与 `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `8238-8239`、`14223-14383` — `puerts` 先 discovery 再 attach 的 session contract，以及参考插件对 debugger hook/attach ownership 的显式分层可直接吸收。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` `L395-L399` — `HandleConnectionAccepted()` 只记录日志并把 socket 入队，没有任何准入或模式协商。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` `L402-L406` — `FTcpListener` 直接绑定 `FIPv4Address::Any`，当前默认监听面不是 localhost-only。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` `L822-L827` — `RequestDebugDatabase` 进入后立即推送 `DebugDatabase` 与 diagnostics，没有 attach 前 discovery/readonly contract。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` `L897-L913` — `StartDebugging` 只读取 `DebugAdapterVersion`，随后立刻置 `bIsDebugging=true`、清空断点并加入调试客户端列表。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` `L20-L23` 与 `L103-L115` — `DebugAdapterVersion` 仍是进程级静态量，而 `FStartDebuggingMessage` 只有单一 `DebugAdapterVersion` 字段。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L78` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L529` — runtime 当前只从命令行读取 `DebugServerPort`，没有 bind-address、remote allowlist 或 token 级配置入口。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L144-L152` 与 `L628-L647` — shared state 只有一个 `DebugServer*`，clone 共享底层状态，但 attach 层没有可公开消费的 host identity。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Session/AngelscriptDebugAdmission.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Session/AngelscriptDebugAdmission.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Session/AngelscriptDebugHostRegistry.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Session/AngelscriptDebugHostRegistry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugAdmissionTests.cpp`
- [ ] **P4.10** 📦 Git 提交：`[DebugAdmission] Refactor: add attach discovery, host registry, and bootstrap ledger`
- [ ] **P4.10-T** 单元测试：验证 attach discovery、实例路由与只读/调试模式隔离
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugAdmissionTests.cpp`
  - 测试场景：
    - 正常路径：localhost 客户端先收到 discovery/hello，再以 `debug` 模式 attach 到指定 `EngineInstanceId`，session bootstrap 返回 capability 与稳定 `SessionId`。
    - 边界条件：legacy `V2` 客户端不升级时仍能 attach primary engine；两个 engine/clone 并存时 discovery 同时列出多宿主；`PauseOnFirstScriptLine` 与 `reattachSessionId` 能保留断点状态。
    - 错误路径：remote 客户端未携带 token、请求未知 `EngineInstanceId`、或 `readonly` client 试图发送 `Pause/Continue/Step*` 时，服务端必须显式拒绝且不改变全局调试状态。
  - 测试命名：`Angelscript.TestModule.Debugger.AdmissionAndAttachRouteByEngineInstance`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.10-T** 📦 Git 提交：`[DebugAdmission] Test: cover discovery, attach routing, and readonly session guards`

- [ ] **P4.11** 建立 `DebugHookArbiter + CallbackOwnerRegistry`
  - 当前调试/覆盖率 callback 的“owner 在谁”仍然没有正式合同。底层 `asCContext` 在创建时就被装上全局函数 `LogAngelscriptException`、`AngelscriptLineCallback`、`AngelscriptStackPopCallback` 与 loop-timeout callback，但这些回调真正触发时并不按 `Context->GetEngine()` 或稳定 registry 找 owner，而是重新走 `FAngelscriptEngine::Get()`；与此同时，`UpdateLineCallbackState()` 又把 engine-local 的 `DebugServer`/`CodeCoverage` 状态写回 AngelScript 进程级静态位 `asCContext::CanEverRunLineCallback` / `ShouldAlwaysRunLineCallback`。结果是谁最后更新谁生效，callback 从哪个 engine 发出也不重要，只要 current-engine 刚好指到另一台 wrapper，就会把异常、断点、stack-pop、coverage 全部路由错。
  - 本条目的第一阶段引入 `CallbackOwnerRegistry`，以 `asIScriptEngine*`、`asCContext*` 与 `EngineInstanceId` 为键，显式回答“这个 callback 属于哪台 runtime / 哪个 debug host”；异常、line、stack-pop、并行编译 message callback 都统一从 registry 解析 owner，不再重新走 ambient/current-engine。第二阶段引入 `DebugHookArbiter`，把 debugger、coverage、debug-values、loop-timeout、startup bootstrap 等所有会影响 line callback 的参与者收成一张 state table，并在 engine create/clone/shutdown、session attach/detach、coverage hook on/off 时统一重算全局静态位，而不是继续由单个 engine 的即时判断覆盖全进程。第三阶段再把“owner 缺失 / stale context / unsupported hook combination”变成显式 diagnostics，而不是静默 misroute。
  - 这条线与 `P4.9 ExecutionStateProvider` 互补：`P4.9` 解决解释器/JIT 执行态如何暴露统一观察面，`P4.11` 解决这些观察回调归谁、谁有权打开 line callback、loop-timeout 与 coverage 是否会互相踩踏。若没有 owner registry，前者再完整也会继续被 callback 错绑与全局静态开关污染。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` `发现 B-09 / A-19 / B-12 / D-08` — line callback 开关仍是进程级静态量，调试/覆盖率 callback 也仍依赖 `FAngelscriptEngine::Get()`，现有自动化没有覆盖 cross-engine misroute。
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT42` — attach/bootstrap 仍依赖 line callback 破坏性启停；`Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `1290` — hook ownership 没有跟随 engine/request 走，易产生串扰与测试泄漏。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `8058-8239` — `sluaunreal`/`UnLua` 都把 timeout/debugger hook 冲突显式化，当前插件仍缺同等级的 hook-owner 协商语义。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L243-L257` — context 创建时直接注册全局 exception/line/stack-pop/loop-detection callback，没有 owner 句柄或 registry data。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L5305-L5309` — `LogAngelscriptException(asIScriptContext*)` 通过 `FAngelscriptEngine::Get().DebugServer` 路由异常，而不是从 `Context` 反查 owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L5429-L5460` — `UpdateLineCallbackState()` 把 engine-local 状态直接写进 `asCContext` 两个进程级静态开关。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L5475-L5549` — `AngelscriptLineCallback()` 统一通过 `FAngelscriptEngine::Get()` 取得 `DebugServer` 与 `CodeCoverage`，没有 context-owner 解析。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L5559-L5562` — `AngelscriptStackPopCallback()` 也继续走 `FAngelscriptEngine::Get()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` `L495-L496`、`L573-L574`、`L741-L742`、`L835-L845`、`L855-L875`、`L895-L923` — session 动作多次直接调用 `FAngelscriptEngine::Get().UpdateLineCallbackState()`，没有坚持 `OwnerEngine` 或 host-registry 路由。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/AngelscriptDebugHookArbiter.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/AngelscriptDebugHookArbiter.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/AngelscriptCallbackOwnerRegistry.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/AngelscriptCallbackOwnerRegistry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugHookOwnershipTests.cpp`
- [ ] **P4.11** 📦 Git 提交：`[DebugHooks] Refactor: add callback owner registry and line-hook arbitration`
- [ ] **P4.11-T** 单元测试：验证 callback owner 不再依赖 current-engine，line hook 由统一仲裁器重算
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugHookOwnershipTests.cpp`
  - 测试场景：
    - 正常路径：engine A 触发的 exception/line/stack-pop callback 即使发生在 engine B 的 `FAngelscriptEngineScope` 下，也仍路由到 engine A 的 debug/coverage owner。
    - 边界条件：clone/shared-state、coverage-only、debug-only、`PauseOnFirstScriptLine` 与 loop-timeout 并存时，`DebugHookArbiter` 给出稳定的全局 line-callback 结果，并在 owner 销毁后重新计算。
    - 错误路径：stale context、未注册 owner、或非法 hook 组合时，系统必须输出显式 diagnostics 并安全降级，不能继续误路由到 `FAngelscriptEngine::Get()` 或留下脏静态位。
  - 测试命名：`Angelscript.TestModule.Debugger.HookOwnerRegistryPreventsCrossEngineCallbackMisroute`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.11-T** 📦 Git 提交：`[DebugHooks] Test: cover cross-engine callback routing and line-hook arbitration`

## 本轮追加条目的衔接与边界

1. `P4.10` 是 `Documents/Plans/Plan_DebugAdapter.md` 的 server-side 前置合同，不替代 DAP/VS Code 客户端实现；它只处理 runtime discovery、准入、宿主路由与 bootstrap policy。
2. `P4.10` 和 `P4.11` 都放在 `Phase 4`，与 `todo.md` 当前“已知 blocker 与交付基线优先、架构演进后置”的主线一致；除非 multi-engine 调试已成为当前 blocker，否则不应抢到插件交付、onboarding 入口或 parity 收口之前。
3. `P4.10` 应后置于 `P4.3`/`P4.4` 的 packaging 与 toolchain host 收口，因为 admission/discovery 需要先有稳定的可选 debug 模块边界与工具宿主。
4. `P4.11` 应后置于 `P1.1` 的 runtime identity 与 `P4.9` 的 execution-state provider；前者先固定 engine owner，后者先统一执行态，再由 hook 仲裁器解决“谁有权打开 callback”。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P4.10` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugAdmissionTests.cpp` | discovery/hello、engine-instance attach、readonly/debug mode、重连与 token 拒绝 | 中高 |
| `P4.11` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugHookOwnershipTests.cpp` | cross-engine callback owner、line-hook 重算、stale owner 安全降级 | 中高 |

---

## 深化 (2026-04-09 07:56:12)

### Phase 4 补充：模块边界、公共契约与可选能力 owner

- [ ] **P4.12** 建立 `AngelscriptRuntimeEditorSupport + AngelscriptRuntimeTestSupport` owner
  - 当前 `AngelscriptRuntime` 仍同时承担 shipping runtime、editor-only script glue 和 test-only access seam 三类职责：`Build.cs` 在 editor target 下把 `UnrealEd` 与 `EditorSubsystem` 提升到 runtime public dependency，preprocessor 与 subsystem bind 直接在 runtime 内处理 `UEditorSubsystem`，而 `AngelscriptTest` 继续通过 mirrored include path 和 engine core `friend` access 从 runtime 内部拿测试入口。这样做让“运行时核心边界”与“editor/test 辅助边界”混在同一个 owner 里，后续任何 editor glue 或 white-box helper 都会继续膨胀 `AngelscriptRuntime` 的编译面、依赖面和可见面。
  - 本条目的目标不是一次性重写 editor script 或测试体系，而是先把 owner 关系立起来：新增 `AngelscriptRuntimeEditorSupport` 承接 `UEditorSubsystem` 绑定、editor-only preprocessor glue 和与 `GEditor`/`EditorSubsystem` 相关的运行期桥；新增 `AngelscriptRuntimeTestSupport` 承接 current-engine/test fixture/white-box helper 的正式访问面。`AngelscriptRuntime` 本体只保留最小 extension point 与 bridge interface，`AngelscriptTest` 改为依赖 support owner，而不是继续镜像 runtime 目录结构。
  - 第一阶段允许保留兼容 shim 和少量 `friend` 入口，但新增代码禁止继续把 editor/test seam 写回 `AngelscriptRuntime` 主 owner。这样后续 `P4.13` 的 public contract 收口、`P4.14` 的 editor public DTO 清理以及 `P4.16` 的可选领域 leaf module 才有稳定落点，而不会继续叠在一个 supernode 上。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `Issue-70` 与 `Issue-71` 表明测试 helper/fixture owner 仍不清晰，纯配置 merge helper 也会误入 `FAngelscriptEngineScope`，scenario 脚手架还长期滞留在 compile-failure 文件里。
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-01`、`Arch-MS-57` — 直接指出 runtime/editor/test 职责仍混在少数 supernode 内，public fan-out 与测试支撑面过宽。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — Hazelight / UnLua / UnrealCSharp 都把 editor、toolchain、test 或 extension owner 从 core runtime 边界中显式分出，而不是继续把 editor/test glue 压回主 runtime。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` `L67-L73` — editor target 下仍把 `UnrealEd` 与 `EditorSubsystem` 放进 `PublicDependencyModuleNames`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L28-L30` 与 `L1245-L1253` — runtime preprocessor 直接 include `EditorSubsystem.h`，并为 `UEditorSubsystem` 生成 `EditorSubsystem::GetEditorSubsystem(...)` static glue。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` `L19-L22`、`L43-L54` — runtime bind 直接处理 `UEditorSubsystem` 并依赖 `GEditor` 查询。
    - `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` `L12-L21` — test 模块仍通过 mirrored internal include path 暴露 `Core/Debugger/Dump/Internals/Preprocessor/ClassGenerator`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L458-L470` — engine core 继续堆积 test access `friend`，说明正式 test-support owner 尚未建立。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntimeEditorSupport/AngelscriptRuntimeEditorSupport.Build.cs`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntimeEditorSupport/Private/AngelscriptRuntimeEditorSubsystemBridge.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntimeTestSupport/AngelscriptRuntimeTestSupport.Build.cs`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntimeTestSupport/Public/AngelscriptRuntimeTestHooks.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeSupportBoundaryTests.cpp`
- [ ] **P4.12** 📦 Git 提交：`[ModuleBoundary] Refactor: split editor and test support out of runtime core`
- [ ] **P4.12-T** 单元测试：验证 runtime core 不再默认吞入 editor/test owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeSupportBoundaryTests.cpp`
  - 测试场景：
    - 正常路径：只加载 `AngelscriptRuntime` 时 core boot、compile 与基础 bind 仍可工作；额外加载 `AngelscriptRuntimeEditorSupport` 后，`UEditorSubsystem` glue 才进入可用状态。
    - 边界条件：`AngelscriptRuntimeTestSupport` 在 `IsolatedFull`、`SharedClone` 与 production-like 三种 fixture 下都能提供同一组 white-box hooks，而不需要 mirrored internal include path。
    - 错误路径：缺失 editor-support owner、在 non-editor target 请求 editor subsystem glue、或 test-support 请求无效 engine owner 时，系统返回结构化 unavailable diagnostics，而不是 link failure、裸 `nullptr` 或污染 current-engine 栈。
  - 测试命名：`Angelscript.TestModule.Core.RuntimeSupport.CoreOwnerDoesNotRequireEditorOrTestSupport`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.12-T** 📦 Git 提交：`[ModuleBoundary] Test: cover runtime editor and test support owners`

- [ ] **P4.13** 建立 `RuntimePublicContract + InternalAccessBridge`
  - 当前 runtime 没有真正的最小 public contract。`AngelscriptRuntime.Build.cs` 直接把 `ModuleDirectory`、`Core/` 与 vendored `ThirdParty/angelscript/source` 暴露到 `PublicIncludePaths`，测试辅助头和 interface 测试则继续直接 include `Preprocessor/AngelscriptPreprocessor.h`、`ClassGenerator/ASClass.h`、`source/as_context.h` 以及 `../../AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.h`。这让模块边界退化成“谁记得目录名就能直接摸到 internals”，C++ 消费者依赖的是目录结构和 upstream VM 私有头，而不是插件承诺的稳定 API。
  - 本条目的目标是把“真正允许外部消费的 runtime contract”与“白盒/third-party 访问桥”拆开：新增 `Public/` façade 头与最小 forward types；把 AngelScript private `source/as_*` 的访问收敛到受控 bridge header；同时让 `AngelscriptRuntimeTestSupport` 暴露正式 white-box hooks，替代测试代码散落的 internal include。第一阶段可保留 forwarding shim 和 deprecation 日志，第二阶段再下沉 `PublicIncludePaths`，禁止新增跨模块直接 include `source/as_*`、`ClassGenerator/*`、`Preprocessor/*`。
  - 这样做的价值不是“代码更整洁”而已，而是为后续模块拆分提供静态边界：一旦 runtime public contract 稳定，`P4.14` 的 editor DTO、`P4.15` 的 bind host、`P4.16` 的 feature leaf module 都可以通过 façade 或 support bridge 对接，而不是继续把 internals 当作公共接口。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-08`、`Arch-MS-13` — 直接指出 runtime 把内部目录与 vendored VM 源码外泄，模块边界依赖目录约定而不是 public API。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` 与 `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — UnrealCSharp / UnLua 都采用显式 module dependency 与受控 private bridge，而不是 blanket `PublicIncludePaths`。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` `L15-L22` — `ModuleDirectory`、`Core` 与 `ThirdParty/angelscript/source` 继续放在 `PublicIncludePaths`。
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` `L15-L21` — 测试辅助头直接 include `Preprocessor/`、`ClassGenerator/` 与 `source/as_*` internals。
    - `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` `L1-L6` — 测试仍通过相对路径直接 include runtime binder 内部实现。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` `L1-L5` — 即便位于 `Public/` 树中的实现文件，也继续直接 include `AngelscriptEngine.h`、`ClassGenerator/*` 与 `AngelscriptRuntimeModule.h`。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/AngelscriptRuntimeFwd.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/AngelscriptRuntimeContract.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Private/ThirdParty/AngelscriptInternalBridge.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntimeTestSupport/Public/AngelscriptRuntimeWhiteBoxAccess.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRuntimePublicContractTests.cpp`
- [ ] **P4.13** 📦 Git 提交：`[ModuleBoundary] Refactor: add runtime public contract and internal-access bridges`
- [ ] **P4.13-T** 单元测试：验证消费者改走 façade/test bridge 而不是 runtime internals
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRuntimePublicContractTests.cpp`
  - 测试场景：
    - 正常路径：editor/test 通过 `AngelscriptRuntimeContract` 与 `AngelscriptRuntimeWhiteBoxAccess` 即可完成模块查询、context 查询与 white-box 断言，不再要求直接 include `Preprocessor/` 或 `source/as_*`。
    - 边界条件：legacy forwarding shim 仍可服务过渡期调用点，并且只输出一次可断言 deprecation diagnostics。
    - 错误路径：请求未暴露的 internal view、或在未加载 `AngelscriptRuntimeTestSupport` 的情况下访问 white-box API 时，返回结构化 unavailable 结果，而不是暴露 raw internal pointer。
  - 测试命名：`Angelscript.TestModule.Internals.PublicContract.ConsumersUseFacadeAndTestHooks`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.13-T** 📦 Git 提交：`[ModuleBoundary] Test: cover runtime public facade and white-box bridges`

- [ ] **P4.14** 建立 `EditorRuntimeModelBridge + PublicDtoSurface`
  - `AngelscriptEditor` 当前的 public contract 仍把 runtime/class-generator internals 当签名类型暴露。`BlueprintImpact` public header 直接 include `Core/AngelscriptEngine.h`，并在 public struct / function signature 中使用 `FAngelscriptModuleDesc` 与 `FAngelscriptEngine`；`ScriptEditorMenuExtension.cpp` 甚至放在 `Public/` 树下，且直接监听 `FAngelscriptClassGenerator::OnPostReload` 与 `FAngelscriptEngine::Get().IsInitialCompileFinished()`。这会把 editor shell、runtime reload lane、class generator internals 一起固化到 editor public API 上，后续任何 owner 拆分都会把 public 头一起拉裂。
  - 本条目要做的是“public surface 去 runtime-internals 化”：新增 `AngelscriptEditorRuntimeBridge` 或等价 private owner，负责把 `FAngelscriptEngine` / `FAngelscriptModuleDesc` / class-generator 事件投影成稳定 DTO，例如 `FAngelscriptBlueprintImpactModuleView`、`FAngelscriptEditorWorkspaceView`、`FAngelscriptEditorDiagnosticsView`。`BlueprintImpact`、`ScriptEditorMenuExtension`、后续 authoring workflow 都只在 private bridge 中感知 runtime internals；public 头只暴露 DTO、请求对象和 service interface。
  - 第一阶段不改变功能行为，只替换签名与 owner；第二阶段再把 `Public/EditorMenuExtensions/*.cpp` 等实现文件迁回 `Private/`，并把 `AngelscriptEditor.Build.cs` 的 public deps 收缩到真正的 public shell 所需集合。这样能避免 `Plan_ASBlueprintImpactScanCommandlet.md` 后续实现继续把 runtime internals 固化进 editor public contract。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-06`、`Arch-MS-14` — 指出 `AngelscriptEditor` 的 public deps 与 public header 暴露面过宽，且 public API 直接固化 runtime/class-generator 内部类型。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `D7` 与 `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — 参考插件把 editor shell、generator、runtime bridge 分成稳定 owner，public shell 不直接暴露内部生成/运行时结构。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs` `L12-L26` — editor 模块仍把 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`AssetTools` 一并放进 `PublicDependencyModuleNames`。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h` `L9`、`L55-L67` — public header 直接 include runtime core，并公开 `FAngelscriptModuleDesc` / `FAngelscriptEngine`。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` `L1-L5`、`L25-L43` — 位于 `Public/` 树的实现文件直接 include runtime/class-generator internals，并绑定 reload/initial-compile 状态。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/RuntimeBridge/AngelscriptEditorRuntimeBridge.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/RuntimeBridge/AngelscriptEditorRuntimeBridge.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactDto.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Workspace/AngelscriptEditorWorkspaceView.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorPublicSurfaceTests.cpp`
- [ ] **P4.14** 📦 Git 提交：`[EditorContracts] Refactor: route editor public API through DTO bridge`
- [ ] **P4.14-T** 单元测试：验证 editor public surface 不再直接暴露 runtime/class-generator internals
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorPublicSurfaceTests.cpp`
  - 测试场景：
    - 正常路径：`BlueprintImpact` 扫描与 menu extension 注册都通过 private runtime bridge 驱动，public 只消费 DTO/request。
    - 边界条件：runtime 尚未初始化、或 editor bridge 只拿到空 workspace 时，public API 仍返回稳定的 empty view + diagnostics，而不是要求外部传入 `FAngelscriptEngine`。
    - 错误路径：reload 后 bridge 指向 stale module/runtime state 时，public surface 输出结构化错误并拒绝继续持有过期 internal pointer。
  - 测试命名：`Angelscript.TestModule.Editor.PublicSurface.UsesDtoBridgeWithoutRuntimeInternals`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.14-T** 📦 Git 提交：`[EditorContracts] Test: cover DTO bridge and editor public surface isolation`

- [ ] **P4.15** 建立 `StableBindHost + ModuleRoleManifest + PrivateShardAbi`
  - 现在的 legacy bind generator 仍把“编译分片策略”直接做成“对外可见模块边界”：`GenerateNativeBinds()` 以 `10` 个 package key 一组生成 `ASRuntimeBind_* / ASEditorBind_*` 模块名，runtime/editor 归属由 `HeaderPath.Contains("Editor/")` 决定，`GenerateBuildFile()` 再把分组得到的 `ModuleList` 原样写进 shard `Build.cs`。运行时启动时只从 `BindModules.Cache` 读取字符串名单并逐个 `LoadModule(...)`。这意味着 shard 序号、路径启发式和当前反射到的 package 集，会一起决定 public-looking module identity；UHT/automation 也因此容易把 `_000.cpp`、`Win64/UnrealEditor` 目录这类当前布局误当正式合同。
  - 本条目要把“bind owner”与“内部分片”拆开：新增固定 owner，例如 `AngelscriptGeneratedBindsRuntime` / `AngelscriptGeneratedBindsEditor`（或等价 provider owner），对外只暴露稳定 host；内部 numbered shard 退为 host 私有 source partition，由 `BindHostManifest.json` / `ModuleRoleManifest.json` 描述 `OwnerId`、`PartitionId`、`Role(Runtime/Editor)`、`SourcePackage`、`Hash/Revision`。`HeaderPath.Contains("Editor/")` 只作为迁移期 fallback，runtime 则优先按 stable owner + manifest 加载，不再把 shard 名当公共 ABI。
  - 这条线不是重复已有 `P4.1` / `P4.4`，而是把它们落到 bind generation 这一块最脆弱、最 worktree-sensitive 的 owner 上。等 bind host 稳定后，`P4.16` 的可选能力 leaf module 才能注册到语义 owner，而不是继续借编号 shard 和路径启发式挂进去。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `Issue-58` 与 `Issue-68` 直接暴露现有测试对 shard 文件名与 UHT 输出目录布局的脆弱耦合，说明 generator layout 仍被误当正式合同。
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-15`、`Arch-MS-16`、`Arch-MS-17`、`Arch-MS-58` 与 `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-5`、`Arch-BP-7` — 指出模块角色 authority、依赖闭包和 shard public ABI 仍不稳定。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` 与 `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — UnrealCSharp / UnLua 都把 generated artifact 粒度与 UE module owner 分离，当前插件仍把编号 shard 暴露给 runtime/cache/test。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L999-L1077` — `GenerateNativeBinds()` 继续生成 `ASRuntimeBind_* / ASEditorBind_*` 并把结果写入 `BindModules.Cache`。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L1136-L1158` — runtime/editor 归属仍由 `HeaderPath.Contains("Editor/")` 启发式决定。
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` `L1166-L1206` 与 `L1214-L1265` — generator 继续在 `Source/` 下创建新模块目录，并把分组结果直接转成 shard `Build.cs` 依赖。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1477-L1488` — runtime 启动时只按 `BindModules.Cache` 的字符串模块名逐个 `LoadModule(...)`。
    - `Plugins/Angelscript/Angelscript.uplugin` `L18-L33` — 插件声明层仍只有 `AngelscriptRuntime / AngelscriptEditor / AngelscriptTest` 三个 checked-in owner，synthetic shard 没有正式拓扑身份。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Angelscript.uplugin`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/GeneratedBinds/BindHostManifestWriter.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindHostManifest.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindHostManifest.cpp`
    - 新增 `Config/AngelscriptBindModuleRoles.json`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindHostManifestTests.cpp`
- [ ] **P4.15** 📦 Git 提交：`[BindHost] Refactor: replace numbered shard modules with stable bind hosts and role manifest`
- [ ] **P4.15-T** 单元测试：验证 bind owner 稳定、reshard 不再改变对外契约
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindHostManifestTests.cpp`
  - 测试场景：
    - 正常路径：同一批 class/package 经过不同 reshard 后，runtime 仍按稳定 `OwnerId` / `Role` 加载 bind host，外部不再感知 numbered shard 变化。
    - 边界条件：`ModuleRoleManifest` 缺项时允许回退 legacy cache 并输出 warning；runtime/editor 归属由 manifest 决定，而不是依赖 header path 包含 `Editor/`。
    - 错误路径：manifest 与 legacy cache 冲突、role 不一致、或 stale numbered shard 被误引用时，loader 必须显式拒绝并报告冲突原因，而不是继续静默 `LoadModule(...)`。
  - 测试命名：`Angelscript.TestModule.Bindings.BindHostManifest.StableOwnersSurviveReshardAndRoleChanges`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.15-T** 📦 Git 提交：`[BindHost] Test: cover stable owners, role manifest, and reshard parity`

- [ ] **P4.16** 建立 `CapabilityLeafModules + OptionalFeatureBridge`
  - 当前 `GameplayAbilities`、`GameplayTasks`、`EnhancedInput` 仍是主 runtime 的硬依赖。插件描述符默认启用这些插件，runtime `Build.cs` 直接依赖对应模块，`UAngelscriptAbilityTaskLibrary` public header 进一步把大量 ability-task 类型直接 include 到 core runtime contract 里，GAS / EnhancedInput bind 也继续作为 `AngelscriptRuntime/Binds` checked-in 源文件存在。结果是 core runtime 对 optional gameplay/input feature 的编译面、包依赖与 public fan-out 过宽，任何后续 feature bind 都容易继续塞回同一个 supernode。
  - 本条目不扩大功能覆盖，也不改变现有脚本默认可用面；它只重构 owner：先新增 `AngelscriptGameplayBinds`、`AngelscriptEnhancedInputBinds` 两个 leaf runtime module，把相关 bind 注册与 support glue 迁出 `AngelscriptRuntime`；第一阶段保持它们在 `.uplugin` 中默认启用，兼容现有工程。第二阶段再评估 `UAngelscriptAbilityTaskLibrary` 等 public wrapper 是否要迁入 feature owner，若涉及 `/Script/<Module>` 包名变化，则通过 alias/bridge 单独灰度，不与第一阶段 owner 调整捆绑。
  - 这样可以把“core runtime 基座”与“高层 feature bind 面”拆开：以后新增 GAS/EnhancedInput/UI 这类高耦合能力，先判断应落哪个 leaf owner，而不是继续修改 `AngelscriptRuntime.Build.cs` 与主插件固定依赖；同时 bind coverage/manifest 也能按 feature owner 报告能力开关，而不只是以 runtime 总体口径统计。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` `Arch-MS-03`、`Arch-MS-57` — 明确指出 GAS / EnhancedInput 对 core runtime 的硬耦合，以及 `AngelscriptRuntime` / `AngelscriptEditor` 的 public fan-out 仍然过宽。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `D1` — Hazelight 把 `EnhancedInput` / `GAS` 放到外挂插件，UnLua / UnrealCSharp 也把 optional capability 通过 leaf owner 或上层模块接入，而不是压进 core runtime。
  - 源码验证：
    - `Plugins/Angelscript/Angelscript.uplugin` `L35-L47` — `StructUtils`、`EnhancedInput`、`GameplayAbilities` 仍是主插件固定启用依赖。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` `L45-L65` — runtime 继续直接依赖 `EnhancedInput`、`GameplayAbilities`、`GameplayTasks` 等 optional feature 模块。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h` `L5-L39` — runtime public header 直接 include 大量 GAS task 类型。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp` `L1-L15` — GAS bind 仍是 runtime checked-in bind owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp` `L1-L46` — EnhancedInput bind 仍直接在 runtime 主 owner 内实现。
  - 涉及文件：
    - `Plugins/Angelscript/Angelscript.uplugin`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptGameplayBinds/AngelscriptGameplayBinds.Build.cs`
    - 新增 `Plugins/Angelscript/Source/AngelscriptGameplayBinds/Private/AngelscriptGameplayBindsModule.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEnhancedInputBinds/AngelscriptEnhancedInputBinds.Build.cs`
    - 新增 `Plugins/Angelscript/Source/AngelscriptEnhancedInputBinds/Private/AngelscriptEnhancedInputBindsModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFeatureLeafModuleTests.cpp`
- [ ] **P4.16** 📦 Git 提交：`[FeatureLeafModules] Refactor: move gameplay and input binds behind optional leaf owners`
- [ ] **P4.16-T** 单元测试：验证 optional feature 只影响对应 leaf owner，不再扩大 core runtime 边界
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFeatureLeafModuleTests.cpp`
  - 测试场景：
    - 正常路径：启用 `AngelscriptGameplayBinds` / `AngelscriptEnhancedInputBinds` 时，现有 GAS / EnhancedInput 代表性脚本 API 仍保持可编译、可执行、可查询。
    - 边界条件：关闭单个 leaf owner 时，只对应 feature surface 变为 unavailable；core runtime 初始化、非相关 bind 与测试夹具保持正常。
    - 错误路径：底层 UE plugin 缺失、leaf owner manifest 未声明、或 feature owner 与 core runtime 版本不兼容时，系统输出稳定 `ReasonCode`/capability diagnostics，而不是让 `AngelscriptRuntime` 整体加载失败。
  - 测试命名：`Angelscript.TestModule.Bindings.FeatureLeafModules.IsolateOptionalGameplayAndInputSurfaces`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P4.16-T** 📦 Git 提交：`[FeatureLeafModules] Test: cover optional gameplay and input leaf owners`

## 本轮追加条目的衔接与边界

1. `P4.12` 与 `P4.13` 是这一轮其余条目的前置边界整理：如果 runtime 仍继续吞入 editor/test owner，并通过宽 include 面向外泄漏 internals，后续 `P4.14`、`P4.15`、`P4.16` 会继续落回 supernode。
2. `P4.14` 只处理 editor public contract 和 owner 隔离，不重复 `Documents/Plans/Plan_ASBlueprintImpactScanCommandlet.md` 的扫描功能扩展；后者解决“做什么”，本条解决“通过谁对外暴露”。
3. `P4.15` 是对既有 `P4.1 ModuleLayerContract` 与 `P4.4 ToolchainProgramHost` 的 bind-generation 落地深化，不重复 `Documents/Plans/Plan_BindParallelization.md` 的并行调度主题；同时它应作为 `Documents/Plans/Plan_OpportunityIndex.md` 中 `Plan_BindShardConsolidation` 占位方向的架构前置。
4. `P4.16` 只移动现有 GAS / EnhancedInput owner，不替代 `Documents/Plans/Plan_UEBindGapRoadmap.md`、`Documents/Plans/Plan_HazelightCapabilityGap.md` 的功能差距收口；它解决的是“能力属于哪个模块”，不是“再补哪些 API”。
5. 这 5 个条目都属于长期架构收口，优先级应服从 `todo.md` 与 `Documents/Plans/Plan_StatusPriorityRoadmap.md` 的主线：先已知 blocker 与交付基线，再 workflow/onboarding，然后才进入本轮 Phase 4 owner 重构。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P4.12` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeSupportBoundaryTests.cpp` | core/runtime 与 editor-support/test-support owner 分离；editor glue 按模块显式启用 | 中高 |
| `P4.13` | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRuntimePublicContractTests.cpp` | façade/test bridge 替代 direct internal include；legacy shim 过渡与 unavailable diagnostics | 中高 |
| `P4.14` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorPublicSurfaceTests.cpp` | DTO bridge 隔离 `BlueprintImpact`/menu extension 与 runtime/class-generator internals | 中高 |
| `P4.15` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindHostManifestTests.cpp` | stable bind host、role manifest、reshard 不变对外 owner、legacy dual-read 诊断 | 高 |
| `P4.16` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFeatureLeafModuleTests.cpp` | gameplay/input leaf owner 开关、feature unavailable reason、core runtime 不受牵连 | 中高 |

---

## 深化 (2026-04-09 08:07:06)

### Phase 1 补充：高阶 helper 合同与全 family bind 产物

- [ ] **P1.16** 建立 `InvocationContract + AugmentationDescriptor`
  - 当前 `Bind_AActor`、`Bind_UObject`、`Bind_BlueprintType` 里的高阶 helper 仍靠“先注册，再追写上一条 bind”的 side effect 运转：`GetComponentsByClass` / `GetAllActorsOfClass*` 在 lambda 里手解 `TypeId/templateSubTypes/plainUserData`，`NewObject` / `SpawnActor` 再通过 `SetPreviousBindArgumentDeterminesOutputType()` 追写 output-type，`StaticClass()` 与 `ActorType::Spawn()` 则依赖 `FNamespace` + `PreviousBindPassScriptFunctionAsFirstParam()` 临时增生 per-type augmentation。这样 generic 参数约束、return-type coupling、script-function capture mode 和 namespace augmentation 都没有进入正式 contract，manifest / dump / UHT sidecar 只能看见基础声明，看不见真正决定行为的 helper 语义。
  - 本条目不追求把所有手写 binder 变成万能 IR，而是先把重复模式收口成可增量落地的描述层：新增 `FAngelscriptInvocationContract` 表达 `GenericParamKinds`、`TemplateBaseRequirement`、`RequiredBaseClass`、`OutputTypeSourceArg`、`NeedsScriptFunctionFirstParam`、`WorldContextSource`；新增 `FAngelscriptAugmentationDescriptor` 表达“这个 API 挂到哪个 namespace/type、来自哪个 provider/lane”。第一阶段只覆盖 `GetComponentsByClass`、`GetAllActorsOfClass*`、`NewObject`、`SpawnActor`、`StaticClass()`、`ActorType::Spawn()` 六类模式，并让 legacy `SetPreviousBind*` API 退化成“补当前 descriptor”的兼容层，而不是继续依赖 `PreviouslyBoundFunction` 这个进程级可变槽位。
  - 这样 `P1.9 BindPhasePlan` 才能把 `普通 method / generic helper / per-type augmentation` 区分成稳定 lane，`P1.2` 与 `P4.2` 的 manifest / receipt 也才能真正描述 helper API，而不是只覆盖 class/struct reflective lane。它同样给项目侧扩展 provider 留下正式入口，避免外部模块继续直接解析 `asCTypeInfo`、复制 `PreviousBindPassScriptFunctionAsFirstParam()` 或把 augmentation 语义埋进不可审计 lambda。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` `发现 10 / 66 / 83` — spawn 审核点缺口、`GetComponentsByClass` 追加语义漂移、delegate raw helper 泄露都说明高阶 helper 仍停留在手写 live lambda + side-effect 模式。
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` `NewTest-2`、`Issue-71` — actor/world helper 需要 world-backed 行为回归，现有测试却抓不住 generic out-array 与 helper contract。
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-17` — 明确要求把 generic 参数约束、return-type coupling 与 per-type augmentation 提升为可序列化 descriptor。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `[维度 D8]` 与 `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` `[维度 D7]` — 参考插件都把 extension/augmentation 先沉成生成物或 registry，再由 runtime 消费，而不是只靠手写 runtime lambda。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` `L39-L67`、`L89-L148` — `GetComponentsByClass` 两个 overload 仍直接解析 `templateBaseType/templateSubTypes[0].plainUserData`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` `L286-L312`、`L317-L402`、`L450-L467` — `ActorType::Spawn()`、`GetAllActorsOfClass*()` 与 `SpawnActor()` 仍靠 runtime augmentation、手写 generic 约束和 `SetPreviousBindArgumentDeterminesOutputType()` 追写元数据。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` `L61-L72`、`L556-L579` — `GetTypedOuter()` 与 `NewObject()` 仍在注册后通过 `SetPreviousBindArgumentDeterminesOutputType()` 补 output-type，helper 语义不在独立 contract 中。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `L684-L694`、`L1154-L1250` — `StaticClass()` 与 generated property accessor 继续依赖 `PreviousBindPassScriptFunctionAsFirstParam()`、deprecated/editor-only/generated trait 的后置追写。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` `L627-L640` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` `L377-L398`、`L409-L433` — bind core 仍把 helper 语义暴露为 `GetPreviousBind()` 系列 process API，并以 `PreviouslyBoundFunction` 保存最后一条 bind。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h` `L56-L87` — `FAngelscriptMethodBind` 目前只保存基础声明字段，没有 generic 参数约束、augmentation 来源或 script-function capture mode。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptInvocationContract.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptInvocationContract.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAugmentationDescriptor.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAugmentationDescriptor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindInvocationContractTests.cpp`
- [ ] **P1.16** 📦 Git 提交：`[BindingContracts] Refactor: add invocation contracts and augmentation descriptors for high-order helpers`
- [ ] **P1.16-T** 单元测试：验证高阶 helper 不再只依赖 previous-bind side effect
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindInvocationContractTests.cpp`
  - 测试场景：
    - 正常路径：`GetComponentsByClass`、`GetAllActorsOfClass`、`NewObject`、`SpawnActor`、`StaticClass()`、`ActorType::Spawn()` 都产出稳定 `InvocationContract/AugmentationDescriptor`，并保留当前脚本行为。
    - 边界条件：legacy `SetPreviousBindArgumentDeterminesOutputType()`、`PreviousBindPassScriptFunctionAsFirstParam()` 与 generated accessor 仍能回填到 descriptor，不要求一次性重写所有 binder。
    - 错误路径：helper 注册缺失 `OutputTypeSourceArg`、generic base class 不匹配、或 augmentation 声明缺少 target/provider 时，系统输出显式 diagnostics，而不是静默丢失 metadata。
  - 测试命名：`Angelscript.TestModule.Bindings.InvocationContract.DescribesGenericAndAugmentedHelpers`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.16-T** 📦 Git 提交：`[BindingContracts] Test: cover invocation contracts and augmentation descriptors`

- [ ] **P1.17** 建立 `FamilyCompleteBindingArtifactCatalog`
  - 当前 `Binds.Cache` 的正式持久化合同只覆盖 `Classes/Structs` 两个 family，`UEnum`、delegate 和 `Bind_UObject` / `Bind_AActor` 里那些脚本高频 global/namespace helper 仍主要停留在 live bind lane：它们最多只在 `.Headers` sidecar 留一条 header link，或者完全没有任何 artifact entry。结果是“完整脚本 API 面”无法通过单一产物审计，docs/dump/coverage/provider audit 也必须继续为 `Enum/Delegate/GlobalFunction/NamespaceFunction` 做 family 特判。
  - 本条目是对 `P1.2 BindingManifest` 与 `P4.2 ArtifactReceipt` 的 family 深化，不推翻现有 `Binds.Cache` 主路径。第一阶段新增轻量 `FAngelscriptBindingArtifactEntry` 与 `EAngelscriptBindingArtifactKind`，把 `Class`、`Struct`、`Enum`、`Delegate`、`GlobalFunction`、`NamespaceFunction`、`ManualType` 至少纳入同一 sidecar catalog；`Bind_UEnum.cpp`、`Bind_Delegates.cpp`、`BindGlobalFunction()` 与 per-type augmentation 在注册成功后额外写 entry，cook/editor 保持 dual-write 旧 `Binds.Cache`，runtime 只把新 catalog 用于 docs/dump/coverage/provider audit，不立即改变真实绑定加载。
  - 这样可以让“新增一个 enum / delegate / global helper”至少先进入正式 artifact family，而不是继续只有 class/struct 才有离线合同。后续若要让 `P1.16` 的 helper descriptor、`P1.2` 的 provider manifest、`P4.2` 的 artifact receipt 互相 join，就必须先把这些 family 提升到同一可追踪层级。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` `发现 10 / 83` — spawn/delegate 这类 live-only helper 已经出现审核点丢失和内部协议外泄，说明非 class family 缺少同级 artifact 约束。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-11`、`Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` `NewTest-54 / Issue-7` — 现有 bind DB 测试只 round-trip class/struct 与 headers，enum/delegate/helper family 仍靠弱覆盖或零覆盖存活。
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-18` — 明确要求把 `Enum/Delegate/GlobalHelper` 提升成与 `Class/Struct` 同级的 artifact family。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `707-707`、`1551-1551` 与 `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` 的 `FExported` family — 参考插件都把 `class/function/enum/type` 视作同一份 exported / artifact contract，而不是让 enum/function 永远停留在 live-only 边缘状态。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` `L27-L31`、`L65-L98`、`L103-L115` — `Serialize()` 仍只持久化 `Structs/Classes`，`BoundEnums/BoundDelegateFunctions` 只参与 `.Headers` sidecar，加载阶段也不会恢复成可消费 artifact entry。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h` `L132-L137` — bind DB 的持久化主表仍只有 `Structs`、`Classes`，其余 family 只保存在内存数组。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp` `L358-L385` — enum 绑定完成后仅 `Register(MakeShared<FEnumType>(Enum))` 并把 `Enum` 追加到 `BoundEnums`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` `L432-L439` — delegate 声明完成后仅把 `Function` 追加到 `BoundDelegateFunctions` 并注册 live type。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` `L556-L579` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` `L303-L312`、`L450-L467` — `NewObject`、`ActorType::Spawn()`、`SpawnActor()` 等高频 helper 直接 `BindGlobalFunction(...)`，没有任何 DB 结构体或 artifact entry。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingArtifactCatalog.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingArtifactCatalog.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingArtifactCatalogTests.cpp`
- [ ] **P1.17** 📦 Git 提交：`[BindingArtifacts] Refactor: add family-complete binding artifact catalog`
- [ ] **P1.17-T** 单元测试：验证 enum/delegate/helper family 进入统一 artifact catalog
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingArtifactCatalogTests.cpp`
  - 测试场景：
    - 正常路径：class、struct、enum、delegate、global helper、namespace helper 六类代表样本都能写入同一 artifact catalog，并与 bind dump/docs/provider audit 对齐。
    - 边界条件：只有 legacy `Binds.Cache` 而没有新 sidecar 时，runtime 继续保持现有绑定行为，同时显式记录 `artifactCatalog=legacy-only`。
    - 错误路径：artifact key 冲突、family kind 与 live bind observation 不一致、或缺失 `OwnerOrNamespace/ProviderName` 等 join 字段时，系统输出结构化 diagnostics，而不是静默写出半残 sidecar。
  - 测试命名：`Angelscript.TestModule.Engine.BindingArtifactCatalog.CoversEnumDelegateAndHelperFamilies`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.17-T** 📦 Git 提交：`[BindingArtifacts] Test: cover family-complete artifact catalog`

## 本轮追加条目的衔接与边界

1. `P1.16` 是对既有 `P1.9 BindPhasePlan` 的 helper/augmentation 深化，不重复 `Documents/Plans/Plan_UFunctionReflectiveFallbackBinding.md` 的 backend 路线选择，也不替代 `Documents/Plans/Plan_UEBindGapRoadmap.md` 的 API 覆盖补齐；它解决的是“高阶 helper 如何被正式描述”，不是“再补哪些 helper”。
2. `P1.17` 是对 `P1.2 BindingManifest` 与 `P4.2 ArtifactReceipt` 的 family 范围补完，不重复 `Documents/Plans/Plan_UhtPlugin.md` 的 exporter 主链，也不重复 `Documents/Plans/Plan_BindParallelization.md` 的分片并行与调度；它解决的是“哪些 family 进入同一 artifact contract”，不是“UHT 如何更快生成”。
3. 这两项都应后置于 `todo.md` 与 `Documents/Plans/Plan_StatusPriorityRoadmap.md` 的主线优先级，只在已知 blocker、交付基线与 onboarding 入口稳定后推进；在架构子序列里，它们也应排在 `P1.2` 和 `P1.9` 之后，否则只会再生成两份平行 sidecar。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.16` | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBindInvocationContractTests.cpp` | generic helper、output-type coupling、per-type augmentation 的结构化 descriptor 与兼容 shim | 中高 |
| `P1.17` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingArtifactCatalogTests.cpp` | class/struct/enum/delegate/global/namespace helper 同级 artifact catalog 与 legacy fallback | 中高 |

---

## 深化 (2026-04-09 08:12:38)

### Phase 1 输入边界校正

1. 为严格对齐本轮指定输入集，`P1.16` 的执行依据以 `[A] Documents/AutoPlans/BindSystem_Analysis.md`、`[D] Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md`、`[E] Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` 与 `Documents/AutoPlans/ReferenceComparison/puerts_Analysis.md` 为准；前一轮段落中出现的 `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` 不作为本轮立项依据，也不作为后续拆解时的引用源。
2. `P1.17` 的测试覆盖维度只保留 `[C] Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-11`；前一轮段落里并列写入的 `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` 同样不作为本轮输入来源。后续若要补充其他 `TestCoverage` 证据，应在独立轮次先显式纳入输入范围，再追加深化。
3. 上述校正不改变 `P1.16`、`P1.17` 的优先级、源码验证、涉及文件与 `-T` 测试任务；本轮只收窄来源边界，不扩展执行范围。

---

## 深化 (2026-04-09 08:27:33)

### Phase 2 深化：qualified identity 与 runtime descriptor owner

- [ ] **P2.16** 建立 `QualifiedSymbolKey + QualifiedInterfaceRef`
  - 当前 `P2.2` 已经给出了 canonical symbol key 的总方向，但 `ClassGenerator` 自己仍保留一套 namespace-blind 的落地实现：`DataRefByName`、`GetClassDesc()`、`FAngelscriptModuleDesc::GetClass()` 和 `GetUnrealName()` 继续只看短名，`ResolveInterfaceClass()` 也把 `ImplementedInterfaces` 当字符串处理，并在最后一步回退到 stripped-name + `TObjectIterator<UClass>`。这意味着 `Namespace + ClassName` 只在拿 `asITypeInfo` 时短暂存在，真正决定 `UASClass` 命名、reload replace、接口装配与冲突诊断的仍是短名 heuristic。
  - 本条不重复 `P2.2` 的整体 symbol/source canonicalization，而是把它落实到动态类生成这条最脆弱的 consumer 链。第一阶段在插件内新增 `FAngelscriptQualifiedSymbolKey` / `FAngelscriptQualifiedInterfaceRef`（命名可调整），至少包含 `Namespace`、`ShortName`、`Kind(Class/Interface/Struct/Delegate)`、`OwnerModule` 与 revision；`DataRefByName`、`ImplementedInterfaces`、旧类查找和 interface finalize 统一优先读 qualified key。第二阶段再把 short-name fallback 降级成仅用于 legacy 兼容与错误信息的人类可读别名，并在发现同短名冲突时显式报诊断，不再 silent 命中第一个 `UClass`。
  - 这条线仍严格遵守当前项目约束：不改 Unreal Engine、不要求 wholesale 升级 AngelScript 2.38，也不改现有脚本语法；namespaced interface 如仍需 `RegisterObjectType()` 占位，可继续保留，但 owner 必须收敛到一份 qualified authority，而不是让 `Preprocessor`、`ClassGenerator` 和 engine type table 各自决定。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 26 / 27 / 53 / 55 / 56 / 57` — namespaced class/interface、`A/U` 前缀折叠与短名 interface fallback 都已确认会互相覆盖，而且当前自动化几乎没有保护。
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-07`、`Arch-TS-26`、`Arch-TS-36`、`Arch-TS-45` — canonical key、`ImplementedInterfaces` identity seam、stripped-name heuristic 与 reload version chain 仍未收敛。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` `3317-3325`、`4399-4404` 与 `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `7454-7555` — 参考实现都先稳定 interface/type symbol authority，再让 class materialization 消费；当前插件也更适合先补 stable symbol key，而不是继续扩张短名猜测。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` `L141-L142`、`L216-L242` — generator 当前仍以 `DataRefByName` 和 `GetClassDesc(const FString& ClassName)` 为主 lookup，只有 `GetNamespacedTypeInfoForClass()` 是 namespaced。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L126-L156`、`L159-L170` — `EnsureClassAnalyzed()` / `GetClassDesc()` 继续按短名查找，`GetUnrealName()` 继续剥 `U/A/F` 前缀。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L1759-L1766`、`L2570-L2578` — 新 class 注册与 full reload replace 仍以 `ClassName -> UnrealName` 为键。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L5063-L5108`、`L5142-L5148` — `ResolveInterfaceClass()` 仍以 `InterfaceName` 字符串驱动，并在线性扫描里用 stripped-name 命中第一个 `UClass`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L5912-L5934` — AngelScript 侧其实已经有 namespace-aware `TypeInfo` 查询，但这份身份没有贯穿到 UE 侧。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` `L1104-L1109`、`L1187-L1198`、`L1336-L1344` — `ImplementedInterfaces` 仍只存 `FString`，`AreFlagsEqual()` 与 module-local `GetClass()` 也继续按短名比较。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptQualifiedSymbolKey.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptQualifiedSymbolKey.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptQualifiedInterfaceRef.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptQualifiedInterfaceRef.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptQualifiedSymbolIdentityTests.cpp`
- [ ] **P2.16** 📦 Git 提交：`[QualifiedIdentity] Refactor: add namespace-qualified symbol keys and interface refs for class generation`
- [ ] **P2.16-T** 单元测试：验证 namespaced class/interface 不再在 class generator 与 reload 路径上互相覆盖
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptQualifiedSymbolIdentityTests.cpp`
  - 测试场景：
    - 正常路径：同一轮编译里同时声明两个不同 namespace 但同短名的 script class / script interface，验证 `StaticClass()`、`ImplementedInterfaces`、`GetClassDesc()` 与 reload head 都指向各自的 qualified symbol。
    - 边界条件：同模块出现 `AFoo` / `UFoo` 这类 prefix-collapsed 名称，qualified key 仍能区分 logical symbol，legacy short-name alias 只作为兼容 lookup 并输出 warning。
    - 错误路径：只给出短名且存在多重命中、或 interface fallback 命中多个 native `UInterface` 时，编译阶段输出稳定 diagnostics，而不是 silent 命中第一个 `TObjectIterator<UClass>` 结果。
  - 测试命名：`Angelscript.TestModule.ClassGenerator.QualifiedIdentity.SeparatesNamespacedTypesInterfacesAndReloadHeads`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.16-T** 📦 Git 提交：`[QualifiedIdentity] Test: cover namespaced symbol collisions and interface resolution`

- [ ] **P2.17** 建立 `ClassRuntimeDescriptorRegistry + ScriptTypeMirror`
  - `UASClass` 仍同时承担“UE 反射壳”“脚本类型句柄”“构造/析构入口”“源码/模块索引”“GC schema cache”“debug value 宿主”六类职责：`ScriptTypePtr` 驱动虚调用、析构、source path 和 developer-only 判断，`ReferenceSchema` 由 class generator 直接追加回写，`bIsScriptClass` 也在类生成期直接写壳对象。结果是 class shell 上的可变字段既是 type identity，又是 runtime behavior，又是 tool/source metadata；一旦 reload/remove 发生半步更新，就会出现 live `UObject`、`GetObjectType()`、GC schema 与 source lookup 不同代的问题。
  - 本条不是去改引擎，也不把脚本类 carrier 从 `UASClass` 挪走。第一阶段只在插件内补一层 engine-owned `ClassRuntimeDescriptorRegistry`：以 `UClass*` / `asITypeInfo*` / qualified symbol revision 为双索引，集中保存 `ScriptType`、construct/defaults policy、source/module metadata、reference-schema snapshot、debug-value prototype 与 remove/reload state；`UASClass` 现有字段暂时退化成 mirror/fallback。第二阶段再让 `ResolveScriptVirtual()`、`RuntimeDestroyObject()`、`GetSourceFilePath()`、`IsDeveloperOnly()`、`asIScriptObject::GetObjectType()`、reference-schema rebuild 优先走 descriptor，而不是直接依赖壳对象上那几根裸字段。
  - 这样可以把 `P2.2` 的 symbol identity、`P1.15` 的 clone projection、未来 interface shell/value bridge 与 GC schema 继续放在插件 owner 内解决，不触碰 Unreal Engine 主干补丁，也不要求立刻扩大 `Plan_InterfaceBinding.md` / `Plan_CppInterfaceBinding.md` 的功能闭环。它先解决的是“谁拥有 runtime metadata 与 epoch”，不是“再支持哪些 interface 能力”。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 8 / 10 / 46 / 67 / 74 / 75` — `ReferenceSchema` / `ScriptTypePtr` / `bIsScriptClass` 相关状态在 reload、remove、GC 与旧实例窗口里都已暴露出 owner 分裂风险。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `NewTest-45` — 当前没有任何测试直接保护 script object 的 `GetObjectType()` 与生成类 runtime metadata 的一致性。
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-03`、`Arch-TS-10`、`Arch-TS-18` — 脚本类型身份仍靠 `ScriptTypePtr / GetUserData()` 双向回填，`UASClass` 责任过载，reference owner 也还压在 class shell 上。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` `9340-9566` 与 `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `8804-9106` — 参考插件把 runtime descriptor / registry 生命周期建成独立 owner，同时仍能让 UE GC 保持最终 authority；当前插件也应吸收“descriptor registry 收口，但不退回 VM-side keepalive 为主”的做法。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` `L18-L35` — `UASClass` 当前直接持有 `CodeSuperClass`、`ConstructFunction`、`DefaultsFunction`、`ComposeOntoClass`、`ScriptTypePtr`、`bIsScriptClass` 与 `ReferenceSchema`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L104-L120`、`L965-L977`、`L1037-L1048`、`L1137-L1146` — 虚调用解析、析构、对象分配与构造流程都直接依赖 `ScriptTypePtr` / `GetUserData()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L1497-L1532` — source path、relative path 与 developer-only 判断仍通过 `ScriptTypePtr -> Module` 即席回查。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L3290-L3291`、`L3677-L3680`、`L4200-L4203` — class generator 在多个 phase 直接写 `bIsScriptClass` 与 `ScriptTypePtr`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L4875-L4924`、`L4990-L4996` — reference schema 与 removed-class cleanup 也继续直写 class shell 字段。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` `L210-L227`、`L291-L302` — type usage 读取 `ScriptClass->GetUserData()` 与 `UASClass::ScriptTypePtr` 才能回到 `UClass` / script object family。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassRuntimeDescriptor.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassRuntimeDescriptor.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassRuntimeDescriptorRegistry.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassRuntimeDescriptorRegistry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassRuntimeDescriptorTests.cpp`
- [ ] **P2.17** 📦 Git 提交：`[ClassRuntimeDescriptor] Refactor: move script-type metadata behind class runtime descriptors`
- [ ] **P2.17-T** 单元测试：验证 script class runtime metadata 进入统一 descriptor owner，而不是散落在 `UASClass` 裸字段上
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassRuntimeDescriptorTests.cpp`
  - 测试场景：
    - 正常路径：生成 script class 后，`GetObjectType()`、source path、developer-only、virtual dispatch 与 reference-schema 查询都通过同一 descriptor 命中当前 epoch metadata。
    - 边界条件：full reload 后旧实例仍存活、但新类已 swap-in 时，descriptor registry 能同时保留旧 epoch 只读视图与新 epoch active view，直到 teardown 完成；legacy `UASClass` mirror 与 descriptor 结果保持一致。
    - 错误路径：descriptor 缺失、revision 失配、或 removed class 仍被旧 `asITypeInfo::UserData` 引用时，查询入口返回显式 diagnostics / safe null，不再直接解裸 `ScriptTypePtr` 或过期 `UserData`。
  - 测试命名：`Angelscript.TestModule.ClassGenerator.RuntimeDescriptor.UnifiesObjectTypeSourceAndReferenceMetadataAcrossReloadEpochs`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.17-T** 📦 Git 提交：`[ClassRuntimeDescriptor] Test: cover descriptor registry, object type reflection, and reload epochs`

## 本轮追加条目的衔接与边界

1. `P2.16` 是 `P2.2 type/symbol identity + source metadata canonicalization` 在 `ClassGenerator` / `ImplementedInterfaces` 消费链上的深化，不重复 `Documents/Plans/Plan_InterfaceBinding.md`、`Documents/Plans/Plan_CppInterfaceBinding.md` 的 callable/value capability 闭环；它只先解决“我到底实现了谁、reload 里谁是同一个 symbol”。
2. `P2.17` 是 `UASClass` runtime metadata owner 的收口，不替代 `Documents/Plans/Plan_FullDeGlobalization.md` 的全局状态治理，也不把工作扩张成引擎补丁迁移；第一阶段仍严格限定在插件内 registry/mirror 重排。
3. `P2.17` 吸收的是 `UnrealCSharp / UnLua` 的窄切口 descriptor/registry 经验，不重复 `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md` 的广域参考梳理；这里落地的是 runtime metadata owner，不是再开一轮跨仓对比。
4. 这两项都属于长期架构 P2 深化，优先级必须后置于 `todo.md` 的“全局变量问题 / 各种全局数据结构的问题 / 委托”主线，以及 `Documents/Plans/Plan_StatusPriorityRoadmap.md` 里的 blocker、交付基线与 onboarding 入口；只有在这些更高优先级工作稳定后，才适合推进 qualified identity 与 descriptor owner 收口。
5. 两项工作都必须维持当前约束：继续兼容 AngelScript `2.33.0 WIP`、不修改 Unreal Engine、优先采用 dual-write / legacy mirror 渐进迁移，而不是一次性切断现有 `ScriptTypePtr` / short-name 路径。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.16` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptQualifiedSymbolIdentityTests.cpp` | namespaced class/interface、`A/U` prefix 折叠、reload head 与 interface resolution 的 qualified symbol 合同 | 高 |
| `P2.17` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassRuntimeDescriptorTests.cpp` | object-type reflection、source metadata、reference-schema 与 reload epoch 的 descriptor registry 合同 | 高 |

---

## 深化 (2026-04-09 08:36:40)

### Phase 1 补充：delegate 公共接口与生成桥接合同

- [ ] **P1.18** 建立 `DelegateBindingSurfaceContract + InternalSignatureBridge`
  - 当前 delegate 绑定同时暴露了两层本不该并列的协议：脚本作者应该使用的 2 参数 `BindUFunction/AddUFunction`，以及仅供 preprocessor 生成代码补 `__DelegateSignature(this)` 的 3 参数 raw overload。`Bind_Delegates.cpp` 把这两层合同挂在同一 public type surface 上，`AngelscriptPreprocessor.cpp` 又直接把生成桥接写死为 `_Inner.BindUFunction/AddUFunction(..., __DelegateSignature(this))`。结果是生成器内部协议泄漏到脚本公共 API，delegate family 无法像其他 family 一样清楚区分“用户可调用 surface”和“tooling/internal bridge”。
  - 本条目要先把 delegate 调用面收口成正式合同。第一阶段引入 `DelegateBindingSurfaceContract`：继续保留 2 参数 public API，但把 3 参数 raw overload 改成 `__Internal_*` 或等价 internal namespace，并让 `__DelegateSignature` 退化为生成器私有 bridge，而不是公共 global function。第二阶段引入 `InternalSignatureBridge` 或等价 `DelegateDescriptorHandle`，让 preprocessor、bind dump、未来 manifest/docs 共享同一 internal 表达，描述“当前 wrapper 绑定的到底是哪一个 delegate signature”，而不是继续靠 public overload + 裸 `UDelegateFunction` 穿透。
  - 这样做直接对齐 `todo.md` 的“委托”主线，但调度上应晚于 `P1.10` / `P1.11` 这类 owner/global-state 收口：先把 runtime 生命周期与委托句柄账本稳定，再缩公共 API 面，避免一边收全局状态，一边继续扩大 delegate 对外协议。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` `发现 83` — 生成器内部签名 helper 继续以 public overload 暴露，脚本侧能直接误用 raw `UDelegateFunction Signature` 入口。
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-8` — 绑定签名合同已经分裂成手写字符串、runtime descriptor、UHT descriptor 三套模型；delegate bridge 当前又额外泄漏出第四层“生成器私有 API”。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md`、`Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — 参考实现把 delegate 当作 registry/descriptor family 管理，而不是把 raw signature overload 暴露成公共脚本 surface。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` `L627` — `_FScriptDelegate` 公开 2 参数 `BindUFunction(UObject, FName)` 用户面。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` `L1116` — multicast delegate 公开 2 参数 `AddUFunction(const UObject, FName)` 用户面。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` `L1428-L1439` — 同一文件又额外挂出 `__DelegateSignature` 全局函数，以及 3 参数 `BindUFunction/AddUFunction(..., UDelegateFunction Signature)` raw overload。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `L665`、`L710`、`L714` — 生成代码直接把 `_Inner.AddUFunction/BindUFunction(..., __DelegateSignature(this))` 写死为桥接协议。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDelegateBindingSurfaceContract.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDelegateBindingSurfaceContract.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateBindingSurfaceTests.cpp`
- [ ] **P1.18** 📦 Git 提交：`[DelegateSurface] Refactor: split public delegate API from internal signature bridge`
- [ ] **P1.18-T** 单元测试：验证 delegate public surface 与生成器内部桥接彻底分层
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateBindingSurfaceTests.cpp`
  - 测试场景：
    - 正常路径：unicast / multicast delegate 继续通过 public 2 参数 `BindUFunction/AddUFunction` 成功绑定，生成器产出的 wrapper 通过 internal bridge 自动补齐 signature，不影响现有脚本写法。
    - 边界条件：public surface snapshot / bind dump / docs sidecar 不再把 3 参数 raw overload 记成与用户 API 同级的 callable；internal bridge 仍可被生成代码消费。
    - 错误路径：用户脚本直接调用 internal helper，或手工传入不匹配的 signature handle 时，编译阶段输出稳定 diagnostics，而不是静默落到 raw overload。
  - 测试命名：`Angelscript.TestModule.Delegate.BindingSurface.HidesRawSignatureHelpersButPreservesGeneratedWrappers`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.18-T** 📦 Git 提交：`[DelegateSurface] Test: cover public API and internal signature bridge separation`

### Phase 2 补充：delegate reload epoch 与依赖图

- [ ] **P2.18** 建立 `DelegateSignatureEpoch + DelegateDependencyPlan`
  - delegate 在当前架构里已经不是“简单语法糖”，但它的 reload owner 仍然被拆散在 `ClassGenerator`、`Bind_Delegates` 与 type materialization 三处。分析阶段已经把签名变化认定为 `FullReloadRequired`，执行阶段却没有兑现这条门禁；`EnsureReloaded(int TypeId)` 明明能拿到 `DelegateData`，却只真正处理 class；而 full reload 对 delegate function 只会 rename old + root new，后续 cleanup 仍没有与 class/struct 对称的 tombstone 回收。
  - 本条目要把 delegate 提升成真正的 reload node。第一阶段引入 `DelegateSignatureEpoch`：以 delegate canonical key、signature hash、epoch/revision 作为 authoritative identity，让 `ShouldFullReload(FDelegateData&)` 与 `SoftReloadOnly` gate 显式消费它。未变更签名的 delegate 可继续复用旧 epoch；一旦签名变化，transaction 必须像 class structural change 一样保留旧代码活跃并排队 full reload，而不是把 `NewDelegate->Function` 直接指回旧 `UDelegateFunction`。第二阶段引入 `DelegateDependencyPlan`：让 property/function/delegate materialization 在创建 `FDelegateProperty`、函数参数或返回值前，统一先解析 delegate reload node，而不是继续靠零散的 `EnsureReloaded()` 和调用顺序碰运气。第三阶段再补 delegate tombstone ledger，让 replaced `UDelegateFunction` 在 session cleanup 时移除 `RF_Standalone` / root，而不是永久留在包里。
  - 这条工作属于 `todo.md` 第三条“委托”主线，但执行顺序应后置于 `P2.1` hot reload session owner 与 `P2.17` runtime descriptor owner；它是前两项在 delegate family 上的专门落地，不应在更高优先级的全局变量/全局数据结构收口之前单独前冲。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` `发现 29 / 38` — delegate signature 变更已经在分析期被提升为 `FullReloadRequired`，但 `SoftReloadOnly` 仍复用旧 `UDelegateFunction`；replaced delegate signature 也没有对称的退场流程。
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` `Arch-TS-50` — 类型依赖预热仍是 site-specific `EnsureReloaded()`，而当前实现明确漏掉了 delegate 节点。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md`、`Documents/AutoPlans/ReferenceComparison/UnrealCSharp_Analysis.md` — 参考实现把 delegate 作为 registry family 管理，并把 reload gate / dependency owner 正式建模；当前项目应保留 typed reload ledger 的优势，但把 delegate 纳入同一 contract。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L1539-L1548` — delegate 分析阶段已把签名变化升级成 `FullReloadRequired`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L2105-L2113` — `ShouldFullReload(FDelegateData&)` 在 `SoftReloadOnly` 下完全不看 `ReloadReq`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L2525-L2548` — `EnsureReloaded(int TypeId)` 取得 `DelegateDataPtr` 后仍只调用 class 路径。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L3131-L3156` — `GetDataFor(asITypeInfo*)` 已能返回 `DelegateData`，说明遗漏并不是数据模型限制。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L2712-L2731` — full reload 会 rename old delegate function，并创建带 `RF_Standalone | RF_MarkAsRootSet` 的新 `UDelegateFunction`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L4016-L4021` — soft reload 直接把 `NewDelegate->Function` 指回旧 `UDelegateFunction`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L5019-L5035` — cleanup 当前只对 class / struct 做 `RemoveFromRoot()` 与 `ClearFlags(RF_Standalone)`，没有 delegate 对称分支。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` `L133-L137`、`L180-L195` — `FDelegateProperty` 物化和属性匹配都直接依赖 `SignatureFunction` 身份；因此 stale delegate signature 不是局部 reload 小 bug，而是 type system contract 破裂。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptDelegateSignatureEpoch.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptDelegateSignatureEpoch.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptDelegateDependencyPlan.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptDelegateDependencyPlan.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptDelegateReloadContractTests.cpp`
- [ ] **P2.18** 📦 Git 提交：`[DelegateReload] Refactor: add signature epoch gate and delegate dependency plan`
- [ ] **P2.18-T** 单元测试：验证 delegate signature reload gate 与 materialization 前置依赖合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptDelegateReloadContractTests.cpp`
  - 测试场景：
    - 正常路径：delegate 签名未变化时，soft reload 保持同一 signature epoch，`FDelegateProperty::SignatureFunction` 与当前 runtime descriptor 一致。
    - 边界条件：脚本类属性、函数参数或返回值引用 script delegate 时，`DelegateDependencyPlan` 会先完成 delegate node reload，再进行 property/function materialization，不再依赖调用顺序。
    - 错误路径：delegate 签名变化发生在 `SoftReloadOnly` 时，系统必须保留旧代码活跃并排队 full reload，而不是复用 stale `UDelegateFunction`；session cleanup 后 replaced delegate tombstone 也必须去 root / 清 `RF_Standalone`。
  - 测试命名：`Angelscript.TestModule.HotReload.Delegate.SignatureEpochGuardsSoftReloadAndDependencyMaterialization`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.18-T** 📦 Git 提交：`[DelegateReload] Test: cover signature epoch gate and delegate dependency plan`

## 本轮追加条目的衔接与边界

1. `P1.18` 解决的是 delegate public/internal contract，不重复 `Documents/Plans/Plan_AngelscriptLearningTraceTests.md` 里关于 delegate 教学 trace 的观测目标，也不替代 `Documents/Plans/Plan_StructUtilsMigration.md` 对 payload delegate 语义边界的专项迁移。
2. `P2.18` 解决的是 delegate 作为 reload/type node 的 owner，不重复 `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` `P5.2` 的验证轨；后者继续负责“现状回归是否正确”，本条则负责“为什么能稳定正确”的架构前提。
3. 两项都吸收了 `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md` 提到的 registry / dependency-graph 思路，但只在 delegate 这一条最脆弱的 family 上窄切口落地，不重新展开整套对比计划。
4. 这两项都属于 `todo.md` 的“委托”主线，应排在“全局变量问题”“各种全局数据结构的问题”之后推进；在本 Plan 内部，也应后置于 `P1.10`、`P2.1`、`P2.17` 这些 owner/session 基建，否则只会再次形成一套平行 delegate 旁路。
5. 两项工作都必须维持当前硬约束：继续兼容 AngelScript `2.33.0 WIP`、不修改 Unreal Engine、优先采用 internal alias / dual-write / legacy shim 渐进迁移，而不是一次性切断现有 preprocessor 与 runtime bind 路径。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.18` | `Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateBindingSurfaceTests.cpp` | delegate public 2 参数 API、internal signature bridge、raw helper 隐藏与误用诊断 | 中高 |
| `P2.18` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptDelegateReloadContractTests.cpp` | delegate signature epoch、dependency preflight、soft reload gate 与 replaced delegate tombstone cleanup | 高 |

---

## 深化 (2026-04-09 08:48:35)

### Phase 1 补充：live bind registry owner 与 scoped mutation overlay

- [ ] **P1.19** 建立 `BindRegistryGraph + ScopedMutationOverlay`
  - 当前 `P1.2` 已经在解决 bind artifact / manifest 的 authority，但 live bind registry 仍是另一套 process-global 可变状态：`GetBindState()` 在没有 current engine 时退回 `LegacyBindState`，`GetBindArray()` / `RegisterBinds()` 持有全进程静态 `BindArray`，`FBind` 构造即永久注册，`ResetBindState()` 又会直接把当前 bind state 清零。结果是 production baseline、test-only 注入、startup bind 回放、dump/debug 查询共用同一套 mutable surface，测试和工具只能靠“追加更多 bind”或“整桶清空”去借道。
  - 本条目解决的是 live registry owner，不重复 `P1.2` 的 manifest，也不把范围扩张成 `Plan_TestEngineIsolation.md` 的全量 engine-local 迁移。第一阶段先引入 engine-owned `FAngelscriptBindRegistryGraph`：startup 从 resolved provider plan 一次性 materialize immutable baseline，`GetBindInfoList()` / dump / smoke tests 统一读取 graph snapshot，而不是每次都对全局 `BindArray` 即席排序。第二阶段再引入 `FAngelscriptScopedBindMutationOverlay` 与 `BindRegistrySnapshot`：测试、工具和 future provider 的临时注入只写 overlay delta，离开 scope 后自动回滚，不再通过 `FBind` 永久污染 baseline，也不再用 `ResetBindState()` 把生产基线整桶清空。
  - 目标状态是：runtime 能回答“这台 engine 当前有哪些 baseline binds、哪些 overlay 变更、它们来自谁”；`BindConfig` / startup smoke / provider 验证拿到的是可恢复、可比较、可导出的 registry snapshot；legacy static registration 先退化成 baseline producer compatibility shim，而不是继续承担 live mutation API。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` `Issue-29`、`Issue-17`、`Issue-14` — 现有 smoke 只验证同一 registry 的自洽；`FBind` 与 `ResetBindState()` 会持续污染或清空全局 bind registry。
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` `Arch-BP-1`、`Arch-BP-20` — 当前注册目录仍是 process-global，contributor 又是 opaque lambda，缺少 provider ownership 与可追溯 live registry。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — 参考结论明确要求先收口 explicit provider owner / environment-owned registry graph，再继续提高自动化比例；`Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` 也表明 `FLuaEnv` 在 env 初始化时统一消费 exported registry，而不是让测试或工具直接改 live registration。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` `L23-L33` — `GetBindState()` 仍在 current engine 失败时回退到进程级 `LegacyBindState`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` `L132-L153` — `GetBindArray()` 与 `RegisterBinds()` 继续把 bind contributor 存进静态 `BindArray`，owner 不是 engine / session。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` `L176-L188` — `GetBindInfoList()` 只是对同一静态数组排序取视图，`ResetBindState()` 直接把当前 bind state 清空，没有 snapshot / restore 合同。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` `L440-L467` — `FAngelscriptBinds::FBind` 只有构造注册，没有对称反注册或 scoped mutation owner。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1915-L1921` — `BindScriptTypes()` 仍直接对共享 registry 执行 `CallBinds(CollectDisabledBindNames())`，没有 baseline / overlay 分层。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindRegistryGraph.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindRegistryGraph.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptScopedBindMutationOverlay.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptScopedBindMutationOverlay.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindRegistryOverlayTests.cpp`
- [ ] **P1.19** 📦 Git 提交：`[BindRegistry] Refactor: add baseline registry graph and scoped mutation overlay`
- [ ] **P1.19-T** 单元测试：为 live bind baseline / overlay 的可恢复合同补齐回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindRegistryOverlayTests.cpp`
  - 测试场景：
    - 正常路径：production-like baseline 初始化后，scoped overlay 注入 named / unnamed bind 只影响当前 overlay 视图，退出后 baseline 与 startup bind surface 完整恢复。
    - 边界条件：嵌套 overlay、legacy `UnnamedBind_*`、engine-level disabled bind names 与 snapshot 导出同时存在时，排序、`bEnabled` 与 provenance 仍稳定可对账。
    - 错误路径：冲突 bind name、overlay 在未持有 owner graph 时写入、或 test helper 试图调用 destructive reset 时，系统返回显式 diagnostics，而不是静默污染或清空 baseline。
  - 测试命名：`Angelscript.TestModule.BindingRegistry.OverlayPreservesBaselineAndScopedMutations`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P1.19-T** 📦 Git 提交：`[BindRegistry] Test: cover baseline snapshot and scoped bind overlays`

### Phase 2 补充：统一执行合同与失败策略

- [ ] **P2.19** 建立 `ExecutionRequest/ExecutionResult + LifecycleFailurePolicy`
  - 当前 runtime 把 pooled context 直接固化成公共执行接口：engine 初始化时全局注册 `AngelscriptRequestContext` / `AngelscriptReturnContext`，public side 只有 `PrepareExternal()` / `ExecuteExternal()` 这种薄包装，nested execution 直接 `PushState()`。与此同时，class generator、post-init、script-object construct / defaults / reinitialize 等生命周期调用又在多个地方直接 `Context->Execute()`，而且多数不看返回状态。结果是“如何拿 context”“这次执行是谁触发的”“失败是否阻断 reload / construct”“是否允许 timeout / tracing”没有同一份 contract。
  - 本条目不推翻 context pool，也不重复 `P2.4` 的 quiescence barrier。第一阶段先引入 `FAngelscriptExecutionRequest`、`FAngelscriptExecutionResult`、`IAngelscriptExecutor` 和 `EAngelscriptExecutionFailurePolicy`；默认 `FPooledContextExecutor` 继续复用现有 pool / `PushState()` 语义，但 runtime external calls、`CallPostInitFunctions()`、`ExecuteConstructFunction()`、`ExecuteDefaultsFunctions()`、`ReinitializeScriptObject()` 都改为走同一请求入口并返回结构化结果。第二阶段再在 executor 外侧接入 tracing / timeout middleware，让 reload 与 class generation 基于 result / failure policy 决定 rollback、`bModuleSwapInError`、diagnostic aggregation 或 retry，而不是继续依赖 silent `Context->Execute()`.
  - 目标状态是：execution caller 必须显式提交 `Function`、`ModuleId`、`DebugName`、`WorldContext`、`bAllowNestedReuse` 与 `FailurePolicy`；pooled context 退化成默认优化细节；class generator / reload 不再静默吞掉异常；后续 `P2.4`、`P4.7`、debugger / toolchain 也能共享同一份 execution metadata。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — 已指出 `CallPostInitFunctions()` 仍按裸名查找并直接 `Context->Execute()`，`ExecuteConstructFunction()` / `ExecuteDefaultsFunctions()` / `ReinitializeScriptObject()` 也忽略 `Execute()` 返回状态，失败不会进入统一 rollback / failure lane。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` `Arch-SL-21` — 当前把 context pooling 固化成公共契约，缺少可插拔 `Executor / Middleware` seam。
    - [E] `Documents/AutoPlans/ReferenceComparison/UnLua_Analysis.md` 与 `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` 中对 `LoadString(..., ChunkName)` 的对比 — 参考实现把 `ChunkName` / debug identity 当作执行 contract 的一部分，而不是只让底层 VM 隐式知道。
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L910-L911` 与 `L1422-L1423` — engine 创建时立即 `SetContextCallbacks(&AngelscriptRequestContext, &AngelscriptReturnContext, nullptr)`，执行策略直接绑死在 pool callback 上。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1722-L1750` — `AngelscriptRequestContext()` / `AngelscriptReturnContext()` 只做池化分配归还，没有 request / result 元数据。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` `L1785-L1806` — `PrepareExternal()` / `ExecuteExternal()` 只是薄包装底层 `Prepare/Execute`，嵌套执行直接 `PushState()`。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L5775-L5799` — `CallPostInitFunctions()` 直接构造 context 并 `Context->Execute()`，没有结构化结果或失败聚合。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` `L1086-L1133` — `ExecuteDefaultsFunctions()` 与 `ExecuteConstructFunction()` 直接 `Context->Execute()`，没有 failure policy。
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `L4825-L4844` — `ReinitializeScriptObject()` 仍直接 `Context->Execute()`，reload / reinstance 对异常没有统一阻断合同。
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExecutionRequest.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExecutionRequest.cpp`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExecutor.h`
    - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExecutor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptExecutionContractTests.cpp`
- [ ] **P2.19** 📦 Git 提交：`[ExecutionContract] Refactor: add execution request-result and lifecycle failure policy`
- [ ] **P2.19-T** 单元测试：统一验证 external execute、post-init 与 class lifecycle 的执行合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptExecutionContractTests.cpp`
  - 测试场景：
    - 正常路径：external function、post-init helper、construct / defaults / reinitialize path 都通过 `ExecutionRequest` 返回稳定 `ExecutionResult`，默认 executor 行为与 today 保持一致。
    - 边界条件：nested execution 打开 `bAllowNestedReuse` 时仍保持当前 `PushState()` 语义；tracing middleware 能拿到 `ModuleId`、`DebugName`、`Duration` 而不改变脚本结果。
    - 错误路径：constructor / post-init / reinit 抛 exception 或 timeout 时，failure policy 把结果显式反馈给 reload / class-generation caller，不再静默继续 swap-in 或默认对象初始化。
  - 测试命名：`Angelscript.TestModule.Execution.ContractUnifiesExternalPostInitAndLifecycleFailureHandling`
  - 隔离方式：`FAngelscriptEngineScope`
  - 测试框架：`IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- [ ] **P2.19-T** 📦 Git 提交：`[ExecutionContract] Test: cover request-result and lifecycle failure handling`

## 本轮追加条目的衔接与边界

1. `P1.19` 不重复 `P1.2` / `P1.9` 的 manifest 与 bind phase authority，也不替代 `Documents/Plans/Plan_TestEngineIsolation.md` 的全量 engine-local state 迁移；它只在现有 keyed-state groundwork 基础上，为 live bind registry 增加 baseline / overlay / snapshot 合同。
2. `P2.19` 不重复 `P2.16` 的 symbol qualification、`P2.4` 的 execution quiescence barrier，或 `P4.7` 的 source map / diagnostics translator；`P2.16` 解决“执行哪个 symbol”，`P2.19` 解决“执行如何被表示、失败如何被传播”，`P2.4` 再消费这份合同做 lease / retire barrier。
3. 在调度上，`P1.19` 直接贴合 `todo.md` 的“各种全局数据结构的问题”，应与去全局化 keyed-state 主线紧密联动；`P2.19` 则应后置于 `P1.19`、`P1.10` 与 `P2.4` 之后，避免先抽象执行层、后补 owner 与 retire contract，重新形成一套平行旁路。
4. 两项工作都必须维持当前硬约束：继续兼容 AngelScript `2.33.0 WIP`、不修改 Unreal Engine、优先采用 compatibility shim / dual-write / scoped adapter 渐进迁移，而不是一次性切断现有 static bind 与 pooled-context 路径。

## 本轮追加条目的单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.19` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindRegistryOverlayTests.cpp` | baseline registry snapshot、scoped mutation overlay、disabled/provenance 对账与 destructive reset 诊断 | 高 |
| `P2.19` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptExecutionContractTests.cpp` | request / result 合同、nested execution 兼容、post-init / construct / reinit failure policy | 中高 |
