# 测试覆盖率系统性扩展计划

## 背景与目标

### 背景

当前仓库 `Plugins/Angelscript/Source/AngelscriptTest/` 已积累约 140 个测试 `.cpp` 文件，覆盖了引擎核心执行、内部实现、热重载、Actor/组件场景、接口、脚本类生成等主题。但对照 `AngelscriptRuntime` 的 8 个功能子目录、125 个 Bind 文件、20 个 FunctionLibrary 和 `AngelscriptEditor` 模块，仍有大量功能域存在零覆盖或严重不足：

| 功能域 | 源码规模 | 当前覆盖 |
|--------|---------|---------|
| Binds/（125 个 `Bind_*.cpp`） | 数学/变换/输入/世界/JSON/UI/资产等 | ~20%（仅 13 个测试文件） |
| FunctionLibraries/（20 个 `.h`） | Actor/Component/World/Math/Tag/Widget 等库 | ~5%（间接触及） |
| GAS 绑定（8+ 源文件） | AbilitySystem/AttributeSet/GameplayEffect 等 | 0% |
| StaticJIT/（14 个源文件） | JIT 编译/预编译数据/字节码持久化 | ~10%（仅 1 个 Runtime/Tests 文件） |
| Serialization | UnversionedPropertySerialization | 0% |
| Editor 模块（25 个源文件） | ClassReloadHelper/ContentBrowser/DirectoryWatcher/菜单 | ~5%（仅 1 个导航测试） |
| Network | FakeNetDriver 已建立但未使用 | 0% |
| Commandlets | TestCommandlet/AllScriptRootsCommandlet | 0% |
| Docs 生成 | AngelscriptDocs | 0% |

### 与已有计划的关系

- `Plan_AngelscriptUnitTestExpansion.md`：偏组织结构、分层、命名与分组策略。本计划在其框架下，聚焦**逐域补齐具体测试用例**。
- `Plan_ASDebuggerUnitTest.md`：已覆盖 Debug Server 测试规划，本计划不重复，仅在验收标准中引用其进度。
- `Plan_AngelscriptTestScenarioExpansion.md`：偏模板与角度矩阵，本计划补齐的测试将按其模板规范落地。

### 目标

1. 将 Bind 类型测试覆盖率从 ~20% 提升到 ~70%（覆盖所有高频使用的 UE 类型绑定）
2. 为 GAS、StaticJIT、Serialization 建立首批专项测试基线
3. 将 Editor 模块测试从 1 个扩展到覆盖 ClassReload/DirectoryWatcher/ContentBrowser 核心路径
4. 为 FunctionLibrary 建立代表性覆盖
5. 所有新测试遵循现有分层与命名约定，文件控制在 300–500 行

## 范围与边界

### 在范围内

