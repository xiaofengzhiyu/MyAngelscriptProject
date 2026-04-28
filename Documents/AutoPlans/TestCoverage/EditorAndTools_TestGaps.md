# EditorAndTools 测试覆盖缺口分析

---

## 测试审查 (2026-04-08 13:07:29 +08:00)

### 一、现有测试问题

#### Issue-1：`SourceNavigation.Functions` 只验证可导航判定，没有验证真实导航动作

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 测试名 | `Angelscript.TestModule.Editor.SourceNavigation.Functions` |
| 行号范围 | 57-78 |
| 问题描述 | 该用例只断言生成出来的 `UASFunction` 保存了 `GetSourceFilePath()` / `GetSourceLineNumber()`，以及 `FSourceCodeNavigation::CanNavigateToClass()` / `CanNavigateToFunction()` 返回 `true`。但 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp` 中真正执行导航的是 `NavigateToClass()` / `NavigateToFunction()`，它们会继续走 `OpenModule()` / `OpenFile()` 并调用 `FPlatformMisc::OsExecute`。当前测试没有覆盖这条路径。 |
| 影响 | 只要类型识别还在，测试就会通过；即使真实导航丢了 `--goto` 行号、文件路径为空时误返回 `true`、或 handler 注册后无法真正打开文件，这个唯一用例也不会报警。 |
| 修复建议 | 把当前单测拆成“metadata 保留”和“真实导航调用”两个用例。前者保留现有断言；后者为 `FAngelscriptSourceCodeNavigation` 增加可替换的 `OpenFile/OpenModule` seam 或测试委托，断言 `NavigateToFunction()` / `NavigateToClass()` 传入了正确的绝对路径与行号，并补一个空路径返回 `false` 的负例。 |

#### Issue-2：`NormalizePaths` 在数量断言失败后继续下标访问，存在越界风险

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.NormalizePaths` |
| 行号范围 | 257-261 |
| 问题描述 | 用例先执行 `TestEqual(..., NormalizedPaths.Num(), 2)`，但无论该断言是否失败，都会继续读取 `NormalizedPaths[0]` 和 `NormalizedPaths[1]`。一旦归一化逻辑回归导致结果数量变化，测试会变成越界访问或级联失败，而不是给出单一、可定位的断言结果。 |
| 影响 | 回归时容易出现崩溃或二次噪声，降低定位效率，也掩盖真正的问题是“结果数量不对”。 |
| 修复建议 | 先用 `if (!TestEqual(...)) { return false; }` 守住数量，再比较元素内容；或者直接构造 `ExpectedPaths`，使用长度和逐项比较/数组比较一次性校验。 |

#### Issue-3：`AnalyzeLoadedBlueprint` 系列用例只检查 `Contains(ExpectedReason)`，无法发现误报

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.AnalyzeParentClass`, `Angelscript.Editor.BlueprintImpact.AnalyzeVariableType`, `Angelscript.Editor.BlueprintImpact.AnalyzePinType`, `Angelscript.Editor.BlueprintImpact.AnalyzeNodeDependency`, `Angelscript.Editor.BlueprintImpact.AnalyzeReferencedAsset`, `Angelscript.Editor.BlueprintImpact.AnalyzeDelegateSignature` |
| 行号范围 | 328-334, 369-375, 396-402, 423-429, 444-450, 484-490 |
| 问题描述 | `AnalyzeLoadedBlueprint()` 会把 `ScriptParentClass`、`NodeDependency`、`PinType`、`VariableType`、`DelegateSignature`、`ReferencedAsset` 累加到同一个 `Reasons` 数组中。当前 6 个用例都只断言“返回 `true` 且 `Reasons.Contains(ExpectedReason)`”，没有校验 `Reasons.Num()`，也没有排除其他 reason。只要被测函数过度报告，测试仍然是绿的。 |
| 影响 | 扫描器一旦把无关 dependency、pin 或 replacement ref 误记成影响原因，当前测试不会发现，后续 `ClassReloadHelper` 和 commandlet 输出会出现误报而单测仍通过。 |
| 修复建议 | 每个用例都把 `Reasons` 作为精确集合校验，例如先断言 `Reasons.Num() == 1`，再断言唯一值等于目标 reason；如果预期是多 reason，改为排序后与 `ExpectedReasons` 完整比较。同时为每个 reason 增加一组“相近但不应命中”的负例，避免跨分支串扰。 |

#### Issue-4：`FindBlueprintAssetsDiskBacked` 的 helper 在失败路径泄漏 `Blueprint` / `Package`

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.FindBlueprintAssetsDiskBacked` |
| 行号范围 | 223-241 |
| 问题描述 | `CreateDiskBackedBlueprintChild()` 在 `AssetRegistryModule.AssetCreated(Blueprint)` 之后，如果 `SaveBlueprintToDisk()` 失败，会直接 `return nullptr`。调用方收到的是空指针，`ON_SCOPE_EXIT` 中的 `CleanupBlueprint(Blueprint)` 不会处理这个已经创建出来的对象和 package，asset registry 里也可能留下已注册但未清理的条目。 |
| 影响 | 一旦磁盘写入失败或包保存失败，测试会把脏对象和注册状态留给后续用例，形成测试间污染，且问题只会在失败路径出现，最难排查。 |
| 修复建议 | 把 helper 改成“返回 `bool` + 通过输出参数返回 `Blueprint`”或在失败分支显式调用 `CleanupBlueprint(LocalBlueprint)`；同时在失败后撤销文件和 registry 侧副作用，保证 `ON_SCOPE_EXIT` 能拿到真实对象并完成清理。 |

#### Issue-5：`AngelscriptBlueprintImpactScannerTests.cpp` 已超过单文件 500 行，上下文混杂

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.*` |
| 行号范围 | 1-521 |
| 问题描述 | 当前文件同时承载 path normalize、module match、symbol build、`AnalyzeLoadedBlueprint` 六种 reason、disk-backed asset scan、commandlet invalid argument，以及一整套 blueprint/package helper，文件总长 521 行，已经超过规则中单文件 300-500 行的上限。 |
| 影响 | 共享 helper 与多类关注点耦合在一个文件里，后续继续补 scanner 测试时很快会失控；同时 review diff 很难定位到底是在改 pure function、蓝图分析还是 commandlet 行为。 |
| 修复建议 | 按职责拆成至少 3 个文件：`AngelscriptBlueprintImpactScannerCoreTests.cpp`（normalize/match/build）、`AngelscriptBlueprintImpactAnalyzeLoadedBlueprintTests.cpp`（各类 reason）、`AngelscriptBlueprintImpactCommandletTests.cpp`（commandlet / asset scan）；公共 blueprint helper 下沉到 `Tests/Shared/`。 |

#### Issue-6：`DirectoryWatcher` 队列测试完全没有校验异步 reload 依赖的时间戳副作用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove`, `Angelscript.Editor.DirectoryWatcher.Queue.IgnoresNonScriptFiles`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator`, `Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd` |
| 行号范围 | 94-102, 122-128, 151-158, 180-191, 214-222 |
| 问题描述 | `QueueScriptFileChanges()` 在识别到 root 内文件后会先写入 `Engine.LastFileChangeDetectedTime`，运行时 reload 逻辑再用这个时间戳和 `0.2s` 窗口做合并处理。当前 5 个用例只看 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload`，完全没有断言时间戳是否更新，也没有区分 root 内变化与 root 外变化。 |
| 影响 | 即使目录事件还能入队，只要时间戳更新回归，真正的异步 reload 节流逻辑就可能失效；当前测试仍然全部通过。 |
| 修复建议 | 每个 queue 场景在调用前记录 `InitialTimestamp`，调用后断言 root 内变化会使 `LastFileChangeDetectedTime > InitialTimestamp`；另外补一个 root 外文件变化的负例，要求队列和时间戳都不变。这样至少能把“事件入队”和“异步合并触发条件”一起守住。 |

#### NewTest-1：补齐 `SourceNavigation` 的真实导航动作与负路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp` |
| 关联函数 | `FAngelscriptSourceCodeNavigation::NavigateToFunction`, `NavigateToClass`, `NavigateToProperty`, `NavigateToStruct` |
| 现有测试覆盖 | 有测试但只覆盖 `UASFunction` metadata 和 `CanNavigate*` 正路径 |
| 风险评估 | 真实导航打开错误文件、错误行号，或 `empty path` / 非 Angelscript symbol 误返回 `true` 时不会被发现 |
| 建议测试名 | `Angelscript.TestModule.Editor.SourceNavigation.NavigateToFunctionUsesStoredSourceLocation` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`，若超过 300 行则拆成新建 `AngelscriptSourceNavigationHandlerTests.cpp` |
| 场景描述 | 编译一个带 `UFUNCTION`、`UPROPERTY`、`USTRUCT` 的 script module，分别调用 `NavigateToFunction` / `NavigateToProperty` / `NavigateToStruct`，并覆盖空路径和非 `UASFunction` 的负例 |
| 输入/前置 | 使用 `AcquireProductionLikeEngine` 或 `FAngelscriptTestFixture` 编译测试模块；为 `OpenFile` / `OpenModule` 增加 test seam 记录 `Path`、`Line`、调用次数 |
| 期望行为 | `NavigateToFunction` 返回 `true`，记录的 `Path` 等于脚本绝对路径、`Line` 等于源码行号；`NavigateToProperty` / `NavigateToStruct` 命中各自声明行；空路径或原生 `UFunction` 返回 `false` 且不触发 seam |
| 使用的 Helper | `FAngelscriptTestFixture` + `AcquireProductionLikeEngine` + 自定义 navigation seam |
| 优先级 | P1 |

#### NewTest-2：验证 `BuildImpactSymbols` 会收集 delegate 并跳过空指针

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::BuildImpactSymbols` |
| 现有测试覆盖 | 有测试但只断言 class / struct / enum，未覆盖 `Delegates` 和 null entry 过滤 |
| 风险评估 | delegate reload 影响蓝图事件签名时，symbol 集合可能不完整，`AnalyzeLoadedBlueprint` 和 `ClassReloadHelper` 会漏报依赖 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.BuildImpactSymbols.CollectsDelegatesAndSkipsNulls` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerCoreTests.cpp` |
| 场景描述 | 构造一个 module，同时放入有效 delegate、空 `Class`、空 `Struct`、空 `Enum` 条目，调用 `BuildImpactSymbols` |
| 输入/前置 | 手工构造 `FAngelscriptModuleDesc`、`FAngelscriptDelegateDesc`、`FAngelscriptClassDesc`、`FAngelscriptEnumDesc` |
| 期望行为 | `Symbols.Delegates` 只包含有效 delegate；`Classes/Structs/Enums` 都只包含非空对象；各集合 `Num()` 与预期完全一致 |
| 使用的 Helper | 纯 unit helper，无需引擎；使用 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + 本地 builder helper |
| 优先级 | P1 |

#### NewTest-3：补齐 `AnalyzeLoadedBlueprint` 的无影响路径和 `Reasons.Reset()` 语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint` |
| 现有测试覆盖 | 目标文件只有正向命中；目录外 `AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp` 只覆盖 script-parent partial-impact，不覆盖空 symbol / 无影响路径 |
| 风险评估 | `OutReasons.Reset()` 回归、空 symbol 误报、无关 blueprint 被误标 impacted 时不会被 editor 单测发现 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.AnalyzeLoadedBlueprint.EmptySymbolsReturnsFalseAndClearsReasons` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactAnalyzeLoadedBlueprintTests.cpp` |
| 场景描述 | 用一个普通 transient blueprint 预填 `Reasons` 数组，再分别传入空 `FBlueprintImpactSymbols` 和一组与 blueprint 无关的 symbols |
| 输入/前置 | 复用现有 `CreateTransientBlueprintChild` helper，`Reasons` 初始填入一个假值 |
| 期望行为 | 两次调用都返回 `false`；第一次调用后 `Reasons.Num() == 0`；第二次调用后 `Reasons.Num() == 0`，且不包含任何 `EBlueprintImpactReason` |
| 使用的 Helper | `FAngelscriptTestFixture` 或现有 blueprint helper + `IMPLEMENT_SIMPLE_AUTOMATION_TEST` |
| 优先级 | P1 |

#### NewTest-4：补齐 `BlueprintImpact` commandlet 的 `EngineNotReady` 返回码

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` |
| 关联函数 | `UAngelscriptBlueprintImpactScanCommandlet::Main` |
| 现有测试覆盖 | 只有 `ChangedScriptFile` 不存在返回 `1`，未覆盖 `bDidInitialCompileSucceed == false` 的早退路径 |
| 风险评估 | commandlet 在引擎未就绪时继续扫资产会输出误导日志，CI 也无法正确区分“参数错误”和“环境未就绪” |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.CommandletEngineNotReadyReturnsExitCode2` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactCommandletTests.cpp` |
| 场景描述 | 构造 commandlet，并临时把 `FAngelscriptEngine::Get().bDidInitialCompileSucceed` 置为 `false` 后调用 `Main("")` |
| 输入/前置 | 使用 `FAngelscriptEngineScope` 或保存/恢复引擎状态的 RAII helper，确保测试结束恢复原值 |
| 期望行为 | `Main` 返回 `2`；不会尝试读取 `ChangedScriptFile`，也不会触发 asset scan 相关副作用 |
| 使用的 Helper | `FAngelscriptEngineScope` + 状态恢复 helper |
| 优先级 | P1 |

#### NewTest-5：补齐 `DirectoryWatcher` 的 root 外负例和时间戳断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 关联函数 | `AngelscriptEditor::Private::QueueScriptFileChanges` |
| 现有测试覆盖 | 有测试但都在 root 内，且未断言 `LastFileChangeDetectedTime` |
| 风险评估 | root 外文件变化被误纳入 reload，或 root 内事件不再刷新时间戳时，编辑器 hot reload 行为会抖动或失效 |
| 建议测试名 | `Angelscript.TestModule.Editor.DirectoryWatcher.Queue.IgnoresOutsideRootsWithoutTimestampMutation` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 场景描述 | 向 `QueueScriptFileChanges` 传入一个不在 `Engine->AllRootPaths` 下的 `.as` 文件变化，再传入一个 root 内变化进行对照 |
| 输入/前置 | `MakeTestEngineWithRoot` 建一个 root；记录 `InitialTimestamp`；enumerator lambda 内维护调用计数 |
| 期望行为 | root 外变化后两个队列都为空、`LastFileChangeDetectedTime == InitialTimestamp`、enumerator 未被调用；root 内变化后时间戳严格增大且对应队列只包含该文件 |
| 使用的 Helper | 现有 `MakeTestEngineWithRoot` + `IMPLEMENT_SIMPLE_AUTOMATION_TEST` |
| 优先级 | P1 |

#### NewTest-6：覆盖 `EditorModule` 的 `StartupModule` / `ShutdownModule` 生命周期

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h` |
| 关联函数 | `FAngelscriptEditorModule::StartupModule`, `ShutdownModule` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 目录监听、`OnObjectPreSave`、tool menu callback、state dump extension 任一注册/反注册回归都会直接影响整个编辑器会话，且目前没有安全网 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.StartupShutdownRegistersAndCleansHooks` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleLifecycleTests.cpp` |
| 场景描述 | 启动模块后检查关键 hook 已注册，关闭模块后检查 hook 已移除且不会重复注册；重点覆盖 `OnObjectPreSave`、tool menu owner、directory watcher callback 数量、state dump extension handle |
| 输入/前置 | 提供 `IDirectoryWatcher` test double 和可查询的 tool menu / delegate 计数 seam；必要时用 `FAngelscriptTestFixture` 初始化 editor 环境 |
| 期望行为 | `StartupModule` 后每个 script root 都注册一个 watcher，`UToolMenus` owner 存在，`OnObjectPreSave` 新增一个 handler；`ShutdownModule` 后这些注册全部撤销，重复执行不产生重复注册或悬挂 owner |
| 使用的 Helper | `FAngelscriptTestFixture` + `IDirectoryWatcher` test double + delegate count seam |
| 优先级 | P0 |

#### NewTest-7：覆盖 `ClassReloadHelper` 的 reload state 聚合与 post-reload reset

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` |
| 关联函数 | `FClassReloadHelper::Init`, `FClassReloadHelper::ReloadState`, `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | interface/class/struct/delegate reload 一旦没有正确落入 `ReloadState` 或 post-reload 未 reset，会直接造成 blueprint action 列表、volume rebuild、依赖蓝图刷新异常 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.ReloadStateTracksAndResetsOnPostReload` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp` |
| 场景描述 | 调用 `FClassReloadHelper::Init()` 后，触发 `FAngelscriptClassGenerator::OnClassReload` / `OnStructReload` / `OnDelegateReload` / `OnPostReload(true)`；使用 interface class、volume child、普通 class 三种样本验证状态变化 |
| 输入/前置 | 准备可识别的旧/新 class、struct、delegate；必要时用 script compile helper 生成临时类型，测试结束后恢复 `ReloadState()` |
| 期望行为 | interface reload 使 `bRefreshAllActions == true`；volume child reload 使 `bReloadedVolume == true`；reload map / set 包含对应条目；`OnPostReload(true)` 之后 `ReloadState()` 被清空复位 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + script compile helper / `FAngelscriptTestFixture` |
| 优先级 | P0 |

#### NewTest-8：覆盖 `ContentBrowserDataSource` 的 include/exclude 过滤和 item 属性

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `CompileFilter`, `EnumerateItemsMatchingFilter`, `CreateAssetItem`, `GetItemAttribute`, `Legacy_TryGetPackagePath`, `Legacy_TryGetAssetData` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | content browser 一旦列出错误资产、include/exclude 失效，或 item attribute 填错，编辑器里的 script asset 视图会直接错乱且难以通过人工回归稳定复现 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.FiltersAssetsAndBuildsExpectedAttributes` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceTests.cpp` |
| 场景描述 | 在 `FAngelscriptEngine::Get().AssetsPackage` 下创建两个不同 class 的脚本资产，编译 include/exclude filter，然后枚举 item 并读取 attribute |
| 输入/前置 | 初始化 `UAngelscriptContentBrowserDataSource`；构造 `FContentBrowserDataFilter` 的 class include/exclude；准备一个以 `Asset_` 开头的对象名验证 display name 处理 |
| 期望行为 | 只枚举 include 命中的资产且排除 exclude；item virtual path 为 `/All/Angelscript/<AssetName>`；display name 去掉 `Asset_` 前缀；`ItemTypeName == "Script Asset"`、`ItemIsProjectContent == true`，`Legacy_TryGetPackagePath` / `Legacy_TryGetAssetData` 返回对应 payload |
| 使用的 Helper | `FAngelscriptTestFixture` + editor object factory helper |
| 优先级 | P0 |

#### NewTest-9：覆盖 `EditorMenuExtensions` 的函数筛选、默认挂点和注册快照

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp` |
| 关联函数 | `GatherExtensionFunctions`, `GetExtensionPointOrDefault`, `RegisterExtensions`, `UnregisterExtensions`, `UScriptActorMenuExtension::CallFunctionOnSelection`, `UScriptAssetMenuExtension::CallFunctionOnSelection` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | `CallInEditor` 函数筛选、默认挂点、选择过滤或 prompt 转发一旦出错，菜单项会消失、出现在错误菜单，或把错误对象传给脚本函数 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.RegisterFunctionsAndForwardFilteredSelection` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp` |
| 场景描述 | 准备一个 test extension class，包含 `CallInEditor` void 函数、带返回值函数、actor-only 函数、asset-only 函数；分别对 base / actor / asset extension 执行注册、函数收集和 selection 转发 |
| 输入/前置 | 提供 `FScriptEditorPrompts` 的 test seam，构造混合 `SelectedObjects` / `SelectedAssets`；对 `LevelViewport_ContextMenu` 和 `ContentBrowser_AssetViewContextMenu` 各跑一次默认挂点 |
| 期望行为 | 只有 `CallInEditor` 且无返回值的函数被收集；默认挂点分别是 `ActorOptions` / `CommonAssetActions`；`RegisterExtensions` 后 `GetRegisteredExtensionSnapshots()` 数量增加且记录正确 `Location/ExtensionPoint`；`CallFunctionOnSelection` 只把受支持的 actor / asset 传给 prompt seam |
| 使用的 Helper | `FAngelscriptTestFixture` + prompt seam + test extension subclasses |
| 优先级 | P0 |

#### NewTest-10：覆盖 `ScriptEditorSubsystem` 的生命周期与 tick gate

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h` |
| 关联函数 | `ShouldCreateSubsystem`, `Initialize`, `Deinitialize`, `IsAllowedToTick`, `Tick` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | `bSubsystemInitialized` 或 `IsAllowedToTick()` 一旦回归，editor subsystem 会在错误时机 tick 或完全不 tick，脚本侧行为会变得随机 |
| 建议测试名 | `Angelscript.TestModule.Editor.ScriptEditorSubsystem.InitializeAndTickRespectLifecycleGuards` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptScriptEditorSubsystemTests.cpp` |
| 场景描述 | 定义一个 test subclass，记录 `BP_ShouldCreateSubsystem`、`BP_Initialize`、`BP_Deinitialize`、`BP_Tick` 调用；分别验证模板对象、未初始化对象、已初始化对象的 tick gating |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 或 `FAngelscriptTestFixture` 创建测试子系统实例；提供计数器成员记录事件 |
| 期望行为 | `ShouldCreateSubsystem` 返回脚本实现值；`Initialize` 后 `IsAllowedToTick()` 变为 `true`；`Deinitialize` 后恢复 `false`；`Tick` 只在已初始化且非 template 时调用一次脚本实现 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + 自定义 test subclass |
| 优先级 | P2 |

#### NewTest-11：为 editor function library wrappers 增加 smoke test

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/BlueprintMixinLibrary.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h` |
| 关联函数 | `UBlueprintMixinLibrary::GetGeneratedClass`, `UEditorSubsystemLibrary::GetEditorSubsystem`, `UEditorStatics::*`, `UAssetToolsStatics::*`, `UScriptableFactory::FactoryCreateText`, `FactoryCreateBinary`, `CreateOrOverwriteAsset` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 这些 wrapper 大多是一层转发，最容易被认为“太简单不用测”，但一旦签名、null-safe 行为或 buffer forwarding 改坏，脚本侧 API 会直接断裂且编译期不一定能发现 |
| 建议测试名 | `Angelscript.TestModule.Editor.FunctionLibraries.WrappersForwardAndPreserveInputs` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorFunctionLibraryTests.cpp` |
| 场景描述 | 对 wrapper 做最小 smoke：`GetGeneratedClass(nullptr)` 返回 `nullptr`，传入 blueprint 返回 `GeneratedClass`；`GetEditorSubsystem` 与 `UEditorSubsystemBlueprintLibrary` 结果一致；`EditorStatics` 与 `GEditor` 当前状态一致；`AssetToolsStatics::FixupReferencers` 仅转发 redirector；`ScriptableFactory` 把完整 text/binary buffer 原样传给测试子类事件 |
| 输入/前置 | 创建一个 blueprint、一个 editor subsystem class、若干 redirector/非 redirector 对象；再创建一个 blueprint test subclass of `UScriptableFactory`，在蓝图事件 `CreateFromText` / `CreateFromBinary` 中把收到的 buffer 写回可读取字段，供断言使用 |
| 期望行为 | 每个 wrapper 的返回值和直接底层调用一致；`FixupReferencers` 不把非 redirector 混入；`FactoryCreateText` / `FactoryCreateBinary` 收到的内容与输入 buffer 完全一致，长度不截断 |
| 使用的 Helper | `FAngelscriptTestFixture` + editor object factory helper + blueprint factory test asset |
| 优先级 | P2 |

#### NewTest-12：覆盖 `EditorStateDump` 的 CSV 输出和扩展注册

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp` |
| 关联函数 | `RegisterStateDumpExtension`, `UnregisterStateDumpExtension`, `SaveEditorReloadState`, `SaveEditorMenuExtensions` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 一旦 state dump 扩展没有注册、重复注册或 CSV 写错列，排查 editor reload 问题时会直接失去关键诊断材料 |
| 建议测试名 | `Angelscript.TestModule.Editor.StateDump.RegistersExtensionAndWritesExpectedCsvFiles` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorStateDumpTests.cpp` |
| 场景描述 | 手工填充 `FClassReloadHelper::ReloadState()` 至少一条 `ReloadClass` 记录；再定义一个 test `UScriptEditorMenuExtension` 子类并通过 `UScriptEditorMenuExtension::InitializeExtensions()` 让其生成注册快照，随后注册 dump extension、触发一次 state dump 到临时目录，再注销扩展重复触发 |
| 输入/前置 | 临时输出目录、可控的 `FDelegateHandle`、预填充的 reload state、可被 `TObjectIterator<UClass>` 发现的 test extension class |
| 期望行为 | 首次 dump 生成 `EditorReloadState.csv` 和 `EditorMenuExtensions.csv`，文件包含正确表头且分别至少有一条 `ReloadClass` / menu extension 记录；重复 `RegisterStateDumpExtension` 不会重复注册；`UnregisterStateDumpExtension` 后再次触发不会新增文件内容 |
| 使用的 Helper | `FAngelscriptTestFixture` + temp directory helper |
| 优先级 | P2 |

#### NewTest-13：补齐 `ScanBlueprintAssets` 的混合集合部分命中场景

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets` |
| 现有测试覆盖 | 目标目录只有 `FindBlueprintAssetsDiskBacked` 正例；目录外 scenario test 只覆盖 script-parent partial-impact，未覆盖 mixed candidate + `PinType` / `VariableType` reason |
| 风险评估 | 如果扫描器把所有候选 blueprint 都标成 impacted，或者漏掉单个真正命中的 blueprint，当前 editor 侧测试无法发现 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.ScanBlueprintAssets.MixedCandidatesOnlyReturnsImpactedBlueprints` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactAnalyzeLoadedBlueprintTests.cpp`，若文件接近 500 行则新建 `AngelscriptBlueprintImpactScanTests.cpp` |
| 场景描述 | 准备两个 disk-backed blueprint：一个含受影响 `FVector` pin，另一个无相关 pin；构造仅匹配该 struct 所在 module 的 `Request.ChangedScripts`，执行 `ScanBlueprintAssets` |
| 输入/前置 | 复用现有 disk-backed blueprint helper；用 `MakeVector` 节点给 blueprint A 制造 `PinType` reason，blueprint B 保持空图；asset registry 重新扫描两份资产 |
| 期望行为 | `CandidateAssets` 同时包含两个 blueprint；`Matches.Num() == 1`；唯一命中的包路径等于 blueprint A，且 `Match.Reasons` 精确等于 `[EBlueprintImpactReason::PinType]`；`FailedAssetLoads == 0` |
| 使用的 Helper | `FAngelscriptTestFixture` + 现有 blueprint disk-backed helper |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 模块文件口径 | `Plugins/Angelscript/Source/AngelscriptEditor/` 下 `.cpp/.h` 共 31 个，其中 `Private/Tests/*.cpp` 2 个，生产文件 29 个 |
| 目标目录内有直接对应测试的生产文件 | `Private/AngelscriptSourceCodeNavigation.cpp`; `Private/AngelscriptDirectoryWatcherInternal.cpp`; `Private/AngelscriptDirectoryWatcherInternal.h`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`; `Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h` |
| 目标目录内完全无测试的生产文件：Module / Reload / DataSource | `Private/AngelscriptEditorModule.cpp`; `Private/AngelscriptEditorModule.h`; `Private/ClassReloadHelper.cpp`; `Private/ClassReloadHelper.h`; `Private/AngelscriptContentBrowserDataSource.cpp`; `Private/AngelscriptContentBrowserDataSource.h`; `Private/AngelscriptEditorStateDump.cpp` |
| 目标目录内完全无测试的生产文件：EditorMenuExtensions | `Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`; `Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptActorMenuExtension.h`; `Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptAssetMenuExtension.h`; `Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`; `Public/EditorMenuExtensions/ScriptEditorPrompts.h` |
| 目标目录内完全无测试的生产文件：FunctionLibraries / Subsystem | `Public/BaseClasses/ScriptEditorSubsystem.h`; `Public/FunctionLibraries/BlueprintMixinLibrary.h`; `Public/FunctionLibraries/EditorSubsystemLibrary.h`; `Private/FunctionLibraries/EditorStatics.h`; `Private/FunctionLibraries/AssetToolsStatics.h`; `Private/FunctionLibraries/ScriptableFactory.cpp`; `Private/FunctionLibraries/ScriptableFactory.h` |
| 目录外补充覆盖说明 | `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp` 额外为 `BlueprintImpactScanner.cpp` 提供 3 个 scenario 用例，其中包含一个 partial-impact 场景；该文件不在本轮“现有 3 个测试文件质量审查”范围内，但已用于避免重复建议 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-3 |
| MissingCleanup | 1 | Issue-4 |
| AntiPattern | 1 | Issue-5 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 4 | NoTestForSource: 4 |
| P1 | 6 | MissingScenario: 3, MissingErrorPath: 2, MissingEdgeCase: 1 |
| P2 | 3 | NoTestForSource: 3 |

---

## 测试审查 (2026-04-08 13:26:48 +08:00)

### 一、现有测试问题

#### Issue-7：`BuildImpactSymbols` 只验证“包含某项”，没有守住集合精度和 delegate 通道

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.BuildImpactSymbols` |
| 行号范围 | 290-309 |
| 问题描述 | 被测实现 `BuildImpactSymbols()` 会分别填充 `Classes`、`Structs`、`Enums`、`Delegates` 四个集合，并在遇到空指针时跳过对应条目。当前用例只做了 3 个 `Contains(...)` 断言，既没有校验 `Num()`，也没有断言 `Delegates` 通道，因而无法发现“多收集了无关 symbol”“空条目未被过滤”或“delegate 集合完全回归为空”的问题。 |
| 影响 | 只要 class/struct/enum 中仍然含有这 3 个样本，测试就会通过；即使 symbol 聚合开始夹带脏数据，后续 `AnalyzeLoadedBlueprint()` 和 commandlet 输出出现误报，单测也不会报警。 |
| 修复建议 | 把断言升级为精确集合校验：分别断言 `Classes/Structs/Enums/Delegates.Num()` 与预期一致，并显式构造一个有效 `FAngelscriptDelegateDesc` 和若干空 `Class/Struct/Enum/Delegate` 条目，验证 `BuildImpactSymbols()` 只保留有效对象。若保持单文件大小约束，建议把该用例迁到独立的 scanner core 测试文件。 |

#### Issue-8：`CommandletInvalidFile` 依赖环境里的全局引擎状态，返回码并不稳定

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.CommandletInvalidFile` |
| 行号范围 | 337-348 |
| 问题描述 | `UAngelscriptBlueprintImpactScanCommandlet::Main()` 在读取 `ChangedScriptFile` 之前，先检查 `FAngelscriptEngine::Get().bDidInitialCompileSucceed`，为 `false` 时直接返回 `2`。当前用例只 new 了 commandlet 并断言缺失文件返回 `1`，却没有显式设置或恢复 `bDidInitialCompileSucceed`，完全依赖外部测试环境碰巧已经初始化好引擎。 |
| 影响 | 该用例会随着前序测试或启动环境不同，在 `1` 和 `2` 两条早退路径之间漂移；一旦在未初始化环境运行，就会把“参数错误”的断言失败误报成 commandlet 行为回归。 |
| 修复建议 | 为 `bDidInitialCompileSucceed` 增加 scoped state guard，在本用例内显式置为 `true` 并在退出时恢复；同时把 `EngineNotReady` 单独做成另一个测试，分别守住返回 `2` 和返回 `1` 的两条前置条件。 |

#### Issue-9：`FindBlueprintAssetsDiskBacked` 没有验证 `disk-only` 过滤真的排除了内存资产

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.FindBlueprintAssetsDiskBacked` |
| 行号范围 | 493-518 |
| 问题描述 | 被测函数 `FindBlueprintAssets(..., true)` 的核心行为不是“能找到一个保存到磁盘的 blueprint”，而是“在所有 blueprint 资产中只保留 package 真正落盘的那部分”。当前用例只创建了一个正例磁盘资产，然后断言结果里 `Contains(PackagePath)`；没有并列放入一个 transient/未保存 blueprint 作为对照，因此即使过滤逻辑失效、函数退化成“返回全部 blueprint”，这条测试依然会通过。 |
| 影响 | `bIncludeOnlyOnDiskAssets` 的真正契约没有被守住，asset scan 一旦把内存临时 blueprint 混进结果集，后续扫描规模、误报率和 commandlet 输出都会退化，而现有单测不会发现。 |
| 修复建议 | 在同一用例里同时创建一个 disk-backed blueprint 和一个 transient blueprint，调用 `FindBlueprintAssets(..., true)` 后既断言包含前者，也断言不包含后者；再补一个 `bIncludeOnlyOnDiskAssets == false` 的对照用例，验证两者都能返回，从而把正反两条路径一起锁住。 |

#### Issue-10：`CreateTransientBlueprintChild` 在失败路径没有回收刚创建的 package

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.AnalyzeParentClass`, `AnalyzeVariableType`, `AnalyzePinType`, `AnalyzeNodeDependency`, `AnalyzeReferencedAsset`, `AnalyzeDelegateSignature` |
| 行号范围 | 122-150 |
| 问题描述 | `CreateTransientBlueprintChild()` 先 `CreatePackage()` 并给 package 打上 `RF_Transient`，但如果 `FKismetEditorUtilities::CreateBlueprint()` 返回 `nullptr`，helper 会直接返回空指针，没有把已创建的 transient package 标记回收。调用侧的 `ON_SCOPE_EXIT` 只有拿到非空 `Blueprint` 才会进 `CleanupBlueprint()`，因此该失败分支没有任何 cleanup。 |
| 影响 | 一旦 blueprint 创建失败，测试会把临时 package 留在进程内，后续同名对象、GC 和 asset 查询都可能受到污染，而且这类问题只在异常路径触发，排查成本最高。 |
| 修复建议 | 把 helper 改成局部变量 + 统一失败出口：在 `CreateBlueprint()` 失败时显式 `MarkAsGarbage()` 对应 package 并触发一次 `CollectGarbage`，或者改成返回结构体/布尔值，让调用侧无论成功失败都能拿到需要清理的 package 句柄。 |

#### Issue-11：`AnalyzeReferencedAsset` 没有隔离出真正的 replacement-object 场景

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.AnalyzeReferencedAsset` |
| 行号范围 | 432-450 |
| 问题描述 | 该用例创建的是 `AActor` 子类 blueprint，然后只向 `Symbols.ReplacementObjects` 写入 `AActor::StaticClass() -> UObject::StaticClass()`。但场景中没有显式构造任何“图中引用的资产/对象”；`AnalyzeLoadedBlueprint()` 最后阶段会把整个 `Blueprint` 送进 `FArchiveReplaceObjectRef` 做全对象引用扫描，因此当前绿灯并不能区分到底是命中了测试真正想覆盖的 replacement-object 扫描，还是仅仅因为 blueprint 自身已经持有 `ParentClass` 等现成引用。 |
| 影响 | 用例名宣称在验证 `ReferencedAsset` reason，但实际上没有锁住“哪一种引用触发了该 reason”。一旦 replacement-object 扫描只剩下 parent metadata 还能命中、而真实图引用扫描已经回归，测试仍可能保持绿色。 |
| 修复建议 | 把父类改成与 `ReplacementObjects` 无关的 `UObject` 或自定义空类，并显式在 blueprint 中放入一个需要被 `FArchiveReplaceObjectRef` 发现的对象引用，例如默认值引用、组件模板引用或节点持有的对象引用；断言只在加入该引用后才返回 `ReferencedAsset`，去掉该引用后返回 `false`。 |

