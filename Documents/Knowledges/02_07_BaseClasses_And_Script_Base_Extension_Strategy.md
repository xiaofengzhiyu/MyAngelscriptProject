# BaseClasses 与脚本基类扩展策略

> **所属模块**: 类型系统与生成链路 → BaseClasses / Script Base Extension
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/`, `Documents/Hazelight/ScriptClassImplementation.md`

这一节真正要解释的不是“BaseClasses 里有几个 subsystem 基类”，而是这些基类为什么要作为插件显式提供。当前 BaseClasses 的作用，可以概括成一句话：**给脚本继承链提供一组已经被 UE 生命周期驯化过的原生基座。**

## 2.7.1 Runtime 基类封装的目的与范围

- 当前 BaseClasses 主要集中在 `ScriptWorldSubsystem`、`ScriptGameInstanceSubsystem`、`ScriptEngineSubsystem`、`ScriptLocalPlayerSubsystem`
- 这些类不是为了替代 UE 原生 subsystem，而是为了把脚本可重写的生命周期、tick 能力和 Blueprint 接缝包装成稳定基类
- 它们负责把“脚本想接管哪个生命周期”转译成 UE 子系统框架可接受的形式

## 2.7.2 脚本继承链与原生继承链的衔接点

- BaseClasses 作为原生父类进入 `UASClass` / 类生成链，给脚本类提供一个明确的 C++ 起点
- 例如 World / Engine / GameInstance / LocalPlayer 这几条 subsystem 链路，都通过对应基类把脚本事件接到真实的 UE 生命周期上
- 这些类通常提供 `BP_Initialize`、`BP_Deinitialize`、`BP_Tick` 一类脚本侧扩展点，让脚本类可以在不直接实现原生复杂接口的前提下接入系统

因此脚本继承链不是悬空长出来的，它是踩在这些 Runtime 基类之上生长的。

## 2.7.3 BaseClasses 与类生成 / Bind 的耦合边界

- BaseClasses 本身不是 Bind 系统，但它们会影响类生成器如何理解可继承原生类型
- 同时它们也为 FunctionLibrary / Bind 暴露提供了更稳定的目标类型和脚本扩展面
- 耦合边界在于：BaseClasses 负责提供“可继承的原生脚手架”，类生成器负责产生脚本类，Bind 负责把 API 暴露给这些类使用

## 当前策略最值得记住的点

- BaseClasses 的价值不在数量，而在于把脚本扩展点固化成稳定的 UE 生命周期接缝
- 脚本继承链与原生继承链不是平行存在，而是在这些基类上明确衔接
- 它们与类生成、绑定系统相关，但不应该反向吞并这些系统的职责

## 小结

- BaseClasses 为脚本类提供经过 UE 生命周期驯化的原生基座
- 它们是脚本继承链接入原生系统的第一跳
- 当前边界是：基类管生命周期脚手架，类生成器管脚本类，Bind/Library 管可用 API 面
