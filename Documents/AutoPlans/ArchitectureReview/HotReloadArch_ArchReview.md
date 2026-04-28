# HotReloadArch 架构评审

---

## 架构分析 (2026-04-08 14:03)

### Arch-HR-1：版本链以 `UClass` 壳为中心，函数改动可走 `SoftReload`，但执行单元仍然是“整个类”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载版本链与增量重载边界 |
| 当前设计 | `DirectoryWatcher` 只负责把变更文件入队；真正的重载决策和执行落在 `FModuleData / FClassData` 上。函数 body-only 变更通常不要求 `FullReload`，但仍会进入所属类的 `DoSoftReload()`，对该类做整类 relink、函数重绑、实例遍历和 `CDO` 重建。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`：输入是文件/目录变化队列，而不是函数级补丁。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:61-74,93-105`：重载工作单元是 `FClassData` / `FModuleData`，保存 `OldClass`、`NewClass`、`ReplacedClass`、`CDONoDefaults`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1196-1262`：函数删除、签名变化、definition 变化会升级到 `FullReloadRequired`；仅 body 改动不会在这里升级。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2496-2504`：`EnsureReloaded()` 仍按类分派到 `DoSoftReload()` / `DoFullReload()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4135-4188,4244-4275,4562-4776`：`DoSoftReload()` 会重新链接整类属性、重绑整类函数、遍历该类全部直接实例并重建实例/`CDO` 脚本对象。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2572-2579,3695-3700` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:18-20`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:912-923`：`FullReload` 通过 rename old class、设置 `CLASS_NewerVersionExists`、串 `NewerVersion` 版本链，再由 `GetMostUpToDateClass()` 手工追链。 |
| 优点 | 反射契约清晰，和 UE 的 `UClass/UFunction/FProperty` 模型兼容；body-only 改动可以保持同一个 `UClass` 壳，避免无谓的 `FullReload`。 |
| 不足 | 当前没有“函数级 patch slot”。修改一个函数 body 虽然通常不需要 `FullReload`，但仍需要重载所属类；`SoftReload` 成本里包含整类 relink、实例扫描、`CDO` 差异迁移。版本链还是显式调用式消费，遗漏 `GetMostUpToDateClass()` 的调用点就可能继续看到旧类。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载核心是 Lua module cache，而不是 `UClass` 壳。`FLuaEnv::HotReload()` 只触发 `UnLua.HotReload()`；Lua 侧自定义 `require()`，优先命中 `package.loaded` / `loaded_modules`，并对改动模块做 table/function/upvalue/global merge。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-176,480-549,604-623` | 把“脚本 body 变化”处理成 module table 更新，而不是强制进入 UE class relink。适合作为 Angelscript 的预过滤层或快速路径灵感。 |
| puerts | 编辑器只监听已经加载过的 JS source 文件，文件变更后直接 `ReloadSource()`；运行时通过 `__reload` 调到 `hot_reload.js`，再用 `Debugger.setScriptSource` 原地替换脚本内容，不需要重建 `UClass` 壳。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-146`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | 把“已加载 source 的 body 更新”降成 source-level patch，而不是对象壳级 reload。对于 Angelscript，可借鉴成 `UASFunction::ScriptFunction` 级别的快速重绑。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `SoftReload` 前加一个保守的 `FunctionBodyOnly` 快速路径，先把“函数 body 改了但反射契约没变”的场景从整类重载里剥出来。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` 的 `FClassData` 增加 `EDeltaKind` 或布尔标记，明确区分 `FunctionBodyOnly`、`FunctionTypePatch`、`StructuralChange`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1196-1319` 的分析阶段，把“仅 body 改动、默认值/metadata/flags/属性布局均未变”的类标成 `FunctionBodyOnly`。<br>3. 新增 `DoMethodPatchReload(FModuleData&, FClassData&)`：仅更新受影响 `UASFunction::ScriptFunction`，复用现有 `SoftReloadFunction()` 做参数类型刷新，避免进入 `GetObjectsOfClass()`、属性 relink、`CDO` 迁移。<br>4. 在 `EnsureReloaded()` 里优先分派 `DoMethodPatchReload()`；任何发现默认值、metadata、super/interface、property offset、instanced property 变化时，立即回退到现有 `DoSoftReload()`。<br>5. 给该路径加一个默认关闭的 `CVar`，先以实验特性落地，再扩展自动化测试覆盖。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 快速路径最容易漏掉“看似 body-only、实则影响默认值或序列化布局”的边缘场景；必须保持严格保守，宁可多回退到旧路径，也不要错误跳过类级重载。 |
| 兼容性 | 向后兼容。默认保持现有行为，只在显式开启 fast path 后尝试优化；脚本 API 与类版本链都不需要破坏性调整。 |
| 验证方式 | 复用并扩展 `FunctionChange` / `BodyOnlyChange` 测试：确认仍返回新逻辑；新增计数或 trace，证明 fast path 下没有进入 `GetObjectsOfClass()` / `CDO` 重建；对签名变更和属性变更验证仍会正确回退到 `SoftReload` / `FullReload`。 |

### Arch-HR-2：状态保持覆盖面集中在 `UObject property memory + Editor 资产修补`，对外部状态和 UE 原生热重载缺少统一桥接

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 状态保持健壮性、`C++ Hot Reload / Live Coding` 交互 |
| 当前设计 | 当前状态迁移的核心是 `CDONoDefaults` 差异法和对象属性复制：`SoftReload` 只保存/恢复脚本对象里能映射到属性 offset 的状态；`FullReload` 之后由 `ClassReloadHelper` 修补 Blueprint graph、DataTable、打开中的资产编辑器、组件类型列表和 volume geometry。换句话说，当前系统更像“反射内存迁移 + Editor 侧补洞”，不是统一的 runtime state bridge。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4093-4110`：`PrepareSoftReload()` 先构造 `CDONoDefaults` 作为差异基准。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4562-4776`：实例/`CDO` 迁移都围绕 `PropertiesToCopy`、旧 `CDO`、父 `CDO` 做属性级复制；未见超出属性内存的统一状态迁移层。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:50-175`：`Init()` 只订阅 `FAngelscriptClassGenerator::OnStructReload / OnClassReload / OnDelegateReload / OnLiteralAssetReload / OnEnum* / OnFullReload / OnPostReload`，负责 Blueprint action、component registry、volume rebuild 等 Editor 收尾。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:20-23`：源码直接注明“new unreal reload system is not yet up to providing for AS reloads”。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:42-120,181-299`：依赖修补聚焦在 loaded Blueprint、pin type、`ReparentHierarchies()`、Blueprint recompilation、DataTable row struct。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:387-440`：`UAngelscriptReferenceReplacementHelper` 额外处理的是 open asset editors，不是任意 runtime 外部状态。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1184-1223`：部分运行时消费点需要显式调用 `GetMostUpToDateClass()` 追版本链，说明“拿到最新类版本”目前仍是 call-site 责任。 |
| 优点 | 对 `UObject` 实例属性、`CDO` 默认值、Blueprint/Editor 依赖的修补已经比较完整；相比只做脚本 VM reload 的方案，更贴近 UE 编辑器工作流。 |
| 不足 | 推断上，当前系统对“未存放在对象属性内存中的状态”覆盖有限，例如外挂缓存、外部持有的旧类引用、非 `GetAllEditedAssets()` 能枚举到的编辑器对象。对 `C++ Hot Reload / Live Coding` 也没有看到统一桥接层；`Init()` 只接了 Angelscript 自己的 reload 广播，没有形成 native reload -> AS reload 的明确通路。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 状态保持的重心在 Lua VM：热重载时不仅替换模块函数，还会更新 running stack、`_G`、registry、userdata uservalue、function upvalue；多 `LuaEnv` 场景下还能由 `EnvLocator` 批量广播 `HotReload()`。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:367-477,480-549`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:28-33,76-82` | 可以借鉴成“状态覆盖面审计”或“外部状态回调”机制：把当前 CDO/property 迁移之外的状态也显式纳入可扩展接口，而不是全靠默认对象复制。 |
| puerts | 明确把 UE 原生热重载接入 JS 侧：模块启动时订阅 `ReloadCompleteDelegate` / `OnHotReload()`，回调里重建 `JsEnv` 并 `RebindJs()`；编辑器热更新则继续走 source-level `__reload`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185-244,424-438`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | Angelscript 最值得借鉴的是“native reload bridge”：当 C++ 基类或绑定发生热重载时，不要静默依赖旧版本链，而要显式重绑或显式告警。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `ClassReloadHelper + CDO` 迁移之外，新建一层 `NativeReloadBridge + StateCoverageAudit`，把原生热重载和未覆盖状态都显式化。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptNativeReloadBridge.*`（或 Editor 对等文件），在 UE5 接 `FCoreUObjectDelegates::ReloadCompleteDelegate`，UE4 fallback 接 `IHotReloadInterface::OnHotReload()`；若工程启用了 `LiveCoding`，再以可选编译路径接 `ILiveCodingModule`。<br>2. native reload 完成后，扫描活跃 `FAngelscriptModuleDesc` 与 `UASClass`：凡是 `CodeSuperClass`、默认组件类、`UFUNCTION/UPROPERTY` 绑定目标落在已重载 native type 上的，统一升级成 `FullReloadSuggested` 或 `FullReloadRequired`，不要继续静默沿用旧版本链。<br>3. 扩展 `FClassReloadHelper::FReloadState`，新增可注册的 `ExternalStateAdapters`：允许 subsystem、runtime cache、asset tool、custom editor 面板把“我持有的旧对象引用”注册到统一 replacement pass。<br>4. 新增状态覆盖审计输出：统计本轮迁移了多少实例、多少 `CDO`、多少 Blueprint/DataTable/open editor 资产，同时列出仍然指向 `CLASS_NewerVersionExists` 类的外部引用，作为 warn-only 诊断。<br>5. 把当前零散的 `GetMostUpToDateClass()` 调用点收敛成公共 helper，降低遗漏调用点继续消费旧版本类的概率。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptNativeReloadBridge.*` |
| 预估工作量 | L |
| 架构风险 | 最大风险是重复触发 reload：AS 自己的文件热重载和 native reload bridge 可能在同一帧内叠加；需要 debounce 和分阶段队列，避免同一类被连续 reinstance。 |
| 兼容性 | 向后兼容。第一阶段可以只做 `warn-only` 桥接和 audit，不改变现有脚本行为；确认稳定后再让 bridge 驱动自动的 `FullReloadSuggested/Required`。 |
| 验证方式 | 增加“脚本类继承 native C++ 类，随后触发 native Hot Reload / Live Coding”的集成测试；验证 bridge 会触发显式告警或重新调度 Angelscript reload。扩展 Editor 自动化测试，覆盖 open asset editor、Blueprint/DataTable、component list、volume rebuild，以及 audit 对未替换旧引用的输出。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-2 | 状态保持覆盖面与 `C++ Hot Reload / Live Coding` 交互 | 结构性补桥 + 诊断能力新增 | 高 |
| P1 | Arch-HR-1 | 函数 body-only 变更的重载粒度 | 增量快速路径 | 中高 |

---

## 架构分析 (2026-04-08 14:43)

### Arch-HR-3：`ReloadReq` 传播按“类型闭包”放大，缺少基于变更种类的依赖裁剪

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 增量重载的依赖传播粒度 |
| 当前设计 | 编辑器侧 `DirectoryWatcher` 只把文件/目录变化归一化入队；真正决定 blast radius 的是 `FAngelscriptClassGenerator` 在 `Setup()` 阶段对所有 class / delegate 做一次类型闭包传播，再把最强 `ReloadReq` 递归推给所有 dependee。结果是：一个脚本类型只要出现在 super、property type、return type、param type 上，就会成为传播边；传播边本身不区分“body-only”“signature”“layout”这几类变更。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`：输入只是文件与目录变化队列。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1884-1905`：`Setup()` 先对全部 class / delegate 做 `PropagateReloadRequirements()`，再汇总全局 `ReloadReq`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1923-1961`：`AddReloadDependency()` 命中脚本类型后，直接把目标 `ReloadReq` 抬到 source，并通过 `PendingDependees` 建反向等待链。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1975-2037`：类级传播会扫描 super、全部 object property type、全部 method return / param object type。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2063-2076`：`ResolvePendingReloadDependees()` 会把 strongest `ReloadReq` 递归推给所有 dependee。 |
| 优点 | 安全边界明确，传播规则完全由源码类型关系驱动，不依赖启发式字符串匹配；遇到不确定场景时更偏保守，不容易漏重载。 |
| 不足 | 传播边过粗。推断上，只要 `B` 的变更把自己抬到 `FullReloadSuggested/Required`，任何在签名上“提到过 `B`”的 `A` 都可能被一起抬高，即使本次改动并不影响 `A` 的调用契约。当前也没有把“为什么 `A` 被牵连”暴露成一等诊断，导致增量能力很难继续向函数级演进。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 重载前沿是 module 名集合，而不是类型闭包。Lua 侧接管 `require()` 缓存，`M.reload()` 只枚举 `loaded_module_times` 中发生时间戳变化的 module，再把这些 module 送进 `reload_modules()` 与 `update_modules()`。未进入 `module_names` 的模块不会因为“在签名里提到过某个类型”被被动牵连。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-176`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:560-625` | 可以借鉴“先把变更边界保持为显式 changed module set，再决定是否向外扩散”的两阶段模型。 |
| puerts | 监听边界是“已加载 source file”。`FSourceFileWatcher` 只注册已加载过的目录与文件，MD5 变化时只把单个 `NotifyPath` 回调出去；运行时 `ReloadSource()` 把这一个 path 交给 JS 侧 `__reload`，由 `Debugger.setScriptSource` 原位更新脚本。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:52-80`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 可以借鉴“changed file/source set 是一等数据结构”的设计，把传播从默认全递归改成先精确、再保守回退。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `ReloadReq` 模型前面增加“变更种类 + 依赖边原因”层，优先收窄传播，再保守回退到当前全递归策略。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` 的 `FReloadPropagation / FClassData / FDelegateData` 中新增 `FReloadEdge` 记录，至少保存 `EdgeKind`（`Super` / `PropertyType` / `ReturnType` / `ParamType` / `DelegateArg`）、源成员名和源行号。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 的 `Analyze()` 阶段，把当前已存在的差异判断结果进一步归类成 `BodyOnly`、`DefaultsOnly`、`Signature`、`Layout`、`NewType` 等 delta kind。<br>3. 改写 `AddReloadDependency()` 与 `ResolvePendingReloadDependees()`：只有当 target 的 delta kind 可能影响当前 edge kind 时才继续向 dependee 推送；例如 body-only 改动不应仅因为某类把它当作参数类型就升级该 dependee。<br>4. 对无法可靠分类的场景维持当前行为，直接回退到“传播 strongest `ReloadReq`”；首版以 `CVar` 或编译开关保护。<br>5. 在 Editor / log 中输出依赖解释链，例如“`ClassA` 因 `ParamType -> ClassB` 被抬到 `FullReloadSuggested`”，便于校验传播是否过度。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` |
| 预估工作量 | M |
| 架构风险 | delta kind 判错会导致漏传播，尤其是 script type size、default statements、metadata 与 signature 交叉变化的边缘场景；因此首版必须保守，宁可误报也不能误放过。 |
| 兼容性 | 向后兼容。默认可继续走当前递归传播；只有在 edge-filter 明确可判定时才减少受影响范围。脚本 API 与现有 full reload 路径不需要破坏性修改。 |
| 验证方式 | 新增“共享类型 `B` 的 body-only / defaults-only / signature-change 分别影响依赖类 `A` 的程度”测试；对比 trace 确认 body-only 不再抬高仅签名引用 `B` 的 dependee，而 signature / layout 变化仍会正确升级。 |

### Arch-HR-4：`SoftReload` 通过重跑 constructor 保持对象壳可用，但会重放非属性副作用且没有对象级 HMR 钩子

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 状态保持健壮性，尤其是 live object 的非属性状态 |
| 当前设计 | 当前 `SoftReload` 的核心不是“原地替换函数实现”，而是“保存可复制属性 -> 析构脚本对象 -> 重新构造脚本对象 -> 把属性拷回去”。这能保住对象 identity 和大部分 reflected property 值，但 `ReinitializeScriptObject()` 会无条件重跑 script constructor；运行时对外只暴露 `OnClassReload` / `OnStructReload` / `OnFullReload` / `OnPostReload(bool)` 这类全局广播，没有对象级 `pre/post` 钩子让实例自行修复副作用。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12-19,31-38`：公开的是 class / struct / delegate / reload session 级 delegate，没有对象级 hot reload 回调。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:50-175`：编辑器侧消费的也是全局广播，职责集中在 Blueprint action、component list、volume rebuild 等 Editor 刷新。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4572-4638`：live instance 路径会保存属性、`DestructScriptObject()`、`ReinitializeScriptObject()`，再把属性拷回去。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4672-4769`：`CDO` 也走同样的暂存-重建-回填流程。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4825-4845`：`ReinitializeScriptObject()` placement-new `asCScriptObject` 之后，会无条件执行 script constructor。 |
| 优点 | `UObject` identity 不变，反射属性值大多可恢复；对 UE 现有引用关系和 Editor 对象图更友好。 |
| 不足 | 推断上，凡是 constructor 内注册到外部系统、但不落在 reflected property memory 里的状态，都有重复执行或丢失风险，例如 timer / delegate 订阅、latent handle、外部 cache 注册、运行时创建的临时 subobject、对世界或 subsystem 的一次性登记。当前全局 `OnPostReload(bool)` 太晚也太粗，无法把修复责任下沉到具体对象。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 设计目标就是“替换运行环境中的函数，保持 upvalue 和运行时 table”。Lua 侧先定义 `module_loaded` hook，再在 `update_modules()` 中把新函数写回旧 module table，最后 `merge_objects()` / `update_global()` 修补对象图；整个过程没有重建 UE object，也没有重跑对象 constructor。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:2-7`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:20-26`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549` | 两点都值得借鉴：一是尽量 patch code / closure，而不是重建对象；二是给业务侧保留显式 hook，而不是只给全局广播。 |
| puerts | source hot reload 走 `Debugger.setScriptSource` 原位替换脚本内容；JS 侧显式发 `HMR.prepare` / `HMR.finish`，给业务留出模块级恢复窗口。C++ 侧 `ReloadSource()` 只把 path 与 source 交给 JS 热更新函数，不重建 UE object。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 可以借鉴“对象不动、代码变”和“prepare/finish lifecycle hook”这两层拆分。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `SoftReload` 从“盲目重跑 constructor 的对象重建”演进成“存储重建 + 可选 constructor replay + 对象级恢复钩子”的两阶段模型。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 中把 `ReinitializeScriptObject()` 拆成两步：`RebuildScriptStorage()` 只做 placement-new / runtime storage rebuild，`ReplayConstructFunction()` 负责是否执行 script constructor。<br>2. 先只对高置信度场景开放“skip constructor on soft reload”，例如 body-only 且 property layout / defaults / constructor signature 都未变；其余场景继续沿用当前 replay 路径。<br>3. 在 runtime 新增对象级 hook 约定，可选做成 `IAngelscriptHotReloadAware`、或脚本方法 `__PreSoftReload()` / `__PostSoftReload(bool bCtorReplayed)`；在属性暂存前与属性回填后调用，让对象自己恢复 timer、delegate、latent action、外部 cache 等非属性状态。<br>4. 保留现有 `OnPostReload(bool)` 与 `ClassReloadHelper` 机制不变，只把对象级恢复做成增量补充层；避免一次性重写 Editor 刷新逻辑。<br>5. 新增诊断：统计本轮 soft reload 重放了多少 constructor、多少对象声明自己需要 `post-rebind`；当同一对象在 reload 后出现重复 delegate/timer 绑定时输出告警。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadHooks.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadHooks.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp` |
| 预估工作量 | L |
| 架构风险 | 如果过早跳过 constructor，可能破坏某些依赖 constructor 初始化 internal buffer 的脚本对象；因此第一阶段必须 opt-in 或受 `CVar` 保护，并对不满足条件的类保留旧行为。 |
| 兼容性 | 向后兼容。默认仍可保留当前 constructor replay 路径；对象级 hook 与 no-ctor path 都可作为增量特性引入，不要求现有脚本立刻适配。 |
| 验证方式 | 增加“constructor 内注册 timer / delegate / subsystem listener”的 soft reload 用例，验证 reload 后不会出现重复绑定；增加 body-only 场景下 `bCtorReplayed == false` 的 trace 断言，以及 fallback 场景仍保持旧路径的回归。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-4 | `SoftReload` 的 constructor replay 与对象级恢复缺口 | 状态保持机制补强 | 高 |
| P1 | Arch-HR-3 | `ReloadReq` 的依赖传播粒度 | 增量传播裁剪 + 可诊断性增强 | 中高 |

---

## 架构分析 (2026-04-08 14:55)

### Arch-HR-5：PIE 下的 `SoftReloadOnly + QueuedFullReloadFiles` 形成“双状态窗口”，结构性变更被延后提交

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | PIE / game world 下热重载的一致性模型 |
| 当前设计 | 当前 runtime 明确把“能立即生效的 script code 更新”和“必须等到允许 `FullReload` 才能提交的反射面变化”拆成两段。进入 game world 后，`Tick()` 只跑 `CheckForHotReload(ECompileType::SoftReloadOnly)`；与此同时，`ClassGenerator` 又会把部分新类、flag 变化、delegate 新增刻意降到 `FullReloadSuggested`，让 `SoftReloadOnly` 仍可先 `SwapInModules()` 并执行 `PerformSoftReload()`。如果本轮只是 `PartiallyHandled` 或已经 `ErrorNeedFullReload`，文件再被塞进 `QueuedFullReloadFiles`，但这个集合只有文件粒度，直到后续允许 `FullReload` 的轮次才会真正消费。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2819-2829`：PIE / game world 只允许 `SoftReloadOnly`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1063-1071,1327-1330,1555-1560`：新类与部分新增 delegate 被明确降到 `FullReloadSuggested`，目的是让 `SoftReloadOnly` 还能先 swap in module。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3942-3965`：`FullReloadSuggested` 在 `SoftReloadOnly` 下会先 warning，再 `SwapInModules()` + `PerformSoftReload()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3972-3991`：`FullReloadRequired` 在 `SoftReloadOnly` 下直接保留旧 code active。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4168-4186`：`ErrorNeedFullReload` / `PartiallyHandled` 都会把文件放进 `QueuedFullReloadFiles`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2758-2762` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:418-419`：`QueuedFullReloadFiles` 只有文件集合，并且只在 `CompileType != SoftReloadOnly` 时才会被消化。 |
| 优点 | 编辑器或 PIE 中的 body-level 改动可以尽快生效，不必因为一次结构变化就立刻打断当前游戏世界。 |
| 不足 | 当前实际上存在一个显式的“双状态窗口”：script module 代码可能已经是新的，但 `UPROPERTY` / `UFUNCTION` 可见面、Blueprint / asset reinstance、Editor action database 仍停留在旧状态。`QueuedFullReloadFiles` 只有 file set，没有 session 级原因、受影响符号和提交时机信息；推断上，多次保存叠加后很难解释“哪些变化已经生效、哪些还在等 full reload”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载入口是当前 `LuaEnv` 里的即时调用：`FLuaEnv::HotReload()` 直接执行 `UnLua.HotReload()`；Lua 侧 `M.reload()` 只扫描已加载 module 的修改时间，并在当前 env 里立刻 `reload_modules(modified_modules)`。它没有额外的“待 full reload 文件队列”。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-624` | 可以借鉴把“pending reload session”做成一等状态，而不是仅靠散落的文件队列隐式表达。 |
| puerts | `ReloadSource()` 直接在活跃 `JsEnv` 里调用 JS 侧 reload；`hot_reload.js` 通过 `HMR.prepare` / `Debugger.setScriptSource` / `HMR.finish` 原位替换 source。整个过程是“当前环境即时 patch”，没有等下一次允许某种 reload 模式再补提交的 backlog。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1541`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | Angelscript 很难完全照搬 source patch，但可以借鉴“deferred state 显式化”和“prepare/finish 生命周期”而不是只记文件名。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前隐式的 `QueuedFullReloadFiles` 升级成显式的 `PendingFullReloadSession`，让“PIE 下延后提交”的一致性窗口可见、可合并、可自动清空。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 用 `FPendingFullReloadSession` 取代裸 `QueuedFullReloadFiles`，至少记录 `SessionId`、`Files`、`Modules`、`ReloadLines`、`ReasonKinds`、`CreatedAtMode(PIE/Editor)`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3942-3991,4168-4186`，把 `FullReloadSuggested` / `ErrorNeedFullReload` 下的延期逻辑统一写入该 session，而不是只追加文件集合。<br>3. 当 `CompileType == SoftReloadOnly` 且 session 非空时，向 Editor / output log 暴露固定格式的 pending summary，明确指出“代码已 swap in”还是“仍保留旧 code active”。<br>4. 在 PIE end / world teardown 后自动尝试一次 `FullReload` flush；如果同一 session 期间用户继续保存，按 `SessionId` 合并为 latest-wins manifest，而不是积累多个无上下文的文件集合。<br>5. 增加自动化测试：覆盖 `FullReloadSuggested in PIE`、`FullReloadRequired in PIE`、PIE 结束后自动 flush，以及多次保存 coalesce。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDeferredReloadState.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDeferredReloadState.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把“只需提示”的延期状态误做成自动强制 reload，导致 PIE 退出时出现意外 reinstance；因此首版应先把 session 显式化和自动 flush 做成可诊断、可关闭路径。 |
| 兼容性 | 向后兼容。第一阶段只是把现有延期机制产品化，不改变 `SoftReloadOnly` / `FullReload` 的实际选择规则；现有脚本与用户工作流不需要立刻调整。 |
| 验证方式 | 运行 PIE 场景测试，验证 `FullReloadSuggested` 时 session 被记录且 soft path 仍工作；验证 `FullReloadRequired` 时旧 code 继续运行且 session 明确标记为 blocked；退出 PIE 后自动触发一次 full reload 并清空 session。 |

### Arch-HR-6：状态迁移按“脚本本地字段名 + 精确类型”匹配，schema 演进时会静默掉值

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `CDO` / live instance 状态迁移在字段演进场景下的健壮性 |
| 当前设计 | 当前 `SoftReload` 的字段迁移模型非常保守：只从 script object 的“脚本本地内存段”里收集可 copy / construct / destruct 的字段；新旧字段只有在 `Name` 和 `Type` 完全相等时才会加入 `PropertiesToCopy`。随后实例与 `CDO` 都按这个映射做暂存、重建、回填。结果是：字段 rename、类型升级/降级、字段搬到 `CodeSuperClass`、或 `CanCopy/CanConstruct/CanDestruct` 为 false 的 script type，都不会进入迁移表，值会在 reload 时静默丢失。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4466-4470,4510-4513`：`IgnoreBeforeOffset = CodeSuperClass->GetPropertiesSize()`，明确只处理脚本本地字段，不处理 C++ base property 区域。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4479-4503`：旧字段必须 `CanCopy() && CanConstruct() && CanDestruct()` 才能进入候选表。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4523-4531`：新字段只在 `OldProperties.Find(LocalProp.Name)` 且 `Copy->Type == PropertyType` 时才会进入 `PropertiesToCopy`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4572-4601,4672-4726`：实例和 `CDO` 都只围绕这份 `PropertiesToCopy` 做暂存与回填。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4825-4845`：字段回填之前会重建 script object 并重跑 constructor。 |
| 优点 | 规则清晰、实现保守，不会尝试跨 ABI / layout 猜测式搬运数据；出错面可控。 |
| 不足 | 这套迁移规则把很多“语义上兼容、实现上只是字段演进”的改动都看成 delete + add。典型丢状态场景包括：字段改名、`int32 -> int64` 之类的安全扩宽、同名字段迁入 code superclass、以及任何不可 copy 的 script type。当前路径也不会为这些 drop 生成结构化报告，开发者只能从 reload 后行为倒推是哪一项值没带过来。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载时先在 sandbox 中加载新模块，之后 `update_modules()` 把新函数写回旧 module table，并通过 `merge_objects()` / `update_global()` 修补运行时对象图。对于纯代码更新，它并不依赖一层“字段名 + 精确类型”的对象重建迁移表。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549` | 可以借鉴“优先保留现有对象图，再做显式字段迁移”的思路，把 schema migration 做成 opt-in 扩展层，而不是只有 exact-match copy。 |
| puerts | `ReloadSource()` 调用 JS 侧 reload，`hot_reload.js` 通过 `Debugger.setScriptSource` 原位替换 source，并包裹 `HMR.prepare` / `HMR.finish`。body 更新不会走字段级 rehydrate，自然也不会因为字段 rename/type change 去跑一套隐式对象搬运。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1541`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | Angelscript 仍需面对 `UClass` / layout 迁移，但可以借鉴“把 schema migration 做成显式协议”而不是静默掉值。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 exact-match 迁移表之外，增加 opt-in 的字段别名与类型适配层，把可安全演进的 schema change 从“静默丢值”升级成“显式迁移”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` / `.cpp` 的字段分析阶段新增 `FHotReloadFieldMatch`，除现有 `Name + Type` 外，再支持从 `Meta` 读取 `HotReloadAlias`。<br>2. 在 `PropertiesToCopy` 构建阶段，若 exact-match 失败，则尝试 `Alias -> OldFieldName` 匹配；只有 alias 明确声明时才允许跨名称迁移。<br>3. 新增小型 type adapter registry，只开放少数可证明安全的转换，如整数扩宽、enum 同底层类型迁移、`UObject*` 子类向上兼容；其余类型仍维持当前“拒绝迁移”。<br>4. 为复杂场景提供可选脚本级 hook，例如 `__HotReloadMigrate(FHotReloadMigrationContext&)`，仅在 schema change 且 alias / adapter 仍不足时调用，让脚本自行修复 renamed field 或 external cache。<br>5. 对所有未匹配旧字段生成结构化诊断，明确输出“field dropped because rename/type mismatch/noncopyable”，先让丢状态可见，再逐步扩展 adapter 覆盖面。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadMigration.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadMigration.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadStateTraceTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 类型适配做得过宽会把“不兼容迁移”误包装成“安全迁移”；首版必须坚持 opt-in，只做 alias 和极少数确定安全的 adapter。 |
| 兼容性 | 向后兼容。未声明 alias / adapter 的字段仍保留当前 exact-match 行为；现有脚本不需要修改，只是新增了更强的 opt-in 能力与诊断输出。 |
| 验证方式 | 新增字段 rename、`int32 -> int64`、字段迁入 superclass、noncopyable script type 四类用例；分别验证 alias / adapter 生效时值能保留，未声明时会输出明确 dropped-field 诊断而不是静默失败。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-5 | PIE 下 deferred full reload 的一致性窗口 | 延期状态显式化 + 自动提交机制 | 高 |
| P1 | Arch-HR-6 | schema 演进时的字段级状态迁移 | 状态迁移协议增强 | 中高 |

---

## 架构分析 (2026-04-08 15:04)

### Arch-HR-7：变更检测存在 editor/runtime 双轨语义，runtime checker thread 对删除与 rename 不可见

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载输入收集的一致性与增量边界 |
| 当前设计 | 当前 hot reload 输入并不是单一管线。Editor 下由 `DirectoryWatcher` 订阅所有 script roots，并把 `.as` 文件增改、目录新增展开、目录删除枚举结果直接塞进 `FileChangesDetectedForReload / FileDeletionsDetectedForReload`；非 editor runtime 则启动 checker thread，周期性递归扫描全部 script roots，只比较“当前仍存在的文件”的时间戳。`CheckForHotReload()` 只有在删除队列已被预先填充时才会消费删除，因此 checker thread 路径本身看不到磁盘上已经消失的文件。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78-94,366-381`：静态 `OnScriptFileChanges()` 通过 `FAngelscriptEngine::Get()` 把所有 root watcher 事件送入主引擎。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`：`.as` 增改直接入 `FileChangesDetectedForReload`，目录删除只能依赖 `EnumerateLoadedScripts()` 补全删除队列。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1658-1700`：runtime 使用独立 checker thread，并在启动时先用 `CheckForFileChanges()` 预填时间戳状态。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1999-2015,2870-2895`：checker thread 每轮重新枚举 `AllRootPaths` 下全部 `.as` 文件，只对“当前枚举得到的文件”执行新增/时间戳变更检测，没有对 `FileHotReloadState` 做缺失项 diff。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2749-2755`：删除只有在 `FileDeletionsDetectedForReload` 已有内容时才会进入本轮 `FileList`。 |
| 优点 | Editor 路径对 rename window、目录新增、目录删除都有显式处理，且已有 watcher automation tests；实现偏保守，不容易漏掉普通的文件修改。 |
| 不足 | runtime checker thread 与 editor watcher 语义不一致：非 editor 场景下删除/rename 不会自然形成 reload 输入。另一个问题是两条路径都以“全 script roots”而不是“已加载模块/已激活脚本”作为默认边界，随着脚本量增长，噪声事件和无关扫描会持续放大。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor watcher 只负责触发 `HotReload()`；真正决定 reload 集合的是 Lua 侧 `loaded_module_times`，`M.reload()` 仅枚举已加载过的 module，并按修改时间筛出 `modified_modules`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-36,112-118`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:112-119,604-624` | 把“文件系统事件”与“实际重载集合”分层：前者只负责唤醒，后者以 loaded-module manifest 为准。 |
| puerts | `FSourceFileWatcher` 只有在 source 被加载后才注册目录监听，并为每个 watched file 保存 `FMD5Hash`，只有 hash 变化才通知 reload；编辑器侧 `UPEDirectoryWatcher` 还会把 `Added/Modified/Removed` 三类变化显式分发。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:20-68` | 已加载文件清单 + 内容 fingerprint + 显式删除事件，这三件事一起保证输入边界稳定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入统一的 `HotReloadWatchManifest`，让 editor watcher 与 runtime checker thread 都围绕同一份“已知脚本集 + 指纹”工作，并补齐 checker thread 的删除检测。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `.cpp` 新增 `FHotReloadWatchManifest`，记录 `KnownFiles`、`LastFingerprint`、`bLoadedInActiveModule`、`PendingDeletion`。<br>2. 让 `QueueScriptFileChanges()` 不再直接写裸数组，而是更新 manifest；checker thread 则把当前磁盘扫描结果与 manifest 做 set diff，把“缺失文件”转换成删除事件，即使没有 `DirectoryWatcher` 也能发现 rename/remove。<br>3. 把 `FindAllScriptFilenames()` 的全量递归保留为 fallback，但默认快速路径优先从 active modules / 已加载文件集合生成候选集，只有目录新增或 manifest miss 时才退回全 root discover。<br>4. 将当前纯时间戳比较扩展为 `mtime + file size`，或在 editor 下可选开启 hash 校验，减少保存一次产生多次 modified event 时的误触发。<br>5. 补自动化测试：覆盖 checker-thread 下的删除、rename、inactive plugin script 变更不触发 reload，以及 watcher/checker 两条路径产出相同 `FileList` 的一致性测试。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFileDetectionTests.cpp` |
| 预估工作量 | M |
| 架构风险 | manifest 一旦失真，会把“新文件未发现”或“旧文件误删”放大成 reload 误判；因此首版必须保留现有全量扫描 fallback，并为 manifest miss 打 warning。 |
| 兼容性 | 向后兼容。第一阶段只是收敛输入收集策略，不改变 `PerformHotReload()` 的决策规则；现有脚本与 reload 结果应保持一致，只是 runtime 删除场景会开始被正确发现。 |
| 验证方式 | 在 editor 与 non-editor test harness 下分别执行：新增/修改/删除/rename 四类文件事件，确认产出的 `FileList` 与 `FileDeletionsDetectedForReload` 一致；再验证 inactive script roots 不会被无意义地送入 reload。 |

### Arch-HR-8：hot reload 协调器仍是全局单例，和 engine clone / 多实例模型脱节

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载状态管理的实例隔离与扩展承载点 |
| 当前设计 | runtime 核心已经显式支持 `Full` / `Clone` 两种 engine creation mode，并给每个 engine 分配 `InstanceId`、`SourceEngine`、`SharedState`；但 editor 侧的 hot reload 却仍然围绕全局状态展开。`OnScriptFileChanges()` 通过 `FAngelscriptEngine::Get()` 解析当前环境里的单个引擎，`FClassReloadHelper` 的 `ReloadState()` 返回的是进程级 static singleton，`ReplaceHelper` 也是全局静态对象，所有 reload bookkeeping 都假定“只有一个主引擎和一个当前 session”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:112-136,206-212`：`FAngelscriptEngine` 已暴露 `Full/Clone`、`CreateCloneFrom()`、`GetSourceEngine()`、`GetInstanceId()` 等实例化接口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:628-650`：`CreateCloneFrom()` 会复制 `SharedState`、维护 `ActiveCloneCount` 并生成独立 `InstanceId`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78-94,351-381`：watcher callback 是静态函数，启动模块时只注册一套 root watchers，并直接把事件路由到 `FAngelscriptEngine::Get()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:44-176`：`ReloadState()` 是 function-local static，`Init()` 安装的也是全局 lambda。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:20-38`：`GAngelscriptUseUnrealReload` 与 `ReplaceHelper` 都是进程级静态变量。 |
| 优点 | 当前 editor 工作流下实现简单，单主引擎场景没有额外的生命周期管理负担；大部分历史代码都可以假设“ambient engine 就是目标 engine”。 |
| 不足 | 这套设计直接绕开了 runtime 已经具备的实例模型。只要未来需要让 clone engine、隔离测试引擎、或多个 world-context 下的 engine 各自做 hot reload，当前 watcher、reinstance 状态和 replacement helper 就会互相踩踏。进一步说，native `ReloadCompleteDelegate` / `OnHotReload()` 这类桥接也没有稳定落点，因为现有 helper 不是一个 engine-owned service。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ULuaEnvLocator_ByGameInstance` 会为不同 `GameInstance` 维护独立 `LuaEnv`，`HotReload()` 不是写死到单一全局环境，而是广播给 default env 和每个已登记 env。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:40-82` | 即使入口仍是统一接口，真正的 reload 执行器也应该按 env / instance 维度组织，而不是靠全局静态状态。 |
| puerts | `FPuertsModule::MakeSharedJsEnv()` 支持单 `FJsEnv` 与 `FJsEnvGroup` 两种模式；native reload 回调里重建的是当前模块持有的 env container，而不是散落在 editor helper 里的静态状态。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185-244,424-438` | 把 hot reload state 挂在 runtime owner 上，可以同时支持单实例与多实例扩展。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 editor 侧的静态 watcher / reload bookkeeping 收拢成 `engine-owned coordinator`，让 reload session 成为显式对象，而不是散落在全局 static 里。 |
| 具体步骤 | 1. 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptHotReloadCoordinator.h/.cpp`，由它持有 watcher handles、`ReplaceHelper`、session id 和一份实例级 `FReloadState`。<br>2. 将 `FAngelscriptEditorModule::StartupModule()` 中的静态 `OnScriptFileChanges` 绑定改为 coordinator 方法，默认先给 primary engine 创建一个 coordinator；`CreateCloneFrom()` 或 test harness 需要 hot reload 时可显式注册自己的 coordinator。<br>3. 把 `FClassReloadHelper` 从 static utility 演进成 coordinator 的成员服务：提供 `BeginSession()`、`RecordClassReload()`、`RecordStructReload()`、`FinalizeSession()`，并保留现有静态入口作为向后兼容 adapter。<br>4. 扩展 `FAngelscriptClassGenerator` 的 reload 广播，给事件附带 engine/session 身份，避免不同 engine 的 reload 共享同一份全局 `ReloadState`。<br>5. 第二阶段再把 native reload bridge 接到 coordinator 上，而不是继续增加新的全局静态对象；这样 `ReloadCompleteDelegate`、`Live Coding`、script file reload 最终都能落到同一个实例级 session 模型。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptHotReloadCoordinator.h`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptHotReloadCoordinator.cpp` |
| 预估工作量 | L |
| 架构风险 | 核心风险是 delegate 生命周期与 engine 生命周期脱钩，尤其在 editor 模块卸载、test engine 销毁、或 source engine 先释放时容易留下悬空回调；必须把注册/反注册绑定到 coordinator 析构。 |
| 兼容性 | 向后兼容。第一阶段可以只在内部引入 coordinator，并保留当前 `FClassReloadHelper::Init()` 和 `FAngelscriptEngine::Get()` 入口作为 adapter；现有脚本与 editor 操作方式不需要变化。 |
| 验证方式 | 增加 clone-engine 测试：同时创建 primary engine 与 clone engine，分别触发 reload，验证各自的 watcher state、`ReloadState`、`ReplaceHelper` 不会串台；再验证 editor 关闭模块或 engine 销毁时 watcher 能正确解绑。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-7 | editor/runtime 变更检测语义分叉与 runtime 删除盲区 | 检测管线统一 + manifest 化 | 高 |
| P1 | Arch-HR-8 | hot reload 状态的实例隔离与扩展承载点 | 协调器重构 | 中高 |

---

## 架构分析 (2026-04-08 15:14)

### Arch-HR-9：`default` 与 `CPP_Default_*` 变更被降为“建议 full reload”，soft path 会继续运行旧默认语义

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 默认值语义的增量重载边界 |
| 当前设计 | 当前架构把 `default` 语句、函数参数默认值、以及大部分 metadata 变化都归入 `FullReloadSuggested`，允许模块在 `SoftReloadOnly` 或 deferred full reload 场景下先继续运行；但 `DoSoftReload()` 明确保留旧 `DefaultsCode`，也不会重写 `CPP_Default_*` 或多数 `UFunction/UClass` metadata。结果是：代码 body 可以先换，默认值语义与部分 Blueprint 反射信息仍停留在旧版本，直到真正的 full reload。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1222-1237`：参数默认值或参数名变化只提升到 `FullReloadSuggested`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1252-1259,1311-1323`：函数 metadata、类 metadata、类 flags 变化也只是 `FullReloadSuggested`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1290-1295`：`DefaultsCode` 变化只建议 full reload。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4140-4141`：`DoSoftReload()` 直接把 `ClassDesc->DefaultsCode` 设回旧版本。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4244-4275,4779-4791`：soft path 只重绑 `ScriptFunction` 并刷新参数类型，不会重建 `UFunction` 或回写 `CPP_Default_*` metadata。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3995-4003`：`CPP_Default_*` 只在 full reload 的 `AddFunctionArgument()` 路径里写入。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5807-5840`：只有 `ShouldFullReload()` 为真的类才会重新 `InitDefaultObject()`。<br>`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp:265-301`：现有测试也把默认值更新验证放在 full reload 场景。 |
| 优点 | 允许编译链在默认值或 UI metadata 变化时先保住运行，减少“为了一个默认参数就硬阻塞 body 热更新”的频率。 |
| 不足 | `FullReloadSuggested` 现在混合了“可立即应用的代码变化”和“必须重建反射/默认值语义的变化”。这会让编辑器、Blueprint 节点默认 pin、以及新建对象的默认值在一段时间内继续暴露旧行为。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `HotReload()` 直接进入 Lua 侧 `UnLua.HotReload()`；`update_modules()` 会把新函数回写到旧 module table，再统一 `merge_objects()` 和 `update_global()`。热更新边界集中在 module/runtime object graph，没有一层“代码已更新但 UE 反射默认值仍旧”的中间状态。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549,553-625` | 把可热更新面限定在 runtime 语义上；不尝试在成功热更新后保留一套陈旧的反射默认值契约。 |
| puerts | `ReloadSource()` 只把 path 和 source 交给 JS 侧 `__reload`；`hot_reload.js` 用 `Debugger.setScriptSource` 原位替换源码，并在前后发 `HMR.prepare/finish`。更新面是 source/module 本身，而不是半更新的 `UFunction` metadata。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1541`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | 先把 HMR 面压缩到 source patch，再决定是否需要额外的 UE 桥接，避免“脚本变了、反射默认值没变”的灰色区。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `FullReloadSuggested` 再拆成“可安全软刷新默认值/metadata”的子类和“必须强制 full reload”的子类，先收敛默认值语义漂移。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 为 class/function/property delta 增加 `EDefaultSemanticDelta`，至少区分 `DefaultsOnly`、`FunctionDefaultArgOnly`、`ReflectionMetaOnly`、`UnsafeReflectionContract`。<br>2. 对 `default` 语句只改赋值表达式、且 property 集合与类型未变的场景，新增 `DoDefaultsReplayReload()`：不复用旧 `DefaultsCode`，而是对 base CDO 与派生 CDO 重放新的默认赋值，再按现有差异复制规则回填 live instance。<br>3. 对函数默认参数和纯展示型 metadata，新增 `RefreshFunctionMetadataSoft()` / `RefreshClassMetadataSoft()`：显式更新 `CPP_Default_*`、`DisplayName`、`ToolTip`、`Category` 等安全键，并刷新受影响的 Blueprint action database。<br>4. 对会改变节点形状或调用契约的 metadata/flags，例如 `BlueprintCallable`、`BlueprintPure`、`WorldContext`、`Latent`、`Exec`，直接从 `Suggested` 提升到 `FullReloadRequired`，不再允许 soft path 继续带旧反射契约运行。<br>5. 先以 `CVar` 保护 `DefaultsOnly/ReflectionMetaOnly` 快速路径；补自动化测试覆盖“只改 default 语句”“只改 `CPP_Default_*`”“只改 `DisplayName`”“改 `BlueprintCallable`”四类分流。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果把“看似 defaults-only”的改动误判成可软刷新，可能跳过本该重建的 Blueprint 节点或 CDO；首版必须保守，只对白名单 metadata 与不改 property 集合的 `default` 赋值开放。 |
| 兼容性 | 向后兼容。第一阶段可以只把危险 metadata 从 `Suggested` 提升为 `Required`，不改变现有脚本语法；第二阶段再以 opt-in 方式启用 soft metadata/defaults replay。 |
| 验证方式 | 新增测试验证：`SoftReloadOnly` 下 body 改动仍可立即生效；仅改 `default` 赋值时，新建对象默认值能更新；仅改 `CPP_Default_*`/`DisplayName` 时 Blueprint 节点刷新到新默认值/显示名；改 `BlueprintCallable` 或 `WorldContext` 时必须返回 `ErrorNeedFullReload` 或进入 `FullReloadRequired`。 |

### Arch-HR-10：`DefaultComponent/OverrideComponent` 描述符只在 full reload 重建，soft path 会保留旧 actor 构造契约

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | actor/component 构造描述符与状态保持 |
| 当前设计 | Angelscript 把 actor 的组件构造契约缓存到 `UASClass::DefaultComponents` / `OverrideComponents`，`StaticActorConstructor()` 再据此创建 CDO 与实例组件。但属性 metadata 变化只会得到 `FullReloadSuggested`；soft path 并不会重新生成这两张描述表，只会修正已有条目的 offset。于是像 `Attach`、`AttachSocket`、`RootComponent`、`EditorOnly` 这类现有组件属性 metadata 的改动，在 soft reload 或 deferred full reload 窗口里不会落到新 CDO 和新生成 actor 上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1176-1345`：`StaticActorConstructor()` 完全依赖 `OverrideComponents` 与 `DefaultComponents` 来设置 override class、创建默认组件、建立 attach/root 关系。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1117-1124`：property metadata 变化只提升到 `FullReloadSuggested`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4181-4197`：soft reload 只更新 `DefaultComponents/OverrideComponents` 里已有条目的 `VariableOffset`，没有重建 `ComponentClass`、`Attach`、`bIsRoot`、`bEditorOnly` 等描述。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5214-5445`：`FinalizeActorClass()` 才会从 property metadata 重新收集并填充 `DefaultComponents/OverrideComponents`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2284-2294,5807-5840`：`FinalizeClass()` 与 `InitDefaultObjects()` 只对 `ShouldFullReload()` 为真的类执行；soft path 不会重建 actor CDO。 |
| 优点 | 组件构造规则被集中到 `UASClass` 描述表，actor 构造过程稳定、可验证，也便于 editor 校验 attach/root 约束。 |
| 不足 | 当前“建议 full reload”与“实际还在跑旧构造契约”之间存在明显鸿沟。推断上，只要团队允许 deferred full reload 或 `SoftReloadOnly` 工作流，组件 attach/root/editor-only 之类的改动就会在一段时间内只更新源码，不更新 archetype 与新建 actor 的组件树。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载核心是 module table/function/upvalue merge；`update_modules()` 不维护一份独立的 `UClass` 组件描述缓存，HMR 作用面集中在脚本运行态对象图。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549,553-625` | 把热更新边界收敛到 runtime 层，避免“缓存的构造描述符”和“脚本源码”在同一 session 中脱节。 |
| puerts | `ReloadSource()` + `Debugger.setScriptSource` 直接替换 JS source，HMR 事件只围绕 module/source 生命周期展开，没有额外的 actor 组件描述表需要和热更新状态保持同步。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1541`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | 如果某类构造语义不能被安全增量更新，应尽早把它判成结构性变更，而不是维持一个陈旧的中间缓存。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 component descriptor 变化从 `Suggested` 提升为结构性变更；后续若要追求增量，再单独做受控的 actor component reconcile。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 的 property diff 阶段，对 `DefaultComponent`、`OverrideComponent`、`RootComponent`、`Attach`、`AttachSocket`、`EditorOnly` 这些 key 做专门检测；只要发生变化，就直接把 `ReloadReq` 至少提升到 `FullReloadRequired`。<br>2. 第一阶段不要尝试 soft path 修 descriptor，只保证一旦构造契约变化，就一定走 `FinalizeActorClass()` + `InitDefaultObject()`。这一步最小、最安全，也与现有实现完全兼容。<br>3. 第二阶段若确实需要增量，新增 `RebuildActorComponentDescriptorsSoft()`：清空并按新 property descriptor 重建 `Class->DefaultComponents` / `OverrideComponents`，随后对 CDO 走专门的 component template 重建，而不是只更新 offset。<br>4. 对 live actor instance，引入单独的 component reconcile pass：优先只处理“同名同类型、attach 变化”的安全场景；需要新增/删除组件、root 切换、editor-only 切换时仍回退到 full reload。<br>5. 增加结构化诊断，明确输出“此变更影响 actor construction contract，已强制 full reload”，避免用户误以为 soft reload 已经带上了新组件树。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadComponentDescriptorTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 若直接在 soft path 动态重建组件模板，最容易破坏现有 CDO、Blueprint child CDO 和 live actor 上的 component instance data；因此必须先做“强制 full reload”收口，再逐步开放极少数安全的 descriptor delta。 |
| 兼容性 | 向后兼容。第一阶段只是把原本会产生旧构造契约漂移的场景更早地判成 full reload；脚本语法、组件声明方式和现有 actor class 定义都不需要改变。 |
| 验证方式 | 增加 `Attach` 变更、`RootComponent` 切换、`EditorOnly` 切换、`OverrideComponent` 类型保持但 metadata 变化四类测试；验证这些场景不再停留在 soft path，且 full reload 后新 CDO、新建 actor、Blueprint child actor 的组件树都与新脚本一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-10 | actor `DefaultComponent/OverrideComponent` 构造契约漂移 | 结构性分流收紧 + 后续受控增量化 | 高 |
| P1 | Arch-HR-9 | `default` / `CPP_Default_*` / 展示型 metadata 的延迟生效 | 默认值回放 + metadata 软刷新 | 中高 |

---

## 架构分析 (2026-04-08 15:25)

### Arch-HR-11：`game world / non-editor` 路径会把结构性变更永久降级成 deferred full reload，运行态缺少排空点

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 运行时热重载能力边界与 deferred full reload 的生命周期 |
| 当前设计 | Angelscript 把 `FullReload` 明确绑定到“无 game world 的 editor 场景”。一旦处于 `game world` 或非编辑器环境，tick 只会走 `SoftReloadOnly`；而任何 `PartiallyHandled` / `ErrorNeedFullReload` 都会继续把文件压进 `QueuedFullReloadFiles`，这些文件只有在后续某次 `CompileType != SoftReloadOnly` 时才会被重新消费。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2819-2828`：`!GIsEditor || HasGameWorld()` 时固定调用 `CheckForHotReload(ECompileType::SoftReloadOnly)`，注释直接写明只有 editor object 才能用 Unreal hot reload 机制做 full reload。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2758-2763`：只有 `CompileType != ECompileType::SoftReloadOnly` 时才会把 `QueuedFullReloadFiles` 加回本轮 `FileList`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4168-4186`：`ErrorNeedFullReload` 和 `PartiallyHandled` 都会把本轮编译文件重新压入 `QueuedFullReloadFiles`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-353` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:50-175`：真正的 reinstance 协调器只在 `AngelscriptEditor` 模块里初始化，运行态没有对应 consumer。 |
| 优点 | 对 PIE 和运行中世界更保守，不会在对象图活跃时直接触发 `UClass` reinstance，降低 live gameplay 场景下的崩溃风险。 |
| 不足 | 当前 deferred 语义默认假设“稍后一定还能回到 full reload 场景”。推断上，在长期运行的 `game world` 会话或非编辑器桌面应用里，这个队列可能长期不被消费，结构性变更会持续停留在“旧逻辑继续跑、文件反复排队”的状态。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv::HotReload()` 直接执行 `UnLua.HotReload()`；`ULuaEnvLocator_ByGameInstance::HotReload()` 会把热重载广播到 default env 和每个 `GameInstance` env。Lua 侧随后对 modified module 立即 `reload_modules()`，并更新 running stack、`_G`、registry。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:76-82`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:381-477,604-624` | 运行时能力边界是显式的 env reload，而不是把结构性变更静默排队等待 editor-only 时刻。 |
| puerts | `FSourceFileWatcher` 只监听已加载的 `.js` source，变化后直接回调 `ReloadSource()`；运行时 `FJsEnvImpl::ReloadSource()` 调 JS 侧 `__reload`，`hot_reload.js` 用 `Debugger.setScriptSource` 原位替换脚本。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | 把 runtime HMR 设计成一等能力；即便 native 侧不能 reinstance `UClass`，也不会形成“只有排队、没有出队点”的悬空状态。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 deferred full reload 从“隐式队列”升级成“显式能力边界”，再给 runtime 宿主一个可选的排空策略。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `EDeferredReloadReason` 和 `FPendingFullReloadBatch`，记录哪些文件因为 `SoftReloadOnly` 被延后、当前是否存在消费条件。<br>2. 当 `CompileType == ECompileType::SoftReloadOnly` 且结果为 `PartiallyHandled` / `ErrorNeedFullReload` 时，除了现有入队，再通过 runtime/editor delegate 发出结构化告警；第一阶段只做可观测性，不改行为。<br>3. 在 editor 场景里增加明确的排空触发点：例如从 `HasGameWorld()==true` 过渡到 `false` 时自动调用一次 `CheckForHotReload(ECompileType::FullReload)`，而不是只等下一次普通 tick 恰好遇到。<br>4. 第二阶段为非编辑器宿主新增可选接口，例如 `IRuntimeStructuralReloadFallback`；默认实现仍是 `warn + keep old code active`，宿主若愿意可接入“重建 script env / 重启 subsystem”的自定义策略。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 预估工作量 | M |
| 架构风险 | 如果自动排空点设计不当，可能在刚退出 PIE 或 world teardown 的边界重复触发 reload；需要 batch id 或 debounce，确保同一批 deferred 文件只被消费一次。 |
| 兼容性 | 向后兼容。第一阶段仅增加状态暴露和告警；第二阶段的 runtime fallback 以 opt-in 接口接入，不改变默认的保守策略。 |
| 验证方式 | 新增测试覆盖 `HasGameWorld()==true` 时 structural change 返回 `PartiallyHandled/ErrorNeedFullReload` 后队列累积；退出 game world 后验证队列被自动消费。再加一个 non-editor 模拟测试，确认至少会输出显式 `deferred full reload pending` 诊断，而不是静默停留。 |

### Arch-HR-12：版本链消费是“局部追新 + 大量透传/过滤”，`TSubclassOf` 与绑定 API 可长期持有旧类句柄

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `NewerVersion` 版本链的统一消费与 stale class handle 风险 |
| 当前设计 | Angelscript 在 full reload 时给旧 `UASClass` 打上 `CLASS_NewerVersionExists` 并串 `NewerVersion` 链，但“追到最新类”并不是统一基础设施，而是零散地由少量调用点手工完成。更关键的是，`TSubclassOf` 写路径已经允许旧类临时通过校验，读路径和实例化路径却大多继续返回或使用原始旧类指针。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:18-19,76` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:912-923`：`UASClass` 仅通过 `NewerVersion` 指针链提供 `GetMostUpToDateClass()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2576-2578,3695-3700`：full reload 时旧类被 rename、打 `CLASS_NewerVersionExists`，并把 `NewerVersion` 指向新类。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1184-1189,1217-1222`：默认组件和 override component 构造时会手工追 `GetMostUpToDateClass()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h:80-97`：`SetClass()` 允许“旧类将来会变正确”的情况通过校验，但最终仍把原始 `InClass` 写进 `TSubclassOf`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h:105-137`：`GetClass()` 与 `GetDefaultObject()` 直接返回 `Ptr->Get()` 及其 `CDO`，没有追版本链。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:337-377,556-578`：`GetAllClasses()/GetAllSubclassesOf()` 只是过滤掉 `CLASS_NewerVersionExists`，`NewObject()` 则直接用 `Class.Get()` 实例化。 |
| 优点 | 当前实现改动面小，不会在整个引擎层强制篡改所有 `UClass*`；对旧有绑定的侵入度低。 |
| 不足 | 版本链语义泄漏到了每个 binding call-site。推断上，一个在 reload 窗口内保存下来的 `TSubclassOf<UObject>` 或缓存的 `UClass*` 可能通过 setter 校验，但后续 `GetDefaultObject()` / `NewObject()` 仍继续使用旧类；另一些 API 又只是简单把旧类隐藏掉，形成“有的地方自动追新，有的地方继续透传旧句柄”的非对称行为。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 不是让各个 call-site 自己判断“这是旧函数还是新函数”，而是在 `update_modules()` 里统一更新 module table，再通过 `update_running_stack()` / `update_table()` / `update_global()` 回写 running stack、userdata、upvalue、`_G` 与 registry 里的旧引用。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:381-477,480-549` | “句柄修正”集中在单一 reload pass 完成，而不是把追新责任散给每个绑定 API。 |
| puerts | JS source 热更新统一收敛到 `ReloadSource()` -> `puerts.__reload()`；`hot_reload.js` 在 `HMR.prepare` / `HMR.finish` 包围下对目标 script 执行 `Debugger.setScriptSource`。native hot reload 时，模块直接 `MakeSharedJsEnv()` 并 `RebindJs()`，重新建立统一 env。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185-244,424-438` | 统一把“最新定义”收敛到 env/module 边界，避免读取路径、实例化路径、枚举路径各自实现一套旧句柄判断。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 runtime core 增加统一的 `ResolveLatestASClass()`，先把版本链 canonicalization 收口，再逐步替换散落的 ad-hoc 过滤逻辑。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h/.cpp` 或 `Core/AngelscriptEngine.*` 新增 `static UClass* ResolveLatestASClass(UClass* InClass)`，仅对 `UASClass` 且带 `NewerVersion` 的类做追链。<br>2. 第一批接入 `Bind_TSubclassOf.h` 的 `SetClass()`、`GetClass()`、`GetDefaultObject()`，以及 `Bind_UObject.cpp` 的 `NewObject()`；确保写入、读取、实例化三条路径都使用同一套 canonicalization。<br>3. 第二批替换 `Bind_UClass` 里“只过滤旧类”的枚举逻辑：对外优先返回最新类，必要时保留一个 `bIncludeReplacedClasses` 调试开关。<br>4. 在 `ClassReloadHelper` 或 post-reload pass 中新增 `StaleClassHandleAudit`，统计并日志输出被自动 canonicalize 的 `TSubclassOf` / asset / open editor 引用，首版以 `warn-only` 观察真实命中率。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` |
| 预估工作量 | M |
| 架构风险 | 自动 canonicalize 会改变少数依赖“旧类对象身份仍可见”的调试或工具行为；因此首版应限定只处理 `UASClass`，并保留调试开关查看 replaced class。 |
| 兼容性 | 基本向后兼容。默认只把旧 `UASClass` 句柄提升为最新类，不改变脚本语法；需要观察的仅是外部工具若刻意枚举 `_REPLACED_` 类名时行为会变化。 |
| 验证方式 | 增加测试覆盖：在 full reload 后让旧 `TSubclassOf` 句柄调用 `GetDefaultObject()` / `NewObject()`，确认得到的是最新类；再验证 `GetAllClasses()` 默认不再暴露 replaced class，但开启调试开关时仍可检查版本链。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-11 | `game world / non-editor` 下 deferred full reload 的永久化风险 | 能力边界显式化 + 运行态排空策略 | 高 |
| P1 | Arch-HR-12 | `NewerVersion` 版本链的统一消费与 stale class handle | 统一 canonicalization + 诊断审计 | 中高 |

---

## 架构分析 (2026-04-08 15:38)

### Arch-HR-13：`DirectoryWatcher` 的输入边界是“script root 下任意 `.as`”，新增/修改与删除的收敛策略并不对称

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 变更检测输入粒度与无效热重载噪声 |
| 当前设计 | 当前 watcher 以 script root 为边界：单文件新增/修改的 `.as` 直接入 `FileChangesDetectedForReload`，新目录则立即扫描其下全部 `.as`；只有目录删除时才会退回到 `GetActiveModules()` 上，按“当前已加载 code section”枚举受影响脚本。随后 `CheckForHotReload()` 不再区分文件是否已加载，直接把队列交给 `PerformHotReload()`，并继续送入 preprocessor 和编译队列。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:26-41`：`GatherLoadedScriptsForFolder()` 只从 `Engine.GetActiveModules()` 的 `Module->Code` 收集已加载脚本。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`：`.as` 新增/修改直接入队；目录新增调用 `FindScriptFiles()` 扫描全部 `.as`；只有目录删除才调用 `EnumerateLoadedScripts()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2770`：`CheckForHotReload()` 直接消费 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload`，形成 `FileList` 后立刻 `PerformHotReload()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2450-2453,2467-2468`：本轮 `FileList` 会逐个 `Preprocessor.AddFile()`，再调用 `CompileModules()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3095-3105`：实现注释明确写着“Always compile every module in the list”，说明 watcher 输入会直接决定后续编译前沿。 |
| 优点 | 新脚本文件发现足够激进，目录 rename/add/remove 语义简单，开发者把新 `.as` 放进脚本根后几乎立刻就能进入现有热重载链路。 |
| 不足 | 输入边界早于模块解析，且新增/修改与删除路径不对称。推断上，位于脚本根但当前并未参与运行态模块图的 `.as` 文件，只要被保存或批量复制，也会进入 preprocess/compile；同时当前没有 puerts 式内容哈希去重，内容未变的保存噪声也无法在 watcher 层被抑制。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载前沿先收敛到 `require` 过的 module。Lua 侧自定义 `require()` 时把 module 放进 `loaded_modules` / `loaded_module_times`；执行 `M.reload()` 时，无参数模式只遍历 `loaded_module_times`，仅将时间戳变化的 module 送进 `reload_modules()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp:266-269`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-176,604-623` | “已加载 module 集合”是热重载的一等输入，而不是把脚本根下的任意文件变化都直接抬进 reload pipeline。 |
| puerts | watcher 只在 source 真正被加载后才注册目录和文件；`OnDirectoryChanged()` 只处理已登记的 `.js`，并用 `FMD5Hash::HashFile()` 比较新旧内容，哈希不变则不回调 `OnWatchedFileChanged()`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80` | 把“已加载 source 集合 + 内容哈希”放在 filesystem 事件之前，能明显降低无效 HMR 噪声。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `DirectoryWatcher` 与 `PerformHotReload()` 之间插入一层 `ActiveScriptIndex + ContentFingerprint`，先把“任意文件变化”收窄为“活跃脚本变化”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 增加 `ActiveScriptIndex`，由 `GetActiveModules()` / 最近一次成功编译的 `Module->Code` 维护 `AbsoluteFilename -> ModuleName` 映射。<br>2. 改造 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`：对单文件新增/修改先查 `ActiveScriptIndex`，命中才进入 `FileChangesDetectedForReload`；未命中的文件先进入 `PendingDiscoveryScripts`，仅做诊断或等待后续 import 命中再升级。<br>3. 对目录新增保留扫描，但扫描结果只把命中 `ActiveScriptIndex` 或“被本轮 changed module import 到”的脚本升格成真正热重载输入，其余文件只更新发现缓存。<br>4. 在 `AngelscriptEngine` 为每个活跃脚本记录 `mtime` 或 `hash`，保存时若内容未变则直接吞掉事件；首版优先使用 cheap `mtime + size`，后续再视命中率补 `MD5`。<br>5. 通过 `CVar` 提供 `angelscript.HotReloadActiveFilesOnly` 的 opt-in 开关，并新增 trace/log，输出“本次因 inactive file 被忽略”的原因，先以观测模式收集真实工程命中率。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把“刚新增、尚未进入活跃索引，但本应立即参与下一轮编译”的脚本误判为 inactive；因此首版必须保守，对新文件保持 `warn-only` 或 fallback 到手动 full reload。 |
| 兼容性 | 向后兼容。默认仍可维持当前激进行为；`ActiveFilesOnly` 先以 opt-in 和诊断模式落地，不破坏现有脚本项目的自动发现习惯。 |
| 验证方式 | 增加回归：1. 修改活跃 `.as` 仍会正常入队；2. 修改脚本根下未被活跃模块引用的 `.as` 不再触发实际热重载；3. 对内容未变的保存事件验证不会进入 `PerformHotReload()`；4. 目录新增同时包含 active / inactive 脚本时，只应提升 active 命中的子集。 |

### Arch-HR-14：`angelscript.UseUnrealReload` 目前不是“同语义后端切换”，而是会绕过 Angelscript 专用修补链

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 与 UE 原生 reload / Live Coding 的后端耦合方式 |
| 当前设计 | `FClassReloadHelper::PerformReinstance()` 默认走自定义 reinstance 流，先修 pin/type、DataTable、delegate 依赖，再调用 `FBlueprintCompilationManager::ReparentHierarchies()` 和 Blueprint 重新编译；只有把 `angelscript.UseUnrealReload` 打开后，才改走 `FReload(EActiveReloadType::Reinstancing)`。这不是纯粹的 backend swap，因为大量 Angelscript 专用补丁只存在于默认分支。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:20-23`：源码直接注明 “new unreal reload system is not yet up to providing for AS reloads”，并暴露 `angelscript.UseUnrealReload`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:32-39,104-115,132-175`：`ReloadState` 会累计 `ReloadClasses`、`ReloadStructs`、`ReloadDelegates`、`NewDelegates`、`ReloadAssets`，并在 `OnFullReload` 时统一进入 `PerformReinstance()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:42-163`：默认路径会扫描 loaded Blueprint、替换 struct/enum pin type、收集 delegate 影响，并修补 `UDataTable::RowStruct`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:181-299`：默认路径还会 `ReparentHierarchies()`、按依赖刷新节点并 `QueueForCompilation()` / `FlushCompilationQueueAndReinstance()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:319-329`：`UseUnrealReload` 分支只把 `ReloadStructs`、`ReloadClasses`、`ReloadEnums` 送给 `FReload->NotifyChange()`；这里没有把 delegate 依赖修补、DataTable 行结构替换和 Blueprint dependency 刷新复用进去。 |
| 优点 | 通过一个 `CVar` 就能快速验证 UE 自带 reloader 的可行性，为后续接原生 reload / Live Coding 预留了试验入口。 |
| 不足 | 当前 `UseUnrealReload` 改变的不只是“由谁执行 reinstance”，而是整条补丁链的语义。结果是：团队一旦用它评估 UE 原生 reload，观测到的失败既可能来自 `FReload`，也可能只是因为 Angelscript 默认分支里的 Blueprint / DataTable / delegate 修补没有被复用。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 只有一条热重载执行路径：`FLuaEnv::HotReload()` 统一执行 `DoString("UnLua.HotReload()")`，`ULuaEnvLocator` / `ULuaEnvLocator_ByGameInstance` 只是把同一条路径广播到一个或多个 `LuaEnv`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:28-33,76-82` | native 入口和普通入口不应拥有两套行为分叉的修补链；最小可维护单位应是“单一路径，多宿主广播”。 |
| puerts | `MakeSharedJsEnv()` 始终以相同方式重建 env 并 `RebindJs()`；当 UE5 `ReloadCompleteDelegate` 或 UE4 `OnHotReload()` 触发时，也只是再次调用同一个 `MakeSharedJsEnv()`，不会切换到另一套缩水版 reload 流程。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185-242,424-438`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1618-1645` | 原生 C++ reload 只负责“什么时候触发”，真正的重绑逻辑仍保持单一路径，从而避免 backend skew。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `PerformReinstance()` 拆成“共享修补阶段 + 可替换 reinstance backend”，先消除行为分叉，再谈接 UE 原生 reload / Live Coding。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` 内把当前默认分支拆成几个显式阶段：`CollectLoadedBlueprintImpact()`、`PatchStructEnumDelegateUses()`、`PatchLoadedDataTables()`、`RunReinstanceBackend()`、`RefreshDependentBlueprints()`、`RefreshEditorUI()`。<br>2. 定义一个极小的 backend 适配层，例如 `EAngelscriptReinstanceBackend::LegacyHierarchy / UnrealReload`，让两条路径只在 `RunReinstanceBackend()` 内分叉；其余阶段全部共用。<br>3. 在 `UnrealReload` backend 首版中保守回退：只要本轮存在 `ReloadDelegates`、受影响 Blueprint、或 `ReloadStructs` 命中 DataTable，就强制退回 legacy backend，并打印结构化原因，避免开发者把“缺失补丁”误判为 “UE reload 不可用”。<br>4. 为 `angelscript.UseUnrealReload` 增加对照测试：同一组 class/struct/delegate 场景分别在 `0/1` 下运行，断言 Blueprint 节点、DataTable、编译队列和 `OnPostReload` 后状态一致。<br>5. 第二阶段再新增真正的 native bridge：在 Editor 侧订阅 UE5 `FCoreUObjectDelegates::ReloadCompleteDelegate`、UE4 `IHotReloadInterface::OnHotReload()`，并在支持的平台上可选接 `ILiveCodingModule` 的 patch-complete delegate，但统一只触发上面那条共享修补管线；同时加入 batch id / debounce，避免 AS reload 与 native reload 在同一帧双重执行。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadUnrealReloadParityTests.cpp` |
| 预估工作量 | L |
| 架构风险 | 共享管线拆分时最容易出现“双重修补”或“修补顺序变化”问题，尤其是在 native reload 和 Angelscript full reload 可能同帧相遇时；必须用 batch id 明确一轮 reload 只消费一次。 |
| 兼容性 | 向后兼容。默认仍保留现有 legacy 分支；`UseUnrealReload` 在具备 parity 之前继续视为实验特性，不改变现有脚本 API 和默认热重载行为。 |
| 验证方式 | 1. 对同一组 class/struct/delegate 改动分别在 `angelscript.UseUnrealReload=0/1` 下运行，确认 Blueprint 节点刷新、DataTable `RowStruct`、编译结果一致。<br>2. 触发一次 native C++ Hot Reload / Live Coding patch，验证 Angelscript 只执行一轮共享 post-reload pipeline。<br>3. 对存在 delegate 影响的脚本变更，验证 `UseUnrealReload=1` 时要么正确修补，要么显式 fallback 并给出原因。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-14 | `angelscript.UseUnrealReload` 与原生 reload / Live Coding 的后端分叉 | 共享修补管线 + backend 抽象 | 高 |
| P2 | Arch-HR-13 | watcher 输入边界过宽导致的无效热重载噪声 | 输入前沿收窄 + 内容去重 | 中高 |

---

## 架构分析 (2026-04-08 15:55)

### Arch-HR-15：`CDONoDefaults` 依赖进程级构造开关与初始化栈，热重载构造上下文不是 `thread-scoped`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载临时构造状态的隔离性与重入安全 |
| 当前设计 | 为了给 `SoftReload` 构造 `CDONoDefaults`，当前实现会先把全局 `GConstructASObjectWithoutDefaults` 置为 `true`，再调用 `NewObject` 创建临时对象；与此同时，script allocation 路径又依赖进程级 static `CurrentObjectInitializers` 追踪“当前正在补完构造的脚本对象”。这两份状态都不是 `thread_local`，而同文件里其他构造相关上下文却已经是 `thread_local`。换句话说，hot reload 的“跳过 defaults”与“谁在构造中”目前是 ambient global state，不是线程/对象作用域状态。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4093-4108`：`PrepareSoftReload()` 通过 `GConstructASObjectWithoutDefaults = true` + `NewObject` 构造 `CDONoDefaults`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:987-988`：`CurrentObjectInitializers` 与 `GConstructASObjectWithoutDefaults` 都是进程级 static。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:39-40,1011-1020`：同文件中的 `GIsInAngelscriptThreadSafeFunction`、`GIsAngelscriptWorldContextAvailable` 与 `GASDefaultConstructorOuter` 却是 `thread_local` / RAII。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1075-1079,1140-1168`：script allocation 通过 `CurrentObjectInitializers` push/pop 追踪构造收尾。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1360-1361,1416-1417,1468-1469`：actor/component/object constructor 都通过读取该全局开关决定是否执行 defaults，并在入口处直接复位为 `false`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:72-75`：源码明确说明 default statements 可能运行在 async loading thread。 |
| 优点 | 现有实现侵入小，复用现有 constructor 路径即可构造“无 defaults 的临时 CDO”，不需要重写 `UASClass` 的初始化协议。 |
| 不足 | 推断上，只要出现嵌套构造、异步构造、或未来更积极的并行编译/重载，这种 ambient global 就可能被错误消费或过早复位。最坏情况下，非目标对象会意外跳过 defaults，或 `CurrentObjectInitializers` 在并发场景里串栈；这类问题很难从业务层定位，因为外部只会看到“某些对象偶发没有跑 default/constructor 语义”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载的工作集被封装在 `reload_modules()` 的局部变量里：`tmp_modules`、`old_modules`、`new_modules`、`module_envs` 都是本次 reload 调用的局部状态，并由 `sandbox.enter()` / `sandbox.exit()` 成对包围。`FLuaEnv::HotReload()` 只是把控制权交给当前 env。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549,565-601` | 把 reload 过程状态绑定到一次调用 / 一个 env，而不是暴露成进程级可变开关。 |
| puerts | 编辑器热更新直接把 `Path + JsSource` 传给 `ReloadSource()`，由 `ReloadJs->Call()` 调到 JS 侧 `__reload`；native reload 则在模块持有的 env container 上调用 `MakeSharedJsEnv()`。无论是 source patch 还是 native reload，都没有“影响下一次任意对象构造”的全局布尔开关。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:424-438` | 让 reload 上下文挂在 env / module owner 上，避免和任意对象构造共享同一份 ambient state。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前 `GConstructASObjectWithoutDefaults + CurrentObjectInitializers` 的 ambient protocol 收敛为 `thread-local + RAII` 的构造上下文。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` 新增 `FScopedASConstructionContext`，至少携带 `bSkipDefaults`、`ExpectedRootObject`、`Reason(HotReloadCDODiff/ScriptAlloc)`；上下文容器使用 `thread_local` stack。<br>2. 将 `PrepareSoftReload()` 改成在 `NewObject` 外层压入 `FScopedASConstructionContext{ bSkipDefaults = true }`，不再直接写 `GConstructASObjectWithoutDefaults`。<br>3. 把 `CurrentObjectInitializers` 迁移到 `thread_local` 或收口到 `FUObjectThreadContext` 旁路辅助结构；`AllocScriptObject()` / `FinishConstructObject()` 只读写当前线程自己的构造栈。<br>4. 在 `StaticActorConstructor` / `StaticComponentConstructor` / `StaticObjectConstructor` 增加防御性校验：如果 `bSkipDefaults` 上下文存在但当前对象不是预期 root object 或其默认 subobject，则记录结构化 warning。<br>5. 第一阶段保留旧全局变量作为兼容 shim，仅在没有 scoped context 时兜底读取；待测试稳定后再彻底删除该 shim。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptConstructionContext.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptConstructionContext.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把“目标对象 + 其默认 subobject”这种合法嵌套构造也误判成越界访问；因此 scoped context 需要允许显式声明可消费的 object graph，而不是只匹配单一对象。 |
| 兼容性 | 向后兼容。第一阶段只改变内部状态承载方式，不改变脚本语法与热重载对外接口；现有项目无需修改脚本。 |
| 验证方式 | 新增测试覆盖：1. `PrepareSoftReload()` 构造 `CDONoDefaults` 时仅目标对象及其合法 subobject 会跳过 defaults；2. 并行或伪并行情境下其他对象构造不会误读该上下文；3. `AllocScriptObject()` / `FinishConstructObject()` 在嵌套构造下栈平衡正确。 |

### Arch-HR-16：`ClassReloadHelper` 的 Editor 刷新面是 `full-reload-centric`，`SoftReload` 后的 cache invalidation 没有显式通道

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `SoftReload` 之后 editor cache 与 runtime class shell 的一致性 |
| 当前设计 | 当前 editor 侧刷新几乎全部绑定在 full reload 广播上：`FClassReloadHelper` 通过 `OnClassReload / OnStructReload / OnDelegateReload / OnFullReload` 积累 `ReloadState`，然后在 `PerformReinstance()` 或 `OnPostReload` 里刷新 Blueprint action、component registry、placement mode、property editor。问题是 `DoSoftReload()` 同样会改写类 flags、函数 no-op metadata、`ScriptTypePtr` 和 GC schema，但 soft path 只广播 `OnPostReload(false)`，不会给 helper 提供“哪些类被软变更了”的明确信息。结果是 runtime class shell 已经更新，editor 菜单/面板缓存却可能继续停留在旧状态。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2329-2373`：`OnClassReload` / `OnStructReload` / `OnFullReload` 都在 full reload 收尾阶段广播。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2467-2469`：soft path 只发 `OnPostReload(bIsDoingFullReload)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:52-139`：`ReloadState` 只从 `OnStructReload`、`OnClassReload`、`OnDelegateReload`、`OnLiteralAssetReload`、`OnEnum*` 与 `OnFullReload` 收集变更。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:139-175`：`OnPostReload` 只是根据已积累的 `ReloadState` 做 refresh，然后立即 reset。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:79-100`：`FBlueprintActionDatabase::RefreshClassActions()`、`FComponentTypeRegistry::InvalidateClass()`、volume 标记都在 `OnClassReload` lambda 里。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:333-338,380-383`：`PropertyEditorModule->NotifyCustomizationModuleChanged()` 与 placement mode 广播都只在 `PerformReinstance()` 结束后执行。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4208-4242,4253-4269,4286-4315`：`DoSoftReload()` 会更新 `CLASS_Abstract/Transient/EditInlineNew/DefaultToInstanced` 等 flags、修改 `FUNCMETA_ScriptNoOp`、并改写派生 `UBlueprintGeneratedClass` 的 `ScriptTypePtr` / reference token stream。 |
| 优点 | full reload 的 editor 修补链集中，避免每次 body-only soft reload 都触发大范围 UI / Blueprint cache 刷新。 |
| 不足 | 当前没有“soft reload 只做轻量 editor invalidation”的中间层。推断上，只要 soft path 接受了 editor-visible 改动，例如 component class 的 palette 可见性、`ScriptNoOp` 元数据、`CLASS_Abstract` / `CLASS_NotPlaceable` 变化，运行时与编辑器就可能短时间分叉：执行逻辑已经新，右键菜单、组件面板、细节面板却仍是旧缓存。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | editor watcher 在文件变化后直接调用 `UUnLuaFunctionLibrary::HotReload()`；运行时再由 `ULuaEnvLocator` 把同一条 reload 路径广播给 default env 与各 `GameInstance` env。没有再额外依赖“full reload 完成后另起一条 helper 流”去补提交 editor 侧状态。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-36,112-118`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:76-82` | editor 触发器和 runtime reload 执行器尽量走同一路径，缺的只是额外 refresh，而不是另一套 full-only 广播。 |
| puerts | `FSourceFileWatcher` 的文件变更回调直接读取源码并调用 `JsEnv->ReloadSource()`；也就是说 editor 保存事件和 runtime patch 是同一个即时管线，而不是“先 soft reload，之后等另一条 editor 修补链补状态”。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-146`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538` | 如果 editor cache 需要跟进，应把它建成 reload 同轮次里的显式 delta，而不是只在 full reinstance 时顺带处理。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 full reload helper 之外，增加一层 `SoftReloadEditorDelta`，专门承载“只需 invalidation、不需 reinstance”的 editor 刷新。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` 新增 `FSoftReloadEditorDelta` 与 `OnSoftReloadClassMutated` delegate，记录 `Class`、`ReasonFlags(ClassFlagsChanged/FunctionMetaChanged/ComponentPaletteChanged/VolumeFlagChanged)`。<br>2. 在 `DoSoftReload()` 中，当检测到 class flags 变化、`FUNCMETA_ScriptNoOp` 变化、组件相关 class 变化或派生 `UBlueprintGeneratedClass` schema 变化时，向本轮 delta list 追加记录。<br>3. 扩展 `FClassReloadHelper::FReloadState`，增加 `SoftMutatedClasses` 与 `SoftMutationReasons`；订阅 `OnSoftReloadClassMutated`，但不进入 `PerformReinstance()`。<br>4. 在 `OnPostReload(false)` 路径上，对这些 soft delta 仅做 refresh-only invalidation：调用 `FBlueprintActionDatabase::RefreshClassActions()`、`FComponentTypeRegistry::InvalidateClass()`、`PropertyEditorModule->NotifyCustomizationModuleChanged()`，当 `Placeable/Abstract/Volume` 相关 flag 变化时再补 placement/geometry 广播。<br>5. 首版严格白名单，只覆盖“不会改对象图”的 editor cache；任何需要 Blueprint recompilation 或 asset reinstance 的场景仍保持 current full reload 流程。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 风险主要在过度 invalidation：如果 soft delta 分类过宽，可能让每次 body-only reload 都刷新大量 editor cache，反而拖慢编辑器；因此首版必须基于明确 reason flag 白名单。 |
| 兼容性 | 向后兼容。该方案只新增 editor refresh 通道，不改变现有 soft/full reload 判定与脚本 API；最坏情况只是 editor 刷新更积极。 |
| 验证方式 | 新增测试覆盖：1. `UActorComponent` script 类 soft reload 后组件面板可见性正确更新；2. `ScriptNoOp` / `CLASS_Abstract` 变化后 Blueprint action 与细节面板刷新；3. 纯 body-only soft reload 不应触发无关 class 的 editor invalidation。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-16 | `SoftReload` 后 editor cache invalidation 缺口 | 轻量刷新通道新增 | 高 |
| P1 | Arch-HR-15 | 热重载构造上下文的全局可变状态 | 内部状态隔离重构 | 中高 |

---

## 架构分析 (2026-04-08 16:09)

### Arch-HR-17：`DynamicSubsystem` 的 full reload 只有 class 级 deactivate/activate，且激活晚于 `OnPostReload`，长生命周期单例没有迁移窗口

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | subsystem 类型在 full reload 下的状态续接与生命周期时序 |
| 当前设计 | Angelscript 对 subsystem 的 full reload 处理停留在 class 级别：创建新类时先 `DeactivateExternalSubsystem(ReplacedClass)`，收尾阶段再对 `NewClass` 调 `ActivateExternalSubsystem()`。而 `OnPostReload(true)` 在 subsystem 重新激活之前就已经广播。subsystem 基类自身只暴露 `BP_Initialize/BP_Deinitialize`，没有任何热重载快照或迁移协议。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2372-2395`：full reload 先 `OnFullReload.Broadcast()`，随后在清理旧 script type 之前就执行 `OnPostReload.Broadcast(bIsDoingFullReload)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2442-2463`：`OnPostReload` 之后才 `ForceGarbageCollection(true)`，并对 `ReinstancedSubsystems` 调 `FSubsystemCollectionBase::ActivateExternalSubsystem()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2642-2655`：只要 `CodeSuperClass` 是 `UDynamicSubsystem` 或 `UWorldSubsystem`，旧类就会被 `DeactivateExternalSubsystem(ReplacedClass)`，新类只被加入 `ReinstancedSubsystems` 等待后续激活。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h:29-45`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h:23-38`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h:53-67`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptLocalPlayerSubsystem.h:21-34`：各类 script subsystem 生命周期只有 `Initialize/Deinitialize -> BP_Initialize/BP_Deinitialize`，没有迁移旧实例状态的热重载钩子。 |
| 优点 | 借助 `FSubsystemCollectionBase`，当前实现能复用 UE 现有 subsystem 创建/销毁机制，不必为 engine/game instance/world/local player 各写一套重建流程。 |
| 不足 | 推断上，一旦 subsystem 走 full reload，缓存、注册表、运行时计数器、外部连接句柄等长生命周期状态会随着旧实例退场而丢失；更关键的是，由于 `OnPostReload` 早于新 subsystem 激活，外部即便想在 reload 钩子里做状态交接，也拿不到已创建的新实例。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载沿既有 env 做 in-place 更新：`FLuaEnv::HotReload()` 只执行 `UnLua.HotReload()`；`ULuaEnvLocator` / `ULuaEnvLocator_ByGameInstance` 只是把这条路径广播到现存 env。Lua 侧随后对已加载 module 做 `update_modules()`，并回写 running stack、`_G`、registry。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:28-33,76-82`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:381-477,480-549,604-624` | 宿主边界是稳定的 env，而不是 reload 后重新长出一个“同职责但新身份”的单例；状态迁移窗口天然存在。 |
| puerts | 源码热更新走同一个 `FJsEnvImpl::ReloadSource()`，直接在现存 env 上调用 JS 侧 `__reload`；native reload 时模块在 `ReloadCompleteDelegate/OnHotReload` 回调里立刻 `MakeSharedJsEnv()` 并 `RebindJs()`，env 重建边界显式且与 reload 事件同步。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542,1618-1646`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185-244,424-438` | 即便选择“重建宿主”而不是“保留宿主”，也把重建做成明确、同步的 reload 步骤，而不是在 `OnPostReload` 之后额外补一个 subsystem 激活尾巴。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 script subsystem 增加显式的热重载迁移协议，并把激活时序前移到可观测窗口内。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 新增 `FSubsystemReloadPair` 与 `OnPreSubsystemReinstance/OnPostSubsystemReinstance`，在 `CreateFullReloadClass()` 识别到 subsystem 替换时记录 `OldClass/NewClass/OuterKind`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h`、`ScriptGameInstanceSubsystem.h`、`ScriptWorldSubsystem.h`、`ScriptLocalPlayerSubsystem.h` 增加可选热重载接口，例如 `BP_CaptureHotReloadState()` / `BP_ApplyHotReloadState()`，首版允许默认 no-op。<br>3. 调整 full reload 收尾顺序：先对旧 subsystem 广播 `OnPreSubsystemReinstance` 并抓取快照，再激活 `NewClass` 对应 subsystem，随后执行 `OnPostSubsystemReinstance`，最后再发通用 `OnPostReload(true)`；这样外部观察者在通用 post hook 里拿到的是“已完成替换”的稳定状态。<br>4. 第一阶段只覆盖 `UDynamicSubsystem`/`UWorldSubsystem`，并把状态载体限制为显式 opt-in 的 `UObject`/`UStruct` 快照；未实现钩子的项目保持当前“重新初始化”的行为。<br>5. 补充 `AngelscriptTest`：为 engine/game instance/world/local player 四种 subsystem 各加一个热重载用例，验证 full reload 后计数器、注册的 gameplay 状态或持有的 asset 引用能按预期保留或显式清空。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptLocalPlayerSubsystem.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptSubsystemReloadState.h`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptSubsystemHotReloadTests.cpp` |
| 预估工作量 | L |
| 架构风险 | subsystem 的 owner 维度不同，engine/game instance/world/local player 不能共用一张简单 map；若只按 class 做迁移，容易把多个 world 或 local player 的状态串错。 |
| 兼容性 | 向后兼容。首版把状态迁移做成 opt-in；未实现新钩子的脚本 subsystem 保持当前“deinit + init”语义，只额外获得更清晰的时序。 |
| 验证方式 | 1. 新增 full reload 用例，验证旧 subsystem 的计数器/缓存能迁移到新实例。<br>2. 验证 `OnPostReload(true)` 触发时，新 subsystem 已可通过 `GetEngineSubsystem/GetGameInstanceSubsystem/GetWorldSubsystem/GetLocalPlayerSubsystem` 取得。<br>3. 对未实现迁移钩子的 subsystem，确认行为仍是重新初始化，且日志会提示“未提供 hot reload state bridge”。 |

### Arch-HR-18：默认 legacy reinstance 不发布 UE 标准 `ReloadComplete/OnObjectsReplaced` 信号，外部工具只能依赖 Angelscript 专用刷新链

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | Angelscript 自定义 full reload 与 UE 原生 reload 事件体系的互操作 |
| 当前设计 | 默认 `GAngelscriptUseUnrealReload == 0` 时，Angelscript 走自己的 legacy reinstance：手工分析 Blueprint 依赖、替换 pin/type、`ReparentHierarchies()`、刷新 PropertyEditor/PlacementMode、并通过 `UAngelscriptReferenceReplacementHelper` 修 open asset 引用。只有打开 `angelscript.UseUnrealReload` 时才创建 `FReload(EActiveReloadType::Reinstancing)`。这意味着默认路径不会自然触发 UE 原生 reload 所依赖的 `ReloadCompleteDelegate`、`ReloadReinstancingCompleteDelegate` 和 `OnObjectsReplaced` 语义。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:40-181,179-299`：legacy 分支手工完成 Blueprint 依赖分析、pin 替换、`FBlueprintCompilationManager::ReparentHierarchies()` 与 Blueprint recompile/reinstance。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:333-385`：legacy 分支收尾只做 `PropertyEditorModule->NotifyCustomizationModuleChanged()`、volume factory 刷新和若干 asset action 更新。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:409-439`：`UAngelscriptReferenceReplacementHelper` 只针对 `UAssetEditorSubsystem` 里的 open assets 做定制修补。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:319-329`：只有 `UseUnrealReload` 分支才构造 `FReload(EActiveReloadType::Reinstancing)` 并调用 `NotifyChange()/Reinstance()/Finalize()`。<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h:3300-3302,3362-3368`：UE 把 `OnObjectsReplaced`、`ReloadReinstancingCompleteDelegate`、`ReloadCompleteDelegate` 定义为全局标准 reload 信号。<br>`J:/UnrealEngine/UERelease/Engine/Source/Editor/UnrealEd/Private/Kismet2/ReloadUtilities.cpp:851-854,1161-1163`：`FReload` 会广播 `ReloadCompleteDelegate` 与 `ReloadReinstancingCompleteDelegate`。<br>`J:/UnrealEngine/UERelease/Engine/Source/Editor/UnrealEd/Private/EditorEngine.cpp:4956-4960`：`NotifyToolsOfObjectReplacement()` 最终广播 `OnObjectsReplaced`。<br>`J:/UnrealEngine/UERelease/Engine/Source/Editor/UnrealEd/Private/ComponentTypeRegistry.cpp:784-792` 与 `J:/UnrealEngine/UERelease/Engine/Source/Editor/BlueprintGraph/Private/BlueprintActionDatabase.cpp:1185-1217`：引擎现有 editor 系统本来就订阅 `ReloadCompleteDelegate`。 |
| 优点 | 当前做法能精确覆盖 Angelscript 关心的补丁面，避免把插件成败完全押在 UE 通用 `FReload` 上。 |
| 不足 | 当前 default path 的刷新面是“白名单式”的：BlueprintActionDatabase、ComponentTypeRegistry、PropertyEditor、open assets 被显式照顾，但任何依赖标准 reload delegate 的引擎模块、第三方编辑器工具或项目内插件，都只能在 `UseUnrealReload=1` 时才收到原生信号。结果是 Angelscript full reload 和 UE 生态的 reload contract 之间存在一条隐形断层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 直接订阅 UE5 `ReloadCompleteDelegate` 或 UE4 `OnHotReload()`，回调里执行同一条 `MakeSharedJsEnv()` 路径；源码热更新则走 `ReloadSource()`，由 JS 侧 `__reload` 和 `Debugger.setScriptSource` 完成 patch。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:424-438`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185-244`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:81-90,98` | 既然要和 UE 原生 reload 共存，就把宿主重建挂在标准 delegate 上，避免旁路出一条只有插件自己知道的 reload 时序。 |
| UnLua | 不去创建新的 `UClass`/`UObject` 身份，而是在现有 `LuaEnv` 内广播 `HotReload()` 并更新 module、global、running stack。也就是说，它要么走“稳定宿主 identity”，要么走“标准引擎回调”，不会处在两者之间。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:28-33,76-82`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549,604-624` | 如果插件自己承担 reload 执行器，就尽量保证 UE 对象身份稳定；一旦会替换 `UClass/UObject`，就应补回原生 reload 可观察性。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 legacy reinstance 之上补一层 `UE reload interop facade`，把 Angelscript 已有 replacement 信息显式转成 UE 标准事件。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h/.cpp` 新增 `FAngelscriptReloadInteropBatch`，收集本轮 `ReloadClasses`、`ReloadAssets`、可能的 struct/enum 变化以及“是否发生了真正 reinstance”。<br>2. 在 legacy `PerformReinstance()` 完成 `ReparentHierarchies()`、Blueprint recompile 和 open asset 修补后，若存在真实 replacement map，则通过 `GEditor->NotifyToolsOfObjectReplacement()` 统一对外广播；第一阶段只发送 class/asset replacement，避免伪造不存在的 instance map。<br>3. 紧接着补发 `FCoreUObjectDelegates::ReloadReinstancingCompleteDelegate` 与 `ReloadCompleteDelegate`；首版放在新的 `angelscript.EmitUEReloadDelegates` 开关后面，默认关闭，以便先在项目里观测是否会触发双刷新。<br>4. 当 `angelscript.UseUnrealReload=1` 时禁用这层手工广播，避免和 `FReload` 自带的 delegate 双发；两条 backend 共用同一个 batch id，用于检测重复 reload。<br>5. 增加互操作测试：在 `AngelscriptTest` 或 editor automation 中注册一个外部 observer，分别在 `UseUnrealReload=0/1` 下断言 `ReloadCompleteDelegate`、`ReloadReinstancingCompleteDelegate` 和对象替换通知各触发一次。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadInteropTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是重复广播导致部分 editor 模块被刷新两次，尤其在 `UseUnrealReload=1` 或未来接入 Live Coding 时更明显；因此 batch id 和 backend 判重必须先落地。 |
| 兼容性 | 向后兼容。首版用 opt-in 开关发布 UE 标准信号，不改变当前默认 reload 行为；等项目验证没有重复副作用后再考虑默认打开。 |
| 验证方式 | 1. 注册测试 observer，验证 legacy backend 也能收到 `ReloadCompleteDelegate/ReloadReinstancingCompleteDelegate`。<br>2. 对 open asset、component palette、Blueprint action 三类 editor 对象做回归，确认补发标准信号后不会出现双刷新或 stale pointer。<br>3. 在 `UseUnrealReload=0/1` 两种模式下跑同一组 full reload 场景，断言标准 delegate 的触发次数一致且没有重复广播。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-17 | subsystem full reload 的状态续接与事件时序 | 生命周期协议补齐 + 迁移窗口前移 | 高 |
| P1 | Arch-HR-18 | legacy reload 与 UE 标准 reload 事件体系的断层 | interop facade + 标准 delegate 补发 | 中高 |

---

## 架构分析 (2026-04-08 16:24)

### Arch-HR-19：`SoftReload` 对 instanced/persistent object graph 的预回填语义过粗，容器与 struct 内部状态存在丢失窗口

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `SoftReload` 的状态保持是否覆盖 nested instanced reference / persistent instance |
| 当前设计 | 当前 `SoftReload` 知道 full property 系统里存在 `CPF_InstancedReference`、`CPF_ContainsInstancedReference`、`CPF_PersistentInstance` 和 `STRUCT_HasInstancedReference`，但真正决定“constructor 之前必须先回填”的只有 `Copy.bIsInstanced = PropertyType.IsObjectPointer()` 这一位。结果是 top-level `UObject*` 会走预回填，`TArray`/`TMap`/`TSet`/`UStruct` 里承载的 instanced object graph 则不会。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2868-2904,3041-3099`：类生成阶段会为 container / struct 精确设置 `CPF_ContainsInstancedReference`、`CPF_InstancedReference`、`CPF_PersistentInstance`，说明架构层面已经承认“嵌套实例引用”是一等语义。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4379-4401`：`FLocalPropertyContext` 只会展开非 `STRUCT_Atomic` 的 struct；container 自身仍作为单个 property 参与迁移。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4481-4488`：`FPropertyCopy` 只用 `PropertyType.IsObjectPointer()` 标记 `bIsInstanced`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4607-4620,4712-4752`：只有 `bIsInstanced` 的条目会在 `ReinitializeScriptObject()` 前被先拷回；注释也明确写着“Instanced properties should always be copied over first”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:187-188,537-538`：`IsObjectPointer()` 的定义就是“trivial UObject pointer”，并不是“contains instanced references”。 |
| 优点 | 实现简单，纯 `UObject*` 属性在 soft path 下能较早恢复，避免 constructor 重放时把直接子对象全部覆盖掉。 |
| 不足 | 推断上，一旦脚本类把子对象状态放进 `TArray<UObject*>`、`TMap`/`TSet`、或 `UStruct` 中的 persistent instance 字段，当前 soft path 会先析构/重构对象，再在 constructor 之后整体回填这些容器或 struct。这样既可能让 constructor 期间额外创建一批临时 subobject，也可能把旧 graph 的 runtime wiring 覆写回去，形成重复子对象、悬空引用或静默丢状态。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载不是重建宿主对象内存，而是先把 old/new module match 出 value map，再递归更新 `_G`、registry、userdata uservalue、running stack 和 function upvalue。nested 状态通过 graph walk 原位修补，而不是依赖“constructor 前后两次 memcpy”去恢复。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:415-477`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-547` | 对 Angelscript 来说，可借鉴的是“嵌套状态需要显式 graph-aware 策略”，不能把所有对象图都近似成顶层 `UObject*`。 |
| puerts | JS 热更新直接把同一个 `scriptId` 的 source 送进 `Debugger.setScriptSource`，并围绕现有 module 做 `HMR.prepare/HMR.finish`；`FJsEnvImpl::ReloadSource()` 只是把 path/source 交给这条原位 patch 管线。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538` | 参考点不是照搬 JS 方案，而是把“尽量保持 host-side identity 稳定”当成默认值；一旦必须重建，就要用比 `IsObjectPointer()` 更准确的语义分类。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 用真实 `FProperty`/`UStruct` instancing 语义替代当前的 `IsObjectPointer()` 近似；不确定场景先保守回退 full reload。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 的 `FPropertyCopy` 中把 `bIsInstanced` 升级成 `EPreconstructRestoreKind`，至少区分 `None`、`DirectObjectRef`、`ContainsInstancedReference`、`PersistentInstanceContainer`。<br>2. 构建 `PropertiesToCopy` 时，不再只看 `PropertyType.IsObjectPointer()`，而是直接查询 relink 后 `FProperty` 的 `CPF_InstancedReference` / `CPF_ContainsInstancedReference`，以及 `FStructProperty->Struct->StructFlags & STRUCT_HasInstancedReference`。<br>3. 对 `ContainsInstancedReference` 和 `PersistentInstanceContainer` 条目，新增 `PreconstructRestoreValue()`，保证 container / struct 整体在 `ReinitializeScriptObject()` 前就先恢复到底层内存，而不是等 constructor 跑完后再整体覆盖。<br>4. 首版对无法准确判定的 property 直接把 `ReloadReq` 提升到 `FullReloadSuggested` 或 `FullReloadRequired`，不要继续走当前近似软迁移。<br>5. 增加 hot reload 测试，至少覆盖 `TArray<UObject*>`、`TMap<FName, UObject*>`、`TSet<UObject*>`、以及包含 persistent instance 子字段的 `UStruct`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadInstancedReferenceTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是误判 preconstruct 语义导致对象图比现在更早被覆盖；因此首版宁可把更多场景抬成 full reload，也不要继续用错误的 soft path 冒险。 |
| 兼容性 | 向后兼容。第一阶段只会让部分原本“勉强 soft reload”的场景更保守地回退 full reload，不改变脚本 API。 |
| 验证方式 | 1. 增加包含 nested instanced reference 的 soft reload 用例，确认 reload 后不会生成重复 subobject。<br>2. 对 `PersistentInstance` 容器做回归，确认 constructor 重放前后对象 identity 与引用关系一致。<br>3. 对无法分类的场景验证会明确升级成 full reload，并输出结构化原因。 |

### Arch-HR-20：`ReloadAssets` 只收集不落地，literal asset replacement 在 legacy backend 里基本是死信号

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | literal asset 热重载时的对象 identity replacement 是否真正传播到 editor/runtime 消费方 |
| 当前设计 | 当 literal asset 命中旧类版本链时，运行时会把旧 asset rename 到 transient package、创建同名新 asset，并通过 `OnLiteralAssetReload` 广播 old/new pair。Editor 侧也确实把这对对象收进了 `ReloadState().ReloadAssets`。但 legacy reinstance backend 没有把 `ReloadAssets` 接进任何通用 replacement pass；现存补丁只覆盖 open asset editors。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:617-627,640-671`：旧 literal asset 一旦命中 `CLASS_NewerVersionExists` 就会被 rename，随后创建新对象并广播 `OnLiteralAssetReload(ReloadedObject, ExistingObject)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:32-39,111-115`：`FReloadState` 明确保存 `ReloadAssets`，`Init()` 会从 `OnLiteralAssetReload` 收集 old/new asset pair。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:49-53,170-181`：legacy `PerformReinstance()` 实际只构造 class/struct replace list；`ReloadAssets` 对应的 `GAngelscriptAdditionalReplacementObjects` 接线被整段注释掉，没有真正进入 `ReparentHierarchies()` 前的 replacement map。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:319-329`：`UseUnrealReload` 分支也只把 struct/class/enum 送给 `FReload->NotifyChange()`，同样没有 asset replacement。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:409-439`：`UAngelscriptReferenceReplacementHelper` 只遍历 `UAssetEditorSubsystem::GetAllEditedAssets()`，说明当前 literal asset 替换只覆盖 open editors。 |
| 优点 | 对正在编辑的 asset 窗口，当前 workaround 至少能把 stale pointer 修到新的 asset identity，避免 editor instance 直接悬挂。 |
| 不足 | 推断上，任何不在 `GetAllEditedAssets()` 里的引用面都可能继续持有旧 literal asset，包括 runtime cache、蓝图默认值、未打开但已加载的 editor object，以及项目内自己维护的 object registry。`ReloadAssets` 现在更像“记账”，不是会被统一消费的 replacement contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | module hot reload 先命中 `loaded_modules`/`package.loaded`，随后 `update_modules()` 直接把新函数和新 value merge 回旧 module table，并更新全局图；它不通过“创建新 UE asset / module object 再四处替换引用”来完成 reload。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:151-170`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-547` | 能原位更新 identity 的场景，优先原位更新；只有必须换壳时，才需要 replacement map。 |
| puerts | `hot_reload.js` 先通过 `puerts.getModuleByUrl(url)` 取现有 module，再用 `Debugger.setScriptSource` 更新同一个 `scriptId`，并在前后发 `HMR.prepare/HMR.finish`。模块 identity 保持稳定，因此没有额外的 asset/object replacement backend。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538` | 对 Angelscript 来说，可借鉴的不是 JS 细节，而是“能不换 UObject identity 就别换；如果换了，就必须有完整 replacement backend”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `ReloadAssets` 从记录字段升级成真正的 replacement backend 输入；literal asset 若发生换壳，必须与 class/struct 一起进入统一替换流程。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` 恢复并正式化 replacement map 组装逻辑，不再只处理 class/struct，统一把 `ReloadAssets`、`ReloadDelegates` 也并入同一批次。<br>2. legacy backend 在 `ReparentHierarchies()` 之后、UI refresh 之前，显式对 `ReloadAssets` 调用 `GEditor->NotifyToolsOfObjectReplacement()` 或等价的 archive replacement pass，而不是只依赖 open-editor serialize workaround。<br>3. 对 `angelscript.UseUnrealReload=1` 分支补 asset replacement facade；若 `FReload` 无法直接承载 literal asset，则在 `Finalize()` 后追加一轮 asset replacement sweep，保持两条 backend 行为一致。<br>4. 对于“类未变、只是内容重建”的 literal asset，继续沿用当前 in-place reset 分支；只有真正换类/换壳时才创建新对象并走 replacement map，尽量缩小 replacement 面。<br>5. 新增诊断与测试：输出本轮替换了多少 literal asset、哪些引用面仍未被覆盖；增加 editor automation 验证 open asset、loaded-but-closed asset、Blueprint default object 与 runtime cache 四类引用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptLiteralAssetHotReloadTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 若 replacement map 与 `UseUnrealReload` / legacy backend 同时各发一遍，最容易出现双替换或 editor observer 重复刷新；需要 batch id 和 backend 判重。 |
| 兼容性 | 向后兼容。首版可以先做 `warn-only + editor replacement`，不改变 literal asset 的创建 API；后续再逐步扩大到 runtime cache replacement。 |
| 验证方式 | 1. 构造 literal asset class full reload 场景，验证 open asset editor、Blueprint 默认值和运行时缓存都拿到新对象。<br>2. 在 `UseUnrealReload=0/1` 两种 backend 下跑同一组用例，确认 replacement 次数一致且没有双发。<br>3. 对“类未变仅重置属性”的路径做回归，确认仍保持 in-place identity。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-19 | soft reload 对 nested instanced/persistent object graph 的状态保持 | 迁移语义细化 + 保守回退 | 高 |
| P1 | Arch-HR-20 | literal asset replacement 的传播闭环 | replacement backend 补齐 | 中高 |

---

## 架构分析 (2026-04-08 16:39)

### Arch-HR-21：热重载没有把 in-flight / suspended `asCContext` 纳入迁移契约，运行中帧与新帧会隐式跨 epoch 共存

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 运行中调用栈、挂起上下文与旧模块退役的关系 |
| 当前设计 | 当前热重载会更新模块引用、替换 `UASFunction::ScriptFunction`、并在 swap-in 后丢弃旧模块；但上下文管理只显式回收空闲 context pool。`ReleaseContextsForScriptEngine()` 直接 `check` 掉 `asEXECUTION_ACTIVE/asEXECUTION_SUSPENDED`，说明运行中或挂起的 `asCContext` 不在 reload 迁移面里。与此同时，`FAngelscriptPooledContextBase::Init()` 允许复用当前线程的 `activeContext` 并 `PushState()` 继续嵌套执行，`DoSoftReload()` 则只把旧 `UFunction` 壳重绑到新 `ScriptFunction`。这意味着架构默认接受“旧 stack frame 继续跑旧 epoch，后续新调用进入新 epoch”的混合态，但没有把这件事做成显式协议。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:195-215`：`ReleaseContextsForScriptEngine()` 只释放 pool 中非 `ACTIVE/SUSPENDED` context。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1795-1806`：若当前线程已经有 `activeContext`，新执行会 `PushState()` 复用它。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4244-4260`：soft path 只把旧 `UASFunction` 指向新的 `ScriptFunction`，没有遍历 active/suspended stack frame。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4010-4025`：reload 成功后会立刻 `DiscardModule()` 旧模块。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3878-3884,3972-4003`：当前失败策略是“保留旧代码”或“swap-in 新代码”，但没有第三种“等待旧 frame drain 完成”的执行期策略。 |
| 优点 | 不需要在 VM 内部做 mid-frame stack surgery，reload 路径更直，单次热重载延迟较低。 |
| 不足 | 推断上，timer 回调、latent 恢复、debugger 挂起点、以及任何跨帧保留 `asCContext`/stack 的机制，都会落入“旧 frame 继续执行旧字节码、同对象后续调用却进入新字节码”的混合态。当前没有 epoch 诊断、没有 resume policy、也没有 old-module drain 完成前的显式 pin/unpin 协议，因此该语义对业务和工具链都是隐式的。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 明确把 running stack 和运行时对象图纳入热重载：`update_running_stack()` 会递归改写 `debug.getlocal()/setlocal()`，随后 `update_table()` / `update_global()` 继续修补 `_G`、registry、userdata 和 function upvalue。也就是说，运行中的执行态是 reload contract 的一部分，而不是旁路状态。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:367-477`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549` | 即使不做完整 stack patch，也应把“哪些执行态被 reload 影响”做成显式协议和诊断，而不是默认静默混跑。 |
| puerts | source hot reload 在同一个 `scriptId` 上做 `Debugger.setScriptSource`，并显式发 `HMR.prepare/HMR.finish`；native reload 则在 `ReloadCompleteDelegate/OnHotReload()` 上重建 `JsEnv`。它至少把“旧执行态如何过渡到新代码”包进可观察的生命周期事件里。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:424-438` | Angelscript 不一定能原位 patch stack，但可以先补“prepare/finish + resume policy + old epoch drain”这层执行期契约。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把执行期 reload 语义显式化，再逐步引入 old-epoch drain 和 resume policy；首版保持默认行为兼容。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 与 `FAngelscriptPooledContextBase` 中新增 `ReloadEpoch` / `ContextEpoch` / `SuspendEpoch` 记录；每次 `SwapInModules()` 成功后递增 epoch，并把受影响模块列表挂到本轮 batch。<br>2. 将当前“立刻 `DiscardModule()` 旧模块”的行为拆成 `PendingRetireModules` 队列：只有当该 epoch 下不再存在 `ACTIVE/SUSPENDED` context 时才真正退役旧模块；首版只延迟退役，不改执行语义。<br>3. 在 context resume 入口增加 `EAngelscriptResumeAfterReloadPolicy`，默认 `ContinueOldFrame` 以保持向后兼容，同时支持 `WarnOnly` 和 `AbortStaleSuspendedContext` 两个更保守选项，便于项目逐步收紧。<br>4. 新增 runtime 级 `OnScriptReloadPrepare/OnScriptReloadFinish` delegate，给 timer/latent/debugger/测试框架一个统一窗口去 snapshot 或丢弃旧 handle；不要继续让各系统猜测 reload 是否已经跨 epoch。<br>5. 增加 hot reload 集成测试，至少覆盖“函数执行到一半发生 soft reload”“挂起/恢复的 latent context 跨 reload”“debugger attach 下的 suspended context”三类场景，并把 active/suspended context 数量写入 state dump/diagnostic。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadExecutionEpochTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 延迟退役旧模块会暂时拉长旧代码驻留时间，并增加多 epoch 并存窗口；因此需要 batch 上限和诊断，防止项目长期积压 stale context。 |
| 兼容性 | 向后兼容。首版默认策略仍可继续让旧 frame 跑完，只新增 epoch 记录、延迟退役与诊断；项目可按需开启更严格的 resume policy。 |
| 验证方式 | 1. 增加跨帧/挂起脚本的 hot reload 用例，确认 `ContextEpoch` 与 `ReloadEpoch` 统计正确。<br>2. 验证默认策略下旧 frame 能继续完成，而新调用进入新 epoch；开启严格策略后应得到明确 warning 或中止。<br>3. 对 `PendingRetireModules` 做回归，确认所有旧 context drain 完成后模块才真正被 `DiscardModule()`。 |

### Arch-HR-22：full reload 的修补面仍停留在“内存里已加载的资产”，仓库里现成的 `BlueprintImpact` 离线扫描没有接入主 reload 闭环

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | full reload 后对未加载 Blueprint/DataTable 等资产的影响闭环 |
| 当前设计 | `FClassReloadHelper::PerformReinstance()` 当前只遍历内存里的 `UBlueprint`、`UDataTable` 和 open editors：`TObjectIterator<UBlueprint>` 只分析 loaded Blueprint，`GetTablesDependentOnStruct()` 也是 `GetObjectsOfClass(UDataTable::StaticClass())` 的 loaded-object 枚举，`UAngelscriptReferenceReplacementHelper` 只修 `UAssetEditorSubsystem::GetAllEditedAssets()`。仓库同时已经提供 `BlueprintImpact::ScanBlueprintAssets()` 与 `UAngelscriptBlueprintImpactScanCommandlet`，可以走 `AssetRegistry` 扫 on-disk Blueprint 并按变更脚本构造 impact 集；但这条能力目前仍是独立工具，没有挂回 hot reload 主链。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:42-163`：full reload 只在 loaded Blueprint 上做 `AnalyzeLoadedBlueprint()`、pin/type 修补和 `UDataTable::RowStruct` 替换。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:178-194`：`GetTablesDependentOnStruct()` 通过 `GetObjectsOfClass(UDataTable::StaticClass())` 枚举已加载 DataTable。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:387-439`：reference replacement helper 只处理 `GetAllEditedAssets()` 返回的 open assets。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:278-309`：扫描器已经能从 `AssetRegistry` 找 candidate assets，并加载 Blueprint 做 impact 分析。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55-85`：commandlet 已能在独立流程中调用这条扫描路径。 |
| 优点 | 交互式热重载不会为了修补所有资产而立即拉起大规模 package load，编辑器前台延迟可控。 |
| 不足 | 当前闭环被拆成“两条线”：交互式 hot reload 只保证 loaded/open 资产被修，未加载 Blueprint/DataTable 要么等用户下次打开时才暴露问题，要么依赖人工单独跑 commandlet。仓库明明已经有 impact 扫描器，却没有变成主链里的 deferred repair 能力。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv::HotReload()` 只把控制权交给 `UnLua.HotReload()`；Lua 侧在已有 module table、running stack、`_G` 和 registry 上原位合并，不涉及 UAsset replacement / Blueprint reparent，因此没有“未加载资产还要补修”的第二条链。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549,604-624` | 稳定宿主 identity 的收益之一，就是 reload 闭环不必再额外维护一套离线资产修补流程。 |
| puerts | watcher 只跟踪已加载 source，变化后直接 `ReloadSource()`；JS 侧在同一个 module/script identity 上做 `Debugger.setScriptSource`，也不需要在 reload 当下扫描 AssetRegistry 修补 Blueprint/DataTable。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | Angelscript 既然选择了 `UClass`/asset 级换壳，就必须把“交互式 loaded repair + deferred on-disk repair”两段都产品化。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留当前 loaded-only 快路径，同时把现有 `BlueprintImpact` 扫描器接成 post-reload 的 deferred asset repair 阶段；首版先做只读诊断。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.*` 新增 `FAngelscriptDeferredAssetRepairBatch`，记录本轮 changed scripts、`ReloadClasses/Structs/Enums/Delegates` 和 session id。<br>2. full reload 完成后继续沿用当前 loaded/open 资产修补，但在 `OnPostReload(true)` 后把 batch 交给一个 editor idle/background 任务，复用 `AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets()` 扫描 on-disk Blueprint；第一阶段只产出 impacted asset 列表和数量诊断，不自动加载/重编译。<br>3. 第二阶段把扫描器扩成通用 asset impact 服务：在现有 Blueprint 扫描之外，为 `UDataTable` 和其他 struct-backed 资产补 `AssetRegistry` 查询与按需加载，避免 hot reload 只修内存中的表。<br>4. 为 UI 与 CI 都暴露同一份结果：editor 中给出 notification/可展开列表，命令行继续复用现有 commandlet 输出 JSON；不要再维护两套格式。<br>5. 增加用户可选的“后台批量修复”动作：仅在用户确认或项目设置开启时，按批次加载 impacted assets、`QueueForCompilation()`、标记 package dirty 并保存；默认仍是诊断优先，保证向后兼容。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptAssetImpactScanner.cpp` |
| 预估工作量 | M |
| 架构风险 | 后台扫描/批量加载若直接跟交互式 reload 串在同一帧，会把 editor 卡顿问题重新引回来；因此 deferred 阶段必须异步、分批并可取消。 |
| 兼容性 | 向后兼容。第一阶段只新增诊断与列表输出，不改变当前 hot reload 行为；自动修复能力可以作为 opt-in。 |
| 验证方式 | 1. 构造“未加载 Blueprint 引用被重载脚本类/struct”的用例，确认 full reload 后 batch 能在后台扫描命中该资产。<br>2. 增加 on-disk DataTable 场景，验证扫描结果能识别受影响资产而不是只覆盖 loaded tables。<br>3. 对 editor 前台帧时间做回归，确认开启 deferred scan 后，热重载主路径延迟不显著上升。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-21 | in-flight / suspended context 的跨 epoch 执行语义 | 执行期契约显式化 + 延迟退役 | 高 |
| P2 | Arch-HR-22 | on-disk 未加载资产的 post-reload 修补闭环 | deferred asset repair + 扫描器接主链 | 中高 |

---

## 架构分析 (2026-04-08 23:34)

### Arch-HR-23：热重载入口仍以“全部 script roots + reverse import 闭包”为观察面，缺少 `loaded-source` 索引层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 变更检测边界与编译候选集的放大时机 |
| 当前设计 | 当前热重载在很早阶段就把观察面绑定到全部脚本根。Editor 启动时直接对 `MakeAllScriptRoots()` 返回的每个 root 注册 `DirectoryWatcher`；回调对 root 下任意 `.as` 都直接入队，目录新增还会枚举该目录内所有 `.as`。如果启用了 checker thread，运行时又会周期性扫描 `AllRootPaths` 下全部脚本文件。随后 `PerformHotReload()` 会把 changed file 先映射到 active module；找不到 active module 的文件也会被直接加入 `FilesToHotReload` 并构造成新的 `FAngelscriptModuleDesc`，找到了则继续沿 reverse import / `moduleDependencies` 把整条依赖闭包的 code sections 拉进本轮。也就是说，当前没有一个等价于 `OnSourceLoaded` / `loaded_module_times` 的“已激活脚本索引”层。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:366-380`：Editor 直接对 `MakeAllScriptRoots()` 返回的全部 roots 注册 `DirectoryWatcher`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`：任意 root 下 `.as` 文件变化直接入队；目录新增会 `FindScriptFiles(..., "*.as")` 枚举所有脚本。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1668-1671,1999-2012,2870-2895`：checker thread 先预填时间戳，后续 `FindAllScriptFilenames()` 按 `AllRootPaths` 全量扫描并按 timestamp 产生变化队列。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2282-2443`：`PerformHotReload()` 先把 changed files 归并为 `FilesToHotReload`；找不到 active module 的文件会直接落入 `FilesToHotReload`，找到了则继续扩张到 reverse dependency / `moduleDependencies` 闭包。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:75-83,91-103`：`AddFile()` 会为每个文件创建 `FAngelscriptModuleDesc`，`GetModulesToCompile()` 再按 file-owned module 去重返回编译候选。 |
| 优点 | 新增脚本、插件脚本和 root 下尚未激活的脚本都能被自动发现；对 import 依赖的保守扩张也降低了漏编译概率。 |
| 不足 | 变更源被硬绑定成“本地文件系统下的所有 script roots”，而不是“当前活跃脚本图”。推断上，未激活脚本、批量同步生成的 `.as`、甚至未来想接入的虚拟脚本源/远端脚本源，都会被迫走同一条 root watcher + batch compile 通道。增量优化只能在后面补救，很难在入口就把 blast radius 收窄。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 以 `loaded_module_times` 和自定义 `require()` 为中心维护“已经加载过的 Lua modules”。`M.reload()` 不带参数时，只遍历 `loaded_module_times` 里的 module，对比时间戳后构造 `modified_modules`；未进入 `loaded_modules` 的脚本默认不参与 reload 集合。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:114-170`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-623`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450` | 先建立“已加载模块索引”，再决定哪些模块需要检查变化。这样新脚本发现和活跃模块热重载可以拆成两条不同节奏的流程。 |
| puerts | `FSourceFileWatcher` 不在启动时盲扫整个 JS root，而是在 `OnSourceLoaded()` 时按已加载 source 动态注册 watched dir/file，并保存每个 file 的 `FMD5Hash`。目录变更时只有命中 `WatchedFiles` 且 hash 真变了的文件才会回调 `ReloadSource()`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-147`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538` | 把“source 已被加载”做成 watcher 注册条件，能明显缩小监听与 reload 候选面，也为替换文件来源提供了更清晰的抽象边界。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在当前 root-based watcher 之前增加一层 `loaded-source` 索引与 change-source 抽象，把“活跃脚本热重载”和“新脚本发现”拆成两级流程。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptHotReloadSourceIndex`，记录 `RelativePath -> {SourceState, OwningModule, LastKnownHash/Time}`；`SwapInModules()`、模块丢弃和初始编译后统一回填 active sources。<br>2. 把 `AngelscriptEditorModule.cpp` 里直接对 `MakeAllScriptRoots()` 注册 watcher 的逻辑抽到 `IScriptChangeSource` 接口；首个实现仍基于 `IDirectoryWatcher`，但回调先查询 `FAngelscriptHotReloadSourceIndex`，对 `Loaded/Active` 源走即时 reload，对 `Unknown` 新文件只打 `DiscoveryOnly` 标记。<br>3. 把 checker thread 的 `FindAllScriptFilenames()` 全量扫描拆成两段：高频段只扫描 `Loaded/Active` 索引，低频段或显式命令再做 root-level 新文件发现，避免每轮都把 inactive scripts 当作热重载候选。<br>4. 在 `PerformHotReload()` 前增加 `SourceScope` 分类：`ActiveEdit` 才允许继续做 reverse import / dependency closure；`DiscoveryOnly` 先做轻量 preprocess 与 module manifest 更新，不立刻把整条依赖链拉入热重载。<br>5. 首版用 `angelscript.HotReloadUseSourceIndex` 之类的 `CVar` 保护；默认仍保留现有 root-wide 行为，等 trace 证明 blast radius 收窄且没有漏发现脚本后再逐步切换默认值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadSourceIndex.*` |
| 预估工作量 | M |
| 架构风险 | 如果 `SourceIndex` 与实际 `ActiveModules` 不同步，最坏情况会漏掉应该热重载的文件；因此首版必须保守，允许 `Unknown` 或索引不确定时回退到当前 root-wide 路径。 |
| 兼容性 | 向后兼容。第一阶段只是增加索引和双通道诊断，不改变默认 watcher 行为；项目脚本布局与导入方式无需修改。 |
| 验证方式 | 1. 增加 automated test，覆盖“修改 active script”“新增未导入 script”“插件脚本 root 下批量生成文件”三类输入，确认 `SourceScope` 分类符合预期。<br>2. 增加 trace/统计，比较启用索引前后每轮 `FilesToHotReload` 与 `ModulesToCompile` 的数量变化。<br>3. 对 import 依赖链回归，确认 active script 改动仍能正确拉起所需的 dependent modules。 |

### Arch-HR-24：失败记忆是全局 `PreviouslyFailedReloadFiles`，一个坏脚本会持续污染后续无关热重载批次

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载失败隔离粒度与重试策略 |
| 当前设计 | 当前失败记忆不是按 file/module scoped 的 quarantine，而是一个全局 `PreviouslyFailedReloadFiles`。每次 `PerformHotReload()` 开始，都会把上次失败文件无条件并回 `FileList`；预处理失败会把整批 `FileList` 重新写回 failed set，compile error / full reload required 则把 `AllCompiledFiles` 全量写回 failed set，module swap-in 的 synthetic error 也会把该 module 的全部 code sections 写回 failed set。结果是：一个脚本语法错误、一次 `bModuleSwapInError`，或者一次必须升级 `FullReload` 的结构变化，都会在后续任何无关文件变更时被自动重新并入本轮 batch。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:416,425`：失败记忆入口是进程级 `PreviouslyFailedReloadFiles`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2270-2280`：`PerformHotReload()` 开头把 `PreviouslyFailedReloadFiles` 全量并回当前 `FileList`，随后清空集合。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2455-2460`：预处理失败后直接记录“Keeping all old angelscript code”，并把整批 `FileList` 回写到 `PreviouslyFailedReloadFiles`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4047-4052`：`bModuleSwapInError` 会把该 module 所有 sections 加入 failed set。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4130-4187`：`ErrorNeedFullReload`、`Error` 都会把 `AllCompiledFiles` 写回 failed set，`ErrorNeedFullReload` 还会同时写入 `QueuedFullReloadFiles`。 |
| 优点 | 修复坏脚本后不需要手动定位并重新触发同一批文件，自动重试的使用体验比较直接。 |
| 不足 | 失败隔离粒度过粗。推断上，任何新的无关保存动作都会夹带历史失败文件进入当前 reload batch，导致 blast radius 被旧错误持续放大，也让“本轮到底是被谁拖失败的”变得不透明。对扩展点来说，这相当于把 retry policy 写死在全局引擎状态里，外部工具很难接入更细的失败恢复策略。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `M.reload()` 每轮重新根据 `loaded_module_times` 计算 `modified_modules`，再把这批 module 交给 `reload_modules()`。Lua 侧如果 `xpcall()` 失败会 `sandbox.exit(); return` 终止本轮，但源码里没有一个跨轮次的全局 failed module 集合去强制污染后续无关 reload。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:552-601`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-623` | 失败可以终止本轮，但失败记忆不必做成全局脏集；下一轮仍应由“这次真的改了谁”来决定输入边界。 |
| puerts | watcher 对每个 `NotifyPath` 单独回调，Editor 模块读取该路径内容后直接 `JsEnv->ReloadSource(InPath, ...)`；运行时异常只记录当前 module/path 的 `reload module exception`，`hot_reload.js` 还会在源码未变时直接 `skip` 当前 URL。整个流程没有把一次失败的 path 自动并到后续所有 reload 调用里。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:52-80`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-147`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1468-1500,1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:79-90` | 把失败影响限定在当前 path/module，能让后续无关改动继续独立前进，也更便于给 IDE/UI 做精准反馈。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把失败记忆从“全局 failed file set”改成“按 file/module 隔离的 quarantine entry”，默认只在相关源再次变化或用户显式重试时回放。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 用 `TMap<FString, FFailedReloadEntry>` 取代裸 `PreviouslyFailedReloadFiles`；entry 至少记录 `RelativePath`、`OwningModule`、`FailureKind`（Preprocess / Compile / NeedFullReload / SwapIn）、`LastAttemptTime` 与 `RetryPolicy`。<br>2. 调整 `PerformHotReload()`：当前 batch 默认只消费新的 changed files；只有当本轮 touched path 命中同一个 `RelativePath/OwningModule`，或用户显式执行 `RetryFailedReloads` 命令时，才把 quarantine entries 合并进来。<br>3. 对 `ErrorNeedFullReload` 单独保留 `PendingFullReloadBatch`，不要再同时塞进 general failed queue；对 `bModuleSwapInError` 也只 quarantine 受影响 module，而不是让未来任意批次都被它牵连。<br>4. 在 Editor/UI/日志里输出 quarantine 列表和最近失败原因，提供“仅重试受影响脚本”“清空失败记忆”“升级为 FullReload”三个明确动作，而不是继续让失败文件隐式混入后续批次。<br>5. 第一阶段保留 legacy 自动重试行为作为 `CVar` fallback；确认项目适应后，再把默认策略切到 scoped retry。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadFailureState.*` |
| 预估工作量 | M |
| 架构风险 | scoped retry 若做得太激进，可能导致用户误以为“坏脚本已经恢复”，但实际只是被 quarantine 了；因此必须配套清晰的状态展示和显式重试入口。 |
| 兼容性 | 向后兼容。首版可以只新增 quarantine 诊断和命令接口，默认继续沿用 legacy 自动重试；后续再通过配置切换到更细粒度的失败隔离。 |
| 验证方式 | 1. 构造“A.as 语法错误后再修改无关 B.as”的场景，确认启用 scoped retry 后 `B.as` 仍可独立热重载，而 `A.as` 留在 quarantine。<br>2. 构造 `ErrorNeedFullReload` 场景，确认文件进入 `PendingFullReloadBatch` 而不是继续污染普通 soft reload。<br>3. 对 legacy fallback 做回归，确认旧项目在未开启新策略时仍保持当前自动重试行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-23 | 变更检测入口缺少 `loaded-source` 索引，观察面过早放大 | 入口分层 + change-source 抽象 | 中高 |
| P1 | Arch-HR-24 | 热重载失败隔离粒度过粗，历史坏脚本持续污染新批次 | 失败状态建模 + scoped retry | 高 |

---

## 架构分析 (2026-04-08 23:48)

### Arch-HR-25：native `UClass` 变化不在当前热重载观察面内，`CodeSuperClass` 边界让 `Live Coding` 只能靠隐式外部恢复

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 与 UE 原生热重载的交互边界、native 基类变化后的状态保持 |
| 当前设计 | 当前 hot reload 观察面只覆盖 `.as` 文件和 Angelscript 自己的 reload 广播。Editor 启动时只对 script roots 注册 `DirectoryWatcher`，runtime 只靠 checker thread 和 `Tick()` 触发 `CheckForHotReload()`；`ClassReloadHelper::Init()` 也只监听 `FAngelscriptClassGenerator::On*` 事件。与此同时，实例迁移故意从 `CodeSuperClass->GetPropertiesSize()` 之后才开始复制，native 基类属性与默认值不属于脚本热重载迁移面。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-381`：Editor 启动时只对 `MakeAllScriptRoots()` 注册 `DirectoryWatcher`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1615-1620,2796-2829`：runtime 只在 `bScriptDevelopmentMode` 下启动 checker thread，并在 `Tick()` 中基于 script-file 变化触发 `CheckForHotReload()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:50-175`：helper 只订阅 `FAngelscriptClassGenerator::OnStructReload/OnClassReload/OnDelegateReload/OnFullReload/OnPostReload`，没有 UE native reload delegate。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4466-4470,4510-4513`：`Lookup.IgnoreBeforeOffset = ClassData.*Class->CodeSuperClass->GetPropertiesSize()`，迁移明确跳过 native base 内存段。 |
| 优点 | 脚本热重载职责边界明确，不会在每次 `.as` 保存时顺带猜测 native DLL / patch 状态。 |
| 不足 | 推断上，当 `Live Coding/C++ Hot Reload` 改了 script class 依赖的 native `CodeSuperClass`、绑定 `UFUNCTION`、或 native 默认值时，当前链路既不会自动调度 Angelscript reload，也不会迁移 native 区域状态。这样会形成“native 合同已变，script class 仍按旧合同运行”的灰色窗口；问题通常只有在后续脚本 reload、对象重建或运行时调用出错时才暴露。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv::HotReload()` 只执行 `UnLua.HotReload()`；Lua 侧围绕 `loaded_module_times`、`loaded_modules`、`update_modules()` 和 `update_global()` 更新 module table / upvalue / `_G`，没有把 native `UClass` 内存迁移混进同一条路径。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:114-176`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-623` | 即使不直接处理 native reload，也把 VM 热更新边界保持得很清楚，不会给出“native 基类也被同一条迁移链覆盖了”的错觉。 |
| puerts | 原生 C++ reload 走显式桥接：模块直接订阅 UE5 `ReloadCompleteDelegate` / UE4 `OnHotReload()`，回调里重建 `JsEnv`。source hot reload 则继续独立走 `ReloadSource()` 和 `Debugger.setScriptSource`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:424-438`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | native 变化和脚本 source 变化分两条显式通路处理，适合 Angelscript 借鉴成 `native contract drift -> 显式告警/强制 full reload`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为每个 script class 建立 native contract fingerprint，并在 UE native reload 后只对受影响类升级为显式 full reload / audit。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 或新建 `AngelscriptNativeContractTracker.*`，为每个生成的 `UASClass` 记录 `CodeSuperClass` 路径、`GetPropertiesSize()`、关键 native `UFUNCTION/UPROPERTY` 签名摘要。<br>2. 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptNativeReloadBridge.*`，在 UE5 接 `FCoreUObjectDelegates::ReloadCompleteDelegate`，UE4 fallback 接 `IHotReloadInterface::OnHotReload()`；首版只做 fingerprint 对比和告警。<br>3. bridge 检测到 `SuperSizeChanged`、`NativeFunctionSignatureChanged`、`NativeDefaultChanged` 等 drift 时，把对应 script class 标成 `NativeContractChanged`，并阻止其继续走普通 `SoftReload` 快路径。<br>4. 在 editor 侧把 drift 信息写入 `StateDump` / log / notification，明确列出受影响脚本类和 native 原因；第二阶段再让它自动进入 `QueuedFullReloadFiles` 或单独的 `PendingNativeBridgeReload`。<br>5. 补集成测试：覆盖“script 类继承 native C++ 类，随后触发 Hot Reload / Live Coding patch”的场景，验证系统会输出显式 drift 原因，而不是静默沿用旧合同。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptNativeReloadBridge.*`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptNativeContractTracker.*` |
| 预估工作量 | M |
| 架构风险 | fingerprint 过粗会漏报 drift，过细会在 harmless native patch 上频繁误报；首版应先 `warn-only`，收集工程里的真实命中，再决定哪些 drift 必须升级成 `FullReloadRequired`。 |
| 兼容性 | 向后兼容。第一阶段只新增诊断和 `SoftReload` 阻断条件，不改变脚本语法与现有 API；自动 full reload 可作为后续 opt-in。 |
| 验证方式 | 1. 构造 native 基类新增或修改 `UFUNCTION/UPROPERTY` 的场景，确认 bridge 能识别受影响 script class。<br>2. 构造仅 `.as` 文件未变、但 native patch 已发生的场景，确认系统会输出 `NativeContractChanged` 告警而不是静默无提示。<br>3. 对未命中的普通 body-only script 修改做回归，确认不会误触发 native drift 路径。 |

### Arch-HR-26：hot reload 主链把测试调度耦进同一条 `Tick/session`，`reload complete` 语义被扩展成“代码切换 + 分批验证”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载主路径与自动化验证的耦合方式 |
| 当前设计 | 当前 hot reload 不是“编译/切换完成即结束”的单一事务。引擎初始化时会创建 `FHotReloadTestRunner`；每次 `PerformHotReload()` 成功编译后，会立即根据文件列表和模块依赖准备测试队列；随后 `Tick()` 在检查下一轮 hot reload 之前先跑 `RunTests()`。测试执行还是分批跨 tick 的，源码注释甚至直接写明“hot reload could be triggered while we are executing tests”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1611-1613`：初始化时创建 `HotReloadTestRunner`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2478-2489`：`PerformHotReload()` 在 asset scan 完成且设置开启时调用 `HotReloadTestRunner->PrepareTests(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2796-2829`：`Tick()` 先执行 `RunTests(this)`，之后才进入下一轮 hot reload 检查。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:542-549`：如果测试还没跑完又来了一次 hot reload，会直接清空旧的 `TestAfterHotReload` 队列。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:599-646`：测试因为 PhysX/editor 锁死问题被设计成分批跨 tick 执行。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:337-341`：PIE 下根本不能执行这条 hot reload 测试路径。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h:19-29,51-61`：这些行为由 `bRunUnitTestsOnHotReload`、`LimitNModulesToTestOnHotReload`、`GarbageCollectEveryNTests` 等 editor 设置直接驱动。 |
| 优点 | 对脚本团队很方便，保存代码后能立刻把“重新编译 + 受影响模块测试”串成一个连续反馈环，且已有模块依赖排序与分批机制。 |
| 不足 | `reload result` 和 `verification result` 被混成了同一条 session。推断上，这会带来三个问题：一是 reload 完成时延被测试批次放大，二是新的文件修改会静默覆盖旧的待测队列，三是未来若想接入除单元测试之外的 verifier（例如 Blueprint audit、native drift 检查、lint、coverage gate），都只能继续往 `Tick()` 里堆状态机。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 编辑器自动模式也只是“文件变化 -> `UUnLuaFunctionLibrary::HotReload()`”；而 `FLuaEnv::HotReload()` 本身只执行 `UnLua.HotReload()`，没有把测试调度或分批验证并进 HMR 主链。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h:32-47`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:112-117`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp:51-57` | 把“触发热重载”和“后续是否要做别的验证”分开，能让 HMR 返回边界更清晰。 |
| puerts | `FSourceFileWatcher` 的回调只负责读取文件并调用 `JsEnv->ReloadSource()`；`OnSourceLoaded()` 只用于登记 watched file，没有验证队列或分批测试状态混进 reload pipeline。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-147`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542` | 保持“代码切换”是一个短而明确的 commit 点，其他验证能力通过外部工具链叠加，而不是塞进 HMR 核心状态机。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前 `HotReloadTestRunner` 从 reload 核心路径抽成独立的 `post-reload verification lane`，保留现有设置但拆开结果语义。 |
| 具体步骤 | 1. 新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptReloadVerificationCoordinator.*`，让 `PerformHotReload()` 只发布 `ReloadSessionId + CompiledModules + RelativeFileList + ECompileResult`，不再直接持有测试队列。<br>2. `FHotReloadTestRunner` 改为 coordinator 的一个 verifier，实现上仍可复用当前 `PrepareTests/RunTests` 逻辑，但队列归 coordinator 管，不再在 `PrepareTests()` 里静默清空旧 session。<br>3. 为 verifier 增加 `SessionId` 和 `SupersededBy` 状态：新一轮 reload 到来时，把旧验证标成 superseded 或 cancel，而不是直接丢弃，避免开发者误判“测试已经执行过”。<br>4. 将 `ECompileResult` 与 `EVerificationResult` 分离到 UI/log/state dump：reload 先报告 `FullyHandled/PartiallyHandled/ErrorNeedFullReload`，验证再单独报告 `Pending/Running/Passed/Failed/Superseded`。<br>5. 第一阶段保持现有设置项和默认行为不变，只是把实现迁到 coordinator；第二阶段再让 Blueprint impact audit、native drift audit、coverage gate 等 verifier 按同一接口挂接。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptReloadVerificationCoordinator.*`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果 coordinator 首版没有处理好 superseded session，可能让团队在 UI 上看到更多“待验证”状态，从而误以为系统变慢；需要明确区分 `reload done` 和 `verification pending` 两个阶段。 |
| 兼容性 | 向后兼容。首版可以完全复用现有 `bRunUnitTestsOnHotReload`、`LimitNModulesToTestOnHotReload`、`GarbageCollectEveryNTests`，只是把队列与状态从 engine 核心中挪出去。 |
| 验证方式 | 1. 构造“两次连续保存发生在同一轮测试尚未跑完之前”的场景，确认旧 session 会被标记 `Superseded`，而不是静默清空。<br>2. 验证 reload log 会先输出 `ECompileResult`，测试 UI 再单独显示 `Pending/Running/Passed/Failed`。<br>3. 在关闭 `bRunUnitTestsOnHotReload` 时回归，确认 HMR 主路径仍保持当前行为且不引入额外 tick 状态。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-25 | native `CodeSuperClass` / `Live Coding` 变化不在当前热重载观察面 | native contract fingerprint + bridge 诊断 | 高 |
| P2 | Arch-HR-26 | hot reload 与测试调度耦合，`reload complete` 语义被拉长 | verification lane 解耦 | 中 |

---

## 架构分析 (2026-04-08 23:59)

### Arch-HR-27：热重载以“本轮最大 `ReloadReq`”统一决策，一处结构性变更会拖住同批次无关模块

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 增量热重载的决策粒度是否停留在 module/class，而不是被整个 batch 的最强严重级别抹平 |
| 当前设计 | `FAngelscriptClassGenerator::Setup()` 先在 module/class/delegate 级别分析差异，但最终只返回“所有 module 的最大 `ReloadReq`”。`FAngelscriptEngine::CompileModules()` 再按这个单值为整批 `CompiledModules` 选择 `PerformSoftReload()`、`PerformFullReload()` 或“全部保持旧代码”。结果是：同一轮保存里只要混入一个 `FullReloadSuggested/Required` 模块，其他本来只需 `SoftReload` 的模块也会被拖入同一条批次路径；在 `SoftReloadOnly` 场景下，甚至会让同批次无关 soft 模块一起失去本轮可见性。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1884-1905`：传播完依赖后，只取 `ModuleData.ReloadReq` 的最大值作为本轮 `ReloadReq` 返回。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3936-3996`：engine 只看一个 `ReloadReq` 分支；`FullReloadSuggested` 在非 `SoftReloadOnly` 直接整批走 `PerformFullReload()`，`FullReloadRequired` 在 `SoftReloadOnly` 时直接 `bShouldSwapInModules = false`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2236-2304`：即使 batch 中同时存在 soft/full class，也是在同一轮全局 staged barrier 中统一执行 `PrepareSoftReload -> FullReload -> DoSoftReload -> FinalizeClass -> InitDefaultObjects -> VerifyClass`。 |
| 优点 | 决策模型简单且保守，能避免“同一轮里部分 class 已换 epoch、部分 class 仍在旧 epoch”带来的关系断裂。 |
| 不足 | module 级增量能力被 batch 级最大严重度覆盖。推断上，`A.as` 的 body-only 改动如果和 `B.as` 的 `UPROPERTY/UFUNCTION` 结构改动同批进入 `CompiledModules`，`A` 也会被迫等待 full path，或在 PIE 下跟着 `B` 一起保持旧代码。当前架构回答“是否需要整批升级”，比回答“哪些模块已经可以安全提交”更强。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv::HotReload()` 只调用 `UnLua.HotReload()`；Lua 侧 `M.reload()` 先根据 `loaded_module_times` 计算 `modified_modules`，再把这份显式 module list 交给 `reload_modules(module_names)`。批次边界是“本轮明确要 reload 哪些 module”，而不是“取所有受影响对象的最大严重级别”。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:571-601,604-623` | 先把 reload session 建模成显式 module 集，再决定每个 module 如何应用更新；不要一开始就把整个 session 压成一个全局枚举值。 |
| puerts | Editor 侧 `FSourceFileWatcher` 对单个 `NotifyPath` 读取源码后直接 `JsEnv->ReloadSource(InPath, ...)`；运行时 `ReloadSource()` 也是把一个 path/source 对传给 JS 侧 `__reload`。没有“整批文件先求最大 reload 严重级别”的调度层。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-131,141-146`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:52-80`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1541`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 让 reload 决策尽量保持 path/module 局部化；真的需要扩大 blast radius 时，再由依赖规则显式扩张。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“本轮 batch 最大 `ReloadReq`”拆成显式 `FReloadPlan`，允许 `SoftReady`、`DeferredFull`、`Blocked` 三种 lane 共存；无法安全拆分时再回退当前全局策略。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 新增 `FReloadPlan`，至少记录 `SoftReadyModules`、`DeferredFullModules`、`BlockedModules`、`CrossLaneDependencies`；`Setup()` 改为产出 per-module 结果和全局 fallback reason，而不是只返回单个 `EReloadRequirement`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 的 `CompileModules()` 中，`SoftReloadOnly` 场景先尝试只对 `SoftReadyModules` 调用 `SwapInModules()` + `PerformSoftReload()`；`DeferredFullModules` 留在旧代码并写入 `QueuedFullReloadFiles`，`BlockedModules` 继续沿用当前 error path。<br>3. 非 PIE 场景下也保留 lane：若 `CrossLaneDependencies` 为空，则允许 `SoftReadyModules` 先提交，再单独对 `DeferredFullModules` 走 full reload；一旦发现 import closure、type propagation 或 module reference update 跨 lane，就整体退回 legacy 全局路径。<br>4. 在 log/state dump 中输出每个 module 的 lane 与原因，例如 `BodyOnly -> SoftReady`、`PropertyLayoutChanged -> DeferredFull`、`BlockedByCrossLaneDependency -> LegacyBatch`，避免继续只给一个全局 `ReloadReq`。<br>5. 首版通过 `angelscript.HotReloadSplitLanes` 之类的 `CVar` 保护，默认保留现有全局决策；先在 automation 中证明 lane 切分稳定，再逐步扩大默认覆盖面。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | L |
| 架构风险 | lane 拆分最大的风险是 old/new module epoch 混用，尤其是 `moduleDependencies`、template instance replacement、`UpdateReferencesIn*` 跨 lane 时可能产生悬挂引用；因此第一阶段必须把“无法证明安全”的 batch 全部折回 legacy 行为。 |
| 兼容性 | 向后兼容。首版只新增 plan 与诊断，默认仍用现有“全局最大 `ReloadReq`”策略；开启新 lane 后，脚本语法和现有 class generator API 都不需要破坏性修改。 |
| 验证方式 | 1. 新增混合批次测试：`A.as` 只改函数 body，`B.as` 改 `UPROPERTY`/`UFUNCTION`；验证在 `SoftReloadOnly` 下 `A` 可立即生效而 `B` 被排入 deferred full reload。<br>2. 构造存在 import/type 交叉依赖的同批次改动，确认系统会识别 `CrossLaneDependencies` 并正确回退 legacy 全局路径。<br>3. 对非混合场景做回归，确认纯 soft 和纯 full batch 的行为与现状一致。 |

### Arch-HR-28：`SwapInModules()` 先提交、`bModuleSwapInError` 后置报错，late validation 不是原子失败

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载提交是否具备原子性，以及 late validation 失败时当前 live state 会不会已经进入新 epoch |
| 当前设计 | 当前 compile pipeline 先把新 module 写进 `ActiveModules` / `ActiveClassesByName`，再执行 class generation、default object 初始化和 `VerifyClass()`。但 `FinalizeClass()` / `VerifyClass()` 里仍有大量会把 `ModuleData.NewModule->bModuleSwapInError` 置位的校验，例如 root component 冲突、抽象 override component 缺失、attach parent 不存在或类型不合法、`NotAngelscriptSpawnable` component 等。后处理阶段对这类 late error 的主要动作只是把该 module 的 code sections 重新塞回 `PreviouslyFailedReloadFiles`，并在下一轮 `Analyze()` 时把旧模块升级为 `FullReloadRequired`。推断上，这意味着本轮已经换进去的新 module 和对应 reload 广播并不会因为 late validation 失败而自动回滚。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2907-2939,2948-2963`：`SwapInModules()` 会先改写 `ActiveModules` 并重建 `ActiveClassesByName/ActiveDelegatesByName/ActiveEnumsByName`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3938-3969,3993-3996`：engine 在 `ClassGenerator.PerformSoftReload()/PerformFullReload()` 之前就调用 `SwapInModules()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2306-2315,2317-2373`：`VerifyClass()` 在 `InitDefaultObjects()` 之后执行，但其后仍会继续进入 `OnClassReload` / `OnFullReload` 广播。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5314-5319,5344-5350,5522-5526,5597-5637,5672-5676`：late validation 发现问题时只记录 `ScriptCompileError(...)` 并置 `bModuleSwapInError = true`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4047-4053`：post-swap 仅把 `bModuleSwapInError` 模块加入 failed set，未见本轮 rollback。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:184-186`：直到下一轮 `Analyze()`，旧模块的 `bModuleSwapInError` 才会被提升成 `FullReloadRequired`。 |
| 优点 | 许多依赖真实 `UClass/CDO` 和 editor state 的校验可以在真实对象图上完成，错误信息更接近最终运行态。 |
| 不足 | late validation 不是原子失败。推断上，一次被 `bModuleSwapInError` 标记的 reload 已经修改了 active module graph，并可能已经发出了 `OnClassReload/OnFullReload`；随后系统只是在“下一轮”尝试纠正它。这会让 runtime/editor 在两个保存动作之间观察到一个“已提交但被判坏”的 epoch。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `reload_modules(module_names)` 会先在 sandbox 中逐个 `load/xpcall` 新 module；任一 module 执行失败就 `sandbox.exit(); return`，只有全部成功后才进入 `update_modules(old_modules, new_modules, module_envs)`。也就是说，module table 的真正替换发生在一个更明确的“验证通过后提交”点。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:571-601` | 把“装载并验证新代码”和“提交旧对象引用替换”分成两个阶段，失败时宁可维持旧状态，也不要先提交再补救。 |
| puerts | `ReloadSource()` 的提交面就是单个 `Path + Source`；Editor 也是对某个 `NotifyPath` 直接调用 `JsEnv->ReloadSource()`。即使 `TryCatch` 报错，影响面仍然局限在当前 source path，而不是一个已经改写了全局 module graph 的多模块事务。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1541` | 即便没有完整 rollback，也应尽量缩小“先提交、后发现错误”的作用域，把失败半径限制在单 source/module。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 增加 `PreSwapValidation + CommitSnapshot` 两段式提交：能前移的校验先前移，必须依赖 live `CDO` 的校验才允许进入可回滚的 post-swap 阶段。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 新增 `FPreSwapValidationReport`，把当前 `FinalizeClass()` / `VerifyClass()` 中不依赖真实 `ActiveModules` 提交的检查前移，例如重复 root component、抽象 override component、`NotAngelscriptSpawnable` default component、明显非法的 attach 目标元数据等。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 中让 `SwapInModules()` 只在 `PreSwapValidationReport` 无 hard error 时执行；hard error 直接走现有 compile error/diagnostics path，不再让 bad module 先进入 `ActiveModules`。<br>3. 对仍必须依赖真实 `CDO` / editor state 的校验，新增 `FReloadCommitSnapshot`：保存旧 `ActiveModules` 项、重命名前后的 module 名、以及已有的 script reference reverse map。若 post-swap 阶段再次命中 hard error，则在 `OnClassReload/OnFullReload` 广播前调用 `RevertSwapInModules()` 恢复旧 graph。<br>4. 第一阶段如果 rollback 还做不到完全安全，就至少把 `LateSwapInError` 分类单独输出，并阻止后续 `OnFullReload`/`OnPostReload` 继续广播“成功式”信号；legacy 行为保留在 `CVar` 后面作为 fallback。<br>5. 为 diagnostics/state dump 增加 `PreSwapError`、`LateSwapInError`、`RolledBack` 三种结果，避免继续把这类问题只埋在 `PreviouslyFailedReloadFiles` 里。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | L |
| 架构风险 | 把校验前移可能引入“预演环境”和真实 `CDO` 环境不一致的问题；而 rollback 若覆盖不全，又会把系统带入另一种半提交状态。因此首版应先做 `PreSwapValidation` 和结果分类，把 rollback 作为第二阶段 opt-in。 |
| 兼容性 | 向后兼容。首版可以只增加 pre-swap audit 与 late-error 分类，不改变默认 reload 成功路径；真正的 rollback 与事件抑制通过 `CVar` 渐进开启。 |
| 验证方式 | 1. 构造 `DefaultComponent`/`Attach` 非法配置，确认开启 pre-swap audit 后模块不会先进入 `ActiveModules` 再被标坏。<br>2. 对仍保留在 post-swap 的校验，验证命中 `LateSwapInError` 时系统不会继续广播“成功式” full reload 信号，或在启用 rollback 时能恢复旧 module graph。<br>3. 对正常 soft/full reload 场景做回归，确认 pre-swap audit 不会误伤现有合法脚本。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-27 | “本轮最大 `ReloadReq`”压平 module 级增量能力 | reload plan 分 lane + 按 module 提交 | 高 |
| P1 | Arch-HR-28 | `SwapInModules()` 先提交、late validation 后报错导致非原子失败 | pre-swap audit + 可回滚提交 | 高 |

---

## 架构分析 (2026-04-09 00:15)

### Arch-HR-29：热重载控制面仍是“公共文件队列 + 粗粒度 drain”，外部扩展只能间接改写 engine 状态

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载入口的可扩展性与调用契约 |
| 当前设计 | 当前热重载并没有一个显式的 request/service 层。`FAngelscriptEngine` 公开了 `CheckForHotReload(ECompileType)`，但真正的输入载体是公开可写的 `FileChangesDetectedForReload`、`FileDeletionsDetectedForReload` 和 `LastFileChangeDetectedTime`；`DirectoryWatcher`、测试代码都会直接往这些数组里塞 `FFilenamePair`，然后再调用或等待 `CheckForHotReload()` 把队列 drain 到私有 `PerformHotReload()`。换句话说，现有“API”本质上是改写 engine 队列，而不是提交一个带来源、目标、意图的热重载请求。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:188,361-365,367,425,476-479`：对外公开的是 `CheckForHotReload(ECompileType)` 与三个可写队列字段，而真正执行入口 `PerformHotReload(...)` 仍是私有实现。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2743-2770`：`CheckForHotReload()` 直接把公开队列拼成 `FileList`，随后调用私有 `PerformHotReload()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:55,61-86`：编辑器 watcher 直接更新 `Engine.LastFileChangeDetectedTime`、`Engine.FileChangesDetectedForReload`、`Engine.FileDeletionsDetectedForReload`。<br>`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp:49-69`：测试辅助也通过 `Engine.FileChangesDetectedForReload.AddUnique(...)` 和 `Engine.CheckForHotReload(...)` 驱动热重载，而不是调用独立服务接口。 |
| 优点 | 路径短，接入 watcher 和测试很直接；现有行为对工程内部代码足够可控。 |
| 不足 | 扩展点过低层。推断上，IDE、commandlet、native reload bridge、BlueprintImpact 扫描器、远端脚本源或未来的 `FunctionBodyOnly` 快路径，都必须先理解并操纵 engine 内部队列，才能触发热重载。由于 request 不是一等对象，当前也无法稳定表达“这次 reload 的来源是什么、只针对哪些 module/file、是否允许扩张依赖闭包、是否只是诊断不提交”等意图。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 公开控制面是显式 API：`UUnLuaFunctionLibrary::HotReload()` 作为 `BlueprintCallable` 暴露给外层，最终转发到模块接口；Lua 侧 `M.reload(module_names)` 还支持显式指定 module 列表，而不是要求外部先改内部 watcher 队列。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaFunctionLibrary.h:21-35`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp:31-34`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-623` | 把“触发 reload”抽象成可调用接口，并允许请求显式携带目标 module 集。 |
| puerts | 公开控制面同样是显式方法：`IJsEnv` / `FJsEnv` 直接提供 `ReloadModule()`、`ReloadSource()`、`OnSourceLoaded()`；editor watcher 只负责收集变化，然后调用这些 API，不需要改写 `JsEnv` 内部队列。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:46-50,87-91`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:84-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1504-1542` | 热重载入口应当是 service/facade，文件监听只是其中一个调用方。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有队列前新增显式 `FHotReloadRequest` / `FHotReloadSession` 控制面，把 watcher/test/native bridge 从“写内部数组”迁到“提交请求”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FHotReloadRequest`，至少包含 `Origin`（`DirectoryWatcher/RuntimeScan/Test/Manual/NativeReload`）、`CompileType`、`Files`、`Modules`、`bIncludeDependents`、`bAllowDiscoveryOnly`。<br>2. 新增 engine-owned `RequestHotReload(const FHotReloadRequest&)`，内部统一落到当前 `CheckForHotReload()` / `PerformHotReload()`，但不再要求外部直接写 `FileChangesDetectedForReload`。<br>3. 把 `AngelscriptDirectoryWatcherInternal.cpp` 改成构造 request 后调用 `RequestHotReload()` 或 `EnqueueHotReloadRequest()`；`AngelscriptHotReloadFunctionTests.cpp` 也迁到 request API，避免测试继续把内部数组当契约。<br>4. 第一阶段保留 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 作为兼容 shim：`CheckForHotReload()` 发现旧队列非空时，包装成 `Origin=LegacyQueue` 的 request，再走同一路径。<br>5. 第二阶段为工具链补显式入口，例如 editor menu / commandlet / native reload bridge 都只调用 `RequestHotReload()`，同时在日志与 dump 中输出 request `Origin` 和目标集。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadRequest.h` |
| 预估工作量 | M |
| 架构风险 | 如果 request 层和 legacy 队列层同时生效，容易出现重复调度或 session 归因混乱；首版需要明确 `LegacyQueue -> Request` 的单向兼容桥，不要让两条路径并行执行。 |
| 兼容性 | 向后兼容。第一阶段保留现有 watcher 与测试语义，只是把内部数组访问收口到 request shim；外部脚本用户和现有 `.as` 行为不受影响。 |
| 验证方式 | 1. 保持现有 `DirectoryWatcher` 自动化测试通过，确认 watcher 改走 request API 后入队/消费语义不变。<br>2. 增加“手动提交 `Modules` only / `Files` only / `LegacyQueue`”三类测试，确认最终都会落到同一执行路径。<br>3. 对未来 native reload bridge 做接线试验，验证无需直接改 engine 队列即可触发 targeted reload 或诊断。 |

### Arch-HR-30：热重载可观测性仍停留在 queue 层，`StateDump` 明确承认“私有热重载状态未导出”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载 session / 决策 / 失败状态的可观测性 |
| 当前设计 | 当前 `StateDump` 只能导出 pending queue 视角：`HotReloadState.csv` 只写 `FileChangesDetectedForReload` 与 `FileDeletionsDetectedForReload`，并显式返回 `PartialExport`，说明 `FileHotReloadState`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles`、`bWaitingForHotReloadResults`、测试验证状态等私有跟踪数据都没有导出。与此同时，测试若想验证更深层状态，只能通过 `FAngelscriptHotReloadTestAccess` 等友元访问内部字段。也就是说，当前架构里“真正的 hot reload 决策面”对 dump/tooling 基本是黑盒。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:981-1013`：`DumpHotReloadState()` 仅导出 `PendingReload` / `PendingDeletion` 队列，并把结果改成 `PartialExport`，错误信息明确写着 “Private hot reload tracking data is not exported; only public reload queues are included.”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:400-425`：真正影响行为的 `FileHotReloadState`、`bWaitingForHotReloadResults`、`HotReloadTestRunner`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles`、`NextHotReloadCheck` 都是私有状态。<br>`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp:49-79`：测试需要自定义 `FAngelscriptHotReloadTestAccess` 去读取 `QueuedFullReloadFiles` 和诊断数量，说明现有公开可观测面不足以支撑验证。 |
| 优点 | 避免把 engine 私有实现过早冻结成外部 ABI，短期改内部结构比较自由。 |
| 不足 | 当前无法系统回答“这次 reload 为什么被延后成 `QueuedFullReloadFiles`、为什么某个文件进了 failed set、上一轮是 `PartiallyHandled` 还是 `ErrorNeedFullReload`、验证阶段是否还在跑、native drift 是否已被识别”。这不仅影响 CI / commandlet / dump，也会拖慢后续所有 hot reload 架构改进，因为每项增强都得重新发明一套日志和测试窥探手段。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `M.reload(module_names)` 把 reload 目标显式建模成 module 名集合；即使不做额外 state dump，外部系统也能从 `module_names` 与 `loaded_module_times` 理解“这次到底在 reload 哪些 module”。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-623` | 只要请求对象是显式的，观测与诊断就不必退化成“猜内部队列长什么样”。 |
| puerts | `IJsEnv` / `FJsEnv` 明确区分 `ReloadModule(ModuleName, JsSource)` 与 `ReloadSource(Path, JsSource)`；实现层还会把当前 path/module 写入日志，例如 `reload js module [path]`、`reload js [path]`，异常也限定在当前 path/module。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:46-50,87-91`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1482-1495,1516-1541` | 先把热重载的“单位”和“作用域”建模成一等数据，再谈日志、UI、dump，能天然得到更好的可观测性。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 request 层之上增加 `FHotReloadSessionRecord`，把当前私有状态整理成可查询、可导出的 session 视图；`HotReloadState.csv` 从 queue dump 升级为 session dump。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FHotReloadSessionRecord`，至少记录 `SessionId`、`Origin`、`RequestedFiles/Modules`、`ExpandedFiles`、`CompileType`、`CompileResult`、`DeferredFiles`、`FailedFiles`、`VerificationState`、`NativeDriftReasons`。<br>2. 让 `RequestHotReload()` / `CheckForHotReload()` 在进入 `PerformHotReload()` 前创建 session，随后在队列收集、依赖扩张、compile、swap、defer、verify 等阶段持续写回状态，而不是只把数据散落在多个私有字段里。<br>3. 扩展 `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`：保留现有 `HotReloadState.csv` 作为 legacy queue 视图，同时新增 `HotReloadSessions.csv` 或 JSON，导出最近 N 次 session 的关键决策与结果。<br>4. 给测试与工具链新增只读查询接口，例如 `GetRecentHotReloadSessions()` / `GetHotReloadDebugSnapshot()`；逐步移除 `FAngelscriptHotReloadTestAccess` 对私有字段的直接窥探。<br>5. 把后续计划中的 native reload bridge、verification lane、failure quarantine、deferred asset repair 都统一挂到 session record 上，避免每条新链路各自再造 dump 和日志格式。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadSession.h` |
| 预估工作量 | M |
| 架构风险 | 如果 session 记录字段设计过细，首版容易被内部实现细节绑死；因此第一阶段应只暴露稳定语义字段，不直接把所有私有容器原样序列化出去。 |
| 兼容性 | 向后兼容。现有 `HotReloadState.csv` 可以原样保留，只是新增更高层的 session 导出与查询接口；现有脚本与热重载行为不变。 |
| 验证方式 | 1. 触发一次 `PartiallyHandled`、一次 `ErrorNeedFullReload`、一次普通 `FullyHandled`，确认 dump 能区分三种 session，而不是只看到空/非空队列。<br>2. 让测试改用 `GetRecentHotReloadSessions()` 断言 `QueuedFullReloadFiles` 与失败原因，确认无需再直接窥探私有字段。<br>3. 在开启后续 verification lane 或 native drift 诊断后，验证这些信息能自动出现在同一 session 记录里。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-29 | 热重载入口仍以公共文件队列为事实控制面 | request/service 抽象收口 | 高 |
| P2 | Arch-HR-30 | 热重载 dump 只覆盖 queue，缺少 session 级观测 | observability/session 记录层新增 | 中高 |

---

## 架构分析 (2026-04-09 00:24)

### Arch-HR-31：`DirectoryWatcher` 注册是“一次性局部 handle”，输入源生命周期没有形成可解绑契约

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编辑器侧变更检测入口的生命周期管理 |
| 当前设计 | `AngelscriptEditor` 启动时对每个 script root 直接注册 `DirectoryWatcher` 回调，但 `FDelegateHandle` 只存在于 `StartupModule()` 的局部变量里；模块对象本身没有持有 watcher handle，也没有在 `ShutdownModule()` 里执行对称解绑。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78-94`：`OnScriptFileChanges()` 是静态函数，直接通过 `FAngelscriptEngine::Get()` 把变化写入主引擎队列。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:366-381`：每个 root 都调用 `RegisterDirectoryChangedCallback_Handle(...)`，但 `WatchHandle` 是循环内局部变量。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:57-60`：模块成员只有 `StateDumpExtensionHandle`，没有 watcher handle 容器。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:676-689`：`ShutdownModule()` 只移除 `OnObjectPreSave`、state dump 和 tool menu，没有 `UnregisterDirectoryChangedCallback_Handle(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`：回调本身直接修改 `Engine.LastFileChangeDetectedTime`、`FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`，说明 watcher callback 已经是热重载事实入口。 |
| 优点 | 启动路径非常短，接入 `DirectoryWatcher` 的代码量小；在“编辑器单次启动到关闭”的理想路径下可以工作。 |
| 不足 | 推断上，一旦出现模块重载、测试环境重复初始化、script root 变更或未来的多 coordinator 场景，旧 watcher 既无法显式解绑，也无法更新到新 root 集合。`AddUnique` 能避免同一文件重复入队，但挡不住重复 callback、悬空 callback 或无法回收的注册项。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FSourceFileWatcher` 是显式 watcher owner：注册时把 `Dir -> DelegateHandle` 存进 `WatchedDirs`，析构时遍历 `UnregisterDirectoryChangedCallback_Handle(...)` 做对称解绑。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:92-102` | watcher 句柄和回调生命周期应该由对象持有，而不是留在 `StartupModule()` 的局部变量里。 |
| UnLua | 热重载入口绑定在 `ULuaEnvLocator` / `ULuaEnvLocator_ByGameInstance` 的 env owner 上；`HotReload()` 是 owner 方法，生命周期随 locator / env 走，不是 fire-and-forget 的全局注册。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:18-33`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:76-82` | 即使不照搬 watcher 设计，也值得借鉴“reload 入口必须有明确 owner”的原则。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 script root watcher 引入显式 owner 和对称解绑，把当前“注册即忘”的输入源改成可管理的 editor 服务。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h` 增加 `TMap<FString, FDelegateHandle> ScriptRootWatchHandles`，或新建一个更小的 `FAngelscriptScriptRootWatcher`/`FAngelscriptHotReloadInputSource` 来专门持有 root 与 handle。<br>2. 把 `StartupModule()` 里的 watcher 注册提取成 `RegisterScriptRootWatchers()`，将 `MakeAllScriptRoots()` 的快照与对应 handle 一起保存；如果未来支持动态 root 变更，再补 `RefreshScriptRootWatchers()`。<br>3. 在 `ShutdownModule()` 中于卸载其他 editor hook 之前遍历 `ScriptRootWatchHandles`，逐个调用 `UnregisterDirectoryChangedCallback_Handle(...)`，然后清空容器。<br>4. 将静态 `OnScriptFileChanges()` 收敛到 owner 方法或至少通过 owner 转发，这样后续若引入 `NativeReloadBridge`、多 engine coordinator 或测试 fake watcher，都能复用同一入口。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` 增加生命周期测试：验证重复注册/销毁不会造成同一路径多次入队，模块关闭后也不会继续响应旧 callback。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 预估工作量 | S-M |
| 架构风险 | 主要风险是模块关闭顺序与 `DirectoryWatcher` 模块依赖顺序；首版应保持 owner 很薄，只做注册/解绑和转发，不同时引入更大的 reload 逻辑重构。 |
| 兼容性 | 向后兼容。对脚本作者和现有 `.as` 行为没有破坏，只改变 editor 内部 watcher 生命周期；首版甚至可以保持现有静态回调函数不变，只先把 handle 收口。 |
| 验证方式 | 1. 重复初始化/关闭 editor 模块或专门构造 watcher owner 生命周期测试，确认同一 `.as` 变化只入队一次。<br>2. 在脚本 root 变化后重绑 watcher，确认旧 root 不再触发回调，新 root 能正常入队。<br>3. 运行现有 `AngelscriptDirectoryWatcherTests`，确认 folder add/remove 与 rename window 语义保持不变。 |

### Arch-HR-32：`CheckForHotReload()` 在单个 global drain 中混合 fresh edit、delayed delete 和 deferred full reload，session 边界在语义分析前就已放大

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载批次边界与增量调度粒度 |
| 当前设计 | 当前批次边界不是 module/path manifest，而是“这一 tick 里主引擎所有队列的总和”。`DirectoryWatcher` 和 runtime checker 都把变化压进共享队列；`Tick()` 再按固定节流窗口把 fresh edit、延迟删除和 `QueuedFullReloadFiles` 一次性拼成一个 `FileList`，再交给 `PerformHotReload()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`：每次文件变化都会更新共享的 `Engine.LastFileChangeDetectedTime`，并把新增/修改/删除写入共享队列。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2779`：`CheckForHotReload()` 先 drain `FileChangesDetectedForReload`，再按全局 `0.2s` 规则拼入 `FileDeletionsDetectedForReload`，非 `SoftReloadOnly` 时再拼入 `QueuedFullReloadFiles`，最后立刻 `PerformHotReload(CompileType, FileList)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2794-2829`：`Tick()` 只按固定 `0.1s` 周期检查，并且对本轮全部变化统一决定 `SoftReloadOnly` 或 `FullReload`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4158-4187`：一旦结果是 `PartiallyHandled` 或 `ErrorNeedFullReload`，本轮所有编译文件都会整体回灌到 `QueuedFullReloadFiles`，等待之后再与新变化合批。 |
| 优点 | 合批逻辑简单，能减少过于频繁的小 reload；删除延迟窗口也能照顾 rename 场景。 |
| 不足 | 真正的问题发生在 `ClassGenerator.Setup()` 之前：不同来源、不同风险等级的变化已经被混成一个 session。推断上，两个互不相关的文件只要落在同一 drain 窗口里，就会共享一次预处理、依赖扩张和 reload 决策；而 deferred full reload backlog 也会趁着下一次无关保存一起被放大。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载批次是显式 `module_names` 列表。`loaded_module_times` 先按 module 记录修改时间，`M.reload()` 再生成 `modified_modules` 并只对这组 module 调 `reload_modules(module_names)`；没有把“所有当前队列”默认拼成一个全局 session。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:114-119`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-623` | 先把 reload 单元建模成明确的 module 集，再决定是否扩张依赖，能把 blast radius 控制在语义分析之前。 |
| puerts | watcher 只跟踪已经加载过的具体 source file；`OnDirectoryChanged()` 只有在 watched file 的 `MD5` 真变化时才回调 `NotifyPath`，随后由 `ReloadSource(Path, JsSource)` 处理这个单一路径。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:52-80`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:46-50`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542` | 把 batch 单位固定成 `NotifyPath`/`ModuleName` 这样的显式键，避免 deferred backlog 与 fresh save 在入口层面被无差别合并。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 queue/request 之上补一层 `BatchManifest` 调度，把“何时合批、哪些文件必须一起 reload”从隐式 tick 规则改成显式 manifest 规则。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FHotReloadBatchManifest`，至少记录 `BatchId`、`Origin`、`Files`、`ReasonKinds(Modify/Delete/DeferredFullReload)`、`ReadyAt`、`CompileTypeHint`、`bCanMergeWithDeferred`。<br>2. 让 `QueueScriptFileChanges()`、runtime checker 和未来的 `RequestHotReload()` 不再直接只写共享数组，而是先写入 manifest；删除延迟改成 manifest 级或 path 级 `ReadyAt`，不要继续复用单个 `LastFileChangeDetectedTime`。<br>3. 把 `QueuedFullReloadFiles` 升级为独立 deferred manifest lane：默认只在“同 module/同 request”或显式 `FlushDeferredFullReloads` 时与 fresh edit 合并，避免无关保存顺手触发旧 backlog。<br>4. 调整 `CheckForHotReload()`：每次只 drain ready manifest，形成带 `BatchId` 的 session，再把该 manifest 交给 `PerformHotReload()`；如果担心 tick 里触发过多 session，可增加“每 tick 最多 N 个 batch”的限流。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` 和 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` / `AngelscriptHotReloadScenarioTests.cpp` 增加批次边界测试：覆盖“两个无关文件同帧保存”“rename window”“deferred full reload backlog 不应搭车无关 fresh save”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 调整 batch 边界会改变日志顺序、session 数量和部分测试对“本轮一起 reload 了哪些文件”的预期；首版应先保守只拆开明显无关的 deferred lane，并用 `CVar` 或 debug 开关保留旧合批策略。 |
| 兼容性 | 向后兼容。脚本 API 不变，默认 reload 语义仍是“保存后自动生效”；变化主要体现在 session 粒度更细、无关文件更少被绑进同一轮 reload。 |
| 验证方式 | 1. 构造两个互不依赖的 `.as` 文件在同一 tick 内保存，确认新调度下能看到两个独立 batch/session，而不是一个合并 `FileList`。<br>2. 构造 rename 场景，确认 old/new path 仍会被同一 manifest 正确 coalesce。<br>3. 先触发 `PartiallyHandled` 生成 deferred full reload，再修改另一个无关文件，确认 backlog 不会自动与该新修改绑定到同一 session。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-32 | 热重载 batch 在入口层就把 fresh/deferred/delete 混成单一 session | 调度层重构 + batch manifest | 高 |
| P2 | Arch-HR-31 | `DirectoryWatcher` 注册缺少 handle ownership 与对称解绑 | 生命周期治理 | 中高 |

---

## 架构分析 (2026-04-09 00:33)

### Arch-HR-33：热重载输入面没有显式线程拥有者，`DirectoryWatcher` / checker thread / main-thread drain 共享同一组可变队列

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载输入缓冲的线程所有权与同步边界 |
| 当前设计 | editor watcher、runtime checker thread 和主线程 `CheckForHotReload()` 都直接读写 `FAngelscriptEngine` 上的 hot reload 队列；现有协调基本只靠 `volatile bool bWaitingForHotReloadResults`，没有专门的 hot reload queue lock，也没有“只能在哪个线程 drain/commit”的显式契约。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:394-419,477-479`：引擎为 context pool 提供了 `GlobalContextPoolLock`，但 hot reload 相关的 `FileHotReloadState`、`QueuedFullReloadFiles`、`FileChangesDetectedForReload`、`FileDeletionsDetectedForReload` 仅以普通容器公开保存，线程协调只有 `volatile bool bWaitingForHotReloadResults`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`：`QueueScriptFileChanges()` 直接写 `Engine.LastFileChangeDetectedTime`、`FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1658-1700`：`StartHotReloadThread()` 启动后台线程，并在该线程里切换 `bWaitingForHotReloadResults`、调用 `CheckForFileChanges()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2779`：`CheckForHotReload()` 在主线程侧 append + empty 同一批队列。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2867-2894`：`CheckForFileChanges()` 在 checker thread 路径里先 `Empty()` 再重新填充 `FileChangesDetectedForReload`。 |
| 优点 | 数据路径短，现有实现成本低；在单 producer、单 consumer、时序稳定的场景下比较直接。 |
| 不足 | 当前架构没有把“谁是 producer、谁是唯一 consumer、哪些容器必须在 game thread 上 drain”做成一等设计。推断上，这会让 future native reload bridge、多 coordinator、或 editor watcher 与 runtime scan 叠加时继续共享无锁可变状态；轻则出现重复/丢失事件，重则把 session 诊断和行为一致性都建立在时序偶然性上。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor watcher 不自己操作一组共享 reload 队列，而是在 `Auto` 模式下直接调用 `UUnLuaFunctionLibrary::HotReload()`；Lua 侧再用 sandbox 的 `reloading` 标志和 `enter/exit` 包住一次重载区间，把模块替换串行化。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:112-118`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:121-122,179-190,198-209` | 即使不引入复杂锁，也应先把 reload section 做成显式串行区，而不是让多个 producer 直接改引擎共享数组。 |
| puerts | `FSourceFileWatcher` 明确拥有 watcher 状态，并对 `WatchedDirs/WatchedFiles` 与 `OnDirectoryChanged()` 使用 `FScopeLock`；真正执行 `ReloadModule/ReloadSource()` 时，`FJsEnvImpl` 还会校验线程并在 threaded 模式下持有 `v8::Locker`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80,92-102`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1504-1523` | 输入缓冲和执行面都需要显式线程边界：watch state 由 owner + lock 保护，reload 执行面则坚持 thread-affinity。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 hot reload 输入层补一个 engine-owned `Inbox/QueueOwner`，把 producer 写入与 game-thread drain 分离；第一阶段只收口同步，不改现有重载语义。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptHotReloadInbox`，内部持有 `PendingAdds`、`PendingDeletes`、`PendingDeferredFullReloads`、`LastChangeTimeByOrigin`，并以 `FCriticalSection` 或 `FRWLock` 保护。<br>2. 把 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` 的 direct array write 改成 `Engine.EnqueueHotReloadChanges(EHotReloadOrigin::DirectoryWatcher, ...)`；把 `CheckForFileChanges()` 改成只调用 `EnqueueHotReloadChanges(EHotReloadOrigin::RuntimeScan, ...)`，不再直接 `Empty()/Add()` `FileChangesDetectedForReload`。<br>3. 把 `CheckForHotReload()` 收敛成唯一 drain 点，显式 `check(IsInGameThread())`，并由 `Inbox.DrainReadyBatch()` 返回本轮 batch；未来的 native reload bridge 也只允许往 inbox 入队。<br>4. 将 `bWaitingForHotReloadResults` 从裸 `volatile bool` 收敛为 inbox/session 状态机里的 `TAtomic<EHotReloadScanState>` 或等价字段，避免继续把同步和状态表达混在一起。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` 增加并发契约测试：模拟 watcher 入队与 checker scan 交错，验证不会丢失、重复或越线程 drain。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 锁的范围如果包住目录枚举、hash 或 import 分析，会把 watcher 卡进慢路径；因此第一阶段必须把锁严格限制在“容器写入/交换”上，计算工作留在锁外。 |
| 兼容性 | 向后兼容。首版只改变 producer/consumer 的同步方式，不改变默认 auto hot reload 行为，也不要求脚本层修改。 |
| 验证方式 | 1. 构造 editor watcher 与 runtime checker 同时产生变更的测试，确认 `DrainReadyBatch()` 结果与单线程基线一致。<br>2. 在开发构建中为 drain path 加 `check(IsInGameThread())`、为 producer 加 origin trace，确认没有越线程执行 `PerformHotReload()`。<br>3. 对比变更前后的 queue/session 统计，验证没有新增重复 reload 或漏 reload。 |

### Arch-HR-34：热重载策略被 `bScriptDevelopmentMode` 隐式硬编码，缺少 `Auto/Manual/Never` 策略面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载的策略面、触发策略与用户可控性 |
| 当前设计 | 当前 editor 启动时会无条件安装 `ClassReloadHelper` 和 script root watcher；runtime 侧只要 `bScriptDevelopmentMode` 成立，就会在 `Tick()` 周期性自动执行 `CheckForHotReload()`。已暴露的相关开关只有 `as-development-mode` 和 `angelscript.UseUnrealReload`，前者是是否进入开发模式，后者只是 reinstance backend 切换，不是 `Auto/Manual/Never` 策略。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-381`：`StartupModule()` 无条件调用 `FClassReloadHelper::Init()`，随后直接对所有 script roots 注册 `DirectoryWatcher`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:516-535`：命令行侧只解析 `as-development-mode`，没有独立 hot reload mode。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:869-874`：`bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2797-2829`：一旦进入 script development mode，就会按环境自动走 `CheckForHotReload(ECompileType::SoftReloadOnly/FullReload)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:20-23`：当前唯一公开的 hot reload CVar `angelscript.UseUnrealReload` 只切换 backend，不控制触发策略。 |
| 优点 | 默认体验直接，开发者在 editor 中几乎零配置就能得到自动热重载；测试和调试链也更容易假定“保存即重载”。 |
| 不足 | 策略面过窄。当前很难表达“继续 watch 但由我手动 apply”“本轮大改期间只收集 pending changes 不立刻 reload”“命令行/CI/自定义工具想显式触发某轮 reload”这类工作流。结果是触发策略被绑死在 runtime/editor 生命周期里，而不是成为可扩展的产品化接口。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 明确把策略做成配置枚举：`EHotReloadMode { Manual, Auto, Never }`；watcher callback 只有在 `Auto` 模式才真正触发 `UUnLuaFunctionLibrary::HotReload()`，而手动路径仍然保留。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h:31-48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-35,112-118`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:28-33,76-82` | “是否自动触发”和“如何执行 reload”应拆开；auto 只是其中一种策略，不应写死在 watcher 生命周期里。 |
| puerts | 把启用与 watcher 作为独立配置：`AutoModeEnable` 控制是否启用默认 env，`WatchDisable` 控制是否开启 watch；editor 只在 `IsWatchEnabled()` 时创建 `FSourceFileWatcher`，同时公共模块接口还暴露 `ReloadModule()` 等显式触发入口。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h:24-50`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37-42`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:75-95,441-446`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76-79,116-151,154-164` | 策略面至少要支持“启用运行时”“是否 watch”“显式 reload API”三个维度，而不是只有一个隐式 auto 模式。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有引擎/编辑器热重载链上增加显式 `HotReloadMode` 策略层，先把“收集变化”和“执行 reload”解耦，再逐步补充手动入口。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` 增加 `EAngelscriptHotReloadMode { Auto, Manual, Never }`，并在 `UAngelscriptSettings` 暴露 `HotReloadMode`；默认值保持 `Auto`，保证旧项目零配置兼容。<br>2. 调整 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`：`Auto` 模式维持现有 watcher 注册，`Manual` 模式允许继续注册 watcher 但只入 pending inbox、不自动 apply，`Never` 模式则直接不注册 watcher。<br>3. 调整 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`：`Tick()` 里只有 `Auto` 才周期性 `CheckForHotReload()`；`Manual` 只维护 pending 状态并暴露 `FlushPendingHotReload()` / `RequestHotReloadNow()`，`Never` 则完全跳过自动检查。<br>4. 在 editor 菜单、console command 或 commandlet 中新增显式入口，调用统一 request API 执行“reload pending changes”“reload selected modules/files”；这样大型重构、调试、批量资产修复都能进入 hand-controlled workflow。<br>5. 增加模式测试：覆盖 `Auto` 的现有行为不变、`Manual` 下保存文件只累积 pending 不自动 reload、`Never` 下 watcher 与 tick 都不触发 reload，并在 PIE 前对 `Manual` 模式给出 pending-change 提示。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 预估工作量 | M |
| 架构风险 | `Manual` 模式会引入“磁盘脚本已变、内存代码未变”的有意漂移；如果没有 pending count 和 mode 可视化，开发者会误判当前运行的代码版本。 |
| 兼容性 | 向后兼容。默认 `Auto` 保持现有行为；`Manual/Never` 作为新增策略，不破坏现有脚本或编辑器工作流。 |
| 验证方式 | 1. 在三种模式下跑现有 watcher 与 hot reload 自动化，确认 `Auto` 的结果与当前基线一致。<br>2. 为 `Manual` 模式新增测试：保存脚本后 pending 计数增加，但直到显式调用 `FlushPendingHotReload()` 才发生重载。<br>3. 在 `Never` 模式验证 watcher 未注册、`Tick()` 不调用 `CheckForHotReload()`，且仍可通过显式全量编译路径完成初始化。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-33 | 热重载输入缓冲缺少线程拥有者与同步契约 | 输入层收口 + 线程边界显式化 | 高 |
| P2 | Arch-HR-34 | 热重载策略被隐式写死为 auto，缺少 `Auto/Manual/Never` 策略面 | 策略层新增 + 显式手动入口 | 中高 |

---

## 架构分析 (2026-04-09 00:41)

### Arch-HR-35：Editor 修补批次仍是“静态累积 + 即时改写”，没有 `prepare/commit/abort` 事务边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | full reload 的 Editor 修补是否具备显式事务边界与失败回退能力 |
| 当前设计 | 当前 `ClassReloadHelper` 不是围绕一个显式 batch object 工作，而是把本轮 replacement 信息累积到 `ReloadState()` 这个进程级 static 结构里；随后 `PerformReinstance()` 会在真正的 `ReparentHierarchies()` 之前，直接改写 loaded Blueprint 的 pin/variable 类型和 `UDataTable::RowStruct`，最后仅在 `OnPostReload` 里把 static state 整体清空。也就是说，修补动作已经开始落地时，系统里还没有一个可显式 `commit` 或 `abort` 的 repair transaction。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:44-47`：`ReloadState()` 返回 function-local static `FReloadState`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:52-175`：`OnStructReload`、`OnClassReload`、`OnDelegateReload`、`OnLiteralAssetReload`、`OnEnum*` 都是先向同一个 static state 记账，`OnFullReload` 再统一调用 `PerformReinstance()`，`OnPostReload` 最后直接 `ReloadState() = FReloadState()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:49-53,55-81`：legacy 路径先构造 `ClassReplaceList` 并直接改写 `FEdGraphPinType`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:108-145`：遍历 loaded Blueprint 时直接更新节点 pin、`UserDefinedPins`、`ResolvedWildcardType` 与 `NewVariables`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:148-163`：在 backend 真正 reinstance 之前就直接把依赖 `UScriptStruct` 的 `UDataTable::RowStruct` 改成新 struct。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:181-188,292-298`：这些预修改之后才进入 `FBlueprintCompilationManager::ReparentHierarchies()`、`QueueForCompilation()` 与 `FlushCompilationQueueAndReinstance()`。 |
| 优点 | 所有 full reload 修补都能复用一份聚合状态，接线成本低；对“本轮一定会走到底”的理想路径来说，实现比较直接。 |
| 不足 | 当前 repair batch 是隐式 global state，不是显式事务。推断上，只要 `ReparentHierarchies()`、Blueprint recompile、后续 editor refresh 或未来 native reload bridge 任一阶段失败，Blueprint/DataTable 的前置改写就已经发生，但系统没有 `abort` 或 snapshot restore 语义。这样既不利于失败隔离，也不利于未来把 Angelscript reload 与 native reload 共用同一条修补管线。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载先进入 sandbox `enter()` 建立一次显式 reload 区间；对每个 module 先 `sandbox.load()` / `xpcall(func, ...)`，只有全部成功后才进入 `update_modules()`，失败则 `sandbox.exit()` 并直接返回，不触碰旧模块图。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:179-190`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:565-601` | 先建显式 reload scope，再在成功点之后提交对象图更新；失败路径必须能“退出但不提交”。 |
| puerts | JS 热更新把 patch window 明确暴露成 `HMR.prepare` / `Debugger.setScriptSource` / `HMR.finish` 三段；只有拿到 `scriptId` 且真正执行 `setScriptSource` 时，才进入模块级 patch 区间。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 即便不做完整 rollback，也要把“开始修补”和“修补完成”做成显式阶段，方便宿主扩展在正确的窗口接入。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前隐式 `ReloadState` 升级成显式 `FHotReloadRepairBatch`，让 Editor 修补具备 `Begin/Commit/Abort` 三段式边界。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h/.cpp` 新增 `FHotReloadRepairBatch`，至少记录 `BatchId`、`ReloadClasses/Structs/Delegates/Assets/Enums`、`PendingBlueprintPinPatches`、`PendingDataTableStructPatches`、`bBackendSucceeded`。<br>2. 把当前 `OnStructReload` / `OnClassReload` / `OnDelegateReload` / `OnLiteralAssetReload` 的静态累积改成“写入当前 batch”，但不要在收集阶段直接改 Blueprint pin 或 `RowStruct`；先只生成 patch plan 和 old-value snapshot。<br>3. 在 `PerformReinstance()` 内部先执行 `BeginBatch()`，然后调用 backend；只有 backend 成功后才 `CommitBatch()` 应用 pin/type/DataTable 变更并刷新 UI。若 backend 失败或中途抛错，则 `AbortBatch()` 丢弃 patch plan，并恢复已捕获的旧指针/旧类型。<br>4. 把 `OnPostReload` 改成消费 `BatchId + bBackendSucceeded`，不要再无条件把全局 state 直接清空；未来 native reload bridge 或 `UseUnrealReload` backend 也统一走同一个 batch 协议。<br>5. 增加失败场景测试：模拟 `ReparentHierarchies()` 后续编译失败、struct reload 命中 DataTable 的失败路径，验证 abort 后 Blueprint pin 类型和 `UDataTable::RowStruct` 仍保持旧值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/HotReload/AngelscriptReloadRepairBatch.h`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/HotReload/AngelscriptReloadRepairBatch.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptReloadRepairBatchTests.cpp` |
| 预估工作量 | M |
| 架构风险 | rollback 如果覆盖范围过大，容易把本来已经正确提交的 editor side refresh 一起回滚掉；首版应只把 `Blueprint pin type`、`NewVariables`、`Macro wildcard`、`DataTable.RowStruct` 这类明确可逆的 patch 纳入事务。 |
| 兼容性 | 向后兼容。首版只重构内部批次模型，不改变默认 reload 行为；脚本 API、Editor 菜单和现有 full reload 判定都可保持不变。 |
| 验证方式 | 1. 构造 struct/class full reload 后的 Blueprint/DataTable 场景，确认成功路径下行为与当前基线一致。<br>2. 人为注入 backend 失败，确认 `AbortBatch()` 后 Blueprint pin 类型、变量类型和 `RowStruct` 没有被半提交。<br>3. 在 `angelscript.UseUnrealReload=0/1` 两种 backend 下运行同一组用例，确认都通过同一 batch 协议完成收尾。 |

### Arch-HR-36：`ClassReloadHelper` 作为 hot reload observer 没有 owner/teardown 契约，扩展点不可替换

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | hot reload observer 的生命周期管理与可替换性 |
| 当前设计 | Editor 模块启动时每次都会调用 `FClassReloadHelper::Init()`，而 `Init()` 通过 `AddLambda` 直接订阅 `FAngelscriptClassGenerator` 的一组 static reload delegate，但没有保存 `FDelegateHandle`，也没有对应的 `Shutdown()`；与此同时，`ReloadState()` 与 `ReplaceHelper` 都是进程级 static/global。结果是 `ClassReloadHelper` 更像“全局永久 listener”而不是模块拥有的可替换组件。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-353`：`StartupModule()` 每次启动都会直接调用 `FClassReloadHelper::Init()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:50-175`：`Init()` 对 `OnStructReload`、`OnClassReload`、`OnDelegateReload`、`OnLiteralAssetReload`、`OnEnum*`、`OnFullReload`、`OnPostReload` 全部使用 `AddLambda(...)`，源码中没有任何 `FDelegateHandle` 成员。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:44-47`：`ReloadState()` 是 static 单例。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:25`：`ReplaceHelper` 是文件级 static 全局对象。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:31-38` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:70-77`：这些 reload 事件本身就是 runtime 侧 static multicast delegate。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:676-689`：`ShutdownModule()` 只移除了 `OnObjectPreSave` 和菜单相关状态，没有解除 `ClassReloadHelper` 的任何 reload 订阅，也没有清空 `ReloadState()` / `ReplaceHelper`。 |
| 优点 | 启动接线简单，默认工程里只要模块启动成功，就能自动接上 full reload 的 editor 修补链。 |
| 不足 | observer 生命周期没有 owner。推断上，这会带来三个结构性限制：一是 editor 模块若在同进程内重载，可能重复安装 listener；二是未来想把 helper 拆成 `LegacyRepairHelper` / `NativeReloadBridgeHelper` / `TestSpyHelper` 时，没有可替换插槽；三是全局 static state 很难和 engine clone、测试隔离或按 session 诊断并存。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 模块以 `AddRaw(this, ...)` 绑定 owner，并在 `ShutdownModule()` 中 `RemoveAll(this)` 统一解绑 engine/package 相关事件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54-68`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:72-84` | observer 必须跟模块 owner 同生同灭，shutdown 时显式拆线。 |
| puerts | Editor 模块把 watcher 持有为 `TSharedPtr<FSourceFileWatcher>`，`ShutdownModule()` 里 `Reset()`，而 watcher 析构函数再统一 `UnregisterDirectoryChangedCallback_Handle(...)`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-163`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:17-38`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:92-102` | 把 listener 封装成 owner-owned object，并让 teardown 成为对象析构的一部分，而不是依赖“进程只会初始化一次”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `ClassReloadHelper` 从 static helper 重构为 module-owned `ReloadCoordinator`，显式持有 delegate handles 和 batch state。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h/.cpp` 引入真正的实例类型，例如 `FAngelscriptReloadCoordinator`，把现有 `FReloadState`、`ReplaceHelper`、`Init()` 逻辑搬成成员。<br>2. 为每个 runtime reload delegate 保存 `FDelegateHandle`，在 `Initialize()` 中注册，在 `Shutdown()` 中逐个 `Remove(...)`；不要继续使用无法拆线的裸 `AddLambda`。<br>3. 让 `FAngelscriptEditorModule` 持有一个 `TUniquePtr<FAngelscriptReloadCoordinator>`，`StartupModule()` 创建并初始化，`ShutdownModule()` 先调用 `Shutdown()` 再销毁；这样未来 native reload bridge、test spy 或 alternate backend 可以替换同一协调器接口。<br>4. 把 `ReloadState()` 改成协调器成员批次状态，`ReplaceHelper` 改成成员 `TObjectPtr` 或 `TStrongObjectPtr`；避免跨模块生命周期残留全局对象。<br>5. 增加生命周期测试：模拟 `Startup -> Shutdown -> Startup` 两次，验证同一次 full reload 只触发一轮 `PerformReinstance()`；同时加一个测试 observer，确认 teardown 后不会再收到旧 helper 的广播。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/HotReload/AngelscriptReloadCoordinator.h`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/HotReload/AngelscriptReloadCoordinator.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptReloadCoordinatorLifecycleTests.cpp` |
| 预估工作量 | M |
| 架构风险 | `ReloadCoordinator` 若和现有 static helper 并存一段时间，最容易出现双注册；迁移阶段应先把旧 `Init()` 变成薄转发并加一次性 guard，确认模块启动路径唯一后再删旧入口。 |
| 兼容性 | 向后兼容。改动集中在 editor 内部协调层，不改变脚本 API、reload 事件名或现有用户工作流；只是把 observer 生命周期做成可管理对象。 |
| 验证方式 | 1. 在同进程内执行 `StartupModule()/ShutdownModule()/StartupModule()` 模拟，确认 `OnFullReload` 只触发一次 `PerformReinstance()`。<br>2. 注册测试 observer 后销毁 coordinator，确认后续 reload 不会再命中旧 observer。<br>3. 对现有 full reload 回归做一次基线比对，确认拆线/重绑后 editor refresh 行为不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-35 | Editor 修补批次缺少显式事务边界与失败回退 | 事务批次新增 + commit/abort 协议 | 高 |
| P2 | Arch-HR-36 | hot reload observer 没有 owner/teardown 契约 | 协调器对象化 + 生命周期治理 | 中高 |

---

## 架构分析 (2026-04-09 00:49)

### Arch-HR-37：script module 的 global state 目前只做“引用改址”，没有“值迁移”契约

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载时 module-scope global variable 的状态保持 |
| 当前设计 | 当前热重载会先为新模块分配 global variable 存储，再把旧 bytecode 里引用的 global 地址改到新地址；但进入 `Globals_Stage4` 后，新模块仍会统一 `ResetGlobalVars(0)`，把非 pure-constant globals 清零并重新执行 init。换句话说，系统保证的是“引用指向新 global slot”，不是“旧 global value 迁移到新 slot”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3724-3758`：先 `BuildAllocateGlobalVariables()`，再把 `OldProperty->GetAddressOfValue()` 映射到 `NewProperty->GetAddressOfValue()`，最后只更新旧模块 bytecode 引用。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp:2474-2500`：`DiffForReferenceUpdate()` 只把 name/type 兼容的 global property 加入 `OutUpdateMap.GlobalProperties`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp:2742-2770`：`UpdateReferencesInScriptBytecode()` 对 global 的处理只是 `GlobalVariablePointers` 地址替换。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4403-4410`：`CompileModule_Globals_Stage4()` 无条件执行 `ScriptModule->ResetGlobalVars(0)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp:342-347,366-418`：`ResetGlobalVars()` 先 `CallExit()`，再在 `CallInit()` 中把每个非 pure-constant global 清零并重跑默认构造/初始化函数。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2619-2639` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4044-4055`：当前唯一明确被主动回填的 script global 是 `StaticClass` 变量，说明 generic globals 并没有独立迁移层。 |
| 优点 | epoch 边界清晰，避免把旧布局或旧初始化逻辑下的 global 值盲拷到新模块；对于 body 变更之外夹带 layout/default/init 变化的场景更保守。 |
| 不足 | 所有不镜像到 `UObject` 属性内存的 script state 都可能在 reload 后丢失，例如 module-scope cache、脚本单例、注册表、计数器、服务定位器和驻留句柄表。当前系统对这类状态没有“可迁移”“不可迁移”“需要用户接管”的显式分类。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载不是简单重建 module，而是先 `update_modules()` 匹配新旧模块值映射，再用 `update_global()` 递归修补 running stack、`_G`、registry、userdata uservalue 和 function upvalue。`M.reload(module_names)` 还允许只对指定模块集做这套状态更新。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:367-477`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-529`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-623` | 把“module-local/global state 是否延续”做成显式迁移步骤，而不是默认依赖重新初始化。 |
| puerts | puerts 不直接替换整个模块缓存；`IPuertsModule::ReloadModule()` 提供 module-scoped reload 入口，JS 侧 `hot_reload.js` 在 `Debugger.setScriptSource` 前后对同一个已缓存模块对象 `m` 触发 `HMR.prepare/HMR.finish`，并可通过 `getModuleByUrl()` / `forceReload()` 精确定位模块。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37-42`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:75-94`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-247` | 即使不自动做通用值拷贝，也要把“旧模块对象”和“迁移窗口”暴露出来，给宿主或用户代码接管状态延续。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `GlobalVariablePointers` 之前增加一个 whitelist-based `ScriptGlobalStateMigration` 层：先显式识别可迁移 globals，再对其做值迁移；其余 globals 继续保持当前 reset 语义并输出诊断。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 的 `ScriptUpdateMap.GlobalProperties` 构建之后，新增 `BuildGlobalStateMigrationPlan()`，按“同名 + 同 namespace + type compatible”收集候选 global。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h/.cpp` 为 `asModuleReferenceUpdateMap` 新增迁移元数据，至少记录 global 名称、type id、是否允许 value copy。<br>3. 首版只支持保守白名单：primitive、enum、`FString/FName` 这类稳定值类型，以及 `UObject* / TSubclassOf / TSoftObjectPtr` 这类可通过现有引用更新机制修补的句柄；对自定义 script object、容器嵌套 script object、需要执行自定义 `CallExit/CallInit` 协议的 globals 继续回退到 reset。<br>4. 在 `CompileModule_Globals_Stage4()` 里把顺序改成“先 reset 新 global slot，再对计划内 globals 执行 post-init value restore”，并新增 warning：列出本轮因为类型不兼容或未在白名单中而被重置的 globals。<br>5. 为无法通用迁移的高层状态新增 opt-in 扩展口，例如 `IAngelscriptGlobalStateAdapter` 或 runtime delegate，让插件外部能按模块名接管特定 singleton/cache 的 restore。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadGlobalStateTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 对 script object 或复杂容器做错误的值恢复，风险高于“全部重置”。因此首版必须坚持白名单策略，并把回退 reset 视为默认安全路径。 |
| 兼容性 | 向后兼容。默认行为可保持“无迁移即 reset”；只有命中白名单或显式 adapter 的 globals 才新增状态保持能力，不破坏现有脚本语义。 |
| 验证方式 | 1. 新增 hot reload 测试：对 body-only 变更验证 module-level `int`、`FString`、`UObject*` global 在 reload 后保值。<br>2. 对 type changed / init logic changed 场景验证系统会回退 reset 并输出 warning，而不是错误迁移。<br>3. 增加一个 script singleton/cache 场景，验证 adapter 接管后能保留状态，adapter 未注册时则维持当前重置行为。 |

### Arch-HR-38：hot reload 对外发布的是“流式类型事件”，不是“session 级 delta 协议”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载对外扩展点是否提供完整会话语义 |
| 当前设计 | 当前 runtime 公开的 reload 通知主要是 `OnClassReload / OnStructReload / OnDelegateReload / OnEnum* / OnLiteralAssetReload` 这类流式 old/new 对，以及一个无载荷的 `OnFullReload` 和一个只有 `bool bIsDoingFullReload` 的 `OnPostReload`。结果是，任何需要知道“本轮改了哪些文件/模块、整体结果是什么、哪些变更被 deferred”的扩展，都必须自己在多条 delegate 之间重新拼装上下文。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:8-20,31-38`：公开 delegate 载荷只有 old/new type pair、enum old names、`OnFullReload()` 和 `OnPostReload(bool)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2366-2373`：full reload 时按类型流式广播 `OnClassReload(...)`，随后只发一次无参数 `OnFullReload.Broadcast()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2392-2395,2466-2469`：post 阶段唯一统一信号只有 `OnPostReload.Broadcast(bIsDoingFullReload)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:27-47,50-175`：Editor 侧不得不先把多条流式事件累积到全局 `FReloadState`，`OnFullReload` 时再 `PerformReinstance()`，`OnPostReload` 再整体清空，说明 session 语义并不是由 runtime 一等提供，而是由 consumer 自行重建。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:187-188,425` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2253-2285`：引擎内部热重载入口仍围绕 `ECompileType + FileList` 运转，但这些输入和 compile outcome 并没有被封装成对外可消费的 session summary。 |
| 优点 | 事件颗粒度细，现有 Editor helper 能以最小耦合接到 class/struct/enum 的逐项变化；旧代码接线成本低。 |
| 不足 | 扩展方拿不到本轮 changed files/modules、compile result、是否 `ErrorNeedFullReload`、是否走了 legacy/unreal backend、哪些类型只是 suggested/deferred full reload 等关键信息。结果是 observer 只能“猜本轮发生了什么”，而不是消费一份受控的 reload session。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `M.reload(module_names)` 直接把“本轮目标模块集”做成参数；模块加载时还通过 `call_hook("module_loaded", new_module, module_name, ...)` 把 module name 和 module object 广播出去。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-623` | session 至少要明确“本轮 reload 目标是谁”，而不是只在事后零散广播类型变化。 |
| puerts | C++ 侧 `IPuertsModule::ReloadModule(FName ModuleName, const FString& JsSource)` 提供 module-scoped 显式入口；JS 侧 `hot_reload.js` 在 patch 前后发 `HMR.prepare/HMR.finish`，并把 `moduleName`、旧模块对象 `m` 和 `url` 一起交给监听方。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37-42`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:75-94`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 把 reload window 做成显式 session/hook，并携带足够的定位信息，扩展方才能可靠接入 prepare/commit 逻辑。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有流式 delegate 之上增加一层 `FAngelscriptHotReloadSession` 协议，把“本轮输入、决策、执行结果、受影响对象”变成一份一等数据结构；旧 delegate 保留为兼容包装。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` 新增 `FAngelscriptHotReloadDelta` 与 `FAngelscriptHotReloadSession`，至少包含：`SessionId`、`CompileType`、`Result`、`ChangedFiles`、`AffectedModules`、`AffectedClasses/Structs/Delegates/Enums/Assets`、`bDidFullReload`、`bHadDeferredFullReload`、`BackendKind`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 的 `PerformHotReload()` 开始处构造 session，贯穿 compile、dependency expansion、full/soft 决策、post phase；结束时统一发布 `OnHotReloadSessionFinished(const FAngelscriptHotReloadSession&)`，失败路径发布 `OnHotReloadSessionAborted(...)`。<br>3. 把 `OnClassReload / OnStructReload / OnDelegateReload / OnEnum* / OnLiteralAssetReload` 改成 session 内部的 delta 填充来源；旧 delegate 继续保留，但从 session 里派生广播，避免双份事实源。<br>4. 让 `ClassReloadHelper` 先消费 session，再在需要时向下兼容旧路径；这样 future observer、diagnostics、state dump、native reload bridge 都不必再各自维护一份 `FReloadState` 拼图。<br>5. 第二阶段再给 Editor/automation 增加基于 session 的可视化与测试断言，例如显示“本轮 changed files、哪些类 deferred full reload、最终 compile result”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadSessionTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果 session 与旧 delegate 同时独立维护，最容易出现信息漂移。迁移时必须让旧 delegate 明确从 session 派生，保证单一事实源。 |
| 兼容性 | 向后兼容。旧 delegate 可全部保留；新 session 只是新增更完整的扩展协议，现有 consumer 可以渐进迁移。 |
| 验证方式 | 1. 新增 observer 测试，断言一次 full reload / soft reload / abort 各自产生一份完整 session。<br>2. 校验 session 中的受影响类型集合，与旧 `OnClassReload` / `OnStructReload` 等逐项广播结果一致。<br>3. 用一个模拟扩展 consumer 替换 `FReloadState` 拼接逻辑，验证仅依赖 session 也能完成 editor refresh 决策。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-37 | script module global state 没有值迁移契约 | 状态迁移层新增 + adapter 扩展口 | 高 |
| P1 | Arch-HR-38 | hot reload 对外缺少 session 级 delta 协议 | 扩展协议补强 + 单一事实源 | 高 |

---

## 架构分析 (2026-04-09 01:02)

### Arch-HR-39：现有 `AdditionalCompileChecks` 是 editor-only 的隐式扩展缝，不是可复用的 hot reload 扩展协议

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载扩展点是否真正对 runtime/editor 宿主开放 |
| 当前设计 | 当前唯一显式的“可由外部接入热重载流程”的 runtime 侧接口，其实是 `FAngelscriptEngine::AdditionalCompileChecks` 这张裸 `TMap<UClass*, TSharedPtr<FAngelscriptAdditionalCompileChecks>>`。它按 `CodeSuperClass` 祖先链查找扩展对象，但实际执行点被包在 `#if WITH_EDITOR` 中，非 editor / runtime `SoftReloadOnly` 路径拿不到这条扩展缝。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:357-359`：源码直接注明这张表“可由 game module 在 editor 中填充”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h:4-8`：扩展接口只有 `ScriptCompileAdditionalChecks(...)` 与 `PostReloadAdditionalChecks(...)` 两个极薄虚函数。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1370-1387`：编译期附加检查在 `#if WITH_EDITOR` 下按 `CodeSuperClass` 祖先链遍历。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2472-2489`：post-reload 附加检查同样只在 `#if WITH_EDITOR` 下执行。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2819-2828`：运行时 / game world 依然会实际执行 `CheckForHotReload(ECompileType::SoftReloadOnly)`，但这条扩展缝在该路径上并不参与。 |
| 优点 | 接入成本低，宿主若愿意直接操作 `FAngelscriptEngine`，可以针对某类 `CodeSuperClass` 附加校验逻辑，而不用修改 `ClassGenerator` 主流程。 |
| 不足 | 这不是一个产品化扩展协议，而是 editor-only 的内部缝隙。它没有公开注册/注销 API、没有 session/file/module 上下文、没有 runtime parity，扩展粒度也被强绑到 `CodeSuperClass` 继承树，而不是“这次 reload 的 module / file / delta kind”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime 直接暴露 `FLuaEnv::HotReload()`，Lua 侧 `M.reload(module_names)` 以 module 集合为 reload 边界，并通过 `hook.module_loaded` 暴露模块级回调。扩展点跟着 module/env 走，不依赖 editor-only 编译钩子。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:20-26`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-623` | 扩展协议应当首先是 runtime 可用、module-scoped、带目标集合的 API，而不是某个 editor 内部 `TMap`。 |
| puerts | `IPuertsModule` 公开 `ReloadModule(FName ModuleName, const FString& JsSource)`，JS 侧再围绕同一个 module/object 发 `HMR.prepare` / `HMR.finish`。C++ 入口和脚本生命周期都是公开契约，而不是隐式 internal seam。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37-42`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:80-94`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 让 reload API 成为模块公共接口，再把 prepare/finish/hook 建在这条接口之上，扩展方才不必直接碰 engine internals。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AdditionalCompileChecks` 升级为 runtime-core 的 `IAngelscriptHotReloadParticipant` 注册中心；editor 只在此之上追加 asset repair。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增公开注册 API，例如 `RegisterHotReloadParticipant(FName ParticipantId, TSharedRef<IAngelscriptHotReloadParticipant>)` / `UnregisterHotReloadParticipant(...)`，不要再要求宿主直接改写裸 `TMap`。<br>2. 新接口至少携带 `FAngelscriptHotReloadContext`：包含 `CompileType`、`ChangedFiles`、`AffectedModules`、`bIsFullReload`、`SessionId`；这样扩展逻辑不必再把 `CodeSuperClass` 当唯一定位键。<br>3. 第一阶段保留 `AdditionalCompileChecks` 作为兼容 adapter：在运行前把旧 map 适配成 participant，保持现有 game module 行为不变。<br>4. 将 participant 调度从 `#if WITH_EDITOR` 中抽出，runtime 也能收到 `PreAnalyze/PreCommit/PostCommit` 之类的阶段回调；editor 特有的 Blueprint/DataTable/open asset 修补继续留在 `AngelscriptEditor`。<br>5. 增加一个最小示例 participant 测试：验证 runtime 下 `SoftReloadOnly` 也能收到 context，editor 下则在同一 session 中额外收到 asset repair 结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptHotReloadParticipant.h`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadParticipantTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 若首版同时移除旧 map 和旧调用点，容易把现有外部宿主的私有接入方式直接打断；因此第一阶段必须保留 adapter，先新增协议，再逐步弃用旧缝。 |
| 兼容性 | 向后兼容。旧 `AdditionalCompileChecks` 可继续工作，只是被包装进新 participant registry；现有脚本 API 和 reload 语义不需要破坏性调整。 |
| 验证方式 | 1. 新增 runtime 用例，验证非 editor `SoftReloadOnly` 时 participant 仍能收到 reload context。<br>2. 新增 editor 用例，验证旧 `AdditionalCompileChecks` 通过 adapter 仍能触发。<br>3. 对比没有 participant 与注册 participant 的同一批 reload，确认 core compile/reload 结果不变，只新增可观察扩展回调。 |

### Arch-HR-40：`PostReloadAdditionalChecks` 发生在 `OnPostReload` 之后，扩展层拿到的是“已宣告完成”的 reload

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载扩展回调的阶段顺序是否支持 veto / repair / abort |
| 当前设计 | 当前 `OnPostReload` 已经被当作“本轮 reload 收尾完成”信号使用，但 `PostReloadAdditionalChecks(...)` 反而在它之后才执行，而且返回值是 `void`。这意味着扩展层即便发现新问题，也既不能阻止 `OnPostReload` 对外宣告成功，也不能把修补动作并回同一批次。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2392-2395` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2468-2469`：无论 full 还是 soft path，都会先广播 `OnPostReload.Broadcast(bIsDoingFullReload)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2472-2489`：`OnPostReload` 之后才遍历 `AdditionalCompileChecks` 并调用 `PostReloadAdditionalChecks(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h:7-8`：post-reload hook 没有返回状态，无法表达 `NeedsRepair` 或 `Abort`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:132-175`：Editor 侧在 `OnFullReload` 时 `PerformReinstance()`，在 `OnPostReload` 时刷新 Blueprint action / geometry / component registry，并立即 `ReloadState() = FReloadState()` 清空本轮状态。 |
| 优点 | 现有顺序简单，core reload 不需要等待扩展层返回复杂结果；旧代码假设 `OnPostReload` 就是终点，接线成本低。 |
| 不足 | 扩展层被放到“终点之后”。这会让后置检查只能写日志或做额外副作用，无法成为受控的 session phase；一旦未来接 native reload bridge、runtime participant、结构化 repair batch，它们都会遇到“系统已经先宣布 finished，再让我补救”的顺序问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `reload_modules()` 会在 sandbox 中先加载新模块，再执行 `update_modules()` / `update_global()` 完成对象图更新，最后才退出这次 reload 流程。也就是说，状态修补发生在“reload 返回之前”，不是返回之后再补一个隐藏阶段。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:565-601` | 热重载的 finish 边界应该晚于所有状态修补，而不是早于扩展检查。 |
| puerts | JS 热更新显式分成 `HMR.prepare -> Debugger.setScriptSource -> HMR.finish` 三段，`HMR.finish` 是对外的完成钩子，前面没有另一个“已完成”事件先行发布。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | `finish` 必须是最后一个可见阶段；prepare/repair 要在 finish 之前完成，才能让外部把 finish 视为稳定收尾点。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前 `OnPostReload` 之前/之后的隐式顺序重构成显式 `Prepare -> Commit -> Finalize -> Finished/Aborted` 相位；所有扩展检查都在 `Finished` 之前结束。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/` 新增 `EHotReloadParticipantResult { Continue, NeedsRepair, Abort }` 与 `FAngelscriptHotReloadPhaseContext`。<br>2. 将现有 `PostReloadAdditionalChecks(...)` 改造成 `FinalizeReload(...) -> EHotReloadParticipantResult`，并在 `OnPostReload` 广播之前执行；若 participant 返回 `NeedsRepair`，则允许 editor/runtime coordinator 继续留住当前 batch 状态并执行 repair 子阶段。<br>3. 把现在的 `OnPostReload` 语义收窄为真正的 `OnHotReloadFinished`，只在 participant finalize 全部完成且无 abort 后才广播；若有 abort，则改发 `OnHotReloadAborted` 或在 session 里标记 `bFinished=false`。<br>4. `FClassReloadHelper` 不再在旧 `OnPostReload` 一到就立即清空 `ReloadState`，而是等到 `Finished/Aborted` 决议后再 `CommitBatch()` 或 `DiscardBatch()`；这样扩展层还能在同一批次里请求额外 repair。<br>5. 第一阶段保留旧 `OnPostReload(bool)` 作为兼容事件，但明确从新 `Finished` 事件派生，旧 `PostReloadAdditionalChecks` adapter 默认返回 `Continue`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptHotReloadPhases.h`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPhaseTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果新旧 `OnPostReload`/`Finished` 两套事件并存且都被外部消费，最容易出现双触发或顺序漂移；迁移期必须让旧事件严格从新事件派生，不能各自独立广播。 |
| 兼容性 | 向后兼容。首版可以保留旧 `OnPostReload(bool)` 签名，只调整其触发时机为“所有 finalize 完成之后”；旧 `PostReloadAdditionalChecks` 通过 adapter 自动转成 `FinalizeReload()->Continue`。 |
| 验证方式 | 1. 新增测试 participant，在 finalize 阶段返回 `NeedsRepair`，验证 `OnPostReload/Finished` 会等 repair 完成后再触发。<br>2. 新增 abort 用例，验证 `ClassReloadHelper` 不会提前清空 batch，也不会把这轮 reload 误报为 finished。<br>3. 回归现有 full/soft reload 流程，确认没有 participant 时对外观察到的行为与当前基线一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-40 | 扩展检查发生在 `OnPostReload` 之后，finish 边界不可靠 | 阶段协议重构 + finalize/abort 语义 | 高 |
| P1 | Arch-HR-39 | `AdditionalCompileChecks` 仍是 editor-only 隐式扩展缝 | runtime-core participant registry 新增 | 高 |

---

## 架构分析 (2026-04-09 01:12)

### Arch-HR-41：`SoftReload` 会即时改写 `ClassFlags`，但大部分类 metadata 仍停留在旧版本，形成混合纪元的 `UClass` 身份

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UClass` 反射身份在 soft path 中是否以一致的 contract 提交 |
| 当前设计 | 当前分析阶段把 class metadata 和 class flags 变化都归入 `FullReloadSuggested`；但真正进入 `DoSoftReload()` 后，代码会立刻把 `CLASS_NotPlaceable`、`CLASS_Abstract`、`CLASS_Transient`、`CLASS_HideDropDown`、`CLASS_DefaultToInstanced`、`CLASS_EditInlineNew`、`CLASS_Deprecated` 等 flags 写到现有 `UASClass` 上，而 class-level metadata 仍只有 full/new class 路径才统一 `SetMetaData()`。最终形成的是一个混合状态：同一个 `UASClass` 可能已经带着新 epoch 的 flags，却仍保留旧 epoch 的 `DisplayName`、`Blueprintable/NotBlueprintable`、`IsBlueprintBase`、`HideCategories` 等 metadata。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1252-1259,1311-1323`：函数 metadata、类 metadata、类 flags 变化在分析阶段都只是 `FullReloadSuggested`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2819-2825` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2081-2093`：PIE / runtime 走 `SoftReloadOnly`，`FullReloadSuggested` 不会自动升级成 full reload。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4208-4242`：`DoSoftReload()` 会即时改写上述 `ClassFlags`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3314-3385`：`CopyClassInheritedMetaData()`、`DisplayName`、`Blueprintable/NotBlueprintable`、`IsBlueprintBase`、`HideCategories` 等 class metadata 只在 new/full class 物化阶段写入。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4261-4269`：soft path 对 metadata 的特殊处理几乎只覆盖 `FUNCMETA_ScriptNoOp` 这一小块，没有对 class metadata 做对等刷新。 |
| 优点 | 在运行态能快速拿到部分新的 class 行为标志，例如 `Abstract` 或 `DefaultToInstanced`，不必等待一次更重的 reinstance。 |
| 不足 | 这不是单纯的 editor cache 过期，而是反射 contract 自身被拆成两半提交。推断上，运行时代码、部分 UE 反射查询和 editor 系统可能分别读到“新 flags + 旧 metadata”的组合，从而出现难以解释的行为，例如类已经不可放置/抽象，但仍保留旧的 `DisplayName`、蓝图暴露描述或 category 组织方式。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `M.reload()` 只围绕 `modified_modules` 更新 module table、upvalue 和 `_G`；它没有把“Lua 运行时对象更新”与“UE class metadata 物化”混成同一条半提交路径。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-624` | 当系统无法同时提交完整反射 contract 时，应缩小 HMR 面，而不是让同一个类型在一次 reload 后呈现混合身份。 |
| puerts | `hot_reload.js` 在 `HMR.prepare` / `Debugger.setScriptSource` / `HMR.finish` 中只 patch module/source；`ReloadSource()` 也只把 `Path + JsSource` 交给这条 source-level 管线。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538` | 若无法原子更新 UE 反射对象，就把热更新边界停留在 source/module，而不是制造“flags 已新、metadata 仍旧”的中间态。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 class flags 与 class metadata 当作同一组 `ReflectionContract` 提交；soft path 只能接受显式白名单，不能继续做“flags 先行、metadata 滞后”的拆分提交。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 新增 `EClassReflectionContractDelta`，至少区分 `FlagsOnlySafe`、`MetaOnlyCosmetic`、`FlagsAndMetaCoupled`。<br>2. 在分析阶段把 `ClassFlags` 与相关 metadata 成对分类：例如 `Abstract/NotPlaceable/DefaultToInstanced/EditInlineNew/Deprecated` 与 `Blueprintable/IsBlueprintBase/HideCategories` 这类影响同一 class 身份的变化，直接标成 `FlagsAndMetaCoupled`，不要继续只给 `FullReloadSuggested`。<br>3. 第一阶段在 `SoftReloadOnly` 下对 `FlagsAndMetaCoupled` 只输出 `DeferredClassIdentityReload` 诊断，不即时改写 `ClassFlags`；避免继续制造混合身份。<br>4. 第二阶段为真正安全的 cosmetic metadata 新增 `RefreshClassMetadataSoft()`，仅对白名单 key（如 `DisplayName`、`ToolTip`）开放同步更新；其余 contract 仍要求 full reload。<br>5. 增加自动化测试：覆盖 `Abstract + Blueprintable`、`NotPlaceable + DisplayName`、`DefaultToInstanced + HideCategories` 三类组合，验证 `SoftReloadOnly` 不再产生“新 flags + 旧 metadata”的混合状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassIdentityTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果白名单划分不当，可能把本可即时生效的纯展示型 metadata 也一并锁进 full reload；因此首版应先从最容易造成 contract 漂移的 flag/meta 组合入手。 |
| 兼容性 | 向后兼容。第一阶段只是阻止高风险的“半提交”场景继续即时改 flags，并增加结构化诊断；第二阶段的 soft metadata refresh 以白名单增量开放。 |
| 验证方式 | 1. 在 `SoftReloadOnly` 下修改 `Abstract` 与 `Blueprintable` 组合，确认系统给出 `DeferredClassIdentityReload`，且不会只改 flags。<br>2. 在 editor full reload 下执行同样改动，确认 `ClassFlags` 与 class metadata 一次性同步到新版本。<br>3. 对纯 body-only 或仅 `DisplayName` 这类白名单 metadata 变更做回归，确认不会误触发高风险分流。 |

### Arch-HR-42：`CDONoDefaults` 把 `Config` 与 script `default` 归入同一迁移来源，soft reload 可能顺带回灌配置漂移

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 默认值迁移对 script defaults 与外部 config/ini 变更的区分能力 |
| 当前设计 | `PrepareSoftReload()` 会构造 `CDONoDefaults`，并显式把 `Config` 属性也当作“与 script default 同类的默认值来源”清掉；后续 `DoSoftReload()` 通过比较 `BaseCDO` 与 `CDONoDefaults` 来设置 `bModifiedByDefaults`。这意味着 config-backed 属性一旦在真实 CDO 上有值，hot reload 就会把它视为“默认值发生过修改”，并在实例/`CDO` 迁移时优先复制。结果是：一次与配置无关的 `.as` 热重载，也可能把当前 `ini/config` 漂移一并当作默认值迁移提交到 live instance 或派生 `CDO`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4093-4108`：源码注释明确写着 `CDONoDefaults` 会“unload any Config properties in here, which should be treated the same as properties modified by 'default'”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:852-853` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3020-3021`：`Config` 是显式 property 语义，并会落成 `CPF_Config`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4489-4500`：`BaseCDO` 与 `CDONoDefaults` 不相等时，property 会被标成 `bModifiedByDefaults`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4580-4593`：live instance 迁移时，`bModifiedByDefaults` 会直接让旧值进入复制路径，即便实例本身没改过。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4679-4708`：`CDO`/派生 `CDO` 迁移同样把 `bModifiedByDefaults` 作为优先复制条件，把 base/parent `CDO` 值带进新对象。 |
| 优点 | 对单纯的 script `default` 语句变更很保守，能在 soft path 下尽量延续“默认值改动应传播到未覆写实例/派生 `CDO`”这一语义。 |
| 不足 | 现在的 diff 不区分“脚本代码导致的默认值变化”和“外部 config/ini 导致的默认值变化”。一旦项目里存在 `CPF_Config` 字段，任何后续 hot reload 都可能顺手重放 config 值；这让 HMR session 同时承担了代码 patch 与配置再应用两件事，定位问题时很难回答“这次实例值变化到底来自脚本编辑，还是来自 config source drift”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `M.reload()` 只从 `loaded_module_times` 里找发生时间戳变化的 Lua module，并把这些 module 送进 `reload_modules()`；它的触发边界始终是“哪些脚本模块改了”，而不是“当前默认对象/配置基线是否变化”。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-624`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601` | 代码热更新与外部配置重载应分成两种显式来源，避免在一次 HMR 中隐式重放其他配置通道。 |
| puerts | `ReloadSource()` 的输入只有 `Path` 和 `JsSource`；JS 侧 `hot_reload.js` 只对目标 `scriptId` 执行 `Debugger.setScriptSource`，prepare/finish 生命周期也围绕 source patch 本身展开。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | HMR 只消费显式 source delta；配置或外部状态是否重放，应该由独立策略控制，而不是在 code patch 内隐式发生。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `bModifiedByDefaults` 拆成“script default”与“config default”两类来源；soft reload 默认只自动传播前者，后者改为显式策略。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 的 `FPropertyCopy` 中，把当前单一的 `bModifiedByDefaults` 升级成 `EDefaultMutationSource` 或两位标记，至少区分 `ScriptDefaults`、`ConfigDefaults`、`ParentCDOOnly`。<br>2. 构建 `PropertiesToCopy` 时，对带 `CPF_Config`/`bConfig` 的 property 不再直接沿用“`BaseCDO != CDONoDefaults` 就等于 default changed”的规则；第一阶段先只记录 `ConfigDefaults` 命中并输出诊断。<br>3. 在 live instance / `CDO` 迁移阶段，为 `ConfigDefaults` 增加独立策略位，例如 `angelscript.ReapplyConfigOnSoftReload` 或 session-level `EExternalDefaultPolicy`；默认保持保守，不在普通 code hot reload 中自动重放。<br>4. 如果项目确实需要“保存脚本时顺带刷新 config-backed defaults”，把这条行为做成显式 `ConfigReload` request 或 participant，而不是继续绑在普通 `.as` HMR 上。<br>5. 增加测试与日志：覆盖 `CPF_Config` 属性在 body-only 修改、`default` 语句修改、显式 config reload 三种情形下的差异，并在 session summary 中列出“哪些字段因为 config policy 被重放/跳过”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果立即改变 `Config` 字段的 soft reload 行为，可能影响当前依赖“保存脚本时顺带刷新 config-backed defaults”的项目习惯；因此首版应先 diagnostics + policy switch，再考虑改默认值。 |
| 兼容性 | 向后兼容。第一阶段只新增来源标记、日志和可选策略开关；保留当前默认行为也可以，但至少把 config 重放显式化，便于项目按需关闭。 |
| 验证方式 | 1. 构造一个 `CPF_Config` 字段，仅做函数 body 修改，验证 session 能区分“代码变更”与“config default 命中”。<br>2. 在关闭 `ReapplyConfigOnSoftReload` 时，确认 body-only 热重载不会再意外改写 live instance 的 config-backed 值。<br>3. 在开启策略或显式 `ConfigReload` 时，确认 `CDO` 与未覆写实例仍能按预期重放 config 值。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-41 | `ClassFlags` 与 class metadata 在 soft path 下可能形成混合身份 | 反射 contract 分层 + half-commit 收紧 | 中高 |
| P1 | Arch-HR-42 | `CDONoDefaults` 混淆 script defaults 与 `Config` 来源 | 默认值来源分层 + 显式 config policy | 中高 |

---

## 架构分析 (2026-04-09 23:58)

### Arch-HR-43：enum 热重载把 `OldNames` delta 压扁成 `TSet<UEnum*>`，值名迁移语义在 Editor helper 中丢失

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | enum 变更的 delta 语义是否能完整传到 Blueprint / Editor 修补层 |
| 当前设计 | runtime 在 enum 发生变化时会原地改写现有 `UUserDefinedEnum`，并通过 `OnEnumChanged(Enum, OldNames)` 把旧值名列表广播出去；但 Editor 侧 `FClassReloadHelper` 只把这件事记成 `ReloadEnums.Add(Enum)`，随后仅靠 `ReloadEnums.Contains(...)` 和 `RefreshAssetActions()` 做粗粒度刷新。也就是说，enum rename / reorder / remove 的关键信息在进入 helper 后就被降维成“这个 `UEnum*` 变过”，`OldNames` 没再进入任何修补流程。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3836-3893`：existing enum 变化时，原地 `SetEnums(...)`，再 `OnEnumChanged.Broadcast(Enum, OldNames)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:117-123`：helper 在 `OnEnumChanged` 中只记录 `ReloadEnums`，并把 `FEnumEditorUtils::BroadcastChanges((UUserDefinedEnum*)Enum, OldNames)` 留成注释。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:55-80,208-226`：无论 pre-pass 还是 node refresh，enum 相关判断都只看 `ReloadEnums.Contains((UEnum*)PinType.PinSubCategoryObject.Get())`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:373-378`：post 阶段对 enum 的唯一统一收尾是 `FBlueprintActionDatabase::RefreshAssetActions(ChangedEnum)`。 |
| 优点 | 保持了 `UEnum` identity，不需要为 pin type 维护 old/new enum replacement map；大部分仅“值集合变化但类型壳不变”的场景可以复用现有对象。 |
| 不足 | 当前不是“没有刷新 enum”，而是“只知道 enum 变了，却不知道怎么变了”。推断上，任何依赖旧值名的 pin default、serialized enum entry、细粒度 editor listener 或后续离线资产修补，都拿不到 `OldNames`，因此无法做值名 remap，只能退化成粗刷新和人工重建。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载边界停留在 Lua module cache。`require()` 优先返回 `package.loaded` / `loaded_modules`，`M.reload()` 只把 `modified_modules` 送进 `reload_modules()`，在 Lua table/upvalue 层完成替换，不会去原地改写 UE 的 `UEnum` 反射对象。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:151-169`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-623` | 如果 HMR 必须跨到 UE 反射层，就不能再只保留“对象 identity 没变”这一级信息；需要把 enum delta 本身做成一等输入。 |
| puerts | `FSourceFileWatcher` 只跟踪已加载 source file，`ReloadSource()` 把单个 path 的新源码交给 `hot_reload.js`，再由 `Debugger.setScriptSource` 原位替换脚本文本；prepare/finish 生命周期也只围绕 source patch。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 参考点不在“也去改 UE enum”，而在“source delta 不被中途压扁”。Angelscript 一旦选择原地修改 `UEnum`，就更需要保留 `OldNames -> NewNames` 的显式 delta。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前 `ReloadEnums` 的存在性信号升级成 `FEnumReloadDelta`，让 enum rename / reorder / append 在 helper 和后续资产修补里都是可消费的结构化信息。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` 的 `FReloadState` 中新增 `TMap<TWeakObjectPtr<UEnum>, TArray<TPair<FName, int64>>> ReloadEnumOldNames`，不要再在 `OnEnumChanged` 里丢掉 `OldNames`。<br>2. 新增 `BuildEnumDelta(UEnum* Enum, const TArray<TPair<FName, int64>>& OldNames)`，把本轮变更分类成 `AppendOnly`、`RenameOnly`、`ReorderOrValueChange` 三类；第一阶段分类不确定时保守降级到 `ReorderOrValueChange`。<br>3. 对 `UUserDefinedEnum` 且 engine API 可用的场景，在 `PerformReinstance()` 前调用 `FEnumEditorUtils::BroadcastChanges(...)`；若不满足条件，则实现一个本地 `ApplyEnumDeltaToLoadedBlueprints()`，用 `OldNames` 显式修补 pin default / user variable 的枚举文本，再复用现有 node reconstruct。<br>4. 把 enum delta 挂进 hot reload session diagnostics，至少输出“哪些 enum 发生 rename / reorder，哪些资产只能做粗刷新”；第二阶段再把同一份 delta 接给离线 impact scan，覆盖未加载资产。<br>5. 首版保留现有 `ReloadEnums.Contains(...)` 路径作为 fallback；只有 enum delta pipeline 稳定后，再收紧粗刷新分支。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadEnumTests.cpp` |
| 预估工作量 | M |
| 架构风险 | enum delta 分类一旦误判，最容易把 rename / reorder 当成 append-only，进而错误保留旧 pin default；首版必须保守，宁可多触发重建，也不要静默保留错误值名。 |
| 兼容性 | 向后兼容。现有 `ReloadEnums` 粗刷新可以完整保留；新逻辑只是补充更细的 enum delta 和按需修补。 |
| 验证方式 | 1. 增加 enum value rename、reorder、append 三类热重载用例，验证 loaded Blueprint node、变量默认值和细节面板不会停留在旧值名。<br>2. 对无法安全 remap 的场景，验证系统会输出结构化 warning，并回退到现有粗刷新 + node reconstruct。<br>3. 回归现有 enum 仅追加值的场景，确认不会引入额外 full reload 或重复编译。 |

### Arch-HR-44：full reload 对 loaded Blueprint 仍走“全量对象遍历 + 二次节点扫描”，增量输入在资产层被重新放大

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | full reload 在 Editor 侧是否真正保留了 changed-script 的增量边界 |
| 当前设计 | `PerformReinstance()` 虽然会先构造 `ImpactSymbols`，但随后仍对所有已加载 `UBlueprint` 执行 `AnalyzeLoadedBlueprint()`，并且无论是否命中依赖，都会额外 `GetAllNodesOfClass()` 一遍，把 pin type 替换逻辑跑在每个 loaded Blueprint 上。之后对真正命中的 `DependencyBPs` 又再做一轮节点遍历和 `QueueForCompilation()`。换句话说，脚本层面已经收缩过的变更集合，一到 Editor 修补层又被放大成“扫描全部 loaded Blueprint，命中的再扫描第二次”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83-145`：构造 `ImpactSymbols` 后，直接 `for (TObjectIterator<UBlueprint> BlueprintIt; BlueprintIt; ++BlueprintIt)`；每个 BP 都会先 `AnalyzeLoadedBlueprint(...)`，随后立刻 `GetAllNodesOfClass()` 并遍历 pin / user pin / macro wildcard。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:190-299`：命中的 `DependencyBPs` 还要再执行一次 `RefreshRelevantNodesInBP()`，随后 `QueueForCompilation()` 与 `FlushCompilationQueueAndReinstance()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:34-67`：项目里已经有 `FBlueprintImpactRequest`、`FindModulesForChangedScripts(...)`、`BuildImpactSymbols(...)`、`AnalyzeLoadedBlueprint(...)`、`ScanBlueprintAssets(...)` 这套增量分析接口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:88-109,278-303`：scanner 已支持按 `ChangedScripts` 缩小 `MatchingModules`，再基于这些 module 的 `Symbols` 扫描候选 Blueprint 资产。 |
| 优点 | 不依赖任何持久缓存，现算现用，正确性边界简单；就算依赖索引完全过期，也不会漏掉当前已加载 Blueprint。 |
| 不足 | 当前瓶颈不在 compiler，而在 asset repair 自己没有增量索引。推断上，只要 editor 会话里加载了大量 Blueprint，即便本轮只改一个函数或一个小 module，热重载也仍要付出 `TObjectIterator<UBlueprint> + 全节点扫描` 的固定成本，而且同一 BP 可能在一次 reload 中被扫描两遍。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `M.reload()` 只从 `loaded_module_times` 里挑出 `modified_modules`，再把这一小组 module 送进 `reload_modules(module_names)`；没有在热重载主路径里重新扫描一遍所有已加载游戏资产。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:114-169`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-623` | changed-module set 是一等输入，后续阶段不应把它重新放大成“全量扫描所有 consumer”。 |
| puerts | `FSourceFileWatcher` 只注册已经加载过的 JS 文件；目录变化时只有命中 `WatchedFiles` 且 MD5 变更的 path 才会触发 `OnWatchedFileChanged(...)`，随后 `ReloadSource()` 只把这一个 path 交给 `Debugger.setScriptSource`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:52-80`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538` | 输入边界一旦已经是“loaded-source subset”，后续 HMR 也应尽量保持这个边界，而不是重新对所有潜在 consumer 做全量遍历。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `ClassReloadHelper` 之前增加一层 `LoadedBlueprintImpactIndex`，把“哪些 loaded Blueprint 依赖哪些 script symbols/module”缓存下来，让 full reload 先做候选筛选，再进入节点扫描。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/` 新增 `AngelscriptLoadedBlueprintImpactIndex.*`，缓存 `TWeakObjectPtr<UBlueprint> -> NormalizedChangedScripts / SymbolsDigest / Reasons`；索引在 Blueprint compile、asset save、asset load 时更新。<br>2. 扩展 hot reload session 或 `FReloadState`，把本轮 `ChangedScripts` 明确传到 `ClassReloadHelper`；不要只传 `ReloadClasses/Structs/Enums/Delegates` 的结果集合。<br>3. `PerformReinstance()` 首先用 `FindModulesForChangedScripts(...)` 和 `BuildImpactSymbols(...)` 生成本轮 symbols，再向 `LoadedBlueprintImpactIndex` 查询候选 loaded Blueprint；只有索引 miss 或标记 stale 时，才回退到当前 `TObjectIterator<UBlueprint>` 全量扫描。<br>4. 把当前“先全量 ReplacePinType，再对 `DependencyBPs` 二次扫描”的两段逻辑合并成一次遍历结果：单次 `AnalyzeAndPatchLoadedBlueprint()` 同时产出 `bNeedsCompile`、`PatchedPins`、`PatchReasons`，减少重复 `GetAllNodesOfClass()`。<br>5. 第一阶段把新索引放在 `CVar` 或统计开关后面，持续记录“本轮 scanned BPs / candidate BPs / reconstructed nodes / queued compiles”；确认收益和稳定性后再切换默认路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptLoadedBlueprintImpactIndex.h`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptLoadedBlueprintImpactIndex.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadBlueprintImpactTests.cpp` |
| 预估工作量 | L |
| 架构风险 | cache 一旦过期就会漏掉应被修补的 loaded Blueprint；因此首版必须保留“索引 miss / stale -> 回退全量扫描”的保守兜底，且不能让索引成为唯一事实源。 |
| 兼容性 | 向后兼容。索引只是一层增量优化，任何异常都可以回退到当前全量遍历路径；现有脚本和 Blueprint 行为不需要破坏性调整。 |
| 验证方式 | 1. 构造 large-editor-session 用例，对比启用/关闭索引时的 `scanned BPs / reconstructed nodes / compile queue` 统计，确认候选集合显著收缩。<br>2. 对 class / struct / enum / delegate 四类改动分别做回归，确认索引命中路径与当前全量扫描路径得到相同的 `DependencyBPs` 和 Blueprint 编译结果。<br>3. 人为制造索引失效，验证系统会自动回退到全量扫描，而不是静默漏修补。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-43 | enum 变更 delta 在 helper 中被压扁，缺少值名级修补语义 | delta 协议补强 + Editor 修补完善 | 中高 |
| P2 | Arch-HR-44 | loaded Blueprint 仍走全量扫描，资产层增量边界丢失 | 增量索引新增 + full reload 缩面 | 高 |

---

## 架构分析 (2026-04-10 00:07)

### Arch-HR-45：constructor 变更没有进入 `ReloadReq` 分类，soft reload 会隐式切到新构造语义

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | constructor/default lifecycle 是否被热重载状态机显式建模 |
| 当前设计 | 当前 `Analyze()` 只把 `Methods` 和 `DefaultsCode` 当作 class 级热重载差异输入；constructor 本身不在 `Methods` 集合里，而 soft path 又会在 `UpdateConstructAndDefaultsFunctions()` 中无条件切到新的 `beh.construct`，随后 `ReinitializeScriptObject()` 对 live instance 直接执行这个 constructor。换句话说，constructor 变更不是一等 delta，却会在 soft reload 中立即影响现有对象和后续新对象。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1123-1133`：`FAngelscriptClassDesc` 把 `Methods` 与 `DefaultsCode` 分开存，未见 constructor 描述字段。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1196-1292`：reload 分析阶段只比较 `OldClass->Methods` / `NewClass->Methods` 与 `DefaultsCode`，没有对 `beh.construct` 或 `__InitDefaults()` 做独立分类。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4297` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5905-5919`：soft reload 收尾会重新从 `ObjType->beh.construct` 和 `GetMethodByDecl("void __InitDefaults()")` 更新 `Class->ConstructFunction` / `Class->DefaultsFunction`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4841-4860`：`ReinitializeScriptObject()` 重建 `asCScriptObject` 后会直接执行 `ObjectTypeToConstruct->beh.construct`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1086-1135`：后续正常对象构造也统一走 `Class->ConstructFunction` / `Class->DefaultsFunction`。 |
| 优点 | 构造函数变更可以非常快地进入运行态，不需要等一次更重的 full reload 或重新生成整套类壳。 |
| 不足 | 推断上，constructor body 变更、默认构造逻辑调整、以及 `__InitDefaults()` 与 constructor 的耦合修改，目前都不会被标成单独的 reload kind。系统只会在软重载时“顺手切到新 constructor 并重放”，但不会告诉外部这是一次 construction contract 变化，也没有策略区分“只对新对象生效”还是“必须回放到现有对象”。这会把状态保持问题从显式决策，退化成隐式副作用。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `reload_modules()` 先在 sandbox 中装载新 module，再由 `update_modules()` 把新函数和 upvalue 合并回旧 module table，最后 `update_global()` 修补对象图；热重载边界是 module/function，不会因为脚本改动而重建 UE 对象并重跑 constructor。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-623` | 让“代码更新”和“对象重建”脱钩。即使当前 Angelscript 仍需保留对象重建，也应先把 constructor 变化提成显式 delta，而不是隐式随 soft reload 生效。 |
| puerts | `ReloadSource()` 只是把 `Path + JsSource` 交给 JS 侧 `__reload`；`hot_reload.js` 通过 `Debugger.setScriptSource` 原位替换脚本文本，并在替换前后发 `HMR.prepare` / `HMR.finish`。运行中对象不会因为 source patch 被强制重新走一遍构造流程。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | source-level patch 明确把“现有对象是否要重新初始化”留给业务和宿主策略，而不是默认跟随 reload 一起发生。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增 `ConstructorDelta` 分类，把 constructor/default lifecycle 从“soft reload 的隐式副作用”提升成显式决策面。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 为 `FClassData` 增加 `EConstructionDeltaKind`，至少区分 `None`、`ConstructorBodyOnly`、`DefaultsInitOnly`、`ConstructorAndDefaultsCoupled`、`ConstructorSignatureOrFactoryChange`。<br>2. 在 `Analyze()` 中，除现有 `Methods` / `DefaultsCode` 比较外，再解析旧/新 `ScriptType->beh.construct` 与 `__InitDefaults()` 的 identity 或等价摘要；无法证明等价时，至少提升到 `FullReloadSuggested`，并把原因写入 `ReloadReqLines`/diagnostic。<br>3. 把 `ReinitializeScriptObject()` 的行为策略化：新增 `EConstructorReplayPolicy`，首版默认保持旧行为，但在 `ConstructorBodyOnly` 场景先输出 `warn-only` 诊断，允许项目通过 `CVar` 选择 `Replay`、`SkipForExistingObjects`、`DeferUntilFullReload`。<br>4. 给类级扩展留一个 opt-in 缝，例如 `bAllowSoftConstructorReplay` 或 `IAngelscriptHotReloadConstructionAware`；只有显式声明可安全回放的类，才允许在 constructor delta 下对 live instance 自动重放。<br>5. 增加测试：覆盖“只改 constructor body”“只改 `__InitDefaults()`”“constructor + defaults 同时改”“constructor 引入外部注册副作用”四类场景，验证诊断与策略分流正确。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 预估工作量 | M |
| 架构风险 | constructor/default 变化的等价判定如果过于乐观，最容易把“实际改变对象外部注册语义”的修改误判成安全 soft delta；首版必须保守，宁可多提示 `Suggested`，不要静默重放。 |
| 兼容性 | 向后兼容。第一阶段只新增分类、诊断和可选策略开关；默认仍可保留现有 constructor replay 行为，待项目验证后再逐步收紧默认策略。 |
| 验证方式 | 1. 构造 actor/object 类，仅修改 constructor body，验证系统会产生单独的 `ConstructorBodyOnly` 诊断，而不是落成普通 soft reload。<br>2. 在 `SkipForExistingObjects` 下验证 live instance 不会重放 constructor，但新创建对象会使用新构造逻辑。<br>3. 在 legacy replay 模式下回归现有测试，确认行为与当前基线一致。 |

### Arch-HR-46：delegate reload 在 Blueprint 侧只显式覆盖 `Event Signature`，delegate 变量与绑定节点缺少类型级 remap

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | delegate 签名变化在 Editor 修补链中的覆盖面 |
| 当前设计 | `ClassReloadHelper` 确实收集了 `ReloadDelegates/NewDelegates`，`BlueprintImpactScanner` 也把 `DelegateSignature` 作为一种 impact reason；但真正的 pin/type 修补仍只处理 struct/enum。delegate 相关的显式刷新逻辑只覆盖 `UK2Node_Event::FindEventSignatureFunction()`，而 `FBPVariableDescription`、普通 delegate pin、dispatcher/bind/create 节点并没有对等的 remap 分支。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:38-39,104-109`：state 会收集 `ReloadDelegates` 与 `NewDelegates`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:55-80`：`ReplacePinType()` 只处理 `PC_Struct`、`PC_Enum`、`PC_Byte`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:137-140`：`BP->NewVariables` 也只是复用同一个 `ReplacePinType()`，因此没有 delegate 变量专门分支。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:208-226,266-275`：`RefreshRelevantNodesInBP()` 的类型检查仍只覆盖 struct/enum；delegate 只对 `UK2Node_Event` 的 `FindEventSignatureFunction()` 做显式判断。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:44-56`：`MatchesPinType()` 只识别 struct/enum，不识别 `PC_Delegate/PC_MCDelegate`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:208-215,228-233`：delegate 影响检测只对 `UK2Node_Event` 做 `DelegateSignature` 判断；变量扫描虽然会看 `Variable.VarType`，但仍依赖前面的 `MatchesPinType()`，因此不会命中 delegate 变量。 |
| 优点 | 当前实现足够简单，`Event` 节点这类最常见的 delegate 入口能被快速纳入 dependency compile 流。 |
| 不足 | 推断上，Blueprint 中的 delegate 变量、event dispatcher pin、`CreateDelegate`/`Assign`/绑定类节点，以及任何以 `PC_Delegate/PC_MCDelegate` 表示签名的序列化字段，都没有进入显式 remap 面。它们只能寄希望于 `HasExternalDependencies()` 或整图重编译间接修好；一旦这些路径没暴露旧签名对象，就会形成“命中依赖但没有精确修补”的灰区。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载主路径仍是 `reload_modules()` + `update_modules()`：把新函数并回旧 module table、合并 upvalue、再修 `_G` 与运行中对象图。整个过程中没有一条“重新生成并替换 `UDelegateFunction`”的独立 repair 子系统。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-623` | 如果 HMR 选的是 VM/module patch 路线，就应尽量避免把 delegate 变化外化成大量 UE reflection shell replacement。 |
| puerts | `ReloadSource()` 只是触发 JS 侧 `__reload`，`hot_reload.js` 用 `Debugger.setScriptSource` 原位更新 script，并以 `HMR.prepare/HMR.finish` 暴露生命周期；热重载的 owner 是 source/module，而不是每次都重建 delegate signature 对象。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1542`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | 既然当前 Angelscript 必须保留 `UDelegateFunction` 路线，就更需要把 delegate delta 做细，而不是只保存一个 `TMap<Old, New>` 再靠局部节点刷新碰运气。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `ReloadDelegates` 升级成 `FDelegateReloadDelta` 协议，并为 Blueprint 中的 delegate 变量、pin 与绑定节点补齐类型级修补。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h/.cpp` 新增 `FDelegateReloadDelta`，至少记录 `OldDelegate`、`NewDelegate`、`DeltaKind(SignatureChanged/ImplementationOnly/MetadataOnly)`、`ChangedPinsOrArgs`。<br>2. 扩展 `BlueprintImpactScanner`：让 `MatchesPinType()` 支持 `PC_Delegate`、`PC_MCDelegate`，并为 `FBPVariableDescription`、`UK2Node_CreateDelegate`、`UK2Node_AddDelegate`、dispatcher 相关节点增加显式 delegate 签名检查，而不是只盯 `UK2Node_Event`。<br>3. 扩展 `ClassReloadHelper::ReplacePinType()` 与变量修补逻辑：当 pin/变量引用旧 delegate signature 时，直接 remap 到 `NewDelegate`；无法安全 remap 的场景再退回 node reconstruct + compile。<br>4. 对 `ImplementationOnly` 的 delegate 变化，优先尝试不创建新的 `UDelegateFunction` 壳，而是仅更新底层可调用目标；若现有架构暂时做不到，首版至少把这类场景标成单独的 diagnostics，避免与 `SignatureChanged` 混在一起。<br>5. 增加自动化测试：覆盖 delegate 变量签名改动、event dispatcher 绑定节点、`CreateDelegate` 节点、custom event 对应签名 rename，以及 `ImplementationOnly` 不应触发整图重构的场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadBlueprintImpactTests.cpp` |
| 预估工作量 | M |
| 架构风险 | delegate 消费节点种类多，若首版试图一次性覆盖全部 K2 node，最容易引入误判或重复重建；因此必须先做 delta 分类和有限白名单，再保留 compile fallback。 |
| 兼容性 | 向后兼容。现有 `ReloadDelegates` 与 dependency compile 路径可以完整保留；新逻辑只是补充更细的 delegate delta 和更直接的 remap。 |
| 验证方式 | 1. 增加一个 Blueprint，分别覆盖 delegate 变量、dispatcher 绑定和 `CreateDelegate`，验证签名变化后不再残留旧 delegate shell。<br>2. 对无法安全 remap 的节点验证系统会输出结构化 warning，并回退到当前 compile/reconstruct 路径。<br>3. 回归普通 `Event` 节点场景，确认新增 delegate 逻辑不会破坏现有修补结果。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-45 | constructor/default lifecycle 没有进入 reload 决策面，soft reload 会隐式切到新构造语义 | delta 分层 + 策略化 replay | 高 |
| P1 | Arch-HR-46 | delegate 签名变化在 Blueprint 侧只覆盖 `Event`，缺少变量/绑定节点 remap | delta 协议补强 + Editor 修补补面 | 中高 |

---

## 架构分析 (2026-04-10 00:16)

### Arch-HR-47：`SoftReload` 没有把 `RF_ArchetypeObject` / 模板对象建成独立迁移通道

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模板对象、预览对象与 archetype 链的状态保持 |
| 当前设计 | `DoSoftReload()` 把对象集合切成“`CDO`”和“其余实例”两类；查询阶段没有排除 `RF_ArchetypeObject`，执行阶段也没有 archetype/template 专门分支。结果是模板对象如果命中过滤条件，会跟普通 live instance 一样走 `DestructScriptObject()` + `ReinitializeScriptObject()` + constructor replay。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4151-4155`：`GetObjectsOfClass(Class, Instances, true, RF_NoFlags)` 收集对象时没有过滤 `RF_ArchetypeObject`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4571-4583`：soft path 只把 `RF_ClassDefaultObject` 单独移入 `CDOInstances`，其余对象都按普通实例处理。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4620-4639`：普通实例路径会析构旧 script object、回填 instanced property、再 `ReinitializeScriptObject()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4841-4860`：`ReinitializeScriptObject()` 会无条件执行 `beh.construct`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4116-4118`：当前源码里 `RF_ArchetypeObject` 只在临时 `CDONoDefaults` 构造时出现，并未参与 live/template 分类。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:111-175` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:387-439`：Editor helper 只订阅 full reload 事件，并专门修 open asset editors，不存在“template/archetype owner”收集面。 |
| 优点 | 当前路径非常统一，绝大多数普通实例与 `CDO` 都能复用同一套属性暂存和 script object 重建逻辑。 |
| 不足 | 推断上，component template、资产 archetype、Editor preview object、transaction duplicate 这类 `RF_ArchetypeObject` 并不总等价于 live instance；把它们直接落到实例路径，会把“模板默认值迁移”与“运行时副作用 replay”混在一起，容易在模板层制造重复注册、预览态漂移或默认值污染。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载 owner 在 Lua env/module graph。`update_modules()` 会把新函数并回旧 module table，再由 `update_global()` 递归修 `_G`、registry、userdata、running stack；`ULuaEnvLocator_ByGameInstance::HotReload()` 只是把 reload fan-out 给多个 env，不重建 UE archetype/template 对象。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549,604-623`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:76-82` | 把“脚本逻辑更新”与“宿主对象模板重建”解耦；如果当前 Angelscript 必须重建 `UObject`，也应先把 template object 单独建模。 |
| puerts | `hot_reload.js` 在同一个 `scriptId` 上做 `Debugger.setScriptSource`，HMR 本身不重建 UE 对象；运行时绑定层则显式把 `RF_ClassDefaultObject | RF_ArchetypeObject` 归进 `IsCDO` 分支，对模板/默认对象走不同逻辑。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1550-1599` | 即使无法做到 source-level patch，也应像 puerts 那样先把 template/default object 识别出来，避免与 live instance 共用完全相同的 reload 语义。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `Instance/CDO` 二分法前增加 `ArchetypeTemplate` lane，把模板对象迁移从普通实例路径中剥离出来。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 的对象收集阶段新增 `TemplateInstances`，分类条件至少区分 `RF_ClassDefaultObject`、`RF_ArchetypeObject`、普通实例。<br>2. 新增 `FTemplateReinstanceData` 或 `ETargetObjectKind`，记录 `Object`、`ArchetypeRoot`、`AssociatedCDO`、`bAllowConstructorReplay`；不要再让 archetype 复用 live instance 的默认判定。<br>3. 为模板对象单独实现 `ReinstanceTemplateScriptObject()`：优先迁移 script property 值与 instanced reference，默认关闭 constructor replay；只有显式白名单的 template type 才允许重放构造逻辑。<br>4. 扩展 `ClassReloadHelper` 或新建 `TemplateOwnerCollector`，把 `UBlueprintGeneratedClass` component template、open asset preview object、常见 editor archetype owner 收进 diagnostics/refresh 面；首版只做发现与 warning，不强行修所有模板。<br>5. 增加自动化测试：覆盖 component template、asset archetype、preview actor/template object 三类场景，验证 soft reload 后模板默认值与 live instance 状态不再互相污染。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadTemplateTests.cpp` |
| 预估工作量 | M |
| 架构风险 | archetype/template 对象种类很多，首版如果试图一次性覆盖所有 owner，最容易引入误分类；必须先做显式 lane 和 diagnostic，再逐步扩覆盖面。 |
| 兼容性 | 向后兼容。第一阶段可以只新增分类、diagnostic 和 opt-in 的 template path；默认仍可回退到旧实例路径。 |
| 验证方式 | 1. 创建带 instanced component template 的脚本类，只改函数 body，验证模板对象不再误走 live instance constructor replay。<br>2. 在 open asset preview/editor archetype 场景下执行 soft reload，验证模板默认值与场景实例值分离。<br>3. 打开 legacy fallback 时回归现有 hot reload 测试，确认旧行为仍可用。 |

### Arch-HR-48：full reload 目前以双 `CollectGarbage()` 作为主要收口屏障，replacement correctness 依赖全局 GC

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | full reload 的事务边界、GC 成本与原生 reload 的可组合性 |
| 当前设计 | legacy full reload 的主收口手段不是显式 owner manifest，而是 `CollectGarbage()` 前后双屏障：先改 pin/type 和部分资产引用，再做一次全局 GC，随后 `ReparentHierarchies()` / Blueprint recompile，再做第二次 GC。为了让 open asset editors 不在 GC 中丢失引用，还需要 `UAngelscriptReferenceReplacementHelper` 手工把这些资产 root 住并在 serialize 里补 replacement。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:33-38`：`ReplaceHelper` 是全局 singleton，并被 `AddToRoot()` 常驻。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:166-188`：legacy 分支在 `ReparentHierarchies()` 前后各做一次 `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:179-181,190-299`：GC 之间才执行 `FBlueprintCompilationManager::ReparentHierarchies()`、节点刷新、`QueueForCompilation()` 与 `FlushCompilationQueueAndReinstance()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:387-439`：`UAngelscriptReferenceReplacementHelper` 只为 `UAssetEditorSubsystem::GetAllEditedAssets()` 做额外 rooting/replacement。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:319-329`：只有开启 `angelscript.UseUnrealReload` 时才改走 `FReload(EActiveReloadType::Reinstancing)`，默认 backend 并不复用 UE 原生 reload 的收口机制。 |
| 优点 | 这条路径很保守，能在复杂 replacement 之前尽量清掉旧对象，并降低 Blueprint reinstance 阶段看到悬挂引用的概率。 |
| 不足 | 当前 correctness 更依赖“全局 GC 会把旧对象清干净”而不是“我们明确知道哪些 owner 需要 replacement”。这会放大全量 Editor 会话的延迟，也让 Angelscript 的 legacy reload 更难和 UE 原生 `FReload` / native hot reload 共用同一条 replacement contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv::HotReload()` 只是触发 `UnLua.HotReload()`；Lua 侧 `update_modules()` / `update_global()` 直接在 module table、upvalue、running stack、registry 上原位修补，没有把全局 GC 当成热重载事务主屏障。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549` | 让“谁需要被修补”先成为显式 value map，再决定是否需要清理旧状态；GC 可以是善后，不应天然成为主事务边界。 |
| puerts | JS 热更新只对目标 `scriptId` 执行 `Debugger.setScriptSource`；遇到 native reload，则在 `ReloadCompleteDelegate/OnHotReload()` 上重建 `JsEnv`。这条链路没有自定义的双 GC reload 屏障。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:424-438` | native reload 交互优先走显式 delegate 和 runtime rebuild，而不是叠加另一套“先 GC 两次再修补”的自定义事务。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 legacy full reload 从“双 GC barrier”演进成“显式 owner/replacement manifest + 单次善后 GC”的两阶段模型。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h/.cpp` 新增 `FReloadOwnerManifest`，显式记录 `Blueprints`、`DataTables`、`OpenEditedAssets`、`TemplateOwners`、`ReplacementPairs` 与 `NeedsNativeReloadInterop`。<br>2. 将当前 legacy 路径拆成 `PrepareReplacementManifest()`、`ExecuteBackendReinstance()`、`ApplyReplacementToOwners()` 三步；Blueprint/DataTable/open asset replacement 优先只在 manifest 列出的 owner 上做，不再默认把第一次 GC 当“发现旧引用”的主要手段。<br>3. 第一阶段保留现有前置 `CollectGarbage()` 作为 fallback，但仅在 manifest 标记“未知 owner”或 backend 失败时触发；正常路径先尝试直接 `ReparentHierarchies()` + owner replacement，再做一次 post-commit GC 善后。<br>4. 把 manifest 设计成 backend-neutral：`angelscript.UseUnrealReload=1` 或未来 native reload bridge 都复用同一个 owner/replacement 描述，避免 legacy/backend 各自维护一套 replacement 账本。<br>5. 新增统计与测试：输出 `OwnersTouched / ReplacementPairs / PreGCTriggered / PostGCTriggered / UnknownOwners`，并构造 large-editor-session 用例，比较新旧路径的 GC 次数、耗时与残留旧引用数。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadBlueprintImpactTests.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadGcBoundaryTests.cpp` |
| 预估工作量 | L |
| 架构风险 | 如果 owner manifest 漏收某些 editor/tool cache，首版最容易留下旧引用；因此必须保留“未知 owner -> 回退当前双 GC 路径”的保守兜底。 |
| 兼容性 | 向后兼容。第一阶段以 opt-in/CVar 形式启用 manifest path，legacy 双 GC 路径完整保留作为 fallback。 |
| 验证方式 | 1. 在大型 Editor 会话下对比启用/关闭 manifest path 时的 GC 次数、耗时与 replacement 覆盖率。<br>2. 让 `angelscript.UseUnrealReload=0/1` 两条 backend 都走同一份 manifest，验证 open asset、Blueprint、DataTable 的 replacement 结果一致。<br>3. 人为制造未知 owner，确认系统会回退到旧双 GC 路径而不是静默留下 stale reference。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-47 | `RF_ArchetypeObject` / 模板对象缺少独立迁移语义 | 状态保持分层 + template lane 新增 | 高 |
| P2 | Arch-HR-48 | full reload 以双 `CollectGarbage()` 作为主要收口屏障 | owner manifest 新增 + GC 屏障收窄 | 中高 |

---

## 架构分析 (2026-04-10 00:26)

### Arch-HR-49：watcher 输入路径、module 键和运行时 fallback 的 canonicalization 规则不一致

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载输入路径身份与 module 归属判定的一致性 |
| 当前设计 | `DirectoryWatcher` 入队时用 `TryMakeRelativeScriptPath()` 做“绝对路径 -> 相对脚本路径”映射，但这一步只是对 `AbsolutePath.StartsWith(RootPath)` 做直接前缀判断；同一份相对路径随后又会在预处理阶段变成 `ModuleName`，而运行时 fallback 查 module 时改用 `MakePathRelativeTo_IgnoreCase()` 做大小写无关、斜杠归一化的另一套规则。也就是说，watcher、preprocessor、runtime lookup 并没有共享同一个 path identity contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:8-18`：`TryMakeRelativeScriptPath()` 只做 `StartsWith(RootPath)` 和 `MakePathRelativeTo()`，没有 `NormalizeFilename` / slash / case 统一。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-68`：`.as` 事件入队直接使用这套 `AbsolutePath + RelativePath`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1361`：`DiscoverScriptRoots()` 会排序 plugin roots，并把 project root 强行插到第一个位置。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:91-103`：`AddFile()` 直接把 `RelativeFilename` 喂给 `FilenameToModuleName(RelativeFilename)`，说明相对路径就是 module identity 输入。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3029-3055`：`GetModuleByFilename()` 的 fallback 又改走 `MakePathRelativeTo_IgnoreCase()` + `.as -> .` 转 module 名。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5802-5845`：`MakePathRelativeTo_IgnoreCase()` 会统一 slash，并按 `IgnoreCase` 比较路径段。 |
| 优点 | 实现简单，project root first 让冲突场景下的解析顺序至少是确定的；同时 runtime fallback 具备一定的 Windows 宽容度。 |
| 不足 | 推断上，同一脚本文件在不同入口会被不同 canonicalization 规则处理：watcher 侧更像“原样字符串前缀匹配”，runtime 侧更像“ignore-case 归一化相对路径”。在 Windows 和多 root 工程里，这会把大小写、斜杠形式、root 顺序乃至同前缀 root 的行为变成隐式语义，热重载输入键可能与后续 module lookup 键漂移。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | editor watcher 不负责把磁盘路径映射成多 root 下的 module key；它只在目录有变化时触发一次 `HotReload()`，真正的 reload 键是 Lua 侧的 `module_name`，并用 `config.script_root_path + module_name:gsub(".", "/") + ".lua"` 反查时间戳。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-36,112-118`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:112-119,147-170` | 把“文件系统路径”与“脚本 module 身份”解耦，避免多 root 路径字符串直接成为热重载主键。 |
| puerts | `FSourceFileWatcher` 先在 `OnSourceLoaded()` 时登记精确的 watched dir/file，再在 `OnDirectoryChanged()` 里对 `Change.Filename` 先 `NormalizeFilename()`、`ConvertRelativePathToFull()`，并兼容 `/` 与 `\\` 两种目录键；JS 侧 `hot_reload.js` 又用 `url <-> scriptId` 双向表，并对 Windows 路径额外做 slash 转换。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:13-24,67-90` | 让“同一 source 身份”在 watcher、runtime、debugger 三处共用一套 canonical key，而不是每层各自猜路径。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 runtime core 引入统一的 `ScriptPathId` / canonical resolver，把 watcher 入队、preprocessor 建 module、runtime fallback lookup 三处收口到同一条路径身份规则。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptScriptPathId` 或 `ResolveScriptPathId(...)`，至少产出 `CanonicalAbsolutePath`、`CanonicalRelativePath`、`RootIndex/RootId`、`ModuleName`。<br>2. 让 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` 的 `QueueScriptFileChanges()` 先调用这套 resolver，再写 `FFilenamePair`；不要继续在 watcher 层自己做 `StartsWith`。<br>3. 把 `FAngelscriptPreprocessor::AddFile()`、`GetModuleByFilename()`、`FindScriptFiles()` 统一切到 `CanonicalRelativePath/ModuleName`，移除 watcher 与 runtime 对大小写/斜杠/相对化的双重规则。<br>4. root 匹配从“纯字符串前缀”升级成“路径段边界 + 已规范化 root id”；若同一绝对路径能匹配多个 root，第一阶段直接输出结构化 warning，而不是静默依赖 root 顺序。<br>5. 补自动化测试：覆盖 mixed-case path、`/` vs `\\`、overlapping-root-prefix、project/plugin root 同时存在四类场景，确保 watcher 入队键与 `GetModuleByFilename()` fallback 键完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果 canonical rule 直接改成“更严格”，可能暴露已有工程里依赖模糊 root 解析的历史布局；因此首版应先做双写比对和 warning，再切默认路径。 |
| 兼容性 | 向后兼容。对脚本 API 无破坏；第一阶段只统一内部键值和 diagnostics。只有依赖模糊 root/路径别名的工程，才会看到新的 warning。 |
| 验证方式 | 1. 人工构造大小写不同、slash 不同但指向同一脚本的保存事件，验证只产生一个 canonical reload key。<br>2. 构造 overlapping-root-prefix 场景，确认系统会稳定选择同一 root id，并输出可读诊断。<br>3. 对正常单 root / 常规 plugin root 场景回归，确认 module 名与现有行为一致。 |

### Arch-HR-50：full reload 的 replacement contract 几乎不覆盖 editor 瞬时状态，selection/undo 仍是状态丢失盲区

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | full reload 后 editor 瞬时状态的状态保持健壮性 |
| 当前设计 | `ClassReloadHelper` 的 repair 面主要覆盖 loaded Blueprint、`UDataTable::RowStruct`、PropertyEditor 刷新、volume/component palette 刷新，以及 open asset editors 的引用替换。`FReloadState` 没有 selection、transaction、undo/redo 相关 owner；`UAngelscriptReferenceReplacementHelper` 也只对 `UAssetEditorSubsystem::GetAllEditedAssets()` 做 rooting/replacement。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:27-41`：`FReloadState` 只记录 `ReloadClasses/Assets/Enums/Structs/Delegates` 与少量 UI flag，没有 selection/transaction 维度。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:111-175`：`Init()` 订阅的也只是类型和 asset reload 事件，`OnPostReload` 收尾是刷新 action list、component registry、volume rebuild，然后清空 state。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:166-188,190-299`：legacy path 的核心是 `ReparentHierarchies()`、Blueprint compile/reinstance 和 DataTable/graph 修补。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:333-338`：统一 UI 收口只有 `PropertyEditorModule->NotifyCustomizationModuleChanged()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:387-439`：`UAngelscriptReferenceReplacementHelper` 只枚举 `GetAllEditedAssets()`，并在 serialize 时修 open editors 的 asset 引用。 |
| 优点 | repair 面保持得很窄，当前实现主要承担“脚本类/资产换壳后编辑器还能继续工作”的最低闭环，不会一开始就碰所有 editor 子系统。 |
| 不足 | 推断上，一旦 full reload 让 class/asset/object graph 发生 replacement，当前白名单之外的 editor 瞬时状态仍可能停留在旧对象上，例如 `GEditor` 当前 selection、Details 面板选中对象链、以及 undo/redo transaction buffer。因为现有 contract 既不收集这些 owner，也不在收尾阶段显式 remap 或 reset，它们属于当前状态保持的灰区。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv::HotReload()` 只触发 `UnLua.HotReload()`；Lua 侧 `reload_modules()` / `update_modules()` / `update_global()` 更新的是 module table、function、upvalue 与 `_G` 对象图，而不是重建 UE editor 里的 selected object / asset shell。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549,553-623` | 当热重载 owner 是 VM/module patch 时，editor 瞬时状态不会天然变成 replacement 问题。 |
| puerts | `ReloadSource()` 只是把 `Path + JsSource` 交给 JS 侧 `__reload`；`hot_reload.js` 的主路径是 `HMR.prepare -> Debugger.setScriptSource -> HMR.finish`，仍然是 source-level patch，不生成一条独立的 UE object replacement 链。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 既然当前 Angelscript 选择了 `UClass` / asset reinstance 路线，就必须把 editor 瞬时状态 owner 也显式纳入 replacement contract，而不能只修 open asset editor。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `ClassReloadHelper` 旁边增加一条 `EditorEphemeralState` repair lane，把 selection 与 transaction 从“隐式副作用”提升成显式 owner。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h/.cpp` 为 `FReloadState` 增加 `SelectedActors`、`SelectedObjects`、`bTouchedTransactionalObjects`、`ReplacementMapAvailable` 等字段；full reload 开始前快照 `GEditor->GetSelectedActors()/GetSelectedObjects()` 中命中 `ReloadClasses/ReloadAssets` 的对象。<br>2. backend 完成后，如果本轮有可用 replacement pair，就先用 replacement map 重建 selection；随后显式调用 `GEditor->NoteSelectionChange()`，不要只依赖 `PropertyEditorModule->NotifyCustomizationModuleChanged()`。<br>3. 第一阶段对 undo/redo 采用保守策略：若本轮触及 `RF_Transactional` 对象且无法提供可靠 instance replacement map，则调用 `GEditor->ResetTransaction(...)` 并输出结构化 warning，避免 transaction buffer 继续悬挂旧对象。<br>4. 第二阶段若 legacy/backend-neutral manifest 已存在，再把 transaction policy 升级成“可替换则 remap，不可替换才 reset”，并把 reset 原因写入 hot reload session diagnostics。<br>5. 增加 editor automation：覆盖“选中 actor/object 后执行 full reload”“reload 后立刻打开 Details / 触发 undo”两类场景，验证系统要么正确 remap，要么安全清空，而不是留下 stale handle。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 下新增 selection/transaction hot reload 用例 |
| 预估工作量 | M |
| 架构风险 | 最明显的代价是 undo 栈可能被更频繁地清空，影响 editor 体验；因此首版应只在“有 replacement 且 transaction owner 不可安全修补”的场景触发 reset，并把行为做成可诊断、可关闭。 |
| 兼容性 | 向后兼容。不会破坏脚本 API；首版主要改变的是 editor 行为变得更保守，可能在高风险 reload 后清空 undo history 或刷新 selection。 |
| 验证方式 | 1. 选中一个脚本类 actor / asset 后执行 full reload，确认 reload 后 selection 不会残留旧对象指针。<br>2. 在含 `RF_Transactional` 对象的场景执行 full reload，随后触发 undo/redo，确认不会命中 stale reference；若走 reset 路径，则日志能清楚说明原因。<br>3. 对没有 replacement 的普通 soft reload 场景回归，确认不会引入额外 selection/transaction 扰动。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-50 | full reload 后 editor 瞬时状态的 selection/undo 保持 | 状态保持补面 + editor repair lane | 高 |
| P2 | Arch-HR-49 | watcher 输入路径与 module 键的 canonicalization 不一致 | 输入身份统一 + 路径 canonical resolver | 中高 |

---

## 架构分析 (2026-04-10 00:36)

### Arch-HR-51：base class 的 `SoftReload` 以“first AS ancestor”为 owner，Blueprint descendants 会被一起重建

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 继承链上的增量重载 blast radius |
| 当前设计 | `DoSoftReload()` 虽然入口是单个 `UASClass`，但对象收集与 script type 判定都按“对象的第一个 `UASClass` 祖先”工作。结果是：修改一个 Angelscript base class 时，所有以它为 first AS ancestor 的 Blueprint descendant instance 和 descendant CDO 都会进入同一轮析构/重建，而不是只处理该 base class 自己的直接宿主。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4151-4155`：`DoSoftReload()` 用 `GetObjectsOfClass(Class, Instances, true, RF_NoFlags)` 收集实例，并显式包含 derived classes。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp:4-11`：`asIScriptObject::GetObjectType()` 不是读对象内的旧 type，而是回到 `UASClass::GetFirstASClass((UObject*)this)` 再取该类的 `ScriptTypePtr`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:939-948`：`GetFirstASClass(UObject*)` 会沿继承链向上找到第一个 `UASClass`，因此 Blueprint descendant 会折叠回 base AS class。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4302-4332`：soft reload 会先把所有 child `UBlueprintGeneratedClass` 的 `ScriptTypePtr` 一并切到新 type。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4567-4656,4669-4785`：随后实例和 `CDO` 循环都会对命中的对象执行 `DestructScriptObject()`、`ReinitializeScriptObject()` 和属性回填；对于 descendant instance 还会用 `AssociatedCDO != BaseCDO` 的分支把 descendant CDO override 折叠进同一条迁移逻辑。 |
| 优点 | base class 的 method/default 变更能立刻反映到整个 Blueprint 继承树，不需要额外追踪某个 descendant 是否还挂着旧 base script type。 |
| 不足 | body-only 或局部 method 变更也会放大成“整棵 descendant 树的 instance/CDO 重建”。这会把 constructor replay、属性迁移和状态丢失风险扩散到本轮未直接修改的 Blueprint carriers，热重载成本跟继承树宽度绑定。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载 owner 是 `module_names` 与 old/new module table。`reload_modules()` 只为变更 module 生成 `old_modules/new_modules`，`update_modules()` 再把新函数写回旧 table、合并 upvalue，并在末尾 `merge_objects()` / `update_global()` 修补 Lua 对象图；没有一轮按 UE 继承树扫 live `UObject` 的实例重建。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:511-547,553-623` | 把“脚本实现更新”优先建模成 module graph patch，而不是默认级联到所有宿主对象。 |
| puerts | `execLazyLoadCallJS()` 和 `NotifyRebind()` 走的是 class-level lazy rebind：首次调用时只对命中的 `UTypeScriptGeneratedClass` / super chain 清 `NeedReBind` 并通知 `DynamicInvoker`，没有 `GetObjectsOfClass(..., true)` 式的全实例扫描。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:57-73,77-99` | 可以借鉴“descendant class 标脏、按需 rebind”的模式，让继承树上的提交从 eager reinstance 退到 lazy commit。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `SoftReload` 的 owner 从“first AS ancestor 全树 eager reinstance”拆成“exact AS owner + descendant commit policy”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` 的 `FClassData` 增加 `EReloadCascadeMode`，至少区分 `ExactOwnerOnly`、`DescendantCDOOnly`、`AllDescendants`。<br>2. 在 `Analyze()` 阶段把 delta kind 与 cascade mode 绑定：body-only / method-only 默认只给 `ExactOwnerOnly`；constructor/default/component/layout 变化才升级到 `DescendantCDOOnly` 或 `AllDescendants`。<br>3. 在 `DoSoftReload()` 中把 `Instances` 拆成 exact `UASClass` owner 和 Blueprint descendant carriers；`ExactOwnerOnly` 路径只更新 method/script type 绑定，不再对 descendant instance/CDO 执行 `DestructScriptObject()` / `ReinitializeScriptObject()`。<br>4. 给 descendant `UBlueprintGeneratedClass` 增加显式 dirty bit 或 session marker，参考 puerts 的 `NeedReBind` 思路，在首次脚本调用、Blueprint recompile 或 editor commit 时再补做 descendant rebind。<br>5. 增加继承链测试：`AS base -> BP child -> BP grandchild`，分别覆盖 body-only、constructor、defaults 三类修改，验证只有需要的层级被重建。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果 cascade mode 判得过窄，可能漏掉“base constructor/default 语义实际影响 descendant CDO”的场景；首版应保持保守回退，任何拿不准的 delta 继续走当前全树路径。 |
| 兼容性 | 向后兼容。可以先以 `CVar` / 实验开关启用 selective cascade；默认仍保留当前 eager descendant reload。 |
| 验证方式 | 1. 构造 base AS class 仅改 method body 的用例，确认 descendant instance/CDO 不再进入 `ReinitializeScriptObject()`。<br>2. 构造 constructor/default 变化用例，确认系统仍会升级到 descendant reload。<br>3. 统计 reload session 中 exact owner 与 descendant 两类对象的数量，验证 blast radius 收窄但行为不回退。 |

### Arch-HR-52：`BlueprintImpact` 的“分析”阶段已经在执行 object replacement，检测与提交边界被打穿

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | full reload 前置分析的纯度与事务边界 |
| 当前设计 | `PerformReinstance()` 在真正进入 `CollectGarbage()` / `ReparentHierarchies()` / Blueprint compile 之前，会先对每个 loaded Blueprint 调 `AnalyzeLoadedBlueprint()` 做 impact 分析。但这个“分析器”遇到 `ReplacementObjects` 时会直接构造 `FArchiveReplaceObjectRef` 作用在真实 `UBlueprint` 上；结合 UE archive 的默认行为，这一步不是只读 probe，而是会立刻序列化并替换匹配引用。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83-145`：legacy full reload 先构造 `ImpactSymbols.ReplacementObjects`，随后在 `for (TObjectIterator<UBlueprint> ...)` 中逐个调用 `AnalyzeLoadedBlueprint(*BP, ImpactSymbols, ImpactReasons)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:237-246`：`AnalyzeLoadedBlueprint()` 直接在 `&Blueprint` 上构造 `FArchiveReplaceObjectRef<UObject>`，并以 `GetCount() > 0` 作为 `ReferencedAsset` 理由。<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/Serialization/ArchiveReplaceObjectRef.h:178-197`：`FArchiveReplaceObjectRef` 构造函数默认会立即 `SerializeSearchObject()`。<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/Serialization/ArchiveReplaceObjectRef.h:245-257`：`operator<<` 在命中 replacement map 时会直接把 `Obj = *ReplaceWith`，并累计 `Count`。 |
| 优点 | 复用了 UE 现成的 replacement archive，命中检查和真正替换共享同一套遍历逻辑，实现成本低。 |
| 不足 | “分析”和“提交”被混在一起了。loaded Blueprint 在 session 还没完成 `ReparentHierarchies()`、也还没进入明确的 compile/save owner 之前，就可能被隐式改写引用；如果后续 full reload 失败、回退或被中断，当前架构没有一条对称的 abort 路径把这些前置副作用收回来。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载主路径完全留在 Lua env：`update_modules()` 只改 old module table、upvalue 和全局对象图，`M.reload()` 也只是按 modified module 列表触发这条路径，没有一条“先扫描 Blueprint 再顺手改资产引用”的隐式 editor mutation。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:533-547,553-623` | 如果 owner 是 VM/module graph，分析阶段就应保持纯读，提交副作用留在明确的 reload commit 中。 |
| puerts | puerts 的 Blueprint 写回是显式 authoring compiler：`RemoveNotExistedMemberVariable()` / `RemoveNotExistedFunction()` 明确删 Blueprint 成员；`Save()` 再显式 `MarkBlueprintAsModified()`、`CompileBlueprint()` 和保存 package。写权限存在，但它是独立、可见的 commit 阶段，不伪装成“分析器”。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1269-1289,1294-1395` | 如果必须写资产，应像 puerts 一样把 mutation owner、compile 和 save 做成显式 commit，不要藏在 impact scan 里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 让 `AnalyzeLoadedBlueprint()` 回到纯分析职责，把 object replacement 提升成独立 commit lane。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/` 新增只读 probe（例如 `FBlueprintReplacementProbe`）：要么用 `FReferenceFinder`/自定义 archive 只统计命中，不改原对象；要么先复制到 transient clone，再在 clone 上跑 `FArchiveReplaceObjectRef`。<br>2. 改写 `AnalyzeLoadedBlueprint()`，把当前 `FArchiveReplaceObjectRef` 逻辑替换成 probe 结果；函数返回的 `ImpactReasons` 只能描述命中，不得修改 `Blueprint` 本体。<br>3. 在 `ClassReloadHelper.cpp` 新增 `ApplyBlueprintReplacementObjects()` 显式提交阶段，只在 backend reinstance 成功后再把 `ReplacementObjects` 应用到目标 Blueprint，并随后 `QueueForCompilation()` / `MarkBlueprintAsModified()`。<br>4. 为 commit lane 增加事务与诊断：记录 `Blueprint` 是否被 replacement、是否进入 compile、是否需要保存；full reload abort 时直接跳过 commit，不让分析阶段遗留半提交副作用。<br>5. 补测试：断言 `AnalyzeLoadedBlueprint()` 调用前后 `ParentClass`、`NewVariables[*].VarType`、Graph pin 的 `PinSubCategoryObject` 和 package dirty 状态都不变；再用 end-to-end full reload 测试验证 commit lane 仍能完成替换。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintReplacementProbe.*`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 纯 probe 的命中结果如果和真实 replacement archive 不完全一致，首版可能出现“分析说无影响、提交时才发现有引用”的差异；建议先在 diagnostics 中双跑比对，再切默认路径。 |
| 兼容性 | 向后兼容。首版只改变内部执行顺序，把隐式副作用显式化；最终 replacement 结果保持不变即可不影响现有用户脚本。 |
| 验证方式 | 1. 单测 `AnalyzeLoadedBlueprint()` 前后 Blueprint 对象图不发生变化，也不会被标记 dirty。<br>2. full reload 集成测试验证 replacement 仍在 commit 阶段生效，且 abort 场景不会留下半替换状态。<br>3. 增加双跑诊断，对比 probe 命中数与旧 `FArchiveReplaceObjectRef` 命中数，确认迁移窗口可控。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-52 | `BlueprintImpact` 分析阶段隐式执行 object replacement | 事务边界收敛 + 分析/提交解耦 | 高 |
| P1 | Arch-HR-51 | base class `SoftReload` 对 Blueprint descendants 的级联重建 | 继承链增量化 + lazy descendant commit | 中高 |

---

## 架构分析 (2026-04-10 00:47)

### Arch-HR-53：`bNetValidate` / `_Validate` 的 RPC 合同没有进入 soft reload 判定，`SoftReloadOnly` 可换入新模块但继续沿用旧网络壳

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | RPC `_Validate` 合同与增量热重载的安全边界 |
| 当前设计 | 当前函数差异判定会比较 `bNetFunction / bNetMulticast / bNetClient / bNetServer` 等网络标志，但没有把 `bNetValidate` 纳入 `IsDefinitionEquivalent()`。与此同时，新增方法只会把类抬到 `FullReloadSuggested`；在 PIE 的 `SoftReloadOnly` 路径上，`FullReloadSuggested` 仍然会 `SwapInModules()` 并执行 `PerformSoftReload()`。而 soft path 只复用旧 `UASFunction`，更新 `ScriptFunction` 和参数类型，不会重写 `FUNC_NetValidate`、不会刷新 `ValidateFunction` 缓存，也不会重新跑 `SetUpRuntimeReplicationData()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1032-1053`：`FAngelscriptFunctionDesc::IsDefinitionEquivalent()` 比较了多种函数标志，但没有比较 `bNetValidate`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1019-1048`：分析阶段只验证“如果当前方法声明了 `bNetValidate`，是否存在 `_Validate` 且签名正确”，没有把“是否新增/取消 validate 合同”做成独立 reload delta。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1264-1287`：新增方法只是 `FullReloadSuggested`；如果新增的是 `_Validate` 这种普通方法，并不会自动升级到 `FullReloadRequired`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3942-3965`：`FullReloadSuggested + SoftReloadOnly` 时仍会 `SwapInModules()` 并执行 `ClassGenerator.PerformSoftReload()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4260-4275,4795-4822`：soft path 只把旧 `UASFunction` 指向新的 `ScriptFunction`，并 soft reload 参数类型，不刷新函数 flags。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:126-127`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3485-3489,3676-3680`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1956-1959`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptComponent.cpp:46-49,89-103`：`ValidateFunction` 是缓存指针，只在 full class build 时填充，运行时直接读取。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2284-2294,5055-5065`：`FinalizeClass()` 和 `SetUpRuntimeReplicationData()` 只在 full reload finalize 阶段执行。 |
| 优点 | 当前实现把“body-only 逻辑更新”压到旧 `UASFunction` 壳里，普通 RPC 函数 body 改动的成本较低。 |
| 不足 | 推断上，只要开发者在 PIE 中给现有 RPC 新增 `_Validate` 或切换 `WithValidate`，本轮 session 就可能出现“脚本模块已换新、网络函数外壳仍是旧合同”的混合纪元：新 `_Validate` 方法已编进模块，但旧 `UFunction` 还没有 `FUNC_NetValidate`，`ValidateFunction` 也仍为空或仍指向旧目标。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载 authority 停留在 Lua module cache。`loaded_modules` / `package.loaded` 是显式活动模块表，`reload_modules()` 先基于模块名集合装载/比较，再由 `update_modules()` 回写旧模块对象；它并不在 hot reload 中原地改写 UE 侧的 RPC `UFunction` 壳。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:13-18`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-169`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-600` | 把热更新边界收敛在 runtime module graph，避免进入“脚本实现已换新，但旧 `UFunction` 网络合同没跟上”的半更新状态。 |
| puerts | 热更新以已加载 source 为输入，编辑器 watcher 只跟踪已加载文件；JS 侧 `__reload` 以 `url -> scriptId` 为键调用 `Debugger.setScriptSource`，类级行为改动再通过 `NeedReBind / NotifyRebind()` 延迟重绑。可推断其热更边界仍在 source/class rebind 层，而不是在已有 `UFunction` 壳上局部补网络 flags。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99` | 如果当前 Angelscript 继续保留 `UASFunction` 壳复用路线，就必须把“哪些函数合同绝不能 soft patch”明确编码，而不是默认让 `FullReloadSuggested` 在 PIE 中继续 swap-in。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `_Validate` / `bNetValidate` 提升成独立的 `NetworkContractDelta`，先禁止不安全的 soft swap，再评估是否需要更细的 runtime 合同刷新。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 的 `FAngelscriptFunctionDesc::IsDefinitionEquivalent()` 中补上 `bNetValidate`，避免 validate 合同切换被误判成普通 body change。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 的分析阶段新增 `NetworkContractDelta`：凡是 RPC 的 `_Validate` 新增、移除、rename，或 `bNetValidate` 开关变化，都直接升级到 `FullReloadRequired`；至少在 `SoftReloadOnly` 下禁止 `SwapInModules()`。<br>3. 若后续确实要支持安全增量路径，再新增 `RefreshNetFunctionRuntimeContract(UASFunction* ExistingFunction, UASClass* Owner)`：显式同步 `FUNC_NetValidate`、刷新 `ValidateFunction = Owner->FindFunctionByName(...)`，并在严格白名单下重新执行 `SetUpRuntimeReplicationData()`。<br>4. 扩展 `FAngelscriptEngine::PerformHotReload()` 的日志与 diagnostics：当本轮因为 `NetworkContractDelta` 被挡回 full reload 时，输出具体函数名，而不要继续使用泛化的 “UPROPERTY/UFUNCTION 变化” 文案。<br>5. 增加自动化测试：覆盖“给现有 `Server` RPC 新增 `_Validate`”“移除 `_Validate`”“仅修改 `_Validate` body”“PIE 下 `SoftReloadOnly` 遇到 `NetworkContractDelta` 不得 swap-in” 四类场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果未来尝试在现有 `UASFunction` 上动态补齐 `FUNC_NetValidate` 与 replication data，最容易踩到 UE net cache / RPC dispatch 的隐含前置条件；因此第一阶段应先做“分类更严格 + 禁止不安全 soft swap”，不要一开始就追求在线修补。 |
| 兼容性 | 向后兼容。第一阶段只是把原先会在 PIE 中偷偷换入的新脚本，改成更明确地要求 full reload；对脚本 API 没有破坏，但会让一部分网络函数变更更早暴露为“不能 soft reload”。 |
| 验证方式 | 1. 在 `SoftReloadOnly` 下给现有 RPC 新增 `_Validate`，确认旧模块不会被换出，且日志指出具体函数。<br>2. 在 full reload 路径下新增 `_Validate`，确认 `FUNC_NetValidate`、`ValidateFunction` 与运行时 RPC 执行都切到新合同。<br>3. 仅修改 `_Validate` body 时验证仍可沿用现有 `UASFunction` 壳，只更新 `ScriptFunction`。 |

### Arch-HR-54：脚本类删除没有进入 reload delta，Editor repair 链只能看到 replacement pair，看不到“旧类已消失”的 tombstone

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | class removal 对 Editor repair / 资产修补 / 外部观察者的可见性 |
| 当前设计 | 当前热重载会把被删除的脚本类记录进 `RemovedClasses`，但 full reload 广播阶段只对 `ModuleData.Classes` 中的 old/new pair 触发 `OnClassReload`。随后 `OnFullReload` 立即驱动 `ClassReloadHelper::PerformReinstance()`；直到这个 repair pass 结束后，才遍历 `RemovedClasses` 调 `CleanupRemovedClass()`，把旧类的 `ScriptTypePtr`、构造函数和默认函数清空并隐藏类。也就是说，“类被删掉了”不是一个显式 delta，而是 repair 之后才发生的后置清理副作用。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1864`：分析阶段把不再存在的旧类放进 `ModuleData.RemovedClasses`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2326-2368`：`OnClassReload` 只遍历 `ModuleData.Classes` 的 old/new pair，没有处理 `RemovedClasses`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2372-2389`：先 `OnFullReload.Broadcast()` 驱动 Editor repair，之后才对 `RemovedClasses` 执行 `CleanupRemovedClass()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2651-2655`：`FullReloadRemoveClass()` 对 removed class 只做 subsystem deactivate。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5006-5039`：`CleanupRemovedClass()` 的核心是把旧类变成无 `ScriptTypePtr`、无 `ConstructFunction`、无 `DefaultsFunction` 的隐藏壳。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:59-66,132-137`：Editor helper 只从 `OnClassReload` 收集 `ReloadClasses`，并在 `OnFullReload` 上执行 repair。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:49-54,83-112,181-188`：`PerformReinstance()` 用 `ReloadClasses` 构建 replacement map、`ImpactSymbols` 和 `ReparentHierarchies(ReloadClasses)`；没有单独的 removed-class 输入面。 |
| 优点 | 当前实现非常保守，不会在 class deletion 时擅自给 Blueprint/资产做自动 reparent，避免错误迁移。 |
| 不足 | 推断上，任何把已删除脚本类当成 Blueprint parent、property type、Graph dependency 或 open editor asset 引用的对象，都不会在本轮 repair 里收到“它已经不存在”的显式信号；它们只会在 repair 之后继续指向一个被隐藏、脚本能力被清空的旧壳，问题更晚、也更难诊断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `loaded_modules` / `package.loaded` 本身就是热重载的活动模块账本。`require()` 命中与否、`reload_modules(module_names)` 处理哪些模块，都是显式按模块名集合决定的；模块是否参与 reload 是输入状态，而不是某个 repair pass 之后才把旧壳静默清空。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:13-18`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-169`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-625` | 对当前 Angelscript，可借鉴“删除/缺失本身也是一等 delta”的建模方式，不要只把 replacement pair 当成 hot reload 输入。 |
| puerts | watcher 只跟踪已经加载过的 source file，JS 热更以 `url -> scriptId` 的显式映射执行 `Debugger.setScriptSource`。可推断其活动 source 集在 watcher 和 runtime 映射里都是显式集合，而不是依赖某个 Editor replacement pass 事后发现“旧类不见了”。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-98` | 当前 Angelscript 如果继续走 `UClass` repair 路线，就更需要补一条 `RemovedClass` tombstone lane，让工具链在 repair 前就知道“这是删除，不是替换”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增 `RemovedClass` tombstone delta，把“类被删掉了”从后置清理副作用提升成 full reload session 的显式输入。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 增加 `OnClassRemoved(UClass* OldClass)` 或 session-level `RemovedClasses` delta，并在 `OnFullReload` 之前发布；不要先做 `CleanupRemovedClass()` 再让 Editor 猜。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h/.cpp` 为 `FReloadState` 增加 `RemovedClasses`，并新增 `CollectRemovedClassImpact()`：显式扫描 loaded Blueprint parent、`HasExternalDependencies()`、pin type、变量类型和 open asset editors 中对 removed class 的引用。<br>3. 维持当前 `ReloadClasses` 只承载 old->new replacement pair，不把 `nullptr` 塞进现有 `ClassReplaceList` / `ReparentHierarchies()`；删除场景走独立 tombstone lane，首版只做 warning + compile error，不做自动 reparent。<br>4. 把 `CleanupRemovedClass()` 挪到 Editor/repair 消费 tombstone 之后，再执行隐藏与脚本指针清空；同时把仍引用 removed class 的资产写入结构化 diagnostics。<br>5. 增加自动化测试：覆盖“删除被 Blueprint 继承的脚本类”“删除被 Graph pin / property type 使用的脚本类”“删除 open asset editor 正在编辑的脚本类”三类场景，验证系统能在本轮 reload 中报出明确 tombstone 诊断。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public\BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadBlueprintImpactTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 删除类后的自动修补如果做得过激，最容易把原本应当暴露给开发者处理的丢失依赖 silently reparent 掉；因此首版应坚持 tombstone diagnostics 优先、自动迁移保守。 |
| 兼容性 | 向后兼容。第一阶段只新增 `RemovedClass` delta、告警和测试，不改变现有删除后的隐藏壳清理语义；后续是否引入 rename/迁移清单可再逐步扩展。 |
| 验证方式 | 1. 删除一个被 loaded Blueprint 继承的脚本类，确认本轮 full reload 就能给出明确 tombstone 报错，而不是等到后续随机 compile 失败。<br>2. 删除被变量或 pin 类型引用的脚本类，确认 `BlueprintImpact` 会把该引用列入 diagnostics。<br>3. 回归普通 old->new replacement 场景，确认新增 `RemovedClasses` lane 不会破坏现有 `ReparentHierarchies(ReloadClasses)` 行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-HR-53 | `bNetValidate` / `_Validate` 的 RPC 合同在 `SoftReloadOnly` 下可能发生半更新 | 判定收紧 + 网络合同显式化 | 高 |
| P1 | Arch-HR-54 | 脚本类删除没有进入 reload delta，Editor repair 看不到 tombstone | delta 协议补齐 + 删除诊断 | 中高 |

---

## 架构分析 (2026-04-10 00:56)

### Arch-HR-55：`PostInitFunctions` 的契约与执行时序脱节，literal asset 初始化被插在 `CDO` 建立之前

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块级 post-init 钩子与状态提交时序 |
| 当前设计 | `FAngelscriptModuleDesc::PostInitFunctions` 的注释声明它应在 class `CDO` 建立后执行，但实际 hot reload 流程先跑 `CallPostInitFunctions()`，后跑 `InitDefaultObjects()`。对于 `asset ... of ...` 语法，预处理器会生成 `Get{Name}()` getter，并把它加入 `PostInitFunctions`，于是 literal asset 创建与 `__Init_{Name}` / `__PostLiteralAssetSetup` 会在 `CDO`、Editor repair、`OnPostReload` 之前执行。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1305-1306`：`PostInitFunctions` 注释写的是 “after CDOs for classes are created”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4109-4133`：`asset` 语法被展开为 `Get{Name}()`，内部调用 `__CreateLiteralAsset`、`__Init_{Name}`、`__PostLiteralAssetSetup`，并把 getter 名加入 `PostInitFunctions`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2303-2304,2373,2395`：实际顺序是 `CallPostInitFunctions()` → `InitDefaultObjects()` → `OnFullReload.Broadcast()` → `OnPostReload.Broadcast(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5791-5818`：`CallPostInitFunctions()` 会遍历本轮模块的 `PostInitFunctions` 并直接 `Context->Execute()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:607-686`：literal asset getter 会创建/替换对象，必要时 rename 旧对象并广播 `OnLiteralAssetReload`。 |
| 优点 | 把 literal asset 创建集中到统一 post-compile 阶段，避免脚本首次访问 getter 时才延迟 materialize。 |
| 不足 | 当前实现把“用户级初始化副作用”插进了一个尚未完成 `CDO` 与 Editor repair 的中间阶段。推断上，只要 `__Init_{Name}` 读取重载类的默认值、缓存 class 引用，或依赖 `OnFullReload` 之后的 replacement 结果，就可能看到混合纪元状态；同一模块里哪怕只是普通函数 body 变化，也会重新进入这些 getter。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把热更生命周期显式建模为 hook + module merge：先通过 `call_hook()` 触发可选回调，再在 `update_modules()` 中合并新旧 module，最后统一 `update_global()` 修补全局图。没有额外的“隐式 module post-init 列表”插进宿主提交中段。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:20-26`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:101-109`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:565-600` | 把“用户可观察的初始化/回调”做成显式 phase，而不是隐藏在 reload 主链中段。 |
| puerts | 监听边界只来自已加载 source；真正的代码替换由 `ReloadSource()` 调 JS 侧 `__reload`，并用 `HMR.prepare` / `HMR.finish` 包住 `Debugger.setScriptSource`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-147`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90` | 即使需要用户侧配合，也通过显式生命周期事件暴露，不把副作用混进半提交状态。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `PostInitFunctions` 从“隐式早期执行”改成“显式 late-init phase”，并只对真正需要 replay 的模块运行。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 给 `PostInitFunctions` 增加 phase 语义，例如升级为 `FModuleInitHook { Name, Phase, bReplayOnReload }`；首版只需要 `AfterCDOInit`。<br>2. 调整 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 的顺序：先 `InitDefaultObjects()`，再执行 `CallPostInitFunctions(AfterCDOInit)`，最后再进入 `OnFullReload` / `OnPostReload` 广播，确保 literal asset init 读取到的是本轮新 `CDO`。<br>3. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` 把 literal asset getter 注册成显式 `AfterCDOInit` hook，并额外记录一个轻量 hash；若本轮模块没有 asset declaration / asset init 相关 delta，就跳过 replay。<br>4. 为避免一次性破坏旧脚本，增加 `angelscript.PostInitAfterCDO` 开关；默认可先 `warn-only` 打印“当前顺序与注释不一致”，确认项目脚本没有依赖旧顺序后再切默认。<br>5. 补自动化：覆盖“asset init 读取 class default”“body-only 改动不应重复 replay asset getter”“full reload 后 asset replacement 仍能进入 `ReloadAssets` 收集”三类场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 少量现有脚本如果“恰好依赖旧的早期顺序”，切换 phase 后可能暴露顺序耦合；因此需要先通过开关和诊断过渡，而不是直接硬切。 |
| 兼容性 | 向后兼容。首阶段保留旧路径和开关，只增加新 phase 与诊断；稳定后再把 literal asset 默认迁到 `AfterCDOInit`。 |
| 验证方式 | 1. 构造 literal asset 在 `__Init_{Name}` 中读取重载 class `CDO` 的测试，确认拿到的是新默认值。<br>2. 对同模块普通 body-only 改动做 hot reload，确认 asset getter 不会无条件 replay。<br>3. 回归 literal asset replacement 场景，确认 `OnLiteralAssetReload` 仍在同一 reload session 内被 Editor helper 收到。 |

### Arch-HR-56：`Tick/ReceiveTick` 的 host tick 合同没有进入 soft reload 判定，脚本已换新而 `PrimaryTick` 仍可能停留在旧 epoch

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 增量热重载对 tick capability 的覆盖边界 |
| 当前设计 | `Tick`/`ReceiveTick` 是否让类进入 tick，取决于 `InitClassTickSettings()` 对方法存在性和 `bIsNoOp` 的判断；但 `bIsNoOp` 不在 `IsDefinitionEquivalent()` 里，分析阶段也没有独立的 `TickContractDelta`。结果是：在 `SoftReloadOnly` 下，tick 相关脚本函数可以换成新 `ScriptFunction`，而 class 级 `bCanEverTick / bStartWithTickEnabled` 与实例 `PrimaryActorTick / PrimaryComponentTick` 不会同步刷新。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1017-1053`：`FAngelscriptFunctionDesc` 记录了 `bIsNoOp`，但 `IsDefinitionEquivalent()` 并不比较它。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1196-1287`：分析阶段依赖 `IsDefinitionEquivalent()` 决定是否 `FullReloadRequired`，新增方法默认只是 `FullReloadSuggested`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5713-5789,5837-5855`：`InitClassTickSettings()` 会根据 `Tick` / `ReceiveTick` 是否存在且 `!bIsNoOp` 来设置 `bCanEverTick / bStartWithTickEnabled`，但只在 `ShouldFullReload(ClassData)` 的类上运行。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4260-4285`：`DoSoftReload()` 在 `bIsNoOp` 改变时只更新 `FUNCMETA_ScriptNoOp` metadata，并不会重算 tick flags。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1370-1372,1422-1425`：实例构造时才把 `Class->bCanEverTick / bStartWithTickEnabled` 写入 `PrimaryActorTick / PrimaryComponentTick`。 |
| 优点 | 对纯 tick body 改动很便宜，旧 `UASFunction` 壳可直接切到新脚本实现。 |
| 不足 | tick 是否被调度本质上是 host-side contract，不只是脚本 body。推断上，在 PIE `SoftReloadOnly` 中给原本不 tick 的类新增 `Tick`、把 `Tick` 从 no-op 改成非 no-op，或反向移除/清空 tick 逻辑，都会出现“脚本函数已更新，但宿主调度位仍是旧值”的混合纪元。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热更的主语是 Lua module value graph。`update_modules()` 会把新函数放回旧 module table，`update_global()` 继续修 running stack、`_G`、registry 与 upvalue；没有额外一层独立的 host tick flag 需要同步。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:367-477`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-547` | 当行为切换只存在于脚本函数图时，直接在函数图里 patch；若宿主还维护镜像 contract，就必须单独建 rebinding phase。 |
| puerts | 源码热更显式发 `HMR.prepare` / `HMR.finish`，而 class 侧存在 `NeedReBind` / `NotifyReBind()`，需要重新注入时不会假装成“普通函数 body patch”。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99` | 把“脚本逻辑更新”与“宿主类合同重绑”分开建模，避免 host scheduler 与脚本实现脱节。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `Tick/ReceiveTick` 派生出的调度合同提升为独立 delta；第一阶段先禁止不安全 soft patch，第二阶段再补可选的 tick rebinding。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 的分析阶段新增 `TickContractDelta`：凡是 `Tick` / `ReceiveTick` 新增、移除，或 `bIsNoOp` 在这两个方法上发生变化，都至少升级到 `FullReloadSuggested`；对当前 `bCanEverTick == false` 且准备变为 true 的类，建议直接升到 `FullReloadRequired`。<br>2. 抽出 `EvaluateTickSettings(const FAngelscriptClassDesc&, UClass* SuperClass)` 纯函数，供 `InitClassTickSettings()` 与未来 soft path 共用，避免同一套规则只存在 full reload 路径。<br>3. 若后续要支持安全增量路径，再在 `DoSoftReload()` 末尾新增 `RefreshLiveTickContract(UASClass*)`：同步 `UASClass::bCanEverTick / bStartWithTickEnabled`，并对白名单实例刷新 `PrimaryActorTick / PrimaryComponentTick`；任何注册/反注册风险不明确的场景仍回退 full reload。<br>4. 在 `FAngelscriptEngine::PerformHotReload()` 的 diagnostics 中为 `TickContractDelta` 打专门原因，不再把它埋进泛化的 `UFUNCTION` / metadata 提示。<br>5. 补测试：覆盖“新增首个 `Tick`”“`Tick` 从 no-op 切为有效实现”“从有效实现切回 no-op”“PIE `SoftReloadOnly` 遇到 `TickContractDelta` 不得静默 swap-in” 四类场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果直接在 soft path 上改 live tick registration，最容易触发 UE tick manager 的边界问题；因此首版应优先做“分类更严格”，而不是立即承诺在线 rebinding。 |
| 兼容性 | 向后兼容。第一阶段只是把原先会被当成普通 soft patch 的 tick 合同变化更早暴露为需要 full reload；不会破坏脚本 API，但会让部分迭代场景更早拿到明确提示。 |
| 验证方式 | 1. 在 PIE `SoftReloadOnly` 中给原本不 tick 的脚本类新增 `Tick`，确认系统要求 full reload 或明确拒绝 swap-in。<br>2. 仅修改已有 `Tick` body 时验证仍保留当前轻量路径。<br>3. 对 no-op/non-no-op 切换做回归，确认 class tick flags 与实例 `PrimaryTick` 不会长期停留在旧 epoch。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-55 | `PostInitFunctions` / literal asset 初始化时序与 `CDO` 提交错位 | phase contract 修正 + 增量 replay | 高 |
| P1 | Arch-HR-56 | `Tick/ReceiveTick` 的 host tick 合同未进入 soft reload 判定 | 判定收紧 + 可选 runtime rebinding | 中高 |

---

## 架构分析 (2026-04-10 01:06)

### Arch-HR-57：`ImplementedInterfaces` 被硬编码成 full reload barrier，interface class 上的 body-only 改动也失去增量路径

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | interface 实现类的增量热重载边界 |
| 当前设计 | 当前 `ShouldFullReload()` 只要发现类实现了任意 interface，就无条件返回 `true`。与此同时，接口装配、递归补父 interface、以及“实现类是否补齐 required method”的校验全部都集中在 `FinalizeClass()`；`DoSoftReload()` 并没有对应的 interface refresh lane。结果是：哪怕只是改了一个 interface 实现方法的 body，类也会被强制走 full reload。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2081-2088`：`Class.NewClass->ImplementedInterfaces.Num() > 0` 直接触发 `ShouldFullReload()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5055-5202`：`FinalizeClass()` 里才会解析 `ImplementedInterfaces`、递归写入 `NewClass->Interfaces`、并校验 interface required method。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4129-4300`：`DoSoftReload()` 只重链 property、重绑 `ScriptFunction`、重建实例/`CDO`，没有任何 `Interfaces` 或 interface method contract 的刷新逻辑。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4040-4077`：soft link 阶段对 interface class 本身也只更新 static class/global pointer，不做 interface graph rebuild。 |
| 优点 | 设计非常保守，避免了 `UClass::Interfaces`、interface stub 和 required method 校验在半更新状态下失配。 |
| 不足 | 增量能力被 interface 使用面整体封死。凡是实现了 interface 的常见 gameplay class，都无法享受 body-only 的轻量热更；不仅迭代成本更高，也会额外放大 full reload 带来的状态丢失面。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热更边界是 modified module set。`reload_modules()` 只对目标 module 重新加载，`update_modules()` 会把新函数并回旧 module table，并把缺失键补进旧对象，而不是因为某个 module 承担了“接口角色”就整体禁用增量路径。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-600`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:511-520` | 可借鉴“先按 contract delta 分类，再决定是否需要结构性 rebuild”，不要把“实现了 interface”本身当作 full reload 的永久门槛。 |
| puerts | puerts 把 class-side repair 明确建成 `NeedReBind / NotifyReBind()`。调用点会在需要时对命中的 `UTypeScriptGeneratedClass` / super chain 触发 rebind，而不是用一个“类只要带某种宿主合同就永远不能走轻量路径”的全局禁令。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:25-38`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2365` | 可借鉴“接口/宿主合同变化 -> 专门 rebind lane”的建模方式，让 interface topology 变化和 body-only 变化分流。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“类实现了 interface”从静态 hard barrier 改成显式 `InterfaceContractDelta` 判定；只有 interface topology 或 required method contract 变化时才强制 full reload。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 为 `FClassData` 新增 `EInterfaceContractDelta`，至少区分 `None`、`InterfaceSetChanged`、`RequiredMethodSignatureChanged`、`BodyOnlyOnImplementation`。<br>2. 在分析阶段比较旧/新 `ImplementedInterfaces` 集合，以及各 required method 的 signature；仅当 interface 集合变化、interface 新增/移除、或实现方法不再满足 interface contract 时，才把类抬到 `FullReloadRequired`。<br>3. 把 `FinalizeClass()` 中与 interface 相关的逻辑抽成 `RefreshImplementedInterfaces(UClass*, const FAngelscriptClassDesc&, EInterfaceRefreshMode)`；先支持 full reload 复用，后续再为 topology 未变场景提供 `ValidateOnly` 或 `RebindOnly` 路径。<br>4. 修改 `ShouldFullReload()`：去掉 `ImplementedInterfaces.Num() > 0` 的无条件返回，改为读取 `InterfaceContractDelta`。第一阶段可放在 `CVar` 后面，只对白名单项目开启。<br>5. 补自动化测试：覆盖“实现 interface 的类只改 body”“新增/移除 implemented interface”“改坏 required method signature”“interface 继承链变化”四类场景，验证只有真正的 contract 变化才触发 full reload。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Interface/` |
| 预估工作量 | M |
| 架构风险 | 误把 interface contract 变化判成 body-only，会留下过期的 `UClass::Interfaces` 或缺失 required method 的实现；首版必须保守，只在 topology 和 signature 都确认不变时放行轻量路径。 |
| 兼容性 | 向后兼容。第一阶段可以通过 `CVar` opt-in，仅新增更细的 delta 判定和诊断；关闭开关时继续沿用当前“interface class 一律 full reload”的行为。 |
| 验证方式 | 1. 对实现 interface 的脚本类仅修改方法 body，确认允许走轻量路径且实例状态不被 full reload 打断。<br>2. 新增或移除 implemented interface，确认系统仍强制 full reload。<br>3. 故意破坏 required method signature，确认 compile diagnostics 与 reload gating 都能给出明确原因。 |

### Arch-HR-58：新增普通方法在 `SoftReloadOnly` 下会进入新 script module，但不会物化进现有 `UClass` / `UFunction` 壳

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 新增方法的增量热重载可见性 |
| 当前设计 | 当前分析阶段对“新增方法”默认只给 `FullReloadSuggested`；如果处于 PIE 的 `SoftReloadOnly`，engine 仍会 `SwapInModules()` 并执行 `PerformSoftReload()`。但 soft path 只会重绑旧方法对应的 `UASFunction`，对 `OldClass` 中不存在的方法直接跳过；新的 `UFunction` 分配、`Children` 链接和 `FunctionMap` 注册只发生在 full reload 的建类路径。结果是：新方法已经进入新 epoch 的 script module，但现有 `UClass` 仍看不到它。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1264-1287`：新增方法默认只是 `FullReloadSuggested`，只有新增 `BlueprintEvent` 才升级到 `FullReloadRequired`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3942-3964`：`FullReloadSuggested + SoftReloadOnly` 时仍会 warning 后 `SwapInModules()` 并执行 `ClassGenerator.PerformSoftReload()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4261-4267`：`DoSoftReload()` 遍历新方法时，如果 `OldFuncDesc` 不存在就直接 `continue`，不会为新增方法 materialize 新的 `UFunction`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3426-3640`：`UASFunction::AllocateFunctionFor()`、`AddFunctionToFunctionMap()`、`StaticLink(true)` 和 `FinalizeArguments()` 只在建类/full reload 路径里执行。 |
| 优点 | 允许开发者在 PIE 中继续拿到 body 变更和 script module 更新，而不是因为新增一个普通 helper 方法就彻底阻塞整轮热更。 |
| 不足 | 当前是显式的双纪元：新方法已经存在于新 script module，但旧 `UClass` 的反射面、`FindFunctionByName()`、Editor 菜单和 Blueprint 节点仍看不到它。更关键的是，系统没有区分“仅 script 内部 helper”与“对宿主可见的新方法”，全部都落入同一个 `FullReloadSuggested` 灰区。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `update_modules()` 在 hot reload 时会把 `new_module` 中的函数直接回写到 `old_module`，而且当旧 module 中不存在该键时会补进去。换句话说，新函数会立即出现在旧 module object graph 上，而不是进入一个“脚本里有、宿主对象里没有”的中间态。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:511-520`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-600` | 可借鉴“新增 function 也是一等 delta”的处理方式，至少把 script-only 新方法和需要宿主反射的新方法分开。 |
| puerts | puerts 的 source hot reload 通过 `ReloadSource()` 调 `__reload`，并用 `HMR.prepare / HMR.finish` 把一次 source patch 变成显式生命周期；当 class-side 需要同步时，再通过 `NeedReBind / NotifyReBind()` 进入独立 rebind lane，而不是默默接受“source 已有新方法但 host class 未更新”。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1516-1538`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99` | 可借鉴“新增 API -> 明确 rebind 或明确延后”的协议，而不是继续用单个 `FullReloadSuggested` 混合 script 可见性和 host 可见性。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为“新增方法”建立 `MethodAdditionDelta` 分流，先把 script-only helper 与宿主可见 API 区分开；再决定是允许轻量补入，还是明确要求 full reload。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 新增 `EMethodAdditionDelta`，至少区分 `ScriptLocalOnly`、`HiddenUFunctionOnly`、`HostVisible`、`BlueprintOrNetworkSurface`。<br>2. 在分析阶段给新增方法做 exposure 分类：例如 `bBlueprintCallable`、`bBlueprintEvent`、`bNet*`、`bExec`、interface required method 等直接进入 `HostVisible` 或更高等级；`private/protected` 且不参与宿主反射面的 helper 方法才允许落在 `ScriptLocalOnly`。<br>3. 第一阶段先收紧 gating：`HostVisible` 及以上在 `SoftReloadOnly` 下不再继续 `SwapInModules()`，而是直接输出精确 diagnostics，避免“新 API 已编进 module、旧 `UClass` 却看不到”的灰区。<br>4. 第二阶段若要提升增量能力，再增加 `AddSoftReloadFunction(UASClass*, TSharedPtr<FAngelscriptFunctionDesc>)`，复用 full reload 路径中的 `AllocateFunctionFor()`、`AddFunctionToFunctionMap()`、`StaticLink()`、`FinalizeArguments()`，只对白名单的新方法做 in-place 注入。<br>5. 补测试：覆盖“新增 private helper 方法”“新增 `BlueprintCallable` 方法”“新增 `Exec` 方法”“新增 interface required method”四类场景，验证分流、diagnostics 与可见性一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果过早支持 in-place 新增 `UFunction`，最容易踩到 `Children` 链、`FunctionMap`、Blueprint action cache 和网络/Editor flag 的同步边界；因此应先做“分类更明确 + 不安全场景禁止 soft swap”，再逐步开放白名单注入。 |
| 兼容性 | 向后兼容。第一阶段只是把当前模糊的 `FullReloadSuggested` 拆成更明确的 delta 与诊断；第二阶段的 in-place 注入也可以放在实验开关后面，不破坏现有脚本语法。 |
| 验证方式 | 1. 在 PIE `SoftReloadOnly` 中新增 private helper 方法，确认系统要么允许安全轻量路径，要么给出明确的 script-only 诊断。<br>2. 新增 `BlueprintCallable` 或 `Exec` 方法，确认系统不会再静默 swap-in 半更新 module。<br>3. 若启用 `AddSoftReloadFunction()` 实验路径，验证 `FindFunctionByName()`、Blueprint action database 与调用行为都能看到新增方法。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-57 | interface 实现类被无条件排除出增量热重载 | delta 分类 + 专用 rebind lane | 高 |
| P2 | Arch-HR-58 | 新增普通方法在 `SoftReloadOnly` 下进入 script module 但不进入 `UClass` | 可见性分流 + 白名单注入/更严格 gating | 中高 |

---

## 架构分析 (2026-04-10 01:15)

### Arch-HR-59：`SoftReloadOnly` 已允许 materialize 新的 class/struct/delegate，但结构事件总线仍是 `full-reload-only`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `SoftReloadOnly` 下的新类型可见性与 Editor repair 契约 |
| 当前设计 | 当前 reload 主链已经允许在非 full session 中创建全新的 class/struct/delegate：`ShouldFullReload()` 对 brand-new class/delegate 直接返回 `true`，reload 循环也会照常执行 `CreateFullReload*()`、`DoFullReload*()`、`FinalizeClass()`。但结构性广播几乎全部挂在 `if (bIsDoingFullReload)` 的收尾分支上，soft path 只发 `OnPostReload(false)`。结果是：runtime 里已经出现新的 `UClass/UStruct/UDelegateFunction`，Editor 侧的 `ClassReloadHelper` 却拿不到对应 delta，`NewClasses` 的 placement mode、volume factory、Blueprint action 刷新链也不会执行。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2089-2091`：brand-new class 即使不在 full session，也会因为 `!Class.OldClass.IsValid()` 被判定为 `ShouldFullReload()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2109-2111`：brand-new delegate 同样会在 soft session 里走 `ShouldFullReload()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2144-2294`：reload 主链先 `CreateFullReload*()`，再 `DoFullReload*()`，最后对 `ShouldFullReload(ClassData)` 的 class 调 `FinalizeClass()`，这里不要求 `bIsDoingFullReload == true`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2317-2395`：`OnClassReload`、`OnStructReload`、`OnFullReload` 只在 `bIsDoingFullReload` 分支里广播。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2466-2469`：soft path 收尾只有 `OnPostReload.Broadcast(false)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:52-175`：`ReloadState` 只从 `OnStructReload`、`OnClassReload`、`OnDelegateReload`、`OnEnum*`、`OnFullReload` 收集结构 delta，`OnPostReload` 只是消费已记录状态并立刻 reset。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:340-384`：`NewClasses` 的 volume factory 注册、placement mode 广播都在 `PerformReinstance()` 末尾，而 `PerformReinstance()` 仅由 `OnFullReload` 触发。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3885-3893`：对比之下，enum 创建/变化是在 `DoFullReload(EnumData)` 内直接广播，说明当前结构事件语义本身已经出现类型间不一致。 |
| 优点 | 当前实现给了 soft session 更强的前进能力，新类型不必一律阻塞到下一次 full reload 才能在 runtime 内存在。 |
| 不足 | 结构性 side effect 被拆成“两套事实”：runtime 已经提交了新类型，Editor/工具链却仍认为本轮没有结构变化。推断上，新增 volume class、可放置 actor class、delegate signature、struct row type 在 PIE `SoftReloadOnly` 下都可能进入“对象已创建但面板/节点/工厂未刷新”的灰区。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热重载边界始终停留在 loaded module 集合。`FLuaEnv::HotReload()` 只触发 `UnLua.HotReload()`；Lua 侧 `reload_modules()` 只对 `loaded_modules` 里的模块做重载，`update_modules()` 则把新函数与新键 merge 回旧 module table。它不会在一次“轻量热更”里悄悄生成新的 UE 反射对象，再把通知责任留给 Editor 自己猜。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-176`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-600` | 把“轻量热更的 authority”限定在已加载运行时单元；如果某类变更会产生新的 host artifact，就不要默默沿用轻量事件面。 |
| puerts | puerts 的 editor watcher 只跟踪已加载 source file，变更后直接 `ReloadSource()`；class-side 修补通过 `NeedReBind/NotifyReBind()` 对现有 `UTypeScriptGeneratedClass` 显式 rebind。它的 source patch 和 host class repair 都有明确 owner，不会在没有对应通知链的情况下偷偷 materialize 新的 Editor artifact。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-146`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2365` | 把“source patch”和“host contract repair”做成两条显式 lane；当没有 repair lane 时，宁可不承诺结构提交。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“新类型已 materialize”从 `full reload backend` 的副产品，提升成与 session 类型无关的显式结构 delta；若当前 session 无法安全消费，就直接禁止在 `SoftReloadOnly` 提交。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 新增 session-level `FStructuralReloadDelta`，统一记录 `Created/Replaced/Removed` 的 `Class/Struct/Delegate`，不要再让 `OnClassReload` 等只在 `bIsDoingFullReload` 分支里发布。<br>2. 第一阶段最保守的做法是给 `SoftReloadOnly` 加 gating：凡是本轮出现 `CreatedClass/CreatedStruct/CreatedDelegate`，直接返回精确 diagnostics，并把它们升级为必须等待 full reload 的 structural delta，避免“runtime 已建壳，Editor 没跟上”。<br>3. 若需要保留当前前进能力，再为 `ClassReloadHelper` 增加 `OnStructuralDeltaCommitted` 消费口，让 `NewClasses` 的 placement mode、volume factory、action database refresh 和 delegate/event-node refresh 能在 `OnPostReload(false)` 前拿到同一批 delta。<br>4. 补齐 `OnDelegateCreated` 或把现有 `OnDelegateReload` 升级成 `OldNullable/NewNonNull` 协议，避免 brand-new delegate 永远不进入 observer 面。<br>5. 旧 `OnClassReload/OnStructReload/OnDelegateReload/OnFullReload` 先保留为兼容事件，但由新的 session delta 派生，保证向后兼容；同时新增日志输出“本轮 soft session 是否包含 structural delta，是否被延后/提交”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/`、`Plugins/Angelscript/Source/AngelscriptTest/Delegate/` |
| 预估工作量 | M |
| 架构风险 | 如果直接让 soft session 发布结构 delta，但 Editor repair 仍不完整，最容易把问题从“静默漏刷新”变成“半刷新”。因此首版建议先做 gating 或 refresh-only 白名单，而不是一步到位开放所有新类型。 |
| 兼容性 | 向后兼容。旧 delegate 事件和 full reload 行为可以继续保留；新 session delta 先作为补充来源，或先在 `CVar` 下只做 diagnostics / gating。 |
| 验证方式 | 1. 在 PIE `SoftReloadOnly` 中新增脚本 class、script struct、delegate，验证系统要么明确拒绝并排队 full reload，要么 Editor 能同步刷新 placement/action/event 节点。<br>2. 为新增 volume class 加 editor automation，确认 soft session 后 `ActorFactoryVolume` 与 placement mode 同步可见。<br>3. 为新增 delegate signature 加 Blueprint 事件节点回归，确认不会再出现 runtime 已有新 delegate、Editor 却无刷新信号的灰区。 |

### Arch-HR-60：`ConfigName/CLASS_Config/CLASS_DefaultConfig` 没有进入热重载判定，soft path 会继续沿用旧的 config 身份

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类级 config 合同与状态保持的一致性 |
| 当前设计 | 当前 class diff 只把 `Meta` 变化和 `AreFlagsEqual()` 的结果降成 `FullReloadSuggested`，而 `AreFlagsEqual()` 本身根本不比较 `ConfigName`。full reload 路径会重建 `CLASS_Config`、`CLASS_DefaultConfig` 与 `ClassConfigName`；soft reload 路径只改一部分 `ClassFlags`，完全不触碰 config identity。结果是：脚本类的 `config=...`、`DefaultConfig`、相关 class meta 一旦在 `SoftReloadOnly` 下变动，新的 script epoch 已经生效，但 `UClass` 仍挂着旧的 config section/flags。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1111-1112`：`FAngelscriptClassDesc` 显式保存 `ConfigName`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1187-1198`：`AreFlagsEqual()` 只比较 `bAbstract/bTransient/bHideDropdown/bDefaultToInstanced/bEditInlineNew/bIsDeprecatedClass/bPlaceable/bIsInterface/ImplementedInterfaces`，不比较 `ConfigName`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1311-1322`：class `Meta` 变化与 `AreFlagsEqual()` 变化都只会把本轮抬到 `FullReloadSuggested`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3310-3324`：full reload 路径会根据 `ConfigName` 设置 `CLASS_Config`、`ClassConfigName`，并根据 `NAME_Class_DefaultConfig` 决定 `CLASS_DefaultConfig`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4224-4258`：soft reload 只同步 `NotPlaceable/Abstract/Transient/HideDropdown/DefaultToInstanced/EditInlineNew/Deprecated`，没有任何 `ClassConfigName` 或 `CLASS_Config/CLASS_DefaultConfig` 更新。 |
| 优点 | 当前实现把大多数 class shell 保留在原位，避免了无谓 reinstance；对纯函数 body 变更来说成本很低。 |
| 不足 | 这里丢的不是普通 property 值，而是“这个类默认该从哪个 config section 取值”的宿主合同。推断上，改 `config=Game` 到别的 section、增删 `DefaultConfig`、或切换相关 class meta 后，live class 与新脚本定义会长期处于不同 config epoch；后续创建的 `CDO`/实例、以及基于 class identity 的默认值判断，都可能继续沿用旧 section。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | UnLua 的热更 authority 只在 Lua module graph。`FLuaEnv::HotReload()` 只是把控制权交给 `UnLua.HotReload()`；`update_modules()` 更新的是 Lua table/function/upvalue/global，不会宣称自己顺手完成了 UE `UClass` config identity 的部分重写。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-547` | 当 host-side 合同无法安全原位更新时，边界要明确，不能让“轻量热更”看起来像已经覆盖到类级 identity。 |
| puerts | puerts 对宿主类行为的更新走显式 rebind：`NeedReBind/NotifyReBind()` 负责现有 `UTypeScriptGeneratedClass` 的重新注入，原生 C++ reload 则通过 `ReloadCompleteDelegate/OnHotReload()` 重建 `JsEnv`。也就是说，host contract 变化要么显式 rebind，要么显式 rebuild，不会只把脚本源切新、把 host class identity 留旧。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2365`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:424-438` | 把宿主合同变化从 body patch 中分流出来；凡是无法安全 in-place 更新的 host identity，都应升级为显式 rebind/rebuild。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把类级 config 身份单独建模为 `ConfigContractDelta`；第一阶段先禁止这类变更在 `SoftReloadOnly` 下静默提交，第二阶段再研究受控的 class identity refresh。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h/.cpp` 为 `FClassData` 增加 `EConfigContractDelta`，至少区分 `None`、`ConfigSectionChanged`、`ConfigFlagChanged`、`DefaultConfigMetaChanged`。<br>2. 在分析阶段直接比较 `ClassDesc->ConfigName`、`Meta.Contains(NAME_Class_DefaultConfig)` 与旧值；这类变化不要再混在泛化的 `MetaChanged` / `AreFlagsEqual` 里。<br>3. 第一阶段将任何 `ConfigContractDelta` 在 `SoftReloadOnly` 下升级为不可自动提交的 structural/host delta，至少输出精确 diagnostics，避免继续沿用旧 `ClassConfigName`。<br>4. 若后续确实要支持有限增量，再单独实现 `RefreshConfigIdentity(UASClass*)`，只更新 `CLASS_Config`、`CLASS_DefaultConfig`、`ClassConfigName` 与受影响 `CDO`，并放在实验开关后；没有这条 lane 前，不要默认承诺 soft patch。<br>5. 补状态审计：在 reload summary 中单独列出“哪些类发生了 config contract delta、当前是延后 full reload 还是实验性 refresh”，并加针对 `config=` 与 `DefaultConfig` 切换的自动化测试。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/` |
| 预估工作量 | S-M |
| 架构风险 | 如果直接尝试在 soft path 中原位改 `ClassConfigName` 与 config flags，最容易和现有 `CDO`/config cache 的生命周期打架。因此首版应先做分类更严格与 diagnostics，再决定是否开放实验性 refresh。 |
| 兼容性 | 向后兼容。第一阶段只新增 delta 分类和更严格的 gating/诊断；关闭新开关时仍可保留当前行为。 |
| 验证方式 | 1. 构造脚本类只改 `config=` 的用例，确认 `SoftReloadOnly` 不再静默沿用旧 `ClassConfigName`。<br>2. 切换 `DefaultConfig` meta，确认系统会给出明确的 `ConfigContractDelta` 原因，并要求 full reload 或进入实验性 refresh。<br>3. 若启用 `RefreshConfigIdentity()`，验证新 `CDO` 与后续实例确实从新 config section 读取，而不是继续落在旧 epoch。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-59 | `SoftReloadOnly` 提交新类型但不发布结构 delta | session 结构事件统一 + soft gating | 高 |
| P1 | Arch-HR-60 | 类级 config 身份未进入热重载判定 | host contract 分类 + 更严格 gating | 中高 |

---

## 架构分析 (2026-04-10 01:29)

### Arch-HR-61：`UASStruct::GetNewestVersion()` 已存在，但 struct 版本链没有进入 runtime/editor 的统一 canonicalization

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | script struct full reload 后的类型身份统一 |
| 当前设计 | 当前 full reload 已经为 `UASStruct` 建好了 `NewerVersion` 链，并保留旧 struct 的 `Guid`；但后续修补仍主要依赖本轮 `ReloadStructs` 对 loaded Blueprint / `UDataTable` 做定向替换，而不是提供一个全局的 “old struct -> latest struct” canonicalization 层。基于源码检索，`GetNewestVersion()` 在仓内只看到定义和 struct hot reload test 使用，未见 runtime/editor 业务侧消费。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h:14-30`：`UASStruct` 定义了 `NewerVersion` 与 `GetNewestVersion()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2668-2688`：full reload 创建新 struct 时会 rename 旧 struct，并沿用旧 `Guid`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3218-3246`：提交阶段把 `ReplacedStruct->NewerVersion = NewStruct`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:108-163`：当前 struct repair 主要是 loaded Blueprint pin / variable 与 loaded `UDataTable::RowStruct` 的本轮替换。<br>`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptStructHotReloadTests.cpp:138-176`：测试验证了 struct 版本链可以解析到最新版本。 |
| 优点 | 旧 struct 不会被立刻销毁，理论上已经具备“延迟 canonicalize 到最新版本”的基础设施；保留 `Guid` 也有利于 UE 侧把多个版本视为同一 script struct 演化链。 |
| 不足 | 当前 self-heal 能力停留在 batch-local repair。推断上，只要某个 `UScriptStruct*` 句柄不在 loaded Blueprint / loaded `UDataTable` / open editor 这批显式修补面里，例如 runtime cache、自定义 editor 工具、项目级 registry、延迟加载资产元数据，它就没有类似 `UASClass::GetMostUpToDateClass()` 的统一补救通道，只能继续持有旧 struct 壳。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 热更 owner 在 module/object graph。`reload_modules()` 构造 old/new module 集，`update_modules()` / `merge_objects()` / `update_global()` 在原有 graph 上补丁，不引入一层需要额外 canonicalize 的 `UScriptStruct` 版本壳。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-547`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-623` | 如果已经选择了“宿主反射壳可换代”的路线，就要补一个和 module graph patch 等价的统一 canonicalization 服务，而不是只靠 loaded repair。 |
| puerts | source hot reload 用 `parsedScript` 维护 `url <-> scriptId`，并在 `Debugger.setScriptSource` 前后显式走 `HMR.prepare/HMR.finish`；class-side 再通过 `NotifyReBind()` / `MakeSureInject()` 重建宿主契约。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:13-23`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private\TypeScriptGeneratedClass.cpp:77-99`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2365` | 把“旧身份如何追到新身份”收敛成显式 registry / rebind owner，而不是把 old/new pair 只留在一次 batch repair 里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `NewerVersion` 链之上补一层统一的 struct canonicalization helper，让 loaded repair 与 runtime/editor 裸指针消费共享同一入口。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h` 或新建 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptTypeCanonicalizer.h/.cpp` 增加 `ResolveLatestASScriptStruct(UScriptStruct*)`，对非 `UASStruct` 直接 passthrough，对旧 script struct 统一追 `GetNewestVersion()`。<br>2. 第一阶段只接入当前最容易漂移的消费边界：`ClassReloadHelper` 的 pin/type 修补、`UDataTable::RowStruct` 替换、`BlueprintImpact` 的 struct 比对，以及任何 script-visible struct handle helper；不要改动 class generator 主流程。<br>3. 第二阶段再把 runtime 里的 struct handle 入口也接进来，例如 type usage / bind helper / dump 诊断，确保“拿到 struct 指针”默认先 canonicalize。<br>4. 为避免静默改变行为，首版在命中旧 `UASStruct` 时输出 `warn-only` 诊断，统计哪些路径仍在消费 replaced struct；等项目验证后再把部分路径切成默认 canonicalize。<br>5. 保留现有 `ReloadStructs` batch map 与 loaded repair，不做大爆炸替换；新 canonicalizer 只作为更底层的兜底层，逐步吸收 call-site 上分散的旧/new struct 修补逻辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptTypeCanonicalizer.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptTypeCanonicalizer.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptStructHotReloadTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果把 canonicalization 直接做成“全局无条件替换”，最容易掩盖仍然引用旧 struct 的真实 owner，导致问题从“显式 stale handle”变成“静默自动跳转”；因此首版应先 diagnostics + 白名单接入。 |
| 兼容性 | 向后兼容。首阶段只是新增 helper 和 warn-only 观测，不改变脚本语法；已存在的 `ReloadStructs` / Editor repair 逻辑可以原样保留。 |
| 验证方式 | 1. 新增用例缓存旧 `UASStruct*`，full reload 后验证 `ResolveLatestASScriptStruct()` 能解析到新版本。<br>2. 对 loaded Blueprint / `UDataTable` 回归，确认引入 canonicalizer 后现有 repair 结果不变。<br>3. 加 diagnostics 断言，验证仍持有 replaced struct 的路径会被日志或 state dump 明确列出。 |

### Arch-HR-62：delegate 签名的 identity 只存在于单轮 `OnDelegateReload` batch，没有持久 canonicalization 或 tombstone

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UDelegateFunction` 在 full reload / 删除场景下的身份连续性 |
| 当前设计 | 当前 delegate full reload 的 owner 是“一次性 old/new pair”。生成器在有新 delegate 时会 rename 旧 `UDelegateFunction`、创建新对象，并通过 `OnDelegateReload(Old, New)` 把映射交给本轮 `ReloadState`；但 runtime 侧获取 delegate 签名时仍直接读取 type user data/raw pointer。更关键的是，`FModuleData` 只建了 `RemovedClasses`，没有 `RemovedDelegates` / `RemovedStructs`，说明 delegate 删除根本没有进入 reload delta。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:93-105`：`FModuleData` 只有 `RemovedClasses`，没有 delegate/struct removal lane。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1856-1867`：分析阶段只把旧 module 中缺失的 class 记录为 removed。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2712-2740`：有新 delegate 时才会 rename 旧 `UDelegateFunction` 并创建新对象。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3905-3948`：delegate 提交完成后仅广播 `OnDelegateReload(OldDelegate, NewFunction)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:104-109`：Editor 侧只把这份 old/new pair 记进 `ReloadDelegates` / `NewDelegates`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.h:64-69`：`GetDelegateSignature()` 直接从 `Type->plainUserData` 取 `UDelegateFunction*`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:381-399`：类型系统同样把 raw `UserData` 直接解释为 `UDelegateFunction*`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:89-94`：bind database 导出也缓存并遍历 raw `BoundDelegateFunctions`。 |
| 优点 | 当前 batch 内的 loaded Blueprint/event 节点可以拿到 old/new delegate pair，足以支撑一轮 Editor 修补，不需要每次 full reload 都做全局 delegate 扫描。 |
| 不足 | 这套 identity contract 在 batch 结束后就蒸发了。推断上，任何缓存了旧 `UDelegateFunction*` 的 runtime/editor owner，例如 bind database、custom cache、延迟执行的 type resolver、未进入本轮 loaded repair 的工具层，都会继续持有 renamed delegate。对于“新版本里已经删除的 delegate”，当前甚至没有 tombstone lane 可以发出显式诊断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `reload_modules()` 重新求得 old/new module 集，`update_modules()` 直接把新函数写回旧 module table，再统一 `merge_objects()` / `update_global()` 修补对象图。它维护的是可持续的 module graph identity，不是“一轮广播后就丢”的 raw function shell 映射。 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-547`<br>`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-600` | 如果 current AS 必须暴露宿主 `UDelegateFunction`，就更需要一层持久的 identity registry，而不是只留 batch-local map。 |
| puerts | JS hot reload 用 `parsedScript` 维护 `url <-> scriptId`，并在 `Debugger.setScriptSource` 前后显式发 `HMR.prepare/HMR.finish`；class-side 再通过 `NotifyReBind()` / `MakeSureInject()` 让老对象重新接到新逻辑。 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:13-23`<br>`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-90`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private\TypeScriptGeneratedClass.cpp:77-99`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2365` | 把“身份映射”和“live rebind”做成持久 owner。当前 AS 的 delegate 也需要类似 registry，至少让 cached signature 能追到 latest 或拿到 tombstone。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `UDelegateFunction` 引入持久的 reload registry，补齐 replacement 与 deletion 两类 delta；现有 `OnDelegateReload` 保留为兼容广播。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/` 新增 `FAngelscriptDelegateReloadRegistry`，至少维护 `OldDelegate -> NewDelegate`、`RemovedDelegates`、`SessionId/BatchId`。<br>2. 在 `AngelscriptClassGenerator` 分析阶段扩展 old/new delegate set 比较，新增 `RemovedDelegates`；不要再让 `FModuleData` 只有 `RemovedClasses`。<br>3. 在 `CreateFullReloadDelegate()` / `DoFullReload(FDelegateData&)` 成功后把 old/new pair 写入 registry；若旧 delegate 在新 module 中消失，则写入 tombstone，并新增 `OnDelegateRemoved(UDelegateFunction*)` 或 session-level `RemovedDelegates` delta。<br>4. 第一批接入 runtime 裸指针消费点：`Bind_Delegates.h::GetDelegateSignature()`、`AngelscriptType.cpp` 的 user-data 解释、`AngelscriptBindDatabase.cpp` 的 header 导出，都先通过 `ResolveLatestASDelegateSignature()` 查询 registry；命中 tombstone 时输出显式 diagnostics，而不是继续返回 renamed old delegate。<br>5. Editor 侧继续保留当前 `ReloadDelegates/NewDelegates` loaded repair，但数据来源改成 registry/session delta；这样 batch 结束后 runtime/editor 仍有统一的 latest/remove 查询入口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptDelegateReloadRegistry.h`、新建 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptDelegateReloadRegistry.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 预估工作量 | M |
| 架构风险 | delegate removal 一旦开始显式出 tombstone，可能会把过去被静默吞掉的 stale signature 暴露出来；短期看诊断数量会上升，但这比继续把旧 delegate 留在系统里更安全。 |
| 兼容性 | 向后兼容。首阶段可以保持旧 `OnDelegateReload` 与 Editor repair 行为，只是增加 registry 查询与 tombstone 诊断；只有开启严格模式时，removed delegate 才从 warn 升级到 hard failure。 |
| 验证方式 | 1. 缓存旧 `UDelegateFunction*` 后执行 full reload，确认 `ResolveLatestASDelegateSignature()` 能追到新 delegate。<br>2. 删除脚本 delegate，确认系统会产出明确 tombstone 诊断，而不是继续让 raw pointer 指向旧壳。<br>3. 回归 loaded Blueprint event 节点刷新，确认引入 registry 后现有 `ReloadDelegates/NewDelegates` repair 结果保持不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-HR-61 | `UASStruct::NewerVersion` 没有进入统一 canonicalization | identity resolver + 观测诊断 | 中高 |
| P1 | Arch-HR-62 | delegate 签名 identity 只存在于单轮 batch，且缺少 removed delegate tombstone | registry + 删除 delta 补齐 | 高 |