#### Issue-12：`FolderRemoveUsesLoadedScriptEnumerator` 用 stub lambda 代替真实 enumerator，未覆盖生产 helper

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator` |
| 行号范围 | 161-191 |
| 问题描述 | 生产路径 `OnScriptFileChanges()` 调用 `QueueScriptFileChanges()` 时，传入的是 `GatherLoadedScriptsForFolder(AngelscriptManager, AbsoluteFolderPath)`。当前测试虽然名字写着 “UsesLoadedScriptEnumerator”，但实际传入的是一个手写 lambda，直接返回两条预制 `FFilenamePair`。这只验证了 `QueueScriptFileChanges()` 会消费 callback 的返回值，没有验证真实的 `GatherLoadedScriptsForFolder()` 是否能按文件夹前缀正确枚举、去重和过滤模块 code sections。 |
| 影响 | 只要 `QueueScriptFileChanges()` 还会遍历 callback 结果，这个用例就会通过；即使真实 enumerator 因前缀匹配、重复项或 active module 遍历回归而返回空集/脏数据，测试也不会报警。 |
| 修复建议 | 将当前用例拆成两层：保留现有 queue-level stub 测试验证“folder remove 会消费 enumerator 返回值”；另外新增直接覆盖 `GatherLoadedScriptsForFolder()` 的单测，准备多个 active modules、同目录重复 section 和相似前缀目录，精确断言返回的 `FFilenamePair` 集合与去重结果。 |

### 二、需要新增的测试

#### NewTest-14：补齐 `GatherLoadedScriptsForFolder` 的真实枚举、去重与目录边界

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 关联函数 | `AngelscriptEditor::Private::GatherLoadedScriptsForFolder` |
| 现有测试覆盖 | `FolderRemoveUsesLoadedScriptEnumerator` 只验证 queue 会消费 callback 结果，真实 enumerator 完全无直接测试 |
| 风险评估 | folder remove 事件可能漏掉已加载 script、重复删除同一脚本，或把相似前缀目录里的 script 误当成命中，进而破坏 hot reload 删除队列 |
| 建议测试名 | `Angelscript.TestModule.Editor.DirectoryWatcher.GatherLoadedScriptsForFolder.DeduplicatesAndRejectsPrefixCollisions` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`；若文件接近 300 行则新建 `AngelscriptDirectoryWatcherEnumeratorTests.cpp` |
| 场景描述 | 手工向 `FAngelscriptEngine.ActiveModules` 放入多个 module：一个在 `RemovedFolder/` 下有两条 code section，一个重复引用同一脚本，另一个在 `RemovedFolderSibling/` 下放相似前缀路径；直接调用 `GatherLoadedScriptsForFolder()` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 或现有 `MakeTestEngineWithRoot` 创建 engine；构造 `FAngelscriptModuleDesc` 并填充 `AbsoluteFilename` / `RelativeFilename` |
| 期望行为 | 返回结果只包含 `RemovedFolder/` 及其子目录下的唯一 `FFilenamePair`；重复 section 只出现一次；`RemovedFolderSibling/Leak.as` 不应出现在结果中 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + 本地 `FAngelscriptModuleDesc` builder |
| 优先级 | P1 |

#### NewTest-15：验证 `QueueScriptFileChanges` 不会把“仅共享字符串前缀”的路径误判为 root 内脚本

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 关联函数 | `AngelscriptEditor::Private::QueueScriptFileChanges` |
| 现有测试覆盖 | 有 root 外负例建议，但当前目录内没有覆盖 `StartsWith` 前缀碰撞场景 |
| 风险评估 | 一旦 `Project/Script` 与 `Project/ScriptBackup`、`Root` 与 `RootOld` 这类路径混淆，编辑器会对根目录外脚本误触发 reload，造成随机重编译和误删队列 |
| 建议测试名 | `Angelscript.TestModule.Editor.DirectoryWatcher.Queue.RejectsPathsThatOnlyShareRootPrefix` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 场景描述 | 设定 root 为 `.../Script`，再构造一个变化文件 `.../ScriptBackup/Foo.as` 或 `.../ScriptExtra/Foo.as`，它在字符串上以 root 开头，但并不位于 root 目录层级内 |
| 输入/前置 | 复用 `MakeTestEngineWithRoot`；记录 `InitialTimestamp`，enumerator lambda 维护调用计数 |
| 期望行为 | 调用后 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 仍为空，`LastFileChangeDetectedTime == InitialTimestamp`，enumerator 调用次数为 `0` |
| 使用的 Helper | 现有 `MakeTestEngineWithRoot` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 风格的时间戳断言 |
| 优先级 | P1 |

#### NewTest-16：补齐 `FindModulesForChangedScripts` 的空输入全量回退路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::FindModulesForChangedScripts` |
| 现有测试覆盖 | 只有单一 changed script 命中单个 module 的正例，未覆盖 `NormalizedChangedScripts.IsEmpty()` 早退分支 |
| 风险评估 | commandlet 或 editor scan 在“无 changed scripts => full scan”场景下可能错误返回 0 个 module，直接漏掉所有 blueprint impact 检查 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.MatchChangedScriptsToModuleSections.EmptyInputReturnsAllModules` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerCoreTests.cpp` |
| 场景描述 | 构造两个 module，各自带不同 code sections，向 `FindModulesForChangedScripts()` 传入空数组和只包含空白/相对标记的数组作为对照 |
| 输入/前置 | 复用现有 `MakeTestModule` / `MakeCodeSection` helper；对空白路径可先走 `NormalizeChangedScriptPaths()` 验证其被清空 |
| 期望行为 | 空输入时返回的 module 数量与原数组完全一致，顺序保持不变；不会因为空数组而返回空结果或重复 module |
| 使用的 Helper | 纯 unit helper + `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 风格外壳，内部使用现有 module builder |
| 优先级 | P1 |

#### NewTest-17：验证 commandlet 会合并 `ChangedScript=` 与 `ChangedScriptFile=` 的输入并保留裁剪后的脚本列表

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` |
| 关联函数 | `UAngelscriptBlueprintImpactScanCommandlet::Main` |
| 现有测试覆盖 | 仅覆盖缺失 `ChangedScriptFile` 的 `InvalidArguments` 路径，完全未覆盖参数解析和输入合并 |
| 风险评估 | CI 若同时传 inline 变更列表和文件列表，parser 丢项、去空白失败或分隔符处理错误都会让 impact scan 漏掉模块，但当前 editor 测试完全无告警 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.CommandletMergesInlineAndFileChangedScripts` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactCommandletTests.cpp` |
| 场景描述 | 创建一个临时 `ChangedScriptFile`，内容含空白行与一个脚本路径；参数字符串同时传入 `ChangedScript=Scripts/A.as; Scripts/B.as`；准备两个 active modules 分别命中这三条路径，然后执行 `Main(...)` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 显式保证 `bDidInitialCompileSucceed == true`；构造临时文件；使用 log capture 或结果 seam 读取 commandlet summary 输出 |
| 期望行为 | `Main` 返回 `0`；summary 中 `ChangedScripts` 数量等于去空白后的合并结果；`MatchingModules` 只包含实际命中的 modules，不会遗漏 file 输入或 inline 输入中的任一脚本 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + temp file helper + automation log capture |
| 优先级 | P1 |

#### NewTest-18：补齐 `FindBlueprintAssets` 的双路径契约：`disk-only` 排除 transient，而全量模式保留两者

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::FindBlueprintAssets` |
| 现有测试覆盖 | 只有一个 disk-backed 正例，没有 transient 对照，也没有 `bIncludeOnlyOnDiskAssets == false` 分支 |
| 风险评估 | 一旦 asset discovery 把内存 blueprint 混入 disk-only 扫描，或反过来在全量模式下丢掉 transient 资产，impact scanner 和 commandlet 的候选集都会失真 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.FindBlueprintAssets.DiskOnlyExcludesTransientButAllAssetsIncludesBoth` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactAssetDiscoveryTests.cpp` |
| 场景描述 | 同时创建一个 `CreateDiskBackedBlueprintChild()` 生成的保存资产和一个 `CreateTransientBlueprintChild()` 生成的未落盘 blueprint，然后分别调用 `FindBlueprintAssets(..., true)` 与 `FindBlueprintAssets(..., false)` |
| 输入/前置 | 复用现有 blueprint helper、临时 package 文件 cleanup 和 `AssetRegistryModule.Get().ScanModifiedAssetFiles(...)` |
| 期望行为 | `true` 模式下结果只包含 disk-backed blueprint；`false` 模式下同时包含 disk-backed 与 transient blueprint；两个列表都不应包含重复包路径 |
| 使用的 Helper | `FAngelscriptTestFixture` + 现有 blueprint/package helper |
| 优先级 | P1 |

#### NewTest-19：覆盖 `ScriptEditorPrompts` 对首参对象选择的转发与类型过滤

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.h` |
| 关联函数 | `FScriptEditorPrompts::ShowPromptToCallFunction(const UObject*, FName, FScriptEditorPromptOptions, TArray<UObject*>)` |
| 现有测试覆盖 | 完全无测试；前一轮菜单扩展建议只会验证 prompt seam 被调用，不会执行 prompt 本身的参数转发逻辑 |
| 风险评估 | 菜单项即使成功显示，也可能把错误对象类型传给函数、跳过合法选择对象，或在多选时改变调用次数，导致 editor 工具行为和 UI 预期不一致 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.Prompts.ForwardsFirstParameterSelectionAndSkipsMismatchedObjects` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptScriptEditorPromptsTests.cpp` |
| 场景描述 | 定义一个 transient test object，暴露 `RecordActor(AActor* Actor)` 函数并记录收到的对象；传入 `[MatchingActor, NonActorObject, SecondMatchingActor]` 作为 `FirstParameterObjects` 调用 `ShowPromptToCallFunction()` |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 editor test world 创建两个 actor 和一个普通 `UObject`；函数除首参外不再声明其他参数，以避免弹出真实 prompt UI |
| 期望行为 | 返回 `true`；记录的调用次数等于 2；记录顺序与传入 actor 顺序一致；`NonActorObject` 被静默跳过，不触发 `ProcessEvent` |
| 使用的 Helper | `FAngelscriptTestFixture` + transient test object class + actor factory helper |
| 优先级 | P2 |

#### NewTest-20：覆盖 `OnLiteralAssetPreSave` 的包过滤与 literal 类型分支

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `OnLiteralAssetPreSave`, `OnLiteralAssetSaved` |
| 现有测试覆盖 | 完全无测试；前一轮只覆盖了 `StartupModule` / `ShutdownModule` 生命周期建议，未触达静态 pre-save 回调 |
| 风险评估 | 保存普通编辑器资产时若误触发 literal save，会弹出错误对话框或篡改 script asset content；反过来，真正的 Angelscript literal curve 若没被拦截，编辑器保存行为会悄悄失效 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.LiteralAssetPreSaveOnlyInterceptsAngelscriptAssets` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorLiteralAssetTests.cpp` |
| 场景描述 | 启动 editor module 后，分别创建一个位于 `FAngelscriptEngine::Get().AssetsPackage` 下的 `UCurveFloat`、一个位于普通 transient package 下的 `UCurveFloat`，以及一个位于 assets package 下的非 curve 对象；通过 `FCoreUObjectDelegates::OnObjectPreSave` 或公开的 test seam 触发 pre-save |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 保证 engine 初始化；为 `ReplaceScriptAssetContent` 与 `FMessageDialog::Open` 增加 test seam/spy，避免真实 UI 干扰 |
| 期望行为 | 只有 assets package 下的 `UCurveFloat` 会触发 content replacement spy；普通 transient package 的 curve 完全不触发；assets package 下的非 curve 对象走错误路径 spy，但不会写入 script content |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + module startup helper + content-replacement/dialog spy |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 已统计为“有测试”的源码文件中，本轮确认仍无直接覆盖的函数 | `Private/AngelscriptDirectoryWatcherInternal.cpp::GatherLoadedScriptsForFolder`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp::FindModulesForChangedScripts` 的空输入分支；`Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp::FindBlueprintAssets` 的 `bIncludeOnlyOnDiskAssets == false` 分支；`Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` 的参数合并成功路径 |
| 用户点名区域的新增空白点 | `Private/AngelscriptEditorModule.cpp::OnEngineInitDone`, `OnLiteralAssetPreSave`, `OnLiteralAssetSaved` 仍无直接测试；上一轮生命周期建议尚未覆盖这些静态 editor callback |
| EditorMenuExtensions 组的新增空白点 | `Public/EditorMenuExtensions/ScriptEditorPrompts.cpp/.h` 仍然 0 直接测试；现有建议主要守住 menu registration 与 selection forwarding，prompt 本体的对象/struct 参数转发逻辑仍需单独补 |
| 目录外补充覆盖说明 | `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp` 已额外覆盖 `FindModulesForChangedScripts` 的正向过滤场景，但未覆盖本轮新增记录的空输入全量回退路径，因此本轮建议不重复前面的 partial-impact 场景 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-11 |
| BadIsolation | 1 | Issue-8 |
| MissingCleanup | 1 | Issue-10 |
| WrongHelper | 1 | Issue-12 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 5 | MissingScenario: 3, MissingEdgeCase: 2 |
| P2 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-08 13:37:44 +08:00)

### 一、现有测试问题

#### Issue-13：`DirectoryWatcher` 现有用例把异步 watcher 的重复事件理想化，未守住 `AddUnique` 降噪契约

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts`, `Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd` |
| 行号范围 | 75-103, 131-159, 194-223 |
| 问题描述 | 真实文件系统 watcher 往往会把一次保存或重命名拆成多条事件，例如同一路径连续收到 `Added/Modified`、或同一 rename 窗口里重复出现 `Removed` / `Added`。生产实现 `QueueScriptFileChanges()` 在 `Private/AngelscriptDirectoryWatcherInternal.cpp` 第 61、65、76、86 行使用 `AddUnique` 明确承担了去重职责，但现有 5 个队列测试全部只喂入“每个路径一条事件”的理想输入，没有任何用例验证重复事件不会把 reload 队列膨胀。 |
| 影响 | 一旦 `AddUnique` 被替换、`FFilenamePair` 比较语义回归，编辑器在真实异步文件系统噪声下会出现重复 reload / 重复删除，而当前测试仍然是全绿，无法提前暴露这种抖动型回归。 |
| 修复建议 | 新增一个专门的 burst test，向同一路径连续注入重复 `FCA_Added` / `FCA_Modified` / `FCA_Removed` 事件，并断言 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 里每个脚本只保留一条 `FFilenamePair`。如果保留现有文件，建议把该用例与时间戳负例放在同一组 `DirectoryWatcher` async-noise tests 中，明确守住“异步重复事件只产生唯一队列项”的契约。 |

#### Issue-14：`FindBlueprintAssetsDiskBacked` 清掉磁盘文件后没有回收 asset registry 状态

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.FindBlueprintAssetsDiskBacked` |
| 行号范围 | 493-518 |
| 问题描述 | 该用例在执行期先调用 `AssetRegistryModule.AssetCreated(Blueprint)` 和 `ScanModifiedAssetFiles({ PackageFilename })` 把 blueprint 注册到 asset registry，但 `ON_SCOPE_EXIT` 只做了 `Delete(*PackageFilename)` 与 `CleanupBlueprint(Blueprint)`，没有通知 registry 该资产已删除/失效。生产实现 `FindBlueprintAssets()` 在 `bIncludeOnlyOnDiskAssets == false` 时直接返回 registry 中的 `Assets`，因此这条测试结束后仍可能把一条 ghost asset 留在进程级 registry 里。 |
| 影响 | 后续任何依赖 asset registry 全量枚举的 editor 测试，都可能看到并不属于自身夹具的历史 blueprint 资产，导致 candidate 数量漂移、测试顺序敏感或难以解释的误报。 |
| 修复建议 | 在 `ON_SCOPE_EXIT` 里补上 registry 侧 cleanup：删除磁盘文件后显式调用 `AssetRegistryModule.Get().ScanModifiedAssetFiles({ PackageFilename })` 或等价的删除通知，让 registry 同步失效状态；如果希望更稳妥，可在删除前先 `AssetDeleted(Blueprint)`，再做对象 GC，确保磁盘和 registry 两侧都回到干净状态。 |

---

## 测试审查 (2026-04-08 23:35:40 +08:00)

### 一、现有测试问题

#### Issue-15：`Private/Tests` 两个测试文件的 automation path 前缀偏离 `Angelscript.TestModule.*` 约定

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.*`, `Angelscript.Editor.DirectoryWatcher.Queue.*` |
| 行号范围 | 22-74, 15-37 |
| 问题描述 | 这两份文件中的 16 个用例全部注册在 `Angelscript.Editor.*` 路径下，而同模块的 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` 以及仓库内大多数自动化测试都使用 `Angelscript.TestModule.*` 前缀。当前命名把 editor 私有测试放到另一套筛选命名空间，和本轮补测建议要求的 `Angelscript.TestModule.*` 规范不一致。 |
| 影响 | 基于 path 的批量执行、白名单过滤和覆盖率统计会把这 16 个用例与同模块其他测试拆开，后续新增 editor 测试若按规范命名，模块内结果会继续分裂，增加 CI 配置和人工筛选成本。 |
| 修复建议 | 将 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 的路径统一重命名为 `Angelscript.TestModule.Editor.BlueprintImpact.*` 与 `Angelscript.TestModule.Editor.DirectoryWatcher.*`，并同步更新任何依赖旧 path 的批处理/过滤脚本；若短期不能重命名，至少在文档中明确这是历史例外，并避免新测试继续沿用 `Angelscript.Editor.*` 前缀。 |

#### Issue-16：`FolderAdd` / `FolderRemove` 只验证命中的队列，没有防止另一侧被误填充

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator` |
| 行号范围 | 131-159, 161-191 |
| 问题描述 | `FolderAddScansContainedScripts` 只断言 `FileChangesDetectedForReload` 里有两条新增脚本，没有断言 `FileDeletionsDetectedForReload` 必须保持为空；`FolderRemoveUsesLoadedScriptEnumerator` 则只检查删除队列，完全不看新增队列。当前实现里 folder add 和 folder remove 是两条互斥分支，但这两个用例都没有锁住“只能写入一个队列”的契约。 |
| 影响 | 一旦 `QueueScriptFileChanges()` 回归成“folder add 同时塞进 delete queue”或“folder remove 意外塞进 add queue”，现有测试仍然会通过，直到后续 hot reload 行为在编辑器里出现重复编译/误删才会暴露。 |
| 修复建议 | 两个用例都补上对“另一侧队列”的精确断言：folder add 后 `FileDeletionsDetectedForReload.Num() == 0`，folder remove 后 `FileChangesDetectedForReload.Num() == 0`。更稳妥的写法是同时校验两个队列的 `Num()` 与成员集合，确保只有预期队列发生变化。 |

### 二、需要新增的测试

#### NewTest-21：覆盖 `OnEngineInitDone` 的 data source 创建与激活契约

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `OnEngineInitDone`, `FAngelscriptEditorModule::StartupModule` |
| 现有测试覆盖 | 现有建议只覆盖 `StartupModule` / `ShutdownModule` 注册清理；`OnEngineInitDone` 仍无直接测试 |
| 风险评估 | 一旦 `UAngelscriptContentBrowserDataSource` 未创建、未 root、未初始化或未被 `UContentBrowserDataSubsystem` 激活，script asset 会直接从 content browser 消失，而生命周期测试未必能发现这个 editor-only 回调失效 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.OnEngineInitDoneActivatesAngelscriptDataSourceOnce` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleLifecycleTests.cpp`；若该文件接近 500 行则拆成 `AngelscriptEditorModuleInitTests.cpp` |
| 场景描述 | 启动 module 后显式触发一次 `FCoreDelegates::OnPostEngineInit`，检查 transient package 下是否生成名为 `AngelscriptData` 的 `UAngelscriptContentBrowserDataSource`，并验证 `UContentBrowserDataSubsystem` 已激活该 data source；随后重复触发一次作为幂等性对照 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或专用 module startup helper 初始化 editor 环境；记录触发前后 transient package 中同名对象数量；通过 `IContentBrowserDataModule::Get().GetSubsystem()` 查询激活状态 |
| 期望行为 | 首次触发后存在且仅存在一个 `UAngelscriptContentBrowserDataSource`，对象带 `RF_RootSet | RF_Transient`，`ActivateDataSource(\"AngelscriptData\")` 生效；重复触发不会创建第二个同名 data source，也不会把系统留在重复激活状态 |
| 使用的 Helper | `FAngelscriptTestFixture` + module startup helper + content browser subsystem query helper |
| 优先级 | P1 |

#### NewTest-22：补齐 `QueueScriptFileChanges` 对 `FCA_Modified` 的主路径覆盖

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 关联函数 | `AngelscriptEditor::Private::QueueScriptFileChanges` |
| 现有测试覆盖 | 现有 5 个 queue 用例覆盖了 `Added` / `Removed` / folder add / folder remove / rename，但没有单独覆盖 `Modified` 脚本文件 |
| 风险评估 | 若后续实现只保留 add/remove 分支而把 `FCA_Modified` 意外忽略，真实编辑器中的“保存脚本文件”不会触发 reload，当前目录内测试也不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.DirectoryWatcher.Queue.ModifiedScriptQueuesReloadWithoutDeletion` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 场景描述 | 在 root 内准备一个现有 `.as` 文件，向 `QueueScriptFileChanges()` 传入同一路径的 `FCA_Modified` 事件，和 `FCA_Added` 做对照但不复用同一断言分支 |
| 输入/前置 | 复用 `MakeTestEngineWithRoot` 和 `MakeFileChange`；记录 `InitialTimestamp`；enumerator lambda 应保持未调用 |
| 期望行为 | `FileChangesDetectedForReload` 恰好包含该 `FFilenamePair`；`FileDeletionsDetectedForReload.Num() == 0`；`LastFileChangeDetectedTime > InitialTimestamp`；folder enumerator 调用次数为 `0` |
| 使用的 Helper | 现有 `MakeTestEngineWithRoot` + `IMPLEMENT_SIMPLE_AUTOMATION_TEST` |
| 优先级 | P1 |

#### NewTest-23：覆盖 `ScriptEditorMenuExtension` 的 action metadata 委托与 shift-navigation 分支

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `CreateUIAction`, `CreateToolUIAction`, `GetExtensionActionType` |
| 现有测试覆盖 | 现有建议覆盖注册、默认挂点和 selection forwarding，但没有覆盖 `ActionCanExecute` / `ActionIsVisible` / `ActionIsChecked` 元数据，也没有覆盖按住 `Shift` 时的导航分支 |
| 风险评估 | 菜单项即使成功注册，也可能在 UI 上永远不可见/不可执行、toggle 状态错误，或 `Shift` 导航走不到 `FSourceCodeNavigation`；这些都属于 editor 交互层的高频退化点 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.ActionMetadataDelegatesAndShiftNavigationFallback` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorMenuExtensionActionTests.cpp` |
| 场景描述 | 定义一个 test extension class，其中目标函数标记 `CallInEditor`、`ActionCanExecute`、`ActionIsVisible`、`ActionIsChecked`、`ActionType=ToggleButton`；分别构造 `FUIAction` 与 `FToolUIAction`，在普通点击与按住 `Shift` 的两种输入下执行 |
| 输入/前置 | 提供可切换布尔返回值的 test extension；为 `FSourceCodeNavigation::NavigateToFunction/NavigateToClass` 增加 test seam 或 spy；通过 Slate modifier-key seam 模拟 `Shift` 状态 |
| 期望行为 | `CanExecuteAction`、`IsActionVisibleDelegate`、`GetActionCheckState` 精确反映 helper 函数返回值；普通执行路径调用 `CallFunctionOnSelection`；`Shift` 执行路径优先命中 `NavigateToFunction`，失败时回退到 `NavigateToClass`，且不会触发 prompt 执行分支 |
| 使用的 Helper | `FAngelscriptTestFixture` + test extension subclass + source-navigation spy + Slate modifier seam |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 当前工作区实盘统计 | 截至 `2026-04-08 23:35:40 +08:00`，`Plugins/Angelscript/Source/AngelscriptEditor/` 下排除 `ThirdParty/` 与 `Private/Tests/` 后，实际共有 `29` 个生产 `.cpp/.h` 文件（`13 .cpp` + `16 .h`），不是请求里写的 `31` 个 |
| 有直接对应测试的生产文件（7） | `Private/AngelscriptSourceCodeNavigation.cpp` ← `AngelscriptSourceNavigationTests.cpp`; `Private/AngelscriptDirectoryWatcherInternal.cpp` ← `AngelscriptDirectoryWatcherTests.cpp`; `Private/AngelscriptDirectoryWatcherInternal.h` ← `AngelscriptDirectoryWatcherTests.cpp`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` ← `AngelscriptBlueprintImpactScannerTests.cpp`; `Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h` ← `AngelscriptBlueprintImpactScannerTests.cpp`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` ← `AngelscriptBlueprintImpactScannerTests.cpp`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h` ← `AngelscriptBlueprintImpactScannerTests.cpp` |
| 完全没有直接测试的生产文件（22） | `Private/AngelscriptContentBrowserDataSource.cpp`; `Private/AngelscriptContentBrowserDataSource.h`; `Private/AngelscriptEditorModule.cpp`; `Private/AngelscriptEditorModule.h`; `Private/AngelscriptEditorStateDump.cpp`; `Private/ClassReloadHelper.cpp`; `Private/ClassReloadHelper.h`; `Private/FunctionLibraries/AssetToolsStatics.h`; `Private/FunctionLibraries/EditorStatics.h`; `Private/FunctionLibraries/ScriptableFactory.cpp`; `Private/FunctionLibraries/ScriptableFactory.h`; `Public/BaseClasses/ScriptEditorSubsystem.h`; `Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptActorMenuExtension.h`; `Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptAssetMenuExtension.h`; `Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`; `Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`; `Public/EditorMenuExtensions/ScriptEditorPrompts.h`; `Public/FunctionLibraries/BlueprintMixinLibrary.h`; `Public/FunctionLibraries/EditorSubsystemLibrary.h` |
| 用户点名区域的现状 | `EditorModule`、`ClassReloadHelper`、`ContentBrowserDataSource`、`EditorMenuExtensions` 四组文件在当前工作区里仍然全部没有直接测试；其中 `EditorModule` 与 `EditorMenuExtensions` 还包含多个静态 callback / UI action 分支，仅靠现有 3 个测试文件无法间接覆盖 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-15 |
| WeakAssertion | 1 | Issue-16 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | NoTestForSource: 2, MissingScenario: 1 |

---

## 测试审查 (2026-04-08 23:47:29 +08:00)

### 一、现有测试问题

#### Issue-17：`DirectoryWatcher` 的非文件夹用例没有断言 `EnumerateLoadedScripts` 不应被调用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove`, `Angelscript.Editor.DirectoryWatcher.Queue.IgnoresNonScriptFiles`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts`, `Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd` |
| 行号范围 | 94-97, 122-125, 151-154, 214-217 |
| 问题描述 | `Private/AngelscriptDirectoryWatcherInternal.cpp` 第 72-78 行只会在“folder remove”分支调用 `EnumerateLoadedScripts`，其余 `.as` 文件增删、非脚本文件变化和 folder add 都不该触发该回调。当前 4 个用例都传了一个直接返回空数组的 stub lambda，却没有记录调用次数或在被误调用时失败；只要回调仍返回空集，这些测试就无法发现实现把非文件夹事件也错误地送进了 enumerator。 |
| 影响 | 目录监听回归成“每个文件事件都扫 loaded scripts”时，测试仍可能保持绿色，隐藏额外扫描、错误删除队列填充和性能抖动。 |
| 修复建议 | 把这 4 个用例的 lambda 改成带调用计数的 spy：默认分支下 `InvocationCount` 必须保持 `0`，或直接在 lambda 里 `AddError`/`TestTrue(false, ...)` 让误调用立刻失败。这样可以把“结果队列正确”和“非 folder-remove 不触发 enumerator”两个契约一起锁住。 |

#### Issue-18：`DirectoryWatcher` 的文件系统夹具创建没有 setup 断言，失败时只会演化成二次噪声

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove`, `Angelscript.Editor.DirectoryWatcher.Queue.IgnoresNonScriptFiles`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator`, `Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd` |
| 行号范围 | 84, 114, 141-144, 171, 204 |
| 问题描述 | 这 5 个用例都依赖 `IFileManager::MakeDirectory` 和 `FFileHelper::SaveStringToFile` 构造临时目录/文件，但所有 setup 调用都忽略了返回值。比如 `FolderAddScansContainedScripts` 直接假设 `AddedFolder/Nested`、`A.as`、`B.txt`、`Nested/C.as` 全部创建成功；一旦 CI 机器上的路径权限、杀毒占用或长路径限制导致 setup 失败，测试只会在后面的队列数量断言处报错，看不到真正的根因。 |
| 影响 | 文件系统相关用例会把“夹具创建失败”伪装成“业务逻辑回归”，增加排查成本，也让测试结果更依赖运行环境细节。 |
| 修复建议 | 对每个 `MakeDirectory` / `SaveStringToFile` 都补 `TestTrue` 并在失败时 `return false`；同时把关键路径如 `AddedFolder/A.as`、`Nested/C.as` 的绝对路径打印到断言文案里，保证 setup 失败能在第一时间以单一错误暴露。 |

### 二、需要新增的测试

#### NewTest-24：补齐 `AnalyzeLoadedBlueprint` 的 `EditablePinBase` 与 `MacroInstance` `PinType` 分支

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint` |
| 现有测试覆盖 | `AnalyzePinType` 只覆盖普通 `UK2Node_CallFunction::Pins`，没有覆盖第 196-205 行 `UK2Node_EditablePinBase::UserDefinedPins` 和第 219-224 行 `UK2Node_MacroInstance::ResolvedWildcardType` |
| 风险评估 | 自定义事件/节点的用户定义 pin，或 wildcard macro 绑定到被 reload 的 struct/enum 后，如果扫描器只剩“普通 pin”路径可用，编辑器会漏掉真实受影响 blueprint |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.AnalyzeLoadedBlueprint.EditablePinsAndMacroWildcardReportPinType` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactAnalyzeLoadedBlueprintTests.cpp` |
| 场景描述 | 构造两个 transient blueprint：第一个在 event graph 里放一个 `UK2Node_CustomEvent`，通过 `CreateUserDefinedPin` 添加 `FVector` 类型参数；第二个放一个 `UK2Node_MacroInstance`，把 `ResolvedWildcardType` 设成 `FVector`。分别调用 `AnalyzeLoadedBlueprint()` |
| 输入/前置 | 复用 `CreateTransientBlueprintChild`；新增 `AddCustomEventUserPin(Blueprint, PinType)` 和 `AddMacroInstanceWithResolvedWildcard(Blueprint, PinType)` helper；`Symbols.Structs` 填入 `TBaseStructure<FVector>::Get()` |
| 期望行为 | 两个 blueprint 的分析结果都返回 `true`；`Reasons.Num() == 1` 且唯一值都是 `EBlueprintImpactReason::PinType`；对照组在不添加 user pin / wildcard 时返回 `false` |
| 使用的 Helper | 现有 blueprint helper + 新增 node helper，测试宏使用 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` |
| 优先级 | P1 |

#### NewTest-25：覆盖 `ClassReloadHelper::GetTablesDependentOnStruct` 的精确筛选

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` |
| 关联函数 | `FClassReloadHelper::GetTablesDependentOnStruct` |
| 现有测试覆盖 | 现有建议只覆盖 `ReloadState` 聚合与 `OnPostReload` reset，没有覆盖 struct reload 第 157-162 行依赖的 data table 筛选 helper |
| 风险评估 | 如果 helper 误收集了无关 `UDataTable` 或漏掉真正依赖目标 struct 的表，后续 `PerformReinstance()` 会把 `RowStruct` 改错或根本不改，导致表格资产在 reload 后静默损坏 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.GetTablesDependentOnStructReturnsOnlyExactMatches` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp` |
| 场景描述 | 创建三个 transient `UDataTable`：一个 `RowStruct = FVector`，一个 `RowStruct = FRotator`，一个 `RowStruct = nullptr`；分别对 `FVector` 和 `nullptr` 调用 `GetTablesDependentOnStruct()` |
| 输入/前置 | 直接 `NewObject<UDataTable>(GetTransientPackage())` 构造表对象，`MatchingTable->RowStruct = TBaseStructure<FVector>::Get()`，`NonMatchingTable->RowStruct = TBaseStructure<FRotator>::Get()` |
| 期望行为 | 传入 `TBaseStructure<FVector>::Get()` 时，返回数组只包含 `MatchingTable`；不包含 `NonMatchingTable` 与空 `RowStruct` 表；传入 `nullptr` 时返回空数组 |
| 使用的 Helper | `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + transient `UDataTable` helper |
| 优先级 | P2 |

