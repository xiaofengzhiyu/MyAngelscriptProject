# Angelscript 启动编译错误弹窗 UI 对齐计划

## 背景与目标

### 背景

当前插件并不是完全缺少“启动脚本编译失败时弹出 Slate 窗口”的能力。现状已经确认如下：

- 当前项目在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 的 `InitialCompile()` 失败分支中，已经会创建标题为 `Angelscript Compile Errors` 的模态 `SWindow`，并在修复脚本后通过热重载轮询自动关闭。
- 通过一次真实手工复现，已经确认普通编辑器启动路径下，当前项目确实能弹出该窗口；因此问题不在“有没有窗口”，而在“当前窗口 UI 是否已经对齐 UEAS2 的额外能力”。
- 本机 `J:\UnrealEngine\UEAS2` 参考实现已经确认包含额外 UI 能力：桌面平台使用只读多行文本框、提供 “Open Angelscript workspace (VS Code)” 按钮、并在非桌面平台退化为 `FMessageDialog` 后直接退出。
- 当前项目的 `UAngelscriptSettings` 里还没有 `VSCodeWorkspacePath` 这类工作区路径配置，因此即便直接照搬按钮逻辑，也缺少稳定的配置落点。

### 目标

将当前插件的“启动阶段脚本初编译失败”弹窗，**精确补齐到已确认的 UEAS2 额外 UI 能力**，但不顺手扩展到其他未确认需求。最终目标仅包含以下四点：

1. 桌面平台弹窗从 `STextBlock` 升级为**只读多行文本框**，允许更稳定地查看、复制和选择错误内容。
2. 弹窗增加 **Open Angelscript workspace (VS Code)** 按钮；若配置了 `VSCodeWorkspacePath`，优先打开该相对工作区，否则回退到项目 `Script/` 目录。
3. 非桌面平台不再尝试创建 Slate 模态窗口，而是使用 `FMessageDialog` 显示错误后退出。
4. 保持当前已有行为不变：`commandlet` / `-as-exit-on-error` 仍直接退出；桌面平台模态窗口仍依赖热重载轮询，在脚本修复后自动关闭。

## 范围与边界

- 只处理 `InitialCompile()` 的启动失败路径，不改普通热重载失败、运行中错误通知、Message Log 或调试器面板。
- 不在本计划内引入新的 VS Code Source Link 工作流，也不补 `bOpenFolderOnVSCodeSourceLinks` 等与“点击源码链接”相关的额外设置。
- 不改变 `commandlet`、`-as-exit-on-error`、`DebugServer` 消息泵、`CheckForHotReload(ECompileType::FullReload)` 的既有语义。
- 不把这项工作扩展为“完整编辑器错误体验重构”；只对齐当前已确认的 UEAS2 UI 差异。
- 所有路径配置继续通过 `AgentConfig.ini` / `ProjectDir` / 引擎配置解析，禁止在代码或文档中写死本机绝对路径。

## 当前事实状态快照

### 当前项目基线

- 启动失败窗口实现位于 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`。
- 当前桌面平台 UI 结构是 `SWindow + SBorder + SScrollBox + STextBlock`，每次 modal tick 都直接覆写错误文本。
- 当前弹窗没有 VS Code 工作区按钮，也没有针对非桌面平台的显式 fallback。
- 当前 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` 中不存在 `VSCodeWorkspacePath`。

### UEAS2 参考基线

- 参考实现位于 `Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp`。
- 桌面平台 UI 使用 `SMultiLineEditableTextBox` 且只在文本变化时更新内容，避免在 modal loop 中持续重置选择状态。
- 参考实现额外提供 “Open Angelscript workspace (VS Code)” 按钮，按 `VSCodeWorkspacePath` 或 `Script/` 目录决定打开目标。
- 参考实现对非桌面平台使用 `FMessageDialog` 显示错误，然后直接 `RequestExit(true)`。

## 影响范围

