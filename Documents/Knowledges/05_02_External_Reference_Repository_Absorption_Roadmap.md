# 外部参考仓库吸收路线

> **所属模块**: 工程治理与演进路线 → External References / Absorption Roadmap
> **关键源码**: `Reference/README.md`, `Documents/Hazelight/`, `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md`, `Documents/Guides/AngelscriptForkStrategy.md`, `AGENTS.md`

这一节要明确的不是“我们参考过哪些仓库”，而是不同外部仓库各自承担什么比较角色，以及吸收时必须停在哪条边界上。当前仓库已经很明确：**外部参考只用于比较、迁移分析和架构启发，不是要把别人的实现整包移植过来。**

## 5.2.1 Hazelight 参考点的吸收方式

- Hazelight 更适合拿来对照 Unreal + AngelScript 的集成模式、类/结构体实现专题和测试组织方式
- 它适合作为“问题域参考”和“同类系统对照”，不适合作为当前 fork 的逐文件替换目标
- 当前 `Documents/Hazelight/` 下的专题文档，已经说明仓库更偏向“先理解设计，再决定吸不吸收”

## 5.2.2 UnrealCSharp 架构参考的适用边界

- UnrealCSharp 更适合提供插件工程化、模块划分、工具链和可交付架构的参考
- `Plan_UnrealCSharpArchitectureAbsorption.md` 本身就说明，当前仓库是在把它当作架构对照，而不是直接复制脚本运行时细节
- 因此它更像“工程骨架参考”，而不是语言运行时实现模板

## 5.2.3 与官方 AngelScript 版本演进的关系

- 官方 AngelScript 版本线主要通过 `AngelscriptForkStrategy.md` 与 `Plan_AS238*` 系列计划进入本仓库
- 这里的重点不是升级版本号，而是选择性吸收上游能力，同时不破坏现有 fork 的 UE 集成和插件主线
- 这条路线和 Hazelight / UnrealCSharp 的吸收方式不同：前者偏能力对照，后者偏工程化对照，官方 AS 则偏语言/runtime 基线对照

## 当前吸收路线最值得记住的点

- 外部参考分工不同，不能混成一个“大参考池”
- Hazelight、UnrealCSharp、官方 AngelScript 三条线分别解决不同问题
- 吸收的判断标准不是“别人有这个功能”，而是“这是否服务当前插件主线且不会破坏现有边界”

## 小结

- 当前仓库已经形成一套明确的外部参考分工：Hazelight 看集成模式，UnrealCSharp 看工程骨架，官方 AS 看语言/runtime 基线
- 吸收路线的关键是边界感，而不是速度
- 真正的演进目标是让参考输入服务本地插件架构，而不是让本地架构失去自我主语
