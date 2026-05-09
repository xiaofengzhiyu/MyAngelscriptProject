# Reference

## 目的

- 本目录用于集中维护当前项目依赖的外部参考仓库说明。
- 这些仓库不属于当前项目提交内容，只用于对照、迁移分析、架构参考和实现取舍判断。
- `Agents_ZH.md` 只保留索引级信息；具体说明、用途边界、优先级判断统一维护在本文件。

## 外部参考仓库总表

| 名称 | 入口与说明 |
| --- | --- |
| AngelScript v2.38.0 | 使用 `Tools\PullReference\PullReference.bat angelscript` 默认拉取到当前项目的 `Reference\angelscript-v2.38.0`；GitHub `https://github.com/anjo76/angelscript.git`；SSH `git@github.com:anjo76/angelscript.git`；用于对照 AngelScript 语言本体与官方测试 |
| Hazelight Angelscript | 读取 `AgentConfig.ini` 中 `References.HazelightAngelscriptEngineRoot`；本地配置来源；当前未记录到可直接拉取的 GitHub 地址；用于参考 Hazelight 的 Angelscript 集成、模块拆分、绑定、测试组织以及引擎侧改造 |
| Hazelight Docs | 使用 `Tools\PullReference\PullReference.bat hazelightdocs` 默认拉取到当前项目的 `Reference\Docs-UnrealEngine-Angelscript`；GitHub `https://github.com/Hazelight/Docs-UnrealEngine-Angelscript.git`；SSH `git@github.com:Hazelight/Docs-UnrealEngine-Angelscript.git`；用于参考 Hazelight 公开文档站源码、内容结构和对外能力说明 |
| Hazelight VS Code Angelscript | 使用 `Tools\PullReference\PullReference.bat hazelightvscode` 默认拉取到当前项目的 `Reference\vscode-unreal-angelscript`；GitHub `https://github.com/Hazelight/vscode-unreal-angelscript.git`；SSH `git@github.com:Hazelight/vscode-unreal-angelscript.git`；Marketplace ID `Hazelight.unreal-angelscript`；用于参考 Hazelight 的 VS Code Language Server、Debug Adapter、错误展示、断点调试与编辑器连接工作流 |
| Aura GAS Course Initial Project | 使用 `Tools\PullReference\PullReference.bat aura` 默认拉取到当前项目的 `Reference\GameplayAbilitySystem_Aura_Initial`；GitHub `https://github.com/DruidMech/GameplayAbilitySystem_Aura.git`；SSH `git@github.com:DruidMech/GameplayAbilitySystem_Aura.git`；固定到初始提交 `f778ff39e873a756d5a3f97f263d6f24662fdde9`；用于参考 Aura GAS 课程起始练习资产、UE 5.2 示例内容工程结构和 Gameplay Ability System 练习素材 |
| Aura GAS Course C++ Project | 使用 `Tools\PullReference\PullReference.bat auracpp` 默认拉取到当前项目的 `Reference\GameplayAbilitySystem_Aura_Cpp`；GitHub `https://github.com/DruidMech/GameplayAbilitySystem_Aura.git`；SSH `git@github.com:DruidMech/GameplayAbilitySystem_Aura.git`；跟随 `main` 分支；用于参考实现完成后的 Aura C++ GAS 项目、UE 5.3 工程结构、GameplayAbilities/MVVM/MotionWarping 接入和课程实现演进 |
| Aura GAS Angelscript Rewrite | 使用 `Tools\PullReference\PullReference.bat auraas` 默认拉取到当前项目的 `Reference\AngelscriptAura`；GitHub `https://github.com/najoast/AngelscriptAura.git`；SSH `git@github.com:najoast/AngelscriptAura.git`；跟随 `main` 分支；用于参考第三方 Aura GAS Angelscript 改写、AS 侧 GAS 脚本组织和实现笔记 |
| UnrealCSharp | 使用 `Tools\PullReference\PullReference.bat unrealcsharp` 默认拉取到当前项目的 `Reference\UnrealCSharp`；GitHub `https://github.com/crazytuzi/UnrealCSharp.git`；SSH `git@github.com:crazytuzi/UnrealCSharp.git`；用于横向参考 Unreal 脚本插件工程架构 |
| Tencent UnLua | 使用 `Tools\PullReference\PullReference.bat unlua` 默认拉取到当前项目的 `Reference\UnLua`；GitHub `https://github.com/Tencent/UnLua.git`；SSH `git@github.com:Tencent/UnLua.git`；用于参考 Lua 脚本方案的 UE 反射接入、事件覆写、调试和教程组织 |
| Tencent puerts | 使用 `Tools\PullReference\PullReference.bat puerts` 默认拉取到当前项目的 `Reference\puerts`；GitHub `https://github.com/Tencent/puerts.git`；SSH `git@github.com:Tencent/puerts.git`；用于参考 TypeScript/JavaScript 脚本运行时、声明生成和多后端工程组织 |
| Tencent sluaunreal | 使用 `Tools\PullReference\PullReference.bat sluaunreal` 默认拉取到当前项目的 `Reference\sluaunreal`；GitHub `https://github.com/Tencent/sluaunreal.git`；SSH `git@github.com:Tencent/sluaunreal.git`；用于参考另一套成熟 Lua 方案的静态导出、性能取舍和热更新工作流 |
| Blender MCP | 使用 `Tools\PullReference\PullReference.bat blendermcp` 默认拉取到当前项目的 `Reference\blender_mcp`；Blender Forge `https://projects.blender.org/lab/blender_mcp.git`；无 SSH 地址；用于参考 Blender 官方 MCP Server 实现、MCP 协议接入方式和工具链集成模式 |