本次对齐预计涉及以下文件与操作：

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
  - 启动失败分支的弹窗逻辑对齐；必要时将 UI 构建逻辑从巨型函数中抽出，避免继续膨胀该文件。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`
  - 新增 `VSCodeWorkspacePath` 配置项，作为 VS Code 工作区按钮的正式配置来源。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptStartupCompileErrorDialog.h`
  - 新增；封装启动编译错误弹窗的 UI 模型/帮助函数，降低 `AngelscriptEngine.cpp` 的 UI 负担。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptStartupCompileErrorDialog.cpp`
  - 新增；承接桌面/非桌面分支、错误文案构建、VS Code 打开目标解析与按钮回调。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStartupCompileErrorUiTests.cpp`
  - 新增；为启动错误 UI 的可测试 helper 提供 `Angelscript.CppTests.*` 回归。

如果执行阶段证明 UI 逻辑必须留在 `AngelscriptEngine.cpp` 才能最小化风险，则允许取消新增 `AngelscriptStartupCompileErrorDialog.*`，但必须保留同等可测试边界，不能把全部行为继续塞回匿名 lambda 中。

## 分阶段执行计划

### Phase 1：固定配置入口与可测试边界

> 目标：先把“按钮打开什么路径”“哪些 UI 逻辑值得抽出来测试”固定下来，避免后续把对齐工作做成不可验证的 UI 拼接。

