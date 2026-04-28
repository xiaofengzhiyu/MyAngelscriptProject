# 测试模块总体分层

> **所属模块**: 测试与验证架构 → Test Layering / Topic Layout
> **关键源码**: `Plugins/Angelscript/AGENTS.md`, `Plugins/Angelscript/Source/AngelscriptTest/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`, `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/`, `Documents/Guides/Test.md`, `Documents/Guides/TestConventions.md`

这一节要先钉死一件事：当前仓库的测试体系不是“所有 case 都在 `AngelscriptTest/` 里”，而是先按依赖层切出 Runtime / Editor / TestModule 三条顶层线，再在 `AngelscriptTest` 里继续按层级或主题组织。也就是说，**目录结构只是表层，真正稳定的是测试权限边界与运行环境边界。**

## 4.1.1 `Native` / `Learning` / `Shared` / `Validation` 的职责差异

- `Native/`：纯公共 AngelScript API / ASSDK 适配层验证，不应混入 `FAngelscriptEngine` 或底层 `as_*.h` 私有实现
- `Learning/`：偏教学、可观测和结构化 trace 的测试层
- `Shared/`：共享 helper / fixture 层，不是新的 Automation 分类层
- `Validation/`：规则验证和模板/宏验证的补充层

## 4.1.2 按主题目录组织的测试专题图

在 `AngelscriptTest` 内部，当前主要主题簇包括：

- `Actor` / `Component` / `Interface` / `Delegate`
- `HotReload` / `Debugger` / `Dump`
- `Bindings` / `Internals` / `Compiler` / `Preprocessor` / `ClassGenerator`
- `Examples` / `Subsystem` / `GC` / `Blueprint`

这说明 `AngelscriptTest` 不是单纯的“场景测试目录”，而是和运行时子系统一一对应的专题地图。

## 4.1.3 测试目录与 Runtime / Editor 子系统的映射方式

- `AngelscriptRuntime/Tests/` → `Angelscript.CppTests.*`，验证 Runtime 私有实现与内部状态
- `AngelscriptEditor/Private/Tests/` → `Angelscript.Editor.*`，验证 Editor 私有行为
- `AngelscriptTest/` → `Angelscript.TestModule.*`，验证插件级综合场景与主题行为

## 当前分层最值得记住的点

- 先分顶层权限边界，再分主题目录
- `Native` / `Learning` 偏层级优先，`Actor` / `Component` / `Debugger` 等偏主题优先
- Automation 前缀不是命名修饰，而是测试入口与运行环境边界的外化表达

## 小结

- 当前测试架构是严格分层的，不是单模块大杂烩
- 顶层先分 Runtime / Editor / TestModule，模块内再按层级或主题细分
- 目录、前缀、helper 和运行入口是一起设计出来的，不应该拆开理解