#### NewTest-26：覆盖 `ScriptEditorPrompts` 的 struct 首参转发与类型过滤

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.h` |
| 关联函数 | `FScriptEditorPrompts::ShowPromptToCallFunction(const UObject*, FName, FScriptEditorPromptOptions, TArray<TSharedPtr<FStructOnScope>>)` |
| 现有测试覆盖 | 现有建议只覆盖对象首参 overload；asset menu extension 依赖的 struct 首参 overload 仍然没有直接测试 |
| 风险评估 | 接受 `FAssetData` 或其他 struct 首参的 editor menu action 一旦停止转发选中资产，Content Browser 菜单会静默失效，而对象首参测试不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.ScriptEditorPrompts.StructFirstParameterFiltersMatchingStructs` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptScriptEditorPromptsTests.cpp` |
| 场景描述 | 准备一个 test receiver UObject，声明 `UFUNCTION(CallInEditor)` `RecordAsset(FAssetData SelectedAsset, int32 ExtraValue = 42)`；传入两个 `FAssetData` 的 `FStructOnScope` 和一个非 `FAssetData` 的 `FStructOnScope`，调用 struct overload |
| 输入/前置 | 通过 `FindObject<UStruct>(nullptr, TEXT(\"/Script/CoreUObject.AssetData\"))` 构造 `FStructOnScope`；`Options.HiddenProperties` 预先隐藏 `ExtraValue` 以避免弹出真实 prompt；receiver 记录调用次数、最后一次接收的包名和 `ExtraValue` |
| 期望行为 | 返回 `true`；只对两个 `FAssetData` 输入调用 `ProcessEvent`；调用顺序与传入顺序一致；receiver 记录到的 `ExtraValue` 保持默认值 `42`；非 `FAssetData` 的 `FStructOnScope` 被跳过 |
| 使用的 Helper | `FAngelscriptTestFixture` + test receiver UObject + `FStructOnScope` builder |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| `AnalyzeLoadedBlueprint` 的新增分支空白点 | 目标测试目录内搜索 `UserDefinedPins`、`ResolvedWildcardType`、`CreateUserDefinedPin`、`UK2Node_MacroInstance` 均为 `0` 命中；说明现有 editor tests 仍未直接覆盖 `AnalyzeLoadedBlueprint()` 里的 `EditablePinBase` / `MacroInstance` `PinType` 分支 |
| `ClassReloadHelper` 的新增空白点 | 目标测试目录内搜索 `GetTablesDependentOnStruct`、`UDataTable`、`RowStruct` 均为 `0` 命中；`ClassReloadHelper` 当前仍没有任何 data-table 依赖筛选相关测试 |
| `ScriptEditorPrompts` 的新增空白点 | 目标测试目录内虽然能搜到 `FAssetData`，但只出现在 `AngelscriptBlueprintImpactScannerTests.cpp` 的 asset 结果断言里；`FirstParameterStructs` / struct overload 在现有 editor tests 中仍然 `0` 直接覆盖 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-17 |
| FlakyRisk | 1 | Issue-18 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-10 00:38:37 +08:00)

### 一、现有测试问题

本轮继续按规则先复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与已有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

本轮新增条目已登记为 `NewTest-95`、`NewTest-96`，分别对应：

- `ScriptEditorMenuExtension.cpp` 中 `ToolbarLabel` / `EditorIcon` / `EditorButtonStyle` / `ActionType` 展示层元数据的 0-hit 契约。
- `AngelscriptEditorModule.cpp::ShowCreateBlueprintPopup` 中 dialog `DefaultPath` / `DefaultAssetName` 的默认推导与显式 delegate 覆盖路径。

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `EditorMenuExtensions` 新增 0-hit 事实 | 对 `Plugins/Angelscript/Source/AngelscriptTest/`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 与当前 gap 文档重新全文检索后，`ToolbarLabel`、`EditorButtonStyle`、`GraphEditor.Event_16x`、`StyleNameOverride`、`AddToolMenuButton`、`AddToolMenuEntry`、`AddMenuEntry` 仍然都是 `0` 直接命中；说明 `ScriptEditorMenuExtension` 的展示层 metadata 在本轮之前还没有任何 direct spec。 |
| 本轮 `EditorModule` 新增 0-hit 事实 | 同样重新检索后，`DefaultPath`、`DefaultAssetName`、`RelativeSourceFilePath`、`AssetRegistry.HasAssets(` 在目标测试树中仍然没有任何 `ShowCreateBlueprintPopup` 相关 direct spec；现有建议只锁住“dialog 返回什么就创建什么”，没有锁住“dialog 默认应该展示什么”。 |
| 用户点名热点增量 | 本轮新增 2 条规格都落在用户明确点名的 0-hit 热点里：`Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp/.h` 与 `Private/AngelscriptEditorModule.cpp`。这两条都不是文件级空白复述，而是继续下钻到 workflow / UI metadata 的函数级契约。 |
| 31 文件口径复核 | 本轮再次实扫 `Plugins/Angelscript/Source/AngelscriptEditor/` 后，production 源码口径仍为 `29` 个文件；现有 3 个目标测试文件 direct-hit 的 production 文件仍为 `7` 个，完全无 direct test 的 production 文件仍为 `22` 个。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingScenario: 1 |


---

## 测试审查 (2026-04-10 00:38:37 +08:00)

### 一、现有测试问题

本轮继续按规则先复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与已有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

本轮新增条目已登记为 `NewTest-95`、`NewTest-96`，分别对应：

- `ScriptEditorMenuExtension.cpp` 中 `ToolbarLabel` / `EditorIcon` / `EditorButtonStyle` / `ActionType` 展示层元数据的 0-hit 契约。
- `AngelscriptEditorModule.cpp::ShowCreateBlueprintPopup` 中 dialog `DefaultPath` / `DefaultAssetName` 的默认推导与显式 delegate 覆盖路径。

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `EditorMenuExtensions` 新增 0-hit 事实 | 对 `Plugins/Angelscript/Source/AngelscriptTest/`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 与当前 gap 文档重新全文检索后，`ToolbarLabel`、`EditorButtonStyle`、`GraphEditor.Event_16x`、`StyleNameOverride`、`AddToolMenuButton`、`AddToolMenuEntry`、`AddMenuEntry` 仍然都是 `0` 直接命中；说明 `ScriptEditorMenuExtension` 的展示层 metadata 在本轮之前还没有任何 direct spec。 |
| 本轮 `EditorModule` 新增 0-hit 事实 | 同样重新检索后，`DefaultPath`、`DefaultAssetName`、`RelativeSourceFilePath`、`AssetRegistry.HasAssets(` 在目标测试树中仍然没有任何 `ShowCreateBlueprintPopup` 相关 direct spec；现有建议只锁住“dialog 返回什么就创建什么”，没有锁住“dialog 默认应该展示什么”。 |
| 用户点名热点增量 | 本轮新增 2 条规格都落在用户明确点名的 0-hit 热点里：`Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp/.h` 与 `Private/AngelscriptEditorModule.cpp`。这两条都不是文件级空白复述，而是继续下钻到 workflow / UI metadata 的函数级契约。 |
| 31 文件口径复核 | 本轮再次实扫 `Plugins/Angelscript/Source/AngelscriptEditor/` 后，production 源码口径仍为 `29` 个文件；现有 3 个目标测试文件 direct-hit 的 production 文件仍为 `7` 个，完全无 direct test 的 production 文件仍为 `22` 个。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:34:17 +08:00)

### 一、现有测试问题

本轮按 `TestCoverageGapRule_ZH.md` 先逐行复核了 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与已有 gap 文档逐项去重；截至当前追加点，没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

#### NewTest-95：覆盖 `EditorMenuExtensions` 的 `ToolbarLabel` / `EditorIcon` / `EditorButtonStyle` 元数据输出契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `UScriptEditorMenuExtension::AddMenuEntry`, `AddToolMenuEntry`, `AddToolMenuButton`, `GetExtensionActionType` |
| 现有测试覆盖 | 现有 `EditorMenuExtensions` 建议已覆盖注册、分类排序、`ActionCanExecute/ActionIsVisible/ActionIsChecked`、tool-menu context、lifecycle 与 section metadata；但 `ScriptEditorMenuExtension.cpp:154-251` 里 `ToolbarLabel`、`EditorIcon`、`EditorButtonStyle` 和 `ActionType=CollapsedButton/RadioButton/Check` 这些展示层元数据仍没有任何独立规格 |
| 风险评估 | 即使菜单扩展还能成功注册和执行，toolbar/menu item 仍可能悄悄退回默认文案 `GetDisplayNameText()`、默认图标 `GraphEditor.Event_16x`、或丢失 `StyleNameOverride` / `CollapsedButton` 等 UI 形态。此类回归不会阻止功能运行，但会直接破坏 editor 可发现性和既有工作流记忆。 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.ToolbarMetadataOverridesDisplayNameIconStyleAndActionType` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorMenuExtensionPresentationTests.cpp` |
| 场景描述 | 定义一个最小 test `UScriptEditorMenuExtension` 子类，声明两个 `CallInEditor` 函数：第一个带 `ToolbarLabel="Bake Now"`、`EditorIcon="Icons.Save"`、`EditorButtonStyle="CalloutToolbar"`、`ActionType="CollapsedButton"`；第二个不带任何展示元数据作为默认对照。分别通过 `BuildMenu()`、`BuildToolMenuSection(..., bIsMenu=true)` 和 `BuildToolMenuSection(..., bIsMenu=false)` 生成 menu entry、tool-menu entry 和 toolbar button。 |
| 输入/前置 | 使用 editor fixture；准备可读取 `FMenuBuilder` / `UToolMenu` 生成条目的 inspection helper，记录 label、icon brush/style set、`StyleNameOverride` 与 `UserInterfaceActionType`；若现有 test helper 不足，补一个只读 introspection seam，不要走真实 UI 渲染。 |
| 期望行为 | 带元数据的函数在 3 条路径上都使用 `Bake Now` 作为显示文本，icon 精确等于 `Icons.Save`，toolbar button 的 `StyleNameOverride == "CalloutToolbar"`，`UserInterfaceActionType == EUserInterfaceActionType::CollapsedButton`；默认对照函数仍回退到 `GetDisplayNameText()` 和 `GraphEditor.Event_16x`，且没有 style override。 |
| 使用的 Helper | editor fixture + test `UScriptEditorMenuExtension` subclass + menu/tool-menu inspection helper |
| 优先级 | P2 |

#### NewTest-96：覆盖 `ShowCreateBlueprintPopup` 的默认 `DefaultPath/DefaultAssetName` 推导，锁住脚本目录回溯与 `BP_`/`DA_` 前缀规则

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `FAngelscriptEditorModule::ShowCreateBlueprintPopup` |
| 现有测试覆盖 | 现有 `EditorModule` 建议只覆盖 `ShowCreateBlueprintPopup` 在 dialog 返回最终 object path 后会创建 blueprint/data asset，以及空返回值负例；`AngelscriptEditorModule.cpp:433-477` 里对默认资产名 `BP_` / `DA_`、`RelativeSourceFilePath` 回溯已有内容目录、以及传给 `CreateModalSaveAssetDialog` 的 `DefaultPath/DefaultAssetName` 仍没有任何独立规格 |
| 风险评估 | 如果默认路径推导回归，弹框仍然能创建资产，但会把用户默认落点悄悄改到 `/Game` 根或错误子目录；这会直接破坏编辑器工作流入口的可用性，尤其影响脚本类首次创建蓝图/数据资产时的内容组织。 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.ShowCreateBlueprintPopupSeedsDialogFromScriptPathAndAssetKind` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleAssetCreationTests.cpp`；若该文件不存在则从 `AngelscriptEditorModuleLifecycleTests.cpp` 拆出 asset-creation 子文件，保持单文件 300-500 行 |
| 场景描述 | 准备两个最小 script class：一个普通类、一个 `UDataAsset` 子类；为它们提供可控的 `RelativeSourceFilePath`，例如 `Gameplay/Enemies/Boss/BossLogic.as`。在未绑定 `GetEditorGetCreateBlueprintDefaultAssetPath` 时，给 `AssetRegistry.HasAssets()` 安装 seam，使其只对 `/Game/Gameplay/Enemies` 返回 `true`；拦截 `CreateModalSaveAssetDialog` 读取收到的 `FSaveAssetDialogConfig`，并返回空字符串避免真实落盘。再补一个绑定 runtime default-path delegate 的对照，验证显式路径会覆盖推导逻辑。 |
| 输入/前置 | 使用 editor fixture + dialog-config inspection seam + asset-registry `HasAssets` seam；准备最小 `UASClass` / test double 以返回稳定的 `GetRelativeSourceFilePath()`、`GetName()`、`IsChildOf<UDataAsset>()`；保存并恢复 runtime module 的 default-path delegate 绑定状态。 |
| 期望行为 | 普通类未显式指定路径时，dialog config 的 `DefaultAssetName` 精确等于 `BP_<ClassName>`，`DefaultPath` 精确等于命中的最深已有目录 `/Game/Gameplay/Enemies`；`UDataAsset` 子类则改用 `DA_<ClassName>` 前缀；当 runtime delegate 返回显式 object path（例如 `/Game/Custom/BP_CustomBoss`）时，`DefaultPath == "/Game/Custom"`、`DefaultAssetName == "BP_CustomBoss"`，不会再回退到脚本目录推导。 |
| 使用的 Helper | editor fixture + dialog-config inspection seam + asset-registry seam + runtime delegate guard |
| 优先级 | P1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-08 23:59:20 +08:00)

### 一、现有测试问题

#### Issue-19：`FolderRemoveUsesLoadedScriptEnumerator` 没有断言 enumerator 只会触发一次

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator` |
| 行号范围 | 180-191 |
| 问题描述 | `QueueScriptFileChanges()` 在 `Private/AngelscriptDirectoryWatcherInternal.cpp` 第 72-78 行对每个 folder-remove 事件只应调用一次 `EnumerateLoadedScripts(AbsolutePath / TEXT(""))`。当前用例只在 lambda 内校验了 `AbsoluteFolderPath`，随后只看删除队列里是否有两条 `FFilenamePair`。如果实现回归成对同一删除事件重复调用 enumerator，两次都返回相同脚本时，`AddUnique` 仍会让 `FileDeletionsDetectedForReload.Num()` 保持 `2`，该测试依然为绿。 |
| 影响 | 额外的 enumerator 调用会带来不必要的 active-module 扫描和隐藏的性能回归，但现有断言无法发现；一旦 callback 后续被替换成有副作用的真实 helper 或 spy，这类重复调用还会放大测试噪声。 |
| 修复建议 | 为该用例增加 `InvocationCount` 并在调用后精确断言 `== 1`；更稳妥的做法是在 lambda 中对第二次调用直接 `AddError`/`TestTrue(false, ...)`，同时保留当前路径断言和删除队列断言，这样可以同时守住“路径正确”和“只枚举一次”两个契约。 |

### 二、需要新增的测试

#### NewTest-27：覆盖 `OnScriptFileChanges` 的初始化 gate 与真实队列转发

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `OnScriptFileChanges` |
| 现有测试覆盖 | `DirectoryWatcherTests` 只直接调用 `QueueScriptFileChanges()`；`OnScriptFileChanges` 这个 editor-module 回调在目标测试目录里仍然 `0` 直接覆盖 |
| 风险评估 | 如果模块层的初始化 guard、`AllRootPaths` 转发或 `GatherLoadedScriptsForFolder` 绑定关系回归，编辑器实际收到文件事件时可能在引擎未就绪阶段误入队，或在就绪后完全丢失 reload 事件，而当前内部 helper 测试不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.OnScriptFileChangesGuardsEngineInitAndQueuesRootScripts` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleLifecycleTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorModuleDirectoryWatcherTests.cpp` |
| 场景描述 | 通过 `StartupModule` 注册 `IDirectoryWatcher` test double，抓取 `FDirectoryChanged` delegate；先在 Angelscript 引擎未初始化时触发一个 root 内 `.as` 变化，再在初始化并配置 `AllRootPaths` 后对同一路径重放事件 |
| 输入/前置 | 使用 module startup helper + `IDirectoryWatcher` test double；临时目录下创建一个 script root；为“未初始化/已初始化”切换准备 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 或等价的 test seam，确保测试结束恢复全局引擎状态 |
| 期望行为 | 未初始化阶段触发 callback 后，`FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 仍为空；初始化并设置匹配 root 后再次触发，同一路径会被加入 `FileChangesDetectedForReload`，folder-remove 场景则会通过真实 `GatherLoadedScriptsForFolder` 填充删除队列 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + module startup helper + `IDirectoryWatcher` test double |
| 优先级 | P1 |

#### NewTest-28：覆盖 `ShowPromptToCallFunctionOnObjects` 的 receiver 过滤与默认参数保留

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.h` |
| 关联函数 | `FScriptEditorPrompts::ShowPromptToCallFunctionOnObjects` |
| 现有测试覆盖 | 现有建议只覆盖 `ShowPromptToCallFunction` 的对象首参和 struct 首参 overload；`ShowPromptToCallFunctionOnObjects` 在目标测试目录里仍然 `0` 直接覆盖 |
| 风险评估 | batch menu action 若对 `Objects` 数组中的 null、错误类型或顺序处理回归，会静默漏调或错调 `ProcessEvent`；同时默认参数若在 prompt-less 路径丢失，也不会被现有 prompt 测试发现 |
| 建议测试名 | `Angelscript.TestModule.Editor.ScriptEditorPrompts.ShowPromptToCallFunctionOnObjectsSkipsNullAndMismatchedReceivers` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptScriptEditorPromptsTests.cpp` |
| 场景描述 | 定义一个 transient test receiver class，暴露 `UFUNCTION(CallInEditor)` `RecordBatch(int32 ExtraValue = 7)`；准备 `[MatchingReceiverA, nullptr, UnrelatedObject, MatchingReceiverB]` 调用 `ShowPromptToCallFunctionOnObjects()` |
| 输入/前置 | `Options.HiddenProperties` 预先隐藏 `ExtraValue` 以避免弹出真实 prompt；receiver 记录调用次数、调用顺序和最后一次收到的 `ExtraValue`；`UnrelatedObject` 使用非 `Function->GetOuterUClass()` 的 UObject 子类 |
| 期望行为 | 函数返回 `true`；只对两个 matching receiver 调用 `ProcessEvent`，调用顺序与输入顺序一致；`nullptr` 和 `UnrelatedObject` 被跳过；两次调用收到的 `ExtraValue` 都保持默认值 `7` |
| 使用的 Helper | `FAngelscriptTestFixture` + test receiver UObject + prompt option helper |
| 优先级 | P1 |

#### NewTest-29：覆盖 `UpdateThumbnail` 对 payload 资产的绑定与 foreign item 负例

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `UAngelscriptContentBrowserDataSource::UpdateThumbnail` |
| 现有测试覆盖 | 现有建议覆盖了 filter、`CreateAssetItem`、attribute 与 legacy lookup，但 `UpdateThumbnail` 在目标测试目录里仍然 `0` 直接覆盖 |
| 风险评估 | content browser 若无法把 payload asset 正确写进 `FAssetThumbnail`，script asset 会显示错误缩略图；若 foreign item 也被错误接受，还可能污染其他 data source 的 thumbnail 更新 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.UpdateThumbnailUsesPayloadAssetAndRejectsForeignItems` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceTests.cpp` |
| 场景描述 | 初始化 `UAngelscriptContentBrowserDataSource`，在 `FAngelscriptEngine::Get().AssetsPackage` 下创建一个 script asset，通过枚举或测试 helper 取到对应 `FContentBrowserItemData`；随后分别对该 item 和一个 foreign/non-file item 调用 `UpdateThumbnail()` |
| 输入/前置 | 复用 `CreateAssetItem` 覆盖建议中的 test asset；准备一个可读取当前 asset data 的 `FAssetThumbnail` 或 thumbnail spy；foreign item 可以来自另一个 data source，或手工构造 `OwnerDataSource != this` 的 item |
| 期望行为 | 对 script asset item 调用时返回 `true`，thumbnail 中的 asset data object path 等于 payload 资产路径；对 foreign/non-file item 调用时返回 `false`，不会改写原有 thumbnail 目标 |
| 使用的 Helper | `FAngelscriptTestFixture` + content browser data source helper + thumbnail spy |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| `EditorModule` 新增空白点 | 目标测试目录内搜索 `OnScriptFileChanges` 命中数为 `0`；当前 `DirectoryWatcher` 测试只覆盖内部 queue helper，还没有任何模块级 watcher callback 集成覆盖 |
| `ScriptEditorPrompts` 新增空白点 | 目标测试目录内搜索 `ShowPromptToCallFunctionOnObjects` 命中数为 `0`；说明批量对象执行路径仍完全没有直接测试 |
| `ContentBrowserDataSource` 新增空白点 | 目标测试目录内搜索 `UpdateThumbnail` 命中数为 `0`；现有覆盖建议尚未触达 thumbnail 更新路径 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-19 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 00:14:22 +08:00)

### 一、现有测试问题

#### Issue-20：`SourceNavigation.Functions` 落盘了临时脚本文件，但 teardown 只丢模块不删文件

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 测试名 | `Angelscript.TestModule.Editor.SourceNavigation.Functions` |
| 行号范围 | 41-51, 43-46 |
| 问题描述 | 该用例把 `RelativeScriptFilename` 设为 `RuntimeFunctionNavigationTest.as`，并断言 `ScriptPath == Saved/Automation/RuntimeFunctionNavigationTest.as`。共享 helper `CompileAnnotatedModuleFromMemory()` 实际会在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp:179-191` 把脚本文本写入 `ProjectSavedDir()/Automation/`。但当前 `ON_SCOPE_EXIT` 只执行 `Engine.DiscardModule("RuntimeFunctionNavigationTest")`，没有删除落盘的 `.as` 文件。 |
| 影响 | 测试会在 `Saved/Automation/` 持续留下同名脚本夹具；一旦后续测试或人工调试依赖同目录扫描、时间戳或残留文件列表，`SourceNavigation` 的历史产物可能制造误报，也会让“当前测试真实写了哪些文件”变得不透明。 |
| 修复建议 | 在现有 `ON_SCOPE_EXIT` 里补上 `IFileManager::Get().Delete(*ScriptPath, false, true, true)`，并在删除前后用 `TestTrue` 或日志确认路径可清理；若后续还需要保留文件供断言，应改成 helper 统一返回 cleanup 句柄，确保模块和磁盘夹具一起回收。 |

#### Issue-21：`BlueprintImpact` 共享 blueprint helper 不校验 compile 结果，失败会被后续断言误归因

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.AnalyzeParentClass`, `AnalyzeVariableType`, `AnalyzePinType`, `AnalyzeNodeDependency`, `AnalyzeReferencedAsset`, `AnalyzeDelegateSignature`, `FindBlueprintAssetsDiskBacked` |
| 行号范围 | 138-149, 223-236 |
| 问题描述 | `CreateTransientBlueprintChild()` 和 `CreateDiskBackedBlueprintChild()` 在创建 blueprint 后都会直接调用 `FKismetEditorUtilities::CompileBlueprint(Blueprint)`，但 helper 没有检查 `Blueprint->GeneratedClass`、`Blueprint->SkeletonGeneratedClass` 或编译状态是否有效，就把对象返回给调用侧。这样一来，若父类状态、editor 子系统或 K2 节点变更导致 compile 失败，后续用例会在 `AnalyzeLoadedBlueprint()`、`FindBlueprintAssets()` 或 graph helper 断言处报错，看起来像扫描器回归，而不是“夹具蓝图没编译好”。 |
| 影响 | 失败根因会从 setup 阶段漂移到业务断言阶段，降低定位效率；同时这类问题高度依赖 UE editor 状态，容易在 CI 或不同引擎版本上表现为偶发红灯。 |
| 修复建议 | 在两个 helper 里把 compile 后置为显式 setup 断言，例如检查 `Blueprint->GeneratedClass != nullptr`、`Blueprint->SkeletonGeneratedClass != nullptr`，必要时补 `Blueprint->Status != BS_Error` 或等价编译结果判断；一旦 compile 无效就立刻 cleanup 并返回失败，不要把半成品 blueprint 交给后续测试。 |

### 二、需要新增的测试

#### NewTest-30：覆盖 `ClassReloadHelper` 的 enum / literal-asset reload 分支

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` |
| 关联函数 | `FClassReloadHelper::Init` 中的 `FAngelscriptClassGenerator::OnLiteralAssetReload`, `OnEnumChanged`, `OnEnumCreated` lambda |
| 现有测试覆盖 | 现有建议只覆盖 class / struct / delegate reload 聚合与 data table helper；`ReloadAssets`、`ReloadEnums`、`NewEnums` 仍是 `0` 直接覆盖 |
| 风险评估 | literal asset replacement 或 enum 变更一旦没有进入 `ReloadState()`，后续 `PerformReinstance()`、blueprint action refresh 和 editor 资产刷新会静默漏更新，且当前测试网完全不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.ReloadStateTracksLiteralAssetsAndEnumChanges` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp` |
| 场景描述 | 初始化 `FClassReloadHelper` 后，手工触发一次 `OnLiteralAssetReload(OldAsset, NewAsset)`、一次 `OnEnumChanged(ChangedEnum, OldNames)` 和一次 `OnEnumCreated(NewEnum)`，最后再触发 `OnPostReload(false)` 验证 reset |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 或等价 editor fixture；准备两个 transient UObject 充当 literal asset old/new；准备一个可修改的 test enum 和一组 `OldNames`；测试前后显式保存/恢复 `FClassReloadHelper::ReloadState()` |
| 期望行为 | `ReloadState().ReloadAssets.Find(OldAsset) == NewAsset`；`ReloadState().ReloadEnums.Contains(ChangedEnum)`；`ReloadState().NewEnums.Contains(NewEnum)`；`OnPostReload(false)` 后三者都被清空 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + reload-state RAII helper + transient UObject/enum helper |
| 优先级 | P1 |

#### NewTest-31：覆盖 `ShowAssetListPopup` 的初始化 gate、单资产直开与多资产 popup 配置

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `FAngelscriptEditorModule::ShowAssetListPopup` |
| 现有测试覆盖 | 完全无测试；当前 `DirectoryWatcher` / `BlueprintImpact` 测试都不会走到 debug-server 资产打开入口 |
| 风险评估 | debug server 的“打开受影响资产”入口一旦在未初始化时误弹窗、单资产不再直开，或多资产 popup 丢失 filter，用户会直接失去 editor 工作流入口，而现有测试不会察觉 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.ShowAssetListPopupHonorsInitGateAndBuildsExpectedOpenFlow` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModulePopupTests.cpp` |
| 场景描述 | 先在 `FAngelscriptEngine::IsInitialized()==false` 或 `bIsInitialCompileFinished==false` 时调用一次；再在引擎就绪后分别传入单个 asset path 和两个 asset path + `BaseClass`，覆盖直开分支与 popup 分支 |
| 输入/前置 | 使用 engine-state guard；为 `UAssetEditorSubsystem::OpenEditorForAsset`、`FSlateApplication::PushMenu`、`FContentBrowserModule::CreateAssetPicker` 提供 test seam / spy；准备两个可加载的测试资产路径 |
| 期望行为 | 未就绪阶段不会调用 `OpenEditorForAsset`、不会 `PushMenu`；单资产阶段恰好调用一次 `OpenEditorForAsset(AssetPaths[0])`；多资产阶段不直接打开资产，而是创建一次 picker，`AssetPickerConfig.Filter.PackageNames` 精确等于传入的两个 path，且当 `BaseClass != nullptr` 时 create button 可见 |
| 使用的 Helper | `FAngelscriptTestFixture` + engine-state guard + asset-editor spy + Slate/content-browser seams |
| 优先级 | P1 |

#### NewTest-32：覆盖 `RegisterToolsMenuEntries` 的实际菜单项和 action 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `FAngelscriptEditorModule::RegisterToolsMenuEntries` |
| 现有测试覆盖 | 现有生命周期建议只检查 owner / callback 注册，不会验证 `MainFrame.MainMenu.Tools` 里是否真的生成了预期菜单项 |
| 风险评估 | `Open Angelscript workspace`、`Legacy Native Bind Generator`、`Function Tests` 任何一项消失、落错 section，或 action 指向错误路径，都会直接破坏 editor onboarding 与调试入口 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.RegisterToolsMenuEntriesAddsWorkspaceAndLegacyBindCommands` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleMenuTests.cpp` |
| 场景描述 | 初始化 `UToolMenus` 与 module 后调用 `RegisterToolsMenuEntries()`，读取 `MainFrame.MainMenu.Tools` 的 `Programming` / `Programming Binds` section，并执行 `ASOpenCode` action 一次 |
| 输入/前置 | 提供 `FPlatformMisc::OsExecute` test seam；必要时清理旧的 tool menu owner，避免历史注册污染；保证 `ProjectDir()/Script` 存在或使用虚拟路径 seam |
| 期望行为 | `Programming` section 至少包含 `ASOpenCode` 和 `Function Tests`；`Programming Binds` section 包含 `ASGenerateBindings`；执行 `ASOpenCode` 时 seam 收到的路径精确等于 `FPaths::ProjectDir() / "Script"` |
| 使用的 Helper | `FAngelscriptTestFixture` + `UToolMenus` lookup helper + platform execute seam |
| 优先级 | P1 |

#### NewTest-33：覆盖 `ShowCreateBlueprintPopup` 的资产类型分支与保存路径落点

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `FAngelscriptEditorModule::ShowCreateBlueprintPopup` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 脚本类创建 blueprint / data asset 的主入口一旦选错资产类型、忽略对话框返回路径，或没有打开新建资产，用户会得到错误资产甚至无法继续编辑 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.ShowCreateBlueprintPopupCreatesExpectedAssetAtDialogPath` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModulePopupTests.cpp` |
| 场景描述 | 用一个普通 script class 和一个 `UDataAsset` 派生 script class 各执行一次 `ShowCreateBlueprintPopup()`；对话框 seam 分别返回 blueprint 路径和 data-asset 路径；再补一个空返回值负例 |
| 输入/前置 | 编译两个最小 script class 或构造等价 test `UASClass`；提供 `CreateModalSaveAssetDialog` seam 返回固定 object path；为 `AssetRegistryModule::AssetCreated`、`UAssetEditorSubsystem::OpenEditorForAsset` 提供 spy；测试结束删除创建的 package |
| 期望行为 | 普通 class 分支创建 `UBlueprint`，且 `ParentClass == ScriptClass`；data-asset 分支创建 `Class` 类型的 `UDataAsset` 实例；两条正例都会 `AssetCreated` 并打开 editor；空返回值负例不会创建任何新资产 |
| 使用的 Helper | `FAngelscriptTestFixture` + save-dialog seam + asset-registry spy + asset-editor spy + package cleanup helper |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| `EditorModule` 新增空白点 | 目标测试目录内搜索 `ShowAssetListPopup`、`ShowCreateBlueprintPopup`、`RegisterToolsMenuEntries` 均为 `0` 命中；说明 editor 的资产打开 / 新建资产 / tools menu 入口仍然没有任何直接测试 |
| `ClassReloadHelper` 新增空白点 | 目标测试目录内搜索 `OnLiteralAssetReload`、`OnEnumChanged`、`OnEnumCreated`、`ReloadAssets`、`ReloadEnums`、`NewEnums` 均为 `0` 命中；当前建议集仍未直接覆盖 enum 与 literal-asset reload state |
| `SourceNavigation` 新增事实 | 共享 helper `CompileAnnotatedModuleFromMemory()` 会把相对脚本名写入 `Saved/Automation`；`AngelscriptSourceNavigationTests.cpp` 当前没有对应删除逻辑，说明这不是推断而是已验证的 cleanup 缺口 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-20 |
| WeakAssertion | 1 | Issue-21 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | NoTestForSource: 4 |

---

## 测试审查 (2026-04-09 00:24:50 +08:00)

### 一、现有测试问题

#### Issue-22：`AnalyzeDelegateSignature` 用 blueprint 自生成的 event signature 自证命中，不能代表真实脚本 delegate 影响

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.AnalyzeDelegateSignature` |
| 行号范围 | 453-490 |
| 问题描述 | 该用例先在 blueprint 的 event graph 里手工创建 `UK2Node_CustomEvent`，再直接读取 `CustomEventNode->FindEventSignatureFunction()`，最后把这个同一个 `SignatureFunction` 指针塞回 `Symbols.Delegates`。这会把测试变成“节点返回什么，我就把什么当作 impacted delegate”的自证式夹具，只验证 `AnalyzeLoadedBlueprint()` 对相同指针能命中 `Contains`，没有验证生产路径里由 `BuildImpactSymbols()` 收集的脚本 delegate、蓝图事件节点实际引用的外部 delegate，以及两者之间的真实匹配关系。 |
| 影响 | 即使未来回归把“脚本 delegate reload 影响 blueprint event”这条真实链路破坏，只要 event node 仍能返回一个本地 signature 函数，这个用例就可能继续为绿；它对 `DelegateSignature` 分支提供的是同对象回声测试，而不是 editor 集成语义验证。 |
| 修复建议 | 把用例改成真实外部 delegate 驱动：准备一个来自脚本模块或等价测试 delegate class 的 `UDelegateFunction`，让 blueprint event 节点引用它，再把该外部 delegate 放入 `Symbols.Delegates`。断言 `AnalyzeLoadedBlueprint()` 只在“节点绑定的 delegate 属于 impact set”时返回 `DelegateSignature`，同时补一个使用普通 `CustomEvent` 或无关 delegate 的负例，避免继续用 node 自生成 signature 做自证。 |

### 二、需要新增的测试

#### NewTest-34：覆盖 `UAngelscriptReferenceReplacementHelper` 对 open editor assets 的引用保持与替换通知

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` |
| 关联函数 | `UAngelscriptReferenceReplacementHelper::AddReferencedObjects`, `UAngelscriptReferenceReplacementHelper::Serialize` |
| 现有测试覆盖 | `ClassReloadHelper` 现有建议只覆盖 `ReloadState` 聚合、`GetTablesDependentOnStruct`、enum / literal-asset reload；open asset editor replacement helper 仍完全无测试 |
| 风险评估 | 如果 helper 不再把 `UAssetEditorSubsystem` 中的 open assets 保活，或在引用替换时没有把 editor 实例从 `OriginalAsset` 切换到 `ReplacedAsset`，hot reload 后 editor 会继续持有悬挂对象，随后出现错误编辑目标、丢失已打开标签页甚至 use-after-free 风险 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.ReferenceReplacementHelperRetargetsOpenEditors` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadReferenceReplacementTests.cpp` |
| 场景描述 | 构造 `OriginalAsset` / `ReplacedAsset` 两个 transient 对象，并让 `UAssetEditorSubsystem` test double 报告 `OriginalAsset` 处于已打开状态；随后通过 object-reference-collector archive 驱动 `UAngelscriptReferenceReplacementHelper::Serialize()`，同时单独调用 `AddReferencedObjects()` 覆盖 GC 保活路径 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 editor fixture；提供 `UAssetEditorSubsystem` spy / seam，记录 `GetAllEditedAssets`、`FindEditorsForAsset`、`NotifyAssetClosed`、`NotifyAssetOpened` 调用；准备一个可控制替换映射的 archive helper，把 `OriginalAsset` 序列化成 `ReplacedAsset` |
| 期望行为 | `AddReferencedObjects()` 会把 `OriginalAsset` 加入 collector；`Serialize()` 后恰好对每个打开的 editor 实例执行一次 `NotifyAssetClosed(OriginalAsset, Editor)` 和一次 `NotifyAssetOpened(ReplacedAsset, Editor)`；若 archive 未发生替换，则两个通知都不触发 |
| 使用的 Helper | `FAngelscriptTestFixture` + `UAssetEditorSubsystem` spy + object-reference-collector archive helper |
| 优先级 | P1 |

#### NewTest-35：覆盖 `GameplayTags` 回调注册与 `ReloadTags` 生命周期

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h` |
| 关联函数 | `FAngelscriptEditorModule::StartupModule`, `ShutdownModule`, `ReloadTags` |
| 现有测试覆盖 | 现有生命周期建议覆盖 watcher / tool menu / pre-save / state dump，但还没有任何建议直接验证 `IGameplayTagsModule::OnTagSettingsChanged`、`OnGameplayTagTreeChanged` 与 `ReloadTags()` 的注册和清理 |
| 风险评估 | `StartupModule()` 使用 `AddRaw(this, &FAngelscriptEditorModule::ReloadTags)` 绑定 gameplay tag delegates；如果 `ShutdownModule()` 后仍残留 raw callback，后续 tag 变化可能在已销毁模块上触发悬挂调用，属于 editor 生命周期里的高风险隐患 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.ReloadTagsDelegatesRegisterOnceAndUnbindOnShutdown` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleLifecycleTests.cpp`，若文件接近 500 行则拆成 `AngelscriptEditorModuleGameplayTagsTests.cpp` |
| 场景描述 | 启动模块后触发一次 `OnTagSettingsChanged` 和一次 `OnGameplayTagTreeChanged`，再执行 `ShutdownModule()` 后重复触发；通过 gameplay-tag reload seam 或计数器观察 `ReloadTags()` 是否被调用，以及 shutdown 后是否彻底失效 |
| 输入/前置 | 提供 `AngelscriptReloadGameplayTags()` 的测试 seam / counter，或等价的 gameplay-tag reload spy；使用 module lifecycle fixture 保证测试结束恢复 delegate 状态，避免污染其他 editor tests |
| 期望行为 | `StartupModule()` 后两个 delegate 各只触发一次 reload 计数；重复 `StartupModule()` 不会重复绑定；`ShutdownModule()` 后再次广播两个 delegate，reload 计数不再增加，且 delegate 列表中不保留指向已关闭 module 的 raw 绑定 |
| 使用的 Helper | `FAngelscriptTestFixture` + gameplay-tag reload seam + module lifecycle helper |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| `ClassReloadHelper` 新增空白点 | 目标测试目录与已有 gap 文档内搜索 `UAngelscriptReferenceReplacementHelper`、`AddReferencedObjects`、`NotifyAssetClosed`、`NotifyAssetOpened` 仍为 `0` 直接覆盖；说明 hot reload 后 open editor asset retarget 路径还没有任何专门测试 |
| `EditorModule` 新增空白点 | 目标测试目录与已有 gap 文档内搜索 `ReloadTags`、`OnTagSettingsChanged`、`OnGameplayTagTreeChanged` 仍为 `0` 直接覆盖；说明 `GameplayTags` delegate 生命周期尚未进入任何 editor-module 测试建议 |
| `SourceNavigation` 当前结论 | `AngelscriptSourceNavigationTests.cpp` 仍然只有 1 个用例，当前实际覆盖内容是“脚本 `UFUNCTION` 的 source metadata 保留 + `CanNavigateToClass/Function` 正路径判定”；真实导航动作、property/struct 路径与负例仍全部依赖后续补测 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-22 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 00:40:22 +08:00)

