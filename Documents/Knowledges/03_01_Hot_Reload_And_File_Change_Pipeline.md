# 热重载与文件变更链路

> **所属模块**: 运行时支撑子系统 → Hot Reload / File Change Pipeline
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp`

这一节要回答的不是“脚本改了会自动重编译”，而是变更如何从 Editor 文件系统事件一路传到 Runtime 引擎与类生成器。当前链路可以压成一句话：**Editor 负责发现变化，Runtime 负责排队与判定，ClassGenerator 负责决定 soft/full reload，Editor-side reload helper 再接手实例重建。**

## 3.1.1 文件发现与变更感知入口

- 编辑器侧目录监听来自 `AngelscriptDirectoryWatcherInternal.cpp`
- 目录变更会被转换成 `QueueScriptFileChanges()` 调用，映射到当前脚本根与已加载脚本集合
- Runtime 侧通过 `FileChangesDetectedForReload`、`FileDeletionsDetectedForReload` 和 `FileHotReloadState` 保存待处理事件

因此文件变化不是直接触发重编译，而是先进入引擎的重载队列。

## 3.1.2 reload requirement 传播与 class rebind

- `FAngelscriptClassGenerator` 会分析变更对类/属性/函数签名的影响
- 结果收敛为 `EReloadRequirement`：从 soft reload 到 full reload，再到错误路径
- 重载需求会沿依赖关系传播，确保不是只有直接改动的类被重绑，相关依赖也会进入分析范围

## 3.1.3 Editor / Runtime 在重载中的职责划分

- Runtime 负责脚本状态、编译、队列管理与重载策略判定
- Editor 负责把文件系统事件、Blueprint 影响和 reinstance 工作流接进来
- `ClassReloadHelper` 站在 editor-side，处理 Blueprint 层级的重实例化和影响扩散

所以当前热重载不是单模块能力，而是 Runtime 和 Editor 明确协作的两段式链路。

## 3.1.4 DirectoryWatcher 与轮询保底策略

- 首选路径是 Editor 目录监听器
- 引擎侧仍保留状态队列与时间戳管理，确保变化不会在同一帧内以不稳定方式直接冲进编译链
- 这类设计本质上是把“感知变更”和“消费变更”解耦，给轮询/延迟恢复留出保底空间

## 3.1.5 编译失败重试、排队与延迟恢复

- `CheckForHotReload()` 在 tick 驱动下消费待处理变化，而不是在文件事件回调里同步重编译
- 编译/重载失败时，队列和状态位允许后续重试，而不是直接把引擎状态锁死在一次失败里
- 这也是 `bIsHotReloading`、延迟队列和状态恢复逻辑存在的原因：让热重载成为一个可恢复流程，而不是一次性事务

## 当前链路最值得记住的点

- 目录监听属于 Editor，重载主状态属于 Runtime
- `EReloadRequirement` 是连接文件变化和类重载策略的关键中间语义
- 实例重建是 editor-side 问题，不应该反向塞回 Runtime 主流程

## 小结

- 热重载链路由“发现变化 → 排队 → 判定需求 → soft/full reload → editor-side reinstance”组成
- Runtime 和 Editor 各自承担不同职责，边界清晰
- 失败重试与延迟恢复说明当前系统追求的是可持续恢复的重载流程，而不是同步立即生效的脆弱路径
