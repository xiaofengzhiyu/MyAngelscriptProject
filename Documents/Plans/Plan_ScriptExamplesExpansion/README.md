# Plan_ScriptExamplesExpansion 伴侣目录

本目录是 `Documents/Plans/Plan_ScriptExamplesExpansion.md` 的伴侣资料目录。

## 用途

- 保存当前活跃 Plan 的分波次执行文档。
- 保存首波需要交付的真实 `.as` 资产、覆盖矩阵与后续增量修订材料。
- 保存全能单文件 giant example 的专项执行包与 companion 覆盖矩阵，避免把第二波“总入口资产”混回首波四个小型 coverage 文件。
- 当前阶段把伴侣目录作为首波交付源，等资产稳定后再决定是否提升到仓库正式示例入口。

## 与真实资产的关系

- 当前首波真实 AngelScript 示例资产位于 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`。
- 第二波全能 giant example 计划见 `Documents/Plans/Plan_ScriptExamplesExpansion/Plan_ScriptExampleAllInOneWave.md`。
- 第二波全能 giant example 的真实资产已经落于 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_AllInOne.as`。
- `Script/Examples/` 当前只保留长期正式入口说明。
- 首波 file-backed 自动化验证入口位于 `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`。
- 全能 giant example 的自动化验证方案暂时只保留在 plan 中；等 plan 阶段结束后，再决定是否落地到 `Plugins/Angelscript/Source/AngelscriptTest/Examples/`。
