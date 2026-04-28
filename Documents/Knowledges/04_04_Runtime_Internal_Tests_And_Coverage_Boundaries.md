# Runtime 内部测试与覆盖边界

> **所属模块**: 测试与验证架构 → Runtime Internal Tests / Coverage Boundary
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`, `Plugins/Angelscript/AGENTS.md`, `Documents/Guides/TestConventions.md`

Runtime 内部测试这条线最重要的边界是：**它们不是插件级综合场景验证，而是允许直接碰 Runtime 私有实现、共享状态和内部上下文的 C++ 测试层。** 这也是为什么仓库专门要求它们使用 `Angelscript.CppTests.*` 前缀。

## 4.4.1 `AngelscriptRuntime/Tests` 与 `AngelscriptTest` 的边界

- `Runtime/Tests` 可以直达 Runtime 私有实现和内部状态，适合测隔离、上下文池、协议序列化、预编译数据等底层问题
- `AngelscriptTest` 更偏插件对外综合验证与 UE 场景行为
- 因此两者虽然都叫“测试”，但前者是内部安全网，后者是插件级行为地图

## 4.4.2 `Angelscript.CppTests.*` 自动化前缀的作用域

- 这个前缀明确表达“这是 Runtime 内部测试，不是 TestModule 场景测试”
- 它直接关联测试入口脚本和 suite 组织方式
- 一旦把这类 case 混到 `Angelscript.TestModule.*`，就会同时破坏目录边界和执行入口边界

## 4.4.3 内部测试如何服务运行时重构与验证

- 当前 `MultiEngineTests`、`EngineIsolationTests`、`CodeCoverageTests`、调试协议/传输测试等，都属于运行时重构的第一道安全网
- 它们能在不拉起完整场景语义的前提下，快速验证共享状态、上下文切换、协议序列化和缓存路径
- 因此 Runtime 内部测试更像“重构安全垫”，而不是“最终行为验收”

## 4.4.4 Native Core 适配层与原始 AngelScript API 测试边界

- Native Core 测试虽然也偏底层，但它们落在 `AngelscriptTest/Native/`，因为它们强调公共 API / ASSDK 视角
- Runtime 内部测试则可以使用插件私有实现与内部状态
- 这两者都不是 UE 场景测试，但验证边界完全不同：一个是公共 API，一个是私有实现

## 当前边界最值得记住的点

- Runtime 内部测试验证私有实现，Native Core 测试验证公共 API，`AngelscriptTest` 主题目录验证最终场景行为
- `Angelscript.CppTests.*` 是权限边界和执行入口边界的双重标记
- 这条线对运行时重构特别重要，因为它能最早暴露低层破坏

## 小结

- `Runtime/Tests` 是插件底层能力的内部安全网
- 它和 `AngelscriptTest`、`Native` 目录形成三种不同的低层验证视角
- 前缀、目录和权限边界在这里必须保持严格一致