- [ ] **P1.1** 为启动错误弹窗补齐 VS Code 工作区配置入口
  - 当前项目只有基础 `UAngelscriptSettings`，但缺少 `VSCodeWorkspacePath`，而参考实现的 VS Code 按钮正是依赖这个配置决定工作区或 `Script/` 目录落点。
  - 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` 中增加最小必要的编辑器配置项：`VSCodeWorkspacePath`。配置必须保持“相对项目根目录”的约束，不能引入绝对路径字段。
  - 这一步只补**启动错误弹窗需要的最小设置能力**，不顺手搬运 UEAS2 里与源码链接、Source Navigation 相关的其它编辑器设置。
  - 完成判据：运行时代码可以通过 `ConfigSettings` 稳定解析“配置了工作区文件”和“未配置工作区文件”两种情况，后续按钮逻辑有正式设置来源。
- [ ] **P1.1** 📦 Git 提交：`[Angelscript] Feat: add startup error dialog workspace setting`

- [ ] **P1.2** 为启动错误 UI 建立清晰的 helper / presenter 边界
  - 当前实现把桌面平台窗口创建、错误文案拼接、modal tick 更新、热重载轮询全部塞在 `InitialCompile()` 的本地 lambda 中，不利于补齐额外 UI，也不利于写稳定测试。
  - 优先把“错误文案构建”“VS Code 打开目标解析”“桌面/非桌面分支选择”“桌面内容区 widget 组装”拆到 `AngelscriptStartupCompileErrorDialog.*` 或同等清晰的私有 helper 中，让 `InitialCompile()` 只负责触发时机与成功/退出状态管理。
  - 如果最终判断拆文件风险高，也至少要把上述逻辑抽成具名私有函数，确保后续测试可以直接覆盖，而不是依赖完整启动流程才能验证每个细节。
  - 完成判据：`InitialCompile()` 的失败分支只保留 orchestration；UI 构建和路径解析有独立可读边界，执行者不需要在匿名 lambda 里继续堆行为。
- [ ] **P1.2** 📦 Git 提交：`[Angelscript] Refactor: isolate startup compile error dialog helpers`

### Phase 2：对齐桌面与非桌面 UI 行为

> 目标：在不改变现有启动/退出语义的前提下，把当前项目的 UI 行为精确补齐到已确认的 UEAS2 差异。

- [ ] **P2.1** 将桌面平台错误展示从 `STextBlock` 升级为只读多行文本框
  - 参考实现不是简单换一个控件名字，而是借助只读 `SMultiLineEditableTextBox` 让用户可以更稳定地滚动、选择和复制错误内容；同时它只在错误文本变化时才刷新内容，避免 modal tick 过程中持续打断选择状态。
  - 当前项目需要把 `STextBlock` 替换为只读 `SMultiLineEditableTextBox`，并补上“仅在错误文案变化时调用 `SetText`”的保护逻辑；这一步属于 UI 语义对齐，不是纯视觉替换。
  - 执行时必须显式设计“上一版错误文案”的状态归属，不能把比较逻辑留成口头约定。若采用 `AngelscriptStartupCompileErrorDialog.*`，则由 dialog presenter / view-model 持有上一版 `FText` 或 `FString`；若保留在 `InitialCompile()` 内，则必须使用稳定的具名状态对象或 lambda 捕获共享状态，保证 modal tick 间比较逻辑可持续生效。
  - 现有 `SWindow` 标题、大小、`AddModalWindow()`、热重载轮询和成功后自动关闭逻辑都应保持不变，避免把 UI 对齐变成流程改写。
  - 完成判据：桌面平台 UI 结构与参考实现一致；同一轮 modal loop 中若错误文本未变，不应重复刷新文本框内容。
- [ ] **P2.1** 📦 Git 提交：`[UI] Feat: align startup compile error text widget with UEAS2`

- [ ] **P2.2** 补齐 VS Code 工作区按钮行为
  - 在桌面平台弹窗底部增加 “Open Angelscript workspace (VS Code)” 按钮，按钮文案与参考实现保持一致，避免后续文档或经验口径漂移。
  - 目标路径解析规则必须精确对齐：若 `VSCodeWorkspacePath` 为空，则打开项目 `Script/` 目录；若配置了 `VSCodeWorkspacePath`，则按“项目相对路径”解析为工作区文件并传给 `code` 命令。
  - 为了让这条行为可测，不要把 `FPlatformMisc::OsExecute(...)` 直接写死在不可替换的按钮 lambda 里；需要通过 helper 封装或轻量依赖注入，把“准备打开哪个路径”与“实际执行外部进程”分开。
  - 完成判据：桌面平台弹窗具备按钮；按钮路径选择逻辑可以在自动化测试里验证，不需要真的启动 VS Code 才能断言行为正确。
- [ ] **P2.2** 📦 Git 提交：`[UI] Feat: add VS Code workspace action to startup error dialog`

- [ ] **P2.3** 补齐非桌面平台 fallback
  - 参考实现会在 `!PLATFORM_DESKTOP` 下跳过 Slate 模态窗口，改为弹出 `FMessageDialog` 后退出；当前项目缺少这条显式 fallback。
  - 这一步要补的是“平台分支行为”，不是额外的跨平台 UI 框架。桌面平台继续维持 Slate 模态窗口；非桌面平台仅走 message dialog + exit，不引入额外重试界面。
  - 平台判断方式必须在实现前写死为与 UEAS2 对齐的编译期分支（优先按参考实现使用 `#if !PLATFORM_DESKTOP` / `#else` 结构），不要临时改成运行时探测或 unattended 判定，避免把“平台能力判断”与“命令行退出策略”混在一起。
  - 需要确保这条分支与当前 `commandlet` / `-as-exit-on-error` 的退出路径没有语义冲突：前者是“普通启动失败但无 Slate 能力”，后者是“本来就要求直接退出”。
  - 完成判据：非桌面平台代码路径清晰存在；桌面与 commandlet 语义不被误改。
- [ ] **P2.3** 📦 Git 提交：`[UI] Feat: add non-desktop startup compile error fallback`

### Phase 3：补齐自动化验证与手工复现闭环

> 目标：让这次 UI 对齐不是一次性人工改动，而是有稳定回归和可重复的手工验证脚本。

- [ ] **P3.1** 增加启动错误 UI helper 的 `CppTests` 回归
  - 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStartupCompileErrorUiTests.cpp` 新增低层测试，Automation 前缀使用 `Angelscript.CppTests.Engine.StartupCompileErrorUi.*`。
  - 测试至少覆盖三类稳定逻辑：`VSCodeWorkspacePath` 为空时回退到 `Script/`、配置相对工作区路径时正确拼接到项目根、桌面文本刷新逻辑在文案未变化时不重复更新。
  - 如果 Phase 2 为按钮执行增加了可替换 launcher callback，这里还应断言“传给 launcher 的最终路径”正确，而不是依赖真实 `code` 命令存在。
  - 建议验证命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.CppTests.Engine.StartupCompileErrorUi" -Label startup-error-ui-runtime -TimeoutMs 600000`。
  - 完成判据：新增低层测试稳定通过，并且至少能在无真实 VS Code 进程的情况下保护路径解析与更新语义。
