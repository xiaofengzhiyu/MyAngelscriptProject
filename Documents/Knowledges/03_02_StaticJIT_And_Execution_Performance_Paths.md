# StaticJIT 与执行性能路径

> **所属模块**: 运行时支撑子系统 → StaticJIT / Performance Path
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Documents/Plans/Plan_StaticJITUnitTests.md`

这一节的重点不是“插件支持 JIT”，而是当前 JIT 体系如何与解释执行、预编译缓存和运行时函数对象配合。最核心的判断是：**StaticJIT 不是替代解释器，而是在解释执行之上插入一条可预编译、可缓存、可按函数分流的加速路径。**

## 3.2.1 StaticJIT 数据结构与数据库组织

- `FAngelscriptStaticJIT` 实现 `asIJITCompiler`，是 AngelScript 侧看到的 JIT 编译入口
- `FJITDatabase` 保存函数映射与查找结果，是运行时消费 JIT 函数的重要数据库层
- `FStaticJITContext` 负责字节码扫描、表达式栈管理和 C++ 输出生成

因此当前 JIT 架构不是“一次编译后立即执行”，而是“编译上下文 + 函数数据库 + 运行时分发”三段式。

## 3.2.2 解释执行与 JIT 执行分流

- `UASFunction` 同时保存脚本函数与若干 JIT 入口指针
- 当 JIT 可用时，调用桥会优先走 `JitFunction` 或 `JitFunction_ParmsEntry`
- 当 JIT 不可用、不可安全使用或当前路径不满足条件时，执行会退回普通脚本 VM

这说明解释执行和 JIT 不是两个完全隔离的运行时，而是在函数入口处分流。

## 3.2.3 预编译 / 缓存与限制边界

- `PrecompiledData.h` 序列化模块、类、函数、枚举、全局变量和引用关系
- 这些预编译数据不是泛化缓存，而是为了在后续加载或恢复过程中减少重复工作
- 限制边界在于：缓存必须能够重新映射类型、函数和数据引用，才能在新一次运行里正确恢复

## 3.2.4 `PrecompiledData` 序列化与三阶段应用

可以把当前 `PrecompiledData` 理解成三段：

- **采集阶段**：把模块/类/函数/字节码状态写成可恢复描述
- **恢复阶段**：重建类型与函数引用映射
- **应用阶段**：把恢复后的函数/类型重新挂回运行时对象与调用路径

这也是为什么 `TypeReferences`、`FunctionReferences`、`DataGuid` 这些字段如此关键：它们负责把“旧编译结果”安全迁移到“新运行时身份”。

## 3.2.5 `FJITDatabase` 的函数映射与查找路径

- JIT 数据库的职责不是保存所有脚本语义，而是把“某个函数对应的已生成 JIT 入口”组织成可快速查找的索引
- 运行时调用桥通过 `UASFunction` 上的 JIT 指针与数据库映射协同完成最终分发
- 因此 `FJITDatabase` 属于执行路径加速基础设施，而不是编译前端的一部分

## 当前体系最值得记住的点

- StaticJIT 是运行时加速层，不替代解释器主语义
- 预编译数据和 JIT 数据库一起组成“可恢复的加速路径”
- `UASFunction` 是解释执行和 JIT 分流真正汇合的地方

## 小结

- 当前 JIT 架构由编译器接口、生成上下文、JIT 数据库和函数入口指针共同组成
- 解释执行、JIT 和预编译缓存是同一条执行主线上的不同阶段，不是分裂的子系统
- 理解 `PrecompiledData` 和 `FJITDatabase`，才能看清 StaticJIT 为什么不仅是性能优化，也是工程化缓存能力的一部分
