# 测试基础设施与 Shared Helper

> **所属模块**: 测试与验证架构 → Shared Helpers / Test Infrastructure
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`, `Documents/Guides/TestConventions.md`, `Documents/Plans/Plan_TestEngineIsolation.md`

当前 `Shared/` 目录最容易被误解成“放杂项工具的地方”，但它其实承担的是测试基础设施层：**负责统一引擎创建、生命周期管理、编译执行辅助、场景 fixture、调试会话包装和学习型 trace 支撑。**

## 4.2.1 `AngelscriptTestEngineHelper` 生命周期管理

- `AngelscriptTestEngineHelper` 负责最常见的编译、执行、模块分析与生成类查找辅助
- 它不是引擎创建器本身，而是站在“已有测试引擎上下文”之上的高频 helper
- 真正的引擎生命周期切换，更多由 `AngelscriptTestUtilities` 和宏层完成

## 4.2.2 `AngelscriptTestUtilities` / `Macros` 的职责边界

- `AngelscriptTestUtilities.h` 负责引擎创建模式、共享缓存、隔离克隆、生产引擎获取与覆盖等底层能力
- `AngelscriptTestMacros.h` 在此之上提供 `ASTEST_BEGIN_* / ASTEST_END_*` 等两层宏封装，降低测试样板代码成本
- 因此 Utilities 是能力层，Macros 是使用层

## 4.2.3 Shared Fixture 如何支撑 Debugger / Scenario / Native 测试

- `Shared/AngelscriptScenarioTestUtils.h` 服务 UE 场景类测试
- `Shared/AngelscriptDebuggerTestSession.h` 服务调试协议测试
- `Native/AngelscriptNativeTestSupport.h` 服务 Native Core 测试，它属于 Native 专题支撑而不是 Shared 目录本身
- `Shared/AngelscriptLearningTrace.h` 服务 Learning 目录的可观测 trace

这说明 `Shared/` 并不是一个统一大 fixture，而是一组按测试类型切开的基础设施簇。

## 4.2.4 `AngelscriptDebuggerTestSession` / Client / Fixture 的协作方式

- `AngelscriptDebuggerTestSession` 把 DebugServer 会话、消息泵和等待条件包装成测试可复用对象
- 这类 session helper 使得调试测试不必直接手写 socket 生命周期和轮询细节
- 因此它的职责不是实现调试器，而是把调试器协议面稳定地暴露给自动化测试

## 当前基础设施最值得记住的点

- Utilities 管底层能力，Macros 管测试调用面，专门 helper 管不同专题 fixture
- `Shared/` 是基础设施层，不是新的测试分层标签
- 测试体系之所以能按主题扩张，关键就在于 Shared 层先把引擎创建、会话、场景、trace 等共性抽了出来

## 小结

- `Shared/` 承载的是测试基础设施，而不是测试分类本身
- 它通过 Utilities、Macros 和专题 helper 为不同测试簇提供统一脚手架
- 这层设计保证了测试系统能在不复制大量样板逻辑的前提下持续扩展