- `Plugins/Angelscript/Source/AngelscriptTest/` 下新增测试文件
- `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增低层 CppTests
- `Documents/Guides/TestCatalog.md` 同步更新
- 测试基础设施 `Shared/` 下必要的 helper 补充

### 不在范围内

- Debug Server 测试（由 `Plan_ASDebuggerUnitTest.md` 管辖）
- 运行时或编辑器模块的功能修改（本计划只增测试，不改实现）
- GPU / 视觉回归测试
- 外部测试框架引入
- ThirdParty AngelScript 源码修改

## 分阶段执行计划

### Phase 1：高频 Bind 类型覆盖

> 目标：为最常用的 UE 类型绑定建立编译 + 运行兼容性测试，覆盖数学/变换/碰撞/世界/字符串/文本等核心 Bind。

- [ ] **P1.1** 新增 `Bindings/AngelscriptTransformBindingsTests.cpp`
  - 当前 `Bind_FTransform.cpp` 和 `Bind_FTransform3f.cpp` 提供了 `FTransform` 的脚本绑定（构造/乘法/逆/位置变换/插值等），但没有任何专项测试
  - 覆盖场景：构造（Identity/组合参数）、`TransformPosition`/`InverseTransformPosition`、乘法（`Transform * Transform`）、`Inverse`、`Blend`/`Interp`、`GetLocation`/`GetRotation`/`GetScale3D` 读写、`FTransform3f` 与 `FTransform` 对称操作
  - 参考 `AngelscriptEngineBindingsTests.cpp` 的"脚本编译 + 执行 + 返回值断言"模式，使用 `GetOrCreateSharedCloneEngine()` + `BuildModule` + `ExecuteIntFunction`
- [ ] **P1.1** 📦 Git 提交：`[Test] Feat: FTransform and FTransform3f binding compatibility tests`

- [ ] **P1.2** 新增 `Bindings/AngelscriptRotatorAndQuatBindingsTests.cpp`
  - `Bind_FRotator.cpp`/`Bind_FRotator3f.cpp` 和 `Bind_FQuat.cpp`/`Bind_FQuat4f.cpp` 各提供了旋转相关绑定，当前零测试
  - 覆盖场景：构造、`RotateVector`/`UnrotateVector`、`GetForwardVector`/`GetRightVector`/`GetUpVector`、`Euler`↔`Quat` 转换、`Slerp`/`Lerp`、`IsNearlyZero`、`Normalize`、乘法组合、`FRotator3f`/`FQuat4f` 对称验证
  - 单文件两组测试：Rotator 系列 + Quat 系列，每组 3–5 个用例
- [ ] **P1.2** 📦 Git 提交：`[Test] Feat: FRotator and FQuat binding compatibility tests`

- [ ] **P1.3** 新增 `Bindings/AngelscriptVectorExtendedBindingsTests.cpp`
  - 现有 `AngelscriptEngineBindingsTests.cpp` 中 `ValueTypes` 已轻度触及 `FVector`，但 `FVector4`/`FVector4f`/`FVector3f`/`FVector2f`/`FIntVector`/`FIntVector2`/`FIntVector4`/`FIntPoint` 的专项绑定完全未测
  - 覆盖场景：每种向量类型的构造、算术运算（加减乘除/点积/叉积）、`Size`/`SizeSquared`/`Normalize`、类型间转换（如 `FVector` → `FVector3f`）、`FIntVector` 整型运算
  - 按类型分组，每组 2–3 个用例，合计约 8–10 个用例
- [ ] **P1.3** 📦 Git 提交：`[Test] Feat: extended vector type binding compatibility tests`

- [ ] **P1.4** 新增 `Bindings/AngelscriptColorAndTextBindingsTests.cpp`
  - `Bind_FLinearColor.cpp`、`Bind_FColor.cpp`、`Bind_FText.cpp` 当前零测试
  - 覆盖场景：`FLinearColor` 构造/RGBA 分量/运算/预定义常量、`FColor` 构造/`ToHex`/`FromHex`/与 `FLinearColor` 转换、`FText::FromString`/`ToString`/`IsEmpty`/`Format`/比较
  - 单文件三组测试（LinearColor + Color + Text）
- [ ] **P1.4** 📦 Git 提交：`[Test] Feat: FLinearColor, FColor, and FText binding tests`

- [ ] **P1.5** 新增 `Bindings/AngelscriptWorldAndSubsystemBindingsTests.cpp`
  - `Bind_UWorld.cpp`、`Bind_Subsystems.cpp`、`Bind_UGameInstance.cpp`、`Bind_ULocalPlayer.cpp` 是脚本开发中最常调用的绑定之一，但当前无专项测试
  - 覆盖场景：`UWorld` 的 `GetTimeSeconds`/`GetDeltaSeconds`/`GetWorldSettings`/`SpawnActor` 编译兼容、Subsystem 的 `GetSubsystem<>` 脚本语法、`UGameInstance` 的 `GetEngine`/`GetFirstLocalPlayerController` 编译兼容
  - 部分测试需要 World Context（使用 Template_WorldTick 模式），部分可用纯编译测试
- [ ] **P1.5** 📦 Git 提交：`[Test] Feat: UWorld, subsystem, and game instance binding tests`

- [ ] **P1.6** 新增 `Bindings/AngelscriptCollisionBindingsTests.cpp`
  - `Bind_FCollisionQueryParams.cpp`、`Bind_FCollisionShape.cpp`、`Bind_FBodyInstance.cpp`、`Bind_WorldCollision.cpp`、`Bind_CollisionProfile.cpp`、`Bind_FOverlapResult.cpp` 构成碰撞查询子系统绑定，零测试
  - 覆盖场景：`FCollisionQueryParams` 构造/`AddIgnoredActor`/`AddIgnoredComponent`、`FCollisionShape` 工厂方法（`MakeSphere`/`MakeBox`/`MakeCapsule`）、`FOverlapResult` 字段读取、`ECollisionChannel` 枚举编译
  - 碰撞查询本身需要世界，但参数对象的构造和设置可以纯脚本测试
- [ ] **P1.6** 📦 Git 提交：`[Test] Feat: collision query parameter and shape binding tests`

- [ ] **P1.7** 新增 `Bindings/AngelscriptJsonBindingsTests.cpp`
  - `Bind_Json.cpp` 和 `Bind_JsonObjectConverter.cpp` 提供 JSON 解析/序列化绑定，零测试
  - 覆盖场景：`FJsonObjectWrapper` 的 `Parse`/`Stringify`、字段读写（`GetStringField`/`SetNumberField`/`GetArrayField`）、`FJsonObjectConverter::UStructToJsonObjectString` 编译兼容
  - JSON 操作不需要世界上下文，全部使用纯编译+执行模式
- [ ] **P1.7** 📦 Git 提交：`[Test] Feat: JSON parsing and serialization binding tests`

- [ ] **P1.8** 新增 `Bindings/AngelscriptInputBindingsTests.cpp`
  - `Bind_InputEvents.cpp`、`Bind_UInputSettings.cpp`、`Bind_UEnhancedInputComponent.cpp`、`Bind_FInputActionValue.cpp`、`Bind_FInputBindingHandle.cpp`、`Bind_FInputActionKeyMapping.cpp` 覆盖整个输入子系统绑定，零测试
  - 覆盖场景：`FInputActionValue` 构造/`GetMagnitude`/类型转换、`FInputActionKeyMapping` 构造/字段读写、`FKey` 构造/`IsValid`/`GetDisplayName` 编译兼容
  - 需要注意 Enhanced Input 模块可能在 NullRHI 下部分不可用，仅测试类型和值对象层面
- [ ] **P1.8** 📦 Git 提交：`[Test] Feat: input system type binding tests`

- [ ] **P1.9** 新增 `Bindings/AngelscriptAssetAndDataBindingsTests.cpp`
  - `Bind_AssetRegistry.cpp`、`Bind_UDataTable.cpp`、`Bind_UAssetManager.cpp`、`Bind_SoftObjectPath.cpp`、`Bind_UPackage.cpp` 覆盖资产管理绑定，零测试
  - 覆盖场景：`FSoftObjectPath` 构造/`ToString`/`IsValid`/比较、`UDataTable` 编译兼容（`FindRow` 语法）、`UAssetManager` 的 `GetPrimaryAssetPath` 编译兼容
  - `SoftObjectPath` 已在 `AngelscriptObjectBindingsTests.cpp` 有 `SoftObjectPtrCompat`，本文件聚焦路径级操作和资产管理 API
- [ ] **P1.9** 📦 Git 提交：`[Test] Feat: asset registry, data table, and soft path binding tests`

- [ ] **P1.10** 新增 `Bindings/AngelscriptWidgetBindingsTests.cpp`
  - `Bind_UUserWidget.cpp`、`Bind_FGeometry.cpp`、`Bind_FMargin.cpp`、`Bind_FAnchors.cpp` 覆盖 UMG 层绑定，零测试
  - 覆盖场景：`FMargin` 构造/字段读写/运算、`FAnchors` 预设值（`FAnchors::Minimum`/`Anchors(0.5, 0.5)`）、`FGeometry` 编译兼容
  - UMG widget 实例化在 NullRHI 下受限，优先测试值类型和编译兼容性
- [ ] **P1.10** 📦 Git 提交：`[Test] Feat: UMG widget value type binding tests`

### Phase 2：GAS 绑定与 FunctionLibrary 测试

> 目标：为 Gameplay Ability System 绑定和运行时 FunctionLibrary 建立首批测试基线。

- [ ] **P2.1** 新增 `Bindings/AngelscriptGASCoreBindingsTests.cpp`
  - `Core/` 下 `AngelscriptAbilitySystemComponent.h/.cpp`、`AngelscriptAttributeSet.h/.cpp`、`AngelscriptGASAbility.h/.cpp` 及 `Binds/Bind_FGameplayAttribute.cpp`、`Bind_FGameplayEffectSpec.cpp`、`Bind_FGameplayAbilitySpec.cpp`、`Bind_AngelscriptGASLibrary.cpp` 构成完整的 GAS 脚本层，但当前零测试
  - 覆盖场景：`FGameplayAttribute` 构造/`GetNumericValue`/`IsValid` 编译兼容、`FGameplayEffectSpec` 和 `FGameplayAbilitySpec` 的构造/字段读取编译兼容、`UAngelscriptAbilitySystemComponent` 类可在脚本中声明和引用
  - GAS 依赖 `GameplayAbilities` 模块，需确认 `AngelscriptTest.Build.cs` 已有该依赖；如无则先添加
- [ ] **P2.1** 📦 Git 提交：`[Test] Feat: GAS core type binding compilation tests`

- [ ] **P2.2** 新增 `Bindings/AngelscriptGASActorBindingsTests.cpp`
  - `AngelscriptGASPawn.h`、`AngelscriptGASCharacter.h`、`AngelscriptGASActor.h` 提供了 GAS-ready 的基类，零测试
  - 覆盖场景：脚本中继承 `AAngelscriptGASPawn`/`AAngelscriptGASCharacter`/`AAngelscriptGASActor` 编译通过、从子类访问 `AbilitySystemComponent` 属性、调用 `GetAbilitySystemComponent()` 返回非 null（需 World 场景测试）
  - 使用 WorldTick 模板，在测试世界中 Spawn GAS Actor 并验证 ASC 初始化
- [ ] **P2.2** 📦 Git 提交：`[Test] Feat: GAS actor subclass and ASC initialization tests`

- [ ] **P2.3** 新增 `Bindings/AngelscriptGameplayTagExtendedBindingsTests.cpp`
  - 现有 `AngelscriptGameplayTagBindingsTests.cpp` 覆盖了 Tag/Container/Query 的基础兼容，但 `Binds/Bind_FGameplayTagBlueprintPropertyMap.cpp` 和 `FunctionLibraries/` 下的 `GameplayTagMixinLibrary.h`、`GameplayTagContainerMixinLibrary.h`、`GameplayTagQueryMixinLibrary.h` 未被覆盖
  - 覆盖场景：Mixin 方法的脚本调用（如 `Container.HasTag(Tag)`、`Query.Matches(Container)`）、`FGameplayTagBlueprintPropertyMap` 编译兼容
  - 与已有 GameplayTag 测试互补，不重复基础部分
- [ ] **P2.3** 📦 Git 提交：`[Test] Feat: GameplayTag mixin library and property map tests`

- [ ] **P2.4** 新增 `Bindings/AngelscriptFunctionLibraryBindingsTests.cpp`
  - `FunctionLibraries/` 下 20 个 `.h` 提供了面向脚本的 `UBlueprintFunctionLibrary` 风格 API，但无专项测试
  - 覆盖场景：选取 6–8 个有代表性的库函数做编译+执行验证：
    - `AngelscriptMathLibrary`：`GetDistanceTo`/`GetHorizontalDistanceTo`
    - `AngelscriptWorldLibrary`：`GetWorldDeltaSeconds`/`GetTimeSeconds`
    - `AngelscriptHitResultLibrary`：`GetActor`/`GetComponent`/`GetImpactNormal` 字段读取
    - `SubsystemLibrary`：`GetWorldSubsystem`/`GetGameInstanceSubsystem` 语法
    - `SoftReferenceStatics`：`MakeSoftObjectPath`/`IsValid`
    - `AngelscriptActorLibrary`/`AngelscriptComponentLibrary`：基础方法编译兼容
  - 采用"多个小 `BuildModule` + 逐一断言"结构，每个库 1–2 个用例
- [ ] **P2.4** 📦 Git 提交：`[Test] Feat: representative FunctionLibrary binding tests`

### Phase 3：StaticJIT 与 Serialization 测试

> 目标：为预编译数据管线和属性序列化建立回归测试，保障 cooked build 关键路径。

- [ ] **P3.1** 新增 `AngelscriptRuntime/Tests/AngelscriptStaticJITTests.cpp`
  - 当前 `AngelscriptStaticJIT.h/.cpp` 实现了 `asIJITCompiler` 接口、`FJITDatabase`、`FStaticJITContext`，仅有 `AngelscriptPrecompiledDataTests.cpp` 覆盖了 `PrecompiledData` 的 flag roundtrip，JIT 编译器核心路径零测试
  - 覆盖场景：
    - `FAngelscriptStaticJIT` 的 `CompileFunction` / `ReleaseJITFunction` 基础调用
    - `FJITDatabase` 的 `RegisterModule` / `FindModule` / `GetFunctionEntry` 数据管理
    - 编译一个简单脚本模块后验证 JIT 数据生成非空
  - 使用 `CreateForTesting(Clone)` 获取引擎，避免与生产引擎冲突
- [ ] **P3.1** 📦 Git 提交：`[Test] Feat: StaticJIT compiler and database core tests`

- [ ] **P3.2** 新增 `AngelscriptRuntime/Tests/AngelscriptPrecompiledRoundtripTests.cpp`
  - 现有 `AngelscriptPrecompiledDataTests.cpp` 仅测了 EditorOnly flag 和 ModuleDiff，缺少完整的序列化/反序列化往返测试
  - 覆盖场景：
    - 创建含类/函数/属性/枚举的脚本模块 → `FAngelscriptPrecompiledData::Save` → `Load` → 验证类/函数/属性/枚举数量和名称一致
    - 截断的序列化数据 → `Load` 应失败而不 crash
    - 空模块的 Save/Load roundtrip 应成功且数据为空
  - 需要访问 `PrecompiledData.h` 和 `as_module.h` 内部头文件，属于 `CppTests` 层
- [ ] **P3.2** 📦 Git 提交：`[Test] Feat: precompiled data serialization roundtrip tests`

- [ ] **P3.3** 新增 `AngelscriptRuntime/Tests/AngelscriptSerializationTests.cpp`
  - `Core/UnversionedPropertySerialization.h/.cpp` 已有 `UnversionedPropertySerializationTest.h` 声明了测试入口，但 `AngelscriptTest/` 中无对应测试调用
  - 覆盖场景：
    - 基础类型属性（int/float/FString/FName）的序列化/反序列化 roundtrip
    - 嵌套 UStruct 属性的序列化
    - 版本化兼容性：缺少字段 / 多余字段的反序列化不 crash
  - 确认 `UnversionedPropertySerializationTest.h` 现有 API 后决定是在 `Runtime/Tests/` 还是 `AngelscriptTest/` 中实现
- [ ] **P3.3** 📦 Git 提交：`[Test] Feat: unversioned property serialization roundtrip tests`

### Phase 4：Editor 模块测试扩展

> 目标：将 Editor 模块测试从 1 个扩展到覆盖 ClassReload、DirectoryWatcher、ContentBrowser 核心路径。

- [ ] **P4.1** 新增 `Editor/AngelscriptClassReloadHelperTests.cpp`
  - `AngelscriptEditor/HotReload/ClassReloadHelper.h/.cpp` 提供编辑器侧类/枚举/结构体热重载，是开发者日常体验的关键路径，零测试
  - 覆盖场景：
    - 脚本类变更后 `FClassReloadHelper` 正确重建 `UBlueprintGeneratedClass` 的引用
    - 枚举新增/删除值后相关蓝图不 crash
    - 结构体字段变更后 CDO 正确 reinitialize
  - 需要编辑器上下文（`EAutomationTestFlags::EditorContext`），可能需要在 `AngelscriptTest.Build.cs` 中条件引入 `AngelscriptEditor` 依赖
- [ ] **P4.1** 📦 Git 提交：`[Test] Feat: ClassReloadHelper editor regression tests`

- [ ] **P4.2** 新增 `Editor/AngelscriptDirectoryWatcherTests.cpp`
  - `AngelscriptEditor/HotReload/AngelscriptDirectoryWatcherInternal.h/.cpp` 负责监视脚本文件变更并排队编译，Editor 模块内有一个内部测试，但 `AngelscriptTest/` 中无对应覆盖
  - 覆盖场景：
    - 写入新 `.as` 文件后 watcher 排队事件
    - 修改已有 `.as` 文件后排队修改事件
    - 删除 `.as` 文件后排队删除事件
    - 快速连续写入多个文件的 debounce / 合并行为
  - 需要临时目录和文件 IO，使用 `FPlatformFileManager` 创建临时脚本根
- [ ] **P4.2** 📦 Git 提交：`[Test] Feat: DirectoryWatcher event queueing tests`

- [ ] **P4.3** 新增 `Editor/AngelscriptContentBrowserTests.cpp`
  - `UAngelscriptContentBrowserDataSource` 负责在 Content Browser 中暴露脚本资产，零测试
  - 覆盖场景：
    - 数据源正确注册（`IContentBrowserDataModule` 可发现）
    - 脚本文件对应的虚拟资产路径格式正确
    - 过滤规则（仅 `.as` 文件）工作正常
  - 编辑器上下文测试，不需要渲染
- [ ] **P4.3** 📦 Git 提交：`[Test] Feat: ContentBrowserDataSource registration and filtering tests`

- [ ] **P4.4** 新增 `Editor/AngelscriptEditorMenuTests.cpp`
  - `EditorMenuExtensions/` 下 4 个文件（Script/Asset/Actor/Prompts）提供编辑器右键菜单扩展，零测试
  - 覆盖场景：
    - 菜单扩展点正确注册到 UE 菜单系统
    - 各扩展的 `IsVisible` / `CanExecute` 条件在有/无选中脚本资产时正确返回
  - 轻量级测试，验证注册和条件判断，不真正执行菜单操作
- [ ] **P4.4** 📦 Git 提交：`[Test] Feat: editor menu extension registration tests`

### Phase 5：Network、Commandlet 与杂项

> 目标：利用已有的 FakeNetDriver 基础设施建立首批网络测试，补齐 Commandlet 和 Docs 生成的冒烟测试。

- [ ] **P5.1** 新增 `Network/AngelscriptNetworkReplicationTests.cpp`
  - `Testing/Network/FakeNetDriver.h/.cpp` 已提供网络测试替身，但仓库内无任何网络测试
  - 覆盖场景：
    - 脚本 Actor 的 `UPROPERTY(Replicated)` 属性通过 FakeNetDriver 复制到客户端副本
    - `UFUNCTION(Server)` / `UFUNCTION(Client)` 的脚本层 RPC 编译兼容
    - 基本的属性同步往返验证
  - 这是探索性阶段，先确认 FakeNetDriver 可用再扩展；如不可用则记录阻塞并降级为编译兼容测试
- [ ] **P5.1** 📦 Git 提交：`[Test] Feat: network replication and RPC compilation tests`

- [ ] **P5.2** 新增 `AngelscriptRuntime/Tests/AngelscriptCommandletSmokeTests.cpp`
  - `UAngelscriptTestCommandlet` 和 `UAngelscriptAllScriptRootsCommandlet` 在 `Core/` 下定义，零测试
  - 覆盖场景：
    - `UAngelscriptTestCommandlet` 可被 `NewObject` 创建（不 crash）
    - `UAngelscriptAllScriptRootsCommandlet` 可被 `NewObject` 创建
    - 各 commandlet 的 `Main` 方法在空参数下不 crash（空跑冒烟）
  - 纯冒烟测试，不验证业务逻辑，仅保障可实例化和空调用安全
- [ ] **P5.2** 📦 Git 提交：`[Test] Feat: commandlet instantiation smoke tests`

- [ ] **P5.3** 新增 `AngelscriptRuntime/Tests/AngelscriptDocsGenerationTests.cpp`
  - `Core/AngelscriptDocs.h/.cpp` 负责文档生成，零测试
  - 覆盖场景：
    - 对编译后的脚本引擎调用文档生成入口，输出非空
    - 生成的文档包含已注册的类型和函数名
    - 无注册类型时生成空文档而不 crash
  - 确认 `AngelscriptDocs` 的公共 API 后决定测试粒度
- [ ] **P5.3** 📦 Git 提交：`[Test] Feat: documentation generation smoke tests`

- [ ] **P5.4** 新增 `Bindings/AngelscriptMiscBindingsTests.cpp`
  - 收集剩余的中频 Bind 文件做编译兼容测试：`Bind_FTimespan.cpp`、`Bind_FDateTime.cpp`、`Bind_FMessageDialog.cpp`、`Bind_FMemoryReader.cpp`、`Bind_FInstancedStruct.cpp`、`Bind_LandscapeProxy.cpp`、`Bind_FLatentActionInfo.cpp`、`Bind_FStringTableRegistry.cpp`
  - 每个 Bind 做一个最小编译用例（构造 + 一个方法调用），确保绑定注册正确、脚本可编译
  - 不追求深度行为验证，仅作为"能编译能跑"的回归安全网
- [ ] **P5.4** 📦 Git 提交：`[Test] Feat: miscellaneous binding compilation smoke tests`

- [ ] **P5.5** 同步更新 `Documents/Guides/TestCatalog.md`
  - 把所有新增测试文件和测试名加入目录化清单
  - 更新总测试数统计
  - 标注各 Phase 对应的覆盖率变化
- [ ] **P5.5** 📦 Git 提交：`[Docs] Update: TestCatalog with new test coverage entries`

## 验收标准

1. **Phase 1 完成后**：Bind 覆盖文件数从 13 → 23+，新增 ~40–50 个测试用例，覆盖数学/变换/碰撞/世界/JSON/输入/资产/Widget 类型
2. **Phase 2 完成后**：GAS 绑定有 ≥6 个测试用例；FunctionLibrary 有 ≥8 个代表性用例；GameplayTag 扩展用例 ≥3 个
3. **Phase 3 完成后**：StaticJIT 有 ≥5 个核心路径测试；序列化 roundtrip 有 ≥4 个测试（正例 + 异常）
4. **Phase 4 完成后**：Editor 模块测试从 1 个增至 ≥8 个，覆盖 ClassReload/Watcher/ContentBrowser/Menu
5. **Phase 5 完成后**：网络测试 ≥2 个；Commandlet/Docs 冒烟 ≥4 个；杂项 Bind 冒烟 ≥8 个
6. **所有新增测试**在 `NullRHI` 模式下通过（不依赖渲染）
7. **TestCatalog.md** 与源码同步

## 风险与注意事项

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| GAS 模块在 NullRHI 下初始化不完整 | P2 的 GAS 场景测试部分可能失败 | 降级为编译兼容测试；在 World 场景测试前先验证 `UAbilitySystemGlobals` 可用性 |
| Enhanced Input 在 headless 下不可用 | P1.8 输入测试受限 | 仅测试值类型和编译兼容，不测试运行时输入事件 |
| FakeNetDriver 可能未完成 | P5.1 网络测试受阻 | 先评估 FakeNetDriver 现状；如不可用则仅做 RPC 编译兼容测试并记录 TODO |
| Editor 测试需要 `AngelscriptEditor` 模块依赖 | P4 需修改 `AngelscriptTest.Build.cs` | 使用 `#if WITH_EDITOR` 条件编译隔离；Build.cs 中条件添加 Editor 依赖 |
| 部分 Bind 文件内部实现使用 `asCALL_GENERIC`，脚本语法与 C++ API 不完全对应 | 测试脚本语法需要从 Bind 实现推断 | 先用编译兼容（`BuildModule` 成功即算通过）兜底，再逐步加运行断言 |
| 新增测试过多导致编译时间增长 | CI 反馈变慢 | 控制每文件 300–500 行；Phase 间增量提交，及时观察编译时间 |

