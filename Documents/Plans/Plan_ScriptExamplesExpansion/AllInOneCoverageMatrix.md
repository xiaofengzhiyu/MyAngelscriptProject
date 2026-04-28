# All-In-One Script Coverage Matrix

## 目标

本矩阵用于约束 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_AllInOne.as` 的首版范围，防止 giant example 在实现时退化为“看起来很大、实际覆盖失衡”的单文件堆叠。

当前矩阵只冻结**首版必须覆盖**、**可以延后**与**当前分支明确不应伪装支持**三类项；后续实现与验证都应以这里为对照面。

## 1. 属性声明矩阵

| 主题 | 首版状态 | 代表项 | 说明 |
| --- | --- | --- | --- |
| 基础数值类型 | 必须覆盖 | `bool`、`int8/16/32/64`、`uint8/16/32/64`、`float`、`double` | 对齐 `ASTestActor` 的 benchmark 主矩阵 |
| UE 常见值类型 | 必须覆盖 | `FString`、`FName`、`FText`、`FVector` | 兼顾反射与基准章节复用 |
| 复合反射类型 | 必须覆盖 | `UENUM`、`USTRUCT`、对象引用、类引用 | 用于函数参数与运行期行为联动 |
| 容器类型 | 必须覆盖 | `TArray<int32>`、`TSet<int32>`、`TMap<int32, int32>` | 对齐 `ASTestActor` 的容器访问模式 |
| 默认组件层级 | 必须覆盖 | `DefaultComponent`、`RootComponent`、`Attach` | 当前仓库已有 file-backed 正向证据 |
| 可见性/编辑性 | 必须覆盖 | `BlueprintReadOnly`、`EditDefaultsOnly`、`NotEditable`、`EditConst` | 代表常用 property flags；示例命名优先采用英文后缀，例如 `bBoolValue_BlueprintReadOnly`、`FloatValue_EditDefaultsOnly`、`VectorValue_MakeEditWidget` |
| 分类与元数据 | 必须覆盖 | `Category`、`ClampMin/Max`、`UIMin/Max`、`EditCondition`、`InlineEditConditionToggle`、`MakeEditWidget` | 当前首波 coverage 已有正向证据 |
| 高级说明符 | 延后验证 | `ExposeOnSpawn`、`SaveGame`、`Transient`、`Instanced`、`AssetRegistrySearchable`、`AdvancedDisplay` | 当前仓库还缺首波 file-backed 证据，不在首版承诺 |

## 2. 函数与 UFUNCTION 矩阵

| 主题 | 首版状态 | 代表项 | 说明 |
| --- | --- | --- | --- |
| 基础函数形态 | 必须覆盖 | 无参、单参、多参、返回值 | Giant example 的基础调用骨架 |
| 输出参数 | 必须覆盖 | `FVector&out` | 当前示例测试已有正向证据 |
| 参数类型矩阵 | 必须覆盖 | 基础数值、对象、结构体、容器 | 与属性矩阵共用类型集合 |
| 函数暴露形态 | 必须覆盖 | 全局函数、成员函数、script-only helper | 当前首版先把本地已有证据的调用形态收齐，再决定是否引入静态函数样例 |
| 基础 UFUNCTION | 必须覆盖 | 裸 `UFUNCTION()` | 当前最基础脚本暴露面 |
| 说明符组合 | 必须覆盖 | `BlueprintPure`、`BlueprintEvent`、`BlueprintOverride`、`Category`、`NotBlueprintCallable`、`CallInEditor` | 当前测试已有内联脚本正向证据 |
| 组合案例 | 必须覆盖 | `BlueprintPure + BlueprintEvent + Category` | 体现“排列组合”而非平铺 |
| 网络类说明符 | 延后验证 | `Server`、`Client`、`NetMulticast`、`Reliable`、`Unreliable` | 当前波次不把未收口网络路径写进首版 giant example |
| 高风险函数说明符 | 延后验证 | `Exec`、`BlueprintAuthorityOnly` | 需先补当前分支证据再进入正向示例 |

## 3. 行为与反射矩阵

| 主题 | 首版状态 | 代表项 | 说明 |
| --- | --- | --- | --- |
| Actor 生命周期 | 必须覆盖 | `BeginPlay`、`Tick`、`EndPlay` | 作为运行期主路径 |
| Component 生命周期 | 必须覆盖 | `UAngelscriptComponent`、owner 访问、tick 计数 | 当前仓库已有场景测试证据 |
| Delegate / Event | 必须覆盖 | `delegate`、`event`、`BindUFunction`、`AddUFunction`、`Execute`、`Broadcast` | 当前仓库是强项，应在 giant example 中集中体现 |
| Interface | 必须覆盖 | `UINTERFACE` 声明、单实现、`ImplementsInterface` 检查 | 当前仓库已有较完整正向证据 |
| 基础 replication default | 必须覆盖 | `default bReplicates = true`、标签/默认值透传 | 当前已有 coverage actor 正向证据 |
| Blueprint 子类友好性 | 可选补充 | 保留可继承的 `BlueprintEvent` / `BlueprintOverride` 入口 | 首版可先通过注释和结构体现 |
| World/GameInstance Subsystem | 明确排除 | 不做正向示例 | 当前分支仍是负例边界 |

## 4. Benchmark 矩阵（吸收自 ASTestActor）

| 主题 | 首版状态 | 代表项 | 说明 |
| --- | --- | --- | --- |
| 框架骨架 | 必须覆盖 | `StartTest`、`EndTest`、`Names`、`Checksums`、CSV 内容拼装 | 保留 benchmark 的结果结构 |
| 空/算术函数 | 必须覆盖 | Empty、Add、Subtract、Multiply、Divide | 最基础调用开销案例 |
| 属性访问 | 必须覆盖 | direct set/get for common fields | 对齐外部参考的核心价值 |
| getter / setter | 必须覆盖 | 常见字段函数式读写 | 与 direct access 形成对照 |
| 全局/成员函数 | 必须覆盖 | global call / member call | 当前本地已有正向证据，且与 `ASTestActor` 的调用对照目标一致 |
| 容器元素访问 | 必须覆盖 | array element / map element | 当前脚本侧很有代表性 |
| 时间断言 | 明确不做 | 不比较具体秒数 | 自动化只校验结构与样本存在 |

## 5. 首版结构要求

1. 单文件，但必须分区：类型声明区、属性矩阵区、函数矩阵区、行为区、benchmark 区、已知限制区。
2. 所有 supporting types 使用统一前缀，避免与未来正式示例或测试模块符号冲突。
3. benchmark 区优先复用前面已经声明的属性和函数，不要另起第二套平行字段。
4. 若实现中发现某个候选项缺少当前分支证据，应从 giant example 正文回退，并在本矩阵中补充“延期原因”。
5. 属性命名要显式表达覆盖意图；优先使用英文后缀描述 specifier 或使用场景，例如 `_Default`、`_BlueprintReadOnly`、`_NotEditable`、`_EditConst`、`_EditDefaultsOnly`、`_InlineEditConditionToggle`、`_MakeEditWidget`。

## 6. 当前已知排除项

- `UScriptWorldSubsystem` 正向示例
- `UScriptGameInstanceSubsystem` 正向示例
- 全量 RPC / 网络说明符矩阵
- 未有当前分支正向证据的高级属性说明符
- 依赖额外插件闭环的 GAS / Editor 主题
