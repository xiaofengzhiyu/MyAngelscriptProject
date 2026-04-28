# ModuleStructure 架构审查

---

## 架构分析 (2026-04-08 14:01)

### Arch-MS-01：Bind shard 拓扑通过缓存与动态模块加载隐藏了上行依赖

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定分片模块的声明方式是否让依赖拓扑可见、可验证 |
| 当前设计 | checked-in 的静态模块图只有 `AngelscriptRuntime -> AngelscriptEditor -> AngelscriptTest` 这一组三层 UE 模块，但 `GenerateNativeBinds()` 会在 `Source/` 下生成 `ASRuntimeBind_*` / `ASEditorBind_*` 模块，把名字写入同一份 `BindModules.Cache`，随后由 `AngelscriptRuntime` 在引擎初始化时统一 `LoadModule(...)`。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18` 只声明了 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005` 把运行时绑定按 10 个 class 一组切成 `ASRuntimeBind_*`，并在 `:1043` 开始生成 `ASEditorBind_*`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1021` 与 `:1047` 把 runtime/editor shard 都追加进同一份 `BindModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1069` 到 `:1073` 的 `AngelscriptNativeBinds` 聚合 `Build.cs` 生成路径已被注释掉。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166` 在 `Source/<ModuleName>/` 下写出新模块；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1176` 对 editor shard 额外注入 `AngelscriptEditor` 公共依赖。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1477` 从插件目录读取 `BindModules.Cache`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1483` 到 `:1487` 对缓存内所有模块名统一执行 `LoadModule(...)`。 |
| 优点 | shard 粒度由生成器控制，绑定代码可以按 class bucket 并行编译；静态 `Build.cs` 图本身仍是无环的，`AngelscriptEditor.Build.cs:19` 只向下依赖 `AngelscriptRuntime`，`AngelscriptTest.Build.cs:48` 只在 editor target 下附加 `AngelscriptEditor`。 |
| 不足 | 真实拓扑不再由 `.uplugin + Build.cs` 单独表达，而是分散在生成器、cache 和运行时初始化路径里；`ASEditorBind_* -> AngelscriptEditor` 这条依赖会经由共享 cache 变成隐藏的 `AngelscriptRuntime -> ASEditorBind_* -> AngelscriptEditor` 上行边；cache 与源码生成耦合后，clean build、增量 build、打包场景的失效模式不够直观。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 显式声明 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector` 三类模块；runtime/editor/program 的职责都在声明层可见，没有通过 runtime cache 反向加载 editor 模块。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20` | 生成链可以是 `Program` 模块，但拓扑仍保持 declarative；调试/收集工具不需要 runtime 在初始化期“猜测”要加载哪些 editor 模块。 |
| puerts | 把 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 全部挂进 `.uplugin`；运行时 core 依赖由 `Puerts -> JsEnv -> WasmCore/ParamDefaultValueMetas` 显式表达，editor/generator 模块再向下依赖 runtime。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13` | 可以把生成器和 runtime core 都拆成显式模块，但仍然保持“editor 只向下依赖 runtime”的方向性，不把运行时发现逻辑塞进共享 cache。 |
| UnrealCSharp | `UnrealCSharpCore`、`UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 都是显式模块；`UnrealCSharpEditor` 私有编排 generator/compiler/core，而上层 runtime 只依赖 `CrossVersion + UnrealCSharpCore`。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25` | workflow module 可以很多，但它们的所有权和依赖方向是静态可见的；如果需要生成/编译并行度，优先把复杂度放进显式模块边界，而不是 runtime 初始化链。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 bind shard 带来的并行编译收益，但把 shard 类型、装载责任和依赖方向从共享 cache 里显式化。 |
| 具体步骤 | 1. 先把当前单一 `BindModules.Cache` 拆成 `RuntimeBindModules.cache` 与 `EditorBindModules.cache`，让 `FAngelscriptEngine` 只加载 runtime shard，editor shard 改由 `FAngelscriptEditorModule` 的 editor-only 启动路径装载。<br>2. 在 `GenerateNativeBinds()` 生成 manifest 时记录 shard 类型与依赖目标，避免用“统一字符串列表”隐式承载 runtime/editor 语义。<br>3. 用显式 owner 收口 shard 拓扑。增量方案优先恢复一个 `AngelscriptNativeBinds` / `AngelscriptNativeBindsEditor` 聚合入口，或增加 prebuild 脚本在 UBT 前稳定生成 shard，而不是继续依赖运行时 cache 发现。<br>4. shard 数量先不调整，等 manifest 分离完成后再基于真实编译时间决定是否从当前 10-class bucket 改为更粗粒度分片。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，以及新增的 shard manifest 读写代码 |
| 预估工作量 | M |
| 架构风险 | 需要处理旧 cache 与新 manifest 的迁移；如果项目文件生成链对动态模块目录有隐式假设，需要同步校验 UBT / IDE project generation 行为。 |
| 兼容性 | 对现有 script API 和已生成绑定名保持兼容；变化集中在模块发现和装载流程，对插件使用者属于构建链透明修改。第一次切换时可能需要清理旧 `BindModules.Cache`。 |
| 验证方式 | 1. clean editor build 后重新生成 native binds，确认 runtime 只装载 runtime shard。<br>2. editor target 下验证 editor shard 仍能注册 editor-only 绑定。<br>3. non-editor / cooked target 验证不再尝试加载 `ASEditorBind_*`。<br>4. 对比 build log，确保 shard 并行编译能力未退化。 |

### Arch-MS-02：Runtime 公共边界仍混入 editor/support 语义，Test 模块对白盒内部结构强耦合

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | runtime、editor、test 三层边界是否足够清晰，能否支持后续模块抽取与依赖反转 |
| 当前设计 | `AngelscriptRuntime` 在 editor target 下把 `UnrealEd` 与 `EditorSubsystem` 加入 `PublicDependencyModuleNames`，同时 runtime 源码内部仍承载 editor-only preprocessor / subsystem glue；`AngelscriptTest` 通过 mirrored include path 与直接相对路径 include 访问 runtime 内部实现，因此 test 层依赖的是 runtime 的目录结构而不是稳定的测试支持面。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:67` 到 `:73` 在 `Target.bBuildEditor` 下把 `UnrealEd`、`EditorSubsystem` 加到 runtime 的公共依赖。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:28` 到 `:30` 直接 include `EditorSubsystem.h`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp:19` 到 `:21` include `Editor.h` 与 `EditorSubsystem.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp:43` 到 `:55` 在 runtime 绑定层直接处理 `UEditorSubsystem`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:13` 到 `:21` 把 `Core`、`Debugger`、`Dump`、`Internals`、`Native`、`Preprocessor`、`ClassGenerator` 全部加入测试模块私有 include path。<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp:4` 到 `:5` 通过相对路径直接 include `AngelscriptRuntime/Binds` 内部头。<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:1` 直接 include `AngelscriptRuntime/Core/AngelscriptBinds.h`。<br>`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp:3` 到 `:8` 直接 include `ClassGenerator` 与 third-party `as_*` 头。 |
| 优点 | runtime 内部的 editor-only 分支可以直接复用同一套类型系统与绑定设施；white-box tests 容易覆盖 `ClassGenerator`、`Binds`、`Preprocessor` 等内部行为，定位回归速度快。 |
| 不足 | 任何在 editor target 下依赖 `AngelscriptRuntime` 的下游模块都会经由 public deps 继承 editor 语义；runtime 的 shipping boundary 与 editor/tooling boundary 没有在模块层显式切开；`AngelscriptTest` 对 runtime 目录布局和私有头的强耦合，会直接抬高后续提取 `ClassGenerator` / `Preprocessor` / `Debugging` 子模块的成本。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime 公开依赖保持在 `Core/CoreUObject/Engine/Slate/InputCore/Lua`，editor 场景只把 `UnrealEd` 放进 private deps；测试则放在单独的 `UnLuaTestSuite` 插件里，依赖 `UnLua` 而不是镜像其内部目录树。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:62`<br>`Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:18`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33` | runtime 可以保留 editor-aware 分支，但应尽量把 editor 依赖收在 private boundary；测试插件最好依赖受控的 runtime surface，而不是默认可见全部内部目录。 |
| puerts | runtime core 被拆成 `WasmCore`、`JsEnv`、`Puerts`，editor 相关能力单独留给 `PuertsEditor` 和 `DeclarationGenerator`；即便 runtime 中允许 editor build，`UnrealEd` 仍停留在 private deps。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15`<br>`Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:56`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:23`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21` | 如果 runtime 已经承载太多 editor/tooling 逻辑，优先把它们抽成显式 editor/generator 模块，而不是继续增加 runtime 的公共依赖面。 |
| UnrealCSharp | 把 `UnrealCSharpCore`、`UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler` 拆成显式层级；上层 runtime 只依赖 `CrossVersion + UnrealCSharpCore`，`UnrealEd` 也只存在于 private deps。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:50`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25` | 即便 workflow module 数量增加，也能换来更清楚的依赖方向；generator/compiler/test hook 应先成为显式 owner，再讨论更深层的 runtime 抽象。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先缩窄 `AngelscriptRuntime` 的公共 editor 依赖，再把 editor-only helper 与 white-box test hook 从 runtime 主边界里拆出去。 |
| 具体步骤 | 1. 先做最小变更：审计 `AngelscriptRuntime` 公共头是否真的暴露 editor 类型；对当前已定位的 `Preprocessor`、`Bind_Subsystems` 这类 `.cpp` 级使用，优先把 `UnrealEd` / `EditorSubsystem` 从 `PublicDependencyModuleNames` 下沉到 `PrivateDependencyModuleNames`。<br>2. 新增 `AngelscriptRuntimeEditorSupport`（名字可调整）模块，逐步承接 `ClassGenerator`、editor-only preprocessor glue、`UEditorSubsystem` 绑定等不应进入 shipping runtime boundary 的实现。<br>3. 新增 `AngelscriptTestSupport` 或 `AngelscriptRuntimeTestHooks` 模块，对外暴露受控的 white-box test API，把 `AngelscriptTest` 对 `../../AngelscriptRuntime/...` 相对路径 include 的依赖迁移过去。<br>4. 等 test hook 稳定后，再决定 `AngelscriptTest` 是否继续维持单模块，还是拆成 public integration tests 与 internal white-box tests 两层。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`，以及新增的 `AngelscriptRuntimeEditorSupport` / `AngelscriptTestSupport` 模块文件 |
| 预估工作量 | L |
| 架构风险 | 模块抽取会触发 include path、UHT、link order 和自动化测试启动方式的联动调整；如果一次迁移范围过大，容易把 editor-only 回归与 test hook 回归混在一起。 |
| 兼容性 | 对 script 用户保持向后兼容，前提是不改动现有 runtime public API；变化主要落在 C++ 模块边界与测试基础设施，对外部项目属于低兼容性风险。 |
| 验证方式 | 1. 让一个只依赖 `AngelscriptRuntime` 的下游模块在 editor target 下编译，确认不再被动继承 `UnrealEd` 公共依赖。<br>2. editor、game、cook target 全量编译通过。<br>3. `AngelscriptTest` 回归通过，并确认主要 white-box 测试已经改为走 `TestSupport`/hook surface。<br>4. 抽取后再次检查模块图，确认依赖方向仍是 `Runtime <- EditorSupport <- Editor/Test`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-01 | bind shard 的声明方式与隐藏上行依赖 | 结构性重构 | 高 |
| P1 | Arch-MS-02 | runtime/editor/test 边界收口与依赖下沉 | 结构性重构 | 高 |

---

## 架构分析 (2026-04-08 14:13)

### Arch-MS-03：可选 gameplay 能力被并入 Runtime Core，依赖方向难以向外反转

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `GameplayAbilities`、`GameplayTasks`、`EnhancedInput` 这类可选 UE 能力是否应该属于 core runtime 的固定边界 |
| 当前设计 | 当前静态 DAG 没有模块循环，但方向是 `AngelscriptRuntime -> GameplayAbilities / GameplayTasks / EnhancedInput`：插件描述符强制启用这些插件，`AngelscriptRuntime.Build.cs` 直接声明依赖，运行时源码与绑定文件也直接承载 GAS / EnhancedInput API。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:35`、`Plugins/Angelscript/Angelscript.uplugin:40`、`Plugins/Angelscript/Angelscript.uplugin:44` 把 `StructUtils`、`EnhancedInput`、`GameplayAbilities` 设为固定插件依赖。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:40`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:62`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:63`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:64` 把 `StructUtils`、`EnhancedInput`、`GameplayAbilities`、`GameplayTasks` 直接挂到 runtime 模块依赖。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h:5` 在 public 头里直接 include `Abilities/Tasks/*`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp:4` 直接注册 GAS 异步库绑定。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:1` 直接 include `EnhancedInputComponent.h` 并注册 `UEnhancedInputComponent` 绑定。 |
| 优点 | gameplay 常用能力开箱即用，脚本侧不需要再额外安装桥接模块；当前绑定生成和运行时注册链路也更简单。 |
| 不足 | core runtime 不再是 feature-neutral 基座；新增或裁剪某个可选领域时必须回改 `AngelscriptRuntime` 的 `.uplugin`、`Build.cs` 和源码；后续如果要把绑定分片或脚本 API 做成可选包，依赖方向会卡在 `core -> feature` 而不是 `feature -> core`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLua` core runtime 只保留 `Core/CoreUObject/Engine/Slate/InputCore/Lua` 这类基础依赖；扩展能力用独立插件承载，例如 `LuaSocket` 作为单独 `.uplugin`，再由扩展模块私有依赖 `UnLua`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:62`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:16`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:32`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:41` | 可选能力应该作为 leaf plugin / leaf module 依赖 core，而不是把 core runtime 扩成“所有能力的并集”。 |
| puerts | 把 runtime 拆成 `WasmCore`、`JsEnv`、`Puerts` 三层，再把 `DeclarationGenerator`、`ParamDefaultValueMetas` 放到 editor/program 层；上层 `Puerts` 只 public 依赖 `JsEnv`，扩展职责沿着上层模块堆叠。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15`<br>`Reference/puerts/unreal/Puerts/Puerts.uplugin:28`<br>`Reference/puerts/unreal/Puerts/Puerts.uplugin:33`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21` | 即使模块数量增加，也优先把能力叠在上层模块，而不是继续加粗 core runtime。 |
| UnrealCSharp | `UnrealCSharpCore` 与 `UnrealCSharp` 分层，generator/compiler 再单独成模块；`EnhancedInput` 只出现在上层 `UnrealCSharp` 的 private deps，中间还有 `CrossVersion`、`UnrealCSharpCore` 作为缓冲层。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:37`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38` | 当某项能力无法完全独立成插件时，也应先把“core 基座”和“高层 feature 接缝”拆开，再决定哪些模块向外暴露。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 GAS / EnhancedInput 绑定与 feature glue 从 `AngelscriptRuntime` 主边界中抽成 leaf 模块，再决定是否进一步迁移脚本可见的 wrapper 类型。 |
| 具体步骤 | 1. 新增 `AngelscriptGameplayBinds`、`AngelscriptEnhancedInputBinds` 两个 runtime leaf 模块，依赖 `AngelscriptRuntime` 和对应 UE 模块；第一阶段只迁移 `Bind_AngelscriptGASLibrary.cpp`、`Bind_UEnhancedInputComponent.cpp`、`Bind_FInputBindingHandle.cpp` 这类 bind 注册实现。<br>2. 在 `Angelscript.uplugin` 中把这些模块声明为默认启用的 runtime 模块，保持现有项目开箱即用；但把未来新增的 feature bind 一律落到 leaf 模块，不再继续塞进 `AngelscriptRuntime`。<br>3. 等 bind 迁移稳定后，再评估 `UAngelscriptAbilityAsyncLibrary`、`UAngelscriptAbilityTaskLibrary` 等脚本可见类型是否需要第二阶段迁移。若迁移会改变 `/Script/<Module>` 包名，则单独走一次有破坏性评估，而不是和第一阶段绑定拆分捆在一起。<br>4. 同步让 `AngelscriptUHTTool` 或 bind manifest 识别这些 leaf 模块，避免以后新增 feature module 还要回头修改 runtime core 的 Build.cs 才能被生成链看见。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp`，以及新增的 leaf 模块 `Build.cs` / `Module.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果在第一阶段就移动 `UCLASS` / `UFUNCTION` 所在模块，可能改变反射包名和脚本加载路径；因此应该先只迁移 bind 实现和 feature glue，把破坏性变更拆到第二阶段。 |
| 兼容性 | 第一阶段对现有脚本和现有项目应保持向后兼容，因为脚本可见类型与默认启用策略不变；只有第二阶段若迁移 `UAngelscriptAbility*` 这类类型，才会引入需要显式升级说明的兼容性影响。 |
| 验证方式 | 1. editor/game target 全量编译通过，并确认 runtime core 不再直接编译迁出的 bind 文件。<br>2. 运行现有 GAS / EnhancedInput 相关测试，确认脚本侧绑定行为不变。<br>3. 对新增 leaf 模块做一次 disable/enable 演练，确认模块关闭时只影响对应 feature bind，不影响 core runtime 初始化。 |

### Arch-MS-04：`AngelscriptUHTTool` 是真实的生成模块边界，但它没有进入插件模块拓扑

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 代码生成与 UHT 扩展是否拥有显式、可追踪的模块边界 |
| 当前设计 | `.uplugin` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 UE 模块；真正负责函数表生成的 `AngelscriptUHTTool` 以 `.NET` sidecar 形式存在，并通过扫描 `AngelscriptRuntime.Build.cs` 文本来推导支持模块集合。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18` 到 `Plugins/Angelscript/Angelscript.uplugin:33` 只声明三个 UE 模块，没有 `Program` / generator 模块。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj:7`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj:14` 表明它是输出到 `Binaries/DotNET/UnrealBuildTool/Plugins/AngelscriptUHTTool/` 的 `.NET` library。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21` 到 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27` 把 exporter 绑定到 `ModuleName = "AngelscriptRuntime"`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:336`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:347`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:355`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:364` 通过读取 `AngelscriptRuntime.Build.cs` 文本提取依赖模块。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:387` 到 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:409` 还需要先从 UHT session 里反推 `AngelscriptRuntime.Build.cs` 路径。 |
| 优点 | sidecar 不会扩大运行时模块面，也避免把 generator 逻辑编进游戏进程；现有三模块交付面保持稳定。 |
| 不足 | 生成链与模块图脱节，新增模块时必须依赖“Build.cs 文本格式不变”这一隐式约定；未来如果引入新的 leaf 模块或可选功能模块，`AngelscriptUHTTool` 无法从模块拓扑直接得知谁参与生成。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把 `UnLuaDefaultParamCollector` 作为 `.uplugin` 内显式 `Program` 模块声明；其 `Build.cs` 直接面向 `Programs/UnrealHeaderTool/Public` 和 `UnLua/Private`，tooling 拓扑在描述符层即可见。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:36`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:32`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:47` | generator / collector 应该有自己的 owner 模块，而不是完全寄生在 runtime module 名字和 Build.cs 文本上。 |
| puerts | `.uplugin` 同时声明 `DeclarationGenerator`（Editor）和 `ParamDefaultValueMetas`（Program）；`DeclarationGenerator` 明确依赖 `JsEnv`、`Puerts`，`ParamDefaultValueMetas` 明确依赖 `Core/CoreUObject`。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:28`<br>`Reference/puerts/unreal/Puerts/Puerts.uplugin:33`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:35`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:37` | toolchain 参与方应该由模块图显式表达，这样 editor/runtime/program 责任才能分别被追踪。 |
| UnrealCSharp | `ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 都是 `.uplugin` 可见模块；同时 `UnrealCSharpCore.build.cs` 自己写 `UnrealCSharp_Modules.json` 作为结构化索引，而不是靠解析另一个模块的 `Build.cs` 文本。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:29`<br>`Reference/UnrealCSharp/UnrealCSharp.uplugin:49`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:100`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:142`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:171` | 即便继续保留 sidecar，也应该先把“生成模块清单”结构化，避免 build 规则文本成为事实上的 schema。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `AngelscriptUHTTool` 的输入契约从“解析 `AngelscriptRuntime.Build.cs` 文本”升级为结构化 manifest，再决定是否把它提升为 `.uplugin` 中的显式 `Program` 模块。 |
| 具体步骤 | 1. 在插件根或 `Source/AngelscriptUHTTool/` 下新增单一来源的 `AngelscriptCodeGenModules.json`（或等价的结构化 C# 数据文件），显式列出 runtime modules、editor-only modules、future leaf modules；`AngelscriptUHTTool` 只读取这个 manifest。<br>2. 让 manifest 由 `AngelscriptEditor` 生成 bind shard 时同步写出，或由一个轻量 UBT step 生成；同时把 `factory.AddExternalDependency(...)` 从 `AngelscriptRuntime.Build.cs` 改成这个 manifest 路径。<br>3. 等 manifest 稳定后，再评估是否增加一个显式 `Program` 模块描述符，类似 `UnLuaDefaultParamCollector` / `ParamDefaultValueMetas`，至少让模块拓扑能在 `.uplugin` 层看见“生成器”这一职责。<br>4. 若后续引入 `AngelscriptGameplayBinds`、`AngelscriptEnhancedInputBinds` 之类 leaf 模块，只需要更新 manifest，不再要求修改 runtime core 的 `Build.cs` 文本解析规则。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、新增的 `AngelscriptCodeGenModules.json`（或等价结构化输入） |
| 预估工作量 | M |
| 架构风险 | 主要风险是 manifest 失效或生成时机不对，导致 UHT 读取到陈旧模块清单；需要把 invalidation 规则和 `AddExternalDependency` 绑定好。 |
| 兼容性 | 对现有脚本和运行时行为无影响，变更集中在生成链与构建链；若第二阶段把 generator 正式声明为 `Program` 模块，需要同步验证现有 UBT/UHT 插件注册流程，但不应影响脚本 API。 |
| 验证方式 | 1. 修改模块清单后触发 UHT，确认 `AS_FunctionTable_*.cpp` 能按 manifest 重新生成。<br>2. 对 `AngelscriptRuntime.Build.cs` 做纯格式化改动，确认生成链不再受影响。<br>3. 新增一个试验性 leaf 模块到 manifest，验证 `AngelscriptUHTTool` 能识别它而无需修改文本解析逻辑。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-03 | 可选 gameplay 能力对 core runtime 的硬耦合 | 依赖反转 + 模块边界调整 | 高 |
| P2 | Arch-MS-04 | UHT / codegen sidecar 未进入显式模块拓扑 | 生成链结构化 + toolchain 显式化 | 中 |

---

## 架构分析 (2026-04-08 14:29)

### Arch-MS-05：`AngelscriptEditor` 同时承担 editor shell 与 legacy bind codegen owner，导致模块所有权双轨化

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 模块是否只承载编辑器集成，还是同时背负生成/调试/遗留工作流 owner |
| 当前设计 | 当前只有一个显式 editor 模块 `AngelscriptEditor`，但它既负责日常 editor shell 行为，也保留了一整套 legacy native bind generator；而菜单文案已经明确 UHT pipeline 才是主路径。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:24-33` 只声明一个 editor 模块 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 让同一模块同时 public 依赖 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`AssetTools`，又 private 依赖 `Settings`、`LevelEditor`、`ContentBrowser`、`ToolMenus`、`ToolWidgets`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-416` 的 `StartupModule()` 同时注册 class reload、source navigation、directory watcher、settings、debug asset helper 和 tools menu。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722-730` 把 `GenerateNativeBinds()` 暴露为 `Legacy Native Bind Generator (Debug Only)` 菜单项，并明确写出 `AngelscriptUHTTool pipeline is the primary path`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1207` 却仍在同一模块内实现 `GenerateNativeBinds()`、`GenerateBindDatabases()`、`GenerateNewModule()`、`GenerateBuildFile()`，包含扫描 `UClass`、生成 shard 模块和写 `Build.cs` 的完整链路。 |
| 优点 | editor 相关入口集中在一个模块中，遗留 bind 生成器对调试人员来说容易找到，也不需要再维护额外的工具模块加载顺序。 |
| 不足 | 模块 owner 被拆成“两条事实上的子职责”却没有在拓扑上显式化：一条是 editor UX/shell，一条是 legacy codegen/debug tool。结果是任何想复用或隔离 legacy generator 的后续改造，都必须依赖完整的 editor UI 栈；同时主代码生成链已转到 `AngelscriptUHTTool`，但遗留生成链仍驻留在 `AngelscriptEditor`，形成双轨 owner。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 把 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector` 分成 runtime / editor / program 三个 owner；`UnLuaEditor` 负责 editor 依赖，`UnLuaDefaultParamCollector` 专门承载 UHT collector。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 即便功能总量不大，也会把 editor shell 与 program/tooling owner 拆开，避免单一 editor 模块兼任所有职责。 |
| puerts | `.uplugin` 显式区分 `PuertsEditor`、`DeclarationGenerator`、`ParamDefaultValueMetas`；编辑器集成、声明生成和 UHT/program 扩展分别成为独立模块。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16-45`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44` | 生成器即使和 editor 强相关，也仍然作为独立 owner 暴露；这样 editor 模块只需要“编排”而不是“内嵌整套生成实现”。 |
| UnrealCSharp | `.uplugin` 里把 `UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler` 都声明为独立模块；`UnrealCSharpEditor` 通过 private deps 组合 generator/compiler，而不是把这些实现直接塞进 editor shell。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | 当 editor 需要同时接 UI、watcher、generator、compiler 时，优先把 generator/compiler 拆成 leaf module，再由 editor 做 orchestration。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptEditor` 收敛为 editor shell/orchestrator，把 legacy bind generator 迁成显式的工具模块 owner。 |
| 具体步骤 | 1. 新增 `AngelscriptLegacyBindGenerator`（`Type=Editor` 或 `Developer`，名字可调整）模块，迁移 `GenerateNativeBinds()`、`GenerateBindDatabases()`、`GenerateNewModule()`、`GenerateBuildFile()`、`GenerateSourceFilesV2()` 及其 helper。<br>2. `AngelscriptEditor` 仅保留菜单注册与 editor UX；菜单点击时通过 `FModuleManager::LoadModuleChecked` 调起新模块，而不是继续静态持有生成实现。<br>3. 如果 legacy generator 仍需 headless 调试，追加一个 commandlet 或 console-command owner 到新模块，避免再把“调试工具入口”绑回 UI 生命周期。<br>4. 等新模块稳定后，再评估是否把它默认关闭，仅在 developer/debug 配置启用；这样主路径就只剩 `AngelscriptEditor + AngelscriptUHTTool` 两个明确 owner。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`，以及新增的 `Plugins/Angelscript/Source/AngelscriptLegacyBindGenerator/*` |
| 预估工作量 | M |
| 架构风险 | 主要风险是迁移静态 helper 时引入 include path、module export 宏和命令注册顺序问题；如果仍有外部代码直接调用 `FAngelscriptEditorModule::GenerateNativeBinds()`，需要保留 forwarding shim。 |
| 兼容性 | 对脚本 API 和主 UHT pipeline 无直接影响；对 C++ 层若存在直接调用 legacy generator 的工具脚本，需要短期保留兼容入口。整体属于低向后兼容风险的模块所有权整理。 |
| 验证方式 | 1. editor 启动后确认 directory watcher、settings、tool menus、debug asset helper 行为保持不变。<br>2. 手动触发 legacy generator，确认新工具模块能够生成与旧实现一致的输出。<br>3. 关闭新工具模块时，确认主 UHT pipeline 与常规 editor 使用不受影响。 |

### Arch-MS-06：bind shard 以固定 10-key bucket 直接映射 UE 模块，模块数量与身份都受枚举顺序牵引

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind 并行化是否必须通过“继续增加 UE 模块数”来表达，以及分片身份是否稳定 |
| 当前设计 | 生成器先从 `TObjectRange<UClass>()` 建 runtime/editor class DB，再把 `GetKeys(Keys)` 的结果按固定 `ModuleCount = 10` 切成 `ASRuntimeBind_*` / `ASEditorBind_*` 模块；每个 shard 还会继续生成大量 `Bind_<Class>.cpp` 翻译单元。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1088-1159` 在 `GenerateBindDatabases()` 里遍历 `TObjectRange<UClass>()`，按 package 名把 class 放入 runtime/editor DB。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1058` 读取 `GetRuntimeClassDB().GetKeys(Keys)` / `GetEditorClassDB().GetKeys(Keys)` 后，未见任何 `Keys.Sort()`，直接按 `ModuleCount = 10` 切 bucket，并用起始索引命名 `ASRuntimeBind_<index>`、`ASEditorBind_<index>`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166-1207` 为每个 bucket 在 `Source/<ModuleName>/` 下写出新的 `Build.cs`、module header、module cpp。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1276` 生成的 shard `Build.cs` 还会把 bucket 内 package 名转换成 `PrivateDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1366-1431` 同时又为每个 class 单独落一个 `Bind_<Class>.cpp` 文件。 |
| 优点 | 通过 bucket + per-class `.cpp` 的组合，理论上可以把绑定编译拆成较细粒度的 action；不同 package 的 bind 也能按 shard 隔离，减少单模块文件数。 |
| 不足 | 当前模块粒度同时承担了“并行编译单元”和“拓扑节点”两种职责。代码里没有对 `Keys` 做排序，这意味着分片身份稳定性依赖 `UClass` 枚举与 `TMap::GetKeys()` 返回顺序，存在模块名漂移与增量编译 churn 风险。更关键的是，生成器本身已经按 class 产出独立 `.cpp`，并行度并不只来自“多 16 个 UE 模块”；继续用新模块表达每个 bucket，会把 project file、Build.cs、module load surface 和依赖维护复杂度一起放大。这里关于“模块名漂移风险”的判断，是根据源码缺少显式排序后的推断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 内的 `FUnLuaIntelliSenseGenerator` 把输出写到 `Intermediate/IntelliSense/<ModuleName>/<File>.lua`；生成粒度可以按 module/file 组织，但不会为每个 batch 新建 UE 模块。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233` | 可以把输出拆得很细，但仍把“生成文件的粒度”与“UE 模块边界”解耦。 |
| puerts | `DeclarationGenerator` 在固定模块中按 package 缓存 declaration 输出，`ParamDefaultValueMetas` 也是固定 `Program` 模块，最后只写 `InitParamDefaultMetas.inl` 等生成文件。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:530-558`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1551-1559`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:37-50`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-126` | 生成内容可以按 package/class 分组，但 owner module 数量保持稳定，避免“数据分片数量 = 模块数量”。 |
| UnrealCSharp | `UnrealCSharpEditor` 在固定模块图下串起 `FClassGenerator`、`FStructGenerator`、`FEnumGenerator`、`FAssetGenerator`；生成 pass 很多，但并不为每个 pass 或 batch 新增 UE 模块。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-282`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49` | 扩大编译/生成规模时，优先增加固定 owner 下的 pass/file 数，而不是让模块图随数据量线性膨胀。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 shard 分片改成确定性，再把“并行编译粒度”从 UE 模块数量中解耦出来，收敛为固定 bind owner 模块。 |
| 具体步骤 | 1. 第一阶段只做稳定性修复：在 `GetKeys(Keys)` 后显式 `Keys.Sort()`，并把 bucket 命名从“起始索引”改成稳定 hash 或 package-based label，避免小范围 class 变更导致大面积模块名漂移。<br>2. 第二阶段恢复固定 owner：重新启用一个或两个聚合模块，例如 `AngelscriptNativeBinds` / `AngelscriptNativeBindsEditor`，让生成器继续产出 `Bind_<Class>.cpp` 或按 package 分组的 `.cpp`，但不再为每 10 个 key 新建一个 UE 模块。<br>3. 若担心单模块编译退化，优先在聚合模块上关闭 unity 或为 generated binds 单独调节 unity 策略，而不是继续增加 module surface。<br>4. manifest 与 loader 迁移期间，保留旧 shard 名到新聚合 owner 的兼容映射一段时间，确保缓存切换可控。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及恢复或新增的 `AngelscriptNativeBinds` / `AngelscriptNativeBindsEditor` 模块文件 |
| 预估工作量 | M |
| 架构风险 | 如果直接一步收掉 shard 模块，可能影响现有缓存、IDE project generation 和旧脚本化构建流程；因此必须先修稳定性，再做 owner 收敛。unity/no-unity 策略也需要基于真实编译时间回归。 |
| 兼容性 | 对脚本 API 和 `RegisterBinds(EOrder::Late)` 的运行时语义应保持兼容；变化主要影响构建图、生成缓存和本地自动化脚本。如果有工具直接引用旧 shard 模块名，需要在迁移期提供映射或清缓存说明。 |
| 验证方式 | 1. 连续两次在相同源码状态下生成 bind，确认模块名与输出目录稳定不变。<br>2. 新增或删除少量可绑定 class 后，确认只影响局部输出，不再触发大面积 shard 名重排。<br>3. 对比聚合前后的 full rebuild / incremental build 时间、solution/module 数量与加载日志，确认编译并行度收益仍在而管理复杂度下降。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-05 | `AngelscriptEditor` 的 editor shell / legacy codegen 双重 owner | 模块所有权拆分 | 高 |
| P1 | Arch-MS-06 | bind shard 的分片稳定性与模块粒度耦合 | 结构性重构 + 构建图收敛 | 高 |

---

## 架构分析 (2026-04-08 14:42)

### Arch-MS-07：`AngelscriptEditor` 的 public header contract 与 `Build.cs` 暴露边不一致

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptEditor` 是否把真正的 public API 与 implementation-only 依赖分清 |
| 当前设计 | `AngelscriptEditor.Build.cs` 把 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools` 一并抬进 `PublicDependencyModuleNames`，但 `Public/` 头文件实际暴露的更多是 editor subsystem、tool menu、asset registry 与 runtime query 这类接口；同时部分真正出现在 public 头里的模块并没有在 public deps 中显式表达。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-26` 定义了当前 public deps。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:3-4` 公开头直接依赖 `EditorSubsystem.h` / `Editor.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:3` 公开头依赖 `EditorSubsystemBlueprintLibrary.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:3-6` 公开头依赖 `UICommandList`、`AssetRegistry/AssetData.h`、`ToolMenuDelegates.h`、`ToolMenuSection.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:3-10` 公开头依赖 `AssetRegistry/*` 和 `Core/AngelscriptEngine.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:367-380` 仅 private 实现使用 `DirectoryWatcher`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:510-516` 仅 private 实现使用 `KismetCompiler` / `FKismetEditorUtilities`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h:4-5`、`:34-35`、`:57-58`、`:79-80` 仅 private 实现使用 `AssetTools`。 |
| 优点 | 现阶段 editor 模块内部开发很省事，`AngelscriptTest` 或后续 editor helper 基本不会因为缺依赖而卡在模块声明层。 |
| 不足 | public deps 与实际 public 头不对齐，造成两类问题同时存在：一类 implementation-only 依赖被放大成对下游可见的 public edge；另一类真正出现在 public 头里的依赖又没有被明确声明成 public contract。未来一旦拆 editor 子模块、引入外部 consumer，依赖继承会既过宽又不稳定。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把几乎所有 editor 依赖都留在 `PrivateDependencyModuleNames`，包括 `UnrealEd`、`UMGEditor`、`BlueprintGraph`、`DirectoryWatcher`、`ToolMenus` 与 `UnLua` 本身。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:37-45`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-85` | 先把 editor 模块当作 implementation owner，而不是默认对下游导出一整套 editor workflow 依赖。 |
| puerts | `PuertsEditor` 的 public edge 仍然偏宽，但声明生成被拆到独立的 `DeclarationGenerator` 模块，避免继续把 generator 责任叠在主 editor surface 上。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16-37`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-40` | 即使暂时保留较宽的 editor edge，也应优先把 workflow owner 拆出去，避免 public contract 持续膨胀。 |
| UnrealCSharp | `UnrealCSharpEditor` 只把 `Core`、`UnrealEd`、`DirectoryWatcher`、`CollectionManager` 留在 public deps，`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion` 全部留在 private deps。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-64` | editor shell 对外只暴露最小壳层，把 generator/compiler/runtime orchestration 作为 private implementation 细节。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先做一次 `AngelscriptEditor/Public/**/*.h` 的 contract audit，再让 `Build.cs` 跟随真实公开头，而不是跟随“当前实现顺手可编译”的状态。 |
| 具体步骤 | 1. 逐个核对 `Public/` 头文件，把真正 header-visible 的依赖补成显式 public contract；按当前证据，`ToolMenus`、`AssetRegistry` 至少需要被重新审视。<br>2. 先下沉已确认只在 private 实现中使用的依赖：`DirectoryWatcher`、`Kismet`、`AssetTools` 应先迁回 `PrivateDependencyModuleNames`；`BlueprintGraph` 需要在全模块 grep 与编译验证后决定是否一并下沉。<br>3. 如果 `BlueprintImpact`、`ScriptEditorMenuExtension` 本来就不是想提供给外部 C++ 模块的稳定 API，把它们移入 `Private/` 或新建 `AngelscriptEditorExtensions` leaf module，进一步收窄 `AngelscriptEditor` 的 public edge。<br>4. 增加一个最小 dummy consumer 模块，只依赖 `AngelscriptEditor` 并 include 当前 public 头，确保它仅凭声明好的 public deps 就能独立编译。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/*`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h`，以及可能新增的 `AngelscriptEditorExtensions` 模块 |
| 预估工作量 | M |
| 架构风险 | 若外部 C++ 模块已经隐式依赖当前 accidental public deps，下沉依赖后会暴露编译错误；因此需要先补 dummy consumer，再做逐项回归。 |
| 兼容性 | 对 script 用户无直接影响。对 C++ 扩展方，如果他们 include 了现在的 public 头并依赖未声明的传递依赖，短期内可能需要补自己的 `Build.cs` 或跟随新的模块边界。 |
| 验证方式 | 1. `AngelscriptEditor`、`AngelscriptTest` 全量编译通过。<br>2. dummy consumer 只依赖 `AngelscriptEditor` 时，能够编译所有保留在 `Public/` 的头。<br>3. 迁移后再次 grep，确认 `DirectoryWatcher`、`AssetTools`、`Kismet` 不再出现在 public contract 中。 |

### Arch-MS-08：`AngelscriptRuntime` 直接导出内部目录与 vendored VM 源码，模块边界过于通透

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptRuntime` 是否把稳定 runtime API、内部实现和 upstream AngelScript 私有结构隔离在不同模块边界内 |
| 当前设计 | `AngelscriptRuntime` 目前既是脚本 runtime hub，又是 vendored AngelScript source 的公开 owner：`Build.cs` 直接把 module root、`Core/`、`ThirdParty/angelscript/source` 和 third-party 根目录加入 public include path，结果 editor/test 可以直接 include `Core/*`、`Binds/*`、`ClassGenerator/*`，甚至直接 include `source/as_*`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-22` 把 `ModuleDirectory`、`Core`、`ThirdParty/angelscript/source`、`ThirdParty/angelscript` 暴露为 public include path。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:6-9` 与 `:38-39` 直接 include `AngelscriptEngine.h`、`Binds/Bind_FGameplayTag.h`、`ClassGenerator/ASClass.h`、`AngelscriptBinds.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:9` 公开头直接 include `Core/AngelscriptEngine.h`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:18-20` 直接 include raw `as_scriptengine.h` / `as_generic.h`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp:8-12` 与 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp:6-9` 直接 include `source/as_*` 头。 |
| 优点 | runtime、editor、white-box tests 共用一套头文件体系，调试、上游同步和深度测试都很直接，不需要先搭独立 facade 或 adapter。 |
| 不足 | 从模块图上看不出哪些是稳定 runtime API、哪些是内部实现、哪些是 upstream VM 私有结构；任何依赖 `AngelscriptRuntime` 的下游模块都可以顺手越过边界直连 raw AngelScript internals。这会抬高 VM 升级、runtime 抽取、测试隔离和未来 core 替换的成本。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 通过单独的 `Lua` external module 持有第三方 Lua 构建与头文件；`UnLua` 只 public 依赖 `Lua`，`UnLuaEditor` 若需要内部细节，则通过 private include path 访问 `UnLua/Private`。 | `Reference/UnLua/Plugins/UnLua/Source/ThirdParty/Lua/Lua.Build.cs:29-46`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-57`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:37-45` | 第三方 VM 所有权与上层 runtime 所有权分开，editor 访问内部实现也不会顺手把整个 runtime 目录导出成 public surface。 |
| puerts | `WasmCore` 负责 wasm/third-party 头与基础能力，`JsEnv` 再 public 依赖 `WasmCore` 与 `ParamDefaultValueMetas`，最终 `Puerts` 只 public 依赖 `JsEnv`。 | `Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:39-45`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-93`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:149-152`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-20` | 先用 dedicated core module 承接 VM/third-party，再让上层 runtime 依赖 core，而不是让 product runtime 直接把 vendor header 暴露给所有下游。 |
| UnrealCSharp | `UnrealCSharpCore` 持有 `Mono` 和宿主扫描逻辑，上层 `UnrealCSharp` 只 public 依赖 `CrossVersion` 与 `UnrealCSharpCore`，不直接导出 raw runtime vendor seam。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-46`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-33` | 先把第三方/底层 runtime 接缝固定在 core 模块，再让上层 runtime 通过 core 获取能力。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先拆所有权，再缩 public include surface：把 raw AngelScript 头和 `AngelscriptRuntime` 的稳定 API 分成两个层次，避免继续通过同一个 module edge 暴露。 |
| 具体步骤 | 1. 新增 `AngelscriptVM` 或 `AngelscriptThirdParty` 模块，专门持有 `ThirdParty/angelscript` 的 include path、宏和平台构建选项；第一阶段只迁所有权，不改运行时行为。<br>2. 为 `AngelscriptRuntime` 建立明确的 `Public/` facade，把确实要给 editor/外部 consumer 使用的头迁进去；同时停止在 `PublicIncludePaths` 中导出 `ModuleDirectory` 和 `ThirdParty/angelscript/source`。<br>3. 对当前 editor/test 依赖点增加过渡层：为 `Core/AngelscriptEngine.h`、`AngelscriptBinds.h` 等旧 include 提供 forwarding wrapper，或把需要长期公开的类型搬到新的稳定 public 路径。<br>4. 把确实需要 raw `as_*` internals 的测试迁到专门的 `AngelscriptInternalTestSupport` / `AngelscriptVMTests` 模块，避免普通 runtime consumer 继续获得 VM 私有头的可见性。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/*`、`Plugins/Angelscript/Source/AngelscriptEditor/*`、`Plugins/Angelscript/Source/AngelscriptTest/*`，以及新增的 `AngelscriptVM` / `AngelscriptThirdParty` 模块 |
| 预估工作量 | L |
| 架构风险 | 迁移期会同时存在旧 include 路径与新 facade，如果 export 宏或 include order 处理不当，容易出现双重定义、头文件循环或旧路径未完全替换的问题。 |
| 兼容性 | 对 script API 基本无影响；对 C++ 层若已有代码直接 include `source/as_*`、`Binds/*`、`ClassGenerator/*`，需要通过 forwarding wrapper 或分阶段迁移保持兼容。 |
| 验证方式 | 1. runtime、editor、test 全量编译通过。<br>2. 新建一个只依赖 `AngelscriptRuntime` public API 的 dummy module，确认它不再能直接 include `source/as_scriptengine.h`，除非显式依赖新 VM 模块。<br>3. AngelScript 升级演练时，仅修改 `AngelscriptVM` / `AngelscriptThirdParty` 相关路径与适配层，验证变更半径明显收敛。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-07 | `AngelscriptEditor` 的 public/private contract 失配 | 依赖可见性收敛 | 高 |
| P1 | Arch-MS-08 | `AngelscriptRuntime` 对内部目录与 vendored VM 的直接导出 | 模块边界重构 | 高 |

---

## 架构分析 (2026-04-08 14:55)

### Arch-MS-09：默认启用的主插件把 regression、example、learning surface 一并装入 `AngelscriptTest`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 测试能力是否应该作为主插件默认公开模块交付，以及 test owner 是否混入了 sample / learning / validation 职责 |
| 当前设计 | `Angelscript` 主插件默认启用，并在 `.uplugin` 中直接声明 `AngelscriptTest` 为 `Editor` 模块；同一个 test 模块既承担回归测试，又承载 `Examples`、`Learning`、`Validation` 等面向示例、教学和规则校验的代码。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:13` 把整个插件设为 `EnabledByDefault = true`，`Plugins/Angelscript/Angelscript.uplugin:29-32` 同时把 `AngelscriptTest` 声明为默认装载的 `Editor` 模块。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:12-21` 为测试模块镜像 `Core`、`Debugger`、`Dump`、`Internals`、`Native`、`Preprocessor`、`ClassGenerator` 目录；`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:40-49` 还在 editor target 下附加 `CQTest`、`Networking`、`Sockets`、`UnrealEd`、`AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp:9-16` 表明该模块会在装载时直接启动并打日志。<br>`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp:139-155`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp:112-113`、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp:9-10` 说明同一模块内同时存在 example、learning trace 与 validation 测试面。 |
| 优点 | 验证面是 first-class module，仓库开箱即有完整自动化回归、示例覆盖与学习用 trace，内部开发和 CI 接入门槛低。 |
| 不足 | 主插件的公开交付面把 QA、示例、教学和规则校验一并带入默认装载路径；`AngelscriptTest` 既不是纯 regression module，也不是纯 sample plugin，导致产品边界与内部验证边界混在一起。后续若要做轻量分发、预编译插件或最小依赖宿主工程，这个默认 test surface 会持续放大模块面。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把测试能力放到独立的 `UnLuaTestSuite` 插件，且 `EnabledByDefault = false`；测试插件只通过依赖 `UnLua` 接入主运行时，而不是进入主插件默认交付面。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-55` | regression / sample / qa surface 应该优先作为 opt-in satellite plugin，而不是主产品插件的默认模块。 |
| puerts | 从 `Puerts.uplugin` 的模块清单可推断，公开面聚焦在 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor`，没有把测试模块纳入主插件模块图。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 这里“无测试模块公开面”是依据源码中的模块清单推断；可借鉴点是把公开交付面限定在 runtime/editor/toolchain。 |
| UnrealCSharp | 从 `UnrealCSharp.uplugin` 的模块清单可推断，公开面只包含 runtime、editor、generator、compiler、program owner，没有把验证与示例资产放进主插件默认模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-54` | 即便 workflow module 较多，也仍然把产品模块图与测试/样例边界分开。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptTest` 从主插件默认交付面中剥离，保留 first-class 测试能力，但改成 opt-in 的 satellite plugin / suite。 |
| 具体步骤 | 1. 第一阶段新增兄弟插件 `Plugins/AngelscriptTestSuite/`（名字可调整），把现有 `Source/AngelscriptTest/` 原样迁入；主插件 `Plugins/Angelscript/Angelscript.uplugin` 删除 `AngelscriptTest` 模块声明，新测试插件改为 `EnabledByDefault = false` 并依赖 `Angelscript`。<br>2. 第二阶段把 `Examples/` 与 `Learning/` 再从 regression suite 中拆出，优先迁到单独的 `AngelscriptExamples` / sample plugin，确保 CI-critical 回归集与 onboarding/material 集分层。<br>3. 维持 `Validation/` 中真正参与构建守卫的测试在 suite 内；如果其中有需要常驻 editor 的规则检查，再单独评估是否保留一个极小的 validation module，而不是继续让整套测试常驻。<br>4. 为 CI 和开发文档补一层启用脚本或 target profile，明确“产品插件默认关闭测试，CI/开发环境显式开启测试插件”。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptTest/*`、新增的 `Plugins/AngelscriptTestSuite/AngelscriptTestSuite.uplugin`（或等价目录），以及 CI / 测试启动脚本 |
| 预估工作量 | M |
| 架构风险 | 自动化脚本、IDE 工程生成和现有测试过滤路径可能隐式假设 `AngelscriptTest` 位于主插件目录；迁移时需要同步修正 plugin path 与 test discovery 配置。 |
| 兼容性 | 对脚本 API 和 runtime 行为无直接影响；对运行自动化测试的开发者与 CI，会新增“显式启用测试插件”这一步。若保持 module 名 `AngelscriptTest` 不变，可把 C++ 层迁移成本压到最低。 |
| 验证方式 | 1. 默认只启用主插件启动 editor，确认不再装载 `AngelscriptTest`。<br>2. 启用测试插件后，现有 automation tests 仍能被发现并执行。<br>3. CI 跑一次 regression suite，确认路径迁移没有破坏 test discovery。<br>4. 验证 sample / learning 测试拆分后，主 suite 的编译与装载时间有可见收敛。 |

### Arch-MS-10：真实启动拓扑被硬编码在 `StartupModule` / delegate 中，`LoadingPhase` 没有承担分层语义

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块启动分层是否由 `.uplugin` 声明式表达，还是依赖模块内部的手工时序编排 |
| 当前设计 | 当前 `.uplugin` 把 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 全部放在 `PostDefault`；真正的早期初始化、引擎就绪后的延迟动作和 bind module 装载，都写在 `StartupModule()`、`InitializeAngelscript()`、`OnPostEngineInit` callback 与 runtime 动态 `LoadModule(...)` 里。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 显示三个模块全部使用 `LoadingPhase = PostDefault`，描述符层没有进一步区分 bootstrap、runtime、editor-late、test。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-24` 在 `StartupModule()` 中立即调用 `InitializeAngelscript()` 并注册 fallback ticker。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:138-165` 的 `InitializeAngelscript()` 不仅初始化引擎，还会 `LoadModuleChecked("AngelscriptRuntime")` 并在缺少 owner 时自持 `OwnedPrimaryEngine`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:765` 明确要求外部先完成 `InitializeAngelscript()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-416` 的 `StartupModule()` 里又额外通过 `FCoreDelegates::OnPostEngineInit` 和 `UToolMenus::RegisterStartupCallback(...)` 手工延后 editor 行为。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1477-1487` 还会在运行时读取 `BindModules.Cache` 后动态装载 bind modules。 |
| 优点 | 当前启动链集中在少数模块中，修改路径短；对于内部团队来说，只要熟悉 `StartupModule()` 与引擎 delegate，就能快速插入新行为。 |
| 不足 | 真正的 boot topology 无法从 `.uplugin` 直接读出，而是散落在 runtime/editor 源码与动态装载逻辑里。模块边界没有告诉读者“哪些必须早于 engine init、哪些应该晚于 engine init、哪些只是 test-only”，这会让 commandlet、editor、future headless tool 和 bind loader 的演进持续依赖隐式时序知识。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLua` 把 runtime 设为 `PreDefault`，`UnLuaEditor` 设为 `Default`，`UnLuaDefaultParamCollector` 设为 `Program + PostConfigInit`；collector 还有独立 `Build.cs` 直接面向 UHT 头文件。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-55` | 启动层级先体现在模块声明，再体现在各 owner 的构建规则里，而不是主要靠模块内部 delegate 编排。 |
| puerts | `.uplugin` 显式把 `WasmCore`、`JsEnv` 放在 `PreDefault`，`DeclarationGenerator` 放在 `Default`，`ParamDefaultValueMetas` 放在 `PostConfigInit`，`Puerts` / `PuertsEditor` 放在 `PostEngineInit`；上层 `Puerts` 通过 `Build.cs` 依赖 `JsEnv`，把 late runtime owner 表达成静态层级。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-25`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-39` | 如果某些能力真的要等 engine init，再用模块 `LoadingPhase` 和 owner 分层显式表达，而不是让单模块内部同时扮演 early 和 late 两种角色。 |
| UnrealCSharp | `.uplugin` 里把 `UnrealCSharpCore`、`CrossVersion`、`UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 分开；`UnrealCSharp` 通过 `Build.cs` 依赖 `CrossVersion + UnrealCSharpCore`，而 `SourceCodeGenerator` 走 `Program + PostConfigInit`。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-50`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-45` | 可以不把所有逻辑都推到 `StartupModule()`，而是先把 core/bootstrap/runtime/editor/toolchain owner 分开，让时序依赖变成模块拓扑的一部分。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把“早期 bootstrap”和“引擎就绪后的 runtime/editor 行为”拆成显式 owner，再逐步减少 `StartupModule()` 内部的时序拼装。 |
| 具体步骤 | 1. 第一阶段新增极小的 `AngelscriptBootstrap`（或 `AngelscriptCore`）runtime 模块，`LoadingPhase` 设为 `PreDefault`，只承接 engine context stack、manifest / bind cache 发现、最小配置装载这类 early responsibility；不承载 editor UI 与实际 script engine 编译。<br>2. 现有 `AngelscriptRuntime` 保留对外 module 名与主要 API，但把真正依赖 engine-ready 状态的初始化迁到 `Default` 或 `PostEngineInit` 路径；`BindModules.Cache` 的 runtime shard 装载也从通用 `InitializeAngelscript()` 中抽成更明确的 late-init 步骤。<br>3. 审计 `AngelscriptEditor` 的 `StartupModule()`：凡是已经通过 `FCoreDelegates::OnPostEngineInit` 延后的行为，优先改成模块层面的 `PostEngineInit` owner，或者拆出 `AngelscriptEditorLate` 之类 leaf module，减少 editor shell 同时承担 early 和 late 生命周期。<br>4. 等启动层级稳定后，再让 test suite 只依赖 runtime public boot lane，而不是继续参与主插件默认启动链。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 `AngelscriptBootstrap` / `AngelscriptCore` 模块 |
| 预估工作量 | L |
| 架构风险 | 调整 `LoadingPhase` 会暴露现有隐式时序假设，尤其是 editor-only helper、commandlet 启动路径和 bind module 装载顺序；必须按 bootstrap/runtime/editor 三段分批迁移，避免一次性大改引入启动回归。 |
| 兼容性 | 对脚本 API 预期保持兼容；若保留 `AngelscriptRuntime` 与 `AngelscriptEditor` 现有模块名，对外部项目的 `Build.cs` 影响可控制在“新增可选 bootstrap 依赖”。最主要的兼容性变化是启动顺序更明确，需要同步验证现有自动化与工具脚本。 |
| 验证方式 | 1. editor、commandlet、cook 三种启动路径分别打启动日志，确认 bootstrap/runtime/editor-late 的触发顺序符合预期。<br>2. 验证 `FAngelscriptEngine::Get()` 在所有入口下都不再依赖隐式调用顺序。<br>3. 运行 bind 生成与加载链，确认 runtime shard、editor shard 的装载时机没有回退。<br>4. 对照 `.uplugin`，确认主要时序语义已经从源码 delegate 回收为声明式模块阶段。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-10 | 启动拓扑依赖 `StartupModule` / delegate 手工编排 | 启动分层重构 | 高 |
| P2 | Arch-MS-09 | `AngelscriptTest` 作为默认主插件模块的交付边界过宽 | 模块边界收敛 | 中 |

---

## 架构分析 (2026-04-08 15:10)

### Arch-MS-11：`ASEditorBind_*` 通过 `AngelscriptEditor` supernode 继承整套 editor shell，分片没有形成最小依赖边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor bind 分片是否依赖“最小 editor bind support”，还是直接挂在完整 editor shell 上 |
| 当前设计 | 生成器为 editor shard 统一注入 `AngelscriptEditor` 公共依赖；但生成出来的模块源码本身只需要 `AngelscriptBinds.h` 和各个被绑定类的头。结果是 `ASEditorBind_*` 在拓扑上继承了 `AngelscriptEditor` 的整套 `UnrealEd / BlueprintGraph / Kismet / DirectoryWatcher / AssetTools / Slate` 公共边。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1168-1177` 生成 shard 时先把 `Core`、`CoreUObject`、`Engine`、`AngelscriptRuntime` 设为公共依赖，若 `bIsEditor` 再额外 `PublicDepends.Add("AngelscriptEditor")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1276` 的 `GenerateBuildFile()` 会把这组公共依赖原样写入每个 shard 的 `PublicDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1314-1421` 生成的模块 cpp 只 include `"<ModuleName>Module.h"`、`"AngelscriptBinds.h"` 和被绑定 `UClass` 的头，没有直接 include `AngelscriptEditor` 的 public header。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-26` 却把 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools` 全部放进 `AngelscriptEditor` 的 `PublicDependencyModuleNames`。 |
| 优点 | 生成器实现简单，不需要额外判断 editor shard 到底需要哪一小层 editor support；只要依赖 `AngelscriptEditor`，大多数 editor-only 绑定都能直接编译。 |
| 不足 | `ASEditorBind_*` 不是依赖“editor bind contract”，而是依赖“完整 editor shell”。这会把菜单、watcher、tool menu、asset tools 等与绑定注册无关的依赖一起放大到每个 editor shard 上，导致任何 `AngelscriptEditor` 壳层改动都更容易扩大编译与模块管理影响面。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把 editor 工作流依赖集中在自身 private deps，UHT/program collector 则是独立的 `UnLuaDefaultParamCollector` 模块，不让 collector 反向依赖完整 editor shell。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | editor 壳层和生成/收集 owner 分开后，生成模块不必继承完整 editor UX 依赖。 |
| puerts | `PuertsEditor` 虽然自身 public edge 也偏宽，但声明生成被单独放到 `DeclarationGenerator`，默认参数元数据又进一步放到 `ParamDefaultValueMetas` program 模块。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16-45`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44` | 即便 editor 壳层较重，也会额外提供 generator/program leaf module，避免所有生成产物都直连主 editor 模块。 |
| UnrealCSharp | `.uplugin` 把 `UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler` 分成独立 owner；`UnrealCSharpEditor` 通过 private deps 组合 generator/compiler，而不是让生成模块反向 public 依赖 editor 壳层。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | bind/codegen leaf module 最好直接依赖最小核心，而不是经由 editor 壳层继承大而全的公共边。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 从 `AngelscriptEditor` 中拆出一个最小 `AngelscriptEditorBindSupport` 叶子模块，让 `ASEditorBind_*` 依赖 bind support，而不是依赖完整 editor shell。 |
| 具体步骤 | 1. 新增 `AngelscriptEditorBindSupport`（名称可调整）模块，只放 editor-only 绑定真正需要的契约，例如 editor class DB 访问、editor-only bind helper、必要的 editor 类型前置依赖。<br>2. 把 `GenerateNewModule()` 中 `bIsEditor` 分支的 `PublicDepends.Add("AngelscriptEditor")` 改为依赖新模块；同时保留 `AngelscriptRuntime` 作为公共核心依赖。<br>3. 审计 `ASEditorBind_*` 生成代码真正需要的 editor 头，若只是被绑定类所在模块，则继续由 `GenerateBuildFile()` 写入对应 package module 私有依赖，不再借 `AngelscriptEditor` 兜底。<br>4. 迁移初期在 `AngelscriptEditor` 保留对新模块的 private 依赖，并为可能被外部直接 include 的 helper 提供 forwarding shim，保证旧调用点短期不崩。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 `Plugins/Angelscript/Source/AngelscriptEditorBindSupport/*` |
| 预估工作量 | M |
| 架构风险 | 风险主要在于某些 editor bind helper 目前可能隐式依赖 `AngelscriptEditor` 的其他 public include path；拆分时需要通过真实生成一次 `ASEditorBind_*` 来找出遗漏的最小依赖。 |
| 兼容性 | 对脚本 API 无直接影响；对外部 C++ 若有人直接依赖 `AngelscriptEditor` 里的 bind helper，需要保留一段 forwarding 过渡期。整体属于低到中等向后兼容风险。 |
| 验证方式 | 1. 重新生成 editor bind shard，确认它们只依赖 `AngelscriptRuntime + AngelscriptEditorBindSupport + package modules`。<br>2. 修改 `AngelscriptEditor` 菜单或 watcher 代码后，确认不再触发 editor bind shard 的无关重编。<br>3. 运行 editor-only 绑定回归，确认 `ASEditorBind_*` 仍能注册 editor 类型与函数。 |

### Arch-MS-12：legacy C++ generator 与 UHT sidecar 各自维护“支持模块列表”，模块拓扑没有单一 authority

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定/代码生成链是否由同一份模块清单驱动，还是由多个生成器各自推导“哪些模块属于 Angelscript 支持面” |
| 当前设计 | legacy C++ 生成器通过运行中的 `TObjectRange<UClass>()` 和 package 名构建 runtime/editor class DB，再把 package 名写进 shard `Build.cs` 的 `PrivateDependencyModuleNames`；UHT sidecar 则把自己绑定到 `AngelscriptRuntime`，并在生成 `AS_FunctionTable_*` 前直接解析 `AngelscriptRuntime.Build.cs` 的文本，推导 `allModules` 与 `editorOnlyModules`。也就是说，模块支持面当前有两套 authority。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1088-1159` 的 `GenerateBindDatabases()` 通过 `TObjectRange<UClass>()`、`Class->GetPackage()->GetName(Name)` 和 header 路径是否包含 `Editor/` 来建立 runtime/editor class DB。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1276` 的 `GenerateBuildFile()` 再把这些 package/module 名写入 shard 的 `PrivateDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-27` 把 exporter 直接绑定到 `ModuleName = "AngelscriptRuntime"`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-67` 在生成 `AS_FunctionTable_*` 前先调用 `LoadSupportedModules(factory)`，只处理 `supportedModules.All` 中出现的模块。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-385` 的 `LoadSupportedModules()` 会先 `ResolveRuntimeBuildCsPath(factory)`，再逐行解析 `AngelscriptRuntime.Build.cs` 的 `DependencyModuleNames.AddRange` 文本，并用 `if (Target.bBuildEditor)` 文本块区分 `editorOnlyModules`。 |
| 优点 | 两条生成链都能各自独立工作，不需要先引入统一 manifest 基础设施；legacy bind 仍能基于 live reflection 做广覆盖，UHT sidecar 也能基于 runtime `Build.cs` 自动跟随部分依赖变化。 |
| 不足 | “模块支持面”不再由 `.uplugin` 或单一 manifest 唯一表达，而是同时存在于 live `UClass` 枚举规则、`AngelscriptRuntime.Build.cs` 文本解析和 shard `Build.cs` 生成规则中。未来一旦引入新的 leaf module、可选 feature module 或新的 bind provider，就容易出现一条生成链已经看见、另一条还没看见的漂移。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 显式声明 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector`；collector 作为独立 `Program` 模块直接声明自己的最小依赖，而不是去解析 `UnLua.Build.cs`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 生成器 owner 应该自己声明依赖或消费稳定 manifest，而不是把 runtime `Build.cs` 当隐式数据源。 |
| puerts | `.uplugin` 把 `DeclarationGenerator` 与 `ParamDefaultValueMetas` 做成独立模块；二者都用自己的 `Build.cs` 声明 UHT/program 依赖，没有通过解析 `Puerts.Build.cs` 来推断支持模块。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44` | toolchain 模块越显式，模块拓扑越容易成为“声明式事实”，而不是“运行时/文本推导结果”。 |
| UnrealCSharp | `ScriptCodeGenerator`、`Compiler`、`UnrealCSharpEditor` 都有独立 `Build.cs`，上层 editor 通过 private deps 组合这些工具模块，而不是让工具链去反向解析 `UnrealCSharp.Build.cs`。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63` | generator/compiler 应该是拓扑上的显式节点，依赖关系由自己的模块规则或共享 manifest 描述。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 建立单一 `AngelscriptModuleManifest`，让 legacy bind generator 与 UHT sidecar 共享同一份模块 authority。 |
| 具体步骤 | 1. 第一阶段新增一个稳定 manifest 产物，例如 `Intermediate/Angelscript/AngelscriptModules.json`，内容至少包括 `runtime modules`、`editor-only modules`、`toolchain modules`、`optional feature modules`。<br>2. 由单一入口生成这份 manifest。可选方案是新增一个极小的 `AngelscriptBuildMetadata` helper，或在 prebuild/UBT step 中根据 `.uplugin` 与显式配置生成；重点是不要再让 UHT tool 直接解析 `AngelscriptRuntime.Build.cs` 文本。<br>3. `AngelscriptFunctionTableCodeGenerator` 改为读取 manifest 获取 `supportedModules`；legacy `GenerateNativeBinds()` 仍可继续用 live `UClass` 枚举发现“有哪些类”，但 editor/runtime 分类与允许依赖的模块集合改由同一 manifest 提供。<br>4. 迁移期间保留 `Build.cs` 解析作为 fallback，并增加一个一致性检查：若 fallback 结果与 manifest 不同，生成时报 warning，帮助逐步清理旧路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及新增的 manifest 生成入口 |
| 预估工作量 | M |
| 架构风险 | 如果 manifest 的生成时机放得过晚，UHT 或 editor generator 可能在它产生前就启动；因此第一阶段必须先把产物位置和生命周期稳定下来。 |
| 兼容性 | 对脚本 API 无直接影响；对生成链和本地构建脚本有中等影响。保留 fallback 解析可把迁移做成向后兼容的增量步骤。 |
| 验证方式 | 1. 在相同源码状态下分别运行 legacy generator 与 UHT exporter，确认二者看到的支持模块集合一致。<br>2. 引入一个新的 leaf bind module 后，只更新 manifest 生成入口与模块描述，确认两条生成链都能同步识别。<br>3. 故意让 `Build.cs` 文本与 manifest 不一致，确认 fallback warning 能准确暴露漂移。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-11 | `ASEditorBind_*` 直接依赖完整 `AngelscriptEditor` 壳层 | 依赖边界收缩 | 高 |
| P1 | Arch-MS-12 | generator/toolchain 对模块支持面的 authority 分裂 | 元数据统一 + 结构性整理 | 高 |

---

## 架构分析 (2026-04-08 15:20)

### Arch-MS-13：宽 `PublicIncludePaths` 把模块边界退化成目录约定，runtime internals 与 third-party internals 被跨模块直接消费

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `Build.cs` 是否通过受控 public surface 暴露契约，还是把整个源码树与 third-party internals 一起开放给下游模块 |
| 当前设计 | `AngelscriptRuntime` 直接把 `ModuleDirectory`、`Core/` 和 vendored `ThirdParty/angelscript/source` 放进 `PublicIncludePaths`；`AngelscriptTest` 进一步把 `Core`、`Preprocessor`、`ClassGenerator` 等内部子目录加入 include path。结果是 editor/test 代码可以直接 include `Core/...`、`ClassGenerator/...`、`Preprocessor/...`、`source/as_*`，模块边界更多靠目录命名约定而不是 public API。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-22` 公开暴露 `ModuleDirectory`、`Core` 和 `ThirdParty/angelscript/source`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:12-21` 把 `Core`、`Debugger`、`Dump`、`Internals`、`Native`、`Preprocessor`、`ClassGenerator` 全部加入测试模块 include path。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:9` 直接 include `Core/AngelscriptEngine.h`，并在 `:55`、`:63-67` 的 public API 中暴露 `FAngelscriptModuleDesc` 与 `FAngelscriptEngine`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h:15-21` 直接 include `Preprocessor/AngelscriptPreprocessor.h`、`ClassGenerator/ASClass.h` 与 `source/as_context.h` / `as_module.h` / `as_scriptengine.h`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp:1-6` 既 include `Core/AngelscriptBinds.h` / `Core/AngelscriptEngine.h`，也通过相对路径直达 `../../AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.h`。 |
| 优点 | 内部团队可以快速写 white-box test 和 editor helper，不需要先搭 façade 或 test hook；直接包含 `source/as_*` 也让底层 AngelScript 行为验证很直接。 |
| 不足 | 真实 public contract 变成“整个源码树都算 public”；runtime 内部目录、`ClassGenerator`、`Preprocessor` 乃至 AngelScript 私有头都可能被下游模块固化依赖。后续如果要把 `ClassGenerator`、`Internals` 或 third-party bridge 单独抽模块，变更会立刻扩散到 editor/test 乃至外部 C++ 扩展。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | editor/program owner 需要访问内部实现时，会在自己的 `PrivateIncludePaths` 中显式声明 `UnLua/Private`，而不是让所有下游模块都自动拿到同一组内部目录。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:37-45`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-84`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:39-53` | “谁需要内部头，谁私下 opt-in” 比 “runtime 把整个树公开” 更利于后续抽模块。 |
| puerts | runtime/editor 模块主要通过模块依赖协作；显式 `PublicIncludePaths` 主要用于 UHT public 头或外部 SDK 头，不把自己的 module root 当作通用 public include root。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-26`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:16-20`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:302-329`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:432-435` | third-party 头可以显式开放，但模块内部目录最好不要和 third-party internals 一起变成 public surface。 |
| UnrealCSharp | `UnrealCSharp` / `UnrealCSharpEditor` 默认不通过 `PublicIncludePaths` 暴露模块根目录，跨层访问主要靠显式 module dependency；generator/runtime core 再通过独立模块分层。 | `Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:11-22`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:11-22`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63` | 先把跨层关系表达成 module dependency，再按 owner 精准开放头文件，比 blanket include path 更利于维护。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先收窄 runtime public include surface，再把 third-party internals 与 white-box access 收口到受控 owner。 |
| 具体步骤 | 1. 为 `AngelscriptRuntime` 定义最小 public contract，优先把真正对外承诺的头迁到 `Public/` 或建立 forwarding façade，第一阶段至少停止继续新增 `Core/`、`ClassGenerator/`、`Preprocessor/` 直接 include 到外部模块。<br>2. 把 `ThirdParty/angelscript/source` 从 `PublicIncludePaths` 下沉为 private-only 访问；对确实需要 `source/as_*` 的 runtime 内部实现，改为经 `StartAngelscriptHeaders.h` / `EndAngelscriptHeaders.h` 包装的 bridge header 统一进入。<br>3. 对 `AngelscriptTest` 的 white-box 需求，新增 `AngelscriptRuntimeTestSupport` 或扩展现有 `AngelscriptTestSupport`，把 `Preprocessor`、`ClassGenerator`、`Internals` 的访问改成受控 helper，而不是散落在测试里直接 include 私有目录与 third-party internals。<br>4. 迁移期保留兼容 shim：旧路径可以短期 forwarding 到新 façade，并增加 lint/grep 规则，阻止新代码继续从非 owner 模块 include `source/as_*` 或 `ClassGenerator/*`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`，以及新增的 façade / test support 文件 |
| 预估工作量 | L |
| 架构风险 | include surface 收紧后，会暴露大量历史上依赖“宽 include path”的调用点；如果一次性替换范围过大，容易把真正的模块边界问题和普通 include 修复混在一起。 |
| 兼容性 | 对脚本 API 基本无影响；对 C++ 扩展若已直接 include `Core/*`、`ClassGenerator/*` 或 `source/as_*`，需要提供一段过渡期 shim。属于中等向后兼容风险，但可通过分阶段收口控制。 |
| 验证方式 | 1. 重新编译 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`，确认不再依赖 blanket `ModuleDirectory` public include。<br>2. 用 `rg` 扫描非 owner 模块中的 `source/as_`、`ClassGenerator/`、`Preprocessor/` include，确认只剩允许的 bridge/test support 入口。<br>3. 跑现有 automation tests，确认 white-box 测试仍能覆盖核心路径。 |

### Arch-MS-14：`AngelscriptEditor` 的 public contract 直接暴露 runtime / class-generator 内部类型，依赖反转没有稳定落点

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 模块是否通过稳定 editor-facing contract 与 runtime 协作，还是把 runtime 内部类型直接固化进 public API |
| 当前设计 | `AngelscriptEditor` 目前不仅 public 依赖 `AngelscriptRuntime`，其 `Public/` 树里的 API 还直接 include runtime 核心头，并把 `FAngelscriptModuleDesc`、`FAngelscriptEngine` 这样的 runtime 内部类型暴露给外部调用者；同时 `Public/` 目录下还存在直接依赖 `ClassGenerator` 和 `AngelscriptRuntimeModule` 的实现文件。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-26` 把 `AngelscriptRuntime` 以及大量 editor shell 依赖放进 `PublicDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:9` include `Core/AngelscriptEngine.h`，并在 `:55`、`:63-67` 的 public struct / function signature 中直接使用 `FAngelscriptModuleDesc` 与 `FAngelscriptEngine`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1-5` 这个位于 `Public/` 树中的实现文件直接 include `AngelscriptEngine.h`、`ClassGenerator/AngelscriptClassGenerator.h`、`ClassGenerator/ASClass.h`、`AngelscriptRuntimeModule.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25-43` 还直接监听 `FAngelscriptClassGenerator::OnPostReload` 与 `FAngelscriptEngine::Get().IsInitialCompileFinished()`，说明 public-side editor 行为直接绑定 runtime reload internals。 |
| 优点 | editor 工具可以直接复用 runtime 已有的模块描述、重载事件和引擎状态，不需要额外 DTO、adapter 或 bridge 层；实现路径短，功能落地快。 |
| 不足 | `AngelscriptEditor` 的 public API 已经把 runtime internals 变成外部契约。一旦后续把 `ClassGenerator`、runtime reload lane 或 `FAngelscriptModuleDesc` 所在 owner 抽成新模块，外部 include 这些 public header 的 C++ 代码会立即受影响，导致依赖反转难以实施。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把 `UnLua`、`Lua` 以及大部分 editor workflow 依赖都收在 private deps；参数收集又由独立 `UnLuaDefaultParamCollector` 程序模块承接，不要求 editor public contract 暴露 runtime internals。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-84`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | editor shell 可以使用 runtime / collector，但更适合 private 组合，而不是把运行时内部类型带到 public surface。 |
| puerts | `.uplugin` 中把 `DeclarationGenerator` 单独拆成 owner，声明生成与 editor shell 不是同一 public contract；生成链由独立模块承接，而不是让 `PuertsEditor` 对外暴露 declaration pipeline 细节。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16-45` | 如果 editor 需要深度依赖生成/反射桥接逻辑，优先新增独立 owner，而不是把内部类型塞进 editor public API。 |
| UnrealCSharp | `UnrealCSharpEditor` 通过 private deps 组合 `ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion`；生成与编译职责由独立模块承担，而不是作为 editor public header 的签名类型暴露。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | 先给 runtime core / generator / editor shell 各自明确 owner，再让 editor public surface 只暴露稳定契约。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 editor 侧引入稳定的 editor-facing DTO / bridge，把 runtime / class-generator internals 从 `AngelscriptEditor/Public` 签名里剥离出去。 |
| 具体步骤 | 1. 先从 `BlueprintImpact` 这一条 public API 下手，新增只服务 editor 外部调用方的轻量 DTO，例如 `FAngelscriptBlueprintImpactModuleView` / `FAngelscriptBlueprintImpactContext`，避免 public header 直接暴露 `FAngelscriptModuleDesc` 与 `FAngelscriptEngine`。<br>2. 把 `Public/EditorMenuExtensions` 下的实现 `.cpp` 迁入 `Private/`，保留 public header 只声明蓝图/编辑器扩展需要稳定公开的类型；runtime reload、`ClassGenerator` 事件订阅和 `AngelscriptRuntimeModule` 访问改为 private bridge 实现。<br>3. 新增一个私有 owner，例如 `AngelscriptEditorRuntimeBridge` 或 `AngelscriptEditorModel`（名字可调整），专门承接 `FAngelscriptEngine`、`FAngelscriptClassGenerator`、`FAngelscriptModuleDesc` 到 editor DTO 的转换。<br>4. 迁移期为现有 public API 保留过渡重载或 adapter helper，让旧调用点还能编译，同时逐步把外部依赖迁到新 DTO。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`，以及新增的 editor bridge / DTO 文件 |
| 预估工作量 | M |
| 架构风险 | 如果一次性替换全部 public signature，可能影响现有 editor extension C++ 调用点；更稳妥的做法是先对 `BlueprintImpact` 和 menu extension 这两条最重耦合链做示范迁移。 |
| 兼容性 | 对脚本侧和大多数纯 editor 使用者影响较低；对直接 include `AngelscriptEditor/Public` 并依赖 runtime 内部类型的 C++ 扩展，需要一段 adapter 过渡期。整体属于中等向后兼容风险，但可通过保留旧签名重载分步迁移。 |
| 验证方式 | 1. 新建一个最小外部 editor module，只 include `AngelscriptEditor/Public` 头，确认不再需要 `Core/AngelscriptEngine.h`、`ClassGenerator/*` 或 `AngelscriptRuntimeModule.h`。<br>2. 迁移后重新编译 `AngelscriptEditor` 与依赖它的测试，确认 menu extension 和 blueprint impact 功能行为不变。<br>3. 对 public 头做静态扫描，确认不再直接暴露 `FAngelscriptEngine`、`FAngelscriptModuleDesc`、`ClassGenerator/*`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-13 | 宽 `PublicIncludePaths` 造成 runtime internals / third-party internals 外泄 | 边界收口 + façade / test support 整理 | 高 |
| P1 | Arch-MS-14 | `AngelscriptEditor` public contract 固化 runtime / class-generator 内部类型 | 依赖反转 + public API 清理 | 高 |

---

## 架构分析 (2026-04-08 15:32)

### Arch-MS-15：bind shard 的 runtime/editor 归属由头文件路径启发式决定，模块类型 authority 没有落在声明层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 生成型 bind 模块的 runtime/editor 归属，是否由模块描述符/manifest 显式表达，还是由源码路径约定隐式推断 |
| 当前设计 | 当前 checked-in 的 `.uplugin` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块；legacy bind generator 在扫描 `UClass` 时，先用 `Class->GetPackage()->GetName(Name)` 记录 script package，再用 `HeaderPath.Contains("Editor/")` 决定该 class 落进 runtime DB 还是 editor DB，最后据此生成 `ASRuntimeBind_*` / `ASEditorBind_*`。换句话说，bind shard 的模块类型不是由模块元数据决定，而是由头文件所在路径约定决定。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明三类静态模块，没有 bind shard 的显式模块描述。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1091` 先读取 `Class->GetPackage()->GetName(Name)` 作为分组键。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1111-1116` 通过 `FindClassHeaderPath` 和 `HeaderPath.Contains("Private")` 过滤输入。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1136-1159` 仅用 `HeaderPath.Contains("Editor/")` 把 class 写入 runtime/editor 两个 DB。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 再根据这两个 DB 的 key 生成 `ASRuntimeBind_*` 与 `ASEditorBind_*`。 |
| 优点 | 不需要额外查询 UBT/UHT 模块元数据，legacy generator 可以只依赖 live reflection 和 source navigation 立即完成分片。 |
| 不足 | shard 类型的 authority 不在 `.uplugin`、`Build.cs` 或结构化 manifest，而在路径字符串匹配里。这里关于“误分 runtime/editor shard”的风险，是根据源码中未见模块类型查询、仅依赖 `HeaderPath.Contains("Editor/")` 的事实所作推断：只要模块布局、plugin 目录命名、generated header 路径或 editor-only 类型所在文件夹与约定不一致，分片归属就可能漂移，但这种漂移不会在模块描述层直接暴露。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 直接把 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector` 声明成 `Runtime`、`Editor`、`Program` 三类模块；collector 的 `Build.cs` 也显式面向 `Programs/UnrealHeaderTool/Public` 与 `UnLua/Private`，没有把模块归属建立在头文件路径包含 `Editor/` 的启发式上。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | module role 先在声明层固定，再让 generator/tooling owner 依赖这份声明。 |
| puerts | `.uplugin` 明确区分 `WasmCore` / `JsEnv` / `Puerts` 的 runtime 层级，以及 `DeclarationGenerator`（Editor）、`ParamDefaultValueMetas`（Program）的工具层级；`DeclarationGenerator.Build.cs` 和 `ParamDefaultValueMetas.Build.cs` 直接声明各自依赖，不需要用路径约定反推模块角色。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-45` | runtime/editor/program 责任应由模块图与构建规则明示，而不是由源码目录命名约定隐式推导。 |
| UnrealCSharp | `.uplugin` 把 `UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 的模块类型全部显式列出；同时 `UnrealCSharpCore.build.cs:131-211` 生成 `UnrealCSharp_Modules.json`，把模块索引固化为结构化数据，而不是从头文件路径推断模块角色。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-211`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | 如果生成链需要额外分类信息，优先引入 manifest / index，而不是继续扩展路径启发式。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 bind generator 引入单一的模块角色 authority，让 runtime/editor 归属来自模块元数据或结构化 manifest，而不是来自 `HeaderPath.Contains("Editor/")`。 |
| 具体步骤 | 1. 第一阶段新增结构化输入，例如 `Intermediate/Angelscript/AngelscriptModuleRoles.json`，至少记录 `ModuleName -> Type(Runtime/Editor/Program)` 与必要的 override；生成来源可以是 `.uplugin + 显式补充配置`，不要求一步到位覆盖所有未来模块。<br>2. 修改 `GenerateBindDatabases()`：保留 `Class->GetPackage()->GetName(Name)` 作为 package key，但把 runtime/editor 归属改成“先按 package/module 名查 manifest，再决定进入哪个 DB”；仅在 manifest 缺项时回退到当前 `HeaderPath.Contains("Editor/")`，并输出 warning。<br>3. 增加一份冲突诊断：当 manifest 判定与路径启发式结果不一致时，把 module/class/header path 记入报告，帮助清理历史路径约定。<br>4. 等 UHT sidecar 和 legacy generator 都切到同一份角色清单后，再评估是否继续保留 editor/runtime 双 shard，还是进一步收敛到固定 bind owner 模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`（若同步消费同一份角色清单）、新增的 `Intermediate/Angelscript/AngelscriptModuleRoles.json` 生成入口 |
| 预估工作量 | M |
| 架构风险 | 风险主要在于 manifest 的生成时机和 invalidation 规则；如果角色清单过期，legacy generator 与 UHT sidecar 可能看到不同的模块类型。第一阶段保留路径启发式 fallback，可以把迁移做成可回退的增量修改。 |
| 兼容性 | 对脚本 API 与现有模块名没有直接影响；变化集中在生成链的分类 authority。保留 fallback 后，对现有项目是低到中等兼容性风险。 |
| 验证方式 | 1. 为一个已知 editor-only module 和一个 runtime module 各生成一次 bind，确认 shard 归属来自角色清单而不是头文件路径。<br>2. 人为制造一条“路径像 runtime、角色却是 editor”的样例，确认生成器能报出冲突而不是静默误分片。<br>3. 对比迁移前后 `BindModules.Cache` 或生成结果，确认未改变预期的运行时注册行为。 |

### Arch-MS-16：bind shard 的 `PrivateDependencyModuleNames` 直接跟随反射到的 package 名，静态 DAG 会随可绑定表面漂移

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind shard 的依赖拓扑，是否由少量稳定 owner 模块声明，还是由每次扫描得到的 class package 集动态拼接 |
| 当前设计 | 当前 legacy generator 先把 `UClass` 按 `Class->GetPackage()->GetName(Name)` 聚到 runtime/editor DB，再把这些 package key 按 10 个一组切成 shard；`GenerateNewModule()` 随后把该组 key 原样传给 `GenerateBuildFile()` 作为 `PrivateDependencies`，由后者截取最后一级路径写入 shard 的 `PrivateDependencyModuleNames`。这意味着每个 `ASRuntimeBind_*` / `ASEditorBind_*` 的静态依赖边，不是预先设计好的模块边界，而是当前可绑定 class 集合的直接投影。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1091` 用 `Class->GetPackage()->GetName(Name)` 取 package 名。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1138-1159` 把这些 package 名写入 runtime/editor DB。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 再把 DB 的 key 按固定 bucket 组装成 `ModuleArray`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166-1200` 把 `ModuleArray` 直接传给 `GenerateBuildFile()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1252-1272` 在生成的 shard `Build.cs` 中把这些字符串写成 `PrivateDependencyModuleNames`，并用 `FindLastChar('/')` 截出最后一级名称。 |
| 优点 | 从局部看，这种做法能让 shard 只依赖“当前 bucket 中确实出现了 BlueprintCallable 类型的模块”，避免所有 bind 代码都绑定到一个超大而全的依赖列表。 |
| 不足 | shard 的依赖图会随着可绑定 class 集、引擎升级、启用插件集合甚至 header 可见性变化而漂移，而漂移点既影响 `Build.cs`，又影响模块名与 project file。这里关于“依赖图漂移会放大增量构建 churn”的判断，是基于当前 `PrivateDependencyModuleNames` 直接取自反射结果、而非稳定 allowlist 的事实所作推断。更关键的是，模块边界在这里承载了“并行编译切片”与“产品依赖声明”两种职责，导致任何一个新可绑定模块出现时，都可能把本应属于生成内容层的变化升级成静态拓扑变化。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaDefaultParamCollector` 作为固定 `Program` 模块，只声明 `Core`、`CoreUObject` 和少量 include path；collector 处理的输入范围可以扩张，但不会把每个新发现的 package 直接写进新的 module dependency。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 输入规模可以动态增长，模块依赖面仍应保持稳定。 |
| puerts | `DeclarationGenerator` 和 `ParamDefaultValueMetas` 都是固定 owner 模块，`Build.cs` 依赖列表是静态声明；即便声明生成覆盖更多 UE module，工具模块本身也不按 package 集合重写 `Build.cs`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-45` | generator 可以消费全局模块集合，但不应让“发现了哪些模块”直接改写 generator 模块本身的静态 DAG。 |
| UnrealCSharp | `ScriptCodeGenerator` 和 `Compiler` 的依赖是稳定的；需要感知宿主模块分布时，`UnrealCSharpCore.build.cs` 会把结果写入 `UnrealCSharp_Modules.json`，把“环境发现”与“模块规则”分开。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-211` | 模块索引可以动态，但 build dependency 最好保持为稳定 owner + 结构化索引两层，而不是让发现结果直接变成 `Build.cs`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“发现了哪些可绑定 package”从 shard 的静态 `Build.cs` 里移出去，收敛成稳定 bind owner + 结构化 package manifest 的两层设计。 |
| 具体步骤 | 1. 第一阶段先固定 owner：恢复或新增少量稳定模块，例如 `AngelscriptNativeBindsRuntime` / `AngelscriptNativeBindsEditor`，让 generated `.cpp` 继续按 class 或 package 落盘，但不再按 package bucket 新建 UE 模块。<br>2. 为 generator 新增 `AngelscriptBindPackages.json`（或等价结构），仅记录“这个生成批次覆盖了哪些 package / class / include”，作为内容层索引；它可以随反射结果变化，但不再直接生成新的 `PrivateDependencyModuleNames`。<br>3. 对真正必须新增 build 依赖的领域模块，改成显式 leaf bind support 模块，例如 gameplay、editor-support、future plugin feature binds；也就是说，静态 DAG 只对“长期稳定的功能域”增边，而不是对“当前 bucket 里恰好出现的 package”增边。<br>4. 迁移期保留旧 shard 生成路径作为 fallback，并增加一项 diff 检查：若旧方案会生成新的 `PrivateDependencyModuleNames`，而新 manifest 方案没有覆盖，就把缺失 package 报出来，逐步补足显式 leaf module 或 allowlist。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`（若继续消费 bind module 清单）、新增的稳定 bind owner 模块，以及新增的 `Intermediate/Angelscript/AngelscriptBindPackages.json` 生成入口 |
| 预估工作量 | L |
| 架构风险 | 最大风险是固定 owner 后，某些 generated `.cpp` 可能失去过去依赖 package-derived `Build.cs` 获得的 include/link 环境；因此迁移必须配合 leaf bind support 模块或 allowlist diff，一次只收掉一类动态依赖。 |
| 兼容性 | 对脚本 API 和绑定注册顺序应保持兼容；变化主要落在生成链与静态模块拓扑。若保留旧 shard fallback，一般不需要外部项目立刻调整脚本代码，只可能需要清理旧生成产物。 |
| 验证方式 | 1. 在相同源码状态下连续两次生成 binds，确认新的静态 bind owner `Build.cs` 不再因 package 集变化而重写。<br>2. 新增一个 BlueprintCallable 类型到已有 UE module，确认变化落在 generated `.cpp` / manifest，而不是新增或改写 bind 模块依赖边。<br>3. 对比迁移前后的 full rebuild 与 incremental build 日志，确认 static module count 和 dependency graph 收敛，但绑定覆盖率不下降。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-15 | bind shard 的 runtime/editor 归属 authority 依赖路径启发式 | 元数据显式化 + 分类 authority 收口 | 高 |
| P1 | Arch-MS-16 | bind shard 的静态依赖边直接跟随反射 package 集漂移 | 结构性重构 + 固定 owner / manifest 解耦 | 高 |

---

## 架构分析 (2026-04-08 15:47)

### Arch-MS-17：bind shard 作为 `Source/` 级临时模块存在，模块清单对工作区生成状态敏感

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind shard 是否属于稳定的模块契约，还是属于某次本地生成后的临时工作区状态 |
| 当前设计 | checked-in 的静态模块图仍然只有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 UE 模块；legacy generator 会在运行时把 `ASRuntimeBind_*` / `ASEditorBind_*` 作为新的 sibling module 写到插件 `Source/` 根下，并把本轮生成出来的模块名写入 `BindModules.Cache` 供 runtime 装载。换句话说，bind shard 不是稳定声明层的一部分，而是“当前工作区最近一次生成结果”的一部分。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明了三个静态模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 按当前 class DB 的 key 生成 `ASRuntimeBind_*` / `ASEditorBind_*` 名称。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1180-1207` 通过 `FindModulePath("AngelscriptRuntime")` 推导基目录，并把新模块的 `Build.cs`、header 和 `.cpp` 写到 `Source/<ModuleName>/`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:584-601` 把当前模块名列表直接写入/读出 `BindModules.Cache`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1477-1487` 在运行时读取 cache 并逐个 `LoadModule(...)`。<br>补充工作区观察：按 2026-04-08 当前工作区实际搜索，`Plugins/Angelscript/Source/` 下未找到 checked-in 的 `ASRuntimeBind_*` / `ASEditorBind_*` 源码目录，但 `Plugins/Angelscript/Intermediate/Build/Win64/x64/UnrealEditorGPF/Development/` 下仍可见 11 个 `ASRuntimeBind_*` 构建目录、0 个 `ASEditorBind_*` 构建目录。这说明“12+4”更像某一生成态的快照，而不是源码仓库能直接证明的静态模块清单。 |
| 优点 | shard 数量能随当前绑定表面自动扩缩，不需要手工维护大量固定 `Build.cs`。对追求本地并行编译吞吐的团队，这种生成态驱动的模块图很灵活。 |
| 不足 | 模块清单不再是仓库级事实，而是工作区级事实。代码审查看到的是 3 个模块，本地构建实际经历的可能是 3 + N 个模块；CI、IDE project generation、分支切换、clean build 与问题复现都需要先知道“最近一次生成态”是什么。这里关于“模块清单对工作区状态敏感”的判断，既基于上述源码，也基于 2026-04-08 当前工作区的实际文件搜索结果。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块清单在 `.uplugin` 中固定为 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector`；额外生成产物写到 `Intermediate/IntelliSense/<ModuleName>/`，而不是再创建新的 UE 模块。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:47-48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233` | “输出文件很多”不等于“模块要跟着变多”；稳定模块图 + Intermediate 产物更利于审查与复现。 |
| puerts | `.uplugin` 固定声明 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor`；默认参数元数据生成器在固定 owner 模块里把结果落成单个 `InitParamDefaultMetas.inl` 文件。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:37-50`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-126` | toolchain 可以是动态生成，但模块 inventory 应该保持静态、可追踪。 |
| UnrealCSharp | `.uplugin` 固定声明 runtime/editor/generator/compiler/program owner；构建期额外发现到的宿主模块信息被写入 `Intermediate/UnrealCSharp_Modules.json`，作为结构化索引，而不是转化成新的临时 UE 模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-54`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:140-143`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:171-211` | 若确实需要表达“当前环境下发现了哪些模块”，更合适的做法是生成 manifest / index，而不是让模块图本身漂移。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 bind shard 从“工作区临时模块”降级为“固定 owner 模块下的生成内容”，同时把当前 shard inventory 显式记录为结构化清单。 |
| 具体步骤 | 1. 第一阶段先新增稳定 owner，例如 `AngelscriptGeneratedBindsRuntime` / `AngelscriptGeneratedBindsEditor`，或恢复一个受控的 `AngelscriptNativeBinds` 聚合模块；legacy generator 不再向 `Source/<ModuleName>/` 新建 sibling module，而是把生成 `.cpp` 写入这些固定 owner 的 `Private/Generated/` 或插件 `Intermediate/Angelscript/Generated/`。<br>2. 同步生成一份 `AngelscriptBindShardManifest.json`，记录本轮生成覆盖了哪些 package / class / shard，仅把它作为诊断与增量构建索引，不再作为运行时必须猜测的模块清单。<br>3. `FAngelscriptEngine` 的装载逻辑改为依赖固定 owner 模块，manifest 仅用于调试、增量生成和回归诊断；这样模块 inventory 回到 `.uplugin + checked-in Build.cs` 这条静态主线上。<br>4. 迁移期保留对旧 `BindModules.Cache` 的兼容读取，但仅用于一次性迁移和 warning，避免历史工作区状态继续决定当前模块图。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增的稳定 bind owner 模块与 `AngelscriptBindShardManifest.json` 生成入口 |
| 预估工作量 | L |
| 架构风险 | 风险在于迁移初期需要同时兼容“旧 shard cache + 新固定 owner”两套发现方式；如果 IDE project generation 或自定义脚本依赖了旧 `Source/ASRuntimeBind_*` 路径，需要一轮适配。 |
| 兼容性 | 对脚本 API 和绑定内容可保持向后兼容；主要变化落在构建与生成链。第一轮切换时可能需要清理旧 cache / 旧中间产物，但对插件使用者的脚本层行为应无直接破坏。 |
| 验证方式 | 1. 在 clean workspace 上重新生成 binds，确认 `Source/` 下不再出现新的临时 bind module 目录。<br>2. 比较迁移前后 editor target 的注册结果，确认脚本绑定覆盖率不下降。<br>3. 在另一台机器或新 clone 的工作区重复生成，确认模块 inventory 一致，不再依赖“谁最后生成过一次”。<br>4. 对当前 2026-04-08 工作区中观测到的 `ASRuntimeBind_*` 历史构建目录做一次清理/重建演练，确认生成结果可重复。 |

### Arch-MS-18：legacy generator 只有写入没有模块级收敛，历史 shard 状态容易残留在后续拓扑里

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind shard 在数量减少、分类变化或分支切换后，是否存在明确的 orphan module / stale cache 收敛机制 |
| 当前设计 | 生成器在每轮开始时只清空内存里的 `BindModuleNames`，随后按本轮结果重建列表并写回 `BindModules.Cache`；生成路径会写新的 `Build.cs`、module header 和 `Bind_<Class>.cpp`。在已检查到的逻辑里，只看到了“单个 `Bind_<Class>.cpp` 无内容时删除该文件”的清理，没有看到对过期 `ASRuntimeBind_*` / `ASEditorBind_*` 目录、旧 `*.Build.cs`、旧 module header 或旧 source tree 的统一 reconciliation。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1009` 在生成开始时只执行 `GetBindModuleNames().Empty()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1021-1057` 重新把本轮 shard 名加入列表。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1205-1206` 每次都直接写 `Build.cs` 与 module header。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1436-1440` 只在单个绑定函数为空时删除对应 `Bind_<Class>.cpp`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:584-601` 的 `SaveBindModules/LoadBindModules` 只是简单覆写/读取字符串数组，没有版本号、target hash 或 orphan 检查。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1477-1487` 运行时会无条件遍历 cache 中的模块名并尝试装载。<br>基于上述已检查路径的推断：当前代码对“历史 shard 目录如何删除、旧模块何时失效”没有显式协议，这使拓扑容易受历史生成状态影响。 |
| 优点 | 生成器实现简单，生成一轮就能直接写文件并启动下游构建，不需要额外维护 cleanup manifest 或 target/version metadata。 |
| 不足 | 只要 shard 数量减少、runtime/editor 归属变化、分支切换或支持模块集合变化，就可能出现“当前 cache 与历史文件树/中间产物不同步”的窗口。即使最终运行时只装载当前 cache 里的名字，历史 `Build.cs`、历史 source dir、历史 intermediate 仍可能污染 project generation、增量构建诊断和本地问题复现。这里关于“缺少模块级收敛机制”的结论，是基于已检查到的生成与装载代码路径作出的明确推断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense generator 的输出集合始终收敛在 `Intermediate/IntelliSense`；更新逻辑是按目标路径覆写内容，删除逻辑也针对同一稳定目录中的单文件，不需要处理“过期 UE 模块”的问题。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:47-48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-240` | 如果输出根固定，cleanup 就能退化成普通文件收敛，而不是模块拓扑收敛。 |
| puerts | `ParamDefaultValueMetas` 在固定模块里生成单个 `InitParamDefaultMetas.inl`，`FinishExport()` 只需比较并覆写这一目标文件，不存在 orphan module 清理问题。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:53-60`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-126` | 固定 owner + 固定输出路径，使“收敛”天然成为文件级问题。 |
| UnrealCSharp | `UnrealCSharpCore` 每轮重写同一个 `Intermediate/UnrealCSharp_Modules.json`，发现结果变化不会制造新的临时模块，也不要求额外删除历史 module dir。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:140-143`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:171-211` | 当生成结果只是稳定文件的内容变化时，历史状态更容易被自然覆盖。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在继续保留 legacy generator 的前提下，先补一层显式的 reconciliation / invalidation 协议，确保历史 shard 状态能被当前生成结果收敛。 |
| 具体步骤 | 1. 为 legacy generator 增加 `AngelscriptBindGenerationState.json`，至少记录本轮 target、platform、runtime/editor shard 名、每个 shard 的 package 集合和生成时间；`SaveBindModules()` 不再只写裸字符串列表。<br>2. 每次生成前先扫描旧的 generated root，对比 state/manifest 删除 orphan shard：过期目录、过期 `*.Build.cs`、过期 module header、过期 `Bind_<Class>.cpp` 都由单一 cleanup pass 收敛，而不是只在单文件为空时局部删除。<br>3. `LoadBindModules()` 增加最小校验，例如 target/config hash、manifest version 或 generated root 存在性；校验失败时拒绝装载旧 cache，并提示重新生成。<br>4. 如果后续按 Arch-MS-17 收敛到固定 owner 模块，则把 cleanup 逻辑下沉为“清理稳定 generated 目录”，逐步淘汰“清理整个临时模块”的复杂性。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增的 `AngelscriptBindGenerationState.json` / cleanup helper |
| 预估工作量 | M |
| 架构风险 | 主要风险在于 cleanup 规则过激时误删开发者保留的调试产物；因此第一阶段应把删除范围限制在 generator 明确登记过的 generated root，并保留 dry-run / warning 模式。 |
| 兼容性 | 对脚本 API 无直接影响；对开发流程的影响是第一次切换时会更积极地清理旧生成产物。只要把 generated root 限定清楚，向后兼容风险较低。 |
| 验证方式 | 1. 先生成一轮含较多 shard 的结果，再人为减少输入 package 集，确认 orphan shard 被自动删除而不是残留。<br>2. 切换 target 或分支后直接启动 editor，确认过期 cache 会被识别并要求重生，而不是静默装载旧模块名。<br>3. 对 build 日志与 project file 做对比，确认 cleanup 后不再保留历史 shard 的多余模块痕迹。<br>4. 在 CI 上增加一次“生成 -> 清理 -> 再生成”的往返测试，确认结果幂等。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-17 | bind shard 作为工作区临时模块导致模块清单不稳定 | 结构性重构 + 生成物归位 | 高 |
| P1 | Arch-MS-18 | legacy generator 缺少 orphan shard 收敛与失效协议 | 生成链治理 + 增量清理 | 高 |

---

## 架构分析 (2026-04-08 16:00)

### Arch-MS-19：`AngelscriptEditor` 把可选 editor satellite 静态并入主 shell，模块图缺少可裁剪的 leaf edge

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptEditor` 是否只承接最小 editor shell，而把 watcher、menu、asset/blueprint workflow 作为可选 satellite |
| 当前设计 | 当前静态 DAG 仍然主要表现为 `AngelscriptRuntime <- AngelscriptEditor <- AngelscriptTest`，但 `AngelscriptEditor` 自己直接持有 `Kismet`、`DirectoryWatcher`、`AssetTools`、`ToolMenus` 等多类 editor 工具能力；源码里这些能力又是在局部 feature 路径中按需 `LoadModuleChecked(...)` 使用。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 把 `BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`AssetTools`、`ToolMenus` 等都并入同一 editor shell。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:367-380` 在 `StartupModule()` 中直接注册 `DirectoryWatcher`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:422-423` 与 `:510-516` 只在创建 asset/blueprint 的路径中才加载 `AssetRegistry` 和 `KismetCompiler`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:673-848` 的菜单扩展逻辑再按需加载 `LevelEditor` 与 `ContentBrowser`。 |
| 优点 | 单模块 owner 让 editor 启动链和问题定位都很直接，当前所有 editor 能力只要装载 `AngelscriptEditor` 就能工作。 |
| 不足 | watcher、menu、asset workflow、blueprint tooling 都挤在同一个 module edge 上，导致这些能力既不能单独裁剪，也难以各自演进；未来想把某个 editor feature 变成 opt-in、动态加载或单独发布时，第一步仍然得先切开 `AngelscriptEditor` 这个 super-shell。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把 `Kismet`、`MainFrame`、`AnimationBlueprintEditor` 放到 `PrivateIncludePathModuleNames` 与 `DynamicallyLoadedModuleNames`，并在真正需要时才 `LoadModuleChecked<IMainFrameModule>()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:47-57`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:87-94`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:92-104` | 对 editor shell 的可选集成，先用动态边或私有 include 边承接，而不是全部变成主模块的硬静态边。 |
| puerts | 把菜单/声明生成职责直接拆进 `DeclarationGenerator` 模块；`ToolMenus`、`LevelEditor`、`AssetRegistry` 等 UI 与生成依赖不继续堆到 `PuertsEditor` 主 shell。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1654-1665` | 若某项工具能力有独立生命周期，优先给它单独 owner module。 |
| UnrealCSharp | `UnrealCSharpEditor` 至少把 `ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion` 保留在 private deps，不把 workflow owner 暴露成 editor shell 的外部 contract。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63` | 即便不做动态加载，也应先把主 shell 缩成最小 owner，把重工具链能力下沉到 private 或 leaf module。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptEditor` 收敛为最小 editor shell，再把 watcher、menu、asset/blueprint workflow 迁到独立 leaf module 或 dynamic edge。 |
| 具体步骤 | 1. 第一阶段不改用户可见行为，只把 `ScriptEditorMenuExtension.*` 与相关 `ToolMenus`/`LevelEditor`/`ContentBrowser` 逻辑迁到新模块 `AngelscriptEditorMenus`（命名可调整），由 `AngelscriptEditor` 在 editor ready 后显式加载，或通过 `DynamicallyLoadedModuleNames` 接入。<br>2. 第二阶段把 `DirectoryWatcher` 注册与对应测试迁到 `AngelscriptScriptWatcher`，让文件监听从主 shell 解耦；这样未来可以按设置关闭 watcher，而不是仍要装载整套 editor shell。<br>3. 第三阶段把 `AssetRegistry`、`KismetCompiler`、`AssetTools` 相关 blueprint/asset 创建逻辑迁到 `AngelscriptBlueprintTools` 或 `AngelscriptEditorAssetTools`，使“脚本编译/设置”与“资产创建工作流”分层。<br>4. 迁移期为旧 `AngelscriptEditor` public API 保留 forwarding wrapper，先稳定模块边界，再逐步收窄 `Build.cs` 依赖面。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/AssetToolsStatics.h`，以及新增的 `AngelscriptEditorMenus` / `AngelscriptScriptWatcher` / `AngelscriptBlueprintTools` 模块 |
| 预估工作量 | L |
| 架构风险 | editor feature 迁出后，现有依赖 `AngelscriptEditor` 的 C++ 扩展若直接 include 菜单或资产工具头，可能需要显式补新模块依赖；因此第一阶段应保留 wrapper 与原入口。 |
| 兼容性 | 对 script 用户和 editor 最终功能应保持兼容；对 C++ 扩展方，兼容影响主要是 `Build.cs` 依赖边收紧后需要补 leaf module。 |
| 验证方式 | 1. editor 启动后验证目录监听、菜单扩展、蓝图创建三条链路仍正常。<br>2. 修改仅菜单相关代码时，确认 watcher / blueprint tooling 模块不再跟着重编。<br>3. `AngelscriptEditor` 单独编译时，不再直接依赖 `DirectoryWatcher`、`AssetTools`、`KismetCompiler` 等 leaf feature 模块。 |

### Arch-MS-20：模块声明缺少显式 supported-target contract，平台与预编译范围仍靠默认值外溢

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块支持平台、host type 和预编译范围是否已经在声明层被说清楚 |
| 当前设计 | `Angelscript.uplugin` 目前只给三个模块声明了 `Name`、`Type` 和 `LoadingPhase`；没有 `WhitelistPlatforms`，也没有额外的 target-scope 元数据。与此同时，`AngelscriptRuntime` 已经直接持有 vendored AngelScript 依赖，`AngelscriptTest` 也只在 `Build.cs` 内部用 `Target.bBuildEditor` 补 editor 依赖，模块支持矩阵更多依赖默认行为而不是描述符契约。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 对 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 只声明了 `Type` 与 `LoadingPhase`，没有 `WhitelistPlatforms`。<br>`Plugins/Angelscript/Angelscript.uplugin:13` 让整个插件 `EnabledByDefault = true`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:20-22` 直接把 `ThirdParty/angelscript` 纳入 runtime 模块所有权。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:40-49` 只在 `Target.bBuildEditor` 下追加 `CQTest`、`UnrealEd`、`AngelscriptEditor`，但没有额外的预编译或平台范围声明。 |
| 优点 | 宽默认值让团队在新 target 或新平台上尝试构建时不需要先改描述符，短期探索成本较低。 |
| 不足 | support matrix 无法从声明层直接读出，分发、CI、预编译和问题复现都只能等到更后面的编译或链接阶段才暴露“这个模块其实没被验证过”；对外部使用者来说，插件看起来像是“所有声明的模块都面向所有默认 target”，但源码并没有给出同样清晰的承诺。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime/editor 在 `.uplugin` 中直接声明 `WhitelistPlatforms`；测试能力则留在独立的 `UnLuaTestSuite` 插件里，并在 `Build.cs` 中显式写出 `PrecompileForTargets = PrecompileTargetsType.Any`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:52-64` | 平台范围与预编译策略应成为声明层契约，而不是只靠默认值与约定。 |
| puerts | `.uplugin` 为 `WasmCore`、`JsEnv`、`Puerts` 显式标注 runtime 支持平台，同时把 `DeclarationGenerator` 和 `ParamDefaultValueMetas` 区分成 `Editor` / `Program` 两类 host type。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 即使模块数较多，也应把“谁在哪些 host 上存在”直接写进描述符。 |
| UnrealCSharp | `UnrealCSharp.uplugin` 虽未广泛使用 `WhitelistPlatforms`，但至少把 `SourceCodeGenerator` 明确声明为 `Program`，把 `Compiler`、`ScriptCodeGenerator`、`UnrealCSharpEditor` 与 runtime 层分开，host scope 在模块图里是可见的。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:17-53` | 若暂时不做平台白名单，也应先把 runtime/editor/program 的 host 边界静态说清。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把实际支持矩阵写成声明层 contract，再让 CI 和分发流程围绕这份 contract 运转。 |
| 具体步骤 | 1. 先根据当前 CI 与人工验证结果列出 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 的真实支持矩阵，例如“哪些平台已跑通过 editor/game/cook”。<br>2. 在 `Plugins/Angelscript/Angelscript.uplugin` 中为已验证的平台补 `WhitelistPlatforms`；增量做法是先从 editor/test 模块开始，因为它们最容易与宿主平台能力绑定。<br>3. 对 `AngelscriptTest` 在 `AngelscriptTest.Build.cs` 中补显式 `PrecompileForTargets` 策略，并把它和 CI 的 automation target profile 对齐；若 CI 需要预编译测试模块就明确写成该策略，若发行包不需要则不要继续依赖默认值。<br>4. 把这份支持矩阵加入一次构建守卫：项目文件生成或 CI 初始化阶段检查 `.uplugin` 声明是否与当前验证矩阵一致，避免新平台支持在无记录的情况下悄悄外溢。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`，以及 CI / build graph 中读取模块支持矩阵的配置文件 |
| 预估工作量 | M |
| 架构风险 | 一旦把平台/target 范围写窄，过去依赖“默认也许能编过”的外部环境会更早失败；但这类失败是前移暴露真实支持边界，而不是引入新的运行时不兼容。 |
| 兼容性 | 对已验证平台应无行为变化；对未被白名单覆盖但过去侥幸能编的环境，会从“晚失败”变成“早失败”，属于有意的 contract 收紧。 |
| 验证方式 | 1. 在已声明支持的平台上重新生成 project files 并完成 editor/game 构建。<br>2. 选一个未列入支持矩阵的平台或 target 做一次验证，确认失败发生在描述符/构建入口而不是更深层链接阶段。<br>3. 检查 CI 日志，确认 `WhitelistPlatforms` / `PrecompileForTargets` 改动后，构建矩阵与文档声明一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-19 | editor shell 吸纳过多可选 satellite，缺少 leaf edge | 结构性拆分 + 动态依赖收敛 | 高 |
| P2 | Arch-MS-20 | 模块 support matrix 与预编译范围未显式声明 | 声明层治理 + 分发契约收敛 | 中 |

---

## 架构分析 (2026-04-08 16:34)

### Arch-MS-21：legacy bind shard 不是 dependency-closed 单元，函数签名扫描会在生成期继续扩张模块边

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind shard 的静态依赖边，是否在切分 shard 时已经确定，还是会在生成 `Bind_<Class>.cpp` 的过程中继续膨胀 |
| 当前设计 | `GenerateNativeBinds()` 只按 runtime/editor class DB 的 key 把 shard 初始切成 10-key bucket；但 `GenerateSourceFilesV2()` 在为每个 shard 生成源码时，会把 bucket 初始化成 `ModuleSet`，随后 `GenerateFunctionEntries()` 又根据 return type 和 parameter type 递归调用 `AddParameterInclude()`，把额外 package/module 名继续塞回同一个 `ModuleSet`。最后如果 `ModuleSet` 比初始 bucket 更大，就用扩张后的集合回写 `ModuleList`，再生成新的 shard `Build.cs`。也就是说，shard 在切 bucket 时并不是 dependency-closed，真正的静态依赖闭包要等函数签名扫描结束后才确定。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1329-1330` 先用 `ModuleList` 初始化 `ModuleSet` 并记录初始数量。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1420` 把 `ModuleSet` 传给 `GenerateFunctionEntries(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1928` 与 `:1974` 在处理 return type / parameter type 时都调用 `AddParameterInclude(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:2283-2295` 会从字段所属 package 提取 `ModuleName`，并把它追加进 `ModuleSet`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1446-1449` 若 `ModuleSet` 扩张，则用扩张后的集合覆写 `ModuleList`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1200` 与 `:1252-1276` 再把这个扩张后的 `ModuleList` 写入 shard `Build.cs` 的 `PrivateDependencyModuleNames`。 |
| 优点 | 生成器会把函数签名真正用到的模块一并补进 shard，短期内能降低 generated `.cpp` 因缺少 include 或 module dependency 而编不过的概率。 |
| 不足 | bucket 名称和初始 10-key 分组不再能代表 shard 的真实依赖闭包；一个看似局部的函数签名改动，可能在生成期把新的 module edge 注入现有 shard，导致 `Build.cs`、project file 和增量编译命中面一起漂移。更关键的是，模块边界在这里继续承担“签名依赖补全器”的职责，而不是只承载稳定的产品拓扑。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaIntelliSenseGenerator::Export(const UField*)` 虽然也按 `Package->GetName()` 推导输出目录，但最后只是调用 `SaveFile(ModuleName, FileName, Content)` 把结果写到 `Intermediate/IntelliSense/<Module>/<Type>.lua`，不会回写新的 UE module dependency。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233` | 生成内容可以随发现结果细化，但不应把“本轮扫描到了哪些类型”直接升级成新的静态模块边。 |
| puerts | `ParamDefaultValueMetas` 是固定 `Program` 模块；`GetGeneratedCodeModuleName()` 直接返回 `JsEnv`，`FinishExport()` 只覆写固定输出 `InitParamDefaultMetas.inl`，工具模块自己的 `Build.cs` 依赖保持稳定。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:37-51`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-127` | 应该把“生成内容覆盖范围”与“工具模块静态依赖”分成两层，避免生成期发现结果反向改写工具模块边。 |
| UnrealCSharp | `UnrealCSharpEditor::Generator()` 会串起 `FClassGenerator`、`FStructGenerator`、`FEnumGenerator`、`FBindingClassGenerator` 等多个 pass，但这些 pass 都运行在固定的 `ScriptCodeGenerator` / `Compiler` 模块里；`Build.cs` 依赖是静态声明的。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | 生成 pass 可以很多，输入规模也可以动态，但 owner module 的静态 DAG 应尽量保持稳定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“函数签名依赖闭包”从 shard 模块边界里拿出来，改成内容层 manifest / allowlist；模块 DAG 只保留稳定 owner。 |
| 具体步骤 | 1. 在 legacy generator 中新增一层 `AngelscriptBindDependencyClosure.json`（名字可调整），专门记录每个 generated file 或 shard 额外引用了哪些 module / include，但先不再据此新建或改写 UE shard module。<br>2. 把当前 `ModuleSet` 的扩张逻辑保留下来用于诊断和 include 补全，但把 `GenerateBuildFile()` 的输入改成固定 owner 模块的稳定依赖表，或少量显式 leaf support module 的 allowlist。<br>3. 对高频跨域依赖单独建稳定模块，例如 `AngelscriptGameplayBinds`、`AngelscriptEditorSupportBinds` 这类长期存在的功能域；只有功能域进入 DAG，签名层细节继续留在 manifest。<br>4. 迁移期增加 diff 检查：若旧路径会把某个新 module 注入 shard，而新 allowlist 尚未覆盖，则在生成日志中报出 module/class/function 维度的缺口，逐步补齐而不是静默丢失。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新增的 `Intermediate/Angelscript/AngelscriptBindDependencyClosure.json` 生成入口，以及未来承接稳定依赖的 bind owner 模块 |
| 预估工作量 | M |
| 架构风险 | 如果 allowlist 设计过窄，第一轮迁移容易把过去由 `ModuleSet` 自动兜底的隐式依赖暴露出来；因此应先保留旧扩张逻辑做诊断，而不是一步删掉。 |
| 兼容性 | 对脚本 API 和绑定注册顺序可保持向后兼容；变化主要落在生成链与模块声明。迁移期可能需要清理一次旧的 generated `Build.cs` / intermediate 产物。 |
| 验证方式 | 1. 人为给某个已存在绑定函数增加一个来自新 module 的 parameter type，确认新流程只更新 dependency manifest 或显式 allowlist 告警，而不再无提示改写 shard `Build.cs`。<br>2. 连续两次在相同源码状态下生成，确认固定 owner 的 `Build.cs` 不再因为签名闭包变化而漂移。<br>3. 对比迁移前后的 full rebuild / incremental build 日志，确认依赖图收敛后绑定覆盖率不下降。 |

### Arch-MS-22：primary UHT pipeline 已按真实 module 分片，legacy bucket shard 仍停留在另一套坐标系

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind/codegen pipeline 是否共享同一套分片坐标系，还是同时维护多套彼此不兼容的 shard 规则 |
| 当前设计 | 当前插件实际上并存两套分片坐标系。legacy `GenerateNativeBinds()` 以 runtime/editor class DB 的 key 为输入，按 `ModuleCount = 10` 切 bucket，并生成 `ASRuntimeBind_<起始索引>` / `ASEditorBind_<起始索引>` 这种 synthetic UE module；而 primary `AngelscriptUHTTool` 则直接遍历 `factory.Session.Modules`，按 `module.ShortName` 聚合条目，并用 `MaxEntriesPerShard = 256` 生成 `AS_FunctionTable_<ModuleShortName>_<Shard>.cpp`。前者是 bucket-centric synthetic module，后者是 module-centric generated source。结合 editor 菜单里“legacy generator 仅用于调试、UHT pipeline 才是 primary path”的说明，可以推断当前架构虽然已经拥有更稳定的 module-centric 分片模型，但 legacy 路径仍把模块拓扑拖在另一套坐标系上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722-730` 把 `GenerateNativeBinds()` 标成 `Legacy Native Bind Generator (Debug Only)`，并明确写出 `AngelscriptUHTTool pipeline is the primary path`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 的 legacy 路径按 `ModuleCount = 10` 生成 `ASRuntimeBind_*` / `ASEditorBind_*`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:49` 定义 `MaxEntriesPerShard = 256`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:59-67` 逐个遍历 `factory.Session.Modules` 生成模块摘要。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:115-123` 用 `AS_FunctionTable_<module.ShortName>_<shard>` 命名输出。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:432-446` 还对同一坐标系下的旧 `AS_FunctionTable_*.cpp` 做 stale cleanup。 |
| 优点 | legacy 路径保留了旧 `FunctionCallers` 时代的并行编译经验；UHT 路径则已经把分片收敛到真实 UE module 维度，并具备更好的 stale cleanup。两者并存让团队在迁移期还能对照验证。 |
| 不足 | 同一个插件现在既要维护“按 10 个 key 切 synthetic module”的拓扑规则，又要维护“按真实 module 切 generated source shard”的规则，导致 bind pipeline 的分片语义没有单一 contract。只要后续还保留 legacy 路径，任何关于 shard 数量、清理策略、缓存命名和诊断报表的讨论，都必须同时解释两套坐标系，模块管理复杂度不会真正下降。这里关于“当前已经有更稳定的 module-centric 分片模型”的结论，是基于 UHT generator 的实际实现与 editor menu 对 primary path 的说明作出的明确推断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 把 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector` 固定成 runtime/editor/program 三类 owner；IntelliSense 生成则统一落到 `Intermediate/IntelliSense`，由 `ModuleName + FileName` 组织，不再额外引入第二套 synthetic module 坐标系。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:47-48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233` | 生成输出可以细分，但分片命名与 owner contract 只保留一套。 |
| puerts | `.uplugin` 固定声明 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor`；`ParamDefaultValueMetas` 明确把 generated code owner 绑定到 `JsEnv`，最后只输出固定文件。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:37-51`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-127` | 工具链可以是多阶段，但 generated code 的 module owner 和输出坐标系应保持单一、显式。 |
| UnrealCSharp | `.uplugin` 把 `UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`SourceCodeGenerator` 等职责显式拆开；`GeneratorModules()` 额外写结构化索引 `UnrealCSharp_Modules.json`，而不是再并行维护另一套 synthetic module 命名。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-211`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305` | 若必须兼顾多条生成 pass，优先共享同一份 module/index contract，而不是让不同 pass 自己定义不同的 shard 轴。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 以“真实 UE module 名”作为唯一 shard 主键，逐步让 legacy 路径向 UHT 的 module-centric 分片坐标系收敛。 |
| 具体步骤 | 1. 先定义统一 shard contract，例如 `ModuleName + ShardIndex + EditorOnly`，并让它成为 `Angelscript` 生成链唯一的命名来源；UHT 路径直接沿用，legacy 路径则从 `ASRuntimeBind_<index>` / `ASEditorBind_<index>` 迁到同一 contract。<br>2. 若 legacy `FunctionCallers` 输出仍需保留，不再为它创建 synthetic UE module，而是把生成 `.cpp` 写入固定 owner 模块的 `Private/Generated/<ModuleName>/`，其 shard 命名与 UHT `AS_FunctionTable_<ModuleName>_<Shard>` 对齐。<br>3. 用统一 manifest 记录每个真实 UE module 对应的 generated file 集、shard 数和 editor/runtime 属性；`BindModules.Cache` 只保留过渡兼容，不再承载长期命名 authority。<br>4. 等 legacy 路径与 UHT 路径共享同一坐标系后，再决定是否彻底删除 debug-only legacy generator；这样后续关于并行度、缓存和 stale cleanup 的治理才只需维护一套模型。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增的统一 shard manifest |
| 预估工作量 | M |
| 架构风险 | 主要风险在于 legacy 调试链依赖旧模块名或旧 cache 命名；迁移期应保留 name mapping / compatibility loader，避免一次切换打断已有调试脚本。 |
| 兼容性 | 对脚本用户应保持向后兼容；对内部开发流程，影响主要是旧 `ASRuntimeBind_*` / `ASEditorBind_*` 名称会进入弃用期，第一次切换时可能需要清理旧 cache 和 intermediate。 |
| 验证方式 | 1. 同时运行 UHT primary path 与 legacy debug path，确认两条链最终落到同一 `ModuleName + ShardIndex` 坐标系。<br>2. 修改单个 UE module 的 BlueprintCallable 面，确认只影响该 module 对应的 generated shard，而不会再制造新的 synthetic UE module 名。<br>3. 执行一次 stale cleanup 回归，确认统一坐标系下旧 generated file 和旧 cache 都能被可靠回收。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-21 | legacy shard 在生成期继续扩张依赖闭包，模块边界不稳定 | 结构性收敛 + manifest / allowlist 解耦 | 高 |
| P1 | Arch-MS-22 | legacy bucket shard 与 UHT module shard 并存，缺少统一 shard contract | 生成链收敛 + 命名与坐标系统一 | 高 |

---

## 架构分析 (2026-04-08 16:50)

### Arch-MS-23：测试控制面被拆在 `AngelscriptRuntime` 与 `AngelscriptTest` 两个 owner，上层验证职责没有单一模块 authority

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | test / coverage / commandlet / fixture 是否由单一模块 owner 承载，还是分裂到 production runtime 与 test module 两侧 |
| 当前设计 | 公开模块图虽然已经把 `AngelscriptTest` 单独列成 editor module，但真正的 script 测试控制面仍留在 `AngelscriptRuntime`：runtime 直接持有 test settings、test discovery、unit/integration automation entry、code coverage 与 test commandlet；`AngelscriptTest` 则主要承载 C++ automation case 和反射/UHT fixture 类型。验证职责因此被拆成“runtime control plane + editor test surface”两个 owner。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 把 `AngelscriptTest` 声明为独立 `Editor` 模块。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h:33-100` 定义 `UAngelscriptTestSettings`，其中直接包含 `bEnableTestDiscovery`、`bEnableCodeCoverage`、`UnitTestNamingConvention`、`IntegrationTestMapRoot` 等测试控制项。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1459-1463` 在 runtime 初始化期直接 new `FAngelscriptCodeCoverage`；`:1628-1633` 再把 coverage 挂到 automation controller；`:2232-2244` 与 `:4142-4153` 在 runtime 中执行 test discovery。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp:22-49` 把 coverage 开关与 `AutomationController` 事件直接绑定到 `UAngelscriptTestSettings`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h:8-16` 还把 test commandlet 暴露为 `ANGELSCRIPTRUNTIME_API`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:660-740` 在 runtime 模块内注册 `FAngelscriptUnitTests` automation test，并继续读取 `UAngelscriptTestSettings`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:23-49` 显示 `AngelscriptTest` 只是在 editor 构建时再叠加 `AngelscriptEditor`、`CQTest`、`UnrealEd`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h:9-39`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h:17-79`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h:8-41` 说明 fixture/UHT coverage 类型又落在 `AngelscriptTest` 模块。 |
| 优点 | runtime 直接掌握 script module 与热重载状态，做 test discovery、coverage 和 hot-reload test runner 很方便；`AngelscriptTest` 单独承载大量 C++ automation case，也让验证入口在工程浏览层面可见。 |
| 不足 | 验证能力没有单一 authority，导致任何关于 test discovery、coverage、test settings、fixture 类型或 commandlet 的演进都必须同时改 runtime 与 test module；production runtime 也因此永久携带测试配置与 automation hook 语义，后续若要裁剪发行包、独立发布测试能力或把 runtime 再拆层，边界会先卡在这条“runtime 内置 test control plane”上。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把验证面放到单独的 `UnLuaTestSuite` 插件里，而且 `EnabledByDefault = false`；测试插件在 `Build.cs` 中私有依赖 `UnLua`，方向是 `TestSuite -> UnLua`，不是 runtime 反向持有测试控制面。fixture 类型也全部留在 test plugin 内。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestHelpers.h:67-209` | test fixture 与 automation owner 可以很多，但最好集中在独立 test plugin / module，并让依赖方向保持 `tests -> runtime`。 |
| UnrealCSharp | 主插件模块图显式覆盖 `Runtime / Core / Editor / ScriptCodeGenerator / Compiler / Program`，但 `.uplugin` 公开模块里没有把 test control plane 混进 runtime DAG。也就是说，它愿意把 workflow owner 提升为模块，却没有让 runtime 自己承担测试 authority。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53` | 即使模块数增加，也优先把 workflow 与 toolchain 明确成 owner；测试能力若需要存在，应该作为额外 leaf，而不是侵入 runtime 主干。 |
| puerts | `.uplugin` 把 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor` 分清 runtime/editor/program 边界，没有在 runtime 主模块里暴露 test/coverage owner。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 当插件已经需要多个 workflow module 时，更应该把“验证面是不是产品 runtime 的一部分”说清，而不是让 runtime 默认背测试控制职责。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 script test discovery / coverage / commandlet / settings 收敛到单一验证 owner，runtime 只保留可选 hook，不再直接持有完整 test control plane。 |
| 具体步骤 | 1. 第一阶段先抽象接口而不是立刻搬文件：新增 `IAngelscriptTestServices`（名字可调整）或等价 delegate surface，让 `FAngelscriptEngine` 通过可选服务查询“是否启用 test discovery / coverage”，而不是直接 `GetDefault<UAngelscriptTestSettings>()`。<br>2. 新增 `AngelscriptTestFramework`（建议 `Editor` 或独立 test plugin；若需要 commandlet/headless 可按宿主需求选择 `Developer`/单独 plugin）模块，逐步迁移 `Testing/AngelscriptTestSettings.*`、`Testing/DiscoverTests.*`、`Testing/UnitTest.*`、`Testing/IntegrationTest.*`、`CodeCoverage/*`、`Core/AngelscriptTestCommandlet.*`。<br>3. `AngelscriptTest` 只保留 C++ automation case 与 fixture/UHT coverage 类型，并改为私有依赖 `AngelscriptTestFramework`；这样验证拓扑变成 `AngelscriptRuntime <- AngelscriptTestFramework <- AngelscriptTest`。<br>4. 迁移期保留旧配置节与命令行参数，通过 config redirect 或兼容 adapter 把 `UAngelscriptTestSettings` 映射到新 owner，确保现有 CI、automation 名称和脚本测试入口不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`，以及新增的 `AngelscriptTestFramework` 模块/插件文件 |
| 预估工作量 | L |
| 架构风险 | 主要风险在于现有 automation 名称、commandlet 路径和配置节名被 CI / 本地脚本依赖；迁移必须先做兼容层，否则会把“模块边界收敛”变成“测试基础设施断档”。 |
| 兼容性 | 对 script API 基本可保持向后兼容；对开发工作流的影响集中在模块装载与测试配置归属。若采用独立 test plugin，建议在 editor/dev profile 默认启用、在发布配置允许关闭，以保持现有开发体验。 |
| 验证方式 | 1. editor 下执行现有 `Angelscript.UnitTests` 与 integration tests，确认 test 名称与结果不变。<br>2. 验证 code coverage 仍能在 automation 开始/结束时自动采集。<br>3. commandlet 路径回归通过。<br>4. 重新检查 Build DAG，确认 runtime 不再直接 include test settings / coverage / commandlet 头。 |

### Arch-MS-24：当前静态 DAG 无循环，但缺少 `core/base` 层，`AngelscriptRuntime` 成为所有上层能力的单点 supernode

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前模块图是否存在可复用的 runtime base / core layer，还是所有 editor、test、未来 leaf capability 都只能直接贴在 `AngelscriptRuntime` 上 |
| 当前设计 | 从 `.uplugin + Build.cs` 看，当前声明图是无静态循环的简单三层：`AngelscriptEditor -> AngelscriptRuntime`，`AngelscriptTest -> AngelscriptRuntime`，并在 editor build 下再私有接 `AngelscriptEditor`。问题不在循环，而在图过于扁平：`AngelscriptRuntime` 同时承担 vendor/VM seam、host dependency adapter、配置与测试控制、上层脚本运行能力，缺少一个能被 editor/test/tooling 复用的 `core/base` 中间层。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 公开模块只有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-22` 直接把 `ModuleDirectory`、`Core` 和 `ThirdParty/angelscript/source` 暴露为 public include；`:30-79` 再把 `DeveloperSettings`、`GameplayTags`、`StructUtils`、`EnhancedInput`、`GameplayAbilities`、`GameplayTasks`、`UnrealEd`、`EditorSubsystem` 等不同层次依赖都压进同一个 runtime 模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 让 editor 直接 public 依赖 `AngelscriptRuntime`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:12-49` 则让 test module public 依赖 `AngelscriptRuntime`、editor build 私有依赖 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:94`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:539-551` 表明 host dependency adapter `FAngelscriptEngineDependencies::CreateDefault()` 也落在 runtime core。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:458`、`:605` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1459-1633`、`:2232-2244` 进一步说明 test commandlet、coverage、test discovery 同样在同一个 engine owner 内。 |
| 优点 | 模块数少、声明 DAG 简单、当前 `Build.cs` 图本身没有明显静态循环；对初次接入者来说，知道“所有核心能力都在 `AngelscriptRuntime`”很直观。 |
| 不足 | 只要出现新的 leaf capability，就几乎只能二选一：继续依赖庞大的 `AngelscriptRuntime`，或者再走动态 shard / sidecar 特例。结果是 low-level ABI、host adapter、测试控制、可选 gameplay 领域和高层运行时逻辑都被同一个 supernode 吸附，后续任何“向下抽 core、向上拆 feature、向旁边挂 leaf module”的动作都会先撞上这个集中层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 明确做成三层 runtime DAG：`WasmCore` 负责最底层 wasm/third-party seam，`JsEnv` 再 public 依赖 `ParamDefaultValueMetas + WasmCore`，最上层 `Puerts` 只 public 依赖 `JsEnv`；editor/program tooling 再挂在旁路模块。 | `Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:39-79`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-152`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-26` | 先把 VM seam 固定在 base/core，再让产品 runtime 依赖 core，而不是让所有上层都贴到一个大 runtime 上。 |
| UnrealCSharp | 用 `UnrealCSharpCore + CrossVersion` 作为基座；上层 `UnrealCSharp` public 依赖 `CrossVersion + UnrealCSharpCore`，`UnrealCSharpEditor` 再私有编排 `ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-80`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-57`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-47` | 上层 runtime、editor、generator、compiler 都可以很多，但它们共享的是一个稳定 base layer，而不是共同咬住单个超级 runtime。 |
| UnLua | 虽然层级比 puerts / UnrealCSharp 更薄，但仍然把 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector` 分成 runtime / editor / program 三类 owner，让 tooling 不必直接长在 runtime supernode 上。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66` | 即使不引入太多 runtime 层级，也至少应把“基础运行时”和“editor/tooling owner”分成稳定边界。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不破坏现有模块名兼容性的前提下，先补一个可复用的 `AngelscriptCore` / `AngelscriptRuntimeBase` 层，把 low-level seam 与 host base 从 `AngelscriptRuntime` supernode 中剥离出来。 |
| 具体步骤 | 1. 第一阶段新增极小的 `AngelscriptCore`（名字可调整）runtime 模块，只承接 `ThirdParty/angelscript` 接缝、`FAngelscriptEngineDependencies`、`FAngelscriptModuleDesc`、基础 diagnostics / context stack / bind-manifest 接口这类“谁都要用、但不该等同于高层 runtime 行为”的内容。<br>2. 现有 `AngelscriptRuntime` 改为依赖 `AngelscriptCore`，逐步保留编译执行、高层绑定、hot reload、可选 gameplay bridge 等上层职责；与 Arch-MS-23 配合时，测试控制面不再继续留在 runtime。<br>3. `AngelscriptEditor`、`AngelscriptTest`、未来 bind/generator/support 模块优先依赖 `AngelscriptCore` 获取低层类型，只在确实需要执行/编译/高层 runtime API 时再依赖 `AngelscriptRuntime`。目标静态 DAG 收敛为 `AngelscriptCore <- AngelscriptRuntime <- AngelscriptEditor`，以及 `AngelscriptCore <- ... <- AngelscriptTest`。<br>4. 迁移期保持 `AngelscriptRuntime` 模块名与主要 public API 不变，通过 forwarding header、短期 re-export dependency 和 include shim 降低外部 C++ 调整成本。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增的 `AngelscriptCore` 模块文件 |
| 预估工作量 | L |
| 架构风险 | 风险主要在 C++ include 路径与链接所有权迁移：若一次移动过多低层类型，容易把旧 include 习惯、生成链和 editor/test 依赖一起打断；因此必须先建 core，再做逐目录迁移。 |
| 兼容性 | 对 script 用户应可保持兼容；对 C++ 集成者，短期通过 forwarding header / re-export dependency 维持兼容，长期才逐步收紧 include 面。静态模块图会增加一个新节点，但可换来更稳定的依赖方向。 |
| 验证方式 | 1. 更新后重新画 declared DAG，确认仍无静态循环，并且 editor/test 已不再只能直接贴 `AngelscriptRuntime`。<br>2. full rebuild 与增量编译回归，确认改动 `AngelscriptCore` 之外的高层代码时不再总是拖动整块 runtime。<br>3. editor、test、UHT/生成链全量编译通过，旧 public include 在兼容期仍可工作。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-23 | test / coverage / commandlet authority 分裂在 runtime 与 test module 两侧 | 结构性收敛 + 验证 owner 拆分 | 高 |
| P1 | Arch-MS-24 | 静态 DAG 缺少可复用 core/base 层，`AngelscriptRuntime` 过度中心化 | 结构性分层 + 依赖反转 | 高 |

---

## 架构分析 (2026-04-08 17:07)

### Arch-MS-25：测试分层已经存在三条 owner 线索，但模块图仍把它们压成“生产模块 + 单一 `AngelscriptTest`”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `Native API` 测试、runtime 内部测试、editor 内部测试是否已经形成独立模块边界 |
| 当前设计 | 仓库实际上已经通过目录和命名约定把测试分成三条线：`Source/AngelscriptTest/Native/` 走 public API、`Source/AngelscriptRuntime/Tests/` 走 runtime 内部白盒、`Source/AngelscriptEditor/Private/Tests/` 走 editor 内部白盒；但 `.uplugin` 只声明了一个 `AngelscriptTest` 模块，runtime/editor 内部测试继续跟生产模块共生，`AngelscriptTest.Build.cs` 还把 `Core`、`Dump`、`Internals`、`Preprocessor`、`ClassGenerator` 等目录全部开放给测试模块。 |
| 源码证据 | `Plugins/Angelscript/AGENTS.md:3-7` 明确区分 `Native`、`Runtime/Tests`、`Editor/Private/Tests` 三类测试层级。<br>`Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块，没有单独的 runtime/editor test owner。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:12-21` 把 `Core`、`Debugger`、`Dump`、`Internals`、`Native`、`Preprocessor`、`ClassGenerator` 全部加入 include path，`:23-49` 再把 `AngelscriptRuntime` 设为 public 依赖。<br>`Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeTestSupport.h:3-5` 的确只 include `AngelscriptInclude.h`，但 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h:3-21` 同时 include `AngelscriptEngine.h`、`Preprocessor/AngelscriptPreprocessor.h`、`ClassGenerator/ASClass.h` 和 `source/as_*`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp:17-40` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:15-38` 说明 runtime/editor 内部测试仍直接编译在生产模块里。 |
| 优点 | 不需要新增 UE 模块就能快速铺开 `Native API`、白盒 runtime、白盒 editor 三类验证；现有 Automation 前缀与目录规则也已经比较清楚。 |
| 不足 | 测试层级只存在于目录约定而不在 Build DAG 中，可执行拓扑看不出哪类测试应依赖 public API、哪类测试允许碰内部实现；`Native` 层虽然有规则约束，但与 `Shared`、`Internals`、`Preprocessor` 同处一个模块，静态上无法防止越界；runtime/editor 生产模块继续携带测试编译面，也让后续裁剪、预编译和 CI target 设计更难收口。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把验证面放进独立的 `UnLuaTestSuite` 插件，且 `EnabledByDefault = false`；测试模块自己依赖 `UnLua`，并通过独立的 `Public` test helper 暴露夹具类型，而不是把 runtime/editor 内部测试继续留在生产模块中。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestHelpers.h:22-24`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestHelpers.h:68-189` | “测试层级”一旦重要到需要命名规则和夹具 API，就值得有独立 owner；这样 public API 测试与内部白盒测试可以在插件边界上显式区分。 |
| UnrealCSharp | `.uplugin` 里把 `Runtime / Core / Editor / ScriptCodeGenerator / Compiler / Program` 都提升为显式模块，没有把“重要 workflow 只靠目录约定”藏在生产模块内部。这里虽不是测试插件，但它说明仓库会把长期职责提升成 owner，而不是继续依赖目录约定。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53` | 如果某个职责已经有独立命名、独立入口、独立 CI 语义，就更适合成为模块，而不是继续寄居在生产模块目录里。 |
| puerts | 同样把 `DeclarationGenerator`、`ParamDefaultValueMetas` 这类长期工具职责做成显式 `Editor` / `Program` 模块，而不是让 runtime/editor 主模块通过目录约定兼任。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 对 Angelscript 而言，`Native tests`、`Runtime internal tests`、`Editor internal tests` 已经具备类似的“长期职责”特征，可以沿用这种 owner-first 的拆法。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有三条测试层级从目录约定提升为显式模块或显式插件 owner，先固定依赖方向，再收紧 include 面。 |
| 具体步骤 | 1. 第一阶段先不动测试代码逻辑，只新增 owner：把 `Source/AngelscriptRuntime/Tests/` 抽成 `AngelscriptRuntimeInternalTests`，`Source/AngelscriptEditor/Private/Tests/` 抽成 `AngelscriptEditorInternalTests`，并把 `AngelscriptTest` 收敛为面向夹具、集成场景和 `Native API` 的测试 owner。<br>2. 对 `Native` 层再做一次细分：新增 `AngelscriptNativeTests`（`Runtime` 或 `Developer`，按宿主 CI 需求决定），只依赖 `AngelscriptRuntime` 的 public API；把 `Shared/AngelscriptTestUtilities.h` 一类白盒 helper 留在 `AngelscriptTest` 或新 `AngelscriptTestSupport` 中，不让 `Native` owner 继续看到 `Preprocessor`、`ClassGenerator`、`source/as_*`。<br>3. 迁移期保持现有 Automation 名称与前缀不变，仅调整文件 owner 和 Build.cs 依赖；这样 CI、测试过滤器和文档入口可以保持兼容。<br>4. owner 稳定后，再把 `AngelscriptTest.Build.cs` 中 blanket include path 收紧到实际需要的子集，确保目录规则开始由模块边界提供静态约束。 |
| 涉及文件 | `Plugins/Angelscript/AGENTS.md`、`Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/*`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/*`、`Plugins/Angelscript/Source/AngelscriptTest/Native/*`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/*`，以及新增的测试模块 `Build.cs` / `Module.cpp` |
| 预估工作量 | M |
| 架构风险 | 主要风险在 Automation target、过滤器和 CI 脚本对旧模块名/旧装载位置有隐式依赖；因此第一阶段必须只迁 owner、不改 test 名称和执行入口。 |
| 兼容性 | 对脚本用户和运行时行为无直接影响；对开发工作流的影响主要是测试模块装载与 include 可见性。采用兼容 Automation 名称后，向后兼容风险较低。 |
| 验证方式 | 1. 分别运行 `Native`、runtime internal、editor internal 三类自动化测试，确认名称、结果和过滤器行为不变。<br>2. 对 `AngelscriptNativeTests` 做一次 include 审计，确认它不再能直接 include `Preprocessor/*`、`ClassGenerator/*` 或 `source/as_*`。<br>3. 重新检查 `.uplugin + Build.cs` 模块图，确认测试 owner 已能从声明层读出，而不是只靠目录说明。 |

### Arch-MS-26：长期稳定的 `Preprocessor` / `ClassGenerator` / `Dump` 仍是目录级 pseudo-module，而额外模块预算优先给了临时 bind shard

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块拆分是否优先提升“长期稳定职责”，而不是只给生成期 bucket 提供模块身份 |
| 当前设计 | 当前额外模块几乎都来自 `GenerateNativeBinds()` 生成的 `ASRuntimeBind_*` / `ASEditorBind_*` bucket；与此同时，真正长期存在且跨模块复用的子系统仍停留在目录级 pseudo-module，例如 `Preprocessor`、`ClassGenerator`、`Dump`。也就是说，模块图对“临时并行编译单元”很敏感，对“稳定功能 owner”反而不敏感。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 把 bind 生成切成 `ASRuntimeBind_*` / `ASEditorBind_*` 模块，`:1166-1205` 和 `:1214-1276` 继续为这些 bucket 生成新的 `Build.cs`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5-7` 直接 include `Preprocessor/AngelscriptPreprocessor.h`、`ClassGenerator/AngelscriptClassGenerator.h`、`ClassGenerator/ASClass.h`，说明这些稳定子系统本来就是 runtime core 的长期依赖。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:3-4`、`:27` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:12`、`:52`、`:59`、`:139` 说明 editor 也直接依赖 `ClassGenerator` 的类型和 reload delegate。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:3-4`、`:118`、`:126` 说明 `Dump` 子系统已经被 editor 作为扩展点消费。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h:15-21` 与 `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:15-21` 则说明测试层也把 `Preprocessor`、`ClassGenerator`、`Dump` 当成长期依赖目录。 |
| 优点 | bind shard 能直接换来并行编译收益，不需要先定义新的 checked-in module contract；稳定子系统继续留在大模块内部，也降低了初始拆分成本。 |
| 不足 | 模块预算被优先花在“会随生成批次变化的 bucket”上，而不是“多年都会存在的功能 owner”；结果是 review 和依赖图里最醒目的不是稳定职责，而是临时分片。后续若要把 `Preprocessor`、`ClassGenerator`、`Dump` 之类能力做成可复用 leaf module，迁移成本反而高于处理 bind shard。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把稳定职责拆成 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas` 等显式 owner；这些模块的边界反映的是长期角色，而不是本次生成跑出了多少 bucket。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-45` | 先把长期角色变成模块，再考虑生成文件怎样落盘；不要让 generated shard 反客为主。 |
| UnrealCSharp | `UnrealCSharpCore`、`ScriptCodeGenerator`、`Compiler` 都是长期 owner；尤其 `ScriptCodeGenerator.Build.cs` 与 `Compiler.Build.cs` 说明代码生成和编译职责被固定为稳定模块，而不是临时输出分片。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | 对 Angelscript 而言，`Preprocessor`、`ClassGenerator`、`Dump` 更像这类“稳定 workflow owner”，比 bind bucket 更适合优先成为 leaf module。 |
| UnLua | `UnLuaDefaultParamCollector` 也是固定 `Program` owner，职责长期稳定；即便它处理的输入会变，模块 identity 仍然不变。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 把稳定 owner 固定下来后，生成内容可以变化，但模块图不必跟着抖动。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先停止继续把临时 bind bucket 当作主要模块扩展手段，再按“长期职责优先”的顺序抽取真实 leaf module。 |
| 具体步骤 | 1. 先沿用前几轮关于 shard manifest / 固定 owner 的方向，把 bind shard 收敛到稳定 owner，避免再新增更多 synthetic module。<br>2. 第一优先级建议抽 `AngelscriptPreprocessor`：它已经有独立目录、runtime 直接依赖、测试直接依赖，而且不必像 `ClassGenerator` 那样一开始就牵动全部 reload 流程。第一阶段只迁 `Preprocessor/AngelscriptPreprocessor.*` 与必要的最小依赖，`AngelscriptRuntime`、`AngelscriptTest` 改为显式依赖新模块。<br>3. 第二阶段再评估 `ClassGenerator` 与 `Dump`：`ClassGenerator` 更适合拆成 `AngelscriptReloadAnalysis` / `AngelscriptClassModel` 一类 owner，`Dump` 则可收敛为 `AngelscriptDiagnostics` 或 `AngelscriptStateDump` leaf module，供 runtime、editor、test 共同依赖。<br>4. 迁移期保留 `AngelscriptRuntime` 下的 forwarding header 和短期 re-export dependency，确保现有 C++ include 与 script API 不被一次性打断。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/*`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/*`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/*`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`，以及新增 leaf module 的 `Build.cs` / `Module.cpp` |
| 预估工作量 | L |
| 架构风险 | `ClassGenerator` 与 reload/hot-reload 路径耦合较深，若一开始就一起迁会把很多问题混在一起；因此要先选 `Preprocessor` 这类边界相对干净的稳定子系统做样板。 |
| 兼容性 | 对脚本用户基本无感；对 C++ 扩展若直接 include `Preprocessor/*`、`ClassGenerator/*`、`Dump/*`，需要一段 forwarding/shim 兼容期。属于可控的中等兼容性风险。 |
| 验证方式 | 1. 提取 `AngelscriptPreprocessor` 后，重新编译 runtime/editor/test，确认依赖方向变成“owner module 显式依赖 leaf module”，而不是继续走 blanket include path。<br>2. 跑现有 preprocessor 相关自动化测试与至少一条 editor reload 路径，确认功能不变。<br>3. 对模块图做一次人工审查，确认新增模块对应的是长期职责，而不是新的 generated bucket。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-25 | 测试层级只靠目录规约，未形成显式 test owner DAG | 结构性收敛 + 测试 owner 拆分 | 高 |
| P1 | Arch-MS-26 | 稳定子系统仍是目录级 pseudo-module，模块预算优先给了临时 bind shard | 结构性重排 + 渐进式 leaf module 提升 | 高 |

---

## 架构分析 (2026-04-08 17:23)

### Arch-MS-27：`AngelscriptRuntime` 的 public header 面与 `Build.cs` public contract 不自洽，private deps 正在承担公开 API

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | runtime 模块对外承诺的 C++ contract，是否真的由 `PublicDependencyModuleNames` 和稳定 public header 一致表达 |
| 当前设计 | `AngelscriptRuntime` 通过 `PublicIncludePaths` 把整个 `ModuleDirectory` 与 `Core/` 暴露给外部，但 `Build.cs` 的 public deps 只声明到 `ApplicationCore/Core/CoreUObject/Engine/EngineSettings/DeveloperSettings/Json/JsonUtilities/GameplayTags/StructUtils`。与此同时，多个 `ANGELSCRIPTRUNTIME_API` 类型和可公开 include 的 header 直接依赖 `GameplayAbilities`、`GameplayTasks`、`EnhancedInput`、`UMG`、`InputCore` 这类仍被放在 private deps 的模块。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-22` 把 `ModuleDirectory`、`Core` 和 `ThirdParty/angelscript` 暴露为 public include path。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-42` 的 public deps 不含 `GameplayAbilities`、`GameplayTasks`、`EnhancedInput`、`UMG`、`InputCore`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:45-65` 却把这些模块放进 private deps。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTask.h:5-10` 在 exported class `UAngelscriptAbilityTask` 之前直接 include `Abilities/Tasks/AbilityTask.h`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h:5-18`、`:44-45` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h:5-14`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGASAbility.h:5-13` 都把 GAS 类型放进 runtime 可见头。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.h:7-9`、`:13-14`、`:33-35`、`:148-149` 进一步把 `GameplayEffectTypes.h`、`AbilitySystemComponent.h` 和多组 exported type 固化到 public header。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h:3-5` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h:3-4` 说明，借助宽 `PublicIncludePaths`，连 `InputCore` / `UMG` 相关 helper header 也已处于“外部可 include”状态。 |
| 优点 | 代码就地可用，脚本 runtime wrapper 不需要先经过额外 leaf module 或 façade；团队在内部模块间复用这些类型时几乎没有阻力。 |
| 不足 | `Build.cs` 不再是可信的 public contract。外部 C++ consumer 即使只依赖 `AngelscriptRuntime`，也可能因为 include 某个“看起来公开”的 header 而隐式需要 private deps；后续想把 GAS / Input / UI wrapper 下沉到 leaf module 时，还会同时撞上 accidental public surface 与错误的依赖声明。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime public deps 保持在 `Core/CoreUObject/Engine/Slate/InputCore/Lua`，editor 相关只在 `Target.bBuildEditor` 下作为 private deps 增补。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66` | 即使 runtime 有 editor-aware 分支，也应保证 public deps 代表真实的 public contract。 |
| puerts | `Puerts` 的 runtime public deps 固定为 `Core/CoreUObject/Engine/InputCore/Serialization/OpenSSL/UMG/JsEnv`，editor 依赖 `UnrealEd` 仍留在 private；更底层能力再由 `JsEnv`、`WasmCore` 分层承接。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-25`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98`<br>`Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:56-79` | feature 依赖可以存在，但最好通过稳定层级公开，而不是让 private deps 与 public header 长期失配。 |
| UnrealCSharp | 上层 runtime 只把 `Core/Engine/CrossVersion/UnrealCSharpCore` 放进 public deps，把 `EnhancedInput` 等 feature 依赖留在 private deps；模块结构本身就在表达“这不是 public runtime contract”。 | `Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58` | 若某项能力不准备进入基础 runtime contract，就应通过更高层或更窄的 owner 暴露，而不是留在当前模块的可公开 header 中。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先让 `AngelscriptRuntime` 的 public header 面与 `Build.cs` public deps 对齐，再决定哪些 feature header 应迁到 leaf module。 |
| 具体步骤 | 1. 先补一个最小 `dummy consumer` 验证模块，只依赖 `AngelscriptRuntime` 并 include 当前所有可公开 header，借此列出“公开 header 依赖了哪些未声明 public deps”。<br>2. 对每个命中的 header 做二选一裁决：如果它真是 public C++ contract，就把对应依赖显式提升到 public，或迁入拥有该依赖的独立 feature module；如果它只是内部 helper，就移入 `Private/` 或改成 façade/bridge header。<br>3. 第一批优先处理 `Core/AngelscriptAbilityTask*.h`、`Core/AngelscriptAbilityAsyncLibrary.h`、`Core/AngelscriptGASAbility.h`、`Core/AngelscriptAbilitySystemComponent.h`，以及 `FunctionLibraries/InputComponentScriptMixinLibrary.h`、`FunctionLibraries/WidgetBlueprintStatics.h`，因为它们最直接体现了当前 contract 失配。<br>4. 收敛后加一条静态守卫：public header 只允许 include 出现在 `PublicDependencyModuleNames` 中的模块；新增 feature wrapper 必须明确落在 `AngelscriptRuntime` 还是新的 leaf module。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTask.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGASAbility.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`，以及新增的 contract-check module 或 leaf module 文件 |
| 预估工作量 | M |
| 架构风险 | 如果简单把所有缺失依赖一股脑抬进 public deps，会让 `AngelscriptRuntime` 进一步膨胀；风险更低的做法是先用 contract-check 列表区分“必须公开”与“应当收回”的 header。 |
| 兼容性 | 对脚本侧基本无影响；对 C++ 扩展，短期可能先经历一次“public deps 更真实”的兼容性改善，后续若把 feature header 迁入 leaf module，则需要显式补新的模块依赖。整体属于可增量治理的中等兼容性影响。 |
| 验证方式 | 1. `dummy consumer` 仅依赖 `AngelscriptRuntime` 即可编译通过。<br>2. 审计后再次扫描 public header，确认它们只 include 来自 `PublicDependencyModuleNames` 的模块。<br>3. editor/game target 全量编译，确认 contract 修正没有破坏现有 script/runtime 行为。 |

### Arch-MS-28：UHT 参与关系只写在 C# exporter attribute 中，`.uplugin` 没有把 host contract 显式声明出来

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件是否在声明层清楚表达“自己会参与 UHT / 代码生成 host 链路” |
| 当前设计 | `Angelscript` 实际上已经有活跃的 UHT exporter：`AngelscriptFunctionTableExporter` 通过 `[UnrealHeaderTool]` 和 `[UhtExporter(... ModuleName = "AngelscriptRuntime")]` 参与 `AS_FunctionTable_*.cpp` 生成。但 `.uplugin` 只声明了模块与依赖插件，没有 `CanBeUsedWithUnrealHeaderTool` 元数据，因此这一 host contract 只能靠阅读 `Source/AngelscriptUHTTool/*.cs` 才能知道。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:1-49` 包含 `EnabledByDefault`、`CanContainContent`、`Modules`、`Plugins`，但没有 `CanBeUsedWithUnrealHeaderTool`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:12-27` 通过 `[UnrealHeaderTool]` 与 `[UhtExporter(Name = "AngelscriptFunctionTable", ... ModuleName = "AngelscriptRuntime")]` 注册 exporter。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-75` 与 `:59-66` 直接遍历 `factory.Session.Modules` 并为受支持模块生成 `AS_FunctionTable_*` 输出，说明 UHT 参与关系不是文档占位，而是真实 build-time lane。 |
| 优点 | 不需要为了一个 sidecar 立即增加新的 UE 模块名；tooling 复杂度被收在 `Source/AngelscriptUHTTool` 内，主插件描述符保持精简。 |
| 不足 | 声明层读不到 UHT host scope，build/debug/onboarding 都必须跨到 C# 实现才能确认插件是否参与 UHT；这使模块图与 tooling host contract 脱节，也削弱了与参考插件在描述符层可见性上的一致性。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 直接声明 `CanBeUsedWithUnrealHeaderTool = true`，同时把 `UnLuaDefaultParamCollector` 挂成显式 `Program` 模块。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:16`<br>`Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40` | 即使具体 exporter 实现在别处，descriptor 也应先告诉读者“这个插件参与 UHT”。 |
| puerts | `.uplugin` 同样显式打开 `CanBeUsedWithUnrealHeaderTool`，并把 `DeclarationGenerator` / `ParamDefaultValueMetas` 区分成 `Editor` / `Program` tooling owner。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:14-37` | host type 与生成职责应尽量在描述符层就可见。 |
| UnrealCSharp | `.uplugin` 明确声明 `CanBeUsedWithUnrealHeaderTool = true`，再把 `SourceCodeGenerator` 提升为 `Program` 模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:17-18`<br>`Reference/UnrealCSharp/UnrealCSharp.uplugin:49-53` | 哪怕工具实现细节很复杂，最外层 contract 仍应先在 `.uplugin` 里被看见。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先补 descriptor-level 的 UHT metadata，让 host contract 在 `.uplugin` 层可见，再决定是否需要更重的 tooling owner 拆分。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Angelscript.uplugin` 中显式补上 `"CanBeUsedWithUnrealHeaderTool": true`，不改任何现有模块名、生成输出名或 sidecar 结构。<br>2. 在 `Source/AngelscriptUHTTool/` 或 `Documents/` 增加一份短文档/manifest，明确 `AngelscriptFunctionTable` exporter、owner module `AngelscriptRuntime`、输出模式 `AS_FunctionTable_*.cpp` 与诊断入口，避免后续仍要靠读 attribute 追溯。<br>3. 只有当 UHT 职责继续扩张到多个 exporter 或独立调度链时，再评估引入更显式的 tooling owner；这一步不应阻塞 descriptor metadata 的立即修复。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及新增的 tooling manifest / 文档文件 |
| 预估工作量 | S |
| 架构风险 | 元数据修复本身风险很低；真正的风险在于后续如果继续把更多 tooling logic 藏在 sidecar 内、却仍不更新 descriptor 和文档，可见性问题会再次累积。 |
| 兼容性 | 对现有脚本、C++ API 和生成输出应无破坏性影响；属于 descriptor / 文档层的向后兼容增强。 |
| 验证方式 | 1. 更新后重新运行一次 UHT/function-table 生成，确认 exporter 仍按原路径输出。<br>2. 检查 `.uplugin` 读取结果或项目文件生成日志，确认插件已被标记为 UHT-capable。<br>3. 让未读过 `AngelscriptUHTTool` 实现的维护者仅凭 `.uplugin + manifest` 就能定位生成 owner 与输出文件模式。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-27 | runtime public header 面与 `Build.cs` public contract 失配 | 声明层收敛 + feature 边界校正 | 高 |
| P2 | Arch-MS-28 | UHT host contract 未在 `.uplugin` 显式声明 | 元数据补全 + tooling 可见性增强 | 中 |

---

## 架构分析 (2026-04-08 17:36)

### Arch-MS-29：bind 分片预算仍沿用 legacy 模块口径，但主线工作负载已经完全改由 UHT summary 暴露

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `12+4` 这类 bind shard 预算，是否仍然反映当前主线生成链的真实工作负载 |
| 当前设计 | 当前源码已经明确把 `AngelscriptUHTTool` 标成 primary path，并且会按真实 `UhtModule` 生成 `AS_FunctionTable_*` shard，同时产出 `Summary.json` 与 `ModuleSummary.csv`；但 legacy `GenerateNativeBinds()` 仍保留固定 `ModuleCount = 10` 的 runtime/editor 双桶切法。这意味着团队讨论“当前有多少 bind 模块”时，仍容易沿用旧的 synthetic module 口径，而不是基于当前主线生成链的实测分布。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722-730` 把 `GenerateNativeBinds()` 明确标记为 `Legacy Native Bind Generator (Debug Only)`，并写明 `AngelscriptUHTTool pipeline is the primary path`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 仍把 legacy shard 固定切成 `ASRuntimeBind_*` / `ASEditorBind_*` 两类 bucket。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:49-75`、`:115-123` 按真实 `module.ShortName` 与 `MaxEntriesPerShard = 256` 生成 shard。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:166-205`、`:218-241` 会额外写出 `AS_FunctionTable_Summary.json` 与 `AS_FunctionTable_ModuleSummary.csv`。<br>2026-04-08 当前工作区观测：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv` 显示 runtime 侧为 12 个真实模块、5839 条 entry、30 个 shard file，而 editor-only 侧仅 2 个真实模块、204 条 entry、2 个 shard file；其中 `Engine` 一项就占 4054 条 entry / 16 个 shard，`UMG` 占 753 / 3，`AngelscriptRuntime` 占 408 / 2。 |
| 优点 | 主线生成链已经具备按真实模块和真实条目数观测工作负载的能力，继续优化分片不需要再靠猜测。 |
| 不足 | 当前的架构讨论口径仍容易被 legacy `12+4` 模块印象主导，但实际 hotspot 已经高度集中在少数 runtime module，editor-only 体量则明显更小。继续用固定 runtime/editor 模块预算做设计，会把并行编译优化和模块管理复杂度绑在过时假设上。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 运行时与工具链 owner 固定为 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector`；生成输出按 `Intermediate/IntelliSense/<ModuleName>/...` 组织，但不会再给 runtime/editor 分别预留一组对称 UE 模块预算。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 可以把输出分散到很多文件，但模块预算仍应由稳定 owner 决定，而不是由“预计会有多少批次”决定。 |
| puerts | `DeclarationGenerator` 与 `ParamDefaultValueMetas` 都是固定 owner；生成和默认值收集以固定模块承载，不靠预设一组 runtime/editor synthetic module 预算。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44` | 真正需要调节的是 owner 内部的 generated file 粒度，而不是先把额外 UE 模块数量拍死。 |
| UnrealCSharp | `UnrealCSharpEditor` 组合 `ScriptCodeGenerator` 与 `Compiler` 两个固定工具模块；是否做更多生成 pass，不会再演化成“再加几组 Editor/Runtime bind 模块”。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | 生成复杂度增长时，优先增加固定 owner 内的 pass / file / telemetry，而不是把模块数量当作主要调节旋钮。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 停止把 `12+4` 当作当前主线架构事实，改由 UHT summary 驱动 bind 分片预算，并把并行度收敛到固定 owner 内的 file shard。 |
| 具体步骤 | 1. 先把 `AS_FunctionTable_Summary.json` / `AS_FunctionTable_ModuleSummary.csv` 提升为正式的 build telemetry，纳入 CI artifact，而不是仅作为调试输出。<br>2. 以 `module totalEntries + compile time` 为分片依据，制定阈值策略：例如 editor-only 侧在 entry 数仍维持低量级时保持单 owner 或双 shard；runtime 侧仅对 `Engine`、`UMG`、`AngelscriptRuntime` 这类热点模块增加 file shard。<br>3. 若 legacy debug path 仍需保留，让它读取同一份 telemetry / manifest，只在固定 bind owner 内产出 `.cpp` shard，不再继续维护一套“runtime 12 + editor 4”式 synthetic UE module 预算。<br>4. 在架构文档、脚本和日志中统一改口径：讨论“generated shard files per real module”，而不是“额外新建多少个 bind module”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及消费 summary 的 CI / build report 配置 |
| 预估工作量 | M |
| 架构风险 | 如果只引入 telemetry 而不修改旧日志/脚本，团队仍可能继续沿用旧口径；因此需要把“summary 是唯一预算依据”同步进生成脚本与诊断输出。 |
| 兼容性 | 对现有脚本 API 无影响；变化集中在生成策略、CI 指标和构建输出布局，属于低兼容性风险的增量治理。 |
| 验证方式 | 1. 连续两次生成后比对 summary，确认相同源码状态下 runtime/editor workload 统计稳定。<br>2. 调整单个热点模块的可绑定表面，确认只影响该真实模块对应的 shard 预算。<br>3. 比较改造前后的 full rebuild / incremental build 日志，确认分片策略调整依据变成 summary，而不再依赖固定 `12+4` 假设。 |

### Arch-MS-30：`AngelscriptNativeBinds*` 已经失去源码 authority，但 ghost owner 仍留在当前构建图里

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前 build graph 中是否仍存在“源码和描述符已经不承认，但构建中间产物仍保留”的 ghost owner 模块 |
| 当前设计 | 当前插件的声明层只承认 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 UE 模块；`GenerateNativeBinds()` 里用于生成 `AngelscriptNativeBinds` 聚合 owner 的代码也已经被注释掉。但 2026-04-08 当前工作区的 `Intermediate/Build` 仍保留 `AngelscriptNativeBinds` 与 `AngelscriptNativeBindsEditor` 的 UBT definitions，说明 build graph 里还残留一条“源码无法重新生成、描述符也未声明”的 dead owner 分支。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1068-1075` 中 `GenerateBuildFile("AngelscriptNativeBinds", ...)` 与对应保存逻辑已被整体注释掉。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166-1206` 当前只会为临时 `ASRuntimeBind_*` / `ASEditorBind_*` 写 `Build.cs` 与 header。<br>2026-04-08 当前工作区观测：`Plugins/Angelscript/Source/` 下仅存在 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`、`AngelscriptUHTTool` 四个源码目录；但 `Plugins/Angelscript/Intermediate/Build/Win64/x64/UnrealEditorGPF/Development/AngelscriptNativeBinds/Definitions.AngelscriptNativeBinds.h:18` 与 `.../AngelscriptNativeBindsEditor/Definitions.AngelscriptNativeBindsEditor.h:18` 仍分别写着 `UE_MODULE_NAME "AngelscriptNativeBinds"` / `"AngelscriptNativeBindsEditor"`。 |
| 优点 | 对旧工作区或旧增量构建来说，ghost owner 残留有时能让历史编译状态继续工作，短期内不必立刻 full clean。 |
| 不足 | 这已经不是普通的 stale shard file，而是 stale owner module。架构图、构建诊断和工作区状态会同时出现“源码没有、`.uplugin` 没声明、UBT 却还认识”的模块名，导致问题复现、CI 清理策略和模块 inventory 审查都缺少可信边界。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime/editor/program owner 都在 `.uplugin` 里显式声明；`UnLuaDefaultParamCollector` 也有自己的 checked-in `Build.cs`，不存在只能从旧 `Intermediate` 里看到、却找不到源码 owner 的模块。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 只要某个 owner 仍参与 build graph，它就应该在声明层和源码树里被看见。 |
| puerts | `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor` 都有明确 `.uplugin + Build.cs` 对应关系。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44` | toolchain owner 要么是显式模块，要么就不该继续留在 build inventory 里。 |
| UnrealCSharp | `ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 都是描述符中可见、源码中存在的稳定 owner，没有“只剩中间产物定义”的悬空模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48`<br>`Reference/UnrealCSharp/Source/SourceCodeGenerator/SourceCodeGenerator.Build.cs:1-58` | 如果一个 owner 仍值得保留，就应该恢复成 checked-in 的稳定节点；否则应由清理协议明确退场。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 对 `AngelscriptNativeBinds*` 二选一：要么恢复为真实、稳定的 checked-in owner，要么把它们从构建图中彻底驱逐，并增加 ghost owner 审计。 |
| 具体步骤 | 1. 先新增一个 prebuild audit，基于 `.uplugin` 与 manifest 生成“当前合法 owner 模块清单”；若 `Intermediate/Build/.../Definitions.*.h` 中出现清单外的 `UE_MODULE_NAME`，直接 warning 并触发定向清理，或在 CI 上 fail fast。<br>2. 如果团队仍需要 `AngelscriptNativeBinds` / `AngelscriptNativeBindsEditor` 作为聚合 owner，就把它们恢复成 checked-in 的稳定模块：补回源码目录、`Build.cs`、最小 `Module.cpp`，并让生成器重新把输出落到这些 owner 下。<br>3. 如果不再需要聚合 owner，就删除残余引用路径，并在 legacy generator 结束后断言当前工作区不存在 `AngelscriptNativeBinds*` 相关中间产物；第一次切换时强制 full clean。<br>4. 无论选哪条路，都额外写一份 `DeclaredAndGeneratedModules.json`，把“声明模块”“生成 owner”“临时 shard/file”分层列清，避免之后再次出现源码树与 build graph 的盲区。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 prebuild audit / module inventory manifest；若选择恢复 owner，还包括新增的 `Plugins/Angelscript/Source/AngelscriptNativeBinds/*` 与 `Plugins/Angelscript/Source/AngelscriptNativeBindsEditor/*` |
| 预估工作量 | S / M |
| 架构风险 | 若直接自动清理 ghost owner 而没有先枚举合法 owner，可能误删开发者保留的调试产物；因此第一阶段应先做审计和白名单，再决定自动清理还是 fail fast。 |
| 兼容性 | 对脚本 API 无直接影响；对开发流程的影响主要是第一次切换时需要清理 `Intermediate` 或补回稳定 owner。整体属于构建环境层的低兼容性风险。 |
| 验证方式 | 1. clean `Intermediate` 后重新生成与编译，确认 build graph 中不再出现无源码 authority 的 `AngelscriptNativeBinds*`。<br>2. 在 CI 上执行 audit，确认 `.uplugin + manifest` 之外不会再冒出额外 owner 名。<br>3. 若恢复聚合 owner，确认源码树、`.uplugin`、`Build.cs` 和中间产物中的 owner 名完全一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-29 | bind 分片预算缺少基于真实 workload 的闭环 | 生成策略收敛 + 度量驱动治理 | 高 |
| P1 | Arch-MS-30 | ghost owner 模块仍残留在当前 build graph | 构建图治理 + owner authority 收口 | 高 |

---

## 架构分析 (2026-04-08 17:48)

### Arch-MS-31：UHT 支持模块清单由 `AngelscriptRuntime.Build.cs` 文本解析派生，module inventory 缺少单一 authority

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 主线 `AngelscriptUHTTool` 如何判断“哪些 UE 模块属于可生成绑定的支持面”，这一判断是否有稳定的唯一来源 |
| 当前设计 | 当前主线 UHT generator 并不消费独立 manifest，而是先在 UHT session 中定位 `AngelscriptRuntime.Build.cs`，再逐行扫描 `DependencyModuleNames.AddRange` 与 `if (Target.bBuildEditor)` 文本，把扫描结果塞进 `supportedModules.All / EditorOnly`。因此 `.uplugin`、`AngelscriptRuntime.Build.cs`、`AngelscriptUHTTool` 三者共同决定 module inventory，但没有一个显式的单一 authority。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-48` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块与 3 个插件依赖。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-79` 才列出 runtime/editor build 实际依赖的 UE modules。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-385` 通过 `ResolveRuntimeBuildCsPath()` 找到 `AngelscriptRuntime.Build.cs`，按字符串匹配 `DependencyModuleNames.AddRange`、`if (Target.bBuildEditor)` 构造 `supportedModules`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:387-409` 说明这一解析依赖于从 UHT header path 反推 `Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` 的路径结构。 |
| 优点 | 不需要额外维护一份 checked-in manifest；只要 `AngelscriptRuntime.Build.cs` 继续罗列依赖，UHT generator 就能自动扩展支持面。 |
| 不足 | module inventory 语义被编码进另一个模块的 `Build.cs` 文本格式；`AngelscriptFunctionTableCodeGenerator` 现在对 `Build.cs` 的具体写法、缩进块和条件语句有语法级假设。`Build.cs` 既承担 UBT 依赖声明，又承担 UHT generator 的输入协议，导致模块 authority 分散且脆弱。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 模块职责先在 `.uplugin` 显式拆成 `UnLua` / `UnLuaEditor` / `UnLuaDefaultParamCollector`；`UnLuaEditor` 会在设置修改时 touch `UnLua.Build.cs` 触发重编译，但没有把 `Build.cs` 逐行解析成支持模块清单。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:16-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:134-140` | `Build.cs` 可以作为 invalidation trigger，但不应该同时充当 tooling inventory protocol。generator/program owner 应先是显式模块。 |
| puerts | `.uplugin` 直接声明 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor`；生成器与 runtime/core 的依赖关系通过固定模块图表达，而不是从某个 runtime `Build.cs` 文本倒推。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:14-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44` | 如果 generator 需要了解 host/runtime 能力，优先通过显式 owner 模块和固定依赖传递，而不是解析另一个模块的声明文本。 |
| UnrealCSharp | `UnrealCSharpCore` 在自己的 checked-in `Build.cs` 中扫描 `Project/Engine` 的 `*.Build.cs` 与 `.uplugin`，并把结果固化为 `Intermediate/UnrealCSharp_Modules.json`；下游 tooling 读取的是 manifest，而不是运行时模块的依赖列表。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:17-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-252`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49` | 即便需要动态发现 modules，也应先生成一份具名 manifest，把“发现逻辑”和“消费逻辑”解耦，而不是让 consumer 直接解析 producer 的 `Build.cs`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptRuntime.Build.cs -> UHT parser` 的隐式协议收敛为一个显式、可校验的 module manifest，让 UBT/UHT/legacy generator 共享同一份 inventory。 |
| 具体步骤 | 1. 先新增一个最小 manifest，例如 `Plugins/Angelscript/Intermediate/AngelscriptSupportedModules.json`，字段至少包含 `allModules`、`editorOnlyModules`、`sourceOfTruthVersion`；第一阶段仍可由当前 parser 生成，但 `AngelscriptFunctionTableCodeGenerator` 改为只读 manifest。<br>2. 第二阶段把 manifest producer 提升为稳定 owner：优先放在 `AngelscriptUHTTool` 旁的共享 C# helper，或新增 `Build/AngelscriptModuleInventory.cs` 供 `AngelscriptRuntime.Build.cs` 与 UHT tool 共用，避免再从文本反推语义。<br>3. legacy `GenerateNativeBinds()`、prebuild audit、ghost owner 审计都统一读取同一份 manifest，不再各自猜测 runtime/editor 边界。<br>4. 保留一个过渡期 fallback：当 manifest 缺失时允许回退旧 parser，但输出明确 warning，并在 CI 上逐步切到 manifest-only。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`，以及新增的 module inventory producer/manifest 文件 |
| 预估工作量 | M |
| 架构风险 | 若第一阶段只新增 manifest 却不统一 consumer，团队仍可能同时维护 parser 和 manifest 两套逻辑；必须尽快把 consumer 收敛到单通道。 |
| 兼容性 | 对脚本 API 无直接影响；对构建链属于向后兼容增强。过渡期允许 parser fallback，可降低现有工作区和 CI 的切换风险。 |
| 验证方式 | 1. 修改 `AngelscriptRuntime.Build.cs` 中的依赖集合，确认 manifest、UHT generator、legacy generator 观察到的 module inventory 完全一致。<br>2. 仅重排 `Build.cs` 的格式/缩进，不改依赖语义，确认生成结果不再发生变化。<br>3. 在缺失 manifest 的工作区验证 fallback warning 生效；在 CI 上验证 manifest-only 模式可稳定运行。 |

### Arch-MS-32：legacy bind shard 的模块身份与私有依赖桶对类枚举顺序敏感，`12+4` 并行度伴随缓存抖动

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | legacy `ASRuntimeBind_*` / `ASEditorBind_*` 分片除了“数量多”之外，模块 identity 与依赖拓扑是否稳定，是否利于编译缓存和模块治理 |
| 当前设计 | legacy generator 先遍历 `TObjectRange<UClass>()`，按 `Class->GetPackage()->GetName(Name)` 把类塞进 `RuntimeClassDB` / `EditorClassDB` 这两个 `TMap`；随后直接 `GetKeys(Keys)`，每 10 个 key 切成一个 shard，并把这些 key 再转换成 shard `Build.cs` 的 `PrivateDependencyModuleNames`。源码里没有任何 `Sort()` 或稳定化步骤，因此 shard 名称、每片的私有依赖桶、甚至 `ASRuntimeBind_0` 对应哪组 UE modules，都至少对当前类枚举顺序敏感。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:118-126` 定义 `RuntimeClassDB`、`EditorClassDB` 为 `TMap<FString, TArray<TObjectPtr<UClass>>>`，并维护 `BindModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1088-1159` 通过 `TObjectRange<UClass>()` 和 `Class->GetPackage()->GetName(Name)` 填充 runtime/editor class DB。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 直接 `GetKeys(Keys)` 后按 `ModuleCount = 10` 切成 `ASRuntimeBind_*` / `ASEditorBind_*`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166-1200` 为每个 shard 生成新的 `Build.cs`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1272` 把 `ModuleList` 条目再写成 shard 的 `PrivateDependencyModuleNames`；其中 `:1261-1269` 通过截取 `/Script/Engine` 这类包名的最后一段，把私有依赖转成具体模块名。 |
| 优点 | 这种做法确实能把巨量绑定拆成很多小 UE 模块，每片只私有依赖自己桶内的 UE modules，理论上能提升并行编译和局部重编译隔离。 |
| 不足 | 并行度是用“不稳定的 module identity”换来的：同一片 shard 的名字来自分桶起始索引，而不是稳定业务含义；依赖桶由当前枚举顺序和 DB key 集合决定，不利于编译缓存复用、问题复现和模块 inventory 审查。相比“固定 owner + file shards”，这里把波动提升到了 UE module 层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 顶层 owner 固定为 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector`，测试也被放进独立 `UnLuaTestSuite` 插件；模块 identity 不跟单次生成批次或 class bucket 绑定。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-30` | 即使内部生成文件很多，owner module 仍应稳定；可变性应该下沉到文件级输出，而不是 UE module 名本身。 |
| puerts | `DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 都是固定 module owner；runtime/core 也按 `WasmCore -> JsEnv -> Puerts` 固定分层，没有按“本次收集到多少类”再临时增减 UE modules。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-152`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56` | 若要提高并行度，应先在固定 owner 内做 file shard 或 pass shard，而不是让 module topology 跟数据分布同步抖动。 |
| UnrealCSharp | 固定使用 `UnrealCSharpCore`、`UnrealCSharp`、`ScriptCodeGenerator`、`Compiler` 等稳定 owner；动态发现得到的是 `UnrealCSharp_Modules.json` 这类 manifest，而不是临时生成 `Compiler_0`、`Compiler_1` 之类 synthetic modules。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-252`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49` | 支持面可以动态发现，但 owner 层最好保持稳定；波动应体现在 manifest 和 generated file，不应直接抬升到 `.uplugin` 同级的模块身份。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 legacy shard 稳定化，再逐步把并行度从“module shard”下沉为“fixed owner 内的 file shard”，减少 `12+4` 策略带来的模块管理成本。 |
| 具体步骤 | 1. 低风险第一步：在 `GenerateNativeBinds()` 里对 `Keys` 和每个 shard 的 `ModuleList` 做稳定排序，再写 `BindModules.Cache` 与 `Build.cs`；这样即便暂时保留 `ASRuntimeBind_*` / `ASEditorBind_*`，至少同一输入会得到同一组 shard 名与私有依赖顺序。<br>2. 第二步把 shard 名从“起始索引”改成“稳定 owner + 序号”或“真实模块短名 + 序号”，例如基于排序后的模块集合生成 deterministic name，避免 `ASRuntimeBind_0` 的语义完全取决于这次枚举结果。<br>3. 第三步才评估收口：保留并行编译，但让输出回到固定 owner（例如 `AngelscriptNativeBinds` / `AngelscriptNativeBindsEditor` 或 UHT primary path 的固定 owner）内部，使用 `.cpp` file shard 代替 synthetic UE modules。<br>4. 若团队决定 legacy path 只保留调试用途，则至少应把“稳定排序 + deterministic naming”做完，降低调试构建的不可复现性，再讨论是否彻底退场。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，以及可能新增的 deterministic naming / manifest helper |
| 预估工作量 | M |
| 架构风险 | 若直接改 shard 名而没有处理旧 cache 和旧中间产物，会造成一次性 full rebuild；因此应先做排序稳定化，再引入命名变更或 fixed owner 回收。 |
| 兼容性 | 对脚本 API 无影响；对开发者构建缓存和中间产物路径有一次性影响。保留旧 cache fallback 或要求切换点 full clean，可把兼容性风险控制在构建层。 |
| 验证方式 | 1. 相同源码状态下连续运行两次 legacy generator，确认 `BindModules.Cache`、生成 `Build.cs`、私有依赖列表完全一致。<br>2. 仅增删某个真实 UE module 的可绑定类，确认只影响预期 shard，而不是大面积重排全部 shard 名。<br>3. 比较改造前后的增量编译日志，确认 shard 稳定化后编译缓存命中和问题复现路径更可预测。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-31 | UHT/tooling module inventory authority 分裂 | manifest 收口 + 依赖协议显式化 | 高 |
| P2 | Arch-MS-32 | legacy bind shard 的稳定性与缓存抖动 | 分片稳定化 + module owner 收敛 | 中 |

---

## 架构分析 (2026-04-08 17:57)

### Arch-MS-33：`12+4` 绑定分片在 editor 半区退化为“多模块编译、单模块依赖”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor bind shard 是否真的形成了独立的模块边界，还是只是把编译单元拆细后继续挂在同一个 editor hub 上 |
| 当前设计 | `GenerateNativeBinds()` 会分别为 runtime/editor class DB 生成 shard，但 `GenerateNewModule(..., true)` 无条件把 `AngelscriptEditor` 注入 editor shard 的 `PublicDependencyModuleNames`；同时 `AngelscriptEditor.Build.cs` 自身又把 `UnrealEd`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate*`、`AssetTools` 等都放在 public 侧。结果是：无论 editor 半区最终是 4 个还是更多 shard，它们的拓扑都退化成 `ASEditorBind_* -> AngelscriptEditor -> {UnrealEd, BlueprintGraph, Kismet, ...}`，并且 `AngelscriptTest` 也走同一入口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1035-1057` 生成 `ASEditorBind_*`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1168-1177` 为 editor shard 构造 `PublicDepends`，并在 `bIsEditor` 时追加 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-26` 把 `UnrealEd`、`EditorSubsystem`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools` 公开给所有下游。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:40-49` 让测试模块在 editor target 下继续依赖 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp:197`、`:205`、`:359` 直接调用 `AngelscriptEditor::BlueprintImpact::*`。 |
| 优点 | 生成器实现简单，所有 editor 绑定和 editor 测试都能直接复用 `AngelscriptEditor` 已经拥有的编辑器框架依赖与工具函数。 |
| 不足 | `12+4` 策略在 runtime 半区提供的是“分片 + 相对独立依赖”，但在 editor 半区提供的更像“分片 + 共享重型 hub”；修改 `BlueprintGraph`/`AssetTools` 相关实现时，很容易放大到所有 editor shard 与 editor 测试。后续若要把 blueprint impact、asset scanning、directory watching 等能力继续拆分，就会先被 `AngelscriptEditor` 的 public surface 卡住。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把主要 editor 依赖都收在 `PrivateDependencyModuleNames` 和 `PrivateIncludePathModuleNames`，没有把 `BlueprintGraph`、`DirectoryWatcher`、`Slate*` 等公开给所有下游；同时 `.uplugin` 还把默认参数收集器单独拆成 `Program` 模块。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:47-95`<br>`Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40` | editor owner 可以很重，但它不必成为公共依赖放大器；workflow owner 也不必全部挂在同一个 editor module 上。 |
| puerts | `PuertsEditor` 本身依赖较多，但 `DeclarationGenerator` 与 `ParamDefaultValueMetas` 被拆成独立 `Editor/Program` 模块，至少把“声明生成”“默认值元数据”从单一 `PuertsEditor` hub 中拿出来。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44` | 即便 editor 主模块仍然偏重，也可以先把生成器与 UHT/program 工作流拆成显式 owner，避免所有 editor-related 路径都穿过同一模块。 |
| UnrealCSharp | `UnrealCSharpEditor` 的 public 侧只保留 `Core`、`UnrealEd`、`DirectoryWatcher`、`CollectionManager`，而 `ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion` 以及多数 UI/editor 框架都在 private 侧；并且 `ScriptCodeGenerator` 自己也是独立模块。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53` | 可以先做“薄 public editor API + 重 private workflow owner”，再逐步把代码生成、编译等职责拆成独立模块。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 editor 公共契约与 editor 工作流实现拆开，让 editor shard / tests 依赖薄接口模块，而不是直接依赖完整 `AngelscriptEditor`。 |
| 具体步骤 | 1. 新增一个薄边界模块，例如 `AngelscriptEditorAPI` 或 `AngelscriptEditorCore`，只导出确实需要被 shard/test 复用的 editor-facing 类型与函数，例如 `BlueprintImpact` 的稳定入口。<br>2. 把 `AngelscriptEditor.Build.cs` 里纯实现性质的依赖尽量下沉到 private 侧，尤其是 `BlueprintGraph`、`Kismet`、`AssetTools`、`Slate*`、`DirectoryWatcher` 这类并非所有下游都需要的框架。<br>3. 修改 `GenerateNewModule(..., true)`，让 editor shard 依赖新的薄模块，而不是直接把 `AngelscriptEditor` 写进 `PublicDepends`。<br>4. 让 `AngelscriptTest` 先迁移到 `AngelscriptEditorAPI/TestHooks`，保留 `AngelscriptEditor` 作为过渡 facade 一段时间，再逐步减少直接命名空间调用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp`，以及新增的 `AngelscriptEditorAPI`/`AngelscriptEditorCore` 模块 |
| 预估工作量 | M |
| 架构风险 | 需要重新梳理哪些 editor symbol 真正属于稳定 API；如果一次性迁移过多，容易把“依赖下沉”和“功能拆分”两类回归混在一起。 |
| 兼容性 | 对脚本用户基本无影响；对 C++ 扩展方若直接 include `AngelscriptEditor` 私有实现，需保留过渡 facade 或转发头以维持向后兼容。 |
| 验证方式 | 1. 生成 editor shard 后检查其 `Build.cs`，确认不再直接 public 依赖完整 `AngelscriptEditor`。<br>2. 触发一次只修改 `BlueprintImpact` 实现的增量编译，观察受影响模块是否从“所有 editor shard/test”收敛到新的 API owner 与直接消费者。<br>3. editor automation tests 与 blueprint impact 相关测试全量通过。 |

### Arch-MS-34：测试边界仍挂在主插件模块图上，产品拓扑与验证拓扑没有分离

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptTest` 是否应该继续作为主插件 `Angelscript.uplugin` 的一部分存在，还是应从产品模块图中剥离出来 |
| 当前设计 | 当前主插件描述符直接声明 `AngelscriptTest`，而该模块一方面镜像 `AngelscriptRuntime` 的内部目录结构暴露 include path，另一方面在 editor target 下继续依赖 `AngelscriptEditor`。结果是主插件的模块清单同时承担了“产品交付面”和“白盒验证面”两种职责。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 把 `AngelscriptTest` 与 runtime/editor 一起放进主插件 `Modules`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:12-21` 通过 `Core`、`Debugger`、`Dump`、`Internals`、`Native`、`Preprocessor`、`ClassGenerator` 等目录镜像运行时内部结构。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:23-49` 让测试模块依赖 `AngelscriptRuntime`，并在 editor target 下依赖 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:1` 与 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp:4-5` 直接 include `../../AngelscriptRuntime/...` 内部头。 |
| 优点 | 同仓库开发时最省事，白盒测试能直接访问 runtime/editor 内部实现，CI 也不需要额外维护独立测试插件。 |
| 不足 | 主插件 module inventory 不再只代表产品能力；第三方查看 `.uplugin` 时无法一眼区分“发布面”与“验证面”。更重要的是，测试模块继续依赖运行时目录布局和 editor 实现细节，后续只要想抽取新模块、调整目录、或把 editor API 变薄，就会先撞到测试耦合。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把测试能力放在独立插件 `UnLuaTestSuite` 中，主插件 `UnLua.uplugin` 只声明 runtime/editor/program；测试插件通过 `Plugins` 字段显式依赖 `UnLua`，自己的模块再私有依赖 `UnLua` 与 `UMG`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-30`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-55` | 产品拓扑与测试拓扑可以分离；测试仍可白盒，但不必污染主插件的模块清单。 |
| puerts | 主插件只声明 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor`，没有把测试模块挂进产品插件描述符。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 即使 workflow module 很多，也可以保持产品描述符只承载真实交付 owner。 |
| UnrealCSharp | 主插件模块图只保留 runtime/editor/generator/compiler/program，没有把测试面作为公开模块暴露。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-54` | 模块数量可以多，但测试面最好不与产品模块图混住；这样 architecture review 与消费方都更容易识别真正的交付边界。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptTest` 迁出主插件描述符，先实现“测试插件化”，再逐步收敛白盒访问面。 |
| 具体步骤 | 1. 新建独立测试插件，例如 `Plugins/AngelscriptTestSuite/AngelscriptTestSuite.uplugin`，默认 `EnabledByDefault=false`；第一阶段可以继续沿用现有 `AngelscriptTest` 模块名和源码，先完成描述符分离。<br>2. 让测试插件通过 `Plugins` 字段依赖 `Angelscript` 主插件，并在 `Build.cs` 中继续依赖 `AngelscriptRuntime` 与新的 `AngelscriptEditorAPI/TestHooks`。<br>3. 第二阶段逐步把 `../../AngelscriptRuntime/...` 的相对路径 include 替换为受控 hook/header surface，减少对目录布局的直接绑定。<br>4. 完成迁移后，从 `Plugins/Angelscript/Angelscript.uplugin` 移除 `AngelscriptTest` 条目，把主插件 module inventory 收敛回产品 owner。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp`，以及新增的 `Plugins/AngelscriptTestSuite/AngelscriptTestSuite.uplugin` |
| 预估工作量 | M |
| 架构风险 | 测试目标、CI 脚本与本地工程文件生成流程都需要同步更新；若直接同时改“插件迁移 + test hooks”，排障成本会偏高，因此应分阶段实施。 |
| 兼容性 | 对脚本用户无影响；对开发流程的影响主要是测试插件需要显式启用。若保留原模块名并自动在测试 target 中启用，可把兼容性风险控制在低位。 |
| 验证方式 | 1. 在默认仅启用主插件的工程中编译，确认 `AngelscriptTest` 不再出现在产品模块图里。<br>2. 启用测试插件后重新生成项目文件并运行现有 automation tests，确认用例仍可发现与执行。<br>3. 抽取一个 runtime 内部头路径做重命名/移动演练，确认只需要调整 test hooks，不再需要大面积修改测试相对路径 include。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-33 | `12+4` 分片在 editor 半区被 `AngelscriptEditor` 公共依赖放大 | 边界收口 + editor API 拆分 | 高 |
| P2 | Arch-MS-34 | 测试模块仍挂在主插件模块图上 | 测试插件化 + 产品/验证拓扑分离 | 中 |

---

## 架构分析 (2026-04-08 18:07)

### Arch-MS-35：脚本根目录已支持跨插件扩展，但 native bind 模块发现仍锁死在主插件单缓存

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件级扩展通路是否在 `Script/` 发现与 native bind 发现两条链路上保持一致 |
| 当前设计 | `FAngelscriptEngineDependencies` 已经把脚本根目录发现抽象成“扫描所有 enabled plugins with content”，但 native bind 模块发现仍然只读取 `Angelscript` 主插件根目录下的一份 `BindModules.Cache`，且 `GenerateNewModule()` 只能把生成模块写回主插件 `Source/`。这意味着脚本资源可以跨插件扩展，native bind 却仍然必须回写主插件或共享同一份缓存。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558-565` 通过 `IPluginManager::Get().GetEnabledPluginsWithContent()` 收集所有插件的 `Script` 根目录。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1360` 在 `DiscoverScriptRoots()` 中遍历插件脚本根并排序。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1487` 却只对 `FindPlugin("Angelscript")` 返回的主插件目录读取 `BindModules.Cache` 并逐个 `LoadModule(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1077` 生成 `ASRuntimeBind_*` / `ASEditorBind_*` 并把名字统一写入这份 cache。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166-1205` 通过 `FindModulePath("AngelscriptRuntime")` 反推目录，再把新模块写到 `Plugins/Angelscript/Source/<ModuleName>/`。<br>`Plugins/Angelscript/Angelscript.uplugin:35-47` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp:1-14`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:1-46` 进一步说明可选领域绑定仍被并入主插件 runtime。 |
| 优点 | 脚本文件与绑定代码都集中在主插件内时，生成、落盘和加载路径最直接，工程内排障成本低。 |
| 不足 | 扩展能力出现明显不对称：内容层已经支持“插件自带脚本根”，代码层却没有“插件自带 bind manifest / bind module”通路。结果是可选绑定包无法像脚本包一样按插件增量交付，未来若要把 GAS、EnhancedInput 或团队自定义绑定拆成 leaf plugin，会先被单缓存加载模型卡住。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `LuaSocket` 这类扩展能力不回写 `UnLua` 主插件源码树，而是作为独立扩展插件存在；扩展插件自己的 `.uplugin` 通过 `Plugins` 字段依赖 `UnLua`，模块则私有依赖 `UnLua` 与 `Lua`。 | `Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:16-29`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:32-57` | 可选领域能力可以做成 leaf plugin，依赖方向保持 `Extension -> Core`，不需要主插件维护单一扩展清单。 |
| puerts | 把 `DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 等 workflow owner 直接声明在 `.uplugin`，而不是让 runtime 从单一 cache 猜测还有哪些附属模块要加载。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59` | 即使扩展通路仍在单插件内，也应让 owner 模块静态可见，避免“内容可发现、模块不可发现”的不对称。 |
| UnrealCSharp | `UnrealCSharpCore`、`UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 都是显式 owner；可选 `EnhancedInput` 也只停留在上层 runtime 的 private deps，而不是把所有扩展都沉到 core loader。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-60`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58` | 显式 owner + 叶子依赖比“主插件单 cache + 动态发现”更利于增量扩展和模块治理。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有“跨插件脚本根发现”模式扩展到 bind 模块发现，让 native bind 也能按插件交付，而不是继续依赖主插件单 cache。 |
| 具体步骤 | 1. 在 `FAngelscriptEngineDependencies` 中新增与 `GetEnabledPluginScriptRoots` 对应的 `GetEnabledPluginBindManifests` / `GetEnabledPluginBindModules` 回调，先让 `FAngelscriptEngine` 能遍历所有 enabled plugins 的 bind manifest。<br>2. 保持向后兼容：第一阶段仍读取主插件 `BindModules.Cache`，但同时额外扫描 `Plugins/*/Config/AngelscriptBindModules.cache` 或插件根下同名 manifest，把结果合并并排序后再 `LoadModule(...)`。<br>3. 新建一个 leaf plugin 或 leaf module 试点，例如 `AngelscriptGameplayBinds`，先迁出 `Bind_AngelscriptGASLibrary.cpp`、`Bind_UEnhancedInputComponent.cpp` 这类可选领域绑定，使依赖方向改为 `GameplayBinds -> AngelscriptRuntime + GameplayAbilities/EnhancedInput`。<br>4. 等插件级 bind manifest 稳定后，再决定 legacy `ASRuntimeBind_*` 是否继续保留为主插件内部优化，还是也允许按 owner plugin 写出独立 shard。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`，以及新增的扩展插件描述符与 manifest 读写代码 |
| 预估工作量 | L |
| 架构风险 | 多插件 manifest 合并会引入新的加载顺序与缺失插件容错问题；若不做 deterministic sort 与 fallback，会把当前单 cache 的隐式顺序问题扩散到多插件。 |
| 兼容性 | 对现有脚本 API 可保持兼容；第一阶段保留主插件 `BindModules.Cache` fallback，即使扩展插件尚未迁移也不会破坏当前工程。切换后仅对绑定模块的交付方式和插件启用方式有影响。 |
| 验证方式 | 1. 仅启用主插件时，确认 bind 加载结果与当前一致。<br>2. 启用新的 `AngelscriptGameplayBinds` 后，确认其脚本目录和 native binds 都能被发现与加载。<br>3. 关闭扩展插件后，确认 runtime 不再尝试加载其 bind 模块。<br>4. 连续两次扫描 enabled plugins，确认合并后的 bind manifest 顺序稳定。 |

### Arch-MS-36：`AngelscriptTest` 仍按默认启动的可复用模块建模，但真实测试执行 owner 已经在 Runtime

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 验证层是否被建模成 opt-in harness，而不是随主插件默认启动、同时暴露 public 传递面的产品模块 |
| 当前设计 | 主插件整体 `EnabledByDefault=true`，`AngelscriptTest` 作为 `Editor/PostDefault` 模块随之进入默认模块图；`AngelscriptTest.Build.cs` 又把模块根目录和 `AngelscriptRuntime`、`Json`、`GameplayTags` 等依赖放到 public 侧。但模块自身的 `StartupModule()` / `ShutdownModule()` 只记录日志，真正的测试执行入口和断言基础设施已经位于 `AngelscriptRuntime` 的 `Testing/` 与 `UAngelscriptTestCommandlet` 中。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:13-33` 说明主插件默认启用，且 `AngelscriptTest` 以 `Editor` / `PostDefault` 形式加载。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:12-32` 把 `ModuleDirectory` 加入 public include path，并把 `Core`、`CoreUObject`、`Engine`、`GameplayTags`、`Json`、`JsonUtilities`、`AngelscriptRuntime` 放进 `PublicDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp:5-16` 显示该模块启动与关闭时只输出日志。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h:8-15` 已在 runtime 中导出 `UAngelscriptTestCommandlet`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTest.h:10-63` 已在 runtime 中定义 `FAngelscriptTest` 与 assert 绑定入口。 |
| 优点 | 所有 C++ automation tests、脚本测试命令和运行时断言都能在一个插件里直接工作，不需要额外启用测试插件或维护单独 target。 |
| 不足 | 默认 editor 会话会加载一个几乎不承载业务逻辑的测试模块，同时还要在 Build DAG 上保留它的 public transitive surface；验证层因此同时承担“默认启动成本”和“可复用模块契约”两种职责，但这两者都与它的真实 owner 角色不匹配。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaTestSuite` 被放进独立测试插件，且 `EnabledByDefault=false`；其模块对外 public 的只有基础引擎依赖，真正的 `UnLua` 依赖留在 private 侧。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:13-30`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64` | 测试 owner 可以存在，但不应该默认跟着产品插件启动，更不应把 core runtime 依赖再公开传递给下游。 |
| puerts | 主插件模块图只暴露 runtime/editor/generator/program owner，没有默认启动的测试模块。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 若验证层不是产品交付面，就不应默认进入主插件模块拓扑。 |
| UnrealCSharp | 主插件也只声明 runtime/editor/generator/compiler/program，各模块 public/private 依赖分工相对清楚，没有单独的 always-on test owner。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-54`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58` | 默认模块图应优先代表产品能力；验证层若必须存在，应尽量保持 private dependency 和显式启用。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 runtime 内的测试执行基础设施，把 `AngelscriptTest` 收敛成 opt-in automation harness，并先缩掉它不必要的 public 传递面。 |
| 具体步骤 | 1. 低风险第一步：把 `AngelscriptTest.Build.cs` 中的 `AngelscriptRuntime`、`GameplayTags`、`Json`、`JsonUtilities` 从 `PublicDependencyModuleNames` 下沉到 `PrivateDependencyModuleNames`，并审计 `PublicIncludePaths.Add(ModuleDirectory)` 是否还能缩减为只暴露少量稳定 helper 头。<br>2. 保持 `FAngelscriptTest`、`UAngelscriptTestCommandlet` 和 runtime `Testing/` 目录留在 `AngelscriptRuntime`，让真正的执行 owner 不变；`AngelscriptTest` 只保留 C++ automation suites、shared helpers 和 editor-only harness。<br>3. 第二阶段把 `AngelscriptTest` 移到单独的 `AngelscriptTestSuite` 插件，默认 `EnabledByDefault=false`，仅在 CI / test target / 开发者显式启用时加载。<br>4. 若有自定义测试模块曾直接 include `AngelscriptTest` 的 public 头，则新增一个极薄的 `Public/TestHarness` 兼容层做过渡，而不是继续公开整个模块根目录。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTest.h`、`Plugins/Angelscript/Angelscript.uplugin`，以及新增的 `AngelscriptTestSuite.uplugin`（若实施第二阶段） |
| 预估工作量 | M |
| 架构风险 | 若现有外部测试代码直接依赖 `AngelscriptTest` 的 public include path，下沉 public deps 后会出现编译面调整；因此应先做 include 审计，再执行插件拆分。 |
| 兼容性 | 对脚本用户无影响；对内部 C++ 自动化测试有轻微迁移成本，但可以通过保留过渡头与显式启用测试插件控制风险。 |
| 验证方式 | 1. 正常 editor 启动时，确认默认日志中不再需要 `AngelscriptTest module started.` 才能完成主插件功能。<br>2. 调整为 private deps 后重新编译 `AngelscriptTest`，确认 automation suites 仍能运行。<br>3. 把模块迁到独立测试插件后，在未启用测试插件的工程中验证主插件功能不变；启用测试插件后验证 commandlet 与 automation tests 仍可执行。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-35 | 脚本根发现与 bind 模块发现的插件级扩展通路不对称 | 扩展通路重构 + leaf plugin 落地 | 高 |
| P2 | Arch-MS-36 | `AngelscriptTest` 被当成默认启动的 public 模块建模 | 依赖下沉 + opt-in test harness | 中 |

---

## 架构分析 (2026-04-08 18:22)

### Arch-MS-37：legacy bind 模块拓扑仍是 debug 菜单触发的工作区状态，不是自动构建契约

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ASRuntimeBind_*` / `ASEditorBind_*` 若仍被视为插件架构的一部分，它们是否已经进入自动 build / generate contract |
| 当前设计 | legacy bind shard 的生成入口仍挂在 `AngelscriptEditor` 的 `ToolMenus` debug 菜单上；runtime 启动时又会尝试读取 `BindModules.Cache` 并动态装载模块名。也就是说，legacy 模块图既不在 `.uplugin` 里，也不在 prebuild/UHT 自动链里，而是依赖“是否有人在 editor 里手动点过一次生成”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722-730` 把 `GenerateNativeBinds()` 暴露为 `Legacy Native Bind Generator (Debug Only)` 菜单项。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1078` 中自动重建代码工程的 `GameProjectUtils::BuildCodeProject(...)` 已被注释掉。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:584-601` 对 bind 模块清单只做 `SaveStringArrayToFile` / `LoadFileToStringArray`，没有版本、target 或生成状态校验。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1488` 在 runtime 初始化期读取插件根目录 `BindModules.Cache` 并逐个 `LoadModule(...)`。<br>2026-04-08 当前工作区观测：`Plugins/Angelscript/BindModules.Cache` 不存在；对 `GenerateNativeBinds()` 的源码检索只命中菜单 lambda、声明和定义，未见进入 UBT/UHT/CI 的自动入口。 |
| 优点 | 把 legacy 路径降成 debug-only 工具，避免正常 editor 使用路径持续触发 source-tree 变更；主线 `AngelscriptUHTTool` 可保持为 primary path。 |
| 不足 | 只要团队仍把 legacy shard 当成“现有模块架构”的一部分，模块拓扑就会退化成工作区状态而不是仓库契约：有没有 cache、有没有人手动点过菜单、有没有历史中间产物，都会改变实际 build / startup 观察到的模块图。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaDefaultParamCollector` 是 `.uplugin` 声明的 `Program` 模块，并在 `StartupModule()` 中注册 `ScriptGenerator` modular feature，collector 会自动进入 UHT/生成链，而不是依赖 editor 菜单去“补全模块图”。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:36-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/Private/UnLuaDefaultParamCollector.cpp:47-69` | 只要某条生成链仍是受支持路径，它就应有自动进入流水线的 host type 和 owner，而不是停留在 debug UI 动作里。 |
| UnrealCSharp | `UnrealCSharpCore.build.cs` 会在构建期自动执行 `GeneratorModules()` 写 `Intermediate/UnrealCSharp_Modules.json`；`UnrealCSharpEditor` 启动时还会自动执行 `FDynamicGenerator::Generator()`。生成链既有 build-time 入口，也有固定 editor owner。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:100-171`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:33-42` | 若某条链路会改变模块索引或生成结果，应把入口固定在 Build.cs / StartupModule / Program owner，而不是让维护者猜“是否需要点菜单”。 |
| puerts | `DeclarationGenerator` 是 `.uplugin` 可见的固定 `Editor` 模块；即便它也提供 UI 触发能力，菜单注册仍发生在独立模块的 `StartupModule()` 中，而不是由 runtime 去发现一个临时生成态模块集。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:28-31`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-40`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640-1655` | 交互式工具可以存在，但其 owner 应是固定、声明式的模块，不应把“是否执行过该工具”变成模块拓扑的一部分。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 legacy generator 从“debug 菜单动作”升级成可审计的 headless owner；若团队确认它只剩历史调试价值，就把它彻底从默认模块拓扑里降级。 |
| 具体步骤 | 1. 新增一个最小 headless 入口，例如 `AngelscriptLegacyBindGenerator` commandlet、developer module 或 prebuild step；保留现有菜单，但菜单只负责调用这个 headless 入口，不再直接改写 source tree。<br>2. 为 legacy 输出增加结构化 state，例如 `Intermediate/Angelscript/LegacyBindModules.json`，字段至少包含 `generated_modules`、`target`、`platform`、`source_of_truth_version`、`generated_at`；`BindModules.Cache` 只作为兼容层，不再是唯一 authority。<br>3. 让 `FAngelscriptEngine` 读取该 state 后再决定是否允许 legacy load path 生效；当 state 缺失、版本不匹配或 legacy path 被禁用时，明确记录 warning，而不是静默退化成“也许有模块、也许没有”。<br>4. 若团队决定 legacy path 仅保留调试用途，则在第二阶段把 runtime 默认启动路径中的 `BindModules.Cache` 读取改成 developer-only / debug-only 开关，避免主插件继续承诺一条实际靠手工维持的模块链。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增的 legacy bind state / commandlet 文件 |
| 预估工作量 | M |
| 架构风险 | 主要风险在于现有内部脚本或开发者流程可能默认依赖“点一次菜单再重编”的老习惯；迁移时需要把新 headless 入口接进 CI、本地调试脚本或文档。 |
| 兼容性 | 对脚本 API 与正常 UHT 主线无影响；变化主要落在开发工作流和 legacy 调试链。保留旧菜单作为 wrapper 可把短期兼容性风险控制在低位。 |
| 验证方式 | 1. 在全新 clean workspace 中，不手动点菜单也能明确得到两种可预期结果之一：legacy path 被显式禁用，或通过 headless 入口自动生成所需 state。<br>2. 故意删除 / 篡改 legacy state，确认 runtime 启动给出清晰 warning，而不是无提示地改变模块图。<br>3. 保留旧菜单入口时，确认其实际调用的是新 headless owner，生成结果与原逻辑一致。 |

### Arch-MS-38：`AngelscriptRuntime` 正在充当 editor 行为委托总线，静态单向 DAG 掩盖了语义反向依赖

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前模块依赖方向虽然静态无环，但 runtime 是否已经承载了 editor-only 行为 contract，形成语义上的反向依赖 |
| 当前设计 | `AngelscriptEditor.Build.cs` 静态上只依赖 `AngelscriptRuntime`，没有显式反向依赖或循环；但 `AngelscriptRuntimeModule.h` 公开声明了 `DebugListAssets`、`EditorCreateBlueprint`、`EditorGetCreateBlueprintDefaultAssetPath` 等 editor 语义 delegate，`AngelscriptEditor` 在 `StartupModule()` 里向这些 runtime delegate 绑定 UI/资产创建 lambda，`AngelscriptDebugServer` 再从 runtime 侧广播它们。结果是“editor 需要的行为接口”被固化进 runtime ABI，而不是留在 editor owner 内。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-26` 静态依赖方向仍是 `AngelscriptEditor -> AngelscriptRuntime`，未见显式循环。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:19-21` 与 `:45-47` 在 runtime public header 中声明并导出 editor-specific delegates / accessors。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:120-135` 为这些 delegate 提供静态存储。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397-409` 把“列出资产”“创建蓝图”lambda 绑定到 runtime delegate；`:434-435` 还从 runtime 回调获取默认资产路径。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1167-1180` 在 runtime debug server 中直接 `Broadcast(...)` 这些 editor 语义 delegate。 |
| 优点 | 不需要显式引入 `AngelscriptEditor` 的反向编译依赖，也让 runtime 调试链可以在 editor 模块存在时自动获得 UI/资产辅助行为。 |
| 不足 | 这条“隐藏反向边”并不会表现为 link cycle，却会把 editor 语义写进 runtime 的 public module contract，后续一旦想把 `Debugging`、资产工具或 editor shell 再拆细，就会发现 runtime ABI 已经默认自己拥有 editor 扩展点。静态 DAG 看起来无环，语义 owner 却已经部分反转。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | editor 菜单、目录监听和 packaging 设置都留在 `UnLuaEditor` 的 `StartupModule()` 中；tooling owner 由 editor 模块本身承担，而不是把 editor 行为 delegate 公开到 runtime 头文件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48-70`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-84` | editor 扩展点可以很多，但 owner 仍应留在 editor module 内部，runtime 只暴露与运行时语义相关的接口。 |
| puerts | `DeclarationGenerator` 在自己的 `StartupModule()` 中注册命令和菜单；它作为独立 editor module 声明在 `.uplugin` 中，而不是让 `Puerts` runtime 公开 editor 操作 delegate。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:28-31`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640-1655`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-40` | editor/tooling 行为最好由显式 editor owner 承接，而不是隐藏在 runtime module 的公共事件总线里。 |
| UnrealCSharp | `UnrealCSharpEditor` 在自己的 `StartupModule()` 里处理设置与 generator 触发，`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore` 这些 workflow owner 都通过 editor module 的 private deps 编排。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:33-42`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63` | 即便 editor 需要编排很多 runtime/tooling 能力，也应优先通过 editor private edge 组合，而不是把 editor-specific callback 升格为 runtime public API。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留当前静态无环 DAG，但把 editor-only 行为 contract 从 `AngelscriptRuntime` 公共头中抽走，改成显式 editor service owner。 |
| 具体步骤 | 1. 新增一个最小 bridge，例如 `IAngelscriptEditorServices` 或 `AngelscriptEditorServices` 模块/接口，专门承载 `ShowAssetList`、`CreateBlueprint`、`GetDefaultBlueprintAssetPath` 这类 editor 行为。<br>2. `AngelscriptEditor` 在 `StartupModule()` 中注册该 service 实现；`AngelscriptDebugServer` 与其他 runtime 调用点只在 `WITH_EDITOR` 且 service 可用时通过 bridge 调用，不再依赖 runtime public delegate。<br>3. 迁移期保留 `FAngelscriptRuntimeModule::GetDebugListAssets()` / `GetEditorCreateBlueprint()` 作为 deprecated wrapper，由 wrapper 内部转发到新 service，避免现有 C++ 调用点一次性断裂。<br>4. 迁完这三条 editor 语义后，再统一审计 `FAngelscriptRuntimeModule` 中其余 delegate：凡是本质上属于 editor UX / asset tooling / debug UI 的，都继续向新 bridge 收口；真正 runtime 生命周期相关的 delegate 才保留在 runtime owner。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 editor service 接口/模块文件 |
| 预估工作量 | M |
| 架构风险 | 风险主要在于已有内部扩展代码可能直接使用 `FAngelscriptRuntimeModule` 的这些 accessor；因此需要保留一段过渡 wrapper，并逐步在编译期给出 deprecation 提示。 |
| 兼容性 | 对脚本用户无影响；对 C++ 扩展方的影响可通过 deprecated wrapper 和 forwarding adapter 控制在低到中等。完成迁移后，runtime public API 会更窄、更贴近真正的 runtime 语义。 |
| 验证方式 | 1. 重新画模块 DAG，确认仍无静态循环，同时 runtime public header 不再直接声明 editor-only 行为 delegate。<br>2. 在 editor 中触发 debug server 的 `ListAssets` / `CreateBlueprint` 消息，确认功能保持可用。<br>3. 在非 editor 或 service 不可用场景验证 graceful degradation，不再要求 runtime 永远携带 editor 行为 contract。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-37 | legacy bind 模块仍依赖 debug 菜单触发，未进入自动构建契约 | 生成入口重构 + state/manifest 显式化 | 高 |
| P1 | Arch-MS-38 | runtime 正在承载 editor 行为委托总线 | 依赖反转 + editor service bridge | 高 |

---

## 架构分析 (2026-04-08 18:32)

### Arch-MS-39：`AngelscriptRuntime.Build.cs` 把 UHT 生成面、手写 bind 面与宿主/调试基础设施压进同一个 owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptRuntime` 的静态依赖面是否仍对应单一职责 owner |
| 当前设计 | `AngelscriptRuntime.Build.cs` 一次性声明了 31 条 public/private 模块边；但当前主线 `AngelscriptUHTTool` 只对 14 个 UE 模块产出 `AS_FunctionTable_*` summary。未进入 summary 的依赖里，至少还混有 `Sockets` 调试链、`Projects`/`IPluginManager` 宿主发现、`Slate` 编译失败弹窗、`StructUtils` 公共数据桥这些完全不同的职责。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-79` 同时声明 `GameplayTags`、`StructUtils`、`Sockets`、`Slate`、`Projects`、`GameplayAbilities`、`UMGEditor` 等不同层次依赖。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-76` 只遍历 `supportedModules.All` 中的模块生成 function table，`:166-241` 再写出 `AS_FunctionTable_Summary.json` 与 `AS_FunctionTable_ModuleSummary.csv`。<br>当前工作区产物 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv:2-15` 只列出 `Engine`、`Landscape`、`AIModule`、`NavigationSystem`、`GameplayTasks`、`GameplayTags`、`UMG`、`EngineSettings`、`AssetRegistry`、`UnrealEd`、`UMGEditor`、`GameplayAbilities`、`EnhancedInput`、`AngelscriptRuntime` 14 个模块。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:3-11` 直接把 `Sockets`/`TcpListener`/`UdpSocketReceiver` 编进 runtime debug owner。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:22`、`:539-567` 用 `IPluginManager` 发现插件脚本根；`:2138-2167` 直接通过 `FSlateApplication` 驱动启动期编译失败弹窗。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAnyStructParameter.h:3-14` 把 `StructUtils/InstancedStruct.h` 放进 runtime exported type。 |
| 优点 | 所有脚本 runtime 相关能力都能通过一个 UE 模块开箱即用，Build DAG 简单，不需要额外管理多个 leaf module 的加载与发布。 |
| 不足 | `Build.cs` 已经不再表达“单一模块职责”，而是在同时携带 `UHT 生成支持面`、`手写 bind 支撑面`、`宿主发现/Debug UI` 三类 owner。后续如果要根据真实工作负载调窄依赖，维护者无法仅凭静态模块图判断一条边属于哪类语义。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime `UnLua` 保持最小 public deps，editor 和默认参数收集分别落到 `UnLuaEditor`、`UnLuaDefaultParamCollector`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-85`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:47-55` | 先把 runtime、editor、program 的 owner 分开，才能让依赖边重新具备语义。 |
| puerts | `WasmCore`、`JsEnv`、`Puerts` 分担运行时层级，`DeclarationGenerator`、`ParamDefaultValueMetas` 再承接工具链。 | `Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:56-79`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:89-97`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-26`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56` | runtime core、上层 runtime、tooling owner 分层后，单个 `Build.cs` 不必再同时承载所有职责。 |
| UnrealCSharp | `UnrealCSharpCore` 负责 host/module inventory，`UnrealCSharp` 只承接上层 runtime，`ScriptCodeGenerator`/`Compiler` 独立成 editor workflow module。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-79`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49` | 可以先抽出“inventory/debug/toolchain owner”，再逐步收窄 runtime core，而不是一开始就大拆 public API。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `AngelscriptRuntime` 里的依赖按职责显式分桶，再按分桶结果做小步 leaf module 提取。 |
| 具体步骤 | 1. 基于现有 `AS_FunctionTable_ModuleSummary.csv` 与 `AngelscriptRuntime.Build.cs` 生成一份新的 `Intermediate/Angelscript/ModuleCapabilityReport.json`，把每条依赖标记为 `uht_generated`、`manual_bind`、`debug_host`、`core_runtime` 之一；第一阶段允许 `Sockets`、`Slate`、`Projects`、`StructUtils` 这类非 UHT 依赖通过人工白名单入表。<br>2. 第一批先抽 `AngelscriptRuntimeDebug`（名字可调整）模块，承接 `Debugging/AngelscriptDebugServer.*` 以及启动期编译失败 UI/消息桥；`AngelscriptRuntime` 只保留 runtime lifecycle 与 service hook。<br>3. 第二批再抽 `AngelscriptDataInterop` / `AngelscriptStructSupport`，承接 `StructUtils` 包装和其他不属于 engine lifecycle 的数据桥头文件，避免它们继续挂在 core runtime public contract 上。<br>4. 等 leaf owner 稳定后，再收窄 `AngelscriptRuntime.Build.cs`：凡是不属于 `core_runtime` 或 `uht_generated` 的依赖，都要求有显式 leaf owner 或保留理由。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAnyStructParameter.h`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及新增的 capability report / leaf module 文件 |
| 预估工作量 | L |
| 架构风险 | 风险主要在于 `AngelscriptEngine.cpp` 目前把 runtime lifecycle、plugin root discovery、Slate prompt 混在同一个 owner；抽取时需要先建立 service/bridge，避免把初始化时序打散。 |
| 兼容性 | 对脚本 API 可以保持向后兼容；对 C++ 扩展方的影响主要是 include 路径与模块依赖变化，可通过 forwarding header 和默认启用 leaf module 缓解。 |
| 验证方式 | 1. 生成 `ModuleCapabilityReport.json` 后，对照 `AS_FunctionTable_ModuleSummary.csv` 验证每条 `AngelscriptRuntime.Build.cs` 依赖都已归类。<br>2. editor/game target 全量编译通过，并确认 `AS_FunctionTable_ModuleSummary.csv` 的 14 个生成模块集合不因 leaf module 提取而变化。<br>3. 启动 editor，验证 debug server、插件脚本根发现和启动期编译失败提示仍可用。 |

### Arch-MS-40：同一 UE 模块的脚本支持同时分散在 UHT function-table 与 `Binds/*.cpp` 手写注册两条管线

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 单个 UE 模块的脚本支持 owner 是否唯一、可追踪 |
| 当前设计 | 当前主线 UHT tool 已按模块统计 `DirectBindEntries` / `StubEntries`，但同一个 UE 模块往往还同时存在手写 bind 文件和 runtime helper。也就是说，`GameplayTags`、`AssetRegistry`、`GameplayAbilities` 这类领域的脚本支持并不是单一 owner，而是被拆在 `AS_FunctionTable_*` 生成结果与 `Binds/*.cpp` 的手写注册逻辑里。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:103-139` 在生成阶段区分 `DirectBindEntries` 与 `StubEntries`，`:166-241` 把这些统计写入 summary/json/csv。<br>当前工作区产物 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv:7` 显示 `GameplayTags` 为 `35` 条 `StubEntries` / `0` 条 `DirectBindEntries`，`:10` 显示 `AssetRegistry` 为 `48` 条总 entry 中 `43` 条 stub，`:13` 显示 `GameplayAbilities` 里仍有 `101` 条 stub。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp:26-179` 又手写注册了 `FGameplayTagQuery`、`FGameplayTag`、`FGameplayTagContainer` 和 tag reload helper。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AssetRegistry.cpp:20-288` 手写注册了整套 `AssetRegistry` namespace 与 Blueprint/Widget blueprint 查询 helper。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp:4-15` 还为 `UAngelscriptAbilityAsyncLibrary` 单独追加 GAS function entries。 |
| 优点 | UHT stub 可以快速覆盖大面积 `UFUNCTION` 表面，手写 bind 又能补足 global function、custom helper、reload hook 等 UHT 难以自动表达的能力。 |
| 不足 | 对同一个 UE 模块而言，维护者无法从模块图直接判断“脚本支持由谁负责”。一条 API 可能同时受 `UHT stub`、`手写 bind`、`runtime helper` 三个 owner 影响，后续做 leaf module 拆分或删除遗留路径时，很容易漏掉另一条注册通路。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 默认参数收集被固定在 `UnLuaDefaultParamCollector` program owner，运行时 `UnLua` 自己不再兼任这条 metadata 收集职责。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:47-55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/Private/UnLuaDefaultParamCollector.cpp:47-72`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66` | 先把 codegen / metadata owner 固定下来，runtime wrapper 再回到运行时模块。 |
| puerts | `ParamDefaultValueMetas` 单独实现 `ScriptGenerator` / metadata 导出，`JsEnv`/`Puerts` 则继续承接 runtime wrapper。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:20-51`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-129`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:89-97` | metadata/codegen 与 runtime module 分 owner 后，单个领域模块不会再同时隐含两条注册 authority。 |
| UnrealCSharp | `ScriptCodeGenerator`、`Compiler` 与 `UnrealCSharp` runtime 分离，生成和运行时 wrapper 由不同模块承担。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58` | 即使最终仍要组合多个生成/运行时环节，也应把 owner 模块显式化，而不是让同一领域 API 同时分散在两条隐式通路。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 UHT summary 之上补一份“模块支持能力清单”，先显式化双轨 owner，再按领域拆 leaf support module。 |
| 具体步骤 | 1. 扩展 `AngelscriptFunctionTableCodeGenerator` 的产物，新增 `BindingCapabilityReport.json`，字段至少包含 `moduleName`、`uhtDirectEntries`、`uhtStubEntries`、`manualBindFiles`、`runtimeHelperFiles`。`manualBindFiles` 第一阶段可以通过维护一份小型 registry（如 `Binds/BindingOwners.inl`）填充，不要求立刻自动化解析全部 C++。<br>2. 先把当前双轨最明显的领域标清：`GameplayTags` 绑定到 `Bind_FGameplayTag.cpp`、`AssetRegistry` 绑定到 `Bind_AssetRegistry.cpp` / `Debugging/AngelscriptDebugServer.cpp`、`GameplayAbilities` 绑定到 `Bind_AngelscriptGASLibrary.cpp` 和相应 `Core/AngelscriptAbility*` helper。<br>3. 第二阶段按领域拆 leaf owner，例如 `AngelscriptGameplayTagsSupport`、`AngelscriptAssetRegistrySupport`、`AngelscriptGameplayAbilitiesSupport`；UHT 生成的 stubs 继续留在 toolchain，手写 runtime helper 则迁到这些显式模块。<br>4. 迁移期保持现有 script 名称与 `FAngelscriptBinds::AddFunctionEntry` 注册语义不变，只改变 owner module 与构建组织；这样脚本用户不需要改调用点。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AssetRegistry.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`，以及新增的 capability report / binding owner registry / leaf support module 文件 |
| 预估工作量 | M |
| 架构风险 | 风险在于现有手写 bind 文件没有统一 owner metadata；第一阶段若 registry 漏项，就会让 report 低估某些领域的 manual bind 面，因此需要把 report 作为逐步收敛工具而不是一次性 truth。 |
| 兼容性 | 对脚本调用面可以保持兼容；对 C++ 构建链的变化主要是新增几个默认启用的 leaf module。只要保留现有注册名和导出宏，对插件使用者影响较低。 |
| 验证方式 | 1. 生成 `BindingCapabilityReport.json`，确认 `GameplayTags`、`AssetRegistry`、`GameplayAbilities` 同时展示 UHT 与 manual owner 信息。<br>2. 抽出首批 leaf support module 后，运行 editor build 并验证对应脚本 API 仍能注册与调用。<br>3. 对比迁移前后的 `AS_FunctionTable_ModuleSummary.csv` 与 manual bind registry，确认没有领域在重构后失去一条注册通路。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-39 | `AngelscriptRuntime` 单模块混合 UHT 生成面、手写 bind 面与宿主/调试基础设施 | 依赖分层 + leaf 模块提取 | 高 |
| P2 | Arch-MS-40 | 同一 UE 模块被 UHT function-table 与手写 bind 双轨共同承接 | owner 显式化 + 领域 support module 拆分 | 中 |

---

## 架构分析 (2026-04-08 23:35)

### Arch-MS-41：`12+4` bind shard 把编译粒度控制上升成 UE 模块层级，但生成的 `Build.cs` 本身并没有承载独立 owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `12+4` 绑定分片到底是在表达稳定模块边界，还是仅仅在表达编译调度策略 |
| 当前设计 | `AngelscriptRuntime` 主模块已经在 `Build.cs` 里主动调小 unity 合并阈值，但 legacy native bind 生成器仍会再按 `10` 个 key 一组创建 `ASRuntimeBind_*` / `ASEditorBind_*` 新模块；而生成出的 shard `Build.cs` 只设置 `PCHUsage` 和依赖数组，没有额外的 compile policy、模块 API 或长期 owner 语义。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:10-11` 设置 `PCHUsage = UseExplicitOrSharedPCHs` 与 `NumIncludedBytesPerUnityCPPOverride = 131072`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 在 `GenerateNativeBinds()` 中用 `ModuleCount = 10` 把 runtime/editor DB 切成 `ASRuntimeBind_*` / `ASEditorBind_*`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1168-1177` 说明每个 shard 的固定 public deps 只有 `Core`、`CoreUObject`、`Engine`、`AngelscriptRuntime`，editor shard 额外再加 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1217-1275` 生成的 shard `Build.cs` 仅写入 `PCHUsage`、`PublicDependencyModuleNames`、`PrivateDependencyModuleNames`，没有 `bUseUnity`、`NumIncludedBytesPerUnityCPPOverride` 或其他独立编译策略。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1477-1487` 运行时只是按缓存名加载这些模块。 |
| 优点 | 这套做法能强制 UBT 把 generated bind 工作切到多个 UE 模块上，并通过 `bIsEditor` 把 editor-only 绑定与 runtime 绑定分开。 |
| 不足 | 当前模块数实际上在承载“并行编译切片”而不是“长期 owner 边界”。由于 shard `Build.cs` 几乎没有独立策略，`12+4` 的主要收益来自额外 module scheduling，而代价却是 `Source/` 下新增 16 个 synthetic 模块、project generation 波动和额外的加载/治理复杂度。也就是说，模块拓扑和编译粒度被绑成了同一个旋钮。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 主插件只固定声明 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector` 三个 owner；如果某个扩展模块需要不同编译粒度，就在稳定模块内直接调整 compile policy，例如 `LuaSocket` 用 `bUseUnity = false`，而不是临时生成更多 UE 模块。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:16-29`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:19-30` | 编译并行度可以通过稳定 owner 模块内的 unity/no-unity 策略解决，不必让“模块数量”本身变成主要调参手段。 |
| puerts | `.uplugin` 固定声明 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor`；运行时层级和工具链层级是长期模块，不跟单次生成批次联动。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:11-79`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:12-29` | 即使需要多模块，也优先把模块预算给稳定 owner，而不是给“本次编译想并行多少份”这种短期策略。 |
| UnrealCSharp | 把 `UnrealCSharpCore`、`UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 作为固定 workflow/runtime owner；生成与编译职责通过这些长期模块分担，而不是再派生一组 synthetic micro-module。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-128`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | “模块层级”更适合表示稳定职责拆分；“编译粒度”则应下沉到 owner 模块内部的文件布局与编译参数。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 generated bind 的并行编译需求收回到 1 到 2 个稳定 owner 模块中，用文件级分片与 compile policy 调优替代 `12+4` synthetic modules。 |
| 具体步骤 | 1. 第一阶段恢复稳定 owner，例如 `AngelscriptNativeBindsRuntime` 与 `AngelscriptNativeBindsEditor`；保留当前 `Bind_<Class>.cpp` / package 级生成文件，但不再按 bucket 生成新的 `.Build.cs`。<br>2. 把现有 `GenerateNewModule()` 改成“生成 `.cpp` 文件到稳定模块目录”，并让 `GenerateBuildFile()` 仅维护这 1 到 2 个固定模块的规则文件。<br>3. 在稳定 owner 模块上做真实编译调优：优先试 `bUseUnity = false` 或更小的 `NumIncludedBytesPerUnityCPPOverride`，并与当前 `AngelscriptRuntime.Build.cs` 的 unity 配置一起评估总编译时间，而不是继续通过新增模块数换并行度。<br>4. 迁移期保留旧 `BindModules.Cache` 到新 owner 模块名的兼容映射，确保已有工程不会因为缓存里仍残留 `ASRuntimeBind_*` / `ASEditorBind_*` 而直接失效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增或恢复的 `AngelscriptNativeBindsRuntime` / `AngelscriptNativeBindsEditor` 模块文件 |
| 预估工作量 | M |
| 架构风险 | 最大风险是一次性收掉 synthetic 模块后编译时间出现回退，因此必须先保留文件级分片，再用 build timing 数据决定 unity 参数；不能直接把所有 generated binds 粗暴并回一个 unity 大模块。 |
| 兼容性 | 对现有脚本 API 与绑定注册名可保持向后兼容；变化主要在 build graph、缓存文件和 IDE project generation。对 C++ 消费者的影响可通过固定 owner 模块名和旧缓存兼容映射控制在低到中等。 |
| 验证方式 | 1. clean build 对比迁移前后的总编译时间、link 时间和 project generation 稳定性。<br>2. editor/game target 验证 generated binds 仍能完整编译并被运行时加载。<br>3. 连续两次生成 native binds，确认模块清单不再因为 bucket 切分而波动。<br>4. 检查 `Source/` 下的 owner 模块数是否稳定收敛为固定 1 到 2 个。 |

### Arch-MS-42：`AngelscriptRuntime.Build.cs` 的 `PrivateDependencyModuleNames` 已被 `AngelscriptUHTTool` 解释成代码生成支持矩阵，`public/private` 语义被工具链折叠

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `Build.cs` 中的 public/private 依赖边界，是否还能只代表链接与头文件可见性 |
| 当前设计 | 当前 UHT sidecar 会直接解析 `AngelscriptRuntime.Build.cs`，把所有 `DependencyModuleNames.AddRange` 中出现的模块都纳入 `supportedModules.All`；只有落在 `if (Target.bBuildEditor)` 文本块中的模块，才额外标记为 `editorOnlyModules`。这意味着 runtime 的 private deps 已经不仅是实现依赖，还同时承担“哪些 UE 模块进入 function-table 生成”的 schema 角色。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-42` 把 `ApplicationCore`、`Engine`、`GameplayTags`、`StructUtils` 等声明为 public deps。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:45-65` 又把 `AIModule`、`Landscape`、`UMG`、`AssetRegistry`、`EnhancedInput`、`GameplayAbilities`、`GameplayTasks` 等放入 private deps。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-384` 的 `LoadSupportedModules()` 会逐行读取 `AngelscriptRuntime.Build.cs`，只要命中 `DependencyModuleNames.AddRange` 就把引号里的模块名加入 `allModules`，并不会区分 public/private。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:350-380` 仅用 `if (Target.bBuildEditor)` 文本块来额外标记 `editorOnlyModules`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:387-409` 还通过 UHT session 反推 `AngelscriptRuntime.Build.cs` 路径，把这份 runtime 规则文件变成 codegen inventory 的事实 authority。 |
| 优点 | 目前只维护一份 `Build.cs` 就能同时驱动链接依赖和 codegen 支持模块，短期内配置重复较少。 |
| 不足 | `PrivateDependencyModuleNames` 已经不再只是“实现细节”。一条本来只想优化链接面的 private 依赖改动，也可能悄悄扩大或收缩 function-table 覆盖范围。后续如果把 GAS、UI、AssetRegistry 之类能力迁到 leaf module，维护者会同时碰到“链接依赖变化”和“代码生成支持矩阵变化”两个连锁问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaDefaultParamCollector` 作为独立 `Program` 模块直接声明自己面向 UHT 的最小依赖，只依赖 `Core`、`CoreUObject` 和 `Programs/UnrealHeaderTool/Public`；它不通过解析 `UnLua.Build.cs` 来推断支持范围。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:30-55` | codegen/tooling 模块应有自己的依赖契约，runtime private deps 不该自动升级成 codegen schema。 |
| puerts | `ParamDefaultValueMetas` 与 `DeclarationGenerator` 分别是 `Program` / `Editor` 模块，各自用自己的 `Build.cs` 声明依赖；runtime 层的 `Puerts`、`JsEnv` 依赖并不会被工具链通过文本解析折叠成同一份支持矩阵。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-26`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:23-43` | 把 runtime、editor generator、program metadata owner 分开后，private deps 和 codegen scope 就不会天然绑死。 |
| UnrealCSharp | `UnrealCSharpCore.build.cs` 会生成结构化的 `UnrealCSharp_Modules.json` 索引；与此同时，上层 runtime `UnrealCSharp.Build.cs` 仍单独维护自己的 public/private deps。也就是说，模块清单与 runtime 链接依赖是两份不同但可追踪的 contract。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:100-212`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58` | 如果 tooling 真的需要 inventory，优先读取结构化 manifest，而不是继续让 runtime private deps 兼任第二种语义。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 codegen/UHT 引入独立的结构化模块清单，显式区分 `link deps` 与 `uht/codegen scope`，恢复 `Build.cs` 的边界语义。 |
| 具体步骤 | 1. 在 `Source/AngelscriptUHTTool/` 或插件根新增 `AngelscriptCodeGenModules.json`，字段至少包含 `linkPublic`、`linkPrivate`、`uhtSupported`、`editorOnly` 四类集合；第一版可以由现有 `Build.cs` 自动导出，避免一次性手工重写。<br>2. 修改 `LoadSupportedModules()`，停止扫描所有 `DependencyModuleNames.AddRange`；改为只读取 manifest 的 `uhtSupported` 与 `editorOnly`。<br>3. 迁移期保留一个 diff 检查：把旧文本解析结果与新 manifest 对比，若有差异就输出 warning，帮助逐步把真正需要 codegen 的模块补齐。<br>4. 等 manifest 稳定后，再独立审计 `AngelscriptRuntime.Build.cs` 的 private deps，允许某些模块只作为 runtime 实现依赖存在，而不再自动进入 function-table 支持矩阵。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及新增的 `AngelscriptCodeGenModules.json` 或等价 manifest 生成逻辑 |
| 预估工作量 | M |
| 架构风险 | 主要风险是 manifest 首次落地时与当前文本解析结果不一致，导致某些模块暂时掉出 function-table 生成范围；因此必须先做双轨对比和 warning，再切换 authority。 |
| 兼容性 | 对现有脚本 API 无直接破坏；变化集中在 build/codegen contract。对插件维护者而言，新增模块时需要同时决定它是 `link dependency` 还是 `uht/codegen` 参与方，但这正是边界显式化后的预期成本。 |
| 验证方式 | 1. 先用当前 `Build.cs` 自动生成第一版 manifest，确认新旧两种算法产出的 supported module 集合一致。<br>2. 人工删除或新增一个纯 runtime private 依赖，验证在未修改 manifest 时 codegen scope 不再跟着变化。<br>3. 重新生成 `AS_FunctionTable_*` 与 summary 文件，确认迁移前后的输出在预期模块集合上保持一致。<br>4. 审计一次 `AngelscriptRuntime.Build.cs`，确认 private deps 的收窄不再意外触发 codegen 范围漂移。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-41 | `12+4` bind shard 把编译粒度策略固化成模块拓扑 | 模块收敛 + 文件级并行编译调优 | 高 |
| P1 | Arch-MS-42 | runtime private deps 被 UHT 工具链折叠成 codegen 支持矩阵 | codegen manifest 显式化 + 语义解耦 | 高 |

---

## 架构分析 (2026-04-08 23:48)

### Arch-MS-43：`AngelscriptTest` 更像 automation harness，却仍按可复用库模块公开依赖

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptTest` 是否真的提供了稳定的 public module contract，足以支撑当前的 `PublicIncludePaths` / `PublicDependencyModuleNames` |
| 当前设计 | `AngelscriptTest.Build.cs` 公开了整个模块根目录，并把 `AngelscriptRuntime`、`GameplayTags`、`Json`、`JsonUtilities` 放进 public deps；但模块本身的启动逻辑只写日志，真正的测试执行 owner 仍在 `AngelscriptRuntime/Testing` 与 `UAngelscriptTestCommandlet`。`AngelscriptTest` 内部的所谓“support”头也只是继续转发到 `Shared/` utilities。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:13-21` 公开模块根并镜像多个内部子目录，`:23-32` 把 `AngelscriptRuntime`、`GameplayTags`、`Json`、`JsonUtilities` 置于 `PublicDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp:9-16` 的 `StartupModule()` / `ShutdownModule()` 仅输出日志。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTest.h:10-63` 定义了真正的 `FAngelscriptTest` 执行与 assert 绑定入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h:8-16` 把 `UAngelscriptTestCommandlet` 暴露在 runtime owner。<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTestSupport.h:1-3` 只是一层对 `Shared/AngelscriptTestUtilities.h` 的转发。 |
| 优点 | 维护者在同一个模块里即可放 automation case、shared helper 和 learning/example 素材，短期内写测试的摩擦较低。 |
| 不足 | `AngelscriptTest` 现在同时承担了“默认装载模块”“测试源码容器”“潜在 public 库”三种角色，但源码里没有与之匹配的稳定 public API。结果是 public deps 主要在放大传递面，而不是表达真实 contract；一旦后续有外部 test plugin 或自定义 harness 依赖它，拿到的将是整个测试树而不是精简的测试服务接口。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaTestSuite` 被建模成独立测试插件，默认不启用；其模块只把基础引擎依赖放在 public 侧，把 `UnLua` 与 `Lua` 留在 private deps，同时在 `Public/` 下提供明确的模块接口与 test helper 类型。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestSuite.h:21-32`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestHelpers.h:17-24` | 只有当测试模块真的准备被外部复用时，才提供受控 `Public/` contract；否则测试 owner 更适合作为 opt-in harness，而不是主插件里的“公开库模块”。 |
| puerts | 主插件 `.uplugin` 只声明 runtime/editor/toolchain 模块，没有把测试 owner 纳入产品插件描述符。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 产品模块图与测试/验证面分离后，公开依赖边会更接近真实交付 contract。 |
| UnrealCSharp | `.uplugin` 公开面同样聚焦在 runtime/editor/generator/compiler，没有额外对外暴露测试模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53` | 如果测试只是仓库内部验证面，就不应默认继承产品模块的 public library 语义。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `AngelscriptTest` 从“公开库模块”收敛为“显式 harness owner”，只为真正需要的跨模块测试入口保留最小 public surface。 |
| 具体步骤 | 1. 第一阶段先做低风险收口：把 `AngelscriptTest.Build.cs` 中 `AngelscriptRuntime`、`GameplayTags`、`Json`、`JsonUtilities` 下沉到 `PrivateDependencyModuleNames`，并移除 `PublicIncludePaths.Add(ModuleDirectory)`；若仍需要对外共享极少数测试入口，则新建 `Source/AngelscriptTest/Public/TestHarness/`，只暴露少量 façade 头。<br>2. 把现有 `Angelscript/AngelscriptTestSupport.h`、`Shared/AngelscriptTestUtilities.h` 这类“半公开 helper”分成两层：一层留给模块内部 tests 使用，一层若确有外部 consumer，再通过 `Public/TestHarness` 明确发布。<br>3. 维持 `FAngelscriptTest` 与 `UAngelscriptTestCommandlet` 继续留在 `AngelscriptRuntime`，避免同时挪动执行 owner；`AngelscriptTest` 只保留 C++ automation suites、夹具和 learning/example 素材。<br>4. 若后续确实出现独立复用需求，再把 `AngelscriptTest` 升格为单独 `AngelscriptTestSuite` 插件；这一步应建立在第一阶段 public surface 已经收敛完成之后。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTestSupport.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTest.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h` |
| 预估工作量 | M |
| 架构风险 | 若仓库外还有自定义测试模块隐式依赖当前 `AngelscriptTest` 的模块根 include path，第一阶段收紧 public edge 会暴露编译错误；因此应先做一次 include 审计，再决定是否需要兼容 façade。 |
| 兼容性 | 对脚本用户无影响。对 C++ 测试扩展方，兼容影响集中在 include 路径和 `Build.cs` 依赖需要显式化，属于可控的中低风险。 |
| 验证方式 | 1. 在收紧 public deps 后重新编译 `AngelscriptTest`，确认现有 automation suites 不回归。<br>2. 新建一个 dummy consumer，仅 include 预期保留的 `Public/TestHarness` 头，确认它不再因 accidental public deps 而拿到整棵测试树。<br>3. 运行 `UAngelscriptTestCommandlet` 与现有 automation tests，确认 runtime 执行 owner 保持不变。 |

### Arch-MS-44：runtime internals 的跨模块访问没有 consumer-side 声明，特权依赖与正常 API 依赖不可区分

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptEditor` / `AngelscriptTest` 访问 runtime internals 时，是否存在可追踪的“friend access contract” |
| 当前设计 | 当前做法基本把责任放在 producer 侧：`AngelscriptRuntime` 直接 public 导出模块根、`Core/` 和 raw AngelScript 头；于是 editor/test 只需普通依赖 `AngelscriptRuntime`，就能直接 include `AngelscriptEngine.h`、`Binds/Bind_FGameplayTag.h`、`ClassGenerator/ASClass.h`、`AngelscriptBindDatabase.h`、`Preprocessor/AngelscriptPreprocessor.h` 乃至 `source/as_*`。消费者没有任何 `PrivateIncludePaths` 或独立 support owner 来声明“我在访问内部实现”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-22` public 导出 `ModuleDirectory`、`Core`、`ThirdParty/angelscript/source`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 只声明普通模块依赖，没有任何指向 runtime internal access 的私有 include 约定。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:6-10` 与 `:38-39` 直接 include `AngelscriptRuntimeModule.h`、`AngelscriptEngine.h`、`Binds/Bind_FGameplayTag.h`、`ClassGenerator/ASClass.h`、`AngelscriptBindDatabase.h`、`AngelscriptBinds.h`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h:15-21` 直接 include `Preprocessor/AngelscriptPreprocessor.h`、`ClassGenerator/ASClass.h` 与 `source/as_context.h`、`source/as_module.h`、`source/as_scriptengine.h`。 |
| 优点 | 跨模块取用内部能力几乎零门槛，editor/test 写白盒逻辑非常直接。 |
| 不足 | 从 `Build.cs` 看不出哪些依赖是在消费稳定 runtime API，哪些是在走 privileged internal lane；这会让未来的模块抽取、header 收口、third-party 升级都缺少“应该先通知谁”的边界。换句话说，internal coupling 目前是 ambient capability，而不是声明式 contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 需要内部实现时，会在自己一侧显式声明 `PrivateIncludePaths` 指向 `UnLua/Private`，同时把 `UnLua`、`Lua` 放在 private deps；内部访问是 consumer-side opt-in，而不是让 `UnLua` 默认把整棵源码树公开。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:37-45`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:47-57`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-84` | “谁需要内部头，谁显式声明”能把 privileged coupling 变成可审计的模块关系。 |
| UnLua | `UnLuaTestSuite` 也把 `UnLua` 留在 private deps，并通过 `Public/` helper 暴露测试夹具，而不是继续依赖 producer 的 accidental export。 | `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:43-55`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestSuite.h:21-32` | 测试/工具链若要访问内部能力，最好通过专用 helper 或 façade，而不是直接共享生产模块的全部 internals。 |
| UnrealCSharp | `UnrealCSharpEditor` 把 `ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion` 等 workflow owner 保持在 private deps，中间层访问关系在 consumer 的 `Build.cs` 中是可见的。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63` | 即便 editor 需要编排大量内部能力，也应通过显式 private edge 或 support owner 表达，而不是默认拿到 producer 的全部内部路径。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 editor/test 建立一条显式的 internal-access lane，把“允许访问 runtime internals 的消费者”从普通 `AngelscriptRuntime` 依赖中分离出来。 |
| 具体步骤 | 1. 新增一个极小的 support owner，例如 `AngelscriptRuntimeInternalAccess`（名称可调整，建议 `Developer` 或 `Editor+Developer` 取决于使用面），第一阶段先只放 wrapper/header façade，承接当前确实被 `AngelscriptEditor`、`AngelscriptTest` 跨模块消费的 internal types：如 `AngelscriptEngine.h` façade、`AngelscriptBinds.h` façade、`ASClass` 访问 façade、preprocessor/VM bridge façade。<br>2. 让 `AngelscriptEditor` 与 `AngelscriptTest` 改为 private 依赖新模块；跨模块 internal 访问必须经由新 owner，而不是继续默认来自 `AngelscriptRuntime` 的 public include roots。<br>3. 完成 consumer 迁移后，再从 `AngelscriptRuntime.Build.cs` 收回 `ModuleDirectory`、`Core`、`ThirdParty/angelscript/source` 的 public export，只保留真正的 runtime public contract。<br>4. 迁移期保留兼容 wrapper：旧 include 路径可以短期在新 support owner 中转发到新 façade，避免一次性改动全部 in-tree include；但新增代码禁止继续直接 include `source/as_*` 或 `ClassGenerator/*`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`，以及新增的 `AngelscriptRuntimeInternalAccess` 模块文件 |
| 预估工作量 | M |
| 架构风险 | 需要先枚举 editor/test 真实依赖的 internal headers，否则 façade 漏项会导致迁移期编译失败；但这种失败是边界显式化必须暴露的真实耦合。 |
| 兼容性 | 对脚本与运行时行为无直接影响。对仓库内 C++ 模块，兼容影响主要体现在 include 路径和模块依赖要显式化，属于中等但可增量控制的风险。 |
| 验证方式 | 1. 迁移后全文搜索，确认 `AngelscriptEditor`、`AngelscriptTest` 不再直接 include `source/as_*` 或 `ClassGenerator/*` 等 runtime internal 路径。<br>2. `AngelscriptEditor`、`AngelscriptTest`、`AngelscriptRuntime` 分别独立编译通过，且 private 依赖图能明确看见 internal-access owner。<br>3. 新建一个只依赖 `AngelscriptRuntime` 的 dummy consumer，确认它不能再 accidental include editor/test 当前使用的 internal headers。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-44 | runtime internals 缺少 consumer-side internal-access contract | 支持模块新增 + public surface 收口 | 高 |
| P2 | Arch-MS-43 | `AngelscriptTest` 公开依赖面与真实 harness 角色不匹配 | 依赖下沉 + test harness public contract 重建 | 中 |

---

## 架构分析 (2026-04-08 23:58)

### Arch-MS-45：bind shard 的扩展协议仍是“加载模块后向 ambient bind state 注册 lambda”，不是显式 provider contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind shard / 未来 leaf bind 模块是否拥有可发现、可替换、可声明的扩展协议 |
| 当前设计 | 当前 generated shard 本质上只是 `FDefaultModuleImpl` 壳：模块被 runtime 通过 `BindModules.Cache` 按名字装载后，`StartupModule()` 里立即调用 `FAngelscriptBinds::RegisterBinds(...)` 把一段注册 lambda 塞进全局 bind registry。更关键的是，这个 registry 的状态 owner 不是 provider module，而是“当前 `FAngelscriptEngine` 的 bind state；如果此时没有 engine，就退回静态 `LegacyBindState`”。因此模块扩展协议实际上是“先 `LoadModule(...)`，再依赖 ambient state 生效”，而不是一条显式的 provider contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1303-1325` 生成的 shard module class 继承 `FDefaultModuleImpl`，并在 `StartupModule()` 中直接调用 `FAngelscriptBinds::RegisterBinds((int32)FAngelscriptBinds::EOrder::Late, [](){...})`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:23-33` 的 `GetBindState()` 优先走 `FAngelscriptEngine::TryGetCurrentEngine()`，失败时回退到静态 `LegacyBindState`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:471-482` 当前暴露的核心扩展点仍是 `RegisterBinds(...)` 与 `GetBindModuleNames()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:594-601` 只把模块名字符串从 cache 读回。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1487` 在 runtime 初始化期读取 `BindModules.Cache` 并逐个 `LoadModule(...)`。 |
| 优点 | 当前路径实现成本低，generated shard 不需要实现额外接口；只要模块名进入 cache 并能被装载，现有 `RegisterBinds(EOrder::Late)` 语义就能继续工作。 |
| 不足 | module graph 无法表达“谁提供 bindings、谁只是 owner shell”；provider 能力既不在 `.uplugin`，也不在 interface，而是隐含在 `LoadModule + RegisterBinds + LegacyBindState` 组合里。后续若要做多插件 bind provider、按领域裁剪 leaf module、或让外部插件安全扩展绑定，当前协议缺少可审计的声明层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaDefaultParamCollector` 直接实现 `IScriptGeneratorPluginInterface`，在 `StartupModule()` 中通过 `IModularFeatures::Get().RegisterModularFeature("ScriptGenerator", this)` 注册；同一 module instance 还显式提供 `GetGeneratedCodeModuleName()` 与 `ShouldExportClassesForModule(...)`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/Private/UnLuaDefaultParamCollector.cpp:47-69` | “谁提供生成能力”是显式 interface contract，而不是运行时按字符串猜测要加载哪些模块。 |
| puerts | `ParamDefaultValueMetas` 同样实现 `IScriptGeneratorPluginInterface`，在 `StartupModule()` / `ShutdownModule()` 中注册与注销 `ScriptGenerator` modular feature，并显式声明导出目标模块与支持的 module type。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:20-50` | provider 通过 modular feature 接入后，owner module 的职责、生命周期和 target scope 都可追踪。 |
| UnrealCSharp | `ScriptCodeGenerator` 是 checked-in 的显式 module owner，`Public` 头直接声明 `IModuleInterface` 生命周期；它不是 synthetic shard，也不依赖 cache 字符串驱动才能存在。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Public/ScriptCodeGenerator.h:8-14`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/ScriptCodeGenerator.cpp:7-20` | workflow owner 即使不使用 `IModularFeatures`，也可以先成为稳定的 module contract，而不是临时生成的 module shell。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 bind shard / leaf bind 模块引入显式 `provider` 协议，把“能提供绑定”的语义从 `LoadModule + ambient state` 中抽出来。 |
| 具体步骤 | 1. 新增一个极小的接口头，例如 `IAngelscriptBindProvider`（建议放入新的 `AngelscriptBindCore` 或等价稳定 owner），至少声明 `GetProviderName()`、`GetBindOrder()`、`RegisterBindings(FAngelscriptBindRegistrar&)` 这类最小 contract。<br>2. 修改 `GenerateSourceFilesV2()`：generated shard 仍可保留现有 module 名与 `IMPLEMENT_MODULE(...)`，但 `StartupModule()` 不再直接向 ambient `FAngelscriptBinds` 注册 lambda，而是把 provider instance 注册到 `IModularFeatures` 或等价 registry。<br>3. 修改 runtime/editor 装载侧：`FAngelscriptEngine` 仍可暂时读取 `BindModules.Cache` 触发模块装载，但装载后应通过 provider registry 枚举真实 provider，而不是把“模块被 load 过”当作“绑定已经注册”的唯一语义。<br>4. 迁移期保留兼容路径：旧 shard 仍可走 `RegisterBinds(EOrder::Late)`，新 provider registry 先做桥接适配；待 generated shard 与 hand-written optional binds 都迁完后，再逐步收缩 `LegacyBindState + LoadModule string list` 这一条隐式协议。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增的 `IAngelscriptBindProvider` / provider registry 文件 |
| 预估工作量 | M |
| 架构风险 | 迁移时会并存“旧 lambda 注册”和“新 provider 注册”两套路径，必须明确去重与顺序规则，否则可能出现重复注册或注册顺序漂移。 |
| 兼容性 | 对现有 script API 可保持向后兼容；第一阶段不要求修改现有 generated module 名、bind 顺序或 runtime 行为。兼容影响主要落在 C++ 扩展方和生成器实现，需要从“直接调用 `RegisterBinds`”过渡到显式 provider contract。 |
| 验证方式 | 1. editor/game/commandlet 三种路径下确认 provider 枚举结果与当前 `BindModules.Cache` 装载结果一致。<br>2. 保持 `RegisterBinds(EOrder::Late)` 的最终执行顺序不变。<br>3. 新建一个试点 leaf bind module，只实现 provider interface，不接入旧 cache 直写逻辑，验证它能被 runtime 正常发现并注册。<br>4. 增加重复注册检测，确认新旧双轨时期不会重复装入同一组 bindings。 |

### Arch-MS-46：bind leaf module 缺少轻量 `bind core`，导致分片并行编译单元仍硬依赖完整 Runtime/VM/JIT 表面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind shard 是否能依赖一个稳定、轻量、低层的 binding kernel，而不是直接依赖完整 `AngelscriptRuntime` |
| 当前设计 | generator 为每个 shard 固定写入 `Core`、`CoreUObject`、`Engine`、`AngelscriptRuntime` 公共依赖，editor shard 再额外依赖 `AngelscriptEditor`；而生成出来的 module cpp 实际只 include `AngelscriptBinds.h` 和被绑定类的头。问题在于 `AngelscriptBinds.h` 自身已经把 `AngelscriptInclude.h`、`AngelscriptBindDatabase.h`、`AngelscriptType.h`、`StaticJIT/StaticJITBinds.h` 这些重依赖打包进来，`AngelscriptRuntime.Build.cs` 又把 `ModuleDirectory`、`Core`、`ThirdParty/angelscript/source` 与一批 runtime public deps 暴露出来。结果是：bind shard 虽然在拓扑上是 leaf module，但在依赖语义上仍直接挂在完整 runtime/VM/JIT 表面下。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1168-1177` 生成 shard 时固定把 `Core`、`CoreUObject`、`Engine`、`AngelscriptRuntime` 放进 `PublicDepends`，editor shard 再额外加 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1276` 的 `GenerateBuildFile()` 只是把这组依赖原样写入 shard `Build.cs`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1314-1325` 生成的 module cpp 只 include `"<ModuleName>Module.h"` 与 `"AngelscriptBinds.h"`，然后进入 `RegisterBinds(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:7-14` 直接 include `AngelscriptInclude.h`、`AngelscriptBindDatabase.h`、`AngelscriptBindString.h`、`AngelscriptType.h`、`StaticJIT/StaticJITBinds.h`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-22` public 暴露 `ModuleDirectory`、`Core`、`ThirdParty/angelscript/source`；`:30-42` 又把 `ApplicationCore`、`EngineSettings`、`DeveloperSettings`、`Json`、`GameplayTags`、`StructUtils` 放到 public deps。 |
| 优点 | 对 generator 很省事，所有 shard 只要统一 include `AngelscriptBinds.h` 就能拿到完整 binding 能力，不需要先设计细粒度核心层。 |
| 不足 | 模块数虽然被切细了，但依赖边没有一起变轻。后续若要把 bind shard 变成稳定 leaf owner、按领域拆出可选 bindings、或做更清晰的 `Runtime Core -> Feature Binds` 反向依赖，都会先撞上“leaf module 仍直接依赖完整 runtime”这道结构性阻力。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `WasmCore` 单独承接 wasm/third-party include；`JsEnv` 再 public 依赖 `ParamDefaultValueMetas`、`WasmCore`，而不是让上层 owner 直接把所有 VM 细节暴露给更多 leaf 模块。 | `Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:39-50`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:149-152` | 先抽出低层 core，再让上层 runtime 或 tooling 依赖它，能显著降低 leaf module 的直接耦合面。 |
| UnrealCSharp | `UnrealCSharpCore` public 只承接 `Core`、`Projects`、`Mono`；上层 `UnrealCSharp` 再 public 依赖 `CrossVersion + UnrealCSharpCore`，其他 UI/输入能力留在 private deps。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-46`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-45` | 即使上层 runtime 很重，也应该给下游保留一层更轻的 core owner，而不是让所有 consumer 直接站到完整 runtime 之上。 |
| UnLua | `UnLuaDefaultParamCollector` 作为 `Program` owner，只依赖 `Programs/UnrealHeaderTool/Public` 与 `Core/CoreUObject`；collector 不需要直接吞下 `UnLua` runtime 的全部公开表面。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:30-56` | tooling / leaf owner 应尽量只拿到自己真正需要的最小 contract。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先抽出一个稳定、轻量的 `bind core`，把 shard 对完整 runtime 的直接耦合降为对最小 registration contract 的依赖。 |
| 具体步骤 | 1. 新增 `AngelscriptBindCore`（名称可调整）模块，第一阶段只承接 bind registration 的最小契约：`EOrder`、provider/registrar 接口、必要的轻量字符串/元数据类型；不要把 `StaticJIT`、raw AngelScript VM 头和完整 type system 一起搬进去。<br>2. 在 `AngelscriptRuntime` 中保留现有 `AngelscriptBinds.h`，但把它逐步改成兼容 façade：对内继续连到旧实现，对外把真正稳定的 registration API 转发到 `AngelscriptBindCore`。<br>3. 修改 `GenerateNewModule()` / `GenerateBuildFile()`，让 runtime shard 的固定公共依赖从 `AngelscriptRuntime` 改为 `AngelscriptBindCore`；editor shard 则优先依赖更薄的 `AngelscriptEditorAPI` 或等价 façade，而不是继续直接依赖完整 `AngelscriptEditor`。<br>4. 第二阶段再审计 `AngelscriptBinds.h` 中的重 include：`StaticJIT/StaticJITBinds.h`、`AngelscriptType.h`、`AngelscriptBindDatabase.h`、`AngelscriptInclude.h` 能 private 就 private；确实需要跨模块暴露的部分，再考虑落到 `AngelscriptVMCore` / `AngelscriptTypeCore` 之类更低层 owner。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`，以及新增的 `AngelscriptBindCore` / 兼容 façade 文件 |
| 预估工作量 | M |
| 架构风险 | 主要风险是第一阶段 façade 设计不当，导致 `AngelscriptBinds.h` 既想兼容旧代码又想收口依赖，最终形成双层壳和循环 include；必须先严格定义 `bind core` 的最小职责，再迁 header。 |
| 兼容性 | 可做成向后兼容迁移：旧的 `AngelscriptBinds.h`、现有 shard module 名和 `RegisterBinds` 调用点都可暂时保留，只是底层 owner 从 `AngelscriptRuntime` 逐步转移到更轻的 core。对脚本 API 无直接破坏。 |
| 验证方式 | 1. 生成一个试点 shard，让它只依赖 `AngelscriptBindCore` 仍能成功编译并注册 bindings。<br>2. 对比迁移前后的 UBT action graph / include graph，确认 shard 不再被动继承 `AngelscriptRuntime` 的完整 public 表面。<br>3. editor/game target 全量编译通过，验证旧 `AngelscriptBinds.h` include 路径仍兼容。<br>4. 新建一个 dummy leaf module，只 include `AngelscriptBindCore` 暴露的最小头，确认不再 accidental 获得 VM/JIT/runtime 内部头。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-45 | bind 扩展协议缺少显式 provider contract，仍依赖 `LoadModule + ambient bind state` | 扩展点重构 + provider registry 引入 | 高 |
| P1 | Arch-MS-46 | bind shard 缺少轻量 `bind core`，leaf module 仍硬依赖完整 runtime/VM/JIT | 依赖分层 + core owner 新增 | 高 |

---

## 架构分析 (2026-04-09 00:08)

### Arch-MS-47：`12+4` bind shard 仍是生成期派生物，当前 checkout 无法从静态模块文件恢复完整模块清单

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块清单是否能在 fresh checkout 中仅通过 `.uplugin + checked-in Build.cs` 被稳定恢复 |
| 当前设计 | 当前插件的静态声明层只公开 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 UE 模块；`ASRuntimeBind_*` / `ASEditorBind_*` 的模块身份与 `Build.cs` 内容仍由 `AngelscriptEditor` 在生成阶段按需写出，`AngelscriptNativeBinds` 这一类聚合 owner 也只剩注释中的历史痕迹。换句话说，`12+4` 更像 compile partition 的瞬时结果，而不是 checkout-stable 的 module inventory。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-32` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1017-1057` 在运行时拼接 `ASRuntimeBind_*` / `ASEditorBind_*` 名称并调用 `GenerateNewModule(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1069-1073` 显示 `AngelscriptNativeBinds` 的 `GenerateBuildFile(...)` 与保存逻辑已经被注释掉。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166-1200` 为每个 shard 生成模块目录、头文件、cpp 与 `Build.cs`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1276` 说明 shard `Build.cs` 本身也是由代码拼接输出，而非稳定 checked-in 规则文件。 |
| 优点 | 生成器可以自由调整 shard 数量、命名策略与依赖集合，不需要每次都修改 `.uplugin` 或维护大量手写 `Build.cs`。 |
| 不足 | 架构审查、交付验证和新开发者入场都无法只靠静态模块文件恢复“当前到底有哪些模块”；想分析代表性 bind shard 依赖时，必须先跑 generator 才会得到可读的 `Build.cs`。这让模块拓扑的 authority 从声明层滑到了工作区状态，也让“12+4 模块架构”更难成为可审计的交付契约。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 直接声明 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector`；生成链 owner 是显式 `Program` 模块，依赖规则由各自 `Build.cs` 静态维护。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-39`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-84`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:47-55` | checkout 本身就能恢复模块 inventory；tooling owner 与 runtime/editor owner 的边界不依赖“先生成一次模块目录”才能成立。 |
| puerts | `.uplugin` 静态列出 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 六个模块；每个 owner 的依赖在 checked-in `Build.cs` 中直接可见。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-47`<br>`Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:56-71`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-47`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:37-43` | 即便模块较多，也优先让模块身份稳定存在于源码树，而不是把“编译分片”直接等同于“模块声明”。 |
| UnrealCSharp | `.uplugin` 明确公开 `UnrealCSharp`、`UnrealCSharpCore`、`CrossVersion`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator`；Runtime/Core/Editor/Generator/Compiler/Program 的依赖都能从源码静态读出。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-52`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-58`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-52`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-44`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-44` | 架构图的可读性首先来自“模块是否能被静态枚举”，然后才是模块数量多少。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“交付层 authoritative modules”与“生成层 bind shards”明确拆成两层，避免再把瞬时分片数量当成插件的稳定模块清单。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/` 下新增一份结构化清单，例如 `Config/AngelscriptModuleInventory.json` 或等价 manifest，显式记录 `declared_modules`、`generated_bind_shards`、`legacy_status`，先把 authority 从口头约定落回源码。<br>2. 修改 `GenerateNativeBinds()` 与 `GenerateBuildFile()`，让它在写 shard 目录/`Build.cs` 时同步生成 `BindShardManifest.json`；`BindModules.Cache` 只保留运行时兼容用途，不再承担“模块清单来源”职责。<br>3. 文档与工具链统一改读这份 inventory/manifest：架构审查、CI 诊断、开发文档只把 `.uplugin` 三模块视为交付层模块，把 `12+4` 视为生成层 compile partitions。<br>4. 若后续仍需要长期保留 legacy shard 路线，再评估是否恢复稳定 checked-in owner（如 `AngelscriptNativeBindsRuntime` / `AngelscriptNativeBindsEditor`），让分片数量退回 owner 内部实现细节，而不是继续裸露成顶层模块名。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 `Plugins/Angelscript/Config/AngelscriptModuleInventory.json` / `BindShardManifest.json` |
| 预估工作量 | M |
| 架构风险 | 主要风险是历史工具或脚本仍把 `ASRuntimeBind_*` 名称当作 authoritative module list；迁移期必须保留 alias/兼容读取，否则会把“信息收口”演成“旧流程失效”。 |
| 兼容性 | 对脚本 API 无直接影响；对构建和诊断工具属于低到中等兼容影响，前提是保留旧 `BindModules.Cache` 与旧 shard 名的过渡映射。 |
| 验证方式 | 1. fresh checkout 下不运行 generator，仅读取 `.uplugin + inventory manifest` 即可得到完整的模块清单与层级分类。<br>2. 运行一次 legacy bind 生成后，确认 `BindShardManifest.json` 与实际生成目录一致。<br>3. 让架构审查脚本或文档生成脚本只依赖 manifest，不再要求磁盘上存在代表性 shard `Build.cs` 才能分析。<br>4. 清理 generated shard 后再次检查，确认 authoritative inventory 仍可被静态恢复。 |

### Arch-MS-48：`AngelscriptTest` 被固定为 `Editor` 模块，但其中相当一部分夹具与验证入口已经是 runtime-capable

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 测试模块的 host type 是否与其实际承载内容一致，能否支持 runtime/headless 验证复用 |
| 当前设计 | `.uplugin` 把 `AngelscriptTest` 固定成 `Editor` 模块，但它的 `Build.cs` 公共依赖主要是 `Core/CoreUObject/Engine/GameplayTags/Json/AngelscriptRuntime`，只有 `CQTest`、`UnrealEd`、`AngelscriptEditor` 这类 editor 依赖被包在 `if (Target.bBuildEditor)` 中；同时模块内还承载大量只依赖 UObject/UInterface/UBlueprintFunctionLibrary 的 runtime 夹具类型。验证拓扑因此呈现出“host type 在 editor，内容边界却部分落在 runtime”这一错位。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:29-32` 把 `AngelscriptTest` 声明为 `Type = "Editor"`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:13-21` 公开暴露整棵测试模块目录与子目录；`:23-31` 的 `PublicDependencyModuleNames` 只有 runtime 侧基础依赖与 `AngelscriptRuntime`；`:40-49` 才在 editor 构建下附加 `CQTest`、`Networking`、`Sockets`、`UnrealEd`、`AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h:17-27` 定义普通 `UENUM` / `UCLASS` runtime fixture。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h:8-35` 定义 `UINTERFACE` / `IInterface` 夹具。<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h:9-29` 定义 `UObject` 与 `UBlueprintFunctionLibrary` 型 UHT coverage 夹具。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h:8-16` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:663-739` 又说明仓库已经存在 runtime 侧 commandlet 与 automation entry，并不是所有验证都天然只能活在 editor host。 |
| 优点 | 把 `AngelscriptTest` 设为 editor-only 可以避免默认发行路径误带大量测试 case，同时让蓝图影响、CQTest、editor automation 这类场景直接复用 `AngelscriptEditor`。 |
| 不足 | runtime-capable fixture 被锁进 editor host type 后，未来若要做 headless/native regression、独立测试插件、或只复用 UHT coverage/native fixture，都必须继续经过 editor 模块入口；同时主插件描述符也被迫长期携带一个 editor-only test module，即使其中不少内容并不依赖 editor。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把测试能力放进独立 `UnLuaTestSuite` 插件，默认 `EnabledByDefault = false`；其模块 host type 是 `Runtime`，并显式 `PrecompileForTargets = PrecompileTargetsType.Any`，只有在 `Target.bBuildEditor` 时才额外附加 `UnrealEd`。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-28`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-63` | runtime-capable 测试夹具可以单独成为 leaf owner，再由 editor 条件依赖补齐 editor-only 行为，而不是先把整个测试模块锁成 `Editor`。 |
| puerts | 主插件 `.uplugin` 只公开 runtime/editor/program 三类产品模块，`WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 的 host type 都直指交付职责，没有额外把验证模块钉死在主插件 DAG 里。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-47` | 验证能力若需要存在，最好作为独立 leaf/plugin；产品模块图不必先为测试 host type 做静态承诺。 |
| UnrealCSharp | `.uplugin` 同样把公开模块限定在 runtime/editor/generator/compiler/program owner，没有在主描述符中再叠一个 editor-only 测试模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-52` | 交付拓扑优先表达产品 owner，测试/验证能力再按需要外挂，更利于后续裁剪与预编译策略管理。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptTest` 拆成“runtime-capable fixtures/support”与“editor automation/cases”两层，让 host type 与内容边界重新对齐。 |
| 具体步骤 | 1. 第一阶段新增 `AngelscriptTestFixtures`（名称可调整，建议 `Runtime` 或单独 disabled test plugin）模块，先迁移 `AngelscriptNativeScriptTestObject`、`AngelscriptNativeInterfaceTestTypes`、`AngelscriptUhtCoverageTestTypes` 这类不依赖 editor 的 UHT/native fixtures。<br>2. 现有 `AngelscriptTest` 保持 `Editor` host，但改为 private 依赖 `AngelscriptTestFixtures`，只保留 `CQTest`、BlueprintImpact、editor-only automation cases 与需要 `AngelscriptEditor` 的测试代码。<br>3. 若后续要支持 headless/native test distribution，再把 runtime 侧 commandlet 或 runtime automation helper 收敛到同一测试 support owner；这样 topology 可演进为 `AngelscriptRuntime <- AngelscriptTestFixtures <- AngelscriptTestEditor`。<br>4. 迁移期保留旧 include 路径和自动化测试名：旧头先转发到新模块，现有 `Angelscript.UnitTests`、BlueprintImpact 测试名和 CI 入口不改，只调整其 owner module。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h`，以及新增的 `AngelscriptTestFixtures` 模块文件 |
| 预估工作量 | M |
| 架构风险 | 主要风险在于现有 UHT 生成、automation 注册和测试 include 路径都默认这些类型仍在 `AngelscriptTest` 模块中；迁移要配合重定向头和 CI 校验，避免把模块拆分变成测试基线断裂。 |
| 兼容性 | 对脚本用户无直接影响；对测试基础设施属于中等兼容影响，但可通过 forwarding headers、保持 automation 名称不变和短期双模块并存实现增量迁移。 |
| 验证方式 | 1. 拆分后重新跑 UHT，确认 fixture 类型仍能生成并被脚本/UHT coverage 用例识别。<br>2. editor automation 与 `Angelscript.UnitTests` 保持同名通过。<br>3. 若引入 runtime fixture 模块，验证 commandlet 或非 editor target 至少能编译该模块，不再被 `Editor` host type 阻断。<br>4. 检查新的依赖图，确认 `AngelscriptTest` 只承载 editor-only case，而 runtime-capable 夹具已经移出。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-47 | 静态模块清单无法恢复 `12+4` 拓扑，bind shard 更像生成期 compile partition | authority 收口 + manifest 新增 | 高 |
| P1 | Arch-MS-48 | `AngelscriptTest` 的 `Editor` host type 与 runtime-capable fixtures 不一致 | 模块拆分 + host type 对齐 | 高 |

---

## 架构分析 (2026-04-09 00:18)

### Arch-MS-49：`.uplugin` 没有声明 UHT 参与能力，build-time toolchain owner 仍停留在描述符盲区

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | build-time `UHT` 参与关系是否已经在插件描述符层显式声明，能否让模块拓扑完整反映真实 toolchain owner |
| 当前设计 | `AngelscriptUHTTool` 已经是实际生效的 build-time exporter，但 `Plugins/Angelscript/Angelscript.uplugin` 仍未声明 `CanBeUsedWithUnrealHeaderTool`；结果是描述符层只能看见 `AngelscriptRuntime / AngelscriptEditor / AngelscriptTest` 三个 UE 模块，看不见“本插件会把自己接入 UHT”这一事实。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:1-49` 含 `EnabledByDefault`、`CanContainContent`、`Modules`、`Plugins`，但未出现 `CanBeUsedWithUnrealHeaderTool`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:12-27` 通过 `[UnrealHeaderTool]` 与 `[UhtExporter(... ModuleName = "AngelscriptRuntime")]` 注册真实 exporter。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:35-53` 说明 exporter 会在 UHT 会话里遍历 `factory.Session.Modules` 并生成 `AS_FunctionTable_*` 输出，toolchain 不是文档概念，而是当前源码中的真实 owner。 |
| 优点 | 现有 `.NET` sidecar 方案不需要立即增加新的 UE module 名，也避免把主路径和 legacy generator 再次混在同一个 editor shell 中。 |
| 不足 | 架构图从描述符层观察时会低估插件真实职责：模块列表看起来像“三模块插件”，但实际还存在一条 build-time UHT lane。对于新开发者、CI 审计和后续 leaf module 增长而言，这会让“谁参与 codegen”继续依赖源码阅读和经验，而不是首先体现在声明层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 直接声明 `CanBeUsedWithUnrealHeaderTool = true`，同时把 `UnLuaDefaultParamCollector` 显式作为 `Program` 模块；collector 的 `Build.cs` 直接依赖 `Programs/UnrealHeaderTool/Public`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:13-16`<br>`Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-55` | UHT 参与能力先在描述符层声明，再由独立 tool module 承接实现，toolchain owner 对外是可见的。 |
| puerts | `.uplugin` 同时声明 `CanBeUsedWithUnrealHeaderTool = true`、`DeclarationGenerator`（Editor）和 `ParamDefaultValueMetas`（Program）；后者的 `Build.cs` 同样直接面向 `Programs/UnrealHeaderTool/Public`。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:13-16`<br>`Reference/puerts/unreal/Puerts/Puerts.uplugin:28-37`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-43` | 即使 toolchain 分散在 editor/program 两个 owner 上，描述符层仍先把“会接入 UHT”这件事说清。 |
| UnrealCSharp | `.uplugin` 也显式声明 `CanBeUsedWithUnrealHeaderTool = true`，并把 `ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 这类 workflow owner 提升为公开模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:13-17`<br>`Reference/UnrealCSharp/UnrealCSharp.uplugin:29-53`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-47` | 即便具体 exporter 逻辑不完全相同，描述符至少先把“本插件存在 build-time 生成链”固定成静态契约。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先补齐描述符级 toolchain capability contract，再把 sidecar owner 写进统一模块清单，避免 build-time lane 继续处于模块图盲区。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Angelscript.uplugin` 中先做低风险声明补齐：增加 `CanBeUsedWithUnrealHeaderTool = true`，不改变现有 `AngelscriptUHTTool` 的实现方式。<br>2. 在现有或新增的模块清单（如前几轮建议的 `AngelscriptModuleInventory.json` / `AngelscriptCodeGenModules.json`）中显式加入 `toolchain_owners` 字段，记录 `AngelscriptUHTTool -> AngelscriptRuntime` 的 build-time 关系。<br>3. 增加一个轻量审计步骤：CI 或本地诊断脚本同时检查 `.uplugin` 的 `CanBeUsedWithUnrealHeaderTool`、`AngelscriptFunctionTableExporter.cs` 的 `[UhtExporter]` 和清单中的 toolchain owner 是否一致。<br>4. 第二阶段再评估是否需要把 sidecar 升格为显式 `Program` owner；若当前 UBT/UHT 集成方式已满足需求，则可以继续保留 sidecar，只要求描述符与清单把其存在显式化。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，以及模块清单/审计脚本文件 |
| 预估工作量 | S |
| 架构风险 | 风险主要在于某些本地工具链可能隐式依赖“未声明也能工作”的现状；因此应先做声明补齐和只读审计，再决定是否触及更深的 toolchain owner 重构。 |
| 兼容性 | 对脚本 API 和运行时行为无直接影响；对构建链属于低兼容性影响，前提是先验证现有 UHT exporter 注册路径不会因新增描述符字段而改变。 |
| 验证方式 | 1. 补齐字段后重新执行一次 UHT / editor build，确认 `AS_FunctionTable_*` 仍正常生成。<br>2. 运行审计脚本，确认 `.uplugin`、`[UhtExporter]` 和模块清单对 toolchain owner 的描述一致。<br>3. 对比补齐前后的模块说明文档/架构图，确认 build-time owner 已能在声明层被读出。 |

### Arch-MS-50：script root 扩展入口被 `CanContainContent` 隐式绑定，未来 code-only leaf plugin 难以自然接入

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件级脚本扩展入口是否与“是否带内容资源”解耦，能否支持未来的 code-only leaf plugin / bind plugin |
| 当前设计 | 当前默认实现通过 `IPluginManager::Get().GetEnabledPluginsWithContent()` 收集插件脚本根；与此同时，`Plugins/Angelscript/Angelscript.uplugin` 自身又声明 `CanContainContent = false`。这意味着脚本扩展入口目前不是“显式 Angelscript extension contract”，而是“带 content 的 enabled plugin 才可能被发现”。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:13-18` 显示插件 `EnabledByDefault = true` 且 `CanContainContent = false`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558-565` 的默认依赖实现只遍历 `IPluginManager::Get().GetEnabledPluginsWithContent()`，并把 `Plugin->GetBaseDir() / "Script"` 加入脚本根列表。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1360` 的 `DiscoverScriptRoots()` 再基于这组插件脚本根构造最终 root 集合。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1487` 同一初始化链里对 bind module 的发现又只读取主插件 `Angelscript` 目录下的 `BindModules.Cache`，进一步说明“脚本扩展”和“代码扩展”目前都还没有显式的 leaf plugin contract。 |
| 优点 | 对当前 host project 和带 content 的插件扩展来说，实现简单，默认行为也容易和 Unreal 的内容插件习惯保持一致。 |
| 不足 | 一旦未来把可选脚本/绑定能力做成 code-only leaf plugin，最自然的做法往往会沿用主插件当前的 `CanContainContent = false`；此时它即使带有 `Script/` 目录，也不会被默认发现。换句话说，扩展性被一个与脚本语义并不等价的内容标志隐式限制了。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `LuaSocket` 作为独立扩展插件，通过 `.uplugin` 的 `Plugins` 字段显式依赖 `UnLua`，模块 `Build.cs` 再私有依赖 `UnLua` 与 `Lua`。从声明层可见，扩展 owner 通过 plugin/module 边接入，而不是依赖主插件运行时去猜“哪些插件有 content”。 | `Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:16-29`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:32-45` | 对可选能力，优先建立 declarative `Extension -> Core` 关系；这是基于 `.uplugin + Build.cs` 的源码推断。 |
| puerts | 主插件把 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 全部作为显式 owner 列在 `.uplugin` 中，`Puerts` 再 public 依赖 `JsEnv`。扩展/层级关系首先由模块图表达，而不是由 content-bearing plugin 扫描推导。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-25`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98` | 当 owner 是显式 module/plugin 时，扩展能力是否参与运行时，不必再借用 `CanContainContent` 这样的旁路条件。 |
| UnrealCSharp | `UnrealCSharp` 同样把 runtime/editor/generator/compiler/program owner 公开写入 `.uplugin`；额外能力如 `EnhancedInput` 通过 `Plugins` 字段和上层 module 依赖显式接入。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:13-17`<br>`Reference/UnrealCSharp/UnrealCSharp.uplugin:55-59`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-44` | 扩展参与条件更适合作为声明式依赖，而不是隐藏在内容插件枚举逻辑里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“能提供 Angelscript scripts/binds 的插件”从 content flag 中解耦，建立显式的 extension plugin 发现契约。 |
| 具体步骤 | 1. 扩展 `FAngelscriptEngineDependencies`：新增面向插件扩展的枚举回调，例如 `GetEnabledAngelscriptPluginRoots` 或等价接口，默认实现改为遍历 `IPluginManager::Get().GetEnabledPlugins()`，而不是只遍历 `GetEnabledPluginsWithContent()`。<br>2. 为了避免无差别扫描所有插件，新增显式 opt-in 条件。增量方案可选其一：检查插件根下是否存在 `Script/` 目录，或读取一份固定文件如 `Config/AngelscriptExtension.json`，把“这是一个 Angelscript extension plugin”显式落回插件目录。<br>3. 让 bind/plugin 扩展发现与这条契约收口到同一入口：未来 leaf plugin 若提供 `Script/` 与 bind manifest，应由同一枚举逻辑发现，而不是继续一边走 content-plugin 扫描、一边只认主插件 `BindModules.Cache`。<br>4. 迁移期保留当前 `GetEnabledPluginsWithContent()` 结果作为 fallback，并在日志中提示哪些 code-only plugin 因新 contract 被额外纳入，以便逐步验证兼容性。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增的 extension manifest / 依赖注入接口文件 |
| 预估工作量 | M |
| 架构风险 | 若直接从“只看 content plugins”切到“扫描所有 enabled plugins”，可能把不相关插件也带入脚本发现范围；因此必须配合显式 opt-in 条件，不能只做粗暴放宽。 |
| 兼容性 | 对现有 host project 和 content plugin 保持向后兼容，前提是保留旧扫描逻辑作为 fallback；新增影响主要体现在未来的 code-only extension plugin 能被自然接入。 |
| 验证方式 | 1. 保持现有 content plugin 场景不变，确认 project root 和已有 plugin script root 发现结果一致。<br>2. 新建一个 `CanContainContent = false` 的试验性 leaf plugin，并放置 `Script/` 或 extension manifest，确认它现在能被 `DiscoverScriptRoots()` 发现。<br>3. 若同时接入 bind manifest，验证 script root 与 bind owner 都来自同一插件枚举入口，不再出现“脚本能发现、绑定不能发现”或反之的分裂。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-50 | script 扩展入口被 `CanContainContent` 隐式绑定 | 扩展契约显式化 + plugin discovery 收口 | 高 |
| P2 | Arch-MS-49 | `.uplugin` 未声明 `CanBeUsedWithUnrealHeaderTool`，toolchain owner 仍处于描述符盲区 | 描述符补齐 + toolchain capability 审计 | 中 |

---

## 架构分析 (2026-04-09 00:31)

### Arch-MS-51：`12+4` bind shard 不是可声明依赖目标，模块图无法表达“按领域选择性启用绑定”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind 分片是否真能作为外部模块/扩展插件可依赖、可裁剪、可选择启用的稳定 owner |
| 当前设计 | 当前 legacy bind 分片的命名与加载语义都围绕“本次生成产出了哪些 bucket”展开，而不是围绕“这个模块代表哪类绑定能力”。`GenerateNativeBinds()` 只生成 `ASRuntimeBind_<起始索引>` / `ASEditorBind_<起始索引>` 这类编号模块名；runtime 启动时再把 cache 里的模块名全部 `LoadModule(...)`。因此模块图能表达“全部加载这些分片”，却不能表达“只依赖 Gameplay/Input/UI 某个绑定域”。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-47` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个固定 UE 模块，以及插件级 `StructUtils` / `EnhancedInput` / `GameplayAbilities` 依赖，没有任何语义化 bind leaf owner。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1017-1021`、`:1027-1031`、`:1043-1047`、`:1053-1057` 生成的模块名是 `ASRuntimeBind_*` / `ASEditorBind_*` 起始索引，而不是稳定领域名。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1477-1488` 只读取主插件 `BindModules.Cache`，随后对 cache 中所有模块名逐个 `LoadModule(...)`，没有领域级选择或依赖裁剪入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp:1-15` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:1-46` 又说明，`GameplayAbilities` / `EnhancedInput` 这类可选领域绑定仍落在主 `AngelscriptRuntime` checked-in owner 下，而不是成为可单独依赖的 bind module。 |
| 优点 | 对当前单插件、全量启用的开发方式最省事：生成器只负责产出 bucket，runtime 只负责全部加载，不需要再维护领域级 manifest 和显式 leaf dependency。 |
| 不足 | 这些分片并不是“可被别人依赖的模块”，而只是“需要被 runtime 全量装载的编译分片”。一旦未来要把 GAS、Input、UI、调试绑定拆成可选 leaf plugin / leaf module，现有编号分片既没有语义名称，也没有选择性启用入口；结果只能继续把可选领域绑定留在主 runtime 或主插件依赖里。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 扩展能力用语义化 leaf plugin / module 承载。`LuaSocket` 自己有 `.uplugin`、稳定模块名和对 `UnLua` 的声明式插件依赖，因此“启用哪个扩展”能直接从描述符层读出。 | `Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:16-29`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:32-45` | 领域能力最好是可命名、可依赖、可开关的 leaf owner，而不是只存在于 runtime 的一串编号分片里。 |
| puerts | `.uplugin` 把 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 都提升为语义化 owner；依赖方可以声明依赖 `JsEnv` 或 `DeclarationGenerator`，不需要理解某次编译切成了多少 bucket。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-25`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98` | 模块名首先表达职责和依赖目标，其次才考虑内部如何分片编译。 |
| UnrealCSharp | `UnrealCSharpCore`、`UnrealCSharp`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 都是稳定 dependency target；生成链和编译链能被静态模块图引用，而不是隐藏成一组编号分片。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-33`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-31`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-31` | 如果一个能力需要长期演进或被其他模块依赖，它就应该先成为稳定 owner，再决定内部的并行编译策略。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“bind 能力的领域 owner”和“内部并行编译分片”解耦：对外只暴露语义化 bind owner，对内才保留编号 shard 作为私有实现。 |
| 具体步骤 | 1. 先定义稳定领域 owner。最小方案可先从 `AngelscriptRuntimeBinds` / `AngelscriptEditorBinds` 两个固定模块开始；如果要支持更细粒度可选启用，再继续细分成 `AngelscriptGameplayBinds`、`AngelscriptInputBinds`、`AngelscriptUIBinds` 等语义模块。<br>2. 把现有 `ASRuntimeBind_*` / `ASEditorBind_*` 降级为语义 owner 内部的私有实现分片，例如生成到固定模块目录下的 `.cpp` shard，或作为语义 owner 私有子目录存在，而不是继续充当外部可见模块名。<br>3. runtime 装载侧改为按语义 owner 或 provider manifest 装载；只有语义 owner 对外可见，内部编号分片由 owner 自己管理，不再暴露给 `.uplugin`、外部插件依赖和缓存协议。<br>4. 把 `GameplayAbilities` / `EnhancedInput` 这类插件级可选依赖逐步从主 `Angelscript.uplugin` 下沉到对应语义 leaf owner；迁移期保留旧全量路径和旧 cache 映射，避免现有工程直接断裂。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`，以及新增的语义 bind owner 模块/manifest 文件 |
| 预估工作量 | L |
| 架构风险 | 领域 owner 一旦命名并公开，就会成为后续插件和 CI 的长期契约；因此第一批 owner 不宜切得过细，否则会把当前“只有编译分片”的复杂度改写成“太多长期产品模块”。 |
| 兼容性 | 对脚本 API 可保持兼容；变化主要在模块依赖和交付方式。可通过保留旧 `BindModules.Cache` 到新语义 owner 的映射，以及短期内继续支持主插件全量路径，做到增量迁移。 |
| 验证方式 | 1. 新增一个试点语义 owner（例如 `AngelscriptGameplayBinds`）并迁出 GAS/EnhancedInput 绑定，确认关闭该 owner 时 runtime 不再加载对应领域绑定。<br>2. 启用/禁用试点 owner，验证脚本可见能力随之变化，而其他领域绑定不受影响。<br>3. 检查 `.uplugin`、manifest 和 build graph，确认对外可见的 bind dependency target 已经从编号 shard 收敛为语义 owner。 |

### Arch-MS-52：generated shard 模板承载不了模块级扩展契约，复杂绑定领域只能继续回流到 `AngelscriptRuntime`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前 generated bind shard 是否具备“独立扩展模块”应有的 build contract 表达能力，例如自定义定义、额外 include path、第三方库或领域级编译选项 |
| 当前设计 | 当前 generator 写出的 shard `Build.cs` 只是一个极薄模板：设置 `PCHUsage`，写入 public/private 依赖数组，然后结束。生成的 module cpp 也只是 include `AngelscriptBinds.h`，在 `StartupModule()` 里注册一段 lambda。这样的模板适合承载纯反射型 wrapper，却不适合承载需要额外定义、私有命名空间、额外 include path、第三方链接或领域级编译开关的真正扩展模块。结果是稍复杂的领域绑定仍更容易留在 checked-in `AngelscriptRuntime/Binds`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1282` 的 `GenerateBuildFile()` 只输出 `PCHUsage`、`PublicDependencyModuleNames`、`PrivateDependencyModuleNames`，没有 `PublicDefinitions`、`PrivateDefinitions`、`bUseUnity`、`PrecompileForTargets`、额外 include path 或第三方链接配置。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1314-1325` 生成的 module cpp 只 include `"<ModuleName>Module.h"` 与 `"AngelscriptBinds.h"`，随后在 `StartupModule()` 里注册绑定。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp:1-15` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:1-46` 显示当前稍复杂、需要特定 helper/type 的领域绑定仍直接放在主 runtime checked-in 源码里；这里关于“复杂领域更难落到 generated shard 模板”是基于模板能力与现有落点的源码推断。 |
| 优点 | 模板极简，生成器实现成本低；只要领域绑定属于“include 现有头 + 调用 `AngelscriptBinds` 注册”，就可以快速塞进 shard，不必为每个分片维护独立规则文件。 |
| 不足 | 一旦某个领域绑定需要自己的 build contract，generated shard 就不再是自然落点。这样模块预算虽然花在了 `12+4` 分片上，但真正需要独立 build 规则的扩展领域仍只能回流到 `AngelscriptRuntime` 或手工新增特殊路径，模块边界对扩展性的帮助被削弱。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `LuaSocket` 作为稳定扩展模块，能在 `Build.cs` 中表达自己的 build contract：`bUseUnity = false`、`PrivateDefinitions` 中的扩展命名空间宏、额外 `src` include path，以及对 `UnLua`/`Lua` 的私有依赖。 | `Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:21-57` | 真正的扩展模块需要能携带自己的编译策略和 ABI 配置，而不是只有一张依赖表。 |
| puerts | `JsEnv` 自己承载大量 `PublicDefinitions`、异常开关、third-party 链接和内容复制逻辑；这类复杂 contract 被固定在语义模块里，而不是交给一组无语义的 micro-shard。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:57-100`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:149-181` | 复杂领域 owner 应该有稳定 `Build.cs`，才能持续承载 third-party、宏定义和构建行为。 |
| UnrealCSharp | `ScriptCodeGenerator`、`Compiler` 都是 checked-in 稳定模块，各自拥有独立 private deps 和 workflow 所需构建契约，而不是依赖一个统一的生成模板。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-47` | 只要某个领域需要独立构建语义，就应提升为稳定 owner，而不是继续强行塞进统一模板。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 明确区分“两类绑定 owner”：纯生成 wrapper 继续走 shard 模板；需要独立 build contract 的领域绑定升级为 checked-in 语义模块。 |
| 具体步骤 | 1. 先建立简单分类规则：如果绑定只需要现有 UE 头 + `AngelscriptBinds.h` 即可注册，就允许继续作为 generated shard；如果需要额外 `Definitions`、include path、第三方库、异常开关、namespace 宏或特定 helper owner，就必须升级为 checked-in 模块。<br>2. 为这类 checked-in 模块预留稳定 owner，例如 `AngelscriptGameplayAbilitiesSupport`、`AngelscriptInputSupport`、`AngelscriptEditorAssetSupport`；把现有复杂领域绑定逐步从 `AngelscriptRuntime/Binds` 迁到这些 owner，而不是继续堆回 runtime。<br>3. 反过来收窄 generated shard 模板职责：它只负责纯 wrapper 分片，不再承担“未来任何可选领域扩展都能落进去”的幻想。这样 `GenerateBuildFile()` 可以保持简单，同时不会再阻碍复杂领域模块化。<br>4. 若后续仍想保留统一生成入口，可让 generator 产出到固定语义 owner 的源文件目录，但不再生成新的模块规则；复杂领域 owner 的 `Build.cs` 一律 checked-in 管理。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`，以及新增的 checked-in 语义 support/bind 模块文件 |
| 预估工作量 | M |
| 架构风险 | 如果分类标准不清，容易出现“简单 wrapper 也被过度模块化”或“复杂领域继续偷偷堆回 runtime”两种反复。第一阶段应只迁最明确的复杂领域，不要一次性重划所有 bind owner。 |
| 兼容性 | 对脚本 API 可保持兼容，前提是不改现有注册名与导出类型；变化主要体现在 C++ 模块依赖和源码落点。通过 forwarding include 和保留旧注册入口，可做到逐领域迁移。 |
| 验证方式 | 1. 选一个复杂领域试点（例如 GAS 或 EnhancedInput），迁到 checked-in 语义模块后重新编译，确认无需修改 generated shard 模板也能正常工作。<br>2. 再选一个纯 wrapper 领域继续走 generated shard，确认新分类规则能同时容纳两类 owner。<br>3. 审查 `AngelscriptRuntime/Binds`，确认复杂领域源码已逐步移出主 runtime，而 generated shard 仍只承担纯 wrapper 分片。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-51 | 编号 bind shard 不是可声明依赖目标，无法按领域选择性启用绑定 | 语义 owner 收口 + 依赖反转 | 高 |
| P1 | Arch-MS-52 | generated shard 模板无法承载模块级扩展契约，复杂领域绑定被迫回流主 runtime | 扩展模块分层 + owner 分类治理 | 高 |

---

## 架构分析 (2026-04-09 00:40)

### Arch-MS-53：主插件声明面优先暴露了验证 owner，却把真实 primary toolchain owner 隐藏在模块图之外

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 主插件 `.uplugin` 当前优先公开的是哪类 owner：产品 owner、toolchain owner，还是验证 owner |
| 当前设计 | 当前描述符把 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 放进主插件模块图，但真正承担 primary build-time 生成链的 `AngelscriptUHTTool` 仍停留在 `.NET sidecar`。这使得主插件公开面先暴露了 verification owner，而不是 toolchain owner。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 UE 模块。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp:9-16` 显示 `AngelscriptTest` 的 `StartupModule()` / `ShutdownModule()` 仅输出日志。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722-730` 把 legacy native bind generator 明确标成 `Debug Only`，并写明 `AngelscriptUHTTool pipeline is the primary path`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:12-27` 通过 `[UnrealHeaderTool]` 与 `[UhtExporter(... ModuleName = "AngelscriptRuntime")]` 注册真实生效的 function-table exporter。 |
| 优点 | 现有主插件描述符保持了很小的 UE module surface，短期内不需要为 sidecar 再引入新的 UE module 名。 |
| 不足 | 对维护者和架构审查工具来说，主插件模块图首先暴露的是 verification owner，而不是当前实际更关键的 toolchain owner。结果是“默认加载什么”和“真正决定绑定生成/交付的是什么”被分散到两张不同的图里。这里关于“公开面优先级倒置”的判断，是基于当前描述符、测试模块实现和 exporter 注册位置的源码推断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 主插件 `.uplugin` 直接公开 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector` 三类产品/toolchain owner；测试能力则被放进单独的 `UnLuaTestSuite` 插件，并且默认不启用。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:16-40`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:16-30` | 主插件公开面优先表达产品 owner 与 toolchain owner，verification owner 通过独立插件 opt-in。 |
| puerts | 主插件 `.uplugin` 公开 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor`；主模块图里直接能读出 runtime/editor/toolchain 层次。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:14-49` | 若某条生成链是长期产品能力，就应进入主模块清单，而不是只藏在 sidecar 或脚本里。 |
| UnrealCSharp | 主插件 `.uplugin` 同时公开 `UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion`、`SourceCodeGenerator`，没有在主描述符里再叠一个默认加载的测试模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:17-54` | 主插件的 declared topology 优先表达 runtime/editor/toolchain，而不是 verification。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 重排主插件“谁应该先被描述出来”的优先级：先把 runtime/editor/toolchain owner 固化为主清单，再把 verification owner 降为 opt-in 或次级清单。 |
| 具体步骤 | 1. 先不改现有模块名，新增一份 checked-in `AngelscriptArchitectureInventory.json`（或等价清单），字段至少包含 `product_owners`、`toolchain_owners`、`verification_owners`；第一版就把 `AngelscriptUHTTool` 明确记为 `toolchain_owner`，把 `AngelscriptTest` 明确记为 `verification_owner`。<br>2. 同步补齐 `Plugins/Angelscript/Angelscript.uplugin` 的 toolchain 能力声明，例如与已有 exporter 对齐的 `CanBeUsedWithUnrealHeaderTool`，让主插件描述层至少能读出“本插件存在 build-time lane”。<br>3. 第二阶段再调整 owner 落点：把 `AngelscriptTest` 迁到独立 `AngelscriptTestSuite` 插件或等价 disabled verification plugin，主插件只保留 runtime/editor/toolchain owner。<br>4. 迁移期保持现有 automation 名称、`UAngelscriptTestCommandlet`、测试源码路径与 CI 入口不变，只改变“这些验证能力属于主插件还是 opt-in verification plugin”的声明方式。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，以及新增的 `AngelscriptArchitectureInventory.json` / verification plugin 描述文件 |
| 预估工作量 | M |
| 架构风险 | 如果直接把 `AngelscriptTest` 从主插件移走，可能影响现有本地开发习惯、CI 启用方式和少量直接依赖测试模块的内部工具；因此应先做角色清单，再做插件拆分。 |
| 兼容性 | 对脚本 API 无直接影响。对 C++/CI 层的影响主要在“测试模块是否默认启用”和“toolchain owner 如何被声明”，属于低到中等兼容性风险，可通过保留旧测试入口实现平滑迁移。 |
| 验证方式 | 1. 让新清单能同时列出 `AngelscriptRuntime` / `AngelscriptEditor` / `AngelscriptUHTTool` / `AngelscriptTest` 的 owner 角色，并与当前 `.uplugin + exporter` 事实一致。<br>2. 补齐描述符后重新运行一次 UHT/function-table 生成，确认主生成链不回归。<br>3. 若第二阶段拆出 verification plugin，则在默认未启用它的工程中验证主插件功能不变；启用后验证现有 automation 与 commandlet 入口仍可执行。 |

### Arch-MS-54：当前“模块数量”至少有三套口径，`12+4` 容易与真正 owner 图混用

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前团队在讨论“插件有多少模块”时，是否存在统一、可审计的计数口径 |
| 当前设计 | 当前至少同时存在三套不同的模块统计口径：描述符口径只有 3 个 declared UE modules；legacy generator 口径是 `ASRuntimeBind_*` / `ASEditorBind_*` synthetic bind modules；primary UHT 口径又是“真实 UE module scope + shard file 数”。这些数字描述的是不同层级，但目前缺少统一命名与报告面。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 的 declared owner 只有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` 的 legacy 路径会额外生成 `ASRuntimeBind_*` / `ASEditorBind_*` synthetic modules。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-27` 说明主线生成链并不是围绕这些 synthetic module，而是围绕 `AngelscriptRuntime` 的 UHT exporter。<br>`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv:2-15` 显示当前 primary UHT 口径下共有 14 个真实 module scope（其中 `UnrealEd`、`UMGEditor` 为 editor-only），合计 32 个 shard file。 |
| 优点 | 多套口径分别回答了不同问题：declared owner 图回答交付面；legacy synthetic module 回答旧生成链的 compile partition；UHT summary 回答当前主线生成链按真实 UE module 分布的覆盖情况。 |
| 不足 | 如果不显式标明口径，`3`、`12+4`、`14/32` 会被混成同一类“模块数量”叙述，进而误导模块裁剪、并行编译、交付边界和路线图优先级判断。这里关于“至少三套口径”的结论，是根据描述符、legacy generator 和当前 UHT summary 的源码/产物交叉验证所得。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 主插件 `.uplugin` 固定公开 3 个 owner（`UnLua` / `UnLuaEditor` / `UnLuaDefaultParamCollector`），测试插件则单独拥有自己的 `.uplugin` 与模块计数口径。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-30` | declared owner 数与 verification owner 数分开统计，不把它们混成一个“主插件总模块数”。 |
| puerts | 主插件 `.uplugin` 固定公开 6 个 owner；即使内部还有代码生成与 metadata 输出，模块数量本身并不会随单次生成批次波动。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 固定 owner 数用于表达长期拓扑，生成规模通过其他报告面表达。 |
| UnrealCSharp | 主插件 `.uplugin` 固定公开 7 个 owner；当它需要第二套“环境发现”口径时，`UnrealCSharpCore.build.cs` 会把结果写成 `UnrealCSharp_Modules.json`，而不是改写主模块数量。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:140-143`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:171-211` | 如果确实需要第二套统计视角，就把它变成具名 manifest / report，而不是继续复用“模块数”一个词。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为不同层级建立具名、稳定的模块统计口径，并让文档/CI/路线图只引用这些具名指标。 |
| 具体步骤 | 1. 新增统一的 `AngelscriptModuleMetrics.json`（或等价报告），字段至少包含 `declared_owners`、`verification_owners`、`legacy_synthetic_bind_modules`、`primary_uht_module_scopes`、`primary_uht_shard_files`。<br>2. 让该报告由脚本自动生成：`declared_owners` 来自 `.uplugin`，`legacy_synthetic_bind_modules` 来自 bind manifest / cache，`primary_uht_*` 来自 `AS_FunctionTable_ModuleSummary.csv`。<br>3. 更新 `Documents/Plans/Plan_StatusPriorityRoadmap.md`、架构审查脚本和后续文档模板，禁止再直接写“当前有 X 个模块”而不标明口径；必须写成例如“declared owners = 3（2026-04-09）”“primary UHT module scopes = 14 / shard files = 32（2026-04-09）”。<br>4. 等 legacy generator 退场后，再把 `legacy_synthetic_bind_modules` 指标降级为 archived metric，但保留历史数据，避免过去 roadmap 与当前主线数据断裂。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv`，以及新增的 `AngelscriptModuleMetrics.json` / 生成脚本 / 路线图文档 |
| 预估工作量 | S |
| 架构风险 | 主要风险不是代码行为回归，而是历史文档和讨论习惯需要一起迁移；若只生成新报告、不更新路线图和脚本，旧口径仍会继续扩散。 |
| 兼容性 | 对脚本 API 和构建行为无直接影响；变化集中在架构治理、报告和文档口径，属于低兼容性风险。 |
| 验证方式 | 1. 在当前工作区生成一次 `AngelscriptModuleMetrics.json`，确认其值能同时复现实测的 `declared owners = 3` 与 `primary UHT module scopes = 14 / shard files = 32`。<br>2. 让后续一份路线图或架构文档引用该报告，确认不再出现未标口径的“模块数量”表述。<br>3. 在 legacy generator 输出变化或 UHT summary 变化后重新生成报告，确认不同指标只影响各自字段，不会互相覆盖语义。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-53 | 主插件 declared topology 先暴露 verification owner、后暴露 toolchain owner | 模块角色重排 + 主插件交付面收口 | 高 |
| P2 | Arch-MS-54 | 模块数量存在多套混用口径，影响路线图和架构决策 | 指标治理 + 报告清单化 | 中 |

---

## 架构分析 (2026-04-09 00:51)

### Arch-MS-55：缺少 `CrossVersion` / compatibility owner，UE 版本与 build-profile 漂移直接落在 `AngelscriptRuntime` 生产模块

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | UE 版本差异、`WITH_EDITORONLY_DATA` / `WITH_EDITOR` 等 build-profile 差异，是否由独立模块 owner 承载 |
| 当前设计 | `.uplugin` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块，没有 dedicated compatibility owner；实际的版本/配置分支直接散落在 runtime core、bind 实现与 preprocessor 中。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明三模块，没有 `CrossVersion` / `Compat` 类 owner。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:669-674` 与 `:1310-1314` 直接用 `#if WITH_EDITOR && ENGINE_MAJOR_VERSION >= 5` 决定 object ptr resolve 行为。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp:70-80` 直接用 `#if ENGINE_MAJOR_VERSION >= 5` 决定是否绑定 `SmoothStep(float64)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:40-45` 把 `WITH_EDITORONLY_DATA`、`UE_BUILD_SHIPPING`、`WITH_SERVER_CODE` 直接翻译成脚本预处理 flags。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/UnversionedPropertySerialization.h:14-20` 直接以 `#if WITH_EDITORONLY_DATA` 包围 schema hash 接口。 |
| 优点 | 少一个模块，短期处理单个适配点时路径最短，不需要先设计新的 owner。 |
| 不足 | 兼容性漂移直接污染生产 owner：UE 升级、editor/data profile 调整和未来 selective migration 都会扩散到 `AngelscriptRuntime` / `Binds` 本体，无法像普通 feature 一样被收敛到单一依赖节点；任何适配也会放大为 runtime 级重新编译与代码审查面。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把 compatibility owner 直接提升为独立 `CrossVersion` 模块；上层 `UnrealCSharp` public 依赖它，`UnrealCSharpEditor` private 依赖它；`UEVersion.h` 统一发布命名好的 capability 宏，且已覆盖到 UE 5.7。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:39-48`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/CrossVersion/Public/UEVersion.h:3-6`<br>`Reference/UnrealCSharp/Source/CrossVersion/Public/UEVersion.h:164-184` | 先把“版本差异 authority”做成稳定 owner，再让 runtime/editor 以依赖关系消费，而不是在各个功能文件里临时开 `#if`。 |
| UnLua | runtime 自身仍是主 owner，但把运行时层的兼容 typedef / helper 收口到 `UnLuaCompatibility.h`；同时 UBT 级版本分支留在 `UnLua.Build.cs` / `UnLuaEditor.Build.cs`，没有把两类兼容逻辑混成一锅。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:17-31`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:22-26`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaCompatibility.h:17-84` | 即便不单独拆模块，至少也要把 runtime-level compatibility contract 与 UBT-level build policy 分层。 |
| puerts | `JsEnv` 通过 `UECompatible.h` 暴露统一兼容 helper；而 `ParamDefaultValueMetas.Build.cs` 这类 toolchain module 再单独处理 UBT 版本分支。compatibility 既有 owner，也有明确入口头。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/UECompatible.h:19-50`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:15-19` | 兼容层可以是“模块 + façade header”组合，不必把所有版本分支都直接散落在产品实现里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增最小 `AngelscriptCrossVersion`（名称可调整）owner，把 engine/build-profile compatibility 从 `AngelscriptRuntime` 生产实现中剥离出来。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Angelscript.uplugin` 中新增 `AngelscriptCrossVersion` runtime 模块；第一阶段可以做成 header-only façade，避免立刻引入复杂启动行为。<br>2. 在新模块中提供 `Public/AngelscriptUEVersion.h` 与 `Public/AngelscriptUECompatible.h`（名字可调整），优先承接当前已定位的差异点：object ptr resolve、`SmoothStep(float64)` 可用性、`WITH_EDITORONLY_DATA` / schema-hash 暴露规则、脚本预处理的 build-profile flag 翻译。<br>3. 让 `AngelscriptRuntime` public 依赖 `AngelscriptCrossVersion`；`AngelscriptEditor`、`AngelscriptTest` 只在需要时 private 依赖。新增代码禁止继续直接在 feature 文件里写 raw `ENGINE_MAJOR_VERSION` / `WITH_EDITORONLY_DATA` 判定，必须走 compatibility façade。<br>4. 第二阶段再把零散 compatibility 头从 `Core/`、`Binds/`、`Preprocessor/` 迁到新 owner；后续 UE 5.7 适配与 AS 2.38 selective migration 先改 `CrossVersion` owner，再改消费点。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/UnversionedPropertySerialization.h`，以及新增的 `Plugins/Angelscript/Source/AngelscriptCrossVersion/*` |
| 预估工作量 | M |
| 架构风险 | 风险在于把 `CrossVersion` 做成新的“宏垃圾桶”。第一阶段必须只收真正跨多个生产 owner 复用的 compatibility 规则，不能把普通 feature helper 也一并吸进去。 |
| 兼容性 | 对脚本 API 无直接影响；对 C++ 层属于内部 owner 重排。只要保留旧 include 一段 forwarding/shim 过渡期，向后兼容风险较低。 |
| 验证方式 | 1. 迁移后重新编译 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`，确认新模块不会引入静态循环。<br>2. 用 `rg` 审计 `Plugins/Angelscript/Source`，确认 raw `ENGINE_MAJOR_VERSION` / `WITH_EDITORONLY_DATA` 只剩在 `AngelscriptCrossVersion` 或极少数明确例外。<br>3. 回归现有 compat 相关 automation，用于确认 façade 没有改变脚本可见行为。 |

### Arch-MS-56：compatibility contract 已经在 `AngelscriptTest` 中形成体系，但生产模块没有共享 spec owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | compatibility knowledge 是不是已经形成可复用的模块契约，还是只在测试里被“事后验证” |
| 当前设计 | 当前仓库已经把兼容性问题系统化地写进 automation tests，但这些规则并没有反哺成共享的生产 owner。compatibility knowledge 主要存在于 `AngelscriptTest` 的断言常量、测试命名和 case 组织里；runtime/binds 仍各自内联实现判定。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp:14-36` 维护了一整组 2.33 / selective 2.38 兼容常量与 flag 约束；`:49-78` 进一步把 `ANGELSCRIPT_VERSION`、property id 区间和 fork 偏移写成 regression assertions。<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp:9-27` 把 `ObjectCastCompat`、`ObjectEditorOnlyCompat`、`TimespanCompat`、`DateTimeCompat` 独立成 compat 测试面。<br>与此同时，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:669-674`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp:70-80`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:40-45` 仍直接内联 compatibility 判定，没有共享 spec/header 被这些实现与测试同时消费。 |
| 优点 | regression 覆盖面已经存在，而且能直接钉住当前 fork 与脚本兼容行为，不会让兼容性 silently drift。 |
| 不足 | compatibility authority 还停留在“测试里知道答案、实现里各自重写逻辑”。这会导致未来升级时必须同时改测试常量和生产实现，外部 C++ 扩展、leaf module、future bind provider 也拿不到可依赖的 compat contract。这里“compatibility contract 目前主要由测试承担 authority”是基于上述测试面与生产实现分布的源码推断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `CrossVersion/Public/UEVersion.h` 直接发布 capability 宏，runtime/editor/tooling 都可以依赖同一份 spec，而不是由测试文件维护 magic numbers。 | `Reference/UnrealCSharp/Source/CrossVersion/Public/UEVersion.h:3-6`<br>`Reference/UnrealCSharp/Source/CrossVersion/Public/UEVersion.h:164-184` | compat spec 应先成为生产 owner 的公共契约，测试只是验证这个契约。 |
| UnLua | `UnLuaCompatibility.h` 把 typedef、property 类型兼容、delegate helper 都做成统一头文件，运行时代码复用这些入口，而不是在每个 call site 自己记版本分界。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaCompatibility.h:17-84` | 即使 compat header 留在模块内部，也应该让实现共享同一份 named contract。 |
| puerts | `JsEnv/Public/UECompatible.h` 把 `UEObjectIsPendingKill`、`FindAnyType`、ticker typedef 等兼容 helper 公开出来；生产代码直接复用这些函数/typedef，而不是仅靠测试覆盖。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/UECompatible.h:19-50` | compatibility helper 最好成为可 include 的 shared façade，而不是只在测试中以断言形式存在。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 compat 测试里已经沉淀出的规则提炼成 shared spec/header，由生产模块与测试共同消费；测试继续做回归，不再充当唯一 authority。 |
| 具体步骤 | 1. 依附 `AngelscriptCrossVersion`（或新增同级 `AngelscriptCompatibility`）增加 `Public/AngelscriptCompatibilitySpec.h`；其中先只暴露命名好的 capability 与 fork policy，例如 object-ptr resolve 支持、`SmoothStep` 双精度可用性、editor-only schema/hash 可见性、当前 selective 2.38 feature gate。<br>2. 对当前确实需要保留 magic number 的 fork 常量，先放入 `Private/AngelscriptForkCompatibility.inl` 或等价私有 spec，再通过少量命名 accessor 暴露给测试和运行时；不要把整张 2.38 property-id 表一次性全部公开成外部 ABI。<br>3. `AngelscriptUpgradeCompatibilityTests.cpp` 与 `AngelscriptCompatBindingsTests.cpp` 改为 include 新 spec/header，测试“导出的 compat contract 与实际行为一致”，而不是继续各自维护独立常量副本。<br>4. 后续新增 compatibility 适配时，流程改为“先写/更新 spec owner，再修改实现与测试”；这样未来 leaf module、generated bind support 也能复用同一份 contract。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，以及新增的 compatibility spec/header 文件 |
| 预估工作量 | M |
| 架构风险 | 若把过多 fork 细节直接暴露成 public ABI，会反向锁死未来 2.38 selective migration 的弹性；因此应先区分“外部 consumer 需要的 capability”与“仅供内部升级测试使用的常量表”。 |
| 兼容性 | 对脚本 API 无影响；对仓库内 C++ 代码是正向收口。只要保留旧测试行为和现有 automation 名称，兼容性风险低。 |
| 验证方式 | 1. 迁移后运行现有 `Angelscript.TestModule.Angelscript.Upgrade.*` 与 `Angelscript.TestModule.Bindings.*Compat*` automation，确认行为不变。<br>2. 审计 runtime/bind 代码，确认 compat 相关命名 helper 已开始被实现与测试共同引用。<br>3. 未来做一次小型 compat 调整时，只更新 spec owner 和少量消费点，验证 blast radius 明显小于当前“测试常量 + 生产实现双改”模式。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-55 | 缺少 `CrossVersion` / compatibility owner，UE 版本与 build-profile 漂移直接污染生产模块 | 新增 compatibility owner + 依赖收口 | 高 |
| P2 | Arch-MS-56 | compatibility knowledge 主要沉淀在测试侧，生产模块没有共享 spec owner | spec 提炼 + 测试从 authority 改为 consumer | 中 |

---

## 架构分析 (2026-04-09 01:05)

### Arch-MS-57：`3` 个 checked-in owner 没有换来更窄的 public fan-out，依赖暴露仍集中在 `AngelscriptRuntime` / `AngelscriptEditor`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 少量顶层模块是否真的降低了依赖复杂度，还是只是把 public fan-out 集中到少数 supernode |
| 当前设计 | 当前 checked-in 静态 DAG 仍是无环的三层结构：`AngelscriptEditor -> AngelscriptRuntime`、`AngelscriptTest -> AngelscriptRuntime`、`AngelscriptTest -(editor target)-> AngelscriptEditor`；但 `AngelscriptRuntime` 基础 public deps 就有 10 条，editor build 再额外公开 `UnrealEd` / `EditorSubsystem`，`AngelscriptEditor` 又直接公开 12 条 editor/runtime 依赖，generated editor shard 还会继续继承 `AngelscriptEditor` 的 public edge。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个稳定 owner。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-42` 公开 `ApplicationCore`、`Core`、`CoreUObject`、`Engine`、`EngineSettings`、`DeveloperSettings`、`Json`、`JsonUtilities`、`GameplayTags`、`StructUtils`；`:67-73` 在 editor build 下再把 `UnrealEd`、`EditorSubsystem` 放进 `PublicDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-26` 公开 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools` 等 12 条依赖。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:13-32` 公开模块根目录与 `AngelscriptRuntime`、`GameplayTags`、`Json`、`JsonUtilities`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1168-1177` 生成 bind shard 时固定注入 `Core`、`CoreUObject`、`Engine`、`AngelscriptRuntime`，editor shard 再额外 public 依赖 `AngelscriptEditor`；`:1214-1276` 把这组依赖原样写进 generated `Build.cs`。 |
| 优点 | 顶层 owner 数量少，checked-in 模块图没有直接静态循环；对维护者来说，主路径入口集中，初次定位模块归属成本较低。 |
| 不足 | public fan-out 与 transitive exposed surface 仍然很宽，模块数量少并不等于耦合少。任何依赖 `AngelscriptRuntime` 或 `AngelscriptEditor` 的 leaf module / generated shard，都会被动继承一大束 engine/editor 依赖，后续想再拆 `BlueprintImpact`、菜单扩展、bind support 或 feature leaf module 时，会先撞上“supernode 过宽”的问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLua` runtime 公开面只保留 `Core`、`CoreUObject`、`Engine`、`Slate`、`InputCore`、`Lua`；`UnLuaEditor` 把 `UnrealEd`、`BlueprintGraph`、`DirectoryWatcher`、`ToolMenus`、`UnLua` 等 editor/workflow 依赖全部留在 private；`UnLuaDefaultParamCollector` 作为独立 `Program` owner 承担 UHT collector。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 少量 public deps + 把 editor/toolchain 依赖收进 private 或独立 owner，能显著压低 transitive fan-out。 |
| puerts | `Puerts` 只把 `JsEnv` 暴露为 runtime 依赖，底层再由 `JsEnv -> ParamDefaultValueMetas` 承接更多实现；`DeclarationGenerator` 单独成 editor module，toolchain fan-out 不必继续挂在 `PuertsEditor` 主表面上。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-29`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16-45`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59` | 即便 editor 模块本身也有较宽依赖，只要 toolchain/runtime 层被拆成稳定 owner，宽 fan-out 就不会全部压回同一条主边。 |
| UnrealCSharp | `UnrealCSharp` runtime public 只保留 `Core`、`Engine`、`CrossVersion`、`UnrealCSharpCore`；`UnrealCSharpEditor` public 只有 `Core`、`UnrealEd`、`DirectoryWatcher`、`CollectionManager`，`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion` 全部是 private orchestration 依赖。 | `Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | public shell 要尽量薄，生成器/编译器/runtime core 这些“组合关系”应优先留在 private 或 leaf module。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先收缩 public fan-out，再决定是否继续增加 owner；不要再用“模块数少”掩盖依赖暴露过宽的问题。 |
| 具体步骤 | 1. 先建立事实基线：新增一个极小 `dummy consumer` 或扫描脚本，只 include 当前 `AngelscriptRuntime/Public` 与 `AngelscriptEditor/Public` 头，列出“真正 header-visible 的依赖集合”。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` 先下沉已确认不该 public 传播的依赖，优先处理 `UnrealEd`、`EditorSubsystem`，随后审计 `ApplicationCore`、`EngineSettings`、`DeveloperSettings`、`JsonUtilities`、`GameplayTags`、`StructUtils` 是否真被 exported header 直接 include。<br>3. 在 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs` 只保留 editor shell 级 public surface；对 `BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`AssetTools`，若只是实现依赖则迁回 private，若确实属于公开 API，则单独抽成更窄的 leaf module，例如 `AngelscriptEditorBlueprintImpact` / `AngelscriptEditorMenus`（名字可调整）。<br>4. 在 `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` 同步下沉 `AngelscriptRuntime`、`GameplayTags`、`Json`、`JsonUtilities` 的 public 暴露，并让 `GenerateNewModule()` 生成的 editor shard 后续依赖新的窄 support module，而不是整块 `AngelscriptEditor`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及可能新增的窄 editor support 模块 |
| 预估工作量 | M |
| 架构风险 | 最大风险是“看似 private 的依赖其实被 public header 间接使用”，如果在没有 consumer 基线的情况下直接下沉，容易造成编译回归；因此必须先做 header-visible inventory，再分批移动依赖。 |
| 兼容性 | 对脚本 API 预期无影响；兼容性风险主要落在外部 C++ consumer 和 generated shard 的编译链。只要保留现有模块名并通过窄 support module 过渡，整体属于低到中等风险。 |
| 验证方式 | 1. `dummy consumer` 只依赖 `AngelscriptRuntime` / `AngelscriptEditor` 时，能够编译当前保留在 `Public/` 的头。<br>2. 修改 `AngelscriptEditor` 的菜单、watcher 或 `AssetTools` 实现后，确认不再无关触发 editor shard 重编。<br>3. 重新审查模块图，确认 checked-in DAG 仍无循环，同时 `AngelscriptRuntime` / `AngelscriptEditor` 的 public deps 明显收窄。 |

### Arch-MS-58：模块分层规则仍停留在人工评审提示，没有进入可执行 guardrail

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块分层规则是否已经变成可执行契约，还是仍靠人工读取 `.uplugin` / `Build.cs` 推断 |
| 当前设计 | 当前关于模块拓扑的“规则”仍主要存在于人工分析流程里：评审脚本要求人去读 `.uplugin`、`Build.cs`、generated shard，再自己画拓扑；runtime 只把 generated module 名保存成字符串列表；UHT codegen 仍靠逐行解析 `AngelscriptRuntime.Build.cs` 推导支持模块。也就是说，当前并没有一份可执行的 layering contract 去自动阻止 `Runtime -> Editor`、`runtime shard -> editor shell`、或非 editor target 混入 `ASEditorBind_*` 这类漂移。 |
| 源码证据 | `Tools/ArchitectureReview/RunArchitectureReview.ps1:66-82` 明确把“读取 `.uplugin`、读取 `Build.cs`、绘制依赖拓扑、检查循环”作为人工步骤。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:584-601` 的 `SaveBindModules()` / `LoadBindModules()` 只读写 `BindModuleNames` 字符串数组。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1488` 读取 `BindModules.Cache` 后直接逐个 `LoadModule(...)`，并没有消费任何“层级规则”或 target 约束。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-384` 通过逐行扫描 `AngelscriptRuntime.Build.cs` 构造 `allModules` / `editorOnlyModules`；`:387-409` 还要先从 UHT session 反推 `AngelscriptRuntime.Build.cs` 的路径。 |
| 优点 | 初期维护成本低，架构评审可以保留人工判断空间，不需要先建立完整的 metadata 与校验工具。 |
| 不足 | 分层约束无法持续执行，任何新模块、新 feature leaf、或 generated shard 规则变更，都只能靠人再次读源码才能发现 drift。即便当前 checked-in DAG 无循环，也没有自动守卫去保证下一轮改动仍满足 `Runtime < Editor < Verification` 这类基本方向。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `.uplugin` 直接声明 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector`，并通过 `CanBeUsedWithUnrealHeaderTool` 把 build-time lane 明确放进描述符；长期 owner 的角色从声明层即可读出。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:16-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 长期 owner 一旦在描述符和固定 `Build.cs` 中稳定下来，就更容易被脚本或 CI 自动验证。 |
| puerts | `.uplugin` 把 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 全部静态声明出来，层级关系由固定 module graph 表达，而不是由运行时 cache 或文本启发式重建。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-29`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59` | 模块职责只要稳定落在声明层，就能为后续 guardrail 提供可机读输入。 |
| UnrealCSharp | 除了 `.uplugin` 中的固定 owner 外，`UnrealCSharpCore.build.cs` 还会额外生成 `Intermediate/UnrealCSharp_Modules.json`，把 Project/Engine modules 与 plugins 组织成结构化索引。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-212` | 除了静态 owner 图，还可以补一层 machine-readable inventory，作为自动化脚本和 CI 校验的输入。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“模块分层规则”从人工提示升级为 checked-in contract + 可执行 validator，让后续重构有持续守卫。 |
| 具体步骤 | 1. 新增一份 checked-in `Config/AngelscriptModuleLayers.json`（文件名可调整），字段至少包括 `module_name`、`role`、`layer`、`target_visibility`、`allowed_dependencies`；第一版先覆盖 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`、`AngelscriptUHTTool`、legacy/generated bind owner。<br>2. 同步生成 `Intermediate/Angelscript/ModuleTopology.json`，把 `.uplugin`、checked-in `Build.cs`、generated bind manifest/cache、UHT module summary 合并成 machine-readable 视图，明确区分 `declared_owners`、`generated_partitions`、`toolchain_owners`。<br>3. 新增 `Tools/Diagnostics/ValidateAngelscriptModuleGraph.ps1`，至少校验四类规则：`Runtime` 不得依赖 `Editor/Verification`；`Editor` 不得反向依赖 `Verification`；non-editor target 不得携带 `ASEditorBind_*`；以及 public fan-out 不得超过约定预算。<br>4. 让 `Tools/ArchitectureReview/RunArchitectureReview.ps1`、CI 初始化脚本、必要的 build/test 入口先调用 validator；文档审查继续保留人工深挖，但基础 layering 错误改由脚本 fail fast。 |
| 涉及文件 | `Tools/ArchitectureReview/RunArchitectureReview.ps1`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及新增的 layer/topology manifest 与 `Tools/Diagnostics/ValidateAngelscriptModuleGraph.ps1` |
| 预估工作量 | M |
| 架构风险 | 若 layer contract 定得过粗，会把当前过渡期的 legacy/generated 特例都判成“非法”；因此第一版需要允许显式 `legacy_exception` / `generated_partition` 标记，先实现观测与 warning，再逐步收紧为 fail。 |
| 兼容性 | 对脚本 API 无影响；对构建/CI 工具链是低风险增强。迁移期只要保留 warning-first 模式，不会阻断现有开发流程。 |
| 验证方式 | 1. 人为引入一条禁止边（例如临时让 runtime 依赖 editor），确认 validator 能在编译前失败或至少告警。<br>2. 正常 editor/game target 构建时，validator 输出的 DAG 与当前 `.uplugin + Build.cs` 一致，并确认仍无静态循环。<br>3. 在 non-editor target 下验证 `ASEditorBind_*` 不再出现在 topology/manifest 中。<br>4. 让后续一轮架构审查直接消费 `ModuleTopology.json`，确认人工分析从“恢复基础事实”转向“讨论改进”。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-57 | public fan-out 被 `AngelscriptRuntime` / `AngelscriptEditor` 过度集中 | 依赖暴露收缩 + support module 收口 | 高 |
| P2 | Arch-MS-58 | 分层规则仍靠人工评审，缺少可执行 guardrail | contract 新增 + validator/CI 守卫 | 中 |

---

## 架构分析 (2026-04-09 01:14)

### Arch-MS-59：generated bind shard 被建模成公共库模块，但模板并没有产出可复用 ABI

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ASRuntimeBind_*` / `ASEditorBind_*` 这些分片模块是否真的应该暴露成 public module contract |
| 当前设计 | generator 会为每个分片写出 `Public/<ModuleName>Module.h`，同时把 `Core`、`CoreUObject`、`Engine`、`AngelscriptRuntime`，以及 editor 分片额外的 `AngelscriptEditor` 写进 `PublicDependencyModuleNames`；但模板生成的 public 头只有一个 `FDefaultModuleImpl` 壳类，连 `*_API` 导出宏都被注释掉，实际注册逻辑完全落在 private `StartupModule()` 和 `FAngelscriptBinds::RegisterBinds(...)` 中。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1168-1177` 固定生成分片的公共依赖集合。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1189-1206` 把产物写到 `Public/<ModuleName>Module.h` 与 `Private/<ModuleName>Module.cpp`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1227-1276` 把上述依赖全部写入 `PublicDependencyModuleNames` / `PrivateDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1300-1304` 仅生成 `class F<ModuleName>Module : public FDefaultModuleImpl`，且 `*_API` 拼接被注释掉。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1314-1325` 生成的 cpp 只 include `"<ModuleName>Module.h"` 与 `"AngelscriptBinds.h"`，随后在 `StartupModule()` 里注册绑定。 |
| 优点 | 生成模板很简单，分片模块不需要维护复杂 public API；对 legacy generator 而言，任何 bucket 都能快速生成可编译的模块壳。 |
| 不足 | 模块图把“纯编译分片”伪装成了“可被别人依赖的公共库”。这会放大 transitive fan-out，也会误导后续重构者把编号分片当成稳定 owner；而事实上，这些模块没有语义化名称，也没有可维护的 C++ ABI。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `ScriptCodeGenerator` 与 `Compiler` 也是工具链 leaf module，但它们要么提供真实导出 API，要么把重依赖留在 private：`ScriptCodeGenerator.Build.cs` 公开面只有 `Core` + `UnrealCSharpCore`，而 `FGeneratorCore` / `FCSharpCompiler` 通过 `SCRIPTCODEGENERATOR_API`、`COMPILER_API` 暴露明确 contract。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Public/FGeneratorCore.h:53-59`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48`<br>`Reference/UnrealCSharp/Source/Compiler/Public/FCSharpCompiler.h:5-24` | 只有当模块真的提供稳定 API 时，才值得保留 public shell；否则应把重依赖压回 private。 |
| UnLua | `UnLuaEditor` 并没有把 watcher / toolbar / editor workflow 相关模块抬成 public contract，而是把 `UnLua`、`DirectoryWatcher`、`BlueprintGraph` 等都放在 private 侧，再用 `DynamicallyLoadedModuleNames` 显式描述 runtime 需要 load 的 editor 模块。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95` | 内部工作流模块可以很复杂，但不必因此暴露成宽 public edge。 |
| puerts | `DeclarationGenerator` 是语义明确的 generator owner，不是编号分片；它的 public 依赖是为声明生成能力服务，而不是为了单纯换取编译并行度。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59` | compile partition 和 module owner 应分离建模；只有有语义的 owner 才进入长期模块图。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 generated bind shard 明确降级为 internal compile partition：先收回 public contract，再决定是否继续保留独立模块名。 |
| 具体步骤 | 1. 第一阶段只改模板，不改运行时装载语义：`GenerateBuildFile()` 里把 `CoreUObject`、`Engine`、`AngelscriptRuntime`、`AngelscriptEditor` 从 `PublicDependencyModuleNames` 下沉到 `PrivateDependencyModuleNames`；public 侧只保留 `Core`，满足 `CoreMinimal.h` / `ModuleManager.h` 所需即可。<br>2. 第二阶段把 `GenerateSourceFilesV2()` 生成的 `Module.h` 迁到 `Private/`，不再创建 `Public/` 目录；如果短期担心旧 include，保留一个仅含 `#include "../Private/<ModuleName>Module.h"` 的兼容 shim。<br>3. 第三阶段再与既有 `Arch-MS-41` / `Arch-MS-46` 的方向汇合，评估是否把编号分片进一步收口到固定 owner（如 `AngelscriptGeneratedBindsRuntime` / `AngelscriptGeneratedBindsEditor`），让“并行编译”彻底脱离“公开模块身份”。<br>4. 追加一个最小 consumer check：确认没有仓库内代码主动 include `ASRuntimeBind_*` / `ASEditorBind_*` 的 public 头后，再去掉兼容 shim。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及所有未来生成的 `ASRuntimeBind_*` / `ASEditorBind_*` `Build.cs` 与 module header/cpp 模板 |
| 预估工作量 | M |
| 架构风险 | 若外部工作区或本地脚本已经错误地把编号分片当 public module 依赖，第一轮收紧后会暴露编译失败；但这类失败本质上是在清理 accidental contract。 |
| 兼容性 | 对脚本 API 无影响；对 C++ 侧属于低到中等风险的构建边界收口。建议一轮过渡期保留 header shim 和旧模块名，避免历史工作区立即中断。 |
| 验证方式 | 1. 重新生成一批 bind shard，确认 `Build.cs` 的 public deps 已明显收窄。<br>2. editor/game target 编译通过，运行时仍能按旧模块名加载并注册绑定。<br>3. 搜索仓库确认没有代码再 include 这些编号模块的 public 头。<br>4. 对比 action graph，确认 transitive deps 没再通过 shard public edge 向外扩散。 |

### Arch-MS-60：`AngelscriptEditor.Build.cs` 低估了真实私有依赖，当前拓扑部分建立在传递依赖泄漏上

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | checked-in `Build.cs` 是否完整表达了 `AngelscriptEditor` 的真实依赖拓扑 |
| 当前设计 | 静态声明层依然保持无循环：`AngelscriptEditor -> AngelscriptRuntime`，`AngelscriptTest -> AngelscriptEditor/AngelscriptRuntime`。但 `AngelscriptEditor` 的实现文件会直接 include 并 `LoadModuleChecked` `AssetRegistry` 与 `KismetCompiler`，而这两个模块并没有出现在 `AngelscriptEditor.Build.cs` 的 public/private 依赖表中。当前编译能成立，说明这里至少部分借用了 `UnrealEd`、`BlueprintGraph`、`Kismet` 等上游模块泄漏出来的 include/link surface。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 当前只声明了 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`AssetTools`、`LevelEditor`、`ContentBrowser`、`ContentBrowserData`、`ToolMenus` 等依赖，未声明 `AssetRegistry` 与 `KismetCompiler`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:12` include `KismetCompilerModule.h`，`:23-24` include `AssetRegistry/IAssetRegistry.h` 与 `AssetRegistry/AssetRegistryModule.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:422-423`、`:564-566` 直接 `LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")` 并访问 `IAssetRegistry`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:510-511` 直接 `LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler")`。 |
| 优点 | 当前工作区在默认 UE 依赖闭包下能编译运行，说明这些功能路径已经被实际使用并验证过，不是死代码。 |
| 不足 | 模块拓扑被低报了。架构分析、未来拆分和 CI 守卫若只看 `Build.cs`，会误以为 `AssetRegistry` / `KismetCompiler` 不是 `AngelscriptEditor` 的直接边；一旦上游模块收紧 public surface，或者后续把相关逻辑拆到新 leaf module，就会出现“图上没这条边，编译却依赖它”的脆弱点。这里仍无静态循环，但真实耦合度比声明 DAG 更高。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把 `DirectoryWatcher`、`MainFrame`、`AnimationBlueprintEditor` 等 editor 依赖显式写在 `PrivateIncludePathModuleNames` / `PrivateDependencyModuleNames` / `DynamicallyLoadedModuleNames` 中；对应源码再去 `LoadModuleChecked<IMainFrameModule>("MainFrame")`、`LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher")`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:47-57`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:103-104`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-35` | 即使模块选择动态加载，也先把边声明清楚，避免依赖图与实现图脱节。 |
| puerts | `DeclarationGenerator` 明确把 `AssetRegistry`、`LevelEditor` 等工具依赖写进 `Build.cs`，源码随后再显式 `LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")` 与 `LoadModuleChecked<FLevelEditorModule>("LevelEditor")`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-50`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:625-632`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1654-1661` | 先让 `Build.cs` 成为真实拓扑，再讨论这些工具能力是否要拆成独立 owner。 |
| UnrealCSharp | `UnrealCSharpEditor` 至少把 `ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion` 这些工作流依赖放进 private orchestration，而不是继续借道上游 public surface。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63` | editor shell 的“真实私有边”需要先被 Build 规则承认，后续模块化才有可靠基线。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `Build.cs` 修正为“真实依赖图”，再在准确拓扑上做 leaf module 拆分；不要继续让 `UnrealEd` / `BlueprintGraph` 充当隐式转运站。 |
| 具体步骤 | 1. 第一阶段只做事实对齐：把 `AssetRegistry` 与 `KismetCompiler` 加入 `AngelscriptEditor.Build.cs` 的 `PrivateDependencyModuleNames`；若后续确认某些模块只需要动态加载，也可再评估是否补到 `DynamicallyLoadedModuleNames`，但前提是 private 依赖先声明完整。<br>2. 用一次仓库级 grep 建立清单：扫描 `AngelscriptEditor/Private/*.cpp` 中的 `LoadModuleChecked` / `GetModuleChecked` / `#include \"*Module.h\"`，找出所有未在 `Build.cs` 声明的模块，形成 `actual_private_edges` 列表。<br>3. 把这份列表接入轻量诊断脚本，例如 `Tools/Diagnostics/ValidateBuildDependencyDeclarations.ps1`，至少校验 editor 模块的“源码用到了但 Build.cs 未声明”的边。<br>4. 在显式边补齐后，再按既有路线把 `AssetRegistry` / `KismetCompiler` 相关逻辑下沉到更窄的 `AngelscriptBlueprintTools` / `AngelscriptEditorAssetTools`；这样拆分时迁的是已声明的真实边，而不是继续处理 accidental transitive edge。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 `Tools/Diagnostics/ValidateBuildDependencyDeclarations.ps1`（或等价脚本） |
| 预估工作量 | S-M |
| 架构风险 | 显式声明缺失依赖本身风险低；真正的风险在于补齐后会暴露更多类似“借道传递依赖”的历史点，需要逐步清理，而不是期待一次补齐就结束。 |
| 兼容性 | 对脚本 API 完全兼容；对 C++ 构建链是低风险修正。唯一可能变化是某些过去依赖 `AngelscriptEditor` accidental public surface 的模块，在后续真正收紧依赖时需要补自己的 `Build.cs`。 |
| 验证方式 | 1. 补齐依赖后做 editor target 全量编译，确认行为不变。<br>2. 人为临时去掉 `AssetRegistry` 或 `KismetCompiler` 声明，验证诊断脚本能报出缺失边。<br>3. 在后续拆分 `Blueprint` / `Asset` 工具模块时，确认移动代码不再触发“找不到上游隐式依赖”的随机回归。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-59 | generated bind shard 被错误建模成 public module contract | 依赖收口 + internal compile partition 化 | 高 |
| P2 | Arch-MS-60 | `AngelscriptEditor.Build.cs` 低估真实私有依赖，拓扑建立在传递依赖泄漏上 | 声明修正 + 诊断守卫 | 中 |

---

## 架构分析 (2026-04-09 23:56)

### Arch-MS-61：缺少固定的目标 owner 矩阵，新增职责仍只能回流 supernode 或 synthetic shard

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前模块图是否已经定义出稳定的 owner 层级，使后续拆分有明确落点 |
| 当前设计 | checked-in 静态 DAG 仍然无循环，核心边为 `AngelscriptEditor -> AngelscriptRuntime`、`AngelscriptTest -> AngelscriptRuntime`，且 editor target 下 `AngelscriptTest -> AngelscriptEditor`。但仓库并没有一份固定的 owner 层级矩阵：`.uplugin` 只声明 3 个 UE 模块，`Source/` 下另有未进描述符的 `AngelscriptUHTTool` sidecar；一旦出现新职责，当前唯一的自然落点仍是继续加粗 `AngelscriptRuntime` / `AngelscriptEditor`，或生成继续依赖这两个 supernode 的 synthetic bind shard。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-79` 把 runtime、可选 feature、editor build 语义一起压进单一 owner。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 把 editor shell、asset workflow、watcher、menu 依赖放进同一模块。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:23-49` 让 verification owner 同时依赖 runtime 与 editor。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1168-1177` 为所有 generated shard 固定注入 `Core`、`CoreUObject`、`Engine`、`AngelscriptRuntime`，editor shard 再额外依赖 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj:12-14` 说明真实 toolchain owner 以 sidecar 形式存在，但没有进入 `.uplugin` 模块层级。 |
| 优点 | 低模块数让初始理解和构建入口都很直接，legacy generator 也容易通过“依赖 runtime/editor supernode”快速产出可编译分片。 |
| 不足 | 之前各轮审查里提到的 `CrossVersion`、`BindCore`、`TestFramework`、feature leaf、toolchain owner 等建议，目前都缺少统一落点。没有固定 owner 矩阵时，任何新增拆分都容易退化成局部重构，下一轮又被 supernode 吞回去。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 用 `UnLua` / `UnLuaEditor` / `UnLuaDefaultParamCollector` 先把 `Runtime` / `Editor` / `Program` 三条主线固定下来，再把测试与扩展做成独立 plugin。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 先固定 owner 层级，再决定哪些能力继续细分；新职责不会默认回流到 runtime/editor 两个 supernode。 |
| puerts | 用 `WasmCore -> JsEnv -> Puerts` 表达 foundation/runtime 递进，再把 `DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 放到 toolchain/editor 层。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/WasmCore/WasmCore.Build.cs:56-79`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-152`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-26`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-43` | compile/runtime/toolchain 层级先有稳定 owner，内部再做更细的实现或生成分片。 |
| UnrealCSharp | 把 `CrossVersion`、`UnrealCSharpCore`、`UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 组成固定矩阵，并在 core 层额外生成结构化 inventory。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/CrossVersion/CrossVersion.Build.cs:25-44`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-79`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-57`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | 当未来要继续拆分时，工程已经有既定 lane，新增模块是在矩阵里补 owner，而不是继续制造一批无长期语义的边。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把“Angelscript 目标模块层级”固定成 checked-in contract，再让后续所有拆分建议按该矩阵落地。 |
| 具体步骤 | 1. 新增一份 checked-in `Config/AngelscriptModuleTargetMatrix.json`（文件名可调整），第一版至少定义 `foundation`、`runtime_product`、`editor_shell`、`toolchain`、`verification`、`feature_pack`、`compile_partition` 七类 lane。<br>2. 先不要求一次性创建所有模块，但要把当前 owner 和保留槽位记清：例如 `AngelscriptRuntime` 归 `runtime_product`，`AngelscriptEditor` 归 `editor_shell`，`AngelscriptUHTTool` 归 `toolchain`，并预留 `AngelscriptCrossVersion`、`AngelscriptBindCore`、`AngelscriptTestFramework`、`AngelscriptExtensions/*` 这类后续 owner 名位。<br>3. 修改 `GenerateNewModule()` 与相关诊断脚本：generated shard 只能标记为 `compile_partition`，且必须声明其父 owner；禁止再把编号 shard 当长期 module identity 使用。<br>4. 之后所有新拆分都按矩阵落位：compatibility 进 `foundation`，bind registration 最小契约进 `foundation` 或 `runtime_product` 的薄 façade，UHT/legacy generator 进 `toolchain`，验证基础设施进 `verification`，可选领域能力进 `feature_pack`。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj`，以及新增的 `Config/AngelscriptModuleTargetMatrix.json` / 诊断脚本 |
| 预估工作量 | M |
| 架构风险 | 如果矩阵直接写成“最终答案”而没有过渡标记，很容易和当前过渡期代码冲突。第一版应该允许 `legacy_owner` / `planned_owner` 字段，并先以 warning 方式运行。 |
| 兼容性 | 对脚本 API 完全兼容；对现有 C++ 依赖也应保持兼容，因为第一阶段只是固定目标层级与命名，不要求立即改动模块名。 |
| 验证方式 | 1. 让矩阵能完整覆盖当前 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`、`AngelscriptUHTTool` 与 generated shard。<br>2. 用矩阵重新绘制依赖拓扑，确认静态 DAG 仍无循环。<br>3. 新增一个试验性 leaf owner 时，检查它能被矩阵归类到唯一 lane，而不是继续默认依赖 supernode。<br>4. 让后续架构审查与诊断脚本直接消费该矩阵，确认“新模块该放哪”已经有统一答案。 |

### Arch-MS-62：主插件尚未形成 `product core + satellite packs` 的交付拓扑

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 主插件的交付轮廓是否已经把产品核心、验证能力和可选领域能力区分开 |
| 当前设计 | 当前主插件仍同时承载 `product core`、`verification` 与一部分 `optional feature domain`。`Angelscript.uplugin` 默认启用并同时声明 `AngelscriptTest`，还把 `StructUtils`、`EnhancedInput`、`GameplayAbilities` 作为主插件级依赖；运行时源码中又直接包含 GAS / EnhancedInput helper 与 hand-written binds。静态依赖图本身没有循环，但交付轮廓已经把“核心必需”和“可选扩展”压进同一个主插件。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:13-18` 说明主插件 `EnabledByDefault = true`，`:29-32` 同时把 `AngelscriptTest` 声明进主插件模块图，`:35-47` 还固定启用 `StructUtils`、`EnhancedInput`、`GameplayAbilities`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:61-64` 把 `EnhancedInput`、`GameplayAbilities`、`GameplayTasks` 直接挂进 runtime 依赖。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h:14` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h:45` 把 GAS helper class 继续放在主 runtime。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp:9-12`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp:1-58`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:1-45` 显示 GAS / EnhancedInput 绑定也仍在主插件 owner 下。 |
| 优点 | 当前 host project 开箱即用，主插件启用后即可拿到测试模块与常用 gameplay/input 支持，不需要再额外安装 companion plugin。 |
| 不足 | 主插件的交付边界越来越宽，未来每新增一个可选领域都容易继续扩张 `Angelscript.uplugin` 与 `AngelscriptRuntime`。这与“仓库是插件开发宿主，真正交付物是可复用插件”这一定位并不完全一致，因为 downstream 项目很难只选择产品核心而排除 validation / domain pack。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 主插件保持 `Runtime + Editor + Program` 三条产品主线，验证能力放在默认关闭的 `UnLuaTestSuite`，领域扩展则做成 `LuaSocket`、`LuaRapidjson` 这类 companion plugin，统一依赖 `UnLua`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:16-30`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:16-29`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:32-45`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaRapidjson/LuaRapidjson.uplugin:16-29`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaRapidjson/Source/LuaRapidjson.Build.cs:33-46` | 交付核心、测试套件、可选扩展分别有自己的 plugin lane，主插件不需要永远背着所有 satellite。 |
| puerts | 主描述符只暴露 `WasmCore`、`JsEnv`、`Puerts`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`PuertsEditor` 这类产品/工具链 owner，没有再把验证模块放进主插件拓扑。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-26`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56` | 主插件清单优先表达产品核心与 toolchain，validation 不必先占据主交付面。 |
| UnrealCSharp | 主插件描述符把 `UnrealCSharp`、`UnrealCSharpCore`、`CrossVersion`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 保持在产品/工具链范围内；尽管也依赖 `EnhancedInput`，但没有再把测试 owner 混进主插件。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:17-60`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-57`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63` | 即使某些 engine plugin 依赖暂时保留在主插件，主拓扑也应先保持“产品核心 + toolchain”清晰，而不是继续叠加 verification 与 future feature pack。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `satellite/plugin-pack` 作为正式交付层引入仓库：验证能力先迁，未来新增可选领域一律走 companion plugin。 |
| 具体步骤 | 1. 在仓库中正式建立 `Plugins/AngelscriptTestSuite/` 与 `Plugins/AngelscriptExtensions/` 两条 lane，并在架构清单/文档中声明它们是主插件之外的标准交付层。<br>2. 第一阶段只做低风险迁移：把现有 `AngelscriptTest` 与后续 `Examples` / `Learning` 内容迁到 `AngelscriptTestSuite` companion plugin，`EnabledByDefault = false`，并通过 `Plugins` 字段依赖 `Angelscript` 主插件；这样立即收窄主插件交付面，而且不改变任何脚本 API。<br>3. 从这一轮开始立新规：新增的可选 engine-domain 绑定不再直接进入 `Plugins/Angelscript/Angelscript.uplugin` 或 `AngelscriptRuntime`，而是进入 `Plugins/AngelscriptExtensions/<FeaturePack>/`，依赖方向固定为 `FeaturePack -> Angelscript`。<br>4. 现有 GAS / EnhancedInput support 先标记为 `legacy-in-core`，单独保留兼容窗口；如果未来要迁出 `UAngelscriptAbilityAsyncLibrary`、`UAngelscriptAbilityTaskLibrary` 这类 script-visible class，再走独立的 redirect / package-name 兼容评估，而不是和本轮交付层收口捆绑。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptTest/*`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`，以及新增的 `Plugins/AngelscriptTestSuite/*.uplugin` 与 `Plugins/AngelscriptExtensions/*` |
| 预估工作量 | M-L |
| 架构风险 | 风险主要在于错误地把现有 script-visible class 与 package owner 立即迁出主插件，那会带来 `"/Script/<Module>"` 层面的兼容问题。因此第一阶段必须先迁 verification 和未来新增 feature pack，不直接移动现有核心脚本类型。 |
| 兼容性 | 第一阶段对脚本 API 完全向后兼容，因为只调整测试/示例与未来增量功能的交付位置。只有后续若决定迁出现有 GAS / EnhancedInput 类型，才需要单独的兼容计划与 redirect 验证。 |
| 验证方式 | 1. 主插件只保留 `product core + editor + toolchain` 后，默认工程仍能正常编译和启动。<br>2. 显式启用 `AngelscriptTestSuite` 后，现有 automation 名称、commandlet 和测试入口保持可用。<br>3. 新增一个试验性 `FeaturePack` plugin，确认它能依赖 `Angelscript` 工作，而无需继续修改 `Angelscript.uplugin` 主描述符。<br>4. 检查主插件 module inventory，确认未来新增可选领域不再直接膨胀主插件交付面。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-61 | 缺少固定的目标 owner 矩阵，新增职责没有统一落点 | 目标拓扑 contract + owner lane 固定 | 高 |
| P1 | Arch-MS-62 | 主插件尚未形成 `product core + satellite packs` 的交付拓扑 | 交付面收口 + companion plugin lane 引入 | 高 |

---

## 架构分析 (2026-04-10 00:09)

### Arch-MS-63：未收口的 standalone bind plugin 生成路径，仍在源码里保留第三套模块拓扑语义

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前模块图是否已经收敛到单一可维护拓扑，还是仍保留过时的并行表达 |
| 当前设计 | 当前源码里并存三套模块拓扑语义：`.uplugin` 中 checked-in 的 `AngelscriptRuntime` / `AngelscriptEditor` / `AngelscriptTest`，menu action 触发的 numbered bind shard 生成路径，以及一个不再被调用的 standalone bind plugin 描述符生成器。结合 `rg -n "GeneratePluginDirectory"` 只命中声明与定义，可推断第三条路径已经不是现行流程，但它的模板仍与当前主拓扑共存。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 checked-in owner。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722-722` 菜单入口仍直接触发 `GenerateNativeBinds()`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1058` 继续批量生成 `ASRuntimeBind_*` / `ASEditorBind_*`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166-1207` 的 `GenerateNewModule()` 仍把 generated shard 作为 sibling module 写出。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:51-51` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:2300-2337` 仍保留 `GeneratePluginDirectory()`，并把所有模块硬编码为 `"Type": "Runtime"`、`"LoadingPhase": "PostDefault"`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:2308-2328` 生成的 standalone 描述符没有 `Plugins` 依赖、没有 host type 区分、也没有平台过滤。 |
| 优点 | 这条残留路径说明项目曾考虑过把 generated binds 交付成独立 plugin，理论上给未来 companion plugin 化留过一个实验入口。 |
| 不足 | 该 helper 既没有调用点，也没有反映当前交付现实：它把 editor/runtime 差异全部抹平成 `Runtime`，还遗漏了 plugin 依赖和平台约束，容易误导后续维护者认为“独立 bind plugin”仍是受支持拓扑，从而在模块边界和交付策略上做出错误假设。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `LuaSocket` 不是临时拼接出来的 descriptor，而是 checked-in 的 companion plugin：`.uplugin` 显式声明 `LuaSocket` 模块、`LoadingPhase = PreLoadingScreen`、`WhitelistPlatforms`，并通过 `Plugins` 字段依赖 `UnLua`；`Build.cs` 再把 `UnLua` / `Lua` 收进私有依赖。 | `Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:16-29`<br>`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/LuaSocket.Build.cs:19-57` | companion plugin 要先有稳定 descriptor contract，再由模块实现填充，不应在 editor helper 里临时拼出一份“默认全是 Runtime”的描述符。 |
| puerts | `Puerts.uplugin` 把 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor` 的 host type 和 loading phase 全部固定在描述符层，`Runtime` / `Editor` / `Program` 的职责划分从声明层就是清晰的。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 长期 owner 的拓扑应该在 descriptor 层直接可读，而不是保留一条无人调用的生成分支去“想象另一种交付形态”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 二选一收口：要么正式引入 bind companion plugin lane，并改成 manifest/template 驱动；要么删除 `GeneratePluginDirectory()`，明确当前只支持 checked-in owner + generated partition。 |
| 具体步骤 | 1. 先做决策收口：在 `Documents/Plans/Plan_StatusPriorityRoadmap.md` 或后续 owner matrix/交付面文档中明确，当前是否仍计划支持“generated binds 独立 plugin 化”。<br>2. 如果答案是否定的，直接删除 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:51` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:2300-2337`，并在诊断/文档里声明当前受支持的模块拓扑只有 declared owner 与 compile partition 两类。<br>3. 如果答案是肯定的，不要继续复用现有 helper；改为新增一份 checked-in descriptor template 或 `Config/AngelscriptGeneratedPluginManifest.json`，字段至少包含 `ModuleName`、`Type`、`LoadingPhase`、`Plugins`、`WhitelistPlatforms`、`ParentOwner`。<br>4. 让 editor 生成器只负责填充 manifest 和 module list，不直接在 `AngelscriptEditorModule.cpp` 里硬编码所有 descriptor 字段；随后再引入一个独立脚本或 toolchain step 去生成 companion plugin。<br>5. 无论走哪条路，都补一个最小 guardrail：扫描 `AngelscriptEditorModule.cpp` 中的 descriptor generator 是否存在未调用 helper，防止下一轮又把废弃拓扑留在源码里。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Angelscript.uplugin`，以及可能新增的 `Config/AngelscriptGeneratedPluginManifest.json` / 诊断脚本 |
| 预估工作量 | S-M |
| 架构风险 | 若仓内或外部私有分支仍手工复用这段 helper，直接删除会暴露其隐藏耦合；因此应先做一次仓库级搜索并在文档里明确迁移路径。若改为 manifest 驱动，风险主要是第一版字段定义过粗，需要允许 `legacy/generated` 过渡标记。 |
| 兼容性 | 对现有脚本 API 和当前默认构建链影响低，因为这条 standalone helper 当前无调用点。若未来正式引入 companion plugin lane，也可以在不改现有模块名的前提下增量落地。 |
| 验证方式 | 1. 再次执行 `rg -n "GeneratePluginDirectory"`，确认源码只剩受支持实现或已完全移除。<br>2. 触发现有 `GenerateNativeBinds()` 菜单路径，确认 numbered shard 生成流程不受影响。<br>3. 若引入 manifest/template，生成一个试验性 companion plugin，验证其 descriptor 含正确的 `Type`、`LoadingPhase`、`Plugins` 与平台约束。<br>4. 重新绘制模块拓扑时，确认“独立 bind plugin”不再作为隐含第三拓扑残留在源码里。 |

### Arch-MS-64：`Private` watcher helper 通过 `ANGELSCRIPTEDITOR_API` 外溢，内部实现被误提升为模块 ABI

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptEditor` 是否把真正的内部实现细节保持在模块内部，还是把 private helper 误导出为 ABI contract |
| 当前设计 | `AngelscriptDirectoryWatcherInternal.h` 位于 `Private/`，但其中两个 helper 函数带有 `ANGELSCRIPTEDITOR_API`；当前实际调用方却都还在 `AngelscriptEditor` 同一模块内部，包括 editor 回调路径和 `Private/Tests/` 下的自动化测试。这意味着当前并不是“为了跨模块复用而显式导出 API”，而是把纯内部实现误提升成了模块导出面。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.h:8-13` 在 `Private` 头中导出 `GatherLoadedScriptsForFolder(...)` 与 `QueueScriptFileChanges(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:24-90` 显示这两个函数只是 watcher 队列整理逻辑，并不构成独立产品 API。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78-93` 的 `OnScriptFileChanges()` 直接调用这两个 helper。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:1-1` 直接 include 该 private 头；`:94-97`、`:122-125`、`:151-154`、`:180-187`、`:214-217` 又反复直接调用 `QueueScriptFileChanges(...)`。 |
| 优点 | 当前测试可以直接覆盖 watcher 队列算法，不必完整拉起 editor UI 或构造更重的 black-box 场景；短期内复用成本很低。 |
| 不足 | 这种做法把“同模块内部测试复用”错误地建模成“模块 ABI 导出”。一旦后续要把 directory watcher、reload queue 或 editor shell 进一步拆分，这个 `Private + *_API` 混搭会让边界变得模糊，也会诱导其他模块继续 include private header，而不是通过稳定 support owner 或 owner object 访问能力。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把 `DirectoryWatcher` 放在 `PrivateDependencyModuleNames`，watcher 注册逻辑也留在 `UnLuaEditorFunctionLibrary.cpp` 的 private 实现中，没有把内部 watcher helper 额外导出成 editor ABI。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:47-95`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-35` | 同样需要 watcher 复用时，优先保持 private owner 封装，而不是把 helper 从 `Private/` 头直接导出。 |
| UnrealCSharp | watcher 生命周期由 `FEditorListener` owner object 管理，析构中完成 `UnregisterDirectoryChangedCallback_Handle(...)`，实现细节停留在 listener 自身，不暴露为独立 editor ABI。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69-84` | 若一段逻辑需要被多个 editor 流程共享，更稳妥的方式是收敛到 owner object，而不是导出 free helper。 |
| puerts | `UPEDirectoryWatcher` 把 watch/unwatch/cleanup 封装在专门的 watcher owner 里，`Watch()`、`UnWatch()` 与析构形成完整生命周期，目录监听能力有单独 owner，但没有靠 `Private` 头导出一组 free function。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14-89` | 当 watcher 逻辑值得复用时，应提升为语义明确的 owner/support，而不是让 private helper 直接成为 ABI。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `Private` helper 退回真正的模块内部实现；如果未来确实需要跨模块复用，再显式引入窄 support owner，而不是继续靠 `*_API + Private/` 混搭。 |
| 具体步骤 | 1. 先做最小收口：移除 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.h:12-13` 上的 `ANGELSCRIPTEDITOR_API`，因为当前已知调用方都位于 `AngelscriptEditor` 同一模块内部。<br>2. 把 `QueueScriptFileChanges(...)` / `GatherLoadedScriptsForFolder(...)` 进一步下沉为真正 internal helper：可以保留在 `Private/`，也可以改成 `namespace` 内的 `static`/匿名实现，并只由 `AngelscriptEditorModule.cpp` 暴露更高层 owner 行为。<br>3. 如果仍希望对队列算法做细粒度自动化测试，不要继续让测试依赖 `Private + exported function`；改为新增极薄的 `AngelscriptEditorWatcherSupport` / `AngelscriptEditorInternalTestSupport`，或把测试迁为黑盒 owner 测试。<br>4. 增加一个轻量 guardrail：扫描 `Private/` 目录下的 `*_API` 暴露，人工白名单之外一律告警，避免 future helper 再被误抬成 ABI。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`，以及可能新增的 watcher support owner / 诊断脚本 |
| 预估工作量 | S |
| 架构风险 | 如果仓内存在未搜索到的其他 private include 或外部分支直接引用这些 helper，移除 `*_API` 后会暴露这些越界用法；但这类 breakage 本身就是边界污染的证据。若改成 support owner，风险主要在测试组织迁移，而不是运行时行为。 |
| 兼容性 | 对脚本 API 无影响。对当前仓内 C++ 调用面预计也是低风险，因为现有已知调用都在 `AngelscriptEditor` 模块内；真正受影响的只会是越界 include private 头的非预期 consumer。 |
| 验证方式 | 1. 去掉 `ANGELSCRIPTEDITOR_API` 后重编 `AngelscriptEditor`，确认 `AngelscriptEditorModule.cpp` 与当前 `Private/Tests/` 仍能通过。<br>2. 仓库级搜索 `AngelscriptDirectoryWatcherInternal.h` 与 `QueueScriptFileChanges`，确认没有新的跨模块 include/callsite。<br>3. 若引入 watcher support owner，确认 `AngelscriptEditor` 不再通过 exported private helper 暴露 queue 逻辑，测试仍能覆盖脚本新增、删除、目录新增、目录删除与 rename window 场景。<br>4. 运行 `DirectoryWatcher` 相关自动化用例，确认行为与当前队列语义一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P2 | Arch-MS-63 | 未收口的 standalone bind plugin 生成路径仍保留第三套模块拓扑语义 | 拓扑收口 + companion plugin/manifest 决策固化 | 中 |
| P1 | Arch-MS-64 | `Private` watcher helper 通过 `ANGELSCRIPTEDITOR_API` 外溢，内部实现被误提升为 ABI | 边界收口 + internal support owner 治理 | 中-高 |

---

## 架构分析 (2026-04-10 00:21)

### Arch-MS-65：`AngelscriptEditor` 仍内嵌 `Private/Tests`，verification owner 没有真正收口到 `AngelscriptTest`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 验证入口是否已经统一收口到专用测试模块，而不是继续散落在产品模块内部 |
| 当前设计 | 当前静态模块图依旧无循环，editor 侧主边仍是 `AngelscriptEditor -> AngelscriptRuntime`，而 `AngelscriptTest` 也已经在 editor target 下 private 依赖 `AngelscriptEditor`。但即便已经存在独立的 `AngelscriptTest` owner，`AngelscriptEditor` 仍继续编译并注册自己的 `Private/Tests` 自动化用例，形成“专用测试模块 + 产品模块内嵌测试”两条 verification lane 并存。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:24-32` 把 `AngelscriptEditor` 与 `AngelscriptTest` 同时声明为 `Editor` 模块。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:40-49` 表明 `AngelscriptTest` 在 editor 构建下已经 private 依赖 `AngelscriptEditor`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp:5-16` 说明仓内已存在专门的 `FAngelscriptTestModule` owner。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:1-18` 直接在产品模块里 include internal header 并注册 `Angelscript.Editor.DirectoryWatcher.*` 测试。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:94-97` 直接调用 `AngelscriptEditor::Private::QueueScriptFileChanges(...)`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:1-2` 与 `:22-75` 又在同一产品模块里注册 `Angelscript.Editor.BlueprintImpact.*` 自动化测试。 |
| 优点 | editor 模块内部测试可以直接覆盖 watcher queue、BlueprintImpact scanner 这类 white-box 行为，不需要先设计额外的 test support surface。 |
| 不足 | verification ownership 被拆散后，`AngelscriptEditor` 的产品边界继续承载测试注册与 internal test-only include；未来要把验证能力迁到 companion plugin、disabled test plugin 或单独 CI lane 时，需要同时迁两条代码路径，而不是只处理 `AngelscriptTest`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把自动化测试完整放进独立的 `UnLuaTestSuite` 插件与模块，默认 `EnabledByDefault = false`；测试规格和 binding tests 都位于 `UnLuaTestSuite/Private/Specs`、`UnLuaTestSuite/Private/Tests`，而不是回流到 `UnLuaEditor`。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-63`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaEnv.spec.cpp:23-25`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp:48-76` | verification lane 可以作为独立 owner 存在，产品 editor module 不必再内嵌 test source。 |
| puerts | `Puerts.uplugin` 的模块图只公开 runtime、editor 与 toolchain owner；`PuertsEditor` 负责 editor integration，本身并没有在模块声明层再混入单独的 verification owner。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:12-45` | 即便 editor 模块很重，也优先把主模块图保持为产品/工具链 owner，而不是让验证职责散落在产品模块里。 |
| UnrealCSharp | 主描述符只暴露 `UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion`、`SourceCodeGenerator`；`UnrealCSharpEditor` 通过 private deps 组合 workflow module，没有再把测试 owner 混进 editor 壳层。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-63` | 先保证 editor owner 只承担产品与 toolchain orchestration，验证若需要扩张，再沿独立 lane 增长。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 让 `AngelscriptTest` 成为唯一 verification owner；`AngelscriptEditor` 只保留最小 test-support contract，不再继续携带 `Private/Tests`。 |
| 具体步骤 | 1. 第一阶段把 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp` 迁入 `Plugins/Angelscript/Source/AngelscriptTest/Editor/` 新分组，保持现有 automation 名称不变。<br>2. 针对当前只能通过 private header 访问的逻辑，新增一个窄的 `AngelscriptEditorTestSupport` owner（或等价 editor test-support surface），先只承接 watcher queue helper 与 BlueprintImpact test hooks，避免 `AngelscriptTest` 继续 include `Private/` 头。<br>3. 调整 `AngelscriptTest.Build.cs`，优先 private 依赖新的 test-support owner；只有仍需要完整 editor 壳层的场景才继续依赖 `AngelscriptEditor`。<br>4. 迁移完成后删除 `AngelscriptEditor/Private/Tests/`，并把“editor 验证代码一律进入 `AngelscriptTest`”写入 `Plugins/Angelscript/AGENTS.md` 或测试约定文档，防止新用例再次回流到产品模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`，以及新增的 `Plugins/Angelscript/Source/AngelscriptEditorTestSupport/*` 或等价 test-support 模块 |
| 预估工作量 | M |
| 架构风险 | 风险主要在于 test-support surface 定义过宽，结果只是把另一套 internal API 永久公开出去；因此第一版应只搬运测试确实需要的窄 helper，而不是把整个 `AngelscriptEditor` 壳层复制成第二个 support module。 |
| 兼容性 | 对脚本 API 无影响；对现有 automation 名称和 CI 入口也可保持兼容。变化主要是 C++ owner 与源码位置调整，对外部项目的风险低。 |
| 验证方式 | 1. editor target 重新编译，确认 `AngelscriptEditor` 不再包含 `Private/Tests` 源文件。<br>2. 运行 `Angelscript.Editor.DirectoryWatcher.*` 与 `Angelscript.Editor.BlueprintImpact.*` 用例，确认名称和行为保持不变。<br>3. 仓库级搜索 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests`，确认 verification source 已全部迁出产品模块。<br>4. 再次绘制模块拓扑，确认 editor verification 只通过 `AngelscriptTest`（及其 support owner）进入图中。 |

### Arch-MS-66：`BlueprintImpact` 已形成独立 analyzer/commandlet 工作流，却仍寄生在 `AngelscriptEditor` supernode

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `BlueprintImpact` 是否已经具备独立 module owner 的条件，而不是继续塞在 editor 壳层里 |
| 当前设计 | `BlueprintImpact` 现在同时具备三类职责：一是公开 analyzer API，二是 commandlet 入口，三是被 class reload 流程直接调用的 runtime-adjacent analysis pass。但这些职责全部仍挂在 `AngelscriptEditor` 单模块之下，导致 editor 壳层同时拥有 UI/integration、reload orchestration、asset scan、commandlet 和测试五类责任。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 说明当前所有 editor 责任都仍收在 `AngelscriptEditor` 一处。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:3-10` 公开头直接暴露 `AssetRegistry` 与 `FAngelscriptEngine` 相关依赖，`:62-68` 又把 scanner API 作为 `ANGELSCRIPTEDITOR_API` 导出。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:5-14` 与 `:112-217` 展示了独立的符号构建与蓝图依赖分析实现。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h:8-14` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55-120` 说明同一子系统还承载 commandlet entry。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:2` 与 `:83-112` 表明 class reload 主路径会直接调用 `AngelscriptEditor::BlueprintImpact` analyzer。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:1-2` 与 `:22-75` 又显示其测试仍编译在产品模块内部。 |
| 优点 | 把 scanner、commandlet、reload helper 放在同一模块里，短期不需要额外设计跨模块调用面；BlueprintImpact 也能直接复用 `AngelscriptEditor` 现有依赖和 include path。 |
| 不足 | `BlueprintImpact` 已经不是单纯的 editor utility function，而是独立 workflow owner；继续寄生在 `AngelscriptEditor` 会让 editor 壳层长期保持 supernode 形态，也让 commandlet/analysis 逻辑无法在不携带完整 editor shell 的前提下被复用或预编译。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把 build-time/default-parameter collector 单独拆成 `UnLuaDefaultParamCollector` `Program` 模块，而不是继续塞在 `UnLuaEditor`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 一旦某条分析/收集链条具备独立输入输出，就应先成为独立 owner，再由 editor 进行 orchestration。 |
| puerts | `PuertsEditor` 负责 editor integration，而声明生成则单独放进 `DeclarationGenerator`；toolchain 责任与 editor 壳层是并列 owner，不是内嵌关系。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:28-49`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:12-45`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-59` | analyzer/generator 这类 workflow 更适合成为稳定 leaf module，由 editor shell 调度，而不是让 editor 本体无限膨胀。 |
| UnrealCSharp | `UnrealCSharpEditor` 通过 private deps 编排 `ScriptCodeGenerator` 与 `Compiler`，而不是把生成器与编译器直接塞进 editor 本体。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48` | editor shell 最稳妥的角色是 orchestration，而不是同时拥有所有 analyzer/commandlet 实现。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `BlueprintImpact` 从 `AngelscriptEditor` supernode 中抽成独立 editor/toolchain leaf module，第一阶段优先迁 scanner 逻辑，保留 commandlet 外部入口兼容。 |
| 具体步骤 | 1. 新增 `AngelscriptBlueprintImpact`（名称可调整）`Editor` 模块，先迁移 `Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h` 与对应 `Private/BlueprintImpact/*.cpp` 分析实现，使 `ClassReloadHelper.cpp` 通过 private 依赖调用新 owner。<br>2. 为了避免立刻改变 commandlet 类路径，第一阶段保留 `UAngelscriptBlueprintImpactScanCommandlet` 在 `AngelscriptEditor` 中，但把其 `Main(...)` 实现改成薄 façade，只转调 `AngelscriptBlueprintImpact` 的扫描服务。<br>3. 把 `AngelscriptBlueprintImpactScannerTests.cpp` 迁到 `AngelscriptTest/Editor/BlueprintImpact/`，并让测试依赖新 leaf module，而不是继续编译在产品 editor module 内。<br>4. 等 scanner owner 稳定后，再评估第二阶段是否连 commandlet 一起迁出；若要迁移 commandlet 类本身，再补 `CoreRedirects` 或保留 forwarding class，单独处理兼容问题。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp`，以及新增的 `Plugins/Angelscript/Source/AngelscriptBlueprintImpact/*` |
| 预估工作量 | M-L |
| 架构风险 | 主要风险在于 commandlet 类路径和 scanner header include 可能已被外部脚本或工具直接引用；因此第一阶段不应马上移动 `UAngelscriptBlueprintImpactScanCommandlet` 类本身，而应先做 implementation owner 抽离。 |
| 兼容性 | 只要 commandlet 类名与现有 automation 名称保持不变，第一阶段对外部调用基本兼容。脚本 API 不受影响；变化集中在 editor/toolchain module 边界。 |
| 验证方式 | 1. editor target 全量编译，确认 `ClassReloadHelper` 仍能经由新模块执行 BlueprintImpact 分析。<br>2. 运行 `UAngelscriptBlueprintImpactScanCommandlet` 现有命令行入口，确认输出与退出码保持一致。<br>3. 运行 `Angelscript.Editor.BlueprintImpact.*` 自动化测试，确认迁移后仍全部通过。<br>4. 检查新依赖图，确认 `AngelscriptEditor` 降回 editor shell/orchestration owner，BlueprintImpact 成为独立 leaf module。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-65 | `AngelscriptEditor` 仍内嵌 `Private/Tests`，verification owner 没有真正收口到 `AngelscriptTest` | owner 收口 + test-support 模块化 | 高 |
| P1 | Arch-MS-66 | `BlueprintImpact` 已形成独立 analyzer/commandlet 工作流，却仍寄生在 `AngelscriptEditor` supernode | workflow leaf module 拆分 + editor shell 瘦身 | 高 |

---

## 架构分析 (2026-04-10 00:31)

### Arch-MS-67：`AutomationController` 依赖仍直接编进 `AngelscriptRuntime`，developer/tooling lane 没有独立 module owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | runtime 内的 coverage / automation / test commandlet 是否已经形成独立 module lane，且真实依赖是否在 `Build.cs` 中可见 |
| 当前设计 | checked-in 静态模块图仍只有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 owner；其中 `AngelscriptRuntime` 的 `Build.cs` 没有声明 `AutomationController`，但 `CodeCoverage` 与 `Testing/IntegrationTest` 直接 include 并 `LoadModuleChecked("AutomationController")`，同时 `UAngelscriptTestSettings` 与 `UAngelscriptTestCommandlet` 仍由 runtime 对外导出。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:45-79` 的 private/editor 依赖列表里没有 `AutomationController`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h:6-8` 在 `WITH_EDITOR` 下直接 include `IAutomationControllerModule.h`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp:21-29` 直接 `LoadModuleChecked<IAutomationControllerModule>("AutomationController")` 并挂接 coverage hook。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp:850-878` 再次通过 `GetAutomationController()` 直接依赖同一模块。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1459-1463` 创建 `FAngelscriptCodeCoverage`，`:1627-1633` 在 `OnPostEngineInit` 中接线 test framework hook。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h:32-125` 仍把 test discovery / coverage / network emulation 配置留在 runtime。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h:8-16` 仍由 `ANGELSCRIPTRUNTIME_API` 导出测试 commandlet。 |
| 优点 | 覆盖率记录、automation controller 监听和 script test commandlet 能直接复用 runtime 生命周期，不需要先搭额外 service bridge。 |
| 不足 | 当前没有显式循环，但真实 DAG 被低报了：`AngelscriptRuntime` 实际上还背着一条未声明的 `AutomationController` developer edge。只要 coverage / integration test / commandlet 继续留在 runtime，本体就很难缩回纯 runtime core，也很难为 future `Developer` / `Editor` test infrastructure 建立独立 owner。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 主插件在描述符层显式分成 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector`；其中 build-time collector 直接建成 `Program` 模块，而不是寄生在 runtime。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 只要某条能力链已经明显偏向 toolchain / build-time，就先给它独立 owner，再由 runtime 通过最小 contract 对接。 |
| UnLua | 编辑器工具依赖全部收在 `UnLuaEditor` 的 private deps 中，runtime 不承担这类 editor/tooling 负担。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95` | 与 editor / tooling 强绑定的依赖，应该优先留在 editor/tool module，而不是继续压在 runtime core 里。 |
| puerts | `.uplugin` 同时声明 `DeclarationGenerator` `Editor` 模块和 `ParamDefaultValueMetas` `Program` 模块；当 generator 需要 `AssetRegistry` 时，`Build.cs` 和源码都显式表达这条边。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-37`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:625-628` | toolchain owner 不但要单独存在，还要把真实依赖完整写回模块声明层，避免“源码用了但 DAG 看不见”的隐形边。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先补齐 `AutomationController` 的声明事实，再把 coverage / automation / test commandlet 从 runtime core 渐进抽成独立 developer/tooling owner。 |
| 具体步骤 | 1. 低风险第一步：在 `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` 的 editor-only private deps 中补上 `AutomationController`，先让 `Build.cs` 与当前源码事实对齐。<br>2. 新增 `AngelscriptRuntimeDevTools` 或 `AngelscriptTestFramework`（名称可调整）模块，第一阶段迁移 `CodeCoverage/*`、`Testing/IntegrationTest.*`、`Testing/UnitTest.*`、`Testing/DiscoverTests.*`、`Testing/AngelscriptTestSettings.*`、`Core/AngelscriptTestCommandlet.*`，让 runtime 只保留最小 service hook。<br>3. 在 `FAngelscriptEngine` 侧新增一个窄接口，例如 `IAngelscriptAutomationServices`，用它替代当前直接 new `FAngelscriptCodeCoverage` 与直接绑定 automation controller 的做法；迁移期允许 runtime 在 editor 下按可选模块查询 service。<br>4. 为了保持向后兼容，第一版保留旧配置节名和 `UAngelscriptTestCommandlet` 外部入口，可以通过 forwarding wrapper / config redirect 把调用转到新 owner，而不是一次性改掉命令名与 ini 路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/*`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/*`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h`，以及新增的 `Plugins/Angelscript/Source/AngelscriptRuntimeDevTools/*` 或等价模块 |
| 预估工作量 | M-L |
| 架构风险 | 主要风险在于新模块类型选择：如果需要保留 commandlet/headless 入口，`Editor`、`Developer`、独立 test plugin 的 target contract 要先界定清楚；另外 config class 与 commandlet class 若直接搬家，容易触发反射路径回归。 |
| 兼容性 | 对脚本 API 无直接影响。只要保留原有 config 节名、automation 名称和 commandlet 外部入口，现有 CI 与开发者工作流可以平滑过渡。 |
| 验证方式 | 1. 补齐 `AutomationController` 依赖后做 editor target 全量编译，确认没有新的 undeclared edge。<br>2. 运行 coverage 开关路径，确认 `Saved/CodeCoverage/` 报告仍能生成。<br>3. 执行 `UAngelscriptTestCommandlet` 现有入口，确认参数与退出码保持兼容。<br>4. 迁移后重新绘制模块图，确认 `AutomationController` 不再由 `AngelscriptRuntime` 直接消费，而是经由新 developer/tooling owner 间接接入。 |

### Arch-MS-68：`AngelscriptTest` 的 `BlueprintImpact` 场景仍借 `AngelscriptEditor` 的 fan-out 泄漏依赖，真实测试拓扑没有收口

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptTest` 是否只依赖自己声明的 editor/test 叶子模块，还是仍在借 `AngelscriptEditor` 的 public fan-out 和未声明边工作 |
| 当前设计 | 当前静态图表面上仍是 `AngelscriptTest -> AngelscriptEditor -> AngelscriptRuntime`，没有显式反向依赖；但 `AngelscriptTest.Build.cs` 没有声明 `AssetRegistry`，`BlueprintImpact` 场景测试却直接 include `AssetRegistryModule.h` 并 `LoadModuleChecked("AssetRegistry")`。再加上 `BlueprintImpactScanner.h` 的 public contract 本身就暴露 `AssetRegistry` 类型，而 `AngelscriptEditor.Build.cs` 也没有显式声明 `AssetRegistry`，导致测试模块实际上建立在“editor fan-out + 上游泄漏”之上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:23-49` 的 public/private 依赖列表中没有 `AssetRegistry`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp:1` include `BlueprintImpact/AngelscriptBlueprintImpactScanner.h`，`:5` 直接 include `AssetRegistry/AssetRegistryModule.h`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp:133-134` 与 `:354-355` 两次 `LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:3-10` 的 public header 直接暴露 `AssetRegistry/*` 与 `FAngelscriptEngine`；`:62-67` 的导出 API 也把 `IAssetRegistry&` 放进函数签名。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 目前同样没有 `AssetRegistry` 依赖声明。 |
| 优点 | `AngelscriptTest` 现在能直接覆盖真实的 asset-backed `BlueprintImpact` 扫描流，验证价值高，而且不需要先设计新的 test-support façade。 |
| 不足 | 这里同样没有静态循环，但真实依赖图再次被低报。即使后续先修正 `AngelscriptEditor` 自身的缺失边，`AngelscriptTest` 也仍然在直接调用 `FModuleManager::LoadModuleChecked<FAssetRegistryModule>`，因此它需要自己的显式叶子依赖或更窄的测试 owner；否则 `AngelscriptTest` 很难收敛成独立 test plugin / precompiled suite。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaTestSuite` 被建成单独插件，默认 `EnabledByDefault = false`；测试模块只声明自身直接需要的 runtime / editor 依赖，没有把“某个 editor leaf 场景”通过上游 fan-out 隐式借进来。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:16-30`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-55` | 测试 owner 可以独立存在，但前提是它的依赖必须是 direct edge，而不是建立在产品模块的 accidental fan-out 上。 |
| puerts | 当 `DeclarationGenerator` 需要扫描资产时，`Build.cs` 直接声明 `AssetRegistry`，源码再显式 `LoadModuleChecked<FAssetRegistryModule>`；依赖边在声明层和实现层是一致的。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-49`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:625-628` | 只要一个模块自己 `LoadModuleChecked` 某个 leaf module，它就应该自己声明这条边，而不是依赖其他 owner 的传递暴露。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `AngelscriptTest -> AssetRegistry` 这条事实边写回 `Build.cs`，再把 `BlueprintImpact` 场景测试收敛到更窄的 editor scenario owner。 |
| 具体步骤 | 1. 最小修复：在 `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` 的 editor-only private deps 中补上 `AssetRegistry`，先让当前 `BlueprintImpact` 场景不再依赖隐式 fan-out。<br>2. 在 `Arch-MS-66` 建议的 `AngelscriptBlueprintImpact` leaf module 落地后，把 `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp` 改为优先依赖该 leaf module，而不是继续通过整个 `AngelscriptEditor` 壳层拿 scanner API。<br>3. 若后续希望让主 `AngelscriptTest` 继续瘦身，可再新增一个更窄的 `AngelscriptEditorScenarioTests` / `AngelscriptBlueprintImpactTests` 模块，专门承接 `AssetRegistry`、`UnrealEd`、`BlueprintImpact` 这类 editor asset 场景；基础 test harness 留在现有 `AngelscriptTest`。<br>4. 迁移期保持 automation 名称和 test path 过滤器不变，只改变模块 owner 与依赖声明，避免 CI 规则和文档入口同时破裂。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`，以及后续可能新增的 `Plugins/Angelscript/Source/AngelscriptEditorScenarioTests/*` 或等价测试模块 |
| 预估工作量 | S-M |
| 架构风险 | 第一阶段补依赖风险很低；真正的风险在第二阶段 owner 调整，如果 `BlueprintImpact` 叶子模块与测试模块一起落地不稳，可能暂时同时存在 “旧 editor 壳层入口 + 新 leaf 入口” 两套调用面。 |
| 兼容性 | 对脚本 API 无影响。只要保留现有 automation 名称与测试过滤前缀，CI 与开发者的测试命令基本不需要改动。 |
| 验证方式 | 1. 补齐 `AssetRegistry` 后重新编译 `AngelscriptTest`，确认 `BlueprintImpact` 用例不再依赖隐式上游泄漏才能通过。<br>2. 运行 `Angelscript.TestModule.BlueprintImpact.*` 相关 automation tests，确认三条场景用例保持通过。<br>3. 在后续 leaf module 拆分后重新检查模块图，确认 `AngelscriptTest` 对 `BlueprintImpact` 的依赖已经从 `AngelscriptEditor` supernode 收窄到显式 leaf edge。<br>4. 仓库级搜索 `LoadModuleChecked<FAssetRegistryModule>`，确认每个调用方都在各自 `Build.cs` 中声明了 `AssetRegistry`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-67 | `AutomationController` 依赖仍直接编进 `AngelscriptRuntime`，developer/tooling lane 没有独立 module owner | 依赖声明补齐 + developer/tooling 模块抽取 | 高 |
| P2 | Arch-MS-68 | `AngelscriptTest` 的 `BlueprintImpact` 场景仍借 `AngelscriptEditor` 的 fan-out 泄漏依赖，真实测试拓扑没有收口 | 测试依赖显式化 + editor scenario owner 收窄 | 中-高 |

---

## 架构分析 (2026-04-10 00:40)

### Arch-MS-69：主线并行编译已经转向 `UHT file shard`，legacy `12+4` synthetic module 只剩第二套冗余坐标系

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前“绑定分片”到底由哪条主线承担，以及 `12+4` 模块分片是否仍是主架构的一部分 |
| 当前设计 | checked-in 模块层只有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 owner；legacy 路径仍能按 `ModuleCount = 10` 生成 `ASRuntimeBind_*` / `ASEditorBind_*` synthetic modules，但源码已经把它标成 `Legacy Native Bind Generator (Debug Only)`，同时明确 `AngelscriptUHTTool` 才是 primary path。更关键的是，当前主线 UHT 已按真实 `UhtModule` 生成 `AS_FunctionTable_<Module>_<Shard>.cpp` 文件分片，并在当前工作区产出 `14` 个真实 module scope、`32` 个 shard file，这条并行轴已经覆盖了 legacy `12+4` 想解决的编译吞吐问题。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 checked-in 模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:722-730` 把 `GenerateNativeBinds()` 明确标成 `Legacy Native Bind Generator (Debug Only)`，并写明 `AngelscriptUHTTool pipeline is the primary path`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1057` 的 legacy 路径仍按固定 `10` key bucket 生成 `ASRuntimeBind_*` / `ASEditorBind_*`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1282` 显示 legacy shard `Build.cs` 只是模板化写出依赖。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:46-79`、`:115-139`、`:166-215` 显示主线 codegen 直接遍历 `factory.Session.Modules`，按真实 module 输出 `AS_FunctionTable_*` shard 与 summary。<br>`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv:2-15` 显示当前工作区已生成 `Engine=16`、`UMG=3`、`AngelscriptRuntime=2` 等总计 `32` 个 shard file。 |
| 优点 | 主线 UHT 分片已经是 deterministic、按真实 UE module 聚合、并且自带 summary/report，天然比 synthetic module 更适合做长期并行编译与覆盖统计。 |
| 不足 | 现在插件同时维护“legacy synthetic module 分片”和“primary UHT file shard 分片”两套并行编译坐标系。继续把 `12+4` 当作当前模块架构描述，会夸大 module management 复杂度，也会误导后续优化仍围绕 synthetic module 数量，而不是围绕当前主线真正使用的 `AS_FunctionTable_*` file shard。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把 build-time collector 收敛为显式 `Program` 模块 `UnLuaDefaultParamCollector`，主插件模块图仍保持 `Runtime + Editor + Program` 三类 owner，没有再为了并行生成引入一组 synthetic runtime modules。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56` | 生成吞吐应优先落在 tool/program owner 内部，而不是继续膨胀产品模块数。 |
| puerts | `ParamDefaultValueMetas` 和 `DeclarationGenerator` 分别承担 `Program` / `Editor` 生成职责；运行时模块图固定为 `WasmCore`、`JsEnv`、`Puerts` 等稳定 owner，而不是额外派生编号 bind modules。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-59` | 先固定 toolchain owner，再把生成量分摊到文件或工具内部；不要让 compile partition 直接变成新的模块身份。 |
| UnrealCSharp | 主插件把 `ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 作为显式 toolchain owner，同时由 `UnrealCSharpCore` 生成结构化 `UnrealCSharp_Modules.json`；它扩展的是 manifest/index，而不是再平行维护第二套 synthetic module 命名。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-48`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-211` | 一旦主线已经有稳定 toolchain lane，剩余优化应继续投入 report/manifest，而不是保留第二套模块级分片。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 正式把“当前架构的并行编译主线”收口到 `AngelscriptUHTTool + AS_FunctionTable_*`，把 legacy `12+4` 降级为 developer/debug compatibility lane。 |
| 具体步骤 | 1. 在 `Documents/Plans/Plan_StatusPriorityRoadmap.md`、`Documents/Guides/Build.md`、后续 module inventory/架构图中，把“当前绑定分片”改写成两层口径：`declared owners = 3`，`primary compile shards = AS_FunctionTable_*`；`ASRuntimeBind_* / ASEditorBind_*` 明确标注为 `legacy/debug-only`。<br>2. 在 `Tools/ArchitectureReview/` 或 `Tools/Diagnostics/` 新增一个轻量检查，直接读取 `AS_FunctionTable_ModuleSummary.csv` / `AS_FunctionTable_Summary.json` 作为主线并行度来源，不再把 `12+4` 作为默认 KPI。<br>3. 给 legacy 路径补一个显式开关，例如 `bEnableLegacyNativeBindGenerator` 或独立 developer module；默认 build/documentation/CI 不再承诺这条路径。<br>4. 若后续确认 legacy 路径只剩历史调试价值，再把 `FAngelscriptEngine` 对 `BindModules.Cache` 的默认读取改成 developer-only，避免主插件继续同时背两套分片协议。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv`、`Documents/Plans/Plan_StatusPriorityRoadmap.md`、`Documents/Guides/Build.md`，以及新增的 inventory/diagnostic 脚本 |
| 预估工作量 | S-M |
| 架构风险 | 风险主要在于少量内部调试流程可能仍把 `ASRuntimeBind_*` 当成当前主线；因此第一阶段应先做“口径收口 + 文档/脚本切换”，而不是立刻删除 legacy 代码。 |
| 兼容性 | 对现有脚本 API 与 UHT 主线生成结果无影响。legacy FunctionCallers 调试流程可通过显式开关保留；变化集中在架构口径、构建入口和默认装载策略。 |
| 验证方式 | 1. 运行一次当前 UHT 生成链，确认 `AS_FunctionTable_Summary.json` / `AS_FunctionTable_ModuleSummary.csv` 仍能稳定产出，并作为新的主线分片报告。<br>2. 检查构建文档和架构文档，确认不再把 `12+4` 写成“当前主模块数”。<br>3. 关闭 legacy 开关后做常规 editor build，确认主线功能不受影响。<br>4. 如保留 legacy 调试入口，单独触发一次，确认其输出仍可用但不会再被主线 inventory 误记为正式模块架构。 |

### Arch-MS-70：三个 checked-in owner 仍按“仓内源码协作”暴露边界，尚未收敛成可复用插件的交付级 ABI

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前真正对外可见的 checked-in owner，是否已经形成适合 standalone/reusable plugin 的稳定公共边界 |
| 当前设计 | 当前仓库里能被静态读到的 owner 只有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块，因此它们实际上就是插件交付面的 C++ contract。但这三个模块的边界仍明显偏向“仓内源码共享”：`AngelscriptRuntime` 公共 include 直接暴露 `ModuleDirectory`、`Core` 和 third-party 源码目录；`AngelscriptEditor` 把大量 editor/tooling 依赖放在 `PublicDependencyModuleNames`，并且 `Public/` 树里存在直接 include runtime internals 的实现 `.cpp`；`AngelscriptTest` 仍把模块根目录当成 public include root。对于“仓库是宿主工程，真正交付物是可复用插件”这一目标，这意味着 checked-in owner 还没有收敛成 binary/distribution 友好的 ABI。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 checked-in 模块。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-22` 把 `ModuleDirectory`、`Core` 和 `ThirdParty/angelscript/source` 暴露为 public include roots；`:30-42` 与 `:69-73` 又把 `DeveloperSettings`、`GameplayTags`、`StructUtils`、`UnrealEd`、`EditorSubsystem` 直接带进 public deps。<br>`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-26` 把 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools` 放进 `PublicDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:3-9` 的 public header 直接 include `AssetRegistry/*` 与 `Core/AngelscriptEngine.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:1-5` 这个位于 `Public/` 树中的实现文件直接 include `AngelscriptEngine.h`、`ClassGenerator/*`、`AngelscriptRuntimeModule.h`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:13-32` 继续把测试模块根目录与 `AngelscriptRuntime` 等依赖暴露为 public contract。 |
| 优点 | 仓内开发时，跨目录 include 和白盒测试访问非常直接；新加 editor/runtime 调用点时不需要先设计窄 façade。 |
| 不足 | 一旦把这三个 checked-in owner 当成真正的插件交付面，这种“源码级共享边界”就会把 internal layout、third-party 源码路径、runtime internals 和测试 harness 一起暴露出去。下游若想把它当成可复用插件依赖，很难只消费稳定 public API，而不被迫跟随仓内目录与实现细节。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLua` runtime 的 public deps 只保留 `Core/CoreUObject/Engine/Slate/InputCore/Lua`，而 `UnLuaEditor` 把 `UnrealEd`、`BlueprintGraph`、`DirectoryWatcher`、`ToolMenus`、`UnLua` 等 editor/workflow 依赖收在 private deps；默认参数 collector 再独立成 `Program` owner。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95`<br>`Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40` | runtime/editor/toolchain 的公共边界先收窄，再通过独立 owner 承接重型实现。 |
| puerts | `Puerts` runtime 的 public 依赖集中在 `Core/CoreUObject/Engine/InputCore/Serialization/OpenSSL/UMG/JsEnv`，声明生成与默认参数元数据分别由 `DeclarationGenerator` 和 `ParamDefaultValueMetas` 承担，避免把 toolchain contract 混进 runtime public 面。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-29`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-98`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:12-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-44` | 就算 editor/tooling 很重，也应通过独立 owner 吸收，而不是继续扩大主交付 ABI。 |
| UnrealCSharp | `UnrealCSharp` runtime 公共边界只依赖 `CrossVersion + UnrealCSharpCore`；`UnrealCSharpEditor` 再通过 private deps 组合 `ScriptCodeGenerator`、`Compiler` 等 toolchain 模块，`UnrealCSharpCore` 负责底层 runtime/core contract。 | `Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-80` | 面向复用交付时，先建立可依赖的 core/runtime contract，再把 editor/toolchain 与仓内发现逻辑压回 private 或独立 owner。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前三个 checked-in owner 从“仓内源码共享边界”收敛成“可复用插件交付边界”：public 只保留稳定 API，internal layout 通过 façade/support owner 暴露。 |
| 具体步骤 | 1. 先给 `AngelscriptRuntime` 建立真正的 public header 白名单：新增或收敛到明确的 `Public/` 入口，逐步撤掉 `PublicIncludePaths.Add(ModuleDirectory)` 与 `PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Core"))` 这类整目录暴露；迁移期可用 forwarding headers 兼容旧 include。<br>2. 清理 `AngelscriptEditor` 的 public 树，把 `Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp` 这类实现文件移回 `Private/`；对确实需要给外部 editor 模块使用的能力，改成薄的 `EditorServices` / `EditorAPI` 头，不再直接 include `ClassGenerator/*`、`AngelscriptRuntimeModule.h`、`Core/AngelscriptEngine.h`。<br>3. 按 public header 实际需求重新裁剪 `AngelscriptEditor.Build.cs` 的 `PublicDependencyModuleNames`，把仅 private 实现使用的 `BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`AssetTools` 等尽量下沉；若某能力必须持续暴露，就优先提炼到更窄的 support owner。<br>4. 把 `AngelscriptTest` 视为非交付 owner：第一阶段先取消其公共 include/export 诉求，只保留 private harness；第二阶段再按既有路线迁出主插件或降为 opt-in test suite。<br>5. 增加一个 `binary consumer smoke test`：新建极小外部模块，仅依赖 `AngelscriptRuntime` 或 `AngelscriptEditor` 的 public contract，确保不需要仓内 internal header/目录才能编译。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`，以及新增的 façade/support headers 与 smoke test module |
| 预估工作量 | M-L |
| 架构风险 | 风险主要在于仓内现有 C++ 调用点可能深度依赖旧 include 路径和 internal header；因此应按“forwarding header + consumer smoke test”分阶段推进，避免一次性断掉编辑器扩展和测试。 |
| 兼容性 | 对脚本 API 可保持向后兼容。受影响的主要是 C++ 扩展方与仓内模块 include 路径；通过保留 forwarding headers、deprecated wrapper 和分阶段下沉 public deps，可以把 breakage 控制在可迁移范围内。 |
| 验证方式 | 1. 新增 `binary consumer smoke test` 后，仅依赖 `AngelscriptRuntime` / `AngelscriptEditor` public 面即可编译通过。<br>2. 移动 `Public/*.cpp` 与收窄 include roots 后，仓库全量编译仍通过。<br>3. 运行 editor 侧关键路径，确认 menu extension、BlueprintImpact、runtime 初始化行为保持一致。<br>4. 再次扫描 public 头与 public deps，确认不再直接暴露 `ClassGenerator/*`、`AngelscriptRuntimeModule.h`、整目录 include roots 这类内部 contract。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-MS-70 | 三个 checked-in owner 仍按仓内源码协作暴露边界，尚未收敛成可复用插件 ABI | 交付边界收口 + public contract 重构 | 高 |
| P1 | Arch-MS-69 | 主线并行编译已转向 `UHT file shard`，legacy `12+4` synthetic module 只剩冗余坐标系 | 架构口径收口 + legacy 路径降级 | 高 |

---

## 架构分析 (2026-04-10 00:50)

### Arch-MS-71：模块控制面仍依赖 concrete module class，缺少可反转的 thin module interface

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | runtime/editor 模块是否提供稳定、可依赖的模块级接口，而不是让下游直接耦合具体实现类或 runtime delegate 总线 |
| 当前设计 | `AngelscriptRuntime` 对外暴露的是 concrete `FAngelscriptRuntimeModule`，其 public 头同时承载 runtime 生命周期、调试 delegate、以及 editor create-blueprint/debug asset 这类跨层入口；相对地，`AngelscriptEditor` 的控制面只存在于 private `FAngelscriptEditorModule` 静态函数里，editor 行为再通过 runtime delegate 反向挂回 runtime 模块。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:25-49` 直接导出 `FAngelscriptRuntimeModule`，其中既有 `StartupModule()/ShutdownModule()`，也有 `GetDebugListAssets()`、`GetEditorCreateBlueprint()`、`GetEditorGetCreateBlueprintDefaultAssetPath()` 这类 editor 向 runtime 暴露的入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:138-166` 的 `InitializeAngelscript()` 在模块内部再次 `LoadModuleChecked("AngelscriptRuntime")`，说明控制面没有收敛成清晰的 consumer-facing `Get()/IsAvailable()` 接口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h:6-18` 只在 private 头里暴露 `ShowAssetListPopup()`、`ShowCreateBlueprintPopup()`、`GenerateNativeBinds()` 等静态入口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397-409` 通过 `FAngelscriptRuntimeModule::GetDebugListAssets()` / `GetEditorCreateBlueprint()` 把 editor 行为注册回 runtime delegate。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:5` 位于 `Public/` 树的实现文件还直接 include `AngelscriptRuntimeModule.h`，进一步暴露了 concrete module header。 |
| 优点 | 仓内协作时很直接：editor 可以不设计额外接口就把能力挂到 runtime；旧代码也不需要区分“模块实现类”和“模块服务接口”。 |
| 不足 | 模块控制面是 concrete class + ambient delegate 的混合体，扩展模块无法只依赖薄接口；editor 没有公开的 module/service contract，只能继续走 runtime delegate 或 private header；这会抬高后续把 `BlueprintImpact`、watcher、legacy generator、未来 feature pack 抽成 leaf owner 时的依赖反转成本。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime 控制面通过公开的 `IUnLuaModule` 抽象出来，consumer 统一走 `Get()`；具体 `FUnLuaModule` 留在 private 实现中。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaModule.h:18-32`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:43-48` | 先把“模块接口”和“模块实现类”分开，owner 才能稳定成为可依赖 contract。 |
| puerts | runtime 是 `IPuertsModule`，editor 也有独立的 `IPuertsEditorModule`；`PuertsEditor` 在 `StartupModule()` 中只依赖 `IPuertsModule::Get()`，而不是通过 runtime concrete module 反向塞 delegate。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17-55`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PuertsEditorModule.h:17-30`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76-79` | runtime/editor 控制面各自有 thin interface，依赖方向可以保持显式单向。 |
| UnrealCSharp | `UnrealCSharpCore` 至少提供了独立 public module entry `FUnrealCSharpCoreModule::Get()`，使下游不必从 editor/private helper 反推控制面；toolchain owner 也在 descriptor 层有稳定名字。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/UnrealCSharpCore.h:9-23`<br>`Reference/UnrealCSharp/UnrealCSharp.uplugin:29-53` | 即便不完全使用抽象接口，也应该先把模块入口收敛成稳定 public facade，而不是把控制面散落在 runtime concrete class 与 private editor 静态函数里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不打断现有脚本 API 的前提下，引入显式 module/service interface，把 concrete module class 退回实现层。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/IAngelscriptRuntimeModule.h`（名字可调整），提供 `Get()` / `IsAvailable()` 与少量稳定 runtime 能力；现有 `FAngelscriptRuntimeModule` 改为 `final` 实现类，定义留在 private 或至少不再作为推荐 consumer contract。<br>2. 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/IAngelscriptEditorServices.h` 或 `IAngelscriptEditorModule.h`，只暴露真正跨模块需要的 editor 行为，如 `ShowAssetListPopup`、`ShowCreateBlueprintPopup`、默认 Blueprint 路径查询。<br>3. 把 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397-409` 这类 runtime delegate 绑定改为过渡 wrapper：旧 `FAngelscriptRuntimeModule::GetEditorCreateBlueprint()` 继续可用，但内部转发到新 editor service，并标记 deprecated。<br>4. 清理 `Public/` 树里的 concrete module include；`ScriptEditorMenuExtension.cpp`、未来 leaf module 和 test support 优先转向接口头。迁移期保留旧 `AngelscriptRuntimeModule.h` 兼容 shim，避免一次性破坏仓内调用点。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`，以及新增的 `IAngelscriptRuntimeModule.h` / `IAngelscriptEditorServices.h` |
| 预估工作量 | M |
| 架构风险 | 主要风险在于仓内已有 C++ 调用点直接依赖 `FAngelscriptRuntimeModule` 的静态 accessor；若一次性删除旧入口，会把 editor/tooling/test 支持的编译错误混在一起。 |
| 兼容性 | 对脚本 API 可保持向后兼容。对 C++ 扩展方建议走“新接口 + 旧 header forwarding/deprecated wrapper”过渡；第一阶段不必改变现有模块名。 |
| 验证方式 | 1. 新增接口后，创建一个最小 editor leaf consumer，只 include `IAngelscriptRuntimeModule.h` / `IAngelscriptEditorServices.h` 即可编译，不再需要 concrete module header。<br>2. editor 启动后验证 `ShowAssetListPopup`、`CreateBlueprint`、debug asset list 仍正常工作。<br>3. 仓库级搜索 `#include "AngelscriptRuntimeModule.h"` 与 `#include "AngelscriptEditorModule.h"`，确认跨模块 consumer 数量下降且逐步收口到接口头。<br>4. 回归 `BlueprintImpact`、watcher、debug server 相关 editor 流程，确认新接口没有引入启动顺序回归。 |

### Arch-MS-72：`Source/AngelscriptUHTTool` 的 sidecar 命名与落位仍会误导模块清单

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `Source/` 目录中的 owner 身份是否足够清晰，能让人和工具区分 UE module、UBT/UHT sidecar 与其它生成工具 |
| 当前设计 | `Plugins/Angelscript/Source/` 顶层同时放着三个真实 UE 模块目录和一个名为 `AngelscriptUHTTool` 的 `.NET` sidecar 目录；但 `.uplugin` 只声明三个模块，而 exporter 又实际绑定到 `AngelscriptRuntime`。因此 `AngelscriptUHTTool` 在目录层看起来像“第四个模块”，本质上却是 build-time `ubtplugin`。这里关于“目录扫描会误判 owner 身份”的结论，是根据目录名、`.uplugin` 只声明三模块、以及 `.csproj` 产物路径共同推导出的。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 UE 模块。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj:1-15` 表明该目录实际是 `Microsoft.NET.Sdk` 工程，`AssemblyName` / `RootNamespace` 都是 `AngelscriptUHTTool`，输出路径是 `Binaries/DotNET/UnrealBuildTool/Plugins/AngelscriptUHTTool/`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:12-27` 通过 `[UnrealHeaderTool]` 和 `[UhtExporter(... ModuleName = "AngelscriptRuntime")]` 说明它服务的是 `AngelscriptRuntime` 的 UHT 生成，而不是一个独立 UE module owner。 |
| 优点 | 当前目录很直接，sidecar 源码离 runtime/editor 都近；对熟悉仓库的人来说，查找 UHT 导出逻辑比较方便。 |
| 不足 | 目录名没有编码“这是 `ubtplugin` sidecar 而不是 UE module”这一事实，容易让 inventory、架构审查、文档统计、甚至人工沟通把它和真正的模块 owner 混为一谈；这正是当前“模块数量口径容易漂移”的一个基础诱因。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 真正的 build-time owner 叫 `UnLuaDefaultParamCollector`，在 `.uplugin` 里显式声明为 `Program` 模块；对应的 C# sidecar 另用 `UnLuaDefaultParamCollectorUbtPlugin` 命名，名字上就与 UE module 分离。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-35`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollectorUbtPlugin/UnLuaDefaultParamCollectorUbtPlugin.ubtplugin.csproj:1-15` | sidecar 与真实 module owner 使用不同命名层级，目录扫描时更不容易误把 `.NET` 工程当模块。 |
| puerts | `.uplugin` 显式声明 `ParamDefaultValueMetas` 这个 `Program` 模块，真正的 C# sidecar 则叫 `CSharpParamDefaultValueMetas`；同一职责链上，module 名和 sidecar 名被明确区分。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:29-36`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-26`<br>`Reference/puerts/unreal/Puerts/Source/CSharpParamDefaultValueMetas/CSharpParamDefaultValueMetas.ubtplugin.csproj:1-20` | 如果 sidecar 必须与模块共存，至少要在名字上编码 `CSharp` / `UbtPlugin` 身份，避免 owner 命名重叠。 |
| UnrealCSharp | toolchain owner 直接在 `.uplugin` 里列成 `ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 等稳定名字，模块图先把产品与 toolchain owner 说清楚。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:29-53`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:5-30` | 即便内部仍有其它生成资产，descriptor 层先给出稳定 toolchain owner，外部就不必靠目录猜测。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 UHT sidecar 机制，但把“UE module owner”和“ubtplugin sidecar”在命名、目录和清单上彻底分开。 |
| 具体步骤 | 1. 第一阶段只做低风险收口：为 `Source/AngelscriptUHTTool/` 增加更明确的身份命名，优先方案是目录或工程名改成 `AngelscriptUHTToolUbtPlugin`，或移动到 `Source/UbtPlugins/AngelscriptUHTTool/` 这类不会伪装成 UE module 的位置。<br>2. 新增一份 checked-in inventory，例如 `Config/AngelscriptOwnerInventory.json`，显式列出 `ue_modules`、`ubt_sidecars`、`generated_shards` 三类 owner；让架构审查脚本、文档和诊断工具优先读这份清单，而不是按 `Source/*` 目录名猜。<br>3. 同步更新 `Documents/Guides/Build.md`、后续 module inventory 文档和诊断脚本，把 `AngelscriptUHTTool` 的默认口径改成 `toolchain sidecar`，不再与 `.uplugin` 模块数混算。<br>4. 如果未来真的要把 function-table 生成升级成一个可声明的 `Program` owner，再使用独立 UE module 名称，例如 `AngelscriptFunctionTableProgram`，不要直接复用当前 sidecar 名称。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`Plugins/Angelscript/Angelscript.uplugin`、`Documents/Guides/Build.md`，以及新增的 `Config/AngelscriptOwnerInventory.json` / 诊断脚本 |
| 预估工作量 | S-M |
| 架构风险 | 主要风险在于本地脚本、工程文件生成或文档引用可能硬编码了当前目录名；因此第一阶段应优先保证 `AssemblyName` / 输出路径兼容，先改 inventory 与目录语义，再考虑完全迁名。 |
| 兼容性 | 对脚本 API、runtime 行为和 UHT 生成结果应无影响；变化主要影响构建脚本、文档路径和内部工具口径。迁移期保留旧输出路径或兼容别名即可把风险压低。 |
| 验证方式 | 1. 调整命名/清单后，完整执行一次 UHT 导出，确认 `AS_FunctionTable_*` 继续稳定生成。<br>2. 让模块审计脚本或人工清单同时输出 `declared UE modules` 与 `ubt sidecars`，确认 `AngelscriptUHTTool` 不再被计入 UE module 数。<br>3. 回归 `Documents/Guides/Build.md` 中的构建步骤，确认新目录/清单不会破坏本地开发流程。<br>4. 对照参考仓库的 owner 口径，确认文档中不再把 sidecar 名误写为 product module。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-71 | 模块控制面仍依赖 concrete module class，缺少可反转的 thin interface | 控制面接口化 + 依赖反转 | 高 |
| P2 | Arch-MS-72 | `Source/AngelscriptUHTTool` 的 sidecar 命名与落位会误导模块清单 | owner 身份显式化 + inventory 收口 | 中 |

---

## 架构分析 (2026-04-10 00:59)

### Arch-MS-73：可选 editor integration 缺少 declarative soft-edge，`LoadModule("SourceCodeAccess")` 仍是隐藏依赖

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 模块对“可选集成”的依赖是否在声明层可见，还是继续散落在源码里的 ad-hoc `LoadModule(...)` |
| 当前设计 | `AngelscriptEditor.Build.cs` 只有 `PublicDependencyModuleNames` / `PrivateDependencyModuleNames` 两条硬边声明，没有 `DynamicallyLoadedModuleNames` 这条 soft-edge 车道；但源码仍在 source navigation 路径里直接 `LoadModule("SourceCodeAccess")`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 只声明 public/private 依赖，没有任何 `DynamicallyLoadedModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:3151-3153` 在注释掉 `ISourceCodeAccessModule` typed load 后，退回到 `IModuleInterface* Interface = FModuleManager::Get().LoadModule("SourceCodeAccess");`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:385` 同一个模块对 `Settings` 则使用 `GetModulePtr<ISettingsModule>("Settings")`，而 `Settings` 已在 `Build.cs:32` 被显式声明，说明当前只有部分 optional edge 被写回声明层。 |
| 优点 | raw `LoadModule("SourceCodeAccess")` 允许 editor 在没有 source accessor provider 的机器上继续启动，不会把 IDE 集成变成硬启动前提。 |
| 不足 | “可选但受支持”的依赖没有单独 contract，模块图无法区分 hard edge 与 soft edge；后续若继续接入 `SourceControl`、多 IDE 导航器或其它 developer-only service，只会积累更多源码里的隐式 `LoadModule(...)`，而不是进入可审计的依赖拓扑。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 把 `Kismet`、`MainFrame`、`AnimationBlueprintEditor` 明确放进 `DynamicallyLoadedModuleNames`，随后在 `OnPostEngineInit()` 中按需 `LoadModuleChecked<IMainFrameModule>("MainFrame")`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:87-93`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88-105` | optional editor integration 可以延后加载，但仍应先进入 `Build.cs` 的 declarative lane。 |
| puerts | `PuertsEditor` 对可选 `SourceControl` 不是在源码里裸 `LoadModule`，而是先用 `JsEnv.WithSourceControl` 条件把边写进 `Build.cs`，再通过宏定义暴露 feature state。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:40-43` | 即使能力是 feature-gated，也应优先让构建规则成为事实 authority。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `AngelscriptEditor` 增加显式 soft-edge 车道，让 optional integration 不再靠源码里的匿名 `LoadModule(...)` 存活。 |
| 具体步骤 | 1. 第一阶段先建立规则：凡是 `LoadModule(...)` / `LoadModuleChecked(...)` 指向、但并非始终必需的 editor integration，一律进入 `DynamicallyLoadedModuleNames` 或具名 feature-gated `PrivateDependencyModuleNames`。按当前证据，`SourceCodeAccess` 应先纳入这条清单。<br>2. 把 `SourceCodeAccess` 相关路径收口到一个窄接口，例如 `IAngelscriptSourceNavigationService` 或 `AngelscriptSourceNavigation` support owner；`AngelscriptEditor` 只依赖该接口或动态模块名，不再在大模块内部直接裸调 `LoadModule("SourceCodeAccess")`。<br>3. 在 `Tools/Diagnostics/` 增加一条简单检查：扫描 `LoadModule(` / `LoadModuleChecked(` 出现的模块名，要求它们必须能在 `PublicDependencyModuleNames`、`PrivateDependencyModuleNames` 或 `DynamicallyLoadedModuleNames` 三者之一中找到。<br>4. 迁移期保留现有导航行为和失败回退逻辑；只有在声明层与接口层稳定后，再考虑把更多 IDE / source-control 集成抽成 companion support owner。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 `IAngelscriptSourceNavigationService` / `AngelscriptSourceNavigation` 诊断脚本 |
| 预估工作量 | S-M |
| 架构风险 | 风险主要在于当前 source navigation 可能隐式兼容多个 accessor provider；因此第一阶段应先补 declarative edge 与 wrapper，不要立刻重写导航实现。 |
| 兼容性 | 对脚本 API 与现有 editor 用户行为应保持兼容；变化主要落在 `Build.cs` 与内部 source-navigation 接缝，对外部项目属于低兼容性风险。 |
| 验证方式 | 1. 补充 `DynamicallyLoadedModuleNames`/wrapper 后重新编译 `AngelscriptEditor`，确认功能不变。<br>2. 在缺少可用 source accessor provider 的环境中打开 editor，确认导航路径仍是可失败回退，而不是启动失败。<br>3. 运行新诊断脚本，确认 `SourceCodeAccess` 之类 raw load 已能被声明层正确追踪。 |

### Arch-MS-74：`AngelscriptEditor` 直接持有长期 listener/data-source 生命周期，editor shell 仍是 background workflow supernode

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 壳模块是否只负责编排，还是继续直接拥有目录监听、content browser data source、asset pre-save 这类长期后台生命周期 |
| 当前设计 | `FAngelscriptEditorModule::StartupModule()` 直接挂接 `OnPostEngineInit`、directory watcher、settings、runtime delegate bridge、`OnObjectPreSave` 与 tool menus；`OnEngineInitDone()` 又在模块自由函数里创建并激活 `UAngelscriptContentBrowserDataSource`。这些后台责任没有单独的 listener owner，也没有独立的 workflow 模块。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 说明 `ContentBrowserData`、`Settings`、`ToolMenus`、`LevelEditor` 等长期 editor workflow 依赖仍全部挂在同一 shell 上。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:361` 注册 `FCoreDelegates::OnPostEngineInit`，`:376` 注册 directory watcher，`:412` 注册 `FCoreUObjectDelegates::OnObjectPreSave`，`:415` 注册 tool menus startup callback。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:118` 在 `OnEngineInitDone()` 中直接 `ActivateDataSource("AngelscriptData")`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:680` 只在 shutdown 里手工移除 pre-save handle，说明这些后台生命周期仍靠 module class 本身零散管理。 |
| 优点 | 所有 editor 后台行为都从一个入口启动，调试时容易沿 `StartupModule()` 一路跟到实际注册点。 |
| 不足 | editor shell 继续承担“壳层 + 持久监听器 + 资产工作流 + data source”四种角色；后续无论想拆 `ContentBrowserData`、literal asset workflow，还是只想让 watcher/asset workflow 独立测试或按配置关闭，都要先穿过这个 supernode。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `UnrealCSharpEditor` 模块自身只持有 `FEditorListener` 与 `UDynamicDataSource` owner；后台生命周期集中在 `FEditorListener` 中注册和析构移除，编译监听又进一步抽到单独 `Compiler` 模块。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/UnrealCSharpEditor.h:49-55`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18-135`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-57` | 先把长期 listener 收口成 owner object，再把更重的后台 workflow 抽成 leaf module，能显著降低 editor shell 的职责密度。 |
| UnLua | `UnLuaEditorModule` 至少把 directory watcher 委托给 `UUnLuaEditorFunctionLibrary::WatchScriptDirectory()`，并在 module shutdown 中成组解除 `OnPostEngineInit` 与 package save hooks。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:54-84`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88-105` | 即便暂时不新建模块，也应先把后台行为收敛到更窄的 helper/listener owner，避免全部堆在 `StartupModule()`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把后台生命周期从 `FAngelscriptEditorModule` 本体里抽成 listener owner，再把 asset/data-source 工作流逐步迁到更窄的 support module。 |
| 具体步骤 | 1. 第一阶段在现有模块内新增 `FAngelscriptEditorLifecycle` / `FAngelscriptEditorListeners`（名字可调整），把 `OnPostEngineInit`、directory watcher、`OnObjectPreSave`、tool menus startup callback 的注册与注销集中到该 owner；`FAngelscriptEditorModule` 只持有它的实例。<br>2. 把 `OnEngineInitDone()` 中的 `UAngelscriptContentBrowserDataSource` 创建与激活、以及 literal asset pre-save 逻辑继续收敛到 `AngelscriptEditorAssetWorkflow` 或等价 support owner，避免自由函数和全局句柄继续扩散。<br>3. 如果第二阶段稳定，再把 `ContentBrowserData`、`PropertyEditor`、可能的 `AssetRegistry` 资产工作流边从 `AngelscriptEditor` 主 shell 下沉到新 leaf module；主 editor 模块只保留菜单、settings、runtime bridge 等壳层行为。<br>4. 为新 owner 增加最小生命周期测试或诊断：验证 editor shutdown 后 callback/handle 全部解除，避免长期 listener 重复注册或泄漏。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.*`，以及新增的 `FAngelscriptEditorLifecycle` / `AngelscriptEditorAssetWorkflow` owner 文件 |
| 预估工作量 | M |
| 架构风险 | 风险主要在于 editor 生命周期顺序较敏感，尤其是 `OnPostEngineInit`、content browser data source 与 pre-save hook 的执行时序；因此第一阶段应只改变 owner，不改变注册时机。 |
| 兼容性 | 对脚本 API、菜单入口和现有 editor 工作流应保持兼容；变化集中在内部 C++ owner 划分，对插件用户基本透明。 |
| 验证方式 | 1. 迁移后启动/关闭 editor，确认 directory watcher、literal asset 保存、content browser data source 仍正常工作。<br>2. 用 `rg` 或自动化检查确认相关 callback/handle 的注册与注销都集中到新 owner。<br>3. 反复重启 editor 或热重载 editor 模块，确认不会出现重复注册、未解除 delegate 或 rooted object 遗留。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-74 | editor shell 直接持有长期 listener/data-source 生命周期 | owner 收口 + workflow leaf 模块化 | 高 |
| P2 | Arch-MS-73 | 可选 editor integration 缺少 declarative soft-edge | 依赖声明补齐 + dynamic lane 引入 | 中 |

---

## 架构分析 (2026-04-10 01:10)

### Arch-MS-75：未声明的 generated bind module 让插件交付拓扑无法闭合到 installed/precompiled 场景

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前模块图是否已经形成可交付、可预编译的闭包，还是仍依赖运行期临时生成的模块 owner |
| 当前设计 | checked-in 的声明层依旧是无静态循环的三层 DAG：`.uplugin` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`；但真实执行拓扑还额外依赖 `GenerateNativeBinds()` 生成的 `ASRuntimeBind_*` / `ASEditorBind_*` 模块，以及 runtime 启动时从 `BindModules.Cache` 读出的模块名列表。换句话说，当前“能运行”的模块图并不是 descriptor-closed graph，而是 `declared modules + generated shards + cache` 的组合。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:13-16` 显示插件当前仍是 `EnabledByDefault=true`、`Installed=false`；`Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个 UE 模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1058` 运行时拼接 `ASRuntimeBind_*` / `ASEditorBind_*` 名称并生成新模块。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1166-1207` 在 `Source/<ModuleName>/` 下写出新的模块目录、header 与 `Build.cs`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1214-1282` 生成的 shard `Build.cs` 只有最小 `PublicDependencyModuleNames` / `PrivateDependencyModuleNames`，没有 `PrecompileForTargets`、安装包相关策略或其它交付元数据。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1488` 又在 runtime 初始化时从插件根目录读取 `BindModules.Cache` 并逐个 `LoadModule(...)`。 |
| 优点 | 本地源码工作区里，这种做法能把 bind 编译并行度做高，而且不需要人工维护完整 shard 清单。 |
| 不足 | 对 source checkout 来说这还能工作，但对 installed/precompiled plugin、二进制交付、或者“先打包插件再给宿主项目消费”的场景，模块闭包并不成立。这里关于“交付拓扑未闭合”的判断，是根据 `.uplugin` 只声明三模块、generated shard 运行期才落盘、而 runtime 又把这些未声明 owner 视作必需模块这三组源码事实推导出的。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把 `UnLua`、`UnLuaEditor`、`UnLuaDefaultParamCollector` 明确写进 `.uplugin`；测试插件 `UnLuaTestSuite` 也是单独的 `.uplugin + Build.cs` owner，并显式写出 `PrecompileForTargets = PrecompileTargetsType.Any`。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/UnLuaDefaultParamCollector.Build.cs:20-56`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:52-64` | 交付层 owner 必须静态可见；即使有 toolchain/test lane，也应该在 descriptor 与 `Build.cs` 里先闭合模块图。 |
| puerts | `.uplugin` 静态声明 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor` 六个 owner；生成/默认参数收集能力都在固定 editor/program 模块内完成，而不是让 runtime 从 cache 发现额外模块。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:90-152`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59`<br>`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/ParamDefaultValueMetas.Build.cs:13-45` | 代码生成可以很复杂，但“哪些模块构成交付物”仍应由静态 owner 决定，而不是由运行时 cache 决定。 |
| UnrealCSharp | 直接把 `UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion`、`SourceCodeGenerator` 全部声明成稳定模块；toolchain owner 很多，但都是预先可见的。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-58`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-47` | 只要目标是可复用插件，编译分片最好退回模块内部实现细节，而不是继续暴露成运行时必需但未声明的 module owner。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 generated bind 的并行编译收益，但把“交付 owner”固定下来，让 installed/precompiled plugin 只依赖静态声明模块。 |
| 具体步骤 | 1. 第一阶段建立固定交付 owner：新增或恢复 checked-in 的 `AngelscriptGeneratedBindsRuntime` / `AngelscriptGeneratedBindsEditor`（也可以沿用 `AngelscriptNativeBinds*`，但必须重新变成真实源码目录与稳定 `Build.cs`）。<br>2. 修改 `GenerateSourceFilesV2()` 与 `GenerateBuildFile()`：继续生成 `Bind_<Class>.cpp` 或 package 分组 `.cpp`，但输出落到固定 owner 的 `Private/Generated/`；停止再为每个 bucket 新建一个 UE 模块。<br>3. 为固定 owner 增加显式交付策略，例如必要时补 `PrecompileForTargets`、生成前置 manifest、插件打包前的 prebuild 步骤；让二进制交付链能在不运行 editor 菜单生成器的前提下得到完整模块闭包。<br>4. `BindModules.Cache` 在迁移期只保留兼容映射用途，例如把旧 `ASRuntimeBind_*` / `ASEditorBind_*` 名称解析到新的固定 owner；等 installed/precompiled 路径稳定后，再把 cache 降级为诊断产物。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增或恢复的 `Plugins/Angelscript/Source/AngelscriptGeneratedBindsRuntime/*` / `Plugins/Angelscript/Source/AngelscriptGeneratedBindsEditor/*`（或等价固定 owner） |
| 预估工作量 | M-L |
| 架构风险 | 主要风险在于 project file 生成、旧 cache、旧中间产物与新固定 owner 并存时会出现重复模块名或脏状态；第一轮切换需要强制 clean generated root 与 `Intermediate`。 |
| 兼容性 | 对脚本 API 与已生成绑定行为应保持向后兼容；对 C++ consumer 的影响主要在构建链。迁移期只要保留旧 shard 名到固定 owner 的兼容映射，就不必立即改外部调用点。 |
| 验证方式 | 1. clean checkout 下执行一次 bind 生成与 full rebuild，确认最终模块图只依赖 `.uplugin` 中声明的固定 owner。<br>2. 打包或预编译插件后，在新的宿主工程中仅启用该插件，确认无需再生成临时 shard 模块也能启动并完成绑定注册。<br>3. 对比迁移前后的 build log，确认并行编译收益仍在，同时 `BindModules.Cache` 不再决定交付 owner。 |

### Arch-MS-76：`AngelscriptEditor` 既是 workflow shell，又承担了 script-visible editor extensibility ABI owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 模块边界里，哪些类型属于“稳定扩展 ABI”，哪些属于“具体 workflow 实现”，当前是否已经分开 |
| 当前设计 | `AngelscriptEditor` 不只是 editor 壳层。它在 `Public/` 中直接导出了一整组面向脚本/Blueprint 扩展的 editor 基类与辅助类型，包括 `UScriptEditorSubsystem`、`UScriptEditorMenuExtension`、`UScriptActorMenuExtension`、`UScriptAssetMenuExtension`、`UEditorSubsystemLibrary`、`UBlueprintMixinLibrary` 与 `FScriptEditorPromptOptions`；同时这个模块本身又继续拥有菜单初始化、directory watcher、content browser、BlueprintImpact、source navigation 等具体 workflow。也就是说，用户扩展 ABI 与重 workflow 壳层被锁在了同一个 module owner 里。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7-58` 把 `UScriptEditorSubsystem` 定义成 `UCLASS(NotBlueprintable, Abstract)`，并公开 `BlueprintNativeEvent` / `BlueprintImplementableEvent` 扩展点。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:9-138` 公开 `BlueprintType` 的 `UScriptEditorMenuExtension` 与 menu-extension 枚举、注册接口。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.h:5-26`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h:5-25` 继续公开可继承的 actor/asset 菜单扩展基类。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:7-20` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/BlueprintMixinLibrary.h:6-18` 又把 editor 辅助 `UObject` 直接放进同一模块的 public ABI。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.h:6-36` 再导出 `FScriptEditorPromptOptions` 与 `FScriptEditorPrompts`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-368` 在 `StartupModule()` 中直接调用 `UScriptEditorMenuExtension::InitializeExtensions()`，并继续初始化 source navigation、directory watcher 等 workflow。 |
| 优点 | 对插件使用者来说，editor 扩展入口非常集中，找一个模块就能拿到 subsystem、菜单扩展和提示框 helper。 |
| 不足 | 这使 `AngelscriptEditor` 的模块边界变成 ABI 敏感区。这里关于 `"/Script/AngelscriptEditor"` 兼容压力的判断，是根据这些 `UCLASS/USTRUCT` 都由同一模块生成反射代码这一源码事实推导出的。结果是：一旦后续想把 watcher、BlueprintImpact、content browser 或 source navigation 拆出 leaf module，就要同时考虑脚本/Blueprint 可见类型、序列化路径、redirect 与外部 consumer 的模块依赖，而不是单纯地移动内部实现。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 的 public 面主要是 commandlet 与 IntelliSense 生成入口，例如 `UUnLuaIntelliSenseCommandlet`、`UnLua::IntelliSense`、`FUnLuaIntelliSenseGenerator`；而 `Build.cs` 把大多数 editor/workflow 依赖留在 private。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/Commandlets/UnLuaIntelliSenseCommandlet.h:17-32`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSense.h:19-52`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSenseGenerator.h:22-66`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-95` | 即使 editor public surface 存在，也优先把它收敛成工具入口，而不是让 abstract extensibility base class 与重 workflow 壳层共居一个 owner。 |
| puerts | `PuertsEditor` 的 public 类型主要是 concrete utility，例如 `UPEBlueprintAsset`、`UPEDirectoryWatcher`；而声明生成职责另有 `DeclarationGenerator` 模块承接。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintAsset.h:19-176`<br>`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEDirectoryWatcher.h:11-40`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-59` | concrete workflow utility 可以留在 editor 模块，但 generator/toolchain owner 应尽量分离，避免主 editor shell 同时承担“扩展 ABI + 重工具链”。 |
| UnrealCSharp | `UnrealCSharpEditor` 的 public 代表类型 `UDynamicDataSource` 是 concrete content-browser data source；而 `ScriptCodeGenerator`、`Compiler` 等 toolchain 仍是独立模块，并且 `UnrealCSharpEditor.Build.cs` 通过 private deps 编排它们。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Public/ContentBrowser/DynamicDataSource.h:12-148`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs:25-49`<br>`Reference/UnrealCSharp/Source/Compiler/Compiler.Build.cs:25-47` | editor owner 可以公开少量 concrete API，但稳定 extensibility ABI 与 generator/compiler 最好不要和重 workflow 壳层完全绑死。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把“稳定 editor extensibility ABI”与“具体 workflow 壳层”在模块层显式分开，再决定是否迁移既有 `UCLASS/USTRUCT`。 |
| 具体步骤 | 1. 第一阶段新增 `AngelscriptEditorExtensibility` 或 `AngelscriptEditorAPI` 模块，只承接未来新增的稳定扩展 contract 与服务接口，例如菜单注册服务、prompt service、subsystem extension façade；`AngelscriptEditor` 改为 private 依赖它。<br>2. 现有 `UScriptEditorMenuExtension`、`UScriptEditorSubsystem` 暂时保留在 `AngelscriptEditor` 以避免立刻改 `"/Script/AngelscriptEditor"` 路径，但其具体启动/注册实现应收回到 `Private/` 或新的 workflow leaf；例如把 `ScriptEditorMenuExtension.cpp`、`ScriptEditorPrompts.cpp` 从 public tree/重 workflow owner 中分离出去，保留薄 header facade。<br>3. 新规从本轮开始生效：新增的 editor 扩展基类、Blueprint/script-visible ABI 不再继续塞进当前重 workflow 模块，而是优先进入 `AngelscriptEditorExtensibility`；`AngelscriptEditor` 只放 concrete integration、watcher、asset workflow、BlueprintImpact、source navigation 等实现。<br>4. 第二阶段如果确实要迁移现有 `UCLASS/USTRUCT`，再单独准备 `ClassRedirects` / `StructRedirects` 与 forwarding headers，把 ABI 迁移和 workflow 模块拆分解耦，避免一次性大爆炸。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 `AngelscriptEditorExtensibility/*`（若进入第二阶段，还包括相应 redirect 配置） |
| 预估工作量 | M |
| 架构风险 | 最大风险是把“新 API 模块”做成另一个泛化 supernode，或过早移动现有 `UCLASS` 导致 asset/script path 兼容问题；因此第一阶段必须只切新 contract 与实现 owner，不立即移动旧反射类型。 |
| 兼容性 | 第一阶段对现有脚本/Blueprint/editor 扩展基本保持兼容，因为旧 `"/Script/AngelscriptEditor"` 类型不移动。只有第二阶段真正迁移旧 `UCLASS/USTRUCT` 时，才需要通过 redirect 与文档说明管理兼容性。 |
| 验证方式 | 1. 保持现有 editor 菜单扩展与 subsystem 流程回归通过，确认第一阶段没有改变行为。<br>2. 新建一个最小外部 editor 模块，只依赖新的 `AngelscriptEditorExtensibility` 即可编译其扩展入口，不再需要吞下 `AngelscriptEditor` 的全部 workflow fan-out。<br>3. 仓库级扫描新增 public 头，确认新的 editor extensibility ABI 不再继续写入 `AngelscriptEditor` 重 workflow 模块。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-MS-75 | generated bind module 让 installed/precompiled 交付拓扑不闭合 | 交付级结构收口 + 固定 owner 化 | 高 |
| P1 | Arch-MS-76 | `AngelscriptEditor` 同时承载 editor 扩展 ABI 与 workflow 壳层 | ABI lane 分离 + 渐进式模块拆分 | 中高 |

---

## 架构分析 (2026-04-10 01:21)

### Arch-MS-77：Debugger session / socket 场景被压进通用 `AngelscriptTest` owner，验证 DAG 无法区分普通回归与网络协议测试

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `AngelscriptTest` 是否已经把“普通 automation 回归”与“依赖 debug server/socket 的协议场景”拆成不同 owner |
| 当前设计 | 当前静态图仍是 `AngelscriptTest -> AngelscriptRuntime`，editor target 下再额外吸入 `AngelscriptEditor + Networking + Sockets + CQTest`；而 `TESTING_GUIDE` 已明确把 `Debugger/` 视为需要 socket/session fixture 的专门测试类别。也就是说，模块图只看到一个验证 owner，但 owner 内部实际上已经同时承载了通用回归和网络化 debugger 会话两条验证车道。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:29-32` 把 `AngelscriptTest` 固定为 `Editor` / `PostDefault` 模块。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:40-49` 在 editor 构建下把 `CQTest`、`Networking`、`Sockets`、`UnrealEd`、`AngelscriptEditor` 一并挂进同一个 test owner。<br>`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md:243-254` 又把 `Debugger session tests` 单独列为“attach to a running debug server and need socket/session fixtures”的专门类别，并配套 `AngelscriptDebuggerTestSession.h` / `AngelscriptDebuggerTestClient.h` / `AngelscriptDebuggerScriptFixture.h`。<br>`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp:18-33` 直接创建 `FAngelscriptDebuggerTestSession` 与 `FAngelscriptDebuggerTestClient`，连接 `127.0.0.1` 上的 debug server。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp:32-68` 使用 `ISocketSubsystem` 与 `FSocket` 建立真实 socket 连接。 |
| 优点 | debugger 协议回归可以直接复用现有 editor automation 进程，不需要额外启用第二个测试插件或单独维护调试专用 target。 |
| 不足 | 只要 `Debugger/` 子树继续留在同一个 owner，`Networking` / `Sockets` / `AngelscriptEditor` 这条较重的 fan-out 就会被整个 `AngelscriptTest` 背负；后续不论是把 `AngelscriptTest` 迁到独立 suite、做预编译、还是只想复用不依赖网络的测试夹具，都必须先处理这条 debugger/session 专用依赖链。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 测试能力被放进独立的 `UnLuaTestSuite` 插件，默认 `EnabledByDefault = false`；其模块依赖面也保持克制，`Build.cs` 公开侧只有 `Core`、`CoreUObject`、`Engine`、`Slate`，`UnLua` 与 `UnrealEd` 分别留在 private 和 editor 条件边。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64` | 专门的验证 lane 可以是 opt-in owner，不必让整个主验证模块长期承担所有 editor / tooling / transport 依赖。 |
| puerts | 主插件 `.uplugin` 只公开 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor` 这些产品与 toolchain owner，没有再把网络化验证模块塞进主拓扑。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 若某条验证或调试车道需要额外 host/transport 依赖，更适合作为独立 leaf owner，而不是继续压进泛化 test module。 |
| UnrealCSharp | `.uplugin` 同样只把 `UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion`、`SourceCodeGenerator` 作为稳定 owner；测试/验证并未进入主产品模块图。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:17-53` | 主模块图优先表达产品和 toolchain 责任，专门的验证 lane 再按需要外挂，能显著降低 supernode 化风险。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 debugger session / socket 场景从通用 `AngelscriptTest` 拆成更窄的验证 owner，让非网络化回归不再被动继承 transport 依赖。 |
| 具体步骤 | 1. 第一阶段新增 `AngelscriptDebuggerTests` 或 `AngelscriptDebuggerTestSuite`（名称可调整，建议 `Editor` host）模块，先迁移 `Source/AngelscriptTest/Debugger/*` 与 `Shared/AngelscriptDebugger*` 三类文件；把 `Networking`、`Sockets`、`CQTest` 这些依赖从 `AngelscriptTest.Build.cs` 收到新模块。<br>2. 现有 `AngelscriptTest` 只保留不需要 socket/debug session 的通用 automation、runtime fixture 与 shared harness；它若仍需 editor-only case，可仅 private 依赖 `AngelscriptEditor`，不再默认背 transport lane。<br>3. 如果后续要把整个测试体系迁到独立插件，优先把 `AngelscriptDebuggerTests` 作为第一个 companion verification plugin；这样迁移顺序可以是 `AngelscriptTest` 瘦身，再逐步外移，而不是一次性搬全部测试。<br>4. 迁移期保持 automation 名称、filter path 与 debug server 行为不变；旧目录下先放 forwarding include 或 wrapper，避免 CI 脚本和文档入口同时变化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/*`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.*`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.*`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.*`，以及新增的 debugger test owner 模块或插件文件 |
| 预估工作量 | M |
| 架构风险 | 主要风险在于现有 shared helper 与测试目录约定默认认为 debugger 夹具仍在 `AngelscriptTest` 模块内；因此第一阶段应只移动 owner，不改测试名和执行协议。 |
| 兼容性 | 对脚本用户无影响；对 C++ 自动化测试属于低到中等兼容风险，可通过 forwarding headers 和保持 automation 名称不变平滑过渡。 |
| 验证方式 | 1. 拆分后重新编译 editor target，确认 `AngelscriptTest` 不再直接依赖 `Networking` / `Sockets`。<br>2. 运行 `Angelscript.TestModule.Debugger.*` 现有用例，确认 handshake、breakpoint、callstack 等 socket 场景结果不变。<br>3. 运行一组不涉及 debugger 的普通 test group，确认它们在不加载 debugger test owner 时仍可通过。<br>4. 重新绘制 DAG，确认 transport fan-out 已从通用 test owner 收窄到专门 leaf module。 |

### Arch-MS-78：Debugger test support 直接暴露 `AngelscriptDebugServer` 协议类型，shared helper 没有形成独立 support boundary

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | debugger 测试辅助层是否已经被封装成窄接口，还是仍把 runtime debug protocol 直接暴露给整个 test owner |
| 当前设计 | `AngelscriptTest` 的 debugger helper 不是“黑盒 client/session”封装，而是直接 include runtime 的 `Debugging/AngelscriptDebugServer.h`，并把 `EDebugMessageType`、`FAngelscriptDebugMessageEnvelope`、`FAngelscriptBreakpoint`、`FAngelscriptClearBreakpoints`、`FAngelscriptDebugServer` 等协议/服务类型公开到 helper 签名里。再加上 `AngelscriptTest.Build.cs` 把模块根目录作为 public include root，导致这套 protocol-aware helper 成为整个 test owner 的 ambient contract，而不是只属于 debugger lane。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:13-21` 直接把 `ModuleDirectory` 和 `Debugger` 子目录纳入模块 include 面。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h:3-5` 直接 include `CoreMinimal.h` 与 `Debugging/AngelscriptDebugServer.h`；`:27-30`、`:38-45`、`:68-80` 又把 `EDebugMessageType`、`FAngelscriptDebugMessageEnvelope`、`FAngelscriptBreakpoint`、`FAngelscriptClearBreakpoints` 暴露到 public helper API。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h:8` 前置声明 `FAngelscriptDebugServer`，`:39-40` 与 `:52-55` 直接把 debug server 指针和访问器暴露在 session helper 上。<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h:125-145` 的 `TryGetRunningProductionDebuggerEngine()` 直接按 `Engine->DebugServer != nullptr` 搜索可调试 engine，说明 generic test utility 也已经知道 debugger runtime 细节。 |
| 优点 | debugger 测试写起来很直接，断点、变量、callstack 等协议消息可以在测试里直接序列化和断言，不需要额外翻译层。 |
| 不足 | 共享 helper surface 被 runtime debug protocol 污染后，通用 test harness 与 debugger protocol lane 难以独立演进；只要 debug server envelope 或 breakpoint payload 变更，整个 shared helper contract 都会跟着抖动。对模块结构而言，这意味着 `AngelscriptTest` 不是依赖“窄的 debugger test service”，而是依赖“runtime debug server internals”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaTestSuite` 的 public helper `UnLuaTestHelpers.h` 暴露的是测试夹具 `UObject` / `AActor` / `UBlueprintFunctionLibrary` 与 delegate/stub 类型；`Build.cs` 同时把真正的 `UnLua` runtime 放在 private 依赖。 | `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestHelpers.h:17-24`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestHelpers.h:67-115`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-55` | 测试 public/helper surface 更适合暴露 fixture 和断言载体，而不是直接泄漏产品 runtime 的协议或内部服务类型。 |
| puerts | `DeclarationGenerator` 需要重工具链依赖时，会把它们明确留在专门的 tool module 中，例如 `JsEnv`、`Puerts`、`AssetRegistry` 依赖都写在该模块自己的 `Build.cs`，而不是扩散成一个泛化 shared helper surface。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs:21-56` | 专门 lane 的重依赖应收口在专门 owner，自身再向外暴露窄的 contract，而不是把底层依赖直接变成共享 helper 签名。 |
| UnrealCSharp | `UnrealCSharpCore` 在需要额外环境发现信息时，选择生成 `UnrealCSharp_Modules.json` 这种机器可读中间层，而不是把扫描细节直接暴露成各处共享 API。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-212` | 如果某条 lane 需要携带额外协议/环境细节，更好的做法是先放进专门 support owner 或中间层，而不是继续污染通用共享接口。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先给 debugger test support 建立单独的 support boundary，把通用 test utilities 与 debug protocol 类型解耦。 |
| 具体步骤 | 1. 新增 `AngelscriptDebuggerTestSupport`（名称可调整，可作为新 `AngelscriptDebuggerTests` 的 private sibling 或独立 support module），迁移 `AngelscriptDebuggerTestClient.*`、`AngelscriptDebuggerTestSession.*`、`AngelscriptDebuggerScriptFixture.*`；这些文件不再从通用 `Shared/` 根暴露。<br>2. 为 debugger lane 提供更窄的 façade，例如 `IAngelscriptDebuggerTestClient` / `FAngelscriptDebuggerScenarioHarness`，把 envelope serialization、breakpoint payload、debug server version 等协议细节尽量留在 support owner 内部。<br>3. 把 `AngelscriptTestUtilities.h` 中依赖 `Engine->DebugServer` 的 helper 移到 debugger support owner；通用 utility 只保留 engine lifecycle、脚本编译和普通 fixture 辅助，不再默认知道 debug server。<br>4. 迁移期保留旧 `Shared/AngelscriptDebugger*.h` 作为 forwarding headers，内部转到新 support owner；待仓库内 include 收敛后，再撤掉对 `Debugging/AngelscriptDebugServer.h` 的直接共享暴露。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.*`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`，以及新增的 debugger support owner 文件 |
| 预估工作量 | M |
| 架构风险 | 如果 façade 设计得过窄，可能在第一轮迁移时挡不住现有 debugger case 对 raw protocol message 的断言需求；因此第一阶段应允许 support owner 内部继续保留 raw envelope API，只是先把暴露面从通用 shared root 收回。 |
| 兼容性 | 对脚本用户零影响；对仓内 C++ 测试代码有可控迁移成本，可通过 forwarding headers 和保持 automation 名称不变实现向后兼容。 |
| 验证方式 | 1. 迁移后用仓库级搜索确认 `Shared/` 下不再直接 include `Debugging/AngelscriptDebugServer.h`。<br>2. 重新编译 debugger tests，确认 breakpoint / callstack / break-filters 用例仍能通过。<br>3. 检查非 debugger test 源文件，确认它们不再因为 include `AngelscriptTestUtilities.h` 而被动看到 debug protocol 类型。<br>4. 若新增 support module，再次检查依赖图，确认 runtime debug protocol 只经由 debugger support owner 暴露。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-77 | debugger session/socket 场景压在通用 `AngelscriptTest` owner | 验证 lane 拆分 + 依赖收口 | 高 |
| P2 | Arch-MS-78 | debugger test helper 直接暴露 runtime debug protocol | support boundary 收口 + façade 化 | 中高 |

---

## 架构分析 (2026-04-10 01:33)

### Arch-MS-79：`AngelscriptEditor` 的 public contract 与 `Build.cs` 声明失配，真实 public edge 同时被低报与高报

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | editor 模块的 `PublicDependencyModuleNames` 是否真的对应 public header contract |
| 当前设计 | `AngelscriptEditor.Build.cs` 目前把 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools` 都声明成 public edge，但实际 public header 直接暴露出来的依赖却是另一组：`AssetRegistry`、`ToolMenus`、`EditorSubsystem` 与 runtime 内部 `Core/AngelscriptEngine.h`。同时，`Public/EditorMenuExtensions/` 目录下还混放了实现 `.cpp`，把 `LevelEditor`、`ContentBrowser`、`ToolMenus`、runtime internal include 一并带进所谓 public tree。结果是声明层一边高报了若干实现态依赖，一边又低报了真正进入 public header 的依赖。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-26` 把 `UnrealEd`、`EditorSubsystem`、`AngelscriptRuntime`、`BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`Slate`、`SlateCore`、`AssetTools` 放进 `PublicDependencyModuleNames`；`:28-40` 又把 `ToolMenus`、`ContentBrowser`、`ContentBrowserData`、`LevelEditor` 只留在 private。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:3-10` public header 直接 include `AssetRegistry/AssetData.h`、`AssetRegistry/IAssetRegistry.h` 与 `Core/AngelscriptEngine.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:3-6` public header 直接 include `AssetRegistry/AssetData.h`、`ToolMenuDelegates.h`、`ToolMenuSection.h`。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:2-8` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/EditorSubsystemLibrary.h:3-19` 说明 `EditorSubsystem` 确实属于 public contract。<br>`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:16-21` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:1-13` 则显示 `LevelEditor`、`ContentBrowser`、`ToolMenus`、`AngelscriptBinds.h`、`AngelscriptEngine.h` 等实现依赖直接位于 public tree。 |
| 优点 | 当前 owner 对脚本化 editor 扩展很集中，仓内开发者不必额外寻找 API 层和 workflow 层的分界。 |
| 不足 | 对外部 consumer 而言，`Build.cs` 的 public edge 既不完整也不精确：真正会进入 public header 的 `AssetRegistry` / `ToolMenus` 没有被正确声明，而 `BlueprintGraph` / `Kismet` / `DirectoryWatcher` / `AssetTools` 又被提前公开成 editor ABI 的一部分。这会直接抬高后续拆 `BlueprintImpact`、菜单扩展或 companion editor module 时的误判概率。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaEditor` 基本不建立 public dependency 面；`BlueprintGraph`、`DirectoryWatcher`、`ToolMenus`、`UnLua`、`Lua` 都留在 private / dynamic lane，public tree 不承担 workflow implementation。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:30-45`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:47-57`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-94` | editor shell 可以很重，但依赖声明应先按“外部 ABI 需要什么”收口，而不是把实现里顺手用到的 editor 模块全部公开。 |
| UnrealCSharp | `UnrealCSharpEditor` 只把 `Core`、`UnrealEd`、`DirectoryWatcher`、`CollectionManager` 留在 public deps；`ToolMenus`、`ToolWidgets`、`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion` 全部留在 private orchestration。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25-33`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:37-63` | 即使 editor 需要组合大量工作流 owner，也应先把 public edge 限定为少量稳定 contract，再让其余依赖停留在 private 实现层。 |
| puerts | `Puerts.uplugin` 直接把 `DeclarationGenerator` 与 `PuertsEditor` 分成独立 owner，避免由单一 editor shell 同时对外承诺声明生成、内容工作流与 runtime bridge。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:28-48` | 如果某块 public ABI 很难继续收窄，就优先把对应 workflow owner 单独拆出，而不是让主 editor 模块继续背模糊的 public edge。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `AngelscriptEditor` 的 public contract 做成“声明层可验证”的最小集合，再把 workflow 实现完全收回 private tree。 |
| 具体步骤 | 1. 第一阶段先移动文件所有权：把 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/*.cpp` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp` 移回 `Private/`，让 public tree 只保留 header。<br>2. 重新按 header 需求审计 `AngelscriptEditor.Build.cs`：若 `ScriptEditorMenuExtension.h` 与 `AngelscriptBlueprintImpactScanner.h` 仍直接暴露 `AssetRegistry` / `ToolMenus` 类型，则先把这两条边补成 public；与此同时把没有进入 public header 的 `BlueprintGraph`、`Kismet`、`DirectoryWatcher`、`AssetTools` 先下沉到 private。<br>3. 第二阶段再收口 header：对 `ToolMenuDelegates.h`、`ToolMenuSection.h`、`AssetRegistry` 这类当前直接进入 public header 的类型，优先用 façade / forwardable wrapper 收窄，避免把 `ToolMenus` 和 `AssetRegistry` 长期固定为 editor ABI。<br>4. 为外部 C++ consumer 增加一个最小 smoke module，只依赖 `AngelscriptEditor` 并逐个 include public header；这条编译验证应成为后续 editor 模块拆分的守门条件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h`，以及新增的 public-contract smoke module |
| 预估工作量 | M |
| 架构风险 | 最大风险是第一阶段把 `AssetRegistry` / `ToolMenus` 简单补成 public 后，反而把不该长期公开的 editor edge 固化下来；因此第二阶段必须紧跟 façade 收口，而不是把“先修声明”误当终态。 |
| 兼容性 | 对脚本/Blueprint 用户基本向后兼容，因为第一阶段不改类型名与反射路径。对 C++ consumer 的影响主要是 include 路径和依赖声明会更明确，若存在依赖当前 accidental transitive edge 的外部模块，修正后需要补自己的 `Build.cs`。 |
| 验证方式 | 1. 新增 smoke consumer 后，只依赖 `AngelscriptEditor` 编译 public header，确认 `AssetRegistry` / `ToolMenus` 等 public 需求被声明层完整表达。<br>2. 重新编译 editor target，确认把 `.cpp` 移回 `Private/` 不改变现有菜单扩展、BlueprintImpact、prompt 行为。<br>3. 仓库级扫描 `Public/`，确认不再残留实现 `.cpp`。<br>4. 复查 `AngelscriptEditor.Build.cs`，确认 `PublicDependencyModuleNames` 与 public header 需求一一对应，不再明显高报。 |

### Arch-MS-80：`AngelscriptTest` 作为 automation owner 没有明确 public API，却继续向外导出 runtime / json / gameplay tag 依赖

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 测试模块是否真的需要 public contract，还是应保持 private harness owner |
| 当前设计 | `AngelscriptTest` 当前在 `Build.cs` 里把整个 `ModuleDirectory` 暴露成 public include root，并把 `GameplayTags`、`Json`、`JsonUtilities`、`AngelscriptRuntime` 作为 public dependencies 导出；但模块入口本身只负责启动/关闭日志，真正使用这些依赖的是模块内部 automation case 与 shared helper。换句话说，这个 owner 的实际角色是“加载后自动发现测试”，不是“被其它模块链接的测试库”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:12-32` 把 `ModuleDirectory` 暴露为 public include root，并把 `GameplayTags`、`Json`、`JsonUtilities`、`AngelscriptRuntime` 放进 `PublicDependencyModuleNames`。<br>`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp:9-16` 显示模块生命周期只有日志输出，没有对外服务注册或 shared API 装配。<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp:1-4`、`:25-32` 说明 `GameplayTags` 的直接使用发生在模块内部测试用例。<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:1-15`、`:49-57` 说明 `Json` / `JsonUtilities` 相关类型同样只被内部测试读取 UHT summary 与 generated output。 |
| 优点 | 现状对仓内测试编写最省事，任何测试文件都能直接 include shared helper 和 runtime 头，不需要额外区分“可复用测试支持面”和“仅当前模块内部可用”。 |
| 不足 | 这会把验证 owner 伪装成一个可复用库：模块图会错误暗示 `AngelscriptTest` 公开承诺了 runtime/json/gameplay tag surface，而实际上这些边只是内部用例在消费。后续若要把测试迁到 companion plugin、做预编译，或只保留窄的 shared fixture，就必须先清理这层 accidental export。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLuaTestSuite` 被做成默认关闭的独立插件；如果要公开测试夹具，会明确放在 `Public/UnLuaTestHelpers.h`，而不是把整个模块根目录当成 public include root。 | `Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestHelpers.h:17-24`<br>`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestHelpers.h:67-115` | 如果测试模块确实要有 public surface，应显式命名那组 helper/fixture；否则就让测试 owner 维持 private-only。 |
| puerts | 主插件模块图只公开 runtime / editor / toolchain owner，没有把测试模块当产品 ABI 的一部分。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` | 验证层不需要先占据主拓扑中的 public contract；能保持 private harness 就先别做成公共边。 |
| UnrealCSharp | 同样只把 `UnrealCSharp`、`UnrealCSharpCore`、`CrossVersion`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator` 这些稳定 owner 放进主模块图，没有把测试 harness 当成外部消费者应该依赖的模块。 | `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53` | 对可复用插件而言，验证 owner 最好默认是内部或 companion lane，而不是和产品模块一样对外承诺 ABI。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 `AngelscriptTest` 从“公共模块”收口为“private harness owner”；只有确实需要复用的夹具才单独显式导出。 |
| 具体步骤 | 1. 第一阶段直接做低风险收口：把 `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` 中的 `AngelscriptRuntime`、`GameplayTags`、`Json`、`JsonUtilities` 从 `PublicDependencyModuleNames` 下沉到 `PrivateDependencyModuleNames`，并移除 `PublicIncludePaths.Add(ModuleDirectory)`。<br>2. 如果迁移时发现确有 in-tree consumer 需要共享测试 helper，不要恢复整模块 public export，而是新增窄的 `Public/TestHarness/` 或单独 `AngelscriptTestSupport` owner，仅放少量稳定 fixture/header。<br>3. 第二阶段再配合既有路线，把 `AngelscriptTest` 迁到独立 `AngelscriptTestSuite` companion plugin；第一阶段先完成 public/private 收口，可显著降低迁移面。<br>4. 为后续维护加一条 guardrail：非 `AngelscriptTest` 模块禁止 include `AngelscriptTest/Shared/*` 或 `AngelscriptTest/Core/*`，只有显式导出的 harness surface 可以跨模块引用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`，以及可选新增的 `Plugins/Angelscript/Source/AngelscriptTest/Public/TestHarness/*` 或 `Plugins/Angelscript/Source/AngelscriptTestSupport/*` |
| 预估工作量 | S-M |
| 架构风险 | 风险主要在于仓内可能存在少量未被发现的测试辅助复用点；如果直接收掉 public include root，个别 include 可能在第一轮编译时暴露出来。但这类风险属于“应尽早显性化的 accidental coupling”。 |
| 兼容性 | 对脚本用户和 automation 名称完全兼容。对仓内 C++ 代码的兼容影响低，只要在第一轮编译后把确实需要共享的 helper 收到显式 harness surface 即可。 |
| 验证方式 | 1. 收口后重新编译 editor target，确认 `AngelscriptTest` 自身测试源码仍可通过。<br>2. 仓库级搜索非 `AngelscriptTest` 目录，确认没有继续依赖 `AngelscriptTest` 的内部头。<br>3. 若新增 `TestHarness` / `TestSupport`，用最小 consumer 只 include 那组显式导出的 header，验证不需要重新暴露整模块根目录。<br>4. 为后续独立 test plugin 试点重新绘制 DAG，确认 `AngelscriptTest` 已不再向外导出无关 runtime/json/gameplay tag edge。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-MS-79 | `AngelscriptEditor` public contract 与 `Build.cs` 声明失配 | public edge 收口 + header/impl 分离 | 高 |
| P2 | Arch-MS-80 | `AngelscriptTest` 在缺少明确 public API 时仍导出依赖 | 验证 owner 私有化 + harness 显式化 | 中高 |
