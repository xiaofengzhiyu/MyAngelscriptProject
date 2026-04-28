# CodeCoverage 与脚本覆盖率统计链路

> **所属模块**: 运行时支撑子系统 → CodeCoverage / Coverage Pipeline
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/LineCoverage.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp`, `Documents/Guides/Test.md`

覆盖率这条线的关键不在“能不能统计 hit count”，而在于它已经被做成了一条完整的工程化链：**从模块可执行行映射，到运行时 hit 采集，再到 HTML/JSON 报告和自动化测试框架接缝。**

## 3.6.1 覆盖率数据采集入口

- `FAngelscriptCodeCoverage` 是总控对象
- `CoverageEnabled()` 根据设置或 `-as-enable-code-coverage` 命令行参数决定是否启用
- `MapExecutableLines()` 负责先扫描模块，建立“哪些行可执行”的基线
- `HitLine()` 在执行过程中递增具体行的命中次数

这意味着覆盖率采集不是盲记日志，而是建立在脚本函数可执行行映射之上的结构化统计。

## 3.6.2 覆盖率聚合与输出格式

- `FLineCoverage` 保存单文件命中数据
- 报告生成器会产出带标注的 HTML 源码视图和 JSON 摘要
- 输出面不只是“有无覆盖”，还会给出目录级、文件级和总览级的覆盖统计

## 3.6.3 覆盖率能力与测试/验证体系的关系

- Editor-only 测试框架钩子会在自动化测试开始/结束时接入 recording 和 report 输出
- 这说明覆盖率不是独立工具，而是当前测试体系的补充观测面
- 它的价值不是替代断言，而是帮助定位哪些脚本路径尚未被现有专题测试覆盖

## 当前链路最值得记住的点

- CodeCoverage 是插件级能力，不是外部脚本工具附加件
- 行映射、命中采集、报告输出和测试钩子共同组成一条完整链路
- 覆盖率和测试体系是互补关系：前者给覆盖广度，后者给行为语义保证

## 小结

- 当前覆盖率子系统已经具备采集、聚合、可视化和测试集成四个环节
- 它帮助仓库把“脚本执行过哪些路径”沉淀成可回归、可观察的数据产物
- 在插件工程化视角里，它属于验证基础设施的一部分