## 参考源说明

### 1. AngelScript v2.38.0

- 默认路径：当前项目的 `Reference\angelscript-v2.38.0`
- GitHub：`https://github.com/anjo76/angelscript.git`
- SSH：`git@github.com:anjo76/angelscript.git`
- 拉取命令：`Tools\PullReference\PullReference.bat angelscript`
- 重点目录：
- `Reference\angelscript-v2.38.0\sdk\angelscript\source\`
- `Reference\angelscript-v2.38.0\sdk\add_on\`
- `Reference\angelscript-v2.38.0\sdk\tests\`
- 主要用于确认 AngelScript 原生运行时、编译器、语法行为、调用约定、标准附加组件和官方测试基线。
- 涉及引擎核心源码文件时，应优先以这个上游版本为准，避免把 Unreal 集成差异误判成 AngelScript 原生行为。

### 2. Hazelight Angelscript

- 路径来源：读取 `AgentConfig.ini` 中的 `References.HazelightAngelscriptEngineRoot`
- GitHub：当前未记录到可直接使用的远程地址
- SSH：当前未记录到可直接使用的远程地址
- 拉取命令：当前不支持通过 `Tools\PullReference\PullReference.bat` 自动拉取
- 该参考源统一承载原先拆开的两类信息：Hazelight Unreal 插件集成方式，以及 Hazelight 引擎侧的 Angelscript 改造与底层支撑。
- 主要用于确认 Unreal 集成方式，包括插件结构、模块拆分、UE 类型绑定、编辑器扩展、测试组织方式以及脚本资产工作流。
- 同时也用于比对引擎级补丁、引擎内扩展点、底层绑定支撑和插件与引擎协同方式。
- 当前仓库里的 `Plugins/Angelscript` 本质上是朝"插件化、可维护"的方向整理这个参考源，因此后续迁移、对齐、补能力时都优先参考这个本地配置路径。
- 该参考源由本机配置显式指定，不走当前项目内置的 GitHub 同步脚本流程。

### 3. Hazelight Docs

- 默认路径：当前项目的 `Reference\Docs-UnrealEngine-Angelscript`
- GitHub：`https://github.com/Hazelight/Docs-UnrealEngine-Angelscript.git`
- SSH：`git@github.com:Hazelight/Docs-UnrealEngine-Angelscript.git`
- 拉取命令：`Tools\PullReference\PullReference.bat hazelightdocs`
- 重点目录：
- `Reference\Docs-UnrealEngine-Angelscript\content\`
- `Reference\Docs-UnrealEngine-Angelscript\templates\`
- `Reference\Docs-UnrealEngine-Angelscript\static\`
- 该参考源用于查看 Hazelight 对外公开的能力说明、示例叙述、文档编排方式以及站点内容结构。
- 它不是 Hazelight 引擎/插件源码参考源，不能替代 `HazelightAngelscriptEngineRoot` 指向的本地源码路径；源码对照与能力 parity 仍应优先看 `Hazelight Angelscript`。

### 4. Hazelight VS Code Angelscript

- 默认路径：当前项目的 `Reference\vscode-unreal-angelscript`
- GitHub：`https://github.com/Hazelight/vscode-unreal-angelscript.git`
- SSH：`git@github.com:Hazelight/vscode-unreal-angelscript.git`
- Marketplace ID：`Hazelight.unreal-angelscript`
- 拉取命令：`Tools\PullReference\PullReference.bat hazelightvscode`
- 重点目录：
- `Reference\vscode-unreal-angelscript\client\`
- `Reference\vscode-unreal-angelscript\server\`
- `Reference\vscode-unreal-angelscript\syntaxes\`
- 该参考源用于查看 Hazelight VS Code 扩展如何组织 Angelscript Language Server、Debug Adapter、断点调试、错误展示、语义高亮、Go To Definition、命令入口与编辑器连接。
- 它是编辑器外部工具链参考源，不是 UE 插件源码参考源；调试协议、VS Code 工作流、workspace 约定和用户侧错误展示可优先参考它，Runtime / Editor 插件内部实现仍应优先参考 `Hazelight Angelscript`。

### 5. Aura GAS Course Initial Project

- 默认路径：当前项目的 `Reference\GameplayAbilitySystem_Aura_Initial`
- GitHub：`https://github.com/DruidMech/GameplayAbilitySystem_Aura.git`
- SSH：`git@github.com:DruidMech/GameplayAbilitySystem_Aura.git`
- 拉取命令：`Tools\PullReference\PullReference.bat aura`
- 固定提交：`f778ff39e873a756d5a3f97f263d6f24662fdde9`（`Initial Project Files`，2023-03-23）
- 工程版本：初始提交的 `Aura.uproject` 关联 `EngineAssociation` 为 `5.2`；仓库 `main` 后续升级到 UE 5.3，不要把两者混作同一基线。
- 重点目录：
- `Reference\GameplayAbilitySystem_Aura_Initial\Content\Assets\`
- `Reference\GameplayAbilitySystem_Aura_Initial\Content\Maps\`
- `Reference\GameplayAbilitySystem_Aura_Initial\Config\`
- 该参考源用于查看 Stephen Ulibarri Aura GAS 课程起始练习资产、Top Down RPG 示例项目素材组织、角色/地牢/UI 资源布局和初始内容工程结构。
- 它不是 Angelscript 插件源码参考源，也不是 Hazelight Angelscript 能力 parity 基准；只在需要练习素材、GAS 示例资产组织或 UE 示例项目对照时使用。
- 如果需要课程完成态或后续实现演进，应显式切换到上游 `main` 或另建单独本地副本，避免污染起始练习资产基线。

### 6. Aura GAS Course C++ Project

- 默认路径：当前项目的 `Reference\GameplayAbilitySystem_Aura_Cpp`
- GitHub：`https://github.com/DruidMech/GameplayAbilitySystem_Aura.git`
- SSH：`git@github.com:DruidMech/GameplayAbilitySystem_Aura.git`
- 拉取命令：`Tools\PullReference\PullReference.bat auracpp`
- 分支：`main`
- 工程版本：当前 `main` 的 `Aura.uproject` 关联 `EngineAssociation` 为 `5.3`，包含 `Aura` C++ Runtime 模块，并启用 `GameplayAbilities`、`MotionWarping`、`ModelViewViewModel` 等插件。
- 重点目录：
- `Reference\GameplayAbilitySystem_Aura_Cpp\Source\Aura\`
- `Reference\GameplayAbilitySystem_Aura_Cpp\Data\`
- `Reference\GameplayAbilitySystem_Aura_Cpp\Content\`
- `Reference\GameplayAbilitySystem_Aura_Cpp\Plugins\`
- 该参考源用于查看课程完成态或接近完成态的 C++ GAS 项目实现，包括 Ability System Component、Attribute Set、Gameplay Effect / Ability 数据组织、UI/MVVM 接入、敌人/玩家控制流和课程资产最终落地方式。
- 它可以作为本项目 GAS Angelscript 示例和绑定体验的横向参考，但不是 Angelscript 插件架构基准；迁移设计时仍需回到当前插件边界和 Hazelight Angelscript 参考源做判断。
- 如果需要稳定复盘某一课的状态，应额外记录具体提交，而不是把 `auracpp` 的 `main` 工作副本固定成历史点。

### 7. Aura GAS Angelscript Rewrite

- 默认路径：当前项目的 `Reference\AngelscriptAura`
- GitHub：`https://github.com/najoast/AngelscriptAura.git`
- SSH：`git@github.com:najoast/AngelscriptAura.git`
- 拉取命令：`Tools\PullReference\PullReference.bat auraas`
- 分支：`main`
- 重点目录：
- `Reference\AngelscriptAura\Script\`
- `Reference\AngelscriptAura\Script\GAS\`
- `Reference\AngelscriptAura\Script\Documents\`
- `Reference\AngelscriptAura\Content\`
- 该参考源用于查看第三方 Aura GAS Angelscript 改写中的脚本分层、GAS API 调用方式、属性集、伤害计算、AI、角色和 UI 组织方式。
- 它不是官方 Aura 课程完成态，也不是 Hazelight Angelscript 插件能力基准；执行移植时应把它作为 AS 写法参考，再回查 `GameplayAbilitySystem_Aura_Cpp` 和当前插件绑定能力。

### 8. UnrealCSharp

- 默认路径：当前项目的 `Reference\UnrealCSharp`
- GitHub：`https://github.com/crazytuzi/UnrealCSharp.git`
- SSH：`git@github.com:crazytuzi/UnrealCSharp.git`
- 拉取命令：`Tools\PullReference\PullReference.bat unrealcsharp`
- 该参考源主要用于参考另一套成熟的 Unreal 脚本插件工程如何组织模块、桥接运行时、管理代码生成、处理编辑器集成以及维护插件工程边界。
- 对于"插件架构怎么拆""宿主工程怎么最小化""代码生成和绑定流程怎么组织"这类问题，可以把 `UnrealCSharp` 作为横向参考。

