# 附录 A.3 外部参考对照表

> **所属模块**: 附录 → External Reference Comparison
> **关键源码**: `Reference/README.md`, `Documents/Hazelight/`, `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md`, `Documents/Guides/AngelscriptForkStrategy.md`

## 对照表

| 参考源 | 主要用途 | 适合回答的问题 | 不应直接照搬的部分 |
| --- | --- | --- | --- |
| Hazelight Angelscript | UE + AngelScript 集成对照 | 类/结构体支持、脚本测试、UE 集成模式 | 引擎改造细节、与当前 fork 不兼容的实现路径 |
| UnrealCSharp | 插件工程化与架构对照 | 模块划分、工具链、工程骨架、可交付性 | 语言运行时细节、直接复制的架构假设 |
| 官方 AngelScript 2.38 | 语言/runtime 基线 | 新特性、bugfix、升级候选、API 对照 | 整体升级替换当前 deeply customized fork |

## 当前仓库的吸收策略

- 先判断问题属于“UE 集成”“插件工程化”还是“语言/runtime”
- 再决定参考 Hazelight、UnrealCSharp 或官方 AngelScript 哪条线
- 最后回到本地插件边界判断是否值得吸收，以及吸收到哪个层级

## 小结

- 三类参考源解决的是不同问题，不应混用
- 对照表的作用是帮助后续专题选择正确参考入口
