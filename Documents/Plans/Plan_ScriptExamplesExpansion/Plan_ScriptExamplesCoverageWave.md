# Script 参考案例首波 Coverage 执行包

## 背景与目标

当前主 Plan 需要一份能把首波交付资产、覆盖矩阵与后续修订入口放在一起的执行包；本波次按用户要求，先把需要交付的 `.as` 代码放进伴侣目录，而不是先提升到 `Script/Examples/`。

本执行包聚焦第一波最应该先落地的四类参考案例：`Actor`、`Component`、`UObject` 与属性声明，并要求它们由真实磁盘文件驱动测试，而不是继续停留在测试内联字符串里。

## 本波次交付物

- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_Actor.as`
- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_Component.as`
- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_UObject.as`
- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_PropertySpecifiers.as`
- `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`
- `Script/Examples/README.md`

## 覆盖矩阵

| 主题 | 真实资产 | 验证点 |
| --- | --- | --- |
| Actor | `Example_Coverage_Actor.as` | 默认值、`BeginPlay`、`UFUNCTION`、默认语句 |
| Component | `Example_Coverage_Component.as` | `BeginPlay`、`Tick`、宿主 Actor 访问 |
| UObject | `Example_Coverage_UObject.as` | `UObject` 生成、默认值、`UFUNCTION` |
| PropertySpecifiers | `Example_Coverage_PropertySpecifiers.as` | `DefaultComponent`、`RootComponent`、`Attach`、`Category`、`NotEditable`、`EditConst`、`BlueprintReadOnly`、`EditDefaultsOnly`、常用元数据 |

## 执行项

- [x] **CW1** 新增首波 Coverage 示例资产并固定当前交付落点到伴侣目录 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`
  - 这一步把“真实磁盘示例”从主 Plan 的抽象目标推进为已存在的第一批样本，并先满足“交付代码进入伴侣目录”的要求。
  - 本波次刻意只覆盖 `Actor`、`Component`、`UObject` 与属性声明，先把最核心的脚本类/反射表面稳定下来，再继续向 `Subsystem`、`Networking` 等主题扩展。
- [x] **CW1** 📦 Git 提交：`[Docs/Plans] Feat: add first-wave coverage companion assets`

- [x] **CW2** 新增基于真实文件的综合覆盖测试文件
  - 新测试文件要求直接读取伴侣目录里的 `.as` 资产，避免继续复制一份内联脚本文本。
  - 四个自动化 case 共同验证 Actor、Component、UObject、属性声明四大面，作为首波真实资产的 smoke + 行为基线。
- [x] **CW2** 📦 Git 提交：`[Test/Examples] Test: add file-backed script example coverage tests`

- [ ] **CW3** 后续扩充第二波 Coverage 示例
  - 在当前基线稳定后继续追加 `Interface`、`Subsystem`、`ConsoleWorkflow`、`Networking` 等更接近插件差异化能力的主题。
  - 这一步仍要求真实文件优先，并与 `Plan_ScriptExamplesExpansion.md` 的 Phase 3/4 对齐。
- [ ] **CW3** 📦 Git 提交：`[Script/Examples] Feat: extend second-wave coverage examples`
