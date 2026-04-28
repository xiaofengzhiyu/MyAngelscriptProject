# EditorArch 架构评审

---

## 架构分析 (2026-04-08 13:59)

### Arch-01：编辑器特性缺少统一拥有者，`EditorSubsystem` 还不是正式扩展面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编辑器模块的初始化编排、退出清理、用户扩展入口 |
| 当前设计 | `FAngelscriptEditorModule` 在 `StartupModule()` 中直接注册大量 editor hook，但 `ShutdownModule()` 只回收极少数句柄；`UScriptEditorSubsystem` 目前只提供 `BP_Initialize/BP_Tick` 生命周期壳层，没有参与菜单、数据源、文件监听的正式注册流程。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:356` — `StartupModule()` 直接注册 `GameplayTags` delegate、`OnPostEngineInit`、`UScriptEditorMenuExtension::InitializeExtensions()`、`DirectoryWatcher`、`Settings`、runtime bridge lambda、`OnObjectPreSave`、`UToolMenus` callback。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:375` — `DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(...)` 的 `WatchHandle` 是局部变量，模块自身没有保存。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:676` — `ShutdownModule()` 只移除了 `OnObjectPreSave`、state dump extension 和 `UToolMenus` owner。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:136` — 通过 `FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation)` 注册源码导航 handler，没有对应的卸载路径。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7` — `UScriptEditorSubsystem` 只暴露 `BP_ShouldCreateSubsystem`、`BP_Initialize`、`BP_Deinitialize`、`BP_Tick`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` — 仅把 `GetEditorSubsystem()` 暴露给脚本，没有额外的 editor feature 注册 API。 |
| 优点 | 新增一个 editor 能力时接线成本低，功能可以快速在模块入口落地；脚本侧已经有 `UScriptEditorMenuExtension` 和 `UScriptEditorSubsystem` 两个可用基类。 |
| 不足 | 生命周期拥有者不清晰，多个 delegate/watcher/navigation handler 的注册与回收不对称。推断：在模块动态重载、编辑器长时运行或未来增加更多 editor feature 时，容易出现重复注册、悬挂回调和难以排查的顺序问题。更关键的是，用户虽然能创建 `UScriptEditorSubsystem` 子类，但不能通过它正式注册 `ToolMenus`、`ContentBrowser` 或文件监听扩展。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块只负责装配；`OnPostEngineInit` 之后再初始化 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar`，并在 `ShutdownModule()` 中移除 `OnPostEngineInit` 与 settings/package-save delegate。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:72` | 先把 editor 能力拆成独立对象，再由模块统一装配和回收，生命周期边界更清楚。 |
| puerts | 把目录监听包装成 `UPEDirectoryWatcher`，对象内部持有 `DelegateHandle`，`UnWatch()` 和析构函数负责注销；`DeclarationGenerator` 的 `StartupModule()/ShutdownModule()` 对 `ToolMenus` callback 与 commands 做对称处理。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:74`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1690` | 让“谁注册，谁注销”落到具体对象，而不是散落在模块入口和全局函数里。 |
| UnrealCSharp | 模块显式持有 `PlayToolBar`、`BlueprintToolBar`、`DynamicDataSource`、`OnPostEngineInitDelegateHandle` 和 `FEditorListener`；`ShutdownModule()` 与 `FEditorListener::~FEditorListener()` 会成组移除 `ToolMenus` owner、property customization、ticker、directory watcher 和 editor delegates。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:127`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69` | 把 editor integration 组织成“模块成员 + listener/data source 对象”的显式所有权树，便于增量扩展，也便于验证卸载路径。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptEditor` 改成“模块编排 + feature 对象拥有资源”的结构，同时把 `UScriptEditorSubsystem` 升格为脚本侧正式扩展入口。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorFeature`（或等价基类），约定 `Startup()` / `Shutdown()` / `GetFeatureName()`，并在 `FAngelscriptEditorModule` 中持有 `TArray<TUniquePtr<IAngelscriptEditorFeature>>`。<br>2. 先把最容易出问题的三类资源迁出模块入口：`DirectoryWatcher`、`SourceCodeNavigation`、`ContentBrowserDataSource`；每个 feature 自己保存 `FDelegateHandle`、强引用对象和反注册逻辑。<br>3. 在 `UScriptEditorSubsystem` 基础上新增脚本可调用的注册 API，例如 `RegisterToolMenuExtension`、`RegisterContentBrowserAction`、`RegisterDirectoryWatch`；旧的 `UScriptEditorMenuExtension` 先适配到这套注册器上，避免一次性废弃。<br>4. 将 `FAngelscriptRuntimeModule::GetDebugListAssets()`、`GetEditorCreateBlueprint()` 等 bridge delegate 改为模块成员句柄或 feature 成员句柄，`ShutdownModule()` 逆序回收。<br>5. 为模块重载/退出增加最小验证：重复启停后不应出现重复 menu entry、重复文件监听和重复 source navigation handler。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Features/*.h/*.cpp`。 |
| 预估工作量 | L |
| 架构风险 | 需要重新梳理 `StartupModule()` 的初始化顺序，尤其是 `OnPostEngineInit`、`IsInitialCompileFinished()` 和 menu extension 重载顺序；如果拆分不当，可能引入 editor startup 时序问题。 |
| 兼容性 | 可增量实施。第一阶段只迁移资源拥有者，不改变现有 `UScriptEditorMenuExtension` 和 runtime bridge 的外部用法；第二阶段再把脚本侧扩展入口切到 `UScriptEditorSubsystem` 注册器上，对现有脚本保持兼容。 |
| 验证方式 | 1. 启动编辑器后确认 `Tools` 菜单、脚本菜单扩展、source navigation、文件监听都正常工作。<br>2. 触发一次脚本全量 reload，确认 menu extension 只注册一次。<br>3. 关闭编辑器模块或执行模块重载后，确认没有重复日志、重复 callback 和残留 menu section。 |

### Arch-02：菜单扩展已经接上 `ToolMenus`，但还没有形成正式的 command/style 层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 与 UE 编辑器命令系统、快捷键、样式系统的集成深度 |
| 当前设计 | 固定菜单项和脚本菜单扩展都直接构造 `FUIAction` / `FToolUIAction`，执行体以 inline lambda 或 `ProcessEvent` 反射调用为主；当前模块没有自己的 `TCommands` / `UI_COMMAND` / plugin style namespace。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` — `RegisterToolsMenuEntries()` 直接用 `FToolUIActionChoice(FExecuteAction::CreateLambda(...))` 注册 `ASOpenCode`、`ASGenerateBindings`、`Function Tests`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:184` — `AddMenuEntry()` 直接把 `UFunction` 元数据转成 `FUIAction`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:204` — `AddToolMenuEntry()` 直接把 `UFunction` 元数据转成 `FToolUIAction`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:254` — `CreateToolUIAction()` 通过 `ProcessEvent` 和元数据字符串动态解析 `CanExecute/IsVisible/IsChecked`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:352` — `CreateUIAction()` 同样以反射和 lambda 组装普通菜单动作。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:199` — 图标统一落到 `FAppStyle::GetAppStyleSetName()`，没有插件专属 style set。 |
| 优点 | 脚本函数只要打上元数据即可快速长出 editor action，开发效率高；不需要为每个 action 手写 `TCommands` 模板。 |
| 不足 | 命令没有稳定 identity，导致快捷键、命令重映射、统一日志/埋点、跨入口复用和多处共享启用状态都比较弱。脚本 action 当前更像“菜单时临时求值”，而不是 UE 编辑器里可注册、可复用、可绑定快捷键的正式 command。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 定义 `FUnLuaEditorCommands : TCommands<FUnLuaEditorCommands>`，用 `UI_COMMAND(...)` 注册命令，其中 `HotReload` 直接绑定 `Alt+L`；`MainMenuToolbar` 再用 `FUICommandList` 映射动作并生成菜单。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.h:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29` | 先注册 command，再决定放到哪个 toolbar/menu，快捷键和菜单展示自然统一。 |
| puerts | `FGenDTSCommands` 通过 `TCommands` + `FGenDTSStyle` 提供命令与图标，`DeclarationGenerator` 用 `PluginCommands->MapAction(...)` 把同一命令同时接到主菜单和 toolbar。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/GenDTSCommands.h:15`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp:13`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` | 命令、样式、入口三层分离，入口切换不会影响 action 身份。 |
| UnrealCSharp | 启动时初始化 `FUnrealCSharpEditorStyle` 和 `FUnrealCSharpEditorCommands`，`PlayToolBar` / `BlueprintToolBar` 分别通过 `FUICommandList` 复用同一批命令。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:43`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditorCommands.h:9`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditorCommands.cpp:7`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:11` | 先建立统一 command/style 层，再把 action 分发到不同编辑器表面，后续扩展和维护成本更低。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `AngelscriptEditor` 增加正式的 `command + style` 层，并把脚本函数扩展从“即时 lambda”升级为“可注册 command”。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorStyle` 与 `FAngelscriptEditorCommands`，先把固定功能项 `ASOpenCode`、`ASGenerateBindings`、`FunctionTests` 收敛成 `UI_COMMAND(...)`。<br>2. 在 `FAngelscriptEditorModule::StartupModule()` 中初始化 style/commands，并用模块级 `FUICommandList` 取代当前 inline lambda。<br>3. 为 `UScriptEditorMenuExtension` 增加 command descriptor 生成层：以 `ScriptClass + FunctionName` 作为稳定 key，生成或缓存 `FUICommandInfo`，再由 `ToolMenus` / `FExtender` 只负责展示。<br>4. 把现有元数据 `EditorIcon`、`ToolbarLabel`、`ActionCanExecute`、`ActionIsVisible`、`ActionIsChecked` 保留为 command 元数据来源；新增可选 `InputChord` / `CommandContext` 元数据，允许脚本 action 获得快捷键。<br>5. 第一阶段没有 style 资源时，默认回落到 `FAppStyle`；后续再逐步引入插件专属 icon。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorCommands.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStyle.h/.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 动态脚本 command 的 key 设计如果不稳定，会在脚本 reload 后产生重复 command 或失效快捷键；需要同时考虑 `UASClass` reload 和 `UToolMenus` rebuild。 |
| 兼容性 | 可向后兼容。旧元数据和现有脚本函数签名都可保留；没有注册成 command 的 action 仍可退回当前反射执行路径。 |
| 验证方式 | 1. 验证固定命令在 `Tools` 菜单和其它入口共享同一启用状态。<br>2. 给一个脚本 action 配置快捷键后，确认菜单点击和快捷键触发结果一致。<br>3. 执行一次脚本 reload，确认 command 不重复注册、图标和可见性状态正常。 |

### Arch-03：`ContentBrowserDataSource` 已接入浏览器，但仍停留在“展示层”，没有闭合为原生资产工作流

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产在 `ContentBrowser` 中的展示、编辑、创建和引用闭环 |
| 当前设计 | 模块在 `OnPostEngineInit` 后创建并激活一个匿名 `UAngelscriptContentBrowserDataSource`，它能把 `FAngelscriptEngine::AssetsPackage` 里的对象包装成 `/All/Angelscript/*` 虚拟项并显示缩略图，但编辑、批量编辑、引用拼接、物理路径和虚拟路径反查基本都未实现；模块另外再用自定义 popup 补上“打开/新建 Blueprint”工作流。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` — `OnEngineInitDone()` 直接 `NewObject<UAngelscriptContentBrowserDataSource>(..., RF_MarkAsRootSet | RF_Transient)` 并 `ActivateDataSource("AngelscriptData")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:57` — 模块头里没有保存 data source 成员，只有 `StateDumpExtensionHandle`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16` — `CreateAssetItem()` 把对象映射到 `/All/Angelscript/<AssetName>`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:65` — `EnumerateItemsMatchingFilter()` 从 `FAngelscriptEngine::Get().AssetsPackage` 中枚举资产并按类过滤。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:182` — `GetItemPhysicalPath()` 直接 `return false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:187` — `CanEditItem()` / `EditItem()` / `BulkEditItems()` / `AppendItemReference()` 均未实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:249` — `Legacy_TryConvertPackagePathToVirtualPath()` / `Legacy_TryConvertAssetDataToVirtualPath()` 未实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` — `ShowCreateBlueprintPopup()` 仍通过 `CreateModalSaveAssetDialog` + `FKismetEditorUtilities::CreateBlueprint` 走旁路创建。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` — `ShowAssetListPopup()` 通过自定义 `AssetPicker` popup 弥补浏览/打开体验。 |
| 优点 | 用很少的代码把脚本资产接进了 `ContentBrowserData`，并且保留了缩略图和 `AssetRegistry` 友好的显示路径，用户能在浏览器里看到 `Angelscript` 资产。 |
| 不足 | 浏览器数据源和实际工作流被拆成两套路径：`DataSource` 负责“看见”，popup 负责“打开/创建”。这会让后续的右键菜单、引用复制、批量操作、路径跳转、shutdown 清理都变得零散。推断：如果未来要支持更多脚本资产类型或用户自定义 asset action，这种“双轨结构”会显著放大维护成本。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 模块显式持有 `TStrongObjectPtr<UDynamicDataSource>`，启动时初始化、首帧激活、关闭时 `Reset()`；`UDynamicDataSource` 自己注册 `ContentBrowser.AddNewContextMenu`，并实现 `EditItem`、`BulkEditItems`、`DeleteItem`、`AppendItemReference`、`Legacy_TryConvert*Path*`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:104`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:164`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:518`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704` | 如果决定走 `ContentBrowserDataSource` 路线，就把“显示、创建、编辑、删除、引用、路径转换、收尾清理”全部放回同一个数据源拥有者里。 |
| puerts | 没有把脚本工作流拆成“半个虚拟数据源 + 半个旁路 popup”，而是把编辑器集成重点放在 `UTypeScriptBlueprint` compiler 和 `DeclarationGenerator` tool 上，保持 asset workflow 依附 Unreal 原生 Blueprint/ToolMenus 管线。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573` | 另一种可借鉴点是：如果暂时不准备补完整个 `ContentBrowserDataSource` 合同，就不要让用户同时面对“浏览器入口”和“旁路入口”两套不一致的行为。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 要么补完整个 `AngelscriptData` 合同，要么收缩回更纯粹的原生 asset workflow；就当前代码基础，优先建议“补完整个合同”，因为展示路径已经接好了。 |
| 具体步骤 | 1. 在 `FAngelscriptEditorModule` 或新的 `ContentBrowser` feature 中保存 `TStrongObjectPtr<UAngelscriptContentBrowserDataSource>`，并在 `ShutdownModule()` 中显式 `DeactivateDataSource()` / `Reset()`，去掉匿名 `RootSet` 对象。<br>2. 补齐最关键的反向操作：`EnumerateItemsAtPath()`、`EditItem()`、`BulkEditItems()`、`AppendItemReference()`、`Legacy_TryConvertPackagePathToVirtualPath()`、`Legacy_TryConvertAssetDataToVirtualPath()`；其中 `EditItem()` 可优先打开 `UAssetEditorSubsystem` 或跳到脚本源码。<br>3. 把 `ShowCreateBlueprintPopup()` 的入口迁到 `ContentBrowser.AddNewContextMenu` 与 `ContentBrowser_AssetViewContextMenu`，让“新建/打开”回到浏览器原生表面；现有 popup 暂时保留为 fallback。<br>4. 为 `/All/Angelscript` 设计稳定的 folder/file 层级，而不是只有扁平化的 `<AssetName>`；这样后续才能承载更多脚本资产类型和用户自定义分类。<br>5. 增加 editor 测试：验证虚拟项可以打开、复制引用、从 `AssetData` 回到虚拟路径，并在脚本 reload 后刷新视图而不是残留旧项。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`；必要时新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Features/AngelscriptContentBrowserFeature.*`。 |
| 预估工作量 | L |
| 架构风险 | 一旦补 `Legacy_TryConvert*Path*` 和 `AppendItemReference`，虚拟路径命名就会变成对外契约；路径设计如果后续再改，需要兼容旧路径或提供 alias。 |
| 兼容性 | 可增量实施。第一阶段先增加 data source 所有权和编辑/引用闭环，不移除现有 popup；第二阶段再逐步把创建入口迁回 `ContentBrowser` 原生菜单。对现有脚本和资产路径可保持兼容。 |
| 验证方式 | 1. 在 `ContentBrowser` 中浏览 `/All/Angelscript`，确认刷新后条目稳定。<br>2. 右键或双击脚本资产能直接打开正确编辑器/源码。<br>3. 复制引用、从 `AssetData` 回到虚拟路径、批量打开至少一个用例可用。<br>4. 关闭模块或编辑器后重新启动，确认 data source 不重复激活。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-01 | 生命周期拥有者、delegate 清理、`EditorSubsystem` 扩展入口 | 结构性重构 | 高 |
| P1 | Arch-03 | `ContentBrowserDataSource` 工作流闭环与所有权 | 扩展点补完 | 高 |
| P2 | Arch-02 | command/style 正式层 | 框架集成深化 | 中 |

---

## 架构分析 (2026-04-08 14:43)

### Arch-04：脚本侧可扩展面仍是 `menu-only` contract，`EditorSubsystem` 还没有成长为 editor capability registrar

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户是否能通过 `EditorSubsystem` 正式扩展 editor 功能，而不只是增加菜单动作 |
| 当前设计 | `UScriptEditorSubsystem` 目前只提供生命周期与 tick 壳层；真正的脚本扩展模型仍是 `UScriptEditorMenuExtension` + `EScriptEditorMenuExtensionLocation`，能力边界几乎全部围绕 `ToolMenu`、`LevelViewport`、`ContentBrowser` 的 menu/extender 挂点。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:26` — `ShouldCreateSubsystem()` 只转发到 `BP_ShouldCreateSubsystem`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:33` — `Initialize()` 只触发 `BP_Initialize()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:40` — `Deinitialize()` 只触发 `BP_Deinitialize()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:57` — `BP_Tick(float DeltaTime)` 是 subsystem 唯一持续性钩子。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` — 脚本侧只得到 `GetEditorSubsystem()` 查询 helper。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:9` — `EScriptEditorMenuExtensionLocation` 枚举的全部入口都是 menu/context menu 位置。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:845` — `RegisterExtensions()` 遍历 `UASClass` 并把脚本类注册到 `LevelEditor` / `ContentBrowser` extender。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:257` — action 执行通过临时 `UScriptEditorMenuExtension` 对象和 `ProcessEvent` 回调完成。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:10` — 更深的 `AssetTools` 能力目前也只是静态 utility wrapper。 |
| 优点 | 脚本作者接入成本低；新增 editor action 时，不需要先设计复杂的 handle 或 registrar。 |
| 不足 | 推断：基于本轮实际读取到的 `AngelscriptEditor/` 源码，用户当前能稳定扩展的是“菜单动作”，而不是“editor capability”。也就是说，`EditorSubsystem` 还不能正式承载 `DirectoryWatcher`、`ContentBrowser` provider、`PropertyEditor` customization、toolbar command list 等需要注册/反注册句柄的能力。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块在 `StartupModule()` 中先创建 `FMainMenuToolbar`、`FBlueprintToolbar`、`FAnimationBlueprintToolbar`，`OnPostEngineInit()` 再统一初始化；命令身份由 `UI_COMMAND(...)` 固定注册。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:98`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/AnimationBlueprintToolbar.cpp:22`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19` | 把 editor 扩展拆成 capability-specific object，扩展入口先按“能力”分层，再按“菜单位置”挂接。 |
| puerts | `UPEDirectoryWatcher` 是脚本可持有的 `UObject` service，公开 `BlueprintAssignable` 的 `OnChanged`，并在 `Watch()/UnWatch()` 内管理 `DelegateHandle`；工具入口另由 `DeclarationGenerator` 自己持有 `PluginCommands` 和 `ToolMenus` 生命周期。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:18`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` | 可扩展能力以“可持有的 service object”暴露，而不是把所有高级能力压成一次性 menu callback。 |
| UnrealCSharp | 模块成员显式持有 `FEditorListener`、toolbar 对象与 `TStrongObjectPtr<UDynamicDataSource>`；`FEditorListener` 自己管理 editor delegates、`AssetRegistry` 和 `DirectoryWatcher` 订阅。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18` | 先建立 feature/service 所有权树，再考虑把哪些能力向外开放，扩展性和回收路径都会更清楚。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `UScriptEditorSubsystem` 从生命周期壳层升级为 capability registrar，同时保留现有 `UScriptEditorMenuExtension` 作为兼容层。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorRegistrationHandle` 与 capability-specific descriptor，例如 `FAngelscriptMenuExtensionDesc`、`FAngelscriptDirectoryWatchDesc`、`FAngelscriptContentBrowserActionDesc`，统一由 subsystem 返回 handle。<br>2. 在 native 层新增 `UAngelscriptEditorExtensionRegistrySubsystem`，或直接扩展现有 `UScriptEditorSubsystem`，把注册/反注册逻辑从 `UScriptEditorMenuExtension::RegisterExtensions()` 迁入 subsystem。<br>3. 让现有 `UScriptEditorMenuExtension` 在内部转调新 registry；旧脚本类保持可用，但新脚本可在 `BP_Initialize/BP_Deinitialize` 中持有并释放 handle。<br>4. 第二阶段再把 `DirectoryWatcher`、`AssetTools`、prompt factory 等 utility 迁成 subsystem service，避免脚本直接依赖全局 static wrapper。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/ExtensionRegistry/*` 与 `Private/ExtensionRegistry/*`。 |
| 预估工作量 | M |
| 架构风险 | registry handle 设计如果不稳定，脚本 reload 后容易出现重复注册或悬挂句柄；另外要防止为简单 action 引入过重样板代码。 |
| 兼容性 | 可增量实施。第一阶段只把现有 menu extension 包到新 registry 后面，对现有脚本零破坏；新 subsystem API 作为可选高级路径逐步推广。 |
| 验证方式 | 1. 新建一个 `UScriptEditorSubsystem` 子类，在 `BP_Initialize` 里同时注册 menu 与 directory watch。<br>2. 触发一次 full reload，确认注册数量不增长。<br>3. 关闭 editor 或模块重载后，确认没有残留 menu item、watch callback 和 warning log。 |

### Arch-05：模块已依赖深层 editor 框架，但实际接法仍偏向 transient popup，尚未沉入稳定 editor surface

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 与 UE 编辑器框架的集成深度，是否真正进入 `BlueprintEditor`、`PropertyEditor`、compiler pipeline 等稳定表面 |
| 当前设计 | `AngelscriptEditor` 已经依赖 `AssetTools`、`PropertyEditor`、`Kismet`、`ContentBrowserData`、`ToolMenus` 等 editor 模块，但当前深层能力主要被用作 modal/popup helper 或一次性 utility；主线体验仍是全局菜单、临时资产选择器和临时 prompt window。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:20` — 模块显式依赖 `BlueprintGraph`、`Kismet`、`AssetTools`、`PropertyEditor`、`ContentBrowserData`、`ToolMenus`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:480` — 资产创建首先走 `CreateModalSaveAssetDialog()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:510` — 之后再用 `IKismetCompilerInterface::GetBlueprintTypesForClass()` 与 `FKismetEditorUtilities::CreateBlueprint()` 在 modal 流程里完成创建。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:603` — 打开资产的主路径是 `CreateAssetPicker()` 包一层临时 widget。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:648` — 再由 `FMenuBuilder`/popup 呈现，而不是挂回某个专用 asset/editor surface。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:53` — `PropertyEditor` 目前主要用于生成 `IStructureDetailsView`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:166` — prompt 最终也只是一个 `SWindow` modal。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:333` — reload 后对 `PropertyEditor` 的深度交互也只是 `NotifyCustomizationModuleChanged()` 刷新 UI。 |
| 优点 | 实现成本低，当前工作流改动小；不需要维护持久化 panel 或 asset editor 状态。 |
| 不足 | 推断：基于本轮 inspected sources，插件还没有把脚本工作流沉入 `BlueprintEditor` toolbar、`PropertyEditor` customization、custom compiler context 或 dockable panel 这些稳定 editor surface，因此新需求很容易继续长出新的 popup/helper，而不是复用已有 editor framework。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FMainMenuToolbar` 通过 `FUICommandList` + `UI_COMMAND` 进入 `LevelEditor` toolbar；`FBlueprintToolbar` 与 `FAnimationBlueprintToolbar` 直接挂到对应 asset editor 的 extender 上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/AnimationBlueprintToolbar.cpp:22`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19` | 不是只在全局菜单上“开一个口子”，而是让命令落进具体 editor surface。 |
| puerts | `FPuertsEditorModule::OnPostEngineInit()` 为 `UTypeScriptBlueprint` 注册专用 compiler；`FTypeScriptCompilerContext::SpawnNewClass()` 直接接管生成类创建；工具入口则由 `DeclarationGenerator` 以 `UI_COMMAND` + `ToolMenus` 独立维护。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:19`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` | 当语言桥接影响 `Blueprint` 资产语义时，把集成点下沉到 compiler/asset pipeline，而不是停在菜单辅助层。 |
| UnrealCSharp | 启动时对称注册 `RegisterCustomPropertyTypeLayout()`、play/blueprint toolbar 和 `UDynamicDataSource`；`UDynamicDataSource` 把 “New Dynamic Class” 接入 `ContentBrowser.AddNewContextMenu`，`DynamicNewClassUtils` 打开专用 `SDynamicNewClassDialog`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:104`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:138`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:17`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:71`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:18` | 即便仍然需要自定义 window，也把入口和上下文放在原生 editor surface 上，而不是完全依赖通用 popup helper。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 popup 作为 fallback，但后续新增功能优先下沉到具体 editor surface，而不是继续堆全局 helper。 |
| 具体步骤 | 1. 先挑一个最有收益的 surface 下沉，例如给 script-backed `Blueprint`/`DataAsset` 增加 `BlueprintEditor` 或 `LevelEditor` toolbar extender，把“打开源码 / 创建派生蓝图 / 运行 impact scan”放到上下文相关的位置。<br>2. 把 `ScriptEditorPrompts` 中高频的创建参数 struct 提炼成 `PropertyEditor` customization 或专用 `Slate` dialog，而不是每次都临时创建 `IStructureDetailsView` modal。<br>3. 为 script-backed `Blueprint` 创建增加轻量 validator/compiler hook：第一阶段只做创建后校验与错误汇总，第二阶段再评估是否需要类似 puerts 的专用 `FKismetCompilerContext`。<br>4. 等 command/style 层稳定后，再补一个 dockable results panel，例如 `BlueprintImpact` 结果面板；现有 commandlet 和 popup 可继续作为 fallback。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Toolbars/*`、`Private/Details/*`，必要时新增 `Private/Panels/*`。 |
| 预估工作量 | L |
| 架构风险 | 如果在 command/style 层尚未稳定前直接铺太多 surface，容易出现命令复用不一致；另外 compiler hook 过早做重，可能把本来可增量的改造拉成大改。 |
| 兼容性 | 可增量实施。第一阶段只新增 toolbar/customization/panel，不移除现有 popup 与 helper；旧脚本和旧工作流继续可用。 |
| 验证方式 | 1. 在相关 editor surface 中验证新 toolbar 只在有意义的上下文出现。<br>2. 通过新 surface 创建/打开一次脚本相关资产，确认仍能回退到旧 popup。<br>3. reload 后确认 property customization、toolbar 和 panel 不重复注册。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-04 | `EditorSubsystem` 是否能承载 capability 级扩展 | 扩展点重构 | 高 |
| P2 | Arch-05 | editor surface 深度集成（toolbar / customization / compiler / panel） | 框架集成深化 | 中高 |

---

## 架构分析 (2026-04-08 15:00)

### Arch-06：脚本资产视图仍是“被动枚举器”，缺少从文件变更到 `ContentBrowser` 的增量刷新总线

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本文件变化、脚本类重载与 `ContentBrowserDataSource` 是否形成稳定的 editor 同步链 |
| 当前设计 | `AngelscriptEditorModule` 在 `OnPostEngineInit` 后一次性激活 `UAngelscriptContentBrowserDataSource`，但目录变化只会被排入 `FAngelscriptEngine` 的 reload 队列；`ContentBrowserDataSource` 自身仍是“按查询枚举 `AssetsPackage` 中对象”的薄包装，没有自己的 hierarchy rebuild / item invalidation / add-new menu 接口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78` — `OnScriptFileChanges()` 只调用 `QueueScriptFileChanges(...)`，把变化压入 `FAngelscriptEngine` 的 reload 队列。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` — `OnEngineInitDone()` 仅 `NewObject`、`Initialize()` 并 `ActivateDataSource("AngelscriptData")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h:68` — 头文件声明了 `CreateFolderItem(FName InFolderPath)`，说明设计上预留过 hierarchy 入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16` — 实现文件实际只定义了 `CreateAssetItem(UObject* Asset)`，虚拟路径固定为 `/All/Angelscript/<AssetName>`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:65` — `EnumerateItemsMatchingFilter()` 每次都从 `FAngelscriptEngine::Get().AssetsPackage` 重新枚举对象。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:124` — `EnumerateItemsAtPath()` 为空实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:128` — `EnumerateItemsForObjects()` 直接 `return false`。 |
| 优点 | 结构非常轻，数据源接入成本低；只要 `AssetsPackage` 中对象存在，浏览器就能“看到”脚本资产。 |
| 不足 | 推断：当前 editor 侧没有一个显式的“脚本变化 -> hierarchy rebuild -> item invalidation”链路，因此脚本 reload、目录新增删除、脚本类批量变化对 `ContentBrowser` 的影响主要依赖外部重建时机，而不是数据源自己主动刷新。再加上 `CreateFolderItem()` 未落地、`EnumerateItemsAtPath()` 为空，当前视图更像扁平的 read-only snapshot，而不是会随脚本结构演进的 editor model。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 模块显式持有 `FEditorListener` 与 `TStrongObjectPtr<UDynamicDataSource>`；`UDynamicDataSource::Initialize()` 订阅 `OnDynamicClassUpdated` 与 `OnEndGenerator`，并把 `ContentBrowser.AddNewContextMenu` 接入数据源；`UpdateHierarchy()` 会重建虚拟树并调用 `NotifyItemDataRefreshed()` 或 `QueueItemDataUpdate(...)`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:104`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:179`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:63`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:71`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:739` | `ContentBrowserDataSource` 不只是“枚举器”，而是一个真正拥有刷新策略、层级树和创建入口的 editor feature。 |
| UnrealCSharp | `FEditorListener` 同时接入 `AssetRegistry` 与 `DirectoryWatcher`；文件变化先积累，激活窗口或资产变化时再触发生成/编译，形成明确的 editor 同步总线。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:45`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:59`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:251`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:322`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:373` | 目录变化、资产变化和生成结果由 listener 汇总，再驱动 editor feature 更新，而不是让每个表面各自猜测何时刷新。 |
| puerts | `UPEDirectoryWatcher` 被做成可持有的 `UObject` service，`Watch()/UnWatch()` 管理 `DelegateHandle`，变化通过 `BlueprintAssignable OnChanged` 暴露。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:25`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:20`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:74` | 如果还没有完整的 editor listener，总线的第一步至少可以先把目录变化提炼成可复用 service，而不是只写进 runtime 单例状态。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `UAngelscriptContentBrowserDataSource` 从“被动枚举 `AssetsPackage`”升级为“拥有 hierarchy + invalidation + create entry 的同步型 feature”，并让文件变化事件能显式流到该 feature。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 内新增 `FAngelscriptEditorAssetSyncService`（或等价 feature），统一监听 `DirectoryWatcher`、`FAngelscriptClassGenerator::OnPostReload` 与脚本资产创建/删除入口；第一阶段只负责把“需要刷新浏览器”的事件汇总出来。<br>2. 扩展 `UAngelscriptContentBrowserDataSource`，补上 `CreateFolderItem()`、`EnumerateItemsAtPath()`、`EnumerateItemsForObjects()`，并基于 `UASClass::GetRelativeSourceFilePath()` 或模块路径构建稳定的虚拟 folder tree，而不是只生成扁平 `/All/Angelscript/<AssetName>`。<br>3. 在 sync service 中调用 `SetVirtualPathTreeNeedsRebuild()` + `NotifyItemDataRefreshed()`；如果当前 UE 版本没有该接口，则退回 `QueueItemDataUpdate(...)` 的增量模式。<br>4. 把“新建脚本相关资产”的入口接到 `ContentBrowser.AddNewContextMenu`，根据选中的虚拟路径反推默认创建目录；现有 `ShowCreateBlueprintPopup()` 先保留为 fallback。<br>5. 第二阶段再补 editor-facing 测试或自动化验证，覆盖“脚本新增/删除/重载后浏览器是否自动更新”的场景。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Features/AngelscriptEditorAssetSyncService.*`。 |
| 预估工作量 | M |
| 架构风险 | 刷新时机如果选在“脚本还未完成 full reload”之前，可能导致 `ContentBrowser` 看到半成品类或瞬时空树；需要把 refresh 挂到明确的 reload 完成点，而不是目录变化回调本身。 |
| 兼容性 | 可增量实施。第一阶段只新增 hierarchy/invalidation，不改变现有 `/All/Angelscript` 根路径和 popup 工作流；脚本用户无破坏性变化。 |
| 验证方式 | 1. 新增、删除、重命名 `.as` 文件后，确认一次 reload 内 `ContentBrowser` 自动刷新，不需要重启编辑器。<br>2. 从脚本类创建一个新 Blueprint/DataAsset，确认虚拟路径树与浏览器 Add New 菜单都能定位到正确目录。<br>3. 连续执行两次 full reload，确认没有重复 item、旧 folder 残留或失效缩略图。 |

### Arch-07：`UScriptEditorMenuExtension` 把 editor surface 写死在 enum/switch 中，既不开放也会丢上下文

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor surface 的扩展模型是否对新增表面开放，以及现有表面能否把真实上下文传给脚本扩展 |
| 当前设计 | `UScriptEditorMenuExtension` 用 `EScriptEditorMenuExtensionLocation` 枚举和一对巨型 `switch` 统一处理所有 `LevelEditor` / `ContentBrowser` / `ToolMenu` 表面；脚本侧上下文只被抽象成 `SelectedObjects` 和 `SelectedAssets`，部分 surface 的原生上下文在注册时直接被丢弃。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:10` — `EScriptEditorMenuExtensionLocation` 把所有支持的 surface 编码为固定 enum。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:98` — `FExtenderSelection` 只包含 `SelectedObjects` 与 `SelectedAssets`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:671` — `UnregisterExtensions()` 用长 `switch` 手工拆解每一种 surface 的反注册。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:845` — `RegisterExtensions()` 通过 `TObjectIterator<UASClass>` 扫描所有脚本扩展类，再用另一段长 `switch` 逐个注册。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1020` — `ContentBrowser_AssetContextMenu` 的 lambda 收到 `Paths`，但直接丢弃，转而传入空的 `FExtenderSelection()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1032` — `ContentBrowser_PathViewContextMenu` 同样忽略 `Paths`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:254` — action 执行时再临时 `NewObject<UScriptEditorMenuExtension>`，说明 surface adapter 本身并没有稳定实例化的上下文对象。 |
| 优点 | 对脚本作者很直接：只要继承 `UScriptEditorMenuExtension` 并选一个既有 enum，就能快速把 action 挂到某个常见 surface。 |
| 不足 | 新增任何一个 editor surface 都必须改 `ScriptEditorMenuExtension.h/.cpp` 的 enum 和双 `switch`，不符合 open/closed。更严重的是，部分现有 surface 已经在 native 层丢失了上下文，例如 path view / asset context menu 的 `SelectedPaths` 根本没有传到脚本，这会把很多本该是“上下文相关”的扩展退化成无上下文按钮。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 编辑器集成不是一个通吃的泛化扩展器，而是 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个 surface-specific object；不同 toolbar 在注册时拿到真正的 editor context，例如 `UBlueprint` 或 `IAnimationBlueprintEditor`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/AnimationBlueprintToolbar.cpp:26` | 把“扩展挂在哪个 surface”与“这个 surface 需要什么上下文”封装在各自对象里，避免用一个 enum/switch 模型吃掉所有场景。 |
| UnrealCSharp | 模块成员显式持有 `PlayToolBar`、`BlueprintToolBar`、`EditorListener`、`DynamicDataSource`；`BlueprintToolBar` 在 asset editor extender 中保留 `UBlueprint` 上下文，`DynamicDataSource::PopulateAddNewContextMenu()` 则读取 `UContentBrowserDataMenuContext_AddNewMenu::SelectedPaths`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:706` | 不同 surface 用不同 feature object 处理，且上下文在 native 层保持完整，不会被压缩成一个过窄的通用 selection。 |
| puerts | `DeclarationGenerator` 自己维护 `PluginCommands` 与 `RegisterMenus()`，目录监听则另用 `UPEDirectoryWatcher` 这样的 service object 处理；能力拆在各自模块/对象里，而不是收敛成一个中心化扩展开关。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:18` | 能力边界按 feature 拆开，新增 editor 表面时不必修改一份巨大注册器。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `AngelscriptEditor` 内建立 surface adapter registry，把“surface 注册/反注册”和“上下文提取”从 `UScriptEditorMenuExtension` 的大 `switch` 中拆出去，并把旧 enum 变成兼容别名。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorSurfaceAdapter`（或等价接口），最少包含 `Register()`、`Unregister()`、`BuildContext()`、`GetSurfaceId()`；每个 adapter 只负责一种 surface，例如 `LevelViewport.ContextMenu`、`ContentBrowser.PathViewContextMenu`、`BlueprintEditor.Toolbar`。<br>2. 引入统一的 `FAngelscriptEditorSurfaceContext`，至少包含 `SelectedObjects`、`SelectedAssets`、`SelectedPaths`、`ToolMenuContext`，必要时再扩展为 `ContextSensitiveObjects` 或弱引用 toolkit。<br>3. 让 `UScriptEditorMenuExtension` 新增 `SurfaceId` 字段；旧的 `ExtensionMenu` 先映射到内置 adapter，保持现有脚本可用。<br>4. 把现有 path view / asset context / collection view 这几类 surface 先迁到 adapter 上，确保 `SelectedPaths`、collection 信息不再丢失；这一步能立刻改善现有 API，而不需要等待新 surface。<br>5. 再通过 native `UEditorSubsystem` 或扩展后的 `UScriptEditorSubsystem` 暴露 adapter registry，使项目方可以注册自己的 surface adapter，而不必修改插件核心源码。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/SurfaceAdapters/*` 与 `Private/SurfaceAdapters/*`。 |
| 预估工作量 | L |
| 架构风险 | adapter registry 一旦设计得过泛，容易把简单 menu 扩展搞得过重；因此第一阶段应只覆盖现有 surface，并且让旧 enum 路径继续工作。 |
| 兼容性 | 可向后兼容。现有 `UScriptEditorMenuExtension` 子类保持不变，只是内部改走内置 adapter；新的 `SurfaceId` / `SelectedPaths` / registry API 作为增强能力渐进开放。 |
| 验证方式 | 1. 为 `ContentBrowser_PathViewContextMenu` 写一个脚本扩展，确认脚本能收到真实 `SelectedPaths`。<br>2. 新增一个 `BlueprintEditor.Toolbar` adapter，并验证不需要再修改 `ScriptEditorMenuExtension.cpp` 的中心 `switch`。<br>3. full reload 后检查每个 adapter 只注册一次，没有残留 extender 或空上下文回调。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-06 | 脚本资产视图的增量刷新、hierarchy 与 `ContentBrowser` 同步链 | editor data model 补强 | 高 |
| P1 | Arch-07 | editor surface adapter 开放性与上下文保真 | 扩展模型重构 | 高 |

---

## 架构分析 (2026-04-08 15:11)

### Arch-08：`UScriptEditorSubsystem` 仍是生命周期壳层，还不是可组合的 editor capability host

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户能否通过 `EditorSubsystem` 在不改插件核心源码的前提下扩展 editor 能力 |
| 当前设计 | `UScriptEditorSubsystem` 目前只封装了 `ShouldCreate/Initialize/Deinitialize/Tick` 四个生命周期入口和 `GetWorld()`，`EditorSubsystemLibrary` 也只暴露一个 `GetEditorSubsystem()` 查询函数；真正有状态、有资源、有注册动作的 editor 能力仍然散落在 `FAngelscriptEditorModule`、`FClassReloadHelper` 和 `UScriptEditorMenuExtension` 的静态路径里。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7` — `UScriptEditorSubsystem` 被声明为 `NotBlueprintable, Abstract, Meta = (NoBlueprintsOfChildren)`，从类型层面就是一个较封闭的宿主。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:26` — 只转发 `BP_ShouldCreateSubsystem()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:33` — `Initialize()` 只调用 `BP_Initialize()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:40` — `Deinitialize()` 只调用 `BP_Deinitialize()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:57` — 唯一额外回调是 `BP_Tick(float DeltaTime)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:60` — `GetTickableGameObjectWorld()` 直接返回 `nullptr`，没有额外的 editor context API。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` — 脚本侧只拿得到 `GetEditorSubsystem()`，拿不到任何菜单、reload、watcher、asset workflow 的注册器。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h:13` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h:16` — 同插件 runtime 子系统族已经有 `bIsTickableWhenPaused`、`bCreateForLevelEditorWorlds`、`BP_PostInitialize`、`BP_OnWorldBeginPlay` 等更细的生命周期与配置面，说明 editor 侧 subsystem 目前明显更薄。 |
| 优点 | 自动实例化和 tick 语义已经接通，作为“项目级 editor 常驻对象”成本很低。 |
| 不足 | 推断：项目方今天即使拿到 `UScriptEditorSubsystem` 实例，也只能在 `BP_Initialize/BP_Tick` 中做旁路逻辑，无法正式注册 `DirectoryWatcher`、reload 观察点、asset workflow、source navigation 或 `ToolMenus` capability。它更像“脚本能挂进去的宿主壳层”，而不是“可组合的 editor service root”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 不依赖一个泛化 `EditorSubsystem` 来承载所有能力，而是由模块显式持有 `FEditorListener`、`FUnrealCSharpPlayToolBar`、`FUnrealCSharpBlueprintToolBar`、`UDynamicDataSource`；每个对象只负责一类 editor 能力，并各自维护初始化/反初始化。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59` | 把 editor 扩展拆成 capability-specific object，再由模块或宿主统一编排，比把所有责任压给一个过薄的 subsystem 更容易开放给项目方复用。 |
| puerts | 把目录监听做成 `UPEDirectoryWatcher` 这种可实例化 `UObject` service，既有 `BlueprintCallable Watch/UnWatch`，又有 `BlueprintAssignable OnChanged`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:17`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14` | 面向项目方开放的不是“拿到一个 subsystem 自己想办法”，而是“直接拿一个能力对象并订阅它的事件”。 |
| UnLua | 编辑器层能力也按功能拆成 `MainMenuToolbar` 和 `UUnLuaEditorFunctionLibrary::WatchScriptDirectory()` 这样的定向服务，而不是定义一个通用 editor 宿主让所有能力自己钻进去。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27` | 当能力边界清楚时，项目方扩展时只需要接对应 service，不需要理解一个“空壳宿主 + 大量静态旁路”的组合。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 `UScriptEditorSubsystem` 的自动创建优势，但把它升级成 editor service container，而不是只保留 `BP_Tick` 壳层。 |
| 具体步骤 | 1. 在 `UScriptEditorSubsystem` 上新增 capability registry，例如 `GetDirectoryWatchService()`、`GetReloadService()`、`GetAssetWorkflowService()`、`GetNavigationService()`，第一阶段先只提供只读访问器和事件订阅，不改旧行为。<br>2. 新增一组 capability-specific `UObject` service：`UAngelscriptEditorDirectoryWatchService`、`UAngelscriptEditorReloadService`、`UAngelscriptEditorAssetWorkflowService`、`UAngelscriptEditorNavigationService`；每个 service 暴露脚本可调用的注册 API 与 multicast delegate。<br>3. 把当前散落在 `FAngelscriptEditorModule` 里的 bridge lambda、`DirectoryWatcher` 注册、asset popup helper 和 source navigation 包装逐步迁入这些 service，由 subsystem 持有生命周期。<br>4. 为 `UScriptEditorSubsystem` 增加最少一组 editor-specific 生命周期事件，例如 `BP_PostEditorInit`、`BP_BeginPIE`、`BP_EndPIE` 或等价 delegate；保持它比 runtime 子系统更贴近 editor frame，而不是只有 `Tick`。<br>5. 保留 `UEditorSubsystemLibrary::GetEditorSubsystem()` 作为兼容入口，让旧脚本先继续通过旧方式取 subsystem，再渐进切到 capability API。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/*` 与 `Private/Services/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一版 registry 做得过宽，会把 subsystem 重新做成另一个“大而全入口”；应坚持 capability object 粒度，而不是把所有 editor API 直接塞回 subsystem 本体。 |
| 兼容性 | 可向后兼容。现有 `UScriptEditorSubsystem` 子类和 `GetEditorSubsystem()` 调用都可保留；新 service 先作为增强能力追加，不要求脚本立即迁移。 |
| 验证方式 | 1. 在不修改 `AngelscriptEditorModule.cpp` 的前提下，通过 subsystem/service 注册一个新的目录监听或 asset workflow 钩子，确认能工作。<br>2. 确认旧的 `UScriptEditorMenuExtension`、`ShowCreateBlueprintPopup()`、`ShowAssetListPopup()` 仍然可用。<br>3. 模块重载后检查 service 对象不会重复注册 delegate，也不会遗留悬挂 watcher。 |

### Arch-09：reload 到 Blueprint 修复的 editor 链路仍是静态黑箱，缺少可观察阶段和统一 UI 反馈

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本 full reload、Blueprint 修复、editor UI 刷新的主链路是否对外开放、是否充分接入 UE 编辑器反馈机制 |
| 当前设计 | `FClassReloadHelper` 通过静态 lambda 直接订阅 `FAngelscriptClassGenerator` 的 class/struct/delegate/enum/full-reload/post-reload 事件，把 Blueprint 影响分析、节点重构、编译队列、PropertyEditor 刷新、PlacementMode 刷新和几何重建串成一条同步链；外部唯一明显的 reload 消费者是 `UScriptEditorMenuExtension` 在 `OnPostReload` 时全量重建菜单。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:50` — `FClassReloadHelper::Init()` 直接把多个 `FAngelscriptClassGenerator::*` event 绑定到静态 lambda。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:132` — `OnFullReload` 直接调用 `ReloadState().PerformReinstance()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:139` — `OnPostReload` 内直接刷新 `FBlueprintActionDatabase`、`BroadcastBlueprintCompiled()`、触发 `MAP REBUILD ALLVISIBLE`，最后整包重置 state。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83` — reload 时临时构造 `BlueprintImpactSymbols`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:108` — 遍历全部 `UBlueprint` 并调用 `AnalyzeLoadedBlueprint()` 判定依赖。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:291` — 对受影响 Blueprint 批量 `QueueForCompilation()` 并 `FlushCompilationQueueAndReinstance()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:333` — reload 结束后直接 `NotifyCustomizationModuleChanged()` 刷新 PropertyEditor。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25` — 菜单扩展也只是 raw 地监听 `OnPostReload` 并在 full reload 后 `UnregisterExtensions()/RegisterExtensions()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` — 模块启动时只 `FClassReloadHelper::Init()`，没有单独的 reload orchestrator、`FScopedSlowTask` 或统一 notification。 |
| 优点 | reload 正确性相关的修复逻辑集中在一处，保证了 struct/class/delegate/enum 变化后 Blueprint、PropertyEditor、PlacementMode 至少会被一并处理。 |
| 不足 | 推断：这条链路今天更像“内部补救事务”，而不是“可被 editor feature 订阅的正式工作流”。项目方和后续 feature 无法知道 reload 进行到哪一阶段，也拿不到统一的受影响资产结果；大 reload 期间也缺少 `FScopedSlowTask`、notification 或 panel 级反馈，用户只能感知到 editor 卡顿或事后结果。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成主链先 `OnBeginGenerator.Broadcast()`，最后 `OnEndGenerator.Broadcast()`，中间用 `FScopedSlowTask` 分阶段报告进度；`FEditorListener`、`UDynamicDataSource`、`FUnrealCSharpBlueprintToolBar` 再各自订阅同一组 delegate 更新文件监听、内容浏览器和 Blueprint surface。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:250`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:309`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:63`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:38` | 先建立“阶段事件 + 进度反馈”总线，再让不同 editor feature 订阅，而不是把所有副作用都锁进一个静态 helper。 |
| puerts | `UPEDirectoryWatcher` 通过 `OnChanged` 把文件变化显式广播出去；`DeclarationGenerator` 完成后用 `FSlateNotificationManager` 给出 editor 内结果反馈。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:25`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:59`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1622` | 即使功能没有做成完整 panel，也至少把“变化事件”和“完成反馈”做成 editor contract。 |
| UnLua | IntelliSense 生成使用 `FScopedSlowTask` 并支持取消；toolbar 侧用 `FSlateNotificationManager` 把缺少模块名、文件不存在等状态直接反馈给用户。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:76`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:274`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:345` | 长耗时 editor 工作流至少要有进度和结果反馈，否则后续再深的 Slate 集成也很难被用户感知为稳定工具。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不推翻现有 reload 修复逻辑的前提下，加一层 `reload orchestrator + feedback contract`，把内部事务升级为可订阅的 editor 工作流。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorReloadOrchestrator` 或 `UAngelscriptEditorReloadService`，至少定义 `OnReloadQueued`、`OnPreBlueprintRepair`、`OnPostBlueprintRepair`、`OnEditorRefreshCompleted`、`OnReloadCompleted` 五个阶段事件，并把 `FClassReloadHelper` 当前逻辑包进它。<br>2. 在 `PerformReinstance()` 周围加显式阶段广播：构建 `ImpactSymbols` 前发 `OnReloadQueued`，收集 `DependencyBPs` 后发 `OnPreBlueprintRepair`，`FlushCompilationQueueAndReinstance()` 后发 `OnPostBlueprintRepair`，PropertyEditor/PlacementMode 刷新完成后发 `OnReloadCompleted`。<br>3. 对 full reload 引入 `FScopedSlowTask`，至少把“扫描 Blueprint / 重建节点 / 编译 / UI 刷新”四段展示出来；第一阶段不必做复杂 panel，只要能让用户看到 progress 和可取消性即可。<br>4. 在 orchestrator 末尾增加统一 notification 摘要，报告受影响 Blueprint 数、struct/class/enum/delegate 变化数；后续再把这份摘要接到可停靠 results panel。<br>5. 让 `UScriptEditorMenuExtension`、未来的 `UScriptEditorSubsystem` service 和 `ContentBrowser` feature 改为订阅 orchestrator，而不是各自 raw 绑 `OnPostReload`。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Services/AngelscriptEditorReloadOrchestrator.*`。 |
| 预估工作量 | M |
| 架构风险 | 阶段边界如果定义在“对象替换尚未稳定”的时间点，会把半成品状态暴露给订阅者；必须先固定哪些广播发生在 GC 前、哪些发生在 Blueprint 编译后。 |
| 兼容性 | 可向后兼容。第一阶段只是在现有 reload 路径外面包事件和反馈，不改变现有修复顺序与 `UScriptEditorMenuExtension` 的可见行为；旧路径可继续工作。 |
| 验证方式 | 1. 执行一次触发 class/struct/delegate 变化的 full reload，确认阶段事件按顺序触发且无重复。<br>2. 确认受影响 Blueprint 仍被正确重构与重新编译，旧菜单扩展照常重建。<br>3. 在大 reload 场景下验证 `FScopedSlowTask` 和 completion notification 能正确显示数量摘要，不会卡住 shutdown 或模块重载。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-08 | `EditorSubsystem` 是否能承载 capability 级扩展 | 扩展宿主补强 | 高 |
| P1 | Arch-09 | reload/Blueprint 修复链的可观察性与 UI 合同 | 工作流编排重构 | 高 |

---

## 架构分析 (2026-04-08 15:22)

### Arch-10：资产扩展能力停留在 `utility/factory primitive`，还没有升格为可注册的 asset workflow contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户是否能基于现有 `AssetTools` / `UFactory` 包装，把脚本资产创建、导入和打开流程正式接入 UE 编辑器工作流 |
| 当前设计 | `AngelscriptEditor` 已经依赖 `AssetTools`，也提供了 `UAssetToolsStatics` 和 `UScriptableFactory` 这类底层 primitive；但模块自带的主路径仍然是 `CreateModalSaveAssetDialog()` + `CreatePackage()` + `FKismetEditorUtilities::CreateBlueprint()` 的静态 popup 流程，现有 primitive 没有被收敛成可注册的 asset workflow。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:22` 与 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:25` — editor 模块显式依赖 `DirectoryWatcher` 与 `AssetTools`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:21` — `UAssetToolsStatics::CreateAsset(...)` 直接暴露 `AssetToolsModule.Get().CreateAsset(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h:16` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h:28` — `UScriptableFactory` 允许脚本实现 `CreateFromText/CreateFromBinary`，本质上已经准备好了可扩展 `UFactory` 入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.cpp:13` — factory override 最终会回调脚本实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:471` — 内置创建流程先弹 `CreateModalSaveAssetDialog()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:497` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:514` — 然后直接 `CreatePackage()` / `CreateBlueprint()`，没有经过 `UAssetToolsStatics` 或 `UScriptableFactory`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` — 打开资产列表同样是静态 popup helper，而不是一个可注册的 asset workflow surface。 |
| 优点 | 低层能力其实已经具备，项目方理论上可以用脚本自己调用 `AssetTools` 或自定义 `UFactory`；内置 popup 流程也足够直接。 |
| 不足 | 这些能力今天是“知道内部 helper 才能用”的 primitive，而不是“能挂进 `ContentBrowser` / `Add New` / factory registry”的 contract。推断：后续每增加一种脚本资产创建路径，都容易再长出一条新的静态 helper，而不是复用同一条 asset workflow。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `UDynamicDataSource::Initialize()` 直接把 “New Dynamic Class” 挂到 `ContentBrowser.AddNewContextMenu`；`PopulateAddNewContextMenu()` 根据当前选中路径分发创建请求，再由 `FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog()` 打开专用 dialog。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:71`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:12` | 先把“创建入口”接入原生 editor surface，再决定是否弹自定义窗口；dialog 只是 workflow 的一环，不是 workflow 本身。 |
| puerts | `FPuertsEditorModule::OnPostEngineInit()` 为 `UTypeScriptBlueprint` 注册专用 compiler；`UPEBlueprintAsset::LoadOrCreate()` 统一处理“已有则加载、没有则创建、最后通知 `AssetRegistry`”的资产语义。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:154` | 当语言桥接需要特殊资产语义时，把“创建/加载/更新”封装进稳定对象，比把逻辑散在多个 popup 和 lambda 中更易扩展。 |
| UnLua | `CreateLuaTemplate` 被注册成正式 command，执行时先校验上下文，再写模板文件，并通过 notification 向用户反馈错误。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:257`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:274` | 即使不是完整的 asset pipeline，也把“生成文件”做成可发现、可校验、可反馈的 editor action，而不是隐藏的 utility。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不废弃现有 popup 的前提下，引入一层 `asset workflow service/registry`，把现有 `AssetTools` / `UFactory` primitive 收敛成正式 editor contract。 |
| 具体步骤 | 1. 新增 `UAngelscriptAssetWorkflowService` 或等价接口，统一承载 `CreateDerivedBlueprint`、`CreateDataAsset`、`CreateGenericAssetWithFactory` 等入口；第一阶段只把 `ShowCreateBlueprintPopup()` 内部逻辑搬进去，不改外部行为。<br>2. 让 service 内部优先复用 `UAssetToolsStatics::CreateAsset(...)` 与 `UScriptableFactory`，而不是继续在模块里手写 `CreatePackage()` / `AssetCreated()` 细节。<br>3. 在 `UAngelscriptContentBrowserDataSource` 或新的 content browser feature 中注册 `ContentBrowser.AddNewContextMenu` / path view context action，把当前 popup 作为默认 handler 接回原生 surface。<br>4. 为脚本侧开放“workflow provider”注册能力，允许项目按 `AssetClass`、`BaseClass` 或 `FactoryClass` 追加创建策略；旧的 `FAngelscriptRuntimeModule::GetEditorCreateBlueprint()` 继续保留为 fallback。<br>5. 第二阶段再评估是否为脚本虚拟资产补轻量 `IAssetTypeActions` 或 `UAssetDefinition`，但这一步可以延后，避免大爆炸。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/AngelscriptAssetWorkflowService.*`。 |
| 预估工作量 | M |
| 架构风险 | 如果一开始把 registry 设计得过泛，容易把少量内置工作流包装成新的“大总线”；第一阶段应只覆盖当前 blueprint/data asset 流程，避免把所有资产类型一次性抽象。 |
| 兼容性 | 向后兼容。现有 popup、现有 runtime delegate 和已有脚本 helper 都可以保留；新 service 只是把内部实现收敛并增加原生入口。 |
| 验证方式 | 1. 通过旧的 “Create Blueprint” 路径创建一次脚本派生资产，确认行为与今天一致。<br>2. 再从 `ContentBrowser` 的新增入口创建同类资产，确认最终仍走同一条 workflow service。<br>3. 如接入 `UScriptableFactory`，验证一个脚本自定义 factory 可以在不改模块核心代码的前提下被调用。 |

### Arch-11：源码导航 contract 被 `VS Code` 硬编码，尚未真正接入 UE 的 `SourceCodeNavigation/SourceCodeAccessor` 生态

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 内的源码打开、跳转行号和 IDE 选择是否可配置、是否能被项目方替换 |
| 当前设计 | `AngelscriptEditor` 表面上接入了 `FSourceCodeNavigation`，但真正执行打开文件的末端仍然是 `FPlatformMisc::OsExecute(nullptr, TEXT("code"), ...)`；因此 menu、Shift+点击和 navigation handler 最终都被锁到 `VS Code`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:708` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:710` — `ASOpenCode` 明确写成 “Open Angelscript workspace (VS Code)” 并直接执行 `code`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:95` — 自定义 navigation handler 的 `OpenModule()` 内直接 `OsExecute("code", "--goto ...")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:110` — `OpenFile()` 同样直接执行 `code`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:136` — `RegisterAngelscriptSourceNavigation()` 把这个 handler 全局注册进 `FSourceCodeNavigation`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:257` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:261` — `ToolMenu` action 在按住 `Shift` 时走 `FSourceCodeNavigation::NavigateToFunction()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:355` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:359` — 普通 menu action 也走同样的导航路径。 |
| 优点 | 对以 `VS Code` 为主的项目非常直接，`--goto` 行号跳转也简单可靠。 |
| 不足 | 这让 editor 集成在“最后一跳”失去了可替换性。推断：用户若把 UE 的 source accessor 配成 `Rider`、`Visual Studio` 或远程开发工具，Angelscript 菜单和导航仍会绕开 editor 设定；同时失败路径没有 notification，项目方也无法通过 `EditorSubsystem` 或 setting 覆盖这一行为。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 内容浏览器编辑、Blueprint toolbar 的“Open File”，以及新建动态类完成后的自动打开，都统一走 `FSourceCodeNavigation::OpenSourceFile(...)`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:523`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:59`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:775` | 让所有 editor 表面共享一套 engine-native source open contract，IDE 选择和 accessor fallback 由 UE 统一承接。 |
| UnLua | 源码相关动作先注册成正式 command；当文件不存在时，不是静默失败，而是通过 `FSlateNotificationManager` 给出 editor 内反馈。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:315`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:345` | 即使动作最终要离开编辑器，也应保留 command identity 和失败反馈，而不是把外部进程调用直接硬编码进业务逻辑。 |
| puerts | `DeclarationGenerator` 把工具动作挂在 `PluginCommands + ToolMenus` 上，并在完成后通过 `FSlateNotificationManager` 反馈结果，保持 editor action 与执行实现解耦。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1569`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1622` | 即便底层执行逻辑特殊，editor 层也应先构建 command/feedback contract，再把外部工具调用放到可替换实现里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“源码导航”拆成 engine-native path + 可配置 fallback path 两层，默认保留 `VS Code` fallback，但不再把它写死为唯一实现。 |
| 具体步骤 | 1. 新增 `FAngelscriptSourceNavigationService` 或等价接口，统一提供 `OpenWorkspace()`、`OpenFileAtLine()`、`OpenClassDefinition()` 三个 API，模块菜单和 `FAngelscriptSourceCodeNavigation` 全部改调这层。<br>2. 第一优先级走 `FSourceCodeNavigation::OpenSourceFile(...)` 或 `ISourceCodeAccessor` 等 UE 原生入口；若脚本文件不在 accessor 支持范围内，再走可配置的外部命令模板。<br>3. 在 `UAngelscriptSettings` 或 editor setting 中新增可选 `ExternalSourceOpenCommand` / `ExternalWorkspaceCommand`；默认值保持当前 `code`/`code --goto`，以维持老项目行为。<br>4. 当原生 accessor 和外部命令都失败时，使用 `FSlateNotificationManager` 给出错误提示，而不是静默返回 `false`。<br>5. 若后续引入 `UScriptEditorSubsystem` service container，可把 navigation service 暴露给项目方，允许他们在不改核心模块的情况下替换 IDE 路由。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/AngelscriptSettings.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/AngelscriptSourceNavigationService.*`。 |
| 预估工作量 | M |
| 架构风险 | UE 原生 `SourceCodeAccessor` 对 `.as` 文件或自定义 workspace 的支持可能不完整，因此必须保留外部命令 fallback；否则会把今天可用的 `VS Code` 工作流做坏。 |
| 兼容性 | 向后兼容。默认配置仍可维持 `VS Code` 打开方式；只是把它从“硬编码实现”降级为“默认 fallback”。 |
| 验证方式 | 1. 在 `VS Code` 默认配置下，验证 `ASOpenCode`、Shift+点击 menu action、`NavigateToFunction()` 仍可打开正确文件和行号。<br>2. 将 UE editor 的 source accessor 改成其他 IDE 后，确认新增 service 优先走原生 accessor。<br>3. 人为制造外部命令失效场景，确认 editor 内会出现 notification，而不是静默无响应。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-10 | `AssetTools/UFactory` primitive 尚未形成正式 asset workflow contract | 扩展面收敛 | 高 |
| P2 | Arch-11 | 源码导航被 `VS Code` 硬编码，缺少 engine-native accessor 层 | editor contract 补强 | 中 |

---

## 架构分析 (2026-04-08 15:33:29)

### Arch-12：脚本扩展点仍停留在 generic menu 注入，缺少 host-specific editor adapter

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本侧 editor 扩展是否能深入接入 `BlueprintEditor`、`AnimationBlueprintEditor`、`ContentBrowser AddNew` 等 host-specific surface |
| 当前设计 | `UScriptEditorMenuExtension` 当前把扩展位置收敛为 `ToolMenu`、`LevelViewport_*`、`ContentBrowser_*` 这一组 generic menu location；上下文只保留 `SelectedObjects`/`SelectedAssets` 和可选 `ToolMenuContext` 查找，真正执行动作时又统一回落到 `ShowPromptToCallFunction()` 的 modal `SWindow`。`UScriptEditorSubsystem` 则只提供生命周期与 tick 壳层，没有 host adapter 注册能力。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:10` — `EScriptEditorMenuExtensionLocation` 只覆盖 `ToolMenu`、`LevelViewport_*`、`ContentBrowser_*`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:98` — `FExtenderSelection` 只有 `SelectedObjects` 和 `SelectedAssets`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:105` — `GetToolMenuContext()` 只是从 `FToolMenuContext` 做 generic `FindByClass()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:143` — `CallFunctionOnSelection()` 统一走 `FScriptEditorPrompts::ShowPromptToCallFunction(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:166` — 参数收集 UI 通过 `GEditor->EditorAddModalWindow(Window)` 弹出 modal `SWindow`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:47` — `UScriptEditorSubsystem` 只暴露 `BP_Initialize/BP_Deinitialize/BP_Tick`。 |
| 优点 | 脚本类只要继承 `UScriptEditorMenuExtension` 并打 `CallInEditor` 元数据，就能快速长出 menu action，进入门槛低，覆盖 `LevelViewport` 和 `ContentBrowser` 的通用挂点也足够快。 |
| 不足 | 这套 contract 还没有把“具体 editor host”建模出来。推断：项目方无法在不改插件 C++ 的情况下拿到 `FBlueprintEditor`、`IAnimationBlueprintEditor`、`UContentBrowserDataMenuContext_AddNewMenu` 等真实 host 上下文，因此难以把脚本扩展升级成 blueprint toolbar、animation toolbar、`Add New` 工作流或可停靠 tool surface。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块显式持有 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三种 surface 对象；各对象分别挂到 `LevelEditor.LevelEditorToolBar.User`、`FBlueprintEditorModule` 的 `FAssetEditorExtender`、`FAnimationBlueprintEditorModule` 的 toolbar extender。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/AnimationBlueprintToolbar.cpp:26` | 先按 host 维度拆 surface adapter，再把命令映射到具体 editor host，能拿到更丰富的上下文对象。 |
| puerts | `DeclarationGenerator` 先建立 `PluginCommands`，再把同一命令显式投影到 `LevelEditor.MainMenu.Window` 和 `LevelEditor.LevelEditorToolBar.User/PlayToolBar` 两个 surface，而不是只依赖 metadata 驱动的临时 menu action。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1569`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1593`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1654` | surface 注册应是显式的 editor contract，同一 action 可以复用到多个 host，而不是每次临时拼装。 |
| UnrealCSharp | 模块显式拥有 `PlayToolBar`、`BlueprintToolBar`、`DynamicDataSource`；`BlueprintToolBar` 接入 `FBlueprintEditorModule` 的 `ExtenderDelegates`，`DynamicDataSource` 则扩展 `ContentBrowser.AddNewContextMenu` 并读取 `UContentBrowserDataMenuContext_AddNewMenu` 的选中路径。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:17`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:71`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704` | 不同 editor host 可以由独立对象管理，并向脚本/逻辑层暴露更准确的上下文，而不是只传一组对象选择集。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `UScriptEditorMenuExtension` 的前提下，新增一层 `editor host adapter` 注册面，把“扩展点位置”从 generic menu location 升级为“可绑定具体 editor host 的 contract”。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorHostAdapter` 或等价接口，第一阶段只实现四个高价值 host：`LevelEditorToolBar`、`BlueprintEditorToolBar`、`AnimationBlueprintEditorToolBar`、`ContentBrowserAddNew`。<br>2. 在 `AngelscriptEditor` 模块中加入 surface registry，并让各 adapter 自己管理对应的 UE extender / `ToolMenus` / host context 解包逻辑。<br>3. 为脚本侧新增基于 `UScriptEditorSubsystem` 的注册 API，例如 `RegisterHostAction(HostId, ActionDescriptor)`；同时保留现有 `UScriptEditorMenuExtension`，并把它适配成 registry 的一个 generic-menu provider。<br>4. 第一阶段继续复用 `FScriptEditorPrompts` 做参数收集，避免一次性改造 UI；第二阶段再为 `BlueprintEditor` 和 `ContentBrowser AddNew` 增加可选的 host-local widget/context wrapper。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorHosts/*.h` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/EditorHosts/*.cpp`。 |
| 预估工作量 | L |
| 架构风险 | 多个 host module 的生命周期不同，若 registry 与 script class reload 顺序处理不当，容易留下重复 extender 或失效 context wrapper。 |
| 兼容性 | 向后兼容。现有 `UScriptEditorMenuExtension`、`CallInEditor` 函数和 modal prompt 路径都可以保留；新 adapter 只是新增更深的集成面，不要求项目方迁移旧扩展。 |
| 验证方式 | 1. 用脚本新增一个 `BlueprintEditor` toolbar action 和一个 `ContentBrowser AddNew` action，确认无需改模块主流程即可注册。<br>2. 验证现有 `ToolMenu` / `ContentBrowser_AssetViewContextMenu` 扩展行为不变。<br>3. 执行一次 script reload 与模块卸载，确认不会出现重复 toolbar/button。 |

### Arch-13：脚本文件监听与 reload 触发仍是内部副作用，尚未升格为 editor event service

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 项目方能否通过正式 editor service 复用脚本文件监听、接收变更事件并在 reload 前后扩展自己的 editor 自动化 |
| 当前设计 | `FAngelscriptEditorModule::StartupModule()` 直接为每个 script root 注册 `DirectoryWatcher`，统一绑定到静态 `OnScriptFileChanges()`；这个回调再立刻进入 `QueueScriptFileChanges()`，把结果写入 `FAngelscriptEngine` 的 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 队列。`UScriptEditorSubsystem` 仍只有生命周期/tick，`UEditorSubsystemLibrary` 也只暴露 `GetEditorSubsystem()`，没有任何 watcher 或 reload 事件注册接口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:361` — 模块启动时直接注册 `OnPostEngineInit`、`DirectoryWatcher` 等 editor hook。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:367` — 遍历 `FAngelscriptEngine::MakeAllScriptRoots()` 并为每个 root 注册 `RegisterDirectoryChangedCallback_Handle(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78` — `OnScriptFileChanges()` 直接调用 `QueueScriptFileChanges(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.h:10` — watcher helper 的公开 contract 只有“枚举已加载脚本”和“把变化排入引擎队列”。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43` — `QueueScriptFileChanges()` 逐条变化处理。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:61` — 删除直接进入 `Engine.FileDeletionsDetectedForReload`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:65` — 新增/修改直接进入 `Engine.FileChangesDetectedForReload`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:47` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:16` — 脚本侧没有 watcher/reload service API。 |
| 优点 | 默认 reload 路径很短，文件变化能直接落入 runtime 的 reload 队列；`QueueScriptFileChanges()` 已经被抽成独立 helper，后续重构可以沿用现有逻辑和测试。 |
| 不足 | 当前的 file change pipeline 还是“模块内部副作用”，不是“项目可订阅的 editor contract”。推断：项目方若想在脚本变化时刷新自定义面板、更新索引、追加静态分析或延迟 reload，只能改插件源码或旁路监听文件系统，无法通过 `EditorSubsystem` 合法接入。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块只负责调用 `WatchScriptDirectory()`，真实的 `DirectoryWatcherHandle` 持有和脚本目录监听逻辑放在 `UUnLuaEditorFunctionLibrary` 内。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.h:35` | 即便不做完整 event bus，也应先把 watcher 责任从模块入口剥离到独立拥有者。 |
| puerts | `UPEDirectoryWatcher` 是可实例化的 `UObject`，提供 `Watch/UnWatch`，并通过 `BlueprintAssignable OnChanged` 向外广播 `Added/Modified/Removed` 三类变化；析构函数会自动注销 watcher。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:11`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:25`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:59`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:74` | 文件变化首先应是一个可复用、可订阅的 editor service，然后再决定默认消费者是谁。 |
| UnrealCSharp | `FEditorListener` 把 `OnPostEngineInit`、PIE、generator、`AssetRegistry`、`MainFrame`、`DirectoryWatcher` 全部收进一个 listener 对象，并在析构函数中对称回收。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:42`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:52`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:100` | watcher 不应是孤立 callback，而应成为 editor 事件聚合器的一部分，便于统一管理生命周期与扩展入口。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 `DirectoryWatcher -> QueueScriptFileChanges -> engine reload queue` 提炼为 `editor watch service`，默认继续驱动当前 reload，但额外向项目方暴露可订阅事件。 |
| 具体步骤 | 1. 新增 `UAngelscriptEditorWatchService` 或等价 feature 对象，由模块持有 `RootPath -> FDelegateHandle` 映射，负责 watcher 注册、注销和路径标准化。<br>2. 保留 `QueueScriptFileChanges()` 作为默认消费者，把它挂到 service 内部；同时新增 `OnScriptFilesChanged`、`OnReloadQueued` 两个 multicast 事件，对外暴露 `Added/Modified/Removed` 与标准化相对路径。<br>3. 为 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 增加访问与注册 API，允许项目方 editor subsystem 订阅 watcher 事件，或按需追加额外 watch root/filter。<br>4. 第一阶段不改变现有 reload 判定和时机，只是把现有内部副作用包进 service；第二阶段如确有需求，再增加可选 filter/policy 链，但默认实现保持今天的 reload 语义。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/AngelscriptEditorWatchService.*`。 |
| 预估工作量 | M |
| 架构风险 | 若对外广播发生在错误线程或错误阶段，可能把半处理状态暴露给脚本侧；需要先固定事件是在排队前、排队后还是 reload 完成后触发。 |
| 兼容性 | 向后兼容。默认消费者仍然是当前 `QueueScriptFileChanges()` 和 runtime reload 队列；新 service 只是把这条路径显式化，并额外提供订阅能力。 |
| 验证方式 | 1. 现有 `AngelscriptDirectoryWatcherTests` 应继续通过，证明默认 reload 队列行为未变。<br>2. 新增自动化测试，验证 service 在新增、删除、文件夹重命名场景下只广播一次正确 payload。<br>3. 手工创建一个 `UScriptEditorSubsystem` 订阅 watcher 事件，确认无需修改模块源码即可收到文件变化通知。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-12 | 扩展面缺少 host-specific editor adapter，无法深入接入 `BlueprintEditor` / `ContentBrowser AddNew` | 扩展点分层 | 高 |
| P1 | Arch-13 | 文件监听与 reload 仍是内部副作用，缺少可订阅的 editor watch service | service 提炼 | 高 |

---

## 架构分析 (2026-04-08 15:47:55)

### Arch-14：editor 配置面仍寄宿在 runtime settings，缺少 editor-specific policy 与 customization contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 集成功能是否有独立的配置控制面，便于项目方调参、验证和扩展 |
| 当前设计 | `AngelscriptEditor` 启动时只向 `Project/Plugins` 注册 `UAngelscriptSettings`；该 settings 类是 `Config=Engine` 的 runtime/compiler 配置集合，editor 侧暴露的设置入口也只是通用 `OpenSettings()` helper。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:384` — 模块启动阶段唯一直接注册的 settings 是 `GetMutableDefault<UAngelscriptSettings>()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41` — `UAngelscriptSettings` 被声明为 `Config=Engine, DefaultConfig`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:51` — 主要字段围绕预处理、bind、Blueprint specifier 和 warning policy。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:137` — 与 editor 直接相关的字段目前只读到 `EditorMaximumScriptExecutionTime` 这类 runtime safety policy。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h:71` — editor 侧只提供 `OpenSettings(Container, Category, Section)` 这种通用 viewer 跳转。 |
| 优点 | 配置入口很少，当前维护成本低；runtime/compiler 行为集中在一个对象上，现有项目不需要区分 runtime setting 与 editor setting。 |
| 不足 | 推断：随着 `EditorArch` 已经暴露出的 `source navigation`、`ContentBrowser`、reload feedback、watch service、host adapter 等能力继续增长，单个 `UAngelscriptSettings` 会把 runtime policy 与 editor policy 混在一起，项目方也拿不到 editor-only 的校验、即时应用和复杂字段 customization。当前设计更像“给编译器和引擎设参数”，而不是“给 editor 集成设策略”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 明确拆出 `UUnLuaEditorSettings`，字段覆盖 `HotReloadMode`、`IntelliSense`、build/debug 选项；注册 settings section 时绑定 `OnModified()`，并在主菜单命令里分别暴露 runtime/editor settings 入口。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h:39`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:114`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:134`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:39` | 把 editor policy 做成独立 settings object，并为“修改后如何生效”定义清晰回调，用户理解成本更低。 |
| UnrealCSharp | `UUnrealCSharpEditorSetting` 是独立的 editor 配置对象，字段覆盖 `ScriptDirectory`、`SupportedModule`、`SupportedAssetPath/Class`、编译与生成策略；注册 settings 时会填充默认模块/资产集，同时对 `ProjectDirectoryPath` 和 `GameContentDirectoryPath` 做 `PropertyEditor` customization。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:32`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:75`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:107`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:49` | editor 配置不只是 bool 列表，而是能承载路径、模块过滤和资产范围等复杂 contract，并通过 customization 降低误配置概率。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增 `UAngelscriptEditorSettings` 作为 editor-only control plane，`UAngelscriptSettings` 保持 runtime/compiler 职责不变。 |
| 具体步骤 | 1. 新增 `UAngelscriptEditorSettings`，使用独立 config 名称，例如 `config=AngelscriptEditor`；第一阶段只承载 editor-only 字段：`ExternalSourceOpenCommand/WorkspaceCommand`、额外 watch root/filter、reload notification/slow-task policy、`ContentBrowser` 虚拟根显示策略、Blueprint 默认创建目录策略。<br>2. 在 `FAngelscriptEditorModule::StartupModule()` 中并列注册 `Angelscript Editor` settings section；保留旧的 `Angelscript` section，避免打破现有项目配置。<br>3. 为需要强约束的字段补 `PropertyEditor` customization，例如脚本根路径选择、长包名目录选择、外部命令模板校验；先从一个 `DirectoryPath` customization 开始，不做大范围 UI 重写。<br>4. 给新 settings section 加 `OnModified` 回调：即时更新无需重启的 editor feature，例如 watcher root、notification policy 和 source navigation fallback；仍需重启的字段继续标 `ConfigRestartRequired`。<br>5. 第二阶段再把 toolbar/command 中的“打开设置”入口正式化，区分 runtime/editor 两个 section；现有 `UEditorStatics::OpenSettings()` 继续保留为通用 fallback。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/AngelscriptEditorSettings.h`、`Private/AngelscriptEditorSettings.cpp`、`Private/DetailCustomization/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就把太多 runtime 字段搬迁到新 section，容易造成配置分裂和旧 ini 失配；更稳妥的做法是先新增 editor-only 字段，旧字段保持原位。 |
| 兼容性 | 向后兼容。现有 `UAngelscriptSettings`、旧 ini 路径和通用 `OpenSettings()` 都可保留；新 `UAngelscriptEditorSettings` 只追加 editor control plane，不要求旧项目迁移。 |
| 验证方式 | 1. 新增一个 editor-only setting，验证能在 Project Settings 中显示、保存并在重启后恢复。<br>2. 修改一个无需重启的 editor setting，例如 source navigation fallback 或 watcher root，确认对应 feature 在当前 editor session 内生效。<br>3. 人为输入非法路径或非法命令模板，确认 customization/校验能阻止无效配置落盘。 |

### Arch-15：editor 与 runtime 的交互仍依赖 UI 向的全局 delegate 槽位，跨层边界不稳定

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 功能与 runtime/debugging 功能之间是否通过稳定、可替换的边界协作 |
| 当前设计 | `AngelscriptRuntimeModule` 直接定义 `GetDebugListAssets()`、`GetEditorCreateBlueprint()`、`GetEditorGetCreateBlueprintDefaultAssetPath()` 这类 editor 行为导向的全局 delegate 槽位；runtime debug server 在处理调试消息时直接广播，editor 模块再用匿名 lambda 把它们接到 popup 逻辑上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:19` — runtime 模块头直接声明 `FAngelscriptDebugListAssets` 与 `FAngelscriptEditorCreateBlueprint`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:45` — 这些 delegate 以全局 `Get*()` 静态入口暴露。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1170` — debug server 在 `FindAssets` 消息里直接 `Broadcast(AssetList.Assets, BaseClass)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1180` — `CreateBlueprint` 消息同样直接 `Broadcast(Cast<UASClass>(...))`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397` — editor 模块用 lambda 把 `GetDebugListAssets()` 绑定到 `ShowAssetListPopup()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:405` — `GetEditorCreateBlueprint()` 被绑定到 `ShowCreateBlueprintPopup()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:434` — editor popup 内又反向执行 `GetEditorGetCreateBlueprintDefaultAssetPath()`，形成双向全局 delegate 依赖。 |
| 优点 | 现有桥接路径短，runtime 与 editor 之间几乎不用写额外样板代码；debug server 能很快复用 editor 现有 popup。 |
| 不足 | 推断：这条边界现在暴露的是“UI 动作槽位”，不是“语义稳定的 editor service”。结果是 runtime 侧知道了 `show asset list / create blueprint` 这类 editor 交互语义，editor 侧也反向查询 runtime delegate 决定默认路径。后续如果想让同一请求同时支持 popup、`ContentBrowser AddNew`、自动化测试或项目自定义 handler，就只能继续在同一组全局 delegate 上叠加约定，替换能力和组合能力都很弱。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 共享层暴露的是 `OnBeginGenerator`、`OnEndGenerator`、`OnCompile` 这类生命周期/数据事件；editor 侧再由 `FEditorListener` 订阅这些事件并更新自身 feature，而不是在 core/runtime 层暴露“打开某个 UI”的回调槽。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:33` | 共享边界更适合承载阶段事件或数据事件，UI 决策则留在 editor feature 对象中。 |
| puerts | editor 模块在 `OnPostEngineInit()` 里直接注册 `UTypeScriptBlueprint` compiler 和 `SourceFileWatcher`；目录变化服务 `UPEDirectoryWatcher` 也是独立 `UObject`，通过 `OnChanged` 向外广播文件变化。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:17` | editor 侧 capability 由 editor module 或 editor service 自己拥有，runtime 不需要公开 UI 向 delegate 槽位。 |
| UnLua | editor 集成由 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 等对象持有，命令映射发生在 editor toolbar 内；脚本目录监听也由 editor function library 负责发起。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:32` | editor surface 和 editor command 由 editor 模块拥有，runtime 不承载“打开 editor UI”的桥接职责。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前 UI 向全局 delegate 收敛成一层语义化 `editor bridge/service`，runtime 只发请求，不再直接依赖具体 UI 动作。 |
| 具体步骤 | 1. 在 editor/runtime 共享边界新增 `IAngelscriptEditorBridge` 或等价请求总线，最少定义 `RequestOpenAssets(AssetPaths, BaseClass)`、`RequestCreateDerivedAsset(UASClass*)`、`ResolveDefaultAssetPath(UASClass*)` 三个语义化入口；默认实现放在 `AngelscriptEditor`。<br>2. `AngelscriptDebugServer` 改为只依赖这层 bridge；bridge 不存在时返回结构化失败或 notification，而不是假定某个 UI handler 一定已绑定。<br>3. 把 `ShowAssetListPopup()`、`ShowCreateBlueprintPopup()`、未来的 `ContentBrowser AddNew`/dockable panel 都放到 bridge 的 editor 实现里决定；runtime 只传 class/asset 标识，不再决定使用哪种 surface。<br>4. 兼容阶段保留现有 `GetDebugListAssets()` / `GetEditorCreateBlueprint()` / `GetEditorGetCreateBlueprintDefaultAssetPath()`，但让它们内部转发到新 bridge，并标注为 deprecated adapter。<br>5. 第二阶段再把 bridge 暴露给 `UScriptEditorSubsystem` service container，让项目方在不改 runtime 源码的情况下替换 editor 行为，例如把“创建 Blueprint”改成走 `ContentBrowser AddNew` 或项目自定义 wizard。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/AngelscriptEditorBridge.*` 与 `Private/Services/AngelscriptEditorBridge.*`。 |
| 预估工作量 | M |
| 架构风险 | 如果 bridge 设计仍然直接暴露 popup 细节，只是把名字换了一层，问题不会消失；必须坚持“语义请求”而不是“UI 动作名”。 |
| 兼容性 | 可增量实施。第一阶段只是把现有 global delegate 包到 bridge 后面，默认行为保持 `ShowAssetListPopup()` / `ShowCreateBlueprintPopup()` 不变；旧调用方和调试工作流可以继续工作。 |
| 验证方式 | 1. 通过 debug server 触发 `FindAssets` 与 `CreateBlueprint`，确认现有 editor 行为不回归。<br>2. 在 bridge 默认实现之外新增一个测试实现，验证无需修改 runtime 代码即可替换默认 asset path 与创建 surface。<br>3. 模块重载后确认 bridge 不重复注册，且 runtime 侧在无 editor bridge 的场景下会安全失败而不是悬挂调用。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-15 | editor/runtime 仍通过 UI 向全局 delegate 槽位协作，边界不稳定 | 跨层 contract 重构 | 高 |
| P2 | Arch-14 | editor 配置面缺少独立 settings/customization contract | control plane 补强 | 中高 |

---

## 架构分析 (2026-04-08 15:58:17)

### Arch-16：`BlueprintImpact` 已有可复用分析内核，但 editor 层还没有把它升格为统一 automation surface

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 现有分析能力是否能被 editor 内命令、toolbar、subsystem 和项目自动化统一复用 |
| 当前设计 | `BlueprintImpact` 的核心扫描 API 已经是 public contract，并同时被 reload 修复链和 commandlet 复用；但 `AngelscriptEditor` 自身没有把这套能力接到 `ToolMenus`、`FAutoConsoleCommand` 或 `UEditorSubsystem` 服务面，用户在 editor session 里只能间接受益于 reload，或单独运行 commandlet。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:62` — public 头直接导出 `NormalizeChangedScriptPaths`、`BuildImpactSymbols`、`AnalyzeLoadedBlueprint`、`FindBlueprintAssets`、`ScanBlueprintAssets`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83` — reload 修复阶段手工构造 `FBlueprintImpactSymbols`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:111` — 对每个 `UBlueprint` 调用 `AnalyzeLoadedBlueprint()` 判定依赖。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55` — commandlet 主入口只是在 CLI 上包装同一个 scanner。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` — editor 菜单注册入口从 `RegisterToolsMenuEntries()` 开始。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:713` — `Tools` 菜单只注册了 `ASOpenCode`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:726` — 第二个固定入口是 `ASGenerateBindings`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:737` — 还保留 `Function Tests`，但没有 `BlueprintImpact` 入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:47` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:16` — `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 没有暴露 impact scan service。 |
| 优点 | 扫描逻辑已经被做成可测试、可复用的分析内核，当前没有再写一套 editor-only 分支；`ClassReloadHelper` 与 commandlet 共享同一套命中规则，有利于保持结果一致。 |
| 不足 | editor 内缺少统一 automation surface，导致这套能力在架构上仍是“两头可用、中间缺口”：CI 可以跑 commandlet，reload 内部会用 scanner，但项目方不能在 toolbar、console、subsystem 或自定义 panel 里稳定调用同一 contract。推断：后续若要接入 dockable results panel、右键菜单扫描或项目自定义批量检查，容易再次长出旁路包装，而不是复用同一 service。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaEditorCommands` 把 `GenerateIntelliSense` 做成正式 command，`FMainMenuToolbar` 再把同一命令接到 toolbar/menu。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:34`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:123` | 已存在的 editor 工具能力应先升格成稳定 command，再决定挂到哪个 surface。 |
| puerts | `DeclarationGenerator` 把 `GenUeDts` 同时接到 `ToolMenus` 和 `Puerts.Gen` console command，完成后再用 `FSlateNotificationManager` 给出 editor 内反馈。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1668` | 同一分析/生成能力可以同时服务 toolbar、menu 和命令行，而不是把 headless 路径和 editor 路径拆成两套。 |
| UnrealCSharp | 模块启动时注册 `CodeAnalysis`、`SolutionGenerator`、`Compile`、`Generator` 四个 `FAutoConsoleCommand`，toolbar 再把 `GeneratorCode` 等命令投到 editor UI；执行主链还通过 core delegates 向外广播。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:68`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:94`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:39`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditorCommands.h:25` | 先把能力做成 `service + command + console entry`，再由不同 editor surface 复用，扩展性和可发现性都更强。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `BlueprintImpact` 从“scanner + commandlet + reload 内部 helper”提升为 `AngelscriptEditor` 的正式 automation service，并让 UI、console、subsystem 共用同一入口。 |
| 具体步骤 | 1. 新增 `UAngelscriptBlueprintImpactService` 或等价 native service，内部统一封装 `ScanBlueprintAssets()`、`BuildImpactSymbols()` 和参数标准化；第一阶段只接受 `FBlueprintImpactRequest` 并返回结构化 `FBlueprintImpactScanResult`，不改 scanner 核心。<br>2. 让 `UAngelscriptBlueprintImpactScanCommandlet::Main()` 退化为 service adapter：负责解析命令行参数，然后把真正扫描转交给新 service，保持现有 exit code 和 stdout contract 不变。<br>3. 在 editor 模块新增一个 `FAutoConsoleCommand`，例如 `Angelscript.Editor.BlueprintImpact`；同时给 `Tools` 菜单或后续 toolbar 增加同名 action，但实现全部调用同一个 service，而不是再写一套 popup/commandlet 旁路。<br>4. 通过 `UScriptEditorSubsystem` 或新的 `EditorServices` 容器暴露只读访问入口，允许项目方 editor subsystem、右键菜单或自定义 panel 在不改插件核心的前提下调用 impact scan。<br>5. 第一阶段结果展示只需加 `FSlateNotificationManager` 或 `FMessageLog` 摘要；第二阶段再考虑 dockable results panel。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactService.*` 与 `Private/BlueprintImpact/AngelscriptBlueprintImpactService.*`。 |
| 预估工作量 | M |
| 架构风险 | 如果 service 直接把 `commandlet` 的日志格式或 `reload` 的内部临时状态暴露到 editor API，会把 headless contract 和 UI contract 再次耦合；service 应只暴露结构化请求/结果，日志与 surface 由 adapter 决定。 |
| 兼容性 | 向后兼容。现有 commandlet 参数、exit code、reload 内部调用和 scanner API 都可以保留；新 service 只是把这些入口收敛到同一条执行链上。 |
| 验证方式 | 1. 对同一组 `ChangedScript` 输入，同时跑 commandlet 和 console/service 调用，确认 `Matches`、`FailedAssetLoads` 和命中原因一致。<br>2. 在 editor session 内手动触发一次 impact scan，确认无需脚本 reload 也能得到结果摘要。<br>3. 让一个项目自定义 `UScriptEditorSubsystem` 调用新 service，确认不修改 `AngelscriptEditorModule.cpp` 也能复用扫描能力。 |

### Arch-17：public 边界把实现细节和依赖一并泄露，外部扩展拿不到窄而稳的 editor contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部模块和项目方是否能通过稳定的 public contract 扩展 editor 功能，而不是依赖 transitive dependency 与 static helper |
| 当前设计 | `AngelscriptEditor` 的 `Build.cs` 把 `BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`AssetTools` 等 editor 实现依赖放进 `PublicDependencyModuleNames`；但 public 头真正暴露给外部的却主要是 `FAngelscriptEditorModule` 里的 popup/codegen static helper，以及一个只负责 `Tick` 的 `UScriptEditorSubsystem` 壳层。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12` — public 依赖列表开始于 `PublicDependencyModuleNames.AddRange(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:17` — `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime` 被放到 public 依赖。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:20` — `BlueprintGraph`、`Kismet`、`DirectoryWatcher` 也被公开泄露。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:23` — `Slate`、`SlateCore`、`AssetTools` 同样属于 public 依赖。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:12` — public 头直接暴露 `ShowAssetListPopup()` 和 `ShowCreateBlueprintPopup()` 这类具体 UI helper。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:15` — 还公开 `GenerateNativeBinds()`、`GenerateBindDatabases()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:17` — `GenerateNewModule()` 也是 static helper。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:21` — `GenerateBuildFile()` 等 codegen 细节继续暴露在同一个模块头里。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:8` — public 的 `UScriptEditorSubsystem` 只继承 `UEditorSubsystem` + `FTickableGameObject`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:16` — library 仅包装 `GetEditorSubsystem()`，没有能力级 service getter。 |
| 优点 | 旧代码若直接 include 一个模块头，就能立刻调用现成 helper；短期内新增 debug 工具或工程脚手架时接线非常快。 |
| 不足 | public 边界同时泄露了“重型依赖”和“具体实现”，却没有给出窄而稳定的 service contract。结果是外部扩展一方面会被迫承受宽 public dependency 带来的编译耦合，另一方面又只能依赖 `ShowCreateBlueprintPopup()`、`GenerateBuildFile()` 这种 concrete helper，而不是 `asset workflow service`、`watch service`、`impact service` 这类可替换接口。推断：这会让后续任何 editor 扩展都更容易变成“再加一个 static helper”，而不是形成可组合能力。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor.Build.cs` 把几乎全部 editor 依赖都放在 `PrivateDependencyModuleNames`，public 面并不透传 `Slate`、`BlueprintGraph`、`ToolMenus` 等实现细节。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:87` | 先把构建边界收窄，外部模块如果真需要某个 editor 框架，应自己显式声明依赖，而不是吃 transitive leak。 |
| puerts | 把 `DeclarationGenerator` 拆成独立模块；`PuertsEditor` 的 public contract 只暴露一个很小的 `IPuertsEditorModule::SetCmdImpl()`，而目录监听能力则通过单独的 `UPEDirectoryWatcher` public service 暴露。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PuertsEditorModule.h:17`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:17` | 把“模块接口”和“具体能力对象”拆开，public surface 可以保持很窄，同时仍然允许按能力暴露扩展点。 |
| UnrealCSharp | `Build.cs` 只把少量基础 editor 依赖放进 public，`ToolMenus`、`Slate`、`ContentBrowserData` 等 UI 集成依赖都留在 private；public 模块头则显式表达自己拥有 `FEditorListener`、toolbar 和 `UDynamicDataSource`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55` | public 头更适合暴露“谁拥有哪些 editor service”，而不是一串具体 popup/generator helper。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先引入一个窄 public interface，再逐步把 static helper 和重型 public dependency 收回实现层，形成真正可扩展的 editor service boundary。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorModule` 或等价 public interface，只保留稳定的 service getter，例如 `GetAssetWorkflowService()`、`GetBlueprintImpactService()`、`GetWatchService()`、`GetNavigationService()`；不在 interface 上暴露 popup 细节。<br>2. 把 `FAngelscriptEditorModule.h` 里的 `ShowAssetListPopup()`、`ShowCreateBlueprintPopup()`、`GenerateBuildFile()`、`GenerateSourceFilesV2()` 等 legacy static helper 迁到 `Private`，或拆到单独的 `AngelscriptEditorLegacyTools` / `AngelscriptCodeGenEditor` 模块；第一阶段保留 deprecated forwarding wrapper，避免外部调用方立即断裂。<br>3. 在第二阶段收窄 `AngelscriptEditor.Build.cs` 的 public 依赖：优先把 `Slate`、`SlateCore`、`AssetTools`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher` 收回 private；仅保留 public 头真正需要的最小集合。<br>4. 让 `UScriptEditorSubsystem` 和 `UEditorSubsystemLibrary` 不再只是 generic shell，而是能从新 interface 或 service container 上拿到能力对象；这样项目方扩展 editor 功能时就不必 include 模块 private-style helper。<br>5. 待调用方迁移完成后，再评估是否把 `BlueprintImpact`、asset workflow、codegen 彻底拆分成独立 editor 子模块，进一步降低单模块维护半径。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/IAngelscriptEditorModule.h`。 |
| 预估工作量 | M |
| 架构风险 | 真正收窄 public dependency 时，任何依赖 transitive module leak 的下游模块都可能暴露出缺失的 `Build.cs` 声明；因此必须先引入新 interface 和兼容 wrapper，再做依赖收口。 |
| 兼容性 | 可增量实施。第一阶段只新增 `IAngelscriptEditorModule` 并保留旧 static helper；旧调用方继续工作。第二阶段收窄 public dependency 时，可能需要少量下游模块补显式依赖，但这属于构建层修正，不改变脚本行为。 |
| 验证方式 | 1. 新增一个最小外部 editor 模块，只通过 `IAngelscriptEditorModule` 获取 service 并调用，确认无需 include `Slate`/`AssetTools` 相关 helper 头。<br>2. 在保留旧 static helper 的阶段，确认旧调用点仍能编译并正确转发到新 service。<br>3. 收窄 public dependency 后全量编译工程，检查是否只剩真正依赖这些 editor 框架的模块需要显式补 `Build.cs`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-16 | `BlueprintImpact` 仍停留在 scanner/commandlet/reload helper，缺少统一 automation surface | service 提炼 + 入口收敛 | 高 |
| P2 | Arch-17 | public 边界泄露重型依赖与 static helper，缺少窄 public contract | 边界收口 | 中高 |

---

## 架构分析 (2026-04-08 16:27:52)

### Arch-18：editor feature bootstrap 缺少显式 phase coordinator，ready 条件分散在多条一次性回调

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编辑器特性的启动编排是否具备明确 phase、幂等初始化和 late-load/module reload 适应性 |
| 当前设计 | `AngelscriptEditor` 目前把不同 feature 的 ready 条件分散在 `StartupModule()` 立即执行、`OnPostEngineInit` 静态回调、`UToolMenus` startup callback，以及 `UScriptEditorMenuExtension` 的“引擎已完成初编译 or full reload”两条路径上；模块内没有统一的 bootstrap coordinator 去声明“哪些特性依赖 engine ready、哪些依赖 ToolMenus ready、哪些依赖 script compile ready”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` — `StartupModule()` 里立即执行 `FClassReloadHelper::Init()` 与 `RegisterAngelscriptSourceNavigation()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:361` — `ContentBrowserDataSource` 不是立即初始化，而是仅通过 `FCoreDelegates::OnPostEngineInit.AddStatic(&OnEngineInitDone)` 延后。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` — `OnEngineInitDone()` 自己 `NewObject<UAngelscriptContentBrowserDataSource>` 并 `ActivateDataSource("AngelscriptData")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `Tools` 菜单又走 `UToolMenus::RegisterStartupCallback(...)` 这条单独 phase。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25` — `InitializeExtensions()` 还额外绑定 `OnPostReload` 与 `OnEnginePreExit`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:41` — 菜单扩展只有在 `FAngelscriptEngine::IsInitialized()` 且初编译完成时才立即 `RegisterExtensions()`。 |
| 优点 | 每个 feature 都能按自己最短路径接线，早期实现成本低；对“编辑器启动一次、模块不热插拔”的主路径足够直接。 |
| 不足 | 推断：当前没有一层显式的 phase model，导致 feature ready 语义只能从多个 callback 之间反推。这样一来，模块若在 `OnPostEngineInit` 之后才被重新装载，`ContentBrowserDataSource` 这类只依赖一次性 engine callback 的特性可能不会自动补注册；同时项目方也拿不到“editor feature 已经全部 ready”的正式时机，难以通过 `EditorSubsystem` 安全追加扩展。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `StartupModule()` 先创建 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar`，`OnPostEngineInit()` 再统一 `Initialize()`，最后 `OnMainFrameCreationFinished()` 才补快捷键映射，形成清晰的 staged bootstrap。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:98`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:103` | 不是把所有接线都塞进模块入口，而是显式分成 startup、engine ready、main frame ready 三段。 |
| puerts | `FPuertsEditorModule::StartupModule()` 在完成基础成员构造后直接调用 `OnPostEngineInit()`，随后由该函数一次性注册 `Blueprint` compiler、`SourceFileWatcher` 和 editor `JsEnv`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:107`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122` | 至少把“editor 内真正的 ready 主链”收敛到一个函数里，而不是由多个静态 callback 共同拼接。 |
| UnrealCSharp | 模块成员显式持有 `OnPostEngineInitDelegateHandle`、toolbar 和 `TStrongObjectPtr<UDynamicDataSource>`；`StartupModule()` 先构造对象并 `Initialize()` 数据源，再用 first tick 激活 `ContentBrowserDataSource`，`OnPostEngineInit()` 只负责菜单/toolbar 初始化。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:24`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:39`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:51`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:104`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:108`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:173`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:188`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:193` | 把 phase 分成“对象创建”“engine ready 菜单”“first tick 激活数据源”，每段由明确成员和函数承接，模块 reload 时也更容易补齐缺失阶段。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入 `editor bootstrap coordinator`，把现有散落的 callback 收敛成少量幂等 phase，并为 `EditorSubsystem` 提供可订阅的 ready contract。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 内新增 `FAngelscriptEditorBootstrapper` 或等价对象，显式拆成 `EnsureCoreFeatures()`、`EnsureEngineReadyFeatures()`、`EnsureToolMenusReadyFeatures()`、`EnsureScriptReadyFeatures()` 四段；每段必须幂等，可重复调用。<br>2. 把 `ContentBrowserDataSource`、`Tools` 菜单、`UScriptEditorMenuExtension::RegisterExtensions()` 分别迁到对应 phase 中；`StartupModule()` 只负责创建 bootstrapper、注册 phase 触发器并在当前条件已满足时立即补调一次。<br>3. 为 `OnPostEngineInit`、`UToolMenus` callback、`FAngelscriptClassGenerator::OnPostReload` 统一加“已经完成则跳过，未完成则补齐”的状态位，避免依赖一次性回调自然到达。<br>4. 通过 `UScriptEditorSubsystem` 或新的 `EditorServices` 容器暴露 `OnEditorFeaturesReady` / `OnScriptFeaturesReady` 事件，让项目方扩展不必再猜测初始化窗口。<br>5. 第一阶段不改变现有 feature 本身，只重排 bootstrap 路径；第二阶段再根据 phase 边界决定哪些 feature 继续留在模块、哪些迁入独立 service object。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Bootstrap/AngelscriptEditorBootstrapper.*`。 |
| 预估工作量 | M |
| 架构风险 | phase 边界如果切错，容易把本来依赖 `engine/script compile` 顺序的 feature 提前触发；尤其 `ContentBrowserDataSource` 和 menu extension 的初始化时机必须与 `FAngelscriptEngine` 的初编译状态对齐。 |
| 兼容性 | 向后兼容。第一阶段只是把现有 callback 路径统一转发到 bootstrapper，并保留旧 feature 的外部行为；现有脚本扩展、popup、reload 流程不需要改。 |
| 验证方式 | 1. 正常启动编辑器，确认 `AngelscriptData`、`Tools` 菜单、脚本菜单扩展都与当前行为一致。<br>2. 在模块重载或脚本 full reload 后，确认 bootstrapper 不会重复注册，但能补齐此前缺失的 phase。<br>3. 新建一个 `UScriptEditorSubsystem` 子类订阅 `OnEditorFeaturesReady`，确认项目方不需要依赖隐式启动顺序也能安全注册扩展。 |

### Arch-19：脚本 editor 扩展发现/注册没有 deterministic governance，`Function` 有 `SortOrder`，`Extension` 却没有

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多个脚本扩展同时命中同一 editor surface 时，是否存在稳定的注册顺序、冲突治理和项目级启停控制 |
| 当前设计 | `UScriptEditorMenuExtension` 的注册是“遍历所有 `UASClass`，过滤后立即挂到对应 surface”；系统只给单个 `UFunction` 提供了 `SortOrder` 元数据，但对“扩展类之间谁先注册、同一 `ToolMenu` section 如何排序、项目方如何禁用某个扩展”没有正式 contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:47` — `GetRegisteredExtensionSnapshots()` 只暴露 `Location`、`ExtensionPoint`、`SectionName`，没有 `Priority`、`OwnerId`、`Enabled` 等治理信息。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:427` — `SortOrder` 结构与比较逻辑只存在于 `FSortedFunction`，作用域是同一扩展类内部的 function 列表。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:459` — `SortFunctionsByCategory()` 只读取 `UFunction` 的 `Category` / `SortOrder`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:845` — `RegisterExtensions()` 直接 `for (TObjectIterator<UASClass> ScriptClass; ...)` 全量扫描脚本类。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:851` — 过滤条件仅有 `IsChildOf/Abstract/NewerVersionExists/ScriptTypePtr`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1094` — `ToolMenu` section 名直接取 `CDO->GetClass()->GetFName()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1116` — 注册完成后只是把结果推入 `RegisteredExtensions`，没有显式排序或冲突解析阶段。 |
| 优点 | 零配置、低门槛，脚本类一旦存在就能自动扩展编辑器；对单项目、少量扩展的场景非常省心。 |
| 不足 | 推断：当前 contract 对“多扩展共存”几乎没有治理面。多个扩展类落到同一 surface 时，最终顺序更多取决于 `TObjectIterator<UASClass>` 的遍历副作用和 `ToolMenu` section 名，而不是明确的项目策略；项目方也无法通过 `EditorSubsystem` 或 setting 只禁用某个扩展、提升某个扩展优先级，后续一旦扩展数量增长，editor 行为会变得难以预测和审计。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FMainMenuToolbar` 在构造时把 `HotReload`、`GenerateIntelliSense`、`OpenRuntimeSettings`、`OpenEditorSettings` 等动作映射到单一 `CommandList`，`Initialize()` 再明确写入 `UnluaSettings` section。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:32`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:39`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:45`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:85`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:86` | 动作和 surface owner 都是显式对象，顺序和 section 归属由 native toolbar owner 决定，不依赖全局类扫描。 |
| puerts | `DeclarationGenerator` 把 editor 工具收敛到模块成员 `PluginCommands`，先 `MapAction()` 到单个 `PluginAction`，再在 `RegisterMenus()` 中按固定顺序接到主菜单和 toolbar。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1569`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1579`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1593`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1648`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1650` | 先建立统一的 action owner，再显式决定投影到哪些 surface；治理面在模块和 command 层，而不是 UObject 遍历顺序。 |
| UnrealCSharp | `FUnrealCSharpEditorCommands` 用 `TCommands` 声明固定 command 集，模块再显式持有 `PlayToolBar`、`BlueprintToolBar` 与 `DynamicDataSource`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditorCommands.h:9`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditorCommands.h:25`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditorCommands.h:33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditorCommands.cpp:7`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditorCommands.cpp:9`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:63` | command identity、surface owner、feature object 三层是分开的，因此项目更容易讨论“哪个动作存在、挂在哪里、谁拥有它”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `UScriptEditorMenuExtension` 易用性的前提下，引入一层 `extension descriptor + registry`，把注册顺序、启停策略和冲突治理显式化。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorExtensionDescriptor`，最少包含 `ExtensionId`、`OwnerClassPath`、`SurfaceId`、`Priority`、`SectionName`、`bEnabledByDefault`、`RequiredContextTypes`；默认 `ExtensionId` 可用脚本类路径生成。<br>2. 让现有 `UScriptEditorMenuExtension` 在 native 层自动导出 descriptor：若脚本未声明新元数据，则兼容填充 `Priority = 0`、`SectionName = ClassName`；现有 `UFunction::SortOrder` 继续只负责类内 function 顺序。<br>3. 新增 `UAngelscriptEditorExtensionRegistrySubsystem` 或等价 registry service，统一收集 descriptor，再按 `SurfaceId -> Priority -> SectionName -> OwnerClassPath` 做稳定排序后注册，而不是在 `TObjectIterator<UASClass>` 循环里边发现边挂接。<br>4. 通过 `EditorSubsystem`/setting 暴露项目级 policy：允许禁用某个 `ExtensionId`、重写其 `Priority`，或为特定 surface 增加白名单/黑名单；这一步不改变扩展实现，只增加治理面。<br>5. 扩展 `GetRegisteredExtensionSnapshots()`，让它返回 `ExtensionId/Priority/Enabled/SurfaceId` 等结构化快照，便于调试、审计和自动化测试。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/ExtensionRegistry/AngelscriptEditorExtensionDescriptor.*`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/ExtensionRegistry/AngelscriptEditorExtensionRegistrySubsystem.*` 与对应 `Private/*`。 |
| 预估工作量 | M |
| 架构风险 | registry 设计如果过重，会把原本简洁的脚本扩展写法变成配置负担；因此第一阶段必须保持“无新元数据也能工作”，只是在 native 层补排序和 policy。 |
| 兼容性 | 向后兼容。现有 `UScriptEditorMenuExtension` 子类继续自动生效，默认 `Priority=0` 时行为尽量接近当前实现；只有项目主动配置 policy 时，注册顺序和启停才发生受控变化。 |
| 验证方式 | 1. 写两个命中同一 `ToolMenu` 的脚本扩展，验证 registry 能稳定按 `Priority` 排序，而不是受 `TObjectIterator` 顺序影响。<br>2. 在 project setting 中禁用其中一个 `ExtensionId`，确认无需删脚本类也不会注册对应菜单。<br>3. full reload 后再次抓取 registry snapshot，确认 `ExtensionId`、`Priority`、`SectionName` 和启停状态保持稳定。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-18 | editor feature bootstrap 缺少显式 phase coordinator，ready 条件分散 | 启动编排收敛 | 高 |
| P1 | Arch-19 | 脚本扩展发现/注册没有 deterministic governance，难以排序和治理 | registry + policy contract | 高 |

---

## 架构分析 (2026-04-08 16:19:44)

### Arch-20：editor session 状态仍是隐式环境，`EditorSubsystem` 拿不到正式的 `PIE/MainFrame/Activation` 协同面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 项目方是否能通过正式的 editor session contract，与 `PIE`、主窗口 ready、应用激活状态协同扩展功能 |
| 当前设计 | `AngelscriptEditor` 当前只在模块入口接入 `OnPostEngineInit`、`OnObjectPreSave` 和 `ToolMenus` startup callback；资产创建/打开路径只检查引擎初始化和初编译完成，没有显式 `PIE` / main frame / app activation 协调层。`UScriptEditorSubsystem` 也只提供 `Initialize/Deinitialize/Tick`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:359` — 模块启动时注册的是 `IGameplayTagsModule` 变化、`FCoreDelegates::OnPostEngineInit`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:412` — 另一类全局钩子是 `FCoreUObjectDelegates::OnObjectPreSave`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — UI 初始化也只是 `UToolMenus::RegisterStartupCallback(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` — `ShowCreateBlueprintPopup()` 直接进入创建流程。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:480` — 创建资产前直接弹 `CreateModalSaveAssetDialog()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:514` — 继续走 `FKismetEditorUtilities::CreateBlueprint(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:537` — 创建完成后直接 `OpenEditorForAsset(Asset)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:543` — `ShowAssetListPopup()` 只检查 `IsInitialized()` 和 `bIsInitialCompileFinished`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:33` — subsystem 初始化只触发 `BP_Initialize()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:57` — 唯一额外回调仍是 `BP_Tick(float DeltaTime)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:17` — 脚本侧只拿得到泛型 `GetEditorSubsystem()`，拿不到 session 事件。 |
| 优点 | 入口简单，当前模块不用维护额外的 session 状态机；对“启动后直接使用菜单/弹窗”的主路径实现成本低。 |
| 不足 | 推断：当前 editor 行为默认依赖“调用时环境刚好可用”。一旦项目方想在 `PIE` 前后、主窗口创建完成后、应用重新激活后安全地挂接 watcher、批量打开资产、延迟 reload 或追加自定义 UI，就只能猜测时机或直接改模块源码。更具体地说，现有创建/打开流程没有像参考插件那样把 `PIE` 视作正式 session state，因此也缺少统一的 defer/reject 策略。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `StartupModule()` 先注册 `OnPostEngineInit`，等 editor 真正 ready 后再初始化 toolbar / IntelliSense，并在 `OnMainFrameCreationFinished()` 才把 `HotReload` 映射到全局 play actions。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:103`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:107` | 把 “engine ready” 和 “main frame ready” 视为不同 session phase，而不是都塞进模块启动入口。 |
| puerts | 模块显式绑定 `PreBeginPIE` 和 `EndPIE`；真正的 `Blueprint` 资产操作又在 `UPEBlueprintAsset` 内通过 `IsInPIEMode()` 阻止布局修改与创建。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:80`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:52`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:57`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:119` | session state 不只影响启动，还应进入具体资产工作流的执行前校验。 |
| UnrealCSharp | `FEditorListener` 统一持有 `OnPostEngineInit`、`PreBeginPIE`、`PrePIEEnded`、`CancelPIE`、`OnMainFrameCreationFinished`、`OnApplicationActivationStateChanged` 和 `DirectoryWatcher` 句柄；窗口重新激活时还会基于 `bIsPIEPlaying/bIsGenerating` 决定是否延迟编译。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/Listener/FEditorListener.h:13`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/Listener/FEditorListener.h:37`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:24`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:27`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:144`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:300`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:306` | 把 session 事件收敛成一个 listener/service 后，项目方扩展和默认能力都能共享同一组状态判定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增 `editor session service`，把 `PIE`、main frame ready、应用激活和资产工作流可执行性统一成正式 contract，再通过 `EditorSubsystem` 对外暴露。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 新增 `FAngelscriptEditorSessionService` 或等价对象，第一阶段只负责绑定 `FEditorDelegates::PreBeginPIE/PrePIEEnded/CancelPIE`、`IMainFrameModule::OnMainFrameCreationFinished()`、`FSlateApplication::OnApplicationActivationStateChanged()`，并维护只读状态位，例如 `bIsPIEPlaying`、`bIsMainFrameReady`、`bIsApplicationActive`。<br>2. 把现有 `ShowCreateBlueprintPopup()`、`ShowAssetListPopup()`、后续 watch/reload 入口都先经过 `SessionService->CanRun(EAngelscriptEditorAction)`；对 `PIE` 中不安全的动作，第一阶段只做“拒绝并提示”或“排队到 `PrePIEEnded` 后执行”，避免立即大改行为。<br>3. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 暴露只读 session 事件，例如 `OnMainFrameReady`、`OnEditorSessionStateChanged`、`IsPIEPlaying()`；项目方自定义 editor subsystem 从此不必再自己绑原生 delegate。<br>4. 第二阶段再把 `DirectoryWatcher`、impact scan、future toolbar/panel 的 enable/disable 条件也迁到这层 session service，形成统一 gating，而不是每个 feature 自己猜测时机。<br>5. 若需要 `MainFrame` 能力，再把 `MainFrame` 依赖加到 `AngelscriptEditor.Build.cs` 的 `PrivateDependencyModuleNames`，保持 public 面不变。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/AngelscriptEditorSessionService.*` 与 `Private/Services/AngelscriptEditorSessionService.*`。 |
| 预估工作量 | M |
| 架构风险 | session service 如果同时承担“事件收集 + 动作排队 + UI 提示”，容易过胖；第一阶段应只先做状态收敛和可执行性判断，把具体 UI 反馈留给调用方。 |
| 兼容性 | 可增量实施。旧菜单、旧 popup、旧 debug-server 路径都可继续调用原函数；只是内部多一道 session gate。对现有脚本和资产命名规则没有破坏性影响。 |
| 验证方式 | 1. 进入 `PIE` 后触发创建 Blueprint 或打开资产请求，确认系统给出一致的 reject/defer 行为，而不是直接进入不受控 modal/asset editor。<br>2. 编写一个项目级 `UScriptEditorSubsystem` 订阅新事件，确认无需修改插件核心即可在 `MainFrameReady` 后注册自定义扩展。<br>3. 切出/切回编辑器窗口并制造脚本文件变化，确认后续接入该 service 的 feature 能在 `ApplicationActivated` 后统一恢复处理。 |

### Arch-21：editor 操作缺少统一的 task/reporting contract，扩展者只能复用零散的 modal 与局部 `SlowTask`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 内触发的生成、打开、分析、测试等操作是否拥有可复用的进度与结果反馈面，便于项目方扩展 |
| 当前设计 | 当前固定菜单动作主要是直接绑定 inline lambda；反馈手段分散在 `CreateModalSaveAssetDialog`、单点 `FMessageDialog` 和 `ShowAssetListPopup()` 内部局部 `FScopedSlowTask`，没有统一的 task runner、notification sink 或 result log contract 可供 `EditorSubsystem` / 脚本扩展复用。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:706` — `ASOpenCode` 直接在 menu action 里 `OsExecute(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722` — `ASGenerateBindings` 直接绑定 `GenerateNativeBinds()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:735` — `Function Tests` 同样直接绑定 `FunctionTests()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:471` — 创建资产流程的主要反馈仍是 `CreateModalSaveAssetDialog()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:492` — 失败路径只弹一次 `FMessageDialog::Open(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:554` — `FScopedSlowTask` 只在“打开单个资产”这条具体路径里临时创建。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:51` — subsystem 没有任何 task lifecycle 或 report API。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:17` — library 也只暴露 generic subsystem getter。 |
| 优点 | 现在的实现非常直接，单个功能接线快，不需要先建设额外的 task infrastructure。 |
| 不足 | 结果是 editor 操作的 UX 和可扩展性都高度偶然。长一些的流程没有稳定的 progress/reporting 约定，项目方如果想从 `EditorSubsystem` 发起自己的生成或分析任务，只能复制现有 modal/popup 代码或退回 `UE_LOG`。推断：随着 `BlueprintImpact`、watch service、toolbar action 继续增多，缺少统一 task/reporting contract 会让相同的“进度条、通知、失败摘要、结果复用”逻辑在各个 feature 内重复生长。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 全量生成通过 `FScopedSlowTask` 暴露可取消进度；toolbar 动作在用户输入缺失或文件不存在时会统一发 `FSlateNotificationManager` 通知，而不是只打日志。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:87`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:274`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:345` | 任务执行和用户反馈是正式 editor surface 的一部分，而不是每个动作各自决定。 |
| puerts | `DeclarationGenerator` 的 `GenUeDts()` 在生成完成后统一构造 `FNotificationInfo`，并通过 `FSlateNotificationManager` 回报结果；同一能力又由 `PluginCommands` 和 console command 共用。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1622`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1627`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1648`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1668` | “执行能力”和“反馈渠道”可以先统一，再让 menu/toolbar/console 共享。 |
| UnrealCSharp | 代码生成主链先广播 `OnBeginGenerator`，再用多阶段 `FScopedSlowTask` 驱动 `Solution / Analysis / Generator / Compile`，最后广播 `OnEndGenerator`；任务生命周期因此可被 listener 和其它 surface 复用。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:250`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:266`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:303`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:309` | task 生命周期如果被显式化，后续就能自然接 toolbar、listener、结果面板和项目自定义 automation。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入统一的 `editor task service`，把“执行动作、展示进度、产出结果、对外广播生命周期”收敛成正式 contract，再让现有菜单和未来 `EditorSubsystem` 扩展都走这层。 |
| 具体步骤 | 1. 新增 `UAngelscriptEditorTaskService` 或等价 native service，定义最小 contract：`RunTask(TaskId, DisplayText, ExecuteLambda, Options)`、`ReportInfo/ReportWarning/ReportError`、`OnTaskStarted/OnTaskFinished`；第一阶段只包当前 editor module 内已有操作，不改业务逻辑。<br>2. 把 `ASGenerateBindings`、`Function Tests`、`BlueprintImpact` 后续入口，以及创建/打开资产流程中的长操作改为先进入 task service；轻量操作可以直接完成，但失败和完成结果仍统一走 notification/report API。<br>3. 约定三类默认反馈 surface：需要阻塞和进度时用 `FScopedSlowTask`，短结果用 `FSlateNotificationManager`，结构化失败摘要用 `FMessageLog`；现有 `FMessageDialog` 只保留给真正必须 modal 的错误。<br>4. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 暴露 task service，让项目方自定义 editor subsystem、脚本菜单扩展或未来 dockable panel 能发起任务并复用统一反馈，而不是复制一套 `SWindow/SlowTask`。<br>5. 第二阶段再把 task service 与新的 command/style 层、impact service、watch service 连接起来，形成统一的 “command -> task -> result” 链。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Tasks/AngelscriptEditorTaskService.*` 与 `Private/Tasks/AngelscriptEditorTaskService.*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段试图一次性把所有 editor 流程都迁到 task service，容易把当前逻辑牵得过大；更稳妥的是先包固定菜单动作和一两个长任务，把反馈 contract 做实，再逐步吸纳其它 feature。 |
| 兼容性 | 向后兼容。现有菜单项、popup 和创建流程都可继续存在，只是内部改为调用 task/reporting service；项目已有脚本扩展不需要立即迁移。 |
| 验证方式 | 1. 触发 `ASGenerateBindings`、`Function Tests` 和一个较慢的资产打开/分析操作，确认都能通过统一入口输出进度或结果通知。<br>2. 写一个项目级 `UScriptEditorSubsystem` 调用新 task service，验证无需复制 `FScopedSlowTask` / `FMessageDialog` 代码也能得到一致反馈。<br>3. 故意制造一个失败场景，确认错误会落到统一 notification/log surface，而不是只剩 `UE_LOG` 或静默返回。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-20 | editor session 事件缺少正式 contract，`EditorSubsystem` 无法协同 `PIE/MainFrame/Activation` | session service 新增 | 高 |
| P2 | Arch-21 | editor 操作缺少统一 task/reporting contract，反馈与扩展复用都很零散 | task/reporting 收敛 | 中高 |

---

## 架构分析 (2026-04-08 16:30:29)

### Arch-22：reload 期间打开中的 editor state 仍靠 `UAssetEditorSubsystem` 引用替换补丁维持，没有正式的 toolkit/session contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | script reload 与已打开 asset editor 的状态保持、恢复与扩展接入 |
| 当前设计 | `FClassReloadHelper` 在 reload 主链里创建一个全局 `UAngelscriptReferenceReplacementHelper`，依赖 `GC + FArchive` 引用收集去替换 `UAssetEditorSubsystem` 中的旧 asset 指针，并用 `NotifyAssetClosed/NotifyAssetOpened` 重新挂接 editor 实例。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:20` — 文件头注释直接把当前路径描述为 `hacked-together mess`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:25` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:33` — `ReplaceHelper` 是全局指针，并在 `PerformReinstance()` 内 `NewObject(...); AddToRoot()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:166` — reinstance 前显式 `CollectGarbage(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:387` — `AddReferencedObjects()` 主动把 `UAssetEditorSubsystem::GetAllEditedAssets()` 收进引用集合。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:409` — `Serialize()` 针对 `ObjectReferenceCollector` 路径工作。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:419` — 注释明确写明这是为了修补 `asset editor subsystem` 中的 stale object pointer。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:431` — 通过 `FindEditorsForAsset()` 找到 editor instance。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:434` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:435` — 再调用 `NotifyAssetClosed/NotifyAssetOpened` 替换 asset 引用。 |
| 优点 | 在没有自定义 asset editor toolkit 和专用 compiler hook 的前提下，当前实现已经能尽量避免 reload 后打开中的 asset editor 持有旧对象指针，短期内对现有 Blueprint/DataAsset 工作流改动最小。 |
| 不足 | 这条链路只知道 `OriginalAsset -> ReplacedAsset`，却不知道具体 editor host 的 graph、tab、selection、toolbar state 和项目自定义扩展状态。推断：未来如果项目方通过 `EditorSubsystem` 增加自定义 asset editor surface 或附加缓存，这些状态无法通过当前 `NotifyAssetClosed/Opened` 补丁被正式接入。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把 Blueprint 相关 editor 操作放在 `FBlueprintEditorModule` / `FBlueprintEditor` 语义里：toolbar 初始化时注册 `FAssetEditorExtender`，执行动作时直接拿到 `FBlueprintEditor`，再 `Compile()`、`OpenGraphAndBringToFront()`、`RefreshEditors()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:188`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:194`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:197`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:225`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:231` | editor state 的协调点是具体 toolkit，而不是 GC 阶段的全局引用替换；这样 host context 更清楚，也更容易继续挂项目自定义行为。 |
| puerts | `FPuertsEditorModule::OnPostEngineInit()` 为 `UTypeScriptBlueprint` 注册专用 `FKismetCompilerContext`；`FTypeScriptCompilerContext::SpawnNewClass()` 在编译阶段直接寻找或创建 `UTypeScriptGeneratedClass`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:19`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:27`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:33` | 当语言桥接会改变 Blueprint/generated class 语义时，优先在 compiler pipeline 建正式接点，比在 reload 尾部修补 editor subsystem 更稳。 |
| UnrealCSharp | 一方面在 `UDynamicBlueprintExtension::HandleGenerateFunctionGraphs()` 直接参与 `FKismetCompilerContext`，另一方面 `FUnrealCSharpBlueprintToolBar::Initialize()` 显式挂到 `FBlueprintEditorModule` 的 extender，并订阅 `OnEndGenerator`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/DynamicBlueprintExtension.cpp:7`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/DynamicBlueprintExtension.cpp:9`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:23`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:25`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:38` | compiler phase 与 asset editor surface 都是正式 contract，feature 可以在生成结束后做显式刷新，而不是等 GC 替换引用。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“reload 后如何恢复打开中的 editor”从 `GC workaround` 升级为可注册的 `edited-asset session` contract，当前 helper 退为兼容 fallback。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditedAssetAdapter` 或 `FAngelscriptEditedAssetSessionService`，定义最小接口：`CanHandle(UObject*)`、`CaptureSession(UObject*, IAssetEditorInstance*)`、`RestoreSession(OldAsset, NewAsset, Snapshot)`、`RefreshAfterReload(...)`。<br>2. `FClassReloadHelper::PerformReinstance()` 在 reinstance 前先从 `UAssetEditorSubsystem` 与 `FToolkitManager` 收集打开中的 asset/session snapshot；reinstance 后优先调用 adapter 恢复，而不是直接 `NotifyAssetClosed/NotifyAssetOpened`。<br>3. 第一阶段只实现两个内建 adapter：`UBlueprint` 和 `UDataAsset`。`Blueprint` adapter 可复用 `FBlueprintEditorModule` / `FBlueprintEditor` 做 `Compile/Refresh/OpenGraph`，避免把 graph state 丢给底层 GC。<br>4. 现有 `UAngelscriptReferenceReplacementHelper` 保留为 fallback，仅处理还没有 adapter 的 asset 类型，确保旧工程不因新 contract 不完整而退化。<br>5. 通过新的 editor service 或 `UScriptEditorSubsystem` 暴露 adapter 注册入口，让项目方可以为自定义 asset editor 或自定义 dockable panel 提供恢复逻辑，而不必修改核心 reload 代码。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Reload/AngelscriptEditedAssetAdapter.*`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Reload/AngelscriptEditedAssetSessionService.*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就想覆盖所有 editor host，容易把 reload 主链拖得过重；更稳妥的是先把 `Blueprint`/`DataAsset` 这两条主路径 formalize，其它类型继续走旧 helper。 |
| 兼容性 | 向后兼容。旧的 `ReplaceHelper` 路径先保留；没有注册 adapter 的资产仍按当前方式处理。对现有脚本类和资产命名没有破坏性变化。 |
| 验证方式 | 1. 打开一个 `Blueprint` editor、一个 `DataAsset` editor，触发 script reload，确认不会出现 stale pointer、重复打开或丢失当前 asset。<br>2. 对 `Blueprint` 验证 graph/tab 恢复和 `Compile/Refresh` 路径是否正常。<br>3. 注册一个项目级自定义 adapter，确认无需改核心插件也能接入自定义 asset editor 的恢复逻辑。 |

### Arch-23：`ContentBrowserDataSource` 仍缺少可逆的路径/命名空间 contract，虚拟脚本资产只是单桶扁平映射

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产在 `ContentBrowser` 中的 identity、folder namespace 和用户可扩展性 |
| 当前设计 | `UAngelscriptContentBrowserDataSource` 当前把每个脚本资产直接映射到 `/All/Angelscript/<AssetName>`，只在根路径 `/` 编译过滤器，`EnumerateItemsAtPath()` 为空，路径反查与引用拼接也没有合同；与此同时，`ShowCreateBlueprintPopup()` 明明已经能从 `Class->GetRelativeSourceFilePath()` 推导真实默认目录，但这份层级信息没有进入 `ContentBrowser` 模型。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` — `OnEngineInitDone()` 激活 `AngelscriptData` data source。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16` — `CreateAssetItem()` 构造 payload。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:28` — 虚拟路径固定为 `/All/Angelscript/<AssetName>`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:44` — `CompileFilter()` 在 `InPath != "/"` 时直接返回。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:82` — `EnumerateItemsMatchingFilter()` 直接从 `FAngelscriptEngine::Get().AssetsPackage` 枚举全部对象。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:124` — `EnumerateItemsAtPath()` 为空实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:133` — `DoesItemPassFilter()` 直接 `return true`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:182` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:202` — `GetItemPhysicalPath()`、`AppendItemReference()` 都未实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:249` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:254` — `Legacy_TryConvert*ToVirtualPath()` 都未实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:447` — 创建资产时会读取 `Class->GetRelativeSourceFilePath()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:454` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:468` — 再用脚本相对路径推导默认 `/Game/...` 目录。 |
| 优点 | 这条实现非常轻，能快速把脚本资产显示进浏览器，而且不要求项目先建立额外的 module/folder 元数据。 |
| 不足 | 当前 identity 只有“对象名在 `/All/Angelscript` 里显示一次”，没有“它属于哪个 module / source folder / project extension”的正式 contract。推断：未来一旦脚本资产类型增多，或者项目想通过 `EditorSubsystem` 添加自己的脚本资产根、子分类或 `AddNew` 工作流，现有单桶模型会迫使所有扩展继续堆在同一个虚拟根下。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 不构造一个匿名虚拟脚本资产桶，而是把 editor 操作直接挂在真实 `UBlueprint` asset editor 上；`FBlueprintToolbar::Initialize()` 注册的是基于 `ContextSensitiveObjects` 的 `FAssetEditorExtender`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:25` | 如果没有准备好完整的虚拟资产 namespace，就优先站在真实 asset identity 上扩展，而不是先造一个不可逆的虚拟路径。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 直接在真实 `PackageName` 下 `CreatePackage()` 并创建 `UTypeScriptBlueprint`，随后 `AssetRegistryModule::AssetCreated(Blueprint)`；同模块又把 `UTypeScriptBlueprint` 注册到专用 compiler。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:144`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:147`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120` | 另一条路径是直接给脚本资产一个真实 package-backed identity；这样路径、编译和资产管理天然一致。 |
| UnrealCSharp | `UDynamicDataSource` 把虚拟路径树、folder/file 枚举、路径转换、引用拼接和 `ContentBrowser.AddNewContextMenu` 作为同一个 data source contract 的组成部分来实现。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:71`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:369`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:409`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:592`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:666`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:745`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:788`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:824` | 如果选择虚拟 data source 路线，就必须把 namespace、path conversion、AddNew context 和 item refresh 一起做成正式 contract，而不是只实现“显示一个 file item”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptData` 从“扁平展示桶”升级为“可逆的虚拟路径树”，并给项目方留出注册额外 root/category 的扩展面。 |
| 具体步骤 | 1. 为 `UAngelscriptContentBrowserDataSource` 增加内部 path model，例如 `ScriptRoot / Module / RelativeFolder / AssetName`；第一阶段直接复用 `UASClass::GetRelativeSourceFilePath()` 或等价脚本相对路径，避免重新设计一套新元数据。<br>2. 基于这个 path model 落实 `CreateFolderItem()`、`EnumerateItemsAtPath()`、`DoesItemPassFilter()`、`Legacy_TryConvertPackagePathToVirtualPath()`、`Legacy_TryConvertAssetDataToVirtualPath()` 和 `AppendItemReference()`，保证每个虚拟项都能从浏览器路径回到内部 identity。<br>3. 把 `ShowCreateBlueprintPopup()` 的默认目录推导逻辑迁到同一个 path service；当用户在 `ContentBrowser` 选择某个虚拟 folder 时，新建资产应直接继承该 path context，而不是重新猜目录。<br>4. 新增轻量 `IAngelscriptVirtualRootProvider` 或等价 registry，让项目方通过 `EditorSubsystem`/native service 注册额外脚本根、分类策略或显示名，避免未来所有自定义资产都挤进 `/All/Angelscript`。<br>5. 兼容阶段保留旧的 `/All/Angelscript/<AssetName>` 作为别名视图或 fallback root；等路径转换与引用复制稳定后，再逐步把主入口切到新 tree。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/ContentBrowser/AngelscriptVirtualPathRegistry.*` 与对应 `Private/*`。 |
| 预估工作量 | M |
| 架构风险 | 路径模型一旦设计过窄，后续增加 plugin script root、generated asset 或项目自定义分类时还会再次重写；因此第一阶段至少要把 `Module/Folder/Asset` 三层保留下来。 |
| 兼容性 | 可增量实施。现有虚拟根和现有 `ShowCreateBlueprintPopup()` 可继续工作；新 tree 先作为更丰富的呈现和创建上下文，不会破坏现有脚本类或已有资产。 |
| 验证方式 | 1. 在 `ContentBrowser` 中验证脚本资产能按 folder/module 层级展开，而不是全部落在单层根下。<br>2. 在某个虚拟 folder 上触发新建 Blueprint/DataAsset，确认默认保存目录与该 folder 的内部 path 一致。<br>3. 复制引用、按路径搜索、从 `AssetData` 回到虚拟路径都应成功。<br>4. 通过项目级 provider 注册一个额外脚本根，确认无需修改核心 data source 就能出现在浏览器树中。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-22 | reload 期间打开中的 asset editor 仍靠 `UAssetEditorSubsystem` 引用替换补丁维持 | reload/session contract 收敛 | 高 |
| P2 | Arch-23 | `ContentBrowser` 虚拟脚本资产缺少可逆路径与 namespace contract | 虚拟路径模型升级 | 中高 |

---

## 架构分析 (2026-04-08 16:43:41)

### Arch-24：脚本 editor action 的状态查询与执行分裂在不同对象生命周期上，无法成长为会话级扩展实例

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor action 是否由稳定 owner 持有，从而支持状态共享、会话缓存与 `EditorSubsystem` 协同 |
| 当前设计 | `UScriptEditorMenuExtension` 在构建 action 时，用原始扩展对象处理 `CanExecute/IsVisible/IsChecked`，但真正执行时又临时 `NewObject` 一个 `TempObject`；选择集和 `ToolMenuContext` 只通过 `TGuardValue` 临时压栈，没有稳定的 action instance。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:104` — `CurrentSelection` 只是扩展对象上的临时可变字段。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:143` — `CallFunctionOnSelection()` 统一转到 `FScriptEditorPrompts::ShowPromptToCallFunction(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:257` — `CreateToolUIAction()` 的 `ExecuteAction` 使用 `NewObject<UScriptEditorMenuExtension>(..., Extension->GetClass())` 创建临时对象。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:272` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:275` — `ToolMenuContext` 与 `CurrentSelection` 只在调用期间临时注入到 `TempObject`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:287`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:309`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:331` — `CanExecute/IsVisible/IsChecked` 仍在原始 `Extension` 对象上 `ProcessEvent`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:367` — 普通 `FUIAction` 的执行路径同样重新创建临时对象。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:33` — `UScriptEditorSubsystem` 只有生命周期与 tick，没有 action host / service registry。 |
| 优点 | 现有实现很轻，不需要维护长生命周期对象；脚本 action 几乎可以“零样板”挂到 editor menu/toolbar。 |
| 不足 | action 的“可执行状态”“可见性”“勾选态”和“真正执行逻辑”不在同一个实例上，导致脚本扩展难以安全保存会话状态、缓存 host context、订阅 editor 事件或与 `EditorSubsystem` 共享 service。推断：一旦项目方想做 stateful toggle、分步骤向导或依赖 editor session 的 action，就会落入 `CDO + TempObject` 双生命周期的不一致区。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaEditorToolbar` 在对象构造时创建 `CommandList`，`BuildToolbar()` 把真实 `ContextObject` 写回同一个 toolbar 对象，后续 `BindToLua_Executed()` 等动作也读取这个同一实例上的状态；`FBlueprintToolbar` 再把 `UBlueprint` context 传入 `GetExtender(TargetObject)`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:26`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:37`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:59`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:127`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:138`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23` | 查询状态与执行都依附同一个 toolbar owner，context 不是临时堆栈变量，而是该 owner 的稳定成员。 |
| puerts | `FDeclarationGenerator` 把 `PluginCommands` 和 `ConsoleCommand` 作为模块成员保存，`StartupModule()` 中 `MapAction(...)` 后同时复用到 `ToolMenus` 和 console command；没有“每次点击重新 new 一个扩展对象”的执行模型。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1569`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1648`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1650`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1668` | editor action 首先是“被某个长期存在的 feature owner 持有”，入口只是这个 owner 的投影。 |
| UnrealCSharp | `FUnrealCSharpBlueprintToolBar` 构造时创建 `CommandList`，`Initialize()` 注册 `FAssetEditorExtender` 与 `OnEndGenerator` delegate；`GenerateBlueprintExtender()` 把 `UBlueprint` 写入同一个 toolbar 对象的 `Blueprint` 状态，再由 `BuildAction()` 中的命令处理函数复用。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:15`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:57`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:170` | 如果 action 需要跟随 editor session 演化，应该先有稳定 host instance，再谈脚本化或 UI 投影。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把脚本 action 从“CDO 查询 + 临时对象执行”升级为“稳定 action host 查询与执行共用同一实例”，并把该 host 暴露给 `UScriptEditorSubsystem`。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorActionHost` 或 `UAngelscriptEditorActionHost`，最少定义 `BindAction(...)`、`Execute(...)`、`CanExecute(...)`、`IsVisible(...)`、`IsChecked(...)`、`UpdateContext(...)`；host 负责保存 `Selection`、`ToolMenuContext`、可选 `Toolkit`/`EditorSession` 弱引用。<br>2. 在 `UScriptEditorMenuExtension::RegisterExtensions()` 时，不再直接把 `UFunction` 绑定到 `CDO` 与 `TempObject`；改为为每个已注册扩展创建一个长期存在的 host，并由 `FUIAction`/`FToolUIAction` 的查询与执行统一转发到同一个 host。<br>3. 第一阶段保留现有脚本函数元数据和 `ShowPromptToCallFunction()` 行为，但 prompt 调用由 host 触发；这样旧脚本无需改写，先只修正生命周期。<br>4. 在 `UScriptEditorSubsystem` 上增加只读的 action/service 访问入口，例如 `GetActionHost(FName ExtensionId)` 或 `RegisterNativeEditorActionHost(...)`，让项目级 subsystem 能给脚本 action 提供缓存、session gating、notification 或外部 service。<br>5. 第二阶段再允许脚本扩展声明“stateful host class”或“native host adapter”，把 toggle、wizard、长任务 action 逐步迁移到真正的会话实例上。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Actions/AngelscriptEditorActionHost.*` 与对应 `Private/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段试图一次性把所有 script extension 都改成自定义 host，容易把现有元数据路径和 reload 注册路径一并打乱；更稳妥的是先做“默认 host 包装器”，仅替换生命周期，不改脚本 authoring model。 |
| 兼容性 | 可增量实施。旧的 `UScriptEditorMenuExtension` 类、`CallInEditor` 函数和现有元数据都可保留；只是内部从 `TempObject` 模式切到默认 host 包装器。对现有脚本扩展应保持源级兼容。 |
| 验证方式 | 1. 写一个带本地状态的 toggle action，验证 `CanExecute/IsChecked/Execute` 在一次 editor session 中共享同一份状态。<br>2. 让一个项目级 `UScriptEditorSubsystem` 向 action host 注入只读 service，验证脚本扩展不改核心源码也能消费该 service。<br>3. full reload 后确认 host 不重复注册，且旧菜单/toolbar action 仍能正常触发。 |

### Arch-25：`ContentBrowser` 虚拟 item payload 仍以瞬时 `UObject` 为中心，reload 后缺少稳定 identity 回桥

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ContentBrowser` item 在脚本 reload / reinstance 后是否仍有稳定 identity，能否继续提供属性、缩略图与对象回桥 |
| 当前设计 | `UAngelscriptContentBrowserDataSource` 的 payload 只保存 `Asset->GetPathName()` 和一个 `TWeakObjectPtr<UObject>`；属性、缩略图和 `Legacy_TryGetAssetData()` 都依赖当前 live object。若浏览器缓存的 item 遇到脚本对象被替换，数据源没有稳定 item id 或 resolver 去重新找回新对象。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:10` — `FAngelscriptContentBrowserPayload` 只有 `Path` 与 `TWeakObjectPtr<UObject> Asset`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:18` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:19` — payload 初始化时把 identity 简化为 `GetPathName()` 与 live object 指针。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:142` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:144` — `GetItemAttribute()` 一旦弱引用失效就直接返回 `false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:212` — `UpdateThumbnail()` 每次都从当前弱引用重新构造 `FAssetData`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:241` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:242` — `Legacy_TryGetAssetData()` 同样依赖当前 live object。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:128` — `EnumerateItemsForObjects()` 仍然直接 `return false`，说明当前没有“对象 -> 现有 item identity”的反向恢复通道。 |
| 优点 | 实现非常直接，只要 `AssetsPackage` 中对象还活着，就能快速构造 item 与缩略图，不必维护额外缓存。 |
| 不足 | 这套 payload 更像“浏览器里的 live object 句柄”，而不是“可跨 reload 生存的 item identity”。推断：当脚本对象被重建、替换或短暂失效时，浏览器缓存项会失去属性与缩略图读取基础；未来若要支持更多虚拟资产操作、结果面板或跨 surface 复用，这种 object-centric payload 会持续放大不稳定性。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 不再引入一层虚拟 `ContentBrowser` item payload，而是直接把操作挂在真实 `UBlueprint` asset editor context 上；`FBlueprintToolbar` 从 `ContextSensitiveObjects` 拿到真实 `UBlueprint`，`FUnLuaEditorToolbar` 也把 `InContextObject` 持有为成员。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:28`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:59` | 如果 editor feature 可以依附真实 asset identity，就应尽量避免再造一层易失的 shadow item。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 直接在真实 `PackageName` 下 `CreatePackage()` 并创建 `UTypeScriptBlueprint`，随后 `FAssetRegistryModule::AssetCreated(Blueprint)`；`FPuertsEditorModule` 再为该真实 Blueprint 类型注册专用 compiler。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:144`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:147`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120` | 真实 package-backed asset identity 天然比弱 `UObject` payload 更稳，路径、编译与编辑器表面可以共享同一个身份源。 |
| UnrealCSharp | `FDynamicFileItemDataPayload` 除了 `TWeakObjectPtr<UClass>`，还缓存 `InternalPath` 与 `FAssetData`；`UpdateThumbnail()` 直接使用缓存的 `AssetData`，而 `UDynamicDataSource::OnEndGenerator()` 会重建 hierarchy 并 `NotifyItemDataRefreshed()`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ContentBrowser/DynamicFileItemDataPayload.h:6`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ContentBrowser/DynamicFileItemDataPayload.h:13`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ContentBrowser/DynamicFileItemDataPayload.h:17`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicFileItemDataPayload.cpp:4`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicFileItemDataPayload.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicFileItemDataPayload.cpp:39`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:734`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:745`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:748` | 即便仍然使用弱引用，item payload 也应该首先保存稳定 identity 与可降级的显示数据，而不是把全部功能绑定在 live object 是否还活着上。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptData` 的 item payload 从 object-centric 结构升级为 identity-centric 结构：先保存稳定 key 和可降级显示数据，再按需解析 live object。 |
| 具体步骤 | 1. 把 `FAngelscriptContentBrowserPayload` 改成显式 item identity 结构，第一阶段至少保存 `InternalObjectPath`、`VirtualPath`、`DisplayName`、`FAssetData AssetDataSnapshot`，并保留弱 `UObject` 仅作为加速缓存。<br>2. 新增 `ResolveAssetFromPayload(...)` 或等价 helper：优先使用弱引用，失效时再根据 `InternalObjectPath` 或脚本内部 identity 到 `FAngelscriptEngine::Get().AssetsPackage` 中重新解析 live object；`GetItemAttribute()`、`UpdateThumbnail()`、`Legacy_TryGetAssetData()` 都通过这层访问。<br>3. 补上 `EnumerateItemsForObjects()`，使当前 live object 能稳定回到既有 item identity；这样 reload/reinstance 后浏览器与其它 surface 才能共享同一 item key。<br>4. 在 full reload 完成点上显式触发 `NotifyItemDataRefreshed()` 或等价 item update，让旧 payload 快照在合适时机整体刷新，而不是等某个 surface 偶然重新枚举。<br>5. 兼容阶段保持现有 `/All/Angelscript/<AssetName>` 虚拟路径与现有菜单行为不变；第二阶段再把更稳定的 script-relative key 接到路径与引用合同里。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；可新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ContentBrowser/AngelscriptContentBrowserItemIdentity.*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段只沿用 `GetPathName()` 作为唯一 key，脚本类重命名或未来的虚拟 folder 重映射仍可能让 resolver 不稳；因此内部结构至少应预留从“对象路径”迁移到“脚本相对路径/模块 key”的空间。 |
| 兼容性 | 可增量实施。payload 结构是内部实现细节，现有虚拟路径、现有菜单入口和现有脚本资产展示都可保持不变；外部脚本用户不应感知破坏性变化。 |
| 验证方式 | 1. 保持 `ContentBrowser` 打开状态触发一次 full reload，确认已有 item 的类型显示与缩略图不会因弱引用失效而变空。<br>2. 通过 `EnumerateItemsForObjects()` 或等价回桥验证：reinstance 后的新对象仍能映射回同一虚拟 item。<br>3. 手工重命名/替换一个脚本资产对象，确认刷新后旧 item 不残留、引用复制与属性读取仍可工作。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-24 | 脚本 action 的查询与执行分裂在不同对象生命周期上，无法形成会话级扩展实例 | action host 重构 | 高 |
| P2 | Arch-25 | `ContentBrowser` 虚拟 item payload 缺少稳定 identity，reload 后难以可靠回桥 | item identity 收敛 | 中高 |

---

## 架构分析 (2026-04-08 16:55:28)

### Arch-26：`PropertyEditor` 仍被当作瞬时表单渲染器使用，没有形成可注册的 detail customization contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 扩展者能否通过正式注册面，为设置、prompt 和后续面板补充 `PropertyEditor` customization，而不是只能复用通用表单 |
| 当前设计 | `AngelscriptEditor` 虽然依赖了 `PropertyEditor`，但当前读取到的实际接法只有两类：一是 `ScriptEditorPrompts` 在调用时临时创建 `IStructureDetailsView`；二是 reload 结束后粗粒度地 `NotifyCustomizationModuleChanged()` 强刷 UI。模块中没有 `RegisterCustomPropertyTypeLayout()` / `RegisterCustomClassLayout()` 级别的正式注册器，也没有任何可经由 `EditorSubsystem` 暴露给项目方的 customization handle。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:35` — editor 模块私有依赖了 `PropertyEditor`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:53` — prompt 运行时临时加载 `FPropertyEditorModule`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:54` — 仅通过 `CreateStructureDetailView(...)` 生成通用 `IStructureDetailsView`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:57` — 对 details UI 的唯一定制是 `SetIsPropertyVisibleDelegate(...)` 做字段显隐。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:164` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:178` — details view 最终被包进一次性的 modal `SWindow`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:333` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:338` — reload 结束后对 `PropertyEditor` 的交互仅是 `NotifyCustomizationModuleChanged()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:47` — `UScriptEditorSubsystem` 没有暴露任何 detail/property customization 注册 API。 |
| 优点 | 现有方案非常通用，任何反射可见的 `USTRUCT/UFunction` 参数都能快速长出一个可工作的表单，不需要先建设专门的 customization 体系。 |
| 不足 | `PropertyEditor` 目前只是“把结构体画出来”的通用渲染器，还不是 editor contract 的一部分。推断：当项目方需要目录/包路径选择器、脚本根过滤器、命名规则校验、结果面板内的只读详情，或 editor settings 的专门字段 UI 时，只能继续在各处手写局部 Slate，而不能通过统一注册面增量扩展。更关键的是，reload 后的全局 `NotifyCustomizationModuleChanged()` 只是刷新，不携带“哪类 customization 生效、谁拥有它”的生命周期信息。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 模块启动时显式向 `FPropertyEditorModule` 注册 `GameContentDirectoryPath` 与 `ProjectDirectoryPath` 的 property type customization，关闭时对称注销；具体定制类 `FDirectoryPathCustomization` 在 header/value row 中加入浏览按钮、路径有效性校验与写回逻辑。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:51`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:59`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:138`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:142`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:144`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/DetailCustomization/DirectoryPathCustomization.cpp:19`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/DetailCustomization/DirectoryPathCustomization.cpp:81`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/DetailCustomization/DirectoryPathCustomization.cpp:142` | 先把 customization 当作正式模块资源注册/注销，再让具体 widget 负责选择器与校验；这样 `PropertyEditor` 才能成为稳定扩展面，而不是一次性 helper。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `AngelscriptEditor` 增加模块级 `PropertyEditor` registrar，把当前“瞬时 detail view”升级为“可注册、可回收、可被项目扩展”的 customization surface。 |
| 具体步骤 | 1. 新增 `FAngelscriptPropertyEditorRegistrar` 或等价 feature 对象，由它统一持有 `FPropertyEditorModule` 访问、已注册 type/class customization 列表和 shutdown 时的反注册逻辑；第一阶段只迁移 native 内置 customization，不碰脚本侧 authoring。<br>2. 选一个高收益且风险低的类型先落地，例如 `DirectoryPath`/`PackagePath`/script root 配置结构；把当前散落在 prompt 或设置里的路径输入逻辑提炼成 `IPropertyTypeCustomization`，避免继续在各处手写按钮和校验。<br>3. 通过 `IAngelscriptEditorModule` 或 `UScriptEditorSubsystem` 新增只读访问入口，例如 `GetPropertyEditorRegistrar()` / `RegisterNativePropertyTypeCustomization(...)`；项目级 editor 扩展由此可追加自己的 customization，而不是改核心模块。<br>4. 把 reload 结束时的 `NotifyCustomizationModuleChanged()` 收口到 registrar 内部，并区分“刷新现有 customization”和“注册表变更后刷新”两种场景，避免未来 feature 各自直接碰 `FPropertyEditorModule`。<br>5. 兼容阶段继续保留 `FScriptEditorPrompts::ShowPromptForStruct()` 的通用 details fallback；如果某个类型没有专门 customization，行为与今天保持一致。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Details/AngelscriptPropertyEditorRegistrar.*` 与 `Private/Details/*Customization.*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就开放“任意脚本动态注册 customization”，容易把 `PropertyEditor` 生命周期和脚本 reload 生命周期直接耦合；更稳妥的是先只支持 native 注册器，再逐步把脚本侧能力建立在稳定 handle 之上。 |
| 兼容性 | 可增量实施。旧 prompt、旧 reload 刷新行为和现有脚本调用都可保留；新增 customization 仅影响命中的字段类型，不会破坏未迁移表单。 |
| 验证方式 | 1. 给一个路径类字段接入新的 property type customization，确认在 prompt 或 settings 中能看到专门的 picker/validation，而不是退回纯文本输入。<br>2. 触发一次 script reload，确认 customization 仍然有效且不会重复注册。<br>3. 卸载/重载 `AngelscriptEditor` 模块后检查 `PropertyEditor` 中无残留 customization，未定制类型仍走原有 details fallback。 |

### Arch-27：脚本 action 的参数输入仍是 `UFunction -> FStructOnScope -> modal prompt` 反射通道，没有成长为可替换的 action input contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor action 的“收集参数、校验输入、执行动作”是否具备独立抽象，从而允许 host-specific widget、wizard 和 `EditorSubsystem` 扩展 |
| 当前设计 | 当前脚本 action 的主路径仍是 `CallFunctionOnSelection()` 统一转发到 `FScriptEditorPrompts::ShowPromptToCallFunction(...)`；后者把 `UFunction` 直接包装成 `FStructOnScope`，导入 K2 默认值，按首参数与选中对象的关系隐藏字段，然后在必要时弹出通用 details modal，最后直接 `ProcessEvent` 执行函数。换言之，action input 的 schema、UI、校验和执行今天仍是一条反射式单通道。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:143` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:146` — 选中集动作统一进入 `ShowPromptToCallFunction(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:192` — 直接用 `UFunction` 构造 `FStructOnScope` 参数内存。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:202` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:204` — 通过 `FindFunctionParameterDefaultValue()` + `ImportText_Direct()` 导入默认值。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:208` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:214` — 如果首参数可由当前 selection 绑定，就隐藏对应字段而不是建立显式输入 schema。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:233` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:238` — 只要还有剩余参数，就弹一次通用 prompt。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:243` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:255` — 针对 selection 的逐对象执行直接调用 `ProcessEvent(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:258` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:260` — 无 selection 绑定时同样直接 `ProcessEvent(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:51` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:57` — `UScriptEditorSubsystem` 没有 action presenter / validator / input service API。 |
| 优点 | 对脚本作者极其省样板，几乎任何 `CallInEditor` 风格的函数都能自动获得一个可用表单；旧 action 不需要编写额外的 Slate UI 或参数类。 |
| 不足 | 这条通道默认假设“参数就是一个短表单，校验和执行都可以同步完成”。推断：一旦 action 需要多步 wizard、路径/类名的实时验证、host-specific 上下文控件、非 modal 交互、记忆上次输入或异步任务确认，就只能继续在外围打补丁，而不是替换 action input 本身。它也让 `EditorSubsystem` 难以提供更高阶的交互能力，因为 subsystem 无处插入独立的 presenter/validator 生命周期。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | “新建动态类”不是把函数参数直接画成临时 details 表单，而是由 `FDynamicNewClassUtils` 打开挂在主窗口上的专用 `SWindow`，内部用 `SDynamicNewClassDialog` 的 `SWizard` 维护多页状态，并通过 `CanFinish()` / `UpdateInputValidity()` 在提交前做类名、父类、路径合法性校验。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:12`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:42`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:101`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:103`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:760`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:767`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:864` | action 输入可以先建模成“有状态的交互流程”，再决定是否最终落成窗口、wizard 或 toolbar 下拉，而不是把 `UFunction` 直接等同于 UI。 |
| UnLua | toolbar action 直接围绕具体上下文对象做校验：`CreateLuaTemplate_Executed()` 在拿到 `UBlueprint` 与 `ModuleName` 后先检查必填信息，再用 notification 报错；`RevealInExplorer_Executed()` 也在文件不存在时通过 notification 终止，而不是先弹一个通用参数框。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:257`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:272`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:274`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:315`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:339`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:345` | action 输入与校验更适合依附具体上下文语义，而不是总退化成一个“把函数参数列出来”的匿名表单。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 把“已有则加载、PIE 中禁止创建、按父类选择 blueprint 类型、成功后注册 `AssetRegistry`”都收敛在稳定对象方法里；创建语义不依赖一次性 prompt，而是由明确的 workflow object 承担。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:94`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:119`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:127`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | action/workflow 先被建模成稳定服务对象时，校验、环境约束和最终执行可以自然聚合，不需要把所有交互压回 `ProcessEvent` 前的一次性 prompt。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有反射 prompt 兼容路径的前提下，引入独立的 `action invocation + input presenter` 合同，让 UI、校验和执行可以分层演进。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorActionInvocation` 或 `IAngelscriptEditorActionInputPresenter`，最少建模 `Function`、`SelectionBinding`、`DefaultParams`、`ValidateInputs()`、`PresentInputs()`、`Execute()`；第一阶段只在 native 内部使用，不要求脚本作者修改现有 action 声明方式。<br>2. 实现一个 `DefaultReflectedPresenter`，内部继续复用当前 `FStructOnScope + IStructureDetailsView + ProcessEvent` 逻辑，作为所有旧 action 的默认 fallback，保证兼容。<br>3. 允许 `EditorSubsystem` / host adapter / native registry 通过 `ActionId`、`FunctionName` 或 `ContextType` 绑定专门 presenter；例如 `BlueprintEditor` action 可以换成局部 toolbar widget，创建类动作可以换成 wizard，而不必重写整个 action 系统。<br>4. 把现有“默认值导入、首参数绑定、执行 guard、notification/reporting”拆到 invocation 生命周期里，形成 `Prepare -> Validate -> Present -> Execute -> Report` 五段；这样后续才能无缝接入 `FSlateNotificationManager`、task service 或非 modal surface。<br>5. 先挑一个需要路径/类名校验的 action 迁到 custom presenter，验证新旧 presenter 可以并存；未迁移 action 继续走当前反射 prompt，不影响旧脚本。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Actions/AngelscriptEditorActionInvocation.*`、`Public/Actions/AngelscriptEditorActionInputPresenter.*` 与对应 `Private/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就试图让脚本侧动态声明任意 presenter/widget，容易把 host 生命周期、reload 注册和 UI 线程约束一次性耦合起来；应先用 native presenter registry 验证合同，再逐步开放项目扩展。 |
| 兼容性 | 可增量实施。没有专门 presenter 的旧 action 仍走当前反射 prompt 路径；只有显式迁移的 action 才使用新 wizard/widget。对现有脚本函数签名保持源级兼容。 |
| 验证方式 | 1. 保持一个旧的 menu action 不迁移，确认仍按今天的 details prompt 执行。<br>2. 给一个需要路径或类名校验的 action 接入新 presenter，确认无效输入会在执行前被阻止，而不是进入 `ProcessEvent` 后才失败。<br>3. 执行一次 full reload，确认 presenter registry 不重复注册，旧 action 和新 action 都仍可从各自 surface 触发。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-27 | 脚本 action 输入仍是单一反射 prompt，难以承载 wizard、typed validation 和 host-specific widget | invocation/presenter contract 新增 | 高 |
| P2 | Arch-26 | `PropertyEditor` 缺少正式 customization 注册面，项目方无法通过稳定 contract 扩展 details UI | customization registrar 收敛 | 中高 |

---

## 架构分析 (2026-04-08 17:08:05)

### Arch-28：编辑器扩展 surface 已经铺得很宽，但注册层把原生上下文压扁成了“对象/资产选择”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 不同 editor surface 的上下文抽象是否足够强类型，项目扩展者能否在不改核心源码的情况下复用 `BlueprintEditor` / `AnimationBlueprintEditor` / `ContentBrowser` 的原生上下文 |
| 当前设计 | `UScriptEditorMenuExtension` 用 `EScriptEditorMenuExtensionLocation` 覆盖了大量挂点，但注册器统一把上下文压成 `FExtenderSelection`；这个结构只有 `SelectedObjects` 和 `SelectedAssets` 两个数组，很多 UE 原生 surface 的 typed context 在注册层被直接丢弃。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:98` — `FExtenderSelection` 只有 `SelectedObjects` 与 `SelectedAssets`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:105` — `GetToolMenuContext()` 只是从 `ActiveToolMenuContext` 按类临时取 `UObject*`，没有稳定的 surface contract。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:900` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:903` — 多个 `LevelViewport_*` 分支直接以空 `FExtenderSelection()` 调 `Extend()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1020` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1023` — `ContentBrowser_AssetContextMenu` 收到 `SelectedPaths` 却完全没有传下去。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1032` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1035` — `ContentBrowser_PathViewContextMenu` 同样丢弃 `Paths`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1107` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1110` — `ToolMenu` 动态 section 也默认用空 selection 构建。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp:77` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:41` — 实际执行层继续围绕选中 actor / asset 数组做 prompt 与调用。 |
| 优点 | 一套脚本基类就能覆盖大量菜单挂点，新增 editor action 的 authoring 成本很低，脚本作者不需要先理解每个 UE editor toolkit 的 C++ API。 |
| 不足 | breadth 有了，depth 还不够。像 `SelectedPaths`、`UBlueprint`、`IAnimationBlueprintEditor`、asset editor toolkit 这类强语义上下文没有成为正式 contract，只能依赖空 selection、临时 `GetToolMenuContext(UObject)` 或继续加特判。推断：如果后续想把脚本扩展延伸到 `BlueprintEditor` toolbar、`ContentBrowser.AddNewContextMenu`、自定义面板或项目级 `EditorSubsystem`，当前注册模型会迫使扩展者继续绕过统一基类。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 不做“万能菜单枚举”，而是按 surface 拆出 `FBlueprintToolbar`、`FAnimationBlueprintToolbar`、`FMainMenuToolbar`。`BlueprintToolbar` 从 `ContextSensitiveObjects` 取 `UBlueprint`，`AnimationBlueprintToolbar` 直接拿 `IAnimationBlueprintEditor` 与 `PersonaToolkit`，再交给统一 toolbar 对象处理。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/AnimationBlueprintToolbar.cpp:26`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:127` | “菜单位置”不是扩展 contract，本质 contract 是“当前 surface 能提供什么上下文对象”。把 surface adapter 独立出来后，扩展入口更少但语义更强。 |
| UnrealCSharp | 同样把 `PlayToolBar`、`BlueprintToolBar`、`DynamicNewClassContextMenu` 分成不同对象。`BlueprintToolBar` 通过 `FAssetEditorExtender` 拿到 `UBlueprint`，`DynamicNewClassContextMenu` 直接消费 `SelectedClassPaths`，没有把路径上下文先丢成空 selection。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:17`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:7` | 同一插件可以有多个 editor surface contract，每个 contract 只暴露自己真正拥有的上下文；这样扩展点天然适合后续挂到模块成员或 subsystem registrar。 |
| puerts | `PuertsEditor` 没有把所有 editor 行为揉成一个脚本化菜单层，而是把 `UTypeScriptBlueprint` compiler 注册与 `DeclarationGenerator` 的全局菜单/toolbar 分开。Blueprint 相关行为走 blueprint compiler contract，全局工具走独立命令模块。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` | 参考价值不在“功能更多”，而在“surface 分层更清晰”：不同 surface 用不同模块/对象接线，避免注册器知道所有上下文细节。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `UScriptEditorMenuExtension` 脚本基类的前提下，引入显式的 `surface adapter + typed context` 层，把 editor 扩展从“位置枚举”升级为“位置 + 上下文合同”。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorSurfaceContext`（或等价结构），至少覆盖 `SelectedObjects`、`SelectedAssets`、`SelectedPaths`、`const FToolMenuContext*`、`TWeakObjectPtr<UBlueprint>`、`SurfaceId` 等字段；旧的 `FExtenderSelection` 先作为其兼容子集保留。<br>2. 把 `RegisterExtensions()` 的大 `switch` 拆成若干 `IAngelscriptEditorSurfaceAdapter` 实现，例如 `LevelViewportContextAdapter`、`ContentBrowserPathContextAdapter`、`BlueprintEditorToolbarAdapter`；每个 adapter 只负责把 UE 原生回调参数转成 `FAngelscriptEditorSurfaceContext`。<br>3. 给 `UScriptEditorMenuExtension` 增加新的只读查询 API，例如 `GetSelectedPaths()`、`GetBlueprintContext()`、`GetSurfaceId()`；现有 `GetToolMenuContext(TSubclassOf<UObject>)` 保留，作为未迁移 surface 的 fallback。<br>4. 在 `UScriptEditorSubsystem` 或新的 native registrar 中加入 `RegisterSurfaceExtension(...)` / `RegisterSurfaceAdapter(...)` 入口，让项目方可以新增 surface，而不是继续改 `EScriptEditorMenuExtensionLocation` 与核心 `switch`。<br>5. 第一阶段优先补两类高收益上下文：`ContentBrowser_PathViewContextMenu` 的 `SelectedPaths`，以及 `BlueprintEditor` / `AnimationBlueprintEditor` 的 typed context；确认旧脚本菜单不需要改动即可继续工作。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Surface/*.h/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 需要仔细处理 `reload` 与 extenders 反注册顺序，否则 adapter 对象如果早于 `LevelEditor` / `ContentBrowser` 销毁，容易留下悬挂 handle。另一个风险是同时维护旧 selection API 与新 context API 的双轨期。 |
| 兼容性 | 可增量实施。旧脚本继续依赖 `SelectedObjects/SelectedAssets` 与现有 `ShouldExtend()`；只有需要强类型上下文的新扩展才使用新 API。对现有脚本菜单声明和元数据保持源级兼容。 |
| 验证方式 | 1. 给 `ContentBrowser_PathViewContextMenu` 写一个试验扩展，确认脚本能拿到真实 `SelectedPaths`，而不是空 selection。<br>2. 新增一个 `BlueprintEditor` 专用扩展，确认能读取当前 `UBlueprint` 上下文且不会出现在无关 surface。<br>3. 触发一次 script reload 和一次 editor module reload，确认 extender 不重复注册、旧脚本菜单仍正常出现。 |

### Arch-29：脚本资产 authoring 仍是模块级静态 popup，没有形成可替换的 workflow service

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产的创建、打开、引用和路径决策是否有统一 workflow service，还是继续散落在模块静态函数和临时 Slate popup 里 |
| 当前设计 | 与脚本资产 authoring 相关的主路径仍集中在 `FAngelscriptEditorModule::ShowCreateBlueprintPopup()` 与 `ShowAssetListPopup()` 两个静态函数里：前者根据脚本源文件路径猜默认目录、弹 `CreateModalSaveAssetDialog`、手工 `CreatePackage` / `CreateBlueprint`；后者用一次性的 `CreateAssetPicker` + `SPositiveActionButton` 组合出“打开/新建”窗口。`UAngelscriptContentBrowserDataSource` 负责显示虚拟 item，但并不拥有这些 authoring 行为。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` — `ShowCreateBlueprintPopup()` 是模块静态函数，不是独立 workflow 对象。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:447` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:468` — 默认保存目录通过 `GetRelativeSourceFilePath()` 与现有资产目录启发式推导。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:480` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:514` — 创建路径直接走 `CreateModalSaveAssetDialog()` + `FKismetEditorUtilities::CreateBlueprint()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:532` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:537` — 保存与打开同样在模块函数里完成。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:612` — 多资产场景通过自建 `AssetPicker` popup 处理，而不是挂回 `ContentBrowser` 的标准 add-new / edit contract。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:625` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:638` — popup 底部按钮再次回调 `ShowCreateBlueprintPopup()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:187` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:204` — `CanEditItem()`、`EditItem()`、`BulkEditItems()`、`AppendItemReference()` 仍全部 `return false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:249` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:256` — 路径回桥接口也还没有接上。 |
| 优点 | 当前实现直接复用 UE 原生 `SaveAssetDialog`、`UAssetEditorSubsystem` 与 `FKismetEditorUtilities`，第一次把脚本类转成可编辑资产的成本较低，也不需要先建立独立 factory/service 体系。 |
| 不足 | 这套路径更像“模块内部 helper”，不是正式 authoring contract。创建、打开、路径推导、虚拟 item 编辑和内容浏览器入口分属不同位置，项目方即使继承 `EditorSubsystem` 也没有稳定的 service 可以替换或复用。推断：一旦要支持“从当前 `ContentBrowser` 路径新建”“右键批量打开”“项目自定义创建向导”“不同脚本资产类型共用 authoring 流程”，静态 popup 会迅速变成维护瓶颈。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `UDynamicDataSource` 在初始化时直接往 `ContentBrowser.AddNewContextMenu` 挂 dynamic section，再由 `PopulateAddNewContextMenu()` 读取已选路径并转给 `OnNewClassRequested()`；同一个 data source 还实现了 `EditItem`、`BulkEditItems`、`DeleteItem`、`AppendItemReference` 和 `Legacy_TryConvert*`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:71`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:523`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:592`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:666`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:12` | 如果决定支持虚拟数据源，就把 authoring 行为也放进同一条 workflow 链里，让“显示、创建、编辑、引用、路径转换”共享同一个拥有者。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 把“先尝试加载、父类不一致时修正、PIE 中禁止创建、按父类决定 blueprint 类型、成功后通知 `AssetRegistry`”收敛到稳定对象方法，而不是散落在 editor 模块静态 popup 中。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:94`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:119`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:127`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:151`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 哪怕暂时还不引入 `UFactory`，也值得先把 authoring 语义沉淀成稳定 service object，避免模块函数同时负责 UI、路径、创建和保存。 |
| UnLua | 资产相关 editor action 基本都绑定在当前 `UBlueprint` 上下文中执行，例如 `CreateLuaTemplate_Executed()` 直接从当前 blueprint 取 `GetModuleName()`，失败时用 notification 反馈；`RevealInExplorer_Executed()` 也是围绕当前蓝图决定目标文件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:257`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:269`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:274`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:315`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:336`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:345` | 不一定所有 authoring 都要进 `ContentBrowserDataSource`，但应当挂在稳定的上下文 service 上，而不是退回一个全局资产列表 popup。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把现有“创建/打开/路径推导”逻辑抽成统一 `asset workflow service`，再逐步把 `ContentBrowser`、runtime bridge 和脚本扩展入口都收口到这条服务线上。 |
| 具体步骤 | 1. 新增 `FAngelscriptAssetWorkflowService`（或 `UAngelscriptEditorAssetWorkflowSubsystem`），第一阶段只把 `ShowCreateBlueprintPopup()` 与 `ShowAssetListPopup()` 的逻辑搬进去，形成 `RequestCreateAsset(...)`、`RequestOpenAssets(...)`、`CreateOrOpenAsset(...)` 三个入口；旧模块函数改成薄封装。<br>2. 在 editor 模块或 `UAngelscriptContentBrowserDataSource` 中补一条 `ContentBrowser.AddNewContextMenu` 注册路径，消费 `UContentBrowserDataMenuContext_AddNewMenu` 的选中目录，把当前 folder 作为默认创建路径，而不是只靠脚本源文件路径启发式猜测。<br>3. 把 `UAngelscriptContentBrowserDataSource::EditItem()`、`BulkEditItems()`、`AppendItemReference()`、`Legacy_TryConvert*()` 改为委托给 workflow service，优先补齐“打开资产、复制引用、路径回桥”三项最常用行为；显示层与 authoring 层共享同一套路径/对象解析逻辑。<br>4. 通过 `UScriptEditorSubsystem` 或 native registry 暴露 `RegisterAssetAuthoringHandler(...)` / `OverrideDefaultCreatePathResolver(...)`，让项目方可以在不改核心模块的前提下替换路径规则或创建 UI。<br>5. 第二阶段再评估是否需要引入 `UFactory` / `AssetDefinition`；这一步不是第一阶段前置条件，避免为了架构纯度一次性重写整条 authoring 流。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AssetWorkflow/AngelscriptAssetWorkflowService.*`。 |
| 预估工作量 | M |
| 架构风险 | 需要谨慎处理物理 `/Game` 目录与当前 `/All/Angelscript` 虚拟路径的映射边界；如果第一阶段就同时改动虚拟路径结构和 authoring 入口，容易与现有 data source 行为互相干扰。更稳妥的顺序是先收口 service，再补 path bridge。 |
| 兼容性 | 可增量实施。现有 runtime bridge、现有按钮和现有 popup 都可以继续调用旧函数名，只是内部改为转发到新 service；项目脚本用户不需要立刻改调用方式。 |
| 验证方式 | 1. 从 `ContentBrowser` 当前目录触发一次“新建脚本 Blueprint/DataAsset”，确认默认路径来自已选目录而不是仅靠脚本源文件推导。<br>2. 旧的 asset list popup 入口仍然可用，但内部应走统一 workflow service；创建后的资产可以正常保存并自动打开。<br>3. 给一个虚拟 `Angelscript` item 调用 `EditItem()` 与 `AppendItemReference()`，确认内容浏览器内的打开/复制引用行为与旧 popup 路径保持一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-28 | editor surface 的上下文合同过于扁平，导致 `ContentBrowser` 路径、`BlueprintEditor` toolkit 等强语义上下文无法稳定外露 | surface adapter + typed context 收敛 | 高 |
| P2 | Arch-29 | 脚本资产 authoring 仍是模块静态 popup，缺少可复用 workflow service 与 `ContentBrowser` add-new 合同 | asset workflow service 收敛 | 中高 |

---

## 架构分析 (2026-04-08 17:21:34)

### Arch-30：interactive editor lane 与 headless automation lane 还没有正式分层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编辑器模块在交互式编辑器、cook/commandlet、无主窗口场景下的装配边界是否清晰 |
| 当前设计 | `AngelscriptEditor` 当前把 `ToolMenus`、`ContentBrowserDataSource`、脚本菜单扩展、目录监听和 runtime->editor popup bridge 都直接塞进 `StartupModule()`；`BlueprintImpact` commandlet 虽然存在，但它与 interactive surface 没有共用的 lane coordinator。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` — `StartupModule()` 启动后立即初始化 `ClassReloadHelper`、源码导航、`GameplayTags` delegate、`OnPostEngineInit`、脚本菜单扩展、目录监听、settings、runtime bridge、`OnObjectPreSave` 与 `UToolMenus` callback。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:361` — `OnPostEngineInit` 总是绑定到 `OnEngineInitDone()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` — `OnEngineInitDone()` 直接创建并激活 `UAngelscriptContentBrowserDataSource`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25` — `InitializeExtensions()` 无 `IsRunningCommandlet()` / `GEditor` 分流，直接接入 reload 与 engine-exit hook。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `UToolMenus::RegisterStartupCallback(...)` 也是无条件注册。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55` — `BlueprintImpact` 的 headless 入口另在 commandlet 内单独实现，没有和模块启动阶段共享显式执行 lane。 |
| 优点 | editor target 下功能接线很直接，新能力容易快速挂进来；CLI 也已经有可用的 `BlueprintImpact` 入口。 |
| 不足 | interactive UI hook 与 headless automation contract 没有正式边界。推断：在 cook/commandlet、`-game`、无主窗口或未来需要更多自动化入口的场景里，模块仍会尝试装配 `ToolMenus`、`LevelEditor` 相关 surface 或 popup bridge，项目方也无法通过 `EditorSubsystem` 明确区分“可在 headless 复用的 service”和“只能在 UI editor 中工作的 feature”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `StartupModule()` 只先装配样式、命令和 toolbar 对象；真正进入 `MainMenuToolbar` / `BlueprintToolbar` / `AnimationBlueprintToolbar` 前，`OnPostEngineInit()` 会先检查 `GEditor`，在无主编辑器窗口场景下直接返回。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:92` | 先区分“模块已加载”和“可安全装配 interactive surface”两个阶段，避免把 UI feature 混进所有 editor target 场景。 |
| puerts | `PuertsEditorModule` 在启动时先计算 `Enabled = IsWatchEnabled() && !IsRunningCommandlet()`，`OnPostEngineInit()` 里只有 `Enabled` 为真时才注册 `FKismetCompilerContext` 和文件监听；全局生成工具 `DeclarationGenerator` 则把相同能力同时投到 `ToolMenus` 与 `Puerts.Gen` console command。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:78`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1668` | 把 interactive lane 与 automation lane 分开判断；同一 service 可以有 UI 投影，也可以有 headless/console 投影。 |
| UnrealCSharp | `StartupModule()` 会先注册命令、toolbar、data source，但在 `IsRunningCookCommandlet()` 分支中改为等待 `AssetRegistry::OnFilesLoaded()` 后执行 `Generator()`；`FEditorListener` 构造与析构又显式跳过 cook commandlet，只在真正的交互式 editor session 注册目录监听、PIE、MainFrame 和应用激活事件。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:115`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:119`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:22`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:24`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69` | lane 分层不仅体现在 if-guard，还体现在对象所有权上: cook lane 只保留生成/资产注册相关逻辑，interactive lane 才装配 UI listener。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `AngelscriptEditor` 增加显式 `execution lane` 分层，把 interactive surface、automation service、cook/commandlet adapter 分开装配。 |
| 具体步骤 | 1. 在 `FAngelscriptEditorModule` 中新增 `DetermineExecutionLane()`，至少区分 `InteractiveEditor`、`CookCommandlet`、`OtherCommandlet` 三类；判断条件基于 `IsRunningCookCommandlet()`、`IsRunningCommandlet()` 与 `GEditor != nullptr`。<br>2. 把当前 `StartupModule()` 拆成两个最小装配入口：`RegisterAutomationServices()` 和 `RegisterInteractiveFeatures()`；第一阶段先把 `UToolMenus`、`ContentBrowserDataSource`、`UScriptEditorMenuExtension::InitializeExtensions()`、`ForceEditorWindowToFront` 相关 popup bridge 收进 interactive lane。<br>3. 把 `BlueprintImpact`、后续 console command、以及不依赖主窗口的扫描/校验能力收口到 automation service；interactive 菜单与未来 `EditorSubsystem` 只调用该 service，而不是各自再复制逻辑。<br>4. 对 runtime->editor bridge 增加 lane-aware adapter：interactive lane 继续走 popup/UI，headless lane 返回结构化失败或日志摘要，避免在无窗口环境中继续尝试 `LevelEditor`/`Slate` 路径。<br>5. 在 `ShutdownModule()` 中按 lane 对称回收，只注销当前 lane 注册过的 handle；同时补一组最小自动化验证，确认 commandlet 启动时不会触碰 `ToolMenus`/`LevelEditor`。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Services/AngelscriptEditorAutomationService.*` 或等价文件。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段同时改动所有 editor feature 的启动顺序，容易和现有 reload / data source 初始化时序互相影响；更稳妥的做法是先只隔离 `ToolMenus`、`ContentBrowser` 和 popup bridge，保留 reload/impact scanner 内核不动。 |
| 兼容性 | 可增量实施。interactive editor 的现有菜单、popup 和浏览器行为可以保持不变；变化主要体现在 commandlet/cook lane 下不再装配无意义的 UI feature。对现有脚本作者与项目工程应保持源级兼容。 |
| 验证方式 | 1. 用普通 editor session 启动，确认 `Tools` 菜单、`ContentBrowserDataSource`、脚本菜单扩展仍正常注册。<br>2. 用 commandlet/cook 场景启动，确认不会触发 `ToolMenus` / `LevelEditor` / popup 路径，且 `BlueprintImpact` 仍可执行。<br>3. 模块重载后核对不同 lane 的 handle 不重复注册，shutdown 时无残留 callback。 |

### Arch-31：脚本根的宿主接入仍是隐式约定，editor 层缺少可诊断的 project integration policy

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 是否把脚本根发现、宿主工程配置和打包/交付约束提升为正式的 project integration contract |
| 当前设计 | runtime 已经会自动发现 `<Project>/Script` 与 enabled content plugin 下的 `<Plugin>/Script`，但 `AngelscriptEditor` 侧只注册通用 `UAngelscriptSettings`；脚本根需要怎样被宿主工程接纳、哪些目录需要校验或修复，当前没有 editor policy surface。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:14` — 插件声明 `CanContainContent = false`，宿主集成默认依赖代码/外部脚本目录，而不是插件内容资产。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558` — 默认依赖表会枚举 enabled content plugins，并把 `<Plugin>/Script` 收进候选脚本根。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326` — `DiscoverScriptRoots()` 发现 `<Project>/Script`，在 editor 模式下还会自动创建缺失目录。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1347` — plugin script roots 也会被纳入搜索顺序。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:384` — editor 模块当前只注册 `Project/Plugins/Angelscript` settings section。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` — 本轮对 `StartupModule()` 全段核对后，未见 `UProjectPackagingSettings`、`DirectoriesToAlwaysStageAsUFS` 或等价的宿主修复/诊断接线。 |
| 优点 | 宿主项目几乎不需要额外样板就能得到脚本根发现和目录创建，插件边界保持得比较轻。 |
| 不足 | “脚本根会被 runtime 使用”这件事目前是隐式约定，而不是 editor contract。推断：一旦项目需要稳定地把脚本目录带进打包、多人协作模板、插件扩展目录或 CI 检查，用户只能依赖外部文档和人工配置；`EditorSubsystem` 也拿不到一个正式的 project integration service 去检查或修复这些约束。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | editor 模块不仅注册 `UnLua Editor` settings，还在 `OnModified()` 中定义变更后的生效逻辑；`SetupPackagingSettings()` 会把 `Script` 和 `UnLuaExtensions` 下的脚本目录写入 `DirectoriesToAlwaysStageAsUFS`，然后持久化到 config。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:114`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:123`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:134`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:186`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:208`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:227` | 把“宿主需要满足哪些工程约束”显式写进 editor 模块，用户不必靠文档猜测。 |
| UnrealCSharp | `UpdatePackagingSettings()` 在 editor 启动时检查 `PublishDirectory` 是否已进入 `DirectoriesToAlwaysStageAsUFS`，缺失时自动补齐并 `TryUpdateDefaultConfigFile()`；同一模块还把 runtime/editor settings 分开注册。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:37`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:39`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:102`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:230`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:232` | project integration 可以是“可自动修复的 editor policy”，而不是把修复责任完全留给项目方。 |
| puerts | puerts 的 editor workflow 更偏向 `UTypeScriptBlueprint` / `PEBlueprintAsset` 资产路径: `OnPostEngineInit()` 注册专用 compiler，`PEBlueprintAsset::LoadOrCreate()` 把创建与加载收敛在 asset object 上，而不是依赖一个额外的 project script root policy。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:94`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:154` | 如果插件确实要依赖宿主脚本根，就更应该把这类约束做成 editor 侧可检查的 policy；否则长期维护成本会比 asset-centric 工作流更高。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增一层 `project integration service`，先把脚本根诊断和 repair surface 正式化，再决定哪些宿主配置允许自动写回。 |
| 具体步骤 | 1. 新增 `FAngelscriptProjectIntegrationService` 或 `UAngelscriptEditorProjectIntegrationSettings`，第一阶段只负责 `InspectScriptRoots()`、`ValidateProjectIntegration()` 和 `BuildIntegrationReport()`，输入直接复用 `FAngelscriptEngine::MakeAllScriptRoots()` 的发现结果。<br>2. 在 editor settings 中新增显式 policy，例如 `bWarnOnUnstagedProjectScriptRoot`、`bAutoRepairProjectScriptRoot`、`bCheckPluginScriptRoots`；默认采用 `warn-only`，避免未征得用户同意就改宿主 config。<br>3. 第一阶段只对 `<Project>/Script` 提供自动修复: 如果当前运行模式仍需要原始脚本目录，就把该目录写入 `UProjectPackagingSettings::DirectoriesToAlwaysStageAsUFS` 并 `TryUpdateDefaultConfigFile()`；plugin `<Plugin>/Script` 先输出结构化诊断和修复建议，避免一次性引入过宽的宿主写回。<br>4. 给 `Tools` 菜单或新的 settings 页面增加 `Repair Project Integration` / `Show Integration Report` 入口；同时通过 `UScriptEditorSubsystem` 暴露只读查询，让项目级 editor extension 可以在自己的 panel 或 CI 命令里复用同一诊断结果。<br>5. 第二阶段再评估两条长期路线：要么继续完善 script-root staging policy，要么把更多 workflow 向 asset-centric/editor-centric surface 收口，降低对外部脚本目录约束的依赖。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Settings/AngelscriptEditorProjectIntegrationSettings.*`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Services/AngelscriptProjectIntegrationService.*`。 |
| 预估工作量 | M |
| 架构风险 | 自动修复如果一步到位覆盖 project root 和所有 plugin root，容易误改宿主工程配置；更稳妥的顺序是先做诊断和 project-root repair，再让 plugin root policy 通过显式设置开启。 |
| 兼容性 | 可向后兼容。默认 `warn-only` 模式下不修改任何现有项目配置；只有用户明确开启 auto-repair 时才写回宿主 config。现有脚本目录发现逻辑和旧项目结构都可保持不变。 |
| 验证方式 | 1. 在一个缺少 `<Project>/Script` 集成策略的测试项目中启动 editor，确认能看到明确诊断，并可通过 repair 入口补齐配置。<br>2. 保持默认 `warn-only`，验证旧项目不会被静默改写 config。<br>3. 在启用 content plugin script roots 的项目中生成 integration report，确认 project root 与 plugin roots 都能被枚举并给出稳定、可复用的修复建议。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-30 | interactive editor lane 与 headless automation lane 未分层，UI 装配和 commandlet/cook contract 混在同一启动链 | execution lane 分层 + service 收敛 | 高 |
| P2 | Arch-31 | 脚本根宿主接入缺少 project integration policy，发现结果没有升级为可诊断/可修复的 editor contract | project integration service + opt-in repair | 中高 |

---

## 架构分析 (2026-04-08 17:34:05)

### Arch-32：`EditorSubsystem` 目前只能承载生命周期，拿不到可消费的 editor capability object

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户是否能通过 `EditorSubsystem` 复用现有 editor 能力，并在不改核心模块的前提下编排自己的 editor 工具 |
| 当前设计 | `UScriptEditorSubsystem` 只提供 `ShouldCreate/Initialize/Deinitialize/Tick` 壳层，`UEditorSubsystemLibrary` 也只暴露 generic getter；真正有价值的 editor 能力仍然留在 `FAngelscriptEditorModule` 的私有注册代码和 `UScriptEditorMenuExtension` 的全量扫描逻辑里。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7` — `UScriptEditorSubsystem` 被声明为 `NotBlueprintable, Abstract` 的 editor 生命周期壳。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:33` — `Initialize()` 仅设置状态并调用 `BP_Initialize()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:40` — `Deinitialize()` 仅回调 `BP_Deinitialize()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:80` — 唯一持续能力是 `BP_Tick()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` — library 只公开 `GetEditorSubsystem(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:366` — 目录监听直接在模块启动里原地注册。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397` — runtime 到 editor 的“打开资产”桥接通过模块内 lambda 直接接到 `ShowAssetListPopup()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:405` — “创建 Blueprint”桥接同样直接接到模块静态 popup。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25` — 菜单扩展初始化只是监听 reload/exit。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:845` — 真正注册扩展仍靠 `TObjectIterator<UASClass>` 全量扫描。 |
| 优点 | `EditorSubsystem` 已经给脚本提供了 editor 生命周期驻留点，项目方至少可以在 editor session 内运行长寿命逻辑。 |
| 不足 | 这还不是正式的 editor service 层。项目方虽然能“在 editor 里跑代码”，却拿不到目录监听、资产工作流、任务反馈、菜单注册等 capability object，也没有成对的 registration handle 可在 `BP_Deinitialize()` 中释放。推断：当前更像“允许脚本存在于 editor 中”，而不是“允许脚本搭建 editor 工具”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把目录监听包装成可实例化的 `UPEDirectoryWatcher`：对象本身公开 `Watch/UnWatch` 和 `OnChanged` delegate；`CodeAnalyze.ts` 直接在 TypeScript 里 `new UE.PEDirectoryWatcher()` 并绑定文件变化回调。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:17`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:25`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:74`<br>`Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:465`<br>`Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:468` | editor 能力先被做成脚本可消费的 service object，脚本工具再在其上自由编排，而不是只能依赖模块私有静态函数。 |
| UnrealCSharp | 模块头显式持有 `FUnrealCSharpPlayToolBar`、`FUnrealCSharpBlueprintToolBar`、`FEditorListener`、多个 `FAutoConsoleCommand` 和 `UDynamicDataSource`；启动阶段实例化这些对象，关闭阶段成对释放。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:41`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:68`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:104` | 即使扩展仍以 native 为主，能力边界也先被拆成可组合的 feature owner，而不是只剩一个空壳 subsystem。 |
| UnLua | `StartupModule()` 只做装配：先注册 commands，再创建 `FMainMenuToolbar`、`FBlueprintToolbar`、`FAnimationBlueprintToolbar`；真正 action 绑定发生在 toolbar 对象的 `CommandList->MapAction(...)` 内。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:32` | 先把 editor 能力拆成对象，再谈 module startup；后续要给外部暴露 registry 或 service，会比“模块里到处是私有 lambda”更顺。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `UScriptEditorSubsystem` 从“生命周期壳层”升级为“typed editor service facade”，先让脚本拿到能力对象，再谈更高层的 surface 扩展。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 中新增几类最小 `UObject` service：`UAngelscriptEditorMenuService`、`UAngelscriptDirectoryWatchService`、`UAngelscriptAssetWorkflowService`、`UAngelscriptEditorTaskService`；第一阶段只包现有 native 逻辑，不改现有菜单或 popup 行为。<br>2. 扩展 `UScriptEditorSubsystem` 与 `UEditorSubsystemLibrary`，新增 typed getter，例如 `GetAngelscriptEditorMenuService()`、`GetAngelscriptDirectoryWatchService()`；避免项目脚本只能拿到一个无能力的 generic subsystem。<br>3. 让 `FAngelscriptEditorModule::StartupModule()` 负责创建并持有这些 service，再把当前目录监听、runtime bridge、固定 menu 注册迁到 service 内部；旧静态 helper 暂时保留，但只做转发。<br>4. 为每个 service 增加 handle-based 注册 contract，例如 `RegisterWatch(...) -> FAngelscriptEditorRegistrationHandle`、`RegisterMenuEntry(...) -> Handle`；这样 `UScriptEditorSubsystem::BP_Deinitialize()` 才能成对清理自己注册的扩展。<br>5. 第二阶段再让 `UScriptEditorMenuExtension`、未来的 impact/panel 功能和项目级 editor subsystem 都改为消费同一批 service，而不是继续各自直连模块私有实现。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/*.h` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Services/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | service 生命周期必须和 editor module、script full reload、module shutdown 对齐；如果继续沿用当前“谁注册谁不持有 handle”的做法，service facade 只会把问题换个地方堆积。 |
| 兼容性 | 可增量实施。现有 `BP_Initialize/BP_Deinitialize/BP_Tick`、`UScriptEditorMenuExtension`、runtime bridge 和旧静态 helper 都可先保留；新 service 先作为更正式的入口并存，不会破坏已有脚本。 |
| 验证方式 | 1. 新建一个项目级 `UScriptEditorSubsystem` 子类，只通过新的 typed service 注册目录监听和一个 menu action，确认无需修改核心模块即可生效。<br>2. 触发一次 full reload，再关闭 editor，确认 subsystem 自己注册的 handle 能被正确释放，没有重复菜单或残留 watch callback。<br>3. 让一个现有内建功能继续走旧入口，确认其内部实际已转发到新 service，行为与旧版本一致。 |

### Arch-33：内建 editor tool 和外部扩展分属两条注册轨道，默认工具无法按同一 contract 被替换

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件内建的 editor 工具是否与用户扩展共用同一 command/tool registry，从而允许项目方基于 `EditorSubsystem` 做覆盖、禁用或替换 |
| 当前设计 | `AngelscriptEditor` 的默认工具仍是模块私有代码：`RegisterToolsMenuEntries()` 直接塞固定菜单项，资产创建/打开工作流则由 runtime delegate 直连模块静态 popup；与之并列的 `UScriptEditorMenuExtension` 只负责脚本扩展类扫描，两条轨道没有共享的 `ToolId`、descriptor 或 override policy。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` — `RegisterToolsMenuEntries()` 是固定工具入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:706` — `ASOpenCode` 直接在模块 lambda 里执行外部命令。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722` — `ASGenerateBindings` 直接绑定 `GenerateNativeBinds()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:735` — `Function Tests` 同样是模块内硬编码动作。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397` — 打开资产的 editor bridge 直接绑定 `ShowAssetListPopup()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:405` — 创建 Blueprint 的 bridge 直接绑定 `ShowCreateBlueprintPopup()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` — 资产创建逻辑集中在模块静态 popup。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` — 资产打开列表逻辑同样集中在模块静态 popup。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:845` — 外部扩展的另一条轨道则是 `UASClass` 扫描式注册。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` — `EditorSubsystem` 侧没有 `RegisterEditorTool/OverrideEditorTool` 这类统一工具注册 API。 |
| 优点 | 新增内建工具非常快，模块作者可以直接把一条 editor 功能塞进 `Tools` 菜单或 popup，不需要先设计 descriptor。 |
| 不足 | 内建工具和外部扩展不在同一个 contract 上，导致项目方即便有 `UScriptEditorSubsystem`，也不能按同一套规则禁用、替换或复用默认工具。推断：后续如果要给 `ASOpenCode`、`Create Blueprint`、impact scan、task feedback 全部引入统一 policy，必须同时改两套实现，而不是只升级一条扩展总线。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 先定义 `FUnLuaEditorCommands` 的稳定 command identity，再由 `FMainMenuToolbar` 持有 `CommandList` 并 `MapAction(...)`；模块只负责创建 toolbar owner。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:32` | 内建工具首先是 command + owner object，菜单只是投影；后续扩展、替换或迁移 surface 时，不需要回到 module 主文件改 lambda。 |
| UnrealCSharp | 模块头明确持有 `FUnrealCSharpPlayToolBar` 等 owner；`FUnrealCSharpPlayToolBar` 在构造时建立 `CommandList`，`BuildAction()` 统一 `MapAction(...)`，`Initialize()` 再把这些动作投到 toolbar。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:198`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:11`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:17`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:37` | 默认工具被做成 feature owner 后，module 可以只做装配，项目后续也更容易围绕 tool owner 追加 policy 或替代入口。 |
| puerts | `DeclarationGenerator` 直接是独立 feature module：内部持有 `PluginCommands` 和 `ConsoleCommand`，`StartupModule()` 里 `MapAction(...)`，再统一注册 menu/toolbar/console。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1569`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1650`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1668` | 更进一步的做法是把“默认工具”直接提炼成独立 feature/module，使其天然具备稳定身份、可测试性和多 surface 复用能力。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入统一的 `editor tool registry`，让内建工具和项目扩展都落到同一套 descriptor/owner contract 上，再由不同 surface 决定如何呈现。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorToolDescriptor` 或 `IAngelscriptEditorToolOwner`，至少包含 `ToolId`、`DisplayName`、`SurfaceIds`、`Execute`、`CanExecute`、`OptionalConsoleName`、`OwnerClassPath`；第一阶段先只服务 native 内建工具。<br>2. 把 `ASOpenCode`、`ASGenerateBindings`、`Function Tests`、`ShowCreateBlueprintPopup`、`ShowAssetListPopup` 各自迁成独立 tool owner 或 descriptor，`RegisterToolsMenuEntries()` 改成仅遍历 registry 并把 tool 投影到 `ToolMenus`。<br>3. 在 `UScriptEditorSubsystem` 或新的 tool service 上开放 `RegisterEditorTool(...)`、`OverrideEditorTool(...)`、`SetEditorToolEnabled(...)`；项目方若要替换默认工具，只需基于 `ToolId` 提供新的实现，而不是去 patch 模块主文件。<br>4. 让 `UScriptEditorMenuExtension` 可以引用已注册 `ToolId`，而不必每次都重新拼一份 `FUIAction`；这样脚本菜单、固定菜单和未来 console/toolbar 能共享同一个执行内核。<br>5. 兼容阶段保留现有 label、菜单路径和 popup 行为；等 registry 稳定后，再决定哪些 tool 继续保留为默认实现，哪些转为项目可选。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Tools/*.h` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tools/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果 `ToolId` 设计不稳定，full reload 或模块重载后会出现“旧菜单还在、新工具又注册一遍”的重复注册问题；因此 tool registry 必须和前面的 command/service 持久 identity 一起设计。 |
| 兼容性 | 可增量实施。第一阶段只是把现有默认工具搬进 registry，外部菜单路径、按钮文案、popup 行为都可保持不变；只有项目显式调用 override API 时，默认工具才会被替换。 |
| 验证方式 | 1. 回归现有 `Tools` 菜单，确认 `ASOpenCode`、`ASGenerateBindings`、`Function Tests` 仍各出现一次且功能不变。<br>2. 编写一个项目级 `UScriptEditorSubsystem`，通过新 API 禁用或替换其中一个默认工具，确认无需改核心插件源码即可生效。<br>3. 让一个脚本菜单扩展复用同一个 `ToolId`，验证固定菜单和脚本菜单触发的是同一执行内核，而不是两套分离逻辑。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-32 | `EditorSubsystem` 只有生命周期，没有可消费的 editor capability object，项目方难以通过脚本搭建正式 editor 工具 | service facade + handle 化注册 | 高 |
| P2 | Arch-33 | 内建 tool 和外部扩展分属两条注册轨道，默认工具无法按同一 contract 被替换 | unified tool registry + tool owner 收敛 | 中高 |

---

## 架构分析 (2026-04-08 17:46:33)

### Arch-34：UI 展示宿主仍是全局/`LevelEditor` 绑定，复杂交互还不能跟随真实 editor host

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `Slate`/popup/modal 的宿主窗口解析是否是正式 contract，项目方能否让自定义 editor 工具挂到当前发起交互的 host 上 |
| 当前设计 | `AngelscriptEditor` 的交互式 UI 仍默认把“当前活动窗口或 `LevelEditor` 主标签页”当成唯一宿主：创建资产前先强制抢前台，资产列表 popup 也自己推导父窗口与屏幕中心位置，通用 prompt 则直接走 `GEditor->EditorAddModalWindow(...)`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:96` — `ForceEditorWindowToFront()` 先取 `FSlateApplication::Get().GetActiveTopLevelWindow()`，拿不到时退回 `LevelEditor` tab。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:107` — 最终直接调用 `ParentWindow->HACK_ForceToFront()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:425` — `ShowCreateBlueprintPopup()` 在真正进入保存对话框前总是先 `ForceEditorWindowToFront()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:654` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:658` — `ShowAssetListPopup()` 的父窗口同样只在 active top-level 与 `LevelEditor` tab 之间二选一。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:665` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:671` — popup 位置按父窗口屏幕中心重新计算，再 `PushMenu(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:166` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:178` — 通用参数收集窗口最终统一走 `GEditor->EditorAddModalWindow(Window)`，没有把发起 surface 的 host 传进来。 |
| 优点 | 这套路径实现成本低，在最常见的单主窗口 editor session 中大概率能工作，不需要先定义复杂的 host/presentation 抽象。 |
| 不足 | UI 宿主语义仍是隐式环境而不是正式 contract。推断：当动作从 `BlueprintEditor`、`ContentBrowser`、未来的 dockable panel、项目自定义 `EditorSubsystem` 或多窗口编辑场景发起时，当前实现容易继续把交互弹到错误的窗口层级，或通过 `HACK_ForceToFront()` 打断用户当前 focus；项目方也无法在不改核心模块的情况下控制“窗口挂在哪个 host 上、popup 相对谁定位、哪些交互该用 child window 而不是全局 modal”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FMainMenuToolbar` 的 `About` 动作通过 `IMainFrameModule::GetParentWindow()` 取正式父窗口，再用 `AddModalWindow(...)` 挂到主框架；窗口归属是显式的，不依赖“当前哪个 top-level 窗口正好激活”。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:72`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:75` | 先显式解析 host window，再决定 modal/child 的呈现方式，避免把 UI 绑定到偶然的活动窗口。 |
| UnrealCSharp | `FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog()` 明确创建 `SWindow`，并通过 `IMainFrameModule::GetParentWindow()` 决定是 `AddWindowAsNativeChild(...)` 还是普通 `AddWindow(...)`；`SDynamicNewClassDialog` 本身也把 `ParentWindow` 作为输入参数的一部分。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:40`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/NewClass/SDynamicNewClassDialog.h:20` | 把“对话框内容”和“窗口宿主策略”拆开后，同一个工具流程更容易被不同 editor host 复用。 |
| puerts | `DeclarationGenerator` 把入口保持在 `ToolMenus`/toolbar 命令上，执行完成后统一用 `FSlateNotificationManager` 反馈，而不是为了展示结果再抢前台或额外拼一个全局 popup host。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1579`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1593`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1625`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1627` | 不是所有交互都该退回 popup；能留在原 surface 的结果反馈，尽量不要额外发明全局窗口宿主。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `AngelscriptEditor` 增加正式的 `presentation host` 层，把“交互内容”和“挂在哪个 editor host 上”分开建模；旧全局 popup 路径作为 fallback 保留。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorPresentationContext` 与 `UAngelscriptEditorPresentationService`（或等价 native service），最少建模 `SurfaceId`、`ParentWindow`、`ToolMenuContext`、`WeakToolkitHost`、`RequestedAnchorRect` 等信息；第一阶段允许这些字段为空。<br>2. 把 `ShowCreateBlueprintPopup()`、`ShowAssetListPopup()` 与 `FScriptEditorPrompts::ShowPromptForStruct()` 的展示逻辑改为统一调用 `PresentationService->ShowModal(...)` / `ShowPopup(...)` / `ShowNotification(...)`；旧静态函数只负责组装 context 并转发。<br>3. 先实现三类最小 host resolver：`MainFrame`、`LevelEditor`、`BlueprintEditor`。没有真实 host 时再退回今天的 active top-level 逻辑，但把 `HACK_ForceToFront()` 限制为兼容分支，而不是默认路径。<br>4. 在 `UScriptEditorSubsystem` 或 editor service getter 上暴露只读 presentation service，让项目级 editor subsystem、未来 `BlueprintImpact` 面板和脚本 action presenter 能在不改模块主文件的情况下复用同一套宿主策略。<br>5. 第二阶段再补更强的锚点语义，例如从 `FToolMenuContext` 或 `ContentBrowser` 选中项计算 popup 锚点，而不是统一居中到父窗口。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/AngelscriptEditorPresentationService.*` 与 `Private/Services/AngelscriptEditorPresentationService.*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段同时追求所有 editor host 的完美挂接，容易把 `ToolMenus`、`BlueprintEditor`、`ContentBrowser` 三套时序一起搅乱；更稳妥的顺序是先把今天最常见的 `MainFrame/LevelEditor` 路径纳入 service，再逐步为 `BlueprintEditor` 等 host 增加 resolver。 |
| 兼容性 | 可增量实施。现有静态 helper 和现有菜单入口都可以继续存在，只是内部改为调用 presentation service；当没有显式 host context 时，仍回退到当前 active-window/`LevelEditor` 逻辑，保证旧工作流不被打断。 |
| 验证方式 | 1. 从 `Tools` 菜单、`ContentBrowser` 菜单和一个 `BlueprintEditor` 上下文分别触发同一类交互，确认窗口会挂到正确的父宿主，而不是总跳到主编辑器。<br>2. 在多窗口编辑场景下打开资产列表 popup，确认不会再通过 `HACK_ForceToFront()` 抢错窗口焦点。<br>3. 保留一个未传 presentation context 的旧入口，确认仍能按今天的 fallback 路径工作。 |

### Arch-35：`script literal asset` 持久化仍依赖全局 `PreSave` 钩子和 debug sidecar，不是正式 authoring contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | script literal 资产的保存/回写是否是 editor-native 的 authoring workflow，项目方能否在不改核心模块的前提下扩展新的 literal 类型或替换持久化后端 |
| 当前设计 | 当前 literal 资产保存链路被挂在全局 `FCoreUObjectDelegates::OnObjectPreSave` 上；保存时若对象位于 `FAngelscriptEngine::Get().AssetsPackage`，就直接进入 `OnLiteralAssetSaved()`。其中真正落地的实现几乎只覆盖 `UCurveFloat`，并要求存在 debug server client，否则直接弹出 “Visual Studio Code extension must be running” 错误；最终文本回写也不是走正式 asset/file workflow，而是通过 runtime debug transport 发送 `ReplaceAssetDefinition` 消息。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:121` — `OnLiteralAssetSaved()` 的第一条有效路径就是把对象尝试转成 `UCurveFloat`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:125` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:127` — 没有 debug server client 时直接提示 “Visual Studio Code extension must be running to save a script literal curve”。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:131` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:331` — 曲线保存逻辑把 key/frame 序列化成文本命令与 ASCII 预览，再调用 `ReplaceScriptAssetContent(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:333` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:335` — 不是 `UCurveFloat` 时直接提示 “Cannot save asset declared as an angelscript asset literal”。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:339` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:347` — `OnLiteralAssetPreSave()` 只按 `AssetsPackage` 判断，命中后在 pre-save 阶段直接回写。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:412` — 模块启动时把这条路径全局挂到 `OnObjectPreSave`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2028` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2034` — `ReplaceScriptAssetContent()` 的实际落地是通过 `DebugServer->SendMessageToAll(EDebugMessageType::ReplaceAssetDefinition, Message)` 广播给 debug client。 |
| 优点 | 现有实现复用了已经存在的 debug transport，不需要另建独立文件同步协议，就能把 editor 中改动过的 literal curve 回推到脚本定义。 |
| 不足 | 这条链路把“保存资产”“和 IDE sidecar 通信”“序列化具体 literal 类型”三件事压在了一起。推断：后续要支持更多 literal 类型、无 debug client 的纯编辑器工作流、项目自定义 literal serializer，或把保存逻辑搬到 `EditorSubsystem`/toolbar/context menu 时，当前全局 pre-save + sidecar 写回模型会快速变成瓶颈；它也让 authoring contract 变得不可见，项目方只能碰巧触发保存，而不能正式调用或替换这一能力。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 把“加载已有资产、PIE 下禁止创建、决定 blueprint 类型、创建 package、`AssetCreated`、`MarkPackageDirty`”收敛成稳定对象方法；资产持久化是显式 authoring workflow，而不是全局 save 钩子副作用。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:119`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:145`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 先把保存/创建行为做成显式 workflow owner，后续才能在其中挂 PIE 检查、校验和用户扩展。 |
| UnrealCSharp | `SDynamicNewClassDialog::FinishClicked()` 在显式完成动作里生成内容、`SaveStringToFile(...)`、关闭窗口并 `OpenSourceFile(...)`；保存发生在明确的 authoring 交互末端，而不是 piggyback 在 UObject pre-save 上。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:765`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:775`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:777`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:781` | 对于脚本/代码类资产，更稳妥的模式是显式 commit authoring，而不是等待引擎统一存盘时再偷偷做转换。 |
| UnLua | `CreateLuaTemplate_Executed()` 先验证 `ModuleName`，然后读取模板并 `SaveStringToFile(...)` 生成目标脚本文件；错误反馈用 notification，而不是让一次普通 asset save 隐式触发外部协议。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:257`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:272`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:274`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:305`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:310` | 即便输出目标是脚本文件，也可以先做成显式 editor command + 校验 + 结果反馈链，而不是隐藏在底层 save delegate 里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 literal 资产保存从“全局 pre-save 副作用”提升成正式的 `literal authoring/persistence service`；旧 curve + debug server 路线先保留为兼容 sink。 |
| 具体步骤 | 1. 新增 `UAngelscriptLiteralAssetPersistenceService`、`IAngelscriptLiteralAssetSerializer` 与 `IAngelscriptLiteralAssetSink`（或等价结构），把“能处理哪种资产”“如何序列化成脚本文本”“通过什么后端应用文本”三件事拆开；第一阶段只内建 `UCurveFloat` serializer 和现有 debug-server sink。<br>2. 把 `OnLiteralAssetSaved()` 里的曲线转文本逻辑迁到 `CurveFloatLiteralSerializer`；`OnLiteralAssetPreSave()` 退化为兼容 adapter，只负责把命中的对象转交给 persistence service，而不再直接拼文本和弹对话框。<br>3. 给 editor 层新增一个显式入口，例如 `Apply Literal Asset To Script` command 或 content menu action；成功/失败优先走统一 notification/task service。`OnObjectPreSave` 路径在第一阶段继续存在，但只作为 fallback。<br>4. 让 `IAngelscriptLiteralAssetSink` 支持多后端：默认 `DebugServerSink` 继续调用 `ReplaceScriptAssetContent(...)`，后续可增量增加本地文件 sink 或队列化失败结果，而不需要重写 serializer。<br>5. 通过 `UScriptEditorSubsystem` 或新的 registrar 暴露 `RegisterLiteralAssetSerializer(...)` / `RegisterLiteralAssetSink(...)`，让项目方可以为自定义 literal 资产类型补作者化路径，而不是继续改 `AngelscriptEditorModule.cpp`。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/LiteralAssets/*.h` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/LiteralAssets/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就试图同时替换 debug transport、引入本地文件 sink，并覆盖所有 literal 资产类型，风险会直线上升；更稳妥的是先做 service/serializer/sink 拆分，保留今天的 debug-server sink 为默认实现，再逐步增加其它后端。 |
| 兼容性 | 可增量实施。默认配置下，`UCurveFloat` 仍然可以通过现有 debug client/VS Code 工作流保存；变化只是内部改由 persistence service 调度。未迁移的其它 literal 资产类型可以继续保持今天的 “unsupported” 行为，直到项目方或插件内置 serializer 补上。 |
| 验证方式 | 1. 在连着 debug client 的 editor session 中修改一个 literal curve，确认保存后仍能得到与旧实现一致的脚本文本更新。<br>2. 在没有 debug client 的情况下触发显式 `Apply Literal Asset To Script`，确认系统给出结构化错误或 fallback，而不是只在 `PreSave` 时突然弹出 IDE 依赖提示。<br>3. 新增一个试验性 literal serializer 并通过 subsystem/registrar 注册，确认无需修改模块主文件即可接入新的 literal 资产类型。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-34 | UI 展示宿主仍绑定全局/`LevelEditor` 窗口，复杂交互无法跟随真实 editor host | presentation service + host resolver | 高 |
| P1 | Arch-35 | `script literal asset` 保存仍依赖全局 `PreSave` 与 debug sidecar，不是正式 authoring contract | literal persistence service + serializer/sink 拆分 | 高 |

---

## 架构分析 (2026-04-08 17:56:43)

### Arch-36：`BlueprintImpact` 扫描核心已经可复用，但 editor surface 仍停留在 `commandlet-only`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `BlueprintImpact` 是否已经形成“核心扫描服务 + editor 入口”的双层架构，项目方能否通过 `EditorSubsystem` 或菜单挂点复用它 |
| 当前设计 | `BlueprintImpact` 已经有独立的 request/result 结构和导出 API，但当前 editor 集成只把它接到 `UCommandlet`；`AngelscriptEditor` 模块启动时没有注册任何 `BlueprintImpact` 命令、菜单项、toolbar 或 subsystem facade。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:34` — `FBlueprintImpactRequest` 已把 `ChangedScripts` 与 `bIncludeOnlyOnDiskAssets` 抽成显式输入。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:52` — `FBlueprintImpactScanResult` 已把 `MatchingModules`、`CandidateAssets`、`Matches`、`FailedAssetLoads` 抽成稳定输出。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:67` — `ScanBlueprintAssets(...)` 是 `ANGELSCRIPTEDITOR_API` 导出函数，说明扫描核心本身并不依赖 commandlet 宿主。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55` — `Main()` 直接组装 request 并调用 `ScanBlueprintAssets(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:414` — 模块启动只注册 `ToolMenus` startup callback。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` — `RegisterToolsMenuEntries()` 当前只落了 `ASOpenCode`、`ASGenerateBindings` 与 `Function Tests`，没有 `BlueprintImpact` 入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:47` — `UScriptEditorSubsystem` 仅暴露生命周期与 tick 事件，没有 `BlueprintImpact` facade。 |
| 优点 | 扫描逻辑已经与 UI 分离到可测试、可批处理的程度，CI 与命令行场景接入成本低。 |
| 不足 | 推断：当前问题不是“没有扫描能力”，而是“扫描能力没有 editor contract”。项目方若想从 `Tools` 菜单、`ContentBrowser` 右键、`BlueprintEditor` 或 `EditorSubsystem` 触发同一套扫描，只能自己重组 request、重做结果展示与日志格式化；这会导致命令行和编辑器入口逐步分叉。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把生成核心先抽成 `IDeclarationGenerator` 模块接口，再由 `DeclarationGenerator` 模块同时暴露 `ToolMenus` 按钮、toolbar 按钮和通知反馈；UI 只是调用同一个 service。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:16`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:29`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1654`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1579`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1593`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1627` | 先定义稳定 service 接口，再给 editor、console、automation 各挂一个薄适配层，避免“核心逻辑只活在某个入口文件里”。 |
| UnLua | 模块在 `OnPostEngineInit()` 中实例化 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar`，把 editor surface 当成独立对象；命令本身由 `FUnLuaEditorCommands` 统一注册。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:98`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19` | editor 功能不一定要做成 panel，但应该至少成为“可注册命令 + 可挂接 surface”的正式一层，而不是只留在 commandlet。 |
| UnrealCSharp | 模块显式持有 `PlayToolBar`、`BlueprintToolBar`、console commands 与 `DynamicDataSource`，`RegisterMenus()` 只负责把这些能力挂到 editor surface；同一能力既可从按钮触发，也可从其它 editor listener 重用。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:68`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:193`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` | “service owner + 多 surface adapter” 的所有权树更适合把 headless 核心增量回接到 editor。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `BlueprintImpact` 从“公开函数 + commandlet 宿主”升级为“公开 service + 多 editor 入口”，commandlet 退化为其中一个 adapter。 |
| 具体步骤 | 1. 新增 `IAngelscriptBlueprintImpactService` 或 `UAngelscriptBlueprintImpactService`，把 `ScanBlueprintAssets(...)`、request 构建、结果格式化与进度回调收敛到一个正式服务对象中；第一阶段内部仍直接调用现有 scanner。<br>2. 在 `FAngelscriptEditorModule` 中新增 `BlueprintImpact` 命令入口，至少先挂到 `Tools` 菜单；第二阶段再把同一命令接到 `ContentBrowser`/`BlueprintEditor` 上下文菜单。<br>3. 在 `UScriptEditorSubsystem` 或新的 editor facade 上暴露 `RunBlueprintImpactScan(...)`、`BuildImpactRequestFromSelectedAssets(...)` 一类 API，让项目方无需复制 scanner 调用代码即可扩展自定义 UI。<br>4. 保留 `UAngelscriptBlueprintImpactScanCommandlet`，但改为只做参数解析和 exit code 映射，真正执行转发给 service。<br>5. 第三阶段再补一个最轻量的结果 presenter，例如 `Slate` 列表或 notification + report file，避免 editor 入口只能看 log。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactService.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactService.cpp`，以及可选的 `Private/BlueprintImpact/Widgets/*`。 |
| 预估工作量 | M |
| 架构风险 | 最大风险是 editor 入口直接在 UI 线程上全量 `LoadAsset`，把本来适合 batch 的逻辑带进交互卡顿；因此第一阶段应先做 service 抽象和命令接线，再决定是否需要异步执行、进度条和 cancel。 |
| 兼容性 | 向后兼容。`commandlet` 参数、日志和 exit code 可以保持不变；新增的 editor 命令和 subsystem facade 只是复用现有 scanner，不影响现有 CI 脚本。 |
| 验证方式 | 1. 用相同输入分别走 `commandlet` 和 editor 命令，确认 `Matches` 数量与原因集合一致。<br>2. 在项目级 `UScriptEditorSubsystem` 中调用新 facade，确认无需直接依赖 `BlueprintImpactScanCommandlet` 也能触发扫描。<br>3. 回归现有 `commandlet` 自动化路径，确认 JSON 摘要和退出码未变化。 |

### Arch-37：脚本资产虚拟路径仍是扁平根目录，`ContentBrowserDataSource` 没有保留 source/module 层级语义

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产在 `ContentBrowser` 中的虚拟路径模型是否保留了 source path/module 层级，从而支持 folder 级浏览、筛选和项目级扩展 |
| 当前设计 | `UAngelscriptContentBrowserDataSource` 当前把所有资产都压到 `/All/Angelscript/<AssetName>` 单层虚拟根下；编译过滤只接受根路径，folder 枚举和 path 反查几乎为空实现，而资产创建默认目录又在模块入口里单独根据 `RelativeSourceFilePath` 推导。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:19` — payload 已保存 `Asset->GetPathName()`，但只是作为内部字段缓存。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:28` — 虚拟路径固定为 `/All/Angelscript/` + `Asset->GetName()`，没有 source/module 层级。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:44` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:45` — `CompileFilter()` 遇到非根路径直接返回。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:124` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:126` — `EnumerateItemsAtPath()` 为空。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:128` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:130` — `EnumerateItemsForObjects()` 直接 `false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:249` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:254` — `Legacy_TryConvert*ToVirtualPath()` 仍未接通。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:449` — 创建 Blueprint 默认目录时读取 `Class->GetRelativeSourceFilePath()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:454` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:468` — 模块另起一套基于 source subfolder 的目录推导逻辑，未与 data source 共用。 |
| 优点 | 扁平根目录实现简单，短期内能尽快让用户“看见所有脚本资产”。 |
| 不足 | 推断：当前问题已经不只是 workflow 不完整，而是 namespace 自身过于贫血。因为 data source 不知道 folder 语义，项目方无法围绕“当前脚本目录”“某个 module”“选中的虚拟路径”追加 AddNew、批量工具或过滤策略；同时 browse/create 两条路径各自维护目录语义，后续演进很容易继续分叉。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `DynamicDataSource` 明确维护 `virtual path <-> internal path` 映射、folder/file 双类型过滤、路径内枚举和 `AddNewContextMenu`；folder 层级是正式 contract，而不是显示上的附属品。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:146`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:152`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:169`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:282`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:409`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:666`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:683`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:788` | 一旦决定走 `ContentBrowserDataSource` 路线，就应该把 path tree、folder item、selected path menu 和 path conversion 一起做成一层。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 的资产 identity 直接绑定到调用者传入的 `InPath`，最终 package 名是 `/Game` + path + name；路径语义是资产工作流的一部分，而不是 UI 层临时推导。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:90`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 即便不使用虚拟 data source，也要让“资产在哪个层级里”成为正式输入，而不是后置 heuristic。 |
| UnLua | 不走虚拟 asset data source，但 `BlueprintToolbar`/`ModuleLocator` 会根据当前 `Blueprint` 或 package 路径推导稳定的 `LuaModuleName`，脚本 identity 仍然绑定到 source/package 层级。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:25`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:159`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:165`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:169`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:173` | 参考点不一定是“也做虚拟 data source”，而是“source/module path 应该成为 editor integration 的一等概念”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `Angelscript` 引入统一的 script asset path mapper，让 browse/create/context menu 都围绕同一套 source/module 层级语义工作。 |
| 具体步骤 | 1. 新增 `FAngelscriptAssetPathMapper`（或等价 service），统一定义 `Asset/InternalPath/VirtualPath/RelativeSourcePath` 的映射规则；第一阶段至少支持“按 script root + source subfolder”生成多层虚拟路径。<br>2. 改造 `UAngelscriptContentBrowserDataSource`：补 `BuildRootPathVirtualTree` 等价逻辑、`CreateFolderItem()`、`EnumerateItemsAtPath()`、`EnumerateItemsForObjects()` 和 `Legacy_TryConvert*ToVirtualPath()`，让 subfolder 成为真正可枚举的浏览层级。<br>3. 把 `ShowCreateBlueprintPopup()` 里的默认目录推导改为调用同一个 path mapper，不再在模块入口里单独解析 `RelativeSourceFilePath()`。<br>4. 在 path mapper 稳定后，再追加 `ContentBrowser.AddNewContextMenu` 或 script-side API，使项目方能对“当前脚本目录”注册创建/批量处理工具。<br>5. 为兼容现有行为，第一阶段保留旧的扁平路径解析作为 alias，把老 bookmark 或 selection 恢复到新的层级路径上；确认迁移稳定后再考虑移除 alias。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/ContentBrowser/AngelscriptAssetPathMapper.h` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ContentBrowser/AngelscriptAssetPathMapper.cpp`。 |
| 预估工作量 | L |
| 架构风险 | 主要风险是路径迁移会影响 `ContentBrowser` 的缓存状态、用户 bookmark 和未来脚本资产引用串。为避免一次性破坏，必须先做 alias 和双向 path conversion，再逐步把创建入口切到新 path mapper。 |
| 兼容性 | 可增量实施。现有资产对象和 `AssetsPackage` 不需要迁移；变化主要在虚拟路径与浏览层级。保留旧 flat path alias 后，现有脚本用户不需要立刻调整工作流。 |
| 验证方式 | 1. 在至少两个不同 source subfolder 下生成脚本资产，确认 `ContentBrowser` 能按 folder 浏览，而不是只看到同一层级列表。<br>2. 从某个脚本 subfolder 触发“创建 Blueprint/DataAsset”，确认默认目录与浏览器中该对象的虚拟路径一致。<br>3. 验证旧 flat path 书签或选择恢复后仍能定位到正确对象，确认 alias 生效。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-36 | `BlueprintImpact` 已有扫描核心，但缺少 editor-facing service/command/subsystem contract | service facade + multi-surface adapters | 高 |
| P1 | Arch-37 | `ContentBrowserDataSource` 的路径模型仍是扁平根目录，source/module 层级没有成为正式 contract | path mapper + hierarchical virtual tree | 高 |

---

## 架构分析 (2026-04-08 18:09:47)

### Arch-38：脚本 `ToolMenu` 扩展仍依赖 `section-name` 删除与 `full reload` 全量重建

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ToolMenus` 扩展的拥有者模型、失效粒度与 reload 后的一致性 |
| 当前设计 | `UScriptEditorMenuExtension` 的 editor 菜单注册仍是“`OnPostReload(bFullReload)` 触发时，全量 `UnregisterExtensions()/RegisterExtensions()`；`ToolMenu` surface 以脚本类名当 `SectionName`，清理时按 section 名直接删除”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25` — `InitializeExtensions()` 只在 `bFullReload` 为真时触发 `UnregisterExtensions()/RegisterExtensions()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:47` — `GetRegisteredExtensionSnapshots()` 只暴露 `Location`、`ExtensionPoint`、`SectionName`，没有 owner/signature/version。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:845` — `RegisterExtensions()` 每次都 `TObjectIterator<UASClass>` 全量扫描脚本扩展类。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1094` — `ToolMenu` 分支把 `SectionName` 直接设成 `CDO->GetClass()->GetFName()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1107` — section 内通过 `AddDynamicEntry(...)` 动态构建条目。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:835` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:836` — 反注册时不是按 owner/handle，而是 `ExtendMenu(...)->RemoveSection(SectionName)`。 |
| 优点 | 实现非常直接，full reload 后可以粗暴恢复到单一已知状态；不需要为每个脚本扩展维护单独 owner object。 |
| 不足 | `ToolMenu` 清理是 section 级破坏式操作，而不是 owner-scoped 回收。推断：当脚本扩展只发生局部变更、元数据改动或未来引入非 full reload 更新路径时，菜单状态容易出现“未刷新”“整段 section 被误删/重建”“无法判断哪个扩展真正变更”的问题；这也让项目方很难在 `EditorSubsystem` 中做稳定的增量治理。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 不把 editor surface 做成 class-scan registry，而是模块启动时显式创建 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个长期 owner；`MainMenuToolbar` 自己持有 `CommandList`，再把固定入口挂到 `ToolMenus`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82` | surface owner 是稳定对象时，reload 不需要靠“删整段 section 再重扫全类”来恢复状态。 |
| puerts | `DeclarationGenerator` 把 `ToolMenus` 注册包在 `FToolMenuOwnerScoped OwnerScoped(this)` 里；模块 `ShutdownModule()` 对称执行 `UToolMenus::UnRegisterStartupCallback(this)`、`FGenDTSCommands::Unregister()` 与 style shutdown。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1576`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1654`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1690` | `ToolMenus` owner identity 应该属于 feature owner，而不是临时拼出的 section 名。 |
| UnrealCSharp | 模块在 `RegisterMenus()` 中用 `FToolMenuOwnerScoped OwnerScoped(this)` 装配菜单，关闭时统一 `UToolMenus::UnregisterOwner(this)`；toolbar 本身又是独立 owner，可单独 `Initialize()/Deinitialize()`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:127`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:132`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:193`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:196`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:17`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21` | 先有 owner，再谈 surface；这样增量失效和 shutdown 都能围绕 owner 做，而不是围绕 menu tree 的结构副作用做。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把脚本 `ToolMenu` 扩展从“section-name + full reload rescan”改成“owner-scoped registry + diff-based invalidation”。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorMenuRegistration` 或等价 owner object，最少持有 `ExtensionId`、`SurfaceId`、`SectionName`、`SignatureHash`、`FDelegateHandle/OwnerToken`；`ToolMenu` 分支不再直接把 class 名当唯一身份。<br>2. 对 `ToolMenu` surface 引入真正的 owner-scoped 注册路径：优先让 native registry/service 持有 owner，再由 `UToolMenus` 按 owner 回收；现有 `RemoveSection(SectionName)` 先保留为 fallback。<br>3. 把 `InitializeExtensions()` 的 reload 钩子改为“每次 `OnPostReload` 都做 diff”，先比对扩展类集合与签名；只有 `SignatureHash` 变化的扩展才重建。full reload 仍保留为兜底路径。<br>4. 扩展 snapshot/diagnostics，至少输出 `ExtensionId`、`SignatureHash`、`OwnerClassPath`、`LastReloadMode`，便于定位“为什么某个菜单没更新”。<br>5. 第二阶段再把 `ToolMenu` 之外的 `LevelEditor`/`ContentBrowser` extender 也收口到同一 registry，使 `EditorSubsystem` 后续可以统一订阅和治理。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/MenuRegistry/*`。 |
| 预估工作量 | M |
| 架构风险 | 增量 diff 如果只比较 class 名而不比较函数元数据/排序/可见性规则，会出现“扩展已变但未刷新”的新型脏状态；因此第一阶段最好保留 full rebuild fallback。 |
| 兼容性 | 可向后兼容。现有 `UScriptEditorMenuExtension` 子类、`ExtensionPoint`、函数元数据和菜单外观都可保持不变；变化先限制在 native 注册/回收机制内部。 |
| 验证方式 | 1. 修改一个 `ToolMenu` 脚本扩展的 `Category/ToolbarLabel` 或 `ActionIsVisible` 元数据，只触发非 full reload，确认菜单能刷新。<br>2. 连续做两次 full reload 与一次模块卸载，确认没有遗留 section、重复 dynamic entry 或空回调。<br>3. 抓取新的 registry snapshot，确认同一扩展在 reload 前后保持稳定 `ExtensionId`，只在签名变化时重建。 |

### Arch-39：editor 扩展 API 的 `public surface` 仍直接暴露具体 editor primitive，导致 build boundary 与扩展模型一起耦合

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 扩展能力的公开边界是否足够薄，能否在不拖入整套 `ToolMenus/Slate/Kismet` 细节的情况下被项目方复用 |
| 当前设计 | `AngelscriptEditor` 目前把大量 editor 细节直接放进 public 头与 public 目录：模块公开依赖已经包含 `UnrealEd`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`AssetTools`；而 `UScriptEditorMenuExtension` 的 public 头又直接编码 `LevelViewport_* / ContentBrowser_* / ToolMenu` surface 与 `ToolMenus` 相关类型。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12` 到 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:26` — `PublicDependencyModuleNames` 已直接公开 `UnrealEd`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:3` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:6` — public 头直接 include `UICommandList`、`AssetData`、`ToolMenuDelegates`、`ToolMenuSection`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:9` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:32` — public enum 直接把支持 surface 编码成 `LevelViewport_*`、`ContentBrowser_*`、`ToolMenu`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:82` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:94` — `GetToolMenuContext()` 与 registry snapshot 也是 public API 的一部分。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:1` — 注册逻辑与 prompt 实现本身也位于 `Public/EditorMenuExtensions/` 目录。 |
| 优点 | 对当前实现来说非常直接：native 调用方一 include public 头，就能接触到完整的 editor 菜单模型，不需要再经过额外的 registry/service 抽象。 |
| 不足 | 公开边界已经把“如何接 Unreal editor”与“项目方如何声明扩展”绑成同一件事。推断：后续一旦想把扩展模型从 menu-centric 演进到 service-centric、surface-adapter-centric 或 `EditorSubsystem` registry-centric，public 头、`Build.cs` 和项目侧 include 都会一起受影响，迁移成本偏高。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | surface wiring 基本都收在 `Private/Toolbars/*`；模块只在内部创建 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个 owner，没有把 `LevelEditor/BlueprintEditor` 的接线细节公开成通用 public 枚举。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:15`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:15`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/AnimationBlueprintToolbar.cpp:15` | 公开给外部的应是能力和入口，不一定是所有 surface 的接线细节。 |
| puerts | public 侧主要暴露 `IDeclarationGenerator` 这种薄接口；真正的 `ToolMenus`/toolbar/style wiring 留在 `DeclarationGenerator.cpp` 私有实现里。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:16`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:29`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573` | 先暴露稳定语义接口，再把 editor 表面细节藏在模块私有实现中，更利于后续替换 UI/入口。 |
| UnrealCSharp | public 头 `UnrealCSharpEditor.h` 主要暴露模块与 feature owner 成员；真正的 `ToolBar`、`ContentBrowser` 行为实现在 `Private/ToolBar/*` 与 `Private/ContentBrowser/*`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:5`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:1`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:1`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:1` | 即使 feature owner 类型需要 public 可见，surface-specific wiring 仍然可以保持 private，避免 public API 过早固化。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 public 侧收缩成“声明式 descriptor + service/subsystem 接口”，把 `ToolMenus/Slate/Kismet` 接线细节下沉到 private adapter 层。 |
| 具体步骤 | 1. 新增薄 public contract，例如 `FAngelscriptEditorSurfaceId`、`FAngelscriptEditorExtensionDescriptor`、`IAngelscriptEditorExtensionRegistry` 或等价接口；这些类型只表达“要注册什么”，不直接暴露 `FToolMenuSection`、`FUICommandList`。<br>2. 把 `UScriptEditorMenuExtension` 的 native 注册逻辑、`ScriptEditorPrompts` 的 UI 组装和具体 surface adapter 迁到 `Private/`；public 侧保留脚本声明所需的最小属性与查询 API。<br>3. 重构 public 头后，回收 `AngelscriptEditor.Build.cs` 里的 public 依赖，优先把 `BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`AssetTools` 迁回 `PrivateDependencyModuleNames`；只有确实被 public 头使用的模块才继续保留为 public。<br>4. 给 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 增加 typed getter 或 registry 访问入口，让项目方通过 service 接口拿能力，而不是直接 include 具体 menu/prompt 头。<br>5. 兼容阶段保留旧 `ScriptEditorMenuExtension.h` 名称与常用字段，但内部逐步转发到新 descriptor/registry，并在文档中标明新的扩展入口。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；迁移 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp` 到新的 `Private/EditorMenuExtensions/*`，并新增 `Public/ExtensionContracts/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就一次性删除旧 public 头或大幅改字段名，会直接打断项目侧已有 include 和脚本习惯；更稳妥的是先做 wrapper/deprecated adapter，再逐步瘦身 public 依赖。 |
| 兼容性 | 可增量实施。现有 `UScriptEditorMenuExtension`、`UEditorSubsystemLibrary::GetEditorSubsystem()` 和大部分脚本声明字段都可先保留；变化主要体现在 native include 路径与内部注册路径。 |
| 验证方式 | 1. 建一个仅依赖 `AngelscriptEditor` public 头的最小 native 调用点，确认迁移后不再被迫包含 `ToolMenus/Slate` 细节。<br>2. 回归现有脚本扩展示例，确认旧的 `UScriptEditorMenuExtension` 字段和函数元数据仍然可用。<br>3. 检查 `AngelscriptEditor.Build.cs` 调整后，依赖该模块的编译单元不会因为 public 头仍残留重 editor include 而失败。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-38 | 脚本 `ToolMenu` 扩展仍是 `section-name` 清理 + `full reload` 全量重建 | owner-scoped registry + diff invalidation | 高 |
| P2 | Arch-39 | editor 扩展 public API 直接暴露具体 editor primitive，build boundary 过宽 | public contract 瘦身 + private adapter 下沉 | 中高 |

---

## 架构分析 (2026-04-08 18:18:41)

### Arch-40：`AngelscriptData` 还是一次激活的查询型数据源，缺少 `ContentBrowser` 侧的主动刷新 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ContentBrowserDataSource` 的失效通知、增量刷新、创建入口回接 |
| 当前设计 | `Angelscript` 目前在 `OnPostEngineInit` 后一次性创建并激活 `UAngelscriptContentBrowserDataSource`，数据源自身只有查询型 override，没有和脚本 reload / 文件变化建立正式的刷新桥。文件监听与 reload 副作用都落在其它 helper 上，`ContentBrowser` 侧没有等价的 `UpdateHierarchy()` 或 `NotifyItemDataRefreshed()` 通道。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:118` — `OnEngineInitDone()` 里只做 `NewObject<UAngelscriptContentBrowserDataSource>()`、`Initialize()` 和 `ActivateDataSource("AngelscriptData")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:5` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:8` — `Initialize()` 仅调用 `Super::Initialize(true)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:65` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:120` — 查询时临时从 `FAngelscriptEngine::Get().AssetsPackage` 枚举对象并返回 item，没有内部层级缓存或更新入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:89` — 文件变化只被压入 `Engine.FileChangesDetectedForReload` / `FileDeletionsDetectedForReload`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:139` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:154` — reload 后只刷新 `FBlueprintActionDatabase` 并 `BroadcastBlueprintCompiled()`，没有 `ContentBrowser` 数据源刷新调用。 |
| 优点 | 实现面很薄，当前脚本资产数量不大时几乎不需要维护额外缓存；数据源和编译/重载逻辑暂时解耦，定位查询代码比较直接。 |
| 不足 | 数据源缺少主动 invalidation 通道。推断：当脚本全量 reload、目录变化或 script asset 结构变化发生后，`ContentBrowser` 是否立即看到最新状态主要依赖浏览器自身重新查询时机，而不是插件明确发出的“数据已变更”信号；这会让虚拟资产视图与真实脚本状态之间的时序关系不稳定。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `UDynamicDataSource` 初始化时直接订阅 `OnDynamicClassUpdated`、`OnEndGenerator`，并向 `ContentBrowser.AddNewContextMenu` 注入创建入口；层级变化后通过 `SetVirtualPathTreeNeedsRebuild()` + `NotifyItemDataRefreshed()` / `QueueItemDataUpdate(...)` 主动刷新浏览器。模块还持有 `TStrongObjectPtr<UDynamicDataSource>` 并在关闭时 `Reset()`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:88`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:729` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:759`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:104` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:106`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:164` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:180` | 如果选择虚拟数据源路线，就必须把“变化来源”和“浏览器刷新”接成闭环，而不是停留在一次性激活。 |
| UnLua | 没有额外铺一层虚拟 `ContentBrowserDataSource`，目录监听只服务于脚本热更本身；也就是说它没有把编辑器暴露成一个需要长期保持同步的虚拟资产树。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:36` | 反向借鉴点是：若虚拟资产面暂时还不想承担刷新 contract，就不要让用户依赖一个看起来像“原生资产源”但没有变化通知的表面。 |
| puerts | `DeclarationGenerator` 的编辑器入口全部挂在稳定的 `ToolMenus`/console command 上，公开接口则收敛在 `IDeclarationGenerator`，没有再额外引入一个需要同步的虚拟内容层。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:16` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:33`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1655` | 另一条可借鉴路线是先把“生成/创建”能力做成稳定工具入口，等刷新和层级 contract 准备好后再引入虚拟内容面。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `AngelscriptData` 增加最小可用的“事件源 -> 数据源刷新 -> 创建入口”闭环，先补主动刷新，再补更细粒度 diff。 |
| 具体步骤 | 1. 在 `FAngelscriptEditorModule` 或新的 `ContentBrowserFeature` 中持有 `TStrongObjectPtr<UAngelscriptContentBrowserDataSource>`，替代匿名 `RootSet` 对象，补显式 `Shutdown()` / `DeactivateDataSource()`。<br>2. 在 `UAngelscriptContentBrowserDataSource` 新增 `RequestFullRefresh()` 与 `RequestPathRefresh(...)`，第一阶段先做全量刷新：调用 `SetVirtualPathTreeNeedsRebuild()`，并在 UE 版本允许时调用 `NotifyItemDataRefreshed()`；低版本回退到 `QueueItemDataUpdate(...)`。<br>3. 把三个已有变化源接进来：`FAngelscriptClassGenerator::OnPostReload`、目录监听入队完成点、`ShowCreateBlueprintPopup()` 创建成功后的 asset 新增点；这些点不直接碰 UI，只调用数据源的 refresh API。<br>4. 把现有“创建 Blueprint/DataAsset”入口补到 `ContentBrowser.AddNewContextMenu`，让用户在 `/All/Angelscript/...` 选中路径时走同一条创建逻辑，而不是只能依赖旁路 popup。<br>5. 第二阶段再缓存 item key/path，按新增、删除、重命名生成细粒度 update；第一阶段保留全量刷新兜底，确保行为稳定优先。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`。 |
| 预估工作量 | M |
| 架构风险 | 风险主要在刷新时序。若在 script reload 尚未稳定时立即通知浏览器，可能看到瞬时空列表或重复 item；因此需要把 refresh 放在 reload 完成点，而不是文件变化刚入队时。 |
| 兼容性 | 可增量实施。第一阶段不改现有虚拟路径和 popup 行为，只增加主动刷新与 `Add New` 入口；现有脚本资产对象和外部使用路径保持不变。 |
| 验证方式 | 1. 在编辑器打开 `/All/Angelscript` 时执行一次 script full reload，确认视图无需手工切换也能更新。<br>2. 通过“创建 Blueprint/DataAsset”新增一个脚本派生资产，确认新 item 直接出现在当前浏览路径。<br>3. 删除或重命名相关脚本文件后，确认浏览器 item 数量与 `AssetsPackage` 内对象一致，没有残留幽灵项。 |

### Arch-41：`UScriptEditorSubsystem` 目前只是 `Initialize/Deinitialize/Tick` 壳层，还不是正式的 editor 扩展总线

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户是否能通过 `EditorSubsystem` 稳定扩展菜单、reload、文件监听与 asset 工作流 |
| 当前设计 | `UScriptEditorSubsystem` 现在只提供 `BP_ShouldCreateSubsystem`、`BP_Initialize`、`BP_Deinitialize`、`BP_Tick` 四类生命周期入口；真正的 editor 事件订阅与功能注册仍直接散落在模块启动代码、`UScriptEditorMenuExtension` 静态初始化和 `ClassReloadHelper` 的 lambda 中。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:26` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:84` — `UScriptEditorSubsystem` 只封装创建、初始化、反初始化和每帧 `BP_Tick`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:20` — 对外只额外暴露一个通用 `GetEditorSubsystem()` getter。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:416` — `StartupModule()` 直接注册 source navigation、`OnPostEngineInit`、菜单扩展初始化、目录监听、settings、runtime bridge lambda、`OnObjectPreSave` 和 `UToolMenus` callback，没有把这些 editor 事件转发给 subsystem。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:44` — 菜单扩展通过静态 `OnPostReload` / `OnEnginePreExit` 直接完成注册与注销。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:132` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:154` — reload 后的 `BlueprintActionDatabase` 刷新和 `BroadcastBlueprintCompiled()` 也是内部 lambda 副作用。 |
| 优点 | 当前壳层足够轻，项目方至少可以得到一个 editor 生命周期稳定、可 tick 的脚本对象；对简单“常驻服务/轮询逻辑”已经够用。 |
| 不足 | `EditorSubsystem` 还拿不到 editor 关键事件，也没有 handle-based 注册器。推断：用户若想用 subsystem 扩展菜单、订阅 reload、响应目录变化或接管 asset 入口，仍然需要依赖其它静态类或轮询状态，而不是通过 subsystem 拿到正式、可组合的 editor contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块启动时显式构建 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个长期 owner，`OnPostEngineInit()` 再统一 `Initialize()`；其中 `FMainMenuToolbar` 自己持有 `FUICommandList` 并完成 action map。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:45`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:94` | editor 扩展面先收敛到长期 owner 对象，再决定是否向脚本暴露，比把所有事件埋进模块 lambda 更容易形成正式 API。 |
| puerts | 公开层只暴露 `IDeclarationGenerator` 这样的薄接口；文件监听则包装成 `UPEDirectoryWatcher`，对象内部持有目录、`DelegateHandle` 和 `OnChanged` delegate，析构自动 `UnWatch()`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:16` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:33`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:9` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:18`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:64` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:89` | 对外扩展点应该是“可订阅、可清理、可复用”的 service/object，而不是只有一个通用 getter。 |
| UnrealCSharp | 模块公有成员里直接持有 `FEditorListener` 与 `UDynamicDataSource`；`FEditorListener` 内部集中管理 `OnPostEngineInit`、PIE、generator、asset registry、主窗口激活和目录监听等 editor 事件。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/Listener/FEditorListener.h:5` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/Listener/FEditorListener.h:73`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:133` | 先把 editor 事件收敛到一个 listener/service，再决定谁消费这些事件；这正是 `EditorSubsystem` 可以承接的角色。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `UScriptEditorSubsystem` 生命周期壳层，但在其上补一个事件驱动的 editor extension hub，让 subsystem 真正成为脚本侧正式扩展入口。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorEventHub` 或等价 native service，集中管理 `OnToolMenusReady`、`OnScriptFilesQueuedForReload`、`OnBeforeScriptReload`、`OnAfterScriptReload`、`OnContentBrowserDataRefreshed`、`OnAssetEditorOpened` 等 typed delegate；模块和 helper 不再各自埋匿名 lambda，而是统一向 hub 发事件。<br>2. 扩展 `UScriptEditorSubsystem`：新增 script-callable 注册 API，例如 `BindOnAfterScriptReload`、`BindOnToolMenusReady`、`RegisterDirectoryWatch`、`RegisterToolMenuEntry`；返回 handle 或 token，支持明确解除绑定。<br>3. 把 `UScriptEditorMenuExtension::InitializeExtensions()` 和 `ClassReloadHelper` 的 reload 副作用先接到 hub，再由旧静态逻辑与新 subsystem API 同时消费，确保迁移期双轨可用。<br>4. 对最常见的 editor surface 先做一层 typed wrapper，而不是直接暴露底层 `UToolMenu*` / `FUICommandList`；例如先支持 `MainFrame.MainMenu.Tools`、`ContentBrowser` 路径菜单和 reload 事件，后续再扩面。<br>5. 第二阶段再把目录监听、`ShowCreateBlueprintPopup()`、source navigation 等旁路能力收口到 subsystem service 上，使项目方新增 editor 功能时不再需要修改模块入口。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ExtensionHub/*`。 |
| 预估工作量 | L |
| 架构风险 | 主要风险是把原先分散在模块和 helper 的时序收口后，某些回调触发顺序会变化；若事件定义过粗，脚本侧又会退回轮询。第一阶段应只暴露已经稳定存在的事件，不一次性把所有内部细节都公开。 |
| 兼容性 | 可向后兼容。现有 `BP_Initialize/BP_Deinitialize/BP_Tick`、`UScriptEditorMenuExtension` 和现有菜单扩展脚本都可以保留；新 hub 先作为附加能力引入，后续再逐步迁移旧静态入口。 |
| 验证方式 | 1. 写一个最小 script editor subsystem，只绑定 `OnAfterScriptReload` 与 `OnToolMenusReady`，确认无需轮询即可收到事件。<br>2. 保留现有 `UScriptEditorMenuExtension` 示例，确认引入 hub 后旧菜单仍按原路径注册。<br>3. 执行一次脚本 reload、一次目录变更和一次编辑器关闭，确认 subsystem 解绑后没有重复回调或悬挂句柄。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-40 | `ContentBrowserDataSource` 缺少主动刷新与 `Add New` 闭环 | refresh contract + data source lifecycle 收口 | 高 |
| P1 | Arch-41 | `UScriptEditorSubsystem` 仍是轮询壳层，不是正式 editor 扩展总线 | event hub + handle-based subsystem API | 高 |

---

## 架构分析 (2026-04-08 18:28:18)

### Arch-42：`ContentBrowser` item 语义仍被压成统一的 `Script Asset`，脚本内容没有进入 UE 的内容分类体系

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产在 `ContentBrowser` 中是否携带足够的 item taxonomy 与 collection/plugin 语义，供浏览器、扩展器和项目侧 `EditorSubsystem` 复用 |
| 当前设计 | `UAngelscriptContentBrowserDataSource` 为所有脚本对象统一创建 `Type_File | Category_Asset` item；类型显示名、`Class` 与 `Type` 属性都被硬编码为同一个 `"Script Asset"`，并且固定标成 `ProjectContent=true`、`PluginContent=false`。与此同时，`GetItemAttributes()`、`TryGetCollectionId()` 仍为空实现，意味着 item 只有“能显示出来”的最小语义，没有进入更细的 `ContentBrowser` 分类体系。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:25` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:28` — `CreateAssetItem()` 把所有对象统一包装为 `EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:149` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:152` — `ItemTypeName/NAME_Class/Type/ItemTypeDisplayName` 全部返回 `INVTEXT("Script Asset")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:161` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:170` — item 被固定标记为 `ProjectContent=true`、`PluginContent=false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:177` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:179` — `GetItemAttributes()` 直接 `return false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:220` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:222` — `TryGetCollectionId()` 未实现。 |
| 优点 | 语义面极薄，初期只要有 `UObject` 和缩略图就能把脚本对象展示到浏览器中，接入成本很低。 |
| 不足 | item 语义被压平成单一 `"Script Asset"` 后，浏览器无法区分“脚本类资产”“literal 资产”“由插件提供的脚本内容”或“项目侧脚本内容”；项目扩展者也没有稳定的语义钩子去追加 icon、collection、filter preset 或基于类型的右键行为。推断：后续即使补了刷新和路径树，用户看到的仍会是“一桶通用资产”，而不是可被编辑器其它表面识别的脚本内容类型。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `DynamicDataSource` 明确把 folder/file 分开建模，folder/file 都带 `Category_Class`，并在可用时追加 `Category_Plugin`；payload 还把 `ItemTypeDisplayName` 明确为 `"DynamicClass"`，`TryGetCollectionId()` 则直接返回 `AssetData` 对应的对象路径。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:621` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:637`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:788` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:821`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:824` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:833`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicFileItemDataPayload.cpp:11` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicFileItemDataPayload.cpp:18` | 虚拟 item 不是“只要能显示就行”，而是要把类别、plugin 归属、collection 身份一起做成正式 contract。 |
| puerts | 不额外发明 generic virtual item；`UPEBlueprintAsset::LoadOrCreate()` 直接在真实 `Package` 下创建或加载 `UBlueprint`/`UTypeScriptBlueprint`，随后调用 `FAssetRegistryModule::AssetCreated(Blueprint)`，让内容浏览器天然继承 UE 原生 asset taxonomy。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:94` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:113`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:144` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 如果脚本派生产物可以落到真实 asset 上，就优先复用原生 asset 语义，而不是在虚拟层重新造一个过于粗糙的“脚本资产”类别。 |
| UnLua | 编辑器侧直接围绕 `AssetRegistry` 中的原生 `UBlueprint` / `UWidgetBlueprint` 工作，`UpdateAll()` 用 `FARFilter` 取真实蓝图资产，而不是给 Lua 关联内容额外制造一个统一的虚拟资产类型。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:49` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:60` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:73` | 另一条稳妥路线是尽量依附现有资产分类面，而不是把语言插件相关内容都扁平化为一个新的 generic item label。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持当前 `/All/Angelscript` 入口不变的前提下，为脚本 item 增加语义描述层，让 `ContentBrowser` 看见的不再只是统一 `"Script Asset"`。 |
| 具体步骤 | 1. 新增 `FAngelscriptContentBrowserItemSemantic` 或等价 descriptor，至少承载 `CategoryFlags`、`TypeDisplayName`、`bIsProjectContent`、`bIsPluginContent`、`CollectionId`、`IconName`；第一阶段只在 native 内置几类脚本 item 上使用。<br>2. 重构 `CreateAssetItem()`：不要再把所有对象都落到固定 `Category_Asset`，而是根据对象来源和脚本语义选择 descriptor；例如 script-generated class proxy、literal asset、project script asset、plugin-shipped script asset 至少能返回不同的 `TypeDisplayName`。<br>3. 兑现 `GetItemAttributes()` 与 `TryGetCollectionId()`，把 descriptor 中的扩展属性统一暴露出去；旧的 `GetItemAttribute()` 仍保留兼容路径，但内部改为读 descriptor。<br>4. 通过 `UScriptEditorSubsystem` 或新的 registrar 暴露 `RegisterContentBrowserItemSemantic(...)`，允许项目方为自定义脚本资产类型追加语义，而不是继续修改 `AngelscriptContentBrowserDataSource.cpp`。<br>5. 第一阶段保持现有 virtual path 和缩略图逻辑不变，只增加语义层；等语义稳定后，再决定是否为不同 item kind 配专属 icon 或 `ContentBrowser` context action。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/ContentBrowser/AngelscriptContentBrowserItemSemantic.h` 与 `Private/ContentBrowser/AngelscriptContentBrowserSemanticRegistry.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就试图同时重做 path tree、refresh、authoring 和 item taxonomy，容易把 `ContentBrowser` 改造再次耦合成大手术；更稳妥的是先只补语义描述层，让现有 item 可以被更准确地识别。 |
| 兼容性 | 向后兼容。旧的 `/All/Angelscript/<AssetName>` 路径、当前显示列表和现有脚本资产对象都可以保持不变；变化主要体现在浏览器属性、过滤维度和扩展接口变得更丰富。 |
| 验证方式 | 1. 在 `ContentBrowser` 中检查脚本 item 的 `ItemTypeDisplayName`、project/plugin 标记和 collection identity，确认不再统一显示为 `"Script Asset"`。<br>2. 为一种自定义脚本资产注册新的 semantic descriptor，确认无需修改数据源主逻辑即可改变显示类型与属性。<br>3. 回归现有打开/缩略图路径，确认只增加语义信息，不改变今天的基本浏览行为。 |

### Arch-43：`ContentBrowser` filter contract 仍是 root-only 特判，虚拟脚本项没有真正进入 UE 的浏览器查询模型

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产视图是否完整实现了 `ContentBrowser` 的 query/filter/path conversion contract，从而支持标准浏览、筛选、搜索和对象回桥 |
| 当前设计 | `CompileFilter()` 只接受 `IncludeFiles + IncludeAssets` 且路径必须是根 `/` 的场景；真正枚举时则全量遍历 `AssetsPackage`。与此同时，`EnumerateItemsAtPath()` 为空、`EnumerateItemsForObjects()` 直接 `false`、`DoesItemPassFilter()` 永远返回 `true`，`Legacy_TryConvertPackagePathToVirtualPath()` / `Legacy_TryConvertAssetDataToVirtualPath()` 也都没有实现。结果是当前数据源更像“根路径下的一次性类过滤枚举器”，而不是完整的 `ContentBrowser` 查询后端。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:38` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:48` — `CompileFilter()` 只支持 `IncludeFiles + IncludeAssets`，并要求 `InPath == "/"` 且存在 `ClassFilter`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:81` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:120` — `EnumerateItemsMatchingFilter()` 每次都从 `FAngelscriptEngine::Get().AssetsPackage` 全量枚举对象后再做 include/exclude。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:124` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:126` — `EnumerateItemsAtPath()` 为空实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:128` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:130` — `EnumerateItemsForObjects()` 直接 `return false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:133` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:135` — `DoesItemPassFilter()` 永远返回 `true`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:249` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:255` — 包路径和 `AssetData` 到 virtual path 的反向转换都未实现。 |
| 优点 | 实现路径极短，在当前脚本资产数量有限、且主要从根目录浏览时，展示层成本很低。 |
| 不足 | 这个 contract 无法支撑标准的 `ContentBrowser` 浏览语义：路径内枚举、递归/非递归切换、对象选中回桥、collection/filter 复用和 `AssetData -> virtual path` 恢复都不可靠。推断：只要项目脚本量增长，或用户从搜索、引用、右键菜单、selection restore 这些原生浏览器能力进入，当前 root-only 特判就会不断暴露出“看得见但不真属于浏览器模型”的问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `DynamicDataSource::CompileFilter()` 同时处理 folder/file、virtual/internal path、递归路径和 class permission list；`DoesItemPassFilter()` 会按 folder/class 编译结果精确判定 item 是否命中；`Legacy_TryConvertPackagePathToVirtualPath()` / `Legacy_TryConvertAssetDataToVirtualPath()` 也都能回到 virtual path。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:109` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:220`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:457` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:505`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:666` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:678` | 一旦选择虚拟数据源路线，就必须把 query/filter/path conversion 做完整，而不是只实现根目录枚举。 |
| UnLua | 不在 `ContentBrowser` 里重造一套自定义查询模型，而是直接用 `AssetRegistry` 的 `FARFilter` 获取 `UBlueprint` / `UWidgetBlueprint` 集合，并订阅 `OnAssetAdded/Removed/Renamed/Updated` 做后续同步。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:49` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:60` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:73` | 如果脚本相关能力可以依附现有 asset query plane，就尽量复用 `AssetRegistry` 的成熟过滤语义，而不是手写一个能力残缺的浏览器后端。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 让脚本派生内容落在真实 package-backed `UBlueprint` 上，并通过 `AssetCreated()` 注册进 `AssetRegistry`；因此标准的 `ContentBrowser` 路径、筛选和 `AssetData` 工作流天然可用。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:94` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:113`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:144` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 如果最终还是要支持标准过滤和 `AssetData` 回桥，真实 asset identity 仍是最省维护成本的基线方案。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `AngelscriptData` 增加独立的 query/index 层，先补完整 `ContentBrowser` filter/path contract，再考虑更复杂的 UI feature。 |
| 具体步骤 | 1. 新增 `FAngelscriptContentBrowserIndex` 或等价 service，缓存 `InternalObjectPath`、`VirtualPath`、`AssetClass`、`FolderPath`、`FAssetData` snapshot；第一阶段数据仍可来自 `AssetsPackage`，不强行改成真实 registry asset。<br>2. 重写 `CompileFilter()`：支持 virtual/internal path、folder/file、递归路径和 class filter 的组合，把命中的 folder/item key 预编译进 `CompiledFilters`；旧的根路径行为可作为 fallback alias 保留。<br>3. 让 `DoesItemPassFilter()` 读取编译结果而不是 `return true`；同时补齐 `EnumerateItemsAtPath()` 与 `EnumerateItemsForObjects()`，让浏览器 selection、搜索结果和外部对象回桥能共享同一个 index。<br>4. 基于 index 中的 `FAssetData/InternalPath` 实现 `Legacy_TryConvertPackagePathToVirtualPath()`、`Legacy_TryConvertAssetDataToVirtualPath()` 与 `TryGetCollectionId()`，把搜索、引用和浏览器恢复路径接回标准 contract。<br>5. 通过 `UScriptEditorSubsystem` 或新的 index registrar 暴露 `RegisterContentBrowserIndexContributor(...)`，允许项目方为额外脚本根或自定义脚本资产提供 path/filter 贡献，而不是继续把所有规则写死在数据源主类里。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ContentBrowser/AngelscriptContentBrowserIndex.*`。 |
| 预估工作量 | M |
| 架构风险 | 最大风险是 index 与 reload/创建时序不同步，导致过滤结果比当前列表更“正确”但更容易暴露 stale data；因此第一阶段应保留全量枚举兜底，并把 index 刷新挂到现有 reload 完成点上。 |
| 兼容性 | 可增量实施。旧的 `/All/Angelscript/<AssetName>` 根路径与当前 class include/exclude 行为可以继续保留；新实现只是让更多原生浏览器路径开始真正工作。 |
| 验证方式 | 1. 在 `/All/Angelscript` 子路径与根路径分别执行 folder/file/class 过滤，确认结果一致且不再依赖根目录特判。<br>2. 从 `UObject` 选择、搜索结果和 `AssetData` 反查三条路径进入，确认都能回到同一个 virtual item。<br>3. 触发一次脚本 reload 后重复上述过滤，确认 index 没有残留旧项，也不会把新项漏掉。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-43 | `ContentBrowser` filter/search 仍是 root-only 特判，虚拟脚本项未真正进入 UE 浏览器查询模型 | query/index layer + path/filter contract 补全 | 高 |
| P2 | Arch-42 | 脚本 item 被压成统一 `Script Asset`，缺少 class/plugin/collection 语义 | semantic descriptor + registrar | 中高 |

---

## 架构分析 (2026-04-08 23:37:31)

### Arch-44：虚拟脚本资产仍是“可浏览对象”，还没有进入 UE 的可变更资产合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `/All/Angelscript` 下的虚拟脚本项能否参与 `ContentBrowser` 的编辑、删除、撤销、引用复制与对象回桥工作流 |
| 当前设计 | `UAngelscriptContentBrowserDataSource` 目前仍是 read-mostly 数据源：类声明只覆盖了 `CanEditItem`、`EditItem`、`BulkEditItems`、`AppendItemReference`、`TryGetCollectionId` 与 `Legacy_TryConvert*`，而这些实现又统一返回 `false`；同时数据源根本没有声明 `CanDeleteItem` / `DeleteItem` 之类的 mutation override。结果是脚本项虽然能被浏览器枚举出来，但一进入“删除 / 复制引用 / 选中恢复 / 撤销重做”这类标准 asset workflow，就只能退回旁路 popup，或者完全失效。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h:22` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h:64` — override 列表止于 `CanEditItem`、`EditItem`、`BulkEditItems`、`AppendItemReference`、`TryGetCollectionId` 与 `Legacy_TryConvert*`，未声明 `CanDeleteItem` / `DeleteItem`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:187` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:204` — `CanEditItem()`、`EditItem()`、`BulkEditItems()`、`AppendItemReference()` 全部 `return false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:220` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:256` — `TryGetCollectionId()`、`Legacy_TryConvertPackagePathToVirtualPath()`、`Legacy_TryConvertAssetDataToVirtualPath()` 也都 `return false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` — 创建和打开仍由 `ShowCreateBlueprintPopup()` / `ShowAssetListPopup()` 两条旁路 UI 补齐。 |
| 优点 | 只读实现很保守，短期内不会因为误接 mutation 路径而损坏虚拟脚本项。 |
| 不足 | “能显示”不等于“进入资产工作流”。推断：当用户从 `ContentBrowser` 右键、引用复制、undo/redo、selection restore，或项目自定义 `EditorSubsystem` 进入时，脚本项仍没有正式 mutation identity；这会迫使后续所有资产管理能力继续长在 popup/helper 上，而不是沉入 UE 的标准资产面。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `UDynamicDataSource` 同时实现 `CanEditItem`、`EditItem`、`BulkEditItems`、`CanDeleteItem`、`DeleteItem`、`AppendItemReference`、`TryGetCollectionId` 与 `Legacy_TryConvert*`；删除路径还显式使用 `FScopedTransaction` 与 `GUndo`，把虚拟项接回 UE 的可撤销资产工作流。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:518` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:539`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:542` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:589`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:592` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:678` | 如果决定走虚拟 data source 路线，就应该把 mutation/transaction contract 一起补齐，而不是只做展示层。 |
| puerts | 不把变更语义停留在脆弱的虚拟 item 上，而是让 `UPEBlueprintAsset::LoadOrCreate()` 直接 `LoadObject<UBlueprint>()` / `CreatePackage()` / `AssetCreated(Blueprint)`，把脚本派生产物落到真实 package-backed asset；后续 blueprint 图修改再进入 `FScopedTransaction`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:479` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:482` | 另一条更稳的路线是给脚本派生产物真实 asset identity，让删除、重命名和撤销天然复用 UE 现有资产语义。 |
| UnLua | 不额外发明一个独立的虚拟变更平面，而是围绕真实蓝图资产工作；`AssetRegistry` 层直接订阅 `OnAssetAdded/Removed/Renamed/Updated`，工具链始终站在原生资产变更语义上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:50` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:73`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:284` | 如果脚本内容最终还是归属真实资产平面，editor 工具应优先复用 `AssetRegistry` 的成熟 mutation 语义，而不是在虚拟层重新补半套。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `AngelscriptData` 增加最小可变更资产合同：先补“打开 / 删除 / 复制引用 / 路径回桥”，再决定是否继续保留纯虚拟项路线。 |
| 具体步骤 | 1. 在 `UAngelscriptContentBrowserDataSource` 新增 `CanDeleteItem()` / `DeleteItem()` override，并把 `CanEditItem()`、`EditItem()`、`BulkEditItems()`、`AppendItemReference()`、`TryGetCollectionId()` 与 `Legacy_TryConvert*()` 收口到同一个 `FAngelscriptAssetMutationService` 或等价 service。<br>2. 第一阶段只支持 identity 明确的脚本项：`EditItem()` 优先走 `UAssetEditorSubsystem` 或 source navigation，`AppendItemReference()` 与 path conversion 统一从同一个 resolver 取值，避免再次散落启发式。<br>3. `DeleteItem()` 先实现两类保守语义：对 package-backed 派生资产走 `AssetTools` / `ObjectTools`，对 file-backed 虚拟脚本项走文件删除，并用 `FScopedTransaction` 包裹可撤销操作；暂时无法安全删除的类型继续返回 `false`。<br>4. 把删除、undo/redo 与新增成功后的刷新统一接到现有 `ContentBrowser` refresh path，避免 mutation 生效后浏览器仍残留旧 item。<br>5. 通过 `UScriptEditorSubsystem` 暴露 `RegisterAssetMutationHandler(...)` 或等价 registrar，让项目方可以为额外脚本资产类型追加 delete/edit 策略，而不必继续改主数据源类。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ContentBrowser/AngelscriptAssetMutationService.*`。 |
| 预估工作量 | M |
| 架构风险 | 当前并非所有脚本项都拥有稳定的 file/package 身份；如果第一阶段对所有虚拟项一刀切开放删除，会把 identity 问题直接放大成数据丢失风险。应先限定在 identity 明确的子集上。 |
| 兼容性 | 向后兼容。现有浏览和 popup 打开路径可以保留；新行为只是在支持的 item 上新增标准 `ContentBrowser` 操作，未支持的 item 仍维持今天的只读表现。 |
| 验证方式 | 1. 对一个支持 mutation 的脚本项执行打开、复制引用与删除，确认都能直接从 `ContentBrowser` 完成。<br>2. 删除后执行 undo/redo，确认 item 与底层文件/资产都能正确恢复或移除。<br>3. 对未接入 mutation handler 的脚本项执行相同操作，确认仍安全失败，不会出现半删半留状态。 |

### Arch-45：绑定生成与模块脚手架仍是模块静态流水线，还没有升级为正式的 editor toolchain service

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定生成、模块脚手架和调试型工具能否以统一的 `service + command + headless adapter` 架构被菜单、console 与 `EditorSubsystem` 共同复用 |
| 当前设计 | `FAngelscriptEditorModule` 目前把 `GenerateNativeBinds()`、`GenerateBindDatabases()`、`GenerateNewModule()`、`GenerateBuildFile()`、`GenerateSourceFilesV2()`、`OriginalGenerate()` 全部作为模块静态函数持有；固定 UI 只在 `RegisterToolsMenuEntries()` 中把 `ASGenerateBindings` 与 `Function Tests` 直接绑到 lambda。也就是说，工具链能力虽然存在，但它们没有独立 service owner、没有 console/headless adapter，也没有可被项目级 `EditorSubsystem` 复用的正式接口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:12` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:30`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:55` — 模块头直接声明 `ShowAssetListPopup()`、`ShowCreateBlueprintPopup()`、`GenerateNativeBinds()`、`GenerateBindDatabases()`、`GenerateNewModule()`、`GenerateBuildFile()`、`GenerateSourceFilesV2()`、`OriginalGenerate()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:740` — `RegisterToolsMenuEntries()` 当前只把 `ASOpenCode`、`ASGenerateBindings` 与 `Function Tests` 接到固定菜单；`ASGenerateBindings` 直接 lambda 到 `GenerateNativeBinds()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1081` — `GenerateNativeBinds()` 内部再串联 `GenerateBindDatabases()`，并多次调用 `GenerateNewModule()` 批量生成 runtime/editor bind module。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1285` — `GenerateNewModule()`、`GenerateBuildFile()` 与 `GenerateSourceFilesV2()` 依然是模块内部静态流水线，不是正式 tool service。 |
| 优点 | 调试期接线很快，遗留 bind generator 仍然可以通过一个固定菜单入口继续使用。 |
| 不足 | 工具链缺少稳定身份。推断：项目方若想从 `EditorSubsystem`、console、未来 toolbar 或 CI 侧复用“生成绑定 / 生成模块脚手架”这条能力，只能 include 模块静态 helper，或者重新实现一套旁路入口；任何新增工具也更容易继续长成“再加一个 static helper + 再加一个菜单 lambda”，而不是沉淀成可组合的 toolchain service。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 先定义薄接口 `IDeclarationGenerator`，再由 `FDeclarationGenerator` 持有 `PluginCommands`、`RegisterMenus()`、`Puerts.Gen` console command 和完成通知；菜单、toolbar 与 console 只是同一生成服务的不同投影。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:16` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:32`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1697` | 先把工具链能力做成独立 service/module，再决定挂到哪些 editor surface。 |
| UnrealCSharp | 模块启动时注册 `CodeAnalysis`、`SolutionGenerator`、`Compile`、`Generator` 四个 `FAutoConsoleCommand`；真正的 `Generator()` 又有 `OnBeginGenerator/OnEndGenerator` 事件与 `FScopedSlowTask`，因此菜单、console、listener 可以共享一条正式执行主链。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:68` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:99`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:309` | 除了 command identity，还要把进度、阶段事件和 console 入口一起纳入同一条 toolchain contract。 |
| UnLua | `FUnLuaEditorCommands` 先把 `GenerateIntelliSense`、`HotReload` 等能力注册成正式 `UI_COMMAND`；`MainMenuToolbar` 通过 `CommandList->MapAction(...)` 复用这些命令，同时又保留 `UUnLuaIntelliSenseCommandlet` 作为 headless 入口。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:21` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:30`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:32` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:37`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:124` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:125`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/Commandlets/UnLuaIntelliSenseCommandlet.h:20` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/Commandlets/UnLuaIntelliSenseCommandlet.h:29` | 对开发者工具来说，`UI_COMMAND` 和 headless adapter 都应是正式一层，而不是菜单事件里临时执行一段实现。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把绑定生成与模块脚手架从“模块静态流水线”升级成“toolchain service + command/console adapter”，菜单只保留为展示层。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 内新增 `IAngelscriptEditorToolchainService` 或等价 `UObject` service，第一阶段只吸纳 `GenerateNativeBinds()`、`GenerateBindDatabases()`、`GenerateNewModule()`、`GenerateBuildFile()`、`GenerateSourceFilesV2()`；现有静态函数改成薄 forwarding wrapper。<br>2. 为 service 建立稳定 `ToolId` 与 command 层：至少新增 `Angelscript.Editor.GenerateBindings`、`Angelscript.Editor.GenerateModuleScaffold`、`Angelscript.Editor.FunctionTests` 三个 `FAutoConsoleCommand`；现有 `Tools` 菜单继续保留，但内部统一转发到 service。<br>3. 在第二阶段补 `TCommands` / `UI_COMMAND`，让 `Tools` 菜单、未来 toolbar 和脚本扩展都能复用同一批 `CommandList`，避免继续靠模块 lambda 直连实现。<br>4. 让 service 暴露最小阶段事件与反馈接口，例如 `OnToolchainStarted/OnToolchainFinished`、`BeginTask/EndTask`；第一阶段直接复用现有 `FScopedSlowTask` 或 notification，不必重做 UI。<br>5. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 提供只读 getter，让项目级 editor subsystem、未来自定义 wizard 或 CI adapter 可以调用同一条 toolchain service，而不必 include `FAngelscriptEditorModule` 的静态 helper。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Tools/AngelscriptEditorToolchainService.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tools/AngelscriptEditorToolchainService.cpp`，必要时新增 `Private/Tools/AngelscriptEditorToolCommands.*`。 |
| 预估工作量 | M |
| 架构风险 | 这些静态函数当前直接写文件和目录；如果在未抽出统一 path/context policy 前就强拆成独立模块，容易把历史行为改坏。更稳妥的顺序是先在同模块内做 service owner，再视调用方收敛情况决定是否拆模块。 |
| 兼容性 | 向后兼容。现有 `ASGenerateBindings` 菜单、debug-only 绑定生成语义和已有静态调用点都可以保留；第一阶段变化主要是新增 console/service 入口，而不是替换今天的工具行为。 |
| 验证方式 | 1. 分别从 `Tools` 菜单和 console 触发 `GenerateBindings`，确认生成的 bind module 数量、输出文件和日志结果一致。<br>2. 通过一个最小 `UScriptEditorSubsystem` 调用新的 toolchain service，确认无需直接 include 模块静态 helper 也能发起生成。<br>3. 对一次长时间生成过程检查只出现一次进度/完成反馈，不会因为菜单入口和 service 内核重复汇报。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-44 | 虚拟脚本资产缺少删除/撤销/引用复制等 mutation contract，`ContentBrowser` 仍无法把它们当成可变更资产 | mutation service + transaction contract | 高 |
| P1 | Arch-45 | 绑定生成与模块脚手架仍是模块静态流水线，缺少 `service + command + console` 一致性 | toolchain service + command/console adapter | 高 |

---

## 架构分析 (2026-04-08 23:51:51)

### Arch-46：`AssetRegistry` 在 `AngelscriptEditor` 里仍是查询工具，不是可扩展的 editor 事件总线

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产创建、虚拟浏览器刷新、项目级扩展能否围绕 UE 原生 `AssetRegistry` 事件形成统一同步面 |
| 当前设计 | `AngelscriptEditor` 当前只在少数同步路径里临时取用 `AssetRegistry`：创建 Blueprint 时用 `HasAssets()` 猜默认目录、创建成功后 `AssetCreated()`；`BlueprintImpact` commandlet 做一次性扫描；`ContentBrowserDataSource` 则直接用 `FAssetData(...SkipAssetRegistryTagsGathering)` 临时拼装浏览器项。也就是说，`AssetRegistry` 还没有被提升为 editor 内持续存在的 change bus。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:422` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:461` — `ShowCreateBlueprintPopup()` 只在创建时同步读取 `AssetRegistry`，并用 `HasAssets()` 反推默认目录。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:522` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:523` — 资产创建成功后仅调用一次 `FAssetRegistryModule::AssetCreated(Asset)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:565` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:599` — `ShowAssetListPopup()` 再次临时加载 `AssetRegistryModule`，但仍停留在 popup 层资产选择。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:207` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:242` — 浏览器数据源更新缩略图和 `Legacy_TryGetAssetData()` 时直接构造 `FAssetData(...SkipAssetRegistryTagsGathering)`，没有接回 registry 元数据或事件。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:81` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:85` — `AssetRegistry` 只在 commandlet 扫描时被当作一次性输入。 |
| 优点 | 当前实现很保守，不需要额外维护 asset event 生命周期；脚本资产创建和批量扫描都已经能利用 UE 原生 registry 的基础能力。 |
| 不足 | 推断：由于 editor 层没有 `OnFilesLoaded`、`OnAssetAdded/Removed/Renamed/Updated*` 这类长期订阅面，`ContentBrowser` 刷新、脚本资产分类、literal asset 保存、副工具同步和项目级 `EditorSubsystem` 扩展都只能继续靠 popup、reload 或手工 refresh 猜测时机，而不是消费一个稳定的原生资产变化总线。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaIntelliSenseGenerator::Initialize()` 启动即订阅 `OnAssetAdded/Removed/Renamed/Updated`；`UpdateAll()` 先用 `FARFilter` 拉取真实蓝图资产全集，后续增量变化再走各自 handler。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:49` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:58` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:89`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:290` | 先把 `AssetRegistry` 视为 editor 内的长期变化源，再决定哪些工具消费这些变化。 |
| UnrealCSharp | `FEditorListener` 在 `OnFilesLoaded()` 后继续订阅 `OnAssetAdded/Removed/Renamed/UpdatedOnDisk`；`FClassCollector` 也独立维护一套 `AssetRegistry` 句柄，用于动态类树刷新。模块头显式持有 `FEditorListener` 与 `UDynamicDataSource`，形成清晰所有权树。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:42` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:45`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:251` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:298`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/ClassCollector.cpp:51` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/ClassCollector.cpp:118`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/ClassCollector.cpp:306` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/ClassCollector.cpp:350` | `AssetRegistry` 事件可以同时服务“文件生成同步”和“UI 层级刷新”，关键是先把它收敛成长期 listener/service。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 把“已有则加载、没有则创建、最后 `AssetCreated()`”收敛进稳定 workflow object，`PuertsEditorModule` 再为该资产类型注册专用 compiler。虽然不是 asset event listener，但它把资产语义固定在 package-backed identity 上，而不是零散 popup 状态。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:160` | 即使不直接走 event bus，也应先让脚本资产拥有稳定的原生资产身份，而不是停留在零散查询和虚拟 payload。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入 `AssetRegistry` 驱动的 editor asset service，把“查询 registry”升级为“消费 registry 事件”，并通过 `EditorSubsystem` 暴露给项目方。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorAssetRegistryService` 或 `UAngelscriptEditorAssetRegistryService`，由 editor 模块持有；第一阶段只负责订阅 `OnFilesLoaded`、`OnAssetAdded`、`OnAssetRemoved`、`OnAssetRenamed`、`OnAssetUpdatedOnDisk`，并把它们标准化为统一的 `FAngelscriptEditorAssetChange`。<br>2. 把现有 `ShowCreateBlueprintPopup()` 创建成功后的 `AssetCreated()`、`OnLiteralAssetPreSave()`、`ContentBrowserDataSource` 刷新点和后续 `BlueprintImpact` 选择器都改为通知这个 service，而不是各自直接碰 UI 或手动猜刷新时机。<br>3. 在 `UAngelscriptContentBrowserDataSource` 中新增对该 service 的只读订阅：第一阶段仅做 `RequestFullRefresh()`/`RequestPathRefresh(...)`；仍保留当前虚拟数据源路径和 popup 行为，避免一次性重写资产工作流。<br>4. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 暴露只读 getter 或 `BlueprintAssignable` 事件，让项目级 editor subsystem 可以注册 asset change listener、路径分类器或附加同步器，而不必再去修改 `AngelscriptEditorModule.cpp`。<br>5. 第二阶段再评估是否把 literal asset、virtual asset 和 package-backed Blueprint/DataAsset 的三类变化统一到同一个 asset identity contract；第一阶段先只补事件总线，不改外部使用方式。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/AngelscriptEditorAssetRegistryService.*` 与 `Private/Services/AngelscriptEditorAssetRegistryService.cpp`。 |
| 预估工作量 | M |
| 架构风险 | `Angelscript` 当前同时存在 package-backed asset、virtual item 和 debug-side literal asset；如果第一阶段就强行统一成单一资产模型，容易把现有保存与刷新路径改坏。更稳妥的顺序是先建立事件服务，再逐步收敛身份模型。 |
| 兼容性 | 向后兼容。现有 popup、`AssetCreated()`、`ContentBrowser` 虚拟显示和 commandlet 扫描都可保持；新增 service 先作为附加同步面存在，不要求现有脚本或工具立即迁移。 |
| 验证方式 | 1. 新建、删除、重命名一个脚本相关 Blueprint/DataAsset，确认 service 能收到正确事件且 `ContentBrowser`/附加工具只刷新一次。<br>2. 让一个最小 `UScriptEditorSubsystem` 订阅新 service，确认无需修改核心模块即可观察资产变化。<br>3. 回归 `ShowCreateBlueprintPopup()`、literal asset 保存和 `BlueprintImpact` commandlet，确认现有行为不因引入 asset service 而回归。 |

### Arch-47：reload 修复链已经复制出第二套 `BlueprintImpact` 执行合同，命中范围与候选集存在分叉风险

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `BlueprintImpact` 在 commandlet、未来 editor tool、以及 reload 修复链之间是否共享同一套 request/symbol/candidate/result 合同 |
| 当前设计 | `BlueprintImpactScanner` 已经导出 `FBlueprintImpactRequest`、`FBlueprintImpactScanResult`、`BuildImpactSymbols()` 与 `ScanBlueprintAssets()`，commandlet 也沿用这条合同；但 `FClassReloadHelper::PerformReinstance()` 又手工拼出一份 `FBlueprintImpactSymbols`，并直接用 `TObjectIterator<UBlueprint>` 遍历内存中的蓝图集合。结果是 reload lane 与 scanner lane 已经共享了 `AnalyzeLoadedBlueprint()`，却没有共享“候选集选择”和“symbol 构建”的执行主链。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:34` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:67` — scanner 已经把 request、symbols、scan result 和 `ScanBlueprintAssets(...)` 抽成正式 public contract。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:63` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:85` — commandlet 直接组装 `FBlueprintImpactRequest` 并调用 `ScanBlueprintAssets(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:278` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:309` — scanner 的正式候选集来自 `FindBlueprintAssets(AssetRegistry, Request.bIncludeOnlyOnDiskAssets)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:106` — reload helper 手工根据 `ReloadClasses/ReloadStructs/ReloadEnums/ReloadDelegates/NewDelegates` 拼装 `ImpactSymbols`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:108` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:145` — reload lane 直接用 `TObjectIterator<UBlueprint>` 遍历已加载蓝图，再调用 `AnalyzeLoadedBlueprint()`。 |
| 优点 | reload 修复链可以在不依赖 `AssetRegistry` 查询和 disk 资产过滤的情况下，直接处理当前 session 已加载的 `Blueprint`，对热更修复路径比较直接。 |
| 不足 | 推断：这里的风险不是“多一条专用路径”本身，而是两条路径已经开始各自定义执行语义。scanner lane 通过 `FBlueprintImpactRequest::bIncludeOnlyOnDiskAssets` 和 `CandidateAssets` 明确建模候选集，reload lane 则默认为“所有已加载蓝图”；一旦后续给 `BlueprintImpact` 加 editor service、结果面板或缓存层，两边很容易在命中集合、reason 统计和未来扩展字段上悄悄漂移。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 先定义薄接口 `IDeclarationGenerator`，实际的 `FDeclarationGenerator` 再把 menu、toolbar 和 `Puerts.Gen` console command 都汇聚到 `GenUeDts()` 一条执行主链。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:16` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:32`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1615` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1631`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1687` | 不同 surface 可以不同，但“执行核心”应只有一条。 |
| UnrealCSharp | `GeneratorConsoleCommand` 最终直接调用同一个 `Generator()`；`Generator()` 再统一广播 `OnBeginGenerator/OnEndGenerator`、跑 `FScopedSlowTask`、执行各阶段生成与编译。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:94` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:100`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:309` | 一旦决定给能力挂多入口，就要把阶段、结果和反馈也一并收敛到单一执行链。 |
| UnLua | `FUnLuaEditorCommands` 先定义正式命令，`FMainMenuToolbar` 再把 `GenerateIntelliSense` 映射到 `FUnLuaIntelliSenseGenerator::Get()->UpdateAll()`；菜单构建只消费 command，不重复写第二套生成逻辑。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:32`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:45`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:118` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:140` | command 和 toolbar 是入口层，核心 generator/service 不应该在每个入口里再复制一份中间逻辑。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `BlueprintImpact` 补成“共享执行内核 + 不同候选集 adapter”的结构，让 reload lane 和 commandlet/editor lane 共享同一套 symbol/reason/result contract。 |
| 具体步骤 | 1. 新增 `IAngelscriptBlueprintImpactExecutionService` 或扩展前述 impact service，第一阶段只提供两类入口：`ScanAssetRegistry(const FBlueprintImpactRequest&)` 和 `AnalyzeLoadedBlueprintsForReload(const FAngelscriptReloadDelta&)`；两条入口内部都走同一套 symbol builder、reason collector 和 result formatter。<br>2. 把 `ClassReloadHelper.cpp:83-106` 里的手工 `ImpactSymbols` 构造迁成 `BuildImpactSymbolsFromReloadDelta(...)`；保留 reload lane 的“已加载蓝图”候选集，但不要再在 helper 里直接定义 symbol 语义。<br>3. 再把 `ClassReloadHelper.cpp:108-145` 的蓝图遍历抽成共享 helper，例如 `AnalyzeBlueprintSet(TArrayView<UBlueprint*>, const FBlueprintImpactSymbols&) -> FBlueprintImpactExecutionResult`；reload lane 只消费结果去决定 pin 替换、节点刷新和编译。<br>4. commandlet 继续保留 `FBlueprintImpactRequest` / `FBlueprintImpactScanResult` 对外合同，第一阶段只把 `Main()` 改成调用 service；未来 editor 命令、toolbar 和面板也统一走同一个 service。<br>5. 在 `UScriptEditorSubsystem` 上暴露只读 facade 时，明确区分 `registry scan` 与 `loaded-only scan` 两种 mode，避免项目方误以为两者候选集完全相同；这一步是 contract 澄清，不是行为破坏。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactExecutionService.*` 与 `Private/BlueprintImpact/AngelscriptBlueprintImpactExecutionService.cpp`。 |
| 预估工作量 | M |
| 架构风险 | reload lane 当前还承担 pin replacement、节点刷新和编译队列管理；如果一开始就强行把这些副作用也塞进 scanner，容易把交互式修复路径做重。更稳妥的做法是先统一 symbol/candidate/result 合同，再决定副作用编排。 |
| 兼容性 | 向后兼容。第一阶段不改变 commandlet 参数、不改变 reload 的修复时机，也不要求项目方重写任何调用；只是把中间执行层从 `ClassReloadHelper` 内部提炼出来。 |
| 验证方式 | 1. 对同一组 class/struct/delegate 变更，同时跑 reload lane 和 `ScanAssetRegistry()`，确认共享部分的 reason 集合不再由两套代码各自定义。<br>2. 回归现有 `AngelscriptBlueprintImpactScannerTests`，并新增最小测试覆盖 `BuildImpactSymbolsFromReloadDelta()` 与 `AnalyzeLoadedBlueprintsForReload()`。<br>3. 在未来 editor 命令接入后，确认 commandlet、editor 命令和 reload helper 都通过同一个 service 路径进入，不会再出现第三套中间逻辑。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-46 | `AssetRegistry` 仍未成为 editor 内长期事件总线，脚本资产同步和项目扩展只能靠 popup/reload 猜时机 | asset registry service + subsystem facade | 高 |
| P2 | Arch-47 | reload 修复链与 `BlueprintImpact` scanner 已出现两套执行合同，未来 editor service 容易产生命中漂移 | shared execution core + candidate adapters | 中高 |

---

## 架构分析 (2026-04-09 00:03:29)

### Arch-48：扩展 surface taxonomy 仍停在 `menu/context-menu` 枚举，脚本扩展还进不了 `authoring/toolkit/compiler` 正式接点

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户是否能在不修改 `AngelscriptEditorModule.cpp` 的前提下，把脚本 editor 扩展接到 UE 原生的 `ContentBrowser Add New`、asset editor toolbar 或 compiler surface |
| 当前设计 | 当前正式暴露给脚本扩展者的 surface taxonomy 仍由 `EScriptEditorMenuExtensionLocation` 决定，范围只覆盖 `ToolMenu`、`LevelViewport_*` 和 `ContentBrowser_*Context/ViewMenu`。真正承担 authoring/opening 的 `ShowCreateBlueprintPopup()`、`ShowAssetListPopup()` 仍是模块静态 helper；`UScriptEditorSubsystem` 也只提供生命周期钩子，`UEditorSubsystemLibrary` 只有通用 `GetEditorSubsystem()` 查询。换句话说，扩展者拿到的是“把 action 挂到哪里”，拿不到“把能力接到哪类 editor workflow surface”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:10` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:32` — `EScriptEditorMenuExtensionLocation` 只枚举 `ToolMenu`、`LevelViewport_*`、`ContentBrowser_*` menu surface。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:51` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:67` — 扩展声明仍围绕 `ExtensionPoint`、`ToolMenuInsertType`、`ToolMenuSectionAlign` 这些 menu wiring 元数据。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:537` — `ShowCreateBlueprintPopup()` 仍由模块静态函数直接完成保存路径对话框、`CreatePackage()`、`CreateBlueprint()`、`AssetCreated()` 和 `OpenEditorForAsset()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:665` — `ShowAssetListPopup()` 也是模块直接拼 `AssetPicker` + `SWindow` popup。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:26` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:58` — `UScriptEditorSubsystem` 只暴露 `BP_ShouldCreateSubsystem/BP_Initialize/BP_Deinitialize/BP_Tick`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:20` — library 只把 `GetEditorSubsystem()` 暴露出来。 |
| 优点 | menu surface 做成统一枚举后，脚本侧很容易快速接入一批常见 editor 菜单位置，学习成本低。 |
| 不足 | 这里的限制已经不是“上下文被压扁”那么简单，而是 surface taxonomy 本身缺位。推断：即使今天把 `EditorSubsystem` 或 command layer 补强，只要正式 surface 仍停在 menu 枚举，项目方仍无法用同一套 contract 扩展 `ContentBrowser.AddNew`、`BlueprintEditor` toolbar 或 language-specific compiler hook，只能继续回到模块静态 helper 或新增旁路 native 代码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FBlueprintToolbar::Initialize()` 直接挂到 `FBlueprintEditorModule` 的 `GetMenuExtensibilityManager()->GetExtenderDelegates()`，把 Lua 相关操作接入具体 asset editor surface，而不是停在全局菜单枚举。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:30` | 一旦能力属于某类 editor host，就直接为该 host 建正式 surface，而不是只在通用 menu taxonomy 中兜底。 |
| UnrealCSharp | `UDynamicDataSource::Initialize()` 主动扩展 `ContentBrowser.AddNewContextMenu`，`PopulateAddNewContextMenu()` 再把选中的 virtual path 转成 `FDynamicNewClassContextMenu` 的创建入口，让虚拟数据源拥有原生 authoring surface。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:71` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:84`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:724`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:46` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:58` | 对虚拟脚本/动态类内容来说，`ContentBrowser` 不只是浏览 surface，还应该包含正式的 `Add New` surface。 |
| puerts | `FPuertsEditorModule::OnPostEngineInit()` 为 `UTypeScriptBlueprint` 注册专用 `FKismetCompilerContext`，把语言桥接的 Blueprint 语义沉到 compiler surface，而不是继续靠 editor popup 补行为。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120` | 当语言集成会改变 asset/generated class 行为时，compiler surface 也应成为正式扩展位。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptEditor` 的扩展模型从“menu location enum”升级成“editor surface registry”，让 `menu`、`authoring`、`toolkit`、`compiler` 拥有同级 contract。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorSurfaceDescriptor` 或等价类型，第一阶段至少覆盖 `ToolMenu`、`ContentBrowserAddNew`、`BlueprintEditorToolbar`、`AssetWorkflow` 四类 surface；`EScriptEditorMenuExtensionLocation` 暂时保留，只作为旧脚本向新 surface registry 的适配层。<br>2. 在 native 层新增 `IAngelscriptEditorSurfaceRegistry` 或 `UAngelscriptEditorSurfaceRegistrySubsystem`，由 `AngelscriptEditor` 持有；`UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 只暴露 registry facade 和 registration handle，不再让项目方直接碰模块静态 helper。<br>3. 第一阶段只先接两个最有收益的 surface：`ContentBrowser.AddNewContextMenu` 和 `BlueprintEditor` toolbar。现有 `ShowCreateBlueprintPopup()` / `ShowAssetListPopup()` 不删除，改成这两个 surface 的默认 fallback handler。<br>4. 第二阶段再评估是否为 script-backed Blueprint 引入轻量 compiler hook；这一步先从 `validator/post-compile callback` 做起，避免一次性复制 puerts 的完整 `FKismetCompilerContext`。<br>5. 迁移期保持 `UScriptEditorMenuExtension` 可用：旧脚本继续按 menu enum 注册，新脚本可选用新 surface descriptor。文档里明确标出新旧 API 的职责边界，避免项目方继续把 authoring 需求塞回 menu hook。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Surfaces/*` 与 `Private/Surfaces/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就同时引入 `Add New`、toolkit toolbar 和 compiler hook，容易把扩展面做得过宽且互相耦合。更稳妥的顺序是先收敛 surface registry，再逐个增加高价值 surface。 |
| 兼容性 | 向后兼容。旧的 menu-based 扩展类和现有 popup/workflow 都可以保留；新 contract 只是在其上补正式 surface，不要求现有脚本立即迁移。 |
| 验证方式 | 1. 用一个最小 `UScriptEditorSubsystem` 或 native test feature 注册 `ContentBrowser Add New` action，确认无需修改 `AngelscriptEditorModule.cpp` 即可在 `/All/Angelscript` 相关路径下看到入口。<br>2. 在 `BlueprintEditor` 打开 script-backed Blueprint 时验证新 toolbar 入口只在正确上下文显示。<br>3. 做一次 reload / shutdown / reopen，确认 registration handle 能正确回收，不会残留重复 surface。 |

### Arch-49：用户可见工具 surface 仍缺少 capability policy，`legacy/debug` 流程和正式工作流共享同一条 `Tools` 合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 工具是否区分“正式用户工作流”“开发者维护工具”“调试/遗留入口”，以及这些能力能否通过稳定 policy 投影到不同 surface |
| 当前设计 | `AngelscriptEditor` 当前把 `Open Angelscript workspace`、`Legacy Native Bind Generator (Debug Only)` 和 `Function Tests` 直接并列挂到 `MainFrame.MainMenu.Tools`。同时模块头还公开了一整组 static tool helpers，如 `GenerateNativeBinds()`、`GenerateBindDatabases()`、`GenerateNewModule()`、`OriginalGenerate()`。这意味着工具 surface 目前没有独立的 capability descriptor、audience policy 或 developer gating；只要功能存在，最自然的投影方式就是“继续往 Tools 菜单加一个条目”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:745` — `RegisterToolsMenuEntries()` 直接把 `ASOpenCode`、`ASGenerateBindings` 和 `Function Tests` 插到 `MainFrame.MainMenu.Tools`；其中 `ASGenerateBindings` 标题已经明确写着 `"Legacy Native Bind Generator (Debug Only)"`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:12` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:18`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:49` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:55` — 模块 public 头直接暴露 `ShowAssetListPopup()`、`ShowCreateBlueprintPopup()`、`GenerateNativeBinds()`、`GenerateBindDatabases()`、`GenerateNewModule()`、`OriginalGenerate()` 等工具/维护 helper。 |
| 优点 | 内部维护者拿到一份代码后，几乎不用配置额外工具面就能在菜单里快速找到调试和历史流水线入口。 |
| 不足 | 当前问题不是“有没有菜单项”，而是工具没有 capability policy。推断：只要不先定义 `Audience/Visibility/Projection`，后续新增工具仍会沿着“static helper + Tools 菜单 lambda”增长，最终让项目方和插件用户分不清哪些入口是正式产品能力，哪些只是维护/迁移时期的内部桥。更重要的是，`EditorSubsystem` 也无法基于稳定 tool metadata 做筛选、重投影或权限控制。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaEditorCommands` 先把可见能力固定为 `HotReload`、`GenerateIntelliSense`、`Open*Settings`、`ReportIssue`、`About` 等命令；`FMainMenuToolbar::GenerateUnLuaSettingsMenu()` 再把这些命令组织成 `Action` / `Help` 两个 section，而不是把内部维护 helper 直接散落到主菜单。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.h:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.h:39`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:31`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:118` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:140` | 先定义“可见工具目录”，再决定这些工具出现在什么菜单分区里。 |
| UnrealCSharp | `FUnrealCSharpEditorCommands` 只注册一组边界清晰的 editor capability：`GeneratorCode`、`OpenNewDynamicClass`、`OpenEditorSettings`、`OpenRuntimeSettings`、`OpenFile`、`CodeAnalysis`、`OverrideBlueprint`；模块又把部分能力做成独立 console command，而不是把所有实现都塞回一个主菜单。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditorCommands.h:9` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditorCommands.h:37`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditorCommands.cpp:7` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditorCommands.cpp:29`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:68` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:100` | 工具先有清晰 capability identity，再决定哪些同时投影到 menu、toolbar、console。 |
| puerts | 把生成工具独立成 `DeclarationGenerator` 模块，对外只暴露 `IDeclarationGenerator` 薄接口；模块内部再持有 `PluginCommands`、`RegisterMenus()` 和 `Puerts.Gen` console command。这样“生成声明”是一个独立 capability，而不是核心 editor 模块里的顺手 lambda。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:16` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/IDeclarationGenerator.h:33`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1569` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1690` | `tool capability` 和 `editor runtime/watch/compiler` 可以分模块建边界，避免用户主菜单直接继承历史维护债务。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `AngelscriptEditor` 引入正式的 tool descriptor 和 visibility policy，把用户工具、开发工具、遗留调试入口分层，再决定各自投影到哪个 surface。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorToolDescriptor`，至少包含 `ToolId`、`DisplayName`、`Audience`（`User/Developer/Debug`）、`PreferredSurface`、`ConsoleAlias`、`bEnabledByDefault`；第一阶段只先覆盖 `ASOpenCode`、`ASGenerateBindings`、`FunctionTests` 三个现有入口。<br>2. 把 `RegisterToolsMenuEntries()` 改成读 descriptor registry；`MainFrame.MainMenu.Tools` 默认只显示 `Audience=User` 和启用的 `Developer` 工具。`Debug` / `Legacy` 工具先迁到新的 developer subsection，或者仅在显式开启 developer mode 时显示。<br>3. 将 `GenerateNativeBinds()`、`GenerateBindDatabases()`、`GenerateNewModule()` 等历史静态 helper 包装成独立 tool capability；第一阶段不要求拆模块，但需要先有稳定 `ToolId` 和 console alias，避免继续靠匿名 lambda 直接曝光实现。<br>4. 通过 `UScriptEditorSubsystem` 或新的 tool registry facade 暴露只读工具目录，让项目级扩展可以查询、隐藏、重投影某些 capability，例如把 `User` 级工具放到 toolbar，把 `Developer` 级工具放到专用 diagnostics 菜单。<br>5. 兼容期保留现有菜单命名和 console 入口；等文档和项目侧习惯稳定后，再决定是否把 `legacy/debug` 工具迁到独立模块或仅保留 console。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Tools/AngelscriptEditorToolDescriptor.h` 与 `Private/Tools/AngelscriptEditorToolRegistry.cpp`。 |
| 预估工作量 | S-M |
| 架构风险 | 如果第一阶段就试图重排所有已有工具到新模块，容易打断维护者当前工作流。更合适的顺序是先做 descriptor/policy，不改实现；等 usage 稳定后再拆分 module 或 surface。 |
| 兼容性 | 向后兼容。现有工具能力和命令名都可以保留；变化主要是显示策略与注册方式变成显式 policy，旧入口可作为 alias 继续存在一段时间。 |
| 验证方式 | 1. 开启和关闭 developer mode，确认 `Legacy Native Bind Generator (Debug Only)` 与 `Function Tests` 的可见性随 policy 变化，而正式用户工具不受影响。<br>2. 通过一个最小 `EditorSubsystem` 查询 tool registry，确认能按 `Audience` 获得稳定的工具目录，而不是解析菜单树。<br>3. 回归现有 `Tools` 菜单，确认 descriptor 化后动作实现仍指向同一条历史 helper/service，不会破坏当前维护路径。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-48 | 扩展 surface taxonomy 仍停在 `menu/context-menu`，缺少 `Add New` / toolkit / compiler 正式接点 | surface registry + subsystem facade | 高 |
| P2 | Arch-49 | 用户工具、开发工具、遗留调试入口共享同一条 `Tools` 合同，缺少 capability policy | tool descriptor + visibility policy | 中高 |

---

## 架构分析 (2026-04-09 00:18:33)

### Arch-50：脚本监听仍是 `root-wide watch + timestamp scan` 的粗粒度混合模型

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本文件变化捕获的粒度、去重策略，以及这些变化能否成为其它 editor feature 可复用的高精度输入 |
| 当前设计 | `AngelscriptEditor` 在启动时直接对所有 script root 做 `IncludeDirectoryChanges` 监听，事件回调只负责把路径排入 `FAngelscriptEngine` 的 reload 队列；与此同时 runtime 还有一条 `CheckForFileChanges()` 全量 timestamp 扫描路径。两条 lane 共享的是 `FileChangesDetectedForReload` 这样的粗队列，而不是稳定的 watched-file index 或结构化 change set。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:367` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:380` — 启动时遍历 `FAngelscriptEngine::MakeAllScriptRoots()`，对每个 root 注册 `RegisterDirectoryChangedCallback_Handle(..., IncludeDirectoryChanges)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:93` — `OnScriptFileChanges()` 只做 `QueueScriptFileChanges(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:89` — helper 只按路径与扩展名把文件加入 `Engine.FileChangesDetectedForReload` / `FileDeletionsDetectedForReload`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2743` 到 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2756` — runtime reload 前先读取队列，再结合 `LastFileChangeDetectedTime` 延迟处理删除。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2859` 到 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2895` — `CheckForFileChanges()` 又会 `FindAllScriptFilenames()` 后逐个比较 timestamp，把结果重新写回 `FileChangesDetectedForReload`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:15` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:38` — 现有自动化测试命名集中在 `DirectoryWatcher.Queue.*`，验证重心也停在“排队正确”而不是“增量精度/去重/调度策略”。 |
| 优点 | 新增、删除、重命名脚本都不容易漏掉，逻辑简单，而且当前 reload 主链不依赖额外索引即可工作。 |
| 不足 | 当前问题不是“能不能热重载”，而是变化语义过粗。推断：当脚本树变大、IDE 保存触发重复 `Modified`、或未来 editor feature 想做“只刷新已加载模块”“只更新某个 workspace index”“只重跑受影响分析”时，现有 root 级 watcher 与 runtime 全量 timestamp 扫描会同时带来噪声和重复工作；其它 feature 只能继续消费粗队列，而拿不到稳定的增量 change set。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 文件变化回调不是无条件热重载，而是先受 `HotReloadMode` policy 控制；正式入口仍通过 `HotReload` command 暴露。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:36`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:112` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:118`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:37` | 即使底层仍用目录监听，editor 层也应先有明确的 policy 与人工入口，而不是让“root watcher 事件”直接成为唯一驱动源。 |
| puerts | `FSourceFileWatcher` 不是 watch 整个脚本根，而是在 `OnSourceLoaded()` 时只为已加载 source 所在目录注册 watcher；同时缓存每个被 watch 文件的 `FMD5Hash`，只在内容真的变化时才触发回调，并在析构时成组反注册。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/SourceFileWatcher.h:19` 到 `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/SourceFileWatcher.h:37`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22` 到 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:52` 到 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:103` | 对交互式脚本编辑来说，`loaded-file scoped watch + content dedup` 比根目录级粗监听更适合作为增量 editor feature 的基础设施。 |
| UnrealCSharp | `FEditorListener::OnDirectoryChanged()` 会先过滤扩展名和忽略目录，再把变化缓存到 `FileChanges`；真正的分析在 `OnCompile()` 中只针对变化文件执行 `FCodeAnalysis::Analysis(File)`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:322` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:370`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:228` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:248` | 把“目录变化”先压缩成高质量 change list，再决定谁消费这些变化，能显著降低后续 feature 的耦合度。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留当前 reload 兼容路径的前提下，新增 `script change tracker`，把脚本变化从“根目录粗队列”升级成“可去重、可过滤、可复用”的 editor contract。 |
| 具体步骤 | 1. 新增 `FAngelscriptScriptChangeTracker` 或等价 feature，内部显式维护 `WatchedDirectories`、`WatchedFiles`、`LastSeenContentHash/LastSeenTimestamp` 与 `FScriptChangeSet`；第一阶段仍可把最终结果回写到 `FAngelscriptEngine::FileChangesDetectedForReload`，不打断现有 reload 主链。<br>2. 把 watch 策略拆成两层：`root discovery watch` 只负责新增/删除目录与新文件发现，`loaded-file watch` 则根据当前 active modules / open source files 精准跟踪已加载脚本；参考 puerts，优先对交互式工作流走后者。<br>3. 在新的 `UAngelscriptEditorSettings` 中加入最少的 editor-only policy：`WatchMode`（`RootWideCompat` / `LoadedFilesPreferred`）、忽略目录列表、去重策略；默认值保持当前 `RootWideCompat`，确保旧项目行为不变。<br>4. 让 `CheckForFileChanges()` 退化为 fallback/resync 路径，而不是 interactive editor 的主入口；第一阶段只在 tracker 不可用或显式全量校验时才走 runtime 全量 timestamp 扫描。<br>5. 扩展测试覆盖：在现有 `DirectoryWatcher.Queue.*` 之外，新增“重复保存不重复入队”“忽略目录不触发”“仅已加载脚本触发局部 change set”“fallback scan 可修复 watcher 漏报”几类自动化测试。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Reload/AngelscriptScriptChangeTracker.*` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Settings/AngelscriptEditorSettings.h`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就完全移除 runtime timestamp scan，容易在 watcher 漏报或跨平台差异下引入漏编译；更稳妥的是先把 tracker 作为高精度主路径，再保留 fallback scan 兜底。 |
| 兼容性 | 向后兼容。默认 `WatchMode` 保持当前 root-wide 行为，现有热重载与 VS Code 工作流不需要立即迁移；只是 editor 内部开始生成更精确的 `FScriptChangeSet` 供后续 feature 复用。 |
| 验证方式 | 1. 对同一脚本连续保存多次，确认 tracker 能去重，而现有 reload 结果不变。<br>2. 在大型 script root 下修改一个已加载脚本，确认只生成局部 change set，不需要每次都依赖全量扫描才能得到正确 reload。<br>3. 关闭/重载 `AngelscriptEditor` 模块后检查 watcher 全部被回收，重新打开后无重复回调。 |

### Arch-51：编辑器缺少持续的 `workspace intelligence` 层，`SourceNavigation` 与 `BlueprintImpact` 仍各自重建局部知识

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编辑器是否维护一份可复用的脚本符号/资产关系/工作区元数据，让导航、分析、IDE 集成和项目级扩展共享同一条 intelligence contract |
| 当前设计 | 目前 `AngelscriptEditor` 的固定工具入口只有 `Open Angelscript workspace`、`Legacy Native Bind Generator` 和 `Function Tests`；源码导航直接从 `FAngelscriptEngine` 查询类定义后打开文件，`BlueprintImpact` 则另外公开 `BuildImpactSymbols()` / `ScanBlueprintAssets()` 并在 reload helper 里手工构造 `ImpactSymbols`。也就是说，编辑器里已经有“脚本知识”的多个消费者，但没有一个长期驻留的 `workspace intelligence service` 统一维护这些知识。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:745` — 固定 `Tools` 入口只有 `ASOpenCode`、`ASGenerateBindings`、`Function Tests`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:118` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:129` — 导航时直接用 `FAngelscriptEngine::Get().GetClass(...)` 查询 `FAngelscriptClassDesc`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:62` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:67` — `BlueprintImpact` 公开了独立的路径规范化、模块匹配、symbol 构建和扫描 API。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:112` — reload helper 又手工拼出一份 `FBlueprintImpactSymbols`，随后遍历内存里的 `UBlueprint` 调 `AnalyzeLoadedBlueprint()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:47` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:58` — `UScriptEditorSubsystem` 没有 workspace/index/intelligence 访问面。 |
| 优点 | 当前 editor 模块保持得比较窄，避免在功能尚未稳定前提前引入复杂缓存和长期后台任务。 |
| 不足 | 当前缺的不是某一个按钮，而是一层共享知识面。推断：只要没有统一的 workspace intelligence，后续想补 `Go to symbol`、workspace export、IDE metadata、editor-side diagnostics、项目级 subsystem 自定义分析时，就会继续在 `SourceNavigation`、`BlueprintImpact`、reload helper 和外部扩展之间复制各自的 symbol builder / asset matcher，而不是复用单一 contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaIntelliSenseGenerator` 启动后长期订阅 `AssetRegistry` 变化，`UpdateAll()` 先全量导出，再在 `OnAssetAdded/Removed/Renamed/Updated()` 中增量维护 IntelliSense 产物。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:89`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:290` | 先建立一份长期维护的 editor-side metadata，再让 IDE/命令/UI 共享它，比每个入口各自现场重算更可扩展。 |
| puerts | `PuertsEditor` 在 `OnPostEngineInit()` 中启动 `FJsEnv` 并执行 `PuertsEditor/CodeAnalyze`；同时 `DeclarationGenerator` 通过 menu/toolbar/console 统一生成 `ue.d.ts`。分析运行时与声明导出是两个稳定 feature，而不是临时脚本工具。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:150`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1615` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1687` | 对语言插件来说，workspace intelligence 往往需要分成“持续分析服务”和“可显式导出的元数据工具”两层。 |
| UnrealCSharp | `FEditorListener::OnPostEngineInit()` 启动即跑 `FCodeAnalysis::CodeAnalysis()` 与 `FDynamicGenerator::CodeAnalysisGenerator()`；后续 `OnCompile()` 只对变化文件执行 `FCodeAnalysis::Analysis(File)` 并刷新动态映射。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:137` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:142`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:228` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:248` | “全量初始化 + 增量更新”是更适合作为 editor intelligence 基座的 contract，导航、toolbar 和生成器再去消费它。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 基于现有 `FAngelscriptEngine`、`BlueprintImpact` 和 source navigation 能力，新增共享的 `workspace intelligence service`，先统一知识层，再逐步挂 UI/IDE/export 入口。 |
| 具体步骤 | 1. 新增 `FAngelscriptWorkspaceIntelligenceService` 或 `IAngelscriptEditorIntelligenceService`，第一阶段只维护只读索引：脚本 module/class/struct/enum/delegate symbol、相对脚本路径、对应 `UObject/UField`、以及与 `Blueprint`/asset 的基础映射。<br>2. 让 `AngelscriptSourceCodeNavigation` 与 `BlueprintImpact` 先改为查询这层 service：导航从 service 取 definition location，`BuildImpactSymbols()` 则退化为从索引裁剪出当前扫描需要的 symbol slice，而不再在多个调用点各自重建。<br>3. 给 service 增加两条刷新入口：`RefreshAll()` 和 `RefreshChangedScripts(const FScriptChangeSet&)`；第一条用于 editor startup 与显式重建，第二条由前述 change tracker 或 reload lane 喂增量更新。<br>4. 为 service 补一个薄适配层而不是一次性大改 UI：新增 `FAutoConsoleCommand`（例如 `Angelscript.Editor.RefreshWorkspaceIndex`），保留现有 `BlueprintImpact` commandlet 和 `Open workspace` 菜单，同时追加一个可选的 metadata export 入口供 VS Code 扩展或未来 IDE 集成消费。<br>5. 通过 `UScriptEditorSubsystem` 暴露只读 facade，例如 `GetWorkspaceIntelligence()` / `FindDefinition()` / `RequestWorkspaceRefresh()`；项目方 editor subsystem 与自定义工具从此不必再直接依赖 `BlueprintImpact` 私有实现或 `FAngelscriptEngine` 细节。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Intelligence/AngelscriptWorkspaceIntelligenceService.h` 与 `Private/Intelligence/AngelscriptWorkspaceIntelligenceService.cpp`。 |
| 预估工作量 | M-L |
| 架构风险 | 如果第一阶段就让所有现有调用点完全改用新索引，容易把 `BlueprintImpact` 和 reload 行为同时改动过大；更稳妥的是先做只读索引和查询适配，让旧逻辑继续作为 fallback。 |
| 兼容性 | 可增量实施。现有 `BlueprintImpact` commandlet、source navigation 和 VS Code 工作流都可以继续保留；新 service 首先作为共享查询层引入，不要求项目方立即迁移现有脚本或工具。 |
| 验证方式 | 1. editor 启动后做一次 `RefreshAll()`，确认 source navigation 仍能打开正确脚本定义，`BlueprintImpact` 结果与旧实现保持一致。<br>2. 修改单个脚本并执行 `RefreshChangedScripts(...)`，确认导航与相关分析只刷新受影响符号，而不必每次全量重建。<br>3. 给一个项目级 `UScriptEditorSubsystem` 调用新 facade，验证无需改核心模块即可查询 definition / 请求 refresh / 导出 metadata。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-50 | 文件变化捕获仍是 `root-wide watch + timestamp scan` 的粗粒度混合模型 | change tracker + watch policy | 高 |
| P2 | Arch-51 | 缺少持续 `workspace intelligence` 层，导航与分析仍各自重建局部知识 | shared intelligence service | 中高 |

---

## 架构分析 (2026-04-09 00:29:27)

### Arch-52：内建 editor 工具仍停留在全局 `Tools`/transient popup 呈现，缺少 host-local presenter 层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编辑器工具是否真正沉入 UE 的 host-local surface（如 `PlayToolBar`、`BlueprintEditor`、持久面板），以及用户能否通过 `EditorSubsystem` 正式扩展这些 surface |
| 当前设计 | `AngelscriptEditor` 自带工具主要仍挂在 `MainFrame.MainMenu.Tools`，复杂交互则通过 `SWindow`/`PushMenu` 弹出临时 UI；脚本扩展面虽然支持 `LevelViewport_*` 与 `ContentBrowser_*` 菜单，但没有形成“surface presenter 对象 + host context”这一层，`UScriptEditorSubsystem` 也还不能注册任何 presenter。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:414` — 模块只把固定 UI 注册点接到 `RegisterToolsMenuEntries()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:701` — 内建工具 `ASOpenCode`、`ASGenerateBindings`、`Function Tests` 全部扩到 `MainFrame.MainMenu.Tools`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:96` — `ForceEditorWindowToFront()` 直接依赖 `HACK_ForceToFront()` 操作顶层窗口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:603` — 资产选择工作流通过 `CreateAssetPicker` + `FSlateApplication::PushMenu(...)` 生成瞬时 popup，而不是进入持久 `SDockTab`/toolkit surface。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:10` — 枚举表面只显式列出 `ToolMenu`、`LevelViewport_*`、`ContentBrowser_*`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:922` — 扩展注册实现也只落到 `LevelEditorModule` 与 `ContentBrowserModule` 的 menu/tool-menu extender。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7` — `UScriptEditorSubsystem` 只有 `Initialize/Deinitialize/Tick` 生命周期壳层。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` — 对外暴露的 subsystem API 仍只有 `GetEditorSubsystem()` getter。 |
| 优点 | 内建工具接线短，新增一个 editor 命令的落地成本很低；脚本菜单扩展已经能覆盖 `LevelEditor` 与 `ContentBrowser` 的常见菜单面。 |
| 不足 | 当前问题不再是“有没有入口”，而是入口没有宿主语义。推断：当工具需要感知当前 `UBlueprint`、当前 asset editor、当前 play/compile host 或需要长期保持 UI 状态时，现有 `Tools` 菜单和 popup 模式会继续把能力挤回模块静态函数；项目方即使能写 `UScriptEditorSubsystem`，也没有正式的 presenter 注册面，只能继续走字符串式 `ToolMenu` 路径或旁路 Slate 弹窗。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块启动时显式持有 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个 presenter 对象；其中 `BlueprintToolbar` 通过 `FAssetEditorExtender` 直接拿到当前 `UBlueprint`，`BuildToolbar()` 再根据绑定状态构造工具栏按钮和菜单。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47` | 先把 surface presenter 物化，再把当前 editor host 上下文传给 presenter，而不是让模块静态 helper 直接出 UI。 |
| puerts | `DeclarationGenerator` 不是散落在模块函数里的工具项，而是单独的 feature 对象：持有 `PluginCommands`，并把同一命令同时注册到 `LevelEditor.MainMenu.Window` 与 `LevelEditor.LevelEditorToolBar.User/PlayToolBar`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1579`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` | 就算工具还没有进入 asset editor toolkit，也应先形成“命令 + presenter 对象 + 多 surface 分发”的稳定结构。 |
| UnrealCSharp | 模块显式拥有 `UnrealCSharpPlayToolBar` 和 `UnrealCSharpBlueprintToolBar`，`RegisterMenus()` 后分别接入 `PlayToolBar` 和 `BlueprintEditor`；`BlueprintToolBar` 通过 `FAssetEditorExtender` 获取当前蓝图，再根据当前文件/override 状态动态生成菜单。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:193`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:17`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` | `LevelEditor` 工具栏与 `BlueprintEditor` 工具栏应被视为不同 host；把 presenter 分层后，项目工具才能真正跟随当前上下文而不是总回退到全局菜单。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `Tools` 菜单兼容面的前提下，引入显式 `surface presenter` 层，并把 `UScriptEditorSubsystem` 升级为 presenter 注册与 host context 查询入口。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorSurfacePresenter`（或等价接口），最少约定 `Register() / Unregister() / GetPresenterId() / GetSurfaceKind()`；`FAngelscriptEditorModule` 改为持有 presenter 列表，而不是只在 `StartupModule()` 里直接加菜单。<br>2. 第一阶段只实现三个 presenter：`MainFrameToolsPresenter`（兼容现有 `Tools` 菜单）、`LevelEditorPlayToolbarPresenter`、`BlueprintEditorToolbarPresenter`；其中后两者分别参考 `UnrealCSharp`/`UnLua` 接 `UToolMenus` 与 `FAssetEditorExtender`。<br>3. 给 `UScriptEditorSubsystem` 增加只读 host facade 与注册 API，例如 `RegisterSurfacePresenter`、`UnregisterSurfacePresenter`、`GetActiveBlueprintEditorContext`；`UEditorSubsystemLibrary` 保留 getter，但项目方不再需要直接碰模块静态函数。<br>4. 把现有 `ASOpenCode`、`ASGenerateBindings`、`Function Tests` 先改成 command descriptor，再由不同 presenter 决定它们出现在哪个 surface；后续再补一个可选 `NomadTab` presenter，用于 `BlueprintImpact`/workspace 诊断。<br>5. 旧 `UScriptEditorMenuExtension` 先作为兼容适配层继续存在：把 `ToolMenu`/`LevelViewport_*` 扩展翻译成 presenter descriptor，而不是立即废弃原有脚本扩展类。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Presenters/*.h/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | `LevelEditor` 与 `BlueprintEditor` 的 host context 生命周期不同；如果第一阶段就把所有现有工具一次性迁走，容易在 editor startup 和 asset editor reopen 时引入重复注册或空上下文。更稳妥的顺序是先做 presenter 容器，再逐项搬迁工具。 |
| 兼容性 | 向后兼容。现有 `MainFrame.MainMenu.Tools` 入口可以保留为 presenter alias；旧 `UScriptEditorMenuExtension` 脚本不需要立即改写，只是内部注册路径改到 presenter registry。 |
| 验证方式 | 1. 启动 editor 后确认原有 `Tools` 菜单功能不回归。<br>2. 打开任意 `BlueprintEditor`，确认新 presenter 能拿到当前蓝图上下文，而不是回退到全局 popup。<br>3. 关闭并重新打开 editor/asset editor，确认 toolbar/button 不重复注册。<br>4. 用一个项目侧 `UScriptEditorSubsystem` 注册最小 presenter，验证无需修改核心模块即可把动作挂到指定 surface。 |

### Arch-53：脚本资产 authoring 仍绕开 `ContentBrowser Add New / class wizard / Blueprint compiler` 正式通道

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产的创建、派生、编辑和编译校验是否接入 UE 编辑器原生 authoring/compile 通道 |
| 当前设计 | `AngelscriptData` 目前只负责把脚本资产显示到 `ContentBrowser`；真正的新建入口仍来自 runtime delegate 或 asset-picker 底部按钮，随后 `ShowCreateBlueprintPopup()` 手工走 `CreateModalSaveAssetDialog -> CreatePackage -> CreateBlueprint -> Save -> OpenEditorForAsset`。模块对 `KismetCompiler` 的使用也只停留在“为父类选 BlueprintClass/GeneratedClass”，并没有注册 script-aware compiler lane。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` — `OnEngineInitDone()` 只创建并激活 `AngelscriptData` 数据源。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:5` — `Initialize()` 仅调用 `Super::Initialize(true)`，没有注册 `ContentBrowser.AddNewContextMenu` 一类 authoring 入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:124` — `EnumerateItemsAtPath()` 仍为空实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:182` — `GetItemPhysicalPath()`、`EditItem()`、`BulkEditItems()`、`AppendItemReference()` 仍全部返回 `false`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:405` — 新建入口首先从 `FAngelscriptRuntimeModule::GetEditorCreateBlueprint()` 委托跳回 editor 模块静态函数。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:625` — 多资产 popup 底部按钮再次回调 `ShowCreateBlueprintPopup()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:471` — `ShowCreateBlueprintPopup()` 直接使用 `CreateModalSaveAssetDialog()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:510` — `IKismetCompilerInterface` 的使用仅是 `GetBlueprintTypesForClass(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h:16` — `UScriptableFactory` 只暴露 `CreateFromText/CreateFromBinary` primitive。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.cpp:13` — factory wrapper 目前没有任何 `AssetTools`/`ContentBrowser` 注册链。 |
| 优点 | 当前路径足够直接，调试时容易看清每一步创建/保存行为；不需要先引入新的 asset subclass 或 compiler extension 就能完成脚本派生资产创建。 |
| 不足 | 当前缺口不只是“缺一个 Add New 菜单”，而是 authoring contract 没有进入 UE 正式通道。推断：只要继续通过 runtime delegate 和 popup helper 创建脚本资产，后续要补 class wizard、模板选择、按脚本根生成默认路径、Blueprint 编译期校验、项目自定义 authoring 动作时，就会不断复制新的旁路 helper，而不是复用统一的 `ContentBrowser`/`Blueprint compiler` 管线。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 即使没有自定义虚拟数据源，脚本相关修改也尽量贴在当前 `BlueprintEditor` 上下文内完成；`BindToLua/UnbindFromLua` 之后会直接编译并刷新当前蓝图编辑器。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:188`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:225` | 轻量方案也应尽量进入“当前 asset editor + compile/refresh”正式链，而不是只做模块级 popup。 |
| UnrealCSharp | `UDynamicDataSource::Initialize()` 直接给 `ContentBrowser.AddNewContextMenu` 增加动态 section，`PopulateAddNewContextMenu()` 根据选中虚拟路径生成 `New Dynamic Class...` 动作，再通过 `DynamicNewClassUtils` 打开专用 class wizard。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:71`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:7`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:12` | 如果已经有虚拟数据源，就应该把 authoring 入口也挂回 `ContentBrowser` 原生 `Add New` 通道，而不是另开一条 popup 路径。 |
| puerts | editor 模块在 `OnPostEngineInit()` 为 `UTypeScriptBlueprint` 注册专用 `FKismetCompilerContext`；元数据对象 `UPEClassMetaData::Apply()` 会把 flags/meta/blueprint metadata 同步到蓝图，`PEBlueprintAsset` 在需要时还会重新编译蓝图。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintMetaData.cpp:109`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1356` | 一旦脚本资产需要长期参与 Blueprint 语义，最好尽早进入 compiler/metadata lane，而不是把所有校验留到 asset 创建后的人肉步骤。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“显示脚本资产”和“创建/编译脚本资产”从两条旁路收敛成同一条原生 editor contract：`ContentBrowser Add New + class wizard + script-aware compile/validation`。 |
| 具体步骤 | 1. 新增 `FAngelscriptAssetAuthoringService`（或等价 feature），由它统一拥有“新建脚本派生资产”入口；`OnEngineInitDone()` 不再只激活数据源，而是同时注册 `ContentBrowser.AddNewContextMenu` 动态 section，并把旧 `ShowCreateBlueprintPopup()` 包装成该 service 的兼容入口。<br>2. 基于 `UContentBrowserDataMenuContext_AddNewMenu` 新增 `SAngelscriptNewDerivedAssetDialog`/wizard：提供 `UASClass` 选择、资产类型（`Blueprint`/`DataAsset`/未来 factory-backed asset）和目标路径推导，避免继续直接暴露 `CreateModalSaveAssetDialog()` 细节。<br>3. 把 `UScriptableFactory` 与 `AssetToolsStatics` 接到 authoring service：第一阶段只支持显式注册的 factory-backed 资产模板；没有 factory 的类型继续走现有 `Blueprint/DataAsset` 分支。<br>4. 新增 `FAngelscriptBlueprintCompileIntegration`，优先采用 `UBlueprintCompilerExtension` 或受限 compile delegate，只对“父类为 `UASClass`”的蓝图触发：第一阶段只做校验与 metadata/tag 同步，必要时触发局部 `BlueprintImpact` 刷新；不要一开始就把所有蓝图编译都接管。<br>5. 给 `UScriptEditorSubsystem` 增加可选扩展点，如 `RegisterAuthoringAction`、`RegisterPostCompileValidator`；这样项目方可以追加自己的脚本资产模板和校验，而不必继续修改 `AngelscriptEditorModule.cpp`。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Authoring/*.h/*.cpp` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Blueprint/AngelscriptBlueprintCompileIntegration.*`。 |
| 预估工作量 | M-L |
| 架构风险 | compile 集成如果范围过宽，容易拖慢普通蓝图编译或引入重复 `CompileBlueprint()`；必须严格限制到 `UASClass` 相关蓝图，并把第一阶段控制在“校验/同步”而非重写整个编译流程。 |
| 兼容性 | 向后兼容。现有 runtime delegate、asset-picker 底部按钮和 `ShowCreateBlueprintPopup()` 都可以继续保留，只是内部转发到新的 authoring service；没有启用新 wizard/compile validator 的项目，行为可保持现状。 |
| 验证方式 | 1. 在 `ContentBrowser` 的脚本虚拟路径或脚本相关目录下确认出现新的 `Add New` 动作。<br>2. 通过 wizard 创建 `Blueprint`/`DataAsset`，确认资产路径、`AssetRegistry`、保存和打开流程都正确。<br>3. 修改一个脚本父类蓝图并编译，确认 compile integration 只在脚本相关蓝图触发，普通蓝图无额外回归。<br>4. 保留旧 popup 路径回归测试，确认它仍能创建同样的资产并走到同一条底层 service。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-53 | 脚本资产创建仍绕开 `Add New / class wizard / compiler` 正式通道 | authoring service + compile integration | 高 |
| P2 | Arch-52 | 内建工具仍停留在全局 `Tools`/popup，缺少 host-local presenter 层 | surface presenter + subsystem facade | 中高 |

---

## 架构分析 (2026-04-09 00:46:37)

### Arch-54：脚本虚拟资产与派生 `Blueprint/DataAsset` 仍分属两条 identity lane，编辑器缺少稳定的 cross-lane bridge

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptData` 虚拟内容项、脚本类 `UASClass`、以及真实 package-backed 派生资产之间，是否存在统一且可扩展的 identity bridge |
| 当前设计 | 当前 editor 同时维护了一条“虚拟脚本资产 lane”和一条“真实 `/Game/...` 派生资产 lane”。前者由 `UAngelscriptContentBrowserDataSource` 用 `/All/Angelscript/*` 虚拟路径展示，后者由 `ShowCreateBlueprintPopup()` 基于 `UASClass` 和脚本相对路径临时推导目标目录并创建资产。两条 lane 之间目前只有零散的 `Path`/`AssetData` 复用，没有正式的双向映射 contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:118` — `OnEngineInitDone()` 只负责创建 transient `AngelscriptData` 并激活数据源，没有注册 identity bridge 或反查服务。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:29` — `CreateAssetItem()` 的 payload 只保存 `Asset->GetPathName()` 和弱引用 `Asset`，虚拟路径固定为 `/All/Angelscript/<AssetName>`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:225` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:256` — datasource 能把 item 还原成 `PackagePath`/`AssetData`，但 `Legacy_TryConvertPackagePathToVirtualPath()` 与 `Legacy_TryConvertAssetDataToVirtualPath()` 都返回 `false`，说明从真实资产回跳虚拟 lane 的 contract 尚未建立。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:447` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:468` — `ShowCreateBlueprintPopup()` 不是从当前选中的 browser item 出发，而是重新根据 `Class->GetRelativeSourceFilePath()` 倒推 `/Game` 下的初始目录。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:496` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:537` — 真正的派生资产 identity 直到 `CreatePackage()`/`CreateBlueprint()` 执行时才落地。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:639` — `ShowAssetListPopup()` 处理的也是 `AssetPaths + BaseClass`，`Create New` 按钮依旧回到 `BaseClass`，没有沿用当前浏览器项或当前资产的 identity。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:33` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:58`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:19` — 项目侧 `EditorSubsystem` 目前也拿不到任何 identity 查询/桥接 API。 |
| 优点 | 双 lane 分离让虚拟脚本展示不必立即绑定到某一个派生资产，天然允许“一份脚本类对应多个派生资产”这种情况继续存在。 |
| 不足 | 这里的问题不是“少几个路径转换函数”，而是 identity 没有被建模。推断：只要 editor 继续依赖 `UASClass + RelativeSourceFilePath + AssetPaths` 这组三散输入，项目方后续无论要做“从脚本项打开派生资产”“从蓝图反查脚本源”“针对同一脚本类做多模板派生”“在 Editor Subsystem 中追加自定义上下文动作”，都只能各自复制一套启发式映射。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FDynamicFileItemDataPayload` 在同一个 payload 里同时保存 `InternalPath`、`UClass` 和 `AssetData`；`PopulateAddNewContextMenu()` 再把选中的虚拟路径先转换回 internal path，`OnNewClassRequested()` 再从 internal path 落到文件系统路径，`CreateFolderItem()`/`CreateFileItem()` 则共用同一棵 hierarchy。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicFileItemDataPayload.cpp:4` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicFileItemDataPayload.cpp:37`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:724`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:788` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:833` | 虚拟展示身份和真实资产身份不一定要完全合并成一个对象，但至少要落在同一个 payload/registry contract 下，避免“显示一套、创建一套、反查再猜一套”。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 先按 `InName + InPath` 生成稳定 `PackageName`，再优先 `LoadObject<UBlueprint>` 复用同一路径上的既有蓝图；只有找不到时才 `CreatePackage()`/`CreateBlueprint()`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 脚本派生产物只要进入真实 package lane，就应该有稳定的主 identity，而不是每次都重新推导路径并假设调用方自己记得对应关系。 |
| UnLua | UnLua 没有额外再造一条虚拟资产主线，而是直接把交互锚定在当前 `UBlueprint` 上；`BuildToolbar()` 接收当前 asset context，`CopyAsRelativePath_Executed()` 再从当前蓝图读取脚本模块相对路径。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:75`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:354` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:377` | 如果确实不需要第二条 lane，就让当前资产成为主 identity；如果必须保留双 lane，就至少要像 UnrealCSharp/puerts 那样补齐桥。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入正式的 `Angelscript asset identity bridge`，把虚拟脚本项、`UASClass`、源文件路径和派生资产路径统一挂到同一份 descriptor/registry 上，再让旧 API 退化为兼容包装层。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorAssetIdentity`（或等价结构），第一阶段至少包含 `VirtualPath`、`ScriptAssetPath`、`SourceFilePath`、`ScriptClass`、`DerivedAssetPaths`、`OptionalAssetData`。<br>2. 升级 `FAngelscriptContentBrowserPayload`：不再只存 `Path + WeakObjectPtr`，而是持有 `FAngelscriptEditorAssetIdentity`；同时补上 `Legacy_TryConvertPackagePathToVirtualPath()` 和 `Legacy_TryConvertAssetDataToVirtualPath()` 的最小实现，让真实资产能回跳虚拟 item。<br>3. 新增 `FAngelscriptDerivedAssetRegistry` 或 `UAngelscriptAssetIdentitySubsystem`。第一阶段不要求修改已有资产格式：先通过 `ParentClass`、`AssetRegistry` 和现有路径启发式建立只读映射；第二阶段再考虑把 script identity tag 写入新建派生资产，降低反查成本。<br>4. 把 `ShowCreateBlueprintPopup()`、`ShowAssetListPopup()` 内部改为先解析/构造 identity descriptor，再执行创建或打开；对外保留 `UASClass*` 与 `AssetPaths` 旧签名作为 wrapper，避免一次性破坏调用方。<br>5. 在 `UScriptEditorSubsystem` 与 `UEditorSubsystemLibrary` 上新增只读查询入口，例如 `ResolveIdentityFromAsset`、`ResolveIdentityFromVirtualItem`、`FindDerivedAssetsForScriptClass`；这样项目方可以在不修改 `AngelscriptEditorModule.cpp` 的前提下扩展相关工作流。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Identity/AngelscriptEditorAssetIdentity.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Identity/AngelscriptDerivedAssetRegistry.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 同一 `UASClass` 可能天然对应多个派生资产，bridge 不能偷懒假设“一对一”。第一阶段如果过早把 identity 固定成单值，会把现有灵活性反而收窄。 |
| 兼容性 | 向后兼容。现有虚拟路径、`UASClass*` 创建入口、`AssetPaths` 打开入口都可保留；新 bridge 先作为内部统一层和可选查询 API 引入，不要求已有脚本或资产立即迁移。 |
| 验证方式 | 1. 从 `/All/Angelscript` 虚拟项出发，验证可以稳定列出或打开关联的派生 `Blueprint/DataAsset`。<br>2. 从某个派生蓝图出发，验证能够反查回 owning script identity，而不是重新猜目录。<br>3. 通过一个最小 `UScriptEditorSubsystem` 调用新 bridge API，验证无需改核心模块即可追加“Open Related Asset”或“Reveal Owning Script”动作。 |

### Arch-55：编辑器交互仍以 `script class/source` 为主语，缺少围绕当前资产的 asset-centric workflow

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前已经打开的 `UBlueprint`/`UDataAsset` 是否被当作一等 editor context，还是仍要先退回 `UASClass` 或脚本源路径再驱动交互 |
| 当前设计 | 当前 editor 主工作流仍是 source-first / class-first。模块启动时注册的关键交互钩子是 runtime delegate 和全局菜单；真正执行动作时，`ShowCreateBlueprintPopup()` 以 `UASClass*` 为输入，`ShowAssetListPopup()` 以 `AssetPaths + BaseClass` 为输入，最后只是把资产扔给 `UAssetEditorSubsystem::OpenEditorForAsset()`。换句话说，插件可以“把资产打开”，但“当前资产已经打开之后该如何继续交互”还没有成为正式 contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `StartupModule()` 中列出的注册项包括 class reload、source navigation、directory watcher、settings、runtime delegates 和 `Tools` 菜单回调；这里没有形成任何围绕当前 asset editor context 的专门注册链。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:481` — `ShowCreateBlueprintPopup()` 的输入主语就是 `UASClass*`，并非当前 `UBlueprint` 或当前浏览器选择。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:640` — `ShowAssetListPopup()` 的交互主语同样是 `AssetPaths + BaseClass`，底部 `Create New` 按钮也只知道 `BaseClass`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:558` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:583` — 打开动作最终统一退化为 `OpenEditorForAsset(...)`，没有复用当前已打开的 asset editor 上下文。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:33` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:58`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:19` — 项目侧扩展也只能拿到生命周期壳层和通用 getter，无法注册“针对当前资产”的上下文动作。 |
| 优点 | source-first 入口对 debug server、脚本编译错误、命令式“帮我打开相关资产”这类场景很直接，不要求用户先手动打开某个 asset editor。 |
| 不足 | 这里的缺口不是“还没做 toolbar”这么简单，而是交互主语没有升级。推断：只要 `UBlueprint`/`UDataAsset` 不是一等上下文，后续无论是“从当前蓝图打开脚本源”“只对当前脚本蓝图显示校验动作”“在 Blueprint editor 中添加项目自定义工具”，都会继续回到 class/source 解析和全局 popup，而不是围绕当前资产形成闭环。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FBlueprintToolbar::Initialize()` 直接向 `FBlueprintEditorModule` 注册 `FAssetEditorExtender`，`BuildToolbar()` 则把当前 `UBlueprint` 作为交互中心；`BindToLua_Executed()` / `UnbindFromLua_Executed()` 操作完成后会编译并刷新当前蓝图编辑器。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:30`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:75`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:138` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:225` | 当前资产一旦已经在 editor 中打开，就应该成为操作的主语；脚本绑定、路径复制、编译刷新都围绕该上下文展开。 |
| UnrealCSharp | `FUnrealCSharpBlueprintToolBar::Initialize()` 同样通过 `FAssetEditorExtender` 拿到当前 `UBlueprint`；`GenerateBlueprintExtender()` 再根据当前蓝图是否已有 override file，动态给出 `Open File`、`Code Analysis` 或 `Override Blueprint`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:39`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:57` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:137`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:202` | asset-centric workflow 的关键不是“有按钮”，而是按钮背后的 action contract 直接消费当前资产上下文，而不是重新退回全局路径输入。 |
| puerts | `FPuertsEditorModule::OnPostEngineInit()` 为 `UTypeScriptBlueprint` 注册专用 `FKismetCompilerContext`；也就是说，脚本相关蓝图在 editor 里是有专门 compile lane 的，不只是“创建完再打开”。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:152` | 当某类资产长期承担语言桥接职责时，应让 asset type 自己进入 editor/compile 主循环，而不是一直靠外部 helper 驱动。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不推翻现有 source-first 入口的前提下，补一层正式的 `asset context` contract：让“当前打开的脚本相关资产”成为一等上下文，并把项目可扩展动作挂到这层 contract 上。 |
| 具体步骤 | 1. 新增 `FAngelscriptAssetContext`（或等价接口），至少封装 `OpenedAsset`、`ResolvedScriptClass`、`OwningScriptIdentity`、`RelatedDerivedAssets`、`bIsScriptBackedAsset`；这层解析可以直接复用 `Arch-54` 的 identity bridge。<br>2. 在 `AngelscriptEditor` 中新增最小 `BlueprintEditor` 上下文接入：通过 `FBlueprintEditorModule` 的 `FAssetEditorExtender` 或等价机制，把 `Open Script`、`Show Related Assets`、`Run Impact Check For Current Asset` 这类只读动作先接进当前蓝图 editor。第一阶段不要求替换现有全局 popup，只要让当前资产成为正式输入。<br>3. 扩展 `UScriptEditorSubsystem` 与 `UEditorSubsystemLibrary`，增加 `GetActiveAngelscriptAssetContext`、`RegisterAssetContextAction`、`UnregisterAssetContextAction` 等 API，使项目方可以在 Editor Subsystem 中追加自定义动作，而不是继续修改模块静态 helper。<br>4. 保留现有 `GetDebugListAssets()`、`GetEditorCreateBlueprint()` 和 `ShowAssetListPopup()` 作为 fallback：当动作来自 debug server、批处理或用户尚未打开任何 asset editor 时，仍可走 source-first 路径；一旦已有当前资产上下文，就优先走 asset-centric contract。<br>5. 第二阶段再考虑把脚本相关编译校验接到当前资产上下文上，例如只对 `ResolvedScriptClass` 命中的蓝图显示 compile validator 或 diagnostics；避免一开始就把所有蓝图编译路径一起接管。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Context/AngelscriptAssetContext.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Context/AngelscriptAssetContextResolver.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Context/AngelscriptBlueprintContextActions.cpp`。 |
| 预估工作量 | M |
| 架构风险 | `BlueprintEditor`、`AssetEditorSubsystem` 与全局 popup 的生命周期不同，第一阶段若同时引入过多上下文动作，容易出现重复注册或 stale context。更稳妥的顺序是先做只读 context resolver，再逐步挂动作。 |
| 兼容性 | 向后兼容。现有 source-first/debug-server 工作流可以完整保留；新增的 asset-centric contract 只是优先路径，不要求已有脚本项目立刻迁移使用方式。 |
| 验证方式 | 1. 打开一个脚本相关蓝图，确认只在当前资产上下文中出现 `Angelscript` 相关动作，且能直接打开脚本源或列出相关资产。<br>2. 打开普通蓝图，确认这些动作不会误显示。<br>3. 不打开任何 asset editor，验证旧的 debug-server/popup 入口仍然可用。<br>4. 用一个最小 `UScriptEditorSubsystem` 注册自定义 asset-context action，确认无需改核心模块即可对当前资产扩展功能。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-54 | 虚拟脚本项与派生资产分属两条 identity lane，缺少稳定 cross-lane bridge | identity descriptor + derived-asset registry | 高 |
| P1 | Arch-55 | 编辑器交互仍以 `script class/source` 为主语，当前资产不是一等上下文 | asset-context contract + subsystem action registry | 高 |

---

## 架构分析 (2026-04-09 00:58)

### Arch-56：`AngelscriptEditor` 仍缺少可替换、可验证的 service seam，核心 editor workflow 被静态函数与全局单例直接焊死

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编辑器能力是否具备明确的 service object 边界，便于项目方替换、组合和自动化验证 |
| 当前设计 | `FAngelscriptEditorModule` 的 public surface 仍以静态 helper 为主，`ShowAssetListPopup`、`ShowCreateBlueprintPopup`、binding generator 等 workflow 直接暴露为模块静态函数；模块自身几乎不保存 feature object，只在 `StartupModule()` 中一次性把全局 delegate、watcher、popup bridge 和 `ToolMenus` 接上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:12` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:60` — 模块头里公开的是一组静态 workflow/helper，私有状态只有 `StateDumpExtensionHandle`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `StartupModule()` 直接注册 reload helper、source navigation、directory watcher、runtime->editor delegate、`OnObjectPreSave` 与 `ToolMenus` callback。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:538` — `ShowCreateBlueprintPopup()` 同一函数同时负责默认路径推导、modal dialog、`CreatePackage()`、`CreateBlueprint()`、保存和打开编辑器。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:671` — `ShowAssetListPopup()` 直接拼装 `AssetPicker`、`Slate` popup 和 `OpenEditorForAsset()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:81` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:116`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:207` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:242` — data source 直接抓 `FAngelscriptEngine::Get().AssetsPackage` 和 live `UObject`，没有注入式查询/解析层。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:26` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:58`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:19` — `EditorSubsystem` 仍只提供生命周期壳层与 raw getter，没有 typed service access。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:1` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:75`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:1` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:75` — 当前可直接做自动化验证的 editor 代码主要是 helper/scanner；模块主 workflow 没有独立 service seam 可被单独驱动。 |
| 优点 | 接线非常直接，新增一个 editor 行为时实现速度快；现有静态 helper 也让 runtime/debug server 临时调用 editor 弹窗很容易落地。 |
| 不足 | 问题不只是“对象少”，而是能力边界没有被建模。推断：只要资产创建、内容浏览、目录监听、UI 呈现继续被静态函数和全局单例串在一起，项目方就很难只替换其中一个能力，也很难在不启动整套模块装配的前提下验证 editor workflow。随着扩展点继续增多，这会直接抬高回归成本。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块只负责创建和装配 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 等对象；真正的菜单/toolbar 注册落在这些专门对象里，而不是继续堆到模块静态函数。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:49`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:94`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:23` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:30` | 即使仍在单一 editor module 内，也可以先把 surface 和 workflow 抽成独立对象，形成可替换、可验证的 seams。 |
| puerts | `UPEDirectoryWatcher` 和 `UPEBlueprintAsset` 把“目录监听”“蓝图创建/加载”都收进稳定对象；对象内部自己保存 handle、路径和状态，析构时对称清理。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:9` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:17`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:64` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:89`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 先把状态ful workflow 抽成 service object，才能让生命周期、环境约束和后续扩展落到同一处，而不是散在模块静态函数里。 |
| UnrealCSharp | 模块头显式持有 `PlayToolBar`、`BlueprintToolBar`、`FEditorListener`、`DynamicDataSource` 等成员；`StartupModule()` 负责创建它们，`ShutdownModule()` 负责对称释放，listener/data source 则继续封装自己的 editor 订阅。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:106`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:127` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:165`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:45`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:97` | 明确的所有权树会自然带来更好的替换能力和测试入口，因为 feature 不再只能通过“启动整个模块”来触发。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 editor workflow 从模块静态 helper 中拆成可注入的 service object，再让 `EditorSubsystem` 暴露 typed service access；旧静态入口保留为兼容包装层。 |
| 具体步骤 | 1. 新增最小 `IAngelscriptEditorService`/`FAngelscriptEditorHostServices` 边界，第一阶段先只注入 `IAssetRegistry`、`UAssetEditorSubsystem`、`IContentBrowserDataSubsystem`、`IDirectoryWatcher` 和 `FAngelscriptEngine` 访问。<br>2. 把 `ShowCreateBlueprintPopup()`、`ShowAssetListPopup()`、`OnEngineInitDone()` 和 watcher 注册分别迁到 `FAngelscriptAssetCreationService`、`FAngelscriptAssetOpenService`、`FAngelscriptContentBrowserService`、`FAngelscriptScriptWatchService`；`FAngelscriptEditorModule` 只负责创建和销毁这些 service。<br>3. 保留 `FAngelscriptEditorModule` 现有静态函数签名，但内部仅转发到 service locator；这样 runtime bridge、debug server 和旧脚本入口不需要一次性改签名。<br>4. 扩展 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary`，新增只读 typed getter，例如 `GetAngelscriptAssetCreationService()`、`GetAngelscriptContentBrowserService()`；项目方先从消费 service 开始，不立即开放任意注册。<br>5. 新增 native automation tests，直接实例化 service 或替身 dependency：优先覆盖“资产创建默认路径推导”“asset list workflow 不启动真实 popup 时的选择逻辑”“watch service 注册/注销对称”。模块级 smoke test 保留但降到最少。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/*.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Services/*.cpp`、对应 `Private/Tests/*ServiceTests.cpp`。 |
| 预估工作量 | M |
| 架构风险 | service 拆分会重新暴露初始化顺序问题，尤其是 `OnPostEngineInit`、`GEditor`、`ContentBrowserDataSubsystem` 和 runtime bridge 的依赖关系；第一阶段如果同时开放“任意外部注册”会把问题放大。 |
| 兼容性 | 向后兼容。现有静态函数、runtime delegate 和脚本菜单扩展都可以继续存在，只是内部实现改成转发到 service object；旧脚本与外部调用方无需立刻迁移。 |
| 验证方式 | 1. 回归现有 `AngelscriptBlueprintImpactScannerTests` 与 `AngelscriptDirectoryWatcherTests`，确认 helper 层行为不变。<br>2. 新增 service-level automation tests，在不启动完整 popup 的情况下验证资产创建/打开路径和 watcher 生命周期。<br>3. 启动编辑器后检查 `Tools` 菜单、资产创建、asset list popup、data source 和模块 shutdown 行为与现状一致。 |

### Arch-57：editor 侧把多来源 script root 压平成 project content，plugin script root 的 provenance 在 `ContentBrowser` 中丢失

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件脚本根、项目脚本根等多来源内容，是否能在 editor 中保留来源语义并进入 UE 的内容归类/过滤体系 |
| 当前设计 | runtime 已经支持 project root + enabled content plugin root 的混合脚本根发现，但 editor data source 仍把所有脚本项统一映射到 `/All/Angelscript/<AssetName>`，并硬编码成 project content。结果是 plugin-provided script library 进入编辑器后，会被误表现为宿主项目内容。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558` 到 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:564` — `GetEnabledPluginScriptRoots` 会把 enabled content plugin 的 `<Plugin>/Script` 加入候选根。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1342` 到 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1363` — `DiscoverScriptRoots()` 明确把 project root 与 plugin script roots 组合成 `DiscoveredRootPaths`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:372` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:380` — editor watcher 启动时遍历 `MakeAllScriptRoots()`，说明 editor 实际也感知到了多来源 script root。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:10` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:14` — payload 只保存 `Path` 和弱 `Asset`，没有 root provenance 字段。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:29` — `CreateAssetItem()` 无论来源如何都生成 `/All/Angelscript/<AssetName>`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:155` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:169` — `ItemIsEngineContent=false`、`ItemIsProjectContent=true`、`ItemIsPluginContent=false` 被硬编码为统一值。 |
| 优点 | 所有脚本项都能快速集中到单一浏览器根下，最开始做展示时实现成本最低。 |
| 不足 | 这不是单纯的显示问题，而是 provenance 被抹平。推断：只要 plugin root、project root、未来自定义 root 在 payload 和属性层都没有正式来源字段，后续无论是按来源过滤、按 plugin 分组、为某个 root 应用不同创建策略，还是排查“这个脚本来自哪个插件”，都只能继续靠路径猜测或额外启发式逻辑。对于插件产品化交付，这会直接削弱可诊断性。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `DynamicDataSource` 把 internal path 和 virtual path 分开保存，folder/file item 都保留内部路径；创建 item 时还带 `Category_Plugin`，说明内容分类不是靠单一 display path 硬猜。新建类对话框则通过 `FProjectContent` 明确建模“项目名 -> 源路径”映射。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:642` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:681`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:788` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:833`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:50` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:65` | 先把 root/internal path provenance 保存下来，再决定 UI 如何投影；这样内容分类、创建路径和路径回桥才能共用同一份来源语义。 |
| UnLua | 不额外发明 project-only 虚拟 content source，而是直接基于 `AssetRegistry` 的真实 `AssetData` 做筛选与更新；蓝图的 package/object path 自身就是来源语义。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:49` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:73`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:281` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:290` | 如果 editor workflow 能站在真实 asset/path plane 上，project/plugin provenance 会天然跟着 package path 走，不需要再手写一套 project-only flag。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate()` 把脚本派生产物锚定到稳定 `PackageName`，创建成功后直接 `AssetCreated(Blueprint)`；它选择的是“真实 package path 作为唯一来源语义”，而不是在虚拟浏览器层再人为声明一组 project/plugin 属性。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:95`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:144` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 即使只支持一条主内容 lane，也应让 provenance 落在稳定 identity 上，而不是在 editor 展示层全部压平成同一来源。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `AngelscriptData` 增加正式的 root provenance contract，把“来自 project 还是 plugin”从隐藏约定提升为 payload/index 上的显式字段，并把该字段接回 `ContentBrowser` 属性与路径层。 |
| 具体步骤 | 1. 新增 `FAngelscriptScriptRootDescriptor` / `FAngelscriptEditorRootProvenance`，第一阶段至少记录 `RootKind(Project/Plugin/Engine)`、`RootDisplayName`、`OwningPluginName`、`AbsoluteRootPath`、`RelativeSourcePath`。<br>2. 升级 `FAngelscriptContentBrowserPayload`，不再只存 `Path + Asset`；改为同时保存 provenance 和稳定 source-relative key。`CreateAssetItem()` 根据 provenance 生成分层 virtual path，例如 `/All/Angelscript/Project/...` 与 `/All/Angelscript/Plugins/<PluginName>/...`。<br>3. 重写 `GetItemAttribute()`，让 `ItemIsProjectContent`、`ItemIsPluginContent`、`ItemIsEngineContent` 由 provenance 驱动，而不是硬编码统一值；必要时再补 `ItemColor`/display name policy。<br>4. 通过新的 content browser index 或 `EditorSubsystem` facade 暴露 `EnumerateScriptRoots()`、`ResolveScriptRootForItem()` 等只读 API，使项目方能基于 root provenance 追加过滤策略、不同的 authoring policy 或 root-specific menu action。<br>5. 兼容阶段保留旧的平铺 alias 或搜索回桥：已有 `/All/Angelscript/<AssetName>` 仍能定位到 item，但默认 UI 展示切到 provenance-aware 分层视图；等引用和路径合同稳定后再考虑移除 alias。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/ContentBrowser/AngelscriptScriptRootDescriptor.h` 与相应 index/service 实现。 |
| 预估工作量 | M |
| 架构风险 | 若第一阶段就彻底切掉旧 flat path，容易打断已有书签、搜索结果和脚本工具对 `/All/Angelscript/<AssetName>` 的隐式依赖；更稳妥的方式是先引入 provenance-aware path，再保留 alias 过渡。 |
| 兼容性 | 可增量实施。现有脚本资产对象和 runtime root 发现逻辑都可以保留；变化主要发生在 payload、路径和属性层。旧 flat path 可作为兼容别名继续工作。 |
| 验证方式 | 1. 在同时存在 project script root 与 plugin script root 的工程中，确认 `ContentBrowser` 能分开显示并正确过滤 `Project`/`Plugin` 项。<br>2. 为 project/plugin 各放一个同名脚本资产，确认虚拟路径和引用不再冲突。<br>3. 通过最小 `UScriptEditorSubsystem` 调用 root provenance API，验证无需修改核心模块即可基于来源追加自定义菜单或创建策略。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-57 | 多来源 script root 在 editor 中被压平成 project content，plugin provenance 丢失 | provenance descriptor + root-aware content browser contract | 高 |
| P2 | Arch-56 | editor workflow 缺少可替换、可验证的 service seam | service object 拆分 + typed subsystem facade | 中高 |

---

## 架构分析 (2026-04-09 01:14)

### Arch-58：专用 `Script*MenuExtension` 子类把“上下文类型”和“surface 投影”锁进继承树

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 扩展作者能否把同一组 action 逻辑复用到不同 surface，还是必须先选定一个继承分支才能定义上下文模型 |
| 当前设计 | `UScriptActorMenuExtension` 与 `UScriptAssetMenuExtension` 不只是“便利封装”，而是把 `ExtensionMenu/ExtensionPoint`、selection 类型和调用路径都固化进子类。基类只保留 `SelectedObjects/SelectedAssets` 两个数组，并在执行时重新实例化扩展类后调用虚函数，因此 action 的语义边界主要由“继承自哪一个类”决定，而不是由独立的 context policy / projection descriptor 决定。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.h:11` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.h:16` — actor 扩展构造函数直接固定到 `LevelViewport_ContextMenu` 和 `"ActorPreview"`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h:11` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h:15` — asset 扩展构造函数直接固定到 `ContentBrowser_AssetViewContextMenu` 和 `"CommonAssetActions"`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:98` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:107` — 基类上下文只建模 `SelectedObjects` 与 `SelectedAssets`，没有独立的 selection policy / projection 对象。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:367` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:375` — 执行动作时会临时 `NewObject<UScriptEditorMenuExtension>(..., Extension->GetClass())`，再回调该类自己的 `CallFunctionOnSelection(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp:4` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp:75`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp:77` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp:123` — actor 分支自己实现 `SupportedClasses` 过滤、首参数类型匹配和 prompt 调用。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:4` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:39`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:41` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:116` — asset 分支也各自重复一套过滤和调用逻辑。 |
| 优点 | 对单一场景脚本作者很直观：想扩 actor 菜单就继承 actor 类，想扩 asset 菜单就继承 asset 类，不需要先理解额外的 registry 或 descriptor。 |
| 不足 | 这里的限制不只是“上下文不够强类型”，而是扩展作者模型本身被继承树锁死。推断：只要 `surface`、`selection` 和 `invocation` 继续绑定在 `UScriptActorMenuExtension` / `UScriptAssetMenuExtension` 这类专用子类上，项目方即使拿到了 `EditorSubsystem`，也无法把同一 action 逻辑重投影到另一个 surface，只能复制一个新的扩展类分支，并继续维护两套近似相同的过滤与调用代码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FBlueprintToolbar::Initialize()` 通过 `FAssetEditorExtender` 从 `ContextSensitiveObjects` 中提取 `UBlueprint`，再交给 `GetExtender(TargetObject)`；真正的 action owner `FUnLuaEditorToolbar` 再用 `BuildToolbar(..., InContextObject)` 和 `ContextObject` 驱动行为。surface 注入与 action 承载是分开的。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:30`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:75`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:127` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:135` | 不必让“扩展类类型”同时承担 surface 选择和 action 执行；更稳的做法是用 host adapter 注入上下文，再由独立 action owner 消费。 |
| UnrealCSharp | `FUnrealCSharpBlueprintToolBar::Initialize()` 也是先通过 `FAssetEditorExtender` 注入 `UBlueprint*`，再由 `GenerateBlueprintExtender(InBlueprint)` 生成对应 UI。`UDynamicDataSource::PopulateAddNewContextMenu()` 则显式收集 `SelectedClassPaths`，再调用 `FDynamicNewClassContextMenu::MakeContextMenu(...)`，内容浏览器路径 surface 也没有再发明一棵继承树。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:40`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:203`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:727`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:7` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:66` | `BlueprintEditor` 与 `ContentBrowser` 可以各自有 context adapter，但 action/projector 不必靠专门子类复制实现。 |
| puerts | `FPuertsEditorModule` 把 `UTypeScriptBlueprint` 的编译扩展接到 `RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler)`，而 `MakeCompiler(...)` 本身只声明自己吃 `UBlueprint*`。上下文约束由注册点和函数签名表达，不需要再为“这一类上下文”额外造一个继承族。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120` | 即便是语言特定的 editor 集成，也更适合把“在哪注册”和“如何执行”拆开，而不是把它们一起编码到扩展类继承关系里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `UScriptActorMenuExtension` / `UScriptAssetMenuExtension` 从“真正的扩展模型”降级为兼容 wrapper，引入独立的 `selection policy + surface projection` 描述层。 |
| 具体步骤 | 1. 新增 `FAngelscriptSelectionPolicyDescriptor`（或等价类型），第一阶段至少覆盖 `SelectedActors`、`SelectedAssets`、`SelectedPaths` 三类内建 policy；`UScriptEditorMenuExtension` 只负责 action discovery 与元数据，不再要求靠子类类型决定 selection 语义。<br>2. 新增 `FAngelscriptEditorProjectionDescriptor`，把 `SurfaceId`、`ExtensionPoint`、`SectionName`、`SelectionPolicyId`、`Order` 组合成独立描述；旧的 `ExtensionMenu/ExtensionPoint` 字段先继续保留，内部自动转换成 descriptor。<br>3. 将 `UScriptActorMenuExtension` 和 `UScriptAssetMenuExtension` 保留为兼容 wrapper：它们只导出默认 projection 与默认 selection policy，不再各自保存完整的过滤/调用实现；重复逻辑迁到共享 `ActorSelectionPolicy` / `AssetSelectionPolicy`。<br>4. 给 `UScriptEditorSubsystem` 或新的 native registry 增加 `RegisterExtensionProjection()` / `RegisterSelectionPolicy()` 入口，使项目方可以把同一扩展类投影到多个 surface，而不必继续复制一个“Actor 版”和一个“Asset 版”脚本类。<br>5. 迁移期先只重构内建 actor/asset 两条分支；旧脚本类名、旧菜单位置和旧函数元数据全部保留，待 registry 稳定后再考虑开放更多自定义 policy。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/SelectionPolicies/*` 与对应 `Private/*`。 |
| 预估工作量 | M |
| 架构风险 | 双轨期会同时存在“旧继承式声明”和“新 descriptor/policy 声明”；如果迁移顺序处理不好，容易出现同一扩展被注册两次或 selection policy 不一致。 |
| 兼容性 | 向后兼容。现有 `UScriptActorMenuExtension` / `UScriptAssetMenuExtension` 脚本类可以原样保留，只是内部改为导出 projection/policy；已有菜单显示位置和执行入口不需要立即改写。 |
| 验证方式 | 1. 保留一个现有 actor 扩展类不改脚本，确认菜单位置和执行结果与现状一致。<br>2. 用同一套 action 逻辑注册两个 projection，例如一个进 `LevelViewport`、一个进 `ContentBrowser`，确认无需复制第二个继承类即可工作。<br>3. 通过最小 `UScriptEditorSubsystem` 注册一个自定义 projection，确认项目方可以在不修改核心 `switch` 和继承树的前提下追加 surface。 |

### Arch-59：`UScriptAssetMenuExtension` 仍靠首参数反射启发式决定 payload lane，资产 action 合同不稳定

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | asset menu action 的输入 payload 是否有显式 contract，还是由函数首参数和 `SupportedClasses` 的组合被隐式推断 |
| 当前设计 | `UScriptAssetMenuExtension` 当前没有显式声明“给 action 传 `FAssetData`、传已加载 `UObject`，还是传路径/identity”。`GatherExtensionFunctions()` 只负责 `SupportsAsset` 与 `SupportedClasses` 过滤；真正执行时，`CallFunctionOnSelection()` 会检查函数首参数是不是 `FObjectProperty`，再结合 `SupportedClasses` 是否包含同类来推断 `bFunctionTakesObject`。一旦判定为 object lane，就调用 `Asset.GetAsset()` 触发加载；否则把 `FAssetData` 复制到 `FStructOnScope` 里走 struct lane。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:4` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:38` — 函数可见性只依赖 `SupportsAsset(Asset)` 与 `SupportedClasses`，没有独立 payload mode。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:43` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:69` — `bFunctionTakesObject` 通过首参数 `FObjectProperty` 和 `SupportedClasses` 的相等匹配来推断。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:71` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:95` — 真正执行时要么 `CallWithObjects.Add(Asset.GetAsset())`，要么 `CallWithStructs.Add(MakeShared<FStructOnScope>(AssetDataStruct, (uint8*)&Asset))`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:97` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp:115` — prompt/执行分支完全取决于上面的推断结果。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:352` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:375` — UI action 执行层并不知道 asset action 期望哪种 payload，只是无条件回调子类的 `CallFunctionOnSelection(...)`。 |
| 优点 | 对历史脚本 action 很省事：写一个 `FAssetData` 或对象参数函数，就能在不引入新 descriptor 的情况下快速挂进 asset menu。 |
| 不足 | 当前问题不只是“参数推断比较脆弱”，而是 visibility、payload 和加载策略被混在同一个启发式里。推断：只要 action 的真实输入 contract 仍然靠“首参数长什么样”和“`SupportedClasses` 有没有填对”来猜，扩展作者改一个函数签名就可能默默改变 asset loading 行为；更糟的是，当 `SupportedClasses` 为空时，即使函数首参数是对象类型，也不会进入 object lane。这样 `EditorSubsystem` 未来即便能注册 asset action，也拿不到稳定的 payload contract 去做缓存、预加载或 headless 校验。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `BuildToolbar(FToolBarBuilder&, UObject* InContextObject)` 直接接受显式上下文对象；后续命令执行通过 `ContextObject` 读取当前 `UBlueprint`。输入 contract 在 toolbar API 上就已经固定，不需要再从动作函数签名反推。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:138` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:149` | action handler 应该消费显式上下文对象，而不是把 payload 选择留给首参数启发式。 |
| UnrealCSharp | `GenerateBlueprintExtender(UBlueprint* InBlueprint)` 显式接收 `UBlueprint*`；`FDynamicNewClassContextMenu::MakeContextMenu(...)` 则显式接收 `TArray<FName> InSelectedClassPaths`。同一个插件里，不同 surface 各自声明自己吃的是对象还是路径，没有再让 callback 自己猜。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:203`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:7` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:66` | 对象 lane 与路径 lane 应该是显式 contract，而不是同一条 action 注册链里的隐式分支。 |
| puerts | `MakeCompiler(UBlueprint* InBlueprint, ...)` 与 `UPEBlueprintAsset::LoadOrCreate(const FString& InName, const FString& InPath, UClass* ParentClass, ...)` 都把输入对象/路径/父类作为显式参数定义出来，再由注册点决定何时调用。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:159` | 先定义好 payload 形状，再选择注册 surface；不要让 surface 回调去猜 action 真正需要的是对象、路径还是 metadata。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 asset action 引入显式 `payload binding` 描述，把“菜单可见性”“对象过滤”“是否触发加载”三件事拆开；旧启发式保留为 `Auto` 兼容模式。 |
| 具体步骤 | 1. 新增 `EAngelscriptAssetActionPayloadMode`，第一阶段至少包含 `Auto`、`AssetData`、`LoadedObject`、`AssetPath` 四档；`UScriptAssetMenuExtension` 或 action descriptor 必须能为每个函数导出该模式。<br>2. 将 `SupportedClasses` 的职责收窄为“菜单可见性/对象过滤”，不再顺带决定 payload lane；新增独立 `ExpectedObjectClass` 或从显式 mode 推导的 `PayloadClass`。<br>3. 重写 `CallFunctionOnSelection()`：`Auto` 继续保留当前启发式以兼容旧脚本，但只要显式指定了 `AssetData` / `LoadedObject` / `AssetPath`，就按声明执行，不再检查首参数；同时对“对象参数但 `SupportedClasses` 为空”这类旧逻辑给出 warning，帮助项目发现潜在误判。<br>4. 通过 `UScriptEditorSubsystem` 或新的 action registry 暴露 `RegisterAssetPayloadResolver()` / `RegisterAssetActionPolicy()`，让项目方能补充自定义 lazy-load、批量预取或 headless 禁止加载策略，而不必修改 `ScriptAssetMenuExtension.cpp`。<br>5. 迁移期先把一到两个内建/示例 asset action 改成显式 mode，验证旧 `Auto` 与新显式声明可以并存；等文档稳定后再逐步鼓励项目侧弃用启发式。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/AngelscriptAssetActionPayloadMode.h` 与相应 resolver/registry 实现。 |
| 预估工作量 | S-M |
| 架构风险 | 兼容期如果同时保留 `Auto` 和显式 mode，最容易出现的风险是同一 action 在不同模式下表现不一致；因此需要先加诊断日志，把“当前到底走了哪条 payload lane”明确暴露出来。 |
| 兼容性 | 向后兼容。现有 asset action 默认继续走 `Auto`；只有显式声明新 mode 的 action 才会切换到稳定 contract。旧脚本签名可以保持不变。 |
| 验证方式 | 1. 保留一个旧 `Auto` asset action，确认行为与当前版本一致。<br>2. 新增一个显式 `AssetData` action 和一个显式 `LoadedObject` action，确认前者不会触发 `Asset.GetAsset()`，后者会稳定加载并传入对象。<br>3. 让一个最小 `UScriptEditorSubsystem` 注册自定义 payload resolver，验证项目方可以在不改核心启发式的前提下控制 asset loading policy。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-58 | 专用 `Script*MenuExtension` 子类把 context type 与 surface 投影锁进继承树 | projection/policy 解耦 | 高 |
| P1 | Arch-59 | asset action 仍靠首参数启发式决定 payload lane，合同不稳定 | 显式 payload binding contract | 高 |

---

## 架构分析 (2026-04-09 01:29)

### Arch-60：editor 诊断/分析能力仍分散在 `menu lambda`、`commandlet` 与 `dump hook` 三条调用链，没有统一的 diagnostics service

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 内诊断、分析、导出类能力是否有统一调用合同，项目方能否通过 `EditorSubsystem` 复用 |
| 当前设计 | `AngelscriptEditor` 目前把 editor 侧诊断能力拆成三条互不统一的入口：`Tools` 菜单只注册 `ASOpenCode / ASGenerateBindings / Function Tests`；`BlueprintImpact` 通过 public scanner API + `commandlet` 暴露；editor state dump 则通过 `FAngelscriptStateDump::OnDumpExtensions` 私下挂接。`UEditorSubsystemLibrary` 仍只暴露泛型 getter，没有任何 diagnostics facade。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:745` — `RegisterToolsMenuEntries()` 只注册 `ASOpenCode`、`ASGenerateBindings`、`Function Tests`，没有 `BlueprintImpact` 或 dump 入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:364` — 模块启动时单独注册 `RegisterStateDumpExtension(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:105` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:118` — `DumpEditorState()` 只通过 `FAngelscriptStateDump::OnDumpExtensions.AddStatic(...)` 挂到 runtime dump 扩展链。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:120` — `BlueprintImpact` 的正式入口是 `commandlet Main()`，命令行解析、扫描执行和摘要输出都在这里闭合。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:62` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:67` — scanner API 虽然已 public，但没有被 editor tool/service 再封装成统一 contract。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:16` — 对外仍只有 `GetEditorSubsystem(...)`，没有 diagnostics 查询或执行 API。 |
| 优点 | 这些能力各自都已经能工作，且实现之间耦合较低；`BlueprintImpact` 和 state dump 也都保留了明确的机器可读输出。 |
| 不足 | 当前缺的不是某个单点功能，而是一层统一的“editor diagnostics capability”。推断：后续只要再新增 `workspace index`、reload 诊断、content browser consistency check 或用户自定义 editor 审计，就会继续在菜单、console、commandlet、dump hook 之间重复接线，`EditorSubsystem` 也拿不到一个稳定的入口去复用、组合或禁用这些能力。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaEditorModule::OnPostEngineInit()` 同时初始化 toolbar 与 `FUnLuaIntelliSenseGenerator`；`MainMenuToolbar` 把 `Generate IntelliSense` command 映射到 `UpdateAll()`；`UUnLuaIntelliSenseCommandlet` 又复用同一个 generator 执行 headless 导出。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:98` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:101`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:34` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:36`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:109` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:111` | 同一分析能力先沉成 service/generator，再按 toolbar 与 commandlet 两个 surface 做 adapter，避免每条调用链各写一套。 |
| puerts | `FDeclarationGenerator` 用同一个 `GenUeDts()` 承接菜单、toolbar、console 和 notification：`PluginCommands->MapAction(...)`、`UToolMenus::RegisterStartupCallback(...)`、`FAutoConsoleCommand("Puerts.Gen")` 都落到同一执行函数。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1615` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1631`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1650` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1655`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1668` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1686` | 先统一执行合同，再决定放进 menu、toolbar 还是 console；surface 变多不会复制业务逻辑。 |
| UnrealCSharp | `CodeAnalysis` 同时有 console command、startup/generator path 和 `BlueprintEditor` toolbar action；不同入口最终都围绕同一批 code-analysis/dynamic-generator 能力展开。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:68` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:74`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:260` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:264`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:72` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:80`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:182` | 诊断能力可以同时服务 editor startup、当前 asset 上下文和 console，而不必拆成互不相认的独立工具。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增统一的 `editor diagnostics service`，把 `BlueprintImpact`、editor state dump、未来 workspace/reload 检查都收敛到同一能力层；现有菜单、commandlet、dump hook 先改为 adapter。 |
| 具体步骤 | 1. 新增 `UAngelscriptEditorDiagnosticsService` 或等价 native registry，最少定义 `DiagnosticId`、`PreferredSurface`、`bCanRunHeadless`、`RunInteractive(Request)`、`RunHeadless(Request)`、`BuildSummary()` 六个要素；第一阶段只覆盖现有 `BlueprintImpact` 与 editor state dump。<br>2. 让 `UAngelscriptBlueprintImpactScanCommandlet::Main()` 退化为 service adapter：参数解析和 exit code 保持原样，真正扫描改为调用 diagnostics service 中的 `BlueprintImpact` provider。<br>3. 把 `RegisterStateDumpExtension(...)` 改成 diagnostics service 的一个 `DumpProvider` 适配器；保留现有 `EditorReloadState.csv` / `EditorMenuExtensions.csv` 文件名，避免破坏已有外部脚本。<br>4. 给 `RegisterToolsMenuEntries()` 与未来 `FAutoConsoleCommand` 统一接 diagnostics descriptor，而不是继续写匿名 lambda；`BlueprintImpact` 第一阶段可先加一个 developer submenu 入口，结果只需复用现有 JSON 摘要或 notification。<br>5. 在 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 上增加只读 facade，例如 `GetAngelscriptEditorDiagnosticsService()`、`RunDiagnostic(DiagnosticId, Request)`、`EnumerateDiagnostics()`；项目方之后可以新增自定义检查，而不必继续修改模块主文件。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Diagnostics/*.h` 与 `Private/Diagnostics/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 最大风险是把 interactive UX 与 headless contract 过早绑死在一个实现里；第一阶段应只统一 provider/descriptor 与请求结果模型，保留各 surface 自己的展示适配层。 |
| 兼容性 | 向后兼容。现有 `BlueprintImpact` commandlet 参数、state dump 文件名和 `Tools` 菜单现有条目都可以保持；新 service 先作为内部整合层引入，不要求项目方立即迁移已有脚本或流水线。 |
| 验证方式 | 1. 从 commandlet 路径执行 `BlueprintImpact`，确认 exit code 与 JSON 摘要保持一致。<br>2. 触发一次 editor state dump，确认仍生成 `EditorReloadState.csv` 与 `EditorMenuExtensions.csv`。<br>3. 通过新增的 diagnostics facade 从最小 `UScriptEditorSubsystem` 调用一次 `BlueprintImpact` 或 dump，确认无需改模块静态函数即可复用能力。 |

### Arch-61：`EditorStateDump` 仍是硬编码两张表的私有附加项，没有 feature-level snapshot provider contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 架构本身是否具备可观测性，新增 feature 或项目级扩展能否把自己的运行状态纳入统一快照 |
| 当前设计 | 当前 editor dump 只观测两类静态内核：`FClassReloadHelper::ReloadState()` 和 `UScriptEditorMenuExtension::GetRegisteredExtensionSnapshots()`。模块头也几乎不持有其它 feature object，只有一个 `StateDumpExtensionHandle`。这意味着 state dump 今天更像“给两个现成全局结构导出 CSV”，而不是“任何 editor feature 都能贡献自身快照”的 provider 体系。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:57` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:60` — 模块私有状态只有 `StateDumpExtensionHandle`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `StartupModule()` 直接把 reload helper、source navigation、directory watcher、runtime bridge、state dump 和 `ToolMenus` 逐个 inline 注册，没有对应的 feature 成员树。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:21` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:67` — `SaveEditorReloadState()` 只导出 reload 相关类/枚举/结构体/委托。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:78` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:109` — `SaveEditorMenuExtensions()` 只导出菜单扩展的 `ExtensionPoint/Location/SectionName`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:16` — 项目侧也拿不到任何 `snapshot provider` 或 diagnostics registrar。 |
| 优点 | 现有实现非常直接，导出成本低；当前两张 CSV 也确实覆盖了 editor 中最容易失真的两块全局状态：reload 结果和菜单注册结果。 |
| 不足 | 这条快照链今天并不知道 `ContentBrowserDataSource`、watch root、source navigation handler、runtime->editor bridge、未来 `EditorSubsystem` service 或项目自定义扩展的存在。推断：只要 editor 功能继续从 `Arch-60` 之后演化成多个 service/presenter/provider，现有 dump 机制就会越来越像“架构外部的旁路观察”，而不是能帮助定位 feature 生命周期、注册状态和配置漂移的正式可观测性层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 模块头显式持有 `PlayToolBar`、`BlueprintToolBar`、`EditorListener`、`DynamicDataSource` 与多条 console command；各 feature 都有稳定类型和成员位置。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55` | 当 feature 先成为显式对象，再谈 snapshot/provider 就很自然；诊断不必再从模块外部猜测系统有哪些能力。 |
| UnLua | 模块拥有 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 和 `FUnLuaIntelliSenseGenerator`；`FUnLuaIntelliSenseGenerator` 自身又公开 `Initialize/UpdateAll` 与 asset-event handler。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:56` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:98` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:101`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSenseGenerator.h:32` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSenseGenerator.h:35`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSenseGenerator.h:58` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSenseGenerator.h:65` | 先让功能以长生命周期对象存在，后续无论做测试、诊断还是扩展，都有稳定宿主可以挂 provider。 |
| puerts | `UPEDirectoryWatcher` 把 `Directory`、`DelegateHandle` 和 `Watch/UnWatch` 生命周期封装进一个具体对象，而不是隐藏在模块 free function 里。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:18`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:67` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:89` | 即使只是一个 watcher，显式对象化后也更容易输出“当前监视目录、是否已注册、最后一次变更”等诊断信息。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 editor 可观测性从“一个私有 dump callback”升级为“feature-level snapshot provider registry”；现有两张 CSV 继续保留，但由 provider 统一产出。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorSnapshotProvider` 或 `UAngelscriptEditorDiagnosticsProvider`，最少定义 `GetProviderId()`、`AppendCsvSnapshots(OutputDir)`、`BuildStatusSummary()`；第一阶段只实现 `ReloadStateProvider` 与 `MenuExtensionProvider` 两个现有 provider。<br>2. 在 `AngelscriptEditorStateDump.cpp` 中把 `DumpEditorState()` 改为遍历 provider registry，而不是直接调用两个静态函数；旧的 `SaveEditorReloadState()` / `SaveEditorMenuExtensions()` 先作为 provider 内部实现保留。<br>3. 随着 editor feature service 化，逐步补充 `ContentBrowserDataSource`、watch service、source navigation、tool registry、diagnostics service 等 provider；provider 只暴露只读快照，不承担业务逻辑。<br>4. 给 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 增加只读枚举接口，例如 `EnumerateDiagnosticsProviders()`、`DumpProviderState(ProviderId)`；项目方可把自己的 editor service 注册为 provider，而不必修改核心 dump 文件。<br>5. 兼容期保持现有 `FAngelscriptStateDump::OnDumpExtensions` 入口不变，只是内部转成 provider registry；等项目级 provider 稳定后，再考虑增加状态摘要面板或 `FMessageLog` 入口。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Diagnostics/AngelscriptEditorSnapshotProvider.h`、`Private/Diagnostics/AngelscriptEditorSnapshotRegistry.cpp`、对应 provider 实现。 |
| 预估工作量 | S-M |
| 架构风险 | 如果在 feature ownership 尚未稳定前就一次性要求所有模块都实现 provider，容易把 snapshot 接口做得过宽；第一阶段应只收口“输出 CSV/摘要”的最小只读合同，避免 provider 反过来侵入业务层。 |
| 兼容性 | 向后兼容。现有 dump 入口和已有 CSV 文件名都可保持不变；新增 provider 只是扩展快照覆盖面，对现有脚本和外部工具无破坏。 |
| 验证方式 | 1. 执行一次现有 dump 流程，确认 `EditorReloadState.csv` 和 `EditorMenuExtensions.csv` 内容保持一致。<br>2. 新增一个最小 provider（例如 watch service provider），确认不改旧 dump 入口也能产出新 CSV。<br>3. 通过最小 `UScriptEditorSubsystem` 枚举 provider，验证项目方无需修改 `AngelscriptEditorStateDump.cpp` 就能追加自己的 editor 状态快照。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-60 | editor 诊断/分析能力分散在 menu、commandlet、dump hook 三条调用链 | 统一 diagnostics service | 高 |
| P2 | Arch-61 | `EditorStateDump` 仍是硬编码两张表，缺少 feature-level snapshot provider | 可观测性与扩展性补强 | 中高 |

---

## 架构分析 (2026-04-09 01:38:46)

### Arch-62：editor 变更动作只有 `FScopedTransaction` 壳，缺少稳定的 undo participant contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本扩展动作和内建 editor workflow 是否真正接入 UE 的 undo/redo 与 transaction 语义 |
| 当前设计 | `UScriptEditorMenuExtension` 在 generic action wrapper 里统一创建 `FScopedTransaction`，但 transaction participant 没有被框架显式声明；`CallFunctionOnSelection()` 甚至直接丢弃传入 `Selection`。模块内建的“Create Blueprint/DataAsset”工作流则直接创建资产、`MarkPackageDirty()`、保存并打开 editor，没有 transaction host。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:143` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:151` — `CallFunctionOnSelection()` 调 `FScriptEditorPrompts::ShowPromptToCallFunction(...)` 时传入的是空 `TArray<UObject*>()`，没有把 `Selection` 里的对象作为显式参与者带入调用链。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:257` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:279` — `CreateToolUIAction()` 只创建临时对象、设置上下文、包一层 `FScopedTransaction`，随后直接 `CallFunctionOnSelection()`；wrapper 自身没有对选中对象 `Modify()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:355` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:375` — 普通 `FUIAction` 路径同样只有 transaction 标题，没有 participant 收集逻辑。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:497` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:537` — `ShowCreateBlueprintPopup()` 直接 `CreateBlueprint/NewObject`、`AssetCreated`、`MarkPackageDirty`、`PromptForCheckoutAndSave`、`OpenEditorForAsset`，没有 transaction 或 undo host。 |
| 优点 | 实现非常轻，脚本 action 至少能得到统一的 transaction 名称；不要求每个扩展先学习复杂的 editor undo API。 |
| 不足 | 推断：当前 contract 只声明了“这里是一个 transaction 边界”，没有声明“哪些对象/文件是 transaction participant”。结果是 undo 是否可靠完全取决于下游脚本函数或 native helper 是否自行 `Modify()`；generic editor 扩展框架本身无法保证资产、Actor、Blueprint graph 或虚拟文件变更被纳入统一撤销链。对未来 `EditorSubsystem` 扩展者来说，这不是稳定的 editor contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 在 Blueprint 覆写图创建路径里先建立 `FScopedTransaction`，再对 `Blueprint` 和新 graph 显式 `Modify()`，让 transaction participant 由框架代码确定，而不是交给调用者碰运气。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:478` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:485` | 对“谁会被撤销”做显式建模，比只给一个 transaction 标题更适合作为可复用扩展底座。 |
| UnrealCSharp | `UDynamicDataSource` 删除文件时不仅建立 `FScopedTransaction`，还把文件内容封装成 `FDeleteFileChange` 并显式存入 `GUndo`，把非 `UObject` 变更也纳入 undo 链。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:570` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:583` | 如果 editor workflow 会触及文件系统或虚拟资产，transaction contract 不能只覆盖 `UObject`，还要允许 file-backed change 进入统一撤销模型。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有“包一层 `FScopedTransaction`”升级为“显式声明 participant 的 mutation contract”，先覆盖脚本菜单动作，再逐步覆盖内建资产工作流。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorMutationContext` 或等价 service，最少包含 `DisplayName`、`SelectedObjects`、`SelectedAssets`、`TouchedObjects`、`FileChanges` 四类信息；第一阶段只服务 `UScriptEditorMenuExtension`。<br>2. 在 `CreateUIAction()` / `CreateToolUIAction()` 中，把 `Selection` 正式传入 mutation context；默认对 `Selection.SelectedObjects`、从 `SelectedAssets` 解析出的已加载对象执行 `Modify()`，再进入 `ProcessEvent`。<br>3. 给脚本扩展增加可选 hook，例如 `GatherTransactionParticipants` / `IsUndoableAction`；复杂动作可追加 participant，纯只读动作可跳过 transaction，避免无意义的 undo 记录。<br>4. 第二阶段把 `ShowCreateBlueprintPopup()`、未来 `ContentBrowser` 删除/重命名、toolchain 生成结果也迁入同一个 mutation service；对 file-backed 变更按 UnrealCSharp 的思路增加 `FChange` 适配层。<br>5. 为 `EditorSubsystem` 暴露只读 mutation API 或 helper，让项目级 subsystem 不必重新发明 transaction/undo participant 收集逻辑。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Transactions/AngelscriptEditorMutationContext.h` 与对应 `Private/Transactions/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段对所有 `Selection` 都无差别 `Modify()`，可能把大量只读对象误纳入 transaction，增加 undo 栈噪声；participant 收集必须支持 opt-out 与按动作覆盖。 |
| 兼容性 | 向后兼容。旧脚本 action 不需要改签名即可继续工作；第一阶段只是让默认 undo 语义更完整。只有明确声明 `IsUndoableAction=false` 的新扩展才会改变当前“总是开 transaction 壳”的行为。 |
| 验证方式 | 1. 写一个修改 Actor 或 Blueprint 资产的脚本 menu action，确认执行后 `Undo/Redo` 能稳定回滚。<br>2. 对只读 action 验证不会产生无意义 transaction。<br>3. 复测现有 `Shift + 点击` 的源码导航路径，确认不会意外进入 mutation/undo 链。 |

### Arch-63：`LevelViewport_SourceControlMenu` 只是 UI 挂点，editor 写盘路径没有统一的 source-control / writable policy

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 工具在生成代码、创建资产、删除旧文件时，是否有统一的 source control / writeability / save policy，可被 `EditorSubsystem` 和外部扩展复用 |
| 当前设计 | 当前插件把 `SourceControl` 只当成一个可插菜单的位置，而不是一项可消费的 editor capability。包资产创建路径会调用 `PromptForCheckoutAndSave()`，但 legacy bind/module 生成直接 `SaveStringArrayToFile()` 与 `IFileManager::Delete()`；模块内没有统一的写盘策略服务，也没有给脚本侧暴露“checkout before write / make writable / save packages”能力。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:20` — `EScriptEditorMenuExtensionLocation` 把 `LevelViewport_SourceControlMenu` 定义成一个普通 surface enum。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:958` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:967` — 注册时只是把它接到 `GetAllLevelEditorToolbarSourceControlMenuExtenders()`，没有任何 source-control policy/service。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:527` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:534` — `ShowCreateBlueprintPopup()` 的 package 路径只在末尾调用 `PromptForCheckoutAndSave()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1205` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1206` — 生成 bind module 时直接把 `.Build.cs` 和 header 写到磁盘。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1430` — 单个 `Bind_*.cpp` 直接写盘。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1436` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1440` — 旧生成文件直接 `IFileManager::Delete()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1471` — 模块主 `.cpp` 再次直接写盘。 |
| 优点 | 依赖最少，当前 debug/legacy 工具不需要强绑 `SourceControl` 模块；在无源控环境里也能直接工作。 |
| 不足 | 一旦这些 editor 工具进入团队仓库、只读工作区或需要 checkout 的环境，写盘行为就会变成“每条工具链各自猜怎么写”。推断：这会让未来 `EditorSubsystem` 扩展者无法稳定复用 editor 写盘能力，也会让 `SourceControlMenu` 这个 surface 看起来像“有 source control 集成”，实际上只是又一个菜单挂点。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `PuertsEditor.Build.cs` 只有在 `JsEnv.WithSourceControl` 时才引入 `SourceControl` 模块；真正的文件写盘统一经过 `UFileSystemOperation::WriteFile()`，写前先 `CheckoutSourceControlFile()`；helper 再通过 `QueryFileState()` 决定是否需要 checkout。包资产保存仍走 `PromptForCheckoutAndSave()`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:40` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:43`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/FileSystemOperation.cpp:41` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/FileSystemOperation.cpp:46`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/FileSystemOperation.h:54` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/FileSystemOperation.h:59`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/FileSystemOperation.cpp:128` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/FileSystemOperation.cpp:136`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1393` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1395` | 把“文件写盘”和“包保存”分别抽象成统一策略，比在每个工具函数里直接 `SaveStringToFile()` 更适合作为 editor 扩展底座。 |
| UnLua | 模块启动时统一注册 `PreSavePackageWithContextEvent` / `PackageSavedWithContextEvent`，把保存生命周期变成模块级事件，而不是分散在各个工具入口里。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:62` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:69`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:78` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:84` | 即便不直接绑定源控实现，也可以先把“保存/写盘发生在哪里”收束成正式 lifecycle，再决定是否接 source control。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增统一的 editor file/save policy service，把 package save 与 plain-text file write 都纳入同一条 contract；source control 作为可选适配层按需启用。 |
| 具体步骤 | 1. 新增 `UAngelscriptEditorFilePolicyService` 或等价 native service，最少定义 `WriteTextFile(Path, Content, Policy)`、`DeleteFile(Path, Policy)`、`SavePackages(Packages, Policy)`、`EnsureWritable(Path)` 四个入口；第一阶段内部仍可直接回落到现有 `FFileHelper` / `FEditorFileUtils`。<br>2. 把 `GenerateNewModule()`、`GenerateSourceFilesV2()`、legacy bind 生成、测试输出文件等全部改走 file policy service；旧静态 helper 只做 forwarding，避免继续散落新的直接写盘点。<br>3. 把 source control 适配做成可选层：检测 `SourceControl` 模块是否可用，若可用则在 `WriteTextFile/DeleteFile` 前执行 checkout 或 writable 检查；不可用时保持当前直接写盘行为。<br>4. 为 package save 增加统一 policy 枚举，例如 `PromptUser`、`SilentCheckoutIfPossible`、`NoSave`，让创建资产、未来 wizard、脚本扩展和自动化工具共享同一套保存语义。<br>5. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 暴露只读 getter 或高层 wrapper，让项目方扩展能显式请求“生成文件并遵守 editor save policy”，而不是直接 include 模块私有 helper。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/IO/AngelscriptEditorFilePolicyService.h` 与对应 `Private/IO/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果一开始就强制依赖 `SourceControl` 模块，会把当前无源控环境也绑进复杂依赖；适配层必须保持 optional，且默认行为回落到现有实现。 |
| 兼容性 | 向后兼容。无源控环境下行为应与今天一致；有源控环境时只是让写盘更稳，不改变现有脚本、菜单入口或生成文件格式。 |
| 验证方式 | 1. 在无源控环境下运行 legacy bind/module 生成，确认输出与当前一致。<br>2. 在启用 `SourceControl` 的环境下运行同一流程，确认只读文件会先 checkout 或至少被显式标记为无法写入。<br>3. 复测 `ShowCreateBlueprintPopup()`，确认 package save 仍能走统一 prompt/save 语义，没有引入额外 modal 回归。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-63 | editor 写盘与保存路径缺少统一的 source-control / writable policy | service seam 新增 | 高 |
| P2 | Arch-62 | editor transaction 只有壳，没有稳定 undo participant contract | UE framework 集成深化 | 中高 |

---

## 架构分析 (2026-04-09 23:55:02)

### Arch-64：`AngelscriptData` 的 bootstrap 仍是一次性边沿触发，模块后加载/重载后没有自愈路径

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor bootstrap 与 `ContentBrowserDataSource` 激活时序，是否支持模块后加载、动态重载与对称卸载 |
| 当前设计 | `FAngelscriptEditorModule` 在 `StartupModule()` 中只向 `FCoreDelegates::OnPostEngineInit` 追加一次静态回调；真正的数据源创建与激活全部发生在 `OnEngineInitDone()`。模块本身不保存 data source 成员，也没有在 `ShutdownModule()` 中执行 `DeactivateDataSource()` 或移除 `OnPostEngineInit` 句柄。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `StartupModule()` 里只做 `FCoreDelegates::OnPostEngineInit.AddStatic(&OnEngineInitDone)`，没有“引擎已就绪时立即 bootstrap”的分支。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:118` — `OnEngineInitDone()` 直接 `NewObject<UAngelscriptContentBrowserDataSource>(..., RF_MarkAsRootSet | RF_Transient)`、`Initialize()` 并 `ActivateDataSource("AngelscriptData")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:57` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:60` — 模块私有状态只有 `StateDumpExtensionHandle`，没有 `ContentBrowserDataSource` 或 `OnPostEngineInit` 成员。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:676` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:689` — `ShutdownModule()` 只处理 pre-save、state dump 与 `ToolMenus`，没有 data source 反注册。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:5` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:8` — `Initialize()` 只有 `Super::Initialize(true)`，数据源自身也没有额外的 bootstrap/shutdown 逻辑。 |
| 优点 | 早期实现成本低，编辑器正常冷启动时可以很快把 `/All/Angelscript` 接入 `ContentBrowser`。 |
| 不足 | 推断：当前 bootstrap 是“边沿触发”而不是“可重入服务”。只要模块在 `OnPostEngineInit` 之后才启动，或 editor 中途动态重载该模块，就没有第二条路径去重新创建并激活 `AngelscriptData`。同时，匿名 `RF_MarkAsRootSet` 对象缺少模块级所有权，也让卸载路径无法自证完整。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 模块显式持有 `TStrongObjectPtr<UDynamicDataSource>`；`StartupModule()` 中立即 `Initialize()`，首帧 `Tick()` 再调用 `ActivateDataSource("DynamicData")`，`ShutdownModule()` 时 `Reset()` 并移除 ticker。`UDynamicDataSource` 本身还实现了对称的 `Initialize/Shutdown`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:104` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:113`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:164` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:180`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:107` | 把 data source 激活从“一次性 engine event”改成“模块拥有的可重入 feature + 可回收状态”，可以显著降低模块重载时序风险。 |
| puerts | `StartupModule()` 结束前直接调用 `this->OnPostEngineInit()`，而不是只等待外部广播；`OnPostEngineInit()` 再显式创建 `SourceFileWatcher`、注册 compiler 和 `JsEnv`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:108`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:140` | bootstrap 逻辑由模块主动拉起，而不是完全依赖一个可能已经错过的全局事件。 |
| UnLua | 模块在 `StartupModule()` 中先构造 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个成员对象，并保存 `OnPostEngineInit` 订阅；`ShutdownModule()` 则对称移除 delegate。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:72` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:85` | 即使仍使用 `OnPostEngineInit`，也把 bootstrap state 保存在模块成员里，而不是匿名静态路径。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptEditor` 的 bootstrap 改成“模块拥有、可重入、可对称关闭”的显式 feature，而不是单次 `OnPostEngineInit` 回调。 |
| 具体步骤 | 1. 在 `FAngelscriptEditorModule` 中新增 `FDelegateHandle OnPostEngineInitHandle`、`TStrongObjectPtr<UAngelscriptContentBrowserDataSource> AngelscriptDataSource` 与 `bool bEditorBootstrapComplete`；去掉匿名 `RF_MarkAsRootSet` 创建方式。<br>2. 抽出 `EnsureEditorBootstrap()` / `ShutdownEditorBootstrap()`：`Ensure` 负责检查 `GEditor`、`IContentBrowserDataModule` 是否可用，并幂等地 `Initialize + ActivateDataSource`；`Shutdown` 负责 `DeactivateDataSource`、`Reset()` 与句柄清理。<br>3. `StartupModule()` 中优先判断 editor 是否已经可用；若可用就立即执行 `EnsureEditorBootstrap()`，否则再注册 `OnPostEngineInit` 作为 fallback。`OnEngineInitDone()` 仅转调 `EnsureEditorBootstrap()`，避免逻辑分叉。<br>4. 给 `UAngelscriptContentBrowserDataSource` 增加显式 `Shutdown()` 或等价清理点，为后续 `AddNew` 菜单、虚拟树缓存和增量刷新留下对称生命周期。<br>5. 增加最小 bootstrap smoke test：覆盖“正常冷启动”和“editor 已启动后模块重载”两种路径，验证 data source 只会被激活一次。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`。 |
| 预估工作量 | S-M |
| 架构风险 | `ContentBrowserDataSubsystem` 的可用时机与 `GEditor` 初始化顺序需要重新验证；如果 `EnsureEditorBootstrap()` 设计得不够幂等，反而可能引入双重激活。 |
| 兼容性 | 向后兼容。外部脚本、虚拟路径名和现有 `ContentBrowser` 展示不必变化；改动只收敛 bootstrap/teardown 行为。 |
| 验证方式 | 1. 在正常 editor 冷启动下确认 `/All/Angelscript` 仍然出现且只出现一次。<br>2. 在 editor 已启动后触发模块重载，确认 data source 会重新激活，不会因为错过 `OnPostEngineInit` 而消失。<br>3. 关闭模块或退出 editor 时确认没有残留 data source、重复注册 warning 或 root-set 泄漏迹象。 |

### Arch-65：`UScriptEditorSubsystem` 当前只有“取实例”语法糖，没有 discovery/rebind/reload 协调层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户能否把 `UScriptEditorSubsystem` 当成正式 editor 扩展宿主，而不是只能把它当作一个被动可查询对象 |
| 当前设计 | editor 侧对 `UEditorSubsystem` 的支持目前主要停留在 accessor 生成与查询 helper：脚本可以 `Get()` 到 subsystem，但 `AngelscriptEditor` 本身没有任何 subsystem discovery、bootstrap、reload rebind 或 resource-handle 回收链。相对地，菜单扩展有专门的注册/卸载路径。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:84` — `UScriptEditorSubsystem` 只有 `BP_ShouldCreateSubsystem`、`BP_Initialize`、`BP_Deinitialize`、`BP_Tick` 四类壳层，没有任何 editor feature 注册句柄、reload hook 或 bootstrap API。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:20` — 对外只有 `GetEditorSubsystem(TSubclassOf<UEditorSubsystem>)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp:24` 到 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp:55` — runtime 侧对 editor subsystem 的专门支持只是把 `Get()` 绑定到 `GEditor->GetEditorSubsystemBase(SubsystemClass)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1246` 到 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1257` — 脚本类继承 `UEditorSubsystem` 时，预处理器也只额外生成 `EditorSubsystem::GetEditorSubsystem(...)` 的静态 `Get()` 包装。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:363` — 当前 editor 模块唯一显式初始化的脚本扩展入口是 `UScriptEditorMenuExtension::InitializeExtensions()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:44` — `UScriptEditorMenuExtension` 会监听 `FAngelscriptClassGenerator::OnPostReload` 与 `OnEnginePreExit`，在 full reload 后执行 `UnregisterExtensions()/RegisterExtensions()`。 |
| 优点 | 当前实现非常薄，脚本可以用统一方式访问 editor subsystem 实例，编译期和运行期胶水都相对简单。 |
| 不足 | 推断：`UScriptEditorSubsystem` 现在更像“可查询对象类型”，还不是“由插件保证发现、重绑和资源回收”的 editor host。与 `UScriptEditorMenuExtension` 相比，它没有任何 full reload 后的显式重装配路径；项目方若尝试把 watcher、tool menu、content browser 或其它句柄直接挂在 subsystem 上，插件也没有定义这些句柄何时需要重新绑定。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 不把 editor 扩展责任压给一个泛化 `EditorSubsystem`，而是由模块显式持有 `FEditorListener`、`FUnrealCSharpPlayToolBar`、`FUnrealCSharpBlueprintToolBar`、`UDynamicDataSource`，每个对象都有自己的初始化与反初始化。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:10` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:56`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:107`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:127` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:171` | 先建立 capability-specific object 的生命周期树，再决定是否向项目暴露 facade，比直接假设 subsystem 会自己处理 reload 更稳。 |
| UnLua | 模块在启动时构造 toolbar 对象、启动目录监听，并在 `OnPostEngineInit()` 中统一初始化；`ShutdownModule()` 对称移除 settings/save delegate。也就是说，editor feature 的发现与回收都是模块控制的。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:69`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:72` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:85` | 即使对外暴露脚本 API，editor feature 的真实 bootstrap 仍由模块拥有，不把生命周期责任留给调用方猜测。 |
| puerts | `StartupModule()` 中立即调用 `OnPostEngineInit()`；后者显式注册 `UTypeScriptBlueprint` compiler，并创建 `SourceFileWatcher` 与 `JsEnv`。editor 能力是模块直接拥有的对象，而不是仅依赖 `Get()` 到某个 subsystem。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:108`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:140` | 对编辑器集成来说，“如何发现并重绑 feature”通常比“如何拿到对象实例”更关键。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 `UScriptEditorSubsystem` 的 `Get()` 访问方式，但新增一个 native 协调层，明确 subsystem 的发现、重绑与资源回收时机。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorSubsystemCoordinator` 或等价 native service，由 `FAngelscriptEditorModule` 持有；第一阶段只负责 subsystem discovery 与 lifecycle broadcast，不直接承担业务功能。<br>2. 协调层在 `StartupModule()` 中监听 `FCoreDelegates::OnPostEngineInit`、`FAngelscriptClassGenerator::OnPostReload` 和 `OnEnginePreExit`；当 editor 与脚本类都就绪后，枚举 concrete `UASClass` 子类中的 `UScriptEditorSubsystem`，并通过 `GEditor->GetEditorSubsystemBase(Class)` 或等价 API 触发实例解析。<br>3. 为 `UScriptEditorSubsystem` 追加向后兼容的 editor-specific hook，例如 `BP_PostEditorBootstrap`、`BP_PreScriptReload`、`BP_PostScriptReload` 或 native delegate；让 subsystem 有机会释放/重绑 menu、watcher、provider 等句柄。<br>4. 把未来开放给项目方的 editor capability 注册，统一收口到 coordinator 返回的 handle 上；现有生成的 `Get()` 与 `UEditorSubsystemLibrary::GetEditorSubsystem()` 继续保留，只作为读取入口，不再承担隐式 bootstrap 语义。<br>5. 首轮验证只迁移一类低风险能力，例如 menu/notification 或只读 diagnostics provider；等 rebind 语义稳定后，再考虑 watcher、content browser 或更重的 editor resource。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Subsystems/AngelscriptEditorSubsystemCoordinator.h` 与对应 `Private/Subsystems/*`。 |
| 预估工作量 | M |
| 架构风险 | 如果协调层过早实例化脚本 subsystem，可能与初始编译或 `UASClass` 版本切换时序冲突；需要先明确定义“何时允许实例解析”和“full reload 时旧实例如何退场”。 |
| 兼容性 | 向后兼容。现有 `MyEditorSubsystem.Get()`、`UEditorSubsystemLibrary::GetEditorSubsystem()` 以及已有脚本子类都可以保留；新增 hook 与 coordinator 只是补足生命周期定义。 |
| 验证方式 | 1. 新建一个最小 `UScriptEditorSubsystem` 子类，确认 editor 启动后能稳定解析实例。<br>2. 触发一次 full script reload，确认协调层会触发 reload hook，且不会留下重复注册的 menu/watcher 句柄。<br>3. 关闭 editor 或卸载模块时确认 subsystem 相关资源被成组释放，没有残留 warning 或 stale instance 行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-65 | `UScriptEditorSubsystem` 只有 accessor，没有 discovery/rebind/reload 协调层 | 生命周期与扩展宿主补强 | 高 |
| P2 | Arch-64 | `AngelscriptData` bootstrap 依赖一次性 `OnPostEngineInit`，缺少后加载/重载自愈 | bootstrap 时序收敛 | 中高 |

---

## 架构分析 (2026-04-10 00:06:33)

### Arch-66：`UASClass` 元数据已经在 runtime 端成形，但派生 `Blueprint` 缺少 editor-side 的语义同步 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | script class 元数据/flags 如何进入派生 `Blueprint` 的 editor 语义，而不是只停留在 `UASClass` 本体 |
| 当前设计 | `FAngelscriptClassGenerator` 在生成 `UASClass` 时已经写入 `BlueprintType`、`IsBlueprintBase`、`DisplayName` 与自定义 meta；但 editor 创建派生 `Blueprint` 时只是取 `BlueprintClass`/`GeneratedClass` 并 `CreateBlueprint()`，随后直接保存和打开。reload 期间也只做节点重构、编译、`PropertyEditor` 刷新和 `PlacementMode` 刷新，没有“把脚本类元数据再投影到已有 `UBlueprint`”的独立阶段。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3330` 到 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3356` — 新生成的 `UASClass` 会继承父类 meta，并显式写入 `BlueprintType`、`IsBlueprintBase`、`DisplayName` 与 `ClassDesc->Meta`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:507` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:517` — editor 创建路径只调用 `IKismetCompilerInterface::GetBlueprintTypesForClass(...)` 和 `FKismetEditorUtilities::CreateBlueprint(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:521` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:537` — 创建后仅 `AssetCreated`、`MarkPackageDirty`、`PromptForCheckoutAndSave`、`OpenEditorForAsset`，没有额外的 metadata apply/sync。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:291` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:338` — reload 路径只对受影响 `Blueprint` 做节点刷新、编译和 `NotifyCustomizationModuleChanged()`，没有独立的 blueprint semantic sync 步骤。 |
| 优点 | 当前 authoring 路径非常薄，复用 UE 原生 `CreateBlueprint()` 成本低；reload 也尽量只碰节点与编译链，短期回归面小。 |
| 不足 | 这里缺的不是“多一个 toolbar 按钮”，而是 `UASClass -> UBlueprint` 的正式语义投影层。推断：脚本类的 `DisplayName`、`Tooltip`、`Category`、`HideCategories`、`Abstract/Deprecated/Const` 等 editor-facing 语义一旦在脚本端变化，现有派生 `Blueprint` 没有稳定机制去收敛 drift，只能依赖创建瞬间的默认行为或人工修正。项目方也无法通过 `EditorSubsystem` 正式补一个 post-create/post-compile augmentor。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | editor 模块先为 `UTypeScriptBlueprint` 注册专用 `FKismetCompilerContext`；随后 `UPEClassMetaData::Apply()` 在 editor lane 中把 class flags、display name、description、category、`HideCategories` 等同步回 `UBlueprint`。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintMetaData.cpp:109` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintMetaData.cpp:115`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintMetaData.cpp:334` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintMetaData.cpp:374`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:185` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:191` | 先把语言类语义沉入 compiler/metadata lane，再让 asset workflow 消费它，能避免 script class 和派生 `Blueprint` 逐步漂移。 |
| UnrealCSharp | `UDynamicBlueprintExtension::HandleGenerateFunctionGraphs()` 直接进入 `FKismetCompilerContext`，而 `FUnrealCSharpBlueprintToolBar` 又通过 `FAssetEditorExtender` 和 `OnEndGenerator` 把当前 `Blueprint` surface 接回生成完成事件。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/DynamicBlueprintExtension.cpp:7` 到 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/DynamicBlueprintExtension.cpp:10`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:39`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:202` | compiler lane 和当前 asset editor surface 同时存在时，语义同步可以既准确落到 `Blueprint`，又能给当前编辑器即时反馈。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `UASClass` 派生 `Blueprint` 增加一层正式的 `semantic sync` 服务，先补“创建后/重载后元数据回写”，再视需要接入更轻量的 compile hook。 |
| 具体步骤 | 1. 新增 `FAngelscriptBlueprintSemanticSyncService`（或等价 feature），第一阶段只定义 `ApplyScriptClassMetadataToBlueprint(UASClass*, UBlueprint*)`，把现有 `UASClass` 上已经成形的 meta/flags 作为唯一 source of truth。<br>2. 先在 `ShowCreateBlueprintPopup()` 中 `CreateBlueprint()` 之后立即调用这层 service，同步最直接的 editor 语义：`DisplayName`、`Tooltip/Description`、`Category`、`HideCategories`、`Abstract/Deprecated/Const`、`IsBlueprintBase` 等；只有发生差异时才标脏并保存。<br>3. 再给 service 增加 reload adapter：在 `FAngelscriptClassGenerator::OnPostReload` 或 `ClassReloadHelper` 的受影响 `Blueprint` 集合上调用同一个 sync 入口，使“脚本类改 meta”与“脚本类改节点签名”共享同一条 editor 修复链。<br>4. 第二阶段视风险决定是否接 `UBlueprintCompilerExtension` 或受限 compile delegate，但范围只限“父类为 `UASClass`”的 `Blueprint`；不要一开始接管所有蓝图编译。<br>5. 通过 `UScriptEditorSubsystem` 或新的 blueprint integration facade 暴露只读 getter/augmentor 注册，例如 `RegisterBlueprintSemanticAugmentor(...)`，让项目方可以追加自己关心的 category、tooltip 或校验策略，而不必修改模块主文件。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintIntegration/AngelscriptBlueprintSemanticSyncService.h` 与对应 `Private/BlueprintIntegration/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就把节点修复、metadata 回写和 compile extension 全部叠在一起，容易把现有 reload 行为变成大改；更稳妥的是先做 post-create/post-reload 的幂等同步，再评估 compile lane。 |
| 兼容性 | 向后兼容。现有 `CreateBlueprint()` 工作流、已有脚本类和已有派生 `Blueprint` 都可保留；第一阶段的外部可见变化主要是 editor metadata 被更稳定地同步，个别旧资产可能在首次命中时被自动标脏并建议保存。 |
| 验证方式 | 1. 用带 `DisplayName`、`Tooltip`、`NotBlueprintable/Blueprintable`、`Deprecated` 等 specifier 的 `UASClass` 创建派生 `Blueprint`，确认新资产的 editor 语义与脚本类一致。<br>2. 修改同一个脚本类的 meta 后执行一次 script reload，确认已有派生 `Blueprint` 的名称/描述/分类等同步更新，而不是只重构节点。<br>3. 回归普通非 `UASClass` 蓝图，确认不会进入这条同步链。 |

### Arch-67：脚本派生资产的作者化入口仍要求“外部先给一个 `UASClass*`”，缺少正式的 class discovery/context contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户或项目扩展如何在 editor 内发现可派生的脚本类，并把它们稳定接到创建工作流 |
| 当前设计 | `AngelscriptEditor` 的创建入口本身并不负责“找类”，而只负责“拿到类以后创建资产”：runtime debug server 查到 `UASClass` 后直接广播 `GetEditorCreateBlueprint()`，editor 模块则把它转发到 `ShowCreateBlueprintPopup(UASClass* Class)`。资产列表 popup 的 “Create New” 也是把已有 `BaseClass` 原样传回同一个函数。换句话说，当前 authoring contract 假设调用者已经替 editor 选好了类。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1177` 到 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1181` — debug server 先按类名查 `ClassDesc`，再直接 `Broadcast(Cast<UASClass>(ClassDesc->Class))`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:405` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:408` — editor 模块把该广播直接绑定到 `ShowCreateBlueprintPopup(ScriptClass)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:445` — `ShowCreateBlueprintPopup(UASClass* Class)` 的第一步就是基于传入类推导标题、默认名字和默认目录，没有 class picker/catalog 步骤。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:625` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:639` — 多资产 popup 的 “Create New Blueprint/Data Asset” 仍只是回调 `ShowCreateBlueprintPopup(BaseClass)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:19` — 项目侧公开 API 仍只有 `GetEditorSubsystem()`，没有“枚举可派生脚本类/按来源筛类/注册过滤器”一类 authoring contract。 |
| 优点 | 创建执行路径简单直接；只要上游已经解析出 `UASClass*`，就能复用同一套资产创建逻辑。 |
| 不足 | 这里的问题不是“少一个弹窗”，而是 authoring context 没有被建模。推断：当前既不像 UnLua 那样依附当前 `Blueprint` 上下文，也不像 UnrealCSharp 那样提供持久 class catalog；因此项目方若要做 `Add New`、搜索、按模块/插件分组、只显示 `Blueprintable` 类、按项目规则过滤 deprecated 类，都只能自己再造一层类发现逻辑，而不是复用插件已有 contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 通过 `FClassCollector` 长期维护动态类层级，并监听生成结束、reload、`BlueprintCompiled`、`AssetRegistry` 事件；`SDynamicClassViewer` 则直接把这个 collector 投影成带搜索框的选择器，`FDynamicNewClassUtils::GetProjectContent()` 进一步建模项目内容根。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/ClassCollector.cpp:31` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/ClassCollector.cpp:64`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicClassViewer.cpp:17` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicClassViewer.cpp:31`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:50` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:65` | 先把“有哪些可作者化类”建成正式 catalog，再让窗口、搜索、创建路径都共享它，扩展点自然会稳定。 |
| UnLua | 不做全局 class picker，而是通过 `FAssetEditorExtender` 从当前 `Blueprint` editor 的 `ContextSensitiveObjects` 直接拿到 `UBlueprint` 上下文，再决定注入哪些动作。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:30` | 另一种可借鉴路径是把入口显式绑定到当前 asset/editor context，而不是把 class 发现责任丢给匿名上游调用者。 |
| puerts | `UPEBlueprintAsset::LoadOrCreate(...)` 把 `Name/Path/ParentClass` 定义成显式输入，并在 workflow object 内处理 `PIE` 检查、真实 package 创建和 `AssetRegistry` 通知。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:123`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:144` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:160` | 一旦 class/path context 明确，后续 workflow 就应该落到稳定对象方法里，而不是继续靠 popup 和外部广播传来传去。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增正式的 `script class catalog` 服务，把“发现可派生脚本类”和“执行资产创建”拆成两层；旧 `UASClass*` 直达路径保留为兼容 adapter。 |
| 具体步骤 | 1. 新增 `FAngelscriptScriptClassCatalogService`（或 `UAngelscriptScriptClassCatalogService`），第一阶段只维护只读 descriptor：`ClassPath`、`DisplayName`、`RelativeSourceFilePath`、`bIsDataAsset`、`bIsBlueprintBase`、`bDeprecated`、来源模块/插件信息。描述符数据优先复用现有 `UASClass` meta，而不是重新解析脚本文本。<br>2. 让 catalog service 监听 `FAngelscriptClassGenerator` 的 compile/reload 完成点，幂等刷新可派生类列表；避免每个 UI surface 自己 `TObjectIterator<UASClass>` 或重复写过滤逻辑。<br>3. 新增轻量 `SAngelscriptScriptClassPicker` 或等价 picker widget，第一阶段只提供搜索、分组和选择回调；`ShowCreateBlueprintPopup()` 退化为“拿到 descriptor 后执行创建”的第二段流程。<br>4. 把 `ContentBrowser Add New`、未来 toolbar/command、debug server bridge 都改成先请求 catalog service：有当前 asset/editor context 时走 context-bound lane，没有时再弹 class picker；不要再默认要求外部必须自己准备 `UASClass*`。<br>5. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 暴露 `EnumerateScriptBlueprintBases()`、`FindScriptBlueprintBase()`、`RegisterScriptClassCatalogFilter(...)` 等只读/过滤扩展点，让项目方可以增加自定义分组和屏蔽规则，而不必修改 `ShowCreateBlueprintPopup()`。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、必要时修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` 的 adapter 路径；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Catalog/AngelscriptScriptClassCatalogService.h`、`Private/Catalog/*.cpp`、`Private/Widgets/SAngelscriptScriptClassPicker.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就同时改 `ContentBrowser Add New`、debug server 和自定义 wizard，容易把创建入口铺得过宽；更稳妥的顺序是先建 catalog service，再让不同入口渐进改调它。 |
| 兼容性 | 向后兼容。现有 `GetEditorCreateBlueprint()` 广播、`ShowCreateBlueprintPopup(UASClass*)` 和多资产 popup 的 `BaseClass` 入口都可以继续存在，只是内部逐步转发到 catalog/workflow service。 |
| 验证方式 | 1. 让 catalog service 枚举一组 `Blueprintable`/`NotBlueprintable`/deprecated 的脚本类，确认过滤结果与 `UASClass` 元数据一致。<br>2. 从新的 class picker 选择脚本类创建资产，确认最终仍走现有创建逻辑且默认目录推导不回归。<br>3. 让一个项目级 `UScriptEditorSubsystem` 注册额外过滤器或分组规则，确认无需修改核心模块即可影响 picker 展示。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-66 | `UASClass` 元数据未进入派生 `Blueprint` 的正式 sync/compile lane | 语义同步层新增 | 高 |
| P1 | Arch-67 | 脚本派生资产创建缺少正式的 class discovery/context contract | workflow entry point 补强 | 中高 |

---

## 架构分析 (2026-04-10 00:16:53)

### Arch-68：`AngelscriptEditor` 仍以单模块承载全部 editor host，`Build.cs` 还没有形成可演进的 host-adapter 依赖边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 模块的依赖拓扑是否支持后续继续接入更多 UE editor host，而不把全部能力继续堆进同一个 module/main cpp |
| 当前设计 | `AngelscriptEditor` 目前仍是一个“单模块吃所有 editor host”的结构：`Build.cs` 既把一批 editor 依赖公开到 `PublicDependencyModuleNames`，又在同一个 `StartupModule()` 中同时装配 reload、source navigation、directory watch、settings、runtime bridge、literal asset pre-save 和 `ToolMenus`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12` 到 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:39` — `UnrealEd`、`EditorSubsystem`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools` 被直接放进 `PublicDependencyModuleNames`，而 `LevelEditor`、`PropertyEditor`、`ContentBrowser`、`ContentBrowserData`、`ToolMenus` 等也集中在同一个模块的 private 依赖里。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `StartupModule()` 单点串起 `FClassReloadHelper`、source navigation、`GameplayTags`、`OnPostEngineInit`、`UScriptEditorMenuExtension`、directory watcher、settings、runtime delegate bridge、`OnObjectPreSave` 和 `UToolMenus` startup callback。 |
| 优点 | 当前结构对单仓库内快速加功能很直接，feature 不需要跨多个 module 协调即可落地。 |
| 不足 | 这已经不是“一个文件有点长”的问题，而是 editor host 边界没有被建模。继续往里加 `MainFrame`、`BlueprintEditor`、`ClassViewer`、`MessageLog` 或更多 authoring/toolchain surface 时，最自然的落点仍会是同一个 module 和同一批 transitive dependency。推断：这会让 `EditorSubsystem` 很难只消费窄而稳的 host facade，也会让外部 C++ 扩展更容易意外依赖 `AngelscriptEditor` 暴露出来的传递依赖。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把 `MainFrame`、`AnimationBlueprintEditor` 放在 private include/module 与 dynamic load 侧；运行时 editor 能力再由 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个长期对象承接。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:37` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:57`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:87` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:92`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:58` | host-specific 依赖尽量留在 private/dynamic 一侧，再把真正的 UI/host adapter 变成长期对象，而不是把所有 editor 依赖都公开给外部。 |
| puerts | 直接把 editor 集成拆成两个 module：`PuertsEditor` 负责 compiler/watcher/PIE/editor runtime，`DeclarationGenerator` 独立承载 `ToolMenus + command + console` 的声明生成工具链。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:28` 到 `Reference/puerts/unreal/Puerts/Puerts.uplugin:47`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:36`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:39`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1687` | 当 editor 侧已经同时存在“语言运行时集成”和“工具链产物生成”两类能力时，module 级拆分本身就是架构 contract，而不只是实现细节。 |
| UnrealCSharp | `UnrealCSharpEditor` 保持 public 依赖较薄，真正重的 editor host 依赖放在 private；模块启动后显式实例化 `PlayToolBar`、`BlueprintToolBar`、`DynamicDataSource` 等 feature owner。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:31`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:58`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:49` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:104` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:108` | 即使仍保持单个 editor module，依赖也可以先按“外部暴露面”和“内部 host 能力”分层，降低后续扩展面被实现细节拖着走的概率。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `AngelscriptEditor` 做成“薄 module + host adapter/toolchain service”的内部结构，再视需要决定是否追加新 module；第一阶段优先收紧依赖边界，而不是先做大拆分。 |
| 具体步骤 | 1. 审核 `AngelscriptEditor` public header 的真实需求，把不需要出现在 public surface 的依赖从 `PublicDependencyModuleNames` 挪到 `PrivateDependencyModuleNames`，并为相应 public header 增补 forward declaration；优先目标是 `BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/` 下新增 `HostAdapters/` 与 `Toolchain/` 分层，先落四个 native owner：`FLevelEditorAdapter`、`FContentBrowserAdapter`、`FBlueprintAuthoringAdapter`、`FAngelscriptEditorToolchainService`；`FAngelscriptEditorModule` 只负责创建、销毁和注册这些对象。<br>3. 把 `StartupModule()` 中当前直接装配的 watcher、source navigation、runtime bridge、menu/tool entry 和 state dump extension 逐步迁到对应 adapter/service 内，模块主文件只保留 phase 协调与 owner 生命周期。<br>4. 当 `BlueprintImpact`、legacy bind generator、future codegen/export 等能力都已经通过 `ToolchainService` 收口后，再评估是否像 puerts 那样补一个独立 `AngelscriptEditorToolchain` editor module；在此之前先避免 module 名称和加载相位的大改。<br>5. 给 `UScriptEditorSubsystem`/`UEditorSubsystemLibrary` 暴露的是 typed facade，例如 `GetContentBrowserAdapter()`、`GetEditorToolchainService()` 或统一 registry，而不是让项目侧继续依赖 `AngelscriptEditorModule.cpp` 的具体实现位置。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/HostAdapters/*.h/*.cpp` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Toolchain/*.h/*.cpp`。 |
| 预估工作量 | M-L |
| 架构风险 | 收紧 `PublicDependencyModuleNames` 会暴露外部模块对传递依赖的隐式使用；如果一次性移动过多依赖，可能先打断现有本地扩展编译。更稳妥的做法是先加 facade/forward declaration，再分批收紧依赖。 |
| 兼容性 | 对脚本用户和大多数 editor 使用者可保持向后兼容。唯一需要明确标注的兼容性影响是：若有项目内 C++ 模块错误地依赖了 `AngelscriptEditor` 的传递 editor 依赖，收紧后需要在它自己的 `Build.cs` 中显式补依赖。 |
| 验证方式 | 1. 运行一次 editor module 编译，确认 public 头在依赖收紧后仍可被现有包含者正常编译。<br>2. 启动编辑器，回归 `ContentBrowser`、脚本菜单扩展、source navigation、watcher 与 `BlueprintImpact` 入口，确认 adapter/service 化后功能不丢。<br>3. 新增一个最小项目级扩展模块，只通过 `EditorSubsystemLibrary` 访问新 facade，确认无需额外 include `AngelscriptEditorModule.h` 就能接入 editor 能力。 |

### Arch-69：插件描述层仍把 editor/runtime/test 压成同一组 `PostDefault` 模块，尚未利用 `LoadingPhase` 和多 editor module 做能力分层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件描述层是否已经把“核心 editor 集成”“延后 host 接入”“工具链/测试”分成不同 module 与加载相位，以降低启动耦合和未来扩展成本 |
| 当前设计 | `Angelscript.uplugin` 当前把 `AngelscriptRuntime`、`AngelscriptEditor` 和 `AngelscriptTest` 三个模块全部固定为 `PostDefault`。与此同时，`AngelscriptEditor` 一旦加载就立即注册大批 editor 钩子，说明插件描述层还没有把“早期必须能力”和“晚一点再接的 host/tool/test 能力”拆开。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18` 到 `Plugins/Angelscript/Angelscript.uplugin:32` — runtime、editor、test 三个模块全部使用 `LoadingPhase = PostDefault`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `AngelscriptEditor` 加载后马上接入 reload helper、source navigation、watcher、settings、runtime bridge、literal asset pre-save 与 `ToolMenus`。 |
| 优点 | 当前 descriptor 简单直观，插件部署和本地调试时不需要维护复杂的 module phase 矩阵。 |
| 不足 | 问题在于 descriptor 层没有表达 feature 分层。推断：只要 editor 内继续增加 `ContentBrowser` authoring、toolchain 产物导出、workspace intelligence、diagnostics panel 或更重的测试/开发者功能，它们默认都会继续挤进同一个 `PostDefault` editor module，而不是由 descriptor 指挥“谁该早载、谁可晚载、谁应独立禁用”。这会让启动顺序、功能裁剪和未来交付边界都越来越难管理。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | descriptor 明确把 `UnLua` runtime 放在 `PreDefault`，`UnLuaEditor` 放在 `Default`，额外的 `UnLuaDefaultParamCollector` 则是 `Program + PostConfigInit`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23` 到 `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:39` | 即使总体模块数不多，也可以先把 runtime、editor 和离线工具的加载责任分开表达。 |
| puerts | descriptor 层分得更细：`DeclarationGenerator` 是 `Editor + Default`，`PuertsEditor` 是 `Editor + PostEngineInit`，运行时相关模块还有 `PreDefault` 与 `PostEngineInit` 的分工。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15` 到 `Reference/puerts/unreal/Puerts/Puerts.uplugin:47` | 当 editor 内同时存在“工具链生成”和“语言桥接运行时”两类能力时，`LoadingPhase` 本身就是架构工具，可避免所有 feature 抢同一个启动窗口。 |
| UnrealCSharp | descriptor 直接把 `UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler` 三个 editor module 并列声明，另外再给 `SourceCodeGenerator` 一个 `Program + PostConfigInit` 入口。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18` 到 `Reference/UnrealCSharp/UnrealCSharp.uplugin:53` | editor 内部即使都属于“开发期能力”，也可以在 plugin descriptor 里先区分“集成层”“生成层”“编译层”“离线程序层”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 不直接打散现有三模块主干，而是先在 plugin descriptor 层补出“核心 editor 集成 vs 晚加载 host/toolchain”分层；等内部 service seam 稳定后，再决定是否迁移现有功能。 |
| 具体步骤 | 1. 保持 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 现有模块名暂时不变，先在 `Documents/Guides/Build.md` 与 `Documents/Guides/Test.md` 记录一版目标 phase 模型：哪些能力必须随 `AngelscriptEditor` 早载，哪些应进入晚加载 adapter/toolchain。<br>2. 第一阶段新增一个轻量 editor 子模块，例如 `AngelscriptEditorLate` 或 `AngelscriptEditorToolchain`，`LoadingPhase` 设为 `Default` 或 `PostEngineInit`；先迁移最不应挤进主 startup 的能力，如 `BlueprintImpact` UI、legacy generator/toolchain、future workspace intelligence panel。<br>3. 让 `AngelscriptEditor` 主模块收敛为 editor core：settings、subsystem facade、基础 commands/registry、必要的 bootstrap 协调；延后模块只通过公开 facade/service 获取能力，不再反向 include 主模块实现。<br>4. 在第二阶段再评估 `AngelscriptTest` 是否需要继续 always-on `PostDefault` 加载，还是更适合通过测试 target/开关显式启用；这一步要等现有测试入口和自动化脚本完成对齐后再动。<br>5. 修改 `Plugins/Angelscript/Angelscript.uplugin` 后，配套更新 `Documents/Guides/Build.md`、`Documents/Guides/Test.md`、必要的 bootstrap 文档，避免项目方继续按旧的“所有模块同相位”心智扩展。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、新增 late/toolchain module 对应 `*.Build.cs` 与 module cpp/h；同步更新 `Documents/Guides/Build.md`、`Documents/Guides/Test.md`、必要时 `Documents/Plans/Plan_StatusPriorityRoadmap.md`。 |
| 预估工作量 | M |
| 架构风险 | `LoadingPhase` 调整会直接影响 `OnPostEngineInit`、`UToolMenus`、`ContentBrowserDataSubsystem` 和 runtime bridge 的可用时机；如果没有先把内部能力做成 facade/service，单纯改 descriptor 很容易触发“模块先后顺序变了但代码仍假设旧顺序”的回归。 |
| 兼容性 | 对脚本 API 基本可保持兼容，但对项目内自定义 editor 扩展的启动时机会有影响。更安全的路径是先新增 late/toolchain module，保留旧 `AngelscriptEditor` 相位不变；只有在新模块稳定后，再考虑迁移现有 feature。 |
| 验证方式 | 1. 调整 descriptor 后完整启动一次 editor，确认 `ContentBrowser`、script menu、watcher、source navigation、`BlueprintImpact` 入口和测试脚本都能在预期阶段就绪。<br>2. 在日志中核对各 editor module 的实际加载顺序，确认晚加载模块不会在 `GEditor` 或目标 host 未就绪时提前触发。<br>3. 跑一轮现有 build/test runner，确认 module 名称和加载相位调整没有破坏现有自动化入口。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-69 | `.uplugin` 尚未利用多 module + `LoadingPhase` 表达 editor 能力分层 | descriptor / module 拆层 | 高 |
| P2 | Arch-68 | `AngelscriptEditor` 依赖拓扑仍是单模块吃所有 host | host-adapter 分层与依赖收紧 | 中高 |

---

## 架构分析 (2026-04-10 00:28:48)

### Arch-70：编辑器入口仍是散装 `Tools` 菜单项，缺少统一的 `Angelscript workspace shell`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件是否已经形成一个可发现、可组合、可被项目扩展接管的统一 editor 入口壳层，而不是把功能散落到 `Tools` 菜单和零散 context menu |
| 当前设计 | `AngelscriptEditor` 目前没有自己的根级 workspace shell。模块启动后只注册固定 `Tools` 菜单项，并把脚本扩展系统默认也指向 `MainFrame.MainMenu.Tools`；内建能力和未来扩展最自然的落点仍是“继续往 `Tools` 菜单加条目”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:363` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — 模块启动阶段只把脚本菜单系统和 `RegisterToolsMenuEntries()` 接到 editor UI。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:745` — 固定入口仅在 `MainFrame.MainMenu.Tools` 下增加 `ASOpenCode`、`ASGenerateBindings`、`Function Tests` 三个散装菜单项。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:51` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:56` — 脚本扩展默认就是 `ToolMenu + MainFrame.MainMenu.Tools`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1092` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1111` — `ToolMenu` 分支只是向某个 menu section 动态塞条目，没有统一的 workspace hub owner。 |
| 优点 | 入口实现极轻，新增一项工具或脚本 action 的接线成本低。 |
| 不足 | 当前缺的不是“再多几个入口”，而是一层统一壳。推断：只要后续要补 onboarding、settings、diagnostics、asset authoring、workspace intelligence 或项目级自定义入口，能力就会继续散落在 `Tools` 菜单、`LevelEditor` 菜单和 `ContentBrowser` 菜单之间，`EditorSubsystem` 也拿不到一个稳定的“往 Angelscript 工作台里挂内容”的注册点。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FMainMenuToolbar::Initialize()` 直接在 `LevelEditor.LevelEditorToolBar.User` 放一个 `UnLua` combo button；`GenerateUnLuaSettingsMenu()` 再把 `Hot Reload`、`Generate IntelliSense`、`Settings` 子菜单、`Report Issue`、`About` 组织成同一壳层。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:140` | 先给用户和扩展者一个稳定的插件根入口，再决定里面投放哪些 action/help/settings。 |
| UnrealCSharp | `FUnrealCSharpBlueprintToolBar::GenerateBlueprintExtender()` 在 `BlueprintEditor` 工具栏放置 `UnrealCSharp` combo button，内部根据当前蓝图状态切换 `Open File / Code Analysis / Override Blueprint`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:203` | 根入口不必只有全局菜单；它也可以是 host-local 的工具壳，并根据当前上下文投影不同能力。 |
| puerts | `FDeclarationGenerator` 把同一个 `PluginAction` 同时投影到 `LevelEditor.MainMenu.Window`、`LevelEditor.LevelEditorToolBar.User/PlayToolBar` 和 `Puerts.Gen` console command；入口背后始终是同一个 feature owner。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1698` | 即使不是完整 workspace，也应先形成“统一 capability owner -> 多 surface 投影”的壳，而不是每个 surface 各写一套匿名入口。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增统一的 `Angelscript workspace shell`，让内建工具、onboarding 入口和项目级扩展先汇聚到一个稳定壳层，再分发到 `Tools` 菜单、toolbar 或 asset editor host。 |
| 具体步骤 | 1. 新增 `FAngelscriptWorkspaceShellPresenter` 与 `FAngelscriptWorkspaceEntryDescriptor`，第一阶段只收口现有 `ASOpenCode`、`ASGenerateBindings`、`Function Tests` 三个入口，不改变其执行逻辑。<br>2. 在 `LevelEditor.LevelEditorToolBar.User` 增加一个根级 `Angelscript` combo button；同时保留 `MainFrame.MainMenu.Tools` 下的 `Angelscript` 子菜单作为兼容 alias，而不是继续把所有条目平铺在 `Tools` 根下。<br>3. shell 内至少分四组 section：`Workspace`、`Authoring`、`Diagnostics`、`Settings/Help`；后续 `BlueprintImpact`、workspace 打开、settings、文档入口都只往 descriptor registry 注册，不再直接改 `RegisterToolsMenuEntries()`。<br>4. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 暴露 `RegisterWorkspaceEntry(...)`、`RegisterWorkspaceSection(...)` 或等价 facade，让项目方把自定义入口挂进同一壳层，而不是默认回退到 `MainFrame.MainMenu.Tools`。<br>5. 迁移期保留旧菜单 id 和旧命令别名；当 shell 稳定后，再逐步把散装 `Tools` 条目收回到 `Angelscript` 根入口。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Workspace/*.h/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 若第一阶段就同时重投影到 `MainFrame`、`LevelEditor`、`BlueprintEditor` 三类 host，容易与现有 presenter/工具迁移计划交叉；更稳妥的顺序是先做全局 shell，再逐步给高频 host 加局部壳。 |
| 兼容性 | 向后兼容。现有菜单命令、旧 `Tools` 路径和脚本扩展默认行为都可保留；第一阶段只是新增统一入口和 registry，不要求旧脚本马上改注册方式。 |
| 验证方式 | 1. 启动 editor 后确认现有三个内建工具仍可从旧入口触发，同时也能在新的 `Angelscript` shell 中找到。<br>2. 从一个最小项目级 `UScriptEditorSubsystem` 注册额外 workspace entry，确认无需修改模块主文件即可进入同一根入口。<br>3. 执行一次 full reload，确认 shell 中的 section/entry 不重复注册。 |

### Arch-71：脚本 editor 扩展仍是 `CallInEditor` 函数驱动的反射组合器，还不是 capability 级 contribution model

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本侧 editor 扩展能否表达“有状态的能力贡献”，还是仍然只能把一组 `CallInEditor` 函数拼成菜单/工具栏条目 |
| 当前设计 | `UScriptEditorMenuExtension` 虽然已经支持按 `Category` 生成 submenu 和 toolbar combo button，但它的基本构件仍然只是“void `CallInEditor` 函数 + 若干元数据 + 选中对象/资产数组”。换句话说，当前系统能组合 action 列表，却还不能正式表达带状态、带自定义内容、带多 surface 投影的 capability contribution。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:68` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:86` — `GatherExtensionFunctions()` 只收集带 `CallInEditor` 且无返回值的 `UFunction`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:98` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:102` — 扩展上下文仍只建模 `SelectedObjects/SelectedAssets`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:520` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:570` — 普通菜单完全由函数 category 和函数列表生成。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:572` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:660` — toolbar combo button 也只是 category wrapper，内容仍然是一组函数条目。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:254` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:349`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:352` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:447` — 执行和状态查询最终都通过临时对象 + `ProcessEvent` + 元数据字符串完成，没有稳定的 contribution owner。 |
| 优点 | 对脚本作者极省样板，做一组快捷 action 很快；category/submenu 也足够支撑简单的工具分组。 |
| 不足 | 当前短板不在“能不能做 submenu”，而在 contribution 的基本单位太小。推断：只要能力需要混合 `action + help link + settings link + status summary + dynamic widget + multi-surface alias`，当前模型就会继续退化成更多 `CallInEditor` 函数、更多元数据和更多旁路 native helper；`EditorSubsystem` 也依旧拿不到稳定的 capability handle，只能注册更多函数。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `GenerateUnLuaSettingsMenu()` 不是反射函数列表，而是由 `FUnLuaEditorCommands` 和明确的菜单构造逻辑混合出 `Action`、`Help`、`Settings` 子菜单等不同类型内容。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:118` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:140` | contribution 单位应该是“一个 feature shell 可以构建哪些内容”，而不是“有哪些 `CallInEditor` 函数”。 |
| UnrealCSharp | `FUnrealCSharpBlueprintToolBar` 自己持有 `CommandList`、`TWeakObjectPtr<UBlueprint>` 和 override-file map；`GenerateBlueprintExtender()` 会根据实时状态切换 `Open File / Code Analysis / Override Blueprint`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ToolBar/UnrealCSharpBlueprintToolBar.h:33` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ToolBar/UnrealCSharpBlueprintToolBar.h:41`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:57` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:90`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:203` | capability 应由长期 owner 持有状态，再把状态投影成 menu content；这比临时 `ProcessEvent` 更适合扩展。 |
| puerts | `FDeclarationGenerator` 以 feature object 形式同时拥有 `PluginCommands` 和 `ConsoleCommand`，同一 capability 被投影到 menu、toolbar 和 console，而不是拆成多组反射函数。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1698` | 把 capability 先建模成稳定对象，surface 只是投影；这样后续新增入口不会复制业务逻辑。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `CallInEditor` 兼容面的前提下，引入 capability 级 contribution descriptor，让脚本/native 扩展能注册“内容构建者”，而不只是注册函数。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorContributionDescriptor` 或 `IAngelscriptEditorContribution`，第一阶段至少包含 `ContributionId`、`SurfaceId`、`CommandBindings`、`BuildMenuContent(...)`、`BuildToolbarContent(...)`、`ResolveContext(...)`、`Audience`。<br>2. 为现有 `UScriptEditorMenuExtension` 提供默认 adapter：`GatherExtensionFunctions()` 仍可把旧 `CallInEditor` 函数转成 descriptor；这样旧脚本不需要重写，只是内部不再直接驱动最终 UI。<br>3. 先把内建 `ASOpenCode` / `ASGenerateBindings` / `Function Tests` 和一个脚本扩展迁到新 descriptor，验证同一 contribution 能同时出现在 workspace shell、`Tools` alias 和未来 host-local surface。<br>4. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 暴露 `RegisterEditorContribution(...)`、`UnregisterEditorContribution(...)`、`EnumerateEditorContributions()`，让项目方可以追加 help link、settings link、状态项或动态 section，而不是继续塞更多 `CallInEditor` 函数。<br>5. 第一阶段只开放 menu/toolbar content builder；更复杂的自定义 widget 和长期状态 owner 先限定在 native contribution 中。等 registry 稳定后，再考虑把 richer widget/panel contribution 逐步向脚本扩展开放。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Contributions/*.h` 与对应 `Private/Contributions/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就让脚本侧任意构建 Slate widget，会把 UI 线程、reload 生命周期和资源回收一次性耦合起来；更安全的顺序是先让脚本生成 descriptor，复杂 widget 继续由 native contribution owner 承担。 |
| 兼容性 | 向后兼容。旧 `CallInEditor` 函数、`Category`、`ToolbarLabel`、`EditorIcon` 等元数据都可保留；未迁移扩展继续由默认 adapter 生成 contribution。 |
| 验证方式 | 1. 保留一个旧 `UScriptEditorMenuExtension` 不迁移，确认仍能正常出现在菜单中。<br>2. 用新 descriptor 为同一个 capability 同时生成 shell 入口和旧 `Tools` alias，确认执行逻辑不复制。<br>3. 让一个项目级 `UScriptEditorSubsystem` 注册 help/settings 类型 contribution，确认无需伪造 `CallInEditor` 函数也能进入 editor 壳层。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-70 | 缺少统一 `Angelscript workspace shell`，editor 入口仍是散装 `Tools` 菜单项 | 工作台壳层与入口注册表 | 高 |
| P1 | Arch-71 | 脚本 editor 扩展仍是 `CallInEditor` 函数驱动的反射组合器 | capability contribution model 新增 | 高 |

---

## 架构分析 (2026-04-10 00:38:42)

### Arch-72：editor 扩展注册仍依赖脚本类全量扫描，缺少 module-owned contributor contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 功能是否能由独立 module/plugin 在 `StartupModule()/ShutdownModule()` 中显式贡献和回收，还是只能依赖主模块硬编码与脚本类扫描副作用 |
| 当前设计 | `AngelscriptEditor` 目前有两类 editor feature 装配方式：一类直接写死在 `FAngelscriptEditorModule::StartupModule()`；另一类由 `UScriptEditorMenuExtension` 在初始化时监听 reload，然后用 `TObjectIterator<UASClass>` 全量扫描所有脚本扩展类再原地注册到 UE surface。换句话说，能力拥有者是“主模块入口”和“已加载脚本类集合”，不是“显式 contributor 对象”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — 主模块在一个 `StartupModule()` 里直接串起 `ClassReloadHelper`、source navigation、script menu 初始化、watcher、settings、runtime bridge 和 `ToolMenus` callback。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:363` — 脚本扩展入口仅是一次 `UScriptEditorMenuExtension::InitializeExtensions()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:45` — 初始化逻辑只绑定 `OnPostReload` / `OnEnginePreExit`，以及在“引擎已完成初编译”时直接 `RegisterExtensions()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:845` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1118` — `RegisterExtensions()` 通过 `TObjectIterator<UASClass>` 枚举全部 `UScriptEditorMenuExtension` 子类，并立即把它们挂到 `LevelEditor`、`ContentBrowser`、`ToolMenu` 等 surface。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:84` — `UScriptEditorSubsystem` 仍只有 lifecycle/tick 壳层。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:20` — 对外只暴露 `GetEditorSubsystem()`，没有 contributor 注册/回收 API。 |
| 优点 | 对脚本扩展作者门槛低，只要类被编译并加载，就能在 full reload 后重新挂回菜单。 |
| 不足 | 当前缺的不是“再多一个 registry”，而是 contributor ownership。推断：未来若项目方想把 editor 扩展拆到独立 C++ module、独立插件包、可选开发工具模块或晚加载 adapter 中，现有模式很难在模块边界上显式 `install/uninstall` 能力；它更擅长“全量重扫后恢复”，不擅长“按 owner 增量加入与退出”。这也让 `EditorSubsystem` 很难成为真正的扩展宿主，因为它拿不到稳定的 feature handle。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `StartupModule()` 先构造 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个长期对象；`OnPostEngineInit()` 再调用这些对象的 `Initialize()`。feature 是 module-owned object，而不是靠类扫描自然出现。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:105` | 先把 feature 变成 module 持有的明确对象，再决定何时初始化/回收，扩展包与主模块边界才稳定。 |
| UnrealCSharp | editor 模块在 `StartupModule()` 中显式注册 settings、property customization、toolbar、console command 和 `DynamicDataSource`，并在 `ShutdownModule()` 中对称反注册/`Deinitialize()`/`Reset()`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:33` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:59`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:94` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:116`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:127` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:152` | feature 注册的最小单位是 module 成员和显式 handle，因而独立模块可以明确声明自己贡献了什么。 |
| puerts | `DeclarationGenerator` 作为独立 module/feature owner，自己持有 `PluginCommands` 与 `ConsoleCommand`；`StartupModule()` 里注册 menu/toolbar/console，`ShutdownModule()` 中回收 startup callback。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1607`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1695` | 独立 editor capability 可以先有自己的 owner 和生命周期，再决定要不要投影到主 editor 模块的 surface。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `UScriptEditorMenuExtension` 兼容路径的前提下，引入 module-owned contributor registry，让内建功能和外部扩展都按“显式 owner -> 注册句柄 -> 对称回收”接入 editor。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorContributor` 或 `FAngelscriptEditorContributorHandle`，最小定义 `Startup(Registry)`、`Shutdown(Registry)`、`GetContributorId()`；第一阶段只服务 native 侧。<br>2. 把当前写死在 `StartupModule()` 的几类能力逐步抽成 contributor：`ToolsMenuContributor`、`ContentBrowserContributor`、`SourceNavigationContributor`、`DiagnosticsContributor`。<br>3. 为现有 `UScriptEditorMenuExtension` 增加一个兼容 adapter，例如 `FScriptMenuExtensionContributor`：仍可扫描脚本类，但扫描结果不再直接改 UE surface，而是先转换成 contributor descriptor 再进入 registry。<br>4. 通过 `UScriptEditorSubsystem` 或新的 native facade 暴露 `RegisterNativeContributor(...)` / `UnregisterContributor(...)`，让项目方自己的 editor module 能在 `StartupModule()/ShutdownModule()` 中显式加入和退出功能，而不是等待 full reload 重扫。<br>5. 第二阶段再考虑把 contributor registry 下沉为独立 late-load module，使未来 `BlueprintImpact` UI、workspace panel、开发者工具包都能独立启停；第一阶段不要求拆模块，只先建立 contract。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Contributors/*.h` 与对应 `Private/Contributors/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 若第一阶段同时迁出过多 feature，容易与现有 bootstrap/reload 顺序互相干扰；更稳妥的是先让 registry 承载“内建固定工具 + 一个脚本扩展 adapter”，再逐步收编其它 feature。 |
| 兼容性 | 向后兼容。旧 `UScriptEditorMenuExtension` 和现有 `StartupModule()` 行为都可以先保留，只是内部改为通过 contributor registry 安装。未迁移的脚本扩展仍可在 full reload 后继续生效。 |
| 验证方式 | 1. 新增一个最小独立 editor module，通过 `StartupModule()` 注册 contributor、`ShutdownModule()` 注销 contributor，确认无需触发 full reload 也能增删入口。<br>2. 保留一个旧 `UScriptEditorMenuExtension`，确认它仍能通过兼容 adapter 出现在原有 surface。<br>3. 重载 `AngelscriptEditor` 或独立贡献模块，确认菜单、toolbar、data source 不会重复注册，也不会在 module 卸载后残留。 |

### Arch-73：分析与重载结果仍停留在 `UE_LOG` 和隐式编译队列，缺少 editor-native diagnostics sink

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `BlueprintImpact`、script reload、Blueprint 修复等 editor 行为是否把结果投影到 UE 编辑器原生的 diagnostics surface，而不是只留在 log 或内部副作用里 |
| 当前设计 | 当前分析/修复链虽然已经能算出受影响 `Blueprint` 与原因集合，但结果主要停留在两种地方：一种是 `commandlet` 中的 `UE_LOG` 文本；另一种是 `ClassReloadHelper` 内部的节点重构、编译队列与 `PropertyEditor` 刷新。交互式 editor 侧没有看到稳定的 `MessageLog`、`FCompilerResultsLog` bridge 或可导航的资产级诊断列表。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:100` — `BlueprintImpact` 的正式输出是 `UE_LOG` 的错误与 JSON 摘要。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:102` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:108` — 每个命中的资产也只是继续写一行 log。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:112` — reload 链确实会为每个已加载 `Blueprint` 计算 `ImpactReasons`，但这些原因只在局部变量里存在。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:291` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:299` — 受影响 `Blueprint` 最终只被 `QueueForCompilation()` 和 `FlushCompilationQueueAndReinstance()` 处理。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:333` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:338` — reload 结束后的 editor 反馈只剩 `NotifyCustomizationModuleChanged()`。 |
| 优点 | 现有实现对 headless automation 和 CI 足够直接，日志摘要也方便外部脚本抓取。 |
| 不足 | 当前短板不是“没有结果”，而是结果没有 editor sink。推断：在交互式 editor 中，用户和项目扩展都拿不到一个稳定的、可点击跳转资产的诊断面，因而 `BlueprintImpact` 与 reload repair 更像内部修复副作用，而不是 UE 编辑器可消费的第一等能力。随着未来接入 workspace shell、panel 或 `EditorSubsystem` 自定义分析，这种“只有 log，没有 listing”的形态会持续放大信息丢失。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | editor 模块通过 `FKismetCompilerContext::RegisterCompilerForBP(...)` 把 `UTypeScriptBlueprint` 接到专用 compiler；`FTypeScriptCompilerContext` 直接接收 `FCompilerResultsLog& MessageLog`，因此诊断可以进入 UE 原生蓝图编译结果通道。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:9` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:17` | 语言插件的诊断不一定要先做 panel，先进入 UE 原生 compile/log sink 就已经大幅提升可消费性。 |
| UnLua | editor toolbar 在上下文缺失或目标文件不存在时，不是只写 log，而是用 `FSlateNotificationManager` 直接向用户反馈。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:272` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:277`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:345` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:350` | 即便先不引入复杂 listing，editor-native notification 也应成为默认反馈面，而不是只靠 `UE_LOG`。 |
| UnrealCSharp | 编译线程在开始、成功、失败三个阶段都通过 `FSlateNotificationManager` 发出 editor 通知，把后台过程变成用户可感知的 diagnostics surface。 | `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:214` 到 `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:232`<br>`Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:271` 到 `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:289` | 诊断 sink 可以先从 notification 开始，再逐步升级为更结构化的结果面；关键是 editor 要有正式输出口。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不破坏现有 `UE_LOG`/commandlet 输出的前提下，为 `AngelscriptEditor` 增加统一的 editor diagnostics sink：notification 负责摘要，`MessageLog`/listing 负责结构化详情，后续再视需要接入 compile sink。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 新增 `FAngelscriptEditorDiagnosticsSink` 与专用 `MessageLog` listing，例如 `AngelscriptEditor`；第一阶段只负责 `ReportInfo/Warning/Error` 和 `OpenAsset` 跳转数据。<br>2. 把 `BlueprintImpact` editor 路径接到该 sink：摘要继续保留 JSON/log，但交互式入口额外写入“命中资产 + reasons”结构化条目；每条记录都保留 `AssetData/ObjectPath`，便于未来从 listing 直接打开资产。<br>3. 给 `ClassReloadHelper` 或未来的 reload orchestrator 增加 `FReloadDiagnosticReport`：至少汇总 `DependencyBPs` 数量、节点重构数、失败资产加载数、受影响类型计数，并在 `FlushCompilationQueueAndReinstance()` 后推送 summary notification。<br>4. 第二阶段只对 `UASClass` 派生 `Blueprint` 评估轻量 `UBlueprintCompilerExtension` 或 `FCompilerResultsLog` bridge，把真正的编译语义错误并入 UE 原生 compile lane；第一阶段不要接管全部蓝图编译。<br>5. 通过 `UScriptEditorSubsystem` / `UEditorSubsystemLibrary` 暴露 diagnostics sink facade，让项目方的 panel、toolbar 或自动化工具可以复用同一输出口，而不是各自再写一套 log/notification。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Diagnostics/AngelscriptEditorDiagnosticsSink.h` 与对应 `Private/Diagnostics/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 若第一阶段就把 `BlueprintImpact`、reload repair、compile lane 三者强行合一，容易把已有稳定日志路径打乱；更稳妥的是先做 additive sink，保留原有 `UE_LOG` 和 exit code，再逐步把交互式入口接上 listing/notification。 |
| 兼容性 | 向后兼容。现有 `commandlet` 参数、JSON 摘要和 `UE_LOG` 输出都可保持；新 sink 只在 editor 交互式场景追加可视化结果，不会破坏现有自动化脚本。 |
| 验证方式 | 1. 从 editor 内触发一次 `BlueprintImpact`，确认除了原有 log 外，还能在新的 diagnostics sink 中看到带 asset 跳转的结果条目。<br>2. 触发一次包含受影响 `Blueprint` 的 script reload，确认会出现 summary notification，且 listing 中能看到本轮修复/编译摘要。<br>3. 回归 commandlet 路径，确认 exit code 和日志格式不变，证明新 sink 是 additive 而不是替换。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-72 | editor 扩展仍依赖脚本类全量扫描，缺少 module-owned contributor contract | contributor registry / owner 显式化 | 高 |
| P2 | Arch-73 | 分析与重载结果缺少 editor-native diagnostics sink | `MessageLog` / notification / compile sink 补强 | 中高 |

---

## 架构分析 (2026-04-10 00:50:16)

### Arch-74：`UScriptEditorSubsystem` 没有接入 `FSubsystemCollectionBase` 依赖图，因而还成不了 editor capability 的正式编排层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户若想通过 `EditorSubsystem` 扩展 editor 功能，当前 subsystem 能否表达“我依赖哪些 editor service、我该在何时启动” |
| 当前设计 | `UScriptEditorSubsystem` 目前只是 `UEditorSubsystem` 的脚本化壳层：`Initialize(FSubsystemCollectionBase& Collection)` 并不使用 `Collection` 声明依赖，也没有把现有 editor feature 的启动顺序收敛到 subsystem 图里；真正的 bootstrap 仍在 `FAngelscriptEditorModule::StartupModule()` 直接发生。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:33` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:38` — `Initialize(FSubsystemCollectionBase& Collection)` 只设置 `bSubsystemInitialized = true` 并调用 `BP_Initialize()`，完全没有使用 `Collection`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:26` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:31` — `ShouldCreateSubsystem()` 只转发到 `BP_ShouldCreateSubsystem()`，没有额外的依赖或 phase 约束。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:19` — 对外只有 `GetEditorSubsystem(...)` 的 raw getter，没有 service registry 或 dependency-aware facade。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `StartupModule()` 仍直接串起 `ClassReloadHelper`、source navigation、`UScriptEditorMenuExtension::InitializeExtensions()`、directory watcher、settings、runtime bridge、`OnObjectPreSave` 与 `UToolMenus` callback。 |
| 优点 | 结构足够轻，当前没有引入额外的 subsystem 层级和依赖声明复杂度；脚本作者要做一个最小 editor subsystem 时，上手成本低。 |
| 不足 | 当前问题不是“没有 subsystem”，而是 subsystem 没有进入 UE 的依赖编排体系。推断：项目方即便新增自己的 `UScriptEditorSubsystem` 子类，也无法正式表达“我依赖某个 native editor service 已经 ready”或“我希望在内建 watcher / menu / diagnostics service 之后启动”。结果是扩展仍只能赌模块启动顺序，`EditorSubsystem` 本身并没有成为正式的 capability composition layer。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 不依赖一个空壳 subsystem，而是让 editor module 明确拥有 `FEditorListener`、toolbar 和 `UDynamicDataSource`；`FEditorListener` 自己列出要接的 editor 事件和句柄，模块头也显式暴露 owner。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:35` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/Listener/FEditorListener.h:13` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/Listener/FEditorListener.h:41`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:65` | 即便不走 `UEditorSubsystem`，也先把 owner 和依赖点显式化；这正是当前 Angelscript 可以补到 subsystem facade 里的内容。 |
| UnLua | `StartupModule()` 先构造 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar` 三个长期对象，`OnPostEngineInit()` 再统一初始化；依赖顺序由 staged bootstrap 明确表达，而不是留给外部调用者猜。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:69`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:105` | capability 先成为长期对象，再按 phase 初始化，后续才容易被 facade 或 registry 接管。 |
| puerts | `StartupModule()` 明确绑定 PIE delegate、console command，并立即调用 `OnPostEngineInit()`；真正的 compiler、watcher 与 `JsEnv` 由这条 ready 主链装配。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:108`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:151` | 先把 editor ready 主链集中，再决定向外开放怎样的扩展面；依赖图不应散落在匿名 callback 和脚本类扫描里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 让 `UScriptEditorSubsystem` 从“只有生命周期壳层”升级为“依赖感知的 facade + service registry”，但第一阶段不推翻现有模块启动顺序。 |
| 具体步骤 | 1. 新增 `IAngelscriptEditorService` 或 `FAngelscriptEditorServiceDescriptor`，最少定义 `ServiceId`、`Dependencies`、`Startup()`、`Shutdown()`；第一阶段只服务 native editor feature。<br>2. 扩展 `UScriptEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)`：先对确实是 subsystem 的依赖执行 `Collection.InitializeDependency<...>()`，再启动 `IAngelscriptEditorServiceRegistry`，最后才调用现有 `BP_Initialize()`；这样旧脚本生命周期仍然保留。<br>3. 把当前 `StartupModule()` 里最需要顺序控制的能力先迁成 service object，例如 `ContentBrowserDataSource`、directory watcher、menu/tool registry、diagnostics sink；模块主文件只保留 bootstrap 和兼容 forwarding。<br>4. 在 `UEditorSubsystemLibrary` 上新增 `RegisterNativeEditorService(...)`、`FindEditorService(...)`、`EnumerateEditorServices()` 之类的 facade，让项目方自己的 editor module 或未来脚本 facade 能按 service id 扩展，而不是继续假设模块静态函数的启动时机。<br>5. 兼容期保留当前 `StartupModule()` 路径：若某个 capability 还未迁到 service registry，就继续按旧顺序启动；待核心 owner 全部迁完，再收紧直接模块接线。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/*.h` 与对应 `Private/Services/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就要求所有内建 feature 同时迁到新 registry，容易把现有 startup 时序一次性打散；更稳妥的是先把 subsystem registry 建起来，再按高风险 feature 逐个收编。 |
| 兼容性 | 向后兼容。现有 `BP_Initialize/BP_Deinitialize/BP_Tick` 和模块启动逻辑都可保留；新增的 dependency/service 层是 additive contract，不会立即破坏现有脚本 subsystem。 |
| 验证方式 | 1. 添加两个最小 service，其中一个显式依赖另一个，验证启动与关闭顺序稳定且可测试。<br>2. 回归现有菜单、watcher、data source 和 source navigation，确认迁入 registry 后行为不变。<br>3. 让项目方自定义一个 editor module 在 `StartupModule()` 中注册 service，确认无需依赖隐藏的模块启动顺序也能正常挂入。 |

### Arch-75：`UScriptEditorSubsystem` 仍是 `BP_Tick` 轮询宿主，缺少 editor-native 事件桥接 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户能否通过 `EditorSubsystem` 以事件驱动方式扩展 editor，还是只能靠每帧 `Tick` 轮询和外部模块私下绑 delegate |
| 当前设计 | `UScriptEditorSubsystem` 目前只有 `BP_Initialize/BP_Deinitialize/BP_Tick` 三类执行入口；与此同时，真正的 editor 事件却仍直接绑在模块或静态函数上，例如 `OnPostEngineInit`、`GameplayTags` 变化、directory watcher、`UToolMenus` startup callback。换句话说，subsystem 并没有成为 editor 事件的正式桥。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:51` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:58` — 除了 `BP_Initialize/BP_Deinitialize` 外，唯一持续执行入口是 `BP_Tick(float DeltaTime)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:21` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:24` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:60` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:63` — `GetWorld()` 返回 editor world，但 `GetTickableGameObjectWorld()` 却固定返回 `nullptr`，说明当前 tick contract 本身也没有把 world/session 语义表达清楚。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:19` — library 仍只提供 getter，没有任何 editor event facade。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:94` — 脚本文件变化直接进入 `QueueScriptFileChanges(...)`，没有经过 subsystem 事件层。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:359` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:415` — `GameplayTags`、`OnPostEngineInit`、directory watcher、`OnObjectPreSave` 与 `UToolMenus` callback 都直接在模块启动时绑定。 |
| 优点 | 实现简单，当前 editor 事件量不大时，模块级 raw delegate 足够直接；保留 `BP_Tick` 也方便写一些一次性的轮询脚本。 |
| 不足 | 当前短板不是“没有事件”，而是事件没有进入正式 contract。推断：项目方若要基于 `EditorSubsystem` 扩展 `PIE`、主窗口 ready、脚本文件变化、toolchain 阶段、diagnostics 更新等能力，只能在 `Tick` 里自己轮询，或绕过 subsystem 再绑一套 native delegate。长期看，这会让 user extension 与内建功能越来越像两套平行世界。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FEditorListener` 明确列出 `OnPostEngineInit`、`OnPreBeginPIE`、`OnPrePIEEnded`、`OnCancelPIE`、`OnMainFrameCreationFinished`、`OnApplicationActivationStateChanged`、`OnDirectoryChanged` 等回调；另外 `FUnrealCSharpCoreModuleDelegates` 还公开 `OnBeginGenerator`、`OnEndGenerator`、`OnCompile`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/Listener/FEditorListener.h:13` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/Listener/FEditorListener.h:41`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:24` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14` 到 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:21` | 先把 editor 事件与 toolchain 阶段变成显式 delegate，再决定哪些 UI/service 要消费它们；不必把所有状态都压给每帧轮询。 |
| UnLua | 模块在 `OnPostEngineInit()` 中统一初始化 toolbar 和 `IntelliSenseGenerator`，再单独监听 `OnMainFrameCreationFinished()` 与 package save 事件；关键 editor 生命周期都是事件驱动，而不是靠 subsystem tick 自己猜。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:69`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:107` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:112` | editor-ready、main-frame-ready、package-save 这些信号可以先收敛为正式事件，再让不同 feature 订阅。 |
| puerts | `StartupModule()` 明确绑定 `PreBeginPIE`、`EndPIE`，并通过 `OnPostEngineInit()` 启动 compiler、watcher 与 `JsEnv`；editor lane 的关键状态不是靠轮询，而是靠明确 delegate 驱动。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:83`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:151`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:167` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:175` | 对语言插件而言，PIE、compile、watcher、engine-ready 都应该是 first-class event，而不是 tick 里重建的隐式状态。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `BP_Tick` 兼容入口的前提下，引入 `editor event hub`，把现有模块级 delegate 汇总成 `EditorSubsystem` 可消费的 typed event contract。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorEventHub` 或 `UAngelscriptEditorEventRouter`，第一阶段至少汇总 `OnEditorReady`、`OnMainFrameReady`、`OnPIEStateChanged`、`OnScriptFilesChanged`、`OnGameplayTagsChanged`、`OnToolMenusReady`。<br>2. 让 `FAngelscriptEditorModule::StartupModule()` 继续绑现有 native delegate，但回调第一站改为 event hub；旧 feature 先从 hub 订阅，逐步消除模块内直接扇出。<br>3. 扩展 `UScriptEditorSubsystem`：保留 `BP_Tick`，同时新增更细的 `BlueprintNativeEvent`/delegate facade，例如 `BP_OnEditorReady()`、`BP_OnMainFrameReady()`、`BP_OnPIEStateChanged(bool bEntering)`、`BP_OnScriptFilesChanged(...)`；旧脚本 subsystem 可以不改。<br>4. 明确 tick/world 语义：对需要 world 的逻辑，让 `GetTickableGameObjectWorld()` 返回 editor world，或把 world-sensitive 回调从全局 session event 中拆开；不要继续让所有 editor 扩展都依赖 worldless tick。<br>5. 在 `UEditorSubsystemLibrary` 上补只读状态与订阅 facade，例如 `IsEditorReady()`、`IsPIEPlaying()`、`GetLastScriptFileChanges()` 或等价接口，让项目级脚本扩展能消费同一份状态，而不必自己再绑 native delegate。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Events/*.h` 与对应 `Private/Events/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段把所有细粒度 editor signal 都原样广播到脚本层，容易引入事件风暴和重入问题；更稳妥的是先聚合为少量高价值事件，并统一切回 game thread 再触发脚本回调。 |
| 兼容性 | 向后兼容。现有 `BP_Tick`、模块级 delegate 绑定和现有 feature 行为都可保留；新的 event hub 先作为 additive bridge，引导项目扩展逐步从轮询迁移到事件驱动。 |
| 验证方式 | 1. 写一个最小 `UScriptEditorSubsystem` 扩展，仅监听新事件而不使用 `BP_Tick`，验证 `OnEditorReady`、`OnPIEStateChanged` 与 `OnScriptFilesChanged` 都能按预期触发。<br>2. 回归现有 watcher、menu 注册和 tags reload，确认它们改走 event hub 后行为不变。<br>3. 保留一个旧 tick-only subsystem，验证迁移后仍能正常初始化和逐帧执行，证明新 contract 是增量补强而不是破坏性替换。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-74 | `UScriptEditorSubsystem` 未接入 subsystem 依赖图，无法正式编排 editor capability | dependency-aware service registry / subsystem facade | 高 |
| P1 | Arch-75 | `UScriptEditorSubsystem` 仍以 `BP_Tick` 轮询为主，缺少 editor-native 事件桥接 contract | event hub / typed editor events | 高 |

---

## 架构分析 (2026-04-10 00:59:31)

### Arch-76：`ClassReloadHelper` 的 reload 委托生命周期脱离模块所有权，动态重载时存在重复绑定风险

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | reload 回调注册的所有权、反注册路径，以及 editor module 动态重载时的稳定性 |
| 当前设计 | `FAngelscriptEditorModule::StartupModule()` 只调用一次 `FClassReloadHelper::Init()`；`Init()` 内部直接对 `FAngelscriptClassGenerator` 的多条事件执行 `AddLambda(...)`，但没有保存任何 `FDelegateHandle`，也没有提供 `Shutdown()`。`ShutdownModule()` 只回收 `PreSave`、state dump 和 `ToolMenus`，不会触及这些 reload 回调。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:353` — 模块启动时第一步就是 `FClassReloadHelper::Init()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:50` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:175` — `Init()` 直接对 `OnStructReload`、`OnClassReload`、`OnDelegateReload`、`OnLiteralAssetReload`、`OnEnumChanged`、`OnEnumCreated`、`OnFullReload`、`OnPostReload` 连续 `AddLambda(...)`；结构体中没有任何 `FDelegateHandle` 成员，也没有 `Shutdown()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:676` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:689` — `ShutdownModule()` 只移除 `GLiteralAssetPreSaveHandle`、state dump extension 和 `UToolMenus` owner。 |
| 优点 | reload 修复逻辑集中在一个 helper 内，当前主路径实现简单，不需要在模块里维护一长串 handle。 |
| 不足 | 当前 shortcoming 不在“功能能不能跑”，而在生命周期 contract。推断：如果 editor module 被动态卸载再加载，或者未来需要把 reload pipeline 拆成多个 feature，这些匿名 lambda 会继续存活在 `FAngelscriptClassGenerator` 的 delegate 上，导致重复执行、悬挂依赖，且外部扩展者也没有办法按 feature 粒度退订或替换其中一段。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ShutdownModule()` 对称移除 `OnPostEngineInit` 和 package save 相关 delegate；回调生命周期明确绑定在 module 实例上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:72` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:85` | 只要 callback 属于模块，就必须有与模块同寿命的卸载路径。 |
| UnrealCSharp | `FEditorListener` 把 `OnPostEngineInit`、`PreBeginPIE`、`OnBeginGenerator`、`OnEndGenerator`、`OnCompile`、`OnMainFrameCreationFinished` 等 handle 都存成成员，并在析构函数逐一 `Remove(...)`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:134` | 把 editor event bridge 做成长期对象，并把“谁注册谁注销”固化为对象职责。 |
| puerts | `DeclarationGenerator` 在 `StartupModule()` 注册 `UToolMenus::RegisterStartupCallback(...)`，并在 `ShutdownModule()` 对称 `UnRegisterStartupCallback(this)`；对象级 watcher `UPEDirectoryWatcher` 也通过 `DelegateHandle` + `UnWatch()` / 析构函数完成回收。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1694`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:89` | 即便能力是临时对象或工具模块，也要显式保存注册句柄并提供反注册。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `FClassReloadHelper` 从“匿名静态委托集合”收敛成“module-owned reload bridge”，显式保存并回收所有 generator delegate handle。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorReloadBridge`（或等价类型），把当前 `FClassReloadHelper::Init()` 的事件注册逻辑迁入 `Initialize()`，并为每条 `FAngelscriptClassGenerator::*` delegate 保存 `FDelegateHandle`。<br>2. 给 bridge 增加 `Shutdown()` 和 `bInitialized` 防重入保护；`Shutdown()` 需要按注册顺序逆序 `Remove(...)`，并在 editor module 关闭时调用。<br>3. 将 `FAngelscriptEditorModule` 改为持有该 bridge 的成员对象或 `TUniquePtr`；`StartupModule()` 里创建并初始化，`ShutdownModule()` 里优先关闭 reload bridge，再做现有 `ToolMenus` / `PreSave` 清理。<br>4. 在 bridge 上增加一个更窄的 outward-facing contract，例如 `OnReloadPrepared` / `OnReloadFinished` 事件或 `RegisterReloadParticipant(...)`，让后续 `ContentBrowser`、diagnostics、project 扩展不必再直接绑 `FAngelscriptClassGenerator`。<br>5. 第一阶段保留 `FClassReloadHelper::FReloadState` 和现有修复逻辑不变，只处理注册所有权；第二阶段再考虑把更多 reload feature 拆分为 participant。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Reload/AngelscriptEditorReloadBridge.h/.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就同时改写 reload 算法和注册层，容易把现有稳定修复链一起打散；更稳妥的方式是先只迁移 delegate ownership，不改 `PerformReinstance()` 细节。 |
| 兼容性 | 向后兼容。对现有脚本和项目工程没有 API 破坏；变化主要是内部生命周期管理更严格，防止 module reload 后重复绑定。 |
| 验证方式 | 1. 在支持动态模块卸载/重载的场景下重复加载 `AngelscriptEditor`，确认一次 full reload 只触发一轮 `OnPostReload` 副作用。<br>2. 为 bridge 增加临时日志或测试钩子，验证 `Initialize()`/`Shutdown()` 成对执行，重复 `StartupModule()` 不会叠加注册。<br>3. 回归脚本 reload、`Blueprint` 编译、菜单扩展重建与 `ContentBrowser` 相关行为，确认 ownership 收敛后功能不变。 |

### Arch-77：reload 修复产物缺少 ownership ledger，`ReplaceHelper` 与 volume `ActorFactory` 以 additive 方式留在 editor 全局状态

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | reload 过程中临时创建的 editor artifact 是否有正式 owner、回收策略和重复重建保护 |
| 当前设计 | `PerformReinstance()` 里同时存在两类“只增不减”的 repair artifact：一类是全局 `ReplaceHelper`，首次使用时 `NewObject(...)` 后 `AddToRoot()`；另一类是新 volume class 对应的 `UActorFactory`，每次 reload 都直接 `NewObject<UActorFactory>(...)` 并追加到 `GEditor->ActorFactories`。`FReloadState` 本身不维护这些对象的 ledger，模块关闭时也没有统一清理。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:27` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:39` — `FReloadState` 只记录 class/struct/enum/delegate 变化和少量 flag，没有任何“owned editor artifact”容器。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:25` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:37` — `ReplaceHelper` 是文件级全局指针，首次命中时 `NewObject<UAngelscriptReferenceReplacementHelper>(...)` 后 `AddToRoot()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:340` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:368` — 对新 volume class 逐个构造 `UActorFactory` 并 `GEditor->ActorFactories.Add(NewFactory)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:421` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:435` — 打开中的 asset editor 也是通过 `NotifyAssetClosed/NotifyAssetOpened` 直接施加副作用，没有单独的 session artifact owner。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:676` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:689` — 模块关闭时没有涉及 `ReplaceHelper`、`ActorFactories` 或 asset-editor session 修复产物的清理。 |
| 优点 | 对当前内建问题的修补速度快，volume 放置模式和打开中的 asset editor 能在同一轮 reload 后尽快恢复可用。 |
| 不足 | 当前短板是 artifact lifecycle 没有账本。推断：`ReplaceHelper` 会在 editor 生命周期里一直 rooted；volume class 频繁 reload 时，`GEditor->ActorFactories` 有累加重复 factory 的风险；未来如果项目方要为自定义 toolkit、panel 或 asset editor 添加 reload 修复，也没有标准位置声明“我创建了什么、何时清理”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `UDynamicDataSource` 自己拥有运行期层级缓存和 generator 事件句柄；`Shutdown()` 会 `Reset()` 层级并移除 delegate，而 `UpdateHierarchy()` 每次都重建 `DynamicHierarchy`，避免往 editor 全局状态里持续 append。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:107`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:739` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:748` | 动态 editor artifact 应该由 feature 自己持有，并支持“重建”而不是无界追加。 |
| puerts | `UPEDirectoryWatcher` 虽然同样是 `UObject` 并 `AddToRoot()`，但它把 `DelegateHandle` 和 `Directory` 作为对象状态保存，`UnWatch()` 与析构函数会明确释放监听。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:9` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:12`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:64` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:89` | 即使使用 rooted helper，也应当把释放路径做成对象职责，而不是让对象永久挂在全局状态里。 |
| UnrealCSharp | `FEditorListener` 的所有 editor session hook 都是成员句柄，析构时逐一移除，不把临时 session 修补逻辑遗留在全局回调中。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:134` | session repair 和 event hook 最终都需要回到一个可清理的 owner，而不是只靠全局副作用。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 reload repair 引入显式 `artifact ledger`，把 rooted helper、spawned factory 和 session repair 都纳入同一个 owner 下，支持重建与清理。 |
| 具体步骤 | 1. 在新的 reload bridge 或 `FReloadState` 外围新增 `FAngelscriptEditorReloadArtifactLedger`，最少追踪 `TStrongObjectPtr<UAngelscriptReferenceReplacementHelper>`、`TArray<TWeakObjectPtr<UActorFactory>> OwnedVolumeFactories` 和本轮捕获的 asset-editor session snapshot。<br>2. 把 `ReplaceHelper` 从文件级全局指针迁移到 ledger：只在 reinstance 前创建并 root，`PerformReinstance()` 结束后 `RemoveFromRoot()` / `Reset()`；若某些路径仍需跨函数访问，由 bridge/ledger 传参而不是靠全局静态。<br>3. 对 volume factory 改成“先清理再重建”：在添加新 factory 之前，先从 `GEditor->ActorFactories` 中移除上一轮 ledger 记录且仍指向旧 script volume class 的 factory，然后基于当前有效 class 重新生成并更新 ledger。<br>4. 为未来自定义 repair logic 预留 `RegisterReloadArtifactParticipant(...)` 或等价接口，让项目方的 custom asset editor / dockable panel 可以声明自己的 `Capture()` / `Restore()` / `Cleanup()`。<br>5. 第一阶段维持现有 `NotifyAssetClosed/NotifyAssetOpened` 语义不变，只把其所需的会话信息纳入 ledger；第二阶段再评估是否把 `Blueprint`、`DataAsset` 等类型迁成更强的 session adapter。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Reload/AngelscriptEditorReloadArtifactLedger.h/.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 直接动 `GEditor->ActorFactories` 的移除顺序如果处理不好，可能影响现有 volume 放置入口；更稳妥的是先只清理 ledger 自己创建过的 factory，并保留旧 fallback 逻辑。 |
| 兼容性 | 向后兼容。现有 reload 行为、volume 放置和 asset editor 重开路径都可保留；变化主要是全局状态从“只增不减”收敛为“由 ledger 管理的可重建状态”。 |
| 验证方式 | 1. 对同一个 script volume class 连续触发多次 full reload，确认 `GEditor->ActorFactories` 中对应 factory 数量保持稳定，不会线性增长。<br>2. 在本轮 reload 完成后检查 `ReplaceHelper` 不再保持 rooted 状态，再次 reload 仍能正常完成引用替换。<br>3. 保持若干 asset editor 打开，执行 reload 后确认每个 editor 只经历一轮 reopen/refresh，且模块关闭时没有残留 repair artifact。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-76 | `ClassReloadHelper` 的 reload 委托未绑定到模块生命周期，动态重载时可能重复注册 | delegate ownership 收敛 + reload bridge | 高 |
| P1 | Arch-77 | reload 修复产物缺少 ownership ledger，临时 helper 与 volume factory 可能长期滞留 editor 全局状态 | artifact ledger + rebuild/cleanup contract | 高 |

---

## 架构分析 (2026-04-10 01:07:01)

### Arch-78：`BlueprintImpact` 已具备可测试扫描内核，但还没有 editor-native tool contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `BlueprintImpact` 在 editor 内的入口组织、状态复用和可扩展工具契约 |
| 当前设计 | 当前实现把 `BlueprintImpact` 核心能力收敛成纯扫描函数 + `Commandlet`：`FBlueprintImpactRequest` / `FBlueprintImpactScanResult` 是轻量 POD 结构，`ScanBlueprintAssets(...)` 同步遍历 `AssetRegistry` 并加载 `Blueprint`；`Commandlet` 负责解析参数、输出日志和返回码。与此同时，`AngelscriptEditor` 的常驻 UI 入口仍只注册 `OpenCode` / `GenerateBindings` / `FunctionTests`，没有把 `BlueprintImpact` 提升成可复用的 editor command、service 或结果模型。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:34` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:68` — 请求、匹配、扫描结果和公开 API 都是无 owner 的 free-function contract。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:278` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:309` — 扫描主流程同步构建 symbol、枚举候选资源、逐个 `GetAsset()` 加载并分析。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:120` — editor 外入口是 `Commandlet`，输出为日志行和退出码。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:22` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:75` — 现有测试已经覆盖路径归一化、symbol 构建、节点/变量/Delegate 影响和 `Commandlet` 错误码。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:745` — 常驻 `ToolMenus` 入口只注册了 3 个工具项，没有任何 `BlueprintImpact` UI/action。 |
| 优点 | 现在的扫描核心解耦度高、测试基础扎实，天然适合命令行回归、CI 和后续抽成共享 backend。 |
| 不足 | 当前短板不是“没有扫描能力”，而是它还不是 editor first-class tool。推断：项目方若想在 `BlueprintEditor`、`ContentBrowser`、reload 后诊断或 `EditorSubsystem` 扩展中复用该能力，只能重新拼请求、自己接 UI 和状态缓存；用户也无法在 editor 里获得进度、取消、历史结果、上下文路径或统一通知。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `IntelliSense` 能力不是只有 `Commandlet`，而是同时存在 `FUnLuaIntelliSenseGenerator` 常驻 service、`FUnLuaEditorCommands` 命令、`FMainMenuToolbar` 工具栏入口；模块在 `OnPostEngineInit()` 初始化 toolbar 和 generator。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:70`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:32`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:29` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:37`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:118` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:140`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:107` | 先把算法变成常驻 service，再提供 `toolbar/menu/commandlet` 多入口，UI 与 backend 复用同一能力。 |
| puerts | `DeclarationGenerator` 直接是独立 editor tool module：有自己的 `FGenDTSCommands`、`FGenDTSStyle`、`PluginCommands`、`ToolMenus` 注册和 console command，生成结束后还发 notification。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1566` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1698`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp:13` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp:17`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSStyle.cpp:17` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSStyle.cpp:31`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSStyle.cpp:49` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSStyle.cpp:56` | 工具型能力如果要长期演进，最好拥有独立 command/style/notification contract，而不是只留下底层函数。 |
| UnrealCSharp | `Generator()` 不只是按钮回调，而是 editor module、toolbar、console command 和 `FEditorListener` 共用的 toolchain 主链；前后阶段通过 `FUnrealCSharpCoreModuleDelegates::OnBeginGenerator/OnEndGenerator` 暴露给其它 feature。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:33` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:125`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:309`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:67`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:176` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:226` | 对 editor toolchain 来说，扫描/生成阶段本身也应当是可订阅事件，而不是仅能被终端或日志消费。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `BlueprintImpact` 从“扫描 backend + commandlet”升级为“共享 backend + editor service + 多入口 facade”，但第一阶段不改扫描算法和测试。 |
| 具体步骤 | 1. 新增 `FAngelscriptBlueprintImpactService`（或等价 `UEditorSubsystem`/feature 对象），持有 `LastRequest`、`LastResult`、`bIsScanning`，并统一暴露 `RunScan(...)`、`CancelScan()`、`GetLastResult()`、`OnScanStarted/OnScanFinished`。<br>2. 保持 `ScanBlueprintAssets(...)` 作为纯 backend；第一阶段 service 只是在 game thread 上同步调用它，第二阶段再视资产规模决定是否拆成可取消 job。<br>3. 在 `AngelscriptEditorModule` 中新增正式 command，把当前没有 editor 入口的 `BlueprintImpact` 接到 `ToolMenus`，并预留 `BlueprintEditor` / `ContentBrowser` 上下文入口；现有 `Commandlet` 改为调用同一 service/backend，而不是维护另一套输出路径。<br>4. 给 service 增加轻量结果 adapter：最小版本先做 `notification + message log/json export`，第二阶段再补 dockable results panel；这样现有自动化测试和 CLI 使用方式都不受影响。<br>5. 通过 `UScriptEditorSubsystem` 或 `EditorSubsystemLibrary` 暴露只读 facade，让项目方能在自定义 editor 扩展里复用同一份扫描结果，而不是重新直接 include scanner 头。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactService.h` 与对应 `Private/BlueprintImpact/AngelscriptBlueprintImpactService.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就把扫描改成异步并允许在任意线程加载 `Blueprint`，容易碰到 editor-only UObject 访问和 `AssetRegistry` 时序问题；因此应先把 owner 与入口统一，再逐步异步化。 |
| 兼容性 | 向后兼容。现有 `Commandlet` 参数、扫描结构和测试都可以保留；新增的是 editor 入口与 service facade，不会破坏已有自动化或脚本用户。 |
| 验证方式 | 1. 回归 `Angelscript.Editor.BlueprintImpact.*` 现有自动化测试，确认 backend 未回归。<br>2. 新增 editor command 后，在 `Tools` 菜单或指定 surface 触发一次 full scan 和 changed-script scan，确认结果与 `Commandlet` 一致。<br>3. 写一个最小项目级扩展订阅 `OnScanFinished`，确认无需直接 include scanner internals 也能读取扫描结果。 |

### Arch-79：脚本资产主工作流仍绕过标准 `AssetTools/UFactory/Add New` 合同，扩展者难以插入自己的创建策略

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产的创建、打开和编辑工作流是否真正接入 UE editor 的标准 asset contract，以及项目方能否增量扩展这套流程 |
| 当前设计 | 当前主路径仍是“runtime/debug delegate -> editor popup helper”。`ShowCreateBlueprintPopup()` 直接弹 `CreateModalSaveAssetDialog`，对 `DataAsset` 走 `NewObject<UDataAsset>`，对 `Blueprint` 走 `FKismetEditorUtilities::CreateBlueprint(...)`；`ShowAssetListPopup()` 直接把 `AssetPicker` 包进 `PushMenu()` 临时弹层。与此同时，`ContentBrowserDataSource` 的 `CanEditItem/EditItem/BulkEditItems/AppendItemReference` 全部返回 `false`，而插件虽然已经内置 `UAssetToolsStatics` 和 `UScriptableFactory`，但这些标准 `AssetTools/UFactory` helper 并没有进入主工作流。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:409` — runtime module 通过 delegate 把“列出资产/创建蓝图”转发到 editor popup helper。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:538` — 创建流程使用 modal save dialog，随后直接 `NewObject<UDataAsset>` 或 `FKismetEditorUtilities::CreateBlueprint(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:541` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:672` — 多资源打开流程依赖临时 `AssetPicker` popup 和 `PushMenu()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:187` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:205` — 数据源没有把 edit/bulk edit/reference append 这些标准动作接进来。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:20` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:80` — 插件已经暴露了 `AssetTools::CreateAsset/FixupReferencers/ImportAssetTasks` 的脚本入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h:16` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h:32` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.cpp:8` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.cpp:22` — `UScriptableFactory` 已经能承载 `UFactory` 型创建逻辑，但当前主工作流并未通过它接入 `Add New` 或 `ContentBrowser`。 |
| 优点 | 当前实现把“创建一个 script-backed Blueprint/DataAsset”做得很直接，功能链短，debug server 和 editor 内部都能快速拉起该流程。 |
| 不足 | 当前问题不是“不能创建”，而是创建 contract 没进入 UE 的标准 asset workflow。推断：项目方若想为某类 script asset 增加自定义 `UFactory`、路径默认值、校验、额外 metadata、批量创建、右键菜单或 `EditorSubsystem` 扩展，只能继续改 `ShowCreateBlueprintPopup()` 这样的中心 helper，而不能像原生资产那样注册 handler 或复用 `Add New` 上下文。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `UDynamicDataSource` 在初始化时就把自己挂到 `ContentBrowser.AddNewContextMenu`，`PopulateAddNewContextMenu()` 读取真实 `SelectedPaths` 后交给 `FDynamicNewClassContextMenu`，再由 `SDynamicNewClassDialog` 提供带校验和路径选择的专用 wizard。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:88`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:704` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:727`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:7` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:58`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:27` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:109` | 资产创建入口应当先吃到 `ContentBrowser` 上下文，再进入专门的对话和生成逻辑，而不是先走一个无上下文的全局 popup。 |
| UnLua | 不是用全局 helper 去猜当前上下文，而是在 `BlueprintEditor` toolbar 上拿到当前 `UBlueprint`，再执行 `Bind/Unbind/CreateLuaTemplate/RevealInExplorer` 等动作，必要时直接编译并打开相关 graph。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:75`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:127` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:135`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:138` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:199`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:257` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:377` | 即便功能不是传统 asset factory，也应尽量落在有真实上下文的 editor surface 上，而不是统一退回全局弹窗。 |
| puerts | `TypeScript` 生成类不是脱离原生生命周期单独创建，而是由 `FTypeScriptCompilerContext::SpawnNewClass()` 挂在 `Blueprint` 编译主链中，保证 script-backed class 的生成/重建进入原生 compiler contract。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:19` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:40` | 对 script-backed 资产来说，创建/重建逻辑越接近 UE 原生 pipeline，后续扩展和状态一致性越容易控制。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 建立 `Angelscript asset workflow service` 和 handler registry，把“如何创建/打开哪一类 script asset”从全局 popup helper 中拆出来，并优先接到 `ContentBrowser/Add New` 与具体 editor surface。 |
| 具体步骤 | 1. 新增 `IAngelscriptAssetWorkflowHandler`（或等价 descriptor），最少定义 `CanHandle(UASClass*)`、`BuildDefaultPath(...)`、`CreateAsset(...)`、`OpenAsset(...)`、`CanBulkEdit(...)`；第一阶段内置 `Blueprint` 与 `DataAsset` 两个默认 handler。<br>2. 把当前 `ShowCreateBlueprintPopup()` / `ShowAssetListPopup()` 的逻辑迁入 `FAngelscriptAssetWorkflowService`：popup 仍然可以保留，但只作为默认 handler 的 UI 实现，而不再是唯一入口。<br>3. 扩展 `UAngelscriptContentBrowserDataSource`，至少补上 `EditItem()`、`BulkEditItems()` 和 `AppendItemReference()`，让 `ContentBrowser` 中看到的 script asset 能回到同一套 workflow service。<br>4. 在 `ContentBrowser.AddNewContextMenu` 或等价 `ToolMenus` surface 上增加 script asset 创建入口，并把 `SelectedPaths`、`BaseClass`、默认命名规则传给 handler；现有 runtime delegate 路径继续调用 workflow service，作为兼容入口。<br>5. 把 `UScriptableFactory` / `UAssetToolsStatics` 正式纳入这套 contract：对于需要工厂化创建的资产，优先走 `UFactory` / `AssetTools`；对于仍必须用 `FKismetEditorUtilities::CreateBlueprint(...)` 的路径，则把它包成具体 handler，而不是散落在模块静态函数中。<br>6. 第二阶段再开放 handler 注册 API 给项目方 editor module 或未来的 `UScriptEditorSubsystem` facade，使用户无需修改插件核心源码也能新增某类 script asset 的创建策略。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/AssetWorkflow/*.h` 与对应 `Private/AssetWorkflow/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就强行把所有路径统一改成 `UFactory`，会把当前稳定的 `Blueprint` 创建链一起重写；更安全的做法是先引入 handler registry，把现有 `FKismetEditorUtilities` 路径封装进去，再逐步替换为更标准的 asset contract。 |
| 兼容性 | 向后兼容。现有 debug server、popup 和直接创建逻辑都可继续保留，只是改为转发到统一 workflow service；旧脚本用户和现有项目不需要立即改代码。 |
| 验证方式 | 1. 从 `ContentBrowser` 的目标路径触发一次 script asset 创建，确认默认目录、命名和打开 editor 行为正确。<br>2. 从现有 runtime/debug 入口再次触发相同创建路径，确认新旧入口最终落到同一 handler。<br>3. 为一个自定义 handler 做最小接入测试，确认无需改 `ShowCreateBlueprintPopup()` 主函数也能扩展一类新资产。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-78 | `BlueprintImpact` 仍停留在 backend + `Commandlet`，缺少 editor-native tool contract 与复用事件面 | tool service 化 + command/panel facade | 高 |
| P1 | Arch-79 | 脚本资产创建/打开主路径未接入标准 `AssetTools/UFactory/Add New` 合同 | asset workflow service + handler registry | 高 |

---

## 架构分析 (2026-04-10 01:16:38)

### Arch-80：`BlueprintImpact` 的 changed-script 模式只缩小 symbol 集，不会缩小 Blueprint 候选集，缺少 `AssetRegistry` 驱动的增量候选索引

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `BlueprintImpact` 在 editor 常驻使用时，是否已经具备“全量初始化 + 增量维护”的候选资产索引，而不是每次扫描都重新遍历全部 `Blueprint` |
| 当前设计 | `FBlueprintImpactRequest` 只有 `ChangedScripts` 与 `bIncludeOnlyOnDiskAssets` 两个控制项；changed-script 模式只影响 `MatchingModules` 和 `Symbols`，不会影响 `CandidateAssets` 的来源。`ScanBlueprintAssets(...)` 每次都会重新向 `AssetRegistry` 拉取全量 `Blueprint` 资产，再逐个 `GetAsset()` 加载并分析。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:34` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:43` — 请求模型只有 `ChangedScripts` 与 `bIncludeOnlyOnDiskAssets`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:88` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:110` — `FindModulesForChangedScripts(...)` 仅按 changed scripts 缩小匹配模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:252` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:275` — `FindBlueprintAssets(...)` 总是执行 `AssetRegistry.GetAssets(Filter, Assets, true)`，拿到全量 `UBlueprint` 资产集合。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:278` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:309` — `ScanBlueprintAssets(...)` 无论 full scan 还是 changed-script scan，都会把 `CandidateAssets` 设为 `FindBlueprintAssets(...)` 结果，并对每个 `AssetData` 执行 `GetAsset()`。 |
| 优点 | 无状态实现简单、结果稳定，`Commandlet` 与测试不需要依赖 editor 常驻缓存；任何一次扫描都能独立跑完。 |
| 不足 | 当前问题不是“不能扫描”，而是扫描 contract 还停留在 stateless batch。推断：一旦把 `BlueprintImpact` 提升成 editor 内可频繁调用的能力，changed-script 模式仍会重复遍历并加载全量 `Blueprint`，导致它更像 CI/命令行工具，而不是可被 `EditorSubsystem`、toolbar、右键菜单或结果面板持续复用的 editor service。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaIntelliSenseGenerator` 先在 `Initialize()` 中常驻订阅 `AssetRegistry` 的 `OnAssetAdded/Removed/Renamed/Updated`，`UpdateAll()` 负责一次全量初始化，后续由单资产事件增量维护 IntelliSense 产物。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:58` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:107`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:290` | editor intelligence 先有长期索引 owner，再把 full rebuild 和 incremental update 组合起来，避免每个入口都从零扫描。 |
| UnrealCSharp | `FEditorListener` 在 `OnFilesLoaded()` 后订阅 `OnAssetAdded/Removed/Renamed/UpdatedOnDisk`；每个回调只处理发生变化的 `FAssetData`，把生成与同步收敛到增量事件流里。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:251` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:297` | asset 级增量事件应该成为 editor-side toolchain 的基础设施，而不是每次都重新取全量资产列表。 |
| puerts | `DeclarationGenerator` 的命令入口已经承认“并非所有操作都需要全量跑一遍”，`Puerts.Gen` 同时支持 `FULL` 与 `PATH=` 作用域。虽然它解决的是生成范围，而不是 `BlueprintImpact`，但说明 editor tool contract 已经把作用域控制视为一等能力。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1668` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1686` | 一旦工具要进入 editor 高频使用场景，候选集和执行范围就不应永远隐式等于“全量所有资产”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先为 `BlueprintImpact` 增加一个由 `AssetRegistry` 事件维护的候选资产目录，再逐步追加可选的依赖摘要索引；旧的全量扫描保留为兼容 fallback。 |
| 具体步骤 | 1. 新增 `FAngelscriptBlueprintImpactAssetIndexService`（或等价 `UObject`/service），由 `AngelscriptEditor` 模块持有；第一阶段只维护 `Blueprint` 资产目录：`PackageName/ObjectPath/FAssetData/bOnDisk`，并在 editor 会话中订阅 `OnFilesLoaded`、`OnAssetAdded`、`OnAssetRemoved`、`OnAssetRenamed`、`OnAssetUpdatedOnDisk`。<br>2. 将 `FindBlueprintAssets(...)` 抽成两层：`QueryAllBlueprintAssets()` 作为旧 fallback，`QueryIndexedBlueprintAssets()` 作为新默认路径。若 index 尚未初始化或检测到不一致，仍回退到现有全量 `AssetRegistry.GetAssets(...)`，保证行为兼容。<br>3. 第二阶段在 index 上追加轻量 `FBlueprintImpactAssetDigest`，至少缓存 `ParentClass`、最近一次分析到的 struct/enum/delegate 依赖摘要、以及 package path；`ScanBlueprintAssets(...)` 先用 digest 预筛候选，再只对可能命中的资产执行 `GetAsset()` 精确分析。<br>4. 把 index service 暴露为只读 editor capability，例如 `GetBlueprintImpactAssetIndex()`；未来 `BlueprintImpact` 菜单、dockable panel、`EditorSubsystem` 扩展和 reload 后诊断都消费同一候选目录，而不再各自查询 `AssetRegistry`。<br>5. 保持 `Commandlet` 向后兼容：headless 场景若没有 editor-side index，就继续走当前全量扫描；当未来需要更快的离线路径时，再单独补一个可序列化的 cache 文件，不在第一阶段引入新的持久化格式。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactAssetIndexService.h` 与对应 `Private/BlueprintImpact/AngelscriptBlueprintImpactAssetIndexService.cpp`。 |
| 预估工作量 | L |
| 架构风险 | 如果第一阶段就试图把“依赖摘要”做成唯一真相，容易遇到 `Blueprint` 未编译、摘要过期或 rename 时序带来的漏报；更稳妥的顺序是先补候选目录缓存，再把 digest 作为可回退的预筛层。 |
| 兼容性 | 向后兼容。旧 `ScanBlueprintAssets(...)` 签名、`Commandlet` 输出和现有自动化测试都可以保持；新增 index 只是优化 editor 内候选收集与扩展复用面，未命中 index 时仍可回退到现有全量行为。 |
| 验证方式 | 1. 对同一组 `ChangedScripts`，分别跑“无 index fallback”和“启用 index”两条路径，确认 `Matches`、`CandidateAssets` 子集关系和 `FailedAssetLoads` 统计符合预期。<br>2. 在 editor 内执行资产新增、删除、重命名、磁盘更新，确认 index 能同步变化，且下一次 impact scan 无需重新 `GetAssets(Filter, Assets)` 仍可得到正确结果。<br>3. 保留现有 `Angelscript.Editor.BlueprintImpact.*` 测试，再新增一组 index 同步测试，证明优化是 additive 而非语义替换。 |

### Arch-81：`BlueprintImpact` 请求模型过粗，无法承接 `ContentBrowser`/`BlueprintEditor`/`EditorSubsystem` 的上下文作用域

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `BlueprintImpact` 是否已经具备足够细的 request contract，使 editor 扩展可以表达“只扫当前路径 / 当前资产 / 当前已加载会话 / 指定包范围” |
| 当前设计 | 当前 request 只接受 `ChangedScripts` 和 `bIncludeOnlyOnDiskAssets`，commandlet 也只解析 `ChangedScript=` 与 `ChangedScriptFile=`。扫描端没有 `PackagePath`、`ObjectPath`、`LoadedOnly`、`SelectedAssets`、`MaxAssets` 等作用域字段，因此 editor 内任何想做局部扫描的扩展都只能先跑全局扫描，再自己二次过滤结果。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:34` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:43` — `FBlueprintImpactRequest` 没有包路径、对象路径、加载态或 editor 上下文字段。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:63` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:79` — 命令行只支持 `ChangedScript=` 与 `ChangedScriptFile=` 两种输入。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:252` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:309` — 扫描阶段没有任何“按路径/按资产/按当前 editor context 缩小候选范围”的分支，默认就是全局 `Blueprint` 集合。 |
| 优点 | API 很小，当前 `Commandlet` 和测试写起来直接；调用方不用先理解复杂的 scope 语义。 |
| 不足 | 当前短板不是“参数少一点”，而是 editor 扩展没有能力把自己的上下文传进扫描层。推断：无论未来从 `ContentBrowser_PathViewContextMenu`、`BlueprintEditor` toolbar、右键菜单还是 `UScriptEditorSubsystem` 发起扫描，最后都会被迫绕回“全量请求 + 外部二次裁剪”，这会让 `BlueprintImpact` 很难成为真正可组合的 editor service。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `Puerts.Gen` 的 console command 不只区分 full/non-full，还显式支持 `PATH=`，并把 `SearchPath` 继续传给后续生成流程。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1615` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1631`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1668` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1686`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1700` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1705` | 工具请求如果要服务 editor 交互，就要能表达作用域，而不是只暴露一个全局开关。 |
| UnrealCSharp | `OnCompile(const TArray<FFileChangeData>&)` 直接接收变化文件列表，只分析这些文件并刷新对应动态映射；不是每次都重新处理所有代码。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:228` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:248` | editor toolchain 的请求模型应尽量贴近真实上下文 payload，例如 changed files，而不是总把上下文抹平为全局重扫。 |
| UnLua | `OnAssetAdded`、`OnAssetRemoved`、`OnAssetRenamed`、`OnAssetUpdated` 都直接以单个 `FAssetData` 为输入，增量逻辑天然以“当前资产”作为作用域。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:290` | 若未来要从 `ContentBrowser` 或当前资产上下文触发分析，请求层就该允许显式传入目标资产，而不是强迫调用方事后自己过滤。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持现有 `ChangedScripts` 兼容语义的前提下，把 `FBlueprintImpactRequest` 扩展为可表达 editor 上下文的 scoped request，并为不同 editor surface 提供标准化 builder。 |
| 具体步骤 | 1. 扩展 `FBlueprintImpactRequest`：第一阶段至少新增 `TArray<FName> PackagePaths`、`TArray<FSoftObjectPath> ExplicitBlueprints`、`bool bLoadedBlueprintsOnly`、`int32 MaxAssets`、`FName ContextLabel`；旧 `ChangedScripts` 和 `bIncludeOnlyOnDiskAssets` 保持不变。<br>2. 把当前候选解析抽成 `ResolveCandidateAssets(const FBlueprintImpactRequest&)`：优先处理 `ExplicitBlueprints`，其次处理 `PackagePaths`/`LoadedBlueprintsOnly`，最后才回落到全局候选集；这样 `ContentBrowser`、`BlueprintEditor`、`EditorSubsystem` 都可以共用同一条解析链。<br>3. 新增一组 request builder/helper，例如 `MakeImpactRequestFromPackagePaths(...)`、`MakeImpactRequestFromBlueprintSelection(...)`、`MakeImpactRequestFromLoadedEditors(...)`；builder 放在 editor module/service 层，而不是让每个 surface 自己拼装字段。<br>4. 扩展 `UAngelscriptBlueprintImpactScanCommandlet::Main()`，支持可选参数如 `PackagePath=`、`ObjectPath=`、`LoadedOnly=`、`MaxAssets=`；若用户仍只传 `ChangedScript=`/`ChangedScriptFile=`，行为保持当前版本一致。<br>5. 通过 `UScriptEditorSubsystem` 或新的 impact service facade 暴露这些 builder，让项目方自己的 toolbar、右键菜单、批处理按钮可以直接复用 scoped request，而不必 include scanner internals 或自己后过滤 `ScanResult`。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`；如已引入 impact service，则同步修改对应 service 文件。 |
| 预估工作量 | M |
| 架构风险 | 作用域字段一旦设计混乱，容易让调用方误解不同 scope 的优先级，出现“传了路径又传了显式资产到底听谁”的歧义；因此第一阶段需要把优先级和组合规则写死，并在日志/结果中回显实际生效的 scope。 |
| 兼容性 | 向后兼容。旧请求结构的既有字段继续有效，旧 commandlet 参数无需修改；新字段都是 additive contract，只提升 editor 内复用能力，不会改变现有 full scan 或 changed-script scan 的默认行为。 |
| 验证方式 | 1. 保留现有 `ChangedScript=` 路径，确认结果与当前版本一致。<br>2. 新增 `PackagePath=` 或 `ObjectPath=` 测试，验证 scoped request 只扫描命中的局部范围，不会再无条件遍历全量 `Blueprint`。<br>3. 用一个最小 `UScriptEditorSubsystem` 或 `ContentBrowser` 扩展构造 path-scoped request，确认无需重写 scanner 内核也能发起局部 impact scan。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-80 | `BlueprintImpact` 缺少 `AssetRegistry` 驱动的增量候选索引，changed-script 扫描仍会落到全量候选遍历 | candidate index service + digest prefilter | 高 |
| P1 | Arch-81 | `BlueprintImpact` 请求模型过粗，无法承接 `ContentBrowser`/`BlueprintEditor`/`EditorSubsystem` 的局部作用域 | scoped request contract + request builders | 高 |

---

## 架构分析 (2026-04-10 01:23:24)

### Arch-82：内建 editor workflow 仍以 transient popup 为主，缺少可驻留的 workspace surface

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 核心编辑器交互是挂到标准 editor host surface，还是每次临时拼装 popup / modal widget |
| 当前设计 | `AngelscriptEditor` 已经接上 `ToolMenus`，但真正的创建、选择、参数填写等核心 workflow 仍主要落在 `CreateModalSaveAssetDialog`、`PushMenu`、`EditorAddModalWindow` 这类一次性窗口上；debug server 和 runtime bridge 也是直接跳进这些 popup helper。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:96` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:108` — `ForceEditorWindowToFront()` 通过 `HACK_ForceToFront()` 为后续 popup 抢前台。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:409` — runtime bridge 直接把“列资产 / 创建 Blueprint”绑到 `ShowAssetListPopup()` 与 `ShowCreateBlueprintPopup()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:471` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:537` — 创建流程依赖 `CreateModalSaveAssetDialog(...)`，结束后立即打开 asset editor。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:568` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:671` — 资产列表通过 `CreateAssetPicker(...)` 临时嵌入 `FMenuBuilder`，再用 `FSlateApplication::PushMenu(...)` 弹出。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:53` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:55` — 参数表单是即时创建的 `IStructureDetailsView`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:166` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:179` — 表单最终仍走 `SWindow` + `GEditor->EditorAddModalWindow(Window)`。 |
| 优点 | 交付速度快，可以直接复用 `ContentBrowser` picker、`PropertyEditor` details view、`UAssetEditorSubsystem` 打开资产，不需要先搭一套完整 toolkit。 |
| 不足 | 这些 helper 没有形成“可聚焦、可复用、可承载状态”的正式 workspace。推断：一旦要把 `BlueprintImpact`、批量资产创建、参数化脚本工具、debug 结果面板整合成长期存在的 editor 能力，当前的 popup-first 结构会让状态保持、跨入口复用、项目侧二次扩展都变得困难。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块在 `OnPostEngineInit()` 后初始化 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar`，把功能嵌进主工具栏、Blueprint editor toolbar 和 graph node context menu，而不是把主要 workflow 都导向临时窗口。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:140`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:135` | 让用户在当前 editor surface 完成操作，popup 只保留给“About”这类低频辅助窗口。 |
| puerts | `FGenDTSCommands` 先定义正式 command，`DeclarationGenerator` 再把它挂到 `LevelEditor` 主菜单和 toolbar；生成完成后用 `FSlateNotificationManager` 给出非阻塞反馈，而不是再开一个 modal。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp:13` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp:17`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1573` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1605`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1625` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1631` | 一次性工具也可以走“host surface 触发 + notification 收尾”，不必强耦合到弹窗。 |
| UnrealCSharp | 模块统一初始化 `PlayToolBar` 与 `BlueprintToolBar`；`BlueprintToolBar` 通过 `BlueprintEditorModule` 的 extender 挂到 asset editor 内部，`PlayToolBar` 则常驻 `LevelEditor.LevelEditorToolBar.PlayToolBar`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:193` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:206`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:17` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:31`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:40`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:203` | 把“入口挂载”和“具体操作 UI”绑定到已有 editor toolkit，后续更容易继续叠加 code analysis、override、new class 等能力。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 popup helper 收敛到一个正式的 `workspace service` 之下，优先接入 `LevelEditor` / `BlueprintEditor` 现有 surface；popup API 保留为兼容入口。 |
| 具体步骤 | 1. 新增 `FAngelscriptEditorWorkspaceService` 或等价 feature，对外暴露 `OpenAssetWorkspace(...)`、`OpenCreateAssetFlow(...)`、`OpenPromptHost(...)` 这类 service API；由 `FAngelscriptEditorModule` 持有并在 `StartupModule()` / `ShutdownModule()` 统一管理。<br>2. 第一阶段不做大爆炸重写：保留 `ShowAssetListPopup()`、`ShowCreateBlueprintPopup()`、`FScriptEditorPrompts::ShowPromptForStruct()` 原签名，但内部改为“优先聚焦 workspace / host surface，必要时再回退到旧 popup”。<br>3. 在 `ToolMenus` 之外新增一个正式 surface。优先级建议：`LevelEditor` 工具栏 combo 或 `NomadTab` 形式的 `Angelscript Workspace`；它至少要能承载资产列表、创建参数表单和未来的 `BlueprintImpact` 结果列表。<br>4. 把 debug server bridge 从“静态 helper 直接开窗”改成“发请求给 workspace service”；这样项目方未来若想从 `UEditorSubsystem`、自定义 toolbar 或右键菜单复用同一流程，只需调 service，不必复制 popup 实现。<br>5. 为一次性完成的动作补 `notification` 收尾，例如创建成功、列表刷新、生成完成；只有需要阻塞用户决策的场景才保留 modal。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Workspace/*.h` 与对应 `*.cpp`。 |
| 预估工作量 | L |
| 架构风险 | 如果第一阶段就强行把所有 popup 全部替换成全新 tab/toolkit，容易和现有 debug server、runtime delegate、菜单扩展产生时序耦合；更安全的顺序是先引入 workspace service，再逐个把旧入口转发过去。 |
| 兼容性 | 向后兼容。旧静态 helper、旧 runtime bridge 和现有脚本侧菜单扩展都可以继续存在，只是其实现改为复用新的 workspace/service；项目和脚本调用方无需立即改代码。 |
| 验证方式 | 1. 从现有 `Tools` 菜单、debug server 调用和脚本 prompt 三个入口分别触发 workflow，确认最终聚焦到同一套 service/surface。<br>2. 验证资产打开、资产创建、参数填写后执行行为与旧版本一致。<br>3. 重复触发多次同一动作，确认不会出现多个重复 popup 或丢失状态的临时 widget。 |

### Arch-83：`ContentBrowserDataSource` 仍是 root-only 扁平投影，尚未形成可扩展的 virtual tree

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本资产在 `ContentBrowser` 中的路径模型，是否支持层级浏览、路径作用域和未来的项目侧扩展 |
| 当前设计 | `UAngelscriptContentBrowserDataSource` 把所有对象统一投影到 `/All/Angelscript/<AssetName>` 这一层；过滤逻辑只在根路径 `/` 生效，`EnumerateItemsAtPath()`、`EnumerateItemsForObjects()`、路径反查等关键接口仍是空实现或恒定返回。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:29` — `CreateAssetItem()` 固定生成 `/All/Angelscript/<AssetName>` 文件项。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:31` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:48` — `CompileFilter()` 只有 `InPath == "/"` 且存在 `ClassFilter` 时才继续处理。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:81` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:120` — 匹配项全部来自 `GetObjectsWithOuter(FAngelscriptEngine::Get().AssetsPackage, Assets)`，没有 folder 维度。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:124` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:136` — `EnumerateItemsAtPath()` 为空，`EnumerateItemsForObjects()` 返回 `false`，`DoesItemPassFilter()` 直接返回 `true`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:249` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:256` — `Legacy_TryConvertPackagePathToVirtualPath()` 与 `Legacy_TryConvertAssetDataToVirtualPath()` 仍未实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h:66` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h:69` — 头文件虽然声明了 `CreateFolderItem(...)`，但当前 `cpp` 没有对应 folder item 构建路径。 |
| 优点 | 代码量很小，可以快速把 `AssetsPackage` 下的对象显示到 `ContentBrowser`，适合验证“能否显示脚本资产”这一最小目标。 |
| 不足 | 这不是一个真正的 path-aware `ContentBrowserDataSource`，而是“根路径的一次性 flat projection”。推断：未来要支持多类脚本资产、多 root、路径作用域扫描、`Add New` 上下文菜单或项目方自定义分支时，当前结构几乎没有可复用的 virtual tree contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `UDynamicDataSource` 在 filter 中同时维护 `Classes` 和 `Folders`，初始化时注册 `ContentBrowser.AddNewContextMenu` 并构建 root virtual tree；后续 `CompileFilter()`、`EnumerateItemsAtPath()`、`Legacy_TryConvert*()` 都围绕 virtual/internal path 转换和 folder/file 枚举展开。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ContentBrowser/DynamicDataSource.h:12` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ContentBrowser/DynamicDataSource.h:22`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:109` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:366`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:409` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:438`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:666` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:727` | 一旦决定走 virtual data source，就应该把路径树、folder/file 枚举、路径转换和上下文菜单一起补齐，而不是只投影根层文件。 |
| puerts | `DeclarationGenerator` 的 editor contract 从一开始就保留了 `PATH=` 作用域，工具流程不会把所有对象都压扁成一个无路径语义的入口。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1615` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1623`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1675` 到 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1686` | 即使不是 `ContentBrowserDataSource`，editor tool contract 也应把 path scope 当作一等公民，而不是事后补救。 |
| UnrealCSharp | `DynamicNewClassContextMenu` 会读取当前选中的 path，在对应 branch 下创建 `New Dynamic Class...` 菜单项。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:7` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:58` | 只有 data source 先提供稳定的 branch/path 模型，后续的“按当前路径创建新资产”扩展才有可靠落点。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `AngelscriptData` 从“单层 flat view”升级成真正的 virtual tree，再把未来的 `Add New`、局部扫描和项目扩展都挂在这棵树上；旧 flat path 保留为兼容别名。 |
| 具体步骤 | 1. 在 `UAngelscriptContentBrowserDataSource` 中引入显式 path registry / tree state，第一阶段至少补出 `BuildRootPathVirtualTree` 等价能力和 `CreateFolderItem(...)` 的真实实现；初始 branch 可以按脚本资产大类或脚本源相对路径组织，而不是只有 `/All/Angelscript/<AssetName>`。<br>2. 重写 `CompileFilter()` 与 `EnumerateItemsAtPath()`：支持 `InPath` 的 recursive / non-recursive 查询，同时把 folder/file 两种 item 都放进 compiled filter，而不是只在根路径一次性列出所有文件。<br>3. 实现 `EnumerateItemsForObjects()`、`DoesItemPassFilter()`、`Legacy_TryConvertPackagePathToVirtualPath()`、`Legacy_TryConvertAssetDataToVirtualPath()`，让 `ContentBrowser` 的 object 定位、路径跳转和右键上下文能基于稳定 virtual path 工作。<br>4. 在 data source 或其上层 service 中增加可选注册点，例如 `RegisterVirtualBranch(...)` / `RegisterAssetFamilyProvider(...)`；未来项目方 editor module 或 `UScriptEditorSubsystem` facade 可以扩展自己的 branch，而不必改核心 `cpp`。<br>5. 迁移阶段保留当前 `/All/Angelscript/<AssetName>` 作为兼容 alias；旧 debug server、旧 popup 和已有引用都先继续解析这个别名，等 branch 模型稳定后再逐步把 UI 默认入口切到层级树。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ContentBrowser/*.h` 与对应 `*.cpp`。 |
| 预估工作量 | L |
| 架构风险 | 如果第一版 branch 规则选错，后续 path alias 会变多，容易引入“旧 flat path 与新 branch path 指向同一对象”的歧义；因此需要先把 canonical path 和兼容 alias 的优先级约定清楚。 |
| 兼容性 | 向后兼容。旧 flat path 与现有 asset 展示行为可继续保留，只是新增 branch-aware 能力；项目和脚本用户无需立即改现有调用。 |
| 验证方式 | 1. 在 `ContentBrowser` 中验证 root、子路径、递归搜索、按对象定位四条路径都能正确枚举 script asset。<br>2. 保留旧 `/All/Angelscript/<AssetName>` 入口，确认旧引用仍能定位到同一对象。<br>3. 增加一组 path-aware 自动化测试，覆盖 `EnumerateItemsAtPath()`、`Legacy_TryConvert*()` 和未来 branch 注册后的可见性。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-82 | 核心 editor workflow 仍以 transient popup 驱动，缺少可驻留的 workspace surface | workspace service + host surface 接入 | 高 |
| P1 | Arch-83 | `ContentBrowserDataSource` 仍是 root-only 扁平投影，缺少可扩展 virtual tree | path-aware data source + branch registry | 高 |

---

## 架构分析 (2026-04-10 01:42:36)

### Arch-84：脚本侧 editor API 仍以 helper/wrapper 暴露，缺少面向对象的 host/mixin contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本侧 editor 能力是围绕真实 UE editor object/capability 暴露，还是主要停留在 static helper / namespace wrapper 层 |
| 当前设计 | 当前高频 editor API 仍主要通过 `BlueprintCallable` static wrapper 暴露给脚本，原本更接近对象化扩展的 `ScriptMixin` / `ScriptCallable` 设计被注释掉；`UScriptEditorSubsystem` 本身也没有继续提供更具体的 capability object。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/BlueprintMixinLibrary.h:5` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/BlueprintMixinLibrary.h:18` — `ScriptMixin = "UBlueprintCore UBlueprint"` 与 `ScriptCallable` 都被注释，当前只保留 `BlueprintCallable static UClass* GetGeneratedClass(UBlueprintCore* Blueprint)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:6` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:20` — 注释直接写明“这些函数默认是 blueprint internal，但为了 Angelscript 需要暴露”，实际只提供一个通用 `GetEditorSubsystem(...)` wrapper。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h:11` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h:77` — 明确以“在脚本里创建 `Editor::` namespace”方式暴露一组静态函数。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:10` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:80` — `AssetTools::` 也是同样的 namespace-wrapper 形态。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:47` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:58` — `UScriptEditorSubsystem` 只继续暴露 `BP_ShouldCreateSubsystem/BP_Initialize/BP_Deinitialize/BP_Tick` 生命周期事件，没有承载 typed editor capability。 |
| 优点 | 这种做法覆盖单点能力很快，新增一个 editor helper 的接入成本低，也能直接复用已有 `BlueprintCallable` 暴露路径。 |
| 不足 | 推断：当前脚本 API 更像“把零散 UE editor helper 搬进脚本命名空间”，而不是“给脚本一个可组合的 editor object model”。这会导致 `UBlueprint`、`UEditorSubsystem`、asset workflow、watcher 等能力缺少统一宿主与状态表面，项目方即使能写 `UScriptEditorSubsystem` 或 menu extension，也往往还得回落到 scattered wrapper 才能完成真实 workflow。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `Blueprint` 侧扩展直接围绕真实上下文对象工作。`BuildToolbar(...)` 接收 `UObject* InContextObject`，立即转成 `UBlueprint`；后续 `BindToLua_Executed()` 继续基于 `Blueprint`、`GeneratedClass` 与当前 `FBlueprintEditor` 执行真正操作。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:75`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:138` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:199` | editor API 的高价值入口应尽量贴近真实 host object，而不是先退化成若干全局 helper。 |
| puerts | 目录监听没有做成一组分散 static helper，而是公开成 stateful `UPEDirectoryWatcher`：既有 `Watch/UnWatch`，也有 `BlueprintAssignable OnChanged`，脚本面对的是能力对象而不是命名空间。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:17` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:32`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:74` | 只要能力带状态、句柄或事件，就更适合建模为 `UObject` service，而不是继续追加新的 static wrapper。 |
| UnrealCSharp | `Blueprint` editor 集成由 typed feature object 承载。`FUnrealCSharpBlueprintToolBar` 在 public 头中直接持有 `TWeakObjectPtr<UBlueprint>`，初始化时注册 `BlueprintEditor` extender，生成菜单时再围绕当前 `UBlueprint` 计算 override/code-analysis 行为。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ToolBar/UnrealCSharpBlueprintToolBar.h:21` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ToolBar/UnrealCSharpBlueprintToolBar.h:41`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:40`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:157` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:203` | 与其让脚本侧依赖越来越多的 `Editor::` / `AssetTools::` wrapper，不如先定义 capability-specific host object，再让菜单/toolbar/context menu 共享这层对象模型。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 wrapper 作为兼容层，但把未来 editor API 的主增量切到“typed mixin + stateful service object”两类 contract 上。 |
| 具体步骤 | 1. 第一阶段先恢复最小 mixin 路径：围绕 `UBlueprintCore/UBlueprint` 新增真正的 `ScriptMixin` contract，把当前 `GetGeneratedClass(...)`、未来 Blueprint editor 上下文查询、资产打开/定位等与 blueprint 实体强相关的 helper 收敛到同一 object-facing API。<br>2. 对 watcher、asset workflow、impact scan、reload bridge 这类有状态能力，不再继续塞进 `Editor::` / `AssetTools::` namespace，而是新增 `UObject` service，例如 `UAngelscriptEditorWatchService`、`UAngelscriptAssetWorkflowService`、`UAngelscriptBlueprintEditorHost`；由 `UScriptEditorSubsystem` 提供 getter 或 service locator。<br>3. `UEditorSubsystemLibrary`、`UEditorStatics`、`UAssetToolsStatics` 先保留原签名，但内部逐步转发到新的 mixin/service；这样旧脚本还能继续跑，新脚本则可以直接使用对象化 contract。<br>4. 建一条明确的增量规则：纯无状态查询仍可保留轻量 helper；凡是需要 `ContextObject`、selection、delegate、异步状态、注册句柄的 editor 能力，后续一律优先落到 mixin 或 service object，而不是再加新的 static library。<br>5. 增加一组最小验证脚本样例：一个 `UScriptEditorSubsystem` 子类通过 service 获取 watcher 或 asset workflow，一个 `UBlueprint` 相关菜单动作通过 mixin 完成真实操作，证明常见 editor 扩展不再必须回落到 legacy wrapper。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/BlueprintMixinLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Mixins/*.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/Services/*.h` 与对应 `Private/*.cpp`。 |
| 预估工作量 | M |
| 架构风险 | 如果第一版 mixin/service 划分边界不清，容易出现“一部分能力仍在 wrapper，一部分能力已经对象化”的重复暴露；因此第一阶段应只选 `Blueprint` 与一个 stateful service 做样板，不要一次迁移所有 helper。 |
| 兼容性 | 向后兼容。旧的 `BlueprintCallable` wrapper 与 `GetEditorSubsystem()` 都可保留，新增 mixin/service 只是提高新脚本与项目扩展的可组合性，不要求现有脚本立刻迁移。 |
| 验证方式 | 1. 保留现有 wrapper 调用路径，确认行为不变。<br>2. 新增一个基于 `UBlueprint` mixin 的 editor 菜单动作，验证无需再显式调用 legacy `BlueprintMixinLibrary` helper 也能获取 `GeneratedClass` 并完成操作。<br>3. 新增一个通过 `UScriptEditorSubsystem` 获取 stateful service 的脚本样例，验证 editor session 内的事件/状态可通过能力对象消费，而不是只能依赖纯 static helper。 |

### Arch-85：reflection 可见的 editor primitive 仍埋在 Private 头，native 与 script 扩展面失衡

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 被设计为脚本/反射层可用的 editor primitive，是否同时对 native 项目扩展提供稳定的 public C++ contract |
| 当前设计 | 当前存在一批“从反射角度可见、从物理头文件角度却仍是 private”的 editor primitive：`UEditorStatics`、`UAssetToolsStatics`、`UScriptableFactory` 都定义在 `Private/FunctionLibraries` 下；模块级 helper 也继续停留在 `Private/AngelscriptEditorModule.h`。结果是脚本侧至少能沿反射路径消费这些能力，但 native 项目模块拿不到对应的稳定 public header，只能复制逻辑或依赖 private include。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h:15` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h:77` — `UEditorStatics` 是标准 `UCLASS/UFUNCTION` 反射面，但物理位置仍在 `Private/FunctionLibraries`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:14` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:80` — `UAssetToolsStatics` 同样是可反射 API，却没有 public 头出口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h:8` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h:32` — `UScriptableFactory` 明显是扩展 primitive，但仍埋在 private 路径。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:6` 到 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:60` — editor 模块自己的 helper 也只存在于 private 头。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12` 到 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:40` — 模块已经把 `UnrealEd`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`AssetTools` 等依赖公开出去，但并没有与之配套的最小 public capability facade。 |
| 优点 | 对内部实现来说，这种布局维护成本低，作者不必提前承诺哪些类型是长期支持的 public API；同一批 primitive 也能先在插件内部快速迭代。 |
| 不足 | 推断：当前扩展面在“script 可见性”和“native 可见性”之间出现了结构性不对称。项目方若要做 hybrid editor extension，例如 native 模块提供 toolbar/asset workflow，脚本继续消费同一 factory 或 helper，就缺少一套可共享的稳定 contract；而 `Build.cs` 的 public 依赖成本已经付出，却没有转化成真正可复用的 public surface。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把目录监听能力明确放到 `Public/PEDirectoryWatcher.h`，类型本身就是 `PUERTSEDITOR_API` 的 public `UObject`，既能给脚本用，也能被 native 项目代码直接 include。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:17` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:32`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14` 到 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:74` | 一旦某个 primitive 预期要被项目侧扩展复用，就应让“public 头可见性”和“反射可见性”保持一致。 |
| UnrealCSharp | 关键 editor feature object 都有对应 public 头：模块宿主、`BlueprintToolBar`、`DynamicDataSource` 都在 `Public/` 下声明，模块内部只是实例化并编排它们。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:10` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:56`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ToolBar/UnrealCSharpBlueprintToolBar.h:5` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ToolBar/UnrealCSharpBlueprintToolBar.h:41`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ContentBrowser/DynamicDataSource.h:41` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ContentBrowser/DynamicDataSource.h:148`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61` 到 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:107` | public surface 先稳定“类型边界”，private `cpp` 再负责实现细节，这样项目方 native 扩展能与模块内部共享同一套能力对象。 |
| UnLua | 虽然不是所有 helper 都公开，但它至少把 `IntelliSense` 相关 editor tool surface 显式放进 `Public/`：`FUnLuaIntelliSenseGenerator` 与 `UnLua::IntelliSense::*` 都可直接被外部 include，模块内部也围绕这些 public 类型编排。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSenseGenerator.h:22` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSenseGenerator.h:66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSense.h:19` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSense.h:52`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:98` 到 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:105` | 即使模块内部仍保留 private helper，也可以先把“明确想让项目复用”的 editor tool contract 独立成 public 头，避免所有能力都锁死在模块内部。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“打算让脚本与项目扩展共同消费”的 editor primitive 从 private 物理布局中剥离出来，建立最小稳定 public facade；private 头继续承载实现细节与 legacy 转发。 |
| 具体步骤 | 1. 先做一次 primitive 分级：把 `UScriptableFactory`、未来要给项目方复用的 service object、以及确定需要原生 include 的 helper 列为 `Stable Public Editor Surface`；其余仍只服务模块内部的 wrapper 继续留在 private。<br>2. 第一阶段优先处理最典型的扩展 primitive：把 `UScriptableFactory` 迁到 `Public/FunctionLibraries` 或更明确的 `Public/Factories`，并新增窄 public facade，例如 `UAngelscriptEditorAssetToolsLibrary`、`UAngelscriptEditorNavigationLibrary` 或等价 service getter，避免外部直接 include `FAngelscriptEditorModule` private helper。<br>3. 对已经被脚本使用的 legacy 类，尽量保持类名与函数签名不变；如果需要移动头文件路径，就增加 public forwarding header，兼容现有 include 与 UHT 生成路径，避免大爆炸重命名。<br>4. 利用当前 `Build.cs` 已经公开 editor 依赖这一现实，先发布极小 public facade 集合，不再继续扩大依赖面；待调用方迁移到正式 facade 后，第二阶段再反向收紧不必要的 `PublicDependencyModuleNames`。<br>5. 增加一个最小 native 扩展示例或测试模块，只使用 `AngelscriptEditor/Public` 头就能注册一个 custom factory、访问一个 watcher/service 或触发一个 editor workflow，证明 plugin 已具备 script/native 共用的稳定扩展面。 |
| 涉及文件 | 修改 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/EditorStatics.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`；新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/*.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/Factories/*.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/IAngelscriptEditorModule.h` 或等价 public facade。 |
| 预估工作量 | M |
| 架构风险 | 移动 `UCLASS` 头文件会牵涉 include 路径、generated header 与模块导出宏；若第一阶段直接大范围搬迁，容易引入构建噪音。更稳妥的顺序是先增 public forwarding facade，再逐步收敛 private 直接引用。 |
| 兼容性 | 可向后兼容。第一阶段只新增 public facade 或 forwarding header，不必立刻删除旧 private 实现；现有脚本反射名、函数签名与模块内部行为都可以保持不变。 |
| 验证方式 | 1. 新增一个最小 native 测试模块，只 include `AngelscriptEditor/Public` 头，验证无需 private include 就能访问选定的 editor primitive。<br>2. 重新编译 `AngelscriptEditor` 与现有脚本侧调用，确认 `UScriptableFactory`、`Editor::` / `AssetTools::` 旧入口仍可工作。<br>3. 检查生成文件与 include 路径，确保 public facade 生效后没有强迫外部项目依赖 `Private/` 目录。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-84 | 脚本侧 editor API 仍以 helper/wrapper 暴露，缺少面向对象的 host/mixin contract | API 形态重构 + capability object 引入 | 高 |
| P2 | Arch-85 | reflection 可见的 editor primitive 仍埋在 Private 头，native 与 script 扩展面失衡 | public facade 收敛 + private/public 边界校正 | 中 |
