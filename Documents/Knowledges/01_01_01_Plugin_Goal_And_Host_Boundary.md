# 插件目标与宿主工程边界

> **所属模块**: 插件总体架构 → Plugin Goal / Host Boundary
> **关键源码**: `AGENTS.md`, `Plugins/Angelscript/AGENTS.md`, `Plugins/Angelscript/Angelscript.uplugin`, `Source/AngelscriptProject/`

这篇入口文档要先钉死一个最容易被读错的前提：这个仓库虽然长得像一个 Unreal 工程，但真正交付物不是宿主游戏逻辑，而是 `Plugins/Angelscript` 里的可复用插件。`AngelscriptProject` 的职责是承载插件开发、编译、自动化验证和编辑器侧集成，不是把插件逻辑重新塞回项目模块里。

## 先把主语钉死

- 仓库主目标是把 `Plugins/Angelscript` 打磨成独立、可复用、可验证的 Unreal AngelScript 插件
- `Source/AngelscriptProject/` 是宿主工程壳层，主要提供最小项目上下文、启动入口和测试承载环境
- `Plugins/Angelscript/` 才是运行时、编辑器、测试、UHT、Dump、Debug、Coverage 等能力真正落地的位置

根级 `AGENTS.md` 已经把这件事说得非常明确：当前仓库不是在继续扩张普通游戏项目，而是在“以宿主工程为载体”组织插件开发与交付。因此读源码和写文档时，第一判断题永远不是“这是不是工程功能”，而是“这是不是插件核心职责”。

## 为什么必须把插件和宿主工程拆开看

如果不先建立这层边界，后面几乎所有架构判断都会偏掉：

- 会把 `AngelscriptRuntime` 的运行时主链误读成项目模块逻辑
- 会把 `AngelscriptEditor` 的菜单、导航、重实例化支持看成游戏工程编辑器扩展，而不是插件 editor-side 接缝
- 会把 `AngelscriptTest`、`Dump`、`CodeCoverage` 当作“工程测试代码”，而忽略它们其实是插件可验证性的组成部分

所以“宿主工程”在这里更像一个 **validation host**，而不是产品业务层。

## `Plugins/Angelscript` 才是交付边界

从目录职责和模块拆分看，插件侧已经形成完整交付面：

- `Source/AngelscriptRuntime/`：脚本引擎、类型系统、类/结构体生成、预处理、绑定、热重载、StaticJIT、Debugging、Dump、Coverage
- `Source/AngelscriptEditor/`：编辑器菜单、资产可见性、Source Navigation、重载辅助、Commandlet
- `Source/AngelscriptTest/`：插件级自动化验证、Native Core 适配、Debugger/Dump/HotReload/Actor 等专题回归
- `Source/AngelscriptUHTTool/`：UHT 生成链、函数表导出、签名解析与 Direct-Bind 补链

这套结构说明插件并不是“一个 Runtime 模块再加一点编辑器支持”，而是一整套自足的能力包。宿主工程存在的意义，是给这套能力包提供编译、运行和回归环境。

## 宿主工程保留什么，不保留什么

- **保留**：最小工程模块、插件装载环境、测试/编辑器运行上下文
- **保留**：项目级配置、用于触发插件验证的工程外壳
- **不保留**：应当沉到插件中的脚本运行时逻辑、绑定逻辑、测试辅助逻辑
- **不保留**：把插件能力重新复制到 `Source/AngelscriptProject/` 的“临时过桥实现”

根级 `AGENTS.md` 也直接要求：绝大多数实现、修复、清理和测试都应该优先落到 `Plugins/Angelscript/`，除非任务明确要求宿主工程配合。也就是说，宿主工程是边界外的支撑层，而不是默认落点。

## 这层边界如何影响后续阅读

一旦主语确定，后续文档顺序就会自然成立：

```text
宿主工程只是载体
  -> 插件模块才是主体
      -> Runtime / Editor / Test / UHT 各自承担不同职责
          -> 架构文档、测试文档、治理文档都围绕插件交付面展开
```

这也是为什么 `01_01_02_Directory_Responsibilities.md`、`01_02_*` Runtime 主链、`01_03_*` Editor/Test/Dump 边界，以及后续 `02_*` 到 `05_*` 专题都应该围绕插件讲，而不是围绕宿主项目讲。

## 对写文档和做实现的直接约束

- 新增架构文章时，优先回答“这属于插件哪一层能力”，而不是“它在工程里怎么跑起来”
- 新增实现时，优先放到 `Plugins/Angelscript` 的合适模块，不把插件逻辑回灌到项目模块
- 更新 Build/Test/Release 资料时，主线目标是让外部使用者理解如何使用和验证插件，而不是理解宿主工程业务

换句话说，这个仓库的工程壳层服务于插件，而不是反过来。

## 小结

- `AngelscriptProject` 是插件开发与验证载体，不是最终交付主体
- 真正的产品边界在 `Plugins/Angelscript/`，包括 Runtime、Editor、Test、UHT、Dump、Coverage 等模块
- 判断源码、文档和测试落点时，默认先问“这是不是插件职责”，只有明确需要宿主配合时才回到项目模块