- [ ] **P3.1** 📦 Git 提交：`[Angelscript] Test: cover startup compile error UI helpers`

- [ ] **P3.2** 做一次真实编辑器启动手工 QA
  - 使用临时坏脚本制造“启动初编译失败”场景，必须走普通 `UnrealEditor.exe` 启动路径，而不是 commandlet，也不能携带 `-as-exit-on-error`。
  - 手工确认以下结果：窗口标题仍为 `Angelscript Compile Errors`；错误内容区为只读多行文本框；窗口底部存在 VS Code 按钮；未配置工作区时按钮目标是项目 `Script/`；修复脚本并保存后窗口会自动关闭。
  - 临时坏脚本和任何探针脚本都必须在验证结束后删除，不能把 QA 夹具遗留在仓库里。
  - 建议验证命令：使用普通编辑器启动命令或现有本机探针脚本复现；若需要记录窗口标题，可复用一次性 PowerShell 窗口探测方案，但最终提交前必须清理。
  - 完成判据：本地得到一次真实启动窗口证据，并完成夹具清理；若平台限制无法直接覆盖非桌面 fallback，则由 `CppTests` 和代码审查承担该分支验证。
- [ ] **P3.2** 📦 Git 提交：`[Angelscript] Test: verify startup compile error UI parity manually`

## 验收标准

1. `InitialCompile()` 失败时，桌面平台弹窗内容区已改为只读 `SMultiLineEditableTextBox`，并带“仅在文本变化时刷新”的保护。
2. 弹窗具备 `Open Angelscript workspace (VS Code)` 按钮，且打开目标遵循：`VSCodeWorkspacePath` 优先，否则回退到项目 `Script/`。
3. 非桌面平台存在 `FMessageDialog` fallback，并在显示错误后退出。
4. `commandlet` / `-as-exit-on-error` / 成功后自动关闭窗口 / `CheckForHotReload(ECompileType::FullReload)` 的现有行为保持不变。
5. `Angelscript.CppTests.Engine.StartupCompileErrorUi.*` 目标测试通过，且完成一次真实编辑器启动手工 QA。

## 风险与注意事项

### 风险

1. **把 UI 对齐做成流程重构**
   - 当前窗口之所以能自动关闭，是因为 `InitialCompile()` 失败分支本身就在驱动 modal loop 与热重载检查；若执行时把整段流程大范围迁出，很容易误改退出/成功状态机。
   - **缓解**：把重构范围限制在 UI helper / presenter 边界，`bSuccess`、`bErrorResponseDone`、`PreviouslyFailedReloadFiles` 的控制权仍留在启动编译主流程。

2. **VS Code 按钮引入不可测外部依赖**
   - 直接在按钮 lambda 里硬调 `code` 命令，容易让自动化测试依赖本机 PATH 或真的拉起外部进程。
   - **缓解**：把目标路径解析和外部执行分开；测试只验证解析和 callback 参数，手工 QA 再看真实按钮行为。

3. **继续维持每 tick 都重设文本导致选择体验退化**
   - 这是当前 `STextBlock` 版本与 UEAS2 参考实现的真实差异点之一，如果只换控件不补“文本变化前不更新”的保护，实际上仍没对齐。
   - **缓解**：将“未变化时不调用 `SetText`”写成明确验收条件，并纳入低层测试或 helper 断言。

### 已知行为变化

1. **新增 `VSCodeWorkspacePath` 设置项**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`
   - 变化：新增后，启动编译错误弹窗的按钮会优先打开该相对工作区路径；未配置时才回退到项目 `Script/`。

2. **桌面平台错误内容控件从纯文本显示变为可选择的只读多行文本框**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 或执行阶段拆出的 `AngelscriptStartupCompileErrorDialog.cpp`
   - 变化：用户可以复制错误文本，且 modal loop 不再无条件打断文本选择状态。
