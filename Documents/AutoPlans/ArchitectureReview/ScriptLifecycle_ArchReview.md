# ScriptLifecycle 架构与扩展性分析

---

## 架构分析 (2026-04-08 14:01)

### Arch-SL-01：编译阶段已有零散 hook，但尚未形成可编排的 phase pipeline

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译管线可扩展性，尤其是插入 `lint`、静态检查、优化 pass 的能力 |
| 当前设计 | 当前只在预处理前后和编译前后暴露少量 delegate，但 `Preprocess()` 与 `CompileModules()` 的阶段顺序仍是硬编码流程 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8-30` — 预处理只暴露 `OnProcessChunks` / `OnPostProcessCode`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:14-41` — `PreCompile` / `PostCompile` 是无参 delegate，`PreGenerateClasses` 才能拿到模块列表；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:212-304` — 预处理阶段按 `ParseIntoChunks -> ProcessImports -> DetectClasses -> AnalyzeClasses -> ProcessMacros -> ProcessDelegates -> ProcessDefaults -> Condense -> PostProcess` 固定执行；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3066-3068, 3212-3238, 3266-3285, 3797-3858, 3896-4140` — `CompileModules()` 内部固定串联 `PreCompile -> Stage1/Stage2 -> layout/reference update -> Stage3/Stage4 -> ClassGenerator -> PostCompile` |
| 优点 | 当前阶段边界已经足够清晰，已有 hook 可以作为兼容层；`Stage1-4` 与 `ClassGenerator` 的划分也说明管线天然可以被拆成 phase |
| 不足 | 现有 hook 不能声明执行顺序、不能访问统一上下文、不能返回结构化结果；外部无法在 `Stage2` 和 `Stage3` 之间稳定插入自定义 pass，也无法以标准方式把 `lint warning`、`优化失败`、`NeedFullReload` 这类结果反馈回主流程 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把模块解析拆成 `Search` / `Load` / `Execute` 三段：C++ 只定义 `IJSModuleLoader` 和 `__tgjsSearchModule` / `__tgjsLoadModule` 宿主桥，真正的 `require` 组合逻辑在 `modular.js` 中完成 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-48`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:489-495,621-635,4079-4118`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-199` | 先把“阶段接口”抽出来，再把阶段编排留给上层；这让 loader 可替换、执行链可插拔 |
| UnLua | 直接把 `LoadFromCustomLoader`、`LoadFromFileSystem`、`LoadFromBuiltinLibs` 插入 `package.searchers`，形成有序 searcher chain；每个 loader 都能独立决定命中与否 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100,557-667` | 把扩展点建成链式阶段，而不是只在头尾广播事件；每个阶段既能读取上下文，也能决定是否继续 fallback |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 delegate 的前提下，引入 `phase registry`，把编译-生成-换入拆成可注册、可排序、可返回结果的统一阶段 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptCompilationContext`，统一承载 `CompileType`、`Modules`、`Diagnostics`、`ReloadReq`、`AllRootPaths`、`bHadCompileErrors`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h/.cpp` 新增 `IAngelscriptCompilePhase` 注册接口，支持 `Before/After/Order` 与 `Run(Context)`。<br>3. 把当前 `Preprocess()` 的 `OnProcessChunks` / `OnPostProcessCode`、`CompileModules()` 的 `PreCompile` / `PreGenerateClasses` / `PostCompile` 包成内建 phase，先只做适配，不改行为。<br>4. 把 `Stage1-4`、`ClassGenerator.Setup()`、`PerformSoftReload()` / `PerformFullReload()` 逐步迁移成显式 phase；每个 phase 返回 `Continue / Warning / Abort / NeedFullReload`。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加一个测试 phase，验证它能插在 `Stage2` 和 `Stage3` 之间发出诊断并阻断后续阶段。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | `CompileModules()` 当前同时承担编译、热重载恢复、模板引用替换和 `ClassGenerator` 触发，拆分时如果上下文对象设计得过薄，容易把原本同一调用栈里的隐式状态变成新的耦合点 |
| 兼容性 | 向后兼容。旧 delegate 继续保留，并通过适配层映射到新 phase；默认 phase 顺序保持现状，现有脚本和现有项目 hook 不需要立刻迁移 |
| 验证方式 | 1. 运行现有 `AngelscriptTest` 编译/热重载测试，确认结果不变。<br>2. 新增“自定义 phase 插入点”测试，验证能在 `Stage2` 后中断并保留旧模块。<br>3. 对比迁移前后的 `ECompileResult` 输出，确认 `FullyHandled / PartiallyHandled / ErrorNeedFullReload` 语义不变。 |

### Arch-SL-02：模块标识仍然是 path-derived string，支持简单条件编译，但缺少 version / variant / cache policy

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块系统能力，尤其是条件编译、版本化模块、模块热替换与逻辑模块标识 |
| 当前设计 | 模块名完全由相对路径推导，`import` 只携带一个 `ModuleName` 字符串；条件编译依赖全局 `PreprocessorFlags`，没有模块级 metadata、版本语义或 cache policy |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:86-99` — `FilenameToModuleName()` 直接把 `Foo/Bar.as` 变成 `Foo.Bar`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:101-161` — `FImport` 只有 `ModuleName`、`ChunkIndex`、位置信息；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3497-3510,439-494` — `import` 解析与解析后依赖排序都只按模块名字符串做精确匹配；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1272-1303` — `FAngelscriptModuleDesc` 没有版本、标签、入口策略字段；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:38-73,4328-4346` — 条件编译只支持 `PreprocessorFlags` 中的布尔标识；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3018-3058,4158-4187` — 模块查找和 reload 队列都围绕单一 `ModuleName` / 文件列表运行 |
| 优点 | path-derived module id 简单直观，跨根目录扫描后结果确定；`#ifdef/#ifndef/#if !FLAG` 已经能覆盖 editor/cooked/test 这类全局变体 |
| 不足 | 当前没有逻辑模块层，因此无法表达 `same id, different version/variant`；条件编译只有全局布尔开关，不能按模块配置生效；热替换存在，但它是“文件驱动的活动模块重编译”，不是“带 cache policy 的模块 loader”，因此也无法做版本并存或按模块精确失效 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `DefaultJSModuleLoader` 支持 `.js/.mjs/.cjs/.json/.mbc/.cbc`、`package.json`、`index.js` 和逐级 `node_modules` 查找；`modular.js` 进一步处理 `package.json.type`、`exports`、`moduleCache` 与 `forceReload` | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:21-123`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-245` | 把“模块名”升级成“模块解析协议”，使版本、入口文件、缓存和 reload policy 都可以被模块系统消费 |
| UnLua | `package.searchers` 与 `package.path` 让模块解析天然支持多 loader；`ULuaModuleLocator` 决定对象应绑定哪个模块；`HotReload.lua` 用 `package.loaded` + `loaded_modules` 维护逻辑模块缓存，并只 reload 修改过的模块集合 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:21-33`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp:18-61`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:597-667`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:13-18,151-168,560-627` | 把“对象到模块”的定位、模块搜索路径、模块缓存和热替换脚本都拆开，形成可演进的模块控制面 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 path-derived 模块名之上补一层逻辑模块解析层，把 `module id`、`variant`、`version`、`cache policy` 从文件路径里解耦出来 |
| 具体步骤 | 1. 新增 `FAngelscriptModuleIdentity` 与 `IAngelscriptModuleResolver`，默认实现仍然使用 `FilenameToModuleName()`，保证旧项目零改动可跑。<br>2. 在每个脚本根支持可选 `ScriptModules.json`，字段至少包括 `id`、`entry`、`aliases`、`version`、`tags`、`loadPolicy`；未声明的模块继续走当前路径映射。<br>3. 让 `ProcessImports()` 不再只做“字符串对字符串”的文件内匹配，而是先走 resolver，把 `import Foo.Bar;` 解析成逻辑模块，再映射到具体 entry。<br>4. 让 `PreprocessorFlags` 支持“全局 flags + 模块 tags”两级注入；现有 `#ifdef FLAG` 语法保留，但 flag 来源可以包含 manifest tags。<br>5. 引入 `ModuleCacheState`，把当前 `PreviouslyFailedReloadFiles` / `QueuedFullReloadFiles` 升级为“按逻辑模块失效”，为未来的 `forceReload(moduleId)` 与版本切换做准备。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | resolver 与旧路径规则并存阶段，最容易出现“同一模块被旧路径和新 manifest 双重命中”的歧义；必须先把优先级和冲突诊断做清楚，否则会引入隐蔽的重复编译 |
| 兼容性 | 向后兼容。manifest 完全可选；不提供 manifest 时行为与今天一致；已有 `import Foo.Bar;` 不需要改写 |
| 验证方式 | 1. 增加“无 manifest 旧工程”回归，确认模块名和编译顺序不变。<br>2. 增加 resolver 单测，覆盖 alias、version/tag 选择与冲突诊断。<br>3. 增加“同一逻辑模块切换 variant 后只失效该模块依赖闭包”的 reload 测试。 |

### Arch-SL-03：初始化阶段会全量发现并编译所有脚本，执行 owner 与热更 tick 继续绑定同一引擎实例，不支持按需加载

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本加载延迟策略，以及编译-加载-执行 owner 是否已经解耦 |
| 当前设计 | 引擎初始化时就执行 `InitialCompile()`，递归扫描所有脚本根并预处理全部 `*.as` 文件；之后再由 `RuntimeModule` 的 fallback ticker 或 `GameInstanceSubsystem` tick 驱动热更检查与执行生命周期 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-24,138-165,186-200` — `StartupModule()` 在 editor/commandlet 直接 `InitializeAngelscript()`，且注册 fallback ticker；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1430-1431,1565-1569` — 初始化线程里先发现脚本根，再直接调用 `InitialCompile()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1944-2015,2038-2088` — `InitialCompile()` 会递归枚举 `AllRootPaths` 下全部 `*.as`，对每个文件 `AddFile()`，随后 `Preprocess()` 并一次性 `CompileModules(ECompileType::Initial, ...)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:12-29,81-86` — 世界存在时由 `GameInstanceSubsystem` 持有并 tick 引擎；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2794-2829` — tick 中再根据 world/editor 状态决定 `SoftReloadOnly` 或 `FullReload` |
| 优点 | 启动后脚本状态完整、诊断集中，第一次调用脚本时不会再触发额外编译；对于当前测试体系和初始资产扫描来说也比较稳定 |
| 不足 | 启动时间与脚本总量线性相关；未使用模块也会被预处理和编译；编译、加载、执行 owner 仍然绑在同一个 `FAngelscriptEngine` 生命周期里，因此没有类似 `require()` 的“首次使用再加载”入口，也难以为大型项目做分区冷启动 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 启动只执行一组固定 bootstrap 模块，随后把 `puerts.__require` 存成宿主 `Require`；真正业务模块在 `genRequire()` 中首次 `search -> load -> execute` 时才进入 `moduleCache` | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:621-640,3838-3917,4079-4118`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-199,231-245` | 引擎 bootstrap 与业务模块激活分离，默认就是按需加载 |
| UnLua | `Start()` 只 `require` 一个 `StartupModuleName`；之后模块解析依赖 `package.searchers` / `package.path`，对象绑定时再通过 `ModuleLocator->Locate()` 决定需要哪个模块 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:224-249,336-386,597-667` | 把“运行时 ready”与“脚本模块激活”拆开，先启动 VM，再按对象/入口模块逐步拉起 chunk |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“引擎启动”与“模块激活”拆成两层，并引入 `EagerAll / StartupSet / Lazy` 三档加载策略，先保留默认行为，再允许项目逐步切换到按需编译 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中把当前 `Initialize_AnyThread()` 的职责拆成 `BootstrapRuntime()` 与 `InitialCompile()` 两段，前者只创建 engine、bind builtins、准备 resolver/cache。<br>2. 新增 `EnsureModuleCompiled(ModuleId, CompileReason)`，当 import 解析、对象激活或显式 API 请求某模块时，只编译该模块及其依赖闭包。<br>3. 在 `UAngelscriptSettings` 或新的 runtime config 中加入 `InitialLoadPolicy`，默认值设为 `EagerAll`，保证旧项目行为不变；新项目可选 `StartupSet` 或 `Lazy`。<br>4. `StartupSet` 模式下只编译 manifest 标记的启动模块；`Lazy` 模式下 `InitialCompile()` 退化成 bootstrap 校验，不再扫描所有 `*.as`。<br>5. 保留 `GameInstanceSubsystem` 与 fallback ticker，但它们只负责 `Tick()`、热更轮询和 deferred full reload，不再隐含“引擎必须已经全量编译完所有模块”的前提。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 最大风险不是功能正确性，而是体验迁移：`Lazy` 模式会把部分错误从启动期推迟到首次使用期；如果诊断、profiling 和 warmup 工具没跟上，开发体验会先变差 |
| 兼容性 | 向后兼容。默认继续使用 `EagerAll`；`StartupSet` / `Lazy` 为显式 opt-in；已有项目可以先只把 plugin roots 或特定业务模块切到延迟策略 |
| 验证方式 | 1. 对比 `EagerAll` 与 `StartupSet/Lazy` 的 editor 启动时间、首帧时间、首次模块激活延迟。<br>2. 增加“首次 import 才编译模块”的自动化测试，确认同一模块不会重复编译。<br>3. 验证 `Lazy` 模式下热更仍能把脏模块重新加入待编译集合，并保持现有 `SoftReloadOnly / FullReload` 语义。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-02 | 模块标识、版本/变体、模块缓存策略 | resolver/manifest 增量引入 | 高 |
| P1 | Arch-SL-03 | 启动全量编译与按需加载能力 | 加载策略分层 | 高 |
| P2 | Arch-SL-01 | 编译阶段插拔与 phase 编排 | 扩展点结构化 | 中 |

---

## 架构分析 (2026-04-08 14:13)

### Arch-SL-04：`import` 解析仍绑定“已发现文件集合”，缺少 `search/load` 级模块来源抽象

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 预处理阶段的模块发现与源码装载能否扩展到虚拟模块、生成模块、远端/缓存模块等非文件系统来源 |
| 当前设计 | `FAngelscriptPreprocessor` 的输入就是一组已经知道绝对路径的文件；`import` 解析阶段只在当前 `Files` 数组里按 `ModuleName` 线性查找，找不到就只能依赖外层先把对应文件塞进来 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:14-21,101-108` — 预处理入口只有 `AddFile()` 和 `GetModulesToCompile()`，`FImport` 只保存字符串形式的 `ModuleName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:91-115` — `AddFile()` 直接接受 `RelativeFilename/AbsoluteFilename` 并立刻为该文件创建 `ModuleDesc`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:230-238,265-307` — `Preprocess()` 先对现有 `Files` 做 `ProcessImports()`，最后把每个 `File` 的 `ProcessedCode` 直接装进 `Module->Code`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:439-498,3497-3510` — `import Foo.Bar;` 只解析成 `ImportDesc.ModuleName`，随后在 `for (FFile& OtherFile : Files)` 里做精确匹配；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2068-2082,2448-2455` — 初次编译和热重载都必须先枚举文件，再逐个 `Preprocessor.AddFile(...)`，没有独立的 `Search`/`Load` 抽象层 |
| 优点 | 当前实现简单直接，文件系统就是唯一事实来源，诊断和热重载路径都围绕真实脚本文件展开，行为可预测 |
| 不足 | 预处理器无法原生接入“逻辑模块先解析、源码后装载”的扩展点；生成模块、内存模块、打包缓存模块、按模块缓存命中都只能在外层绕过预处理器自己拼文件列表，扩展成本高且容易破坏导入顺序语义 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | C++ 侧先定义 `IJSModuleLoader::Search/Load`，默认 loader 再实现 `.js/.mjs/.cjs/.json/package.json/index.js/node_modules` 的解析规则；JS 层 `modular.js` 只消费 `searchModule`/`loadModule` 宿主桥，不直接假定模块一定来自磁盘文件列表 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-123`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4079-4120`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-180,231-245` | 先把“模块解析协议”抽成接口，再让默认文件系统 loader 只是其中一个实现，后续才能自然叠加缓存、虚拟模块和包入口规则 |
| UnLua | 直接把 `LoadFromCustomLoader`、`LoadFromFileSystem`、`LoadFromBuiltinLibs` 插进 `package.searchers`；每个 searcher 只负责“能不能命中”和“如何把源码装进 VM”，并不要求调用方事先拿到完整文件列表 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100,557-667,644-666` | loader chain 能让自定义来源和默认文件系统共存，模块来源扩展不会反向污染主编译管线 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `Preprocessor` 前增加统一的 `module source loader` 链，把“模块名解析”与“源码装载”从 `AddFile()` 的文件系统假设里拆出来 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` 新增 `IAngelscriptModuleSourceLoader`，最少提供 `Search(CurrentModuleName, ImportName, OutSourceKey, OutResolvedModuleName)` 与 `Load(SourceKey, OutRelativePath, OutAbsolutePath, OutCode, OutMetadata)`。<br>2. 保留现有 `AddFile()`，但把它降级为默认 `FileSystemLoader` 的适配入口；`InitialCompile()`/`PerformHotReload()` 仍然可以继续枚举磁盘文件，只是改走默认 loader。<br>3. 把 `ProcessImports()` 从“扫 `Files` 数组找同名模块”改为“先问 loader chain，命中后再把未装载的模块 materialize 成 `FFile`”；`FImport` 增加 `ResolvedSourceKey` 或 `ResolvedModuleName` 以便诊断。<br>4. 第一个增量扩展点只实现 `GeneratedModuleLoader` 或 `ManifestCacheLoader` 二选一，验证非文件系统来源不需要改动主编译逻辑。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/` 增加 loader 优先级、缺失模块诊断和循环导入回归，确保默认磁盘行为保持不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/` |
| 预估工作量 | M |
| 架构风险 | loader chain 如果一开始就支持过多来源，最容易把“模块名冲突”和“相同模块多来源命中”变成隐蔽错误；必须先定义优先级与冲突诊断，再扩来源 |
| 兼容性 | 向后兼容。默认 `FileSystemLoader` 完整复用今天的路径语义；已有项目仍然可以只靠磁盘脚本运行，`import` 语法不需要修改 |
| 验证方式 | 1. 运行现有 `Preprocessor`/`HotReload` 测试，确认纯文件系统工程结果不变。<br>2. 新增一个虚拟 loader 测试，验证 `import` 可以命中内存模块且错误信息仍带模块名。<br>3. 验证 loader 冲突时能给出稳定、可读的诊断，而不是静默选择错误来源。 |

### Arch-SL-05：模块进入运行态后立即挂到 engine-global `ActiveModules`，缺少显式 activation/deactivation plane

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本“编译完成”到“进入运行态”的最后一段生命周期，是否存在按模块、按对象或按 `GameInstance` 的激活边界 |
| 当前设计 | 当前编译产物一旦 swap-in 就直接写入 `FAngelscriptEngine::ActiveModules` 全局表；运行时只有 engine 级 `Tick()` 和少量 compile delegate，没有类似 `StartupModuleName`、`ModuleLocator` 或 `ActivateModule()` 的显式激活面 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:169-181,204-205,311-327,384-390` — 对外暴露的是 `DiscardModule()`、`InitialCompile()`、`CompileModules()`、`Tick()` 与全局 `GetActiveModules()`，内部活跃态由 engine 级 `ActiveModules`/`ActiveClassesByName` 持有；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2911-2955` — 新编译模块会被直接 `ActiveModules.Add(...)`，随后重建全局类/枚举/委托索引；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653-1655,3066-3068,4136-4140,2488-2496` — 生命周期 hook 只有 `OnInitialCompileFinished`、`PreCompile`、`PostCompile` 和 `PostCompileClassCollection` 这类编译期广播；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1305-1306` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5775-5803` — 唯一内建“模块后执行”入口是 `PostInitFunctions`，而它只是按名字执行少量全局函数；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4109-4133` — 当前 `PostInitFunctions` 的自动填充场景主要是 literal asset getter；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:12-29,81-86` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2843-2846` — subsystem tick 只负责驱动整个 engine，`ShouldTick()` 只判断 `Engine != nullptr`，并不区分哪些模块处于激活态 |
| 优点 | 全局 swap-in 模型让编译后状态一致性强，脚本类型、反射索引和热重载恢复都围绕一个 engine 状态机完成，调试成本低 |
| 不足 | 缺少显式 activation plane 后，无法表达“模块已编译但尚未激活”“只对某个 `GameInstance` 激活某模块”“对象首次绑定时决定加载哪组脚本”等更细粒度运行时语义；未来做分区冷启动、按对象绑定、模块停用时会被迫继续堆叠 engine-global 特判 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把“环境定位”和“模块定位”分别做成 `EnvLocator`/`ModuleLocator`；`FLuaEnv::Start()` 只执行一个 `StartupModuleName`，对象创建时再通过 locator 找到对应 env 并 `TryBind()`；按 `GameInstance` 的 locator 还支持一个世界多个 Lua env | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:31-33,47-56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:23-34,37-50`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:18-33,40-82`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:21-33`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp:18-65`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:145-156,160-171` | 先把“谁拥有运行时环境”和“对象映射到哪个模块”两个问题显式建模，编译完成不等于所有模块都立刻进入执行态 |
| puerts | 初始化时只跑一小组 bootstrap 模块，随后把 `puerts.__require` 和 `global.require` 暴露出来，让真实业务模块在运行中按需 `require`；模块激活由 `Require` 而不是 compile 结果直接驱动 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:621-635`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:231-245,105-143` | bootstrap 与 activation 分层后，运行态可以自然支持按需激活、强制 reload 和多入口启动 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持现有 engine-global 行为为默认值的前提下，补一层显式 `module activation` 与 `runtime locator` 契约，让“已编译”和“已激活”可分离 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `IAngelscriptRuntimeLocator` 与 `FAngelscriptModuleActivationRequest`，默认实现 `FGlobalAngelscriptRuntimeLocator` 直接复用今天的单 engine 模式。<br>2. 新增 `ActivateModule(ModuleName, ContextObject)` / `DeactivateModule(ModuleName, Reason)` API；默认 locator 的 `ActivateModule()` 只校验模块存在并维持当前立即可见语义，不改旧行为。<br>3. 把当前 `PostInitFunctions` 升级成显式 `StartupFunctions`/`ShutdownFunctions` 元数据；第一阶段先复用现有 `PostInitFunctions` 存储与 `CallPostInitFunctions()` 执行框架，避免大改 `ClassGenerator`。<br>4. 在 `UAngelscriptGameInstanceSubsystem` 接入 locator，使 future `ByGameInstance` 或 `ByWorld` 实现可以决定模块激活边界；没有自定义 locator 时仍然只 tick 全局 engine。<br>5. 在脚本基类、subsystem 绑定点或对象创建路径增加一次 `ResolveModuleForObject()` 钩子，后续如果要做按对象绑定或分区冷启动，可以在不重写编译器的前提下接入。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加“模块已编译未激活”“不同 `GameInstance` 激活集隔离”“停用模块后对象绑定失败可诊断”三类回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 现有很多脚本和绑定默认假设“编译成功后即可全局可见”；如果 activation 语义默认值设计不稳，最容易引入时序问题和难追踪的对象绑定回归 |
| 兼容性 | 向后兼容。默认 locator 继续维持今天的全局激活模式；`ActivateModule()`/`DeactivateModule()` 先作为 opt-in API，不强制现有项目声明启动模块或对象定位规则 |
| 验证方式 | 1. 运行现有执行/热重载/actor lifecycle 测试，确认默认 locator 下行为不变。<br>2. 新增 `ByGameInstance` 测试 locator，验证两个 `GameInstance` 的激活集不会串扰。<br>3. 验证 `DeactivateModule()` 后旧对象的报错路径可读，且不会把整个 engine 误判为不可 tick。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-04 | 模块来源抽象、`import` 的 search/load 扩展能力 | loader chain 新增 | 高 |
| P1 | Arch-SL-05 | 模块激活边界、engine-global 运行态耦合 | activation plane + runtime locator | 高 |

---

## 架构分析 (2026-04-08 14:24)

### Arch-SL-06：`PrecompiledData` 的 artifact identity 仍然只看 `CodeHash`，无法安全承载变体、flags 与自定义 compile pass

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译产物缓存的身份建模，是否足以支撑条件编译、版本化模块和未来的自定义 `lint/optimize` pass |
| 当前设计 | 运行时确实维护了 `CombinedDependencyHash`，预处理器也会根据环境构造 `PreprocessorFlags`；但 `PrecompiledData` 写盘与回放时只保存并比对 `CodeHash`，命中条件没有把依赖闭包、flag 集合或 pass fingerprint 纳入 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2046-2057` — 初始启动允许直接走 `PrecompiledData` 并因此禁用 hot reload；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1288-1291` — `FAngelscriptModuleDesc` 同时持有 `CodeHash` 与 `CombinedDependencyHash`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4262-4284` — 编译阶段先把 import 依赖 XOR 到 `CombinedDependencyHash`，但是否使用预编译数据仍只看“所有 imports 都是 precompiled + `CompiledModule->CodeHash == Module->CodeHash`”；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h:423-467` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:1417-1424,2773-2779` — 预编译模块结构落盘的字段只有 `CodeHash`、`ImportedModules`、`PostInitFunctions` 等，没有 `CombinedDependencyHash`、flag fingerprint 或 pipeline version；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:38-50,58-70,3260-3285,4338-4346` — 条件编译输入来自运行环境和 `UAngelscriptSettings::PreprocessorFlags`，但这些值并未进入预编译缓存键 |
| 优点 | 当前命中逻辑简单，缓存恢复路径短；对“完全冻结脚本、关闭热更”的 shipping 形态足够直接 |
| 不足 | 一旦未来要引入模块变体、版本化模块、target-specific `PreprocessorFlags`、自定义 `lint` 或优化 pass，现有缓存键无法判断“同一源码在不同 compile context 下是否还能复用”；这会把扩展能力锁死在“禁用缓存”或“接受潜在误命中”之间 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块缓存不是“隐式副产品”，而是运行时显式数据结构：`moduleCache` 按解析后的 `fullPath` 建 key，`genRequire()` 在 `searchModule -> loadModule -> executeModule` 之间维护命中与回滚，`forceReload()` 还能按 key 精确失效 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-123`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-71,105-191,205-245` | 先把“缓存 identity”和“失效协议”显式化，再谈 loader 扩展与 reload 策略；cache key 是模块系统的一部分，不是编译器内部细节 |
| UnLua | `loaded_modules`、`package.loaded` 与 `loaded_module_times` 都按 `module_name` 维护，`StartupModuleName` 与 `ModuleLocator` 进一步把“入口模块”和“对象定位模块”配置化；同名模块的缓存与重载边界因此是清晰的逻辑概念 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:31-33,47-52`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-100,230-245,372-376`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170,610-624` | 缓存 identity 先绑定到逻辑模块，再叠加启动入口和对象定位策略；后续新增 loader/searcher 并不会模糊缓存边界 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `PrecompiledData` 引入显式 `artifact fingerprint`，把源码哈希升级为“模块内容 + 依赖闭包 + preprocess context + phase version”的复合键 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptCompileFingerprint`，至少包含 `CodeHash`、`CombinedDependencyHash`、排序后的 `ImportedModules`、规范化后的 `PreprocessorFlags`、`bUseAutomaticImportMethod`、build identifier。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h/.cpp` 扩展 `FAngelscriptPrecompiledModule` 序列化格式，保存 `FingerprintVersion` 与 `CompileFingerprint`；旧 cache 读到无该字段时直接 fallback 为 miss。<br>3. 将 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4284-4299` 的命中判断从“仅 `CodeHash`”改为“完整 fingerprint 相等”，并在 mismatch 日志中指明是源码、依赖、flags 还是 pipeline version 失效。<br>4. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h/.cpp` 增加可选 `CompileFingerprintContributor` 注册口，让未来的 `lint`、optimizer、custom preprocessor phase 可以把自身版本号并入 fingerprint，而不是各自绕开缓存。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 新增回归：同一 `CodeHash` 但不同 `PreprocessorFlags` 不得复用 cache；依赖模块变化应让上游 fingerprint miss；无扩展 contributor 的旧工程行为保持不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 主要风险是 cache format 升级：如果 fingerprint 设计得不稳定，会导致 editor 每次启动都 miss，反而放大初始编译成本；因此第一版必须先锁定序列化顺序与日志可观测性 |
| 兼容性 | 向后兼容。旧 `PrecompiledScript.Cache` 可以视为“低版本 cache，直接失效一次”；没有自定义 phase 的项目不会新增配置项，默认只得到更严格、更安全的命中判定 |
| 验证方式 | 1. 生成一份 cache 后切换 `PreprocessorFlags`，确认运行时拒绝复用并输出具体 miss 原因。<br>2. 修改只被 import 的依赖模块，确认上游模块 fingerprint 失效而不是错误复用旧 stage1 产物。<br>3. 对比升级前后无变更工程的启动日志与首轮编译结果，确认命中率和功能行为保持一致。 |

### Arch-SL-07：hot reload 的回滚与重试账本仍是 file-batch 级，缺少 module-entry 级 transaction journal

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块热替换的失败隔离粒度，以及 reload 失败后能否按模块精确重试、排队和升级为 full reload |
| 当前设计 | 现有 hot reload 入口以文件变更队列驱动：`CheckForHotReload()` 汇总变更文件，`PerformHotReload()` 再把 `PreviouslyFailedReloadFiles`、删除文件和依赖闭包合并成一个 `FilesToHotReload` 批次；一旦该批次任一模块编译或换入失败，就整体保持旧代码，并把“本次涉及的全部文件”重新放回失败队列或 full reload 队列 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:406-419,477-479` — 引擎内部持有的是 `PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 和文件级变更数组，而不是模块级 journal；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2778` — `CheckForHotReload()` 只消费文件列表并按 world/editor 状态选择 `SoftReloadOnly` 或 `FullReload`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2253-2280,2282-2455` — `PerformHotReload()` 先把之前失败过的文件重新并入，再从活动模块图推出整个 `FilesToHotReload` 闭包；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2455-2461` — 预处理失败时直接把原始 `FileList` 放回失败集合；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3874-3885,4066-4112,4158-4187` — 只要本轮任意编译/换入环节失败，就整体不 swap-in，并把本轮 `CompiledModules` 对应的全部文件再次加入 `PreviouslyFailedReloadFiles` 或 `QueuedFullReloadFiles` |
| 优点 | 以批次为单位回滚简单可靠，不容易把 engine 留在半成功状态；对当前“结构一致性优先”的 hot reload 模型比较稳 |
| 不足 | 粒度过粗导致三个后果：一是 unrelated module 也会因为同批次其他模块失败被反复重编；二是 `SoftReloadOnly -> queued full reload` 的升级只能按文件重试，无法表达“只有这几个逻辑模块必须升级”；三是未来即使补齐版本化模块或 lazy compile，也仍然会被 file-batch journal 拉回 engine-global reload 语义 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `forceReload(reloadModuleKey)` 直接按模块 key 标记 `moduleCache[moduleKey].__forceReload`；`require()` 若执行失败，会把当前模块的 local/global cache entry 置回 `undefined`，而不是连坐整个缓存表；`SearchModule` / `LoadModule` 也是一次只处理一个模块 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4079-4112`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-191,205-225` | 把 reload journal 做成“模块 key -> reload intent”，失败隔离和精确重试自然就具备了 |
| UnLua | `M.reload(module_names)` 支持指定模块集合；`reload_modules()` 先把旧模块放入 `tmp_modules`，在 sandbox 里逐个 `sandbox.load()`/`xpcall()`，若任何一个失败则在 `update_modules()` 之前直接 `sandbox.exit()` 返回；`loaded_modules` 与 `loaded_module_times` 都按模块名维护 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170,553-624` | 事务边界是“命名模块集合”而不是文件批次，提交前先在隔离环境中验证，失败时不污染已加载模块表 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留当前强一致 swap-in 语义，但把 reload 调度层从“文件列表”提升为“模块 journal + SCC 原子提交” |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FModuleReloadRecord`，字段至少包括 `ModuleName`、`DirtyFiles`、`ReloadMode`、`LastErrorKind`、`bPendingFullReload`、`LastAttemptTime`。<br>2. 让文件监控仍然只产生 `DirtyFiles`，但 `CheckForHotReload()` 先把它们映射到 `ModuleReloadRecord`，再根据 import/dependency 图计算需要一同提交的模块集合；现有 path-derived `ModuleName` 可先直接复用，避免和 manifest/resolver 改造互相阻塞。<br>3. 把 `PerformHotReload()` 拆成 `BuildReloadClosure(DirtyModules)` 与 `CompileAndCommitReloadBatch(ReloadBatch)` 两段；提交阶段按强连通分量或现有依赖闭包保持原子，避免真正的“半模块提交”。<br>4. 将 `PreviouslyFailedReloadFiles` / `QueuedFullReloadFiles` 逐步降级为兼容层，把失败重试与 full reload 升级改记到 `FModuleReloadRecord`；只有 watcher 层仍然关心文件。<br>5. 在 `CompileModules()` 返回结果之外补一个 `PerModuleResult`，让“代码可热更的模块”和“需要 full reload 的模块”分别记录状态；第一阶段即使仍然整批回滚，也先把 journal 建起来，为后续 selective retry 铺路。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加回归：A 模块失败时 B 模块不应被无限重复加入失败队列；PIE 下只把真正 `NeedsFullReload` 的模块标记为 pending full reload；删除文件与重命名文件仍能正确关联回模块记录。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 这里最大的风险不是数据结构，而是错误地把原本必须一起回滚的依赖模块拆开提交；因此第一阶段必须坚持“module journal 更细，但 commit 仍按 SCC/闭包原子”，不要直接追求最大化 partial success |
| 兼容性 | 向后兼容。默认 `ModuleName` 继续来自现有路径映射；第一阶段 journal 只改变调度与诊断，不改变最终 swap-in 语义；已有项目不会因为引入模块账本而改变脚本写法 |
| 验证方式 | 1. 构造两个无依赖模块，验证其中一个编译失败时另一个不会被重新加入失败队列。<br>2. 在 PIE 触发 `SoftReloadOnly`，确认只有真正 `NeedsFullReload` 的模块被标记为 pending full reload。<br>3. 对比改造前后的失败日志，确认错误信息从“文件批次”升级为“模块记录”，且旧的回滚语义未被破坏。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-06 | precompiled artifact identity、flags/variant 安全性 | cache fingerprint 升级 | 高 |
| P1 | Arch-SL-07 | hot reload 失败隔离、重试与 full reload 升级粒度 | module journal 新增 | 高 |

---

## 架构分析 (2026-04-08 14:35)

### Arch-SL-08：源码装载契约仍是“路径 + 位置布尔”，删除文件与虚拟源码没有一等表示

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译前源码装载 contract 是否足以承载删除文件、异步 I/O、生成模块、内存模块与未来缓存回放 |
| 当前设计 | 预处理器入口只有 `AddFile(Relative, Absolute, bLoadAsynchronous, bTreatAsDeleted)`；初始编译和 hot reload 都先构造文件路径列表，再把文件状态压进位置布尔，没有独立的 source descriptor |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:15` — `AddFile()` 以两个布尔参数编码装载策略；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:91-137` — `bTreatAsDeleted` 只是把 `RawCode` 置空，`bLoadAsynchronous` 则切到异步读；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2068-2079` — 初始编译只能 `FindAllScriptFilenames()` 后逐个 `AddFile()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2266-2279,2448-2452` — hot reload 先收集 `AlreadyDeletedFiles`，随后把 `bTreatAsDeleted` 作为第三实参传入 `AddFile()`，暴露出调用契约对“删除文件”语义没有类型安全表达 |
| 优点 | 文件系统作为唯一事实来源时实现简单，初始编译路径短；异步读能力也能在不改主流程的情况下接入 |
| 不足 | “来源类型”“读取模式”“是否 tombstone”“是否已有内存 buffer”被压缩成位置布尔后，API 很脆；未来即使补 resolver/manifest，预处理器仍然无法稳定承载 generated module、cache hit、远端源码或明确的 deleted source |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 宿主 loader 先 `Search()` 返回 `Path/AbsolutePath`，再 `Load()` 返回字节数组；JS 侧 `genRequire()` 消费 `fullPath/debugPath` 后再执行并缓存 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-123`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-150` | 先把“模块定位结果”和“源码载荷”拆开，删除、缓存、调试路径和多种来源才能通过统一 contract 扩展 |
| UnLua | `FCustomLuaFileLoader` 直接输出 `Data` 与 `ChunkName`；`package.searchers` 固定串接 custom/filesystem/builtin 三段 loader，命中与否由各 loader 自己决定 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22-34`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-100,557-641` | loader contract 以“模块名 -> 字节流 + 调试名/路径”为中心，而不是把文件状态编码在调用约定里 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `AddFile()` 兼容层的前提下，引入一等 `module source descriptor`，把文件、删除、内存、生成、缓存回放统一成显式 source kind |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` 新增 `FAngelscriptModuleSourceDesc`，字段至少包括 `ModuleName`、`RelativePath`、`ResolvedPath`、`DebugPath`、`EAngelscriptSourceKind(File/InMemory/Generated/Deleted/Precompiled)`、`ELoadMode(Sync/Async/ProvidedBuffer)` 与可选 `SourceText`。<br>2. 新增 `AddSource(const FAngelscriptModuleSourceDesc&)`，现有 `AddFile()` 仅作为 `SourceKind::File` 的薄包装，保证现有调用点继续可用。<br>3. 把 `InitialCompile()` 的磁盘扫描与 `PerformHotReload()` 的 `AlreadyDeletedFiles` 都改成先生成 `SourceDesc`；删除文件显式标记为 `Deleted`，不要再靠位置布尔表达 tombstone。<br>4. 在预处理器内部增加统一 `MaterializeSource()`，把同步读、异步读、provided buffer 和空源码分流到一个函数，后续 generated module / manifest entry / precompiled stub 都复用这条入口。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/` 增加回归：删除文件 reload、内存源码注入、async/sync 混用，确认 module name、diagnostics 与 hot reload 行为稳定。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp` |
| 预估工作量 | M |
| 架构风险 | 主要风险是把当前“路径即来源”的隐含假设显式化后，日志与 diagnostics 可能暂时出现 `ResolvedPath` / `DebugPath` 双轨；第一版必须先统一展示规则，否则定位问题会更难 |
| 兼容性 | 向后兼容。`AddFile()` 继续保留并映射到 `SourceKind::File`；现有脚本项目不需要改写 `import` 语法或目录结构 |
| 验证方式 | 1. 删除脚本文件时，确认 hot reload 走 `Deleted` source，而不是误入 async load 分支。<br>2. 构造 in-memory source，验证预处理与 diagnostics 仍能产出稳定的 module name/debug path。<br>3. 对比改造前后的普通文件编译日志与模块数量，确认默认行为不变。 |

### Arch-SL-09：`automatic imports` 与显式 `import` 仍是互斥模式，不是统一依赖图上的两种解析策略

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块依赖图是否统一、可被 lint / hot reload / lazy compile / manifest 验证共同消费 |
| 当前设计 | `bAutomaticImports` 在引擎初始化时成为全局模式开关；manual 模式下预处理器和 Stage1 编译显式消费 `ImportedModules`，automatic 模式下则把 `import` 当作可忽略文本，依赖图改由 AngelScript 内部 `moduleDependencies` 驱动 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:61-67` — 设置项明确写着“explicit import statements no longer used”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291-1292,1415-1416` — 初始化时把 `bAutomaticImports` 写入 `bUseAutomaticImportMethod` 与 `asEP_AUTOMATIC_IMPORTS`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232-239` — 只有 manual 模式才会 `ProcessImports()` 排序文件；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:482-490` — automatic 模式下 `import` 被 blank 掉且只发 warning；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3173-3208` — Stage1 只有 manual 模式才把 `ImportedModules` 转成 `ImportedModules` 列表并 `ImportIntoModule()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2315-2375,2400-2443,4060-4063` — hot reload automatic 模式依赖 `ScriptModule->moduleDependencies`，manual 模式则走 `ImportedModules` 反向图并额外 `ResolveAllDeclaredImports()` |
| 优点 | 兼容老项目迁移简单；automatic imports 能减少显式 `import` 维护成本，并支撑当前的 recompile avoidance |
| 不足 | 同一份源码在不同 setting 下会得到不同依赖图和不同 reload/验证路径；显式依赖无法在 automatic 模式下继续作为 lint、版本约束、warmup set 或 lazy load 策略输入，导致未来扩展必须同时理解两套图 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require(moduleName)` 始终走同一条 `normalize -> builtin/native -> searchModule -> loadModule -> executeModule -> moduleCache` pipeline；`forceReload()` 也复用同一 cache key 与 module request 语义 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-195,205-245` | 显式依赖请求与解析策略共用一条管线，只替换 search/load policy，不切换 dependency model |
| UnLua | `package.searchers` 固定作为统一查找链；`Start(StartupModuleName)`、sandbox `require/load` 与 `reload_modules()` 都复用同一 searcher 语义和 `loaded_modules/package.loaded` 缓存 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-100,230-252,557-667`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:127-176,553-601` | 先统一“模块请求 -> searchers -> cache”的 contract，再在上层附加 startup、hot reload 或对象绑定策略 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把显式 `import` 和自动推导依赖收敛到同一 `dependency graph`，让 automatic/manual 退化成 policy，而不是互斥模式 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 的 `FAngelscriptModuleDesc` 新增 `DeclaredImports`、`ResolvedImports`、`InferredImports` 与 `EImportEdgeKind(Declared/Automatic/ImplicitFunctionImport)`；预处理器无论开关如何都保留 `import` 解析结果。<br>2. 让 `ProcessImports()` 只负责建立 `DeclaredImports` 与拓扑提示，不再决定“这些 import 是否作废”；排序要求、自动补边和缺失依赖策略交给编译 policy 处理。<br>3. 在 `CompileModules()` Stage1 后统一生成 `ResolvedImports = Merge(Declared, Inferred)`；`bAutomaticImports` 第一阶段仅表示“允许自动补边并把未声明边标记为 `Inferred`”，而不是“manual imports 完全忽略”。<br>4. 把 `PerformHotReload()`、`CheckUsageRestrictions()`、未来 manifest/lint pass 都改为消费统一 `ResolvedImports`，只在 diagnostics 中区分边来源，避免 manual/automatic 两套闭包算法继续分叉。<br>5. 增加兼容迁移开关，例如 `LegacyAutomaticIgnoreDeclaredImports`；默认先保留旧 automatic 体验，但把显式 import 记录为 advisory edge 并给出 redundant/missing drift warning，待项目清理后再切 strict mode。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp` |
| 预估工作量 | M |
| 架构风险 | 统一依赖图后，旧项目里 automatic 模式下遗留的无效 `import` 会第一次被系统看见；如果没有过渡 warning/compat mode，容易把历史噪音一次性放大为迁移成本 |
| 兼容性 | 向后兼容。默认仍可维持今天的 automatic 行为；旧项目的 `import` 先作为 advisory edge 进入图，不立即阻断编译；只有显式启用 strict policy 才改变约束强度 |
| 验证方式 | 1. 同一组脚本在 manual/automatic 下应生成同一份 `ResolvedImports` 主图，只是 `EdgeKind` 不同。<br>2. 对比 hot reload 闭包，确认 manual/automatic 不再走两套算法。<br>3. 增加 redundant/missing import 自动化测试，验证兼容模式只报警不阻断。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-08 | 源码装载 contract、删除文件与虚拟模块表示 | source descriptor 增量引入 | 高 |
| P1 | Arch-SL-09 | manual/automatic import 双轨语义 | unified dependency graph | 高 |

---

## 架构分析 (2026-04-08 14:46)

### Arch-SL-10：`FAngelscriptModuleDesc` 同时承载 source、compile artifact 与 live runtime state，导致管线分层被结构体本身锁死

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块数据模型是否分层，能否分别承载源码输入、编译产物、运行态句柄与热更账本 |
| 当前设计 | 预处理器一开始就创建 `FAngelscriptModuleDesc`，随后同一个对象一路被填充源码、语义描述、预编译缓存指针、`asCModule*`、测试发现结果，并最终直接挂入 `ActiveModules` |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:120-161` — `FFile` 同时持有 `Module`、`RawCode`、`ProcessedCode`、`Imports`、异步 I/O 句柄；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:91-101,295-300,4133` — `AddFile()` 立即创建 `FAngelscriptModuleDesc`，预处理结束后把 `ProcessedCode`/`CodeHash`/`PostInitFunctions` 回填到同一对象；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1272-1333` — `FAngelscriptModuleDesc` 既有 `Code`、`Classes`、`ImportedModules`，也有 `ScriptModule`、`PrecompiledData`、`bCompileError`、`UnitTestFunctions`、`IntegrationTestFunctions`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3427-3529` — 热更扩依赖时只能 `MakeShared<FAngelscriptModuleDesc>(*OldModule)`，再手工清空 `ScriptModule`、`PrecompiledData`、类型指针、测试结果；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2938-2979,4038-4053` — 编译成功后同一个对象继续进入 `ActiveModules`、`ModulesByScriptModule`、类型索引与失败重试队列 |
| 优点 | 传递成本低，预处理到编译几乎零中间对象；今天的 hot reload 回滚也能靠“复制整个模块描述”维持语义 |
| 不足 | source/artifact/live state 没有边界后，任何新增能力都会变成“再往 `FAngelscriptModuleDesc` 塞字段”：版本化模块、`lint` 产物、按需激活、side-by-side 变体、analyze-only 编译都会与 `ScriptModule`/测试发现/失败状态缠在一起；热更路径里大段“复制后重置”代码也是这种耦合的直接症状 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | C++ `IJSModuleLoader` 只负责 `Search/Load`；JS 侧 `modular.js` 再把 `moduleCache`、`executeModule()`、`forceReload()` 作为独立 runtime 层维护，源码位置、载荷、缓存对象不是同一个结构体 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-48`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:92-139`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-71,129-150,183-191,205-245` | 先拆“模块来源”“源码载荷”“已执行模块缓存”，后续 loader、cache policy、reload key 才能独立演进 |
| UnLua | `LoadFromCustomLoader` / `LoadFromFileSystem` 只输出 `Data + ChunkName`；运行态缓存单独放在 `loaded_modules` / `package.loaded` / `loaded_module_times`；对象到模块的映射再由 `ULuaModuleLocator` 负责 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-641`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:13-19,151-169,560-624`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:21-37`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp:18-65` | 把 source、cache、locator 分层后，模块热更、对象绑定和文件来源扩展不会反向污染一个中心结构 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前单体 `FAngelscriptModuleDesc` 拆成 `ModuleSource`、`CompiledArtifact`、`LiveModuleState` 三层，第一阶段保留兼容 facade，不立即改外部 API |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptModuleSourceRecord`（`Relative/Absolute path`、`ProcessedCode`、`Imports`、`GeneratedCode`）、`FAngelscriptCompiledModuleArtifact`（`CodeHash`、`CombinedDependencyHash`、`Classes/Enums/Delegates`、`PostInitFunctions`、`Precompiled fingerprint`）、`FAngelscriptLiveModuleState`（`asCModule*`、reload flags、diagnostics/test discovery state）。<br>2. 让 `FAngelscriptPreprocessor::FFile` 首先产出 `ModuleSourceRecord`，再由 `Preprocess()` 生成 `CompiledModuleArtifact`；原 `FAngelscriptModuleDesc` 暂时只做 facade，内部改为持有这三个子对象，保证旧调用点还能编译。<br>3. 把 `PrecompiledData` 指针、`bLoadedPrecompiledCode`、`CodeHash` 等迁到 `CompiledModuleArtifact`，把 `ScriptModule`、`bModuleSwapInError`、测试发现结果迁到 `LiveModuleState`；`SwapInModules()` 与 `ModulesByScriptModule` 只面向 `LiveModuleState`。<br>4. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3427-3529` 这段“复制旧模块再手工清空”的热更逻辑改成“复制 artifact/source，重建 live state”，减少字段级 reset。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h/.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/` 增加回归：同一 source 能生成多个 artifact fingerprint；rollback 时 source/artifact 不应因 live state 失败被污染；测试发现结果只在 commit 后进入 live layer。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 风险不在“拆结构体”本身，而在旧代码大量默认把 `ModuleDesc` 当成 live object 使用；第一阶段必须用 facade 兜住，不能一次性要求所有调用点理解三层对象 |
| 兼容性 | 向后兼容。第一阶段保留 `FAngelscriptModuleDesc` 外观和现有 getter，旧脚本、旧 `import` 语法、旧 test hook 都不需要迁移；变化主要在内部存储分层 |
| 验证方式 | 1. 跑现有 `Preprocessor`、`Core`、`HotReload` 测试，确认 facade 模式下行为不变。<br>2. 增加 hot reload 回归，验证复制依赖模块时不再需要手工清空大批 runtime 字段。<br>3. 构造 analyze-only / precompiled miss 场景，确认 source/artifact 可独立存在而不要求 live `ScriptModule` 已创建。 |

### Arch-SL-11：`CompileModules()` 只有“立即执行并尽量回滚”的大事务，没有 first-class `CompileRequest/CompileTransaction`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译-换入是否存在显式 request / transaction 边界，从而支持 `lint`、预检查、后台 warmup、`analyze-only` 或未来 optimizer |
| 当前设计 | 对外 API 只有 `InitialCompile()`、`CompileModules()` 和 `Stage1-4`；初编译与热更都临时拼出数组后直接进入 `CompileModules()`，该函数内部同时负责建计划、编译、引用替换、class generation、commit/rollback、diagnostics、测试发现与失败重试 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:176-185` — 公开入口只有 `InitialCompile()`、`CompileModules()`、`CompileModule_*_Stage1-4()`，没有独立 request/transaction 类型；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2038-2088,2253-2280,2468` — `InitialCompile()` 与 `PerformHotReload()` 都是现场构造 `ModulesToCompile` / `FileList` 后立即调用 `CompileModules()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3078-3087,3427-3529,3551-3758` — `CompileModules()` 自己维护 `CompiledModules`、`ModulesToUpdateReferences`、`ScriptUpdateMap`，并在编译过程中直接复制依赖模块、更新旧模块引用与 template instances；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3141-3147,4259-4299` — 在是否 commit 尚未确定前就已经把旧模块从 engine availability 移走、创建新的临时 `asCModule` 并尝试套用 precompiled data；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3894-4064,4066-4180` — 同一函数尾部继续执行 `ClassGenerator.Setup()`、`SwapInModules()`、rollback reverse-map、`PostCompile`、测试发现、`PreviouslyFailedReloadFiles/QueuedFullReloadFiles` 更新 |
| 优点 | 大事务路径集中，今天的“一处编译，统一回滚”语义很清楚；对当前 hot reload 稳定性友好 |
| 不足 | 缺少 first-class transaction 后，外部无法请求“先分析再决定是否 commit”；`lint`/optimizer 只能插到 live compile 主链里承担副作用风险；后台预热、启动期 warmup、按需模块激活前的预检查也都不得不复用会改 `ActiveModules`/失败队列的主路径 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `genRequire()` 以一次 `require(moduleName)` 为边界，局部维护 `localModuleCache`、`moduleInfo`、`m`；若执行失败，马上把 `localModuleCache[moduleName]` 与 `moduleCache[key]` 置回 `undefined`，回滚范围严格限定在本次请求 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-150,183-191,205-245`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4079-4130` | 即使没有“编译器事务”这个名字，模块请求本身就是一等事务对象，search/load/execute/rollback 边界清晰 |
| UnLua | `reload_modules()` 先把目标模块集放进 `tmp_modules`，`sandbox.enter(tmp_modules)` 后逐个 `sandbox.load()/xpcall()`；任一模块失败就 `sandbox.exit()` 并返回，只有全部成功才 `update_modules()` 正式提交 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:129-176,560-624` | 显式区分“验证候选模块集合”和“真正提交到 live cache”，这正是 `analyze-only`、批量验证和安全热更需要的事务边界 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不改变现有默认行为的前提下，把 `CompileModules()` 内部大事务显式化为 `CompileRequest -> CompileTransaction -> Commit/Rollback` 三层，并先提供 `AnalyzeOnly` 兼容入口 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptCompileRequest`（`CompileType`、`RequestedModules`、`DirtyFiles`、`ECommitMode(Commit/AnalyzeOnly)`、`Reason`）和 `FAngelscriptCompileTransaction`（`CompiledModules`、`ModulesToUpdateReferences`、`ScriptUpdateMap`、diagnostics snapshot、reload outcome）。<br>2. 让 `InitialCompile()` 与 `PerformHotReload()` 只负责收集输入并构造 `CompileRequest`；当前 `CompileModules()` 先变成 `ExecuteCompileTransaction(Request)` 的兼容包装。<br>3. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3078-4180` 中的本地状态迁入 `CompileTransaction`，并拆成 `PreparePlan()`、`BuildArtifacts()`、`ValidateReload()`、`Commit()`、`Rollback()` 五段；第一阶段内部逻辑保持原样，只是边界显式化。<br>4. 新增 `AnalyzeOnly` 模式：允许执行预处理、Stage1-4、`ClassGenerator.Setup()` 与 diagnostics 收集，但跳过 `SwapInModules()`、`UpdateScriptReferencesInUnrealData()`、测试发现、失败队列写入；输出结构化 `Outcome` 给调用方。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h/.cpp` 增加 `BeforeCommit`/`AfterAnalyze` 钩子，让未来 `lint`、optimizer、warmup profiler 直接消费 transaction，而不是侵入 live reload 主链。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：`AnalyzeOnly` 不得改变 `ActiveModules`；失败 rollback 后 `ModulesByScriptModule` 与旧 bytecode 必须恢复；自定义 `lint` phase 能读取 transaction diagnostics 却不触发 full reload 队列。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 如果 transaction 边界切错，最容易出现“AnalyzeOnly 仍偷偷改 live engine”或“Commit 少做一步导致旧行为退化”；第一阶段必须坚持包装现有逻辑而不是重写算法 |
| 兼容性 | 向后兼容。默认 `CompileRequest` 走 `Commit` 模式，行为应与今天完全一致；`AnalyzeOnly`、`BeforeCommit` 等入口全部为 opt-in，不影响现有脚本项目 |
| 验证方式 | 1. 增加 `AnalyzeOnly` 自动化测试，确认调用前后 `ActiveModules`、`ModulesByScriptModule`、`PreviouslyFailedReloadFiles` 都不变。<br>2. 构造热更失败用例，确认 transaction rollback 后旧模块引用图与 bytecode 未被污染。<br>3. 加一个只读 `lint` hook，验证它能读取 request/transaction diagnostics 并阻断 commit，而无需修改现有编译阶段代码。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-10 | 模块数据模型分层、source/artifact/live state 解耦 | 内部数据模型重构 | 高 |
| P1 | Arch-SL-11 | compile request / transaction 边界、analyze-only 能力 | 编译事务显式化 | 高 |

---

## 架构分析 (2026-04-08 14:57)

### Arch-SL-12：engine 解析仍依赖 ambient context 与进程级 tick owner，生命周期 owner 不是一等 runtime identity

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译-加载-执行链路在多 `GameInstance`、多 `World`、测试 override 或未来多 runtime 并存时，如何稳定解析“当前 engine” |
| 当前设计 | 当前 engine 解析优先走 `FAngelscriptEngineContextStack`，其次走 `UAngelscriptGameInstanceSubsystem::GetCurrent()`，而后者又依赖 `GAmbientWorldContext`；tick owner 只用一个进程级 `ActiveTickOwners` 计数仲裁 fallback tick |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:287-295,391-416,437-506` — `SyncAmbientWorldContextFromCurrentEngine()` 会把 ambient world 回写为当前 engine 的 world，`FAngelscriptEngineContextStack` 只是一个 push/pop 栈；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:718-733,746-766,781-806` — `TryGetCurrentEngine()` 先 peek 栈，再通过 `GameInstanceSubsystem::GetCurrent()` 取 engine，`Get()/GetScriptRootDirectory()/GetPackage()` 都要求调用点已经有正确 ambient engine；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:12-29,32-41,94-118` — subsystem 初始化时优先接管 ambient engine，否则自建 `OwnedEngine`，`GetCurrent()` 通过 `GetAmbientWorldContext()` 找 world，`HasAnyTickOwner()` 只看静态 `ActiveTickOwners`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-24,138-166,186-194` — runtime module 启动时若没有当前 engine 就创建 `OwnedPrimaryEngine` 并 push 到 context stack，fallback ticker 仅依据“是否存在任意 tick owner”决定是否驱动 `TryGetCurrentEngine()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2843-2846` — `ShouldTick()` 只检查 `Engine != nullptr`，并不表达 engine 与 owner 的绑定关系 |
| 优点 | 单 engine editor 流程实现简单；测试和临时 override 可以直接靠 scope push/pop 接管上下文，迁移成本低 |
| 不足 | engine identity 不是显式句柄，而是“谁在 stack 顶部/当前 ambient world 指向谁”；这会让多实例并存、嵌套编译、后台工具链或未来 `ByGameInstance`/`ByProfile` runtime 选择都变成隐式时序问题，且 `ActiveTickOwners` 的进程级计数无法表达“哪个 engine 被哪个 owner 驱动” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `IUnLuaModule::GetEnv(Object)` 始终经由 `EnvLocator->Locate(Object)` 解析 env；`ULuaEnvLocator_ByGameInstance` 维护 `GameInstance -> FLuaEnv` 映射，`FLuaEnv::FindEnvChecked(lua_State*)` 又能从 VM 句柄反查所属 env | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:104-149,152-157,284-292`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:18-25,40-92`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:61-65`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:219-222` | 把 runtime identity 绑定到 `Object/GameInstance` 和 `lua_State`，而不是 ambient 全局状态；owner、执行 env、热更入口都共享同一套定位契约 |
| puerts | `FJsEnvGroup` 显式持有多个 `FJsEnvImpl` 实例，并提供 `SetJsEnvSelector()` 决定对象落到哪个 env；执行态又能通过 `FJsEnvImpl::Get(v8::Isolate*)` 从 isolate 反查所属 env | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:95-132,139-183`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:257-259` | 先把“有多少 runtime”“对象归属哪个 runtime”建模成一等对象，再让执行栈从 runtime handle 反查上下文，而不是依赖全局 current env |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 singleton 路径为默认兼容层的前提下，引入显式 `engine registry + runtime handle`，让 owner、tick 和上下文解析围绕 runtime identity 运转，而不是围绕 ambient 全局状态运转 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptRuntimeHandle` 与 `FAngelscriptEngineRegistry`，至少维护 `RuntimeId`、`Engine*`、`OwningGameInstance`、`TickOwnerCount`、`WorldContextObject`。<br>2. 让 `UAngelscriptGameInstanceSubsystem::Initialize()` 注册/获取所属 `RuntimeHandle`，而不是直接把 `PrimaryEngine` 视为 ambient singleton；`GetCurrent()` 第一阶段可继续从 `GAmbientWorldContext` 找 handle，但内部应返回 handle 所属 engine，而不是直接回到 `TryGetCurrentEngine()`。<br>3. 把 `ActiveTickOwners` 从单个静态计数替换为 per-handle 计数；`FAngelscriptRuntimeModule::TickFallbackPrimaryEngine()` 只驱动“没有世界 owner 的 handle”，避免一个 `GameInstance` 抢占掉全局 fallback lane。<br>4. 将 `FAngelscriptEngineContextStack` 降级为 scoped override 机制，只在测试、命令式嵌套调用或兼容旧 API 时短暂覆盖当前 handle；同时为所有命中 ambient fallback 的路径增加 verbose 诊断，帮助清理隐式依赖。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加多 `GameInstance` / nested scope / fallback tick 回归，验证两个 runtime 互不串扰，且未注册 handle 的旧路径仍保持今天的单例行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是旧代码里大量默认假设“当前 engine 总能被 ambient 解析出来”；第一阶段如果过早移除 stack fallback，会把隐式依赖一次性暴露成运行时断言 |
| 兼容性 | 向后兼容。默认 registry 只注册一个 `PrimaryRuntime`，没有显式 runtime 配置时行为保持现状；`FAngelscriptEngineContextStack` 继续存在，作为 legacy/scoped override 适配层 |
| 验证方式 | 1. 新增双 `GameInstance` 测试，验证两个 subsystem 的 `Tick()` 与 `CompileModules()` 不会解析到同一 engine。<br>2. 新增 nested `FAngelscriptEngineScope` 测试，验证 scope 退出后 runtime handle 与 ambient world 都正确恢复。<br>3. 验证 editor fallback tick 只驱动无 owner 的 runtime，不会因为另一个 `GameInstance` 存在而全局停摆。 |

### Arch-SL-13：`Preprocessor` 与编译开关仍从 ambient engine 抽取，缺少不可变 `CompileProfile`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译-预处理输入是否能被显式建模和复现，以便支持自定义 compile phase、缓存 fingerprint、后台分析或并行/嵌套编译 |
| 当前设计 | `FAngelscriptPreprocessor` 构造和 `Preprocess()` 过程直接读取“当前 engine”的布尔状态与 `ConfigSettings`；`InitialCompile()` 只 new 一个裸 `Preprocessor`，没有显式传入 compile profile 或 request-scoped settings |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:10-24` — 预处理器公开面只有默认构造、`Preprocess()` 与可变的 `PreprocessorFlags`，没有 profile/request 参数；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:38-73` — 构造函数直接通过 `ShouldUseEditorScriptsForCurrentContext()`、`IsSimulatingCookedForCurrentContext()`、`IsForcingPreprocessEditorCodeForCurrentContext()` 以及 `UAngelscriptSettings::PreprocessorFlags` 组装 flags；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:212-215,232-238,4338-4346` — `Preprocess()` 直接读取 `FAngelscriptEngine::Get().ConfigSettings`，并依赖 `ShouldUseAutomaticImportMethodForCurrentContext()` 决定 import 排序策略；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:684-706` — “当前 compile context”的 editor/automatic import 开关本质上来自 `TryGetCurrentEngine()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1280-1294,1415-1431` — engine 初始化时把 `bAutomaticImports` 从默认 settings 拷到成员，再写入 `asEP_AUTOMATIC_IMPORTS` 并准备 root discovery；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2038-2082` — `InitialCompile()` 创建 `FAngelscriptPreprocessor Preprocessor;` 后直接跑预处理，没有把 profile 显式下传 |
| 优点 | 现有调用点简单，默认工程几乎不需要理解 compile profile 概念；编辑器与命令行运行模式自动复用同一套 settings |
| 不足 | 编译输入不是一等对象，而是 runtime 当前状态的投影；这使 `lint`、优化 pass、shadow compile、预热分析、跨 engine 对比编译都难以稳定复现同一组输入，也让未来 cache key、诊断结果和 phase hook 很难准确说明“我是在哪个 compile profile 下得到这个结果的” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime 配置把 `StartupModuleName`、`EnvLocatorClass`、`ModuleLocatorClass` 暴露成显式 settings；`FLuaEnv` 初始化时按顺序安装 `LoadFromCustomLoader` / `LoadFromFileSystem` / `LoadFromBuiltinLibs` searcher，用户还能通过 `AddLoader()` 向 env 实例追加自定义 loader | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:31-33,47-52`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:78-100,227-245,526-588` | 先把“这个 env 用什么启动模块、什么定位器、什么 loader 链”显式挂在 env/profile 上，再让执行期沿用这份契约，而不是临时读 ambient 全局状态 |
| puerts | `FJsEnv` 构造函数直接接收 `IJSModuleLoader`、logger、debug port、flags 等环境参数；`IJSModuleLoader` 把 `Search/Load` contract 明确成接口，`modular.js` 再围绕这套接口维护 `moduleCache`、`genRequire()` 和 `forceReload()` | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:61-70`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-71,105-150,205-245` | 环境参数和 loader policy 在 runtime 创建时就固定下来，后续 search/load/cache/reload 都消费同一份 profile，不必依赖“当前 env 恰好是谁” |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入不可变 `FAngelscriptCompileProfile`，把预处理 flags、automatic import 策略和 runtime mode 从 ambient engine 状态里提取出来，作为 compile request 的显式输入 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptCompileProfile`，字段至少包含 `bUseEditorScripts`、`bAutomaticImports`、`bSimulateCooked`、`bForcePreprocessEditorCode`、规范化后的 `PreprocessorFlags`、`bScriptDevelopmentMode`、可选 `ProfileId/ProfileVersion`。<br>2. 让 `FAngelscriptEngine` 在初始化阶段一次性从 `UAngelscriptSettings`、runtime config 和 registry/runtime handle 组装 `ActiveCompileProfile`；`InitialCompile()`、hot reload 和未来 `AnalyzeOnly` 都通过 `FAngelscriptCompileRequest` 显式携带该 profile。<br>3. 改造 `FAngelscriptPreprocessor` 构造函数为接收 `const FAngelscriptCompileProfile&`；`Preprocess()` 不再调用 `FAngelscriptEngine::Get()` 读取 `ConfigSettings`，而是消费 request/profile 中的设置快照。<br>4. 保留 `ShouldUseEditorScriptsForCurrentContext()` / `ShouldUseAutomaticImportMethodForCurrentContext()` 作为兼容适配层，但内部只转发到“当前 runtime handle 的 active profile”，逐步清理直接读 ambient engine 的旧路径。<br>5. 让 precompiled cache、phase registry 和 diagnostics 记录 `ProfileId/ProfileVersion`，这样未来 `lint`、优化 pass、manifest tag 或 editor/cooked 双 profile 编译都能共用同一套身份定义。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加“双 engine、不同 profile、同一脚本集”的回归，验证 editor/cooked、automatic/manual import 与自定义 `PreprocessorFlags` 都能在不切换 ambient engine 的前提下得到稳定结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险主要在兼容阶段：旧代码里很多 helper 默认从 `TryGetCurrentEngine()` 取开关；如果 profile facade 不完整，容易出现一部分路径已切 profile，一部分路径仍读取 ambient engine，导致调试困难 |
| 兼容性 | 向后兼容。第一阶段可让默认构造 `FAngelscriptPreprocessor()` 继续从当前 engine 构造 profile，再委托到新构造函数；旧项目不需要立即感知 `CompileProfile` 概念 |
| 验证方式 | 1. 增加双 profile 回归，验证同一脚本在 `EditorProfile` 和 `CookedProfile` 下能得到不同但可重复的 `PreprocessorFlags` 结果。<br>2. 验证 `automatic imports`、`simulate cooked` 与自定义 flag 在 `AnalyzeOnly`/热更/初编译三条路径上读取的是同一份 profile 快照。<br>3. 验证无显式 profile 的旧工程仍能沿兼容构造路径编译通过，行为与当前版本一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-12 | engine identity、owner 解析与 tick 仲裁 | runtime registry / 作用域建模 | 高 |
| P1 | Arch-SL-13 | compile profile 显式化、预处理输入可复现 | profile object 增量引入 | 高 |

---

## 架构分析 (2026-04-08 15:09)

### Arch-SL-14：`Tick()` 仍是唯一 lifecycle scheduler，热更线程只产出文件变化信号，无法承载可预算的编译任务编排

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本编译-加载-执行链路是否有独立的任务调度层，从而支持 `lint`、warmup、lazy activation、优先级控制与 frame budget |
| 当前设计 | 后台线程只做低频文件变化轮询，真正的 reload 判定、`PerformHotReload()`、`DebugServer->Tick()` 与测试运行都集中在 `FAngelscriptEngine::Tick()`；无论入口来自 editor fallback tick 还是 `GameInstanceSubsystem` tick，最终都汇聚到同一个 engine tick 主循环 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1658-1700` — `StartHotReloadThread()` 先 `CheckForFileChanges()`，后台线程随后只在 `bWaitingForHotReloadResults` 置位时轮询并 `Sleep(0.001f)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2778` — `CheckForHotReload()` 从 `FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`、`QueuedFullReloadFiles` 拼出 `FileList` 后直接 `PerformHotReload()`，最后再把 `bWaitingForHotReloadResults` 重新置回 true；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2794-2835` — `Tick()` 内同时执行 hot reload 测试、按 0.1 秒节流 reload 检查、选择 `SoftReloadOnly/FullReload`，并顺带驱动 `DebugServer->Tick()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:186-194` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:81-86` — fallback tick 与 world-bound tick 都只是转发到 `CurrentEngine->Tick(DeltaTime)` |
| 优点 | 所有真正会改 UE 对象和脚本模块状态的操作都留在主线程，线程安全边界简单；旧项目也能稳定沿用“每帧驱动一次 engine lifecycle”的行为 |
| 不足 | 缺少 first-class scheduler 后，`lint`、analyze-only、warmup compile、按需激活前预检查都只能挤进同一条 tick 主链；后台线程也只会回填文件列表，不能表达“高优先级用户触发 reload”“低优先级后台预热”“仅分析不提交”这类不同任务语义 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块执行以一次 `require(moduleName)` 为一等请求，`genRequire()` 内部按 `search -> load -> execute -> cache/rollback` 处理本次请求；`forceReload()` 也以 `moduleCache` key 为粒度，不依赖全局 frame tick 轮询模块系统 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-195`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-245` | 先把“模块请求”建模成显式任务，再决定何时从宿主触发；这样 reload、lazy load、包管理策略都能复用同一个 request contract |
| UnLua | `FLuaEnv` 初始化时只安装 `package.searchers`，启动阶段的脚本激活由 `Start(StartupModuleName, Args)` 明确触发；模块解析因此主要是 request-driven，而不是依赖常驻 tick 调度 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-249` | 把“运行时有无 ready”和“某个模块何时执行”分开，宿主只在需要时发起模块请求，而不是把所有生命周期任务都塞进每帧驱动 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 tick 兼容层的前提下，引入显式 `lifecycle scheduler`，把 file watch、analyze、compile、commit、activate 变成可排队、可限流、可分优先级的任务 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptLifecycleTask` 与 `FAngelscriptLifecycleScheduler`，任务类型至少覆盖 `DetectChanges`、`AnalyzeOnly`、`CompileCommit`、`ActivateModule`、`RetryFullReload`、`Warmup`。<br>2. 把 `bWaitingForHotReloadResults`、`FileChangesDetectedForReload`、`QueuedFullReloadFiles` 逐步收敛为 scheduler 的输入队列；后台线程只产出 `FModuleChangeEvent`，不再直接隐式驱动下一次 `PerformHotReload()`。<br>3. 保留 `FAngelscriptEngine::Tick()` 作为默认 drain 点，但第一阶段只让它调用 `Scheduler.Drain(FrameBudgetMs)`；具体 compile/commit 仍然在主线程执行，避免一次性碰 UE 线程亲和性。<br>4. 对外新增 `RequestAnalyzeOnly(...)`、`RequestCompile(...)`、`RequestActivate(...)` API，让未来 `lint` phase、warmup profiler、lazy loader 和 editor commandlet 都能提交显式任务，而不是伪装成文件改动。<br>5. 新增兼容开关 `bImmediateDrainLifecycleTasks=true` 作为默认值，确保旧项目仍表现为“tick 到来时立刻处理全部任务”；后续项目可显式切到带 budget 的 drain。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加 burst file changes、rename/delete 混合、analyze-only 与 debug server 并存场景，验证 scheduler 不会丢任务或饿死高优先级 reload。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 真正的 compile/commit 仍强依赖主线程和 UE 热重载语义；如果第一阶段就试图把编译本身并行化，最容易踩到 `ClassGenerator`、`UObject` 与 debug server 的线程亲和性问题 |
| 兼容性 | 向后兼容。默认 scheduler 仍在 tick 内一次性 drain，现有项目的 hot reload 频率和 fallback tick 语义不变；新 API 全部为 opt-in |
| 验证方式 | 1. 保持现有 hot reload、actor lifecycle、debug server 测试通过，确认默认模式行为不变。<br>2. 新增 scheduler 回归，验证 `AnalyzeOnly`、`Warmup` 与 `CompileCommit` 的优先级和 frame budget 生效。<br>3. 构造短时间多文件 burst change，用日志与断言确认 change event 不会在 `bWaitingForHotReloadResults` 迁移后丢失。 |

### Arch-SL-15：模块入口与调试源标识仍退化为物理文件路径，生成代码和默认代码没有 first-class source identity

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本模块是否拥有独立的 `entry/debug source identity`，从而支持生成代码、虚拟模块、manifest entry、按包缓存与稳定的 editor/debug 定位 |
| 当前设计 | `FAngelscriptModuleDesc` 只保存若干 `FCodeSection` 的文件路径和代码文本；预处理期间产生的 `GeneratedCode` 与 `DefaultsCode` 最终仍被归并到原文件诊断名下，而模块级错误、`UASClass/UASFunction` 的源码定位又统一回落到 `Module->Code[0]` |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1278-1288` — `FCodeSection` 只有 `RelativeFilename`、`AbsoluteFilename`、`Code`、`CodeHash`，没有 `EntryPath`、`DebugPath`、`ChunkName` 或 fragment identity；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:289-301` — 预处理结束后只把 `File.RelativeFilename/AbsoluteFilename/ProcessedCode` 装入 `Module->Code`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:717-722,1362-1373` — delegate helper 与 `DefaultsCode` 等生成片段会被追加或归并，但没有单独 source identity；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4234-4294` — `FileWideError/LineError/ChunkError/MacroError` 全部以 `File.AbsoluteFilename` 记 diagnostics；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4944-4954` — 模块级 `ScriptCompileError(Module, ...)` 也直接退化到 `Module->Code[0].AbsoluteFilename`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1497-1520,1535-1545` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:280-298` — `UASClass`/`UASFunction`/`UClass::GetScriptModuleName()` 的 editor-facing 定位同样依赖 `ScriptTypePtr->GetModule()` 再取 `Code[0]` |
| 优点 | 对今天“一模块基本对应一个物理 `.as` 文件”的主路径来说实现直接，日志和 editor 跳转都容易落到真实磁盘文件 |
| 不足 | 一旦引入 manifest entry、虚拟模块、cache replay、generated module 或更细粒度的 generated/default fragments，现有模型就无法表达“逻辑模块入口是谁、调试名是什么、错误属于哪个生成片段”；这会让 editor 跳转、诊断聚合、热替换日志和未来 custom loader 都继续被迫伪装成磁盘文件 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `searchModule()` 返回 `[fullPath, debugPath]`，`executeModule(fullPath, script, debugPath, ...)` 在执行时同时保留运行路径与调试路径；遇到 `package.json` 时再根据 `main/exports` 解析真正入口 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:56-71`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:132-168` | 把“实际载荷来源”和“调试/入口语义”分开保存，包入口、pak、bytecode 与普通脚本就能共用一套模块 API |
| UnLua | custom loader 与附加 loader 都通过 `FCustomLuaFileLoader`/`Loader.Execute(...)` 同时返回字节流和 `ChunkName`，`LoadString()` 因此始终知道当前 chunk 的显示身份 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22-34`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-589` | 先把 chunk/debug identity 抬成 loader contract，再让 diagnostics、stack trace 与自定义来源统一消费，不需要退化成某个物理文件路径 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `FCodeSection` 之外补一层 first-class `module source identity`，显式记录逻辑入口、调试名和生成片段来源，先修复诊断/导航使用点，再扩展到 resolver 和 custom loader |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 为 `FAngelscriptModuleDesc` 新增 `FModuleSourceIdentity`，字段至少包含 `ModuleId`、`EntryPath`、`DebugPath`、`DisplayName`、`PrimarySectionIndex`，并允许可选 `GeneratedOriginId`。<br>2. 让 `FAngelscriptPreprocessor::FFile` 在生成 `GeneratedCode`、`DefaultsCode` 时附带 `FSourceFragment` 元数据；第一阶段不必拆代码缓冲，只需要能说明“这段 diagnostics 来自主文件 / generated helper / defaults fragment”。<br>3. 把 `ScriptCompileError(Module, ...)`、`UASClass::GetSourceFilePath()`、`UASFunction::GetSourceFilePath()` 与 `Bind_UObject` 的 `GetScriptModuleName()` 改为优先读取 `Module->SourceIdentity`，仅在缺省时回退到 `Code[0]`。<br>4. 后续与 manifest/resolver 对接时，让声明式入口模块直接设置 `EntryPath`，而虚拟/缓存模块可以提供 `DebugPath` 或 `DisplayName`，不再强制伪造磁盘路径。<br>5. 在 debug server diagnostics 消息中新增可选 `DebugPath/DisplayName` 字段；旧客户端仍可继续消费现有 `Filename`，新客户端再逐步切换。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加 generated delegate/defaults diagnostics、in-memory source、manifest entry 三类回归，验证 source identity 与旧的一文件模块都能稳定工作。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 现有 editor/debug 消费者广泛假设 `Filename` 是真实绝对路径；第一阶段如果贸然把它改成虚拟名，可能导致文件跳转和旧诊断面板失效，因此必须先“双轨输出，旧字段保留” |
| 兼容性 | 向后兼容。未显式提供 `SourceIdentity` 的模块继续退回 `Code[0]`；debug server 新字段为追加而非替换，旧工具链无需立刻升级 |
| 验证方式 | 1. 运行现有 diagnostics/source-navigation 回归，确认物理文件模块行为不变。<br>2. 构造 generated delegate/defaults 失败用例，验证 diagnostics 能区分主文件与生成片段来源。<br>3. 为模拟 custom loader/in-memory 模块设置 `DebugPath`，确认 debug server 和 editor source getter 返回稳定显示名而不是空路径。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-14 | lifecycle 调度层、tick 耦合与任务预算 | scheduler / request queue 增量引入 | 高 |
| P2 | Arch-SL-15 | 模块入口、调试路径与生成代码来源标识 | source identity 补强 | 中 |

---

## 架构分析 (2026-04-08 15:20)

### Arch-SL-16：`CreateCloneFrom()` 共享底层 VM 与 shared state，当前 clone 更像测试视图而不是可独立演进的 runtime

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多 runtime / shadow compile / profile 对比 / future lazy activation 场景下，`clone engine` 是否真的是隔离的脚本生命周期实例 |
| 当前设计 | `CreateForTesting()` 在已有 engine 存在时默认走 `Clone`；clone 会直接复用源 engine 的 `SharedState` 与 `asIScriptEngine*`，但只复制少量环境字段，不复制完整的 `ActiveModules`/diagnostics/live module ledger |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:128-136,206,371-375,448-450` — 对外公开 `CreateCloneFrom()`、`CreateForTesting(..., Clone)` 与 `SharedState`/`CreationMode`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:628-650` — clone 直接继承 `Source.SharedState`，`bOwnsEngine=false`，并增加 `ActiveCloneCount`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:922-941,969-987` — `SharedState` 中保存的是 `ScriptEngine`、`PrecompiledData`、`StaticJIT`、`TypeDatabase`、`BindState`、`BindDatabase` 等底层对象；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1132-1226` — owner shutdown 会因为 clone 仍在引用 shared state 而延后释放；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2848-2856` — `AdoptSharedStateFrom()` 只复制 `Engine`、`ConfigSettings`、`AllRootPaths`、初始化状态等少量字段，并没有复制 `ActiveModules`、`ModulesByScriptModule` 或 reload 队列 |
| 优点 | 测试克隆成本低，不需要重新初始化 bind/type 数据和底层 VM；对当前 isolation tests 很友好 |
| 不足 | clone 既不是完全独立 runtime，也不是源 runtime 的完整镜像：它共享底层 VM/JIT/cache，却不共享完整 live module graph；因此不适合作为 `analyze-only`、variant compile、background warmup 或多 profile 对比的承载体，任何把 clone 当“第二个生命周期实例”的扩展都会踩到共享状态与局部账本不一致的问题 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnvGroup` 显式持有多个独立 `FJsEnvImpl` 实例；group 只负责 fan-out 和 selector，真实 env 仍是一组并列 runtime，而不是共享一个底层 VM 指针的“浅克隆” | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnvGroup.h:18-45`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:95-117,119-183` | 先把“共享调度器”和“独立 runtime 实例”分开，后续多 env 选择、reload、bind 路由才不会把共享层误当成 runtime 本体 |
| UnLua | `ULuaEnvLocator_ByGameInstance` 为不同 `GameInstance` 创建独立 `FLuaEnv`；`FLuaEnv` 自己持有 `lua_State` 与各类 registry，并且能从 `lua_State*` 反查所属 env | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:40-99`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:57-65,95-109,161-177`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:145-156` | runtime identity 应该绑定到独立 VM/registry，而不是绑定到“共享底层状态 + 局部壳对象”；这样对象定位、热更和生命周期归属才一致 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前 `clone` 明确降级为 legacy testing view，同时新增“共享 kernel、独立 runtime”的建模，避免继续把共享底层状态误当成可扩展的第二生命周期实例 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptRuntimeKernel`，只承载可共享且应尽量只读的 bind/type 元数据；把 `asIScriptEngine*`、`PrecompiledData` 的 live replay 状态、reload 队列、diagnostics 与 `ActiveModules` 留在 runtime 实例层。<br>2. 把现有 `CreateCloneFrom()` 明确标注为 `LegacySharedViewForTesting` 语义，第一阶段仅允许它做只读查询和兼容测试，不再把它作为 future `AnalyzeOnly` 或 warmup runtime 的默认承载体。<br>3. 新增 `CreateIsolatedFromKernel(...)` 或等价 API：复用 kernel 中的 bind/type 数据，但创建新的 `asIScriptEngine`、新的 module store、diagnostics store 与 lifecycle scheduler。<br>4. 调整 `CreateForTesting()`：保留 `Clone` 默认值以维持现有测试兼容，但增加显式 `Full`/`Isolated` 模式；后续需要真实独立生命周期的测试迁移到 `Isolated`。<br>5. 在 `Shutdown()` 和 shared-state release 路径里，把 `ActiveCloneCount` 仅用于 legacy shared view；isolated runtime 不应阻塞 owner VM 的释放。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：legacy clone 不得触发 `CompileModules()` 提交；isolated runtime 的 `ActiveModules/Diagnostics` 与源 runtime 隔离；owner shutdown 不再被 isolated runtime 的生命周期延后。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 最大风险是把 today 的 shared caches 拆错层级：如果把实际上依赖具体 `asIScriptEngine` 的对象误塞进 kernel，会得到“看似共享、实则悬空”的隐性生命周期 bug |
| 兼容性 | 向后兼容。`CreateCloneFrom()` 与现有测试默认路径先保留；新的 isolated runtime 为显式 opt-in，旧项目和旧测试不需要一次性迁移 |
| 验证方式 | 1. 增加 clone/isolated 双路径测试，确认 legacy clone 仍满足现有测试，而 isolated runtime 拥有独立 `ActiveModules` 与 diagnostics。<br>2. 构造两个 runtime 分别编译不同 profile/模块集合，确认不会通过 shared VM 串扰。<br>3. 验证 owner runtime shutdown 时，只有 legacy shared view 会参与 deferred release，isolated runtime 不会阻塞释放。 |

### Arch-SL-17：`PrecompiledData` 仍把“预热启动”建模成整次运行模式，缺少 mixed-mode 的模块级 artifact replay

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 预编译脚本缓存能否与 source compile / hot reload / lazy load 共存，而不是在启动时二选一 |
| 当前设计 | 当前存在两条彼此割裂的 precompiled lane：一条是在 `InitialCompile()` 里直接用 `PrecompiledData->GetModulesToCompile()` 替代预处理；另一条是在正常 source compile 的 `Stage1/Stage3` 中按模块尝试 `ApplyToModule_*`。一旦走前者，整次运行就通过 `bUsedPrecompiledDataForPreprocessor` 全局禁掉 hot reload |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2038-2057` — `InitialCompile()` 在满足条件时直接跳过预处理，改为 `PrecompiledData->GetModulesToCompile()`，并提示“Hot reloading is disabled for this run”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2730-2733` — `CheckForHotReload()` 只要发现 `bUsedPrecompiledDataForPreprocessor` 就立刻 early return；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4247-4304,4381-4390` — 正常编译路径又支持按模块命中 `PrecompiledData` 的 `ApplyToModule_Stage1/Stage3`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h:423-469,621` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:1417-1424,2752-2783` — precompiled cache 既能保存模块级 artifact，也能在启动时一次性重建 `ModuleDesc` 列表 |
| 优点 | shipping / cooked 冷启动路径非常直接，完全跳过磁盘扫描与预处理时延；对“整包全预编译、整次运行不做开发期变更”的场景稳定性高 |
| 不足 | 模块级 artifact replay 与 run-global precompiled mode 没有统一 contract；这让系统无法表达“多数模块从 cache 启动，但单个脏模块仍允许 source reload”“lazy activation 时优先用 artifact，不命中再落回 source”“开发期带 warm cache 但仍保留细粒度热替换”这类 mixed-mode 生命周期 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require()` 的同一条模块请求链里同时支持 source 与 bytecode；`.mbc/.cbc` 只是 `search/load/execute` 的一种载荷形态，`moduleCache` 和 `forceReload()` 仍按模块 key 工作，不会因为某次 bytecode 命中就全局关闭 reload | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-71,105-191,205-245` | 让 artifact/source 共享同一模块请求 contract，缓存命中只是“这个模块这次怎么加载”，而不是“这个进程以后只能怎么跑” |
| UnLua | `loaded_modules`、`package.loaded` 与 `loaded_module_times` 都是按 `module_name` 维护；`reload_modules()` 与 `M.reload()` 只对修改过的模块集合做 reload，没有 run-global 的“缓存命中后禁用后续热更”模式 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:151-170,553-624` | 即使底层没有 bytecode cache，reload contract 依然是模块级的；source/artifact policy 应该挂在模块 identity 上，而不是挂在整个 runtime 模式上 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `PrecompiledData` 从“启动模式开关”改造成“模块级 artifact catalog”，允许同一 runtime 同时承载 precompiled replay、source compile 与后续 lazy activation |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h/.cpp` 新增 `FPrecompiledArtifactCatalog` 或等价抽象，把当前 `GetModulesToCompile()` 返回的 descriptor 视作模块目录，而不是视作“本次运行直接替代预处理”的单独模式。<br>2. 用模块级 `EArtifactOrigin(Source/PrecompiledDescriptor/PrecompiledCode)` 替换 `bUsedPrecompiledDataForPreprocessor` 的 run-global 语义；`InitialCompile()` 只决定初始模块集如何 materialize，不再决定整次运行能否 hot reload。<br>3. 调整 `CheckForHotReload()`：删除全局 early return，改为按 dirty module / dependency closure 失效对应 artifact；脏模块与其依赖闭包可重新走 source preprocessor，而未脏模块继续保留 precompiled 状态。<br>4. 让后续 `EnsureModuleCompiled()` / `RequestActivate()` / lazy loader 先查 artifact catalog，再决定是 `ApplyToModule_*` 还是落回 source compile，这样 warm start 与按需加载能复用一条模块请求链。<br>5. 第一阶段增加兼容开关，例如 `bEnableMixedPrecompiledRuntime=false`；默认继续保持今天“fully precompiled disables hot reload”的行为，待 mixed-mode 路径验证稳定后再逐步开放。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：fully precompiled 启动后改一份脚本应只 source-compile 脏模块闭包；其余模块仍可沿 artifact replay；lazy activation 场景下未触发的模块不应在启动期被 materialize。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | mixed-mode 的核心风险在于 artifact descriptor 与 source descriptor 必须能描述同一逻辑模块；如果两条路径的 module identity、diagnostics identity 或 dependency closure 不一致，就会出现“同一模块被 cache/source 双重实例化”的隐蔽错误 |
| 兼容性 | 向后兼容。默认仍可保留 today 的 fully precompiled 模式；mixed-mode 通过显式设置开启，旧项目和 shipping 配置不需要改变 |
| 验证方式 | 1. 增加“预编译启动后改单文件”的回归，验证仅脏模块及其依赖闭包回到 source pipeline。<br>2. 验证 mixed-mode 下 `CheckForHotReload()` 不再全局失效，而未脏模块仍保持 artifact replay。<br>3. 对比关闭/开启 `bEnableMixedPrecompiledRuntime` 的启动耗时、首次模块激活耗时和热更行为，确认兼容模式保持当前表现。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-17 | precompiled warm start 与 hot reload/lazy load 的 mixed-mode 共存 | artifact catalog / 模块级失效 | 高 |
| P1 | Arch-SL-16 | clone/shared-state 语义、隔离 runtime 能力 | runtime kernel / isolated runtime | 高 |

---

## 架构分析 (2026-04-08 15:33)

### Arch-SL-18：`import` 解析把依赖图固定成预处理期 DAG，缺少 `Resolving/Loaded/Failed` 级模块状态机

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 循环依赖、重入加载与 future lazy activation / hot replace 所需的模块中间态建模 |
| 当前设计 | `FAngelscriptPreprocessor` 在预处理阶段用 DFS 解析 `import`，遇到回边立即报错；编译阶段只有在全部 imported module 都已找到后才进入 `Stage1`，没有“模块正在解析中”的一等缓存条目 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:439-494` — `ProcessImports()` 以 `bIsResolvingImports` 作为单一递归保护，检测到环时直接报 `Detected circular import`，并且只有在所有 imports 递归完成后才把当前文件加入 `OutSortedFiles`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3171-3208` — `CompileModules()` 先把 `ImportedModules` 全部解析成 `FAngelscriptModuleDesc`，缺失时直接 `ScriptCompileError`，成功后才调用 `CompileModule_Types_Stage1()` |
| 优点 | 依赖顺序确定、错误暴露早，当前 `Stage1-4` 不需要处理半初始化模块，编译实现简单 |
| 不足 | 模块系统只能表达“未解析 / 已解析 / 报错”三态，不能表达 `Resolving`、`LoadedPlaceholder`、`FailedButCached` 这类 runtime module loader 常见状态；一旦未来要支持互相引用的 bootstrap 模块、按需激活、模块级 warmup 或更细粒度 hot replace，就必须继续把这些需求绕回文件级拓扑排序 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require()` 会先查 `localModuleCache/moduleCache`，miss 时先创建 `m = { exports = {} }` 并立刻写入 cache，再执行模块体；失败时才回滚为 `undefined`；`forceReload()` 也是按模块 key 施加状态，而不是重建整张拓扑图 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:107-190`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-225` | 先把“模块请求状态”建模为可缓存对象，再在其上叠加 execute/reload 语义；这样 `Resolving` 和 `ReloadPending` 都是 loader contract 的一部分 |
| UnLua | `FLuaEnv` 先注册 `package.searchers`，自定义 loader 返回 `Data + ChunkName`；热更侧的 `require()` 与 `reload_modules()` 都围绕 `package.loaded/loaded_modules` 这组 runtime cache 工作，而不是在加载前先构造一个静态无环拓扑 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-100`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-589`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:129-169`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:554-624` | 即使底层语言特性不同，也应先有“请求模块 -> loader/searcher -> cache entry”的统一状态机，再决定是否允许循环、如何 reload |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不改变默认无环语义的前提下，把当前硬编码 DFS 升级为显式 `module resolve state machine`，让“检测环”和“是否允许以占位状态继续”成为可配置策略 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h/.cpp` 新增 `FAngelscriptModuleLoadEntry` 与 `EModuleResolveState(Unseen, Resolving, Loaded, Failed)`，把今天的 `bImportsResolved/bIsResolvingImports` 收敛到统一 cache entry。<br>2. 新增 `IAngelscriptImportResolutionPolicy`；默认 `StrictAcyclicPolicy` 完整复用今天的报错行为，保证旧工程零行为变化。<br>3. 在 `ProcessImports()` 中先查询/创建 `ModuleLoadEntry`，将“发现 back-edge 时直接报错”改成委托给 policy；第一阶段即使仍然报错，也先把中间态保存下来，供 diagnostics、analyze-only 和后续工具链消费。<br>4. 第二阶段新增实验性 `CycleAwarePolicy`：只对声明期可解析的依赖组返回 placeholder entry，并把 strongly connected component 记录成一个 compile group；`Stage1/Stage2` 先处理组内声明，`Stage3/Stage4` 再按组提交，避免全局变量初始化穿透未完成模块。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 为 compile result 增加 `CycleDetected/CycleDeferred` 级诊断，避免 future scheduler 或 lazy loader 只能看到泛化的 compile failure。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：默认 strict 模式下 `A -> B -> A` 仍报现有错误；cycle-aware 模式下声明型双向引用可编译；带全局初始化副作用的环仍被拒绝并给出明确诊断。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 真正的风险不在 DFS 改写本身，而在 AngelScript 的类型声明、函数体编译、全局初始化三段是否能安全拆成“组内声明先行、执行后置”；如果边界划错，会把今天简单明确的 compile failure 变成更隐蔽的半初始化错误 |
| 兼容性 | 向后兼容。默认仍保持当前“检测到循环即报错”的严格模式；`CycleAwarePolicy` 作为显式 opt-in，只对有需求的项目开放 |
| 验证方式 | 1. 回归当前所有 `import` 与 hot reload 测试，确认 strict 模式行为完全一致。<br>2. 新增双向依赖样例，分别覆盖声明期可解与全局初始化不可解两种场景。<br>3. 在 debug diagnostics 中验证 `Resolving/Failed` 状态会被保留，而不是统一塌缩成 generic compile error。 |

### Arch-SL-19：模块边界仍是“整模块导入 + 事后限制”，缺少 first-class export surface 与 `PublicApiHash`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块 public/private 边界、依赖失效粒度，以及热更时 importer 是否必须跟着 provider 的实现细节波动 |
| 当前设计 | `import` 记录的只有 `ModuleName`；编译阶段把整个 imported module 注入当前模块；访问控制靠 `#restrict usage allow/disallow` 与编译后 `CheckUsageRestrictions()` 做通配校验，而不是在模块解析时定义显式 export contract |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:101-107` — `FImport` 只有 `ModuleName` 与位置信息，没有 symbol list / export scope；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3171-3208` — `CompileModules()` 按模块名解析 import 后直接把整个 `ImportedModules` 数组交给 `Stage1`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4247-4281` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4420-4423` — `CompileModule_Types_Stage1()` 对每个 import 调用 `ImportIntoModule()`，并把 imported module 的 `CombinedDependencyHash` 整体 XOR 进当前模块；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3363-3392` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4523-4596` — `#restrict usage` 只是记录 wildcard pattern，真正校验发生在模块编译完成后遍历 `moduleDependencies`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3367-3412` — 热更时 provider 的 structural / hard-value dependency 变化会向 importer 递推升级 reload state |
| 优点 | 语言表面简单，旧脚本不需要维护导出清单；全模块导入也让当前绑定和类型可见性规则比较直观 |
| 不足 | 当前没有 first-class `export` 概念，导致“模块公开 ABI”“私有实现细节”“允许谁依赖我”被混在一起；`#restrict usage` 更像编译后 lint，而不是解析期 contract；热更和缓存也只能围绕整模块 dependency hash 运作，难以实现 provider 纯实现改动不牵动 importer 的细粒度失效 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | CommonJS/ESM 统一落到 `m.exports`；遇到 `package.json` 时还能通过 `packageConfigure.exports` 选择真正暴露的入口；cache 与 reload 都以 module key 为中心，但模块对外 ABI 是显式 `exports` 面 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:144-185`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-225` | 把 public surface 从“整个文件默认可见”收束成显式 exports 后，reload/caching 可以围绕对外 ABI 而不是围绕全部内部实现细节工作 |
| UnLua | `require()` 返回的是模块 chunk 执行后的 `new_module` / env；`reload_modules()` 只对指定 `module_name` 集合重建并更新模块表，调用方依赖的是模块返回值，而不是脚本文件里全部顶层定义 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-169`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:554-624` | 即使没有 `package.json` 这类元数据，也可以先把“模块对外可见对象”建成一等值，再让 reload/invalidation 只围绕这个可见面传播 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先在不改旧语法的前提下补一层可选 `public surface` 元数据与 `PublicApiHash`，把 today 的 export-all 语义降级为兼容默认值，而不是唯一模型 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 为 `FAngelscriptModuleDesc` 新增 `FModulePublicSurface`、`PublicApiHash`、`ImplementationHash`；未声明 public surface 的模块继续把全部现有可导入成员视为 public，保证兼容。<br>2. 第一阶段不改语言语法，只在模块旁支持可选 sidecar（例如 `Foo.Bar.asmodule.json`）或已有 manifest 扩展字段，声明 `exports`、`friends`、`deprecatedSymbols`；`FAngelscriptPreprocessor` 读取后填充 `FModulePublicSurface`。<br>3. 把 `ImportIntoModule()` 前的 import 解析改为“先校验当前 import 是否命中对方 public surface / friend 规则，再构造导入描述”；`#restrict usage allow/disallow` 继续保留，但逐步退化为兼容层与诊断提示，而不是唯一权限机制。<br>4. 在热更与 precompiled fingerprint 路径中引入 `PublicApiHash`：provider 仅 `ImplementationHash` 变化时，importer 优先走 `UpdateReferences` 或跳过重编；只有 `PublicApiHash`、hard-value ABI 或显式友元规则变化时，才升级 importer 的 compile/reload requirement。<br>5. 对 editor/debug diagnostics 增加“访问了 private symbol / friend 限制不满足 / public surface 失效”的专门消息，避免继续混入 generic compile error。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：无 sidecar 的旧模块继续 export-all；声明 private symbol 后 importer 会在解析期失败；provider 纯函数体修改不应再无条件牵动所有 importer。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 如果 public surface 与 AngelScript 实际 import 机制之间没有一个清晰的 stub/descriptor 层，容易出现“元数据说 private、底层仍然可见”的双重语义；因此第一阶段必须先把校验放在解析期，并保持导入失败可诊断 |
| 兼容性 | 向后兼容。没有 sidecar/manifest 扩展时继续等价于今天的 export-all；`#restrict usage` 保留并逐步映射到新 surface 规则，不强制老脚本立即迁移 |
| 验证方式 | 1. 回归旧项目，确认没有 public surface 元数据时编译结果与 today 一致。<br>2. 增加 private symbol / friend import / deprecated symbol 三类解析期诊断测试。<br>3. 对比启用 `PublicApiHash` 前后的热更行为，验证 provider 纯实现改动时 importer 的重编范围缩小。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-19 | 模块 public/private 边界与失效粒度 | public surface / `PublicApiHash` 增量引入 | 高 |
| P2 | Arch-SL-18 | 循环依赖与重入加载状态机 | resolver policy / SCC-aware loader | 中 |

---

## 架构分析 (2026-04-08 16:47)

### Arch-SL-20：启动执行仍是 `PostInitFunctions` 的 best-effort 串行调用，缺少 first-class `BootRequest/BootResult`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本执行入口能否表达“启动哪个模块/函数、带什么参数、失败后如何反馈/重试” |
| 当前设计 | 当前公开生命周期 API 停在 `Initialize()` / `InitialCompile()` / `CompileModules()` / `Tick()`；真正的“模块启动”被折叠进 `ClassGenerator` 末端的 `PostInitFunctions`，按字符串函数名逐个执行，没有显式 `Start()`、启动参数或结构化结果对象 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:162-205` — `FAngelscriptEngine` 对外公开的是初始化、编译、hot reload 与 tick，没有单独的 startup/boot API；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1305-1306` — 模块只保存 `TArray<FString> PostInitFunctions`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2302-2304` — `CallPostInitFunctions()` 被硬编码在 `InitDefaultObjects()` 之前执行；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5775-5803` — 启动逻辑只是按名字在 `globalFunctionList` 里查找函数并 `Context->Execute()`，既不传参，也不消费执行结果，`bFound` 也不会汇总成外部可见状态；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4132-4133` — 当前自动写入 `PostInitFunctions` 的主要场景是 literal asset getter；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653-1655` — engine 级只有一次性的 `OnInitialCompileFinished` 广播，没有“某个入口启动成功/失败”的生命周期协议 |
| 优点 | 启动时序简单，且与 class generation/asset materialization 串在同一主线程事务里，默认行为稳定 |
| 不足 | 无法表达 `StartupModuleName`、`EntryFunction`、`Args`、`BootPolicy`、`BootResult` 等概念；大型项目难以做分阶段 warmup、按 runtime 传入依赖对象、记录启动失败边界，后续若要做 lazy activation 或启动诊断，只能继续复用“字符串函数列表 + 日志”这一隐式通道 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv` 把启动入口做成显式 `Start(const FString& StartupModuleName, const TMap<FString, UObject*>& Args)`；启动时通过 `require` 调用指定模块，并把 `Args` 组装成 Lua table 传入 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:63-65`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-252` | 启动入口、参数注入和“env 已启动”状态是 first-class contract，不必把启动副作用塞进编译后回调 |
| puerts | `FJsEnvImpl` 先在 bootstrap 阶段拿到 `puerts.__require`，随后 `Start(const FString& ModuleNameOrScript, const TArray<TPair<FString, UObject*>>& Arguments)` 显式注入 `argv` 参数并 `require` 指定入口；异常也由 `TryCatch` 收敛到明确日志 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:621-635`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551` | 先把 bootstrap 与 boot request 分开，再让每次启动请求带入口和参数；这样入口切换、测试替身和错误采集都可编排 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不破坏现有 `PostInitFunctions` 的前提下，引入显式 `BootRequest/BootResult`，把“启动脚本”从 class generator 内部副作用升级为 runtime API |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptBootRequest`（至少包含 `EntryModuleId`、`EntryFunction`、`Args`、`BootPhase`、`bAllowRetry`）与 `FAngelscriptBootResult`（至少包含 `Succeeded`、`ExecutedEntries`、`Diagnostics`、`bCanRetry`）。<br>2. 新增 `BootRuntime(const FAngelscriptBootRequest&)` / `BootModule(...)` API；默认实现先复用当前 `FAngelscriptContext` 和函数查找逻辑，但把结果汇总进 `BootResult`，不要再让调用方只能读日志。<br>3. 第一阶段把现有 `PostInitFunctions` 适配成 `LegacyImplicitBootPhase::PreCDO`：`ClassGenerator` 不再直接裸调字符串列表，而是构造 `BootRequest` 调 `BootRuntime()`；默认顺序仍保持“`CallPostInitFunctions()` 位置不变”。<br>4. 在 `UAngelscriptSettings` 或后续 manifest 中新增可选 `StartupEntries`，允许项目声明 `ModuleId + Function + ArgsSource`；未声明时继续只执行 legacy `PostInitFunctions`。<br>5. 为启动失败补充专门诊断：区分“入口不存在”“入口 prepare 失败”“入口执行抛错”“入口被策略跳过”，并把这些状态暴露给 future lazy loader、editor diagnostics 与 automation tests。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：旧工程 `PostInitFunctions` 顺序保持不变；显式 `BootRequest` 能传入 `UObject` 参数；某个入口失败时 `BootResult` 能准确标记是否允许重试且不污染未执行入口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 关键风险是启动时序兼容性：当前 literal asset getter 依赖“在 `InitDefaultObjects()` 之前执行”，如果 `BootPhase` 设计不清，会把原本稳定的 asset/materialization 顺序打乱 |
| 兼容性 | 向后兼容。默认仍保留 legacy `PostInitFunctions` 路径；只有显式调用 `BootRuntime()` 或声明 `StartupEntries` 的项目才会进入新协议 |
| 验证方式 | 1. 回归现有 initial compile / hot reload / asset getter 相关测试，确认 legacy 顺序不变。<br>2. 新增 `BootRequest` 参数传入测试，验证脚本入口能稳定读取宿主对象。<br>3. 构造部分入口失败场景，确认 `BootResult`、diagnostics 与后续 retry 行为一致。 |

### Arch-SL-21：执行阶段把 context pooling 固化成公共契约，缺少可插拔 `Executor/Middleware` seam

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本执行层是否支持插入 tracing、budget、timeout、cancel、audit 或替换执行器，而不必改写底层 VM/context 池 |
| 当前设计 | 当前 engine 初始化时就把 `AngelscriptRequestContext/AngelscriptReturnContext` 注册为底层 VM callback，并把 thread-local/global context pool 暴露为核心执行机制；外部执行 API 只有 `PrepareExternal()` / `ExecuteExternal()` 这类薄包装，没有 first-class `ExecutionRequest`、`ExecutionResult` 或 middleware |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:394-398` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:654-661` — runtime 直接持有 `GlobalContextPool` 与 thread-local `FAngelscriptContextPool`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:910-911` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1422-1423` — 创建 engine 时立即 `SetContextCallbacks(&AngelscriptRequestContext, &AngelscriptReturnContext, nullptr)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1722-1750` — `AngelscriptRequestContext()` / `AngelscriptReturnContext()` 只围绕池化复用做分配归还，没有执行元数据或策略对象；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1785-1792` — `PrepareExternal()` 与 `ExecuteExternal()` 只是直接调用底层 `Prepare/Execute`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1797-1806` — 嵌套执行通过 `PushState()` 直接复用当前 active context，说明“如何执行”与“如何优化 context 复用”已经被锁成同一层实现 |
| 优点 | 性能路径短，nested call 复用现有 AngelScript context 也比较高效；对当前单 runtime / 主线程驱动模型足够直接 |
| 不足 | 一旦需要引入 `timeout`、`frame budget`、执行审计、per-call tracing、异常分类、debug correlation id 或可替换 executor，当前架构没有可插拔接缝；调用方只能直接触碰 pooled context，导致优化策略和执行策略强耦合 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | JS 运行时把真正的模块执行收束到 `executeModule(fullPath, script, debugPath, sid, isESM, bytecode)`；`genRequire()` 在 `search/load` 后显式调用这个执行函数，因此执行边界天然携带模块路径、调试名和 module sid | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:56-71`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:183-185` | 先把执行请求建模成显式函数边界，再把 cache、debug 和 reload 元数据都挂到这层，而不是埋进 VM 复用细节 |
| UnLua | loader 侧无论来自 custom loader 还是 file system，最终都走 `LoadString(..., ChunkName)`；chunk identity 作为执行 contract 的一部分向下传递，而不是只在底层 VM 内部隐式存在 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:136-145`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-589`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:597-611` | 让执行入口显式接收 `ChunkName/FullPath` 这类上下文，便于调试、loader 扩展与未来的 middleware/telemetry |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 pooled context 降级为默认执行器的内部优化细节，在其外层补一层显式 `ExecutionRequest/ExecutionResult` 与可选 middleware 链 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptExecutionRequest`（至少包含 `RuntimeId`、`ModuleId`、`DebugName`、`asIScriptFunction*`、`WorldContext`、`Tags`、`DeadlineMs`、`bAllowNestedReuse`）和 `FAngelscriptExecutionResult`（至少包含 `State`、`DurationMs`、`ExceptionString`、`bTimedOut`）。<br>2. 新增 `IAngelscriptExecutor` 接口；默认实现 `FPooledContextExecutor` 内部继续使用现有 `GlobalContextPool` / `GAngelscriptContextPool` 与 `PushState()` 语义，保证行为兼容。<br>3. 把 `FAngelscriptPooledContextBase::PrepareExternal()` / `ExecuteExternal()` 标记为 legacy low-level API，并逐步让 `CallPostInitFunctions()`、console 调用、测试 helper、脚本对象重初始化等路径改为统一走 `Execute(const FAngelscriptExecutionRequest&)`。<br>4. 在 executor 外侧增加可选 `IAngelscriptExecutionMiddleware` 链，第一阶段至少支持 tracing/profiling hook 与 timeout budget hook；不启用 middleware 时应退化成 today 的零额外分支。<br>5. 保留 `Engine->SetContextCallbacks(...)`，但只由 `FPooledContextExecutor` 管理；后续若要做 analyze-only executor、sandbox executor 或 record/replay executor，可以并存于同一 runtime。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：legacy executor 与 pooled executor 的结果一致；嵌套执行仍保持 `PushState()` 语义；开启 tracing middleware 后能拿到 `ModuleId/DebugName/Duration` 而不影响脚本行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险在 nested execution 兼容性：当前 `PushState()` 允许复用 active context，如果新 executor 抽象没有把这层语义建模进去，最容易在重入调用和异常回卷上引入隐蔽回归 |
| 兼容性 | 向后兼容。默认 executor 继续使用当前 pooled context；旧调用点可通过适配层继续拿到 `PrepareExternal/ExecuteExternal` 语义，再逐步迁移到新 request API |
| 验证方式 | 1. 回归现有 console、class generator、script object reinit 与 hot reload 测试，确认默认 executor 行为不变。<br>2. 新增 nested execution 用例，验证 `PushState()` 仍生效。<br>3. 新增 tracing/timeout middleware 用例，验证能采集执行元数据且不会破坏脚本结果。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-20 | 启动入口、参数注入与失败协议 | `BootRequest/BootResult` 增量引入 | 高 |
| P2 | Arch-SL-21 | 执行器抽象、context pool 解耦与 middleware | `Executor/Middleware` 扩展层 | 中 |

---

## 架构分析 (2026-04-08 16:59)

### Arch-SL-22：`CompileModules()` 末端仍会实例化模块并执行脚本副作用，`compile` 与 `instantiate` 没有硬边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译管线可扩展性，以及能否支持 `lint-only`、`analyze-only`、lazy compile 和安全预热 |
| 当前设计 | 当前 `CompileModules()` 在同一条事务里不仅完成编译，还会重置模块 globals、执行 `PostInitFunctions`，并初始化脚本类默认对象；也就是说，“编译完成”基本等价于“模块已被实例化并发生副作用” |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3724-3757` — 在更新旧模块引用前，先为新模块 `BuildAllocateGlobalVariables()` 并把 `GlobalVariablePointers` 写入 `ScriptUpdateMap`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4403-4410` — `CompileModule_Globals_Stage4()` 在编译阶段末尾直接 `ResetGlobalVars(0)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3894-3970` — `CompileModules()` 成功后立即进入 `ClassGenerator.Setup()` 与 `PerformSoftReload()/PerformFullReload()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2302-2304` — reload 末端直接 `CallPostInitFunctions()` 和 `InitDefaultObjects()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5775-5800` — `CallPostInitFunctions()` 会真实 `Prepare...` 并 `Context->Execute()` 脚本函数 |
| 优点 | 一次 `CompileModules()` 就能得到可运行的类、默认对象和 literal asset getter，当前 editor/runtime 主链实现简单，兼容既有行为 |
| 不足 | 自定义 `lint`、优化 pass、模块预热、后台分析、lazy load 都无法稳定停在“仅编译、不执行脚本”的阶段；模块 globals 初始化和 `PostInitFunctions` 仍会把副作用带进编译事务，后续想做按需激活时也必须先穿过这条重路径 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `genRequire()` 明确分成 `searchModule -> loadModule -> executeModule -> moduleCache`；只有 `require()` 真正命中模块请求时，才执行 `executeModule(...)`，`Search/Load` 本身不触发业务模块执行 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:92-123`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-71`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-185` | 先把 `compile/load` 与 `execute/activate` 拆开，后续 lazy load、warm cache、分析模式都能共享同一条前半段管线 |
| UnLua | `LoadFromCustomLoader()` / `LoadFromFileSystem()` 只把 `Data + ChunkName` 交给 `LoadString()`；真正执行 chunk 的时机在 `require()` 里 `xpcall(func, ...)`，并在成功后才写入 `loaded_modules/package.loaded` | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-611`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-169` | loader 只负责“把 chunk 准备好”，实例化与缓存提交是独立步骤；这正是 compile-only / execute-later 能成立的前提 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持默认行为不变的前提下，把 `CompileModules()` 产物拆成 `Compiled/Linked` 与 `Instantiated/Activated` 两层，新增显式模块实例化步骤 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptInstantiationRequest`、`FAngelscriptInstantiationResult` 和模块级 `ELifecycleState(Preprocessed/Compiled/Linked/Instantiated/Activated)`，先只作为内部状态模型。<br>2. 把 `CompileModule_Globals_Stage4()` 的职责拆开：第一阶段保留 globals 分配与链接，但把 `ResetGlobalVars(0)` 挪到新的 `InstantiateCompiledModules()`；`PostInitFunctions` 与 `InitDefaultObjects()` 也从 `ClassGenerator.Setup()` 尾部迁移到该实例化步骤。<br>3. 在 `CompileModules()` 增加 `bAutoInstantiate=true` 的兼容参数或 runtime setting，默认继续走今天的“编译后立刻实例化”行为；分析、lint、lazy compile 场景显式传 `false`。<br>4. 让后续 `EnsureModuleCompiled()` / `BootRuntime()` / lazy activation 只在真正需要运行模块时调用 `InstantiateCompiledModules()`，从而复用同一份已编译结果。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：compile-only 不执行 `PostInitFunctions`；compile-only 不触发 globals init 副作用；默认 `bAutoInstantiate=true` 时旧项目行为与当前一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 关键风险是初始化顺序兼容性：literal asset getter、CDO 初始化和 soft/full reload 目前依赖“编译后立刻实例化”的顺序；拆分时必须把这些依赖显式建模，否则很容易引入“编译成功但运行态不完整”的新隐患 |
| 兼容性 | 向后兼容。默认仍可保持 `CompileModules()` 自动实例化；只有显式启用 analyze/lazy 路径的项目才会看到新生命周期状态 |
| 验证方式 | 1. 回归现有 initial compile、hot reload、asset getter 与 class generation 测试，确认默认路径无行为变化。<br>2. 新增 compile-only 测试，断言脚本副作用函数与 globals initializer 不会被调用。<br>3. 新增 lazy activation 测试，确认模块可先编译后实例化，且第一次激活时行为与今天一致。 |

### Arch-SL-23：热更主链当前只维护 symbol/reference remap，缺少 first-class `module state handoff` 协议

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块热替换时，模块级 globals、静态缓存和脚本单例能否被显式迁移，而不是靠隐式重初始化 |
| 当前设计 | 当前模块描述主要保存 `Code`、依赖和 `PostInitFunctions`；热更主链显式做的是 global variable address 重映射、bytecode reference update 和随后 `ResetGlobalVars(0)`，但在已读主链中没有看到与 `moduleCache/exports` 或 state snapshot/merge 对等的一等状态容器 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1272-1306` — `FAngelscriptModuleDesc` 只保存 `Code`、`ImportedModules`、`PostInitFunctions` 等编译/启动元数据，没有模块状态迁移接口；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3724-3745` — 热更时为新模块重新分配 globals，并仅把旧/新 `GetAddressOfValue()` 写入 `ScriptUpdateMap.GlobalVariablePointers`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3748-3757` — `UpdateReferencesInScriptBytecode()` 只更新字节码中的引用目标；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4403-4410` — `Stage4` 随后直接 `ResetGlobalVars(0)`；推断：在本轮已读的 reload 主链里，可见的 state contract 仍然是“重建并重绑引用”，而不是“保存并迁移模块状态” |
| 优点 | 当前策略保守、实现清晰；默认把热更风险压在“符号/类型引用是否兼容”而不是更难控制的对象图迁移上，对结构变化更安全 |
| 不足 | 纯代码级 hot reload 也缺少显式的模块状态延续点；脚本 globals、模块级 singleton/cache 若需要保留，只能依赖手工再构建或散落在宿主对象里，模块系统自身无法表达“这个模块支持状态迁移、那个模块必须冷启动” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块运行态被显式挂在 `moduleCache[key] = m`，其中 `m.exports` 就是一等的模块状态容器；`forceReload()` 也只标记某个 cache entry 的 `__forceReload`，而不是让状态隐藏在 VM 内部匿名 globals 里 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-146`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-225` | 即使不做深层对象合并，至少先把“模块活状态”建成显式 cache object，reload policy 才能围绕模块状态而不是围绕文件批次工作 |
| UnLua | `reload_modules()` 在 sandbox 中生成 `new_module` 后，`update_modules()` 会匹配旧模块值、函数和 upvalues，最后 `update_global(all_value_maps)` 把运行中引用更新到新模块对象 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:554-624` | 把“模块状态迁移”做成显式步骤后，热更不再只是重新执行 chunk，而是可以按模块声明的兼容边界更新运行态 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先给模块系统补一个可选的 `state handoff` 层，默认仍走今天的冷重置；只有显式声明兼容的模块才参与 globals/state restore |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 为活动模块新增 `FAngelscriptModuleRuntimeState` 或等价结构，至少包含 `ReloadPolicy`、`SnapshotVersion`、`CustomStateAdapterName`；不要把这层直接塞回 `FAngelscriptModuleDesc` 的预处理元数据里。<br>2. 新增可选 `IAngelscriptModuleStateAdapter`（C++）或脚本侧 `OnBeforeModuleReload/OnAfterModuleReload` 约定；第一阶段只支持保守场景，例如 POD-like globals、命名单例表、宿主对象引用句柄，不碰任意脚本对象图迁移。<br>3. 在 `CompileModules()` 热更成功、但 `ResetGlobalVars(0)` 之前，为 `ReloadPolicy != ResetAlways` 的模块抓取 snapshot；通过当前 `ReloadState == RecompiledOnlyCodeChanges`、类型布局未变等保守条件决定是否允许 restore。<br>4. restore 失败时退回今天的冷启动路径，并把失败原因写入专门 diagnostics；不要让 state migration 失败污染整批模块 commit。<br>5. 默认 `ReloadPolicy = ResetAlways` 保持现状；只有显式 opt-in 的模块才启用 state handoff。等后续实现更细粒度 ABI 判定时，再把门槛从“仅 code-only change”升级到独立 `ApiHash`/layout compatibility。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：默认策略下 globals 继续冷重置；opt-in 模块在 code-only reload 后能保留指定 globals；结构变化时自动拒绝 restore 并回退到冷启动。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 最大风险是把不兼容的脚本对象图错误地当成“可迁移状态”恢复回来；因此第一阶段必须极度保守，宁可多回退到冷启动，也不能让损坏状态静默进入运行时 |
| 兼容性 | 向后兼容。默认继续保持今天的 `ResetGlobalVars(0)` 语义；state handoff 为显式 opt-in，旧脚本和旧项目不需要修改 |
| 验证方式 | 1. 回归现有 soft/full reload 与 property/class 保持性测试，确认默认路径不变。<br>2. 新增 code-only reload state-restore 测试，验证 opt-in 模块的指定 globals 能保留。<br>3. 新增结构变化场景，验证系统会拒绝 restore、给出诊断并退回冷启动语义。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-22 | 编译与实例化边界、compile-only/lazy compile 能力 | `Compile`/`Instantiate` 分层 | 高 |
| P1 | Arch-SL-23 | 模块热更状态迁移与模块级 state contract | 可选 `state handoff` 层 | 高 |

---

## 架构分析 (2026-04-08 17:05)

### Arch-SL-24：`FAngelscriptPreprocessor` 仍是单次批处理工作区，缺少跨请求复用的 `module catalog`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 预处理阶段能否复用模块发现/import 图/脏状态，而不是每次编译请求都从头构造一个临时 batch |
| 当前设计 | 当前 `FAngelscriptPreprocessor` 是一次性对象：`AddFile()` 只能在 `Preprocess()` 前调用，`Preprocess()` 只能执行一次，完成后只吐出一批 `ModuleDesc`；`InitialCompile()` 与 hot reload 都会新建一个 preprocessor、重新塞入文件列表、重新解析 import 并丢弃中间状态 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:91-99` — `AddFile()` 在加入文件时立即创建 `FAngelscriptModuleDesc`，且 `bIsPreprocessed` 后禁止再加文件；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:77-83` — `GetModulesToCompile()` 只允许在 preprocess 完成后整体取出模块数组；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:220-238` — `Preprocess()` 只能跑一次，并会在 explicit import 模式下直接把全局 `Files` 数组重排成 `SortedFiles`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2068-2082` — `InitialCompile()` 每次都新建 `FAngelscriptPreprocessor`、枚举全部脚本文件后再 `Preprocess()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2448-2455` — hot reload 也重新构造 preprocessor 并把 `FilesToHotReload` 重新喂进去 |
| 优点 | batch 语义简单、实现直观；每轮预处理互相隔离，不容易因为残留状态把旧错误带进新一轮编译 |
| 不足 | 预处理阶段没有长寿命 `module catalog`，导致 import 图、模块哈希、删除 tombstone 与已知脏模块都只能存在于单次 batch 里；未来要做 lazy compile、后台 analyze、IDE 导航或“只补一个缺失依赖”的请求式编译时，都不得不重建整套 `Files` 工作区 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 运行时长期持有 `moduleCache`，单次 `require()` 只在局部创建 `localModuleCache` 和当前模块对象；模块发现/缓存是 env 级状态，请求级逻辑只消费这份 catalog | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-55`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-147`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-146` | 把“长寿命模块目录/缓存”和“本次请求的局部执行栈”拆开，后续 lazy load、reload 与失败隔离都围绕 catalog 工作，而不是围绕一次性文件数组工作 |
| UnLua | `FLuaEnv` 初始化时只一次性安装 `package.searchers`；后续模块状态长期保存在 `package.loaded`、`loaded_modules` 中，每次 `require(module_name)` 只查询/补齐缺失项，而不是重建 loader 工作区 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170` | 即使底层 loader 可能来自文件系统或自定义来源，也应先有 env 级 catalog/cache，再让单次模块请求围绕 dirty module 或 miss entry 工作 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 engine 内引入长寿命 `FAngelscriptModuleCatalog`，把今天一次性 preprocessor batch 的发现状态抽出来；`FAngelscriptPreprocessor` 降级为“基于 catalog 的 request session” |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleCatalog` 与 `FAngelscriptCatalogEntry`，至少保存 `ModuleName`、`SourceIdentity`、`LastKnownImports`、`LastProcessedHash`、`DirtyReason`、`bDeleted`。<br>2. 把 `InitialCompile()` 的磁盘扫描结果先注册到 catalog，再由 `FAngelscriptPreprocessorSession` 从 catalog 挑选初始模块集进行 preprocess；第一阶段默认仍然把全量脚本都标记为 dirty，以保持现有行为。<br>3. 把 hot reload 的 `FileChangesDetectedForReload` / `AlreadyDeletedFiles` 先映射到 catalog entry，再根据 catalog 中缓存的 import 图扩闭包；只有闭包内模块需要重新 materialize 成本轮 session 的 `FFileWorkItem`。<br>4. 将 `ProcessImports()` 对 `Files` 的原地排序改成“读取 catalog 中的依赖关系生成本轮 compile order”，避免 request session 继续承担全局目录的角色。<br>5. 对外新增内部 API `EnsureCatalogEntryUpToDate(ModuleName)`；未来的 `EnsureModuleCompiled()`、IDE analyze、lazy activation 都先问 catalog，而不是各自再 new 一个 preprocessor。<br>6. 保留今天的兼容路径：若 `bEnablePersistentModuleCatalog=false`，engine 仍可在内部创建临时 catalog 并立即销毁，保证旧项目和测试不变。<br>7. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：全量初编译与兼容模式结果一致；单文件 hot reload 只重建依赖闭包；删除后再恢复文件时 catalog 能稳定保留 tombstone/恢复状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是 catalog 与 live compile session 的一致性维护：若删除、rename、precompiled replay 与 explicit import 排序没有统一写回 catalog，最容易出现“catalog 认为模块有效，但本轮 session 看不到源码”的双重真相 |
| 兼容性 | 向后兼容。默认仍可维持今天的 batch 全量行为；persistent catalog 先作为内部实现替换，不要求用户修改脚本语法或目录结构 |
| 验证方式 | 1. 回归现有 initial compile / hot reload / delete-restore 测试，确认兼容模式无行为变化。<br>2. 新增 request-session 测试，验证第二次编译只为 dirty closure 创建 `FFileWorkItem`。<br>3. 在 debug 日志中输出 catalog 命中/失效信息，确认懒编译或 analyze-only 请求不会重新扫描全部脚本根。 |

### Arch-SL-25：语法 sugar lowering 仍靠 late whole-text rewrite，并在预处理期直接注入 runtime 副作用

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 预处理管线能否把语法 lowering、生成代码与 runtime initialization 解耦，从而安全插入自定义 syntax pass、lint 或优化器 |
| 当前设计 | 预处理前半段仍以 `ChunkedCode` 为主，但在 `CondenseFromChunks()` 把所有 chunk 拼成单个 `ProcessedCode` 后，又执行 `PostProcessRangeBasedFor()` 和 `PostProcessLiteralAssets()` 这种整段字符串正则改写；其中 literal asset lowering 不仅改源码，还会直接往 `Module->PostInitFunctions` 写入 runtime 启动动作 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:190-195` — 预处理器内部显式区分 `CondenseFromChunks()` 与两个 hardcoded post-process；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:265-287` — `OnProcessChunks` 之后先处理 defaults、再 `CondenseFromChunks()`，随后固定调用 `PostProcessRangeBasedFor()`、`PostProcessLiteralAssets()`，最后才广播 `OnPostProcessCode`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3983-4005` — `CondenseFromChunks()` 先串接全部 chunk，再把 `GeneratedCode` 直接追加到尾部；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4008-4087` — `PostProcessRangeBasedFor()` 通过 `FRegexMatcher` 在整段 `ProcessedCode` 上重写 `for (...) : ...` 语法；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4089-4143` — `PostProcessLiteralAssets()` 也是整段文本替换，并在 `4132-4133` 直接 `File.Module->PostInitFunctions.Add(TEXT(\"Get\") + AssetName)` |
| 优点 | 不需要额外 parser/IR，即可快速支持语法糖和 generated helper；当前内建行为集中在一个文件里，调试入口明确 |
| 不足 | lowering 既不纯也不稳定：自定义 pass 若想在 literal asset 或 range-based for 之后工作，只能重新解析被改写过的整段字符串；而 literal asset 还把 runtime boot side-effect 藏进预处理阶段，使“语法扩展”和“执行生命周期”在同一层耦合，后续做 lint-only、optimize-only 或可选语法插件时风险很高 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块请求主链是 `searchModule -> loadModule -> executeModule`；`executeModule(fullPath, script, debugPath, sid, ...)` 明确是执行边界，加载阶段只负责拿到脚本/bytecode，不会在 loader 阶段顺手改写模块启动列表 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:56-71`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-191` | 即使需要 bootstrap、cache 和 reload，load 阶段仍保持“拿到载荷”的纯 contract；执行副作用放到显式 execute/start 边界 |
| UnLua | `LoadFromCustomLoader()` / `LoadFromFileSystem()` 只负责把 `Data + ChunkName` 交给 `LoadString()`；真正执行 chunk 与写入 `package.loaded` 发生在 `require()`/`Start()` 中，而不是发生在 loader 本身 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-609`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:127-170` | 把 source loading 与 module activation 分层后，自定义 loader/searcher 不必理解启动策略；生命周期副作用由更高层显式协调 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把语法 lowering 从“post-condense 的全文正则替换”升级为“基于 chunk/token span 的 lowering artifact”，并把 runtime 初始化动作单独收集为 `InitializationArtifact` |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` 新增 `IAngelscriptSyntaxLoweringPass`、`FAngelscriptLoweringResult` 和 `FAngelscriptInitializationArtifact`；前者只允许返回 `ReplacementSpans`、`GeneratedFragments`、`Diagnostics` 和 `InitArtifacts`，不直接碰 `Module->PostInitFunctions`。<br>2. 把 `PostProcessRangeBasedFor()` 和 `PostProcessLiteralAssets()` 迁成内建 lowering pass：第一阶段仍可复用现有正则实现，但它们应作用在独立 `GeneratedFragments`/`ReplacementSpans` 上，而不是直接覆写整段 `ProcessedCode`。<br>3. 让 `CondenseFromChunks()` 只负责串接“已经 lowering 完成的 chunk + generated fragments”；不要再在 condense 之后做会改变语义的全文 rewrite。<br>4. 把 literal asset 产生的 `Get{Name}` 启动需求从 `PostProcessLiteralAssets()` 中抽出，写入 `FAngelscriptInitializationArtifact`，再由后续 `BootRuntime()`/legacy `PostInitFunctions` 兼容层统一翻译。<br>5. 保留 `OnProcessChunks` / `OnPostProcessCode` 作为兼容层，但新增更细的 `BeforeLowering` / `AfterLowering` seam；旧 hook 继续工作，新扩展优先接 lowering pass。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/` 增加三类回归：range-based for lowering 结果与当前一致；literal asset 仍按旧时序初始化资源；插入一个只读 lint pass 能同时看到 authored chunk 和 generated fragment，而不需要重新解析最终字符串。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/` |
| 预估工作量 | M |
| 架构风险 | 关键风险是时序兼容性：literal asset getter 目前依赖预处理期生成代码 + 后续 `PostInitFunctions` 执行顺序，如果 lowering artifact 与 boot artifact 的提交点切分错误，最容易引入“代码能编过，但 asset 初始化顺序变了”的隐蔽回归 |
| 兼容性 | 向后兼容。默认仍注册与今天等价的两个内建 lowering pass；现有脚本语法不变，旧 hook 继续可用，只是新扩展点鼓励走更细粒度的 lowering API |
| 验证方式 | 1. 回归现有 literal asset、range-based for、initial compile 与 hot reload 测试，确认默认 pass 顺序不变。<br>2. 新增自定义 lowering/lint pass 测试，验证其无需重解析最终字符串即可消费 authored/generated 片段。<br>3. 对比迁移前后的 `PostInitFunctions` 执行顺序，确认 legacy 路径与新 `InitializationArtifact` 翻译结果一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-24 | 预处理 catalog 生命周期、跨请求复用与 dirty closure 复用 | persistent `module catalog` | 高 |
| P2 | Arch-SL-25 | syntax lowering 纯度、生成代码与 runtime 初始化解耦 | lowering artifact / init artifact 分层 | 中 |

---

## 架构分析 (2026-04-08 17:33)

### Arch-SL-26：script root 只是搜索顺序，不是模块命名空间；跨 root 同名脚本只靠隐式优先级与晚期 `ensure`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多 script root 共存时的模块命名隔离、overlay/shadow 规则，以及版本化/私有 vendoring 能力 |
| 当前设计 | 当前模块标识仍由“相对脚本路径”直接派生，project root 与 plugin roots 只影响搜索顺序，不进入模块 identity；因此不同 root 下只要相对路径相同，就会折叠到同一个 `ModuleName` 空间 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1343-1363` — `DiscoverScriptRoots()` 只把 project root 插到首位、plugin roots 排序后附加；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp:76-90` — 测试明确钉死“project first + plugins sorted”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1999-2014,2061-2078` — 初编译把所有 root 下的 `*.as` 扫成同一批 `FFilenamePair`，`RelativeRoot` 统一从空路径开始；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:86-100` — `FilenameToModuleName()` 只把相对路径 `.as`/`/` 规范化为点号模块名；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3018-3052` — 反查时也是把绝对文件名相对到某个 root 后再重建同一个模块名；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3133-3135` — 冲突直到编译期才通过 `ensureMsgf(!CompilingModulesByName.Contains(...))` 暴露，没有 discovery/resolver 层的显式 shadow policy |
| 优点 | 当前行为可预测且便于兼容：project script 可以天然优先于 plugin script，既有工程不需要额外维护命名空间或 manifest |
| 不足 | root 只是“谁先被扫到”，而不是“这份源码来自哪个 package/root”；结果是插件无法安全携带私有同名模块，两个插件也无法并存同名相对路径的脚本目录，未来即使加入版本化模块或 mixed-mode cache，也缺少 `SourceRoot` 维度来区分真正的逻辑来源 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | loader 先以 `RequiredDir` 搜索调用者邻近目录，再逐级向上尝试 `node_modules`，最后回退到 project `ScriptRoot`；真正进入缓存的 key 是解析后的 `fullPath`，不是裸模块名 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-123`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-146` | 把“从哪里解析到这个模块”纳入模块 identity；同名 specifier 能在不同物理来源下稳定区分，而不是都压扁成一个全局名字 |
| UnLua | `LoadFromFileSystem()` 不直接假定唯一根目录，而是读取 `package.path` 模式串逐项展开；`SetPackagePath()` 允许 env 级修改该搜索面，`AddSearcher()` 还把不同来源按顺序挂进 `package.searchers` | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp:277-293`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:598-667` | 先把搜索面显式化，再让优先级与来源成为可配置 contract；模块系统不再依赖“把所有 root 平铺后恰好没有重名”这一隐式前提 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持默认 project-first 行为的前提下，把 `SourceRoot` 与 shadow policy 提升为 first-class 元数据，让“同名模块冲突”成为显式解析结果，而不是晚期 `ensure` |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleSearchRoot`，字段至少包含 `RootId`、`AbsolutePath`、`Priority`、`ShadowPolicy(ProjectOverrides/ExplicitNamespace/ErrorOnDuplicate)` 与可选 `NamespacePrefix`。<br>2. 让 `FindAllScriptFilenames()` / `FAngelscriptPreprocessor::AddFile()` 同时携带 `RootId`；`FAngelscriptModuleDesc` 或新的 source record 必须记录 `SourceRootId`，不要再只有“相对路径 -> ModuleName”这一条信息。<br>3. 默认兼容策略保留今天的 project-first 顺序，但把冲突检测前移到 discovery/preprocess：一旦两个 root 产出相同 `ModuleName`，立即生成结构化诊断，明确说明是“legacy shadow”还是“hard error”。<br>4. 为 plugin 或 package 增加可选 namespace/prefix 设置；只有显式开启前缀的 root 才把模块名展开成 `NamespacePrefix + ModuleName`，这样旧工程零改动，新插件可以 opt-in 到私有命名空间。<br>5. 把 precompiled artifact、reload journal、未来 module catalog 的 key 从裸 `ModuleName` 升级为 `LogicalModuleId + SourceRootId/Namespace`，避免 cache/reload 在同名跨 root 模块上继续互相踩踏。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 增加三类回归：project/plugin 同名脚本按 legacy policy 给出明确 shadow 诊断；两个 opt-in namespace root 可并存同名模块；cache/hot reload key 在 namespaced 模式下不再串扰。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 风险在于兼容策略：若一上来把所有 root 都强制 namespace 化，会直接改变旧工程的 `import Foo.Bar;` 语义；因此第一阶段必须保留 legacy flat namespace，只把冲突和来源信息显式化 |
| 兼容性 | 向后兼容。默认仍保持 today 的 project-first 搜索行为；namespace 与严格 duplicate error 为显式 opt-in，旧脚本无需立即改写 import |
| 验证方式 | 1. 构造 project/plugin 同名脚本，验证默认模式下行为保持不变但会新增可读诊断。<br>2. 构造两个 plugin root 下同名相对路径模块，验证启用 namespace 后可同时编译并独立 hot reload。<br>3. 对比 mixed-mode cache / reload journal 命中日志，确认 key 已区分 `SourceRootId`，不再把同名跨 root 模块视为同一实体。 |

### Arch-SL-27：`import` 只有全局规范名，没有 `requiring module` 上下文；无法做 caller-relative 与 profile-driven 搜索

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `import` 解析是否具备“从哪个模块发起请求”的上下文，从而支持相对导入、可迁移包目录和 profile 级搜索路径 |
| 当前设计 | 当前 `import` 解析只截取原始文本并把它当作最终 `ModuleName`；随后 `ProcessImports()` 在预处理批次里做全局字符串等值匹配，没有 `requiringDir`、`current module dir`、`package path` 或 alias/profile 层 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3499-3510` — `import` 语句只把 `;` 前的原始片段塞进 `ImportDesc.ModuleName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:463-482` — 解析时只在 `Files` 数组里查找 `OtherFile.Module->ModuleName == ImportDesc.ModuleName` 的精确命中；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:86-89` — 模块规范名只来自文件相对路径的 `.as -> .` 变换，没有 caller-relative 规范化步骤；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2061-2078` — 初编译也是先把所有根目录文件平铺出来再交给预处理器，没有独立的 search profile 对 import 请求做逐层解析 |
| 优点 | import 语义非常直接，旧工程里 `import Foo.Bar;` 的行为稳定，预处理器也不需要维护额外的请求上下文 |
| 不足 | 缺少 caller-aware 解析后，库模块不能相对引用同包文件，包一旦换目录或被 vendoring 到其他 root 就需要整体改 import；同时也无法做类似 `package.path` 的 profile 级搜索路径、alias 或私有依赖隔离，模块系统只能依赖全局规范名碰运气命中 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `executeModule()` 在执行时把当前模块目录 `fullDirInJs` 传给 `puerts.genRequire(fullDirInJs)`；后续 `require()` 用 `searchModule(moduleName, requiringDir)` 解析模块，默认 loader 先查调用者目录，再逐级向上查 `node_modules` | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:57-70,129-135`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:92-120` | import/require 请求天然带有 caller context；相对导入、包内依赖和上级 fallback 都建立在统一 resolver contract 上 |
| UnLua | `LoadFromFileSystem()` 先把 `module.name` 转成路径，再按 `UnLua.PackagePath` 的 pattern 列表逐项查找；`SetPackagePath()` 可在 env 上修改搜索面，`AddSearcher()` 让 custom/filesystem/builtin loader 链按顺序参与解析 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp:277-293`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:598-667` | 即使模块 key 仍是逻辑名，也应把搜索策略独立出来；模块名解析不必被硬编码成“全局名字必须已经唯一” |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有全局 `import Foo.Bar;` 语义的前提下，引入带 caller context 的 `ImportResolutionProfile`，增量支持 relative import 与可配置搜索路径 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` 新增 `FImportSpecifier`，字段至少包含 `RawSpecifier`、`NormalizedModuleId`、`EImportKind(Global/Relative/Alias/SearchPath)`、`ImporterModuleId`、`ImporterSourceRootId`。<br>2. 扩展 `ProcessImports()`：若 specifier 以 `./` 或 `../` 开头，则根据 importing module 的逻辑目录做规范化；若为 bare global name，则继续保持今天的 `Foo.Bar` 语义不变。<br>3. 在 `FAngelscriptCompileProfile` 或 settings 中新增 `ImportSearchPaths`/`ImportAliases`，顺序语义对齐 `package.path`：resolver 可按 profile 提供的 pattern/alias 尝试 fallback，而不是只做一次全局字符串匹配。<br>4. 让 resolver diagnostics 输出“请求来自哪个模块、按什么顺序尝试了哪些候选路径/root”，避免 relative import 或 alias 失配时继续只有 generic missing module 错误。<br>5. 第一阶段增加兼容开关，例如 `ImportResolutionMode=LegacyGlobal/RelativeAware`；默认仍走 legacy，全局模块名项目零行为变化，需要私有包布局的项目再 opt-in relative-aware 模式。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 增加三类回归：`import ./Sibling;` 能在搬迁包目录后继续工作；profile 中切换 `ImportSearchPaths` 会改变解析结果但不影响旧 global import；alias/relative 失配时 diagnostics 能展示完整候选链。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 主要风险是把“相对路径语义”与现有点号模块名混在一起后造成解析歧义；第一阶段必须只把 `./`、`../` 这类显式前缀视为 relative import，其余 bare name 继续按 legacy global 模式处理 |
| 兼容性 | 向后兼容。现有 `import Foo.Bar;` 不变；relative import 与 search-path alias 都是显式 opt-in，旧脚本和旧项目无需迁移 |
| 验证方式 | 1. 构造可整体搬迁的包目录，验证 relative-aware 模式下 sibling import 不需要批量改名。<br>2. 构造两套不同 `ImportSearchPaths` profile，验证同一 importer 在不同 profile 下得到可重复但不同的解析结果。<br>3. 对比 legacy 与 relative-aware 模式的 diagnostics，确认 missing import 会输出候选链和 importer 上下文。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-26 | 多 script root 的命名空间、shadow policy 与同名模块冲突 | `SourceRoot`/namespace/shadow policy 显式化 | 高 |
| P2 | Arch-SL-27 | `import` 的 caller-aware 解析与可配置搜索路径 | relative import / `ImportResolutionProfile` | 中 |

---

## 架构分析 (2026-04-08 17:43)

### Arch-SL-28：编译选项与 diagnostics 仍绑定 `engine-global` 状态，缺少 `request-scoped compile session`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译管线可扩展性，尤其是 `lint`、`analyze-only`、不同 warning/优化策略并存时的 session 隔离 |
| 当前设计 | `FAngelscriptEngine` 初始化时一次性把 AngelScript compiler property 和 message callback 写入共享 `asIScriptEngine`；后续 `CompileModules()` 只能复用同一个 engine 成员 `Diagnostics`、`CompilationLock` 和 ambient `CurrentEngine` 解析路径 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:884-910` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1372-1445` — `asEP_OPTIMIZE_BYTECODE`、`asEP_AUTOMATIC_IMPORTS`、warning flags、`asEP_BUILD_WITHOUT_LINE_CUES` 都在 engine 初始化期统一设置，`SetMessageCallback(asFUNCTION(LogAngelscriptError), 0, ...)` 也是一次性注册；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:510-529` — diagnostics 存储是 engine 级 `TMap<FString, FDiagnostics>`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3061-3130` — `CompileModules()` 只会重置并复用这份 engine 级 diagnostics map；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5012-5084` — `LogAngelscriptError()` 通过 `FAngelscriptEngine::Get()` 取当前 engine，并把编译消息直接写回 `Manager.Diagnostics` |
| 优点 | 默认编译语义集中在一个地方，项目启动后 warning/optimization 行为稳定；debug server 和 editor 弹窗也能直接复用同一份 diagnostics 存储 |
| 不足 | 自定义 `lint`/静态分析 pass 很难拥有自己的 diagnostics sink；`line cues`、bytecode optimization、warning 严格度也无法按 request 或按模块安全切换，只能修改共享 engine 状态；未来若要并存 `AnalyzeOnly`、warmup compile、profile A/B 编译或后台命令式验证，仍会继续依赖 ambient engine 与全局锁做串行复用 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnvGroup` 构造时即可注入 `IJSModuleLoader` 与 `ILogger`；`Start()` 通过当前 env 的 `Logger` 和 `TryCatch` 报告启动错误；`modular.js` 又把每次 `require()` 的失败限定在本次模块请求和对应 cache entry 内 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnvGroup.h:21-25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSLogger.h:19-25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-191` | loader、logger、module request 都是 env/request 级 contract，而不是进程内单一全局回调 |
| UnLua | `FLuaEnv` 在各自 env 上安装 `package.searchers`；loader 把 `ChunkName` 一并传入 `LoadString()`；错误上报还允许通过 `FUnLuaDelegates::ReportLuaCallError` 注入自定义 reporter | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100,557-611`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp:95-101,151-156` | 把错误收集和源码装载都做成 env 级可替换接口，工具链扩展不需要重写 VM 全局行为 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留当前默认编译配置的前提下，引入 `FAngelscriptCompileSession`，把 compiler option snapshot 与 diagnostics sink 从 `FAngelscriptEngine` 单例状态中拆出来 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptCompilerOptions` 与 `IAngelscriptDiagnosticSink`，字段至少覆盖 `bOptimizeBytecode`、`bBuildWithoutLineCues`、warning 相关 flag、`ProfileId` 与 sink 回调。<br>2. 让 `CompileModules()`、future `AnalyzeOnly`、warmup compile 和 commandlet 入口统一先构造 `FAngelscriptCompileSession`；默认 session 直接镜像今天的 runtime config，保证旧行为不变。<br>3. 保留 `SetMessageCallback()` 的全局注册，但把 `LogAngelscriptError()` 改成 trampoline：优先写入当前 thread/session 的 `IAngelscriptDiagnosticSink`，没有 active session 时再回退到现有 `Engine.Diagnostics`。<br>4. 第一阶段只允许 session 覆盖“可安全恢复”的编译属性，例如 warning、`line cues`、bytecode optimization；语义更强的语言属性先继续维持 engine 级默认值，避免一次性把所有 `asEP_*` 都做成动态切换。<br>5. 为 debug server、editor UI 和日志系统分别提供 `LegacyEngineSink`、`JsonSink`、`SilentAnalyzeSink` 适配器，让 `lint`/optimizer pass 能消费结构化 diagnostics，而不是只能读日志。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：两个不同 compile session 顺序执行时 diagnostics 不串扰；`AnalyzeOnly` 可开启严格 warning 但不污染默认 profile；session 中断或异常返回后 engine property 能正确恢复。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | AngelScript engine property 本质仍是共享可变状态；第一阶段即使有 session，也必须继续通过 compile lock 串行化，且需要确保异常路径能恢复旧 property，否则会引入更隐蔽的 profile 泄漏 |
| 兼容性 | 向后兼容。默认 session 直接复用当前 config 与现有 `Diagnostics` 存储；旧 delegate、debug server 与 editor 提示链路无需立刻迁移 |
| 验证方式 | 1. 运行现有编译/热更/diagnostics 回归，确认默认 session 下输出不变。<br>2. 新增双 session 测试，验证不同 warning/`line cues` 选项不会互相污染。<br>3. 新增 `SilentAnalyzeSink`/`JsonSink` 回归，确认 `lint` 场景可拿到结构化 diagnostics 且不触发旧 UI 副作用。 |

### Arch-SL-29：`hot reload` 决策与执行被 `ClassGenerator` 单点硬编码，缺少显式 `ReloadPlan`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块热替换能力，以及未来按模块/按运行时定制 reload policy 的扩展性 |
| 当前设计 | `CompileModules()` 在 class generation 之后直接根据 `EReloadRequirement` 分支到 `PerformSoftReload()` / `PerformFullReload()`；`ClassGenerator` 自身同时负责类型重建、`PostInitFunctions`、默认对象初始化、editor 通知、GC 与 subsystem 激活 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:23-27,44-45,143-145` — reload 公开面只有粗粒度 `SoftReload / FullReloadSuggested / FullReloadRequired` 与两个执行函数，内部还直接维护 `ReinstancedSubsystems`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3894-4004` — `CompileModules()` 对 `ClassGenerator.Setup()` 的结果立即 `switch`，并在同一处决定 `SwapInModules()`、`PerformSoftReload()`、`PerformFullReload()` 或“保持旧代码”；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2126-2304` — `PerformReload()` 串行执行 class/struct/delegate 的 full/soft reload、随后直接 `CallPostInitFunctions()` 和 `InitDefaultObjects()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2317-2461` — full reload 尾部还继续广播 `OnFullReload` / `OnPostReload`、强制 GC，并激活 `ReinstancedSubsystems` |
| 优点 | 今天的热更路径非常确定，类重建、默认对象重跑和 subsystem 重新接入都在同一事务里，减少了“编译成功但 Unreal 状态没更新”的漏网情况 |
| 不足 | 模块热替换策略被锁死在 `ClassGenerator` 的单一执行器里，外部无法表达“只 reload 某些模块但延迟 subsystem 激活”“PIE 期间先记 plan，结束后再 full reload”“针对特定模块采用更保守或更激进的 reload policy”；热更扩展点只能插在前后，无法参与 reload 决策本身 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `modular.js` 把 reload 语义限制在 `moduleCache` key 上：`forceReload()` 只标记目标模块，真正失败也只回滚当前 module cache entry；若需要多 env 同步，`FJsEnvGroup::ReloadModule()` 只是把同一个模块 reload 请求 fan-out 给各 env | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-191,205-225`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:163-168` | 先把 reload request 和作用范围显式化，再决定是否、如何把它传播到多个 runtime；reload 执行器不必强绑定类型系统副作用 |
| UnLua | `reload_modules(module_names)` 只接收明确的模块列表，在 sandbox 中逐个 load/执行，再把 old/new module table 交给 `update_modules()`；`ULuaEnvLocator_ByGameInstance::HotReload()` 也是“对每个 env 发出同一 reload 请求”，而不是硬编码单一全局 reload 流程 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-624`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:76-81` | reload scope、env scope 与具体副作用执行是分层的；模块集合可以独立于运行时宿主策略存在 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `ClassGenerator` 从“reload 决策 + reload 执行 + Unreal 副作用提交”的单点枢纽，拆成 `ReloadPlanner + ReloadExecutor` 两层，先显式化 `ReloadPlan`，再逐步开放策略扩展 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptReloadRequest`（至少包含 `CompileType`、`DirtyModules`、`bCanTouchSubsystems`、`bCanRunBootEntries`、`WorldContext`）与 `FAngelscriptReloadPlan`（至少包含 `EffectivePolicy`、`ModuleActions`、`TypeActions`、`SubsystemActions`、`DeferredActions`、`Diagnostics`）。<br>2. 让 `FAngelscriptClassGenerator::Setup()` 或新的 `FAngelscriptReloadPlanner` 只产出 plan，不再由 `CompileModules()` 直接 `switch` 到 `PerformSoftReload()` / `PerformFullReload()`；默认 executor 再按现有顺序执行该 plan，先保证行为不变。<br>3. 把 `PerformReload()` 中的关键副作用拆成命名步骤，例如 `ApplyTypeReload()`、`RunBootEntries()`、`InitDefaultObjects()`、`ReactivateSubsystems()`、`BroadcastReloadEvents()`；第一阶段这些函数内部仍可复用原实现，只是先把边界显式化。<br>4. 新增 `IAngelscriptReloadPolicy` 或 plan mutator，允许 future PIE、dedicated server、lazy activation 场景对同一 plan 做“defer subsystem activation”“forbid full reload in current phase”“queue post-PIE full reload”等策略处理。<br>5. 把 `QueuedFullReloadFiles` / `PreviouslyFailedReloadFiles` 的写入点从今天的硬编码 `ECompileResult` 分支，逐步迁成消费 `ReloadPlan.EffectivePolicy` 与 `DeferredActions`，避免继续只能用文件列表表达 reload 后续动作。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：默认 executor 与 today 的 soft/full reload 顺序完全一致；PIE 策略可把 subsystem/reactivation 延后但仍保留模块 swap-in；特定模块策略拒绝 full reload 时，plan 会给出结构化 deferred action 而不是退化成 generic compile failure。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | reload 顺序目前依赖大量隐式前提，尤其是 `CallPostInitFunctions()`、默认对象初始化、GC 与 subsystem 激活之间的时序；如果 planner/executor 第一阶段边界切得不准，最容易出现“plan 正确，但执行顺序轻微漂移”的隐蔽回归 |
| 兼容性 | 向后兼容。默认 `ReloadExecutor` 直接复用今天的 soft/full reload 顺序；只有显式注册新 policy 的项目才会看到策略差异 |
| 验证方式 | 1. 回归现有 soft/full reload、PIE、subsystem 相关测试，确认默认 executor 行为不变。<br>2. 新增 `DeferredSubsystemActivation` 测试，验证 plan 可延后 subsystem 激活但仍成功 swap-in 模块。<br>3. 构造 `FullReloadRequired` in PIE 场景，确认系统输出结构化 deferred action，而不是只依赖文件队列与日志文本。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-29 | `hot reload` 决策与执行解耦、模块级 reload policy | `ReloadPlan` / planner-executor 分层 | 高 |
| P1 | Arch-SL-28 | compile session 隔离、diagnostics sink 与 compiler option request 化 | `CompileSession` / diagnostic sink | 高 |

---

## 架构分析 (2026-04-08 17:54)

### Arch-SL-30：`CompileModules()` 仍直接消费 `ActiveModules` live 图，缺少 request-scoped dependency snapshot

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译请求是否拥有独立的依赖快照，从而支持 `AnalyzeOnly`、variant compile、lazy activation 前预检，以及未来多 runtime 并行演进 |
| 当前设计 | 当前编译阶段一边把本轮模块放入 `CompilingModulesByName`，一边直接回退到 engine 的 `ActiveModules` 取依赖 provider；依赖解析、`CombinedDependencyHash` 计算和 function import 校验都建立在这份可变的 live graph 上 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3133-3147` — 本轮模块先加入 `CompilingModulesByName`，同时把旧模块从 engine availability 移走；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3169-3208` — `Stage1` 对每个 `ImportName` 先查 `CompilingModulesByName`，找不到就直接 `GetModuleByModuleName(ImportName)` 读取 live module；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4247-4280` — `CompileModule_Types_Stage1()` 直接把 `ImportedModules` 的 `ScriptModule` 注入新模块，并把 provider 的 `CombinedDependencyHash` XOR 进当前模块；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4650-4721` — `CheckFunctionImportsForNewModules()` 先建立 `SwappingModules`，随后对新模块和所有未被替换的 `ActiveModules` 一起做 import 校验，provider 查找仍回退到 live graph |
| 优点 | 复用当前 live 模块图让实现非常直接；热更时未修改的 provider 不需要额外复制 staging graph，也便于沿用今天的 `GetModule(...)`/`GetModuleByModuleName(...)` 访问路径 |
| 不足 | 编译结果是“相对于当前 live engine”的，而不是“相对于本次 request 的依赖快照”的；这会让 `AnalyzeOnly`、profile A/B 对比、lazy compile、后台 warmup 继续受制于 `ActiveModules` 当前长什么样，也让未来想验证“同一模块集在另一套 provider 版本下会不会过编译”时只能真的去碰 live state |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 每次 `require()` 先创建 request-local 的 `localModuleCache`，命中时直接返回；miss 时先把占位 `m` 写入 `localModuleCache/moduleCache`，执行失败只回滚当前 entry，不去直接依赖某个进程级“当前活动模块表” | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-146`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:156-191` | 先把“本次模块请求可见的 provider 集合”收敛成 request-scoped cache，再决定是否提交到 env 级 cache，避免解析阶段直接耦合 live runtime 图 |
| UnLua | 热更时先把目标模块当前值复制到 `tmp_modules`，`sandbox.enter(tmp_modules)` 后在隔离环境里逐个 `sandbox.load()`/`xpcall()`；只有全部成功才 `update_modules()` 正式提交 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:558-601` | 把“候选依赖集”和“live 已加载模块集”分开，先在 request-local sandbox 里验证，再一次性发布结果 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留当前 `ActiveModules` 作为默认来源的前提下，引入 `request-scoped dependency snapshot`，让 `CompileModules()` 对依赖 provider 的观察结果在一次 request 内保持不可变 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleGraphSnapshot` 与 `FAngelscriptProviderView`，至少保存 `ModuleName`、`ScriptModule`、`CombinedDependencyHash`、`bCompileError`、`SourceRootId/ProfileId`。<br>2. 让 `InitialCompile()`、`PerformHotReload()` 和 future `AnalyzeOnly` 在调用 `CompileModules()` 前先构造 snapshot：默认从 `ActiveModules` 拷贝 provider 视图，再叠加本轮 `RequestedModules` 形成 staged graph。<br>3. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3169-3208` 的 import provider 查找改成只读 snapshot，不再直接回退到 `GetModuleByModuleName()`；`CheckFunctionImportsForNewModules()` 同样改为消费 snapshot，而不是再次走 live `ActiveModules`。<br>4. 把 `CompileModule_Types_Stage1()` 的 `CombinedDependencyHash` 聚合改成读取 snapshot 中冻结的 provider fingerprint；这样 `AnalyzeOnly`、variant compile、warmup compile 能稳定复现同一依赖视图。<br>5. commit 成功后再把 snapshot 中对应的 staged provider 发布回 `ActiveModules`；`AnalyzeOnly`/失败回滚路径只销毁 snapshot，不改 live graph。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：同一 request 内 provider 即使在 commit 前被外部修改，编译结果仍保持稳定；`AnalyzeOnly` 不依赖 ambient live graph 漂移；双 profile/双 snapshot 对同一模块集可得到不同但可重复的诊断结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是 snapshot 与 commit 之间的边界切分不清，导致一部分路径还在读 live `ActiveModules`、另一部分已经读 snapshot；第一阶段必须先把 provider 查找点收拢，再谈更多 profile/variant 功能 |
| 兼容性 | 向后兼容。默认 snapshot 只是对当前 `ActiveModules` 的冻结视图，现有脚本写法和默认编译结果不变；只有显式使用 `AnalyzeOnly`、variant compile 或未来的 lazy activation 请求时，才会真正受益于新快照模型 |
| 验证方式 | 1. 回归现有 initial compile / hot reload / precompiled cache 测试，确认默认 snapshot 模式下结果不变。<br>2. 新增 `AnalyzeOnly` 回归，验证 request 期间修改 unrelated live module 不会影响正在进行的编译诊断。<br>3. 新增双 snapshot 测试，验证同一模块在两份 provider 视图下得到各自稳定的 `CombinedDependencyHash` 与 import 校验结果。 |

### Arch-SL-31：declared function import 维护仍是 whole-engine sweep，缺少 provider-to-consumer binding index

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块变更后的 declared function import 维护粒度，是否能只影响真实 consumer，而不是把整个 engine 的 import 绑定都当成同一批工作 |
| 当前设计 | 只要本轮改动涉及模块替换，manual import 模式下就直接全量 `ResolveAllDeclaredImports()`；随后对新模块和所有未替换旧模块再做一轮 `CheckFunctionImportsForNewModules()`，provider 变更的影响面由 whole-engine sweep 决定 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4057-4064` — 代码明确在模块变更后直接“re-resolve all declared imports in all modules”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4426-4429` — `ResolveAllDeclaredImports()` 直接遍历全部 `ActiveModules`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4616-4642` — `ResolveDeclaredImports()` 对每个 module 的每个 imported function 重新按 `SourceModule + Declaration` 查 provider 并绑定；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4646-4721` — `CheckFunctionImportsForNewModules()` 不仅检查新模块，也检查所有未被替换的旧 `ActiveModules`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4705-4707` — 一旦旧模块 import 校验失败，还会把其源码文件重新加入失败重试集合 |
| 优点 | 全量 sweep 语义简单，不需要长期维护反向绑定索引；只要 engine 最终能跑完这两轮，就能保证所有 active module 的 declared import 都指向当前可见 provider |
| 不足 | provider 的局部变更会被放大成 whole-engine binding maintenance；未来一旦引入更多模块、版本化 provider、lazy activation 或多 runtime，同一类 declared import 维护会继续扩大为全局扫描，既不利于 targeted reload，也不利于把错误精确定位到“哪个 provider 影响了哪些 consumer” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `forceReload(reloadModuleKey)` 只给目标 `moduleCache` entry 打 `__forceReload` 标记；后续真正参与重载的只是这次 `require()` 命中的 key，失败回滚也只回到当前 entry | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-191`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-225` | 先把“哪个 provider 被标脏”做成显式 key，再让 consumer 在实际请求时按 key 命中，不把整个 env 的模块绑定一次性全部重算 |
| UnLua | `reload_modules(module_names)` 只遍历调用方传入的 `module_names`，`update_modules()` 只对这组 old/new module table 做状态迁移；没有每次变更都扫描全部已加载模块的固定步骤 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601` | reload 入口首先拿到“受影响模块集合”，后续绑定/状态更新都围绕这个集合展开，而不是从全局 loaded set 反推 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `ResolveAllDeclaredImports()` 作为兼容兜底的前提下，引入 `provider-to-consumer binding index`，把 declared import 维护从 whole-engine sweep 降为 targeted rebind |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FDeclaredImportBindingIndex`，键至少包含 `ProviderModuleName + FunctionDecl`，值保存 consumer module、import slot、最近一次绑定状态。<br>2. 在 `CompileModules()` commit 成功后，从新/旧 `ScriptModule->GetImportedFunctionCount()` 重建或增量更新 index；第一阶段可先在 startup/full reload 时全量生成一次，soft reload 只更新受影响 provider/consumer。<br>3. 当某个 provider 被 swap-in 时，先根据 index 找出受影响 consumer 集合，仅对这组 module 调 `ResolveDeclaredImports()` 或新的 `ResolveDeclaredImportsForConsumers(...)`；provider 签名未变时可直接跳过 rebind。<br>4. 把 `CheckFunctionImportsForNewModules()` 拆成 `CheckImportsForModules(DirtyProviders, AffectedConsumers)`；只有真实受影响的 consumer 才进入校验与失败重试，不再默认扫过全部未替换旧模块。<br>5. 保留 `ResolveAllDeclaredImports()` 作为 debug command、startup consistency check 和索引缺失时的 fallback；默认行为先做到“命中 index 就 targeted rebind，索引异常再退回全量 sweep”。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：provider A 改动只触发其 consumer 集合重绑；无关模块不再因 provider B 的 declared import 变化进入失败队列；fallback 全量 sweep 仍能在索引关闭时复现今天的行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 反向绑定索引最怕 stale entry：如果 provider rename、module discard 或 fallback full reload 没有同步清理 index，反而会让 targeted rebind 比全量 sweep 更隐蔽地漏绑；第一阶段必须保留全量 sweep 兜底和索引自检日志 |
| 兼容性 | 向后兼容。默认仍可在 startup 或索引异常时调用现有 `ResolveAllDeclaredImports()`；旧脚本语法、manual import 语义和错误消息主干不需要改变 |
| 验证方式 | 1. 构造 provider A / consumer B,C / unrelated D 场景，验证 A 改动后只重绑 B,C。<br>2. 构造 provider 签名不变但实现变化的热更，验证 targeted rebind 不会把无关旧模块重新加入 `PreviouslyFailedReloadFiles`。<br>3. 关闭索引或注入索引失效场景，验证 fallback 全量 sweep 仍保持今天的正确性。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-30 | 编译请求依赖 live `ActiveModules`、缺少 request-scoped dependency snapshot | `ModuleGraphSnapshot` / staged provider view | 高 |
| P2 | Arch-SL-31 | declared function import 的 whole-engine rebind / validation sweep | reverse binding index / targeted rebind | 中 |

---

## 架构分析 (2026-04-08 18:09)

### Arch-SL-32：内建 profile flag 同时承担“已定义”和“当前取值”两种语义，`#ifdef`/`#if` 在变体编译里会出现分裂

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 条件编译语义能否稳定承载 `Editor/Cooked/Test/Server` 这类 profile 变体，以及未来的版本化模块选择 |
| 当前设计 | 预处理器把内建环境开关放进同一个 `PreprocessorFlags` 布尔表；`#ifdef/#ifndef` 判断的是“键是否存在”，`#if` 判断的是“当前布尔值”。由于 `EDITOR`、`RELEASE`、`TEST` 这类内建 flag 会始终被放进表里，再按上下文覆写取值，结果就是 `#ifdef EDITOR` 与 `#if EDITOR` 在同一 profile 下可能得到不同结论 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:40-45,61-70` — 构造函数总会注册 `EDITOR`、`EDITORONLY_DATA`、`RELEASE`、`TEST` 等内建 flag，并在模拟 cooked / 强制预处理 editor code 时只覆写布尔值；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3256-3275` — `#ifdef/#ifndef` 分支只用 `PreprocessorFlags.Find(...) != nullptr` / `== nullptr` 判断是否存在；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4329-4346` — `#if` 则通过 `ParsePreProc()` 读取 flag 的实际布尔值，并只支持 `FLAG` 或 `!FLAG` 这种值判断 |
| 优点 | 兼容了接近 C preprocessor 的“defined / not defined”写法，用户自定义 capability flag 可以零额外建模就接进预处理器 |
| 不足 | 对内建 profile flag 来说，“是否存在这个名字”和“当前 profile 是否启用它”被混进同一张表，导致 `#ifdef EDITOR`、`#ifndef EDITOR`、`#if !EDITOR` 不再等价；模块作者很难仅凭语法看出条件分支是否真的在当前 profile 生效，这会直接削弱条件编译、版本化模块和 profile 变体的可预期性 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把变体选择建模成模块解析问题：`DefaultJSModuleLoader::Search()` 会按调用目录、`node_modules`、`package.json`、`index.js` 搜入口，`modular.js` 再根据 `package.json.type/main/exports` 决定真正执行哪个 entry | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:81-121`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:157-176` | 先把“这个 request 应该命中哪个模块变体”显式化，再执行模块；不存在把 `defined` 与 `enabled` 混在同一 flag 表里的歧义 |
| UnLua | `FLuaEnv` 先安装 `package.searchers`，启动时只 `require(StartupModuleName)`；是否加载哪个模块、从哪个 loader 命中，由 `StartupModuleName` 和自定义 loader 决定，而不是靠 chunk 内部去猜 profile flag 的定义态 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-100,230-244,557-609` | profile / 启动变体先通过 loader 与入口模块确定，模块内容里不需要再承受“flag 名存在但值为 false”这种双重语义 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把条件符号拆成“已定义能力”和“当前取值”两层，保留旧语法兼容层，但让内建 profile flag 不再依赖 `#ifdef` 的隐式语义 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 或新的 compile-profile 头文件中引入 `FAngelscriptConditionalSymbol`，字段至少包含 `Name`、`bDefined`、`Value`、`Kind(BuiltInProfile/UserCapability/UserValue)`。<br>2. 让 `FAngelscriptPreprocessor` 内部从 `TMap<FString, bool>` 升级为“符号表 + 取值表”：`#ifdef/#ifndef` 只读 `bDefined`，`#if` 只读 `Value`；内建 profile flag 一律标成 `BuiltInProfile`。<br>3. 第一阶段不改变脚本行为，但在 `#ifdef EDITOR`、`#ifndef RELEASE` 这类针对内建 profile flag 的写法上输出 warning，提示改用 `#if EDITOR` / `#if !RELEASE`。<br>4. 在 manifest / resolver 方案落地后，把模块变体选择提前到“解析哪个 entry”这一步；预处理器只消费已经确定的 profile value，不再承担“哪个变体应该被选中”的职责。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：`#ifdef EDITOR` 与 `#if EDITOR` 在 editor/cooked profile 下的差异被显式诊断；用户 capability flag 继续保持旧 defined 语义；旧脚本在 warning-only 模式下仍能通过编译。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | S |
| 架构风险 | 主要风险不是实现复杂度，而是兼容认知：仓库内外如果已有脚本把 `#ifdef EDITOR` 当成“编辑器专用分支”，引入警告后会暴露一批历史写法；第一阶段必须坚持“只告警不改义”，避免一次性打断现有工程 |
| 兼容性 | 向后兼容。旧脚本语义先保持不变，只新增 warning；严格模式可以后续作为 opt-in 打开 |
| 验证方式 | 1. 构造 editor / simulate-cooked 两套 profile，验证 `#ifdef EDITOR`、`#if EDITOR`、`#if !EDITOR` 的诊断与求值结果可重复。<br>2. 验证用户自定义 `PreprocessorFlags` 仍然支持旧的 `#ifdef SOME_FLAG` 写法。<br>3. 对比修改前后的预处理结果与编译结果，确认没有 warning 以外的行为漂移。 |

### Arch-SL-33：反射成员的条件编译规则被硬编码成 `EDITOR/配置 flag` 白名单，profile 化 API 演进只能退回拆文件

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块系统能否在不复制整份脚本文件的前提下，表达“同一逻辑类在不同 profile / 版本下有不同反射成员面” |
| 当前设计 | 预处理器在进入 `class/struct/interface` chunk 时会记住当时的 `IfDefStack`；之后如果在类体内部再遇到 `UPROPERTY/UFUNCTION`，只允许新增的条件来自 `EDITOR`、`EDITORONLY_DATA` 或配置里声明且当前为 `true` 的 flag。即便条件合法，预处理器也不会保留“这个成员属于哪个 profile 作用域”的结构化信息，而是直接把结果压缩成 `EditorOnly` metadata |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3431-3459,3483-3485` — 进入 `class/struct/interface` chunk 时把当前 `IfDefStack` 复制到 `ClassIfDefs`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3618-3644` — `UPROPERTY/UFUNCTION` 只允许类级条件之外新增 `EDITOR` / `EDITORONLY_DATA` 或“配置中声明且当前取值为 true”的 flag，否则直接报错；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1470-1471,2450-2451` — 合法的 editor 条件最终只会在函数/属性描述上附加 `EditorOnly` metadata，而不会保留更细的 profile 归属 |
| 优点 | 这种白名单规则能防止 class generator 面对任意 profile 下都可能变化的 UObject 布局，降低反射生成和热更时的结构漂移风险；`EditorOnly` 也与现有 Unreal 语义较容易对齐 |
| 不足 | 这套规则把“反射安全”做成了预处理器里的硬编码政策，而不是模块系统的显式 variant contract。结果是 `ServerOnly`、`ClientOnly`、版本化字段、A/B 实验接口这类 member-level API 变化都不能被一等表示；团队只能靠拆文件、拆模块或复制类定义来实现 profile 变体，热替换与缓存也因此很难识别“这是同一逻辑类的不同 profile 变体” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 先由 `StartupModuleName`、`CustomLoadLuaFile`、`package.searchers` 决定加载哪段 chunk；对象绑定时再由 `ULuaModuleLocator::Locate()` 选择逻辑模块。变体建模发生在“选哪个模块/脚本”这一层，而不是在单个类声明内部硬编码允许哪些条件 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-100,230-244,557-609`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp:18-42` | 把 profile 差异抬到模块选择层，避免反射描述器必须理解一组白名单式 member 条件 |
| puerts | `DefaultJSModuleLoader::Search()` 与 `package.json main/exports` 先解析出实际入口，再执行脚本；模块加载层不需要知道“这个类里哪些成员只在某个条件下存在”，因为变体是按 entry/package 选择的 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:81-121`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:160-176` | 先选择变体，再执行/暴露 API，比在类成员层做硬编码白名单更适合承载版本化和 profile 化模块 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 reflected API 的 profile 差异从“成员级 `#if` 白名单”迁到“模块/类变体选择 + 条件作用域元数据”，让现有规则退化为兼容层 |
| 具体步骤 | 1. 在模块 manifest 或 sidecar 中新增可选 `variants`/`profiles` 字段，允许同一逻辑模块在 `Editor`、`Runtime`、`Server` 等 profile 下映射到不同 entry 文件；没有声明时继续走今天的单文件行为。<br>2. 在 `FAngelscriptClassDesc`、`FAngelscriptFunctionDesc`、`FAngelscriptPropertyDesc` 中增加 `ConditionalScopes/ProfileTags` 字段；第一阶段只记录信息，不改变生成行为。<br>3. 保留现有 `UPROPERTY/UFUNCTION` 条件白名单作为 legacy path，但新增 analyzer：当反射成员位于非 editor/profile manifest 管理的条件块里时，提示作者把差异上移为模块/类变体，而不是继续在成员级做 profile 分叉。<br>4. 让 class generator 和 import resolver 优先选择“当前 compile profile 下命中的一个变体模块”；只有落回 legacy path 时，才继续把 `EditorOnly` 之类的 metadata 翻译到描述器。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：同一逻辑类的 editor/runtime 变体只命中一个 entry；legacy `EditorOnly` 成员仍保持现有行为；非白名单 member 条件会给出迁移诊断而不是沉默失败。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险在于 profile 变体一旦同时作用到模块选择和类生成，必须先明确“同一逻辑类在当前 profile 下只能 materialize 一个版本”；如果过早允许多个变体并存，最容易把类名冲突和热更顺序问题重新带回来 |
| 兼容性 | 向后兼容。manifest/sidecar 为空时仍维持当前白名单规则；`EditorOnly` metadata 继续存在，只是后续会由更明确的 `ConditionalScopes` 翻译得到 |
| 验证方式 | 1. 构造 editor/runtime 两套类变体，验证 resolver 只选择当前 profile 的 entry。<br>2. 回归现有 `EditorOnly` 相关脚本，确认 legacy 行为不变。<br>3. 构造 `ServerOnly` 或版本化成员条件场景，验证系统给出迁移诊断并指向建议的模块/类变体方案。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-32 | `#ifdef/#if` 对 profile flag 的双重语义 | 条件符号模型收敛 + 诊断补强 | 高 |
| P1 | Arch-SL-33 | reflected API 的 profile/版本变体表达能力 | 模块/类变体元数据 + legacy 条件收敛 | 高 |

---

## 架构分析 (2026-04-08 18:19)

### Arch-SL-34：宿主绑定仍在 `runtime bootstrap` 阶段整批激活，`lazy load` 还没进入脚本模块前就已经损失大部分收益

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 启动装载边界，尤其是宿主 `bind provider` 是否能晚于 runtime bootstrap 激活 |
| 当前设计 | 引擎初始化阶段先加载 `Binds.Cache`、读取 `BindModules.Cache`，再同步 `LoadModule(...)` 拉起所有 auto-generated bind modules，并立即 `BindScriptTypes()` 执行全部 bind；这一步发生在 `PrecompiledData` 选择和 `InitialCompile()` 之前 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1469-1495` — 先 `Load(Binds.Cache)`、`LoadBindModules(...)`、`LoadModule(...)`，再 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1513-1569` — `PrecompiledData` 选择与 `InitialCompile()` 在 bind bootstrap 之后才发生；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915-1921` — `BindScriptTypes()` 直接调用 `FAngelscriptBinds::CallBinds(...)` 执行全部绑定；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:594-602` — `LoadBindModules()` 只把模块名字符串表读入内存，随后由启动链无条件整批加载 |
| 优点 | 编译开始前宿主 API 面已经完整可见，脚本编译结果稳定；现有 auto-generated bind module 接入简单，几乎没有额外调度层 |
| 不足 | 即使后续把脚本模块改成 `StartupSet/Lazy`，启动期仍会先拉起全部 bind provider 并执行全部 bind；`lint`、`AnalyzeOnly`、warmup compile 这类“只想看脚本、不想激活整套宿主绑定”的场景也拿不到真正轻量的启动路径；插件扩展者无法声明“某类 UE API 只在特定脚本模块首次命中时再装载” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 初始化期只执行固定的 runtime helper modules，把 `__require` 存成 env 级入口；真正业务模块在 `Start()` 时才 `Require(module)`，而类型元数据 miss 还可以通过 `ClassNotFoundCallback` 在 `LoadClassByID()` 里补装 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:621-635,3543-3551`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:85-99` | 先把“env ready”做小，再把业务模块和类型桥接按请求激活；runtime bootstrap 不必等于完整宿主能力已全部 materialize |
| UnLua | `FLuaEnv` 构造时只安装 `package.searchers` 并初始化 registry；`ClassRegistry` 初始只注册 `UObject/UClass`，真正脚本入口在 `Start(StartupModuleName)` 时才 `require`，反射类型也按 `RegisterReflectedType()` 请求加载 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:97-110,230-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:41-66` | 把“基础运行时可用”与“脚本入口执行/反射类型展开”拆开，懒激活才能真正降低冷启动成本 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“绑定 provider 发现/登记”与“真正执行 bind”拆成两层，让 runtime bootstrap 只加载核心内建绑定，业务/插件绑定改为按 compile/boot request 激活 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h/.cpp` 新增 `FAngelscriptBindProviderManifest`，字段至少包含 `ProviderName`、`ModuleName`、`ProvidedNamespaces`、`ProvidedTypes`、`bCoreRequired`、`Phase`；第一阶段由现有 `BindModules.Cache` 适配生成，旧工程不改生成链也能跑。<br>2. 把 `LoadBindModules()` 改成“只登记 provider manifest，不立即 `LoadModule`”；新增 `EnsureBindProvidersLoaded(const TSet<FName>& Providers)`，默认实现先把 `AllProviders` 传进去，保持今天的 eager 行为。<br>3. 把 `BindScriptTypes()` 拆成 `BindCoreScriptTypes()` 与 `BindLoadedProviders()`；前者只注册基础脚本运行时必需的 bind，后者只消费已经激活的 provider 集。<br>4. 让 `InitialCompile()`、future `BootRuntime()`、module manifest 或 compile request 可以声明 `RequiredBindProviders`；没有声明时继续回退到 `AllProviders`，确保兼容。<br>5. 在后续 `Lazy`/`StartupSet` 模式里，先只激活 `bCoreRequired` provider；当某个启动模块或 boot entry 首次请求到额外 provider 时，再调用 `EnsureBindProvidersLoaded()` 增量装载。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：默认 legacy 模式下 provider 加载顺序与今天一致；lazy provider 模式下冷启动只执行核心 bind；首次请求特定 provider 后对应脚本模块能成功编译且其他 provider 不被连带加载。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是 provider 声明不完整时会把“缺少 bind”从启动期错误变成首次命中时错误；第一阶段必须保留 `AllProviders` 默认值和详细缺失诊断，避免把问题隐藏到运行期深处 |
| 兼容性 | 向后兼容。默认仍保持今天的整批加载；按 provider 懒激活为显式 opt-in，旧项目和已有 bind modules 不需要立刻迁移 |
| 验证方式 | 1. 对比 legacy 与 provider-aware 两种模式的启动日志，确认默认路径加载顺序不变。<br>2. 在 lazy provider 模式下统计冷启动实际加载的 bind module 数量，验证少于 today 的全量加载。<br>3. 构造一个依赖特定 provider 的脚本模块，验证首次命中时 provider 会被增量加载且编译结果与 eager 模式一致。 |

### Arch-SL-35：`Binds.Cache` / `BindModules.Cache` 仍是无版本协商的外部产物契约，启动失败边界过粗

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定产物契约、版本协商与 fallback 策略，尤其是 build artifact 漂移时脚本生命周期如何退化 |
| 当前设计 | `Binds.Cache` 直接反序列化并在类/结构体表为空时 `Fatal`；`BindModules.Cache` 只是一个纯字符串数组，没有 schema version、build id、profile/hash 或 per-provider 状态；两者都在 `InitialCompile()` 之前被当成启动前置条件 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:103-115` — `Binds.Cache` 缺失或过旧时直接 `Fatal`，明确写着“script compilation and execution to fail”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:594-602` — `LoadBindModules()` 只用 `LoadFileToStringArray()` 读一份无版本字符串表；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1469-1495,1513-1569` — 这两份外部产物先于 `PrecompiledData` 校验和 `InitialCompile()` 生效，启动链没有结构化的“可降级/需重建”状态 |
| 优点 | artifact 出问题时会非常早地暴露，shipping 环境不容易带着残缺 binding 继续跑；当前实现也足够简单，维护成本低 |
| 不足 | 系统无法区分“engine build 变了”“某个 bind provider 漏编译”“profile 切换”“cache schema 升级”“扩展 phase 版本变了”等不同失效原因；Editor/dev 模式没有一等的 `NeedsRegen/Degraded` 退化路径，未来 `lint`、custom bind phase、profile-specific binding artifact 也无法把自己的版本信息并入契约 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 类型元数据 miss 不会因为缺一份外部 cache 就直接终止 runtime；`LoadClassByID()` 会先查现有注册表，miss 时调用 `ClassNotFoundCallback`，成功后再重试查找 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:85-99` | 把“元数据缺失”建成 request-time fallback，而不是只提供 boot-time fatal；这样扩展点和版本漂移可以有渐进修复路径 |
| UnLua | 反射类型通过 `RegisterReflectedType()` 按名称请求加载；脚本源码装载则走 `LoadFromCustomLoader/LoadFromFileSystem`，失败被限制在当前 `require/load` 请求内，而不是依赖独立 metadata cache 先全局通过 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59-66`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:558-609` | 把“缺少类型/源码”定位成 env/request 级错误，并保留 loader/registry 的补救空间，而不是把所有漂移都挤压到启动前的一次性硬失败 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 用 versioned `BindingManifest` 替代裸 cache contract，把 `Ready/NeedsRegen/Degraded/Fatal` 状态显式化，并为 editor/dev 提供可控 fallback |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h/.cpp` 新增 `FAngelscriptBindingManifest`，字段至少包含 `SchemaVersion`、`EngineBuildId`、`PluginVersion`、`CompileProfileHash`、`BindProviderHashes`、`GeneratedAt`。<br>2. 让 `BindModules.Cache` 的 legacy reader 先转换成 `BindingManifest`；读到旧格式时输出一次迁移 warning，但仍允许继续运行，避免一次性打断老工程。<br>3. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 新增 `FAngelscriptBindingReadiness`（`Ready/NeedsRegen/Degraded/Fatal`）；`Initialize()` 先产出 readiness，再决定是否进入 `BindScriptTypes()` / `InitialCompile()`。<br>4. 默认保持 shipping 行为严格：`Fatal` 仍然退出；但在 editor/dev/runtime test 模式下，`NeedsRegen` 应优先提供“重建 binding artifact”或“仅禁用受影响 provider 并输出结构化诊断”的路径，而不是立即 `Fatal`。<br>5. 给 future `lint`、custom bind generator、profile-specific binding artifact 预留 `ManifestContributor` 接口，使它们可以把自身版本号并入 `BindingManifest`，避免继续靠旁路文件约定。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：旧版 manifest 被识别为 `NeedsRegen` 并给出具体原因；shipping 下不可恢复缺失仍保持 fatal；editor/dev 下缺一个 provider 时系统进入 `Degraded` 并阻止受影响脚本继续编译，而不是直接让整次启动崩掉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险不在序列化本身，而在 fallback 边界：如果 `Degraded` 状态定义过宽，可能把真正必须阻塞的 binding 缺失拖到后续编译或运行时才暴露；第一阶段必须坚持“shipping 严格、editor/dev 可诊断降级”这一分层策略 |
| 兼容性 | 向后兼容。旧 `Binds.Cache` / `BindModules.Cache` 可以先走 legacy 读取并转换为 manifest；shipping 默认仍保持今天的严格失败语义，只有 editor/dev 才新增可控 fallback |
| 验证方式 | 1. 构造 schema 升级、build id 漂移、单个 provider 缺失三种场景，确认系统给出不同 readiness 原因。<br>2. 验证 shipping 模式下不可恢复缺失仍然 `Fatal`，不改变今天的安全边界。<br>3. 验证 editor/dev 模式下 `NeedsRegen/Degraded` 不会直接导致整次启动崩溃，同时能阻止受影响脚本继续进入错误的编译/执行路径。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-34 | 宿主 bind provider 的启动边界、eager bootstrap 对 lazy load 的阻塞 | provider manifest + 增量激活 | 高 |
| P2 | Arch-SL-35 | `Binds.Cache/BindModules.Cache` 的版本协商与 fallback 契约 | versioned binding manifest + readiness 状态机 | 中 |

---

## 架构分析 (2026-04-08 18:28)

### Arch-SL-36：编译扩展 hook 仍是进程级静态单例，无法按 runtime / profile / request 隔离

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译管线扩展点的作用域与隔离性，尤其是 `lint`、custom lowering、分析模式和测试 runtime 是否能拥有各自的 hook 集合 |
| 当前设计 | 引擎虽然支持 `Create` / `CreateCloneFrom` / `CreateForTesting` 这类多 runtime 构造，但预处理和编译 hook 的存储仍是进程级静态对象；调用时只做全局广播，不携带 runtime-owned registry |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:127-136` — engine API 明确支持创建独立/测试 engine；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8-30` — `OnProcessChunks` / `OnPostProcessCode` 是 `static` delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:35-36,212-287` — 这两个 hook 在 cpp 中作为静态单例定义，并由 `Preprocess()` 直接广播；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:37-47` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:72-104` — `GetPreCompile()` / `GetPostCompile()` / `GetPreGenerateClasses()` / `GetPostCompileClassCollection()` 都返回函数内 `static Delegate` |
| 优点 | 当前接入门槛低，插件级扩展者只需注册一次即可影响默认主 runtime；旧工程和 editor 工具链也容易共享同一批 hook |
| 不足 | hook ownership 没有跟随 `FAngelscriptEngine`、`CompileProfile` 或 `CompileRequest` 走，结果是测试 runtime、分析 runtime、游戏 runtime 之间不能安全拥有不同扩展集；一旦未来接入 `lint-only`、profile-specific lowering、按项目/按 world 的 compile pass，静态 hook 很容易出现串扰、顺序污染和测试泄漏 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnv` 在构造时直接接收 `IJSModuleLoader`、logger、debug port 等 env 参数；`JsEnvImpl` 再把 `SearchModule` / `LoadModule` 绑定到当前 `This`，loader policy 随 env 实例化而不是作为全局静态注册 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:61-70`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:489-495,4079-4112` | 扩展点的 ownership 跟随 env，而不是跟随进程；不同 env 可以安全挂不同 loader / logger / debug policy |
| UnLua | 每个 `FLuaEnv` 在自己的 `lua_State` 上安装 `package.searchers`，`AddSearcher()` 把 `this` 作为 upvalue 绑定给搜索器，因此 loader 链天然是 env-scoped | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:74-100`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:644-667` | 即使扩展点最终表现为全局 `require` 语义，注册位置仍然是具体 env；测试 env、运行时 env 和热更 env 可以各自持有不同 searcher 组合 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 hook registry 从进程级静态单例下沉到 runtime / compile session，旧静态 delegate 先保留为兼容桥接层 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptExtensionRegistry`，至少拆出 `PreprocessHooks`、`CompileHooks`、`BootHooks` 三组集合，并挂到 `FAngelscriptEngine` 或 `FAngelscriptCompileSession`。<br>2. 让 `FAngelscriptPreprocessor` 构造时显式接收 registry 或 session context；`Preprocess()` 不再直接广播类静态 `OnProcessChunks` / `OnPostProcessCode`，而是优先广播 session-owned hooks。<br>3. 把 `FAngelscriptRuntimeModule::GetPreCompile()` 等现有静态入口改为“默认全局 registry”的适配 API；没有显式 runtime registry 的旧路径继续复用它，保证向后兼容。<br>4. 为 future `CompileProfile` 增加只读扩展视图，例如 `GetLoweringPasses(ProfileId)`、`GetLintPasses(ProfileId)`，允许同一进程同时运行 `Game`、`AnalyzeOnly`、`Test` 三种 profile。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加回归：两个 testing engine 注册不同 preprocess hook 时互不串扰；legacy 静态 delegate 仍能影响默认 runtime；session 结束后 hook 不泄漏到下一次编译。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 兼容期最大风险是 hook 被“双重触发”：如果新 registry 和旧静态 delegate 同时注册了同一个扩展而没有去重，容易出现重复 lowering / 重复诊断 |
| 兼容性 | 向后兼容。默认 runtime 先自动挂接一个 global registry 并桥接旧 delegate；旧项目不需要立刻迁移，只有需要隔离 runtime/profile 的场景才显式注册自有 registry |
| 验证方式 | 1. 回归现有 `PreCompile/PostCompile` 路径，确认默认 runtime 行为不变。<br>2. 新增双 runtime 回归，验证不同 hook 集不会互相污染 diagnostics 和生成代码。<br>3. 新增 `AnalyzeOnly` profile 测试，验证它只执行自己的 lint/lowering hooks，而不继承游戏 runtime 的副作用 hook。 |

### Arch-SL-37：预处理产物没有 first-class cache，hot reload 仍以“重读源码并重放 lowering”作为增量上限

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 预处理阶段的增量复用能力，尤其是 hot reload / lazy compile / analyze-only 是否能复用上次的 `ProcessedCode`、`Imports` 与 lowering 结果 |
| 当前设计 | `InitialCompile()` 和 `PerformHotReload()` 每次都新建 `FAngelscriptPreprocessor`；被选中的文件会重新读盘、重新 `ParseIntoChunks`、重新做 class/macro/delegate/defaults/lowering，最后再重新生成 `ProcessedCode` 与 `CodeHash`。`PrecompiledData` 的命中发生在 `Stage1` 之后，不能跳过这段预处理成本 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2038-2082` — 初始编译每次都构造新的 `FAngelscriptPreprocessor`，把全部脚本文件逐个 `AddFile()` 后执行 `Preprocess()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2253-2468` — hot reload 同样新建 preprocessor，并把 `FilesToHotReload` 闭包重新喂给 `AddFile()` / `Preprocess()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:91-121,146-177` — `AddFile()` 总是重新加载 `RawCode`（同步或异步）；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:224-301` — `Preprocess()` 对 session 中所有文件固定重跑 `ParseIntoChunks -> DetectClasses -> AnalyzeClasses -> ProcessMacros -> ProcessDelegates -> ProcessDefaults -> Condense -> PostProcess`，随后重新计算 `ProcessedCode` / `CodeHash`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3991-4142` — `CondenseFromChunks()`、`PostProcessRangeBasedFor()`、`PostProcessLiteralAssets()` 都直接重放在整段文本上；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4283-4299` — 即使后续命中 `PrecompiledData`，前面这轮预处理也已经完成 |
| 优点 | 当前失效规则简单明确，只要重建 session 就能保证看到最新源码；预处理阶段没有额外 cache schema，因此调试与回归成本较低 |
| 不足 | dirty file 的依赖闭包一旦稍大，热更就会重复执行磁盘 I/O、chunk parse、regex lowering 和代码拼接；未来即使引入 `Lazy`、`AnalyzeOnly` 或自定义 `lint` phase，也仍然先支付这段全量预处理成本，难以真正做到按需加载和低延迟分析 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `modular.js` 把 `moduleCache` 做成 env 级第一等结构，`require()` miss 才 `search -> load -> execute`，命中时直接返回缓存；`forceReload()` 也只标记目标 key 失效 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-60,105-146,205-225`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24` | 虽然这里缓存的是运行态模块对象而不是 AOT 预处理 IR，但源码清楚表明“缓存 identity + 失效协议”是模块系统的一等组成；Angelscript 的 `ProcessedCode/Imports` 也应拥有同等级的 cache contract |
| UnLua | `package.loaded` 与 `loaded_modules` 长期保存在 env 内；`require()` 优先命中缓存，`reload_modules()` 只对目标模块集合重新 `load()` / `xpcall()`，未变模块保持原状态 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:13-18,147-168,560-589`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:74-100,644-667` | 同样不是预处理缓存，但它证明了“按模块缓存、按模块失效”比“每次重建整个 batch workspace”更适合作为 lifecycle 基础设施 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `module catalog` 之上增加 `PreprocessArtifactCache`，把 `ProcessedCode`、import 图和 lowering 产物缓存成 request 可复用的中间件，而不是每轮都从 `RawCode` 重放 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h/.cpp` 新增 `FAngelscriptPreprocessArtifact`，字段至少包含 `RawSourceHash`、`CompileProfileHash`、`ProcessedCode`、`Imports`、`GeneratedFragments`、`PostInitFunctions`、`DiagnosticsSourceIdentity`、`LoweringVersion`。<br>2. 让 `AddFile()` 或未来的 catalog entry 在读盘后先计算 `RawSourceHash`；`Preprocess()` 在 `ParseIntoChunks()` 前优先查 `ArtifactCache`，命中时直接 hydrate `FFile/FAngelscriptModuleDesc`，跳过 `DetectClasses/AnalyzeClasses/ProcessMacros/ProcessDelegates/ProcessDefaults/Condense/PostProcess`。<br>3. 将 cache key 与 `CompileProfile`、`PreprocessorFlags`、`ShouldUseAutomaticImportMethod()`、lowering pass version 绑定，避免把不同 profile 或不同 pass 顺序下的结果错误复用。<br>4. 让 hot reload 只对 dirty modules 与其失效闭包重建 preprocess artifact；未脏模块即使参与 provider 视图，也只读取缓存的 `Imports/ProcessedCode`，不再重跑全文 lowering。<br>5. 第一阶段不必写盘，只做 engine 内存级 LRU cache；待命中/失效边界稳定后，再决定是否与 `PrecompiledData` 或 future warm-start cache 合并。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加回归：单文件实现改动应让 importer 依赖视图更新但不重放无关模块的 preprocess；切换 `PreprocessorFlags` 或 lowering version 必须 miss cache；默认关闭 artifact cache 时行为与今天一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | cache key 设计如果漏掉 `CompileProfile`、flag 或 lowering version，最容易出现“命中了旧 `ProcessedCode` 但源码其实已在另一 profile 下变化”的隐蔽错误；第一阶段必须宁可多 miss，也不要错 hit |
| 兼容性 | 向后兼容。artifact cache 可以先默认关闭或仅在 editor/dev 打开；miss 时完整回退到今天的预处理流程，不改变现有脚本语义 |
| 验证方式 | 1. 对比开启/关闭 cache 的初编译与 hot reload 结果，确认生成代码、diagnostics、`CodeHash` 保持一致。<br>2. 新增性能回归，验证单文件 hot reload 时 preprocess 命中率和耗时显著改善。<br>3. 构造 `PreprocessorFlags`、automatic import 和 lowering version 变化场景，确认 cache 会稳定 miss 而不是复用错误产物。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-36 | 编译扩展 hook 的作用域与 runtime/profile 隔离 | runtime-scoped extension registry | 高 |
| P1 | Arch-SL-37 | 预处理产物复用、hot reload 增量上限与按需分析成本 | preprocess artifact cache | 高 |

---

## 架构分析 (2026-04-08 18:38)

### Arch-SL-38：编译主链直接编排 `asCBuilder` 私有阶段，扩展能力被锁在 AngelScript 内核 ABI 之下

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译 backend 的抽象边界，以及未来插入 `lint`、optimizer、兼容 backend 或上游 AngelScript 升级时的演进成本 |
| 当前设计 | `FAngelscriptEngine` 没有停在 `asIScriptModule::Build()` 这种公开 ABI，而是直接创建临时 `asCModule`，手动驱动 parse/type/function/code/global 五段编译，并直接读写 reload 私有状态 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h:898-955` — AngelScript 对宿主公开的模块编译入口主要是 `AddScriptSection()`、`Build()`、`BindImportedFunction()`、`ImportModule()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp:254-268,342-347` — 公共 `Build()` 内部自带 `HasExternalReferences(false)` 检查与 `ResetGlobalVars()` 初始化；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4247-4359` — 当前 runtime 直接 `GetModule(..., asGM_ALWAYS_CREATE)`、写入 `baseModuleName`、遍历 imports 后逐段 `AddScriptSection()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3212-3258, 4362-4410` — 主流程直接调用 `builder->BuildParallelParseScripts()`、`BuildGenerateTypes()`、`BuildGenerateFunctions()`、`BuildCompileCode()`、`ResetGlobalVars(0)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3299-3346` — hot reload 还直接操作 `ReloadOldModule`、`ReloadNewModule`、`ReloadState` 这类私有 reload 元数据 |
| 优点 | 当前实现能精细控制 `Stage1-4`、并行 parse 和 recompile avoidance，性能与 hot reload 语义都比直接 `Build()` 更可控 |
| 不足 | 编译 orchestration 已经和 `asCModule/asCBuilder` 私有字段深度绑定，导致自定义 backend、兼容模式、上游内核升级和分析专用 compile path 都必须理解 AngelScript 内部实现；即便只是新增一个“只做公开 API smoke test”的 fallback backend，也无法复用现有 `CompileModules()` 主链 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 宿主侧只定义 `IJSModuleLoader::Search/Load`；真正的模块执行与缓存编排留在 `executeModule()` / `genRequire()`，扩展停在 loader 与 module contract 上，不需要碰 V8 parser/build 私有结构 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:56-71,129-191` | 先把 VM 私有实现包在插件自有 contract 后面，模块系统扩展才不会和底层实现细节一起漂移 |
| UnLua | `FLuaEnv` 通过 `package.searchers`、`LoadString()` / `LoadBuffer()` 与 `lua_pcall` 组织 chunk 生命周期；loader 扩展和热更脚本都停在 `lua_State` 公共调用面，不触达 Lua parser 私有数据结构 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-100,389-430,557-667` | 让“如何加载/执行模块”停在宿主可控边界，未来新增 custom loader、sandbox 或热更策略时不需要复制 VM 内部编译细节 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留当前私有 fast path 的前提下，先补一层插件自有 `CompilationBackend`，把 `CompileModules()` 从 `asCBuilder` 细节中解耦出来 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `IAngelscriptCompilationBackend`，最少定义 `CreateModuleHandle()`、`AddImports()`、`AddSourceSections()`、`CompileDeclarations()`、`CompileFunctions()`、`CompileBodies()`、`InitializeGlobals()`、`CollectReloadMetadata()`。<br>2. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4247-4410` 当前直接操纵 `asCModule/asCBuilder` 的代码搬到 `FAngelscriptPrivateBuilderBackend`，`CompileModules()` 只编排 backend phase，不再直接触碰 `builder` 指针。<br>3. 新增实验性 `FAngelscriptPublicBuildBackend`，只使用 `asIScriptModule::AddScriptSection()` / `Build()` 这套公开 ABI，第一阶段只用于 `AnalyzeOnly`、兼容性 smoke test 和上游升级验证，不承担现有 hot reload 优化语义。<br>4. 在 runtime config 或 future `CompileRequest` 中增加 `CompilationBackendId`，默认值仍然是 `PrivateBuilder`，保证旧工程和现有测试行为不变。<br>5. 把 recompile avoidance 依赖的 `ReloadOldModule/ReloadNewModule/ReloadState` 收敛为 backend capability；没有这项能力的 backend 自动回退到“无优化但正确”的 compile path，而不是要求所有 backend 都理解私有 reload 细节。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加双 backend 回归：默认 backend 继续通过现有 hot reload 测试；public backend 至少要在小规模模块集上产出一致的 diagnostics / `ECompileResult`，用于验证 orchestration 已和私有 ABI 解耦。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCompilationBackend.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptCompilationBackend.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 主要风险不是接口设计，而是 phase 语义对齐：如果 backend 抽象切得太粗，就会把今天依赖 `asCBuilder` 中间态的 recompile avoidance、template remap 和 staged reload 优化都重新挤回 `CompileModules()`；第一阶段必须允许 capability 差异，而不是追求“一套接口覆盖所有优化” |
| 兼容性 | 向后兼容。默认仍走当前 `PrivateBuilder` fast path，现有脚本、热更语义和项目 hook 都不需要迁移；`PublicBuildBackend` 先作为显式 opt-in 的分析/验证路径存在 |
| 验证方式 | 1. 跑现有 `InitialCompile`、soft/full reload 与 `AngelscriptTest` 回归，确认默认 backend 输出不变。<br>2. 对同一组简单模块分别走 `PrivateBuilder` 和 `PublicBuild` backend，比较 diagnostics、import 绑定与 `ECompileResult`。<br>3. 针对一次 AngelScript 上游升级做 smoke test，验证需要修改的代码面被收敛到 backend 实现，而不是再次散落回 `CompileModules()` 主链。 |

### Arch-SL-39：预处理改写后的源码仍以 `AbsoluteFilename + lineOffset=0` 入引擎，authored span 无法稳定回溯

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 预处理/代码生成后的诊断映射能力，以及未来 `lint`、optimizer、debugger、`editor navigation` 是否还能定位回 authored source |
| 当前设计 | 预处理阶段会把 chunk condense、regex lowering 和 generated helper 全部折叠进最终 `ProcessedCode`，但 `FCodeSection` 只保存 `AbsoluteFilename/Code`；真正送入 AngelScript 内核时始终使用原文件名和 `lineOffset=0`，模块级诊断再统一回落到 `Code[0]` |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:276-307` — `CondenseFromChunks()` 后只把最终 `ProcessedCode` 填回 `Module->Code`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3983-4143` — range-based for lowering 与 literal asset helper 都在整段 `ProcessedCode` 上重写/追加文本；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1277-1284` — `FCodeSection` 只有 `RelativeFilename`、`AbsoluteFilename`、`Code`、`CodeHash`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h:898-899` — AngelScript API 本身支持 `AddScriptSection(..., lineOffset)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4343-4345` — 当前调用始终是 `AddScriptSection(..., 0, 0)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4944-4954` — 模块级诊断固定 `Column = 1`，并优先写回 `Module->Code[0].AbsoluteFilename` |
| 优点 | 现有实现非常直接，编译器、日志和 editor 侧都只需要处理“一个 section 对应一个文件名”的简单模型 |
| 不足 | 一旦 pass 在 authored code 之后再做整段 rewrite 或追加 generated helper，后续 diagnostics、stack trace 和 `lint` 结果就很难精确回溯到原始源码；未来即使有 `CompilationBackend` 或 `AnalyzeOnly`，没有 authored span map 也只能继续给出粗粒度文件级错误 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | loader 返回的是 `[fullPath, debugPath]`，`executeModule()` 再把 `debugPath` 传给 `evalScript()`；也就是说模块执行时的 debug identity 从一开始就是 load contract 的一部分 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:56-71,129-138,183-191` | 至少先把“执行时看到的源码名”与“真实载荷路径”解耦；推断：Angelscript 若继续保留 rewrite/generate 流程，就需要比 `debugPath` 更细的 fragment/source map |
| UnLua | custom loader、filesystem loader 和 `DoString()` / `LoadBuffer()` 都显式携带 `ChunkName` 或 `FullPath`，`luaL_loadbufferx` 始终知道当前 chunk 的显示身份 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:389-430,557-611` | 把 chunk identity 做成执行 contract 后，diagnostics 与 stack trace 至少不会退化成匿名文本；Angelscript 可以在此基础上再补 authored-to-generated 映射 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先给 `ProcessedCode` 增加 sidecar `SourceMap`，把 authored span 到 generated span 的映射显式化；等映射稳定后，再决定是否拆成多 `ScriptSection`/非零 `lineOffset` |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 为 `FCodeSection` 增加 `FAngelscriptSourceMap`，最少记录 `FragmentId`、`OriginPath`、`OriginStartLine`、`GeneratedStartLine`、`GeneratedLineCount`、`EFragmentKind(Authored/Lowered/Generated)`。<br>2. 让 `CondenseFromChunks()`、`PostProcessRangeBasedFor()`、`PostProcessLiteralAssets()` 在生成最终文本时同步维护 `SourceMap`；第一阶段不改编译输出文本，只补 sidecar 映射，控制风险。<br>3. 在 `LogAngelscriptError()`、`ScriptCompileError()` 和 future `JsonSink`/editor diagnostics 入口增加 `TranslateDiagnostic()`：把 AngelScript 回传的 `sectionName + row + column` 翻译回 authored fragment，再写入现有 `Diagnostics` 容器。<br>4. 等 diagnostics 翻译稳定后，再把 generated helper 拆成独立 `ScriptSection` 或使用非零 `lineOffset`，让 VM 原生 line info 也能更接近 authored source，而不是所有片段都挂在原文件首行偏移 0 上。<br>5. 对没有 `SourceMap` 的 legacy 模块继续回退到今天的 `AbsoluteFilename + row` 路径，确保旧项目、旧日志客户端和旧测试不会被一次性打断。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/` 与 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：range-based for 语法错误仍能定位到 authored `for` 行；literal asset 生成的 getter/initializer 报错能回溯到原始 `asset` 声明；模块级合成错误不再一律落到 `Code[0]` 的第 1 列。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险集中在 diagnostics 兼容性：如果 source map 生成和翻译顺序不稳定，会出现“真实编译没变，但报错行号漂移”的噪音；因此第一阶段必须坚持“只加 sidecar map，不改最终编译文本与 section 切分” |
| 兼容性 | 向后兼容。旧模块与旧客户端在没有 `SourceMap` 时继续看到今天的文件名/行号；只有新 diagnostics consumer 才会优先消费翻译后的 authored span |
| 验证方式 | 1. 对比开启/关闭 source map 后的编译结果与 bytecode，确认语义不变。<br>2. 为 generated helper、range-based for、literal asset 场景增加定位回归，验证新 diagnostics 能回到 authored 行。<br>3. 验证旧 editor/logging 路径在没有 source map 的模块上仍输出原格式，不引入协议级破坏。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-38 | 编译 backend 与 AngelScript 私有 ABI 的耦合 | backend 抽象层新增 | 高 |
| P2 | Arch-SL-39 | authored span 诊断映射与生成代码定位 | source map / diagnostics 翻译层 | 中 |

---

## 架构分析 (2026-04-08 23:40)

### Arch-SL-40：threaded initial compile 只暴露粗粒度完成广播，扩展侧必须自行处理 thread affinity

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 启动阶段的线程亲和约束，尤其是自定义编译阶段、启动扩展和 side-effectful bind 如何安全接入 |
| 当前设计 | `Initialize()` 可以把初始化与初编译丢到 `AnyHiPriThread`；core 只通过 `bIsInitialCompileFinished` 和一个无参 `OnInitialCompileFinished` 广播向外暴露“差不多结束了”，扩展方需要自己猜哪些动作必须延后到 game thread |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:819-855` — `Initialize()` 在 `ShouldInitializeThreaded()` 为真时用 `AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, ...)` 执行 `Initialize_AnyThread()`，主线程只轮询等待；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:242-249,481-489` — `CanUseGameThreadData()` 与可见启动状态都只围绕 `bIsInitialCompileFinished` 这个布尔值；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:17,39` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653-1655` — 对外唯一启动完成信号是无参 `FAngelscriptCompilationDelegate OnInitialCompileFinished`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h:18-31` — `FScriptConsoleVariable` 明确因为“initial compile can happen on a separate thread”而必须延后 `RegisterConsoleVariable()`，说明扩展点需要自己理解线程边界 |
| 优点 | 后台初始化能缩短 editor 启动阻塞时间；现有插件只要在 `OnInitialCompileFinished` 后做副作用，大多数路径都能工作 |
| 不足 | 线程模型没有 first-class phase/affinity contract，导致 side-effectful 扩展只能复制 `Bind_Console` 这种临时防御；未来插入 `lint`、warmup、custom boot entry 或 provider 懒加载时，很难声明“这段可在 worker thread 执行，那段必须回到 game thread” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnvImpl::Start()` 与 `ReloadModule()` 都先校验绑定线程，再在该 env 内执行 `Require`/reload；线程约束是 env API 的一部分，不需要扩展方猜测一个全局“初始化已完成”时刻 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1504-1514` | 先把 thread affinity 收进 runtime API contract，再暴露 loader/startup 扩展，外部无需靠 one-shot 广播自保 |
| UnLua | `FLuaEnv::Start()` 是显式 env 入口；`LoadFromCustomLoader()` / `LoadFromFileSystem()` 都通过 upvalue 拿到当前 `Env`，扩展 loader 天然挂在 env 生命周期上，而不是挂在进程级初始化屏障上 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:63-65`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:231-251`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-611` | 把“谁拥有运行时”和“在哪个上下文执行扩展”固定下来，比只有一个 `compile finished` 广播更适合长期演进 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不改变现有 threaded init 默认行为的前提下，引入显式 `startup phase + thread affinity` 协议，让扩展注册到具体阶段，而不是赌 `OnInitialCompileFinished` 足够晚 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `EAngelscriptStartupPhase` 与 `FAngelscriptStartupParticipant`，字段至少包含 `Phase`、`RequiredThread(GameThread/AnyThread)`、`Callback`、`DebugName`。<br>2. 把 `Initialize()` 拆成对外可见的阶段，例如 `BootstrapAnyThread`、`InitialCompileAnyThread`、`CommitGameThread`、`PostCompileGameThread`；第一阶段不碰 `Stage1-4` 语义，只给扩展声明线程亲和边界。<br>3. 新增 `RegisterStartupParticipant(...)` 与 `DrainStartupPhase(...)`；`Bind_Console` 这类当前手写的延后逻辑改为注册到 `PostCompileGameThread`，不再直接订阅无参广播。<br>4. 保留 `FAngelscriptRuntimeModule::GetOnInitialCompileFinished()` 作为兼容层，但让它内部映射到 `PostCompileGameThread` 阶段，确保旧项目与已有 bind 不需要立刻迁移。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加 threaded init 回归：一个 `AnyThread` participant 与一个 `GameThread` participant 同时注册，验证前者可在后台执行、后者只会在编译提交后于 game thread 触发；再补一个“旧 `OnInitialCompileFinished` 适配层仍然只触发一次”的兼容测试。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果把 startup phase 设计得过于细碎，会与已有 compile phase/boot phase 方案形成重复抽象；第一阶段应只解决 thread affinity 与 side-effect 安全，不要再次重做 `Stage1-4` 编排 |
| 兼容性 | 向后兼容。旧的 `OnInitialCompileFinished` 继续存在并由新阶段适配；没有注册 participant 的旧扩展仍保持当前行为 |
| 验证方式 | 1. 在 `bForceThreadedInitialize=true` 场景下验证新 participant 顺序与线程归属。<br>2. 回归现有 `Bind_Console` 路径，确认不再需要手写 late-init 逻辑也不会崩溃。<br>3. 关闭 threaded init 再跑一次，确认新阶段系统不会改变旧的单线程启动结果。 |

### Arch-SL-41：启动恢复与 readiness milestone 仍硬编码在 core，宿主无法替换 boot policy

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本启动失败后的恢复策略，以及“脚本已编译”“资产扫描完成”“测试已发现”等 readiness 里程碑是否是显式宿主协议 |
| 当前设计 | `InitialCompile()` 不只负责脚本编译；启动失败时它会在 core 内直接决定 `RequestExit` 还是弹出 editor modal 并驱动 hot reload 重试，成功后又在 `OnPostEngineInit` 里额外挂接 asset scan 完成回调做测试发现，但对外公开的仍只有一个无参 `OnInitialCompileFinished` |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2099-2195` — 初编译失败时，commandlet/`ExitOnError` 直接 `RequestExit(true)`；否则 core 内创建 `SWindow`、提前启动 hot reload thread，并在 modal loop 里反复 `CheckForHotReload(ECompileType::FullReload)` 与 `DebugServer->ProcessMessages()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2201-2218` — 编译结束后又在 `OnPostEngineInit` 注册 `AssetManager` 初始扫描完成回调，扫描完成才 `DiscoverTests()` 并置 `bCompletedAssetScan`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653-1655` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:17,39` — 对外仍只有无参 `OnInitialCompileFinished`，无法表达启动失败、恢复中、asset-scan-ready、tests-ready 等里程碑；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4143-4152` — 后续编译时测试发现还要再次依赖 `bCompletedAssetScan` 这个隐式状态 |
| 优点 | editor 默认体验比较完整：脚本启动失败时用户可以留在对话框内修复并继续进入编辑器；测试发现也能等到 `AssetManager` ready 后再进行 |
| 不足 | boot policy 被锁在 runtime core，commandlet、editor、headless 工具、CI 或自定义宿主没法替换恢复方式；同时 readiness 被拆成多个隐藏布尔与回调，外部无法稳定知道“现在是 scripts ready，还是 tests ready，还是仍在 recovery loop” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 启动入口是显式 `Start(ModuleNameOrScript, Arguments)`；失败通过 `ILogger::Error()` 和 `TryCatch` 报告，核心运行时不内嵌 editor modal 或重试 UI，宿主可以自行决定是否弹窗、重试或直接失败 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSLogger.h:19-26`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3490-3551` | 把 boot error/reporting 留给宿主 policy，比在 runtime core 内硬编码 editor 恢复流程更利于 commandlet、CI 和多宿主复用 |
| UnLua | `FLuaEnv::Start()` 只是按 `StartupModuleName` 执行 `require`；`ULuaEnvLocator` 负责在需要时创建/返回 env，核心启动路径没有把 editor 交互、资产扫描或后续工具准备揉进同一个函数 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:63-65`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:231-251`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp:18-25,40-73` | 先把 runtime start 保持成小而明确的 contract，宿主再按自身需要附加更高层恢复或准备逻辑 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“启动失败怎么恢复”和“哪些 readiness milestone 何时对外可见”从 `InitialCompile()` 中抽出来，收敛为可替换的 `boot host policy + boot milestone` 协议 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptBootReport` 与 `EAngelscriptBootMilestone(BootstrapReady/ScriptsReady/RecoveryLoop/AssetScanReady/TestsReady/Fatal)`，报告至少包含 `Succeeded`、`Diagnostics`、`bCanRetry`、`MilestonesReached`。<br>2. 新增 `IAngelscriptBootHostPolicy`，把当前 `InitialCompile()` 中的三种处理方式分离为默认实现：`EditorModalRetryPolicy`、`CommandletFailFastPolicy`、`HeadlessLogOnlyPolicy`；第一阶段 runtime module 继续安装与今天等价的默认 policy，保证行为不变。<br>3. 让 `InitialCompile()` 只产出 `BootReport` 和 `CompiledModules` 结果，不再直接 `SNew(SWindow)` 或 `RequestExit()`；policy 决定是否进入恢复循环、是否允许热修复后继续。<br>4. 把 `OnPostEngineInit` + `AssetManager->CallOrRegister_OnCompletedInitialScan(...)` 的测试发现链迁移到 `BootCoordinator`，在 `AssetScanReady` 与 `TestsReady` 时分别广播结构化里程碑，而不是继续依赖 `bCompletedAssetScan` 这个内部布尔。<br>5. 保留 `GetOnInitialCompileFinished()` 作为兼容事件，但把它映射为 `ScriptsReady`；需要更细粒度时，宿主和工具改订阅新的 milestone/event。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：默认 editor policy 下 modal retry 行为与今天一致；commandlet policy 仍然 fail-fast；asset scan 延迟存在时 `ScriptsReady`、`AssetScanReady`、`TestsReady` 的触发顺序稳定且可断言。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果 boot milestone 的默认顺序与今天 editor 真实行为不一致，最容易破坏现有 late-init、测试发现和工具脚本的隐式时序；第一阶段必须先把当前顺序建模清楚，再做 policy 抽取 |
| 兼容性 | 向后兼容。默认 runtime module 继续挂接与今天一致的 editor/commandlet policy；旧代码继续监听 `OnInitialCompileFinished` 也能收到 `ScriptsReady` 映射 |
| 验证方式 | 1. 回归启动失败修复流程，确认 editor 仍可在 modal 中修脚本并继续进入工程。<br>2. 回归 commandlet 与 `bExitOnError` 路径，确认仍然 fail-fast。<br>3. 新增 milestone 顺序测试，验证 `ScriptsReady` 不会伪装成 `TestsReady`，asset scan 未完成时也不会提前暴露测试就绪。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-40 | 启动阶段 thread affinity 与扩展安全接入 | startup phase + affinity 协议 | 高 |
| P2 | Arch-SL-41 | 启动恢复策略与 readiness milestone 对外契约 | boot policy + milestone coordinator | 中 |

---

## 架构分析 (2026-04-08 23:50)

### Arch-SL-42：reload 触发面仍然是 file-queue-only，缺少逻辑模块级失效与重载 API

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块热替换入口是否已经从“文件变化”抽象成“逻辑模块请求”，从而支持显式 hot replace、生成模块、内存模块与版本切换 |
| 当前设计 | 当前 runtime 对外暴露的 lifecycle 入口仍是 `InitialCompile()`、`CompileModules()`、`CheckForHotReload()`、`Tick()`；真正的 reload 请求只能先落成 `FFilenamePair` 队列，再由 `CheckForHotReload()` 拼出 `FileList` 调 `PerformHotReload()` |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:176-205` — 公开入口没有 `ReloadModule(ModuleId)` 或 `InvalidateModule()` 这类逻辑模块 API；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:400-426` — hot reload 状态全是 `FileHotReloadState`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles`、`CheckForFileChanges()` 这类文件级结构；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1658-1700` — checker thread 只负责 `CheckForFileChanges()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2778` — `CheckForHotReload()` 只消费 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` / `QueuedFullReloadFiles`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2859-2905` — runtime change detection也是全量扫磁盘得到 `FFilenamePair`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:55-87` — editor watcher 也只是把文件增删改直接塞进 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` |
| 优点 | 文件系统事件接入非常直接，editor 保存脚本即可触发既有 reload 主链；对当前以磁盘 `.as` 文件为唯一事实来源的项目来说，心智负担低 |
| 不足 | 逻辑模块系统没有 first-class invalidation contract，导致“内存源码替换”“manifest 切换同一 `ModuleId` 的 version/variant”“强制只 reload 一个逻辑模块”“生成模块不落盘”都必须伪装成文件变化；未来即使补了 resolver、lazy load 或 mixed precompiled runtime，reload 入口仍会被 file queue 反向拉回文件视角 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | C++ 侧直接暴露 `FJsEnvImpl::ReloadModule(FName ModuleName, const FString& JsSource)`；JS 侧 `modular.js` 则提供 `forceReload(reloadModuleKey)`，并把失效粒度落到 `moduleCache[key]` | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1482-1514`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-225` | reload 请求可以直接按逻辑模块 key 或源码载荷发起，不需要先制造一条假的文件事件 |
| UnLua | `require('UnLua.HotReload').reload()` 最终落到 `M.reload(module_names)`；如果调用方传入模块列表，就只 sandbox-load 那批模块并提交 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp:51-54`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-624` | reload 入口天然接受“模块集合”，文件时间戳只是一种默认收集方式，而不是唯一入口 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有文件监听兼容层的前提下，引入 `module reload request`，把热替换入口从文件队列升级为逻辑模块请求 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleReloadRequest`，字段至少包含 `ModuleIds`、`DirtyFiles`、`SourceOverrides`、`Reason(FileChange/ExplicitReload/VersionSwitch/Generated)`、`RequestedCompileType`。<br>2. 新增 `RequestReloadModules(const FAngelscriptModuleReloadRequest&)` 与 `RequestInvalidateModules(...)`；第一阶段内部仍然把请求翻译成现有 `PerformHotReload()` 所需输入，先不改 reload 算法。<br>3. 把 `StartHotReloadThread()`、`CheckForFileChanges()` 和 `AngelscriptDirectoryWatcherInternal.cpp` 都降级成 adapter：它们只负责把文件变化映射到 `ModuleIds + DirtyFiles` 后调用新 API，而不是直接写运行时私有数组。<br>4. 允许 `SourceOverrides` 携带内存源码或 generated module 文本；这样未来的 `lint fix preview`、manifest version 切换、网络同步脚本都能复用同一 reload 入口。<br>5. 保留 `CheckForHotReload()` 作为兼容 drain 点，但它改为消费“待处理 reload requests”，而不是只认 `FFilenamePair`。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：无文件变化时显式 `RequestReloadModules({Foo})` 仍可触发单模块 reload；generated/in-memory module 可以不落盘就进入编译；旧 directory watcher 路径继续得到与当前一致的结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险在于“文件路径”和“逻辑模块”短期内要双轨并存；如果 adapter 映射和 journal key 定义不一致，最容易出现 watcher 路径与显式 reload 路径行为漂移 |
| 兼容性 | 向后兼容。旧 editor 保存文件、旧 checker thread 和现有 `CheckForHotReload()` 流程都继续可用；显式模块 reload 只是新增入口，不要求现有脚本项目迁移 |
| 验证方式 | 1. 回归现有 file watcher / checker thread hot reload 测试，确认旧路径行为不变。<br>2. 新增“显式模块重载”回归，验证不依赖文件时间戳也能替换单个模块。<br>3. 新增 generated/in-memory module 回归，验证 `SourceOverrides` 能进入编译并在失败时正确回滚。 |

### Arch-SL-43：模块搜索面仍是 startup snapshot，runtime compile roots 与 editor watcher roots 不共享可刷新的 search profile

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块搜索根与 watcher 注册是否是统一、可刷新、可配置的 search surface，从而支持版本化模块、下载脚本、动态挂载 root 与未来 loader 扩展 |
| 当前设计 | 当前搜索面由多个 startup snapshot 组成：runtime 在 `Initialize_AnyThread()` 先记住 project root，`InitialCompile()` 再一次性刷新成 `MakeAllScriptRoots()`；editor 模块则在 `StartupModule()` 单独再算一份 `MakeAllScriptRoots()` 去注册 directory watcher。推断：当前未见共享 search profile 或 roots 变更广播，runtime compile roots 与 editor watcher roots 只在启动时“恰好一致” |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1369` — `DiscoverScriptRoots()` / `MakeAllScriptRoots()` 每次现算一组 root；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1430-1431` — runtime 初始化阶段先只 `DiscoverScriptRoots(true)` 保留 project root；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1999-2015` — 之后所有 `FindAllScriptFilenames()` 都只遍历成员 `AllRootPaths`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2038-2078` — `InitialCompile()` 才把 `AllRootPaths` 刷成 `MakeAllScriptRoots()` 并全量扫盘；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:366-380` — editor watcher 在 `StartupModule()` 里独立调用 `FAngelscriptEngine::MakeAllScriptRoots()` 注册目录监听，而不是复用 engine 内已有 root profile |
| 优点 | 启动时 root 顺序明确，project root 优先、plugin root 排序稳定；对当前“启动后根目录基本不变”的工作流来说足够简单 |
| 不足 | 搜索面不是一等对象，导致“运行中新增/移除插件脚本根”“下载目录挂载为新 script root”“同一工程在不同 runtime/profile 下切换根集合”都缺少增量刷新路径；更关键的是 runtime compile 与 editor watcher 各自缓存一份 roots，后续一旦根集合变化，二者容易发生漂移 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块搜索面首先是 `IJSModuleLoader` 对象；`DefaultJSModuleLoader::Search()` 在每次请求时都基于 `RequiredDir + ScriptRoot` 做解析，而不是依赖 runtime/editor 两份隐式 roots 快照 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-45`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-141` | 即便 root 仍由宿主提供，search surface 至少是显式 loader contract，可以被替换、复用或按 env 构造 |
| UnLua | `SetPackagePath()` 允许显式修改 `package.path`；`FLuaEnv::AddSearcher()` 还能在 env 上插入新的 `package.searchers`，让搜索面作为 runtime 状态存在 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp:277-293`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:644-667` | 搜索路径与 loader 链是可刷新、env-scoped 的协议，而不是仅在启动时拍扁成一次性 root 列表 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入共享的 `module search profile`，让 runtime compile、editor watcher 与 future loader/catalog 读取同一份可刷新的搜索面 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleSearchProfile`，字段至少包含 `Roots`、`Priority`、`NamespacePrefix`、`bWatchEnabled`、`ProfileVersion`。<br>2. 让 `DiscoverScriptRoots()` / `MakeAllScriptRoots()` 降级为默认 profile builder；`FAngelscriptEngine` 持有 `ActiveSearchProfile`，`FindAllScriptFilenames()` 不再直接读裸 `AllRootPaths`，而是读 profile 快照。<br>3. 新增 `RefreshSearchProfile(ERefreshReason)` 与 `OnSearchProfileChanged`；默认实现只在 plugin enable/disable、runtime mount、settings 变更时重算 roots，并输出 diff。<br>4. 将 `AngelscriptEditorModule.cpp` 的 watcher 注册改为订阅 `OnSearchProfileChanged`：初始注册与后续增量 re-register 都使用 engine 当前 profile，而不是自己再算一遍 `MakeAllScriptRoots()`。<br>5. 为 future versioned/lazy modules 增加 `AddSearchRoot()` / `RemoveSearchRoot()` / `SetSearchProfileOverride()` API；没有显式调用时，仍使用今天的静态 roots 顺序。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 增加三类回归：plugin root 启停后 runtime compile 与 watcher 看到同一组 roots；新增下载目录 root 后无需重启即可被发现；未启用动态 search profile 时行为与当前完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 主要风险在 watcher 重注册和 compile roots 刷新之间的时序；如果 roots diff 生效顺序不一致，最容易出现“watcher 已监听新根，但 compile 仍看旧根”或反过来的短暂双重真相 |
| 兼容性 | 向后兼容。默认仍由当前 `project root + sorted plugin roots` 生成静态 profile；只有显式调用刷新或动态 root API 的项目才会进入新能力 |
| 验证方式 | 1. 回归现有 script root discovery 与 directory watcher 测试，确认默认静态 profile 下结果不变。<br>2. 新增动态 root 回归，验证 runtime compile 与 watcher 始终共享同一 profile version。<br>3. 验证 plugin root 移除后相关 watcher 被注销，且后续 reload 不会继续扫描已下线 roots。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-42 | reload 触发入口仍绑定文件事件 | 逻辑模块 reload request 新增 | 高 |
| P2 | Arch-SL-43 | 模块搜索根与 watcher 生命周期分裂 | shared search profile / refresh 协议 | 中 |

---

## 架构分析 (2026-04-08 23:59)

### Arch-SL-44：逻辑模块标识仍然复用可变 `asCModule` 名称，clone 前缀与 reload 临时名会泄漏到生命周期接口

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块系统是否拥有稳定的逻辑 key，以支撑 clone、多 runtime、热替换目标定位、版本化模块与可读 diagnostics |
| 当前设计 | 当前同时把逻辑模块名、runtime 作用域和 reload 临时代号编码进 `asCModule::GetName()`；`FAngelscriptModuleDesc::ModuleName` 才是逻辑名，但大量运行时查找、导入绑定和错误消息仍回退到 backend 名称 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:595-603` — `MakeModuleName()` 在 clone 模式下把 `InstanceId::ModuleName` 拼进模块名；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4251-4260` — 非初编译时先用 `ModuleName_NEW_N` 创建临时 `asCModule`，同时单独把逻辑名写入 `baseModuleName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2914-2938` — swap-in 时旧模块被改名为 `Module_OLD_N`，新模块再改回 `InternalModuleName` 并写入 `ActiveModules`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4620-4626,4671-4694` — declared import 绑定和错误消息都直接消费 `Module->GetName()` / `ScriptModule->GetName()` / `GetImportedFunctionSourceModule()` |
| 优点 | 直接复用 AngelScript 自身的模块查找语义，clone 与热更都能快速避开 backend 名称冲突，实现成本低 |
| 不足 | 同一逻辑模块在生命周期中会暴露为 `Foo.Bar`、`Instance::Foo.Bar`、`Foo.Bar_NEW_7`、`Foo.Bar_OLD_6` 等多个名字；这让 reload 定位、调试显示、跨 runtime 对比和未来的版本化模块都很难围绕稳定 key 建模，外部工具也无法只依赖一个恒定标识 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require()` 先把请求规范化，再用解析后的 `fullPath` 作为 `moduleCache` 稳定 key；`forceReload()` 和 `getModuleByUrl()` 都只针对这个 key，临时 `sid` 只是执行期索引，不参与模块标识 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:107-145`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-223` | 把“稳定模块 key”与“执行期临时句柄”分开，reload、cache 和外部查询都围绕同一 key 工作 |
| UnLua | `package.loaded`、`loaded_modules` 和 `module_loaded` hook 始终用 `module_name` 作为逻辑标识；热更时即便生成新模块对象，外部可见 key 仍然不变 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:151-170`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:560-592` | 先固定逻辑模块名，再让新旧对象、环境和热更合并逻辑在内部演进，避免 backend 句柄名泄漏到生命周期接口 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入稳定的 `logical module key`，把 backend `asCModule` 名称降级成内部句柄，不再让 `_NEW/_OLD` 或 `InstanceId::` 直接成为外部模块身份 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleKey`，至少包含 `LogicalModuleId`、`RuntimeId`、`GenerationId`；`FAngelscriptModuleDesc` 保存该 key，而不是只保存裸 `ModuleName`。<br>2. 保留 `MakeModuleName()` 作为 backend 名称生成器，但仅用于 `Engine->GetModule(...)`；对外查找、diagnostics、declared import 绑定和调试显示统一走 `FAngelscriptModuleKey`。<br>3. 新增 `TMap<asIScriptModule*, FAngelscriptModuleKey>` 或等价 side table；把当前 `ResolveDeclaredImports()`、`CheckFunctionImportsForNewModules()` 中对 `GetName()` 的依赖改成“句柄 -> 稳定 key -> 逻辑模块”。<br>4. 兼容阶段保留“旧 backend 名称 -> 稳定 key”的 alias lookup，这样已有依赖 `GetName()` 文本的日志/工具不会立即失效，但新增路径优先输出逻辑 key。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：clone runtime 下同名模块可共享逻辑 key 但区分 `RuntimeId`；reload 失败与 declared import 错误不再暴露 `_OLD/_NEW` 临时名；按模块目标执行 reload/debug 查询时可用稳定 key 命中当前 generation。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 主要风险在兼容期双标识并存：如果某些路径继续偷读 backend `GetName()`，而另一些路径已经切到稳定 key，就会出现 diagnostics、reload 与调试器各说各话的短暂分裂 |
| 兼容性 | 向后兼容。旧 backend 名称仍可在 alias 层被解析；现有脚本语法、模块名和目录结构都不需要改变，只是运行时新增稳定 key 视图 |
| 验证方式 | 1. 构造 clone runtime，同一逻辑模块在两个 runtime 中并存，确认日志与查询都能稳定区分 `RuntimeId` 而不依赖 `InstanceId::` 字符串拼接。<br>2. 构造一次 hot reload 失败，确认新错误消息不再直接显示 `_OLD/_NEW` 模块名。<br>3. 回归现有 import / reload / debugger 测试，确认 alias 兼容层下旧行为不回退。 |

### Arch-SL-45：reload 提交后没有一等 `module generation ledger`，旧 generation 会在切换完成后立即被遗忘

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块热替换是否保留可追踪的 generation 历史，从而支撑版本化模块、热替换审计、回滚分析和 side-by-side 对比 |
| 当前设计 | 当前只有“本轮新模块”和“待丢弃旧模块”的栈上集合；一旦 `SwapInModules()` 与后续 cleanup 完成，旧 generation 就被 backend 改名、丢弃并从索引中移除，没有长期可查询的 generation 账本 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2914-2945` — 旧模块仅被放进 `DiscardedModules`、改名为 `*_OLD_N` 并从 `ModulesByScriptModule` 移除；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4018-4044` — reload 提交后旧模块马上 `DiscardModule()` / `DeleteDiscardedModules()`，随后 `ModulesByScriptModule` 只回填新模块；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4251-4260` — 新模块虽保存了 `baseModuleName`，但这个逻辑名不会形成持续的 old/new generation 链；推断：当前“新旧两代并存”的窗口只存在于一次 reload 调用栈内部 |
| 优点 | 清理路径直接、内存滞留短，当前 soft/full reload 的提交语义比较简单，不需要维护长期历史结构 |
| 不足 | 版本化模块、预览编译、reload 审计、按 generation diff diagnostics、失败后继续分析“上一代为什么安全/新一代为什么失败”都缺少基础账本；一旦本轮 cleanup 结束，系统只剩“当前代”，之前的 generation 信息和映射关系就消失了 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `reload_modules()` 在 sandbox 中先同时保留 `old_modules`、`new_modules` 和 `module_envs`，只有全部新 chunk 成功执行后才调用 `update_modules(old_modules, new_modules, module_envs)` 合并 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:560-600`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-515` | 把“旧代对象”和“新代对象”明确建模成提交窗口内的一等数据，合并逻辑可以围绕 generation 对来实现，而不是先删后说 |
| puerts | `forceReload()` 针对稳定 `moduleCache` key 打标，reload 始终围绕这个逻辑槽位进行；即便内部临时创建新 `m` 对象，模块系统的长期锚点仍是同一个 cache key | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-145`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-220` | 先有稳定的“模块槽位/世代锚点”，再决定如何替换其当前内容，这样版本切换和 reload 审计才有承载位置 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 reload 流程外加一层 `module generation ledger`，把“当前代”“上一代”“staged 新代”“discarded 旧代”收敛成可查询的长期记录，而不是只靠 `DiscardedModules` 临时数组 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleGeneration` / `FAngelscriptModuleLedgerEntry`，字段至少包括 `ModuleKey`、`GenerationId`、`PreviousGenerationId`、`BackendModuleName`、`CompileFingerprint`、`ReloadOutcome`、`CommitState(Staged/Committed/RolledBack/Discarded)`。<br>2. 在 `CompileModules()` / `SwapInModules()` 开始前为每个待编译模块创建 staged generation；旧 generation 不再只靠 `DiscardedModules` 临时保存，而是先转为 `CommittedButSuperseded` 状态。<br>3. 把 `DiscardModule()` / `DeleteDiscardedModules()` 变成“清 backend 句柄”动作，而不是“删除整条 generation 记录”；默认只保留最近一代 metadata，控制内存开销。<br>4. 让 `ModulesByScriptModule`、`PreviouslyFailedReloadFiles`、future version switch 与 debug 查询都先落到 ledger，再由 ledger 反查当前 committed generation；这样失败分析和 targeted rollback 都有统一入口。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 接入 generation pair，让 soft/full reload、default object 初始化和后续验证可以明确知道自己正在处理哪一对 old/new generation。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：成功 reload 后仍可查询上一代 metadata；失败 rollback 时旧 generation 继续保持 committed；启用 versioned/preview compile 时 staged generation 不会误暴露成 active generation。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把“metadata 历史保留”误做成“旧 backend 模块永久保留”，从而造成内存和类型句柄泄漏；第一阶段必须只保留 generation 元数据或极短生命周期的句柄引用 |
| 兼容性 | 向后兼容。默认 retention depth 可以是 `1` 且只保留 metadata；现有脚本、reload 入口和用户工作流都不需要改写 |
| 验证方式 | 1. 构造成功 soft/full reload，确认 ledger 能查询到 `current + previous` 两代且 active generation 唯一。<br>2. 构造 reload 失败并回滚，确认旧 generation 仍保持 committed，新 generation 标记为 rolled back。<br>3. 回归现有 hot reload 测试，确认默认 retention depth 下功能行为和内存释放时序不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-44 | 模块稳定 key 与 backend 临时名混用 | stable module key / alias 兼容层 | 高 |
| P2 | Arch-SL-45 | 缺少可追踪的 module generation ledger | generation ledger / metadata retention | 中 |

---

## 架构分析 (2026-04-09 00:07)

### Arch-SL-46：条件编译分支中的 `import` 会在预处理期被直接抹除，依赖图缺少可消费的 conditional edge

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 条件编译、版本化模块与按需加载所依赖的模块图，是否能保留“当前 profile 未激活但未来可能激活”的依赖边 |
| 当前设计 | 预处理器会先把 `#if/#ifdef/#ifndef` 为 false 的源码段整体改成空白；`import` 只在“当前激活分支”里被记录进 `File.Imports`，随后再转成 `ImportedModules`。结果是依赖图只保存当前 compile profile 的投影，不保留 conditional import 元数据 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3256-3403` — `#ifdef/#ifndef/#if` 先更新 `IfDefStack`，随后在 `bIfDefStackIsFalse` 时把非空白字符直接改写为空白；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3489-3510` — 顶层 `import` 只有在仍可见时才会生成 `FImport` 并写入 `File.Imports`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:463-483` — `ProcessImports()` 只遍历 `File.Imports`，再把命中的边写入 `File.Module->ImportedModules`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3173-3197` — `CompileModules()` Stage1 也只消费 `Module->ImportedModules` 构造编译期 provider 集 |
| 优点 | 当前 profile 下的编译图非常直接，预处理后不会保留无效边，错误面小，排序与热更闭包计算都比较简单 |
| 不足 | `#if EDITOR import Foo.Editor; #else import Foo.Runtime; #endif` 这类分支在任一时刻都只剩一半依赖边；未来即使补了 manifest、variant、lazy compile 或 warmup planner，也无法只靠当前模块图知道“另一组 profile 切换后还需要哪些模块”，lint 和预检也无法提前验证 dormant dependency |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块变体选择不藏在源码死分支里，而是在 `require()` 请求期通过 `searchModule()`、`package.json main/exports` 决定真正入口；选中的模块 key 再进入 `moduleCache` | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-179`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-191` | 把“当前该选哪个变体”提升到 loader/request 层，模块系统始终持有显式请求与显式入口，而不是依赖预处理把另一条边抹掉 |
| UnLua | `Start(StartupModuleName)` 明确传入启动模块名；后续 `package.searchers` / `package.path` 负责解析，`require(module_name)` 与 `loaded_modules/package.loaded` 也始终围绕显式模块名工作 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-249`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:597-667`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170` | 先保留“请求了哪个模块”的事实，再由 loader 决定命中来源；模块图和缓存边界不会因为 chunk 内部条件分支而失去可见性 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不改变当前编译结果的前提下，为预处理器补充 `conditional import metadata`，把“当前活跃边”和“条件边”分开建模 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` 新增 `FConditionalImportDesc`，字段至少包含 `ModuleName`、`ConditionExpr`、`BranchKind`、`FileLineNumber`、`bActiveInCurrentProfile`。<br>2. 调整 `ParseIntoChunks()` 的处理顺序：即便当前 `bIfDefStackIsFalse`，也在顶层保留对 `import` 语句的轻量扫描，把它写入 `ConditionalImports`，而不是仅靠“抹成空白后完全丢失”。<br>3. `ProcessImports()` 第一阶段继续只把 `bActiveInCurrentProfile=true` 的边转成 `ImportedModules`，保证现有编译顺序与错误行为不变；新增 `AllImportEdges = Active + Conditional` 供 resolver、lint、profile 预检和 lazy planner 使用。<br>4. 在后续 manifest/resolver 落地时，让 `ConditionExpr` 可映射到 `CompileProfile`/`variant tag`，这样 profile 切换前就能预先计算候选闭包，而不是等切 profile 后再全量重扫源码。<br>5. 新增一个 opt-in 诊断开关，例如 `bValidateInactiveImports`：默认只记录 metadata，不对 inactive edge 报缺失错误；启用后可在分析模式下校验“当前未激活但声明存在”的模块是否可被解析。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 主要风险在于“记录条件边”如果误参与了现有 `ImportedModules` 拓扑排序，会直接改变今天的编译顺序；第一阶段必须坚持 metadata-only，不让 inactive edge 干扰现有 compile closure |
| 兼容性 | 向后兼容。默认编译结果、排序和报错行为保持现状；新增的只是附加元数据与可选分析诊断，旧脚本不需要改写 |
| 验证方式 | 1. 增加 `#if EDITOR import Foo.Editor; #else import Foo.Runtime; #endif` 回归，确认当前 profile 仍只编译激活边，但 metadata 同时记录两条边。<br>2. 增加 `AnalyzeOnly`/lint 回归，验证 inactive import 缺失时只在显式启用分析诊断后报告。<br>3. 对比改造前后的 `ImportedModules` 和编译顺序，确认 legacy compile output 无漂移。 |

### Arch-SL-47：模块执行结果仍然是隐式全局副作用，缺少可缓存的 `module runtime entry / exports` 契约

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本“编译完成”之后如何进入运行态，是否存在一等的模块实例、启动结果与可缓存 exports 句柄，从而支撑 lazy activation、热替换提交与运行态检查 |
| 当前设计 | 当前公开生命周期 API 只有 `InitialCompile()`、`CompileModules()`、`Tick()` 等编译/轮询入口；模块运行态主要通过 `ResetGlobalVars(0)` 初始化全局变量，再由 `PostInitFunctions` 做一次性副作用调用，没有显式 `Start/Require`、模块实例对象或结构化启动结果 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:162-205` — runtime API 没有单独的模块启动/获取导出接口；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1272-1306` — `FAngelscriptModuleDesc` 保存的是 `ImportedModules` 和 `PostInitFunctions`，没有 `Exports`、`RuntimeEntry` 或 `BootResult`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4403-4410` — Stage4 直接 `ResetGlobalVars(0)`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2302-2304` — reload 尾部硬编码执行 `CallPostInitFunctions()` 与 `InitDefaultObjects()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5775-5800` — `CallPostInitFunctions()` 只是按名字查全局函数并 `Context->Execute()`，不传参数也不汇总结果；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4090-4133` — literal asset lowering 还会直接把 `Get{Name}` 注入 `PostInitFunctions`，说明当前执行契约本质上是“编译后副作用列表” |
| 优点 | 对现有 static import + class generator 模型最省事，不需要额外的模块实例层，也容易与当前全局脚本类型系统整合 |
| 不足 | 缺少显式 `module runtime entry` 后，系统无法表达“这个模块已经编译，但尚未启动”“这个模块的启动是否成功”“我现在缓存/替换的是哪个模块实例”；lazy activation、版本切换、热更失败回滚和运行态调试都只能围绕全局变量与一次性副作用猜测，而不是围绕稳定的模块实例对象工作 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `executeModule()` 显式创建 `module.exports` 并返回；`genRequire()` 把 `m.exports` 存进 `moduleCache[key]`，失败时回滚该 entry；启动时 `Start()` 直接调用 env 里的 `Require` | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:56-71`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-191`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551` | 把“模块执行结果”建成一等 cache object 后，启动、lazy load、reload 与失败回滚都可以围绕同一个 runtime entry 运转 |
| UnLua | `Start(StartupModuleName)` 本质上是一次 `require`；`require(module_name)` 会把返回的 `new_module` 写入 `loaded_modules/package.loaded`，热更时 `reload_modules()` 先在 sandbox 中生成 `new_module`，成功后才 `update_modules()` 提交 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-249`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601` | 模块实例、启动结果与提交时机都是显式对象，运行态可缓存、可替换、可回滚，而不是只剩“执行过一些全局副作用” |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持现有 `PostInitFunctions` 兼容路径的前提下，引入显式 `module runtime entry`，把启动结果和可导出句柄从“副作用”升级为 runtime state |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleRuntimeEntry` 与 `FAngelscriptModuleActivationReport`，至少包含 `ModuleKey`、`ActivationState`、`LastBootError`、`LastBootTime`、`Exports`（可先是 `FunctionName/GlobalName -> handle` 的轻量映射）。<br>2. 新增 `ActivateModule(ModuleId, Args)`、`GetModuleRuntimeEntry(ModuleId)` API；第一阶段默认在 compile/swap-in 后自动调用它，以复用今天的 eager 行为。<br>3. 支持可选 sidecar 或 manifest 字段（例如 `startup`、`exports`）；未声明时仍走 legacy path，把 `PostInitFunctions` 作为默认 startup 列表，保证旧脚本零迁移。<br>4. 将 `CallPostInitFunctions()` 改成填充/更新 `ModuleRuntimeEntry` 的兼容实现：执行前记录目标 entry，执行后汇总成功/失败状态，而不是只做 fire-and-forget。<br>5. 在 hot reload 路径上保留旧 `RuntimeEntry` 直到新 entry `ActivateModule()` 成功；若启动失败，则回滚到旧 entry 并输出结构化 activation diagnostics，而不是只留下部分全局副作用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把 `Exports` 直接绑定为会随 reload 失效的原始脚本指针，导致外部缓存到 stale handle；第一阶段更安全的做法是缓存“名字到当前 generation 的可解析句柄”，在访问时再通过模块 key 解析 |
| 兼容性 | 向后兼容。默认仍可在 compile 后自动激活，并继续执行旧 `PostInitFunctions`；显式 `startup/exports` 仅为新增 opt-in 能力，不要求现有脚本改语法 |
| 验证方式 | 1. 回归现有 literal asset / post-init 行为，确认未声明 `startup/exports` 的旧模块运行结果不变。<br>2. 新增显式 activation 回归，验证 `ActivateModule()` 可返回稳定的 runtime entry，重复调用命中同一 entry 直到 reload。<br>3. 构造一次启动失败的 hot reload，确认系统保留旧 runtime entry，不会留下半启动状态。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-46 | 条件编译导致的 conditional import 丢失 | 依赖图元数据补全 | 高 |
| P2 | Arch-SL-47 | 模块执行结果缺少显式 runtime entry / exports | 运行态契约补强 | 中 |

---

## 架构分析 (2026-04-09 00:18)

### Arch-SL-48：hot reload 期间任何一个模块 parse 失败都会打开 engine-global 诊断抑制，失败隔离粒度过粗

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译/热更失败是否按 `module` 或 `compile request` 隔离诊断，从而支持 `lint`、分析模式和定向 reload |
| 当前设计 | 当前热更批次里只要任意模块在 parse / `BuildGenerateTypes` 阶段失败，就会把 `bIgnoreCompileErrorDiagnostics` 这个 engine 级布尔打开；随后 `LogAngelscriptError()` 会直接吞掉后续编译器消息，直到整次 `BuildCompleted()` 才恢复 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:510-529` — `Diagnostics` 与 `bIgnoreCompileErrorDiagnostics` 都挂在 engine 全局状态上；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3125-3130` — 诊断桶按 `Section.AbsoluteFilename` 建立，但仍属于本次 engine 编译会话；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3260-3264` — 非 `Initial` 编译只要已有错误就打开全局忽略开关；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3862-3863` — 直到 `BuildCompleted()` 后才重置该开关；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5012-5019` — `LogAngelscriptError()` 一旦看到该布尔就直接 `return` |
| 优点 | 对当前“文件批量热更”路径来说能快速压制依赖级连错误，避免一次保存触发大面积噪声日志 |
| 不足 | 失败隔离粒度过粗：同批次中与出错模块无关的其它模块也会失去后续诊断；未来即使接入 `lint`、`AnalyzeOnly`、显式模块 reload 或优化 pass，也无法得到稳定、可机读的“本模块真实错误集合” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `ReloadModule()` / `JsHotReload()` 只围绕目标 `ModuleName` 执行一次 reload，查找失败、执行异常都以该模块为边界返回或记录；`forceReload()` 也只给指定 `moduleCache[key]` 打标 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1468-1514`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3554-3569`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-219` | 失败与重载目标天然绑定到逻辑模块，不需要一个“吞掉后续所有错误”的全局总闸 |
| UnLua | `reload_modules(module_names)` 先以显式 `module_names` 进入 sandbox，再逐个 `sandbox.load(module_name)`；任一模块失败会终止本次 reload，但失败边界仍然是调用方传入的模块集合，而不是整个运行时的全局诊断面 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170` | 把失败批次建模成显式模块集合，后续才能追加“仅抑制受依赖污染的模块”而不是无差别静音 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入 request-scoped `diagnostic batch`，把“噪声抑制”从 engine 全局布尔改成按模块/依赖关系生效的策略对象 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptDiagnosticBatch`，字段至少包含 `RequestId`、`ModuleKeys`、`Issues`、`SuppressedModules`、`SuppressionReason`、`bHadParseFailure`。<br>2. 让 `CompileModules()` 在进入每轮 `CurrentCompileList` 前创建 batch，并在 Stage1/Stage2/Stage3/Stage4 内把“当前 builder 正在处理哪个模块”写入 thread-local 或 request-local 上下文。<br>3. 把 `bIgnoreCompileErrorDiagnostics` 替换为 `ShouldSuppressDiagnostic(ModuleKey, SourcePhase)`；第一阶段只抑制“依赖于 parse 失败模块的 follow-on diagnostics”，不抑制同批次中独立模块的错误。<br>4. 保留现有 `Diagnostics` / `EmitDiagnostics()` 作为兼容视图，但它们改为从 batch 汇总生成；这样 debug server 仍然收到文件级结果，而 `lint` / 分析工具可直接消费结构化 batch。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：`A` parse 失败时独立模块 `B` 的类型错误仍被保留；依赖 `A` 的 `C` 被抑制并带 `SuppressionReason`；显式 `RequestReloadModules({A})` 只产生 `A` 的 diagnostic batch。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 关键风险在并行 parse 阶段的“消息归属”判定；如果 thread-local / request-local 上下文绑定不稳，诊断可能被错误归属到别的模块 |
| 兼容性 | 向后兼容。现有 debug server、文件级 diagnostics 和日志格式可以保留；新增的只是更细粒度的 batch 元数据与抑制规则，不要求现有脚本改写 |
| 验证方式 | 1. 构造两个互不依赖模块的并行 hot reload，确认一个 parse 失败不会吞掉另一个模块的错误。<br>2. 构造依赖链 `A -> C`，验证 `C` 的 follow-on diagnostics 被标记为 suppressed，而不是静默消失。<br>3. 回归现有热更与 diagnostics 测试，确认默认日志和 debug server 行为没有回退。 |

### Arch-SL-49：模块请求层仍不知道“源码 / 预编译 artifact”是同一模块的两种装载形态

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本加载能否在模块请求时按需选择 `source`、`precompiled artifact` 或 `generated text`，从而支撑 lazy load、mixed-mode 和增量热更 |
| 当前设计 | 当前 `source` 与 `PrecompiledData` 仍是两条启动期分叉路径：`InitialCompile()` 要么整次运行都走 `PrecompiledData->GetModulesToCompile()`，要么整次运行都跑 `Preprocessor` 扫磁盘；后续虽然 `CompileModule_Types_Stage1()` 能在模块级命中 `PrecompiledData`，但这只是编译内部优化，不是模块请求层的统一装载契约 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2046-2056` — 使用 `PrecompiledData` 时直接跳过预处理并显式提示“Hot reloading is disabled for this run”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2068-2082` — 否则就是全量磁盘扫描 + `Preprocessor.GetModulesToCompile()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4258-4297` — 模块进入 Stage1 后才基于 `ModuleName + CodeHash` 尝试命中 `PrecompiledData`，且要求 `bAllImportsPreCompiled`；推断：artifact 格式选择发生在 boot mode 和编译内部，而不是“模块请求 -> artifact provider”的统一协议上 |
| 优点 | 全源码模式与全预编译模式边界清楚，当前启动流程相对容易理解，`PrecompiledData` 命中路径也不需要额外 loader 抽象 |
| 不足 | 无法表达“冷门模块优先吃 precompiled，活跃开发模块继续走 source，generated 模块走内存文本”这类 mixed-mode；lazy load 即使落地，也缺少一个可在模块请求时选择最佳 artifact 的入口；热更与版本切换也只能围绕当前 boot mode 打补丁 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `DefaultJSModuleLoader::Search()` 在每次模块请求时统一解析 `.js/.mjs/.cjs/.json/.mbc/.cbc/package.json/index.js`；`genRequire()` 再根据 `fullPath` 决定读取源码还是 bytecode，并把结果写回同一个 `moduleCache[key]` | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-123`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-191`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4079-4122` | artifact 格式选择是模块加载契约的一部分，而不是“整次运行先选一种模式” |
| UnLua | `AddSearcher()` 把 `LoadFromCustomLoader` 与 `LoadFromFileSystem` 链到同一个 `package.searchers`；两条 loader 最终都把 `TArray<uint8>` 喂给 `LoadString()/LoadBuffer()`，模块请求并不关心字节来自下载目录、工程目录还是自定义 loader | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-608`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:413-439`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:644-667` | 先统一“按模块请求拿到一份可执行 artifact”的契约，再决定 artifact 的具体来源和格式 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 resolver / loader 之下增加 `module artifact provider`，让 `source`、`precompiled artifact`、`generated text` 成为同一模块的可协商装载形态 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleArtifact` 与 `IAngelscriptModuleArtifactProvider`，字段至少包含 `ModuleKey`、`Format(Source/Precompiled/Generated)`、`PayloadRef`、`DebugPath`、`Fingerprint`、`CanHotReload`。<br>2. 默认提供两个 provider：`SourceArtifactProvider` 复用现有 `Preprocessor/AddFile` 路径，`PrecompiledArtifactProvider` 包装现有 `PrecompiledData` 查询；provider 顺序由 runtime config 决定，默认保持今天“source 优先或全预编译”的行为。<br>3. 把 `InitialCompile()` 的启动分叉改成“先构造请求，再向 provider chain 取 artifact”；当模块未命中 precompiled 或被标记 `CanHotReload=true` 时自然回退到 source，而不是直接切整次运行模式。<br>4. 让未来的 `EnsureModuleCompiled()`、`RequestReloadModules()` 和 generated/in-memory module 都复用同一 artifact provider 链；这样 lazy load、版本切换和 generated module 不需要再各自发明装载入口。<br>5. 第一阶段继续保留现有 `-as-generate-precompiled-data` / `-as-ignore-precompiled-data` 作为 provider 排序开关，避免打断旧流程；第二阶段再把“全预编译禁用热更”收敛成“仅对 `CanHotReload=false` 的 artifact 禁用热更”。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：模块 `A` 命中 precompiled、模块 `B` 回退到 source、模块 `C` 来自 generated text 时仍可在同一运行时共存；显式热更只让 `B/C` 失效，不强制整个运行时退出 precompiled 路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 主要风险是 artifact 指纹不完整导致错误命中旧字节码；第一阶段必须把 `ModuleKey + CombinedDependencyHash + CompileProfile/ProviderVersion` 纳入 fingerprint，不能继续只靠单一 `CodeHash` |
| 兼容性 | 向后兼容。旧脚本目录、`import` 语法和启动参数都可继续使用；默认 provider 顺序保持现状，只有显式启用 mixed-mode 或 lazy load 的项目才会观察到新能力 |
| 验证方式 | 1. 构造 mixed-mode 运行时，验证同一轮启动里 `A` 走 precompiled、`B` 走 source、`C` 走 generated artifact。<br>2. 验证 source 模块热更后不会让未修改的 precompiled 模块失效。<br>3. 回归现有 `PrecompiledData` 启动路径，确认未启用新 provider 组合时行为与今天一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-49 | 源码 / 预编译 artifact 仍不是统一模块装载契约 | artifact provider / mixed-mode loader | 高 |
| P2 | Arch-SL-48 | hot reload 诊断抑制仍是 engine-global | request-scoped diagnostic batch | 中 |

---

## 架构分析 (2026-04-09 00:35)

### Arch-SL-50：`OnInitialCompileFinished` 早于 `SharedState` 提交，startup observer 看不到稳定 runtime handle

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 启动完成事件是否真正代表“runtime 已可被扩展点安全消费”，尤其是 `SharedState`、`PrimaryContext`、`DebugServer` 这类后续能力句柄是否已经稳定 |
| 当前设计 | `Initialize()` 在 `Initialize_AnyThread()` 返回后，先调用 `PostInitialize_GameThread()` 广播 `OnInitialCompileFinished`，然后才执行 `InitializeOwnedSharedState()`；因此“脚本初编译完成”与“runtime 共享句柄已提交”并不是同一个里程碑 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:819-857` — `Initialize()` 的固定顺序是 `PreInitialize_GameThread -> Initialize_AnyThread -> PostInitialize_GameThread -> InitializeOwnedSharedState`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653-1655` — `PostInitialize_GameThread()` 只做 `FAngelscriptRuntimeModule::GetOnInitialCompileFinished().Broadcast()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:922-942` — `InitializeOwnedSharedState()` 才把 `ScriptEngine`、`PrimaryContext`、`PrecompiledData`、`StaticJIT`、`DebugServer` 写入 `SharedState`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:628-647` — `CreateCloneFrom()` 还要在发现 `Source.SharedState` 缺失时补做 `Source.InitializeOwnedSharedState()`，说明“engine 已经可用但 shared state 尚未提交”的窗口在真实生命周期中存在 |
| 优点 | 现有实现对“脚本是否编过”给出较早广播，老代码只要做轻量订阅通常能继续工作 |
| 不足 | 新扩展点若把 `OnInitialCompileFinished` 当成 runtime ready 信号，就可能在 `SharedState`、`PrimaryContext` 或 `DebugServer` 尚未稳定时接入；clone/testing/tooling 于是不得不额外补状态，破坏生命周期语义的一致性 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 先执行一组 bootstrap 模块，再把 `__require`、`getESMMain`、`__reload` 这些运行期句柄缓存进 env；公开 `Start()` 时直接消费已经就绪的 `Require`，并在调用后才把 `Started` 置真 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:621-640`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:25-30,61-71` | 先把 env 自己依赖的 runtime handle 建好，再暴露启动入口；“可启动”比“bootstrap 模块执行过”更晚也更可靠 |
| UnLua | `FLuaEnv` 构造函数先创建 `lua_State`、安装 `package.searchers` 和各类 registry，`Start()` 之后才按 `StartupModuleName` 执行 `require` 并将 env 标记为 started | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:74-110`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-252`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:63-65` | runtime 基础设施 ready 与启动入口执行是分层的，扩展点不需要猜一个“编译差不多结束”的时刻是否已经足够安全 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `ScriptsCompiled` 与 `RuntimeReady` 建立两个显式里程碑；短期不重排旧广播，先补一个在 `InitializeOwnedSharedState()` 之后触发的新 ready contract |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `EAngelscriptReadyMilestone` 或 `FAngelscriptRuntimeReadyReport`，至少区分 `ScriptsCompiled`、`SharedStateReady`、`RuntimeReady`。<br>2. 在 `InitializeOwnedSharedState()` 完成后新增 `OnSharedStateReady`/`OnRuntimeReady` 广播，把 `ScriptEngine`、`PrimaryContext`、`DebugServer`、`InstanceId` 等就绪句柄作为 report 内容暴露出去。<br>3. 第一阶段保留 `FAngelscriptRuntimeModule::GetOnInitialCompileFinished()` 原样，避免打断现有监听者；新增扩展点、clone 路径、future lazy loader 与 tooling 全部改订阅新的 ready 事件。<br>4. 在 `CreateCloneFrom()` 中保留当前 defensive `InitializeOwnedSharedState()` 一版作为兼容兜底，但一旦新 ready milestone 稳定，就把 clone 预热收束到源 runtime 的初始化流程，不再在 clone 构造时补交 shared state。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加两类回归：监听 legacy 事件时 `SharedState` 允许为空但会收到明确 milestone；监听新 ready 事件时 `SharedState->PrimaryContext`、`ScriptEngine`、`DebugServer` 必定非空且稳定。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果直接重排旧 `OnInitialCompileFinished` 的触发时机，可能改变少量依赖“越早越好”行为的旧扩展；因此第一阶段更安全的路线是新增更强语义的 ready 事件，而不是立即改旧广播顺序 |
| 兼容性 | 向后兼容。旧广播保留；新扩展和新工具链显式使用 `OnSharedStateReady`/`OnRuntimeReady`；待内部消费者完成迁移后，再决定是否把旧广播映射到新里程碑 |
| 验证方式 | 1. 增加 ready-order 测试，断言 `ScriptsCompiled` 先于 `SharedStateReady`，且后者可见稳定 `PrimaryContext`。<br>2. 增加 clone 测试，验证源 runtime 一旦进入新 ready milestone，`CreateCloneFrom()` 不再需要补初始化 shared state。<br>3. 回归现有启动、热更和 editor 入口测试，确认保留旧广播后默认行为不变。 |

### Arch-SL-51：`bIsInitialCompileFinished` 在 `Full/Test/Clone` 路径上语义失真，无法充当可扩展 lifecycle state

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | runtime 生命周期状态是否能准确表达“只是 bootstrap 完成”“脚本已编译”“clone 仅附着到源 runtime”“可以进入 tick/执行”等不同阶段 |
| 当前设计 | 当前公开 readiness 主要压缩在单一布尔 `bIsInitialCompileFinished` 上，但 `InitializeForTesting()` 会在没有任何脚本编译的情况下把它置真；相反，clone 路径又共享了已编译 engine，却没有把这个布尔从源 runtime 复制过来 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:481-489` — 对外公开的 readiness 查询只有 `bIsInitialCompileFinished`/`IsInitialCompileFinished()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:859-919` — `InitializeForTesting()` 只做 package/engine property/bind bootstrap/context 创建，末尾直接 `bIsInitialCompileFinished = true`，没有 `InitialCompile()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:628-647` — `CreateCloneFrom()` 让 clone 共享 `Source.SharedState` 与底层 engine；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2848-2857` — `AdoptSharedStateFrom()` 复制了 `bDidInitialCompileSucceed`、`bCompletedAssetScan` 等字段，却没有复制 `bIsInitialCompileFinished`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2843-2846` — `ShouldTick()` 甚至只看 `Engine != nullptr`，不表达“这个 runtime 是否真的 scripts-ready” |
| 优点 | 单一布尔对旧启动路径和早期测试代码足够简单，调用方读取成本低 |
| 不足 | 这个布尔已经无法准确区分 `bootstrap-only`、`scripts compiled`、`clone attached to compiled source`、`runtime actually ready to tick`；未来只要引入 `AnalyzeOnly`、lazy activation、strict testing env 或多 runtime handle，这个状态就会继续误导扩展点 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `IJsEnv`/`FJsEnv` 把 `Start(ModuleName, Arguments)` 作为显式 API；实现侧先检查 `Started`，执行入口模块后才把 `Started = true`，没有把“env 已构造”混同为“脚本已启动” | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:25-30,61-71`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551` | 把“env 存在”与“启动模块已经执行”拆开，状态迁移围绕显式 `Start()` 发生 |
| UnLua | `FLuaEnv` 构造时显式以 `bStarted(false)` 开始；只有 `Start(StartupModuleName, Args)` 成功走过 `require` 后才置 `bStarted = true` | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:74-76`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-252`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:63-65` | 明确把“VM 已建立”和“startup module 已执行”分成两个状态，测试或工具环境不需要伪装成 fully started env |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 legacy bool 兼容层，但新增结构化 `LifecycleState` 作为未来扩展 API 的唯一真相来源，先解决 full/test/clone 三条路径的状态可判定性 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptLifecycleState`，至少包含 `CreationMode`、`ReadyPhase(Uninitialized/Bootstrapped/SharedStateReady/ScriptsCompiled/RuntimeReady)`、`bHasCompiledScripts`、`bHasSharedState`、`bCanTick`。<br>2. 让 `Initialize()`、`InitializeForTesting()`、`CreateCloneFrom()`/`AdoptSharedStateFrom()` 都显式写入这份状态：testing 路径可以是 `Bootstrapped + !bHasCompiledScripts`，clone 路径则应继承源 runtime 的 `ReadyPhase/bHasCompiledScripts`，而不是只复制零散布尔。<br>3. 第一阶段保留 `bIsInitialCompileFinished` 和 `IsInitialCompileFinished()` 作为 legacy facade，但 future lazy load、analysis env、boot policy、ready events 和 scheduler 一律改读 `LifecycleState`。<br>4. 增加 `HasCompiledScripts()`、`IsRuntimeReady()`、`CanTickRuntime()` 这类显式查询；在迁移完成前，`ShouldTick()` 可以继续保留旧行为，但新扩展点不得再直接用 `Engine != nullptr` 充当 readiness。<br>5. 为测试体系补两个新 helper：`CreateTestingBootstrappedEngine()` 与 `CreateTestingCompiledEngine()`；旧 `CreateTestingFullEngine()` 先映射到兼容行为，待测试迁移后再收束语义。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：bootstrap-only testing env 不应被识别为 compiled；clone 应继承源 runtime 的 compiled/ready 状态；legacy `IsInitialCompileFinished()` 在兼容模式下仍返回当前期望值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是测试兼容性而不是运行时正确性；仓库里已有测试和工具可能把 `IsInitialCompileFinished()` 当作“testing engine 已可用”的快捷判断，因此第一阶段必须让新旧状态并存 |
| 兼容性 | 向后兼容。legacy bool 与旧 helper 保留；结构化状态先服务新扩展点和新测试；等测试环境完成迁移后，再决定是否弱化 `bIsInitialCompileFinished` 的地位 |
| 验证方式 | 1. 增加 full/test/clone 三路状态断言，验证 `LifecycleState` 能稳定区分 `Bootstrapped`、`ScriptsCompiled`、`RuntimeReady`。<br>2. 增加 clone 回归，确认共享源 engine 的 clone 会继承源 runtime 的 compiled/ready 状态，而不是落回默认 false。<br>3. 回归现有 testing helper 与启动测试，确认保留 legacy facade 后老测试不被立刻打断。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-50 | `OnInitialCompileFinished` 早于 `SharedState` 提交 | ready milestone / runtime-ready event | 高 |
| P1 | Arch-SL-51 | full/test/clone 路径的 lifecycle state 语义失真 | 结构化 lifecycle state | 高 |

---

## 架构分析 (2026-04-09 00:44)

### Arch-SL-52：热更与换入缺少稳定的 module-level lifecycle event，外部扩展只能订阅 compile-global 或 type-level 信号

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块换入、热更提交和失败回滚能否向外部暴露稳定的 module-level lifecycle event，供 cache、tooling、debugger、HMR 扩展消费 |
| 当前设计 | 当前对外可订阅的生命周期信号主要是 compile-global 的 `PreCompile/PostCompile/OnInitialCompileFinished`，以及 class/type 级 `OnClassReload/OnStructReload/OnDelegateReload/OnPostReload`；没有携带稳定 `ModuleKey`、prepare/commit/fail 语义的模块事件 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:14-18,37-42` — runtime module 公开的 delegate 只有无参 `FAngelscriptCompilationDelegate` 和编译后 `CompiledModules` 集合，没有模块级 prepare/commit/fail event；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12-19,31-38` — class generator 暴露的是 class/struct/delegate/full-reload 事件，粒度停在 type 或整次 reload；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2911-2939` — 模块换入时直接重命名旧/新 `ScriptModule` 并写入 `ActiveModules`，中间没有外部可观察的模块提交事件；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4136-4187` — compile 结束后只有 `PostCompile.Broadcast()` 与失败文件回队列更新，没有“哪个模块提交成功/失败”的结构化通知；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2348-2395,2469,3932` — reload 过程中广播的是 type reload 与 `OnPostReload(bool)`，而不是按逻辑模块发出 lifecycle transition |
| 优点 | 当前事件面很窄，调用顺序相对稳定；type-level delegate 对类重载场景已经足够直接，维护成本低 |
| 不足 | 外部 cache、runtime entry、IDE/HMR 工具无法在“某个模块即将被替换 / 已成功提交 / 本轮失败回滚”这些关键时点精确清理或迁移状态；结果是扩展只能订阅 compile-global 信号，自己重新猜测受影响模块集合 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | hot reload 在真正替换脚本源码前后显式发出 `HMR.prepare` / `HMR.finish`，事件参数直接携带 `moduleName`、当前 module object 和 `url`；`moduleCache` 命中/失败回滚也围绕单个 cache entry 发生 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:80-90`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-146,188-190,205-225` | 先把“模块生命周期事件”做成 public contract，再让 HMR、cache 和调试侧围绕它协同，而不是让外部重建内部状态机 |
| UnLua | `HotReload.lua` 维护显式 `hook.module_loaded`，初次加载与 reload 成功后都会按 `module_name` 调用 `call_hook("module_loaded", new_module, module_name, is_reload)`；模块缓存与 hook 都围绕逻辑模块名工作 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:20-21,151-170,553-592` | 即使底层 reload 细节复杂，外部观察者仍然只需要订阅稳定的模块事件与逻辑模块 key，不必感知内部 sandbox/替换步骤 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 compile-global 与 type-level delegate 的前提下，补一层 module-level lifecycle event，显式暴露 `Prepare/Committed/Failed/RolledBack` 四类时点 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h/.cpp` 新增 `FAngelscriptModuleLifecycleEvent` 与 `FAngelscriptModuleLifecycleDelegate`，字段至少包含 `ModuleKey`、`CompileType`、`Phase`、`bIsReload`、`OldBackendName`、`NewBackendName`、`AffectedFiles`、`Result`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2911-2939` 的换入逻辑前后分别发出 `PrepareSwap` 与 `Committed`；在 `4136-4187` 的失败/回队列路径补发 `Failed` 或 `RolledBack`，让 observer 不必再从文件队列反推模块状态。<br>3. 第一阶段不替换 `PreCompile/PostCompile/OnInitialCompileFinished`，而是把它们保留为 legacy coarse-grained signal；新的 module event 只做增量补充。<br>4. 将 `FAngelscriptClassGenerator::OnClassReload/OnStructReload/OnDelegateReload` 视为 module event 之后的 type-level 子事件，文档中明确顺序关系，避免外部在 type reload 前后各自猜测模块是否已提交。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：soft reload 成功时 observer 必须收到 `PrepareSwap -> Committed`；reload 失败时必须收到 `Failed/RolledBack` 且无假阳性 `Committed`；仅 class-level 变更时 module event 与 type event 的顺序稳定可断言。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 主要风险是事件时序与现有 type reload / `PostInitFunctions` / `OnPostReload` 的相对顺序；如果没有固定 contract，外部仍会继续订阅多个信号并引入竞态 |
| 兼容性 | 向后兼容。旧 delegate 与旧脚本行为保持不变；新事件只是额外暴露结构化信息，不要求现有项目改脚本语法或热更流程 |
| 验证方式 | 1. 新增 observer 测试，记录单模块 hot reload 的 `Prepare/Committed/Failed` 顺序。<br>2. 构造 parse 失败和 `FullReloadRequired` 两类回滚场景，验证只会发出失败侧事件，不会误报 commit。<br>3. 回归现有 type reload 与 test discovery 测试，确认新增 module event 不改变旧 delegate 的触发次数和顺序。 |

### Arch-SL-53：当前只有 live `ActiveModules` 与 destructive `DiscardModule()` 两态，缺少独立的 module residency/cache layer

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 按需加载落地后，模块是否可以在“已编译但未激活”“冷缓存”“完全驱逐”之间渐进转移，而不是只能常驻或彻底丢弃 |
| 当前设计 | 当前 runtime 对模块只有两种现实状态：一旦换入就进入 `ActiveModules` 全局表并参与所有后续扫描；若要移除则只能调用 `DiscardModule()` 做破坏性卸载，直接清空 script type、diagnostics 和 file watcher 账本 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:311-326` — 对外查询面只有 `GetModule()` 与 `GetActiveModules()`，没有 `WarmModules/ColdModules/Residency` 之类的中间层；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2911-2939` — 模块一旦编译提交就直接写入 `ActiveModules`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2253-2313` — hot reload 构建依赖闭包时遍历整个 `ActiveModules` 视图，没有“仅扫描 resident 子集”的层次；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2489-2496` — post-compile test discovery 同样直接消费 `GetActiveModules()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1026-1127` — `DiscardModule()` 会同步清掉 `ModulesByScriptModule`、`ActiveClassesByName`、`ActiveEnumsByName`、`ActiveDelegatesByName`、diagnostics、failed/full-reload 队列和文件变化记录，说明当前“移除模块”只有 hard-evict 语义 |
| 优点 | live 集合单一，运行期一致性强；热更、类型索引和 diagnostics 都围绕同一个 `ActiveModules` 工作，逻辑简单 |
| 不足 | 即使未来补上 lazy compile，模块一旦被首次激活后仍会永久停留在 live 集合；系统没有 `Pinned/Warm/Cold/Evicted` 之类的 residency 语义，也没有 access/refcount 数据，按需加载很难真正转化为更小的驻留面和更轻的后续扫描面 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `modular.js` 把执行结果放进显式 `moduleCache` / `localModuleCache`，并提供 `forceReload()`、`getModuleByUrl()`；也就是说，模块缓存本身就是一层独立数据结构，而不是直接等同于“当前所有 live 类型/对象” | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-71,139-146,205-225` | 先把 cache/residency 面建出来，后续 reload、lookup、debug 查询都围绕 cache entry，而不是反向操作 VM 内部 live 集合 |
| UnLua | `HotReload.lua` 维护弱引用 `loaded_modules` 与强引用 `package.loaded` 两层缓存；reload 时先把旧模块装进 `tmp_modules`，成功后再提交 `new_modules`，并通过 `module_loaded` hook 通知外部 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:13-21,151-170,553-592` | 模块缓存、提交时机和观察者 hook 是独立层；这使模块可以拥有“已加载缓存”“当前激活实例”“等待替换的新实例”等不同状态，而不是只剩 live/discard 两态 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `ActiveModules` 之下增加 first-class residency store，把“可驻留的模块记录”与“当前 live script module”解耦；第一阶段默认全部 `Pinned`，只补状态层，不改旧行为 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleResidencyRecord` 与 `EAngelscriptResidencyState(Pinned/Warm/Cold/Evicted)`，字段至少包含 `ModuleKey`、`LastAccessFrame`、`RefCount`、`bCanEvict`、`CompiledArtifactRef`、`LiveModuleRef`、`LastDiagnosticsHash`。<br>2. 让 `ActiveModules` 退化成 residency store 上的“当前 live 投影”，`GetModule()` / `GetActiveModules()` 先继续读取 live 投影，保证外部 API 不变；新的 lazy loader、hot reload planner 和 module cache 先改读 residency store。<br>3. 新增 `EnsureModuleResident(ModuleKey)`、`MarkModuleWarm(ModuleKey)`、`ReleaseModuleResidency(ModuleKey)`；默认 `InitialCompile()` 和现有 eager 路径仍把所有模块标成 `Pinned`，旧项目零行为变化。<br>4. 把 `DiscardModule()` 保留为最终 hard-evict API，但新增更温和的 `DemoteModuleToCold()`：第一阶段仅对没有 `UASClass/UASStruct/Subsystem` 依赖、且没有未完成 runtime entry 的纯脚本工具模块开放，避免一上来就触碰不安全的 UObject 类型卸载。<br>5. 让未来的 `EnsureModuleCompiled()`、artifact provider 与 hot reload journal 先操作 residency record，再决定是否真的创建/丢弃 live `ScriptModule`；这样 lazy compile、warmup 和 targeted reload 才能共享同一模块缓存面。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：默认 eager 模式下所有模块仍为 `Pinned`；opt-in 的 utility module 降为 `Cold` 后不再出现在 live 投影里，但其 artifact/diagnostics 仍保留；再次访问该模块时 `EnsureModuleResident()` 能把它重新提升到 live 态而不影响其他 resident 模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 最大风险是 UObject-backed script type 的安全降温/驱逐；如果在没有 state handoff 和 type safety guard 的前提下让这类模块进入 `Cold/Evicted`，很容易留下悬挂 `UASClass/UASFunction` 指针 |
| 兼容性 | 向后兼容。第一阶段默认所有现有模块都继续 `Pinned`，`ActiveModules` 的外部表现不变；只有显式 opt-in 的 utility/lazy module 才会进入新的 residency 状态机 |
| 验证方式 | 1. 回归现有 initial compile、hot reload、PIE 与 test discovery，确认默认 `Pinned` 模式下行为完全不变。<br>2. 新增 cold-residency 测试，验证 `DemoteModuleToCold()` 不会清空 artifact 与 diagnostics，但会把模块从 live 投影移除。<br>3. 构造 utility module 的首次访问/再次访问场景，验证 `EnsureModuleResident()` 能恢复 live module 且不重复污染无关模块的 reload/test 队列。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-53 | 模块常驻/冷缓存/驱逐之间没有 residency layer | residency store / lazy-load cache 面补强 | 高 |
| P2 | Arch-SL-52 | 缺少 module-level lifecycle event | lifecycle event contract 新增 | 中 |

---

## 架构分析 (2026-04-09 00:56)

### Arch-SL-54：纯脚本模块没有轻量 commit lane，所有成功编译的模块都会被送进 `ClassGenerator` reload 主链

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 纯脚本 utility module、分析模块、lazy-loaded helper 是否能跳过 `UObject`/CDO reload 重路径 |
| 当前设计 | 只要模块有 `ScriptModule`，成功编译后就会统一进入 `ClassGenerator`，没有基于“是否真的声明 reflected type”的 commit 分流 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1293-1306` — `FAngelscriptModuleDesc` 已把 `Classes`、`Enums`、`Delegates`、`PostInitFunctions` 分开建模；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3896-3915` — compile 成功后无条件创建 `FAngelscriptClassGenerator`，并把所有 `ScriptModule != nullptr` 的 `CompiledModules` 送入 `AddModule()` / `Setup()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:85-96` — `AddModule()` 只记录模块和索引，没有 `CodeOnly/TypeBearing` 之类的分类；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2266-2304` — reload 尾部统一执行 `DoSoftReload/DoFullReload`、随后 `CallPostInitFunctions()` 和 `InitDefaultObjects()`，说明当前 commit 主链天然以 class reload 为中心 |
| 优点 | 单一路径保证了 today 的 reload 时序稳定，class/default object/subsystem 的副作用不容易漏掉 |
| 不足 | 没有 reflected type 的工具模块、纯函数模块、延迟加载 helper 也要穿过 `ClassGenerator` 和 property/default object 路径；future `lazy load`、`analyze-only`、warmup compile 即使不需要 `UObject` reload，也拿不到真正轻量的提交通道 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | runtime 模块请求主链就是 `search -> load -> execute -> cache`；`Start()` 也只是调用入口 `Require`，没有把普通 JS 模块送进 UE 类型重建路径 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-191`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551` | 先把“代码模块激活”做成轻量主链，再把 Blueprint/Editor 工具放到独立层，不让每个模块请求都经过重型 UObject 流程 |
| UnLua | `FLuaEnv::Start()` 只 `require` 启动模块，`LoadFromFileSystem()` 只按 `package.path` 找 chunk；对象到模块的映射由 `ULuaModuleLocator::Locate()` 在需要时单独处理 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-249`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:597-642`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp:18-65` | 模块装载与 UObject 绑定分层后，纯脚本模块不会自动承担 class-generation 成本 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 reload 主链旁边增加 `CodeOnly` 快速提交通道，只让真正声明 reflected type 的模块进入 `ClassGenerator` |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 为模块新增 `EAngelscriptModuleCommitKind(CodeOnly/TypeBearing)` 或等价 flags，第一阶段仅根据 `Classes/Enums/Delegates` 是否为空做保守判定。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 的 `CompileModules()` 成功路径把 `CompiledModules` 分成 `CodeOnlyModules` 与 `TypeBearingModules`；只有后者继续 `ClassGenerator.AddModule()`、`Setup()`、`VerifyPropertySpecifiers()`。<br>3. 新增 `CommitCodeOnlyModules()`：负责 `SwapInModules()`、declared import rebind、runtime entry/boot 调度，但跳过 `InitDefaultObjects()`、property 校验和 subsystem/reactivation。<br>4. 第一阶段把 fast lane 限定为“无 reflected type 且无 `ClassGenerator` 依赖”的模块；对存在 `PostInitFunctions` 的纯脚本模块，先走轻量 boot，而不是回退到整条 class reload 链。<br>5. 提供 editor-only fallback 开关，例如 `bForceLegacyClassGeneratorPath`，在出现边界问题时允许项目回到今天的单一路径。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：纯脚本模块 compile/commit 不进入 `ClassGenerator`；包含 `UCLASS/USTRUCT` 的模块仍保持 today 路径；混合批次中 `CodeOnly` 与 `TypeBearing` 模块可在同一事务里共存且顺序稳定。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是误判“看似 code-only，实际通过 boot/global side effect 依赖 class reload 时序”的模块；因此第一阶段必须走保守分类，并保留 fallback 到旧路径的能力 |
| 兼容性 | 向后兼容。即便新增 `CodeOnly` 分类，默认仍可先让所有模块继续走 legacy path；项目可按模块或按 setting 逐步启用 fast lane |
| 验证方式 | 1. 回归现有 initial compile、soft/full reload 和 `PostInitFunctions` 测试，确认 legacy path 行为不变。<br>2. 新增纯脚本模块 fast-lane 测试，验证其不会触发 `ClassGenerator.Setup()` / `InitDefaultObjects()`。<br>3. 对比启用/禁用 fast lane 的 compile 时长和模块提交顺序，确认收益与行为边界可观测。 |

### Arch-SL-55：测试发现仍嵌在 compile/commit 主链里，而且 discovery 阶段会实际执行脚本

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译-加载-执行主链是否被 editor/testing tooling 副作用污染，导致 `analyze-only`、lazy load 和轻量 commit 无法复用同一条核心管线 |
| 当前设计 | test discovery、hot reload test prepare 和 compile diagnostics 注入都直接发生在 runtime core 的 compile 成功路径里；复杂测试发现还会在 discovery 阶段真实执行脚本函数 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2232-2249` — `DiscoverTests()` 在 asset scan 完成后直接遍历 `GetActiveModules()` 发现测试；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4136-4153` — `CompileModules()` 成功后立即对 `CompiledModules` 调 `DiscoverUnitTests()` / `DiscoverIntegrationTests()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2481-2496` — hot reload 结束后还会在 core 内直接调 `HotReloadTestRunner->PrepareTests()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp:90-103` — `ComplexTestScriptCompileError()` 把 discovery 问题直接写入 compile diagnostics；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp:125-145` — 复杂测试 discovery 会 `PrepareAngelscriptContextWithLog(...)` 并 `Execute()` `*_GetTests` 脚本函数，说明“发现测试”不是纯元数据扫描，而是执行期行为 |
| 优点 | editor 里测试列表能自动跟随编译结果刷新，复杂测试参数也能第一时间进入自动化目录 |
| 不足 | compile pipeline 失去纯度：即便只是 compile/commit，也会夹带 test enumeration 和脚本执行；`analyze-only`、lazy activation、code-only fast lane 若复用主链，就会被迫继承测试副作用；discovery 失败还会混进 compile diagnostics，模糊“脚本编译错误”和“测试工具错误”的边界 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | runtime `FJsEnvImpl::Start()` 只负责 `Require` 入口模块；editor 分析工具单独放在 `PuertsEditor` 模块，并由 `PuertsEditorModule::OnPostEngineInit()` 启动 `PuertsEditor/CodeAnalyze` | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551`；`Reference/puerts/unreal/Puerts/Puerts.uplugin:14-47`；`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116-150` | runtime 模块加载与 editor/tooling 分层，分析工具可以自动运行，但不应嵌进核心模块请求/提交事务 |
| UnLua | runtime `FLuaEnv` 的 `Start()` / `LoadFromFileSystem()` 只负责 `require` 与 chunk load；测试能力放在独立 `UnLuaTestSuite` 插件，而不是塞进 `LuaEnv` 主链 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-249`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:597-642`；`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:1-30` | 把 test/tooling 做成独立消费方，runtime lifecycle 只暴露必要事件和 module results，不直接承担测试编排 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 test discovery 从 `FAngelscriptEngine` compile 主链中抽离成 editor/testing observer；runtime core 只产出 module commit 事件和结构化 discovery 输入 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 引入 `FAngelscriptModuleCommitReport` 或复用 future `module lifecycle event`，把 `CompiledModules`、asset-scan readiness、compile outcome 暴露给外部观察者。<br>2. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2232-2249,2481-2496,4136-4153` 中的 `Discover*Tests()` / `HotReloadTestRunner->PrepareTests()` 迁出 core，改由 `AngelscriptTest` 或 editor-only service 订阅 commit/report 后执行。<br>3. 将 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp` 改造成返回 `FAngelscriptTestDiscoveryReport`，不再直接调用 `ScriptCompileError()`；由 discovery service 决定是否把问题映射成 editor diagnostics、automation warning 或 test list update failure。<br>4. 把 `*_GetTests` 的脚本执行限定到显式 discovery phase；`AnalyzeOnly`、headless warmup、lazy compile 和 code-only fast lane 默认不自动触发 discovery。<br>5. 让 `HotReloadTestRunner` 也挂到同一 service，消费 committed modules 与 changed-file list，而不是继续由 `FAngelscriptEngine` 直接调用。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：关闭 auto discovery 时 compile 成功不得执行任何测试 discovery 脚本；editor 默认服务开启时行为与 today 一致；复杂测试 `_GetTests` 执行失败时能进入 discovery report，但不再污染 core compile transaction。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/HotReloadTestRunner.*`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是 editor 用户已经习惯“编译后立刻刷新测试目录”；如果迁移时没有提供默认 observer，体验会被误判成 regression |
| 兼容性 | 向后兼容。默认 editor 配置仍可注册一个 auto-discovery observer，维持今天的自动刷新体验；仅 `AnalyzeOnly`、headless、lazy compile 或显式关闭 observer 的场景才会看到更纯的 compile 管线 |
| 验证方式 | 1. 回归 editor 下自动 test discovery 行为，确认默认 observer 保持 today 体验。<br>2. 新增 `AnalyzeOnly` / lazy compile 测试，确认 compile 成功后不会执行 `*_GetTests` 或 `PrepareTests()`。<br>3. 构造复杂测试 discovery 失败场景，验证错误进入 discovery report，而不是直接污染 core compile diagnostics。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-54 | 纯脚本模块缺少轻量 commit lane | code-only fast path / class-free commit 分层 | 高 |
| P1 | Arch-SL-55 | test discovery 耦合 compile 主链且会执行脚本 | tooling observer 解耦 | 高 |

---

## 架构分析 (2026-04-09 01:08)

### Arch-SL-56：模块启动已有显式副作用，但换入/关闭路径仍缺少对称的 `deactivation` 合约

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块从“已编译并激活”转回“被替换/被卸载/引擎关闭”时，是否存在可扩展、可观测、可由脚本参与的 teardown 阶段 |
| 当前设计 | 当前 runtime 只有启动侧的 `PostInitFunctions`，但旧模块被替换或引擎关闭时，实际执行的是 destructive discard：直接丢弃 backend module、清空类型/诊断/队列状态，没有模块级 teardown 回调或脚本侧停用入口 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2303,5775-5805` — reload 尾部会执行 `CallPostInitFunctions()`，逐个查找并 `Execute()` `PostInitFunctions`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1026-1129` — `DiscardModule()` 只释放空闲 context、调用 `Engine->DiscardModule()`，随后清空 `ModulesByScriptModule`、`ActiveClassesByName`、`ActiveEnumsByName`、`ActiveDelegatesByName` 及 diagnostics/file queue；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1132-1249` — `Shutdown()` 释放 context pool / shared state 后直接 `ActiveModules.Empty()`、`ModulesByScriptModule.Empty()`，没有任何按模块的 shutdown/cleanup 调用；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2373-2469` — 对外广播的是 engine-level `OnFullReload` / `OnPostReload` 与 subsystem 重新激活，不是“某个模块正在退场”的 first-class 事件 |
| 优点 | 当前退出路径直接、确定性强；模块替换失败时也较容易维持“要么旧代码继续可用，要么整个引擎清空”的粗粒度语义 |
| 不足 | 启动副作用与退出副作用不对称：模块若在启动时注册 delegate、缓存句柄、外部服务状态或脚本单例，没有官方位置做反注册/冲洗；结果是扩展只能依赖全局 reload 事件或宿主特判，难以把 teardown 绑定到逻辑模块生命周期 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | hot reload 在真正替换脚本前后显式发出 `HMR.prepare` / `HMR.finish`，而且事件参数直接携带 `moduleName`、当前模块对象和 `url` | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:80-90` | 即使底层仍由宿主完成实际替换，也先把“模块即将退场/已经换完”的窗口公开出来，给缓存、工具和业务清理逻辑一个稳定插点 |
| UnLua | `reload_modules()` 先在 sandbox 中构造 `new_module`，成功后由 `update_modules()` 把函数/表内容合并回 `old_module`，同时通过 `call_hook("module_loaded", ..., is_reload)` 通知外部；旧模块表身份被保留，而不是直接 destructive discard | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601` | 参考点不是“必须做 in-place merge”，而是 reload/teardown 语义要围绕逻辑模块对象展开，而不是只围绕 backend module handle 的生杀予夺 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `PostInitFunctions` 之外补一条对称的 `deactivation` 路径，把“旧模块退场”从内部清指针动作升级为 first-class lifecycle 阶段 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h/.cpp` 新增 `FAngelscriptModuleDeactivationEvent` 与 `PrepareDeactivate/Deactivated` delegate，字段至少包含 `ModuleKey`、`GenerationId`、`Reason(Reload/Discard/Shutdown)`、`AffectedFiles`、`bWillBeReplaced`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 为模块 generation 或 residency record 增加可选 `TeardownEntries` / `DeactivateObservers`；第一阶段允许它为空，保持旧模块零改动。<br>3. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4015-4025,1026-1129,1132-1249` 中旧模块 discard 与 engine shutdown 前的清理步骤前移到统一 `DeactivateModules(...)`，先发 `PrepareDeactivate`、执行 teardown，再做 today 的 `Engine->DiscardModule()` 与索引清空。<br>4. 第一阶段不要要求脚本新增语法；兼容实现可先复用 future manifest/sidecar 元数据或 C++ observer 注册，把 teardown 放在宿主可控边界上。<br>5. 对 teardown 失败采用“模块进入 `RetiredPendingCleanup` 并输出结构化 diagnostics”的保守策略，而不是在半清理状态下继续 destructive discard；真正删除 backend handle 放到 teardown 成功之后。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：模块启动时注册的外部句柄在 reload/shutdown 时能被对称释放；默认无 teardown 配置时行为与今天一致；teardown 报错时旧 generation 会进入可诊断的 pending-cleanup 状态，而不是静默丢失上下文。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险不在“发一个事件”本身，而在清理时序：若 `PrepareDeactivate` 发生得太晚，旧模块的 `UASClass/UASFunction` 指针可能已被清空；若发生得太早，又会与当前 `CallPostInitFunctions()`/subsystem 重新激活顺序冲突 |
| 兼容性 | 向后兼容。默认不声明 teardown 时，模块仍沿用 today 的 destructive discard；新 deactivation 合约只为需要对称清理的模块和工具链显式 opt-in |
| 验证方式 | 1. 回归现有 soft/full reload、engine shutdown 与 script lifecycle 测试，确认默认路径不变。<br>2. 增加“startup 注册 / reload 释放 / 再次启动重建”的回归，验证生命周期对称。<br>3. 构造 teardown 失败场景，验证系统输出结构化 deactivation diagnostics，且不会把失败模块伪装成已完全清理。 |

### Arch-SL-57：模块换入/丢弃默认假设 runtime 已静默，缺少 `in-flight execution` 的 quiescence barrier

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热更提交与旧模块 discard 时，系统能否感知并等待正在执行的脚本帧、嵌套调用或模块相关 context，而不是默认“旧模块现在就可以被丢弃” |
| 当前设计 | 当前 discard 只回收空闲 context pool，再立即 `DiscardModule/DeleteDiscardedModules`；与此同时，执行层允许复用 active context 做嵌套 `PushState()`，而 hot reload 又由 `Tick()` 在常规帧循环里触发。推断：模块换入路径缺少显式 quiescence barrier，默认把“没有空闲 context 残留”等同于“运行态已经安全退场” |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:195-216` — `ReleaseContextsForScriptEngine()` 只处理池中的 context，并且显式 `check` 不接受 `asEXECUTION_ACTIVE/asEXECUTION_SUSPENDED`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1026-1036` — `DiscardModule()` 在真正 `Engine->DiscardModule()` 前仅回收 thread-local/global free pool；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1795-1808` — 执行层若发现当前 thread 已有 active context，会直接 `PushState()` 进行嵌套执行；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2794-2829` — `Tick()` 在常规运行帧内即可发起 `CheckForHotReload(SoftReloadOnly/FullReload)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4010-4025` — 一旦允许 swap-in，旧模块会在同一事务尾部立刻 `Engine->DiscardModule()` 并 `DeleteDiscardedModules()` |
| 优点 | 提交路径短，不需要长期维护“旧 generation 仍在服务中的宽限期”；对当前 editor 驱动的热更流程实现成本低 |
| 不足 | 当前可观察到的安全条件只有“free pool 已清”，而不是“所有与该模块相关的执行都已退出”；这会把 future lazy activation、module deactivation、record/replay、长生命周期脚本任务和更细粒度 HMR 全部压回“只能整批回滚或整批等待”的粗模型 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 替换源码前后显式发出 `HMR.prepare` / `HMR.finish`，事件里直接给出当前模块对象；至少为“先冻结/清理，再替换”提供了稳定窗口 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:80-90` | 即使不立即实现完整 lease/refcount，也应先让模块退场前存在可观测的 quiesce 阶段，而不是直接把 discard 埋在 commit 尾部 |
| UnLua | `reload_modules()` 先 `sandbox.enter(tmp_modules)`，成功后由 `update_modules(old_modules, new_modules, ...)` 把新函数/值合并回旧模块表，再 `sandbox.exit()`；模块表身份与大部分运行中引用因此继续可用 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549` | 参考点不一定是“照抄 Lua table merge”，而是 reload 提交前应有 request-local staging 区，并且旧运行态要么被保留、要么被显式等待，而不是无条件立即销毁 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为模块 generation 增加 `execution lease + deferred retire`，把“能否真正 discard 旧模块”从隐式假设改成可检查、可延期的 runtime 条件 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleExecutionLease`、`FAngelscriptRetireBarrier` 与 `ERetireState(Ready/WaitingForInFlight/Retired)`；lease 至少记录 `ModuleKey`、`GenerationId`、`CallDepth`、`StartTime`。<br>2. 让统一执行入口（包括 future executor facade 与当前 pooled context 兼容层）在 `Prepare/Execute` 前后登记 lease；`PushState()` 路径必须继承或叠加同一 generation 的 lease，而不是继续完全隐形。<br>3. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4010-4025` 的“立即 discard old module”改成两段：先 `MarkGenerationRetiring()`，只有当 barrier 观测到无 in-flight lease 时才真正 `Engine->DiscardModule()/DeleteDiscardedModules()`；否则把旧 generation 放入 deferred-retire 队列。<br>4. 将 `Tick()` 或 future scheduler 扩展为 `DrainDeferredRetire()`，允许在安全窗口统一收割 old generation；对 editor/dev 模式可提供 timeout diagnostics，指出是哪个模块长期未 quiesce。<br>5. 第一阶段不改变 today 的默认热更频率；若模块没有 active lease，行为仍与今天一样立即 discard。只有检测到 in-flight execution 时才进入延期退场分支。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：模块正在执行时触发 reload，不会立即 discard 旧 generation；嵌套 `PushState()` 调用会正确延长同一 generation lease；执行退出后 deferred-retire 队列能在后续 tick 中安全回收旧模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 最大风险是 lease 归属不完整：如果部分执行路径仍绕过登记，系统会得到“看似安全可退场、实际仍有旧栈在跑”的假阴性；第一阶段必须先收敛所有进入脚本 VM 的公共入口，再谈更细粒度 module-level retire |
| 兼容性 | 向后兼容。没有 in-flight lease 的场景仍按 today 的即时 discard 运行；deferred-retire 只在检测到活动执行时介入，不要求现有脚本修改语法或启动配置 |
| 验证方式 | 1. 新增“执行中热更”回归，验证旧模块进入 `WaitingForInFlight` 而不是被立即 `DiscardModule()`。<br>2. 新增嵌套执行测试，验证 `PushState()` 路径也会保持 lease。<br>3. 回归现有 hot reload 与 engine shutdown 测试，确认无 in-flight 执行时行为、性能和时序与 today 保持一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-56 | 模块退场/关闭缺少对称的 deactivation contract | module deactivation / teardown lifecycle | 高 |
| P1 | Arch-SL-57 | 热更 discard 缺少 in-flight execution quiescence barrier | execution lease / deferred retire | 高 |

---

## 架构分析 (2026-04-09 01:22)

### Arch-SL-58：并行编译阶段直接写入 request-global / engine-global 状态，缺少 worker-local artifact 与串行 merge 边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译管线在引入 `lint`、优化 pass、增量分析或更细粒度并行阶段时，是否存在稳定的 worker-local artifact / merge contract |
| 当前设计 | 当前 `CompileModules()` 先开启一次 engine 级 build session，然后在 `ParallelFor` 中直接让每个 `ScriptModule->builder` 解析源码；worker 与后续阶段共同读写 `bHadCompileErrors`、`Module->bCompileError`、`CompiledModules`、`ModulesToUpdateReferences`、`ScriptUpdateMap` 等共享状态 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3090-3093` — `RequestBuild()` / `PrepareEngine()` 把一次编译请求建模成单个 engine-global build session；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3212-3238` — `ParallelFor` 直接调用 `ScriptModule->builder->BuildParallelParseScripts()`，并在 worker 中写 `Module->bCompileError` 与 `bHadCompileErrors`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3288-3758` — 同一次请求随后继续在共享 `CompiledModules` 上构造 `ScriptUpdateMap`、链接 `ReloadOldModule/ReloadNewModule`、做 reference replacement 与 layout；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4066-4111` — 失败回滚也直接反向修改 live module/ref map，而不是回滚独立 artifact |
| 优点 | 现有实现复用了 AngelScript builder 的内部阶段，额外拷贝少，热更成功路径短；对当前固定 `Stage1-4` 流程来说实现成本低 |
| 不足 | 扩展方没有“这个阶段的输入/输出到底是什么”的稳定对象，只能依赖 live `asCModule`、builder 私有状态与共享 map；一旦要插入并行 `lint`、IR 级优化、分析缓存或不同线程亲和的 pass，就会被迫碰 request-global 可变状态，难以做到可组合、可回放、可测试 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require()` 先创建局部 `m` 与 `localModuleCache`，查找/装载/执行都围绕这个 request-local 暂存对象进行；脚本执行失败时会把对应 cache entry 清掉，避免半提交状态泄漏到全局 `moduleCache` | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-146`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:156-195` | 先有 request-local staging object，再决定是否把结果晋升到全局 cache；扩展点可以围绕 staging/commit 两段建模 |
| UnLua | 热更时先把旧模块收集到 `tmp_modules`，在 sandbox 内分别生成 `old_modules/new_modules/module_envs`，只有全部成功后才进入 `update_modules()` 合并回 live 模块表 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:480-549` | 核心不是照搬 Lua table merge，而是把“生成新结果”和“提交到 live runtime”拆成两段，给工具、诊断和回滚留出稳定边界 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `Stage1-4` 顺序和 AngelScript builder 兼容层的前提下，引入 `compile artifact + merge barrier`，先让 worker 产出 request-local 结果，再由主线程统一合并与 commit |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptCompileArtifact`、`FAngelscriptParseArtifact`、`FAngelscriptMergeArtifact`，至少承载 `ModuleKey`、声明摘要、诊断批次、依赖 hash、是否可继续后续阶段、候选 `ScriptModule` 句柄。<br>2. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3212-3238` 的 worker 路径改成只返回 `ParseArtifact`，不再在 worker 内直接写 `bHadCompileErrors` 这类 request-global 变量；主线程汇总 artifact 后再决定请求级结果。<br>3. 将 `ScriptUpdateMap`、`ModulesToUpdateReferences`、`ReloadOldModule/ReloadNewModule` 的构造集中到新的 `MergeArtifacts()`，保证 reference replacement、reload diff 与 rollback 都基于同一份显式 merge 输入。<br>4. 为未来扩展预留 `IAngelscriptCompilePass::Run(Artifact, Context)`，第一阶段只把现有 `BuildGenerateTypes/Functions/CompileCode` 包成内建 pass；自定义 `lint`/optimizer pass 只读 artifact，不直接碰 live `ActiveModules`。<br>5. 保留旧的 `CompileModules()` 对外签名，但内部先走 `CreateCompileRequest -> RunWorkerPasses -> MergeArtifacts -> CommitArtifacts`；默认配置下行为与 today 一致。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：worker parse 失败时错误只体现在对应 artifact，不污染同批独立模块；插入一个只读 `lint pass` 能消费 artifact 而不修改 live state；merge 失败时不会产生半提交的 `ReloadOldModule/ReloadNewModule` 残留。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 风险在于 AngelScript builder 目前天然偏向“边解析边写 live module”；如果第一阶段 artifact 设计得太薄，最后仍会把不可见的 builder 内部副作用漏回主链，变成“看似分层、实际还是共享状态” |
| 兼容性 | 向后兼容。默认 `CompileModules()` 的执行顺序、热更结果和对外 delegate 都保持不变；artifact/pass 只是内部新层，旧项目和旧测试不需要改脚本语法 |
| 验证方式 | 1. 回归现有 initial compile / soft reload / full reload 测试，确认默认路径行为不变。<br>2. 新增一个只读 `lint pass` 测试，验证它能读取 artifact 并发出诊断，但不会修改 live `ActiveModules`。<br>3. 构造 merge 失败场景，验证系统不会留下半链接的 `ReloadOldModule/ReloadNewModule` 或脏 `ScriptUpdateMap`。 |

### Arch-SL-59：脚本来源只有 project/plugin 静态 root，缺少 `source overlay` 策略，无法承载下载补丁与来源优先级

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块加载来源是否支持“下载补丁覆盖打包脚本”“生成模块覆盖磁盘脚本”“多来源分层优先级”这类 source overlay 能力 |
| 当前设计 | 当前脚本来源被硬编码成 project `Script/` 与启用插件的 script roots；初始化后对所有 root 做全量递归扫描，再把发现的 `*.as` 文件按物理路径送入预处理器，预处理器本身也只接受 `relative/absolute filename` 对 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1363` — `DiscoverScriptRoots()` 只发现 project root 与 enabled plugin script roots，并把 project root 固定插到优先级最前；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1999-2015` — `FindAllScriptFilenames()` 仅递归扫描 `AllRootPaths` 下的 `*.as` 文件；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2061-2079` — `InitialCompile()` 把全量扫描结果逐个 `Preprocessor.AddFile(...)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:14-21` — 预处理入口仍只有 `AddFile(relative, absolute)` 与 `GetModulesToCompile()`，没有来源层级、overlay 优先级或来源类型字段 |
| 优点 | 行为简单且可预测，project 覆盖 plugin 的规则固定；对当前全量编译与文件监控实现来说，根目录扫描模型足够直接 |
| 不足 | 一旦需要 runtime hotfix、下载目录覆盖、generated script layer 或受控灰度发布，当前架构只能继续把这些来源伪装成“新的磁盘 root”或提前改写文件系统；模块系统本身不知道“这是补丁层还是基础层”，也无法在回退、诊断和缓存 key 上保留来源语义 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv` 启动时先注册 `LoadFromCustomLoader`、再注册 `LoadFromFileSystem`；文件系统 loader 又先查 `ProjectPersistentDownloadDir()`，再查 `ProjectDir()`，把下载补丁覆盖正式内容的优先级显式写进 loader 链 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-100`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-595`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:614-639` | 来源优先级应是 loader policy，而不是全量扫盘后的副作用；这样热修复、内存 loader 与正式内容可以并存 |
| puerts | `DefaultJSModuleLoader::Search()` 先按 `requiringDir` 与父目录链搜索，再回退到 project content script root 与默认 `JavaScript` root，搜索顺序本身就是显式模块来源层级 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-90`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:92-120` | 模块来源不必只有一组启动快照 root；把“先搜哪里、再回退到哪里”建成 resolver policy，后续新增 patch/generated 层才不会破坏主链 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持当前 project-first/plugin-second 默认行为的前提下，引入 `source layer / overlay profile`，把来源优先级、patch 覆盖和 generated layer 显式建模 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleSourceLayer` 与 `IAngelscriptSourceResolver`，字段至少包含 `LayerId`、`Priority`、`SourceKind(Project/Plugin/PersistentPatch/Generated)`、`AbsoluteRoot`、`bParticipatesInInitialScan`。<br>2. 把 `DiscoverScriptRoots()` 升级为 `BuildSourceLayers()`；默认 profile 仍生成今天的 project root + plugin roots，保持现有搜索顺序不变。<br>3. 为 opt-in 项目新增 `PersistentPatch` layer，默认根可设为 `ProjectPersistentDownloadDir()/Script`；当同一逻辑模块在 patch layer 与 base layer 同时存在时，resolver 记录 `ResolvedLayerId` 与覆盖诊断，而不是静默只认物理路径顺序。<br>4. 让 `FindAllScriptFilenames()` 只负责 eager layers 的初始索引；后续 import 解析、显式模块请求和 future lazy loader 都统一经由 `IAngelscriptSourceResolver` 获取实际来源。<br>5. 在 `FAngelscriptModuleDesc` 或新的 source record 中补 `SourceLayerId` / `SourceOrigin`，并把 precompiled cache、reload journal 与 diagnostics key 升级为 `LogicalModuleId + SourceLayerId`，避免 patch/base 共存时互相踩 cache。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：patch layer 下同名模块优先覆盖 base layer；删除 patch 后会稳定回退到 base layer；未启用 overlay profile 时行为与 today 完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险主要在 cache/reload key 迁移：如果 overlay 引入后仍继续只用裸 `ModuleName` 或物理路径做 key，patch/base 两层会在热更、预编译缓存和诊断聚合上互相覆盖 |
| 兼容性 | 向后兼容。默认 source profile 仍只包含当前 project/plugin roots；`PersistentPatch` 和其它 overlay layer 均为显式 opt-in，不要求现有项目移动脚本目录 |
| 验证方式 | 1. 回归现有 project/plugin 脚本扫描与热更测试，确认默认 profile 不变。<br>2. 新增 patch overlay 回归，验证下载目录中的同名脚本会覆盖 base layer，且 diagnostics 能显示实际命中的来源层。<br>3. 删除 patch 文件后再触发 reload，验证系统能回退到 base layer 并清理旧 layer 的 cache/reload 记录。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-58 | compile worker 缺少 artifact / merge 边界 | request-local artifact + serial merge | 高 |
| P2 | Arch-SL-59 | 脚本来源缺少 overlay / patch 优先级 | source layer / overlay profile | 中 |

---

## 架构分析 (2026-04-09 23:58)

### Arch-SL-60：hot reload 的 invalidation surface 仍只认 `*.as` 文件变化，`CompileProfile` 与 sidecar metadata 变更不会自然触发受影响模块重编

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 生命周期输入的失效面，尤其是 `PreprocessorFlags`、`bAutomaticImports`、未来 manifest / search profile / custom phase 版本这类非源码输入变更后，系统能否只重编真正受影响的模块 |
| 当前设计 | 当前热更与重编调度基本只消费脚本文件时间戳和目录 watcher 事件；`CompileProfile` 相关状态在 engine 初始化和预处理构造时被读取，但这些输入本身没有进入 hot reload 队列或模块级 invalidation 账本 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1280-1293` — `PreInitialize_GameThread()` 把 `ConfigSettings->bAutomaticImports` 采样到 `bUseAutomaticImportMethod`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1415-1416` — 引擎属性 `asEP_AUTOMATIC_IMPORTS` 只在初始化时设置；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:38-73` — 预处理器构造时直接从 `UAngelscriptSettings::PreprocessorFlags` 和运行环境组装 `PreprocessorFlags`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1658-1700,2729-2778,2859-2895` — checker thread 与 `CheckForHotReload()` 只围绕 `CheckForFileChanges()`、`FileChangesDetectedForReload`、`QueuedFullReloadFiles` 运作，而 `CheckForFileChanges()` 又只扫描 `FindAllScriptFilenames()` 返回的 `*.as`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89` — editor watcher 也只把 `.as` 文件或包含 `.as` 的新目录映射成 reload 队列 |
| 优点 | 规则简单，当前实现能稳定覆盖“脚本源码变了就重编”这条主路径；文件时间戳和目录 watcher 的调试成本也较低 |
| 不足 | 一旦模块选择开始依赖 `CompileProfile`、manifest、search profile、package-style 入口规则或 custom compile phase 版本，今天的 invalidation surface 就会失真：输入已经变化，但 runtime 不会主动把相关模块标记为脏；最终要么要求手工全量重启，要么冒着用旧模块图继续运行的风险 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `Search()` 本身就把 `.js/.mjs/.cjs/.json/.mbc/.cbc/package.json/index.js` 当作模块解析输入；`require()` 命中 `package.json` 后还会继续解析 `main/exports` 决定真实入口。这里的关键不是“自动监听 metadata 文件”，而是 loader contract 明确认知这些非源码输入会影响模块装载结果 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-123`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-176` | 先把非源码输入纳入模块请求语义，后续 reload / forceReload / cache miss 才有机会围绕这些输入做精确失效 |
| UnLua | `FLuaEnv` 启动时安装 `package.searchers`；`LoadFromFileSystem()` 每次加载都会读取当前 `package.path` 并据此决定搜索模式。这里同样不是自动 watcher，而是“搜索配置是 loader 的正式输入”，而不是 compile 之外的隐式背景 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:614-641` | 先让 loader/request 显式消费搜索配置，再决定是否为这些输入增加缓存或变更传播机制；这样配置变化不会永远游离在模块系统之外 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 file-only hot reload 兼容路径的前提下，引入 `lifecycle input ledger`，把 `CompileProfile`、search/profile metadata 与 future manifest 版本纳入模块失效面 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptLifecycleInputFingerprint`、`FAngelscriptLifecycleInputChange`，至少记录 `CompileProfileVersion`、`SearchProfileVersion`、`SettingsHash`、`ManifestHashes`、`CompilePhaseFingerprint`。<br>2. 让 `FAngelscriptPreprocessor` 和 `CompileModules()` 在产出 `FAngelscriptModuleDesc` 或 future artifact 时同步记录“本模块消费了哪些 lifecycle inputs”；第一阶段即使先只记录 request-global profile/version，也先把 ledger 建出来。<br>3. 扩展 `CheckForHotReload()` 或 future scheduler：除了 `DirtyFiles` 外，再消费 `ChangedInputs`；如果某次输入变化无法映射到精确模块集合，第一阶段可保守退化为“该 profile 下的全批次模块重编”。<br>4. 在 editor/dev 模式增加 opt-in 的 metadata/settings 变更来源，例如 settings save、manifest 文件更新时间、search profile refresh 事件；默认仍保留今天的 file-only 监视，避免突然放大重编频率。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h/.cpp` 增加 `LifecycleInputContributor` 或等价扩展口，让 future `lint` / optimizer / manifest resolver 能把自己的版本号并入 fingerprint，而不是继续靠旁路文档约定。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：切换 `bAutomaticImports` 或 `PreprocessorFlags` 后受影响模块会进入 reload；manifest/search profile 变化会触发对应模块失效；关闭 metadata watch 时行为与 today 的 file-only 路径一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险在于输入到模块的映射粒度：如果第一阶段就尝试过度精确，最容易漏掉真实受影响模块；应先允许保守扩大失效范围，再逐步把 ledger 从 request-global 收窄到 module-local |
| 兼容性 | 向后兼容。默认仍可保持 today 的 file-only watch 语义；metadata/settings invalidation 可以先作为 editor/dev opt-in 能力，不要求现有脚本修改语法 |
| 验证方式 | 1. 回归现有 file watcher / checker thread / soft/full reload 测试，确认默认路径不变。<br>2. 新增 `PreprocessorFlags` 与 `bAutomaticImports` 切换回归，验证无脚本文件改动时也能触发受影响模块重编。<br>3. 新增 manifest/search profile 版本变更测试，验证系统不会继续复用旧模块图或旧 dependency closure。 |

### Arch-SL-61：模块目录仍把 `*.as` 当作唯一一等成员，sidecar metadata 无法拥有模块入口与依赖归属

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块系统能否把 `ScriptModules.json`、package-style entry metadata、future version/tag sidecar 当作模块图中的正式输入，而不是文件系统之外的旁路说明 |
| 当前设计 | 当前模块目录由 `*.as` 扫描结果直接构成，`ModuleName` 也直接由相对脚本路径推导；目录 watcher 仅把 `.as` 文件和包含 `.as` 的目录视为相关变更，因此 sidecar metadata 既不能决定逻辑模块入口，也没有自己的依赖归属 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1999-2015,2074-2079` — `FindAllScriptFilenames()` 只用 `*.as` 扫描脚本根，`InitialCompile()` 逐个把这些文件送入 `Preprocessor.AddFile(...)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:14-27` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:86-100` — 预处理公开入口仍是 `AddFile(Relative, Absolute)`，`ModuleName` 直接由相对文件名转换而来；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:55-89` — watcher 只把 `.as` 文件或新增目录内的 `.as` 文件加入 reload 队列，非 `.as` metadata 本身不会成为 module graph 的节点 |
| 优点 | 1 个脚本文件对应 1 个模块，规则直接、容易调试；当前仓库的绝大多数脚本组织方式也都能用这套模型运行 |
| 不足 | 如果未来要引入 `ScriptModules.json`、逻辑模块别名、版本化入口、包级 `exports`、按 profile 选 entry 或多文件组成一个逻辑模块，今天的 catalog 没有地方记录“哪份 metadata 拥有这个模块”；最终只能把 metadata 逻辑偷偷塞进 resolver 实现细节，生命周期图、watcher、diagnostics 和 cache key 都看不到它 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `SearchModuleInDir()` 显式把 `package.json`、`index.js` 等作为候选入口；`require()` 命中 `package.json` 后继续读取 `main/exports` 决定真正执行哪个模块。也就是说，sidecar metadata 不是注释性文档，而是 resolver 的正式输入 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-123`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:156-176` | 把 metadata 文件提升为模块解析 contract 的一部分，模块入口、cache key 和 reload 语义才能围绕“逻辑模块”而不是“碰巧选中的脚本文件”组织 |
| UnLua | `FLuaEnv` 先安装 `package.searchers`，再在 `LoadFromFileSystem()` 里用 `package.path` 解释逻辑模块名到 chunk 文件的映射。虽然它没有 `package.json`，但模块入口规则同样由 loader metadata 决定，而不是硬编码成“模块名必须等于某个固定脚本文件路径” | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:614-641` | 即便没有显式 manifest 文件，也应先把“逻辑模块 -> 入口 artifact”做成 loader 所有权，而不是让预处理器直接把物理文件路径当最终模块身份 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 path-derived 默认行为之上新增 `module input artifact` 层，让逻辑模块可以同时拥有入口脚本和 sidecar metadata，而不是只拥有一个 `.as` 文件 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleInputArtifact` 与 `FAngelscriptModuleResolutionResult`；artifact 至少区分 `EntryScript`、`Manifest`、`PackageRule`、`ProfileConfig`，并记录 `ResolvedPath`、`DebugPath`、`Fingerprint`、`OwnerModuleId`。<br>2. 把当前 `FindAllScriptFilenames()` 的职责收束为默认 `FileSystemArtifactResolver`：它仍按 `*.as` 生成一对一模块，保证旧项目零行为变化。<br>3. 新增可选 `ManifestArtifactResolver`，读取 `ScriptModules.json` 或 future package-style metadata，产出 `ModuleId -> EntryArtifact + SupportArtifacts + Aliases/Version/Tags`；未声明模块继续回退到默认文件系统规则。<br>4. 让 `InitialCompile()` / `PerformHotReload()` / future lazy loader 不再直接围绕“脚本文件列表”工作，而是围绕 `ResolutionResult` 中的 artifact 集合工作；metadata 变更时可先定位 owner module，再决定最小失效闭包。<br>5. 扩展 `AngelscriptDirectoryWatcherInternal.cpp` 与 checker thread：除了入口 `.as` 外，也可在 opt-in 模式下注册 artifact 附属文件监视；若 metadata 改变，只回队列其 owner modules，而不是回退到全量重扫。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：无 manifest 的旧工程仍按路径推导模块；加入 `ScriptModules.json` 后逻辑模块可改 entry/alias 而不改 `import` 字符串；metadata 变更只会失效 owner module 及其依赖闭包。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是所有权冲突：同一逻辑模块如果同时被 legacy path 规则和 manifest 定义，必须先有明确优先级和冲突诊断，否则 watcher、cache 和 reload journal 会出现双重真相 |
| 兼容性 | 向后兼容。默认 `FileSystemArtifactResolver` 继续维持 today 的一文件一模块模型；manifest/package metadata 仅为新增 opt-in 能力，不要求现有脚本改写 `import` |
| 验证方式 | 1. 回归现有 `*.as` 扫描、初编译和热更测试，确认默认 resolver 路径不变。<br>2. 新增 manifest entry/alias 测试，验证逻辑模块入口可由 sidecar metadata 决定。<br>3. 新增 metadata watcher 回归，验证修改 `ScriptModules.json` 或同类 artifact 后，系统会定位并失效正确的 owner module 集合。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-60 | 非源码 lifecycle input 的失效传播 | lifecycle input ledger + targeted invalidation | 高 |
| P1 | Arch-SL-61 | sidecar metadata 的模块归属与入口解析 | module input artifact / manifest resolver | 高 |

---

## 架构分析 (2026-04-10 00:08)

### Arch-SL-62：编译后端已经支持 multi-section module，但预处理与预编译落盘仍把“文件入口”写死成单 section / 单文件

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 逻辑模块是否能由多个 section / artifact 组装，从而为 package-style entry、generated prelude、future multi-file bundle 提供增量切口 |
| 当前设计 | `FAngelscriptModuleDesc` 底层已经允许一个模块挂多个 `Code` section，并在编译时逐 section `AddScriptSection()`；但预处理阶段仍把每个 `FFile` 压成一个 `ProcessedCode`，generated/defaults/helper 代码被直接拼回同一段文本，预编译结构也仍只保存单个 `ScriptRelativeFilename` |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1272-1286` — `FAngelscriptModuleDesc` 公开的是 `TArray<FCodeSection> Code`，不是单字符串；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4342-4345` — Stage1 会遍历 `Module->Code`，逐个 `AddScriptSection(...)` 到 `asCModule`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:289-304` — 但 `Preprocess()` 对每个 `FFile` 只生成 1 个 `FCodeSection`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3983-4005` — `CondenseFromChunks()` 会把 authored chunks 与 `GeneratedCode` 直接串接成单个 `ProcessedCode`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h:423-461` — `FAngelscriptPrecompiledModule` 落盘仍只有单个 `ScriptRelativeFilename`，没有 section 清单 |
| 优点 | 后端 seam 已经存在，说明做 module assembly 不必推翻 `asCModule` 接口；默认单文件路径今天也最稳定 |
| 不足 | 当前缺的不是“能不能加第二个 section”，而是“谁来声明 section 的角色、顺序和来源”；没有 assembly 层时，`package entry + generated prelude + defaults support` 只能继续被偷塞回单个 `ProcessedCode`，future multi-file logical module 也会被迫绕过已有 `Code` 数组能力 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require()` 先通过 `searchModule()` 得到 `fullPath/debugPath`，遇到 `package.json` 时再解析 `main/exports` 并 `tmpRequire(url)` 跳到真正入口；模块装配由 resolver 决定，而不是把“一个物理脚本文件”硬编码成唯一入口 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-176` | logical module 应先有 assembly/result，再决定最终执行哪段 entry；metadata 与 entry 是 resolver/assembly 的职责 |
| UnLua | `LoadFromCustomLoader()` / `LoadFromFileSystem()` 返回的是 `Data + ChunkName`，`package.searchers` 决定查找链；chunk 的显示身份与加载来源由 loader 持有，不要求宿主先把模块压成某个固定磁盘文件 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-611,644-666` | loader/assembly contract 应该能表达“这个模块这一段代码从哪里来、以什么名字暴露给 VM”，而不是只剩一个文件路径 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持 today 单文件行为不变的前提下，引入 `module assembly manifest`，把现有 `Code` 数组真正提升为一等装配层 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptModuleAssemblySection` 与 `EAngelscriptSectionRole(Entry/Prelude/Generated/Defaults/Support)`，字段至少包含 `DebugPath`、`RelativePath`、`Order`、`Role`、`Content`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h/.cpp` 新增 `FAngelscriptModuleAssemblyBuilder`；默认 builder 仍把 1 个 `FFile` 产成 1 个 `Entry` section，保持现有行为。<br>3. 把 `CondenseFromChunks()` 之后的 generated/defaults/helper 文本改为先形成独立 section，再由 assembly builder 按顺序 materialize 到 `Module->Code`；第一阶段 authored main body 仍可保留为单个 `Entry` section。<br>4. 扩展 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h/.cpp`，把 today 的 `ScriptRelativeFilename` 升级为 section manifest 或兼容字段组；旧 cache 读取时自动折叠为单 section。<br>5. 让 future `ManifestArtifactResolver` / package-style resolver 直接返回 assembly section 列表，而不是继续把 entry metadata 偷偷拼进 `ProcessedCode`。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：legacy 单文件模块仍只生成 1 个 `Entry` section；generated prelude 与 defaults 可作为独立 section 进入编译；package-style logical module 能把 metadata 决定的 entry 和 support section 一起送进同一个 `FAngelscriptModuleDesc`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险在于 section order 一旦变成显式契约，就必须同步约束 diagnostics/source-map/hash；如果只补装配层而不补 identity/fingerprint，后续 cache 与 reload 会出现“能编出来，但命中/失效不稳定”的灰区 |
| 兼容性 | 向后兼容。默认 assembly builder 继续输出单 section；旧脚本目录结构、`import` 语法和现有一文件一模块工程无需改动 |
| 验证方式 | 1. 回归现有初编译、热更、预编译 cache 测试，确认 legacy 模块仍只产出单 section。<br>2. 新增独立 `Prelude/Generated` section 测试，验证编译结果与 today 拼接文本一致。<br>3. 新增 package-style assembly 测试，验证 metadata 选 entry 后仍能得到稳定 diagnostics/debug path。 |

### Arch-SL-63：模块 identity 仍靠 XOR 聚合 `CodeHash/CombinedDependencyHash`，对 section 顺序与 bundle 结构不敏感

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译产物与预编译缓存的 identity 是否足以稳定承载 ordered sections、package entry、future multi-file bundle 与更细粒度 invalidation |
| 当前设计 | 模块内每个 section 先单独算 `Section.CodeHash`，随后直接 XOR 到 `Module->CodeHash`；依赖模块的 `CombinedDependencyHash` 也继续被 XOR 聚合，而预编译命中判断仍只检查 `CompiledModule->CodeHash == Module->CodeHash` |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:292-301` — `Section.CodeHash = XXH64(...)` 后直接 `File.Module->CodeHash ^= Section.CodeHash`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4262-4280` — `CombinedDependencyHash` 先等于 `Module->CodeHash`，随后对每个 import 做 XOR 聚合；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4283-4299` — precompiled 命中仍只比较 `CompiledModule->CodeHash == Module->CodeHash`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h:432-461` — 落盘字段依旧只有 `CodeHash`、`ImportedModules`、`PostInitFunctions` 等，没有 ordered section fingerprint |
| 优点 | 计算代价低，适合 today 单文件、单入口、低 metadata 复杂度的路径 |
| 不足 | 推断：XOR 聚合天然对 section 顺序不敏感，且成对重复 section 会相互抵消；这意味着一旦模块 assembly 开始显式承载 `Entry/Prelude/Generated/Support`，today 的 hash 就无法稳定表达“同样内容、不同顺序/角色”的差异，也很难为 future package entry 或 bundle 结构提供可靠 cache key |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `moduleCache` 的 key 直接是 resolver 产出的 `fullPath`；`forceReload()` 也只标记这个精确 key，`package.json` 场景先解析到真实入口再进入 cache | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:134-146,205-225` | identity 应优先绑定到稳定的 logical module / resolved entry，而不是绑定到无序聚合 hash |
| UnLua | `loaded_modules`、`package.loaded` 与 `reload_modules(module_names)` 都严格按 `module_name` 工作；reload 的作用范围是显式模块名集合，不依赖某个无序内容聚合值 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170,553-601` | 先把模块 key 变成一等契约，再在其上补内容 fingerprint；失效传播与 cache 命中都更可解释 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 用 ordered `module fingerprint` 替代 today 的 XOR 聚合，让 section 角色、顺序与 dependency edge 一起进入模块 identity |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptModuleFingerprint`、`FAngelscriptSectionFingerprint` 与 `FAngelscriptDependencyFingerprint`；section 至少记录 `Role`、`Order`、`DebugPath`、`ContentHash`。<br>2. 让 `FAngelscriptPreprocessor` 在产出 `Module->Code` 时同步生成 ordered fingerprint；第一阶段可继续保留 legacy `CodeHash` 字段，便于兼容日志与旧 cache。<br>3. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4262-4280` 的 `CombinedDependencyHash` 升级为稳定序列化后的 dependency fingerprint，例如 `ImportedModuleId + ImportedPublicApiHash + EdgeKind` 的有序列表哈希，而不是继续 XOR。<br>4. 扩展 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h/.cpp`，把 precompiled 命中条件从单一 `CodeHash` 改成 `FingerprintVersion + ModuleFingerprint`；旧 cache 读到无新字段时直接视为一次性 miss。<br>5. 在 diagnostics/debug 日志里输出 fingerprint mismatch reason，例如 `SectionOrderChanged`、`PreludeChanged`、`DependencyPublicApiChanged`，避免未来只能看到泛化的“cache mismatch”。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：同内容不同 section 顺序不得再误命中；重复 generated section 不得因 XOR 抵消而错误复用 cache；dependency public surface 不变时 importer fingerprint 保持稳定。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是 fingerprint 迁移过快会一次性放大 cache miss；更稳妥的路径是先双写 `legacy hash + ordered fingerprint`，等命中边界和日志验证稳定后，再逐步让新 fingerprint 成为唯一命中依据 |
| 兼容性 | 向后兼容。第一阶段允许 legacy `CodeHash` 继续存在；旧 `PrecompiledScript.Cache` 最多只会经历一次预期内失效，不要求现有脚本改语法 |
| 验证方式 | 1. 构造 section 重排测试，验证新 fingerprint 能区分顺序变化而 legacy hash 不能。<br>2. 构造重复 generated section 测试，验证不会出现错误 cache 命中。<br>3. 回归现有 precompiled/hot reload 测试，确认默认单文件模块仍能稳定命中新 fingerprint。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-62 | module assembly 仍被单 section / 单文件前端锁死 | section manifest / assembly builder | 高 |
| P1 | Arch-SL-63 | `CodeHash/CombinedDependencyHash` 的无序聚合语义 | ordered fingerprint / cache identity 升级 | 高 |

---

## 架构分析 (2026-04-10 00:16)

### Arch-SL-64：模块激活顺序仍然复用 `Files/CompiledModules` 顺序，缺少独立的 dependency-aware boot plan

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本从“已编译”进入“执行 startup / 初始化默认对象”时，是否存在独立于 compile batch 的模块激活顺序模型 |
| 当前设计 | 当前 runtime activation 顺序不是一等契约，而是沿用 `Preprocessor.Files -> GetModulesToCompile() -> CompiledModules -> ClassGenerator.Modules` 这一条 compile batch 顺序；只有关闭 `automatic imports` 时才会先按显式 `import` 对 `Files` 做一次排序，其余场景默认沿用扫描/输入顺序 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2074-2082` — `InitialCompile()` 先 `FindAllScriptFilenames()`，再按返回顺序 `Preprocessor.AddFile(...)` 并直接取 `GetModulesToCompile()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:75-83` — `GetModulesToCompile()` 只是按 `Files` 当前顺序把模块抛出；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232-239` — 只有 `!ShouldUseAutomaticImportMethodForCurrentContext()` 时才会通过 `ProcessImports()` 重排 `Files`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3901-3907` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:85-95` — `CompileModules()` 会按 `CompiledModules` 顺序把模块逐个 `AddModule()` 进 `ClassGenerator`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5794-5819,5831-5856` — `CallPostInitFunctions()` 与 `InitDefaultObjects()` 都直接按 `Modules` 顺序迭代执行 |
| 优点 | 当前 eager compile + eager activation 路径非常直接，默认文件系统扫描下顺序稳定，排查问题时容易从 compile batch 反推运行顺序 |
| 不足 | compile dependency graph 和 runtime boot graph 被折叠成同一条隐式顺序后，`automatic imports`、precompiled descriptor、future manifest entry、lazy activation 或 `CodeOnly/TypeBearing` 分流都会改变启动时机但没有公开 contract；一旦某个模块依赖“先启动 provider 再启动 consumer”的副作用顺序，今天只能赌扫描顺序或 compile batch 恰好满足 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `genRequire()` 以一次 `require(moduleName)` 为 activation 单位；命中 `package.json` 时会在 importer 继续前先 `tmpRequire(url)` 跳到真实入口，因此模块执行顺序由 runtime dependency request 决定，而不是由宿主的文件扫描顺序决定 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-178` | 应把“谁先激活”建模成 request/entry graph，而不是把 compile batch 顺序直接拿来当 boot 顺序 |
| UnLua | `Start(StartupModuleName, Args)` 明确声明启动根模块；随后 `require(module_name)` 通过 `package.searchers` 递归装载依赖，并把结果写入 `loaded_modules/package.loaded`，启动顺序由入口模块及其 `require` 链显式决定 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-249`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:127-176` | 把 startup root 和后续依赖激活链分开表达，才能在不改变 compile pipeline 的情况下稳定扩展 boot policy |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `PostInitFunctions` 兼容路径之上增加显式 `boot plan`，把 activation 顺序从 compile batch 顺序里解耦出来 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptBootNode`、`FAngelscriptBootPlan` 与 `EAngelscriptBootPhase(PreCDO/PostCDO/AfterSubsystems)`；`BootNode` 至少包含 `ModuleKey`、`EntryFunction`、`Phase`、`DependsOn`、`bLegacyImplicitEntry`。<br>2. 新增 `BuildBootPlan(const TArray<TSharedRef<FAngelscriptModuleDesc>>& Modules, const FAngelscriptCompileProfile& Profile)`；默认 planner 先完全复用 today 顺序，把 `CompiledModules` 线性映射成 `PreCDO` 节点，从而保证老项目零行为变化。<br>3. 把 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5791-5856` 中直接遍历 `Modules` 的启动/默认对象初始化逻辑改成消费 `BootPlan`：先跑 `PreCDO` 节点，再 `InitDefaultObjects()`，后续阶段留给 explicit startup entry 或 toolchain observer。<br>4. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` 或 future manifest resolver 中补可选 metadata，例如 `startup`、`boot_after`、`boot_phase`；未声明时继续走 legacy implicit entry，不要求现有脚本增加新语法。<br>5. 在 planner 中增加冲突诊断：当两个模块都声明 `boot_after` 形成环、或 `automatic imports` 路径下没有足够信息构建稳定顺序时，先回退到 legacy 顺序并输出 warning，而不是悄悄换序。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：默认 planner 下 `PostInitFunctions` 执行顺序与 today 完全一致；显式 `boot_after` 能稳定覆盖扫描顺序；`automatic imports` + explicit startup metadata 下启动顺序不再依赖文件枚举顺序。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险主要在时序兼容性：literal asset getter 和 CDO 初始化今天默认处于同一个 `PreCDO` 窗口，如果 boot phase 边界定义不清，最容易引入“编译成功但资源初始化/默认对象初始化顺序漂移”的隐蔽回归 |
| 兼容性 | 向后兼容。默认 `BuildBootPlan()` 先输出与 today 完全一致的线性顺序；只有显式声明 startup metadata 或自定义 planner 的项目才会改变 activation order |
| 验证方式 | 1. 回归现有 `PostInitFunctions`、literal asset 与 reload 测试，确认默认 planner 与 today 顺序一致。<br>2. 构造两个存在先后副作用依赖的模块，验证 `boot_after` 能稳定覆盖扫描顺序。<br>3. 在 `automatic imports` 模式下做同一批脚本的不同枚举顺序测试，确认显式 boot metadata 能得到一致 activation order。 |

### Arch-SL-65：模块已在 `SwapInModules()` 后进入 live 集合，`PostInitFunctions` 才执行，activation failure 不能事务化回滚

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块从“已编译”到“已激活”是否具备原子提交边界，启动失败时能否保留旧 generation 并避免半启动副作用 |
| 当前设计 | 当前提交顺序是“先 `SwapInModules()` 把新模块写入 `ActiveModules`，再做 reload、startup 和默认对象初始化”；与此同时，globals 在 `Stage4` 已经 `ResetGlobalVars(0)`，而 `CallPostInitFunctions()` 对 `Prepare...`/`Execute()` 的结果没有结构化汇总或 rollback 路径 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4403-4410` — `CompileModule_Globals_Stage4()` 在编译阶段末尾就对 `ScriptModule` 执行 `ResetGlobalVars(0)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3938-3969,3994-3996` — soft/full reload 分支都会先 `SwapInModules(CompiledModules, ...)`，随后才 `PerformSoftReload()` / `PerformFullReload()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2907-2939` — `SwapInModules()` 会立即重命名 backend module 并把新模块写入 `ActiveModules`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2302-2304` — reload 尾部才执行 `CallPostInitFunctions()` 与 `InitDefaultObjects()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5799-5819` — `CallPostInitFunctions()` 只是查函数名、`PrepareAngelscriptContextWithLog(...)` 后直接 `Context->Execute()`，`bFound` 只是局部变量，没有被提升为外部可见 activation result |
| 优点 | 先 publish 再执行 startup 的路径简单粗暴，type reload、反射索引和后续脚本查找都能立刻看到新模块，当前 eager reload 逻辑实现成本低 |
| 不足 | 一旦某个 `PostInitFunctions`、默认对象初始化或未来显式 startup entry 在中途失败，系统已经把新 generation 发布到 `ActiveModules`，而且部分 globals/早期 startup 副作用可能已生效；外部既拿不到“哪些模块已半启动”的结构化状态，也没有官方 rollback 点能把 live 集合退回旧 generation |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require()` 在执行模块前先放入占位 `m`，但 `executeModule()` 或 `package.json` 路径一旦抛错，马上把 `localModuleCache[moduleName]` 与 `moduleCache[key]` 置回 `undefined`，失败模块不会继续留在 live cache 里 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:144-191` | activation commit 必须有失败回滚面；哪怕为了循环依赖先放占位，也要在失败时撤销 publish |
| UnLua | `reload_modules()` 先 `sandbox.enter(tmp_modules)`，对每个目标模块 `sandbox.load()` / `xpcall()`；任一模块失败就 `sandbox.exit()` 返回，只有全部成功才 `update_modules(...)` 把新模块内容提交回 live modules | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601` | 先在 staging/sandbox 中验证 activation，再一次性 commit，是避免半启动状态泄漏到 live runtime 的关键 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 reload 主链外补一层 `activation transaction`，把 publish、startup、CDO init 和 rollback 做成显式边界；第一阶段先让 `CodeOnly` 与纯 startup 模块受益，再逐步覆盖 type-bearing 模块 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptActivationTransaction`、`FAngelscriptActivationStepResult` 与 `EAngelscriptActivationState(Staged/Booting/Committed/RolledBack/Failed)`；结果至少记录 `ModuleKey`、`ExecutedEntries`、`FailedEntry`、`bGlobalsReset`、`bCDOInitStarted`。<br>2. 把 `SwapInModules()` 拆成 `StageModulesForActivation()` 与 `CommitStagedModules()`：第一阶段只对 `CodeOnly` 或无 `UClass/USTRUCT/UDelegate` 变更的模块延后 publish，默认 type-bearing 模块仍走 legacy 顺序，降低改造风险。<br>3. 将 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5791-5821` 改造成返回 `TArray<FAngelscriptActivationStepResult>`，显式记录“入口不存在 / prepare 失败 / execute 抛错 / 成功”；不要再让 `bFound` 在局部作用域内直接消失。<br>4. 为 `CodeOnly` 与 future `module runtime entry` 场景增加 `ActivationSandbox`：先在 staged module 上执行 startup，全部成功后再 `CommitStagedModules()`；失败时丢弃 staged module 并保持旧 `ActiveModules` generation。<br>5. 第二阶段再把 `InitDefaultObjects()` 纳入同一 transaction：先把 CDO init 结果写入 activation report，只有所有 required steps 成功时才最终 publish type-bearing modules；需要保守迁移时可通过 setting 保留 `bLegacyImmediatePublish=true`。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h/.cpp` 增加 `ActivationCommitted/ActivationRolledBack` 事件，让 debugger、tooling 和 future HMR 不必再从日志猜测 startup 是否半成功。<br>7. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：第二个模块 startup 失败时第一个模块不能被错误标成已提交；`CodeOnly` 模块 activation 失败后 `ActiveModules` 仍指向旧 generation；开启 legacy immediate publish 时行为与 today 完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 最大风险是 type-bearing 模块的 staged activation 很容易碰到 `UClass`、`CDO` 与 `Subsystem` 的时序耦合；更稳妥的迁移顺序是先把 transaction contract 建出来，并优先覆盖 `CodeOnly` / startup-only 模块，再逐步把 class reload 路径纳入同一 commit boundary |
| 兼容性 | 向后兼容。第一阶段默认仍允许 legacy immediate publish；atomic activation 先作为 opt-in 或仅对 `CodeOnly` 模块生效，不要求现有脚本改写 |
| 验证方式 | 1. 构造两个带 startup entry 的模块，让后者故意失败，验证前者不会在新 transaction 模式下被错误提交。<br>2. 新增 `CodeOnly` rollback 测试，确认 activation 失败后 `ActiveModules`、diagnostics 与旧 generation 保持一致。<br>3. 回归现有 soft/full reload 和 `PostInitFunctions` 测试，确认关闭 atomic activation 时行为与 today 不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-64 | activation 顺序仍绑定 compile batch 顺序 | boot plan / activation graph 显式化 | 高 |
| P1 | Arch-SL-65 | startup publish 缺少事务边界与 rollback | activation transaction / staged commit | 高 |

---

## 架构分析 (2026-04-10 00:25)

### Arch-SL-66：依赖图仍只有 `ImportedModules` 一种硬编译边，无法表达 `runtime require` / `optional` / `startup-only` 依赖

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块系统能否把“编译依赖”“启动依赖”“运行时按需依赖”“可选依赖”分开建模，从而支持增量加载、版本切换和更细的失效传播 |
| 当前设计 | 当前脚本依赖在前端只会落成 `FImport.ModuleName`，预处理后再收敛成 `FAngelscriptModuleDesc::ImportedModules`；编译器和热更链都把它当成唯一的强依赖边使用 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:101-108` — `FImport` 只有 `ModuleName` 与位置信息，没有 dependency kind / optionality / activation metadata；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1272-1306` — `FAngelscriptModuleDesc` 只有 `ImportedModules` 与 `PostInitFunctions` 两类模块级关系；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3497-3510` — `import Foo.Bar;` 只被解析成字符串模块名；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:463-483` — `ProcessImports()` 会把每条 `import` 直接加入 `Module->ImportedModules` 并从源码里 blank 掉；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:178-185` — `CompileModules()` 明确假设输入模块“已经按依赖顺序排序”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4264-4280` — Stage1 会把所有 `ImportedModules` 导入 `ScriptModule` 并把它们一起并入 dependency hash |
| 优点 | 单一强依赖边让编译顺序、循环依赖报错和热更闭包都比较直观，今天的 eager compile 路径也因此实现简单 |
| 不足 | 当前图无法表达“模块需要先编译但不必启动”“只在某个 startup entry 里按需拉起”“缺失时只降级不报 fatal”“只影响 boot plan 不影响 compile closure”这些常见语义；要做 `lazy load`、版本化模块或 feature-gated 模块时，只能继续把所有关系都伪装成硬编译边 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把模块关系拆成 `Search/Load` 与 `require()` 两层：loader 只负责找源码/字节码，真正的依赖激活在 `genRequire()` 里按请求时刻执行，并通过 `moduleCache` 维持 runtime cache | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-191,205-245` | 先把“怎么找模块”和“什么时候激活模块”拆开，依赖边才能从单一 compile edge 演进为 request-time edge |
| UnLua | `FLuaEnv::Start()` 只 `require` 一个 `StartupModuleName`；后续 chunk 装载由 `LoadFromCustomLoader` / `LoadFromFileSystem` 这条 searcher chain 负责，`require()` 和 `package.loaded` 再把每个模块缓存成运行时单元 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:63-65,126-130`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-245,557-644`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170` | 启动入口、loader 链和 runtime module cache 是三层不同 contract，不必把所有模块关系都压回“编译时导入” |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持旧 `import` 语义不变的前提下，引入一等的 `dependency kind`，先把现有关系显式化，再逐步接入 `runtime require` 与 `optional` 模块 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptModuleDependency` 与 `EAngelscriptDependencyKind(HardCompile/StartupOnly/RuntimeRequire/Optional/ToolingOnly)`；第一阶段让现有 `ImportedModules` 全部映射为 `HardCompile`，保留旧字段作为兼容视图。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h/.cpp` 让 `FImport` 与 `ProcessImports()` 产出结构化依赖；现有 `import Foo.Bar;` 仍只生成 `HardCompile`，不要求脚本改语法。<br>3. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 新增 `RequireModule(ModuleId)` / `EnsureModuleActivated(ModuleId, Reason)`；第一阶段只允许它命中“已经编译好的模块”，先把 activation seam 建出来，不马上改编译器。<br>4. 把 `CompileModules()`、reload invalidation 和 future fingerprint 只绑定 `HardCompile` 边；`StartupOnly` 边交给 boot plan，`RuntimeRequire`/`Optional` 边交给 activation/cache 层，避免继续把所有变化都扩散进 compile closure。<br>5. 在 `Documents/` 侧补一个可选 sidecar 方案，例如 `ScriptModules.json` 或等价 manifest，用于声明 `optional` / `startup-only` 依赖；未提供 manifest 时，行为保持今天不变。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：legacy `import` 仍生成相同 compile order；`RuntimeRequire` 模块不会被提前拉进 compile closure；缺失 `Optional` 模块时 activation 给出结构化 warning 而不是 compile fatal。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险主要在“新边类型被误用于仍依赖 class generation 的模块”。更稳妥的迁移顺序是先允许 `RuntimeRequire/Optional` 只作用于 `CodeOnly` 或已经编译存在的模块，再逐步放宽到更复杂的类型承载模块 |
| 兼容性 | 向后兼容。旧 `import`、旧 `ImportedModules` 和今天的 compile order 不变；新 dependency kind 只对显式 opt-in 的 manifest/API 生效 |
| 验证方式 | 1. 回归现有 initial compile / hot reload / precompiled cache 测试，确认 legacy `HardCompile` 路径完全不变。<br>2. 新增一个 `RuntimeRequire` 用例，验证模块可在 runtime activation 时才进入 live 集合。<br>3. 新增 `Optional` 缺失用例，确认编译成功、启动给出可诊断 warning，且不污染无关模块的 dependency hash / reload 闭包。 |

### Arch-SL-67：预处理失败仍是 batch 级 `bHasError` 短路，dirty closure 中的干净模块没有 `salvage lane`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当一批 hot reload / 初始编译输入里只有部分模块出错时，系统能否让不依赖错误模块的那一部分继续前进 |
| 当前设计 | 当前 `FAngelscriptPreprocessor` 只有一个 batch 级 `bHasError`；hot reload 先把 changed files 与依赖它们的模块全部拉进一个 dirty closure，再统一 `Preprocess()`；只要其中任意文件触发预处理 fatal，整批请求就直接失败并保持旧代码 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:169-170` — 预处理器内部只有单个 `bHasError` 标记；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:212-243` — `Preprocess()` 在 imports/detection 阶段只要看到 `bHasError` 就整体 early-out；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:439-453` — 循环 `import` 会直接把 batch 标成 error；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:4338-4343` — 未知 `PreprocessorFlags` 条件同样直接设置 `bHasError`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2388-2443` — hot reload 会把 changed file 所属模块及其所有 dependent modules 的文件都加入同一个 `FilesToHotReload` 闭包；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2448-2461` — 一次 `Preprocessor.Preprocess()` 失败就记录 “Keeping all old angelscript code.” 并返回，不存在“独立模块继续编译”的分支 |
| 优点 | batch 级短路保证了当前 reload 行为简单保守，不会在预处理阶段留下半成功、半失败的未定义状态 |
| 不足 | 一处语法错误、flag 拼写错误或循环 `import` 会拖住整个 dirty closure；即使另一部分模块与失败模块无依赖关系，也只能一起留在旧 generation，系统也无法区分“模块本身失败”与“仅仅被失败依赖阻塞” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require()` 失败时只回滚当前 `moduleName/fullPath` 对应的 cache entry：`localModuleCache[moduleName]` 与 `moduleCache[key]` 都会在 `catch` 中置空，其它已加载模块不受影响；`forceReload()` 也只标记目标 key | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-191,205-219` | 把失败隔离到 module request / cache entry，而不是把整轮运行时模块集一起判成失败 |
| UnLua | `reload_modules(module_names)` 先把目标模块集放进 `sandbox`，只有全部 `xpcall()` 成功后才 `update_modules(...)`；失败则 `sandbox.exit()` 并保留旧 live modules，不会半提交 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601` | 即使仍是 batch reload，也把作用范围收敛到显式 `module_names` 集合，并用 staging/sandbox 保证失败时旧模块继续可用 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 hot reload 加一层 `module preprocess result` 与 `blocked-by-dependency` 分类，先实现“安全子集可继续”的 `salvage lane`，再决定是否默认放开 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h/.cpp` 新增 `FAngelscriptPreprocessBatchResult`、`FAngelscriptModulePreprocessResult` 与 `EPreprocessStatus(Ready/Failed/BlockedByDependency/Skipped)`；不要再只返回一个 `bool`。<br>2. 保留 today 的 dirty closure 构建，但在 `ProcessImports()` / parse 阶段把错误绑定到具体 `ModuleName`；如果模块 A 失败，则只把直接或间接依赖 A 的模块标成 `BlockedByDependency`，与 A 无关的模块继续进入 `Ready` 集。<br>3. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 让 `CompileModules()` 与 activation transaction 只消费 `Ready` 集；`Failed/Blocked` 模块维持旧 generation，并把原因写入新的 reload journal，而不是继续混在 `PreviouslyFailedReloadFiles` 里。<br>4. 第一阶段只在 `SoftReload` 且 `ReloadRequirement` 可被判定为安全子集时启用 `salvage lane`；`FullReloadRequired`、type-bearing 复杂变更和 legacy 模式仍走今天的全批次短路，控制风险。<br>5. 把 `PreviouslyFailedReloadFiles` 升级成 `FailedReloadModules`，区分 `PreprocessError`、`CompileError`、`BlockedByDependency` 三类原因；retry 也按模块而不是按原始 file list 触发。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：两个互不依赖的 dirty 模块中一者失败时，另一者仍可完成 soft reload；依赖失败模块的 consumer 必须被标成 `BlockedByDependency` 而不是误判为自身编译错误；关闭 `salvage lane` 时行为与今天完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 最大风险是 partial reload 会让“成功模块的提交”和“失败模块的旧 generation”并存，从而放大 today 对 module generation / activation 边界的要求；因此第一阶段必须依赖已有 reload requirement 和 activation transaction 结果，只在安全子集上放行 |
| 兼容性 | 向后兼容。可以先把 `salvage lane` 作为 editor/dev opt-in 或只在明确安全的 `SoftReload` 场景启用；legacy batch abort 路径保留 |
| 验证方式 | 1. 构造两个互不依赖的 dirty 模块，其中一个故意写错 `#if FLAG`，验证另一个仍能完成 soft reload。<br>2. 构造 `Provider -> Consumer` 链路，让 `Provider` 预处理失败，确认 `Consumer` 被标记为 `BlockedByDependency` 且保持旧 generation。<br>3. 回归现有 hot reload / full reload / rollback 测试，确认关闭 `salvage lane` 时行为与 today 一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-66 | 依赖边只有单一硬编译语义 | dependency kind / runtime require seam | 高 |
| P1 | Arch-SL-67 | 预处理失败是 batch 级短路 | preprocess result ledger / salvage lane | 高 |

---

## 架构分析 (2026-04-10 00:33)

### Arch-SL-68：bootstrap artifact 被拆在 project first-root 与 plugin base dir，两套入口无法形成自洽的插件交付单元

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译-加载启动阶段的 artifact 存放契约，是否支持 `Angelscript` 作为独立插件交付而不是依赖 host project `Script/` 目录兜底 |
| 当前设计 | `Binds.Cache` 与 `PrecompiledScript*.Cache` 固定通过 `GetScriptRootDirectory()` 落到 `AllRootPaths[0]`，而 `BindModules.Cache` 又固定从插件 base dir 读取；同一个 bootstrap contract 被拆成 “project root artifact + plugin-local bind module list” 两半 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781-793` — `GetScriptRootDirectory()` 明确返回 `AllRootPaths[0]`，注释写死“first root ... is the game project root”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1363` — `DiscoverScriptRoots()` 把 project root 插在 index `0`，plugin roots 只作为后续附加项；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1469-1477` — `Binds.Cache` 从 `GetScriptRootDirectory()` 读取，而 `BindModules.Cache` 从 `plugin->GetBaseDir()` 读取；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1504-1505,1521-1534,1585-1587` — bind database 与 `PrecompiledScript*.Cache` 的写回/读回也全部锚定 project first-root |
| 优点 | host project 永远掌握最终 script artifact 的落点，调试和人工清理路径直观；对“单工程内开发插件”的当前工作流来说实现成本低 |
| 不足 | 插件源码、bind provider 清单、预编译 cache 不在同一个可版本化单元里；一旦要把 `Plugins/Angelscript` 当作可复用插件交付，host project `Script/` 目录就变成隐藏前置条件。多 root、profile-specific cache、自定义 compile pass 输出也缺少统一归宿 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `IJSModuleLoader` 把 `Search/Load/GetScriptRoot` 聚合成一个 loader contract；bootstrap 只先执行固定 `puerts/*.js`，随后真实模块请求都走同一套 loader/root 语义，而不是把一部分入口信息放 project、一部分放 plugin module | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:92-120`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:621-635` | 先把“模块根、搜索、装载”统一挂在 loader 上，bootstrap 产物与运行时请求才不会分裂成两套路径约定 |
| UnLua | `FLuaEnv` 启动时只安装 `package.searchers`；`LoadFromFileSystem()` 统一从 `package.path` 推导 persistent/project 搜索路径，`Start(StartupModuleName)` 再沿同一 loader 链执行入口模块 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:614-641` | source root、入口模块和 loader 链是同一份 env 配置的产物；交付边界先围绕 runtime profile 定义，而不是围绕若干硬编码文件位置拼装 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入统一的 `artifact store`/`artifact locator`，让 bind database、bind provider 清单、precompiled cache、未来 manifest 都围绕同一份 `RootId/Profile` 契约解析 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptArtifactStore` 与 `IAngelscriptArtifactLocator`，统一暴露 `GetBindDatabasePath()`、`GetBindProviderManifestPath()`、`GetPrecompiledScriptPath(Profile)`、`GetArtifactManifestPath()`。<br>2. 默认 locator 先保持 today 行为：project root 仍可作为 legacy 主路径，plugin `BindModules.Cache` 也继续可读；但调用点不再直接写死 `GetScriptRootDirectory()` 或 `plugin->GetBaseDir()`。<br>3. 为每个参与脚本生命周期的 root/provider 生成一个轻量 `ArtifactManifest`，至少记录 `RootId`、`SourceRoot`、`ArtifactOwner(Project/Plugin)`、`Profile`、`GeneratorVersion`、`BindProviders`、`PrecompiledCacheFiles`。<br>4. `Initialize_AnyThread()` 先构造 artifact locator，再依次读取 bind database、bind provider manifest、precompiled cache；第一阶段只做路径归一，不改编译逻辑。<br>5. 第二阶段允许 plugin root 显式声明“artifact self-contained”，让 reusable plugin 可以把自己的 bind/precompiled 产物随插件一起分发；host project 不声明时仍走 legacy project-root 存放。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：legacy host project 路径不变；plugin-owned artifact 能在没有 project `Script/Binds.Cache` 的情况下完成启动；多 root 情况下 manifest 冲突会给出结构化诊断而不是静默覆盖。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险在于 legacy 路径与新 manifest 并存时容易出现“双份 artifact 都可读”的歧义；必须先定义明确优先级和冲突日志，否则会制造比今天更隐蔽的启动差异 |
| 兼容性 | 向后兼容。旧 `Binds.Cache`、`BindModules.Cache`、`PrecompiledScript.Cache` 继续可读；只有显式开启 plugin-owned artifact 或新增 manifest 的工程才进入新布局 |
| 验证方式 | 1. 回归现有 host-project 工作流，确认路径、启动时序和生成产物与 today 一致。<br>2. 构造一个只携带插件内 artifact 的最小宿主工程，验证 `Angelscript` 插件可自举启动。<br>3. 在多 root/profile 场景下校验 artifact manifest 的解析结果，确认不会再把 bind provider 与 precompiled cache 拆到不同所有者路径上。 |

### Arch-SL-69：`fully precompiled` 启动路径会把 root profile 长期停留在 project-only snapshot，artifact mode 与 search profile 被错误耦合

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块搜索面是否独立于 artifact 命中方式，尤其是后续要支持 `mixed-mode`、plugin-root lazy load 或按模块回退到 source compile 时，runtime 是否仍持有完整 root profile |
| 当前设计 | `Initialize_AnyThread()` 先把 `AllRootPaths` 设成 `DiscoverScriptRoots(true)` 的 project-only snapshot；只有 `InitialCompile()` 落到 source 预处理分支时，才会在 `else` 中改成 `MakeAllScriptRoots()`。因此一旦本次启动直接走 `PrecompiledData->GetModulesToCompile()`，`AllRootPaths` 会一直保留 project-only 视图 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1431` — 初始化早期固定 `AllRootPaths = DiscoverScriptRoots(/*bOnlyProjectRoot =*/ true)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2046-2063` — `InitialCompile()` 只有在不走 fully precompiled 分支时才执行 `AllRootPaths = MakeAllScriptRoots()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1999-2015` — 文件扫描始终读取成员 `AllRootPaths`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3043-3051` — `GetModuleByFilename()` 的 root-relative 回退也依赖同一份 `AllRootPaths`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2733` — 当前 fully precompiled 运行又会直接禁掉 hot reload，所以这个 root profile 缺口暂时被遮住。推断：一旦后续引入 `mixed-mode precompiled runtime` 或 `RequireModule()`，现有初始化时序会首先阻断 plugin-root 的按需回退 |
| 优点 | 对今天的 fully precompiled 模式来说实现简单，少做一次 plugin root 扫描；又因为 hot reload 被整体关闭，短期内不容易暴露错误行为 |
| 不足 | `artifact mode` 和 `search profile` 被绑死在一起：命中 precompiled artifact 不应意味着 runtime 失去完整 root 知识。这样会直接限制未来的按模块 source fallback、plugin script lazy load、逻辑模块定位和跨 root 诊断一致性 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | bootstrap 只先执行固定 `puerts/*.js`，但之后每次模块请求仍统一走 `ModuleLoader->Search(RequiringDir, ModuleName, ...)` 与 `Load(Path, ...)`；是否命中 source、`package.json`、bytecode 都不会改变 loader 自己维护的 root/search policy | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:621-635`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4079-4118`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:92-120` | “怎么找模块”必须独立于“这次命中了哪种 artifact”；loader root 不应该被 cache 命中路径重写 |
| UnLua | `FLuaEnv` 初始化时固定安装 `LoadFromCustomLoader/LoadFromFileSystem/LoadFromBuiltinLibs` searcher；`LoadFromFileSystem()` 每次都按 `package.path` 检查 persistent/project 目录，`Start(StartupModuleName)` 只决定入口，不改变 searcher 链 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:96-100`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:614-641` | 入口模块、已加载模块、loader 搜索面是三层不同 contract；是否走缓存/热更不会反向收窄 root profile |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `search profile` 从 `AllRootPaths` 的 artifact-mode 可变状态里拆出来，启动时总是构建完整 root catalog；fully precompiled 只跳过 scan/compile，不再修改搜索面 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptSearchProfile`、`FAngelscriptRootEntry` 与 `BuildSearchProfile()`；字段至少包含 `RootId`、`AbsolutePath`、`RootKind(Project/Plugin/PersistentPatch)`、`Priority`、`bParticipatesInInitialScan`。<br>2. `Initialize_AnyThread()` 无论是否启用 precompiled，都先构建完整 search profile；`AllRootPaths` 第一阶段可以继续保留为兼容视图，但其内容应来自 search profile，而不是来自 `DiscoverScriptRoots(true)` 的临时 snapshot。<br>3. `InitialCompile()` 在 fully precompiled 分支下只跳过 `FindAllScriptFilenames()` 与 `Preprocessor.AddFile(...)`，不要再承担“顺便刷新 roots”的副作用；source 分支仍按 today 行为扫盘。<br>4. 把 `FindAllScriptFilenames()`、`GetModuleByFilename()`、future `RequireModule()` / `ArtifactStore` / `ReloadJournal` 改为读取 search profile，而不是直接读写裸 `AllRootPaths`。<br>5. 第一阶段保持 `bUsedPrecompiledDataForPreprocessor` 与 hot reload 禁用逻辑不变，只新增完整 root metadata；第二阶段再把 mixed-mode source fallback 建在这份稳定 search profile 上。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：fully precompiled 启动下 search profile 仍含 plugin roots；legacy fully precompiled 依然不会尝试 hot reload；开启 future mixed-mode 开关时，plugin-root 模块可按需回退到 source compile 而不依赖重新初始化 engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险主要在初始化时序：如果 search profile 构建得过早，某些 commandlet 或 plugin enable 状态可能还没稳定；因此第一阶段应只收集 root metadata，不做额外扫盘或 watcher 注册，避免把成本和副作用提前 |
| 兼容性 | 向后兼容。默认 fully precompiled 行为、hot reload 禁用语义和现有 script 用户体验保持不变；变化只在于 runtime 内部持有更完整的 root 信息，为后续 mixed-mode/lazy load 铺路 |
| 验证方式 | 1. 回归现有 fully precompiled 启动路径，确认启动耗时和行为无显著变化。<br>2. 增加 test access 校验：使用 precompiled descriptor 启动时，search profile 仍能枚举 plugin roots。<br>3. 在引入 mixed-mode 开关的实验分支上验证：某个 plugin-root 模块未命中 artifact 时，可沿稳定 search profile 回退到 source compile，而不是因为 `AllRootPaths` 缺失而失败。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-68 | bootstrap artifact 所有权与存放契约分裂 | artifact store / locator 统一化 | 高 |
| P2 | Arch-SL-69 | `fully precompiled` 路径把 search profile 锁成 project-only | search profile 与 artifact mode 解耦 | 中 |

---

## 架构分析 (2026-04-10 00:45)

### Arch-SL-70：`ImportModule()` 在 Stage1 扁平化传递依赖，模块来源 provenance 在 backend module 层被抹平

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块依赖边是否保留“direct import / transitive import / re-export”来源，从而支撑版本化模块、精确失效和受控热替换 |
| 当前设计 | 预处理阶段只把 `import` 语句记成 `ImportedModules` 的字符串数组；进入 Stage1 后，runtime 直接调用底层 `ImportModule()` 把 imported module 及其已导入模块一并压进当前 backend module，direct 与 transitive provenance 在编译内核里被扁平化 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:463-483` — `ProcessImports()` 只把 `ImportDesc.ModuleName` 追加到 `File.Module->ImportedModules`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1302-1303` — `FAngelscriptModuleDesc` 只保存裸 `ImportedModules` 名字，没有 direct/transitive/re-export 区分；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4264-4280,4420-4424` — `CompileModule_Types_Stage1()` 对每个 imported module 调 `ImportIntoModule()`，并把 `CombinedDependencyHash` 直接 XOR 进当前模块；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp:1617-1633` — `asCModule::ImportModule()` 会把目标模块和它的 `importedModules` 一起压平到当前模块的 `importedModules` 列表 |
| 优点 | 现有行为简单，Stage1 不需要再维护显式的 module provenance 图；传递依赖自动可见，旧脚本几乎不需要考虑 re-export 规则 |
| 不足 | 一旦模块边在 backend module 内被扁平化，系统就失去“这个符号是 direct dependency 还是 transitive leakage”的结构化事实；这会直接削弱 `version switch`、`precise reload invalidation`、`strict module boundary` 和 `friend/private surface` 的可实施性，因为运行时只剩“当前模块最终看到了哪些名字”，而没有“是通过谁、以什么契约看到的” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `require()` 以单个模块请求为边界工作：先拿到 `fullPath`，再创建 `m.exports` 与独立 cache entry，业务模块通过 `tmpRequire(url)` 显式请求下一个模块，传递依赖不会被压进当前模块命名空间 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-146`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:174-176`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:183-195` | 让 direct dependency 继续保持“模块对象 -> 下一次请求”的链式结构，provenance 不会在加载期丢失 |
| UnLua | `require(module_name)` 只按 `module_name` 读取/缓存该模块返回的 `new_module` table；如果模块内部还依赖别的模块，会在它自己的 `require()` 中继续展开，而不是把下游 table 合并进当前模块 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170` | 运行态缓存以逻辑模块为单位，传递依赖仍保留为显式模块链，而不是静态 flatten 结果 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 dependency provenance 显式记录下来，再逐步把“自动 flatten 的传递可见性”收敛成可选兼容模式 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 为 `FAngelscriptModuleDesc` 新增 `DirectImports`、`TransitiveImports`、`ResolvedImportProvenance` 与 `EImportVisibility(Direct/ReExported/TransitiveLegacy)`；第一阶段让现有 `ImportedModules` 原样保留为兼容视图。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` 只把源码中的 `import` 记入 `DirectImports`；`TransitiveImports` 改为在 Stage1 解析期根据 direct graph 推导，而不是直接写死进 source artifact。<br>3. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 引入 `bLegacyFlattenTransitiveImports=true` 默认开关：兼容模式下继续调用 today 的 `ImportModule()`；strict 模式下先只记录 provenance 和 warning，不立即改变旧脚本可见性。<br>4. 第二阶段为 opt-in 项目增加显式 `re-export` metadata（可放入 future manifest/sidecar），只有 direct import 或声明了 `re-export` 的模块才允许把下游 symbol 继续暴露给 consumer。<br>5. 把 `CombinedDependencyHash`、reload journal 和 future public-surface hash 从“无来源 XOR 聚合”升级为“按 direct edge / re-export edge 记录原因”；这样 provider 的纯实现变化、transitive module 变化和 direct API 漂移可以拥有不同的失效策略。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：legacy 模式下旧脚本继续可见传递依赖；strict 模式下未声明 `re-export` 的 transitive symbol 会给出结构化诊断；reload invalidation 能指出“为什么当前模块因为哪个 direct edge 被拉进闭包”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | L |
| 架构风险 | 真正收紧 transitive visibility 时，最容易打破今天默认可用的“顺手吃到下游 symbol”脚本；因此第一阶段必须先做 provenance 记录和诊断，strict boundary 只能 opt-in |
| 兼容性 | 向后兼容。默认保留 today 的 flatten 行为；新增 provenance ledger 与 strict/re-export 规则仅对显式开启的工程生效 |
| 验证方式 | 1. 回归现有 `import`、hot reload、precompiled cache 测试，确认 legacy 模式不变。<br>2. 构造 `A -> B -> C` 场景，验证 strict 模式下 `A` 访问 `C` 的 symbol 必须有显式 `re-export`。<br>3. 修改 `C` 的实现与 API，分别验证 `A` 的失效原因会被区分成 `transitive implementation drift` 与 `direct API drift`。 |

### Arch-SL-71：declared import 仍依赖 `GetFunctionByDecl()` 文本签名重绑定，跨模块 ABI 没有稳定 slot

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 跨模块函数导入的 ABI identity 是否稳定，能否支持精确兼容判定、版本化 provider 和低成本热替换重绑定 |
| 当前设计 | 当前 declared import 绑定仍是“`SourceModuleName + declaration string`”模型：runtime 在检查和绑定阶段反复读取 `GetImportedFunctionDeclaration()`，再调用 `GetFunctionByDecl()` 重新解析签名字符串去匹配 provider function；没有稳定的 symbol slot / export id |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4616-4643` — `ResolveDeclaredImports()` 逐个读取 `GetImportedFunctionDeclaration()` 与 `GetImportedFunctionSourceModule()`，随后 `GetFunctionByDecl(Decl)` 再 `BindImportedFunction()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4669-4695` — `CheckFunctionImportsForNewModules()` 也重复用 declaration string 做存在性校验；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp:885-914` — `asCModule::GetFunctionByDecl()` 内部先 `ParseFunctionDeclaration()`，再按 namespace/name/return type/parameter types 搜索；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp:1574-1601` — 底层 `BindAllImportedFunctions()` 同样依赖 `GetDeclarationStr()` + `GetFunctionByDecl()` 这一套文本签名路径 |
| 优点 | 不需要额外导出 manifest，直接复用 AngelScript 现有 declared import 机制；旧模块无需维护独立 ABI 表也能完成绑定 |
| 不足 | ABI identity 绑定在可解析的 declaration text 上，导致重绑定成本高且可演进性弱：provider 若只是做 namespace 调整、声明规范化、sidecar alias 或版本化映射，系统也缺少“这是同一 export slot”的一等事实；未来即使补 `PublicApiHash`、`lazy activation` 或 `mixed-version provider`，最终仍会卡在文本签名匹配上 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块执行结果被写入 `module.exports`，cache 锚点是 `moduleCache[key]`；consumer 先命中模块对象，再通过导出属性访问能力，ABI 主体是“模块 key + export property”，不是“重新解析一段函数声明字符串” | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:56-71`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:139-146`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:178-185` | 先把导出面做成显式 runtime/export object，重绑定和版本迁移可以围绕 export slot 而不是编译器 declaration parser |
| UnLua | `require(module_name)` 返回 `new_module` table，并把它写入 `loaded_modules/package.loaded`；外部依赖的是 module table 的字段面，热更也围绕 module object 提交，而不是围绕导入函数声明文本重跑 parser | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:151-170` | 模块 ABI 先绑定到逻辑模块对象，再谈具体字段/函数如何更新，兼容边界比文本声明更稳定 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 legacy declaration-string 绑定的前提下，引入显式 `export symbol manifest` 与稳定 `ApiSlot`，让 import rebinding 先走 slot，再退回文本匹配 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 新增 `FAngelscriptExportSymbol`、`FAngelscriptImportSymbolRef` 与 `FAngelscriptModuleAbiManifest`；字段至少包含 `OwnerModuleId`、`ApiSlot`、`Decl`、`PublicApiHash`、可选 `LegacyAliases`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/` 或 compile commit 结束后，为模块 public surface 生成默认 manifest：第一阶段 `ApiSlot` 可以由稳定排序后的 export 列表生成，未显式声明 manifest 的旧模块仍可自动得到 slot。<br>3. 把 `ResolveDeclaredImports()` / `CheckFunctionImportsForNewModules()` 改成“先按 `SourceModule + ApiSlot` 查 provider export，再在缺 manifest 的 legacy provider 上 fallback 到 `GetFunctionByDecl()`”；这样旧模块不需要立刻迁移，新模块可以先享受稳定 rebinding。<br>4. 让 precompiled fingerprint、reload journal 与 future version switch 一并记录 `PublicApiHash` / `ApiSlot` 对照；provider 仅实现变化但 `PublicApiHash` 未变时，consumer 不再强依赖 declaration-string 重新解析。<br>5. 对 editor diagnostics 增加 `MissingApiSlot`、`ApiHashMismatch`、`LegacyDeclFallbackUsed` 三类消息，帮助工程逐步从文本绑定迁到显式 ABI manifest。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：legacy 模块仍可通过 declaration string 绑定；带 manifest 的 provider 发生实现改动时 consumer 不会误报 import 缺失；provider 切换 alias/slot 映射时能给出结构化兼容性诊断。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | `ApiSlot` 设计如果不稳定，会比 today 的 declaration string 更难调试；第一版必须保留 clear diagnostics 和 legacy fallback，不能一次性切断旧路径 |
| 兼容性 | 向后兼容。没有 manifest 的旧模块继续走 today 的 `GetFunctionByDecl()`；只有启用 manifest/slot 的模块才进入新 ABI 绑定路径 |
| 验证方式 | 1. 回归现有 declared import 和 hot reload 测试，确认 legacy provider 行为不变。<br>2. 构造带 manifest 的 provider，在只改函数体时验证 consumer 继续命中同一 `ApiSlot`。<br>3. 构造 provider alias/slot 漂移场景，确认系统输出 `ApiHashMismatch` 或 `MissingApiSlot`，而不是泛化成普通 compile error。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-70 | `ImportModule()` 扁平化传递依赖，模块 provenance 丢失 | dependency provenance ledger + strict re-export 模式 | 高 |
| P1 | Arch-SL-71 | declared import 仍靠文本签名重绑定，缺少稳定 ABI slot | export/import ABI manifest | 高 |

---

## 架构分析 (2026-04-10 00:55)

### Arch-SL-72：threaded bootstrap 通过整条 `GameThread` task queue 等待后台编译，启动边界可重入

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本编译-加载管线在 threaded bootstrap 下是否存在稳定、不可重入的启动边界，便于插入自定义 startup phase、lazy boot host 或多 runtime bootstrap policy |
| 当前设计 | `Initialize()` 在 threaded 模式下把 `Initialize_AnyThread()` 扔到 `AnyHiPriThread`，但主线程不是纯等待，而是在循环里主动 `Broadcast OnAsyncLoadingFlushUpdate` 并 `ProcessThreadUntilIdle(GameThread)`；也就是说，脚本 runtime 仍未完成 bootstrap 时，整条 game-thread task queue 可能被提前泵入执行 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:819-857` — `Initialize()` 在 `ShouldInitializeThreaded()` 分支中先 `AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, ...)` 跑 `Initialize_AnyThread()`，随后主线程在 `while (!bInitializationDone)` 内执行 `FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast()` 和 `FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653-1655` — 对外启动完成仍只是在 `PostInitialize_GameThread()` 广播一次 `OnInitialCompileFinished`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:922-942` — `InitializeOwnedSharedState()` 直到 bootstrap 末尾才把 `ScriptEngine`、`PrimaryContext`、`PrecompiledData`、`StaticJIT` 写入共享状态。推断：等待循环期间被泵入的 game-thread task 若读取 runtime 或订阅其他 editor/runtime delegate，会落在“后台编译中但 shared state 尚未 ready”的窗口里。 |
| 优点 | 启动时不会把 game thread 完全卡死，异步加载与部分 editor 主线程任务仍可推进，当前 editor 体感较平滑 |
| 不足 | 启动等待不再是“明确 barrier”，而是“可重入窗口”；未来若要在 bootstrap 前后插入 `lint-only`、`warmup`、`BootHostPolicy`、lazy activation 预热或多 runtime 并存，扩展方无法判断哪些 game-thread 任务可以安全早到，哪些必须等 `runtime ready` 之后再执行 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnvImpl::Start()` 明确要求绑定线程访问，随后在同一调用栈内建立 `Context`、调用 `Require` 并在返回后才把 `Started = true`；未见“等待后台启动时继续泵入宿主主线程任务”的路径 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3485-3551` | 把启动完成定义成单次显式 request 的返回点，host 可在外围自由决定是否异步化，但 VM 本体不把“等待”与“继续跑任意宿主任务”混在一起 |
| UnLua | `FLuaEnv::Start()` 直接在当前线程执行 `require(StartupModuleName)`，`lua_pcall` 返回后才设置 `bStarted = true`；启动边界是同步且封闭的 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:230-253` | 先保证 env 自身的启动 barrier 清晰，再让外层定位器、editor policy 或宿主系统决定何时调用 `Start()`；runtime 内部不制造隐藏 reentrancy seam |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 threaded bootstrap 的“等待策略”从 runtime core 中显式抽出来，先保留现有 `LegacyPump` 行为，再增量加入不可重入的 `StrictBarrier` 路径 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptBootstrapBarrier`、`FAngelscriptBootstrapResult` 与 `EAngelscriptBootstrapWaitPolicy(LegacyPump/StrictBarrier)`；后台 `Initialize_AnyThread()` 只写 request-local result，完成后通过 `FEvent` 或等价同步原语发信号。<br>2. 让 `Initialize()` 的等待逻辑改为读取 `BootstrapWaitPolicy`：`LegacyPump` 完整复用今天的 `ProcessThreadUntilIdle(GameThread)` 行为，保证默认兼容；`StrictBarrier` 只等待 barrier 信号，不主动泵整条 game-thread task queue。<br>3. 如果某些 bootstrap 步骤确实需要 game thread，新增 engine-owned `EnqueueBootstrapGameThreadStep()` 或等价白名单队列，只允许显式登记的步骤在 barrier 未闭合前被执行，禁止“顺便把所有 GT task 都跑一遍”。<br>4. 让 `OnInitialCompileFinished` 与 future startup milestone 都显式携带 `BootstrapWaitPolicy` / `ReadyPhase`，这样扩展方能在日志和测试里确认自己运行在 `LegacyPump` 还是 `StrictBarrier` 模式。<br>5. 第一阶段把 `StrictBarrier` 仅开放给 testing/headless/runtime experiment，editor 默认仍走 `LegacyPump`；待 bind、tooling 和 startup participant 完成迁移后，再评估是否提升默认策略。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：`LegacyPump` 下现有启动行为不变；`StrictBarrier` 下后台编译期间不会执行额外 game-thread task；显式登记的 bootstrap GT step 仍能在 barrier 未闭合前按顺序运行。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险主要在 editor 启动体验和历史扩展点兼容性；如果直接把默认模式切成 `StrictBarrier`，某些依赖“等待时顺便跑了 GT task”的隐式路径会暴露时序问题，因此必须先双轨并行 |
| 兼容性 | 向后兼容。第一阶段默认仍是 `LegacyPump`；只有显式选择 `StrictBarrier` 的 runtime/profile 才会改变启动等待语义，现有脚本与 editor 工作流不受影响 |
| 验证方式 | 1. 回归现有 threaded init、initial compile 和 editor 启动测试，确认 `LegacyPump` 行为不变。<br>2. 新增 `StrictBarrier` 测试，在等待后台初始化时塞入一个普通 game-thread task，验证其不会被提前执行。<br>3. 新增显式 bootstrap GT step 测试，验证白名单步骤仍能在 barrier 关闭前执行且顺序稳定。 |

### Arch-SL-73：hot reload 线程与主线程之间仍靠 `volatile` flag 和共享容器交接变更，缺少显式 change-queue contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本生命周期里的“变化发现 -> reload 请求 -> compile/commit”线程边界是否是显式、安全且可扩展的，从而支持新增 metadata watch、lazy compile request、custom tooling producer |
| 当前设计 | hot reload checker thread、editor watcher 和主线程都直接读写 `FAngelscriptEngine` 的共享容器；线程协调主要依赖 `volatile bool bWaitingForHotReloadResults`，而专门的 `CompilationLock` 只用于编译诊断日志，不覆盖 hot reload 队列本身 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:410,477-478,523` — 引擎持有 `volatile bool bWaitingForHotReloadResults`、`FileChangesDetectedForReload`、`FileDeletionsDetectedForReload` 与 `CompilationLock`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1658-1700` — checker thread 在后台轮询 `CheckForFileChanges()`，完成后直接把 `bWaitingForHotReloadResults = false`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2778` — 主线程 `CheckForHotReload()` 直接 `Append/Empty` 同一批共享数组，并在尾部再次写回 `bWaitingForHotReloadResults = true`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2867-2894` — `CheckForFileChanges()` 会在后台线程里 `Empty()` 并重新填充 `FileChangesDetectedForReload`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5017-5022` — `CompilationLock` 仅在 `LogAngelscriptError()` 中保护 message callback；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89` — editor watcher 也直接写 `Engine.LastFileChangeDetectedTime`、`FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`。 |
| 优点 | 当前实现短路径、低抽象，主线程最终仍掌握 `PerformHotReload()` 的提交权；对纯单 producer 场景来说，调试门槛低 |
| 不足 | 生命周期输入通道不是 first-class queue，而是多个线程/模块共享一组容器的隐式约定；这会直接限制未来把 manifest/settings/profile change、lazy activation miss、外部工具请求也并入同一生命周期系统。推断：仅靠 `volatile` 无法为 `TArray/TSet` 的所有权和时序提供稳定 contract，扩展 producer 数量一旦上升，丢事件、重复消费或测试不可复现的问题会更难治理 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块加载与 reload 都走显式 request：`require()` 在 `localModuleCache/moduleCache` 上以当前请求为边界执行，`forceReload()` 只是给目标模块打标记；失败时立即回滚当前模块 cache entry，没有后台线程直接改共享热更数组 | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:105-195,205-219` | 把“需要 reload 什么”建成显式模块请求和 cache 状态，而不是让后台线程直接搬运文件列表；这样 loader、HMR 和宿主工具都可以安全共用同一协议 |
| UnLua | 热更入口是显式 `reload_modules(module_names)`；它先构造 `tmp_modules`，进入 sandbox，逐模块 `load/execute`，全部成功后才 `update_modules(...)` 提交，失败则 `sandbox.exit()` 退出 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-176,553-601` | 先把变化描述成显式模块集合，再在单条受控路径里验证和提交；没有额外后台 producer 直接改 live queue，生命周期边界清晰 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 用 engine-owned change event queue 替代 today 的共享数组直写；第一阶段保留现有公开队列作为 debug snapshot，真实所有权先收口到单一 drain 点 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptLifecycleChangeEvent`、`EChangeOrigin(RuntimePoller/EditorWatcher/Test/Manual)` 与 `TQueue<FAngelscriptLifecycleChangeEvent, EQueueMode::Mpsc>`；event 至少包含 `Origin`、`Action(Added/Modified/Removed/NeedFullReload)`、`FilenamePair`、`Timestamp`、可选 `ReasonTag`。<br>2. 把 `StartHotReloadThread()`、`CheckForFileChanges()` 和 `QueueScriptFileChanges()` 全部改成“只 enqueue event，不直接写 `FileChangesDetectedForReload/FileDeletionsDetectedForReload`”；主线程 `CheckForHotReload()` 先 drain queue 到局部 batch，再应用 today 的 `0.2s` rename/delete window 与 `SoftReloadOnly/FullReload` 逻辑。<br>3. 用 `TAtomic<bool>` 或 `TAtomic<uint32>` worker state 替代 `volatile bool bWaitingForHotReloadResults`，明确表达 `Idle/Scanning/ReadyToDrain` 等状态；不要再让 queue 所有权依赖布尔约定。<br>4. 第一阶段保留 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 作为只读镜像或 dump/test 兼容视图：主线程在 drain 后再回填这些数组，保证现有 state dump 和调试工具不立刻失效。<br>5. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h/.cpp` 新增 `RequestLifecycleChange(...)` 兼容入口，让 future manifest/settings/profile change、lazy compile miss、commandlet request 都复用同一队列，而不是继续旁路写内部数组。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 增加三类回归：runtime poller 与 editor watcher 并发产生日志时不丢 event；删除/重命名窗口行为与今天一致；旧测试读取 debug snapshot 时仍能看到相同的 file pair 集合。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` |
| 预估工作量 | M |
| 架构风险 | 风险不在队列本身，而在兼容层：state dump、测试 helper 和若干现有调用点已经把公开数组当契约；因此第一阶段必须保留 debug snapshot，避免一次性打断 editor/test 工具链 |
| 兼容性 | 向后兼容。默认 rename/delete 窗口、tick 触发时机和 `PerformHotReload()` 主线程提交语义保持不变；变化主要是事件所有权从“共享数组直写”收口到 engine-owned queue |
| 验证方式 | 1. 回归现有 hot reload、directory watcher 和 state dump 测试，确认默认行为不变。<br>2. 新增并发 producer 测试，同时从 runtime poller 和 editor watcher 注入变化，验证 event 不丢失也不重复。<br>3. 新增 compatibility 测试，确认旧的 debug snapshot/测试 helper 在 drain 后仍能读到与 today 等价的 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 结果。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-73 | hot reload 输入通道仍靠共享容器和 `volatile` flag 交接 | engine-owned change queue / explicit producer contract | 高 |
| P2 | Arch-SL-72 | threaded bootstrap 通过整条 GT task queue 等待后台编译 | bootstrap barrier / wait policy 分层 | 中 |

---

## 架构分析 (2026-04-10 01:04)

### Arch-SL-74：`AddFile()` 的 positional-bool source contract 已在删除文件热更路径里发生语义错位

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | source acquisition 输入契约是否类型安全，能否稳定表达 `File/Deleted/InMemory/Artifact` 等来源语义 |
| 当前设计 | `FAngelscriptPreprocessor::AddFile()` 仍用两个位置布尔 `bLoadAsynchronous` / `bTreatAsDeleted` 编码来源语义，而 hot reload 删除路径已经把 `bTreatAsDeleted` 作为第三个实参传入，实际命中了“异步读盘”分支而不是“删除 tombstone”分支 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:15` — `AddFile(const FString&, const FString&, bool bLoadAsynchronous = false, bool bTreatAsDeleted = false)` 仍是 positional bool API；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2448-2452` — hot reload 删除路径计算 `bTreatAsDeleted` 后直接调用 `Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, bTreatAsDeleted)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:101-111` — 第三个参数为 `true` 时只会设置 `File.bLoadAsynchronous = true`，不会把 `RawCode` 置空；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:149-157` — 后续 `PerformAsynchronousLoads()` 还会对该“已删除文件”执行 `OpenAsyncRead()`，只有 size/read 失败后才退化成空源码 |
| 优点 | API 表面简单，历史调用点少时上手成本低；同步文件读取仍能以最短路径接入预处理器 |
| 不足 | 这不再只是“类型不安全”，而是已出现主路径语义错配：删除文件被建模成一次失败的异步 I/O，而不是 first-class `Deleted` source。未来若继续加 `Generated/Manifest/Remote/Cache` 语义，调用点错位只会更隐蔽，且 diagnostics、reload ledger、source identity 都会先拿到错误的输入类别 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | loader contract 只暴露显式 `Search`/`Load`，模块 miss 与 load failure 都通过返回值/异常表达；`require()` 再基于本次请求结果决定是否继续执行 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-24`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4091-4115`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-131` | “没有模块”“能找到但加载失败”“加载成功”是显式状态，不靠布尔位置约定传递 |
| UnLua | `FCustomLuaFileLoader` 明确返回 `bool + Data + ChunkName`；filesystem loader 命中失败时直接返回 `0`，命中成功时再 `LoadString()`，没有把“删除/缺失”混进另一个参数槽位 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22-34`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-594`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:597-641` | source state 应该是类型化返回结果，而不是“调用者记住第三个还是第四个 bool 代表什么” |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 source 输入从 positional bool 升级为显式 `SourceSpec`，先修正已存在的删除热更错配，再为 future `Manifest/Generated/Remote` 来源留出稳定入口 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h/.cpp` 新增 `FAngelscriptSourceLoadSpec`，字段至少包含 `RelativePath`、`AbsolutePath`、`ESourceState(File/Deleted/InMemory/Artifact)`、`ELoadMode(Sync/Async/ProvidedBuffer)`、可选 `SourceText/DebugPath`。<br>2. 新增显式入口 `AddSource(const FAngelscriptSourceLoadSpec&)`，并提供 `MakeDeletedSource(...)`、`MakeDiskFileSync(...)`、`MakeDiskFileAsync(...)` 辅助构造；旧 `AddFile()` 仅保留为 legacy 包装。<br>3. 立即修正 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2448-2452` 的 hot reload 删除调用，改为传 `Deleted` source，而不是继续把删除语义塞进第三个布尔槽位。<br>4. 在 `PerformHotReload()` 与 future source resolver 中统一消费 `SourceSpec` 的 `ESourceState`，让删除、缺失、生成模块和真实磁盘文件在 diagnostics/reload journal 里拥有不同状态词，而不是都先退化成 I/O 失败。<br>5. 第一阶段保留 `AddFile()` 向后兼容，但在内部只允许它创建 `File + Sync/Async` 两类 spec；新增来源一律禁止再走布尔重载。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：删除文件 hot reload 不再尝试 `OpenAsyncRead()`；legacy `AddFile()` 同步/异步路径行为保持不变；`Deleted` source 会进入结构化 reload diagnostics，而不是被记录为普通读盘失败。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | S |
| 架构风险 | 风险主要在兼容层：仓内已有调用点可能继续误用 legacy `AddFile()`；因此第一阶段应把新 spec 先用于 hot reload/delete、manifest 和测试入口，再逐步压缩布尔重载的使用面 |
| 兼容性 | 向后兼容。旧 `AddFile()` 继续存在并映射到 `File` source；只有内部 hot reload 删除路径与 future 新来源会切到 typed spec，现有脚本用户不需要修改 |
| 验证方式 | 1. 构造删除脚本文件的 hot reload 场景，确认不会再尝试异步打开已删除文件。<br>2. 回归现有初编译与普通 hot reload 测试，确认 legacy 文件来源行为不变。<br>3. 新增一个 `Deleted` source 单测，验证其 diagnostics/reload state 显式标记为删除而非 load failure。 |

### Arch-SL-75：`PerformAsynchronousLoads()` 没有 completion fence 或 request identity，当前 async read 不是可扩展的 source fetch phase

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本 source acquisition 的异步阶段是否拥有完整的完成、取消和状态汇报契约，能否支撑 lazy compile、reload 合并和自定义 loader |
| 当前设计 | 预处理器把异步读取状态塞进 `FFile` 的 `volatile bool` 与裸 `IAsyncRead*` 指针；`PerformAsynchronousLoads()` 发起请求后只做一次单遍检查，仍在加载的文件仅 `Sleep(0.001f)` 一次，随后 `Preprocess()` 就立即进入 `ParseIntoChunks()` |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:156-173` — `FFile` 只保存 `volatile bool bLoadAsynchronous` 和三根裸请求指针，没有 `RequestId`、`CompletionState`、`Cancel` 或 `ErrorKind`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:141-210` — `PerformAsynchronousLoads()` 发起 size/read request 后，在第二个循环里若仍在加载仅 `Sleep(0.001f)`，并未循环等待所有请求进入终态；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:224-229` — `Preprocess()` 在 `PerformAsynchronousLoads()` 返回后立刻 `ParseIntoChunks(File)`；推断：当前 async branch 没有真正的 completion fence，parser 理论上可观察到尚未 materialize 完成的 `RawCode`。 |
| 优点 | 实现轻量，能在文件数量较少、I/O 极快时提供一定 best-effort overlap，而不需要额外 scheduler 或 future 类型 |
| 不足 | 这条“异步”路径既没有正确性屏障，也没有 request ownership：无法表达 `Ready/Missing/Deleted/Failed/Cancelled` 等终态，也无法在未来 `lazy compile`、reload 合并、profile 切换时取消或去重 in-flight source fetch。换言之，它更像一次脆弱的 I/O 微优化，而不是可扩展的生命周期阶段 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 一次 `require()` 内同步完成 `search -> load -> execute`：`LoadModule()` 只有在 `ModuleLoader->Load(Path, Data)` 返回成功后才把内容交给 JS 侧继续执行，失败则直接抛异常并回滚当前 cache entry | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4100-4125`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-146`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:156-191` | source fetch 的完成边界属于当前模块请求本身，不会留下“后台还在读，但 parser 已经往下跑”的灰区 |
| UnLua | searcher 在一次 `require()` 栈内同步完成 chunk 获取与 `LoadString()`；只有 `xpcall(func, ...)` 成功后才把结果写入 `loaded_modules/package.loaded`，加载 miss 则回退到下一 searcher 或 `origin_require` | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-611`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-169` | 先让 source fetch 在 request 内部到达终态，再决定是否提交到 runtime cache；没有 request identity，就不开始讨论 commit |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 today 的裸 async I/O 分支升级为 request-scoped `source fetch` 层，显式建模完成态和取消语义；默认 file backend 仍可复用现有 I/O API，但 parser 只能消费已终态结果 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h/.cpp` 新增 `FAngelscriptSourceFetchRequest`、`FAngelscriptSourceFetchResult` 与 `ESourceFetchState(Ready/Missing/Deleted/Failed/Cancelled)`；result 至少包含 `RequestId`、`ModuleId`、`DebugPath`、`Payload/ErrorMessage`。<br>2. 用 `IAngelscriptSourceFetchHandle` 或等价封装替代 `volatile bool + raw pointer` 三件套；默认 `FileSystemAsyncFetchHandle` 内部可以继续用 `OpenAsyncRead()`，但必须提供 `IsCompleted()` / `Cancel()` / `GetResult()`。<br>3. 把 `PerformAsynchronousLoads()` 改成两段：`BeginSourceFetches()` 只发起请求，`WaitForSourceFetches()` 或 scheduler drain 循环等待所有 request 进入终态，再允许 `ParseIntoChunks()` 开始；不要再用一次性 `Sleep(0.001f)` 伪装 completion。<br>4. 第一阶段让同步读盘直接产出 `Ready` result，保证 legacy 路径零行为变化；异步 file backend 仅在 `WaitForSourceFetches()` 达成后把 `Payload` materialize 到 `FFile`。<br>5. 在 future lifecycle scheduler 中复用同一 fetch handle：如果 reload request 被更新版本覆盖，可显式 `Cancel()` 旧 fetch；如果多个模块请求同一 artifact，可按 `RequestId/ModuleId` 做去重或共享。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：模拟慢速 async backend 时 parser 不会在 `Ready` 前运行；取消 superseded request 不会向下一轮编译泄漏 stale `RawCode`；默认同步 backend 行为与当前完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险在于引入 request handle 后，预处理器与 future scheduler 的边界会更正式，短期内会暴露历史上依赖“文件读取几乎立刻完成”的隐式假设；因此第一阶段应先让默认 backend 走同步完成、异步 backend 只在测试/实验中启用 |
| 兼容性 | 向后兼容。默认同步读盘与旧 `AddFile()` 行为不变；新的 request-scoped async fetch 只为 typed source API、lazy compile 和 future remote loader 提供基础，不要求现有项目立刻切换 |
| 验证方式 | 1. 注入一个可控延迟的 async backend，验证 `ParseIntoChunks()` 只会在所有 fetch 到达终态后开始。<br>2. 新增取消测试，确认被 supersede 的 fetch 不会把过期内容写回下一轮编译。<br>3. 回归现有初编译与热更测试，确认默认同步 backend 结果无差异。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-SL-74 | `AddFile()` positional bool 已在删除文件热更路径里错配语义 | typed source spec / hot reload 删除路径修正 | 高 |
| P1 | Arch-SL-75 | async source fetch 缺 completion fence 与 request identity | request-scoped source fetch handle | 高 |

---

## 架构分析 (2026-04-10 01:10)

### Arch-SL-76：文件系统枚举顺序仍直接泄漏到 compile/boot 顺序，automatic-import 路径下没有稳定 catalog order

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译-加载-执行管线的顺序是否可重复，从而支撑稳定的启动副作用、可复现的分析结果和跨平台一致性 |
| 当前设计 | 当前只有 root 列表做了 project-first/plugin-sorted；进入单个 root 后，文件与子目录枚举顺序没有显式排序，`automatic imports` 路径又会直接沿用 `Files` 当前顺序继续向下编译和启动 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1342-1361` — root 级仅对 plugin roots 调 `Sort()`，再把 project root 插到首位；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1955-1972` — `FindScriptFiles()` 直接按 `FindFiles(LocalFiles, ...)` 结果追加文件，并把子目录先塞进 `TSet<FString>(LocalDirs)` 后立刻遍历，没有后续稳定排序；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2004-2014,2074-2079` — `FindAllScriptFilenames()` 与 `InitialCompile()` 会按这条枚举顺序把所有文件喂给 `Preprocessor.AddFile(...)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:75-83,228-239` — `GetModulesToCompile()` 默认按 `Files` 当前顺序输出模块，只有关闭 `automatic imports` 时才会做一次 `ProcessImports()` 重排；推断：在 `automatic imports` 或无显式 boot metadata 的场景里，模块 compile/boot 顺序仍会受文件系统返回顺序影响。 |
| 优点 | 现有实现短路径、扫描成本低，且在显式 `import` 较完整的工程里通常能得到可用的拓扑顺序 |
| 不足 | 一旦模块依赖启动副作用顺序、测试 discovery 顺序或 compile diagnostics 稳定性，未排序的文件/目录枚举就会成为隐式输入；这会削弱 `AnalyzeOnly`、lazy activation、boot plan 和跨平台复现能力，因为“没有改代码但顺序变了”并不受任何正式 contract 约束 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 模块搜索顺序由 `Search()` 的固定目录/扩展尝试顺序定义，实际执行顺序再由 `require()`/`tmpRequire(url)` 这条显式请求链决定，而不是由全量扫盘顺序决定 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-120`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:129-176` | 把“候选查找顺序”和“真实执行顺序”都做成显式协议，避免文件系统枚举副作用泄漏到模块生命周期 |
| UnLua | `package.searchers` 通过 `AddSearcher()` 按索引插入固定链，`LoadFromFileSystem()` 再按 `package.path` pattern 顺序尝试，真正的执行顺序由 `require(module_name)` 显式驱动 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:614-667`；`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:147-170` | 先固定 searcher/pattern 顺序，再让入口模块的 `require` 链决定后续加载，不把目录遍历顺序当作 lifecycle contract |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 discovery 阶段生成稳定的 `source catalog order`，把“未声明依赖时的默认顺序”从文件系统副作用提升为可审计的显式规则 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptSourceCatalogEntry`，至少记录 `RootPriority`、`RelativePath`、`DiscoveryOrdinal`、`bDerivedFromExplicitImportOrder`。<br>2. 把 `FindScriptFiles()` 改为先对 `LocalFiles` 做稳定排序，再对 `LocalDirs` 先排序后去重；不要再通过 `TSet<FString>(LocalDirs)` 直接丢失遍历顺序。<br>3. 让 `FindAllScriptFilenames()` 返回 `SourceCatalog`，`InitialCompile()`/hot reload 都只按 catalog 追加 source；`FAngelscriptPreprocessor::GetModulesToCompile()` 在没有显式 import/boot 约束时输出稳定的 `DiscoveryOrdinal` 顺序。<br>4. 第一阶段保持现有 project-first/plugin-sorted 规则不变，只把 root 内部顺序显式化；第二阶段再让 future `BootPlan`、`AnalyzeOnly` 和 lazy loader 直接消费同一份 catalog。<br>5. 在 editor/dev 模式新增一次性诊断：如果某模块没有显式 `import`/`boot_after` 约束，却依赖 catalog 顺序才能通过启动或测试，则输出 warning，引导工程迁移到显式 boot metadata。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加三类回归：同一脚本集在不同文件返回顺序下产出相同 catalog；`automatic imports` 模式下 compile/boot 顺序稳定；显式 `import` 或 future `boot_after` 仍能覆盖默认 catalog 顺序。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 风险不在排序实现，而在“今天依赖未定义顺序的项目”会被显式化；因此第一阶段更稳妥的做法是先固定顺序并增加 warning，而不是立即强制所有工程写 boot metadata |
| 兼容性 | 基本向后兼容。project-first 与 plugin root 排序保持不变；可能变化的是过去未定义的 root 内部遍历顺序。若个别工程恰好依赖这种未定义顺序，需要用显式 `import`/`boot_after` 固化意图 |
| 验证方式 | 1. 在测试里注入两组不同的文件枚举顺序，确认 `SourceCatalog`、`GetModulesToCompile()` 和默认 boot 顺序一致。<br>2. 回归现有 startup/hot reload/test discovery 测试，确认没有显式顺序依赖的工程行为不变。<br>3. 构造一个仅靠扫描顺序才能成功的样例，验证新 warning 能准确指出顺序依赖来源。 |

### Arch-SL-77：模块与 reload key 仍缺少统一的路径规范化层，source identity 依赖多套原始字符串表示

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本模块的 source identity、reload key 与 cache key 是否基于统一的 canonical path contract，从而支持稳定的模块查找、热更和 future manifest/cache 扩展 |
| 当前设计 | 当前 discovery、directory watcher、module naming 和 reload state 各自操作原始路径字符串，但没有公共 normalization/canonicalization 层；`ModuleName` 推导和热更 key 因此依赖调用点碰巧给出同一格式的路径 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:86-89` — `FilenameToModuleName()` 只把 `.as` 去掉并把 `/` 替换成 `.`，没有处理 `\\`、`.`/`..` 或大小写规范化；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1960-1963` — discovery 直接把 `SearchDirectory / FoundFile` 与 `RelativeRoot / FoundFile` 写入 `FFilenamePair`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:8-16,47-66` — directory watcher 只用 `StartsWith(RootPath)` + `MakePathRelativeTo()` 生成 `RelativePath` 并直接塞进 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload`，没有显式标准化；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2873-2894` — `CheckForFileChanges()` 又把 `Filename.RelativePath` 直接作为 `FileHotReloadState` key；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3043-3049` — `GetModuleByFilename()` 用 root 相对路径重建模块名时同样只替换 `/`；推断：不同来源若给出不完全一致的路径表示，module lookup、reload state 与 future cache key 就缺少一个共同的 canonical basis。 |
| 优点 | 当前实现避免了额外的路径对象和转换层，纯文件系统工程在“所有调用点都恰好使用同一格式”时可以直接工作 |
| 不足 | 一旦接入 directory watcher、manifest entry、generated source、patch layer 或外部工具请求，路径表示差异就会直接污染模块 identity；这会阻碍精确 hot reload、artifact cache key 和 diagnostics 聚合，因为系统没有正式定义“哪一个 path string 才是这个 source 的稳定键” |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | JS 侧先对 specifier 做 `normalize()`，C++ 侧再用 `PathNormalize()` 折叠 `.`/`..` 和路径片段；真正进入 `moduleCache` 的 key 是规范化后的 `fullPath` | `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:15-20,139-146`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:21-61,92-120` | 先统一 canonical path，再让 search/load/cache/reload 全部围绕同一 key 运作，避免每个调用点各自拼字符串 |
| UnLua | `LoadFromFileSystem()` 先把 `module.name` 规范为目录路径，再按 `package.path` pattern 组合出 `FullPath` 并 `ConvertRelativePathToFull()` 后加载；chunk identity 也随这条显式路径链传递 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:597-639` | 即使没有单独的 path key 类型，loader 也要显式定义“模块名如何变成稳定的 full path”，而不是让 watcher/runtime/loader 各自持有原始字符串 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `source discovery -> watcher -> preprocessor -> reload/cache` 全链路引入统一的 `canonical path key`，把显示路径与内部 key 分离 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptPathKey` 或等价工具函数，至少产出 `CanonicalAbsolutePath`、`CanonicalRelativePath`、`ModulePathKey`；内部统一处理分隔符、`.`/`..`、root 相对化与大小写策略。<br>2. 让 `FindScriptFiles()`、`TryMakeRelativeScriptPath()`、`QueueScriptFileChanges()`、`GetModuleByFilename()` 与 `FAngelscriptPreprocessor::AddFile()/AddSource()` 全部先走同一 canonicalization helper，再写入 `FFilenamePair`、`ModuleName` 和 hot reload 队列。<br>3. 把 `FileHotReloadState`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 和 future artifact/module cache 改为使用 `CanonicalRelativePath` 或 `ModulePathKey`，不要继续混用原始 `RelativePath`。<br>4. 第一阶段保留 today 的显示路径用于日志和 diagnostics，但内部 key 一律改读 canonical form；若发现同一 source 通过不同原始路径映射到同一 canonical key，应输出冲突诊断而不是静默覆盖。<br>5. 与 future manifest/resolver 对接时，让 manifest entry、generated source 和 patch layer 也直接提供 `CanonicalRelativePath/ModulePathKey`，这样非文件系统来源不必再伪装成某种特定斜杠格式。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 增加三类回归：watcher 与 scan 给出不同路径格式时仍命中同一模块；reload/cache key 不再因为表示差异产生重复项；diagnostics 继续显示原始可读路径但内部命中 canonical key。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` |
| 预估工作量 | M |
| 架构风险 | 风险主要在兼容层：现有调试日志、测试 helper 和部分工具可能把原始 `RelativePath` 当成事实来源；因此第一阶段应保留 display path，仅把内部 key 收口为 canonical form |
| 兼容性 | 向后兼容。对现有脚本作者无语法影响；变化主要在内部 key 统一。少数依赖原始路径格式的测试/工具需要改为同时接受 display path 与 canonical key |
| 验证方式 | 1. 构造同一脚本以不同路径表示进入 watcher/scan 的场景，确认只生成一个 hot reload/cache key。<br>2. 回归现有 file watcher、hot reload、debug diagnostics 测试，确认显示路径不变而内部命中变稳定。<br>3. 增加 manifest/in-memory source 测试，验证非磁盘来源也能生成与文件来源一致的 canonical module key。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-76 | 文件系统枚举顺序泄漏到 compile/boot 顺序 | stable source catalog / deterministic discovery order | 高 |
| P1 | Arch-SL-77 | 缺少统一路径规范化层，source identity 与 reload key 分裂 | canonical path key / shared normalization contract | 高 |

---

## 架构分析 (2026-04-10 01:23)

### Arch-SL-78：hot reload 观察面仍是全 root 扫描，lazy/module-scoped 生命周期无法缩小 steady-state watch cost

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 变化观察层是否已经围绕“当前真正活跃/已加载的模块集合”建模，从而为 lazy compile、按模块激活和多 runtime profile 提供可伸缩的 steady-state 成本 |
| 当前设计 | 当前 runtime poller 每轮都对 `AllRootPaths` 做全量 `*.as` 扫描，editor watcher 也会把 root 下任意 `.as` 变化直接推入 reload 队列；观察面绑定的是“脚本根目录全集”，不是“当前 runtime 已加载模块集” |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:401-408,477-480` — hot reload state 只有 `RelativePath -> LastChange` 与两组 file queue，没有独立的 watched-module / observed-source 集合；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1999-2015` — `FindAllScriptFilenames()` 每次遍历 `AllRootPaths` 递归收集全部 `*.as`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2873-2895` — `CheckForFileChanges()` 对扫描到的每个文件逐个比较时间戳并入队；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:57-66,79-86` — editor watcher 对 root 内任意 `.as` 修改或新目录下全部 `.as` 都直接排入 primary engine reload |
| 优点 | eager-all 工程里行为简单，新增脚本文件不需要先注册就能被发现；host project 与 plugin root 的覆盖面也足够完整 |
| 不足 | watch cost 与脚本总量、root 数量线性绑定，lazy activation 即使落地也无法自然缩小 steady-state 观察面；未激活 plugin/private package 的改动同样会搅动 reload 队列，多 runtime/profile 并存时也缺少“哪个 runtime 真的关心这份 source”的一等归属 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | watcher 不是全量扫盘，而是由 `JsEnv` 在源码真正被加载后回调 `OnSourceLoaded(InPath)`，再按目录注册 watch 并把单个已加载文件记入 `WatchedFiles`；后续目录变化只会检查这批 watched file | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:141-146`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-83` | 先把“谁已经进入 runtime”建成观察面，再决定 watch 哪些文件；这样 lazy/module-scoped lifecycle 的收益能传导到 steady-state reload 成本 |
| UnLua | 热更轮询只遍历 `loaded_module_times`，也就是已加载模块的时间戳账本；未进入 `loaded_modules/package.loaded` 的脚本不会进入默认 reload 扫描面 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:112-118,610-623` | 观察面先绑定到 loaded module ledger，再根据模块名回查磁盘时间；即使仍是时间戳方案，作用域也已经是模块级而非全 root 级 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 today 的 `watch-all-roots` 兼容模式下，新增 `observed source set`，让 poller/watcher 围绕“当前 runtime 真正在意的模块与 source”工作 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 新增 `FAngelscriptObservedSourceRecord` 或等价结构，至少记录 `ModuleKey`、`CanonicalPath`、`SourceRootId`、`ObservationReason(InitialCompile/Activated/Pinned/Tooling)`、`LastObservedFingerprint`。<br>2. 让 `InitialCompile()`、future `EnsureModuleCompiled()`、`ActivateModule()` 和 residency layer 在模块 materialize/激活时登记 observed source；eager 默认模式下仍可一次性把全部模块加入，保证旧行为不变。<br>3. 把 `CheckForFileChanges()` 改成优先扫描 observed source 集，而不是每轮重新 `FindAllScriptFilenames()`；仅当 `WatchScopeMode=AllRoots` 时才回退到当前全 root 扫描。<br>4. 将 `AngelscriptDirectoryWatcherInternal.cpp` 的排队逻辑改为先查询 `ObservedSourceSet`：`ObservedOnly` 模式下，只把已观察 source 与其显式 root-metadata 变化入队；对未观察 root 可只记录“profile 可能过期”的轻量事件，等待显式激活时再解析。<br>5. 提供兼容设置 `LifecycleWatchScope=AllRoots/ObservedOnly`，默认仍为 `AllRoots`；当项目显式启用 lazy activation 或 module residency 后，再切到 `ObservedOnly` 以释放 steady-state 成本。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 增加三类回归：默认 `AllRoots` 模式行为与当前一致；`ObservedOnly` 模式下只修改未激活模块不会立即触发 reload；模块首次激活后其 source 会被登记观察，并能在后续编辑中正常 hot reload。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` |
| 预估工作量 | M |
| 架构风险 | 风险在于“未观察 source 的变化何时可见”必须定义清楚；若直接切到 observed-only 而没有 profile/version 补偿，可能把真实变化延后到首次激活时才暴露 |
| 兼容性 | 向后兼容。默认继续 `AllRoots`；只有显式选择 `ObservedOnly` 或后续 lazy/module residency 功能启用时，观察面才会缩到活跃模块集 |
| 验证方式 | 1. 回归现有 poller、directory watcher 和 hot reload 测试，确认默认模式不变。<br>2. 新增 lazy/observed-only 测试，验证未激活模块的改动不会打断当前 runtime。<br>3. 新增激活后观察测试，确认模块进入活跃集后其后续编辑仍能正常触发 hot reload。 |

### Arch-SL-79：变更判定仍以时间戳为主，缺少内容指纹与 no-op reload 抑制

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | reload 触发信号是否足够高质量，能否区分“真实源码变化”和“时间戳抖动/保存未改内容/同步工具触碰” |
| 当前设计 | 当前 poller 的 `FHotReloadState` 只记 `LastChange` 时间戳；只要磁盘时间变化或 watcher 收到 `.as` 事件，就会把文件加入 reload 队列，随后直接展开 dirty closure 并进入编译主链 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:401-404` — `FHotReloadState` 只有 `FDateTime LastChange`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2878-2894` — `CheckForFileChanges()` 仅以 `GetTimeStamp()` 与 `LastChange` 是否不同决定是否入队；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:55-66,79-86` — editor watcher 收到改动/新增目录事件后直接排入 `FileChangesDetectedForReload`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2746-2755` — 只要队列非空或删除窗口到期，就会组 batch 进入后续 reload 逻辑，没有内容相等短路 |
| 优点 | 成本低、实现简单，不需要额外 hash I/O 或额外缓存；对“保存即强制重编”一类粗粒度工作流也比较直接 |
| 不足 | `touch`、同步工具重写时间戳、保存未改内容等 no-op 事件都会触发 dirty closure 和编译事务；随着 module catalog、lazy activation、artifact provider 引入，这类噪音会越来越明显地吞掉生命周期分层收益 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | watcher 为每个 watched file 保存 `FMD5Hash`，目录变化后重新 hash，只有 hash 真变才回调 reload；进入 inspector 热更新前还会取旧 `scriptSource` 与新源码比较，相同则直接 `skip` | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:42-49,75-80`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:81-85` | 先在观察层做内容去重，再在执行层做最后一道 no-op 防线，避免时间戳抖动直接扩散成 reload 事务 |
| UnLua | 虽然仍基于修改时间，但 `loaded_module_times` 至少把时间戳账本收敛到模块级，并只对这批模块比对新旧时间 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:112-118,612-618` | 即便短期还保留时间戳，也应把它放进显式模块账本中，而不是仅靠 file queue 的瞬时事件 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 hot reload 观察层补 `content fingerprint` 与 `manual force` 双轨协议：自动事件默认去重，显式人工请求仍可强制 bypass |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h/.cpp` 扩展 `FHotReloadState`，至少增加 `LastContentHash`、可选 `LastResolvedInputFingerprint` 与 `LastObservedTimestamp`；不要再只保存时间戳。<br>2. 把 `CheckForFileChanges()` 改成两段：先用时间戳做 cheap candidate 过滤，再对 candidate 计算 `RawContentHash` 或 `SourceProviderFingerprint`；只有 fingerprint 变化时才把文件/模块加入 reload 队列。<br>3. 将 `AngelscriptDirectoryWatcherInternal.cpp` 的 watcher 路径也统一接到同一 `ProbeChangedSource(...)` helper，避免“poller 有去重、watcher 没去重”形成双重语义。<br>4. 新增 `EChangeReason(AutoDetected/ManualForce/MetadataChanged)` 或等价字段；`ManualForce` 明确绕过 fingerprint 抑制，给确实依赖“touch 触发”的调试/工具链留兼容口。<br>5. 与 future manifest/search profile/provider 链对接时，让 fingerprint 可以提升为“模块输入指纹”，不局限于原始文件字节；这样 sidecar metadata 或 package entry 未变内容时也能稳定短路。<br>6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 增加三类回归：保存未改内容不会触发 reload；显式 `ManualForce` 仍可绕过 dedupe；metadata/source provider fingerprint 变化能在无源码字节变化时正确触发失效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/`<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` |
| 预估工作量 | S-M |
| 架构风险 | 风险不在 hash 计算本身，而在“自动去重是否会吞掉部分团队依赖的 touch-to-reload 流程”；因此必须给手动强制重载保留明确 bypass 通道 |
| 兼容性 | 基本向后兼容。自动事件只会减少 no-op reload；若项目确实依赖时间戳即触发，可通过 `ManualForce` 或兼容开关回到旧行为 |
| 验证方式 | 1. 构造保存未改内容与时间戳被外部工具刷新两类场景，确认不会触发 reload。<br>2. 新增 `ManualForce` 回归，验证人工强制请求仍能进入编译主链。<br>3. 对比启用前后的 reload 日志和 dirty closure 数量，确认噪音事件下降但真实改动不漏报。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-SL-78 | hot reload 观察面仍是全 root 扫描，无法随 lazy/module-scoped 生命周期收缩 | observed source set / watched-scope 分层 | 高 |
| P2 | Arch-SL-79 | 变更判定仍以时间戳为主，no-op reload 无法被抑制 | content fingerprint dedupe / manual-force bypass | 中 |