### 一、现有测试问题

#### Issue-23：`CleanupBlueprint` 没有回收 `SkeletonGeneratedClass`，编译过的 blueprint 夹具可能跨用例泄漏

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.AnalyzeParentClass`, `AnalyzeVariableType`, `AnalyzePinType`, `AnalyzeNodeDependency`, `AnalyzeReferencedAsset`, `AnalyzeDelegateSignature`, `FindBlueprintAssetsDiskBacked` |
| 行号范围 | 153-170 |
| 问题描述 | 共享 cleanup helper 目前只对 `Blueprint->GeneratedClass`、`Blueprint->GetOutermost()` 和 `Blueprint` 本体执行 `MarkAsGarbage()`，但前面的两个 blueprint helper 在成功路径都会调用 `FKismetEditorUtilities::CompileBlueprint(Blueprint)`。编译后的 `UBlueprint` 同时持有 `GeneratedClass` 和 `SkeletonGeneratedClass`；当前 teardown 没有回收 `SkeletonGeneratedClass`，意味着每个用例都可能把旧 skeleton class 留在进程里。 |
| 影响 | blueprint 相关测试本来就依赖 editor 级反射状态；如果旧 skeleton class 残留在 `TObjectIterator`、name lookup 或 class regeneration 路径里，后续用例会更容易出现“找到了旧类”“编译状态不一致”这类跨测试污染，而且根因不在业务断言而在夹具清理。 |
| 修复建议 | 在 `CleanupBlueprint()` 里显式处理 `Blueprint->SkeletonGeneratedClass`，与 `GeneratedClass` 一起标记回收；如果担心双重标记，可以先比较两个指针是否不同。更稳妥的做法是把 blueprint cleanup 封装成统一 RAII helper，集中回收 `GeneratedClass`、`SkeletonGeneratedClass`、package 和磁盘/registry 副作用。 |

#### Issue-24：`SourceNavigation.Functions` 复用 production-like engine 却使用固定模块名/文件名，存在共享状态碰撞

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 测试名 | `Angelscript.TestModule.Editor.SourceNavigation.Functions` |
| 行号范围 | 20-50 |
| 问题描述 | 用例通过 `AcquireProductionLikeEngine()` 获取的是“运行中的 production engine 或新建 full engine”，并不保证独占实例；但后续直接把模块名、脚本文件名都固定成 `RuntimeFunctionNavigationTest` / `RuntimeFunctionNavigationTest.as`。结合 `FAngelscriptPreprocessor::AddFile()` 会用 `FilenameToModuleName(RelativeFilename)` 生成模块名，这意味着该用例会在共享引擎里反复占用同一个 module slot，并在 `ON_SCOPE_EXIT` 里无条件 `DiscardModule("RuntimeFunctionNavigationTest")`。 |
| 影响 | 一旦同一进程里已有同名测试模块、人工调试模块，或前一次失败运行留下同名状态，这个用例就可能在 setup 阶段吃到旧模块，或在 teardown 阶段把别的测试/会话里的同名模块一并丢掉；结果是断言漂移、类查找串用，以及难以复现的测试间污染。 |
| 修复建议 | 改成每次生成唯一的 `ModuleName` 和 `RelativeScriptFilename`，例如追加 `FGuid` 或使用现成的 test naming helper，并把它们同时用于 compile、查找和 teardown；如果必须复用 production engine，teardown 前还应先确认当前模块确实由本用例创建，避免误删外部状态。 |

### 二、需要新增的测试

#### NewTest-36：补齐 `ScanBlueprintAssets` 的 full-scan 场景，验证空 `ChangedScripts` 会扫描全部 active modules

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets` |
| 现有测试覆盖 | 现有 editor tests 只覆盖 `FindModulesForChangedScripts` 的正向过滤和 `ScanBlueprintAssets` 的部分命中场景；空 `ChangedScripts` 触发 `Request.IsFullScan()` 的顶层扫描路径仍无直接测试 |
| 风险评估 | 如果 full-scan 分支错误地退化成空 `MatchingModules` 或仍然尝试按 changed scripts 过滤，commandlet 在“未提供 ChangedScript / ChangedScriptFile”时会静默漏掉全部 blueprint impact |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.ScanBlueprintAssets.FullScanUsesAllActiveModulesWhenChangedScriptsEmpty` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScanTests.cpp`；若沿用 `NewTest-13` 的同文件接近 500 行，则拆成 `AngelscriptBlueprintImpactScanFullScanTests.cpp` |
| 场景描述 | 准备两个 active script modules，各自产生不同的 class/struct symbol；再创建两个 candidate blueprint，其中只有一个依赖第二个 module 的 symbol。构造 `Request.ChangedScripts` 为空的请求执行 `ScanBlueprintAssets()` |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 `AcquireProductionLikeEngine` 编译两份 annotated script module；复用现有 blueprint helper 创建 disk-backed candidate 资产并重新扫描 asset registry；请求中显式保持 `ChangedScripts` 为空 |
| 期望行为 | `Result.NormalizedChangedScripts.Num() == 0`；`Result.MatchingModules` 至少包含这两个 module；`Result.Symbols` 同时收集到两份 module 的 symbol；`Result.Matches` 只包含真正依赖第二个 module 的 blueprint，另一个无关 blueprint 不命中 |
| 使用的 Helper | `FAngelscriptTestFixture` + `CompileAnnotatedModuleFromMemory` + blueprint disk-backed helper |
| 优先级 | P1 |

#### NewTest-37：补齐 `BlueprintImpact` commandlet 的 `AssetScanFailure` 退出码

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets`, `UAngelscriptBlueprintImpactScanCommandlet::Main` |
| 现有测试覆盖 | 当前只覆盖 `ChangedScriptFile` 缺失返回 `1`，并已有 `EngineNotReady` 的补测建议；`FailedAssetLoads > 0` 导致 commandlet 返回 `3` 的错误路径仍完全没有测试 |
| 风险评估 | 一旦候选 blueprint 因资产损坏、对象路径失效或 registry 脏状态而加载失败，commandlet 可能错误返回成功并让 CI 误以为结果可信；相反，如果退出码契约回归，自动化脚本也无法区分“扫描成功但无命中”和“扫描过程中丢了资产” |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.CommandletFailedAssetLoadsReturnExitCode3` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactCommandletTests.cpp` |
| 场景描述 | 通过 test seam 或 `IAssetRegistry` fake 提供一个 candidate 列表，其中至少包含一个 class path 指向 `UBlueprint` 但 `GetAsset()` 会返回 `nullptr` 的 asset data；在 `bDidInitialCompileSucceed == true` 的前提下执行 commandlet |
| 输入/前置 | 使用 engine-state guard 固定 `bDidInitialCompileSucceed`；准备最小 asset-registry fake 或在 scanner 侧增加仅测试可见的候选资产 seam；如需对照，可同时放入一个可正常加载的 blueprint asset |
| 期望行为 | `ScanBlueprintAssets()` 会把 unloadable candidate 计入 `FailedAssetLoads`；`Main` 返回 `3`；若同时存在有效 blueprint，则仍会输出有效 match，但最终退出码优先体现 asset scan failure |
| 使用的 Helper | `FAngelscriptEngineScope` + engine-state guard + asset-registry fake / scanner seam |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| `BlueprintImpact` 新增空白点 | 目标测试目录与已有 gap 文档内搜索 `FailedAssetLoads`、`AssetScanFailure`、`Request.IsFullScan()` 仍然没有直接 editor test 命中；说明扫描器的 full-scan 顶层路径和 commandlet 返回码 `3` 还未进入任何现有用例 |
| `SourceNavigation` 新增事实 | 该目录唯一 editor test 仍然依赖 `AcquireProductionLikeEngine()`，但模块名/文件名没有唯一化；这不是“未来可能冲突”的推断，而是由 `AcquireProductionLikeEngine()` 可复用运行中引擎、`AngelscriptPreprocessor::AddFile()` 由文件名生成模块名两处实现共同决定的已验证隔离风险 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-23 |
| BadIsolation | 1 | Issue-24 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 1, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 00:54:25 +08:00)

### 一、现有测试问题

#### Issue-25：`CommandletInvalidFile` 把“缺失文件”硬编码成 `J:/...`，带入了平台/环境假设

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.CommandletInvalidFile` |
| 行号范围 | 337-348 |
| 问题描述 | 用例把缺失输入固定写成 `ChangedScriptFile=J:/Missing/DoesNotExist.txt`。根据规则，测试不应硬编码平台相关路径或依赖外部机器约定；这里既假设了 Windows drive-letter 语法，也假设 `J:/Missing/DoesNotExist.txt` 在所有执行环境里都不存在。 |
| 影响 | 一旦测试机恰好存在这一路径，或未来把 editor tests 跑到非 Windows/不同挂载语义的环境里，这个用例就会从“参数错误路径”退化成环境偶然性测试，失败原因和业务契约脱钩。 |
| 修复建议 | 改成在 `FPaths::ProjectSavedDir()/Automation` 下生成唯一的临时文件名，显式保证该文件不存在后再传给 commandlet；这样既保留“缺失文件”语义，也去掉 drive-letter 与外部卷布局假设。 |

#### Issue-26：`CommandletInvalidFile` 只看退出码，没有守住“失败后不得继续扫描”的早退契约

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.CommandletInvalidFile` |
| 行号范围 | 337-348 |
| 问题描述 | `UAngelscriptBlueprintImpactScanCommandlet::Main()` 在 `TryReadChangedScriptsFile()` 失败时会直接返回 `InvalidArguments`，按实现不应继续 `LoadModuleChecked<FAssetRegistryModule>` 或调用 `ScanBlueprintAssets()`。当前用例只断言返回码为 `1`，没有观测 asset registry / scanner 是否完全未触发。即使未来回归成“先扫描再返回 1”，这条测试仍会保持绿色。 |
| 影响 | 参数校验失效后仍然触发 asset scan，会把无效输入误升级为昂贵副作用，污染日志和测试环境；但当前单测只能看见最终退出码，看不见提前终止语义是否被破坏。 |
| 修复建议 | 为 commandlet 增加仅测试可见的 scanner seam，或在 scanner 层引入可替换计数器；用例除了断言返回 `1`，还要断言 `ScanBlueprintAssets()` 调用次数为 `0`、asset registry 模块未被访问，从而把“错误返回码”和“无副作用早退”同时锁住。 |

### 二、需要新增的测试

#### NewTest-38：覆盖 `EditorModule` 在 restart 场景下的 `OnPostEngineInit` 回调去重

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h` |
| 关联函数 | `FAngelscriptEditorModule::StartupModule`, `ShutdownModule`, `OnEngineInitDone` |
| 现有测试覆盖 | 完全无测试；当前 gap 文档已有 `OnEngineInitDone` 单次触发建议，但还没有 module `Startup → Shutdown → Startup` 后 `FCoreDelegates::OnPostEngineInit` 注册数量的覆盖 |
| 风险评估 | `StartupModule()` 第 361 行每次都会 `AddStatic(&OnEngineInitDone)`，而 `ShutdownModule()` 没有对应移除；如果重复启动模块，后续一次 `OnPostEngineInit` 广播可能执行多次，造成 data source 重复创建/重复激活，属于典型 editor 生命周期泄漏 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.OnPostEngineInitDoesNotDuplicateAcrossRestart` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleLifecycleTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorModuleOnPostEngineInitTests.cpp` |
| 场景描述 | 启动一次 module 并广播 `FCoreDelegates::OnPostEngineInit`，记录 `AngelscriptData` data source 创建/激活次数；随后执行 `ShutdownModule()`，重新 `StartupModule()` 并再次广播同一 delegate，验证 restart 后每次广播仍只产生一次有效初始化 |
| 输入/前置 | 使用 module lifecycle helper；提供 `UContentBrowserDataSubsystem` spy 或 data-source activation seam；通过 `FindObject<UAngelscriptContentBrowserDataSource>` 统计 transient package 下的同名 data source 数量 |
| 期望行为 | 第一次广播后存在且仅存在一个 `AngelscriptData` data source；第二次 `StartupModule()` 后再次广播，创建数量不增加为 2，激活计数也只增加 1 次；`ShutdownModule()` 后不会留下导致重复初始化的悬挂 callback |
| 使用的 Helper | `FAngelscriptTestFixture` + module lifecycle helper + content-browser activation spy |
| 优先级 | P1 |

#### NewTest-39：覆盖 `ScriptAssetMenuExtension` 对 `UObject` 首参与 `FAssetData` 首参的分流

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h` |
| 关联函数 | `UScriptAssetMenuExtension::CallFunctionOnSelection` |
| 现有测试覆盖 | 完全无测试；已有 `ScriptEditorMenuExtension` / `ScriptEditorPrompts` 建议集中在注册与 prompt 转发，没有单独覆盖第 48-66 行 `bFunctionTakesObject` 的 object-vs-struct 分流 |
| 风险评估 | 当前实现用 `ObjectField->PropertyClass == SupportedClass` 判定 object 路径，而选择集过滤用的是 `Asset.IsInstanceOf(SupportedClass)`；一旦函数首参是 `UObject` 或与 `SupportedClasses` 存在父子类关系但不完全相等，菜单动作可能错误地走 `FAssetData` struct prompt，而不是把真实 asset object 传进去 |
| 建议测试名 | `Angelscript.TestModule.Editor.ScriptAssetMenuExtension.CallFunctionOnSelectionRoutesObjectsBeforeAssetData` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptScriptAssetMenuExtensionTests.cpp` |
| 场景描述 | 构造一个 test `UScriptAssetMenuExtension` 子类，`SupportedClasses` 设为 `UDataAsset`；准备一个选中的 `UDataAsset`/派生资产，再分别调用两个测试函数：一个首参为 `UObject*`，一个首参为 `FAssetData` |
| 输入/前置 | 使用 prompt spy seam 分别记录 `FScriptEditorPrompts::ShowPromptToCallFunction` 的 object overload 与 struct overload 调用次数和收到的参数个数；构造 `Selection.SelectedAssets` 只包含一个支持的 asset |
| 期望行为 | 对 `UObject*` 首参函数，应只调用 object overload，且传入 `CallWithObjects.Num() == 1`、struct overload 调用次数为 `0`；对 `FAssetData` 首参函数，应只调用 struct overload，且 `CallWithStructs.Num() == 1`、object overload 调用次数为 `0` |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScriptEditorPrompts` spy seam + transient asset helper |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 当前源码清点 | 重新按当前仓库扫描 `Plugins/Angelscript/Source/AngelscriptEditor/`（排除 `Private/Tests` 与 `ThirdParty`）后，实际生产源码为 `29` 个文件：`13 .cpp + 16 .h`；与任务描述里的 `15 .cpp + 16 .h = 31` 不一致，说明 `.cpp` 数量基线已过时 |
| 现有测试命中面 | 当前 3 个测试文件共 `17` 个 automation 用例，只直接覆盖 `SourceNavigation`（1 文件 / 1 用例）、`DirectoryWatcherInternal`（2 文件 / 5 用例）、`BlueprintImpactScanner + Commandlet`（4 文件 / 11 用例）；整体 direct coverage 仍是 `7 / 29` |
| 用户点名区域清点 | `EditorModule` family `3 / 3` 无 direct test（`AngelscriptEditorModule.cpp/.h`, `AngelscriptEditorStateDump.cpp`）；`ClassReloadHelper` `2 / 2` 无 direct test；`ContentBrowserDataSource` `2 / 2` 无 direct test；`EditorMenuExtensions` `8 / 8` 无 direct test |
| 计数差异的已验证原因 | 当前树里 `Private/FunctionLibraries/AssetToolsStatics.h` 与 `Private/FunctionLibraries/EditorStatics.h` 都是 header-only wrapper，仓库本就没有对应 `.cpp`；因此本地实扫结果自然是 `13 .cpp`，不是提示里的 `15 .cpp` |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-25 |
| WeakAssertion | 1 | Issue-26 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
 
---

## 测试审查 (2026-04-09 01:20)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-43：补齐 `ScanBlueprintAssets` 的无影响路径，验证“changed script 不命中任何 module”不会误报

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets`, `FindModulesForChangedScripts` |
| 现有测试覆盖 | 现有 3 个 editor 测试文件里，scanner 只有“正向命中 module / blueprint”的用例；目录外 scenario test 也没有直接覆盖“changed scripts 与所有 active modules 都不匹配”这一条无影响路径 |
| 风险评估 | 如果筛选逻辑回归成“无命中 module 时退化为 full scan”或沿用脏的 symbol 集，commandlet 会把完全无关的 blueprint 误报成受影响；当前 direct tests 看不到这种 false positive |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.ScanBlueprintAssets.NoMatchingModulesProducesNoMatches` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScanTests.cpp`；若文件接近 500 行则拆成 `AngelscriptBlueprintImpactScanNoImpactTests.cpp` |
| 场景描述 | 准备一个 active script module 和一个真实依赖该 module symbol 的 blueprint asset，但 `Request.ChangedScripts` 传入一条不会命中任何 `CodeSection.RelativeFilename` 的脚本路径，然后执行 `ScanBlueprintAssets()` |
| 输入/前置 | 复用现有 module builder + disk-backed blueprint helper；asset registry 重新扫描该 blueprint；`Request.ChangedScripts = { "Scripts/Gameplay/Unrelated.as" }` |
| 期望行为 | `Result.NormalizedChangedScripts.Num() == 1`；`Result.MatchingModules.Num() == 0`；`Result.Symbols.IsEmpty()` 返回 `true`；`Result.Matches.Num() == 0`；`Result.FailedAssetLoads == 0` |
| 使用的 Helper | `FAngelscriptTestFixture` + 现有 module builder + disk-backed blueprint helper |
| 优先级 | P1 |

#### NewTest-44：补齐 `FindModulesForChangedScripts` 的“同一 module 多 section 命中”去重路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::FindModulesForChangedScripts` |
| 现有测试覆盖 | 当前 `MatchChangedScriptsToModuleSections` 只覆盖“一条 changed script 命中一个 module 的一个 section”，没有覆盖同一 module 有多段 code section 同时命中的边界情况 |
| 风险评估 | 一旦 `break` 或 module 收集逻辑回归，同一 module 可能被重复加入 `MatchingModules`，进而让扫描统计、commandlet 日志，甚至后续基于 module 数量的诊断输出膨胀；当前用例不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.FindModulesForChangedScripts.DeduplicatesModulesAcrossMultipleMatchingSections` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerCoreTests.cpp` |
| 场景描述 | 构造一个 `Gameplay.Enemy` module，内含 `Scripts/Gameplay/Enemy.as` 与 `Scripts/Gameplay/EnemyAbilities.as` 两个 section；再传入同时包含这两条脚本的 `ChangedScripts`，并放入一个无关 `Gameplay.Npc` module 作为对照 |
| 输入/前置 | 复用当前文件里的 `MakeTestModule()` / `MakeCodeSection()` helper；`ChangedScripts = { "scripts/gameplay/enemy.as", "scripts/gameplay/enemyabilities.as" }` |
| 期望行为 | 返回数组 `Num() == 1`；唯一元素的 `ModuleName == "Gameplay.Enemy"`；不会出现重复 module，也不会夹带 `Gameplay.Npc` |
| 使用的 Helper | 纯 unit helper（`MakeTestModule` / `MakeCodeSection`）+ `IMPLEMENT_SIMPLE_AUTOMATION_TEST` |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮源码基线 | 按 `Plugins/Angelscript/Source/AngelscriptEditor/` 当前实扫结果，排除 `Private/Tests/`、`ThirdParty/` 和 `AngelscriptEditor.Build.cs` 后，生产源码共 `29` 个文件（`13 .cpp + 16 .h`） |
| 有 direct test 的源码文件（7） | `Private/AngelscriptSourceCodeNavigation.cpp` ← `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`; `Private/AngelscriptDirectoryWatcherInternal.cpp` / `Private/AngelscriptDirectoryWatcherInternal.h` ← `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` / `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` / `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h` / `Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h` ← `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 完全无 direct test 的源码文件（22） | `Private/AngelscriptContentBrowserDataSource.cpp/.h`; `Private/AngelscriptEditorModule.cpp/.h`; `Private/AngelscriptEditorStateDump.cpp`; `Private/ClassReloadHelper.cpp/.h`; `Private/FunctionLibraries/AssetToolsStatics.h`; `Private/FunctionLibraries/EditorStatics.h`; `Private/FunctionLibraries/ScriptableFactory.cpp/.h`; `Public/BaseClasses/ScriptEditorSubsystem.h`; `Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp/.h`; `Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp/.h`; `Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp/.h`; `Public/EditorMenuExtensions/ScriptEditorPrompts.cpp/.h`; `Public/FunctionLibraries/BlueprintMixinLibrary.h`; `Public/FunctionLibraries/EditorSubsystemLibrary.h` |
| `BlueprintImpactScannerTests` 关键场景矩阵 | 当前 11 个用例里，direct coverage 明确命中的只有“有影响”正路径（parent / variable / pin / node dependency / referenced asset / delegate / disk-backed asset / invalid file）；“无影响”在当前 3 个测试文件里仍是 `0` 直接用例；“部分影响”也仍是 `0` 直接用例，只在目录外 `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp` 有 scenario 级补充 |
| 用户点名热点复核 | `EditorModule`、`ClassReloadHelper`、`ContentBrowserDataSource`、`EditorMenuExtensions` 四组源码在当前 direct-test 统计下仍全部是 `0` 命中；这和前面已有新增测试建议一致，没有发现新的隐藏命中入口 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 01:14)

### 一、现有测试问题

#### Issue-27：`FindBlueprintAssetsDiskBacked` 把测试资产写进真实 `/Game` 内容根，副作用超出测试沙箱

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.FindBlueprintAssetsDiskBacked` |
| 行号范围 | 213-217, 493-519 |
| 问题描述 | `CreateDiskBackedBlueprintChild()` 把 `OutPackagePath` 固定写成 `/Game/Automation/<AssetName>`，随后 `SaveBlueprintToDisk()` 用 `FPackageName::LongPackageNameToFilename()` 把它落到项目 `Content` 根目录下的真实 package 文件。也就是说，这条测试不是在 `Saved/Automation` 一类临时位置构造磁盘夹具，而是在仓库内容目录里创建真实 asset，再依赖 teardown 删除它。 |
| 影响 | 一旦删除失败、产生 sidecar 文件、或 asset registry/source-control watcher 抢先观察到这份新资产，测试就会把工作区弄脏，并把 editor 内容扫描、内容浏览器刷新和 source control 状态耦合进本应用例；这超出了“验证 disk-only 过滤”本身需要的范围。 |
| 修复建议 | 把磁盘夹具迁到明确的测试隔离区：优先为测试创建唯一 `/Game/Automation/TestCoverage/<GUID>` 子路径并在 teardown 中递归清掉 package、目录和 registry 状态；若能提供临时 mount point，更应把落盘位置切到 `Saved/Automation` 对应的测试 mount，避免直接写入真实内容树。 |

---

## 测试审查 (2026-04-09 01:08:36 +08:00)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-41：覆盖 `ScriptEditorMenuExtension` 的分类排序与 menu/toolbar 构建分流

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `SortFunctionsByCategory`, `UScriptEditorMenuExtension::BuildMenu`, `BuildToolMenuSection` |
| 现有测试覆盖 | 现有建议覆盖注册、默认挂点、action metadata 与 prompt 转发，但第 459-667 行的 `Category` / `SortOrder` 层级整理、submenu 递归，以及 toolbar 下 `InitComboButton` 的分流仍完全没有单独建议 |
| 风险评估 | 一旦分类层级打平、`SortOrder` 被忽略、或 toolbar 场景错误地继续走菜单型 submenu，editor UI 会出现错序、丢分组或按钮布局错乱；这些都是纯 UI 结构回归，现有任何测试都看不到 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.BuildMenuAndToolMenuSectionRespectCategoryHierarchyAndSortOrder` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorMenuExtensionCategoryTests.cpp` |
| 场景描述 | 定义一个 test `UScriptEditorMenuExtension` 子类，声明 4 个 `CallInEditor` 函数，分别带 `Category="Tools|Bake"`、`Category="Tools|Audit"`、空 category，以及不同 `SortOrder`；分别调用 `BuildMenu()` 和 `BuildToolMenuSection()`，覆盖普通 menu 与 toolbar 两条构建路径 |
| 输入/前置 | 使用 `FAngelscriptTestFixture`；准备 `FMenuBuilder` / `UToolMenu` 可观察夹具或通过 menu-entry spy seam 记录 section、submenu、entry label 顺序；其中 toolbar 路径令 `bIsMenu == false` 以命中 `InitComboButton` 分支 |
| 期望行为 | 空 category 函数直接出现在顶层 section；`Tools` 下生成两个按字母序排列的子分类 `Audit`、`Bake`；同一分类内函数按 `SortOrder` 升序、再按函数名排序；`bIsMenu == true` 时使用 submenu 结构，`bIsMenu == false` 时顶层分类生成为 combo button 而不是普通 entry |
| 使用的 Helper | `FAngelscriptTestFixture` + test extension subclass + menu/toolmenu spy seam |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 11:37:42 +08:00)

### 一、现有测试问题

#### Issue-32：`FolderAddScansContainedScripts` 忽略磁盘夹具创建结果，失败时会把 setup 问题误判成扫描回归

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts` |
| 行号范围 | 141-144, 156-158 |
| 问题描述 | 该用例是当前 `DirectoryWatcher` 组里少数真正依赖磁盘枚举的场景，但它对 `FileManager.MakeDirectory()` 和三次 `FFileHelper::SaveStringToFile()` 的返回值完全不做校验。若目录或文件夹具创建失败，测试最终只会在 `QueueScriptFileChanges()` 之后报“没有扫到 2 个脚本”，而不会指出真正失败的是前置 fixture。 |
| 影响 | 当 CI 磁盘权限、长路径或杀毒占用导致夹具创建失败时，失败会被误归因到 `FindScriptFiles()` / `QueueScriptFileChanges()`；这会稀释该用例对真实 folder-add 行为的诊断价值，也让异步文件系统问题更难定位。 |
| 修复建议 | 在 `MakeDirectory()` 与每次 `SaveStringToFile()` 后立即用 `if (!TestTrue(...)) { return false; }` 守住夹具创建；若后续按 `DirectoryWatcher` 规则拆 helper，可提取 `CreateWatcherFixtureFile(Path, Contents)` 统一返回 `bool`，把 setup 失败和被测逻辑失败明确分层。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无 | 0 | 无 |

---

## 测试审查 (2026-04-09 11:38:28 +08:00)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-54：覆盖 `RegisterSettings` 的 project-settings 生命周期对称性

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h` |
| 关联函数 | `FAngelscriptEditorModule::StartupModule`, `ShutdownModule` |
| 现有测试覆盖 | 现有 lifecycle 建议已覆盖 watcher、`OnObjectPreSave`、runtime bridge delegates、`ReloadTags`、`OnPostEngineInit` 与 tool menu owner，但还没有任何建议直接验证第 384-390 行 `SettingsModule->RegisterSettings("Project", "Plugins", "Angelscript", ...)` 这条 project-settings 注册路径 |
| 风险评估 | 如果模块重启后 settings section 残留或重复注册，`Project -> Plugins -> Angelscript` 会出现 stale 页面或重复入口；当前 `ShutdownModule()` 只移除 pre-save、state dump 和 tool menu callback，并没有任何 direct test 守住 settings 生命周期对称性 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.ProjectSettingsEntryMatchesModuleLifetime` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleLifecycleTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorModuleSettingsTests.cpp` |
| 场景描述 | 先加载 `Settings` 模块并通过本地 `FindSettingsSection("Project", "Plugins", "Angelscript")` helper 查询 section 状态；执行一次 `StartupModule()`，确认该 section 出现且 `GetSettingsObject()` 指向 `UAngelscriptSettings`；随后执行 `ShutdownModule()` 并再次查询；最后再做一次 `Startup -> Shutdown -> Startup` restart 对照，确认不会生成第二份 settings entry |
| 输入/前置 | 使用 editor fixture；提供 `ISettingsModule` section lookup helper，内部按 `Project/Plugins/Angelscript` 路径查找 `ISettingsSectionPtr`；测试结束恢复模块状态，避免污染后续 editor-module tests |
| 期望行为 | 首次 startup 后能查到唯一的 `Angelscript` section，且 settings object 非空并为 `UAngelscriptSettings`；shutdown 后该 section 消失；restart 后重新出现且仍只有 1 份，不会累积重复条目 |
| 使用的 Helper | `FAngelscriptTestFixture` + `ISettingsModule` section lookup helper + module lifecycle fixture |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 31 文件拆解 | 重新实扫 `Plugins/Angelscript/Source/AngelscriptEditor/` 下全部 `.cpp/.h` 后，模块树当前是 `31` 个文件：其中生产源码 `29` 个，模块内测试 `2` 个（`Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp`, `Private/Tests/AngelscriptDirectoryWatcherTests.cpp`） |
| 3 个现有测试文件 -> 直接命中关系 | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`（1 用例）只 direct hit `Private/AngelscriptSourceCodeNavigation.cpp`；`Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp`（11 用例）direct hit `Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`, `Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`, `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`, `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h`；`Private/Tests/AngelscriptDirectoryWatcherTests.cpp`（5 用例）direct hit `Private/AngelscriptDirectoryWatcherInternal.cpp`, `Private/AngelscriptDirectoryWatcherInternal.h` |
| 已有 direct test 的生产文件（7） | `Private/AngelscriptSourceCodeNavigation.cpp`; `Private/AngelscriptDirectoryWatcherInternal.cpp`; `Private/AngelscriptDirectoryWatcherInternal.h`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`; `Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`; `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h` |
| 完全无 direct test 的生产文件（22） | `Private/AngelscriptContentBrowserDataSource.cpp`; `Private/AngelscriptContentBrowserDataSource.h`; `Private/AngelscriptEditorModule.cpp`; `Private/AngelscriptEditorModule.h`; `Private/AngelscriptEditorStateDump.cpp`; `Private/ClassReloadHelper.cpp`; `Private/ClassReloadHelper.h`; `Private/FunctionLibraries/AssetToolsStatics.h`; `Private/FunctionLibraries/EditorStatics.h`; `Private/FunctionLibraries/ScriptableFactory.cpp`; `Private/FunctionLibraries/ScriptableFactory.h`; `Public/BaseClasses/ScriptEditorSubsystem.h`; `Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptActorMenuExtension.h`; `Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptAssetMenuExtension.h`; `Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`; `Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`; `Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`; `Public/EditorMenuExtensions/ScriptEditorPrompts.h`; `Public/FunctionLibraries/BlueprintMixinLibrary.h`; `Public/FunctionLibraries/EditorSubsystemLibrary.h` |
| 用户点名区域复核 | `EditorModule` family 当前仍是 `3 / 3` 生产文件无 direct test（`Private/AngelscriptEditorModule.cpp/.h`, `Private/AngelscriptEditorStateDump.cpp`）；`ClassReloadHelper` `2 / 2` 无 direct test；`ContentBrowserDataSource` `2 / 2` 无 direct test；`EditorMenuExtensions` `8 / 8` 无 direct test。当前 3 个现有测试文件没有一条直接落到这些用户点名文件上 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 11:56:25 +08:00)

### 一、现有测试问题

