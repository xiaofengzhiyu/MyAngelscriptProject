# 主题测试簇与架构映射

> **所属模块**: 测试与验证架构 → Topic Tests / Coverage Mapping
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/`, `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/`, `Plugins/Angelscript/Source/AngelscriptTest/Dump/`, `Documents/Guides/TestCatalog.md`, `Documents/Guides/TestConventions.md`

当前主题测试簇的价值，不在于目录名好看，而在于它们把“运行时子系统地图”反向投影成“验证入口地图”。也就是说，**测试目录树其实是插件架构的一份验证侧镜像。**

## 4.3.1 `ClassGenerator` / `Preprocessor` / `HotReload` 专题测试如何覆盖主链路

- `ClassGenerator/` 覆盖类与结构体生成、重载传播和对象语义
- `Preprocessor/` 覆盖模块组织、import 与宏处理前端
- `HotReload/` 覆盖文件变化后的 reload requirement、soft/full reload 与恢复路径

这三类目录直接对应运行时主链最核心的“前端 → 生成 → 重载”路径。

## 4.3.2 `Actor` / `Component` / `Interface` / `Delegate` 等行为专题的组织方式

- 这些目录不是在测编译器内部，而是在测最终 UObject / World 行为语义
- 它们通常使用 `Angelscript.TestModule.<Theme>.*` 前缀，强调“按主题验证最终行为结果”
- 与 `Native`、`Learning` 不同，它们是典型的主题优先目录

## 4.3.3 `Debugger` / `Dump` / `Subsystem` 等支撑专题的验证入口

- `Debugger/` 验证调试协议、会话、断点与变量面
- `Dump/` 验证状态导出入口与回归
- `Subsystem/` 验证脚本子系统生命周期与集成点

它们共同说明：验证地图不仅覆盖 gameplay-like 行为，也覆盖插件的工程化支撑能力。

## 当前映射最值得记住的点

- 主题测试簇可以直接反向映射到运行时子系统
- 主链专题和行为专题是两种不同的覆盖角度，缺一不可
- Debugger / Dump / Subsystem 等目录说明“支撑系统”同样被纳入正式验证版图

## 小结

- 当前测试目录结构是一份架构侧镜像
- `ClassGenerator`、`Preprocessor`、`HotReload` 这类目录覆盖主链，`Actor`、`Component` 等目录覆盖最终行为语义
- 这套映射让测试体系不仅能发现 bug，还能帮助读者理解插件的子系统边界
