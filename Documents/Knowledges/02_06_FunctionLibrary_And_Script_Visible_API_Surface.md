# FunctionLibrary 与脚本可见 API 暴露面

> **所属模块**: 类型系统与生成链路 → FunctionLibrary / Script-visible API
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`, `Plugins/Angelscript/AGENTS.md`

这一节要钉死的不是“仓库里有很多 Library 头文件”，而是 FunctionLibrary 在当前插件里到底承担什么暴露策略。最核心的判断是：**FunctionLibrary 不是替代 Bind 系统的另一条注册线，而是把脚本可见 API 组织成更稳定、更接近 UE 习惯的 mixin/utility 暴露层。**

## 2.6.1 Runtime FunctionLibraries 的组织方式

- Runtime 侧大量 Library 以 `UCLASS + static UFUNCTION` 方式存在
- 常见模式是 `meta = (ScriptMixin = "TargetClass")`，把库函数按目标类型挂接为脚本侧 mixin 能力
- `AngelscriptMathLibrary` 这类库还会通过 `ScriptName` 组织脚本空间名称，而不是简单复刻 UE 原类名

因此 Runtime FunctionLibrary 的核心职责是：**把常用能力整理成脚本友好的 API 面，而不是让脚本直接面对所有底层绑定细节。**

## 2.6.2 Editor FunctionLibraries 与运行时隔离

- Editor FunctionLibraries 只补 editor-only 入口，不应被 Runtime 反向依赖
- 它们与 Runtime 库的分离，跟插件模块边界保持一致：脚本运行时能力属于 Runtime，编辑器工作流增强属于 Editor
- 这类分离也保证 cooked 场景不会误带 editor-only API 面

## 2.6.3 脚本层 API 暴露边界与常见模式

当前最常见的暴露模式有三类：

- **Mixin 型**：把函数附着到某个脚本可见类上，例如 Actor、Component、Streaming 类型
- **工具型**：Math、Subsystem、Utility 类函数，作为脚本侧工具面存在
- **上下文型**：通过 `WorldContext`、`DeterminesOutputType`、`ScriptName` 等元数据控制脚本层可见性与推导方式

这说明 API 暴露边界并不只由“函数在不在 Runtime 模块”决定，还受：

- 是否需要世界上下文
- 是否属于 editor-only
- 是否希望以 mixin 而非裸全局函数暴露

## 当前体系最值得记住的点

- FunctionLibrary 解决的是 API 组织问题，不是底层绑定发现问题
- Runtime / Editor 分离保证脚本可见 API 面不会把 editor-only 能力带进运行时主线
- `ScriptMixin`、`ScriptName`、`DeterminesOutputType` 等元数据，是脚本可见 API 设计的一部分，不只是装饰标签

## 小结

- Runtime FunctionLibraries 把常用脚本 API 组织成 mixin 和 utility 暴露层
- Editor Libraries 只服务编辑器工作流，与 Runtime 隔离
- 当前脚本 API 暴露面是 Bind、元数据和 Library 组织方式共同作用的结果
