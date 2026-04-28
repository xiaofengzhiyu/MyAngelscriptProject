# ThirdParty AngelScript 内核集成边界

> **所属模块**: 运行时支撑子系统 → ThirdParty Fork / Kernel Boundary
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_parser.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_memory.h`, `Documents/Guides/AngelscriptForkStrategy.md`, `Documents/Guides/ASSDK_Fork_Differences.md`

这一节最重要的结论是：当前 ThirdParty/angelscript 不是一个“等着升级的原版依赖”，而是一个已经深度服务插件运行时目标的 fork。也正因为如此，仓库才会明确采取“2.33 基底 + 选择性吸收 2.38 能力”的策略，而不是整体升级。

## 3.8.1 ThirdParty 源码镜像与本地修改策略

- `ThirdParty/angelscript/source/` 保存的是本地维护 fork，而不是纯镜像拷贝
- 大量 `[UE++]` 标记说明这里有明确的本地改动面，可追踪哪些点是 UE/插件集成需要
- `as_memory.h` 里的 `FMemory` 接管、导出宏改名、测试模块导出面等，都说明这不是透明依赖，而是深度嵌入式内核

## 3.8.2 Parser / ScriptEngine / ScriptFunction 等核心内核点

- `as_parser.cpp` 代表语法与前端能力边界，是选择性吸收 foreach 等 2.38 能力的重要落点
- `as_scriptengine.cpp` 代表类型、对象标志和运行时核心控制面
- `as_scriptfunction.cpp` 代表函数恢复、执行与字节码相关能力

因此这三类文件并不是“普通第三方源码”，而是 fork 能力演进时最需要谨慎处理的核心内核点。

## 3.8.3 上游升级与 fork 差异管理

- `AngelscriptForkStrategy.md` 已经把策略写得很清楚：只吸收有明确收益的上游特性和修复，不做 wholesale upgrade
- 吸收时要保留 APV2/插件定制路径，不破坏现有对象标志、模块存储、内存模型和热重载相关实现
- 这也是为什么仓库会单独维护一批 `Plan_AS238*` 计划，而不是用一个“升级到 2.38”总任务兜底

## 当前边界最值得记住的点

- ThirdParty fork 是插件架构的一部分，不是纯外部依赖
- `[UE++]` 标记是 fork 差异治理的重要索引，不只是代码注释
- 上游版本演进必须围绕“当前插件需要什么能力、哪些路径绝不能被破坏”来做选择性吸收

## 小结

- 当前 AngelScript 内核集成边界由本地 fork、UE 集成修改和选择性升级策略共同定义
- Parser / ScriptEngine / ScriptFunction 是最关键的三类内核点
- 真正的治理重点不是“追最新版本”，而是让 fork 差异始终可理解、可验证、可分阶段吸收