### 9. Tencent UnLua

- 默认路径：当前项目的 `Reference\UnLua`
- GitHub：`https://github.com/Tencent/UnLua.git`
- SSH：`git@github.com:Tencent/UnLua.git`
- 拉取命令：`Tools\PullReference\PullReference.bat unlua`
- 重点目录：
- `Reference\UnLua\Source\`
- `Reference\UnLua\Docs\`
- `Reference\UnLua\Content\Script\Tutorials\`
- 该参考源主要用于观察 Lua 如何直接接入 UE 反射系统、如何覆写 Blueprint 事件、如何组织 Lua 教程与调试支持，以及如何在插件工程中同时承载运行时、编辑器与示例内容。
- 当需要横向比较"脚本事件覆写""Lua 调试与智能提示""零胶水反射暴露"时，优先先看 `UnLua`。

### 10. Tencent puerts

- 默认路径：当前项目的 `Reference\puerts`
- GitHub：`https://github.com/Tencent/puerts.git`
- SSH：`git@github.com:Tencent/puerts.git`
- 拉取命令：`Tools\PullReference\PullReference.bat puerts`
- 重点目录：
- `Reference\puerts\unreal\`
- `Reference\puerts\doc\unreal\`
- `Reference\puerts\doc\unreal\en\`
- 该参考源主要用于观察 TypeScript/JavaScript 运行时如何集成 Unreal、如何组织声明文件生成、如何支持 V8 / QuickJS / Node.js 后端切换，以及如何把脚本生态与宿主引擎解耦。
- `puerts` 虽然与 `UnLua` / `sluaunreal` 同属腾讯生态，但在 Unreal 场景下主要提供的是 JavaScript / TypeScript 能力，应视为独立参考源而不是重复拉取项。

### 11. Tencent sluaunreal

- 默认路径：当前项目的 `Reference\sluaunreal`
- GitHub：`https://github.com/Tencent/sluaunreal.git`
- SSH：`git@github.com:Tencent/sluaunreal.git`
- 拉取命令：`Tools\PullReference\PullReference.bat sluaunreal`
- 重点目录：
- `Reference\sluaunreal\Source\`
- `Reference\sluaunreal\Tools\`
- `Reference\sluaunreal\Content\`
- 该参考源主要用于观察另一套成熟 Lua 插件如何结合 Blueprint 反射、静态代码生成与 CppBinding，以及如何围绕热更新、性能分析与调试器形成完整工作流。
- 当需要比较腾讯内部两条 Lua 路线的差异时，可把 `sluaunreal` 视为偏"静态导出 / 性能 / 线上热更新"的对照项。

### 12. Blender MCP

- 默认路径：当前项目的 `Reference\blender_mcp`
- Blender Forge：`https://projects.blender.org/lab/blender_mcp.git`
- SSH：无（Blender Forge 不提供 SSH clone）
- 拉取命令：`Tools\PullReference\PullReference.bat blendermcp`
- 重点目录：
- `Reference\blender_mcp\mcp\`（MCP Server 工作目录）
- 该参考源用于查看 Blender 官方 MCP Server 的实现方式、MCP 协议接入模式、工具定义与注册方式，以及 DCC 工具链如何通过 MCP 对外暴露能力。
- 未来可能作为子仓库（submodule）独立管理；当前先以 Reference 形式引入做参考。

## 如何选择参考源

- AngelScript 语言或运行时本体问题，优先参考 `angelscript-v2.38.0`。
- Unreal 集成、绑定策略、编辑器交互、测试工程组织问题，优先参考 `HazelightAngelscriptEngineRoot` 指向的 Hazelight 参考仓库。
- 涉及引擎级补丁、引擎内扩展点或插件无法独立解释的底层行为时，同样优先参考 `HazelightAngelscriptEngineRoot` 指向的仓库。
- 需要确认 Hazelight 对外文档、能力描述、教程结构或站点内容组织时，优先看 `Docs-UnrealEngine-Angelscript`。
- 需要确认 Hazelight VS Code 扩展、Language Server、Debug Adapter、断点调试、错误展示或 workspace 交互时，优先看 `vscode-unreal-angelscript`。
- 需要 Aura 课程起始练习资产、GAS Top Down RPG 示例素材组织或 UE 5.2 示例内容工程结构时，优先看 `GameplayAbilitySystem_Aura_Initial`。
- 需要 Aura 课程实现完成态、C++ GAS 代码结构、Ability/MVVM/MotionWarping 接入和最终资源组织时，优先看 `GameplayAbilitySystem_Aura_Cpp`。
- 需要 Aura GAS 的 Angelscript 写法、AS 侧脚本分层或第三方迁移笔记时，参考 `AngelscriptAura`，但关键行为仍回查 Aura C++ 源码和当前插件绑定。
- 跨语言但同属 Unreal 脚本插件架构、模块边界、工程组织问题，可额外参考 `UnrealCSharp`。
- 需要参考 Lua 反射接入、Blueprint 事件覆写、教程组织时，优先看 `UnLua`。
- 需要参考 JavaScript / TypeScript 运行时、声明生成、脚本后端切换时，优先看 `puerts`。
- 需要比较另一套 Lua 静态导出、性能优化与热更新工作流时，优先看 `sluaunreal`。
- 需要参考 MCP 协议接入、MCP Server 实现方式或 DCC 工具链 MCP 暴露模式时，优先看 `blender_mcp`。

## 使用约束

- 外部参考仓库不应直接作为当前项目的一部分提交。
- GitHub 来源的参考仓库，应优先使用各自对应的 SSH 地址，通过统一入口 `Tools\PullReference\PullReference.bat` 拉取或同步到当前项目的 `Reference/` 目录。
- 对于 AngelScript v2.38.0，默认按"每个项目各自拉取到自己的 `Reference/` 目录"处理，不再依赖项目外部的固定公共路径。
- 对于 `Docs-UnrealEngine-Angelscript`，同样按"每个项目各自拉取到自己的 `Reference/` 目录"处理。
- 对于 `vscode-unreal-angelscript`，同样按"每个项目各自拉取到自己的 `Reference/` 目录"处理。
- 对于 `GameplayAbilitySystem_Aura_Initial`，同样按"每个项目各自拉取到自己的 `Reference/` 目录"处理，并固定在课程初始提交，除非明确要分析课程完成态。
- 对于 `GameplayAbilitySystem_Aura_Cpp`，同样按"每个项目各自拉取到自己的 `Reference/` 目录"处理，并跟随上游 `main` 获取课程完成态实现。
- 对于 `AngelscriptAura`，同样按"每个项目各自拉取到自己的 `Reference/` 目录"处理，并跟随上游 `main` 获取第三方 Angelscript 改写参考。
- 对于 `UnrealCSharp`，同样按"每个项目各自拉取到自己的 `Reference/` 目录"处理。
- 对于 `UnLua`、`puerts`、`sluaunreal`，同样按"每个项目各自拉取到自己的 `Reference/` 目录"处理。
- 对于 `blender_mcp`，同样按"每个项目各自拉取到自己的 `Reference/` 目录"处理；该仓库位于 Blender Forge（非 GitHub），仅支持 HTTPS clone。
- 本地配置来源的参考仓库，使用前先读取 `AgentConfig.ini`，不要在通用文档或脚本中写死机器路径。
- 当前本地配置来源包括：`HazelightAngelscriptEngineRoot`。
- 如果不同参考源之间存在差异，应显式区分"语言本体行为""UE 插件集成差异""引擎侧改造差异"，不要混写。
- `Hazelight Docs` 与 `Hazelight Angelscript` 不是同一个参考源：前者是公开文档仓库，后者是本机配置的源码参考路径，不能混用。
- `Hazelight VS Code Angelscript` 与 `Hazelight Angelscript` 也不是同一个参考源：前者是 VS Code 外部工具链，后者是 UE 引擎/插件源码参考路径；调试协议联调时需要同时区分客户端扩展逻辑与插件 DebugServer 逻辑。
- `UnLua` 与 `sluaunreal` 虽然同属 Lua 方案、`puerts` 与它们同属腾讯生态，但三者用途、语言重心和工程结构都不同，不应视为重复仓库。

## 维护规则

- 后续新增参考仓库时，优先按总表补充：名称、入口与说明。
- 如果新增仓库可通过 GitHub 拉取，应优先接入 `Tools\PullReference\PullReference.bat`，而不是再新增单独脚本。
- 如果新增仓库需要更详细的优先级说明，应在本文件追加独立小节，而不是继续堆积到 `Agents_ZH.md`。
