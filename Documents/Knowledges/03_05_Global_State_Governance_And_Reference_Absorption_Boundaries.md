# 全局状态治理与外部参考吸收边界

> **所属模块**: 运行时支撑子系统 → Global State Governance / Reference Absorption
> **关键源码**: `Documents/Guides/GlobalStateContainmentMatrix.md`, `Documents/Guides/TechnicalDebtInventory.md`, `Documents/Plans/Plan_FullDeGlobalization.md`, `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md`, `Documents/Hazelight/ScriptClassImplementation.md`, `Documents/Hazelight/ScriptStructImplementation.md`

这一节的核心不是简单列技术债，而是建立一个治理判断：哪些全局状态访问已经被 containment，哪些路径仍然属于架构级问题；哪些外部参考可以吸收，哪些只能作为问题域对照。当前仓库已经非常明确地把这两件事绑在一起看，因为“怎么去全局化”和“外部实现是否值得照搬”本来就是同一个架构决策问题的两面。

## 3.5.1 全局状态 containment 模式与问题分类

- `TechnicalDebtInventory.md` 已经把全局访问热点按文件和主题分簇
- `GlobalStateContainmentMatrix.md` 进一步区分：已 containment、低风险可 containment、仍需架构重做的路径
- 当前已 containment 的重点在测试包装器和一部分调试路径，而类生成、核心编译、世界上下文传播仍然是高复杂度问题域

## 3.5.2 去全局化阶段规划

- `Plan_FullDeGlobalization.md` 不把这件事当作一次性清理，而是拆成文档定边界、设计 owner/context 流、批量实施和回归验证等阶段
- 这说明去全局化不是“把 `Get()` 全部替换成参数传递”这么简单，而是要先重新设计哪些子系统应该显式持有 engine/world context
- 当前阶段最适合 containment 的通常是测试和工具路径；最不适合轻率 patch 的则是类生成、编译主链和世界生命周期链路

## 3.5.3 Hazelight / UnrealCSharp 参考点与不可照搬项

- Hazelight 更适合作为 UE 集成与脚本运行时问题的对照样本
- UnrealCSharp 更适合作为插件工程化、架构拆层和可交付性参考
- 但两者都不能直接照搬，原因在于当前仓库的 ThirdParty AngelScript fork、运行时模型、绑定策略和测试/治理目标都已经深度定制

## 当前治理边界最值得记住的点

- containment 不是统一动作，而是要按路径复杂度和风险分级
- 外部参考的价值在于帮助识别方案空间，而不是提供可直接复制的目标实现
- 去全局化和外部参考吸收必须一起看，否则很容易把“短期 containment”误当成“长期架构收敛”

## 小结

- 当前仓库已经把全局状态问题从技术债清单提升为架构治理主题
- `Containment Matrix + DeGlobalization Plan + External Reference Plans` 构成了完整判断框架
- 真正重要的不是减少几个 `Get()` 调用，而是让 engine/world context 和外部参考吸收都回到显式边界里
