# Debugger 与调试协议集成

> **所属模块**: 运行时支撑子系统 → Debugger / Debug Protocol
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/`, `Documents/Plans/Plan_DebugAdapter.md`, `Documents/Plans/Plan_ASDebuggerUnitTest.md`

这一节要回答的核心问题是：调试器不是一个孤立 TCP 服务，而是脚本 VM、变量提取、断点管理和测试验证共同组成的协议面。当前实现最关键的特征是：**DebugServer V2 同时承担会话管理、断点控制和变量观察，而测试模块又把这套协议固定成可回归的验证入口。**

## 3.3.1 Debug Server 与连接生命周期

- `FAngelscriptDebugServer` 负责监听端口、接受连接和处理消息队列
- 协议层以版本握手为前提，确保客户端和服务端都在 DebugServer V2 能力面上
- `ProcessMessages()` 这类 tick 驱动逻辑说明调试服务是运行时持续存在的控制面，而不是只在断点命中时临时启动

## 3.3.2 断点、堆栈与脚本控制面

- 行断点通过模块/文件/行号映射保存到断点表
- 调试控制面支持 Pause、Continue、StepIn、StepOver、StepOut 等操作
- `SendCallStack()` 会把 AngelScript 调用栈和 Blueprint 上下文并排组织成统一可观察的调用链

这说明当前调试能力的重点不是“能不能停住”，而是把脚本栈与 UE 环境组合成可解释的调试上下文。

## 3.3.3 调试能力与测试验证边界

- Runtime 负责真正的协议实现、断点命中、变量提取和服务状态维护
- `AngelscriptTest/Debugger/` 负责 smoke tests、协议验证与会话回归
- `AngelscriptDebuggerTestSession` 这类 helper 把调试会话包装成测试可消费的 fixture

因此调试器不是“实现了再单独手测”的系统，而是被明确纳入自动化验证边界。

## 3.3.4 Data Breakpoint / Watchpoint 机制

- 当前实现支持数据断点，并使用有限的硬件断点槽位管理活动 watchpoint
- 这说明调试器不只关注脚本行控制，还在尝试把“数据变化”纳入观察范围
- 同时这也给出一个清晰边界：Data Breakpoint 能力天然受平台和硬件资源限制，不属于完全抽象的纯脚本功能

## 3.3.5 调试值提取、作用域与变量序列化

- `AngelscriptDebugValue.h` 提供调试值原型、作用域和成员提取能力
- 变量观察不是简单把内存地址吐给客户端，而是结合类型系统和调试值桥层生成可读序列化结果
- 这意味着 Debugger 和 Type System 之间有直接耦合：没有统一的调试值提取模型，就不会有稳定的变量面板能力

## 当前体系最值得记住的点

- DebugServer V2 是运行时控制面，不是附属工具进程
- 断点、调用栈、变量提取和 Data Breakpoint 共同组成当前调试能力面
- Test 模块已经把这套协议固化进回归入口，所以调试器也是插件可验证能力的一部分

## 小结

- 当前调试器架构由 DebugServer、调试值桥、断点表和测试会话共同组成
- 它同时服务协议控制、变量观察和自动化验证
- 真正的边界是：Runtime 实现调试面，Test 固化验证面，Type System 支撑值提取面
