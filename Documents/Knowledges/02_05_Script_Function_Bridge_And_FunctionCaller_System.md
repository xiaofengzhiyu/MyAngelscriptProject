# 脚本函数调用桥与 FunctionCaller 体系

> **所属模块**: 类型系统与生成链路 → Function Bridge / FunctionCaller
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Documents/Hazelight/ScriptClassImplementation.md`

这一节要解决的是“脚本函数怎么真正落到 UE 调用栈上”。当前实现不是靠一套硬编码函数表直接跳转，而是通过类型擦除调用器、参数行为枚举和 Blueprint VM / JIT 入口拼成一座桥。

## 2.5.1 Script Function 到 UE 调用栈的桥接方式

- `ASClass.cpp` 里的 `AngelscriptCallFromBPVM` 是 Blueprint VM 进入脚本函数的关键入口
- `UASFunction` / `ScriptFunction` 保存脚本侧函数句柄，JIT 可用时再额外挂 `JitFunction`、`JitFunction_ParmsEntry`
- 调用桥既要支持普通脚本调用，也要支持 Blueprint、JIT 和非虚调用优化路径

这说明函数桥的真正职责是：**把 UE 调用栈、脚本 VM、JIT 优化入口收敛成一条统一调用面。**

## 2.5.2 `FunctionCallers.h` 中的调用器分层

- `FGenericFuncPtr` 负责把函数指针和方法指针擦成统一存储形态
- `ASAutoCaller::FunctionCaller` 根据目标是 free function 还是 method，决定具体转发路径
- `RedirectFunctionCaller` / `RedirectMethodCaller` 模板完成真实调用分发

因此 FunctionCaller 体系不是额外功能，而是脚本绑定层能够泛化调用各种 UE 函数签名的基础设施。

## 2.5.3 参数封送、返回值与错误传播

- `EArgumentVMBehavior` 区分 float 扩展、world context、对象指针、POD 引用、复杂引用和值返回等多种行为
- `PassArgument<T>`、`TSetAngelscriptArgument`、`TGetAngelscriptReturnValue` 把脚本 VM 参数槽和 UE/C++ 参数类型对齐
- JIT 路径和解释执行路径共享大部分调用模型，只是在最终入口上分流

错误传播的关键不在一个单独的“异常桥”，而在：

- 参数与返回值的布局必须在进入 VM 前就被严格规范
- 当签名、值语义或引用语义不匹配时，问题会在调用桥层暴露，而不是留到更后面的 UObject 语义层

## 当前体系最值得记住的点

- FunctionCaller 不是给脚本“找个函数指针”，而是在做签名泛化与调用协议统一
- Blueprint VM、解释执行和 JIT 并不是三套完全独立的桥，而是在同一套参数封送模型上分流
- 参数行为分类是这个体系的关键，不理解它就很难读懂脚本函数桥为什么能覆盖那么多调用场景

## 小结

- `FunctionCallers.h` 提供类型擦除与统一分发
- `ASClass` / `ASFunction` 把这套调用器接到 Blueprint VM 和 JIT 入口上
- 参数封送与返回值处理是脚本函数桥真正的稳定性核心
