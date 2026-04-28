# Script 示例目录

本目录保留为后续正式公开的 Script 示例入口。

## 当前状态

- 当前首波需要交付的 Coverage `.as` 资产先放在 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`。
- 这里暂时只保留长期入口说明，等这批交付资产稳定后，再决定是否同步提升为 `Script/Examples/` 正式示例。

## 维护约束

- 当前波次以 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 为真实交付源，避免与测试内联字符串再次分叉。
- 每个新增示例至少要关联一个自动化验证入口或在对应 Plan 中登记验证策略。
- 需要对外长期公开的稳定案例，后续再从伴侣目录提升到 `Script/Examples/`。

## 当前首波交付示例

- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_Actor.as`：Actor 默认值、`BeginPlay`、`UFUNCTION`、默认语句。
- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_Component.as`：脚本组件 `BeginPlay`、`Tick`、宿主 Actor 访问。
- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_UObject.as`：脚本 `UObject` 默认值与 `UFUNCTION`。
- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_PropertySpecifiers.as`：`DefaultComponent`、`RootComponent`、`Attach` 与常用属性说明符。