#### Issue-33：`CreateDiskBackedBlueprintChild` 在 `CreateBlueprint()` 失败分支泄漏磁盘 package

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.FindBlueprintAssetsDiskBacked` |
| 行号范围 | 213-244 |
| 问题描述 | `CreateDiskBackedBlueprintChild()` 先 `CreatePackage(*OutPackagePath)`，但如果第 223-230 行 `FKismetEditorUtilities::CreateBlueprint()` 返回 `nullptr`，helper 会在第 231-234 行直接返回空指针，没有像 `CreateTransientBlueprintChild()` 那样回收已创建的 package。当前文档前面只记录了 `AssetCreated()` 之后和 `SavePackage()` 失败之后的泄漏；这个更早的失败分支同样会把 `/Game/Automation/...` package 留在进程里。 |
| 影响 | 一旦 blueprint 创建因为父类状态、editor 初始化顺序或工厂回归而失败，后续测试会继承一个未清理的 disk-backed package；这会把真正的夹具构建失败，混淆成 asset registry 或 `FindBlueprintAssets()` 的回归。 |
| 修复建议 | 在 `Blueprint == nullptr` 分支显式把 `Package` 标记为 garbage 并执行最小 cleanup，或者把 helper 改成 RAII/输出参数模式，让 `ON_SCOPE_EXIT` 总能拿到 package 并统一清理。若继续保留 `/Game/Automation` 落盘策略，还应把 package cleanup 与磁盘文件 cleanup 放进同一个 helper，避免不同失败阶段走出不同的半清理状态。 |

### 二、需要新增的测试

#### NewTest-55：覆盖 `OnLiteralAssetSaved` 的曲线文本序列化内容，而不只是在 pre-save 路由上打点

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `OnLiteralAssetSaved` |
| 现有测试覆盖 | 当前已有建议只覆盖 `OnLiteralAssetPreSave` 的包过滤与“是否触发 `ReplaceScriptAssetContent`”的路由判定；`OnLiteralAssetSaved()` 第 121-331 行内部对 `UCurveFloat` 的 graph comment、key serialization、`DefaultValue`、`Pre/PostInfinityExtrap` 生成逻辑仍然没有任何 direct test |
| 风险评估 | 即使 pre-save 钩子还会命中，曲线 literal 真正写回脚本文本的内容仍可能悄悄回归，例如漏写 `AddLinearCurveKey(...)`、丢失 `DefaultValue`、或把 infinity 模式写错；这类问题会直接污染 Angelscript literal asset 内容，而现有建议不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.OnLiteralAssetSavedSerializesCurveKeysDefaultValueAndInfinityModes` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleLiteralAssetTests.cpp` |
| 场景描述 | 在 `FAngelscriptEngine::Get().AssetsPackage` 下创建一个 `UCurveFloat`，填入至少一条 `Linear` key、一个非 `MAX_flt` 的 `DefaultValue`、以及 `PreInfinityExtrap/PostInfinityExtrap`；保证 debug server client gate 通过后，直接触发 `OnLiteralAssetSaved()` 或通过公开 test seam 走同一实现 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 或等价 editor fixture；为 `FAngelscriptEngine::ReplaceScriptAssetContent` 和 `FMessageDialog::Open` 提供 spy/seam；必要时提供 debug-server-client gate seam，避免 UI 干扰 |
| 期望行为 | `ReplaceScriptAssetContent` 恰好调用 1 次；`NewContent` 中至少包含一条 `AddLinearCurveKey(Time, Value);`、一条 `DefaultValue = ...;`、以及正确的 `PreInfinityExtrap = ...;` / `PostInfinityExtrap = ...;` 文本；在有 debug client 的前提下不弹出错误对话框 |
| 使用的 Helper | engine fixture + `ReplaceScriptAssetContent` spy + debug-client gate seam + literal curve helper |
| 优先级 | P1 |

#### NewTest-56：补齐 `ContentBrowserDataSource` 对失效 payload 的拒绝路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `UAngelscriptContentBrowserDataSource::GetItemAttribute`, `Legacy_TryGetAssetData`, `UpdateThumbnail` |
| 现有测试覆盖 | 当前已有建议覆盖 canonical item 的正向 attribute / legacy lookup / thumbnail 路径，以及 foreign item 负例，但还没有任何建议验证 `CreateAssetItem()` 生成的 payload 在 `TWeakObjectPtr` 失效后是否会被安全拒绝 |
| 风险评估 | script asset 一旦被删除、GC 或 hot reload 替换，content browser 里遗留的 item 可能携带失效 weak payload；如果这些 API 继续返回 `true` 或写入无效 `FAssetData`，会把 stale item 伪装成有效资产，造成错误缩略图、错误引用或悬挂路径 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.RejectsStalePayloadAcrossAttributesLegacyLookupAndThumbnail` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceTests.cpp`；若文件接近 500 行则拆成 `AngelscriptContentBrowserDataSourcePayloadTests.cpp` |
| 场景描述 | 先通过真实 script asset 创建一个 canonical `FContentBrowserItemData`，随后把底层资产标记为 garbage 并 `CollectGarbage()`，制造 payload 的 `TWeakObjectPtr` 失效；然后分别调用 `GetItemAttribute()`、`Legacy_TryGetAssetData()` 与 `UpdateThumbnail()` |
| 输入/前置 | 复用现有 data-source 初始化 helper 与 script asset helper；准备一个 `FAssetThumbnail` 初始目标作为“不得被改写”的对照值；必要时提供 `CollectGarbage` 后的 payload-validity helper |
| 期望行为 | 资产失效后 `GetItemAttribute()` 返回 `false`；`Legacy_TryGetAssetData()` 返回 `false` 且不输出伪造的 `FAssetData`；`UpdateThumbnail()` 返回 `false`，不会把 thumbnail 指向空资产或覆盖原有目标 |
| 使用的 Helper | `FAngelscriptTestFixture` + content-browser asset helper + GC helper + thumbnail spy |
| 优先级 | P1 |

#### NewTest-57：覆盖 `UScriptEditorMenuExtension::GetWorld()` 的 editor-world 绑定

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `UScriptEditorMenuExtension::GetWorld` |
| 现有测试覆盖 | 当前 `EditorMenuExtensions` 相关建议集中在注册、分类排序、selection forwarding、prompt routing 和 lifecycle；`UScriptEditorMenuExtension::GetWorld()` 在目标测试目录里仍是 `0` 直接命中 |
| 风险评估 | menu extension 的 `CallInEditor` 函数、prompt 流程以及任何依赖 `GetWorld()` 的 blueprint/script 调用，都默认这个对象能解析到 editor world；如果这里回归为 `nullptr` 或错误 world，菜单仍然会显示，但执行期上下文会静默失效 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.GetWorldReturnsCurrentEditorWorld` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorMenuExtensionContextTests.cpp` |
| 场景描述 | 在 editor fixture 下实例化一个最小 test `UScriptEditorMenuExtension` 子类，直接调用 `GetWorld()`；再用临时 world/context 切换做一次对照，确保返回值跟随 `GEditor->GetEditorWorldContext().World()` |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 editor world fixture；如果仓库已有 world-context RAII helper，则复用它切换当前 editor world |
| 期望行为 | `GetWorld()` 返回值与 `GEditor->GetEditorWorldContext().World()` 指针完全一致，且在 editor context 有效时不为 `nullptr`；切换 editor world 后返回值同步变化 |
| 使用的 Helper | `FAngelscriptTestFixture` + editor world context helper |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮函数级 0-hit 复核 | 对 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 执行全文检索后，`UScriptEditorMenuExtension::GetWorld`、`GetToolMenuContext(`、`Legacy_TryGetAssetData`、`Legacy_TryGetPackagePath`、`OnLiteralAssetSaved`、`ReplaceScriptAssetContent` 在目标测试树中仍是 `0` 直接命中 |
| 本轮现有测试质量新事实 | `FindBlueprintAssetsDiskBacked` 依赖的 `CreateDiskBackedBlueprintChild()` 现在已确认至少存在 3 条独立失败清理缺口：`CreateBlueprint()` 失败、`AssetCreated()` 后保存失败、以及成功路径结束后 asset registry 残留；说明当前唯一 disk-backed 用例的 helper 仍然比断言本身更脆弱 |
| 用户点名区域增量定位 | 相比前面已记录的 lifecycle / registration 缺口，本轮新增的 direct-test 空白更细化到了 `EditorModule` 的 literal-asset serialization body、`ContentBrowserDataSource` 的 stale-payload error path、以及 `ScriptEditorMenuExtension` 的 editor-world context helper，这三块此前尚未以独立测试规格写入文档 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-33 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P2 | 1 | NoTestForSource: 1 |


---

## 测试审查 (2026-04-09 12:24:30 +08:00)

### 一、现有测试问题

#### Issue-34：DirectoryWatcherTests 的临时目录 teardown 吞掉 DeleteDirectory 失败，cleanup 脏数据不会暴露

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp |
| 测试名 | Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove, Angelscript.Editor.DirectoryWatcher.Queue.IgnoresNonScriptFiles, Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts, Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator, Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd |
| 行号范围 | 79-82, 109-112, 136-139, 166-169, 199-202 |
| 问题描述 | 这 5 个用例都会在 Saved/Automation/DirectoryWatcherTests/<Guid> 下创建独立临时根目录，但 ON_SCOPE_EXIT 里只调用 FileManager.DeleteDirectory(*RootPath, false, true)，完全忽略返回值，也没有在删除后复查目录是否仍存在。若杀毒、索引器或 editor 句柄占用导致删除失败，测试仍然保持绿色，只把历史目录悄悄留在磁盘上。 |
| 影响 | 文件系统类测试的隔离边界会被上一次运行残留的数据侵蚀；后续若又有用例扫描 Saved/Automation 或复用相同子目录结构，失败会表现成难以解释的 folder 枚举噪声，而不是明确的 teardown 失败。 |
| 修复建议 | 把目录删除封装成统一的 cleanup helper，在 ON_SCOPE_EXIT 中执行 DeleteDirectory 后立即复查 DirectoryExists(*RootPath)；若删除失败，用 AddError 或 TestTrue 明确报出未清理路径。若担心 scope-exit 中断流程，可先收集 cleanup failure，再在函数尾统一断言。 |

### 二、需要新增的测试

#### NewTest-58：覆盖 `GetToolMenuContext` 只在 tool-menu 执行窗口内暴露当前上下文

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `UScriptEditorMenuExtension::GetToolMenuContext`, `BuildToolMenuSection`, `CreateToolUIAction` |
| 现有测试覆盖 | 当前 `EditorMenuExtensions` 相关建议覆盖注册、分类排序、action metadata、prompt routing 和 `GetWorld()`；`GetToolMenuContext()` 在目标测试树里仍是 `0` 直接命中，也没有任何建议验证 `ActiveToolMenuContext` 的作用域只在 tool-menu callback 内有效 |
| 风险评估 | 依赖 tool-menu context 的 `ActionCanExecute` / `ActionIsVisible` / `CallInEditor` 函数，即使菜单仍能显示，也可能在执行期拿到 `nullptr` 或旧上下文对象；这会让 content browser / level editor 菜单静默失效，而现有建议不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.GetToolMenuContextExposesActiveContextOnlyDuringToolMenuCallbacks` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorMenuExtensionContextTests.cpp` |
| 场景描述 | 定义一个最小 test `UScriptEditorMenuExtension` 子类，提供一个 `CallInEditor` 函数和一个 `ActionCanExecute` helper，二者都会调用 `GetToolMenuContext(UTestToolMenuContextObject::StaticClass())` 并把看到的对象记录下来；手工构造携带 `UTestToolMenuContextObject` 的 `FToolMenuSection.Context`，执行一次 `BuildToolMenuSection()` / `CreateToolUIAction()` 驱动的 tool-menu 回调，再在回调外直接查询 `GetToolMenuContext()` 作为对照 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或等价 editor fixture；提供可插入 `FToolMenuContext` 的最小 `UTestToolMenuContextObject`；若已有 tool-menu 构造 helper，则复用它触发 `FToolMenuExecuteAction` / `FToolMenuCanExecuteAction` |
| 期望行为 | 在 `ActionCanExecute` 和实际执行回调内部，`GetToolMenuContext()` 返回的对象与放入 `FToolMenuSection.Context` 的实例完全一致；回调结束后再次直接调用时返回 `nullptr`；请求不匹配的 `ContextClass` 时同样返回 `nullptr` |
| 使用的 Helper | `FAngelscriptTestFixture` + tool-menu section helper + context spy object |
| 优先级 | P1 |

#### NewTest-59：覆盖 `ClassReloadHelper` 为新 volume class 注册 actor factory 并广播 placeable 刷新

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 当前 `ClassReloadHelper` 建议集覆盖 `ReloadState` 聚合、`GetTablesDependentOnStruct`、enum/literal asset reload 和 open-editor reference replacement；第 340-383 行针对 `NewClasses` 中 volume 子类的 actor factory 生成与 `IPlacementModeModule` 广播仍然没有任何独立测试 |
| 风险评估 | 热重载后如果新 volume 类没有生成对应 `UActorFactory`，或 placeable 资产广播没有触发，编辑器的放置面板和关卡工具栏会继续使用旧缓存，导致新 volume 脚本类“编译成功但无法放置” |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceRegistersActorFactoriesAndPlacementBroadcastsForNewVolumes` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperVolumeTests.cpp` |
| 场景描述 | 在 editor fixture 下准备一个最小的 transient volume 子类并放入 `FClassReloadHelper::ReloadState().NewClasses`，同时保证 `FAngelscriptEngine::Get().bIsInitialCompileFinished == true`；记录调用前 `GEditor->ActorFactories` 中指向该类的 factory 数量，并给 `IPlacementModeModule::Get().OnAllPlaceableAssetsChanged()`、`OnPlaceableItemFilteringChanged()` 挂测试计数器后执行 `PerformReinstance()` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 或等价 editor fixture；提供 volume test class helper；在测试结束时恢复 `ReloadState()`、移除 placement delegates，并清理新增的 actor factory，避免污染其他 editor tests |
| 期望行为 | `PerformReinstance()` 后 `GEditor->ActorFactories` 中至少新增 1 个 `NewActorClass == TestVolumeClass` 的 factory；`OnAllPlaceableAssetsChanged` 与 `OnPlaceableItemFilteringChanged` 各触发 1 次；若重复执行前先清空 `NewClasses`，则不会再次广播或重复新增 factory |
| 使用的 Helper | engine/editor fixture + transient volume class helper + placement delegate counter + actor-factory snapshot helper |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮现有测试质量新事实 | `DirectoryWatcherTests.cpp` 当前不仅 setup 断言不足，teardown 也没有任何删除结果校验；这意味着该组用例对“文件系统事件 cleanup 是否真的完成”仍然没有可观测保障，和用户要求优先复核的 cleanup 方向直接相关 |
| `EditorMenuExtensions` 新增空白点 | 目标测试目录与现有 gap 文档内搜索 `GetToolMenuContext(` 之后，仍只有函数名统计，没有任何独立测试规格；而源码里它被 `BuildToolMenuSection()`、`CreateToolUIAction()` 的多个 callback 直接依赖，属于仍未被具象化的 0-hit 上下文桥接点 |
| `ClassReloadHelper` 新增空白点 | 目标测试目录与现有 gap 文档内搜索 `ActorFactories`、`OnAllPlaceableAssetsChanged`、`OnPlaceableItemFilteringChanged` 仍是 `0` 命中，说明 hot reload 后“新 volume class 是否真正进入 editor 放置体系”这条可见行为链还没有测试建议守住 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:45:13 +08:00)

### 一、现有测试问题

#### Issue-35：`FindBlueprintAssetsDiskBacked` 的磁盘 teardown 吞掉 `Delete` 失败，真实 content 资产残留不会暴露

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.FindBlueprintAssetsDiskBacked` |
| 行号范围 | 498-503 |
| 问题描述 | 该用例在 `ON_SCOPE_EXIT` 中对真正落到 `/Game/Automation/...` 的 `.uasset` 只调用 `IFileManager::Get().Delete(*PackageFilename, false, true, true)`，完全忽略返回值，也没有在删除后复查文件是否仍存在。结合前面已经确认的“测试直接写入项目内容根”事实，这意味着一旦文件被 editor/asset registry/杀毒软件占用，测试仍然会保持绿色，只把真实内容资产悄悄留在仓库工作区。 |
| 影响 | cleanup 失败时，后续任何依赖 `/Game/Automation` 扫描、asset registry 或 content browser 的 editor 测试都可能读到历史脏资产；同时这会把真正的 teardown 失败伪装成后续功能回归，排查成本很高。 |
| 修复建议 | 把文件删除封装成带断言的 cleanup helper：先 `if (!Delete(...)) { AddError(...); }`，再用 `FileExists(*PackageFilename)` 复查磁盘状态；若删除失败，还应同步移除 asset registry 条目或把 package path 记录到统一的 test cleanup 队列，避免把真实 `/Game` 内容残留给下一条用例。 |

#### Issue-36：`DirectoryWatcher` 的 folder add/remove 用例没有守住“另一条队列必须为空”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator` |
| 行号范围 | 156-158, 189-191 |
| 问题描述 | `QueueScriptFileChanges()` 同时维护 `FileChangesDetectedForReload` 和 `FileDeletionsDetectedForReload` 两条队列，但这两个用例只断言了“当前操作应该写入的那一条”。`FolderAddScansContainedScripts` 只检查 additions，完全不检查 deletions 是否仍为 `0`；`FolderRemoveUsesLoadedScriptEnumerator` 只检查 deletions，也没有断言 additions 为空。若后续实现把 folder-added / folder-removed 事件同时错误地写进两条队列，现有测试仍可能保持绿色。 |
| 影响 | add/remove 分支一旦发生串扰，真实 editor reload 会同时看到“新增”和“删除”同一路径，导致热重载抖动或重复处理；当前测试却只能证明“正确队列里至少有东西”，无法证明错误队列没有被污染。 |
| 修复建议 | 在两个用例里都补充 opposite-queue 断言：folder-add 后 `FileDeletionsDetectedForReload.Num() == 0`，folder-remove 后 `FileChangesDetectedForReload.Num() == 0`。若要进一步守住回调语义，可顺手给 folder-remove 的 enumerator 加调用计数并断言恰好执行 1 次。 |

### 二、需要新增的测试

#### NewTest-60：覆盖 `ScriptActorMenuExtension` 的 selection gate 与首参类型过滤

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.h` |
| 关联函数 | `UScriptActorMenuExtension::GatherExtensionFunctions` |
| 现有测试覆盖 | 当前已有建议覆盖 `CallFunctionOnSelection` 的 prompt routing 和通用 menu registration，但第 4-75 行 `SupportsActor`、`SupportedClasses` 与首个 object 参数 class 的联合过滤仍然没有任何独立测试规格 |
| 风险评估 | 如果菜单扩展把不支持的 actor selection 也当作可执行对象，或没有按首参 class 过滤命令，关卡视口会显示本不该出现的 script action；反过来，受支持 actor 也可能被错误过滤掉，直接影响 editor 可用性 |
| 建议测试名 | `Angelscript.TestModule.Editor.ScriptActorMenuExtension.GatherExtensionFunctionsFiltersUnsupportedSelectionAndParameterClass` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionSelectionTests.cpp` |
| 场景描述 | 定义一个最小 test `UScriptActorMenuExtension` 子类，覆写 `SupportsActor()` 只接受带特定 tag 的 actor，并提供三个 `UFUNCTION(CallInEditor)`：一个无参、一个首参为 `AStaticMeshActor*`、一个首参为 `ALight*`。先传入“只有不受支持 actor”的 selection，再传入“一个受支持的 `AStaticMeshActor` + 一个不受支持 `ALight`”的 mixed selection |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或等价 editor world fixture；在测试 world 中 spawn `AStaticMeshActor` / `APointLight`，给受支持样本设置 test tag；通过 helper 直接给 extension 填充 `CurrentSelection` |
| 期望行为 | unsupported-only selection 时 `GatherExtensionFunctions()` 返回空；mixed selection 时返回值只包含“无参函数”和“首参为 `AStaticMeshActor*` 的函数”，不包含 `ALight*` 函数；返回函数名集合要做精确比较，而不是只做 `Contains` |
| 使用的 Helper | `FAngelscriptTestFixture` + `FScopedTestWorldContextScope` + transient actor/menu-extension helper |
| 优先级 | P2 |

#### NewTest-61：覆盖 `ScriptAssetMenuExtension` 的 `SupportsAsset` / `SupportedClasses` 菜单准入

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h` |
| 关联函数 | `UScriptAssetMenuExtension::GatherExtensionFunctions` |
| 现有测试覆盖 | 当前已有建议只覆盖 `UScriptAssetMenuExtension::CallFunctionOnSelection` 在 object / `FAssetData` prompt 之间的路由；第 4-38 行“当 selection 里没有任何受支持资产时整个菜单必须消失”的 gate 还没有任何独立测试规格 |
| 风险评估 | content browser 右键菜单一旦对不受支持资产错误显示 script action，用户会看到点开即无效的菜单项；反之若 gate 误伤，真正支持的资产也会失去入口 |
| 建议测试名 | `Angelscript.TestModule.Editor.ScriptAssetMenuExtension.GatherExtensionFunctionsRequiresSupportedAssetSelection` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionSelectionTests.cpp`；若文件接近 500 行则拆成 `AngelscriptScriptAssetMenuExtensionTests.cpp` |
| 场景描述 | 定义一个最小 test `UScriptAssetMenuExtension` 子类，覆写 `SupportsAsset()` 只接受 `UCurveFloat` 资产，并提供两个 `UFUNCTION(CallInEditor)`。先传入只包含 `UTexture2D` 的 selection，再传入 `UCurveFloat + UTexture2D` 的 mixed selection |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或等价 editor fixture；创建 transient `UCurveFloat` / `UTexture2D` 对象并包装成 `FAssetData`；为 extension 的 `SupportedClasses` 填入 `UCurveFloat::StaticClass()` |
| 期望行为 | unsupported-only selection 时 `GatherExtensionFunctions()` 返回空；mixed selection 时返回值与 `Super::GatherExtensionFunctions()` 的 `CallInEditor` 函数集合完全一致；若把 `SupportedClasses` 临时切成不匹配类型，则再次返回空 |
| 使用的 Helper | `FAngelscriptTestFixture` + transient asset helper + menu-extension test subclass |
| 优先级 | P2 |

#### NewTest-62：覆盖 `ContentBrowserDataSource` 未实现 API 的保守 no-op 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `EnumerateItemsAtPath`, `EnumerateItemsForObjects`, `GetItemPhysicalPath`, `CanEditItem`, `EditItem`, `BulkEditItems`, `AppendItemReference`, `TryGetCollectionId`, `Legacy_TryConvertPackagePathToVirtualPath`, `Legacy_TryConvertAssetDataToVirtualPath` |
| 现有测试覆盖 | 当前已有建议覆盖 filter/枚举/attribute 的正向 happy path，以及 stale payload 负例；但第 124-254 行这些显式返回 `false`/不回调的 API 仍然完全没有测试规格 |
| 风险评估 | 如果后续有人误把这些 stub 改成“返回 `true` 但不真正完成工作”，content browser 会把 Angelscript 资产当成支持物理路径、编辑、批量编辑或 virtual-path 转换的 data source，最终触发更隐蔽的 UI 错误 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.UnsupportedOperationsReturnFalseAndPreserveOutputs` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceTests.cpp`；若文件接近 500 行则拆成 `AngelscriptContentBrowserDataSourceUnsupportedApiTests.cpp` |
| 场景描述 | 用真实 script asset 构造一个 owned `FContentBrowserItemData`，然后依次调用 `EnumerateItemsAtPath`、`EnumerateItemsForObjects`、`GetItemPhysicalPath`、`CanEditItem`、`EditItem`、`BulkEditItems`、`AppendItemReference`、`TryGetCollectionId`、`Legacy_TryConvertPackagePathToVirtualPath`、`Legacy_TryConvertAssetDataToVirtualPath` |
| 输入/前置 | 复用现有 data-source 初始化 helper 与 script asset helper；为每个输出参数准备 sentinel 初值，给 `EnumerateItemsAtPath` / `EnumerateItemsForObjects` 的 callback 维护调用计数 |
| 期望行为 | `EnumerateItemsAtPath` 不触发 callback；其余返回 `bool` 的 API 全部返回 `false`；所有带输出参数的方法在返回 `false` 后都保持 sentinel 值不变，不产生部分写入 |
| 使用的 Helper | `FAngelscriptTestFixture` + content-browser data source helper + script asset helper |
| 优先级 | P3 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `EditorMenuExtensions` 函数级 0-hit 新事实 | 对 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 重新全文检索后，`SupportsActor(` 与 `SupportsAsset(` 仍然都是 `0` 命中；说明 actor/asset menu 的 selection gate 直到这一轮之前都还没有被具象化成测试规格 |
| 本轮 `ContentBrowserDataSource` 函数级 0-hit 新事实 | `EnumerateItemsAtPath(`、`EnumerateItemsForObjects(`、`GetItemPhysicalPath(`、`CanEditItem(`、`EditItem(`、`BulkEditItems(`、`AppendItemReference(`、`TryGetCollectionId(`、`Legacy_TryConvertPackagePathToVirtualPath(`、`Legacy_TryConvertAssetDataToVirtualPath(` 在目标测试树内重新检索后全部仍为 `0` 命中；此前文档里的 data-source 建议只覆盖正向枚举/attribute 和 stale payload，没有落到这组 unsupported API 边界 |
| 本轮现有测试质量新结论 | 现有 3 个测试文件里，除了前面已记录的 helper/cleanup 问题外，这一轮又确认了两类新的“绿灯盲区”：`FindBlueprintAssetsDiskBacked` 的真实 `.uasset` 删除失败不会暴露，`DirectoryWatcher` folder add/remove 也没有守住 opposite queue 为空；说明 EditorAndTools 当前最薄弱的仍是 cleanup 可观测性与精确断言，而不是用例数量本身 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-35 |
| WeakAssertion | 1 | Issue-36 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | MissingScenario: 2 |
| P3 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 12:55:12 +08:00)

### 一、现有测试问题

本轮完整复核了 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 的全文，并与已有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。新增发现集中在用户点名的 `EditorModule` / `ClassReloadHelper` / `ContentBrowserDataSource` / `EditorMenuExtensions` 无 direct-test 分支。

### 二、需要新增的测试

#### NewTest-63：覆盖 `ContentBrowserDataSource::DoesItemPassFilter` 的 include/exclude 精确判定

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `UAngelscriptContentBrowserDataSource::DoesItemPassFilter` |
| 现有测试覆盖 | 现有建议只覆盖 `CompileFilter`、`EnumerateItemsMatchingFilter`、`CreateAssetItem`、`GetItemAttribute` 与 legacy lookup；`DoesItemPassFilter()` 在目标测试树和当前 gap 文档里都还没有独立测试规格 |
| 风险评估 | 该函数当前实现直接 `return true`；如果 content browser 在已有 item 上走增量过滤/复筛路径，exclude class 或 owner 校验失效时，script asset 会在 UI 里错误保留，现有 happy-path 枚举测试不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.DoesItemPassFilterHonorsCompiledClassFiltersAndOwner` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceTests.cpp`；若文件接近 500 行则拆成 `AngelscriptContentBrowserDataSourceFilterTests.cpp` |
| 场景描述 | 初始化 `UAngelscriptContentBrowserDataSource`，准备一个真实 script asset item、一个 foreign owner item，以及两组 `FContentBrowserDataCompiledFilter`：第一组 include 目标类且 exclude 为空，第二组 include 同类但 exclude 也包含该类 |
| 输入/前置 | 复用现有 data-source 初始化 helper 与 script asset helper；用 `CompileFilter()` 构造 compiled filter，避免手写内部结构；额外准备一个 owner 不是 `UAngelscriptContentBrowserDataSource` 的 `FContentBrowserItemData` 作为对照 |
| 期望行为 | include-only filter 下 `DoesItemPassFilter(script item, filter)` 返回 `true`；include+exclude filter 下返回 `false`；foreign owner item 无论 filter 如何都返回 `false`；若 include class 不匹配也返回 `false` |
| 使用的 Helper | `FAngelscriptTestFixture` + content-browser data source helper + script asset helper |
| 优先级 | P1 |

#### NewTest-78：覆盖 `ScriptEditorSubsystem` 的 editor-world 与 tickable trait 契约，防止 template / runtime 实例在错误的 tick 通道注册

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h` |
| 关联函数 | `UScriptEditorSubsystem::GetWorld`, `GetTickableGameObjectWorld`, `GetTickableTickType`, `IsTickableInEditor` |
| 现有测试覆盖 | 现有 `ScriptEditorSubsystem` 建议只覆盖 `ShouldCreateSubsystem`、`Initialize`、`Deinitialize`、`IsAllowedToTick`、`Tick` 的生命周期 gate；`ScriptEditorSubsystem.h:21-24, 60-73` 这些决定 subsystem 绑定哪个 world、模板对象是否会被 tick manager 接管的 trait 仍没有任何独立测试规格 |
| 风险评估 | 如果 runtime 实例不再返回 editor world，或 template/CDO 不再维持 `ETickableTickType::Never`，脚本 editor subsystem 可能挂到错误 world、被错误注册进 tick manager，表现为菜单/工具在 editor 中存在但执行上下文为 `nullptr`，或者模板对象被错误 tick |
| 建议测试名 | `Angelscript.TestModule.Editor.ScriptEditorSubsystem.ExposesEditorWorldButKeepsTickWorldNullAndTemplateTickTypeNever` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptScriptEditorSubsystemTests.cpp`；若文件接近 500 行则拆成 `AngelscriptScriptEditorSubsystemTraitsTests.cpp` |
| 场景描述 | 定义一个最小 native test subclass，分别检查 CDO/template 与 `NewObject` 出来的 runtime 实例；不依赖 `BP_Initialize/BP_Tick`，专门验证 world/tick trait 的静态契约 |
| 输入/前置 | 使用 editor fixture，确保 `GEditor->GetEditorWorldContext().World()` 有效；通过 `GetDefault<UTestScriptEditorSubsystem>()` 取得模板对象，通过 `NewObject<UTestScriptEditorSubsystem>(GetTransientPackage())` 创建 runtime 实例 |
| 期望行为 | 模板对象 `GetTickableTickType() == ETickableTickType::Never`，runtime 实例 `GetTickableTickType() != ETickableTickType::Never`；两者 `GetTickableGameObjectWorld()` 都返回 `nullptr`；runtime 实例 `GetWorld()` 精确等于 `GEditor->GetEditorWorldContext().World()`；`IsTickableInEditor()` 始终返回 `true` |
| 使用的 Helper | editor fixture + 最小 native test subclass of `UScriptEditorSubsystem` |
| 优先级 | P2 |

#### NewTest-79：覆盖 `AssetToolsStatics::CreateAsset` 对 `CallingContext` 和 factory 参数的精确转发

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h` |
| 关联函数 | `UAssetToolsStatics::CreateAsset` |
| 现有测试覆盖 | 现有 `FunctionLibraries` 建议仍停留在整组 smoke，虽然把 `UAssetToolsStatics::*` 列进了关联函数，但没有任何独立测试规格验证 `AssetToolsStatics.h:32-35` 是否把 `AssetName`、`PackagePath`、`AssetClass`、`Factory`、`CallingContext` 原样转发给底层 `IAssetTools::CreateAsset` |
| 风险评估 | 如果 wrapper 丢掉 `CallingContext` 或 factory 指针，脚本侧创建资产时自定义 factory 的上下文感知逻辑、日志归因和导入分支都会静默失效；由于返回资产仍可能成功创建，这类回归很容易被“只看结果不看转发参数”的 smoke test 漏掉 |
| 建议测试名 | `Angelscript.TestModule.Editor.FunctionLibraries.AssetToolsCreateAssetForwardsCallingContextToFactory` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptFunctionLibrariesAssetToolsTests.cpp`；若团队更倾向 editor-private tests，也可放到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptFunctionLibrariesAssetToolsTests.cpp`，保持单文件 300-500 行 |
| 场景描述 | 定义一个 native spy factory，覆写 `FactoryCreateNew` 记录收到的 `AssetClass`、`InParent`、`InName`、`Flags` 与 `CallingContext`；随后通过 `UAssetToolsStatics::CreateAsset()` 创建一个临时 automation asset，并与直接调用 `IAssetTools::CreateAsset()` 做对照 |
| 输入/前置 | 使用 editor fixture；在 `/Game/Automation/EditorGapAudit_<Guid>` 下准备唯一 package path；构造 `UTestSpyFactory` 并记录调用参数；测试结束后清理创建出来的 asset/package 与 asset registry 条目，避免污染工作区 |
| 期望行为 | wrapper 返回的 asset 非空且 outer package 路径等于传入 `PackagePath`；spy factory 恰好调用 1 次，并记录到精确的 `AssetName`、`AssetClass`、`CallingContext`；与直接调用 `IAssetTools::CreateAsset()` 时观察到的参数完全一致 |
| 使用的 Helper | editor fixture + native spy `UFactory` subclass + asset cleanup helper |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 31/29 文件口径复核 | 重新枚举 `Plugins/Angelscript/Source/AngelscriptEditor/` 下 `.cpp/.h` 后，总文件数仍为 `31`；排除模块内 `Private/Tests` 两个测试实现后，production source 仍为 `29`。当前 3 个既有测试文件 direct-hit 的 production 文件数维持在 `7`，其余 `22` 个 production 文件仍无 direct test。 |
| 本轮 `ContentBrowserDataSource` 新增 0-hit 事实 | 现有文档已经覆盖 `CompileFilter` gate、本地枚举 break、stale payload 和 unsupported API，但在本轮之前，没有任何一条规格约束“复用同一个 `OutCompiledFilter` 时必须清空旧的 `IncludeClasses/ExcludeClasses`”。这意味着 query-shaping 入口仍存在一个顺序敏感的 stale-state 盲区。 |
| 本轮 `FunctionLibraries + ScriptEditorSubsystem` 新增 0-hit 事实 | 目标测试树内重新检索后，`UScriptEditorSubsystem::GetTickableGameObjectWorld` / `GetTickableTickType` / `IsTickableInEditor` 仍然是 `0` 直接命中；`UAssetToolsStatics::CreateAsset` 也仍只有组级 smoke 建议，没有任何独立规格去锁住 `CallingContext` 与 factory 参数转发。 |
| 本轮增量结论 | 现有 3 个测试文件的质量问题经过多轮复核后已基本收敛；这一轮新增的是更细的函数级契约，其中 `ContentBrowserDataSource` 继续向状态复用边界下钻，而 `FunctionLibraries + ScriptEditorSubsystem` 开始从“整组无测”细化到可直接落地的单函数规格。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 2 | NoTestForSource: 2 |

#### NewTest-64：覆盖 `ClassReloadHelper` 对 `ReloadEnums` / `NewEnums` 的 blueprint action refresh

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 现有建议覆盖 `ReloadState` 聚合、`GetTablesDependentOnStruct`、literal asset / enum state 收集、open-editor replacement 和 volume actor factory；第 373-378 行 `FBlueprintActionDatabase::RefreshAssetActions` 对 `ReloadEnums`、`NewEnums` 的刷新仍没有独立测试 |
| 风险评估 | 如果 enum reload 只更新了 `ReloadState()`，却没有刷新 blueprint action database，编辑器里的 enum 节点、菜单搜索和 action palette 会继续保留旧缓存，用户只能靠重启 editor 才能看到变化 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceRefreshesBlueprintActionsForReloadedAndNewEnums` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperEnumTests.cpp` |
| 场景描述 | 在 editor fixture 下准备一个 changed enum 和一个 newly-created enum，分别填入 `FClassReloadHelper::ReloadState().ReloadEnums`、`NewEnums`，然后执行 `PerformReinstance()` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 或等价 editor fixture；通过 test seam / spy 包装 `FBlueprintActionDatabase::Get().RefreshAssetActions()`，记录被刷新的 asset 顺序和次数；测试前后保存并恢复 `ReloadState()` |
| 期望行为 | `PerformReinstance()` 后 `RefreshAssetActions` 至少被调用 2 次，且参数集合精确等于 `{ChangedEnum, NewEnum}`；不在这两个集合里的 enum 不会被刷新；重复执行前若清空 `ReloadEnums` / `NewEnums`，刷新调用数不会继续增加 |
| 使用的 Helper | engine/editor fixture + reload-state guard + blueprint-action-database spy |
| 优先级 | P2 |

#### NewTest-65：覆盖 `ClassReloadHelper` 在重载完成后的 `PropertyEditor` 刷新通知

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 当前所有 `ClassReloadHelper` 建议都停留在 reload state、asset replacement、enum/volume 副作用；第 336-338 行 `FPropertyEditorModule::NotifyCustomizationModuleChanged()` 仍然没有任何测试规格 |
| 风险评估 | 如果热重载完成后 details panel 没有收到刷新通知，property customization UI 会继续持有旧 class/struct 缓存，表现为“脚本重载成功但细节面板不刷新”，这种 editor-only 回归很容易逃过现有测试网 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceNotifiesPropertyEditorCustomizationRefresh` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperEditorRefreshTests.cpp` |
| 场景描述 | 在 editor fixture 下准备最小 reload state（例如一条 class reload 或 struct reload），执行 `PerformReinstance()`，并通过 property editor spy 观测 refresh 通知 |
| 输入/前置 | 使用 engine/editor fixture；对 `FModuleManager::GetModulePtr<FPropertyEditorModule>(\"PropertyEditor\")` 暴露 test seam，或在测试模块里挂一个可计数的 shim；保存并恢复 `ReloadState()`，避免污染后续热重载相关用例 |
| 期望行为 | `PerformReinstance()` 完成后 `NotifyCustomizationModuleChanged()` 恰好触发 1 次；若测试 seam 返回 `nullptr` property module，则函数仍可安全完成且不会崩溃；重复执行前清空 reload state 时不应产生额外的通知噪声 |
| 使用的 Helper | engine/editor fixture + reload-state guard + property-editor-module spy |
| 优先级 | P2 |

#### NewTest-66：覆盖 `ScriptEditorMenuExtension::InitializeExtensions` 的 full-reload / pre-exit 生命周期

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `UScriptEditorMenuExtension::InitializeExtensions`, `RegisterExtensions`, `UnregisterExtensions`, `GetRegisteredExtensionSnapshots` |
| 现有测试覆盖 | 现有建议覆盖 `RegisterExtensions` / `UnregisterExtensions` 的直接效果、menu registration、context 与 selection forwarding；第 25-45 行 `InitializeExtensions()` 里对 `FAngelscriptClassGenerator::OnPostReload` 和 `FCoreDelegates::OnEnginePreExit` 的生命周期绑定仍没有任何独立测试规格 |
| 风险评估 | 如果 full reload 时没有先 `UnregisterExtensions()` 再重新注册，或 engine exit 时没有撤销 extenders，menu extender 会在脚本热重载/关闭 editor 后残留重复注册，造成菜单重复、快照膨胀甚至悬挂委托 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.InitializeExtensionsReRegistersOnlyOnFullReloadAndUnregistersOnPreExit` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorMenuExtensionLifecycleTests.cpp` |
| 场景描述 | 在 editor fixture 下准备一个最小 test `UScriptEditorMenuExtension` 脚本类，先调用 `InitializeExtensions()`；随后依次广播一次 `OnPostReload(false)`、一次 `OnPostReload(true)`、一次 `OnEnginePreExit`，每步都读取 `GetRegisteredExtensionSnapshots()` 作为观测 |
| 输入/前置 | 使用 menu-extension fixture；确保 `FAngelscriptEngine::IsInitialized()` 与 `IsInitialCompileFinished()` 条件满足；为 `OnPostReload` / `OnEnginePreExit` 安装测试期 delegate guard，结束时恢复全局 delegate，避免污染其他 editor tests |
| 期望行为 | 初始化后 snapshot 数量大于 `0`；广播 `OnPostReload(false)` 后数量和内容不变；广播 `OnPostReload(true)` 后 snapshot 数量与初始化时相同但不会翻倍；广播 `OnEnginePreExit` 后 snapshot 变为 `0`，且重复广播不会崩溃或重复移除 |
| 使用的 Helper | `FAngelscriptTestFixture` + menu-extension fixture + registered-snapshot helper + delegate guard |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `ContentBrowserDataSource` 新增 0-hit 事实 | 对 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 重新全文检索后，`DoesItemPassFilter` 仍然是 `0` 命中；说明现有 data-source 建议虽然覆盖了枚举、attribute、legacy lookup 和 unsupported API，但“已有 item 的二次过滤判定”直到这一轮之前仍未被具象化成测试规格 |
| 本轮 `ClassReloadHelper` 新增 0-hit 事实 | `NotifyCustomizationModuleChanged`、`RefreshAssetActions` 在目标测试树和当前 gap 文档内都还没有直接测试规格；这表明 `ClassReloadHelper` 当前建议集虽然覆盖了 reload state 与部分副作用，但 hot reload 完成后的 editor UI 刷新链路仍然存在空白 |
| 本轮 `EditorMenuExtensions` 新增 0-hit 事实 | `OnEnginePreExit` 在目标测试树里仍是 `0` 命中，而 `InitializeExtensions()` 目前只有被其他建议间接提到，没有任何把 `OnPostReload(false/true)` 与 pre-exit 生命周期放在同一条测试链里验证的规格 |
| 本轮交叉核对结论 | 结合 UE5-main 中 `IAssetRegistry::ScanModifiedAssetFiles` 的实现可确认它内部调用 `ScanPathsSynchronous()`；因此本轮没有把 `FindBlueprintAssetsDiskBacked` 再追加成新的 asset-registry async 风险，当前剩余盲区仍然集中在 cleanup、精确过滤和 editor 生命周期副作用上 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |
| P2 | 2 | NoTestForSource: 2 |

--- 

## 测试审查 (2026-04-09 13:04:30 +08:00)

### 一、现有测试问题

本轮继续按“先现有测试质量、后无测源码”的顺序复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与已有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。新增发现集中在 `ScriptEditorPrompts::ShowPromptForStruct`、`SettingsModule` 生命周期，以及 31 文件口径下的覆盖归类澄清。

### 二、需要新增的测试

#### NewTest-67：覆盖 `StartupModule` / `ShutdownModule` 的 project settings 注册与撤销

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h` |
| 关联函数 | `FAngelscriptEditorModule::StartupModule`, `ShutdownModule` |
| 现有测试覆盖 | 当前 lifecycle 建议已覆盖 watcher、`OnPostEngineInit`、`ReloadTags`、tool menu owner 和 state dump handle，但第 384-393 行 `SettingsModule->RegisterSettings("Project", "Plugins", "Angelscript", ...)` 以及 shutdown 侧的 settings cleanup 仍没有独立测试规格 |
| 风险评估 | 如果 project settings 注册参数回归、重复启动模块后累积重复注册，或 shutdown 后没有撤销 settings entry，用户会在 Project Settings 里看到重复或悬挂的 Angelscript 配置入口；现有 editor lifecycle 测试建议不会直接暴露这条 UI 生命周期缺口 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.SettingsRegistrationRegistersOnStartupAndUnregistersOnShutdown` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleLifecycleTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorModuleSettingsTests.cpp` |
| 场景描述 | 在 module lifecycle fixture 下为 `ISettingsModule` 安装 spy/test seam，依次执行 `StartupModule()`、`ShutdownModule()`、再执行一次 `StartupModule()`，记录 register/unregister 调用参数与次数 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或等价 editor fixture；对 `FModuleManager::GetModulePtr<ISettingsModule>("Settings")` 暴露 settings-module spy；准备 `GetMutableDefault<UAngelscriptSettings>()` 作为期望配置对象，并在测试结束恢复 spy/module 状态 |
| 期望行为 | 首次 `StartupModule()` 恰好调用一次 `RegisterSettings("Project", "Plugins", "Angelscript", ..., GetMutableDefault<UAngelscriptSettings>())`；`ShutdownModule()` 恰好调用一次匹配的 `UnregisterSettings("Project", "Plugins", "Angelscript")`；第二次 `StartupModule()` 后 register 总次数变为 `2` 且 UI 路径不重复累积 |
| 使用的 Helper | `FAngelscriptTestFixture` + module lifecycle harness + `ISettingsModule` spy |
| 优先级 | P1 |

#### NewTest-68：覆盖 `ShowPromptForStruct` 的 modal window 配置与返回值回传

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.h` |
| 关联函数 | `FScriptEditorPrompts::ShowPromptForStruct` |
| 现有测试覆盖 | 当前 `ScriptEditorPrompts` 建议只覆盖 `ShowPromptToCallFunction` 的 object/struct overload 与 `ShowPromptToCallFunctionOnObjects`；第 164-179 行真正负责创建 `SWindow`、安装 `SScriptEditorPromptDialog`、并把 `Dialog->bOKPressed` 回传给调用方的根入口仍没有独立测试规格 |
| 风险评估 | 如果 `WindowTitle` / `WindowSize` / OK button 文案没有正确传给对话框，或 `ShowPromptForStruct()` 没有把用户的 OK/Cancel 决策正确回传，上层 prompt-routing 用例仍可能因为 stub 掉根入口而保持绿色，但实际 editor 弹窗会出现交互回归 |
| 建议测试名 | `Angelscript.TestModule.Editor.ScriptEditorPrompts.ShowPromptForStructUsesOptionsAndReturnsDialogDecision` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptScriptEditorPromptsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptScriptEditorPromptDialogTests.cpp` |
| 场景描述 | 构造一个最小 `FStructOnScope` 和自定义 `FScriptEditorPromptOptions`（带 `WindowTitle`、`WindowSize`、`OKButtonText`、`OKButtonTooltip`）；通过 modal-window seam 或 dialog factory seam 先模拟一次 Cancel，再模拟一次 OK，并记录创建出的 window/dialog 配置 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或等价 editor fixture；准备一个 transient `UScriptStruct` 作为 prompt 内容；为 `GEditor->EditorAddModalWindow` 或 `SScriptEditorPromptDialog` 安装 test seam，允许测试读取 `SWindow` title/size 并控制 `bOKPressed` |
| 期望行为 | 两次调用都只创建一个 modal window；window 的 title 和 client size 与传入 `Options` 完全一致；dialog 收到的 `OKButtonText` / `OKButtonTooltip` 与输入一致；函数返回值分别精确等于 seam 设置的 `false` 和 `true` |
| 使用的 Helper | `FAngelscriptTestFixture` + modal-window/dialog seam + transient struct helper |
| 优先级 | P2 |

#### NewTest-69：覆盖 `GetItemAttributes` 的 batch API 保守 no-op 契约

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `UAngelscriptContentBrowserDataSource::GetItemAttributes` |
| 现有测试覆盖 | 现有 data-source 建议已覆盖 `GetItemAttribute`、`Legacy_TryGetPackagePath`、`Legacy_TryGetAssetData`、`UpdateThumbnail`、`DoesItemPassFilter` 和一组 unsupported API；但第 177-180 行 batch 版 `GetItemAttributes()` 仍没有任何独立测试规格 |
| 风险评估 | 如果后续实现把 batch API 改成“返回 `false` 但部分写入 `OutAttributeValues`”，或对 foreign item 误写入缓存属性，content browser 会拿到混合的新旧 metadata；现有单属性测试与 unsupported-API 建议都不会直接发现这一类部分写入回归 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.GetItemAttributesReturnsFalseAndPreservesCallerBuffer` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceTests.cpp`；若文件接近 500 行则拆成 `AngelscriptContentBrowserDataSourceAttributeBatchTests.cpp` |
| 场景描述 | 初始化 `UAngelscriptContentBrowserDataSource`，创建一个 canonical script asset item 和一个 foreign owner item；分别预填一个带 sentinel 键值的 `FContentBrowserItemDataAttributeValues`，然后调用 `GetItemAttributes()` |
| 输入/前置 | 使用 `FAngelscriptTestFixture` + 现有 data-source 初始化 helper + script asset helper；为 `OutAttributeValues` 预置一条不会由生产代码生成的 sentinel attribute/value，便于检测函数是否发生部分写入 |
| 期望行为 | canonical item 与 foreign item 两次调用都返回 `false`；`OutAttributeValues` 在两次调用后都仍只包含原有 sentinel 项，键和值均不变；`InIncludeMetaData = true/false` 两种调用都不会新增任何属性 |
| 使用的 Helper | `FAngelscriptTestFixture` + content-browser data source helper + script asset helper |
| 优先级 | P3 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 31 文件口径澄清 | 重新按 `Plugins/Angelscript/Source/AngelscriptEditor/` 目录内全部 `.cpp/.h` 计数后，可确认用户提到的 `31` 文件口径成立，但其中包含 `Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` 与 `Private/Tests/AngelscriptDirectoryWatcherTests.cpp` 这 2 个模块内测试实现；真正需要做“无测源码”判断的 production source 实际是 `29` 文件 |
| 本轮 31 文件复核结果 | 若按 `31` 文件全量口径看，已有测试实现或 direct-hit 覆盖可归到 `9` 个文件：2 个模块内 test cpp + 7 个 production direct-hit 文件；剩余 `22` 个仍然是完全没有 direct test 的 production 文件 |
| 本轮 29 个 production 文件分布 | `BlueprintImpact + Commandlet` `4 / 4` direct-hit；`DirectoryWatcherInternal` `2 / 2` direct-hit；`SourceCodeNavigation` `1 / 1` direct-hit；`EditorModule family` `0 / 3`、`ClassReloadHelper` `0 / 2`、`ContentBrowserDataSource` `0 / 2`、`EditorMenuExtensions` `0 / 8`、`FunctionLibraries + ScriptEditorSubsystem` `0 / 7` |
| 本轮新增 0-hit 函数级事实 | 对 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 全文检索后，`FScriptEditorPrompts::ShowPromptForStruct`、`SettingsModule->RegisterSettings/UnregisterSettings` 路径、`UAngelscriptContentBrowserDataSource::GetItemAttributes` 仍然都是 `0` 直接命中；此前文档只有零散覆盖快照，没有这三条路径的独立测试规格 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 13:14:37 +08:00)

### 一、现有测试问题

本轮重新逐行复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与现有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。新增发现集中在用户点名的 `ClassReloadHelper` / `ContentBrowserDataSource` / `EditorMenuExtensions` 函数级 0-hit 分支。

### 二、需要新增的测试

#### NewTest-70：覆盖 `PerformReinstance` 在 initial compile 未完成时的早退 no-op

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 现有 `ClassReloadHelper` 建议已覆盖 reload state 聚合、enum refresh、volume actor factory、property editor refresh 和 open-editor replacement；但 `ClassReloadHelper.cpp:27-31` 的 `if (!FAngelscriptEngine::Get().bIsInitialCompileFinished) return;` 早退 guard 仍没有任何独立测试规格 |
| 风险评估 | 如果 initial compile 阶段误入 reinstance 路径，会过早触发 GC、`ReparentHierarchies`、blueprint action refresh 或 placement 广播，属于 editor 启动/首次编译期的高风险副作用；当前测试建议不会直接守住这条生命周期边界 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceNoOpsBeforeInitialCompileFinishes` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperLifecycleTests.cpp` |
| 场景描述 | 在 editor fixture 下预填一份非空 `ReloadState()`（至少包含一条 `ReloadClasses`、一条 `ReloadEnums` 和一条 `NewClasses`），临时把 `FAngelscriptEngine::Get().bIsInitialCompileFinished` 置为 `false` 后调用 `PerformReinstance()` |
| 输入/前置 | 使用 engine/editor fixture；通过 reload-state guard 保存并恢复 `ReloadState()`；为 `FBlueprintActionDatabase::RefreshAll/RefreshAssetActions`、`FPropertyEditorModule::NotifyCustomizationModuleChanged`、`IPlacementModeModule` 广播安装 spy，便于观测是否发生副作用 |
| 期望行为 | 调用后 `ReloadState()` 内容保持不变；`RefreshAll` / `RefreshAssetActions` / `NotifyCustomizationModuleChanged` / placement broadcasts 调用数全部保持 `0`；`GEditor->ActorFactories` 不新增条目；恢复 `bIsInitialCompileFinished=true` 后同一测试夹具可继续执行其他 reload 场景 |
| 使用的 Helper | engine/editor fixture + reload-state guard + blueprint-action/property-editor/placement spies |
| 优先级 | P1 |

#### NewTest-71：覆盖 `CompileFilter` 的 root-path / item-type gate，防止非目标查询污染脚本过滤器

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `UAngelscriptContentBrowserDataSource::CompileFilter`, `EnumerateItemsMatchingFilter` |
| 现有测试覆盖 | 现有 data-source 建议覆盖 canonical item 枚举、attribute、legacy lookup、unsupported API 和 `DoesItemPassFilter()`；但 `AngelscriptContentBrowserDataSource.cpp:31-48` 对 `InPath == \"/\"`、`IncludeFiles`、`IncludeAssets`、`ClassFilter != nullptr` 的四个 gate 仍没有任何独立测试规格 |
| 风险评估 | 如果 content browser 在非 root 路径、folder-only 查询或无 class filter 查询时仍编译出脚本过滤器，`EnumerateItemsMatchingFilter()` 可能把 Angelscript 资产错误注入无关视图；这类 UI 污染不会被当前建议集直接发现 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.CompileFilterRejectsNonRootAndNonAssetFileQueries` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceTests.cpp`；若文件接近 500 行则拆成 `AngelscriptContentBrowserDataSourceFilterTests.cpp` |
| 场景描述 | 分别构造 4 组 filter：`InPath=\"/All/Angelscript\"`、folder-only、file-only但非 asset、以及缺少 `FContentBrowserDataClassFilter`；每组都调用一次 `CompileFilter()`，再把结果交给 `EnumerateItemsMatchingFilter()` 统计 callback 次数 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` + data-source 初始化 helper；准备至少一个真实 script asset 作为对照，避免“因为没有资产所以枚举为 0”掩盖 gate 失效；callback 维护命中计数和 item 路径列表 |
| 期望行为 | 四组负例在 `CompileFilter()` 后都不会产出可用的 `IncludeClasses`；`EnumerateItemsMatchingFilter()` callback 调用数保持 `0`，不会枚举到任何 `/All/Angelscript/*` item；对照组 `InPath=\"/\"` 且同时包含 files+assets+class filter 时，才会枚举出真实 script asset |
| 使用的 Helper | `FAngelscriptTestFixture` + content-browser data source helper + script asset helper |
| 优先级 | P2 |

#### NewTest-72：覆盖 `Extend` 的 `ShouldExtend` / 空函数 guard，防止注册空 extender

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `UScriptEditorMenuExtension::Extend`, `GatherExtensionFunctions` |
| 现有测试覆盖 | 现有 `EditorMenuExtensions` 建议覆盖注册、category 排序、tool-menu context、action metadata 和 lifecycle；但 `ScriptEditorMenuExtension.cpp:115-140` 对 `ShouldExtend()` 返回 `false` 以及 `GatherExtensionFunctions()` 结果为空时直接返回空 `FExtender` 的 guard 仍没有任何独立测试规格 |
| 风险评估 | 如果 guard 回归，菜单系统会为本不应扩展的场景注册空 section/空 submenu，造成 UI 空洞、重复空隔断或无意义 callback；这类退化不会被当前已有的“成功注册”类测试发现 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.ExtendSkipsRegistrationWhenShouldExtendFalseOrNoCallableFunctions` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorMenuExtensionGuardTests.cpp` |
| 场景描述 | 准备两个 test extension class：第一个重载/脚本实现 `ShouldExtend()` 返回 `false`，但带一个 `CallInEditor` 函数；第二个 `ShouldExtend()` 返回 `true`，但类里没有任何合法 `CallInEditor` 函数。分别调用 `Extend()` 并检查返回的 `FExtender` |
| 输入/前置 | 使用 editor fixture；`FUICommandList` 用最小 stub 即可；如果需要观测 extender 内容，可通过 `FMenuBuilder` 执行返回 extender 的 menu extension 并记录 section/entry 数量 |
| 期望行为 | 两个负例都返回有效但为空的 `FExtender`；后续执行该 extender 不会生成 menu entry、submenu 或 section；对照组在 `ShouldExtend()==true` 且存在合法 `CallInEditor` 函数时，`FExtender` 至少包含一个 menu extension |
| 使用的 Helper | editor fixture + test `UScriptEditorMenuExtension` subclasses + menu-builder probe |
| 优先级 | P2 |

#### NewTest-73：覆盖 struct reload 的 `BroadcastPre/PostChange` 与 dependent blueprint 重新编译链路

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance`, `FClassReloadHelper::GetTablesDependentOnStruct` |
| 现有测试覆盖 | 现有 `ClassReloadHelper` 建议已覆盖 `GetTablesDependentOnStruct()` 的静态筛选、enum refresh、property editor refresh 和 volume actor factory；但 `ClassReloadHelper.cpp:148-163` 与 `190-265` 对 `UUserDefinedStruct` 的 `FStructureEditorUtils::BroadcastPreChange/PostChange`、`QueueForCompilation()`、`FlushCompilationQueueAndReinstance()` 仍没有任何独立测试规格 |
| 风险评估 | 如果 struct reload 只更新了 `ReloadState()`，却没有广播结构体变更或把依赖 blueprint 重新入队编译，editor 会保留旧 pin/schema 缓存，表现为热重载后蓝图节点报错但测试网不报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceBroadcastsStructChangesAndRecompilesDependentBlueprints` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperStructTests.cpp` |
| 场景描述 | 在 editor fixture 下准备一个 `OldStruct -> NewStruct` reload 对，以及一个通过变量或 pin 依赖 `OldStruct` 的 blueprint；同时准备一张 `RowStruct = OldStruct` 的 `UDataTable`。把 reload 对填入 `ReloadState().ReloadStructs` 后执行 `PerformReinstance()` |
| 输入/前置 | 使用 engine/editor fixture；需要 `UUserDefinedStruct` 或等价可被 `FStructureEditorUtils` 处理的 struct 样本；为 `BroadcastPreChange` / `BroadcastPostChange`、`FBlueprintCompilationManager::QueueForCompilation` / `FlushCompilationQueueAndReinstance` 安装 spy；通过 reload-state guard 保存并恢复全局状态 |
| 期望行为 | `BroadcastPreChange(OldStruct)` 与 `BroadcastPostChange(NewStruct)` 各触发 1 次；依赖 blueprint 恰好被 `QueueForCompilation()` 入队并在 `FlushCompilationQueueAndReinstance()` 中完成一次刷新；对应 `UDataTable->RowStruct` 从 `OldStruct` 更新到 `NewStruct`；不依赖该 struct 的 blueprint 不会被误入队 |
| 使用的 Helper | engine/editor fixture + reload-state guard + structure-editor / blueprint-compilation spies + transient blueprint/data-table helpers |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `ClassReloadHelper` 新增 0-hit 事实 | 对 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 重新全文检索后，`bIsInitialCompileFinished` early-return、`FStructureEditorUtils::BroadcastPreChange/PostChange`、`FBlueprintCompilationManager::QueueForCompilation` / `FlushCompilationQueueAndReinstance` 仍然都是 `0` 直接命中；说明 `ClassReloadHelper` 目前仍缺少对“热重载前置 guard”和“struct 依赖 blueprint 修复链路”的显式保护 |
| 本轮 `ContentBrowserDataSource` 新增 0-hit 事实 | `CompileFilter` 的 `InPath == \"/\"`、`IncludeFiles`、`IncludeAssets`、`ClassFilter` 四个 gate 在目标测试树与现有 gap 文档里仍没有任何单独测试规格；此前 data-source 建议偏重成功枚举与 no-op API，尚未下钻到 query-shaping 入口 |
| 本轮 `EditorMenuExtensions` 新增 0-hit 事实 | `UScriptEditorMenuExtension::Extend()` 的 `ShouldExtend()==false` / `GatherExtensionFunctions().Num()==0` guard 仍无任何直接测试规格；当前文档主要验证“能注册、能执行”，但还没有测试去守住“不该扩展时必须什么都不做” |
| 本轮用户点名区域增量结论 | 在 `EditorModule` / `ClassReloadHelper` / `ContentBrowserDataSource` / `EditorMenuExtensions` 四组重点源码里，已有文档已基本覆盖文件级空白；本轮新增的是函数级 guard 和生命周期分支，说明剩余风险已从“有没有测试”收敛到“关键分支是否被精确约束” |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |
| P2 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 13:22:04 +08:00)

### 一、现有测试问题

本轮重新逐行复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并对照现有 gap 文档与 `TestCoverageGapRule_ZH.md` 去重；没有确认出新的、不重复的现有测试质量问题。新增发现集中在 `ClassReloadHelper` / `ContentBrowserDataSource` / `EditorMenuExtensions` 仍未具象化成测试规格的函数级分支。

### 二、需要新增的测试

#### NewTest-74：覆盖 `PerformReinstance` 的 `angelscript.UseUnrealReload=1` 备用重载路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 现有 `ClassReloadHelper` 建议已覆盖 default 分支上的 reload state 聚合、struct/delegate 依赖刷新、enum refresh、property editor refresh 与 volume actor factory；但 `ClassReloadHelper.cpp:311-330` 的 `GAngelscriptUseUnrealReload != 0` 备用 `FReload` 路径仍没有任何独立测试规格 |
| 风险评估 | 一旦团队或 CI 临时打开 `angelscript.UseUnrealReload` 进行回归比对，这条分支若漏掉 `NotifyChange`、`Reinstance` 或 `Finalize`，会直接把 editor 重载行为切到半执行状态；当前测试网不会给出任何警报 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceUsesFReloadPathWhenCVarEnabled` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperAltReloadTests.cpp` |
| 场景描述 | 在 editor fixture 下预填一份最小 reload state：至少包含 1 条 `ReloadStructs`、1 条 `ReloadClasses`、1 个 `ReloadEnums`；把 `FAngelscriptEngine::Get().bIsInitialCompileFinished` 置为 `true`，临时把 `angelscript.UseUnrealReload` 设为 `1` 后调用 `PerformReinstance()` |
| 输入/前置 | 使用 engine/editor fixture；为 `FReload` 构造可观测 seam 或 factory seam，记录 `NotifyChange` / `Reinstance` / `Finalize` 调用序列；用 RAII 保存并恢复 CVar 与 `ReloadState()` 内容 |
| 期望行为 | 备用路径下会按 `struct -> class -> enum` 顺序把 3 组对象送入 `NotifyChange`；`Reinstance()` 与 `Finalize()` 各恰好执行 1 次；不会误走 `ReparentHierarchies()` / blueprint dependency refresh 分支；测试结束后 CVar 与 reload state 全部恢复 |
| 使用的 Helper | engine/editor fixture + reload-state guard + `FReload` seam / spy + CVar restore helper |
| 优先级 | P2 |

#### NewTest-75：覆盖 `EnumerateItemsMatchingFilter` 在 callback 返回 `false` 时的提前停止契约

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `UAngelscriptContentBrowserDataSource::EnumerateItemsMatchingFilter` |
| 现有测试覆盖 | 现有 data-source 建议覆盖 `CompileFilter` gate、正向枚举、`DoesItemPassFilter`、attribute、thumbnail 和 unsupported API；但 `AngelscriptContentBrowserDataSource.cpp:116-118` 对 callback 主动停止枚举的分支仍没有任何独立测试规格 |
| 风险评估 | 如果枚举器忽略 callback 的 `false` 返回值，content browser 的 consumer 在只请求第一个命中项时仍会继续扫描所有 script assets，既会制造额外 UI 噪声，也会把性能回归隐藏在“结果还是对的”假阳性里 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.EnumerateItemsMatchingFilterStopsAfterCallbackRequestsBreak` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceTests.cpp`；若文件接近 500 行则拆成 `AngelscriptContentBrowserDataSourceEnumerationTests.cpp` |
| 场景描述 | 在 `FAngelscriptEngine::Get().AssetsPackage` 下准备至少 2 个都满足 include filter 的 script asset，先用正向 filter 构造 compiled filter；第一次枚举时让 callback 收到第 1 个 item 后立即返回 `false`，第二次枚举作为对照让 callback 始终返回 `true` |
| 输入/前置 | 使用 `FAngelscriptTestFixture` + data-source 初始化 helper + script asset helper；callback 记录被访问 item 的路径顺序和调用次数，确保两次枚举使用同一组资产 |
| 期望行为 | 提前停止场景下 callback 恰好只被调用 1 次，且只记录第 1 个命中项；对照场景下 callback 调用数等于全部匹配资产数量；两次枚举都不会把 exclude class 资产或 foreign item 混进结果 |
| 使用的 Helper | `FAngelscriptTestFixture` + content-browser data source helper + script asset helper |
| 优先级 | P2 |

#### NewTest-76：覆盖 `BuildToolMenuSection` 的 `ShouldExtend` / 空函数早退，防止 tool menu 残留空 separator

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `UScriptEditorMenuExtension::BuildToolMenuSection`, `GatherExtensionFunctions` |
| 现有测试覆盖 | 现有 `EditorMenuExtensions` 建议覆盖 `Extend()` guard、category 排序、tool-menu context、action metadata、prompt routing 与生命周期；但 `ScriptEditorMenuExtension.cpp:577-585` 中 tool-menu 路径自带的 `ShouldExtend()==false` 与 `Functions.Num()==0` 早退仍没有独立测试规格 |
| 风险评估 | `ToolMenu` 分支不会走 `Extend()`，如果这里的 guard 回归，编辑器会在 toolbar / tool-menu 中生成空 section、空 combo button 或多余 separator；已有 menu-extender 测试不会直接发现这条 UI 路径的退化 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.BuildToolMenuSectionSkipsEmptyToolMenus` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionsTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorMenuExtensionToolMenuGuardTests.cpp` |
| 场景描述 | 准备两个 test extension class：第一个 `ShouldExtend()` 返回 `false` 且打开 `bAddSeparatorBeforeOptions/bAddSeparatorAfterOptions`；第二个 `ShouldExtend()` 返回 `true` 但不提供任何合法 `CallInEditor` 函数。分别调用 `BuildToolMenuSection()` 构建 tool-menu section，并与一个正常扩展类做对照 |
| 输入/前置 | 使用 editor fixture；创建可观测 `UToolMenu` / `FToolMenuSection` 夹具，记录 section 中的 entry 数、submenu 数和 separator 数；必要时复用现有 menu-builder probe |
| 期望行为 | 两个负例都不会向 section 写入 entry、submenu 或 separator，`Section.Blocks.Num()` 保持 `0`；对照组在存在合法函数时至少新增 1 个 entry 或 combo button；负例执行后 `ActiveToolMenuContext` 也不会泄漏到回调外部 |
| 使用的 Helper | editor fixture + test `UScriptEditorMenuExtension` subclasses + tool-menu probe |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `ClassReloadHelper` 新增 0-hit 事实 | 对 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 重新全文检索后，`GAngelscriptUseUnrealReload`、`FReload(`、`Finalize()` 仍然都是 `0` 直接命中；说明 `PerformReinstance()` 的备用 Unreal reload 分支直到本轮之前仍没有任何测试规格 |
| 本轮 `ContentBrowserDataSource` 新增 0-hit 事实 | 目标测试树与现有 gap 文档里仍没有任何一条规格专门约束 `EnumerateItemsMatchingFilter()` 在 callback 返回 `false` 后必须立刻停止；此前 data-source 建议主要守住 filter gate、结果集合和 no-op API，没有落到 consumer break 契约 |
| 本轮 `EditorMenuExtensions` 新增 0-hit 事实 | 现有建议已经覆盖 `Extend()` guard，但 `BuildToolMenuSection()` 自己的 `ShouldExtend()` / `Functions.Num()==0` 早退在目标测试树和 gap 文档里仍是 `0` 独立规格；这意味着 `ToolMenu` 路径仍存在专属 guard 盲区 |
| 本轮用户点名区域增量结论 | `EditorModule` / `ClassReloadHelper` / `ContentBrowserDataSource` / `EditorMenuExtensions` 四组重点源码的文件级空白已基本被前几轮扫过；本轮新增的是“备用分支 + callback 契约 + tool-menu 专属 guard”这类更细的函数级行为，风险形态继续从“完全无测试”收敛到“特定执行路径仍未锁死” |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 3 | MissingScenario: 2, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 13:33:48 +08:00)

### 一、现有测试问题

本轮重新逐行复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与现有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。新增发现集中在 `ContentBrowserDataSource` 和 `FunctionLibraries + ScriptEditorSubsystem` 里此前只有文件级/组级空白、但还没有落成独立测试规格的函数契约。

### 二、需要新增的测试

#### NewTest-77：覆盖 `CompileFilter` 复用同一 `OutCompiledFilter` 时的旧状态清理，防止 stale class filter 污染后续查询

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `UAngelscriptContentBrowserDataSource::CompileFilter`, `EnumerateItemsMatchingFilter` |
| 现有测试覆盖 | 现有 data-source 建议已覆盖 query gate、枚举 break、`DoesItemPassFilter`、attribute、thumbnail 与 unsupported API；但 `AngelscriptContentBrowserDataSource.cpp:35-48` 里先 `FindOrAddFilter`、后走多个 early-return 的实现，仍没有任何规格验证复用同一个 `OutCompiledFilter` 时不会保留上一次的 `IncludeClasses/ExcludeClasses` |
| 风险评估 | 如果 content browser 或测试代码复用同一个 compiled filter，第一次合法查询留下的 class filter 可能污染后续无效查询，导致第二次 `CompileFilter()` 虽然命中了 `InPath != "/"` / `!IncludeFiles` / `ClassFilter == nullptr` 等 gate，`EnumerateItemsMatchingFilter()` 仍然枚举出旧资产；这会把查询结果变成顺序敏感的 stale-state bug |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.CompileFilterClearsPreviousIncludeExcludeStateOnReusedCompiledFilter` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceFilterTests.cpp`；若该文件不存在则从 `AngelscriptContentBrowserDataSourceTests.cpp` 拆出 filter 子文件，保持单文件 300-500 行 |
| 场景描述 | 先用合法 root filter 和 include class 编译一次 `OutCompiledFilter`，确认能枚举到一个真实 script asset；随后在**同一个** `OutCompiledFilter` 上再次调用 `CompileFilter()`，这次分别传入 `InPath="/All/Angelscript"`、folder-only、以及缺少 `FContentBrowserDataClassFilter` 的负例，再立刻调用 `EnumerateItemsMatchingFilter()` |
| 输入/前置 | 使用 `FAngelscriptTestFixture` + data-source 初始化 helper + script asset helper；准备至少一个匹配 include class 的 script asset；callback 记录命中次数和 item path；第二次调用前不重新构造 `OutCompiledFilter`，专门复现状态复用 |
| 期望行为 | 第一次合法编译后 callback 至少命中 1 个 asset；第二次每个负例都会让 `IncludeClasses/ExcludeClasses` 处于空/不可用状态，`EnumerateItemsMatchingFilter()` callback 调用数严格为 `0`，不会把第一次查询的资产残留带到第二次结果里 |
| 使用的 Helper | `FAngelscriptTestFixture` + content-browser data source helper + script asset helper |
| 优先级 | P1 |

---

## 测试审查 (2026-04-09 23:25:47 +08:00)

### 一、现有测试问题

#### Issue-37：`DirectoryWatcher` 的非 remove 分支用例没有断言 enumerator 必须保持未触发

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove`, `Angelscript.Editor.DirectoryWatcher.Queue.IgnoresNonScriptFiles`, `Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts`, `Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd` |
| 行号范围 | 94-97, 122-125, 151-154, 214-217 |
| 问题描述 | 这 4 个用例传给 `QueueScriptFileChanges()` 的 `EnumerateLoadedScripts` 参数都是“直接返回空数组”的 lambda，却没有记录调用次数，也没有在被调用时立即失败。生产实现里只有 `FCA_Removed` 的目录分支才应该触发 enumerator；如果未来回归成“普通 `.as` 变更、`.txt` 修改、folder add、rename 窗口也会误触发 enumerator”，当前测试仍可能保持绿色，因为这个空 lambda 会把错误副作用吞掉。 |
| 影响 | 会把“错误分支误走 folder-remove helper”这一类真实回归隐藏掉。目录枚举一旦在非 remove 路径被误触发，会额外增加 reload 成本，并可能把无关脚本塞进删除队列，但当前 4 个用例都观测不到这类副作用。 |
| 修复建议 | 把这 4 个用例里的 enumerator 换成带 `CallCount` 的 spy lambda，默认断言 `CallCount == 0`；若被调用则记录 `AbsoluteFolderPath` 并用 `AddError`/`TestEqual` 直接报错。只在 `FolderRemoveUsesLoadedScriptEnumerator` 这个 remove-folder 正例里保留“应当调用 enumerator”的断言，从而同时守住队列内容和 helper 选择。 |

### 二、需要新增的测试

#### NewTest-80：覆盖 `OnLiteralAssetSaved` 的无 debug client 错误路径，防止静默改写 script literal 内容

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `OnLiteralAssetSaved` |
| 现有测试覆盖 | 当前已有建议覆盖 `OnLiteralAssetPreSave` 的包过滤与 `OnLiteralAssetSaved` 的成功序列化分支，但第 125-128 行 `!FAngelscriptEngine::Get().HasAnyDebugServerClients()` 的 prerequisite gate 仍没有独立测试规格 |
| 风险评估 | 如果该 gate 回归，未连接 VS Code extension 时保存 literal curve 可能继续调用 `ReplaceScriptAssetContent()` 覆写脚本文本，或者反过来既不改写内容也不给用户任何错误提示；两种回归都会直接破坏 editor 工作流 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.OnLiteralAssetSavedShowsPrereqDialogAndSkipsRewriteWhenNoDebugClients` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorLiteralAssetTests.cpp`；若该文件接近 500 行则拆成 `AngelscriptEditorLiteralAssetErrorTests.cpp` |
| 场景描述 | 在 `FAngelscriptEngine::Get().AssetsPackage` 下创建一个 `UCurveFloat` literal asset，填入至少 1 个 key；把 debug-server-client gate 通过 test seam 固定为 `false` 后，直接触发 `OnLiteralAssetSaved()` 或走等价 test-only bridge |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 或等价 editor fixture；为 `FAngelscriptEngine::HasAnyDebugServerClients()`、`ReplaceScriptAssetContent()` 与 `FMessageDialog::Open()` 安装 spy/seam；准备一个已知 asset 名称，便于检查是否发生错误改写 |
| 期望行为 | `FMessageDialog::Open()` 恰好调用 1 次，提示需要运行 VS Code extension；`ReplaceScriptAssetContent()` 调用次数保持 `0`；曲线 key/default/infinity 配置不被序列化成任何新 script 内容 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + literal-asset fixture + debug-client/dialog/content-rewrite spies |
| 优先级 | P2 |

#### NewTest-81：覆盖 `PerformReinstance` 首次执行时的 `ReplaceHelper` 创建与复用生命周期

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 当前 `ClassReloadHelper` 建议已覆盖 reload state 聚合、struct/delegate 依赖刷新、enum/property editor/placement 副作用，以及 `UAngelscriptReferenceReplacementHelper` 自身的 `Serialize/AddReferencedObjects`；但第 33-38 行 `ReplaceHelper == nullptr` 时的懒创建与 `AddToRoot()` 生命周期仍没有任何独立测试规格 |
| 风险评估 | 如果主 reinstance 路径没有正确创建并复用全局 `ReplaceHelper`，open editor asset replacement 会变成“helper 单测能过、真实热重载时却没装上桥”的假阳性；反过来若每次都新建 helper，还会在长期编辑器会话里累积 rooted object 泄漏 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceCreatesAndReusesRootedReplaceHelper` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperLifecycleTests.cpp` |
| 场景描述 | 在 `bIsInitialCompileFinished == true` 的 editor fixture 下，通过 test accessor 观察 `ClassReloadHelper.cpp` 内的全局 `ReplaceHelper`；对一份最小 reload state 连续执行两次 `PerformReinstance()`，中间不重启 editor |
| 输入/前置 | 使用 engine/editor fixture + reload-state guard；增加 test-only accessor 或 friend seam 读取/重置 `ReplaceHelper` 指针；为 property editor / placement broadcast 安装最小 spy，避免把生命周期验证和其他副作用混在一起 |
| 期望行为 | 第一次执行前 `ReplaceHelper == nullptr`；第一次执行后 helper 非空且带 `RF_RootSet`；第二次执行后 helper 指针保持不变、transient package 下不会新增第二个同类 helper；测试结束时可通过 seam 恢复全局状态，不污染其他 reload tests |
| 使用的 Helper | engine/editor fixture + reload-state guard + `ReplaceHelper` accessor seam + minimal editor spies |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 31 文件逐项映射结论 | 重新按 `Plugins/Angelscript/Source/AngelscriptEditor/` 下全部 `15 .cpp + 16 .h` 逐项核对后，可确认当前口径是：production source `29` 个，其中有 direct 对应测试的只有 `7` 个、完全没有 direct test 的有 `22` 个；另有模块内测试实现 `2` 个，不参与“无测源码”判断 |
| 用户点名热点复核 | `EditorModule` family `3/3` 仍是 `0` direct test；`ClassReloadHelper` `2/2` 仍是 `0` direct test；`ContentBrowserDataSource` `2/2` 仍是 `0` direct test；`EditorMenuExtensions` `8/8` 仍是 `0` direct test。当前 direct coverage 仍然集中在 `SourceNavigation`、`DirectoryWatcherInternal`、`BlueprintImpactScanner + Commandlet` 三组 |

| 文件 | 类型 | 当前状态 | 对应测试 / 说明 |
|------|------|----------|----------------|
| `Private/AngelscriptContentBrowserDataSource.cpp` | Production | 无 direct test | — |
| `Private/AngelscriptContentBrowserDataSource.h` | Production | 无 direct test | — |
| `Private/AngelscriptDirectoryWatcherInternal.cpp` | Production | 有 direct test | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| `Private/AngelscriptDirectoryWatcherInternal.h` | Production | 有 direct test | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| `Private/AngelscriptEditorModule.cpp` | Production | 无 direct test | — |
| `Private/AngelscriptEditorModule.h` | Production | 无 direct test | — |
| `Private/AngelscriptEditorStateDump.cpp` | Production | 无 direct test | — |
| `Private/AngelscriptSourceCodeNavigation.cpp` | Production | 有 direct test | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` | Production | 有 direct test | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| `Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h` | Production | 有 direct test | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| `Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` | Production | 有 direct test | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| `Private/ClassReloadHelper.cpp` | Production | 无 direct test | — |
| `Private/ClassReloadHelper.h` | Production | 无 direct test | — |
| `Private/FunctionLibraries/AssetToolsStatics.h` | Production | 无 direct test | — |
| `Private/FunctionLibraries/EditorStatics.h` | Production | 无 direct test | — |
| `Private/FunctionLibraries/ScriptableFactory.cpp` | Production | 无 direct test | — |
| `Private/FunctionLibraries/ScriptableFactory.h` | Production | 无 direct test | — |
| `Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` | Test | 模块内测试实现 | 当前被审查对象之一，不计入 production direct coverage |
| `Private/Tests/AngelscriptDirectoryWatcherTests.cpp` | Test | 模块内测试实现 | 当前被审查对象之一，不计入 production direct coverage |
| `Public/BaseClasses/ScriptEditorSubsystem.h` | Production | 无 direct test | — |
| `Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h` | Production | 有 direct test | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| `Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp` | Production | 无 direct test | — |
| `Public/EditorMenuExtensions/ScriptActorMenuExtension.h` | Production | 无 direct test | — |
| `Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp` | Production | 无 direct test | — |
| `Public/EditorMenuExtensions/ScriptAssetMenuExtension.h` | Production | 无 direct test | — |
| `Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` | Production | 无 direct test | — |
| `Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` | Production | 无 direct test | — |
| `Public/EditorMenuExtensions/ScriptEditorPrompts.cpp` | Production | 无 direct test | — |
| `Public/EditorMenuExtensions/ScriptEditorPrompts.h` | Production | 无 direct test | — |
| `Public/FunctionLibraries/BlueprintMixinLibrary.h` | Production | 无 direct test | — |
| `Public/FunctionLibraries/EditorSubsystemLibrary.h` | Production | 无 direct test | — |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-37 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | MissingErrorPath: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 23:37:10 +08:00)

### 一、现有测试问题

#### Issue-38：`FindBlueprintAssetsDiskBacked` 只校验“命中目标包”，无法发现结果集过宽

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.FindBlueprintAssetsDiskBacked` |
| 行号范围 | 514-518 |
| 问题描述 | 当前用例在调用 `AngelscriptEditor::BlueprintImpact::FindBlueprintAssets(AssetRegistryModule.Get(), true)` 后，只用 `Assets.ContainsByPredicate(...)` 断言新保存的 `PackagePath` 存在。`FindBlueprintAssets()` 的语义是“返回当前 registry 里所有真正落盘的 blueprint 资产”，而不是“至少包含我刚创建的这一条”。在现有项目本身就存在大量磁盘蓝图、或过滤逻辑回归为过宽结果集时，这条测试仍会保持绿色，因为目标资产依然会被包含在过大的返回集中。 |
| 影响 | 无法发现 `disk-only` 过滤把无关磁盘 blueprint 一并放出来的误报。后续 `ScanBlueprintAssets()` 和 commandlet 如果因此扩大候选集，扫描时间和误报数量都会上升，但当前唯一 disk-backed 用例不会报警。 |
| 修复建议 | 把断言改成“基线 + 增量”或精确集合比较：先记录调用前 `FindBlueprintAssets(..., true)` 的 package 集合，再创建测试资产并重新查询，断言新增集合只多出当前 `PackagePath`；至少要补 `TestEqual` 校验新增条目数为 `1`，并验证返回集中没有额外新增的磁盘 blueprint。 |

### 二、需要新增的测试

#### NewTest-82：覆盖 `OnLiteralAssetSaved` 的零跨度/平坦曲线分支，验证无 ASCII graph 时仍能稳定序列化

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `OnLiteralAssetSaved` |
| 现有测试覆盖 | 当前已有建议只覆盖 `OnLiteralAssetSaved` 的成功图形化序列化路径和“无 debug client”错误路径；第 144-225 行 `MaxTime > MinTime && MaxValue > MinValue` 为 `false` 时跳过 ASCII graph 的分支仍无独立规格 |
| 风险评估 | 单点曲线、平坦曲线或所有 key 同值时，literal 保存逻辑会走“无图形注释”分支；如果这里回归为写出空内容、漏写 key/default/infinity，或错误进入除零敏感的 graph 代码，编辑器会静默改坏脚本文本，而现有建议不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.OnLiteralAssetSavedFlatCurvesSkipAsciiGraphButPreserveSerializedKeys` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorLiteralAssetTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorLiteralAssetSerializationTests.cpp` |
| 场景描述 | 在 `FAngelscriptEngine::Get().AssetsPackage` 下创建两个 `UCurveFloat`：一个只有单个 key，另一个有多个 key 但 `MinValue == MaxValue`；确保 debug client gate 为 `true` 后分别触发 `OnLiteralAssetSaved()` |
| 输入/前置 | 使用 editor fixture；为 `ReplaceScriptAssetContent()` 安装 spy，记录每次写回的 `NewContent`；两条曲线都设置至少 1 条 key、可选 `DefaultValue`、以及 `Pre/PostInfinityExtrap`，确保不依赖 graph comment 也能形成可断言文本 |
| 期望行为 | 两次调用都各自触发 1 次 `ReplaceScriptAssetContent()`；`NewContent` 中不包含 `/*`、`----` 或任何 graph legend 行，但仍包含对应的 `AddConstantCurveKey(...)` / `AddLinearCurveKey(...)`、`DefaultValue = ...;`、以及 `PreInfinityExtrap` / `PostInfinityExtrap` 文本；不会弹出错误对话框 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + literal-asset fixture + debug-client/content-rewrite spies |
| 优先级 | P1 |

#### NewTest-83：覆盖 `RegisterExtensions` 的 `ToolMenu` 注册元数据，验证默认 section header、insert position 与 alignment

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `UScriptEditorMenuExtension::RegisterExtensions` |
| 现有测试覆盖 | 现有 `EditorMenuExtensions` 建议覆盖 `Extend()` / `BuildToolMenuSection()` guard、分类排序、tool-menu context 和生命周期，但 `ScriptEditorMenuExtension.cpp:1092-1111` 里 `ToolMenu` 分支对 `SectionName`、默认 `MenuSectionHeader`、`ToolMenuInsertPosition`、`ToolMenuSectionAlign` 的注册元数据仍没有任何独立测试规格 |
| 风险评估 | 即使 `BuildToolMenuSection()` 本身还能生成 action，若 `RegisterExtensions()` 把 section 注册到错误位置、丢了默认 header，或 alignment 回归，实际 editor tool menu 仍会出现 section 名错误、位置漂移或布局异常；这些 UI 级回归不会被当前建议捕捉 |
| 建议测试名 | `Angelscript.TestModule.Editor.MenuExtensions.RegisterExtensionsToolMenuAppliesSectionMetadataDefaultsAndOverrides` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorMenuExtensionRegistrationTests.cpp`；若文件不存在则从现有 menu-extension 测试拆出 registration 子文件，保持单文件 300-500 行 |
| 场景描述 | 定义两个最小 test `UScriptEditorMenuExtension` 子类并走 `ToolMenu` 注册路径：第一个不设置 `MenuSectionHeader`，只设置 `ExtensionPoint`、`ToolMenuInsertPosition`、`ToolMenuInsertType` 和 `ToolMenuSectionAlign`；第二个显式设置自定义 `MenuSectionHeader` 作为对照。调用 `RegisterExtensions()` 后读取 `UToolMenus::Get()->ExtendMenu(ExtensionPoint)` 的 section 元数据 |
| 输入/前置 | 使用 editor fixture；准备一个可查询的 test `UToolMenu`；确保 `FAngelscriptEngine::IsInitialized()` 与 `IsInitialCompileFinished()` 条件满足，使 `InitializeExtensions()` / `RegisterExtensions()` 真正落到 tool-menu 分支；结束时调用 `UnregisterExtensions()` 清理全局注册 |
| 期望行为 | 默认组的 `SectionName` 等于扩展类 `FName`，section label 回退为扩展类 `GetDisplayNameText()`，`InsertPosition.Name/Position` 与扩展对象设置完全一致，`Section.Alignment` 等于 `ToolMenuSectionAlign`；自定义 header 组则精确使用 `MenuSectionHeader`，不会退回 display name；两组都只注册 1 个 dynamic entry，不会重复添加同名 section |
| 使用的 Helper | editor fixture + test `UScriptEditorMenuExtension` subclasses + `UToolMenu` probe |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮目标范围复核 | 重新核对 `Plugins/Angelscript/Source/AngelscriptEditor/` 后，非 `Tests/` 的 production 源码仍为 `29` 个文件；本轮用户指定测试目录内的现有测试 `.cpp` 仍只有 `3` 个，分别是 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` |
| 本轮 `EditorModule` 新增 0-hit 事实 | 在 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 重新全文检索后，`OnLiteralAssetSaved`、`ReplaceScriptAssetContent`、`PreInfinityExtrap`、`PostInfinityExtrap` 仍然都是 `0` 直接命中；说明 literal curve 写回逻辑到本轮之前仍只停留在路由级建议，没有任何直接测试锁住“写什么内容”与“平坦曲线怎么写” |
| 本轮 `EditorMenuExtensions` 新增 0-hit 事实 | 目标测试树内对 `MenuSectionHeader`、`ToolMenuSectionAlign`、`ToolMenuInsertPosition`、`ToolMenuInsertType` 的检索结果仍为 `0`；说明 `RegisterExtensions()` 的 `ToolMenu` 注册元数据路径到本轮之前还没有任何独立规格，现有 menu-extension 建议仍主要集中在 action 构建和生命周期，而不是 section 注册形态 |
| 本轮增量结论 | 现有测试质量方面，本轮新增问题继续集中在 `BlueprintImpact` 唯一 disk-backed 用例的断言精度不足；无测源码方面，本轮把用户点名的 `EditorModule` 与 `EditorMenuExtensions` 再下钻到两个此前未落盘的函数级分支：`OnLiteralAssetSaved` 的 flat-curve 序列化，以及 `RegisterExtensions` 的 `ToolMenu` section 元数据注册 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-38 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 23:46:22 +08:00)

### 一、现有测试问题

本轮重新逐行复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与已有 `EditorAndTools_TestGaps.md` 记录逐项去重；没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

#### NewTest-84：覆盖 `OnLiteralAssetSaved` 的 weighted cubic tangent 序列化分支，锁定函数名与 `broken` 参数位置

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `OnLiteralAssetSaved` |
| 现有测试覆盖 | 现有建议已覆盖 `OnLiteralAssetSaved` 的普通 key/default/infinity 序列化、无 debug client 错误路径和平坦曲线分支，但第 281-305 行 `RCTWM_WeightedArrive` / `RCTWM_WeightedLeave` / `RCTWM_WeightedBoth` 这组 weighted cubic tangent 序列化仍无任何独立规格 |
| 风险评估 | 这一分支负责输出 `AddCurveKeyWeighted*` 系列文本，一旦函数名选择、`broken` 布尔位置或权重参数顺序回归，保存 literal curve 会写出错误的 script API 调用，用户只有在运行脚本或重新加载曲线时才会发现，当前 editor tests 不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.OnLiteralAssetSavedWeightedTangentsEmitExpectedFunctionAndArgumentOrder` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorLiteralAssetSerializationTests.cpp`；若该文件不存在则从现有 literal-asset 建议拆出新文件，保持单文件 300-500 行 |
| 场景描述 | 在 `FAngelscriptEngine::Get().AssetsPackage` 下创建 3 个 `UCurveFloat` literal asset，分别设置 `RCTWM_WeightedArrive`、`RCTWM_WeightedLeave`、`RCTWM_WeightedBoth` 的 cubic key，其中再挑一个样本把 `TangentMode` 设为 `RCTM_Break` 作为 broken 对照；debug client gate 固定为 `true` 后分别触发 `OnLiteralAssetSaved()` |
| 输入/前置 | 使用 editor fixture；为 `ReplaceScriptAssetContent()` 安装 spy 记录 `NewContent`；每个 key 都显式设置 `Time`、`Value`、`ArriveTangent`、`LeaveTangent`、`ArriveTangentWeight`、`LeaveTangentWeight`，避免断言只停留在“包含某个函数名” |
| 期望行为 | `WeightedArrive` 样本的输出包含且只包含一次 `AddCurveKeyWeightedArriveTangent(...)`；`WeightedLeave` 样本输出 `AddCurveKeyWeightedLeaveTangent(...)`；`WeightedBoth` 样本输出 `AddCurveKeyWeightedBothTangent(...)`；每条语句都按 `time, value, brokenBool, arriveTangent, leaveTangent, arriveWeight, leaveWeight` 的顺序序列化，`RCTM_Break` 样本的第三个实参必须是 `true`，其余样本为 `false` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + literal-asset fixture + debug-client/content-rewrite spies |
| 优先级 | P1 |

#### NewTest-85：覆盖 `PerformReinstance` 的 delegate 依赖蓝图重编译链路，验证 `ReloadDelegates/NewDelegates` 真正进入 dependency queue

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 现有 `ClassReloadHelper` 建议已覆盖 reload state 聚合、struct reload 的 `QueueForCompilation/FlushCompilationQueueAndReinstance`、enum refresh、property editor refresh 和 volume actor factory，但第 98-105 行把 `ReloadDelegates/NewDelegates` 注入 `ImpactSymbols`、以及第 266-299 行据此把依赖 blueprint 加入重编译队列的 delegate 专属链路仍无独立规格 |
| 风险评估 | 一旦 `ReloadDelegates`/`NewDelegates` 没有正确驱动 `AnalyzeLoadedBlueprint()` 和后续 compilation queue，reload 后的 blueprint event 节点会继续引用旧 delegate signature，表现为 editor 里事件节点不刷新、运行时报错或必须手工重建节点；当前测试网无法提前发现 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceRecompilesBlueprintsBoundToReloadedDelegates` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperDelegateTests.cpp` |
| 场景描述 | 在 editor fixture 下准备一个绑定到外部 `UDelegateFunction` 的 blueprint event 节点，并额外准备一个不依赖该 delegate 的对照 blueprint；把 `OldDelegate -> NewDelegate` 填入 `ReloadState().ReloadDelegates`，同时把 `NewDelegate` 放入 `NewDelegates`，随后执行 `PerformReinstance()` |
| 输入/前置 | 使用 engine/editor fixture + reload-state guard；复用或补充一个“创建绑定外部 delegate 的 blueprint event node” helper，确保 `FindEventSignatureFunction()` 返回 `OldDelegate`；为 `FBlueprintCompilationManager::QueueForCompilation()` / `FlushCompilationQueueAndReinstance()` 安装 spy，必要时对 `Schema->ReconstructNode()` 也挂 seam 记录命中的 blueprint |
| 期望行为 | 依赖 `OldDelegate` 的 blueprint 恰好被 `QueueForCompilation()` 入队 1 次，并在 `FlushCompilationQueueAndReinstance()` 后完成一次刷新；其 event node 在重建路径中被命中至少 1 次；不依赖该 delegate 的对照 blueprint 不会入队；执行结束后 `ReloadState().ReloadDelegates` / `NewDelegates` 仍按现有 `OnPostReload` 规则可被后续 reset |
| 使用的 Helper | engine/editor fixture + reload-state guard + delegate-bound blueprint helper + compilation queue / reconstruct-node spies |
| 优先级 | P1 |

#### NewTest-86：覆盖 `CompileFilter` 的“合法类名 + 非法类名”混合输入，验证未知类不会污染有效过滤结果

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h` |
| 关联函数 | `UAngelscriptContentBrowserDataSource::CompileFilter`, `EnumerateItemsMatchingFilter` |
| 现有测试覆盖 | 现有 `ContentBrowserDataSource` 建议已覆盖 root/item-type gate、include/exclude 正常路径、reused compiled filter stale-state、callback break 和 unsupported API，但第 50-61 行 `FindObject<UClass>` 对未知 `ClassNamesToInclude/ClassNamesToExclude` 的忽略分支仍没有任何独立规格 |
| 风险评估 | content browser 或脚本侧一旦传入拼错/未加载的 class path，过滤器如果把未知类当成“清空结果”或“跳过整个 exclude 集”，会导致 `/All/Angelscript` 视图随机漏资产或漏过滤；这种 query-shaping 回归当前文档还没有专门的保护网 |
| 建议测试名 | `Angelscript.TestModule.Editor.ContentBrowserDataSource.CompileFilterIgnoresUnknownClassNamesButKeepsResolvedIncludesAndExcludes` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptContentBrowserDataSourceFilterTests.cpp`；若该文件不存在则从 `AngelscriptContentBrowserDataSourceTests.cpp` 拆出 filter 子文件，保持单文件 300-500 行 |
| 场景描述 | 在 `FAngelscriptEngine::Get().AssetsPackage` 下准备一个 `UCurveFloat` 和一个 `UDataAsset`；构造 `FContentBrowserDataClassFilter`，`ClassNamesToInclude` 同时放入 `"/Script/CoreUObject.Object"` 与一个不存在的类名，`ClassNamesToExclude` 同时放入 `"/Script/Engine.CurveFloat"` 与一个不存在的类名；编译后执行枚举 |
| 输入/前置 | 使用 data-source fixture；确保 `ItemTypeFilter` 包含 `IncludeFiles`，`ItemCategoryFilter` 包含 `IncludeAssets`，`InPath` 为 `"/"`；枚举 callback 记录命中的对象路径和数量 |
| 期望行为 | 枚举结果仍包含 `UDataAsset` 对应 item，说明合法 include 未被未知类名冲掉；`UCurveFloat` item 被 exclude 正确过滤；不存在的类名不会导致 crash、不会让结果集清空，也不会把本应排除的曲线重新放出来 |
| 使用的 Helper | data-source fixture + script asset helper + compiled-filter builder |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 31 文件复扫 | 重新按 `Plugins/Angelscript/Source/AngelscriptEditor/` 下全部 `.cpp/.h` 统计后，当前口径仍是 `31` 个文件，其中 production 源码 `29` 个、模块内测试实现 `2` 个；有 direct 对应测试的 production 文件仍为 `7` 个，完全没有 direct test 的 production 文件仍为 `22` 个 |
| 用户点名热点复核 | `EditorModule` family 仍是 `0/3` direct test，`ClassReloadHelper` 仍是 `0/2`，`ContentBrowserDataSource` 仍是 `0/2`，`EditorMenuExtensions` 仍是 `0/8`；本轮新增建议继续集中在这些 0-hit 区域的函数级分支，而不是重复铺文件级空白 |
| 本轮新增 0-hit 事实 | 在用户指定现有测试目录 `Plugins/Angelscript/Source/AngelscriptTest/Editor/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 重新全文检索后，`WeightedArrive/WeightedLeave/WeightedBoth`、`ReloadDelegates/NewDelegates`、`ClassNamesToInclude/ClassNamesToExclude`、`OnLiteralAssetSaved`、`QueueForCompilation`、`FlushCompilationQueueAndReinstance` 仍然都是 `0` 直接命中；说明本轮新增的 3 条建议对应的确实是此前未落盘的函数级缺口 |
| 本轮增量结论 | 现有 3 个测试文件的质量问题在本轮去重后没有新增；新增缺口主要下钻到 3 条此前未具象化的执行链：literal curve weighted tangent 序列化、delegate reload 驱动 blueprint 重新编译、以及 content browser filter 对未知 class path 的容错 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingEdgeCase: 1, MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 00:34 +08:00)

### 一、现有测试问题

本轮重新逐行复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并对照已有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

#### NewTest-87：覆盖 `LexToString` 的全枚举映射与非法枚举兜底，锁住 commandlet 输出协议

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp` |
| 关联函数 | `AngelscriptEditor::BlueprintImpact::LexToString` |
| 现有测试覆盖 | 现有 `BlueprintImpact` 建议已覆盖 `NormalizeChangedScriptPaths`、`FindModulesForChangedScripts`、`BuildImpactSymbols`、`AnalyzeLoadedBlueprint`、`ScanBlueprintAssets` 与 commandlet 返回码，但第 312-330 行把 `EBlueprintImpactReason` 序列化为日志字符串的 `LexToString()` 仍没有任何独立规格 |
| 风险评估 | commandlet 第 102-115 行会直接把 `LexToString()` 结果写入 `[BlueprintImpact] ... | Reasons=...` 日志。若字符串常量回归、顺序对不上枚举，或 default 分支不再返回 `Unknown`，CI/脚本解析链会在“扫描本身仍成功”的情况下静默失效，而当前测试不会报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.LexToStringMapsAllReasonsAndUnknownFallback` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactFormattingTests.cpp` |
| 场景描述 | 逐个调用 `LexToString(EBlueprintImpactReason::ScriptParentClass / NodeDependency / PinType / VariableType / DelegateSignature / ReferencedAsset)`，再传入一个 `static_cast<EBlueprintImpactReason>(255)` 作为非法枚举对照 |
| 输入/前置 | 无需引擎或 blueprint 夹具；使用纯 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 即可 |
| 期望行为 | 六个合法枚举分别精确返回 `ScriptParentClass`、`NodeDependency`、`PinType`、`VariableType`、`DelegateSignature`、`ReferencedAsset`；非法枚举精确返回 `Unknown`；断言使用 `TestEqual`，不要只做非空判断 |
| 使用的 Helper | 纯 unit automation test |
| 优先级 | P2 |

#### NewTest-88：覆盖 `SaveEditorReloadState` 的完整类别导出，锁住 `EditorReloadState.csv` 的 schema 完整性

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `SaveEditorReloadState`, `RegisterStateDumpExtension`, `UnregisterStateDumpExtension` |
| 现有测试覆盖 | 现有 `EditorStateDump` 建议只要求 `EditorReloadState.csv` 存在并至少包含一条 `ReloadClass` 记录；目标目录外的 `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` 也只在 dump-all end-to-end 中检查文件名存在。`SaveEditorReloadState()` 第 30-60 行导出的 `ReloadClass / NewClass / ReloadEnum / NewEnum / ReloadStruct / ReloadDelegate` 六类行，以及 `Category,OldName,NewName` 表头本身，仍没有任何独立规格 |
| 风险评估 | 若后续重构把某一类 reload 行漏写、列顺序改坏，或 `ReloadDelegate` / `ReloadStruct` 被静默丢掉，dump-all smoke 仍会是绿色，因为文件还在；真正依赖 CSV 做热重载诊断的人工排查和工具链会拿到不完整数据 |
| 建议测试名 | `Angelscript.TestModule.Editor.StateDump.EditorReloadStateExportsAllTrackedReloadCategories` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorStateDumpTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorStateDumpReloadStateTests.cpp` |
| 场景描述 | 先保存当前 `FClassReloadHelper::ReloadState()`，再手工填入 1 条 `ReloadClasses`、1 条 `NewClasses`、1 条 `ReloadEnums`、1 条 `NewEnums`、1 条 `ReloadStructs`、1 条 `ReloadDelegates`；通过 `RegisterStateDumpExtension()` 驱动一次 `FAngelscriptStateDump::DumpAll()`，随后读取生成的 `EditorReloadState.csv` |
| 输入/前置 | 使用 reload-state guard 保存并恢复全局状态；准备最小可命名的 `UClass` / `UEnum` / `UScriptStruct` / `UDelegateFunction` 样本；输出目录使用唯一 `Saved/Automation/StateDump/<Guid>` 子目录；测试结束时调用 `UnregisterStateDumpExtension()` 并清理目录 |
| 期望行为 | CSV 首行精确等于 `Category,OldName,NewName`；数据区恰好包含 6 条类别记录，分别为 `ReloadClass`、`NewClass`、`ReloadEnum`、`NewEnum`、`ReloadStruct`、`ReloadDelegate`；其中 reload 类别的 `OldName/NewName` 对应旧新对象名，new 类别的 `OldName` 为空串；不应只停留在“文件存在”或“包含某个子串”的弱断言 |
| 使用的 Helper | `FAngelscriptStateDump::DumpAll` + reload-state guard + CSV file reader helper |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 31 文件实扫复核 | 重新按 `Plugins/Angelscript/Source/AngelscriptEditor/` 下全部 `.cpp/.h` 统计，口径仍是 `31` 个文件，其中当前分析范围内的 production 源码 `29` 个、模块内测试实现 `2` 个；现有 3 个目标测试文件对 production 源码的 direct-hit 计数仍为 `7`，完全无 direct test 的 production 文件仍为 `22` |
| 用户点名热点复核 | `EditorModule` family 仍是 `0/3` direct test，`ClassReloadHelper` 仍是 `0/2`，`ContentBrowserDataSource` 仍是 `0/2`，`EditorMenuExtensions` 仍是 `0/8`；本轮新增的 2 条建议没有重复铺文件级空白，而是继续下钻到这些周边链路里的函数级导出契约 |
| 本轮新增 0-hit 事实 | 在 `Plugins/Angelscript/Source/AngelscriptTest/Editor/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 重新全文检索后，`LexToString`、`SaveEditorReloadState`、`ReloadDelegate`、`ReloadStruct` 仍然都是 `0` 直接命中；说明本轮新增建议对应的确实是此前未落盘的函数级缺口 |
| 目标目录外的边界说明 | `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` 会在 dump-all end-to-end 中检查 `EditorReloadState.csv` / `EditorMenuExtensions.csv` 文件名存在，但没有校验 CSV 表头、类别行或 `ReloadDelegate/ReloadStruct` 内容；因此它不能替代当前目录范围里对 `AngelscriptEditorStateDump.cpp` 导出语义的 direct test |
| 本轮增量结论 | 现有 3 个测试文件的质量问题在本轮去重后仍无新增；新的空白集中在“导出文本协议”层面，即 `BlueprintImpact` 的 reason string 映射和 `EditorStateDump` 的 reload-state CSV schema，这两处都不是文件存在性 smoke 能守住的分支 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | MissingEdgeCase: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:05:15 +08:00)

### 一、现有测试问题

本轮重新逐行复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与已有 `EditorAndTools_TestGaps.md` 记录逐项去重；没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

#### NewTest-89：覆盖 `AnalyzeLoadedBlueprint` 的 enum/byte 分支，锁住 `MatchesPinType` 对 `PC_Enum` / `PC_Byte` 的判定

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp` |
| 关联函数 | `MatchesPinType`, `AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint` |
| 现有测试覆盖 | 当前 `AnalyzeVariableType` 与 `AnalyzePinType` 只覆盖 `FVector` 对应的 `PC_Struct` 分支；已有新增建议补到了 `EditablePinBase` / `MacroInstance` 与无影响路径，但 `MatchesPinType()` 第 51-54 行对 `PC_Enum` / `PC_Byte` 的分支仍然没有任何 direct spec |
| 风险评估 | 一旦 enum 或 byte-backed enum pin/variable 不再被识别为 impacted symbol，`BlueprintImpact` scanner、`ClassReloadHelper` 的依赖蓝图重编译、以及 commandlet 的影响清单都会对 enum 变更产生漏报 |
| 建议测试名 | `Angelscript.TestModule.Editor.BlueprintImpact.AnalyzeLoadedBlueprint.EnumPinsAndVariablesReportExactReasons` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactAnalyzeLoadedBlueprintTests.cpp`；若该文件不存在则从现有 scanner tests 拆出新文件，保持单文件 300-500 行 |
| 场景描述 | 创建一个 transient blueprint，向 `NewVariables` 加入一个 `PinCategory = PC_Byte`、`PinSubCategoryObject = StaticEnum<EAutoReceiveInput::Type>()` 的变量；再创建一个 graph node，并把其中一个 pin 的 `PinCategory` 改成 `PC_Enum` 且 `PinSubCategoryObject` 指向同一个 enum；随后用仅包含该 enum 的 `FBlueprintImpactSymbols` 调用 `AnalyzeLoadedBlueprint()`，并补一次 unrelated enum 对照 |
| 输入/前置 | 复用现有 `CreateTransientBlueprintChild` / `AddCallFunctionNode` helper；优先使用稳定的引擎内建 enum 如 `EAutoReceiveInput::Type`，避免额外 script compile 依赖；对照组可复用另一个无关 `UEnum` |
| 期望行为 | 命中 enum symbols 时 `AnalyzeLoadedBlueprint()` 返回 `true`；`Reasons.Num() == 2`，且精确等于 `{ EBlueprintImpactReason::PinType, EBlueprintImpactReason::VariableType }`，不能混入 `NodeDependency` / `ReferencedAsset` 等额外 reason；切换到无关 enum 后返回 `false` 且 `Reasons.Num() == 0` |
| 使用的 Helper | 现有 blueprint helper + `IMPLEMENT_SIMPLE_AUTOMATION_TEST` |
| 优先级 | P1 |

#### NewTest-90：覆盖 `QueueScriptFileChanges` 的多 root 前缀冲突路径，验证第二个合法 root 不会被第一个 root 吞掉

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 关联函数 | `TryMakeRelativeScriptPath`, `AngelscriptEditor::Private::QueueScriptFileChanges` |
| 现有测试覆盖 | 当前 `DirectoryWatcherTests` 全部通过 `MakeTestEngineWithRoot()` 只配置单个 root；已有建议覆盖了 root 外负例和“共享字符串前缀但不在任何 root 内”的路径，但还没有任何规格验证“两个都合法、且第二个 root 以前缀形式包含第一个 root”时应选择真正命中的 root |
| 风险评估 | 在同时存在 project root 与 plugin root、或多个 script root 命名相近的工程里，文件变化如果先被较短 root 命中，relative path 会被错误计算甚至落到错误队列，最终表现为 hot reload 找不到变更脚本或把错误模块标成 changed |
| 建议测试名 | `Angelscript.TestModule.Editor.DirectoryWatcher.Queue.UsesMatchingRootWhenMultipleRootsSharePrefix` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`；若文件接近 300 行则拆成 `AngelscriptDirectoryWatcherRootResolutionTests.cpp` |
| 场景描述 | 在同一个 temp base 下创建两个真实 root，例如 `<Base>/Scripts` 与 `<Base>/ScriptsPlugin`，并把较短 root 放在 `AllRootPaths[0]`；随后向 `QueueScriptFileChanges()` 传入一个位于第二个 root 内的 `.as` 文件变化 |
| 输入/前置 | 不复用 `MakeTestEngineWithRoot()`，而是手工构造 `FAngelscriptEngine` 并填入两个 absolute roots；文件系统侧创建第二个 root 下的真实脚本文件；enumerator lambda 维持调用计数，确保非 remove 路径不触发 |
| 期望行为 | `FileChangesDetectedForReload.Num() == 1`，`FileDeletionsDetectedForReload.Num() == 0`；唯一入队项的 `AbsolutePath` 等于第二个 root 下的脚本绝对路径，`RelativePath` 必须相对第二个 root 计算且不带第一个 root 的残余前缀；enumerator 调用次数保持 `0` |
| 使用的 Helper | 现有 temp-directory helper + 手工多 root engine fixture |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `BlueprintImpact` 新增 0-hit 事实 | 在用户指定测试目录与当前 gap 文档内重新检索后，`PC_Enum`、`PC_Byte`、`EAutoReceiveInput::Type` 仍然没有任何 direct test 规格落到 `AnalyzeLoadedBlueprint`；说明 scanner 对 enum/byte impact 的分支直到本轮之前仍停留在源码层空白 |
| 本轮 `DirectoryWatcher` 新增 0-hit 事实 | 目标测试树和 gap 文档里仍然没有任何 case 构造 `AllRootPaths` 包含两个合法 root，更没有覆盖“第二个 root 以前缀形式包含第一个 root”的路径解析；当前 watcher 建议仍主要集中在单 root、时间戳和 enumerator 契约 |
| 31 文件口径复核 | 重新按 `Plugins/Angelscript/Source/AngelscriptEditor/` 统计后，目标范围仍是 `31` 个 `.cpp/.h` 文件，其中 production 源码 `29` 个、模块内测试实现 `2` 个；direct-hit production 文件仍为 `7` 个，完全无 direct test 的 production 文件仍为 `22` 个 |
| 本轮增量结论 | 现有 3 个测试文件的质量问题在本轮去重后仍无新增；新增缺口继续下钻到此前未落盘的两个函数级空白：`BlueprintImpact` 的 enum/byte 符号识别，以及 `DirectoryWatcher` 的多 root 前缀冲突解析 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 1, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 00:15:22 +08:00)

### 一、现有测试问题

本轮再次逐行复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与现有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

#### NewTest-91：覆盖 `OnClassReload` 的即时副作用，锁住 `RefreshClassActions` 与 component registry invalidation

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` |
| 关联函数 | `FClassReloadHelper::Init` 中的 `FAngelscriptClassGenerator::OnClassReload` lambda |
| 现有测试覆盖 | 现有 `ClassReloadHelper` 建议已经覆盖 `ReloadState` 聚合、struct/delegate 依赖重编译、enum refresh、property editor refresh、volume actor factory 和备用 `FReload` 路径，但 `ClassReloadHelper.h:54-89` 里“普通 class/component class reload 发生时立刻调用 `FBlueprintActionDatabase::RefreshClassActions()`，并对新 component class 调用 `FComponentTypeRegistry::InvalidateClass()`”仍没有任何独立规格 |
| 风险评估 | 如果 `OnClassReload` 仍把 old/new class 记进 `ReloadState()`，却不再刷新 blueprint action database 或不再使 component registry 失效，editor 热重载后会表现为节点菜单缺少新类动作、组件面板看不到更新后的 component class，而现有建议不会直接报警 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.OnClassReloadRefreshesClassActionsAndInvalidatesComponentRegistryForNonInterfaceComponents` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperClassReloadTests.cpp` |
| 场景描述 | 在 editor fixture 下准备一对不带 interface 的 `UActorComponent` old/new class，并额外准备一对普通非 component class 作为对照；调用 `FClassReloadHelper::Init()` 后广播 `FAngelscriptClassGenerator::OnClassReload(OldClass, NewClass)` |
| 输入/前置 | 使用 engine/editor fixture + reload-state guard；为 `FBlueprintActionDatabase::RefreshClassActions()` 和 `FComponentTypeRegistry::InvalidateClass()` 安装 spy seam；确保 `GEngine != nullptr`，避免分支被环境短路 |
| 期望行为 | component pair 广播后，`ReloadState().ReloadClasses.Find(OldComponentClass) == NewComponentClass`；`bRefreshAllActions == false`；`RefreshClassActions` 恰好命中 old/new component class 各 1 次；`InvalidateClass(NewComponentClass)` 恰好调用 1 次；`bReloadedVolume` 仍为 `false`。普通非 component 对照组仍会刷新 old/new class actions，但不会触发 `InvalidateClass` |
| 使用的 Helper | engine/editor fixture + reload-state guard + blueprint-action/component-registry spies |
| 优先级 | P1 |

#### NewTest-92：覆盖 `SaveEditorMenuExtensions` 的 CSV schema 与 location 字符串映射

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h` |
| 关联函数 | `SaveEditorMenuExtensions`, `GetExtensionLocationString`, `RegisterStateDumpExtension` |
| 现有测试覆盖 | 现有 `EditorStateDump` 建议只要求 `EditorMenuExtensions.csv` 文件存在且至少包含一条 menu-extension 记录；目标目录外的 dump smoke 也只验证文件名存在。`AngelscriptEditorStateDump.cpp:64-94` 对表头 `ExtensionPoint,Location,SectionName`、`Location` 的 `UEnum` 字符串化，以及 snapshot 行内容本身仍没有任何独立规格 |
| 风险评估 | 如果 `SaveEditorMenuExtensions()` 改坏列顺序、把 `Location` 写成空串/数值、或 `SectionName` 与 snapshot 脱节，dump 文件依然会“存在”，但基于 CSV 做诊断和对比的工具链会静默失真；现有建议守不住这类协议回归 |
| 建议测试名 | `Angelscript.TestModule.Editor.StateDump.EditorMenuExtensionsExportsReadableLocationRows` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorStateDumpTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorStateDumpMenuExtensionsTests.cpp` |
| 场景描述 | 在 editor fixture 下准备两类可识别的 menu extension snapshot：例如一个 `ToolMenu` 扩展和一个 `ContentBrowser_AssetViewContextMenu` 扩展；注册 state dump extension 后触发一次 `FAngelscriptStateDump::DumpAll()`，读取生成的 `EditorMenuExtensions.csv` |
| 输入/前置 | 使用 menu-extension registration guard 保存并恢复全局 `RegisteredExtensions`/tool-menu 状态；通过真实 test `UScriptEditorMenuExtension` 子类或 test-only snapshot seam 生成两条已知 snapshot；输出目录使用唯一 `Saved/Automation/StateDump/<Guid>` 子目录；结束时调用 `UnregisterStateDumpExtension()` 并清理目录 |
| 期望行为 | CSV 首行精确等于 `ExtensionPoint,Location,SectionName`；数据区至少包含两条针对测试 snapshot 的记录；两条记录的 `ExtensionPoint` 与预设值完全一致，`Location` 精确等于 `ToolMenu` 与 `ContentBrowser_AssetViewContextMenu` 这类可读 enum 名称，而不是数字或空串；`SectionName` 精确等于 snapshot 记录的 section 名称 |
| 使用的 Helper | editor fixture + menu-extension registration guard + CSV file reader helper |
| 优先级 | P2 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `ClassReloadHelper` 新增 0-hit 事实 | 在目标测试树与现有 gap 文档内重新检索后，`RefreshClassActions`、`InvalidateClass(`、`OnClassReload(` 仍然都是 `0` 直接命中；说明 `ClassReloadHelper` 目前已有建议已深入到 reload 后半段，但 class-reload 入口的即时副作用仍未被单独锁住 |
| 本轮 `EditorStateDump` 新增 0-hit 事实 | `SaveEditorMenuExtensions`、`GetExtensionLocationString`、`EditorMenuExtensions.csv` 表头 `ExtensionPoint,Location,SectionName` 在目标测试目录里仍没有任何 direct spec；当前已有建议只锁住“文件存在”与 `EditorReloadState.csv`，还没有对 menu-extension dump 协议本身建立精确断言 |
| 31 文件口径复核 | 重新按 `Plugins/Angelscript/Source/AngelscriptEditor/` 统计后，目标范围仍是 `31` 个 `.cpp/.h` 文件，其中 production 源码 `29` 个、模块内测试实现 `2` 个；direct-hit production 文件仍为 `7` 个，完全无 direct test 的 production 文件仍为 `22` 个 |
| 本轮增量结论 | 现有 3 个测试文件的质量问题在本轮去重后仍无新增；新增缺口继续收敛到用户点名热点里的两个函数级协议空白：`OnClassReload` 的即时 editor 副作用，以及 `EditorMenuExtensions.csv` 的导出 schema |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:24:24 +08:00)

### 一、现有测试问题

#### Issue-39：`MakeCodeSection` 在纯 unit helper 里硬编码 `J:/Dummy/Script.as`，把本地盘符假设带进了扫描器测试

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 测试名 | `Angelscript.Editor.BlueprintImpact.MatchChangedScriptsToModuleSections` |
| 行号范围 | 87-94 |
| 问题描述 | `MakeCodeSection()` 的默认参数把 `AbsoluteFilename` 写死为 `TEXT("J:/Dummy/Script.as")`。当前 `FindModulesForChangedScripts()` 只读取 `RelativeFilename`，所以用例暂时还能通过；但这已经把一个开发机盘符假设埋进了共享 helper。只要后续 scanner 分支、日志或断言开始消费 `AbsoluteFilename`，这组本应是纯内存 unit 的测试就会突然依赖本地 `J:` 盘路径。 |
| 影响 | 这会让 `BlueprintImpact` 的最轻量测试夹具继续携带平台/机器耦合：非 `J:` 盘工作区上的失败会变成“看起来像源码回归、实际是 helper 路径假设”，同时也掩盖了 `AbsoluteFilename` 是否真的应该由调用方显式控制。 |
| 修复建议 | 去掉该默认盘符路径。若测试不关心 `AbsoluteFilename`，就让 helper 强制调用方显式传值，或统一改成基于 `FPaths::ProjectSavedDir()/Automation/...` 生成的临时路径；若仅需要占位字符串，也应使用相对项目可解析的 `Saved` 路径而不是固定盘符。 |

### 二、需要新增的测试

#### NewTest-93：覆盖 `PerformReinstance` 对 struct 引用的原地重写，锁住 `ReplacePinType` 在四种 carrier 上都生效

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp` |
| 关联函数 | `FClassReloadHelper::FReloadState::PerformReinstance` |
| 现有测试覆盖 | 现有 `ClassReloadHelper` 建议已覆盖 reload-state 聚合、struct/delegate 依赖重编译、enum refresh、property editor refresh 和 volume/placement 副作用；但 `ClassReloadHelper.cpp:55-140` 里 `ReplacePinType` 对 `Node->Pins`、`EditablePinBase::UserDefinedPins`、`UK2Node_MacroInstance::ResolvedWildcardType`、`Blueprint.NewVariables` 的原地替换仍没有任何独立规格。 |
| 风险评估 | 如果热重载后只有 dependency queue 被触发、但 pin type 没被换成 `NewStruct`，蓝图会继续持有旧 struct 指针，表现为节点刷新后仍报类型错误、宏 wildcard 不恢复、变量面板保留旧 struct，且现有测试只能看到“有没有入队重编译”，看不到“引用到底改没改”。 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceRewritesStructReferencesAcrossPinsVariablesAndMacroWildcards` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperStructRewriteTests.cpp` |
| 场景描述 | 构造一个依赖 `OldStruct` 的 transient blueprint，同时覆盖四种 carrier：普通 node pin、`UK2Node_EditablePinBase` 的 `UserDefinedPins`、`UK2Node_MacroInstance::ResolvedWildcardType`、以及 `NewVariables`。把 `ReloadState().ReloadStructs` 填成 `OldStruct -> NewStruct` 后执行一次 `PerformReinstance()`，并保留一个仍指向无关 struct 的对照 pin。 |
| 输入/前置 | 使用 engine/editor fixture + reload-state guard；补一个最小 helper 负责创建带 `OldStruct` 类型的 variable、custom event user pin、macro wildcard 和普通 graph pin；必要时对 compilation queue 装最小 spy，但断言重点放在 `PinSubCategoryObject`/`VarType` 的最终值。 |
| 期望行为 | 执行后四处 carrier 的 struct 引用都精确改成 `NewStruct`：普通 pin、`UserDefinedPins[*].PinType.PinSubCategoryObject`、`ResolvedWildcardType.PinSubCategoryObject`、`Blueprint.NewVariables[*].VarType.PinSubCategoryObject` 全部等于 `NewStruct`；无关 struct 对照保持不变；断言不能只停留在“queue 触发了”或“AnalyzeLoadedBlueprint 返回 true”。 |
| 使用的 Helper | engine/editor fixture + reload-state guard + blueprint graph helper |
| 优先级 | P1 |

#### NewTest-94：覆盖 `OnPostReload(true)` 的正向 editor 副作用，锁住 `RefreshAll` / `BroadcastBlueprintCompiled` / volume rebuild current-level restore

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::Init` 中的 `FAngelscriptClassGenerator::OnPostReload` lambda |
| 现有测试覆盖 | 现有建议只覆盖 `OnPostReload` 的 state reset、enum refresh、property editor refresh、volume actor factory 和 class-reload 入口副作用；`ClassReloadHelper.h:143-170` 里 `FBlueprintActionDatabase::RefreshAll()`、`GEditor->BroadcastBlueprintCompiled()`、`GEngine->Exec(World, "MAP REBUILD ALLVISIBLE")` 以及 rebuild 后 `World->SetCurrentLevel(CurrentLevel)` 的恢复路径仍没有任何独立规格。 |
| 风险评估 | 如果 full reload 后没有刷新 blueprint action database、没有广播 blueprint compiled、或 volume geometry rebuild 把 `CurrentLevel` 留在错误关卡，editor 表现会直接退化成“右键菜单还是旧类型”“依赖监听不到热重载”“Build Geometry 后活动关卡被悄悄切走”；这些都是用户可见回归，但当前测试网还守不到。 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.OnPostReloadFullReloadRefreshesActionsBroadcastsBlueprintCompiledAndRestoresCurrentLevel` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperPostReloadTests.cpp` |
| 场景描述 | 在 editor fixture 下准备一个有多个 level 的 world，并把 `CurrentLevel` 切到非默认 level；预填 `ReloadState().bRefreshAllActions = true` 与 `ReloadState().bReloadedVolume = true`，然后广播一次 `OnPostReload(true)`。通过 spy/seam 观察 action refresh、compiled broadcast 和 engine exec；exec seam 里模拟 map rebuild 把 `CurrentLevel` 改乱，以验证 lambda 末尾会恢复原 level。 |
| 输入/前置 | 使用 engine/editor fixture + reload-state guard；为 `FBlueprintActionDatabase::RefreshAll()`、`GEditor->BroadcastBlueprintCompiled()`、`GEngine->Exec()` 安装 spy/seam；world fixture 需要至少 2 个有效 `ULevel`，便于验证 `SetCurrentLevel()` 的恢复行为；测试结束恢复所有全局状态。 |
| 期望行为 | `OnPostReload(true)` 后 `RefreshAll()` 恰好调用 1 次，`BroadcastBlueprintCompiled()` 恰好调用 1 次，`GEngine->Exec()` 恰好收到一次 `MAP REBUILD ALLVISIBLE`；即使 exec seam 在执行期把 `CurrentLevel` 改到别的 level，回调结束后 `World->GetCurrentLevel()` 仍恢复为进入回调前的 level；同时 `ReloadState()` 仍按现有契约被 reset。 |
| 使用的 Helper | engine/editor fixture + reload-state guard + blueprint-action/editor/engine exec spies + multi-level world helper |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `BlueprintImpact` 现有测试新事实 | 重新逐行复核 `AngelscriptBlueprintImpactScannerTests.cpp` 后，`MakeCodeSection()` 仍把 `AbsoluteFilename` 默认写成 `J:/Dummy/Script.as`；这说明即使是当前唯一纯 unit 的 module-match 测试，也还残留本地盘符耦合，而不仅仅是前面已记录的 `ChangedScriptFile=J:/...` commandlet 路径假设。 |
| 本轮 `ClassReloadHelper` 新增 0-hit 事实 | 在目标测试树与当前 gap 文档里重新检索后，`ReplacePinType`、`RefreshAll(`、`BroadcastBlueprintCompiled`、`MAP REBUILD ALLVISIBLE`、`SetCurrentLevel(` 仍然都是 `0` 直接命中；说明 `ClassReloadHelper` 虽然已有不少补测建议，但 `PerformReinstance` 的引用改写本体和 `OnPostReload` 的正向 editor 副作用依旧没有被规格化。 |
| 31 文件口径复核 | 本轮继续按 `Plugins/Angelscript/Source/AngelscriptEditor/` 下 `31` 个 `.cpp/.h` 文件复核，口径仍是 production 源码 `29` 个、模块内测试实现 `2` 个；现有 3 个目标测试文件 direct-hit 的 production 文件仍为 `7` 个，完全无 direct test 的 production 文件仍为 `22` 个。 |
| 本轮增量结论 | 现有测试质量本轮新增 1 条 `BlueprintImpact` helper 级反模式；无测源码方面，本轮把用户点名热点继续下钻到 `ClassReloadHelper` 里此前未记录的两条高风险函数级空白：struct 引用原地改写，以及 full-reload 后的 editor 正向副作用链。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-39 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-10 00:38:37 +08:00)

### 一、现有测试问题

本轮继续按规则先复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp` 全文，并与已有 gap 文档逐项去重；没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

本轮新增条目已登记为 `NewTest-95`、`NewTest-96`，分别对应：

- `ScriptEditorMenuExtension.cpp` 中 `ToolbarLabel` / `EditorIcon` / `EditorButtonStyle` / `ActionType` 展示层元数据的 0-hit 契约。
- `AngelscriptEditorModule.cpp::ShowCreateBlueprintPopup` 中 dialog `DefaultPath` / `DefaultAssetName` 的默认推导与显式 delegate 覆盖路径。

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `EditorMenuExtensions` 新增 0-hit 事实 | 对 `Plugins/Angelscript/Source/AngelscriptTest/`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 与当前 gap 文档重新全文检索后，`ToolbarLabel`、`EditorButtonStyle`、`GraphEditor.Event_16x`、`StyleNameOverride`、`AddToolMenuButton`、`AddToolMenuEntry`、`AddMenuEntry` 仍然都是 `0` 直接命中；说明 `ScriptEditorMenuExtension` 的展示层 metadata 在本轮之前还没有任何 direct spec。 |
| 本轮 `EditorModule` 新增 0-hit 事实 | 同样重新检索后，`DefaultPath`、`DefaultAssetName`、`RelativeSourceFilePath`、`AssetRegistry.HasAssets(` 在目标测试树中仍然没有任何 `ShowCreateBlueprintPopup` 相关 direct spec；现有建议只锁住“dialog 返回什么就创建什么”，没有锁住“dialog 默认应该展示什么”。 |
| 用户点名热点增量 | 本轮新增 2 条规格都落在用户明确点名的 0-hit 热点里：`Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp/.h` 与 `Private/AngelscriptEditorModule.cpp`。这两条都不是文件级空白复述，而是继续下钻到 workflow / UI metadata 的函数级契约。 |
| 31 文件口径复核 | 本轮再次实扫 `Plugins/Angelscript/Source/AngelscriptEditor/` 后，production 源码口径仍为 `29` 个文件；现有 3 个目标测试文件 direct-hit 的 production 文件仍为 `7` 个，完全无 direct test 的 production 文件仍为 `22` 个。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:50:01 +08:00)

### 一、现有测试问题

本轮继续按规则全文复核 `AngelscriptSourceNavigationTests.cpp`、`AngelscriptBlueprintImpactScannerTests.cpp`、`AngelscriptDirectoryWatcherTests.cpp`，并与已有 `EditorAndTools_TestGaps.md` 逐项去重；没有确认出新的、不重复的现有测试质量问题。

### 二、需要新增的测试

#### NewTest-97：覆盖 `ShowCreateBlueprintPopup` 的非法 object path 早退，锁住 `NAME_None` 错误路径不会污染 package/save 流程

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `FAngelscriptEditorModule::ShowCreateBlueprintPopup` |
| 现有测试覆盖 | 现有 `EditorModule` 建议已覆盖 dialog 返回合法 object path 时的 blueprint/data-asset 创建，以及空返回值负例和默认 `DefaultPath/DefaultAssetName` 推导；但 `AngelscriptEditorModule.cpp:486-494` 对 `AssetName == NAME_None` 的错误对话框早退仍没有任何独立规格 |
| 风险评估 | 如果 dialog 返回了非法 object path，而函数没有在 `NAME_None` 处及时拦截，就会继续创建 package、触发 `PromptForCheckoutAndSave`，甚至把空/非法资产送进 editor 打开流程；这类回归会直接污染用户工作区并制造误导性保存弹窗 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.ShowCreateBlueprintPopupRejectsInvalidDialogObjectPathBeforePackageCreation` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleAssetCreationTests.cpp`；若该文件不存在则从 `AngelscriptEditorModuleLifecycleTests.cpp` 拆出 asset-creation 子文件，保持单文件 300-500 行 |
| 场景描述 | 准备一个最小 script class，拦截 `CreateModalSaveAssetDialog` 让其返回一个会使 `FPackageName::GetLongPackageAssetName(UserPackageName)` 为空的非法 object path；随后调用 `ShowCreateBlueprintPopup()` |
| 输入/前置 | 使用 editor fixture；为 `FContentBrowserModule::CreateModalSaveAssetDialog`、`FMessageDialog::Open`、`FEditorFileUtils::PromptForCheckoutAndSave`、`UAssetEditorSubsystem::OpenEditorForAsset` 安装 spy/seam；记录调用前 transient/package 基线，避免真实落盘 |
| 期望行为 | `FMessageDialog::Open()` 恰好调用 1 次并报告 invalid-name 错误；`CreatePackage()` 不应留下新的持久 package；`PromptForCheckoutAndSave()` 调用次数保持 `0`；`OpenEditorForAsset()` 调用次数保持 `0`；`AssetRegistryModule::AssetCreated()` 也不应被触发 |
| 使用的 Helper | editor fixture + content-browser dialog seam + message/save/open-editor spies + package baseline helper |
| 优先级 | P1 |

#### NewTest-98：覆盖 `ShowCreateBlueprintPopup` 的工厂失败路径，锁住空资产不会继续 save/open

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `FAngelscriptEditorModule::ShowCreateBlueprintPopup`, `FKismetEditorUtilities::CreateBlueprint` |
| 现有测试覆盖 | 现有 `EditorModule` 建议都默认 blueprint/data-asset 工厂成功，没有任何规格覆盖 `AngelscriptEditorModule.cpp:500-537` 中 `Asset == nullptr` 的失败分支；当前实现即使 `CreateBlueprint()` 返回空，也仍会 `MarkPackageDirty()`、`PromptForCheckoutAndSave()` 并把空资产传给 `OpenEditorForAsset()` |
| 风险评估 | 一旦 blueprint 工厂因为 class 配置、compiler 类型映射或 editor 状态回归而失败，用户会看到“保存新资产”对话框和空 editor 打开链，而不是一个被明确拦住的失败；这类错误路径当前完全没有测试网 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.ShowCreateBlueprintPopupDoesNotPromptSaveOrOpenEditorWhenBlueprintFactoryFails` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleAssetCreationTests.cpp`；若文件接近 500 行则拆成 `AngelscriptEditorModuleAssetFailureTests.cpp` |
| 场景描述 | 准备一个普通 script class，拦截 dialog 返回合法 object path，但通过 `FKismetEditorUtilities::CreateBlueprint` test seam 或可控 factory shim 让 blueprint 创建返回 `nullptr`；随后调用 `ShowCreateBlueprintPopup()` |
| 输入/前置 | 使用 editor fixture；为 `CreateModalSaveAssetDialog`、`FKismetEditorUtilities::CreateBlueprint`、`AssetRegistryModule::AssetCreated`、`FEditorFileUtils::PromptForCheckoutAndSave`、`UAssetEditorSubsystem::OpenEditorForAsset` 安装 spy/seam；记录 package dirty 状态与对象数量基线，测试结束清理临时 package |
| 期望行为 | blueprint 工厂失败后 `AssetRegistryModule::AssetCreated()` 调用次数保持 `0`；`PromptForCheckoutAndSave()` 不应被触发；`OpenEditorForAsset()` 不应收到空指针调用；新 package 不应被错误标记成需要保存，或至少在测试断言里明确暴露这一点而不是静默绿灯 |
| 使用的 Helper | editor fixture + dialog seam + blueprint-factory seam + asset-registry/save/open-editor spies + package cleanup guard |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `EditorModule` 新增 0-hit 事实 | 在目标测试目录与现有 gap 文档内重新检索后，`AssetName == NAME_None`、`PromptForCheckoutAndSave(Packages, Params)`、`OpenEditorForAsset(Asset)` 这条 `ShowCreateBlueprintPopup` 错误链仍没有任何 direct spec；现有建议都停留在 happy path、空返回值和默认 dialog seed。 |
| 本轮新增范围定位 | 两条新增建议都落在用户点名的 `EditorModule` 0-hit 热区内，并且都是函数内部错误路径，而不是重复铺“该文件无测试”的文件级结论。 |
| 29 文件 direct-hit 结论 | 本轮没有新增现有测试命中面；production direct-hit 仍集中在 `SourceCodeNavigation`、`DirectoryWatcherInternal`、`BlueprintImpactScanner + Commandlet`，用户点名的 `EditorModule / ClassReloadHelper / ContentBrowserDataSource / EditorMenuExtensions` 仍全部没有 direct test。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | — |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |

---

## 测试审查 (2026-04-10 01:14:57 +08:00)

### 一、现有测试问题

#### Issue-40：`FolderRemoveUsesLoadedScriptEnumerator` 的关键 helper 断言失败后仍继续返回假数据

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 测试名 | `Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator` |
| 行号范围 | 180-191 |
| 问题描述 | 这个用例把“folder remove 时传给 enumerator 的路径必须带尾随分隔符”写成了 lambda 内部的 `TestEqual(...)`，但无论该断言是否失败，lambda 都会继续返回两条预制 `FFilenamePair`。这样一来，只要 stub 继续喂出 `RemovedA.as` / `RemovedB.as`，后面的删除队列断言仍会命中，真正的 helper 契约错误会被夹在一串二次断言噪声里。 |
| 影响 | 当 `QueueScriptFileChanges()` 回归为传错 folder path 时，测试虽然会报红，但不会在第一处失败就停住；它会继续把“错误路径”和“队列仍有两条删除项”混在一起，降低定位效率，也让这条用例对关键前置条件的保护弱于应有水平。 |
| 修复建议 | 把 enumerator 改成显式 guard：先记录 `AbsoluteFolderPath`，若 `!TestEqual(...)` 则立刻返回空数组，避免继续注入伪造删除项；或者在 lambda 外先用 `CallCount/LastFolderPath` spy 收集输入，再在 `QueueScriptFileChanges()` 之后先断言路径和调用次数，确认通过后再比较队列内容。 |

### 二、需要新增的测试

#### NewTest-99：覆盖 `ForceEditorWindowToFront` 的 active-window 与 level-editor fallback 分支

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 关联函数 | `ForceEditorWindowToFront` |
| 现有测试覆盖 | 现有 `EditorModule` 建议已覆盖 `OnEngineInitDone`、`OnScriptFileChanges`、`ShowAssetListPopup`、`ShowCreateBlueprintPopup`、literal-asset 回调和生命周期；但 `AngelscriptEditorModule.cpp:96-109` 这个被 popup 入口共用的窗口前置 helper 仍没有任何独立规格 |
| 风险评估 | 如果 `GetActiveTopLevelWindow()` 返回空时 fallback 到 `LevelEditor` tab 的路径回归，`ShowAssetListPopup()` / `ShowCreateBlueprintPopup()` 仍可能执行，但弹窗会出现在后台或直接命中 `check(ParentWindow.IsValid())`；这种 editor UX 回归当前没有任何测试网 |
| 建议测试名 | `Angelscript.TestModule.Editor.Module.ForceEditorWindowToFrontPrefersActiveWindowAndFallsBackToLevelEditorTab` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptEditorModuleWindowTests.cpp` |
| 场景描述 | 通过 test seam/spy 分两次驱动 `ForceEditorWindowToFront()`：第一次让 `FSlateApplication::Get().GetActiveTopLevelWindow()` 返回一个可观察的活动窗口；第二次让活动窗口为空，并让 `FLevelEditorModule::GetLevelEditorTab()->GetParentWindow()` 提供 fallback 窗口。 |
| 输入/前置 | 使用 editor fixture；为 `FSlateApplication` 当前活动窗口查询、`FLevelEditorModule::GetLevelEditorTab()` 与 `SWindow::HACK_ForceToFront()` 安装只读 seam/spy，避免真实 UI 抖动；必要时通过 test-only wrapper 暴露静态 helper。 |
| 期望行为 | active-window 场景下只对当前活动窗口调用一次 `HACK_ForceToFront()`，不会查询 fallback tab；fallback 场景下活动窗口为空时会转而对 level-editor parent window 调用一次 `HACK_ForceToFront()`；两条路径都不能静默跳过或调用错误窗口。 |
| 使用的 Helper | editor fixture + Slate active-window seam + level-editor tab seam + window front spy |
| 优先级 | P2 |

#### NewTest-100：覆盖 `OnPostReload(false)` 的 component-registry gate，锁住 soft reload 不会误走 full-reload 副作用

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h` |
| 关联函数 | `FClassReloadHelper::Init` 中的 `FAngelscriptClassGenerator::OnPostReload` lambda |
| 现有测试覆盖 | 现有 `ClassReloadHelper` 建议已覆盖 reload-state 聚合、`OnClassReload` 即时副作用、`PerformReinstance` 的重写/刷新链，以及 `OnPostReload(true)` 的 `RefreshAll` / `BroadcastBlueprintCompiled` / volume rebuild 正路径；但 `ClassReloadHelper.h:156-157` 里 soft reload 下的 `FComponentTypeRegistry::Get().Invalidate()` gate 仍没有任何独立规格 |
| 风险评估 | 如果 initial compile 尚未完成时没有触发 component registry invalidation，editor 里的 component palette 可能继续保留旧 class 集；反过来若 soft reload 误触发 `BroadcastBlueprintCompiled()` 或 map rebuild，则会把 full-reload 副作用错误带入普通热重载流程 |
| 建议测试名 | `Angelscript.TestModule.Editor.ClassReloadHelper.OnPostReloadSoftReloadInvalidatesComponentRegistryWithoutFullReloadSideEffects` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptClassReloadHelperPostReloadTests.cpp`；若文件接近 500 行则拆成 `AngelscriptClassReloadHelperSoftReloadTests.cpp` |
| 场景描述 | 在 editor fixture 下先把 `ReloadState()` 预填为最小非空状态，然后通过 engine-state seam 把 `IsInitialCompileFinished()` 固定为 `false`，广播一次 `OnPostReload(false)`；随后切回 `true` 再广播一次作为对照。 |
| 输入/前置 | 使用 engine/editor fixture + reload-state guard；为 `FComponentTypeRegistry::Invalidate()`、`GEditor->BroadcastBlueprintCompiled()`、`GEngine->Exec()` 与 `FBlueprintActionDatabase::RefreshAll()` 安装 spy/seam；需要 test accessor 临时切换 Angelscript engine 的 initial-compile 完成状态，并在结束时恢复。 |
| 期望行为 | `IsInitialCompileFinished()==false` 的 soft reload 下，`FComponentTypeRegistry::Invalidate()` 恰好调用 1 次，`BroadcastBlueprintCompiled()` / `GEngine->Exec("MAP REBUILD ALLVISIBLE")` 调用次数都保持 `0`；切回 `IsInitialCompileFinished()==true` 后再次触发 soft reload，不会再触发 component invalidation；两次调用结束后 `ReloadState()` 都按契约被 reset。 |
| 使用的 Helper | engine/editor fixture + reload-state guard + engine-state seam + component-registry/editor/engine spies |
| 优先级 | P1 |

### 三、覆盖快照

| 项目 | 内容 |
|------|------|
| 本轮 `EditorModule` 新增 0-hit 事实 | 对目标测试目录重新全文检索后，`ForceEditorWindowToFront`、`HACK_ForceToFront`、`GetActiveTopLevelWindow`、`GetLevelEditorTab` 仍然都是 `0` 直接命中；说明 popup 入口共用的窗口前置 helper 到本轮之前还没有任何 direct spec。 |
| 本轮 `ClassReloadHelper` 新增 0-hit 事实 | 目标测试目录里 `FComponentTypeRegistry::Get().Invalidate()` 与 `OnPostReload(false)` 的 soft-reload gate 仍是 `0` 直接命中；现有建议已经覆盖 full reload 正路径，但还没有一条去锁住“initial compile 未完成时只做 component registry invalidation”的分支。 |
| 本轮 31 文件复核 | 重新实扫 `Plugins/Angelscript/Source/AngelscriptEditor/` 后，口径仍是总文件 `31`、production 源码 `29`、模块内测试实现 `2`；现有 3 个测试文件 direct-hit 的 production 文件仍为 `7` 个，完全无 direct test 的 production 文件仍为 `22` 个。 |
| 本轮增量结论 | 现有测试质量本轮新增 1 条 `DirectoryWatcher` 断言保护问题；新增测试建议继续落在用户点名的 `EditorModule` / `ClassReloadHelper` 热区，并且都不是重复的“文件无测试”结论，而是新下钻出的函数级契约空白。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-40 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | NoTestForSource: 1 |
