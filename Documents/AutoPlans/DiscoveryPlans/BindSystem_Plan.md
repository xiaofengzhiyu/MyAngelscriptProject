# BindSystem 发现与方案

---

## 发现与方案 (2026-04-08 12:27)

### Issue-1：`FInputActionValue::opMulAssign` 返回值被错误绑定成 by-value，链式赋值语义与 UE 原生不一致

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputActionValue.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Plugins/EnhancedInput/Source/EnhancedInput/Public/InputActionValue.h` |
| 行号 | 36-37；89-99 |
| 问题 | 当前绑定把 `operator*=` 声明成 `FInputActionValue opMulAssign(float32 Scalar)`，并用 `METHODPR_TRIVIAL(FInputActionValue&, FInputActionValue, operator*=, (float))` 绑定原生实现。UE 原生 `FInputActionValue::operator*=` 明确返回 `FInputActionValue&`。同文件上一行 `opAddAssign` 也保持了引用返回，因此这里只有 `opMulAssign` 发生了返回值语义漂移。 |
| 根因 | 手写 binding signature 与 native method pointer 没有一起维护，声明字符串退化成 by-value，而底层成员函数仍是 by-reference。 |
| 影响 | 脚本侧 `Value *= 0.5f` 的原地修改仍会发生，但表达式结果会被当成临时副本而不是左值引用；`(Value *= 0.5f) *= 0.5f` 这类链式写法会与 UE/C++ 语义分叉，后续对返回结果的继续修改不会回写原对象。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `opMulAssign` 的脚本签名改回引用返回，并用回归测试锁住链式赋值语义。 |
| 具体步骤 | 1. 将 `Bind_FInputActionValue.cpp:37` 的声明改为 `FInputActionValue& opMulAssign(float32 Scalar)`。 2. 保留 `METHODPR_TRIVIAL(FInputActionValue&, FInputActionValue, operator*=, (float))` 的 native 指针类型，使声明和实现一致。 3. 新增 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp`，编译并执行脚本片段验证 `(Value *= 0.5f) *= 0.5f` 后原对象值应从 `2.0` 变成 `0.5`，而不是停在 `1.0`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputActionValue.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 主要风险是脚本侧若已经依赖当前错误的 by-value 结果，修复后链式表达式会变成真正修改原值；需要用专项测试明确新旧差异。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与 `AngelscriptTest`；运行新增的 Enhanced Input 绑定测试，确认 `opMulAssign` 支持链式回写且不会退化成临时副本。 |

### Issue-2：`UEnhancedInputComponent` 的四个清理接口被错误声明为 `const`，脚本可通过只读句柄修改绑定状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Plugins/EnhancedInput/Source/EnhancedInput/Public/EnhancedInputComponent.h` |
| 行号 | 14-18；414-422 |
| 问题 | 当前绑定把 `ClearActionEventBindings()`、`ClearActionValueBindings()`、`ClearDebugKeyBindings()`、`ClearActionBindings()` 全部注册成了 `...() const`。但 UE 原生声明里这四个接口都不是 `const`，其中前三个直接 `Reset()` 内部数组，`ClearActionBindings()` 也是显式的非 `const` mutating API。也就是说，脚本层目前把“会清空输入绑定状态”的操作伪装成了只读方法。 |
| 根因 | 手写 bind declaration 时在签名字符串里额外加了 `const`，但没有和 native header 的 const-correctness 一起校对。 |
| 影响 | 任何拿到 `const UEnhancedInputComponent` 的脚本代码，现在都可以合法调用这些清理接口并修改底层绑定数组，破坏了只读句柄的语义边界；后续如果团队继续依赖 `const` 作为“不会改输入状态”的信号，这四个接口会成为隐蔽的状态突变入口。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让脚本签名与 native const-correctness 对齐，并用编译级测试禁止 `const` 组件调用清理接口。 |
| 具体步骤 | 1. 将 `Bind_UEnhancedInputComponent.cpp:14-17` 的四个声明改成无 `const` 版本。 2. 保持 `HasBindings()`、`ShouldFireDelegatesInEditor()` 这类真正只读查询仍然是 `const`，避免把整个类都改成“宽松可写”。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp` 增加两类脚本 smoke：一类验证 `const UEnhancedInputComponent` 只能调用 `HasBindings()` 这类只读接口；另一类验证非 `const` 组件仍可正常调用 `ClearActionBindings()`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 若现有脚本已经依赖 `const` 组件也能清空绑定，修复后会在编译期暴露这些误用；这是预期中的兼容性收紧，需要在变更说明里点明。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增的 Enhanced Input 绑定测试；确认 `const` 组件的清理调用在脚本编译阶段被拒绝，而非 `const` 路径仍然通过。 |

### Issue-3：`FInputDebugKeyBinding::Execute` 绑定到了错误的 owner type，当前源码里的成员指针签名自相矛盾

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`J:/UnrealEngine/UERelease/Engine/Plugins/EnhancedInput/Source/EnhancedInput/Public/EnhancedInputComponent.h` |
| 行号 | 68-72；40-51；207-208，264-265 |
| 问题 | `Bind_FInputDebugKeyBinding_Late` 里这条注册：`METHODPR_TRIVIAL(void, FEnhancedInputActionEventBinding, Execute, (const FInputActionValue&) const)`，把 `FInputDebugKeyBinding` 的 `Execute` 错绑成了 `FEnhancedInputActionEventBinding` 的成员函数。`AngelscriptBinds.h` 中 `METHODPR_TRIVIAL` 使用的是精确成员指针 cast；而 UE 原生头文件表明 `FEnhancedInputActionEventBinding` 只声明了 `Execute(const FInputActionInstance&) const`，`FInputDebugKeyBinding` 才声明 `Execute(const FInputActionValue&) const`。这说明当前源码里的绑定目标类型和参数签名无法同时成立。 |
| 根因 | 这是典型的复制粘贴残留：上一段 `FEnhancedInputActionEventBinding` 的绑定代码被复用到 `FInputDebugKeyBinding` 时，没有把 owner type 和参数签名一起改干净。 |
| 影响 | 这条定义至少会制造两个问题中的一个：要么在重新编译/重构该模块时直接触发成员指针签名错误，要么在后续有人“局部修补”时留下错误 owner type，导致 `FInputDebugKeyBinding.Execute` 的脚本暴露面继续指向错误 native 方法。由于当前仓库没有任何 Enhanced Input bind 测试，这类错误会长期停留在源码层。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `Execute` 绑定改回 `FInputDebugKeyBinding` 自身，并增加一个只要求“方法签名存在即可”的编译型回归测试。 |
| 具体步骤 | 1. 将 `Bind_FInputBindingHandle.cpp:72` 改为 `METHODPR_TRIVIAL(void, FInputDebugKeyBinding, Execute, (const FInputActionValue&) const)`。 2. 顺手复核同文件相邻的 `FEnhancedInputActionEventBinding`、`FEnhancedInputActionValueBinding`、`FInputDebugKeyBinding` 三组 `GetHandle()/Execute()` 绑定，确保没有同类 copy-paste 残留。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp` 增加 compile smoke：`void Run(FInputDebugKeyBinding& Binding, const FInputActionValue& Value) { Binding.Execute(Value); }`，用脚本编译是否成功来锁定方法签名。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险很低，主要是修复后可能顺带暴露同文件其它 Enhanced Input binding 的签名问题；因此步骤 2 需要一次性做相邻 bind 复核。 |
| 前置依赖 | 无 |
| 验证方式 | 重新编译 `AngelscriptRuntime`；运行新增 compile smoke，确认 `FInputDebugKeyBinding.Execute(const FInputActionValue&)` 在脚本侧可被正确解析。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-3 | Defect | 立即修复，先消除错误 owner type / 签名不一致 |
| P1 | Issue-2 | Defect | 紧随其后，收紧 `UEnhancedInputComponent` 的 const 语义 |
| P1 | Issue-1 | Defect | 完成前两项后修复，并用链式赋值测试锁住行为 |

---

## 发现与方案 (2026-04-08 12:34)

### Issue-4：`SpawnActor` 三条入口失去 `VerifySpawnActor` 审核钩子，项目级 spawn policy 被整体绕过

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptCodeModule.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptCodeModule.cpp` |
| 行号 | 166-253；9-21，32-47；42-64；223-328；8-10，31-33；16-26 |
| 问题 | `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 10 已确认 current 分支的 `SpawnActorFromMeta()`、`SpawnActor()`、`SpawnPersistentActor()` 在组装完 `FActorSpawnParameters` 后直接 `return World->SpawnActor(...)`。`FAngelscriptRuntimeModule` 当前只保留了 `GetDynamicSpawnLevel()` 等扩展点，没有 UEAS2 的 `FAngelscriptVerifySpawnActor` / `GetVerifySpawnActor()`；而 UEAS2 对应三条路径会统一执行 `GetVerifySpawnActor().Execute(Params, bPersistent)`，返回 `false` 就提前中止。额外扫描 `Plugins/Angelscript/Source/AngelscriptTest`，对 `SpawnActor(`、`SpawnPersistentActor(`、`FinishSpawningActor(`、`VerifySpawnActor` 都未命中，说明这条回退当前没有自动化守护。 |
| 根因 | runtime module 迁移时只保留了 spawn level 选择扩展点，没有把项目级 spawn 审核 delegate 一并迁入，导致 bind 层失去统一 policy 入口。 |
| 影响 | 任何依赖该钩子实现的项目规则都会静默失效，包括拦截非法 spawn、禁止 persistent spawn、统一修正 `FActorSpawnParameters` 或记录审计信息。表面现象是脚本调用继续成功，但项目级约束已经被整体绕过。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 module 级 `VerifySpawnActor` delegate，并让三条 spawn helper 在真正调用 `UWorld::SpawnActor` 前统一经过它。 |
| 具体步骤 | 1. 在 `AngelscriptRuntimeModule.h` 中补回 `DECLARE_DELEGATE_RetVal_TwoParams(bool, FAngelscriptVerifySpawnActor, FActorSpawnParameters&, bool)` 与 `static FAngelscriptVerifySpawnActor& GetVerifySpawnActor();`，命名与 UEAS2 保持一致，第二个参数继续表示是否 persistent。 2. 在 `AngelscriptRuntimeModule.cpp` 中实现 `GetVerifySpawnActor()`，返回静态 delegate 实例。 3. 在 `Bind_AActor.cpp` 的 `SpawnActorFromMeta()`、`SpawnActor()`、`SpawnPersistentActor()` 里，完成 `Params`/`OverrideLevel` 填充后，统一执行 `if (FAngelscriptRuntimeModule::GetVerifySpawnActor().IsBound() && !FAngelscriptRuntimeModule::GetVerifySpawnActor().Execute(Params, bIsPersistent)) return nullptr;`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 actor spawn 绑定测试，绑定一个测试 delegate，分别验证普通 spawn 和 persistent spawn 在返回 `false` 时都稳定返回 `null`，返回 `true` 时继续走 `World->SpawnActor`。 5. 在 module header 附一行注释，明确该 delegate 负责 spawn 审核/参数修正，不负责 world-context 获取和脚本异常抛出，避免后续职责继续膨胀。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 恢复钩子后，历史上依赖“当前一定会 spawn 成功”的脚本可能开始被项目策略拦截；需要同时覆盖 delegate bound/unbound 两种路径，避免默认行为回归。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增测试；运行 actor spawn 绑定测试，确认 delegate 未绑定时行为保持 current 语义，绑定并返回 `false` 时三条入口都不会生成 Actor。 |

### Issue-5：`GetCurrentWorld()` 与三条 spawn helper 在判空前直接解引用 `GEngine`，无引擎窗口会先崩溃再谈错误处理

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp` |
| 行号 | 33-42；166-235；427，3764，14052-14075 |
| 问题 | `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 74 已确认 `GetCurrentWorld()` 与 `SpawnActorFromMeta()`/`SpawnActor()`/`SpawnPersistentActor()` 都先执行 `GEngine->GetWorldFromContextObject(...)`，再判断 world 是否为空。引擎源码明确把 `GEngine` 定义为 `ENGINE_API UEngine* GEngine = NULL;`，并在退出流程再次写回 `NULL`。因此这里的首个失败点并不是脚本可处理的 `"Invalid World Context"`，而是引擎尚未初始化或正在 teardown 时的裸空指针解引用。额外扫描 `Plugins/Angelscript/Source/AngelscriptTest`，对 `GetCurrentWorld(`、`SpawnActor(`、`SpawnPersistentActor(` 都未命中，当前没有自动化覆盖这条生命周期边界。 |
| 根因 | bind 层默认把 `GEngine` 当作始终可用的全局单例使用，没有像其它生命周期敏感 helper 一样先做引擎实例判空，再进入 world-context 解析。 |
| 影响 | 在 commandlet、编辑器初始化早期、引擎退出阶段或自动化测试的 teardown 窗口里，脚本只要触发这些入口，就会从“脚本错误/返回 null”退化成真实崩溃，且崩溃点远离业务调用栈。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `GEngine`/world-context 解析收口成统一 helper，先做引擎生命周期防护，再决定返回 `null` 还是抛脚本异常。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 新增一个仅供 bind 内部使用的 helper，例如 `Bind_WorldContextHelpers.h`，提供 `bool ResolveCurrentWorldOrThrow(UObject*& OutWorldContext, UWorld*& OutWorld)`：先检查 `GEngine == nullptr`，若为空则 `FAngelscriptEngine::Throw("Engine was null.")` 并返回 `false`；否则再调用 `GetWorldFromContextObject(..., ReturnNull)`，world 为空时继续抛 `"Invalid World Context"`。 2. 让 `Bind_UWorld.cpp` 的 `GetCurrentWorld()` 改用该 helper；失败时返回 `nullptr`，成功时返回解析后的 `UWorld*`。 3. 让 `Bind_AActor.cpp` 的三条 spawn helper 复用同一 helper，并保留对 `WorldContext` 的后续 `OverrideLevel` 推导。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` 增加生命周期测试桩，至少覆盖“engine 为 null / world-context 无效时返回 null 且记录脚本错误”的分支；如果现有测试框架不方便真正清空 `GEngine`，就在 helper 层增加可测试的注入点或 wrapper。 5. 把 `"Engine was null."` 与 `"Invalid World Context"` 区分写入测试断言，避免后续又把两类错误重新混成一个分支。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldContextHelpers.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 引入统一 helper 后，现有调用点的报错文本和失败顺序会发生变化；需要同步更新依赖错误字符串的测试或工具代码。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 world binding 测试；验证 `GEngine` 不可用时返回 `null` 且记录 `"Engine was null."`，world-context 无效时记录 `"Invalid World Context"`，正常窗口下 `GetCurrentWorld()` 与三条 spawn helper 行为不回退。 |

### Issue-6：两个 `FinishSpawningActor` overload 遇到空 actor 直接静默返回，错误处理契约与同文件其它 API 自相矛盾

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` |
| 行号 | 39-148；256-283 |
| 问题 | `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 76 已确认 `FinishSpawningActor(AActor)` 与 `FinishSpawningActor(AActor, const FTransform&)` 在 `Actor == nullptr` 时都直接 `return`，没有任何 `Throw(...)`。但同一文件前面的两个 `GetComponentsByClass` overload 在 `Actor == nullptr`、`ComponentClass == nullptr`、类型不匹配时都会显式抛出脚本错误。也就是说，同一组 actor helper 已经出现“空输入是脚本错误”与“空输入静默吞掉”的双重契约。额外扫描 `Plugins/Angelscript/Source/AngelscriptTest`，对 `FinishSpawningActor(` 未命中，当前没有回归锁住这条行为。 |
| 根因 | `FinishSpawningActor` 只覆盖了“重复完成 deferred spawn”这类误用诊断，没有把最基础的空输入校验纳入与同文件其它 actor API 一致的错误处理框架。 |
| 影响 | 当脚本把失败的 `SpawnActor(...)` 结果、条件分支里的空句柄，或已经失效的 actor 继续传给 `FinishSpawningActor` 时，当前实现会把真实错误吞成“什么都没发生”，调用方很难区分是 spawn 失败、逻辑没走到，还是 finish 被静默跳过。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `FinishSpawningActor` 的空输入处理收紧到与同文件 actor helper 一致的显式抛错语义，并用测试锁住三类误用分支。 |
| 具体步骤 | 1. 将 `Bind_AActor.cpp` 中两个 `FinishSpawningActor` overload 的首个 `if (Actor == nullptr)` 分支都改成 `FAngelscriptEngine::Throw("Actor was null."); return;`，与 `GetComponentsByClass` 的错误文案保持一致。 2. 保留现有 `HasActorBegunPlay()` 检查，继续用于识别“没有 deferred spawn 却调用 FinishSpawningActor”的误用。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorBindingsTests.cpp` 增加三组测试：空 actor 调用必须记录 `"Actor was null."`；对已 BeginPlay 的 actor 调用必须记录当前的 deferred-spawn 错误；对真实 deferred actor 调用必须成功完成 spawning。 4. 若当前测试框架已有通用脚本异常断言 helper，复用该 helper，不再为 actor bind 单独发明一套检查逻辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险主要是兼容性收紧：历史脚本如果把 `FinishSpawningActor(null)` 当成 no-op，修复后会在运行期暴露真实错误；这属于预期中的正确性提升。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 actor binding 测试；确认空 actor 路径不再静默返回，而是稳定抛出 `"Actor was null."`，同时已有的 deferred-spawn 防误用分支继续生效。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-5 | Defect | 立即修复，先消除 `GEngine` 生命周期窗口中的直接崩溃 |
| P1 | Issue-4 | Architecture | 紧随其后，恢复 `VerifySpawnActor` 统一审核链路 |
| P2 | Issue-6 | Defect | 完成前两项后修复，统一 `FinishSpawningActor` 的错误处理契约 |

---

## 发现与方案 (2026-04-08 12:40)

### Issue-7：`TArray::Reserve()` 被错误实现成 `Reset()`，脚本扩容会直接清空已有元素

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Containers/ScriptArray.h` |
| 行号 | 910-928；875-892；154-165 |
| 问题 | current 分支的 `FArrayOperations::Reserve()` 在算出 `ReservedSize` 后调用的是 `Arr.Reset(ReservedSize, ...)`，而不是 `ResizeTo(...)`。`ScriptArray.h` 明确显示 `Reset()` 在 `NewSize <= ArrayMax` 时会直接把 `ArrayNum = 0`，否则走 `Empty(...)`；它不是“保留元素只扩 capacity”的语义。UEAS2 对应实现同一位置使用的是 `Arr.ResizeTo(ReservedSize, ...)`。这意味着脚本对一个非空 `array<T>` 调用 `Reserve()` 时，绑定层会先把逻辑长度清零。 |
| 根因 | `Bind_TArray.cpp` 在迁移时把 `Reserve` 与 `Reset` 混用，函数名仍保留扩容语义，但底层调用已经变成清空数组。 |
| 影响 | 任何依赖 `Reserve()` 预留空间后继续追加数据的脚本，都会在调用点静默丢失已有元素；这不是性能退化，而是实际数据破坏。由于 API 名称仍是 `Reserve`，调用方很难从脚本表面看出数组已被清空。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `Reserve()` 恢复为纯 capacity 调整语义，并用脚本回归测试锁住“保留已有元素”的契约。 |
| 具体步骤 | 1. 将 `Bind_TArray.cpp:926-927` 的 `Arr.Reset(...)` 改回 `Arr.ResizeTo(ReservedSize, Ops->NumBytesPerElement, Ops->Alignment)`。 2. 保留前面的 `if (Arr.Num() > ReservedSize) ReservedSize = Arr.Num();`，确保 `Reserve()` 不会缩小逻辑长度。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` 新增脚本测试：先构造 `array<int> Values = {1, 2, 3};`，调用 `Values.Reserve(16);` 后断言 `Values.Num() == 3` 且三个元素顺序不变，再追加新元素确认扩容后内容完整。 4. 再补一条回归验证 `Reserve(1)` 不会把 `Num()` 从 3 降到 1 或 0，锁住“只扩不缩”的边界。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险主要在于若现有脚本错误地依赖“`Reserve()` 会清空数组”的 current 行为，修复后会暴露这类误用；但该行为本身已经违背 API 名称和 UEAS2 语义，应该被视为修正而不是兼容承诺。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增容器绑定测试；执行 `array<T>.Reserve()` 回归，确认扩容前的元素和 `Num()` 都保持不变，且后续追加元素正常。 |

### Issue-8：`TArray::RemoveSwap()` 漏掉删除计数累加，脚本拿到的返回值永远是 `0`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 1157-1192；1146-1166；`rg -n "RemoveSwap\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | current 分支的 `FArrayOperations::RemoveSwap()` 虽然在循环里真正执行了 `Arr.Remove(...)`，但删除分支只有 `Num--` 和 `i--`，没有任何 `NumRemoved++`，最终直接 `return NumRemoved;`。UEAS2 对应实现同一位置明确在删除后执行 `NumRemoved++`。结果是脚本侧无论实际删掉多少元素，`RemoveSwap()` 的返回值都固定为 `0`。 |
| 根因 | 这是一次典型的移植遗漏：删除逻辑和循环控制被保留下来，但返回值统计语句在 current 分支中丢失。 |
| 影响 | 依赖返回值判断“是否删除成功”或“共删除几个元素”的脚本会稳定走错分支，例如 `if (Values.RemoveSwap(Target) > 0)` 会判成失败，但数组内容其实已经被修改；这是返回值语义与 API 契约的直接分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 补回删除计数累加，并用脚本级回归同时锁住“数组内容变化”和“返回值正确”两条语义。 |
| 具体步骤 | 1. 在 `Bind_TArray.cpp:1186-1190` 的删除分支补上 `NumRemoved++;`，位置与 UEAS2 保持一致，确保每次成功删除都能计入返回值。 2. 顺手复核同文件 `Remove()`、`RemoveSingleSwap()`、`RemoveAtSwap()` 的计数语义，避免只修一个函数而遗漏同类统计错误。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` 新增脚本测试：`array<int> Values = {1, 2, 1, 3}; int Removed = Values.RemoveSwap(1);`，断言 `Removed == 2`、`Values.Num() == 2`，并验证剩余元素集合为 `{2, 3}`。 4. 再补一条未命中路径 `Values.RemoveSwap(99)`，断言返回 `0` 且数组内容不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险很低，主要是修复后会暴露现有脚本中那些为了绕过当前错误返回值而写下的补丁逻辑；这类脚本应按正确语义回归。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增容器绑定测试；确认 `RemoveSwap()` 在删除命中时返回实际删除数量，未命中时返回 `0`，并且数组内容与 `RemoveSwap` 的交换删除语义一致。 |

### Issue-9：`Bind_*.cpp` 文件数不能作为 UEAS2 coverage 基线，current 在 2026-04-08 的本地对比里是“多 11 个分片”而不是“缺 11 个文件”

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SystemTimers.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FGeometry.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_InputEvents.cpp` |
| 行号 | 目录统计：current `123`，UEAS2 `112`，`Compare-Object` 差集仅 current 侧多出 11 个文件；5-40；4-14；6-39；7-35 对 10-58；16-38 对 16-63 |
| 问题 | 本地实际对比显示，current `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 下有 123 个 `Bind_*.cpp`，UEAS2 `Private/Binds` 下只有 112 个；差集不是“UEAS2 有 11 个 current 缺失文件”，而是 current 额外拆出了 11 个文件：`Bind_AngelscriptGASLibrary.cpp`、`Bind_FGameplayAbilitySpec.cpp`、`Bind_FGameplayAttribute.cpp`、`Bind_FGameplayEffectSpec.cpp`、`Bind_FGameplayTagBlueprintPropertyMap.cpp`、`Bind_FGenericPlatformMisc.cpp`、`Bind_FInputActionValue.cpp`、`Bind_FInputBindingHandle.cpp`、`Bind_FunctionLibraryMixins.cpp`、`Bind_SystemTimers.cpp`、`Bind_UEnhancedInputComponent.cpp`。代表文件里可以直接看到 current 已把 Enhanced Input、GAS、Timer 等能力拆成独立分片。但同名文件内部仍存在真实 coverage 缺口，例如 `Bind_FGeometry.cpp` current 只有 `GetLocalSize()/GetAbsoluteSize()/AbsoluteToLocal()/LocalToAbsolute()/MakeChild()`，UEAS2 还包含 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()`、`MakeTransformedChild()`；`Bind_InputEvents.cpp` current 也缺少 UEAS2 已有的 `GetVirtualKey()` 和 `SetPreviousBindNoDiscard(true)` 元数据。 |
| 根因 | BindSystem 的分片策略已经与 UEAS2 演化出不同结构，但当前 discovery 仍把“文件数量/文件名差异”当成 coverage 主指标，导致对比口径落在错误层级。 |
| 影响 | 如果继续按“缺失文件数”组织分析和修复，会优先追逐并不存在的文件级缺口，反而漏掉同名文件内部的方法丢失、元数据回退和签名偏差；同时 current 新增的 11 个分片也缺乏独立 coverage 台账，后续更难判断哪些是新增能力、哪些是 sharding 迁移产物。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 放弃文件数作为 coverage 基线，建立按“类型/函数签名”对齐的符号级清单，并把分片策略显式文档化。 |
| 具体步骤 | 1. 在 `Tools/DiscoveryPlanner/` 新增一个对比脚本，例如 `CompareBindCoverage.ps1`，同时扫描 current 与 UEAS2 两棵 `Bind_*.cpp` 中的 `Method(`、`Property(`、`Constructor(`、`BindGlobalFunction(`、`AddFunctionEntry(` 注册字符串，按“拥有者类型 + 签名 + 来源文件”输出结构化清单。 2. 生成两份产物：`Documents/AutoPlans/BindSystem_Coverage_Current.csv` 与 `Documents/AutoPlans/BindSystem_Coverage_UEAS2.csv`，再产出 `BindSystem_Coverage_Diff.md`，把差异分成 `current-only`、`ueas2-only`、`shared-file-symbol-diff` 三类。 3. 对 current-only 的 11 个文件单独补一张“新增分片责任表”，注明它们是新能力引入还是原文件拆分迁移，例如 Enhanced Input、GAS、Timer。 4. 对 shared-file-symbol-diff 按子系统建优先级 backlog，先处理已经在分析中确认的 `Bind_FGeometry`、`Bind_InputEvents`、`Bind_ConfigEnums` 这类同名文件内部缺口，而不是继续用文件差集做代理指标。 5. 把 `BindSystem_Plan.md` 之后各轮的 coverage 结论统一引用这个符号清单，停止使用“缺失 N 个 Bind 文件”作为描述口径。 |
| 涉及文件 | `Tools/DiscoveryPlanner/CompareBindCoverage.ps1`，`Documents/AutoPlans/BindSystem_Coverage_Current.csv`，`Documents/AutoPlans/BindSystem_Coverage_UEAS2.csv`，`Documents/AutoPlans/BindSystem_Coverage_Diff.md`，`Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` |
| 预估工作量 | M |
| 风险 | 主要风险是脚本扫描注册字符串时会遇到 lambda 包装、宏展开和条件编译，初版清单可能需要人工校正；但这比继续用文件数做错误基线的风险更低。 |
| 前置依赖 | 无 |
| 验证方式 | 运行覆盖对比脚本后，确认输出的 `current-only` 文件数为 11、`ueas2-only` 为 0，并且 `shared-file-symbol-diff` 能明确列出 `Bind_FGeometry`、`Bind_InputEvents` 等已人工核实的符号差异；后续新一轮 discovery 只引用该清单，不再使用文件数口径。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-7 | Defect | 立即修复，先消除 `Reserve()` 导致的静默数据丢失 |
| P1 | Issue-8 | Defect | 紧随其后，修复 `RemoveSwap()` 返回值语义并补回归 |
| P1 | Issue-9 | Architecture | 在高优先级缺陷落地后执行，改掉文件数导向的 coverage 基线 |

---

## 发现与方案 (2026-04-08 12:48)

### Issue-10：对象字符串化/调试值格式化在 3 条链路重复解引用 `UASClass*`，native class 一进入日志或调试面板就可能崩溃

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 222-264；431-470；271-289，319-346；13-30；`rg -n "Generic_AppendToString|GetDebuggerValue|ToString\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 仅命中普通 happy-path，用例里没有任何 native `UObject` 调试字符串回归 |
| 问题 | `Bind_UObject.cpp` 的 `FToStringHelper::Register(TEXT("UObject"), ...)` 先做 `UASClass* asClass = Cast<UASClass>(ObjClass)`，随后直接访问 `(ObjClass->HasAnyClassFlags(CLASS_Native) || asClass->bIsScriptClass) ? asClass->GetPrefixCPP() : TEXT("")`。`Bind_FString.cpp` 的 `FToStringHelper::Generic_AppendToString()` 与 `Bind_BlueprintType.cpp` 的 `GetDebuggerValue()` 复制了同一模式，其中 `Bind_BlueprintType.cpp:345` 还会在 native class 场景下直接取 `asClass->ScriptTypePtr`。而 `ASClass.h` 明确表明 `bIsScriptClass` / `ScriptTypePtr` 只存在于 `UASClass`。因此普通 native `UClass` 一旦进入对象字符串化、格式化输出或 debugger value 展示，首个失败点就是对空 `asClass` 的直接解引用。 |
| 根因 | 对象显示/调试格式化逻辑在多个 Bind 文件里复制粘贴，并把“是否 script class”的判定从 `UClass` 语义错误地下沉成了 `UASClass*` 专属字段访问，没有统一的安全 helper。 |
| 影响 | `Log()` / `Print()` / `FString` 拼接 / 调试器对象展示等横切能力都会共享这条崩溃路径；问题不只局限于 `Bind_UObject.cpp`，而是已经扩散到字符串绑定和 debugger value 绑定两条旁路，后续继续复制该模式还会扩大爆炸半径。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先提取统一的 class-display helper，彻底消除 `UASClass*` 裸解引用，再用 runtime test 直接覆盖 native class 的字符串化和 debugger value 两条路径。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 新增内部 helper（例如 `Bind_ObjectDebugHelpers.h`），集中提供 `const TCHAR* GetSafeDisplayPrefix(UClass* Class)` 与 `asITypeInfo* GetSafeScriptType(UClass* Class)`：native `UClass` 直接走 `Class->GetPrefixCPP()`，只有 `Cast<UASClass>(Class)` 成功时才访问 `bIsScriptClass` / `ScriptTypePtr`。 2. 让 `Bind_UObject.cpp:222-264` 与 `Bind_FString.cpp:431-470` 统一改用该 helper，禁止在 ternary 条件或 true 分支里再次直接引用 `asClass->...`。 3. 让 `Bind_BlueprintType.cpp:271-346` 的 debugger value 逻辑也复用同一 helper；`Value.Type` 的 prefix 生成使用 `UClass` 级安全路径，`ScriptTypePtr` 只在 `UASClass` 非空时读取。 4. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 新增自动化测试，例如 `AngelscriptObjectDebugFormattingTests.cpp`，直接调用 `FToStringHelper::Generic_AppendToString()` 和 `GetDebuggerValue()`，分别覆盖 native `AActor::StaticClass().GetDefaultObject()` 与一个运行时 script class 实例，断言两条路径都不会崩溃且输出包含预期类名。 5. 顺手 grep `Cast<UASClass>(ObjClass)` / `Cast<UASClass>(Class)` 在 BindSystem 内的其余格式化调用点，避免修完 3 个显性入口后还有第四个同类拷贝继续潜伏。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ObjectDebugHelpers.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptObjectDebugFormattingTests.cpp` |
| 预估工作量 | M |
| 风险 | 统一 helper 后，native class 与 script class 的显示前缀可能会轻微变化；需要用测试锁住输出格式，避免“修崩溃时顺手改了调试字符串契约”。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime`；运行新增的 object debug formatting 自动化测试，确认 native `UObject` 参与字符串拼接、`Log/Print` 底层格式化和 debugger value 构建时都不再触发空指针解引用。 |

### Issue-11：`FindClass` / `__StaticClass` 与类清理生命周期脱节，热重载后的 tombstone class 仍可能被脚本重新解析出来

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 行号 | 331-398，519-553；4990-5003；201-213，242-269；39-96，276-417；641-689 |
| 问题 | 当前 `UClass` 命名空间 `FindClass()` 直接 `return FindObject<UClass>(nullptr, *Name);`，全局 `FindClass()` 与 `__StaticClass()` 也都是遇到首个名字命中就返回，没有任何 `CLASS_Deprecated`、`CLASS_NewerVersionExists` 或 tombstone 过滤。相比之下，同文件的 `GetAllClasses()` 至少过滤了 `CLASS_Deprecated | CLASS_NewerVersionExists`，但也没有复用 class generator 的删除判定。`AngelscriptClassGenerator.cpp:4990-5003` 明确显示被移除的 `UASClass` 会被清空 `ScriptTypePtr/ConstructFunction/DefaultsFunction` 并打上 `CLASS_Hidden | CLASS_HideDropDown | CLASS_NotPlaceable`；UEAS2 在 `Bind_UObject.cpp:201-213` 已有 `IsDeletedAngelscriptClass()` 并在 `GetAllSubclassesOf()` 里使用它。当前绑定层却让 lookup API 与 cleanup 机制各走各路。 |
| 根因 | class lookup surface 是按“最小可用名字匹配”逐个补上去的，没有提炼统一的“哪些 class 仍可对脚本公开”的判定函数，因此 lookup、枚举、cleanup 三条链路用了三套不同规则。 |
| 影响 | 在 rename、discard、full reload 或将来更复杂的类替换流程里，脚本用 `FindClass("OldName")`、`__StaticClass("OldName")` 或基于 `GetAllClasses()` 的查找逻辑，仍可能重新拿到已经退出活跃集合的旧类壳；随后调用 `GetDefaultObject()`、查函数或实例化时，失败点会远离真正的 lookup 根因。现有 `AngelscriptClassBindingsTests.cpp` 只覆盖 happy-path lookup，`AngelscriptScriptClassCreationTests.cpp` 也只验证新类可见，没有把旧类是否被 lookup surface 清理掉锁成回归。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“class 是否应该继续暴露给脚本”收口成单一 helper，并让所有 lookup/枚举入口统一走这套规则。 |
| 具体步骤 | 1. 在 `Bind_UObject.cpp` 靠近 `Bind_UClass_Base` 的位置提取一个内部 helper，例如 `static bool ShouldExposeClassToScriptLookup(UClass* Class, bool bIncludeAbstractClasses)`，统一处理 `nullptr`、`CLASS_Deprecated`、`CLASS_NewerVersionExists`、abstract 过滤，以及 editor 下的 tombstone 判定（复用或迁回 UEAS2 的 `IsDeletedAngelscriptClass()` 逻辑）。 2. 让 `UClass::FindClass()`、全局 `FindClass()`、`__StaticClass()`、`GetAllClasses()`、`GetAllSubclassesOf()` 全部改用该 helper；名字命中但 helper 返回 `false` 时继续搜索，而不是立刻把失效类返回给脚本。 3. 若当前仓库仍需要区分“只用于 debug/compat 的旧类可见性”，把这条策略做成 bind 内部私有函数，不向脚本层新增配置面，先保证所有 lookup surface 一致。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 增加 lifecycle 场景：先编译 `ABindingLookupLifecycleOld`，再在同模块里编译 renamed 版本 `ABindingLookupLifecycleNew`，随后执行脚本查询，断言 `FindClass("ABindingLookupLifecycleOld") == null`、`__StaticClass("ABindingLookupLifecycleOld") == null`、`FindClass("ABindingLookupLifecycleNew")` 命中新类。 5. 复用 `AngelscriptScriptClassCreationTests.cpp:641-689` 现有 rename 流程或其 helper，避免再造一套类替换测试基建；必要时再补一条 discard-module 场景，确认被清理模块的类不会从 lookup surface 回流。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 预估工作量 | M |
| 风险 | 修复后，少量历史脚本如果依赖当前“旧类还能被名字查到”的错误行为，会在 lookup 阶段直接暴露问题；这是预期中的兼容性收紧，需要在变更说明里明确。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与相关测试模块；运行新增的 class lookup lifecycle 自动化测试，确认 rename / reload / discard 之后 lookup surface 只返回当前活跃类，不再把旧 class tombstone 回流给脚本。 |

### Issue-12：`AActor` 丢失 UEAS2 的 `GetComponentsByClassWithTag` overload，继续补 coverage 会先撞上重复校验逻辑

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 39-149；151-205；151-167 |
| 问题 | current `Bind_AActor.cpp` 只保留了两个 `GetComponentsByClass` overload，没有 UEAS2 已有的 `void GetComponentsByClassWithTag(const FName& Tag, ?& OutComponents) const`。对照 UEAS2 可见，缺失的并不是全新能力，而是与现有组件查询完全同一组 API surface 的 tag-filter 变体。更糟的是，current 两个 `GetComponentsByClass` overload 已经复制了整段 `TypeId -> TArray element type -> UActorComponent subclass` 校验逻辑；如果按当前写法直接把 tag overload 补回来，这段 30 多行的错误检查会再复制第三遍。现有 `AngelscriptNativeEngineBindingsTests.cpp` 也只覆盖了 `GetComponentsByClass(SceneComponents)` 与 `GetComponentsByClass(AllComponents)` 的 happy-path，没有任何 tagged overload 回归。 |
| 根因 | BindSystem 的 actor component 查询接口没有建立统一的 out-array 校验 helper，导致 coverage 扩展只能靠复制 lambda；迁移时于是先把最常用的两个 overload 带过来，把 tag-filter 版本直接落掉。 |
| 影响 | 与 UEAS2 对齐的脚本无法继续使用 `GetComponentsByClassWithTag`，只能手写 `GetComponents()` + `ComponentHasTag()` 过滤，既扩大脚本侧样板代码，也让 BindSystem 的 API coverage 台账继续出现“同族接口只补一半”的状态。若后续继续补回该接口而不先抽 helper，actor bind 的重复校验逻辑会进一步膨胀。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把组件数组类型校验提炼成内部 helper，再按 UEAS2 语义补回 `GetComponentsByClassWithTag`，一次同时解决 coverage 和重复实现。 |
| 具体步骤 | 1. 在 `Bind_AActor.cpp` 顶部附近提取一个内部 helper，例如 `static UClass* ResolveActorComponentArraySubtypeOrThrow(int TypeId)`，把两处重复的 `TypeId` / `templateBaseType` / `plainUserData` / `IsChildOf<UActorComponent>()` 校验全部收进去，并保留现有错误文案。 2. 让当前两个 `GetComponentsByClass` overload 改用该 helper，避免继续复制校验分支。 3. 参照 UEAS2 `Bind_AActor.cpp:154-202` 补回 `void GetComponentsByClassWithTag(const FName& Tag, ?& OutComponents) const`，内部流程复用新 helper，然后在组件遍历时同时判断 `Comp->ComponentHasTag(Tag)` 与 `Comp->IsA(SubClass)`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` 扩展现有 native engine 绑定脚本：给 `ScriptScene` 组件加一个显式 tag，新增 `GetOwner().GetComponentsByClassWithTag(n"ScriptTag", TaggedComponents);` 断言命中 1 个组件，再补一个 miss-tag 断言 `Num() == 0`。 5. 若团队仍在追踪 actor 旧泛型 API 的迁移问题，补接口后顺手在 coverage 台账里把 `GetComponentsByClassWithTag` 记为已恢复，避免后续 discovery 重复把它当成缺口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`，`Documents/AutoPlans/BindSystem_Coverage_Diff.md` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是抽 helper 时如果改动了现有错误文案，可能影响依赖精确字符串的脚本或测试；因此应保持现有报错文本不变。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与 `AngelscriptTest`；运行扩展后的 native engine 绑定测试，确认 `GetComponentsByClassWithTag` 在命中 / 未命中 tag 时都返回正确结果，且原有两个 `GetComponentsByClass` overload 行为不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-10 | Defect | 立即修复，先收口对象字符串化/调试值链路里的空指针解引用 |
| P1 | Issue-11 | Defect | 紧随其后，统一 class lookup 与 cleanup 生命周期规则 |
| P2 | Issue-12 | Architecture | 在高优先级缺陷落地后补齐 actor component tag 查询 coverage，并顺手抽公共校验 helper |

---

## 发现与方案 (2026-04-08 13:01)

### Issue-13：`CopyScriptPropertiesFrom` 把 `asIScriptObject` 赋值原语直接暴露给全部 `UObject`，空句柄与 native 对象都会落入未定义行为

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptobject.cpp` |
| 行号 | 128-132；230-237；714-719，896-904 |
| 问题 | `CopyScriptPropertiesFrom(const UObject OtherObject)` 当前实现只有一行 `*(asIScriptObject*)Object = *(asIScriptObject*)OtherObject;`。同仓库 `AngelscriptEngine.h` 只是把 `UObject*` / `asIScriptObject*` 做裸 `reinterpret_cast` 风格转换，没有任何“这真的是 live script object”的保护；而 AngelScript 自身提供的安全入口 `asIScriptObject::CopyFrom()` 明确会先检查 `other == 0` 和 `GetTypeId()` 是否一致。也就是说，当前 bind 把“只适用于 script object 且应做类型校验的内部原语”挂到了所有 `UObject` 上，却跳过了 null/type 检查。 |
| 根因 | 手写 bind 直接复用了最低层的赋值运算符，而没有先把 `UObject` surface 收紧到 script-class 场景，也没有使用 AngelScript 已经提供的 `CopyFrom()` 错误码路径。 |
| 影响 | 只要脚本把 `null`、native `UObject`、或不同 script class 的实例传进来，这条路径就可能在错误类型的地址上执行脚本对象拷贝，首个结果不是稳定的脚本异常，而是未定义行为、崩溃或半拷贝状态。当前 `Plugins/Angelscript/Source/AngelscriptTest/` 对 `CopyScriptPropertiesFrom` 零覆盖，这个崩溃面没有自动化护栏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把该 helper 收紧成“仅 live script object 可用”的显式校验路径，并改用 `CopyFrom()` 返回码做失败诊断。 |
| 具体步骤 | 1. 在 `Bind_UObject.cpp` 为 `CopyScriptPropertiesFrom` 增加前置校验：`Object == nullptr` 或 `OtherObject == nullptr` 时统一 `Throw("Object was null.")` / `Throw("OtherObject was null.")` 并返回。 2. 继续校验 `Cast<UASClass>(Object->GetClass())` 与 `Cast<UASClass>(OtherObject->GetClass())` 都成功，且两边 `ScriptTypePtr` 非空；任一条件不满足时抛出明确错误，例如 `CopyScriptPropertiesFrom requires live script objects of the same type.`。 3. 将赋值实现改成 `FAngelscriptEngine::UObjectToAngelscript(Object)->CopyFrom(FAngelscriptEngine::UObjectToAngelscript(const_cast<UObject*>(OtherObject)))`；对 `asINVALID_TYPE`、`asINVALID_ARG` 等返回码分别抛出精确错误，禁止继续执行裸赋值。 4. 若团队仍需要“同父类不同 script type 的部分属性复制”，单独新增受控 helper，不要继续复用 `CopyScriptPropertiesFrom` 这个全量赋值入口。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 `AngelscriptObjectBindingsTests.cpp`，覆盖同类 script object copy 成功、null source/dest 抛错、native object 抛错、不同 script class 抛错四条回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 修复后，少量历史脚本如果依赖当前“错误对象也能侥幸复制”的未定义行为，会在运行时立即暴露为显式脚本错误；这是预期中的兼容性收紧。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 object binding 测试；确认同类 script object 可以复制，null/native/mismatched type 路径都会抛出稳定错误，而不会崩溃或静默写坏目标对象。 |

### Issue-14：失效 `UASFunction` 仍可被 `FindFunctionByName` 找到，但真正执行时只会静默返回

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 303-314；149-156，475-482，979-985，1971-1975；4990-5016 |
| 问题 | `Bind_UClass_Base` 把 `FindFunctionByName()` 直接暴露给脚本，而 `UASClass::IsFunctionImplementedInScript()` 只要 `FindFunctionByName()` 命中 `UASFunction` 就返回 true。与此同时，`CleanupRemovedClass()` 在类被移除/替换时并不移除旧函数对象，只是把 `Function->ScriptFunction = nullptr`；后续统一调用入口 `AngelscriptCallFromBPVM()` / `AngelscriptCallFromParms()` 遇到 `ScriptFunction == nullptr` 时又直接 `return`。结果是脚本仍然能发现这类 tombstone function，但真正调用时不会报错，只会静默什么都不做。 |
| 根因 | 绑定层把“函数对象仍在 UObject 树上”误当成“脚本实现仍有效”，而运行时执行层则把“脚本函数指针已空”当成可忽略条件吞掉，没有统一的 live-function 判定。 |
| 影响 | 在 rename、reload、discard 之后，脚本反射层会进入危险的假阳性状态：`Type.IsFunctionImplementedInScript()` 报 true，`Type.FindFunctionByName()` 也能拿到函数句柄，但调用该句柄时却静默 no-op。调用方很难分辨这是逻辑没执行、热重载后的旧句柄，还是函数本身返回了默认结果。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一定义“live script function”判定，同时修正 bind introspection 和 runtime invoke 两条路径。 |
| 具体步骤 | 1. 在 `ASClass.cpp` 提取共享 helper，例如 `static bool IsLiveScriptFunction(const UFunction* Function)`：对 native `UFunction` 返回 true；对 `UASFunction` 则要求 `ScriptFunction != nullptr`。 2. 让 `UASClass::IsFunctionImplementedInScript()` 改用该 helper，只在 `UASFunction` 且 `ScriptFunction` 仍有效时返回 true。 3. 让 `Bind_UObject.cpp` 的 `FindFunctionByName()` 包装也走同一 helper；命中 tombstone `UASFunction` 时返回 `nullptr`，避免把失效句柄继续暴露给脚本。 4. 在 `ASClass.cpp` 的 `AngelscriptCallFromBPVM()` / `AngelscriptCallFromParms()` 中，把 `if (ASFunction->ScriptFunction == nullptr) return;` 改成统一诊断分支，至少记录 `"Attempted to call stale Angelscript function <Class>::<Name>"` 级别的脚本错误，再返回，禁止继续 silent failure。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` 扩展现有 rename/recompile 场景：保留旧类指针，重编译后验证旧类 `IsFunctionImplementedInScript()` 为 false、`FindFunctionByName()` 不再回传有效 script function；再补一条 stale call 断言，确认它会记录显式错误而不是静默通过。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 预估工作量 | M |
| 风险 | 兼容性风险在于：历史脚本如果碰巧依赖当前的 silent no-op，会在修复后开始收到显式错误；但这正是需要暴露出来的错误状态。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与扩展后的 script-class lifecycle 测试；确认 rename/reload/discard 后旧函数不再被 introspection 当成 live script implementation，且 stale function 调用会记录错误而不是静默返回。 |

### Issue-15：`__Actor_GetAllByClass` 作为内部 helper 暴露在公开 bind 面上，却绕过了当前 actor 查询的全部参数校验

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 317-408；1176-1184 |
| 问题 | 同文件公开的 `GetAllActorsOfClass(?& OutActors)` 与 `GetAllActorsOfClass(UClass Class, ?& OutActors)` 都会完整校验 `TypeId`、`TArray` 元素类型、`ActorClass == nullptr` 和 `IsChildOf()`，然后才调用 `UGameplayStatics`。但紧接着公开注册的 `__Actor_GetAllByClass(UClass Class, ?& OutActors)` 只有一行 `UGameplayStatics::GetAllActorsOfClass(...)`，完全跳过这些验证。更糟的是，预处理器里唯一会生成它的 `GetAll()` wrapper 已经整段注释掉，说明当前分支没有正常 codegen 入口在消费这条 API，它却仍然对脚本公开。 |
| 根因 | 一条原本面向代码生成的内部 escape hatch 在演进过程中失去了调用方，却没有从 bind surface 清理掉，也没有和公开 helper 共用参数校验逻辑。 |
| 影响 | 当前脚本如果直接调用这条 `__` helper，就能绕过 actor 查询公开接口已有的错误处理和类型保护；后续维护者也更容易误以为“所有 actor 查询都走了同一套验证”，但实际上还留着一个未测试的裸入口。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 关闭这条公开后门；如果未来仍需要内部 helper，就把它改成真正的内部协议并复用公开验证路径。 |
| 具体步骤 | 1. 在 `Bind_AActor.cpp` 删除 `__Actor_GetAllByClass` 的公开注册；若确认仍有内部需求，则改名为 `__Internal_Actor_GetAllByClass` 并只保留给生成代码使用。 2. 在同文件提取一个共享校验 helper，例如 `static bool ResolveActorArraySubtypeOrThrow(int TypeId, UClass*& OutArraySubClass)`，让所有 actor 查询入口共用同一套 `TypeId` / `IsChildOf<AActor>()` / null 检查，避免以后再出现“公开路径有校验、内部路径裸奔”的分叉。 3. 仅在 preprocessor 真正需要该 helper 时，恢复 `AngelscriptPreprocessor.cpp` 的生成代码，并让它调用新的 `__Internal_` 名称；若继续保持注释状态，就同步删除死代码注释，避免误导。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` 或新建专门的 actor-query 绑定测试，增加 compile smoke：用户脚本不应再解析到 `__Actor_GetAllByClass`，而公开 `GetAllActorsOfClass(...)` 仍能正常编译并返回结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果仓库外部有脚本私自依赖这条 `__` helper，删除或改名后会在编译期暴露；但这正是内部协议泄露需要被纠正的部分。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与相关绑定测试；确认用户脚本无法再直接调用 `__Actor_GetAllByClass`，并且公开 `GetAllActorsOfClass(...)` 两个 overload 的行为、报错文本和结果集都不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-13 | Defect | 立即修复，先把 `CopyScriptPropertiesFrom` 从未定义行为改成显式校验路径 |
| P1 | Issue-14 | Defect | 紧随其后，统一失效 `UASFunction` 的发现与调用语义，消除 silent no-op |
| P2 | Issue-15 | Architecture | 在高优先级缺陷落地后清理 `__Actor_GetAllByClass` 后门并抽共享校验 helper |

---

## 发现与方案 (2026-04-08 13:09)

### Issue-16：`TArray::Reserve()` 实际调用 `Reset()` 清空数组，返回的容量扩展语义与脚本 API 名称完全相反

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Containers/ScriptArray.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 行号 | 910-928；876-892；154-165；162-220 |
| 问题 | 对应 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 81，current `FArrayOperations::Reserve()` 在算出 `ReservedSize` 后执行的是 `Arr.Reset(...)`，而不是 UEAS2 仍在使用的 `Arr.ResizeTo(...)`。引擎 `FScriptArray::Reset()` 明确会在 `NewSize <= ArrayMax` 时把 `ArrayNum = 0`，因此脚本侧 `Values.Reserve(16)` 的真实效果不是“保留元素并扩容”，而是直接把逻辑长度清零。现有 `FAngelscriptTArrayMutationCompatBindingsTest` 只覆盖 `Add/Insert/Remove/Reset`，没有任何 `Reserve()` 回归来锁住这个语义。 |
| 根因 | `Bind_TArray.cpp` 迁移时把 reserve helper 从 UEAS2 的 `ResizeTo` 误改成了 `Reset`，但脚本 surface、函数名和调用方预期仍然保留“只调 capacity、不丢元素”的 contract。 |
| 影响 | 任何在已有数据上调用 `Reserve()` 的脚本都会静默丢失数组内容；更糟的是，当 `ReservedSize <= ArrayMax` 时这条路径只是把 `ArrayNum` 归零，调用方既看不到异常，也不一定能立刻意识到对象生命周期和元素析构语义已经被破坏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `Reserve()` 恢复成纯 capacity 调整语义，并把“reserve 后元素必须完整保留”补进现有 `TArray` 绑定回归。 |
| 具体步骤 | 1. 将 `Bind_TArray.cpp:926-927` 的实现改回 `Arr.ResizeTo(ReservedSize, Ops->NumBytesPerElement, Ops->Alignment)`，不要再调用 `Reset()`。 2. 复核 `Reserve()` 前后的 `InvalidateReferencesToArray()` 路径，确认修复后仍保持现有 reference debugging 语义，不额外引入悬空引用。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` 的 `FAngelscriptTArrayMutationCompatBindingsTest` 中扩展脚本片段：先构造非空 `int[] Values = {1,2,3}`，执行 `Values.Reserve(16)` 后断言 `Values.Num() == 3` 且元素顺序不变，再校验 `Values.Max() >= 16`。 4. 追加一条“reserve 小于当前 Num”用例，验证 `Values.Reserve(1)` 不会裁剪现有元素，只保持 `Num()` 不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果仓库外脚本已经错误地把 `Reserve()` 当成“快速清空数组”使用，修复后会暴露出这类误用；但这是对错误行为的去兼容，应该通过测试和变更说明显式收口。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与 `AngelscriptTest`；运行 `Angelscript.TestModule.Bindings.TArrayMutationCompat`，确认 `Reserve()` 之后元素和 `Num()` 保持不变，而 `Max()` 按预期增长。 |

### Issue-17：`Bind_UWorld.cpp` 把 `WorldType` 与 `OwningGameInstance` 的写入口直接暴露给脚本，绑定层越权改写 engine-owned 世界状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/World.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/World.cpp` |
| 行号 | 73-87；256-264；575-585；1230-1231，4264-4272；7925-7932，9177-9189 |
| 问题 | 对应 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 77 与 78，current `Bind_UWorld.cpp` 既公开了 `void SetGameInstance(UGameInstance NewGI)`，又用默认 `Property(...)` 重载直接把 `WorldType` 暴露成可写 property。bind core 明确表明：不传 `FBindParams` 的 `RegisterObjectProperty(...)` 会按默认读写权限注册，而 `FBindParams` 本身已经支持 `bCanWrite/bCanEdit` 收紧访问。与此同时，引擎 `World.h` 显示 `SetGameInstance()` 只是对 `OwningGameInstance` 的直接赋值，`WorldType` 是世界上下文判定字段；`World.cpp` 中 `GetTimerManager()` / `GetLatentActionManager()` 与 `IsGameWorld()` / `IsEditorWorld()` / `IsPreviewWorld()` 都直接依赖这两个成员。 |
| 根因 | BindSystem 在选择 `UWorld` 暴露面时，没有把“只读查询 API”和“引擎生命周期/所有权状态接线点”分层处理，导致脚本层拿到了原本只应由 engine/runtime core 维护的可写开关。 |
| 影响 | 脚本可以在运行时重写世界类型和 owning game instance，进而污染 world 上下文判定、timer/latent manager 路由以及依赖 `IsGameWorld()` / `GetGameInstance()` 的后续流程。当前 `Plugins/Angelscript/Source/AngelscriptTest/Bindings` 与 `.../Angelscript` 下对 `WorldType`、`SetGameInstance()` 都没有命中，说明这条高风险写入口没有自动化护栏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 `UWorld` 的 engine-owned 状态改成只读公开或内部专用接口，把脚本可见 surface 收紧到“观察世界状态”而不是“改写世界接线”。 |
| 具体步骤 | 1. 将 `Bind_UWorld.cpp:87` 的 `WorldType` 绑定改成显式只读注册：构造 `FAngelscriptType::FBindParams Params; Params.bCanRead = true; Params.bCanWrite = false; Params.bCanEdit = false;`，再用带 `BindParams` 的 `Property(...)` 重载注册该字段。 2. 从公开 bind surface 删除 `SetGameInstance()`；如果极少数内部脚本/生成代码确实需要它，就改名为 `__Internal_SetGameInstance` 并在同文件旁边加注释，明确这是 runtime-owned escape hatch，而不是常规 gameplay API。 3. 保留 `GetGameInstance()`、`IsGameWorld()`、`IsEditorWorld()` 等只读查询，必要时补一个 `EWorldType GetWorldType() const` 方法，避免调用方因为移除可写 property 而失去读取路径。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增或扩展 `AngelscriptWorldBindingsTests.cpp`：验证 `GetCurrentWorld().WorldType` 仍可读取但不能赋值，用户脚本无法再解析 `SetGameInstance(...)`，而 `GetGameInstance()` / `IsGameWorld()` 等只读查询保持可用。 5. 同步检查是否还有其它 `UWorld`/`UGameInstance` 级别的 engine-owned setter 被按普通 gameplay 方法暴露，避免修掉一个入口后同类状态仍从别的 bind 写入。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果现有项目脚本已经依赖直接重写 `WorldType` 或 `SetGameInstance()`，修复后会在编译期暴露这类强耦合用法；需要提前与 gameplay/工具链使用方确认是否存在内部 escape hatch 依赖。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 world bind 测试；确认 `WorldType` 只读、`SetGameInstance()` 不再可被普通脚本调用，而读取 world 状态的既有脚本保持通过。 |

### Issue-18：BindSystem 的 UEAS2 coverage 审计仍按“缺失文件数”思考，但 2026-04-08 实际源码快照已经是 `0 missing / 11 extra`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputActionValue.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp` |
| 行号 | N/A（目录扫描）；8；5-12；5 |
| 问题 | 本轮对两个源码目录做同名 `Bind_*.cpp` 扫描后，结果是 current `123` 个、UEAS2 `112` 个；`MissingSameName=0`，`ExtraSameName=11`。也就是说，按文件名维度 current 已经完整覆盖 UEAS2 的全部 `112` 个 bind shard，并额外新增了 `Bind_AngelscriptGASLibrary.cpp`、`Bind_FGameplayAbilitySpec.cpp`、`Bind_FGameplayAttribute.cpp`、`Bind_FGameplayEffectSpec.cpp`、`Bind_FGameplayTagBlueprintPropertyMap.cpp`、`Bind_FGenericPlatformMisc.cpp`、`Bind_FInputActionValue.cpp`、`Bind_FInputBindingHandle.cpp`、`Bind_FunctionLibraryMixins.cpp`、`Bind_SystemTimers.cpp`、`Bind_UEnhancedInputComponent.cpp` 这 `11` 个 current-only shard。代表性 extra shard 的入口行也证实它们不是空壳文件，而是实际注册 bind 的新域。 |
| 根因 | BindSystem 早已从“纯粹移植 UEAS2 shard”演进成“UEAS2 parity + 本地扩展域”双来源结构，但 discovery/coverage 讨论仍在沿用按文件数量推 gap 的旧口径，缺少按符号和按域分层的审计矩阵。 |
| 影响 | 如果后续规划继续围绕“缺失 11 个文件”推进，就会优先追逐一个已经不存在的文件级缺口，同时低估 current-only shard 的真实维护成本。更直接地说，当前计划里已经落下的 `Issue-1/2/3` 就都来自 current-only 的 Enhanced Input shard，说明真正的风险不在“少文件”，而在“新 shard 没有被纳入统一 coverage/test/parity 台账”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 放弃文件数驱动的 coverage 口径，改成“共享 shard 做符号级 parity、current-only shard 做域内 owner/test 台账”的双矩阵审计。 |
| 具体步骤 | 1. 在 `Tools/DiscoveryPlanner/` 下新增一个轻量 diff 脚本，例如 `CompareBindCoverage.ps1`，扫描 current 与 UEAS2 两侧 `Bind_*.cpp` 中的 `Method(`、`GlobalFunction(`、`Property(`、`GlobalVariable(`、`Constructor/Destructor/Factory` 注册字符串，生成“同名 shard 的符号差异表”。 2. 以本轮目录扫描结果为初始基线，新增 `Documents/AutoPlans/BindSystem_Coverage_Diff.md`：第一部分列出 `112` 个 shared shard 的符号差异，第二部分单独列出 `11` 个 current-only shard，并为每个 shard 记录所属域（EnhancedInput、GAS、SystemTimers、Mixins、PlatformMisc）、测试落点和 owner。 3. 对 `Bind_FInputActionValue.cpp`、`Bind_FInputBindingHandle.cpp`、`Bind_UEnhancedInputComponent.cpp` 这三个已经出现实质缺陷的 current-only shard，优先补 dedicated bind tests；然后再扩展到 GAS 和 timer/mixin 域，避免继续由 discovery 文档代替 coverage 台账。 4. 后续 DiscoveryPlanner 轮次不再使用“缺失文件数”作为优先级输入，而是直接引用 `BindSystem_Coverage_Diff.md` 的符号差异和 test owner 缺口，确保新发现能回到明确的 shard/domain owner。 |
| 涉及文件 | `Tools/DiscoveryPlanner/CompareBindCoverage.ps1`，`Documents/AutoPlans/BindSystem_Coverage_Diff.md`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 脚本化 diff 的签名规范化如果做得太粗，会把重载或 helper 宏展开差异误判成新增/缺失；需要先在 2-3 个 shard 上人工抽样校准，再推广到全部 `112` 个 shared shard。 |
| 前置依赖 | 无 |
| 验证方式 | 运行新 diff 脚本后，输出必须稳定得出 `MissingSameName=0`、`ExtraSameName=11` 的文件级基线，并产出可人工复核的符号差异表；随后从中抽查 Enhanced Input / GAS 各 1 个 shard，确认台账与源码注册字符串一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-16 | Defect | 立即修复，先把 `TArray::Reserve()` 从清空数组改回纯扩容语义 |
| P1 | Issue-17 | Architecture | 紧随其后，收紧 `UWorld` 的 engine-owned 写入口，切断 world 状态泄漏 |
| P2 | Issue-18 | Architecture | 在高优先级缺陷落地后建立符号级 coverage diff，替换过时的文件数口径 |

### 补充说明（去重校正）

| 项目 | 内容 |
|------|------|
| 去重结论 | `Issue-16` 与 `2026-04-08 12:40` 的 `Issue-7` 指向同一 `TArray::Reserve()` 根因，不应重复排期；执行时以 `Issue-7` 为主，`Issue-16` 里补充的 `AngelscriptEngineBindingsTests.cpp` 测试落点可作为备选实现方式。 |
| 去重结论 | `Issue-18` 与 `2026-04-08 12:40` 的 `Issue-9` 指向同一 coverage 基线问题，不应重复统计；执行时以 `Issue-9` 为主，`Issue-18` 里“current-only 11 个 shard 的 owner/test 台账”步骤可并入 `Issue-9` 的实施计划。 |
| 本轮真正新增 | 本轮唯一未与前文重复的新增发现是 `Issue-17`，即 `UWorld` 的 engine-owned 写入口泄漏问题。 |

---

## 发现与方案 (2026-04-08 13:18)

### Issue-19：`FinishSpawningActor` 用 `HasActorBegunPlay()` 代理 spawn 完成态，普通 actor 在 BeginPlay 前仍会被二次完成生成

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/Actor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 256-283；595-596，3116；4331-4356；`rg -n "FinishSpawningActor\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | `Bind_AActor.cpp` 里的两个 `FinishSpawningActor` overload 只在 `Actor->HasActorBegunPlay()` 为真时才拒绝继续执行，然后直接再次调用 `Actor->FinishSpawning(...)`。但引擎原生 `SpawnActor` 在 `bDeferConstruction == false` 时会于 `Actor.cpp:4331-4333` 立刻执行一次 `FinishSpawning(UserSpawnTransform, true)`；真正的二次调用保护则在 `Actor.cpp:4354-4356` 的 `ensure(!bHasFinishedSpawning)`，对应状态位是 `Actor.h:595-596` 的私有字段 `bHasFinishedSpawning`。这意味着“已经完成 spawn 但尚未 BeginPlay”的普通 actor 仍会穿过当前 bind 的检查。 |
| 根因 | 绑定层把 gameplay 生命周期信号 `HasActorBegunPlay()` 误当成 deferred-spawn 完成态，而引擎内部真正的门禁是 `bHasFinishedSpawning`。 |
| 影响 | 脚本一旦把非 deferred spawn 得到的 actor 再传给 `FinishSpawningActor`，不会在 bind 边界被稳定拒绝，而是跌进引擎内部 `ensure(!bHasFinishedSpawning)` 路径。错误从可诊断的脚本 API 误用，退化成更底层的 ensure/重复构造问题。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 不再猜测引擎私有状态，改由 bind 层显式跟踪“哪些 actor 是通过 Angelscript deferred spawn 产生且尚未完成”的集合。 |
| 具体步骤 | 1. 在 `Bind_Actor.h` 与 `Bind_AActor.cpp` 为 `FAngelscriptActorBinds` 增加一个内部跟踪容器，例如 `static TSet<TWeakObjectPtr<AActor>> PendingDeferredSpawns`，并提供 `RegisterDeferredSpawn(AActor*)`、`ConsumeDeferredSpawnOrThrow(AActor*)` 两个私有 helper。 2. 在 `SpawnActor()` 与 `SpawnPersistentActor()` 中，仅当 `bDeferredSpawn == true` 且 `World->SpawnActor(...)` 返回非空时，把 actor 加入 `PendingDeferredSpawns`；普通 spawn 不登记。 3. 将两个 `FinishSpawningActor` overload 的 `HasActorBegunPlay()` 分支替换为 `ConsumeDeferredSpawnOrThrow(Actor)`：若 actor 不在集合中，就抛出明确错误，例如 `"Actor was not spawned with bDeferredSpawn=true or has already finished spawning."`；命中后先从集合移除，再调用 `Actor->FinishSpawning(...)`。 4. helper 内在查找前顺手清理失效 `TWeakObjectPtr`，避免 deferred actor 被销毁后集合永久积累垃圾项。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorBindingsTests.cpp` 增加三组回归：普通 `SpawnActor(..., bDeferredSpawn=false)` 后调用 `FinishSpawningActor` 必须记录新错误；`SpawnActor(..., bDeferredSpawn=true)` 后第一次 `FinishSpawningActor` 成功、第二次调用必须报错；`SpawnPersistentActor(..., true)` 也走同一集合约束。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Actor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 这是对当前宽松行为的收紧，可能会让历史脚本里“对普通 actor 再调一次 FinishSpawningActor 也没事”的误用暴露出来；同时需要确保 deferred actor 在异常路径或销毁路径下不会长期残留在跟踪集合中。 |
| 前置依赖 | 可与 `Issue-6` 并行，但最终应合并到同一组 `FinishSpawningActor` 回归测试中。 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 actor binding 测试；确认普通 spawn 二次 finish 不再触发底层 ensure，而是在脚本层稳定报错，真实 deferred spawn 仍能正常完成。 |

### Issue-20：两个 `GetComponentsByClass(..., ?& OutComponents)` overload 只追加结果，不会清空复用数组

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/GameplayStatics.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 39-87，89-149；340-352，540-552；1097-1105，1165-1173；151-163 |
| 问题 | `Bind_AActor.cpp` 的两个 `GetComponentsByClass` overload 都是直接遍历组件并 `OutComponents.Add(...)`，全程没有任何 `Reset()/Empty()`。同一插件内，`Bind_UObject.cpp` 的 `GetAllClasses()` 在填充前会先 `OutClasses.Reset()`；引擎原生 `UGameplayStatics::GetAllActorsOfClass()` 与 `GetAllActorsOfClassWithTag()` 也都在进入查询前执行 `OutActors.Reset()`。当前唯一现有回归 `AngelscriptNativeEngineBindingsTests.cpp:151-163` 只覆盖 fresh array happy path，其中 `SceneComponents` 还手动先 `Empty()`，没有锁住“复用已有数组再次查询”的行为。 |
| 根因 | `GetComponentsByClass` 采用了手写循环实现，却没有继承同类 out-array 查询 API 的“输出前清空结果数组”契约。 |
| 影响 | 脚本一旦复用同一个数组多次调用 `GetComponentsByClass`，旧元素会和新查询结果混在一起，轻则重复组件，重则在 actor/component 已变化后继续保留陈旧句柄。因为 `?& OutComponents` 的写法强烈暗示“由 API 重新填充输出”，这种偏差很容易变成隐蔽逻辑错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在所有参数校验通过后、真正枚举组件前统一 `Reset()` 输出数组，并把“数组复用”加入 native engine binding 回归。 |
| 具体步骤 | 1. 在 `Bind_AActor.cpp` 两个 `GetComponentsByClass` overload 里，把 `OutComponents.Reset();` 放到 `Actor` / `ComponentClass` / `SubClass` 校验全部通过之后、组件循环之前，保证错误路径不破坏调用方原数组，而成功路径总是返回全新结果集。 2. 若后续按 `Issue-12` 抽出 `ResolveActorComponentArraySubtypeOrThrow()` helper，可同步再提取一个 `ResetAndCollectActorComponents(...)` 小 helper，避免 tag overload 回补后第三次复制“Reset + 遍历 + Add”模式。 3. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`：在现有 `SceneComponents` / `AllComponents` 场景后，再向数组预塞一个伪旧值或先调用一次查询、再立即重复调用第二次，断言两次结果都稳定为 1 个 `ScriptScene`，而不是累积成 2 个元素。 4. 再补一条 `GetComponentsByClass(UActorComponent::StaticClass(), AllComponents)` 的显式 class overload 复用测试，确保两个 overload 都被锁住。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果外部脚本错误地依赖“查询会向现有数组追加”的 current 行为，修复后会暴露这类误用；但该行为已经与同类 Unreal out-array API 契约相冲突，属于必要收敛。 |
| 前置依赖 | 无；若同时执行 `Issue-12`，可共用同一次 helper 提取。 |
| 验证方式 | 编译 `AngelscriptRuntime` 与扩展后的 native engine binding 测试；确认复用数组连续调用两个 overload 时，结果集不会累计旧元素。 |

### Issue-21：`UObject::ImplementsInterface` 把无效 `UClass` 输入静默压成 `false`，错误语义同时偏离 `IsA` 和 Kismet

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/KismetSystemLibrary.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/` |
| 行号 | 88-106；321-331；175-237；`rg -n "ImplementsInterface\\((AActor::StaticClass\\(|nullptr|null)" Plugins/Angelscript/Source/AngelscriptTest/Interface -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | 同一段 `Bind_UObject.cpp` 里，`IsA(const UClass Class)` 在 `Class == nullptr` 时会 `Throw("Class passed in to IsA was nullptr.")`，但紧接着的 `ImplementsInterface(const UClass InterfaceClass)` 对 `InterfaceClass == nullptr` 只是直接返回 `false`，对“传入的类根本不是 interface”也没有任何校验，最终把所有无效输入都压成普通的“不实现接口”。而引擎 `UKismetSystemLibrary::DoesClassImplementInterface()` 在 `KismetSystemLibrary.cpp:321-331` 明确会先检查 `Interface->IsChildOf(UInterface::StaticClass())`，非法时记录 runtime error。现有接口测试 `AngelscriptInterfaceImplementTests.cpp:175-237` 只覆盖 `UIDamageableImplCheck::StaticClass()` 的 happy path，没有任何空 class 或普通 class 误传回归。 |
| 根因 | 绑定层为了直接复用 `UClass::ImplementsInterface()`，漏掉了脚本 API 本该承担的输入合法性校验与错误报告。 |
| 影响 | 当脚本通过 `FindClass`、配置反射或热重载后的动态 lookup 拿到 `nullptr` 或普通 `UClass` 后，再误传给 `ImplementsInterface`，当前实现不会暴露“参数无效”的事实，只会静默返回 `false`。结果是 lookup 错误、配置错误和真实的“不实现该接口”在脚本侧被压扁成同一种结果，排障信号明显变弱。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `ImplementsInterface` 的无效类参数处理同时对齐同文件 `IsA` 和引擎 Kismet：空指针与非 interface 都显式报错，只把“对象为空”保留为 `false`。 |
| 具体步骤 | 1. 修改 `Bind_UObject.cpp:100-106` 的 lambda：先检查 `InterfaceClass == nullptr`，命中时 `FAngelscriptEngine::Throw("InterfaceClass passed in to ImplementsInterface was nullptr."); return false;`；再检查 `!InterfaceClass->IsChildOf(UInterface::StaticClass())`，命中时抛出明确错误，例如 `"Class passed in to ImplementsInterface was not an interface."`。 2. 保留 `Object == nullptr` 直接返回 `false` 的行为，与 `UKismetSystemLibrary::DoesImplementInterface()` 的空对象语义保持一致，避免把“空对象不实现任何接口”也收紧成异常。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp` 基于现有 `ScenarioInterfaceImplMethod` 场景再加两个负向脚本：一个在 `BeginPlay()` 中调用 `this.ImplementsInterface(cast<UClass>(null))`，一个调用 `this.ImplementsInterface(AActor::StaticClass())`。两条测试都用 `AddExpectedError(...)` 锁住新的错误文本。 4. 保留现有 happy-path `UIDamageableImplCheck::StaticClass()` 断言，确认修复不会影响合法 interface 查询。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp` |
| 预估工作量 | S |
| 风险 | 修复后，历史脚本里把 lookup 失败或普通 class 误传给 `ImplementsInterface` 的代码会开始显式报错；这是预期中的正确性提升，但需要在变更说明里提示“无效参数不再静默返回 false”。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与接口测试；确认合法 interface 仍返回 `true/false`，而 `null` 与非 interface class 两条路径都会产生预期 runtime error。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-19 | Defect | 立即修复，先把 `FinishSpawningActor` 从 `BeginPlay` 代理判断改成显式 deferred-spawn 跟踪 |
| P1 | Issue-21 | Defect | 紧随其后，收紧 `ImplementsInterface` 的无效参数错误语义，消除静默失败 |
| P2 | Issue-20 | Defect | 在高优先级行为问题落地后修复，统一 `GetComponentsByClass` 的 out-array 契约 |

---

## 发现与方案 (2026-04-08 13:35)

### Issue-22：`Bind_WorldCollision.cpp` 的 46 个 trace/overlap helper 共享同一个空 world 解引用入口

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 14-17，146-467；264-300 |
| 问题 | `WorldCollision::GetWorld()` 当前固定走 `GEngine->GetWorldFromContextObject(..., EGetWorldErrorMode::LogAndReturnNull)`；world-context 无效时它会返回 `nullptr`。但同文件后续 `LineTrace*`、`Sweep*`、`Overlap*`、`Component*`、`Async*`、`QueryTraceData`、`QueryOverlapData`、`IsTraceHandleValid` 共 46 处调用都直接执行 `WorldCollision::GetWorld()->...`。现有自动化 `AngelscriptEngineParityTests.cpp:264-300` 只验证这些 API 能编译，不执行无效 world-context 运行时场景。 |
| 根因 | world-context 解析被抽成了共享 helper，但 helper 只负责“写 warning + 返回 null”，调用层没有再把 `nullptr` 收口成脚本错误和稳定失败值。 |
| 影响 | 在 constructor、editor tool、缺失 `FAngelscriptEngineScope`、或测试人工注入无 world `UObject` 的场景下，任一 collision helper 都会从“脚本可处理的失败”退化成 bind 层空指针崩溃，而且影响面覆盖整个 collision surface。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 world 解析升级为“判空 + Throw + 默认失败值”的统一入口，并顺手补上 `RequiresWorldContext` trait。 |
| 具体步骤 | 1. 在 `Bind_WorldCollision.cpp` 提取 `static UWorld* ResolveWorldOrThrow()`：先检查 `GEngine == nullptr`，为空时 `FAngelscriptEngine::Throw("Engine was null.")` 并返回 `nullptr`；否则用 `EGetWorldErrorMode::ReturnNull` 解析 `FAngelscriptEngine::TryGetCurrentWorldContextObject()`，失败时统一 `Throw("Invalid World Context")`。 2. 为 bool-return helper、`FTraceHandle` return helper、以及带 `OutHit` / `OutHits` / `OutOverlaps` / `OutData` 的 helper 各提取一层小型失败收口函数；world 解析失败时返回 `false` 或默认构造的无效 `FTraceHandle`，并把输出参数重置到默认值，禁止继续解引用。 3. 将 `Bind_WorldCollision.cpp:146-467` 的 46 个调用点全部改成先拿 `UWorld* World = ResolveWorldOrThrow()`，确认非空后再调用 `World->...`；不要保留任何 `WorldCollision::GetWorld()->...` 链式写法。 4. 对这些 `System::*` bind 在注册后补 `FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true)`，让构造期/默认值场景先在编译期与运行期 trait 层被拦下，而不是留到真正执行时崩溃。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 `AngelscriptWorldCollisionBindingsTests.cpp`：一组测试在有效 world 下跑 `LineTraceTestByChannel` / `AsyncOverlapByProfile` smoke；另一组用 `FScopedTestWorldContextScope` 包裹一个无 world 的 transient `UObject`，断言同步查询返回 `false`、异步查询返回无效 `FTraceHandle`，并记录 `"Invalid World Context"`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 预估工作量 | M |
| 风险 | 行为会从“warning/崩溃”收敛成“Throw + 默认失败值”；如果现有脚本错误依赖某些输出参数在失败时保留旧内容，需要在测试里把新契约固定下来。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增的 world-collision 绑定测试；确认有效 world 下行为不回退，无效 world-context 下同步 helper 返回 `false`、异步 helper 返回无效 `FTraceHandle`，并留下明确脚本错误而不是崩溃。 |

### Issue-23：`FJsonObject::GetNumberField` / `GetBoolField` 在类型错误后继续返回未初始化栈值

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Json.cpp` |
| 行号 | 274-286，306-318；287-299，332-344 |
| 问题 | current 的 `GetNumberField()` 先声明 `double Number;`，`GetBoolField()` 先声明 `bool bBool;`；当字段类型不匹配时，只调用 `TypeErrorMessage(...)`，随后仍然 `return Number` / `return bBool`。而 `FAngelscriptEngine::Throw()` 记录异常后不会中断 C++ 控制流，所以这两个返回值在错误路径上保持未初始化。UEAS2 对应位置仍保留 `double Number = 0.0;` 与 `bool bBool = false;` 的安全默认值。 |
| 根因 | 迁移时移除了 primitive getter 的默认初始化，却没有同步改变 `Throw()` 的“只记录异常不抛 C++ 异常”语义假设。 |
| 影响 | 脚本把 string/object/array 字段误读成 number 或 bool 时，不仅会收到一条异常，还会继续得到不可预测的随机返回值；同一类 JSON 类型错误因此变成“有错误日志但结果不稳定”的双重故障。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复稳定默认值，并在错误分支显式返回，禁止未初始化数据穿透到脚本层。 |
| 具体步骤 | 1. 将 `Bind_Json.cpp:278` 改为 `double Number = 0.0;`，将 `Bind_Json.cpp:310` 改为 `bool bBool = false;`，先与 UEAS2 的安全默认值对齐。 2. 在两个 getter 的 `if (!Value->TryGet...)` 分支里，调用 `TypeErrorMessage(...)` 后立即 `return Number;` / `return bBool;`，把“报错后返回默认值”的契约写死，避免后续维护再次依赖未定义栈状态。 3. 保持 `CheckValidObject()` 失败时现有的 `0.0` / `false` 返回不变，让“无效对象”和“字段类型错误”两条失败路径都稳定落到确定值。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 `AngelscriptJsonBindingsTests.cpp`，构造 `FJsonObject` 后分别把 `Name` 字段读成 number、把 `Count` 字段读成 bool，断言脚本记录类型错误，同时返回 `0.0` / `false`，而不是随机值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 低；唯一兼容性变化是错误路径返回值会从“未定义”收敛成稳定默认值，任何依赖随机栈值的行为都属于被动修复。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 JSON 绑定测试；确认错误路径稳定抛出类型错误，并分别返回 `0.0` 与 `false`。 |

### Issue-24：`FMemoryReader` 读取 helper 缺少长度与 EOF 防线，短读会回传垃圾值，负长度会直接触发 fatal

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Serialization/Archive.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/Containers/ContainerHelpers.cpp` |
| 行号 | 38-56，58-143；1438-1462，1905-1910；6-10 |
| 问题 | `ReadInt8/16/32/64`、`ReadUInt8/16/32/64`、`ReadFloat`、`ReadDouble` 都是“声明局部 `Result`，执行 `*reader << Result`，然后直接返回”；剩余字节不足时，`FArchive` 只置 error flag，不会填充输出缓冲。`ReadBytes(int Count)` 与 `ReadAnsiString(int Count)` 则直接把脚本传入的 `Count` 交给 `SetNumUninitialized(Count)`，既不检查 `Count < 0`，也不检查是否超过剩余长度。结果是短读会把未初始化数字/字节数组/字符串内容回传给脚本，负长度会直接跌进 `OnInvalidArrayNum()` 的 fatal。 |
| 根因 | bind 层错误地把 `FMemoryReader` 的内部 error flag 当成足够的失败语义，却没有在脚本入口处显式校验剩余可读字节，也没有给失败路径准备默认返回值。 |
| 影响 | 对截断 buffer、损坏长度字段或外部输入驱动的读取逻辑，当前行为不是稳定的脚本异常，而是“随机垃圾值继续传播”或“进程级 fatal”。这类 silent corruption / hard crash 在调试协议、反序列化工具和脚本 IO helper 中都很难定位。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 bind 层统一前置校验 `Count` 与剩余字节，所有失败路径都转成 `Throw + 默认返回值`。 |
| 具体步骤 | 1. 在 `Bind_FMemoryReader.cpp` 提取内部 helper，例如 `static bool ValidateReadableBytesOrThrow(FMemoryReader* Reader, int64 Count, const TCHAR* Operation)`：先拒绝 `Count < 0`，再比较 `Count <= Reader->TotalSize() - Reader->Tell()`；任一条件失败都 `FAngelscriptEngine::Throw(...)` 并返回 `false`。 2. 让全部数值型 reader 在声明结果时先零初始化，例如 `int32 Result = 0;`、`double Result = 0.0;`，随后用 helper 校验 `sizeof(T)`；校验失败立即返回默认值，成功后再执行 `Serialize/<<`。 3. 让 `ReadBytes()` 与 `ReadAnsiString()` 在分配前先调用同一 helper；校验失败时分别返回空 `TArray<uint8>` 与空 `FString`。成功路径里改用 `SetNumZeroed(Count)` 或等价安全初始化，避免未来再出现“写入长度不足时缓冲区残留未初始化数据”的回退。 4. 在 `Serialize` 之后补一层 `Reader->IsError()` 检查；如果底层 archive 仍然置错，统一 `Throw("Failed to read requested bytes from FMemoryReader.")` 并返回默认值，禁止继续传播半读数据。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 `AngelscriptMemoryReaderBindingsTests.cpp`，覆盖三类脚本回归：截断 buffer 上的 `ReadInt32()` 返回 `0` 且记录错误；`ReadBytes(-1)` / `ReadAnsiString(-1)` 记录脚本错误而不是 fatal；合法读取路径保持现有行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 错误路径会从“底层 error flag/fatal”改成统一脚本异常，可能暴露此前被静默吞掉的读取错误；需要把新错误文本写进测试，避免后续再回退。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 memory-reader 绑定测试；确认短读与负长度都变成 `Throw + 默认返回值`，合法 buffer 读取结果不变。 |

### Issue-25：`TArray::SetNum()` 把新增 POD / object-handle 槽位暴露成未初始化垃圾值

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 行号 | 930-967；894-935；162-220 |
| 问题 | current 的 `FArrayOperations::SetNum()` 在扩容时调用 `Arr.SetNumUninitialized(NewNum, ...)`，随后只在 `Ops->bNeedConstruct` 为真时做逐元素 `ConstructValue()`。这意味着 `int`、`float`、`UObject*` 这类“不需要构造函数”的元素类型，新增区间完全不会被初始化。UEAS2 对应实现先 `ResizeTo()` + `ArrayNum = NewNum`，然后在 `!bNeedConstruct` 分支显式 `FMemory::Memzero(...)`。现有 `FAngelscriptTArrayMutationCompatBindingsTest` 只覆盖 `Add/Insert/Remove/Reset`，没有任何 `SetNum()` 默认值回归。 |
| 根因 | `SetNum()` 在 current 被错误降级成了 `SetNumUninitialized()` 语义，丢掉了 UEAS2 里用于 POD / pointer 默认值初始化的 `Memzero` 分支。 |
| 影响 | 脚本执行 `Values.SetNum(8)` 后，新增槽位不会按 Unreal/脚本约定变成 `0` / `null`，而会携带随机内存内容。对标量类型这是不稳定数值，对对象句柄则可能伪装成看似有效的引用，后续问题会远离真正的扩容点。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `SetNum()` 回到 UEAS2 的“扩容后对新增非构造类型做零初始化”语义，并用现有 TArray 兼容测试锁住。 |
| 具体步骤 | 1. 将 `Bind_TArray.cpp:957-965` 的扩容路径改回 UEAS2 语义：调用 `Arr.ResizeTo(NewNum, Ops->NumBytesPerElement, Ops->Alignment); Arr.ArrayNum = NewNum;`，不要再使用 `SetNumUninitialized()`。 2. 保留当前已有的负数 `NewNum` 检查、缩容时析构旧元素、以及 reference/iterator invalidation 逻辑；只修正新增区间的初始化策略。 3. 在 `NewNum > PrevNum` 分支里，若 `Ops->bNeedConstruct` 为真则继续逐元素 `ConstructValue()`；否则对 `[PrevNum, NewNum)` 区间执行 `FMemory::Memzero(...)`，确保 `int` 变 `0`、object handle 变 `nullptr`。 4. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` 里的 `FAngelscriptTArrayMutationCompatBindingsTest`：在现有脚本片段后追加 `int[] Zeroes; Zeroes.SetNum(3);` 与 `UObject[] Objects; Objects.SetNum(2);`，断言所有新增元素分别为 `0` 与 `null`。 5. 若团队后续继续修复 `Reserve/RemoveSwap/SetNum` 同类问题，把这组断言收口到专门的 container regression section，避免三个 API 各自维护一份近似脚本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 低；唯一兼容性变化是脚本不再读到垃圾值，任何依赖未初始化内存的行为都属于错误代码被修正。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与扩展后的 `AngelscriptEngineBindingsTests`；确认 `SetNum()` 新增的 `int` 槽位为 `0`，新增的 `UObject` 槽位为 `null`，并且现有 `Add/Insert/Remove/Reset` 断言全部继续通过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-22 | Defect | 立即修复，先收口整张 `WorldCollision` bind surface 的空 world 崩溃入口 |
| P1 | Issue-24 | Defect | 紧随其后，统一 `FMemoryReader` 的短读/负长度错误处理，先消除垃圾值与 fatal |
| P1 | Issue-25 | Defect | 在读取安全问题后修复，恢复 `TArray::SetNum()` 的默认初始化语义 |
| P1 | Issue-23 | Defect | 与上述容器/读取修复并行可做，尽快把 JSON 错误路径收敛成稳定默认值 |

---

## 发现与方案 (2026-04-08 16:32)

### Issue-26：`FindClass()` 已公开 `null` 结果，但多条 `UClass` helper 仍直接解引用接收者

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 276-279，319-326，331-335；46-55，386-393 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 22。当前 `UClass::FindClass()` 与全局 `FindClass()` 都把“查不到类”暴露成 `nullptr`，但同一块 `UClass` bind 里的 `GetDefaultObject()`、`IsAbstract()`、`GetSuperClass()` 仍分别直接执行 `Class->GetDefaultObject()`、`Class->HasAnyClassFlags(...)`、`Class->GetSuperClass()`。与之相对，紧邻的 `FindFunctionByName()` 又显式做了 `Class != nullptr` 判空，说明同一文件内已经出现两套接收者契约。现有测试只覆盖 `FindClass("AActor")`、`FindClass("ABindingStaticClassActor")` 的 happy path，没有任何 missing-class 链式调用回归。 |
| 根因 | 绑定层先扩展了 nullable 的 class lookup surface，再补 `UClass` 成员 helper 时没有统一“`UClass` handle 为空时该返回什么”的规则；部分 helper 已做脚本友好收口，部分仍停留在裸 C++ 解引用。 |
| 影响 | 脚本一旦写出 `FindClass("MissingType").GetDefaultObject()`、`FindClass("MissingType").IsAbstract()` 或 `FindClass("MissingType").GetSuperClass()`，失败不会停留在脚本层的 `null` 检查，而会直接跌进 bind 层空指针解引用。由于 `FindClass()` 本身鼓励这种链式写法，这条崩溃路径比普通手写空句柄更容易被真实业务踩中。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 给 `UClass` helper 建立统一的 null-receiver 收口规则，把 lookup miss 从崩溃改成稳定的 `Throw + 默认返回值`。 |
| 具体步骤 | 1. 在 `Bind_UObject.cpp` 的 `Bind_UClass_Base` 附近提取一个内部 helper，例如 `static bool ResolveClassReceiverOrThrow(UClass* Class, const ANSICHAR* FunctionName)`，统一负责 `Class == nullptr` 时的错误文本与返回判定。 2. 让 `GetDefaultObject()`、`IsAbstract()`、`GetSuperClass()` 改用该 helper；receiver 为空时分别返回 `nullptr`、`false`、`nullptr`，并记录明确脚本错误，而不是继续裸解引用。 3. 顺手复核同一 `UClass_` block 中仍使用 `METHODPR_TRIVIAL` 的入口，至少把 `IsChildOf()` 改成显式 lambda 包装，确保 `FindClass(...).IsChildOf(...)` 也遵循同一 null-receiver 契约。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 增加 negative 回归：构造一个明确不存在的类名，随后链式调用 `GetDefaultObject()`、`IsAbstract()`、`GetSuperClass()`、`IsChildOf(AActor::StaticClass())`，断言脚本记录错误且返回稳定默认值。 5. 保留现有 `FindClass("AActor")` 与 `FindClass("ABindingStaticClassActor")` 的 happy-path 断言，确认修复不会影响合法 `UClass` 的正常行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 修复后，历史脚本里依赖“lookup miss 后继续链式调用也不会显式报错”的路径会开始收到更早的错误提示；这是预期中的正确性收紧。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与 `AngelscriptTest`；运行扩展后的 class binding 测试，确认 missing-class 链式调用不再崩溃，而是稳定返回 `null/false` 并记录预期错误。 |

### Issue-27：手写 world-context bind 没有写回 `asTRAIT_USES_WORLDCONTEXT`，插件自带护栏被整体绕过

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 166-253，286-466；33-41；633；353-358；18132-18137；5164-5178；674-703 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 27。`Bind_UWorld.cpp` 的 `__WorldContext()`、`GetCurrentWorld()`，以及 `Bind_AActor.cpp` 的 `ClassName::Spawn(...)`、`GetAllActorsOfClass*()` 等入口，都直接依赖 `FAngelscriptEngine::TryGetCurrentWorldContextObject()` 或 `GEngine->GetWorldFromContextObject(...)`。但对这两个文件做源码检索，`SetPreviousBindRequiresWorldContext` 命中为 `0`。与此同时，bind core 明确提供了 `SetPreviousBindRequiresWorldContext(bool)`，其实现会把 `asTRAIT_USES_WORLDCONTEXT` 写到脚本函数；编译器和运行时又都基于这个 trait 做 constructor/default statement 警告与 invalid-world-context / `BlueprintThreadSafe` 阻断，测试 `AngelscriptBindConfigTests.cpp` 也已锁住这条行为。也就是说，当前问题不是“系统没有能力”，而是手写 bind 没把能力接回去。 |
| 根因 | world-context 获取从全局变量迁到 helper 调用后，手写注册点继续直接使用 ambient world context，但没有同步补回 trait 元数据；于是同一插件内部出现“UHT 生成函数有 world-context trait，手写 bind 没 trait”的分叉。 |
| 影响 | 这些 API 现在会从“编译期可预警、运行期可提前阻止”的 world-context 依赖，退化成普通函数调用。脚本因此可以在 constructor/default statement、`BlueprintThreadSafe` 或无 world 的对象上下文里更晚才撞上 `Invalid World Context`、空结果，甚至已经进入有副作用的 spawn/query 路径。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 world-context 依赖显式写回 bind trait，并用一个共享注册 helper 杜绝后续再漏标。 |
| 具体步骤 | 1. 在 `Bind_UWorld.cpp` 中，对 `BindGlobalFunction("UObject __WorldContext()", ...)` 与 `BindGlobalFunction("UWorld GetCurrentWorld()", ...)` 各自注册完成后立即调用 `FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true)`。 2. 在 `Bind_AActor.cpp` 中，对 `ClassName::Spawn(...)`、`GetAllActorsOfClass(?&)`、`GetAllActorsOfClass(UClass, ?&)`、`GetAllActorsOfClassWithTag(...)` 以及任何仍保留的内部 world-context helper（如 `__Actor_GetAllByClass`）同样补 trait 标记，确保所有 ambient world-context 入口一律走同一元数据规则。 3. 为避免未来再漏，在 `AngelscriptBinds.h/.cpp` 增加一个轻量封装，例如 `BindGlobalFunctionWithWorldContext(...)`，内部顺序固定为“注册函数 -> 写 `RequiresWorldContext` trait”；后续 `Binds/` 下所有手写 ambient-world helper 优先走这个封装。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 新增针对手写 bind 的断言：通过函数声明或 id 拿到 `GetCurrentWorld`、`GetAllActorsOfClass` 等脚本函数，确认 `traits.GetTrait(asTRAIT_USES_WORLDCONTEXT)` 为 true。 5. 追加一次 `rg "TryGetCurrentWorldContextObject|GetWorldFromContextObject"` 的 bind 目录审计，把 `Bind_SystemTimers.cpp`、`Bind_UUserWidget.cpp`、`Bind_WorldCollision.cpp` 等同类入口也纳入同一 trait 补齐清单，避免只修入口文件后其它 shard 继续裸奔。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | M |
| 风险 | trait 补齐后，一部分当前“能编译/能跑到更晚才报错”的脚本会更早收到 world-context 相关警告或阻断；这是预期中的 guardrail 回归，但需要在变更说明里明确。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与 `AngelscriptTest`；运行扩展后的 bind-config 测试，确认手写 `GetCurrentWorld` / `Spawn` / `GetAllActorsOfClass` bind 已带 `asTRAIT_USES_WORLDCONTEXT`，并抽样验证 constructor/default-statement 或 invalid-world-context 场景会得到与 UHT 生成函数一致的警告/异常。 |

### Issue-28：`GetScriptTypeDeclaration()` 与 native `FindClass("AActor")` 命名契约不一致，class name 无法 round-trip

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 行号 | 299-301，519-531；28-32；46-55；223-246 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 21。当前 `UClass::GetScriptTypeDeclaration()` 只有 `Cast<UASClass>(Class) != nullptr` 时才返回 `PrefixCPP + Name`，对 native `UClass` 一律返回空串；但同仓库 `FAngelscriptType::GetBoundClassName()` 明确对任意 `UClass` 都返回 `Class->GetPrefixCPP() + Class->GetName()`，而 `FindClass("AActor")` 也已经把这个 native 命名契约固定成公开 lookup surface。现有测试一边验证 `FindClass("AActor")` 成功，一边只对 script class 断言 `GetScriptTypeDeclaration()` 非空，没有任何 native class round-trip 回归。 |
| 根因 | `GetScriptTypeDeclaration()` 是新增元数据 helper，却没有复用插件内部已经存在的 canonical class-name helper，而是临时写成了“只有 `UASClass` 才有 declaration”的局部规则。 |
| 影响 | 脚本或工具若先通过 `FindClass("AActor")` / `GetAllClasses()` 拿到 native `UClass`，再试图用 `GetScriptTypeDeclaration()` 回写可重放名字，就会直接得到空串。结果是 lookup、序列化、诊断输出和兼容层代码都需要额外特判 native class，API surface 本身不自洽。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一以 `FAngelscriptType::GetBoundClassName()` 生成脚本侧 class declaration，并与类可见性/存活性规则共用同一 predicate。 |
| 具体步骤 | 1. 将 `Bind_UObject.cpp:299-301` 的 `GetScriptTypeDeclaration()` 改为优先判空 `Class == nullptr`，随后对 native 与 script class 都走 `FAngelscriptType::GetBoundClassName(Class)`，不要再按 `UASClass` 单独短路成空串。 2. 若 `Issue-10` 的 class 可见性 helper 已落地，`GetScriptTypeDeclaration()` 直接复用该 helper 的“live script-facing class”判定；若尚未落地，至少对 `UASClass && ScriptTypePtr == nullptr` 的 tombstone 返回空串，避免 removed class 再次回流到元数据 surface。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 增加 native round-trip 回归：`UClass ActorClass = FindClass("AActor"); FString Decl = ActorClass.GetScriptTypeDeclaration();` 断言 `Decl == "AActor"`，且 `FindClass(Decl) == ActorClass`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 保留现有 script class metadata 断言，同时再补一条 native class 断言，确认这次修复不会回退脚本类自己的 metadata 路径。 5. 用同一 helper 复核其它输出类名字符串的 metadata API，避免后续又出现“lookup 用一种名字，display/declaration 用另一种名字”的新分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 若极少数脚本把“native class declaration 为空串”当成判别条件，修复后会改变其分支行为；建议改由显式 `Cast<UASClass>` / metadata helper 判别，而不是继续依赖空串副作用。 |
| 前置依赖 | 无；若同时执行 `Issue-10`，可复用其类可见性 helper |
| 验证方式 | 编译 `AngelscriptRuntime` 与相关绑定测试；确认 native `AActor` 和 script class 都能返回稳定 declaration，并且 `FindClass(GetScriptTypeDeclaration())` 对两类 class 都能 round-trip。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-26 | Defect | 立即修复，先把 `FindClass` miss 后的 `UClass` 链式调用从崩溃收口成稳定错误 |
| P1 | Issue-27 | Architecture | 紧随其后，补齐手写 world-context bind 的 trait，恢复编译期/运行期护栏 |
| P2 | Issue-28 | Defect | 在高优先级安全语义问题落地后修复，统一 native/script class 的命名 round-trip 契约 |

---

## 发现与方案 (2026-04-08 16:51)

### Issue-29：`UnsafeDuringConstruction` trait 在 current 分支被整条删空，构造期危险 API 已无法表达或传播

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/ThirdParty/source/as_scriptfunction.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/ThirdParty/source/as_builder.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/ThirdParty/source/as_compiler.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptBinds.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Helper_FunctionSignature.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 120-140；627-640；345-390；450-510；556-589；120-129；1478-1481，4198-4203，4708-4709；18184-18189；564-575；176-180；480-483；355-389 |
| 问题 | current 的 `as_scriptfunction.h` 在 `asTRAIT_EDITOR_ONLY` 之后直接把 `0x1000000` 用给了 `asTRAIT_EXPLICIT`，不存在 `asTRAIT_UNSAFE_DURING_CONSTRUCTION`；`AngelscriptBinds.h/.cpp` 也没有 `SetPreviousBindUnsafeDuringConstruction()`；`Helper_FunctionSignature.h` 只写 deprecated/editor-only trait，没有 `UnsafeDuringActorConstruction` metadata 到 script trait 的桥；`Bind_UObject.cpp` 的 `NewObject` / `LoadObject` 绑定后也没有任何 construction-safety 标记。对照 UEAS2，可见这条能力在 third-party parser/builder、compiler、bind core、UHT signature bridge 和手写 bind 上都是成套存在的。 |
| 根因 | current 在同步 AngelScript fork 与 runtime bind surface 时，把 `UnsafeDuringConstruction` 从 trait 位定义、bind helper 到 UHT bridge 一起裁掉了，只保留了 `no_discard`、`editor_only`、`world_context` 等其它 trait。 |
| 影响 | 现在不是“少标了几处函数”，而是整个 BindSystem 已经失去表达这类风险 API 的元数据通道。`NewObject`、`LoadObject` 以及带 `UnsafeDuringActorConstruction` metadata 的 UFUNCTION 无法再被区分成“构造期危险”入口，编译器、测试和调试工具也拿不到对应 trait。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 按 UEAS2 恢复 `UnsafeDuringConstruction` 的端到端 trait 管线，并同时修正 current 自定义 `asTRAIT_EXPLICIT` 占位造成的 bit 冲突。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h` 恢复 `asTRAIT_UNSAFE_DURING_CONSTRUCTION`，保持与 UEAS2 一致使用 `0x1000000`；把 current 自定义的 `asTRAIT_EXPLICIT` 顺移到下一个空闲 bit，并审计 `as_restore.cpp` 等直接依赖该 trait 的代码，避免恢复 unsafe trait 时覆盖 explicit 语义。 2. 参照 UEAS2 的 `as_builder.cpp` / `as_compiler.cpp`，把 `UNSAFE_DURING_CONSTRUCTION` decorator 与“defaults / ref-object constructor 中禁止调用这类函数”的编译期检查一并迁回 current third-party 源码。 3. 在 `AngelscriptBinds.h/.cpp` 补回 `SetPreviousBindUnsafeDuringConstruction(bool)`，使手写 bind 也能写入该 trait。 4. 在 `Binds/Helper_FunctionSignature.h` 恢复 `NAME_UnsafeDuringActorConstruction` metadata 到 script function trait 的桥接逻辑，保证 UHT 生成路径与手写 bind 一致。 5. 在 `Bind_UObject.cpp` 的 `NewObject` 与 `LoadObject` 注册后立即调用 `FAngelscriptBinds::SetPreviousBindUnsafeDuringConstruction(true)`，恢复 UEAS2 已有的手写标记。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h/.cpp` 增加一个带 `meta = (UnsafeDuringActorConstruction)` 的测试函数；在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 增加两组断言：一组检查 UHT 路径写入该 trait，另一组检查手写 `NewObject` / `LoadObject` 也带该 trait，并补一个 defaults/constructor 编译失败回归，锁住编译器护栏已恢复。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_restore.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | L |
| 风险 | 这是 third-party trait 位恢复，若只改 bind 层不改 compiler/builder，会得到“trait 可写但无效果”的假修复；若改 bit 位不同步 explicit-trait 使用点，又可能破坏 restore/序列化兼容。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与 `AngelscriptTest`，确认 third-party trait enum 变更不会破坏构建。 2. 运行新增 `BindConfig` / UHT coverage 回归，确认 `NewObject`、`LoadObject` 与测试 UFUNCTION 都带 `asTRAIT_UNSAFE_DURING_CONSTRUCTION`。 3. 编译一段在 defaults 或 ref-object constructor 中调用这些 API 的脚本，确认会得到与 UEAS2 同等的 construction-safety 诊断。 |

### Issue-30：`UObject` 创建/查找 helper 丢掉 `no_discard`，返回值误用已失去编译期契约

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 514-603；18253-18260；350-392 |
| 问题 | current 的 `GetTransientPackage()`、`GetAngelscriptPackage()`、`NewObject(...)`、`LoadObject(...)`、两个 `FindObject(...)` overload 全都以普通返回值函数注册；而 compiler 侧 `as_compiler.cpp` 明确会在函数带 `asTRAIT_NODISCARD` 时记录 `noDiscardFunction` 并发出“结果不可忽略”的诊断。UEAS2 对应的 `Bind_UObject.cpp` 已把这批入口全部声明成 `... no_discard`，说明 current 回退的不是实现本体，而是返回值契约。 |
| 根因 | 绑定迁移时保留了对象创建/查找逻辑，但没有把 UEAS2 已补齐的返回值语义一起迁回，导致 script declaration 与 compiler 现成的 `no_discard` 机制脱钩。 |
| 影响 | 脚本现在可以静默写出 `NewObject(...);`、`FindObject(...);`、`GetTransientPackage();` 这类“看起来像在做事、实际只产生临时值”的调用，而不会得到任何编译期提醒。对 `NewObject/LoadObject` 来说，这会把真正需要保存的对象句柄误吞掉；对 package / lookup helper 来说，则会放大无效语句和误读 side effect 的概率。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `Bind_UObject.cpp` 的对象创建/查找入口重新显式声明 `no_discard`，并用编译诊断回归锁住这批契约。 |
| 具体步骤 | 1. 将 `Bind_UObject.cpp` 中六条声明改回 UEAS2 语义：`UPackage GetTransientPackage() no_discard`、`UPackage GetAngelscriptPackage() no_discard`、`UObject NewObject(...) no_discard`、`UObject LoadObject(...) no_discard`、两个 `UObject FindObject(...) no_discard`。 2. 保留现有 `SetPreviousBindArgumentDeterminesOutputType(1)` 与 `Class == nullptr` 的运行时校验，不改变函数行为，只恢复返回值契约。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 新增 trait 断言，直接从 script engine 取回这几条 global function，确认 `traits.GetTrait(asTRAIT_NODISCARD)` 为 true。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` 或同类编译诊断测试中新增一段脚本，故意忽略 `GetTransientPackage()`、`NewObject(...)`、`FindObject(...)` 的返回值；用 `CompileModuleWithSummary` 断言 diagnostics 中包含对应函数名，保证不是“trait 写上了但编译器提示没恢复”。 5. 保留现有 `Bindings/AngelscriptObjectBindingsTests.cpp`、`Bindings/AngelscriptCompatBindingsTests.cpp` 的 happy-path 行为测试，作为修复后运行时不回退的基线。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 预估工作量 | S |
| 风险 | 兼容性风险主要体现在编译期：现有脚本里那些长期忽略返回值的调用会开始报 warning / error，属于把原先被吞掉的误用显式化。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与相关测试。 2. 运行新增 trait 断言，确认六条 helper 都带 `asTRAIT_NODISCARD`。 3. 运行新增编译诊断回归，确认忽略这些返回值会产生预期 diagnostics，而正常接收返回值的现有绑定测试继续通过。 |

### Issue-31：actor 旧泛型绑定的弃用控制面被移除，迁移策略在 `StaticClass()` 之外已经失效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptSettings.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_AActor.cpp` |
| 行号 | 39-149，286-313；93-106；684-695；151-165；82-111；34-87，89-205 |
| 问题 | current 的 `Bind_AActor.cpp` 无条件注册旧式 `GetComponentsByClass(..., ?& OutComponents)` 两个 overload，以及每个 actor namespace 下的 `ClassName::Spawn(...)`；`AngelscriptSettings.h` 里只保留了 `StaticClassDeprecation`，没有 UEAS2 的 `DeprecateOldActorGenericMethods`；但同仓库 `Bind_BlueprintType.cpp` 又继续通过 `DeprecatePreviousBind(...)` 对 `StaticClass()` 执行 Allowed / Deprecated / Disallowed 三态控制。也就是说，插件内部仍然保留弃用框架，却只对一部分旧 API 生效。现有 `AngelscriptNativeEngineBindingsTests.cpp` 也只验证旧 `GetComponentsByClass` 的 happy path，没有任何 deprecated / disallowed 回归。 |
| 根因 | current 在清理 settings surface 时删除了 actor 旧泛型 API 的配置项，但没有同步移除 legacy bind，也没有把这组入口迁到新的统一迁移策略上。 |
| 影响 | 当前脚本迁移策略已经分叉：`StaticClass()` 可以逐步弃用，而 `AActor::Spawn()`、旧 `GetComponentsByClass` 永远裸暴露。结果是团队无法用配置分阶段推进到 determines-output-type / 返回数组的新接口，旧写法会继续在代码库里固化，并让 bind naming policy 长期不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 actor 旧泛型 API 的三态弃用开关，让 `Bind_AActor.cpp` 与 `StaticClass()` 共享同一迁移治理方式。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` 新增 `UPROPERTY(Config)`：`EAngelscriptStaticClassMode DeprecateOldActorGenericMethods = EAngelscriptStaticClassMode::Deprecated;`，直接复用 current 已有的 Allowed / Deprecated / Disallowed 三态枚举，避免为同一种策略再引入第二套 enum。 2. 在 `Bind_AActor.cpp` 中参照 UEAS2，把旧 `GetComponentsByClass(..., ?& OutComponents)` 两个 overload、以及每个类型 namespace 下的 `ClassName::Spawn(...)` 放进该配置开关内：`Deprecated` 时注册后立刻 `DeprecatePreviousBind(...)`，`Disallowed` 时不注册，`Allowed` 时保持现状。 3. 继续保留 `SpawnActor(...)` 和 determines-output-type 风格的新入口始终可用，确保迁移只收紧 legacy surface，不影响现代 API。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 增加配置回归：临时修改 `GetMutableDefault<UAngelscriptSettings>()->DeprecateOldActorGenericMethods`，重置 bind state 后分别验证 `Allowed` 模式下旧 bind 存在、`Deprecated` 模式下对应 script function 带 `asTRAIT_DEPRECATED`、`Disallowed` 模式下旧 bind 根本不注册。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` 保留现有 happy-path 覆盖，并补一条 compile-summary 级回归：对旧 `GetComponentsByClass` 或 `AActor::Spawn()` 写一个小脚本，确认 `Deprecated` 模式产生迁移诊断、`Disallowed` 模式编译报 unresolved，从而把用户可见行为也锁住。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 修复后，依赖旧泛型写法的脚本会开始看到 deprecation 或 unresolved；这是治理目标本身，但需要在默认值和升级说明中明确推荐替代 API，避免一次性把历史脚本全部打断。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与相关测试。 2. 运行新增 bind-config 回归，确认三种配置模式下旧 actor 泛型 bind 的注册/弃用行为与预期一致。 3. 运行 native engine / compile-summary 回归，确认旧写法在 `Deprecated` 模式下给出迁移提示，在 `Disallowed` 模式下无法编译，而新 `SpawnActor(...)` 与数组返回式查询保持可用。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-29 | Architecture | 立即修复，先恢复 `UnsafeDuringConstruction` 的端到端 trait 管线，否则后续 bind 标记都无法生效 |
| P2 | Issue-30 | Defect | 紧随其后，补回 `no_discard` 契约并锁住编译诊断 |
| P2 | Issue-31 | Architecture | 在 trait/契约问题稳定后恢复 actor 旧泛型 API 的迁移治理开关 |

---

## 发现与方案 (2026-04-08 17:05)

### Issue-32：`GetCurrentWorld()` 丢失 `no_discard`，当前 world 查询结果可被静默吞掉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 33-41；18253-18260；34-38；`rg -n 'GetCurrentWorld\\(|__WorldContext' Plugins/Angelscript/Source/AngelscriptTest -g '*.cpp' -g '*.as'` 未命中 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 25。current `Bind_UWorld.cpp` 把 `GetCurrentWorld()` 注册成普通返回值函数：`"UWorld GetCurrentWorld()"`。同一入口在 UEAS2 是 `"UWorld GetCurrentWorld() no_discard"`。而 compiler 明确会在 `asTRAIT_NODISCARD` 存在时把函数名写入 `ctx->noDiscardFunction`，用于后续“结果不可忽略”的诊断。当前测试树又完全没有 `GetCurrentWorld()` / `__WorldContext` 回归，因此这条返回值契约回退既没有编译期提示，也没有自动化锁定。 |
| 根因 | `Bind_UWorld.cpp` 在 world-context API 由变量/兼容层演进到 current 形态时，只保留了取 world 的实现本体，没有把 UEAS2 已有的 `no_discard` 声明一起迁回。 |
| 影响 | 脚本现在可以静默写出 `GetCurrentWorld();`、`auto _ = GetCurrentWorld();` 这类只查询 world 却不消费结果的语句，而编译器不会提醒这其实是无效调用。对 world-context 依赖强的脚本，这会把“忘记接收当前 world”长期伪装成正常逻辑。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 `GetCurrentWorld()` 的 `no_discard` 契约，并用 trait 断言和编译诊断同时锁住。 |
| 具体步骤 | 1. 将 `Bind_UWorld.cpp:38` 的声明改成 `UWorld GetCurrentWorld() no_discard`，不改动现有实现体。 2. 若 `Issue-27` 的 `RequiresWorldContext` trait 修复同时落地，确保 `GetCurrentWorld()` 在同一注册点既带 `asTRAIT_NODISCARD`，也带 `asTRAIT_USES_WORLDCONTEXT`，避免只恢复一半元数据。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 增加 trait 断言，直接取回 `GetCurrentWorld` 的 script function，确认 `traits.GetTrait(asTRAIT_NODISCARD)` 为 true。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` 增加 compile-summary 回归，故意编译一段只调用 `GetCurrentWorld();` 而不使用返回值的脚本，并断言 diagnostics 中包含 `GetCurrentWorld`。 5. 补一条 happy-path smoke 到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，确认正常接收返回值的 world 查询脚本继续通过，避免修复后误伤现有 world helper。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 兼容性风险主要是编译期收紧：现有脚本中那些长期忽略 `GetCurrentWorld()` 返回值的写法会开始报 warning/error；这属于把原先被吞掉的逻辑空操作显式化。 |
| 前置依赖 | 无；若与 `Issue-27` 同批执行，可共用同一处 bind metadata 断言 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与相关测试。 2. 运行新增 bind-config 断言，确认 `GetCurrentWorld()` 带 `asTRAIT_NODISCARD`。 3. 运行新增 compile-summary 回归，确认忽略 `GetCurrentWorld()` 返回值会产生预期 diagnostics，而正常消费返回值的脚本不回退。 |

### Issue-33：delegate 的三参数 raw helper 继续以公开名字暴露，codegen 内部协议和用户 API 仍然耦在一起

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Delegates.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 1428-1439；665-665，710-714；18253-18260；1445-1457；676-680；`rg -n 'UDelegateFunction Signature|__Internal_AddUFunction|__Internal_BindUFunction' Plugins/Angelscript/Source/AngelscriptTest -g '*.cpp' -g '*.as'` 未命中 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 83。current `Bind_Delegates.cpp` 仍把 raw overload 暴露成公开 `AddUFunction(..., UDelegateFunction Signature)` / `BindUFunction(..., UDelegateFunction Signature)`，而 preprocessor 生成代码也直接调用 `_Inner.AddUFunction(...)` / `_Inner.BindUFunction(...)`。UEAS2 对应绑定已经把这两条入口改名为 `__Internal_AddUFunction` / `__Internal_BindUFunction`，并让 codegen 只通过内部前缀名字访问。与此同时，current `Delegate_.Constructor("void f(UObject Object, const FName& FunctionName, UDelegateFunction Signature)", ...)` 后面缺少 UEAS2 的 `SetPreviousBindNoDiscard(true)`；compiler 则明确会对带 `asTRAIT_NODISCARD` 的 constructor 生成“结果不可忽略”诊断。当前测试只覆盖两参数公开 wrapper 的 happy path，没有任何 raw helper / constructor 契约回归。 |
| 根因 | delegate bind 与 preprocessor 在迁移时保留了“带显式 Signature 参数”的底层桥接能力，但没有把 UEAS2 已经建立的内部命名隔离和 constructor 返回值契约一并迁回。 |
| 影响 | 当前脚本表面上只应看到两参数 `BindUFunction` / `AddUFunction`，但底层三参数协议仍以同名公开 surface 裸露，用户代码可以手工传入任意 `UDelegateFunction Signature`，绕过正常的 `__DelegateSignature(this)` 推导路径。与此同时，形如 `FExampleDelegate_UnitTest(this, n"ExampleFunction");` 的临时 delegate 构造结果也可以被静默丢弃，没有编译期提醒。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 delegate raw helper 重新收口到 `__Internal_*` 命名面，并补回 constructor 的 `no_discard` 契约，使 codegen 协议与用户 API 分层。 |
| 具体步骤 | 1. 将 `Bind_Delegates.cpp:1432`、`1437` 的两个三参数方法分别重命名为 `void __Internal_AddUFunction(...)` 与 `void __Internal_BindUFunction(...)`，对齐 UEAS2 的内部协议命名。 2. 在 `Bind_Delegates.cpp:1439` 的 constructor 注册后立即补 `FAngelscriptBinds::SetPreviousBindNoDiscard(true)`，恢复 UEAS2 已有的返回值契约。 3. 将 `AngelscriptPreprocessor.cpp:665`、`710`、`714` 的生成字符串全部改成调用 `_Inner.__Internal_AddUFunction(...)` / `_Inner.__Internal_BindUFunction(...)`，确保公开两参数 wrapper 仍保持原名，只有 codegen 触达内部桥接层。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp` 追加回归，确认现有两参数 `BindUFunction` / `AddUFunction` 行为不回退。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` 增加 compile-summary 断言：忽略 `FExampleDelegate_UnitTest(this, n"ExampleFunction")` 这样的构造结果时，应出现 `no_discard` 诊断；同时写一段直接调用三参数 raw overload 的脚本，确认 surface 名字已迁到 `__Internal_*`，不会再与公开 API 同名。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 预估工作量 | M |
| 风险 | 兼容性风险集中在那些直接依赖三参数 raw overload 名字的脚本或生成物；需要把 preprocessor 与 bind 改动同批提交，否则会先出现 generated code 无法解析 helper 名字的中间态。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与相关测试，确认 preprocessor 和 bind 同步后没有 unresolved delegate helper。 2. 运行 delegate 场景/绑定测试，确认现有两参数 `BindUFunction` / `AddUFunction` 继续通过。 3. 运行新增 compile-summary 回归，确认忽略 delegate constructor 结果会得到 `no_discard` diagnostics，且三参数 raw helper 只剩 `__Internal_*` 命名面。 |

### Issue-34：`Bind_InputEvents.cpp` 丢掉 `FKey` / `FInputChord` / `FEventReply` 的 `no_discard`，输入值工厂现在可以被无声忽略

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_InputEvents.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 16-20，50-59，216-227；18253-18260；16-22，52-63，221-231；`rg -n 'Handled\\(|Unhandled\\(|FInputChord\\(|FKey\\(' Plugins/Angelscript/Source/AngelscriptTest -g '*.cpp' -g '*.as'` 未命中 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 73。current `Bind_InputEvents.cpp` 在 `FKey(const FName&)`、两个 `FInputChord` constructor 后都没有 `SetPreviousBindNoDiscard(true)`；`FEventReply::Handled()` / `Unhandled()` 也被注册成普通返回值函数。UEAS2 对应位置已经分别补了三次 `SetPreviousBindNoDiscard(true)`，并把两个全局函数声明成 `no_discard`。compiler 则明确会对 `asTRAIT_NODISCARD` 的函数和 constructor 生成“结果不可忽略”诊断。当前测试树又完全没有这些 API 的命中，说明输入值工厂的返回值契约既回退了，也没人自动化守护。 |
| 根因 | 输入事件 bind 迁移时保留了 FKey / FInputChord / FEventReply 的构造与工厂实现，但把 UEAS2 已经补齐的 `no_discard` 元数据一起落掉了。 |
| 影响 | 脚本现在可以静默写出 `FKey(n"SpaceBar");`、`FInputChord(EKeys::Enter);`、`FEventReply::Handled();` 这类“看起来像在做事、实际只是产生临时值”的语句，而不会得到任何编译期提醒。对 Slate/UI 输入路径来说，这会把本应返回给框架的 handled/unhandled 结果直接吞掉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复输入值工厂的 `no_discard` 元数据，并用 trait 断言 + compile-summary 回归一起锁住。 |
| 具体步骤 | 1. 在 `Bind_InputEvents.cpp:17-20` 的 `FKey` constructor 后补 `FAngelscriptBinds::SetPreviousBindNoDiscard(true)`。 2. 在 `Bind_InputEvents.cpp:51-59` 的两个 `FInputChord` constructor 后分别补 `SetPreviousBindNoDiscard(true)`。 3. 将 `Bind_InputEvents.cpp:219`、`224` 的两个全局函数声明改成 `FEventReply Handled() no_discard` 与 `FEventReply Unhandled() no_discard`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 新增 trait 断言，确认 `Handled()`、`Unhandled()` 以及这三个 constructor 对应的 script function 都带 `asTRAIT_NODISCARD`。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` 增加 compile-summary 回归，故意忽略 `FEventReply::Handled()`、`FKey(n"SpaceBar")` 和 `FInputChord(EKeys::Enter)` 的结果，断言 diagnostics 中包含对应函数/类型名；同时保留一条正常消费返回值的输入脚本 smoke，避免把合法写法误判成噪音。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 预估工作量 | S |
| 风险 | 修复后会在编译期暴露现有脚本里那些长期忽略输入值工厂结果的写法；这是预期中的契约收紧，但需要保证 diagnostics 文案足够明确，避免被误认成行为回归。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与相关测试。 2. 运行新增 trait 断言，确认 `FKey` / `FInputChord` constructor 与 `Handled()` / `Unhandled()` 都带 `asTRAIT_NODISCARD`。 3. 运行新增 compile-summary 回归，确认忽略这些输入值工厂结果会产生预期 diagnostics，而正常使用 `FEventReply` / `FInputChord` 的脚本不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-33 | Architecture | 先处理 delegate 内部协议泄漏，避免后续 codegen 和 bind surface 继续分叉 |
| P2 | Issue-32 | Defect | 紧随其后，补回 `GetCurrentWorld()` 的 `no_discard` 并锁住 world helper 编译诊断 |
| P2 | Issue-34 | Defect | 最后统一修复输入值工厂的 `no_discard`，把 UI/输入脚本中的静默空操作显式化 |

---

## 发现与方案 (2026-04-08 17:27)

### Issue-35：`__PostLiteralAssetSetup` 作为公开 global bind 暴露，但对空 `Asset` 直接解引用

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 690-697；4116-4119 |
| 问题 | `__PostLiteralAssetSetup(UObject Asset, const FString& Name)` 当前被注册成普通全局函数，但实现里第一步就是 `Asset->GetName()`，对 `Asset == nullptr` 没有任何保护。预处理器生成的 literal asset getter 目前确实会先检查 `__CreateLiteralAsset(...)` 的返回值，只有非空时才继续调用 `__PostLiteralAssetSetup(...)`；但这是唯一已知调用方的自律，不是 helper 自身的契约。也就是说，只要脚本、未来的 codegen 变更，或其他 bind 内部调用误把空句柄传进来，这里就会在 bind 层先崩溃，而不是给出稳定脚本错误。 |
| 根因 | literal asset 初始化流程被拆成 `__CreateLiteralAsset` 与 `__PostLiteralAssetSetup` 两段内部 helper 后，第二段继续以公开 global bind 形式暴露，却没有像 `NewObject`、`SpawnActor` 那样把最基本的参数校验固化在 helper 内部。 |
| 影响 | 当前 public surface 上存在一条可直接从脚本触发的空指针解引用路径；而且它和 `__CreateLiteralAsset` 是松耦合的，两段 helper 之间没有类型系统或命名层面的“只能内部调用”约束，后续只要生成模板稍有回退，就会把 crash surface 重新放大。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `__PostLiteralAssetSetup` 收紧成真正的内部 helper：入口先判空并抛脚本错误，同时让生成器只调用内部命名面。 |
| 具体步骤 | 1. 将 `Bind_UObject.cpp:690-697` 的实现改为先检查 `Asset == nullptr`，命中时 `FAngelscriptEngine::Throw("__PostLiteralAssetSetup received a null asset."); return;`，禁止继续解引用 `Asset->GetName()`。 2. 参照 delegate raw helper 的处理方式，把公开名字迁成 `__Internal_PostLiteralAssetSetup`，并在 `AngelscriptPreprocessor.cpp:4119` 同步改成调用内部名字，减少脚本侧误用这类 codegen 协议函数的机会。 3. 如果担心兼容性，可以短期保留旧名字作为薄 wrapper，但旧 wrapper 也必须先做同样的空值检查，并在注释中标明 deprecated，仅供兼容过渡。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` 增加负向回归：直接从脚本调用该 helper 的空参数路径，断言记录预期错误而不是崩溃；再保留一条 literal asset happy-path smoke，确认正常创建资产仍会触发 post-setup 广播。 5. 顺手 grep 其它 `__*` 形式的 bind helper，确认凡是只服务 codegen/预处理器协议的入口，都不再裸暴露无保护实现。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 若外部脚本已经直接调用 `__PostLiteralAssetSetup`，重命名会带来编译期兼容性变化；因此更稳妥的做法是“内部名切换 + 旧名薄 wrapper + deprecate 注释”同批落地。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增对象绑定测试。 2. 运行负向测试，确认向 post-setup helper 传 `null` 只记录脚本错误，不再崩溃。 3. 运行 literal asset 现有/新增 happy-path 回归，确认生成器调用内部 helper 后资产创建与广播行为不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-35 | Defect | 立即修复，先收口公开 helper 上的空指针崩溃入口 |

---

## 发现与方案 (2026-04-08 17:29)

### Issue-36：热重载/丢弃后的 `UASFunction` 调用会静默 no-op，旧 `UFunction` 指针失效后没有任何错误信号

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 行号 | 5008-5016；149-155，475-482，1971-2004；294-335 |
| 问题 | `CleanupRemovedClass()` 在清理 removed/reloaded class 时，会把每个 `UASFunction` 的 `ScriptFunction` 直接置空。随后 `AngelscriptCallFromBPVM()`、`AngelscriptCallFromParms()`，以及 `UASFunction_NoParams::RuntimeCallFunction/RuntimeCallEvent()` 等入口在 `ScriptFunction == nullptr` 时都只是 `return`，既不记录错误，也不把“这个函数体已经失效”反馈给调用方。现有 hot-reload 场景测试只验证 reload 后重新查到的新 `GetValueAfterReload` 能执行，没有覆盖仍然持有 `GetValueBeforeReload` 旧指针时会发生什么。因此当前真实行为是：旧 `UFunction` 句柄失效后，`ProcessEvent`/native thunk 看起来调用成功，实际上函数体根本没有执行。 |
| 根因 | 类生成器把“脚本函数已失效”的状态编码为 `ScriptFunction = nullptr`，但 runtime call stub 把这个状态当成可静默跳过的正常分支处理，没有统一的 stale-function 错误上报或默认返回值收口。 |
| 影响 | 任何缓存旧 `UFunction*` 的路径都会在 hot reload、discard module 或 class cleanup 后进入“表面成功、实际 no-op”的状态，包括反射调用、delegate/事件桥接、蓝图/原生缓存句柄以及测试辅助层。相比显式失败，这种 silent failure 更难排查，因为调用点不会立刻暴露“函数已过期”的事实。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `ScriptFunction == nullptr` 从静默返回改成统一的 stale-function 错误路径，并为返回值提供稳定默认值。 |
| 具体步骤 | 1. 在 `ASClass.cpp` 提取统一 helper，例如 `static bool ReportMissingScriptFunctionAndSetDefault(UASFunction* Function, UObject* Object, void* ResultAddress)`：记录 `UE_LOG(Angelscript, Error, ...)` 或等价 runtime error，错误文本明确包含 `Function->GetPathName()` 与 `Object`；对有返回值的路径同步写入确定的默认值，避免把旧栈内容冒充有效结果。 2. 让 `AngelscriptCallFromBPVM()`、`AngelscriptCallFromParms()` 以及所有仍手写 `if (ScriptFunction == nullptr) return;` 的专用 `RuntimeCallFunction/RuntimeCallEvent` override 统一走这个 helper，而不是直接裸返回。 3. 对 `RuntimeCallFunction` 的 `RESULT_DECL` 路径，按 `ReturnArgument` 的类型信息做零值/默认值初始化；对 `RuntimeCallEvent` 的 `Parms` 路径，至少保证 `out`/return 槽位被清零，不能把调用前残留内存原样留给调用方。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` 扩展现有 `FAngelscriptScenarioHotReloadFunctionChangeTest`：保留 `GetValueBeforeReload` 旧指针，在 soft reload 后先确认 `Cast<UASFunction>(GetValueBeforeReload)->ScriptFunction == nullptr`，再通过 `Actor->ProcessEvent(GetValueBeforeReload, &OldResult)` 或等价 helper 触发一次 stale call，断言会记录预期错误并得到稳定默认返回值。 5. 顺手在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` 把 `ExecuteGeneratedIntEventOnGameThread` 的 `UASFunction` 分支更新为能感知 stale-function 失败，避免测试辅助层继续把“调用没有 crash”误报成成功。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 预估工作量 | M |
| 风险 | 把 silent no-op 改成显式错误后，现有依赖“旧函数调用只是无事发生”的工具链或测试会开始看到 error log；这是期望中的正确性提升，但需要同步更新辅助层对失败的判定。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与 hot-reload/test helper 相关测试。 2. 运行扩展后的 hot-reload 场景测试，确认 reload 后通过新 `UFunction` 仍返回更新值，而通过旧 `UFunction` 会稳定记录错误并返回默认值。 3. 复跑依赖 `ExecuteGeneratedIntEventOnGameThread` 的现有蓝图/delegate 测试，确认合法路径不回退，只有 stale-function 调用被新护栏拦下。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-36 | Defect | 紧随其后，消除热重载后旧函数句柄的静默失败路径 |

---

## 发现与方案 (2026-04-08 17:30)

### Issue-37：`FNumberFormattingOptions` fluent setter 丢失 `accept_temporary_this`，temporary builder 语义已弱于 UEAS2

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FNumberFormattingOptions.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FNumberFormattingOptions.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 行号 | 35-41；35-41；4676-4677；2879-2883；177-207 |
| 问题 | current 的七个 fluent setter 仍然全部返回 `FNumberFormattingOptions&`，但签名已经不再带 UEAS2 仍保留的 `accept_temporary_this`。编译器实现明确表明：只有带该 decorator 的方法才会写入 `asTRAIT_ACCEPT_TEMPORARY_OBJECT`；否则对 temporary object 调用 non-const method 会直接报 `"Cannot call non-const method on a temporary object"`。现有 compat 测试只覆盖了命名变量 `Options.SetAlwaysSign(...).SetUseGrouping(...)` 的 happy path，没有任何 `FNumberFormattingOptions().SetAlwaysSign(true)...` 这类 temporary builder 回归，因此这条返回值语义回退目前完全未被锁定。 |
| 根因 | `Bind_FNumberFormattingOptions.cpp` 在迁移 fluent API 时保留了引用返回类型，却把决定 temporary-this 语义的 decorator 一并删掉，导致声明和预期使用方式分叉。 |
| 影响 | 依赖 builder 风格 temporary 的脚本写法会在 current 上直接编译失败，而同一批 API 在 UEAS2 明确可用。结果是 `FNumberFormattingOptions` 表面上仍像 fluent builder，实际却只能在命名变量上链式调用，脚本语义弱于参考实现，也弱于 C++ 直觉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复七个 setter 的 `accept_temporary_this`，并补一条专门覆盖 temporary builder 的回归测试。 |
| 具体步骤 | 1. 将 `Bind_FNumberFormattingOptions.cpp:35-41` 的七个方法声明全部改成与 UEAS2 对齐的 `... accept_temporary_this` 版本，底层 `METHOD_TRIVIAL` 保持不变。 2. 确认这些方法注册后都获得 `asTRAIT_ACCEPT_TEMPORARY_OBJECT`；如有必要，在 bind trait 测试里直接断言该 trait，而不是只依赖 compile smoke。 3. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp:177-207`：新增脚本片段 `FNumberFormattingOptions Temp = FNumberFormattingOptions().SetAlwaysSign(true).SetUseGrouping(false).SetMinimumIntegralDigits(2);`，再与命名变量构造出的 `Options` 做 `IsIdentical()` 对比，确认 temporary builder 既能编译也保持相同行为。 4. 若团队担心同类问题继续散落，顺手 grep 其它返回 `Type&` 的 value-type fluent API，复核哪些本来就应支持 temporary this，避免 `FString`/`FEventReply` 之外又漏掉新的 builder surface。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FNumberFormattingOptions.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 兼容性风险很低；主要变化是把先前错误拒绝的 temporary builder 写法重新放开。需要注意不要把本来不该允许 temporary-this 的 mutating API 误一起放开。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 core misc binding 测试。 2. 运行新增用例，确认 temporary builder 脚本能够通过编译并与命名变量路径产生相同结果。 3. 如加入 trait 断言，再确认七个 setter 都带 `asTRAIT_ACCEPT_TEMPORARY_OBJECT`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-37 | Defect | 在高优缺陷收口后修复，补回 UEAS2 fluent builder 语义 |

---

## 发现与方案 (2026-04-08 17:37)

### Issue-38：literal asset 软重载未重建 script object，构造/默认值语义会跨版本残留

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/ClassGenerator/ASClass.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/ASClass.cpp` |
| 行号 | 674-682；74-115；475-489；80；1131-1139 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 1。current 的 `__CreateLiteralAsset()` 在命中已有对象时只会遍历 `FProperty` 并执行 `CopyCompleteValue_InContainer(ExistingObject, CDO)`；当前 `UASClass` 头文件也已经没有 UEAS2 仍保留的 `ReconstructScriptObject(UObject*)` 入口。对照 UEAS2，同一条 soft-reload 路径会先对 `UASClass` 调用 `ReconstructScriptObject()`，该实现会连续执行 `CallDestructor()`、`ExecuteConstructFunction()`、`ExecuteDefaultsFunctions()`。也就是说，current 在 literal asset 软重载时只重置了反射属性，没有重建 script object 本体。 |
| 根因 | literal asset 热重载迁移时，只保留了“把属性恢复为 CDO 默认值”的表层逻辑，没有把 script object 生命周期重建链路一并迁回 runtime/class generator。 |
| 影响 | 任何依赖构造函数、defaults function、或 script object 非 `FProperty` 状态初始化的 literal asset，在软重载后都可能继续保留旧版本状态。结果是 asset 文本、CDO 默认值和真实运行态脚本对象发生分叉，而且这种分叉只会在 reload 路径出现，问题定位成本很高。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 UEAS2 的 `ReconstructScriptObject()` 能力恢复到 current `UASClass`，并在 literal asset soft-reload 分支里先重建 script object 再恢复属性。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` 补回 `void ReconstructScriptObject(UObject* Object);` 声明，放在 `RuntimeDestroyObject()` 附近，保持与 UEAS2 的 class-lifecycle API 分组一致。 2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` 增加实现，按 UEAS2 `1131-1139` 的顺序执行 `CallDestructor((asCObjectType*)ScriptTypePtr)`、`ExecuteConstructFunction(Object, this)`、`ExecuteDefaultsFunctions(Object, this)`，并保留 `ScriptTypePtr == nullptr` 的早退保护。 3. 修改 `Bind_UObject.cpp:674-682` 的 soft-reload 分支：若 `AssetClass` 是 `UASClass`，先调用 `ScriptAssetClass->ReconstructScriptObject(ExistingObject)`，再进入属性恢复循环。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` 增加 literal asset 热重载回归，用一个带脚本构造/默认值副作用的 asset class 连续执行两次 `__CreateLiteralAsset()`，断言第二次 reload 后脚本态被重新初始化，而不是保留第一次运行留下的状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 补回重建链路后，当前若已有代码依赖“reload 不会重跑构造/defaults”这一错误行为，行为会被收紧；同时要确认 `CallDestructor()` 不会误清理 editor-only / native-owned 状态。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 object binding 测试。 2. 运行 literal asset reload 回归，确认第二次 `__CreateLiteralAsset()` 后脚本构造计数、默认值和首次运行态不同步残留的问题消失。 3. 额外验证非 `UASClass` 资产仍保持当前 reload 行为，不引入 native asset 回归。 |

### Issue-39：literal asset 重置会无条件覆盖 instanced subobject 属性，软重载可直接破坏对象所有权

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 676-682；481-487 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 2。current 的 `__CreateLiteralAsset()` 在 soft-reload 分支里遍历 `AssetClass` 全部 `FProperty`，对每个属性都直接执行 `Prop->CopyCompleteValue_InContainer(ExistingObject, CDO)`。UEAS2 同一段逻辑则先判断 `!Prop->ContainsInstancedObjectProperty()`，明确跳过带 instanced object 的属性。也就是说，current 会把 instanced subobject / instanced reference 直接从 CDO 覆盖回运行中 asset，而参考实现把这类所有权敏感属性当成特例保护。 |
| 根因 | 迁移 literal asset reset 逻辑时，只保留了统一 property-copy 的便利写法，没有保留 UEAS2 针对 instanced ownership 的防线。 |
| 影响 | 对含有 instanced subobject、instanced component 或嵌套 instanced 引用的 asset，soft-reload 可能把运行中对象直接替换成 CDO 持有的默认实例，导致子对象身份变化、外部引用悬空、编辑器状态丢失，甚至把本该唯一拥有的 subobject 共享到多个 asset 实例。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 literal asset 的默认值恢复逻辑与 UEAS2 对齐，显式跳过 `ContainsInstancedObjectProperty()`，只重置可安全按值复制的属性。 |
| 具体步骤 | 1. 修改 `Bind_UObject.cpp:678-681` 的属性恢复循环，加入 `if (Prop->ContainsInstancedObjectProperty()) { continue; }`，其余属性维持 `CopyCompleteValue_InContainer()`。 2. 若团队担心后续继续在别处手写同类循环，可把这段逻辑提取成 bind 内部 helper，例如 `ResetLiteralAssetPropertiesFromCDO(UObject* ExistingObject, UClass* AssetClass)`，统一封装“跳过 instanced property”的规则。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` 增加 literal asset reload regression：构造一个带 `Instanced` 子对象属性的测试 asset，第一次创建后修改该子对象状态，再执行第二次 `__CreateLiteralAsset()`，断言子对象实例身份不被 CDO 替换，同时普通按值属性仍会恢复默认值。 4. 若当前测试基础设施不便直接声明测试 asset class，可在 test fixture 里使用最小 native test UObject + `Instanced` 属性，避免把验证工作推迟到手工编辑器检查。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险主要在于个别调用方可能错误依赖“instanced 属性也会被 reset”这一现状；修复后这些属性会保留原实例，需要在测试里同时证明普通 value property 仍然继续恢复默认值。 |
| 前置依赖 | 建议与 Issue-38 一起实施，避免只修一半后让 soft-reload 仍保留脚本态残留。 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与 object binding 测试。 2. 运行新增 instanced-asset reload 用例，确认 reload 后 instanced 子对象地址保持稳定，普通属性回到默认值。 3. 在 editor 下重复 soft-reload 一次，确认不会新增 redirector / ownership 异常日志。 |

### Issue-40：`UObject::opCast` 对空句柄不做保护，`Cast<T>(null)` 会先在绑定层解引用崩溃

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` |
| 行号 | 135-219；68-70，137-139 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 15。`UObject_.Method("void opCast(?& Address) const", ...)` 在完成 handle/type 过滤后，直接执行 `const bool bIsA = Object->IsA(AssociatedClass);`，下一行又会在接口分支访问 `Object->GetClass()->ImplementsInterface(AssociatedClass)`。整个函数没有任何 `if (Object == nullptr)` 保护，也没有在早退前把 `OutAddress` 预置为 `nullptr`。与此同时，现有 `AngelscriptInterfaceCastTests.cpp` 只覆盖了 `Self = this` 的成功/失败 cast，用例中的输入都明确是非空对象，没有任何 `Cast<...>(null)` 回归。 |
| 根因 | 通用 UObject cast helper 只考虑了“目标类型是否合法”，没有把“源对象句柄可能为空”纳入脚本层的基础失败语义；同时缺少把输出地址默认清空的防御式写法。 |
| 影响 | 任何脚本里符合直觉的空句柄转型，例如 `UMyType Obj = Cast<UMyType>(MaybeNull);`，都可能在绑定层先触发空指针解引用，而不是稳定返回 `null` 让脚本自行处理。由于 `opCast` 是 UObject 向下转型的公共底座，问题影响普通类 cast、接口 cast 以及相关调试日志路径。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 给 `opCast` 增加统一的空输入早退和默认 `nullptr` 输出，把空句柄 cast 恢复成脚本侧可处理的失败语义。 |
| 具体步骤 | 1. 在 `Bind_UObject.cpp:136-219` 的 lambda 开头先执行 `*(UObject**)OutAddress = nullptr;`，保证所有早退分支都不会留下未定义输出。 2. 在完成 `TypeId`/`ScriptType`/`AssociatedClass` 校验后、首次访问 `Object->IsA(...)` 之前加入 `if (Object == nullptr) return;`，让空源对象直接按“cast 失败”处理。 3. 保留现有接口 cast 逻辑，但把 `Object->GetClass()` 访问收敛到 `Object != nullptr` 之后，避免后续 debug log 或 interface 分支再次产生裸解引用。 4. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp`，增加两个 smoke：一个验证 `Cast<UIDamageableCastOk>(UObject(null))` 返回 `null`；另一个验证 `Cast<AScenarioInterfaceCastSuccess>(UObject(null))` 同样返回 `null`，并确认脚本可继续执行到结尾。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是把此前“直接崩溃”的路径改成“稳定返回 null”；若有现存日志依赖当前异常崩溃点，堆栈会发生变化，但这是期望修复。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与 interface cast 测试。 2. 运行新增 null-cast 用例，确认 cast 结果为 `null` 且自动化测试进程不中断。 3. 回归现有 success/fail cast 用例，确认非空对象的接口转型语义不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-40 | Defect | 立即修复，先把 `Cast<T>(null)` 从崩溃收口成稳定返回 `null` |
| P1 | Issue-38 | Defect | 紧随其后，恢复 literal asset 软重载的 script-object 重建语义 |
| P1 | Issue-39 | Defect | 与 Issue-38 同一个改动窗口内完成，补回 instanced-property 所有权保护 |

---

## 发现与方案 (2026-04-08 17:51)

### Issue-41：`System::SetTimer` 对 bad object / bad function 只返回无效 `FTimerHandle`，绑定层没有把失败升级成脚本可见错误

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SystemTimers.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/KismetSystemLibrary.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTimersTest.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp` |
| 行号 | 10-14；667-739；22-41；49-59 |
| 问题 | 当前 `System::SetTimer` 只是把脚本参数直接转发给 `UKismetSystemLibrary::K2_SetTimer(...)`。引擎实现表明：当 `Object` 为空、函数名不存在，或函数签名不匹配时，这条路径只会写 warning，并返回默认构造的无效 `FTimerHandle`；真正成功调度 timer 的前提是 `Delegate.IsBound()` 且 world-context 有效。绑定层没有在调用前检查 `Object` / `FunctionName`，也没有在拿到无效 handle 后抛出脚本错误。现有两个测试文件都只覆盖了 happy path：`this + 正确函数名`，没有任何 bad object / bad function 的负向回归。 |
| 根因 | current 新增 `Bind_SystemTimers.cpp` 时复用了 Blueprint `K2_SetTimer` 的“日志 + 无效 handle”失败模型，但没有像 `SpawnActor`、`GetComponentsByClass` 那样在脚本 binding 层补一层显式参数校验与错误上报。 |
| 影响 | 脚本写出 `System::SetTimer(null, ...)`、`System::SetTimer(this, n"MissingFunction", ...)` 或把带参数函数名传进去时，不会得到同步脚本错误，只会悄悄拿到一个无效 handle。若调用方又像 `AngelscriptScriptExampleTimersTest.cpp:22` 那样直接忽略返回值，失败就会退化成“定时器永远不触发”的 silent failure，排查成本很高。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 binding 层把 timer 创建前置校验和失败句柄检查补齐，并把返回值收紧成必须消费的契约。 |
| 具体步骤 | 1. 将 `Bind_SystemTimers.cpp:10-14` 的声明改成 `FTimerHandle SetTimer(const UObject Object, const FName& FunctionName, float32 Time, bool bLooping = false) no_discard`，防止脚本继续无提示地忽略 handle。 2. 在 lambda 内先检查 `Object != nullptr`；为空时 `FAngelscriptEngine::Throw("SetTimer received a null object.");` 并返回默认 `FTimerHandle()`。 3. 在调用 `K2_SetTimer` 之前使用 `Object->FindFunction(FunctionName)` 做本地预检：未命中时抛 `SetTimer could not find function '<Name>'.`；命中但 `ParmsSize > 0` 时抛 `SetTimer requires a zero-parameter UFUNCTION.`，不要依赖底层 warning。 4. 调用 `K2_SetTimer` 后补一层 `if (!Handle.IsValid())` 检查；若仍失败，再抛统一错误，例如 `SetTimer failed to create a valid timer handle.`，把 world-context 无效等剩余失败面收口为脚本错误。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSystemTimerBindingsTests.cpp` 增加三组回归：`null object`、不存在函数名、带参数函数名都必须记录脚本错误且返回无效 handle；保留一条 happy path 验证合法 `UFUNCTION()` 仍能成功触发。 6. 如团队担心与 Blueprint 语义完全分叉，可在错误文本里保留底层失败原因关键字，但不要再让日志 warning 成为脚本侧唯一诊断入口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SystemTimers.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSystemTimerBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 修复后，历史上依赖“失败时只是没有回调”的脚本会开始在运行时显式报错；这是期望中的契约收紧，但需要同步更新示例/文档，说明 `System::SetTimer` 现在会主动拒绝坏函数名和空对象。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 timer binding 测试。 2. 运行负向测试，确认 bad object / bad function 都会产生脚本错误且返回无效 handle。 3. 回归 `AngelscriptScriptExampleTimersTest` 与 `AngelscriptLearningTimerAndLatentTraceTests`，确认合法 timer 仍能正常创建并触发回调。 |

### Issue-42：`WaitGameplayTagQueryOnActor` 在 GAS wrapper 头文件里被 stub 成 `nullptr`，handwritten bind 也完全漏注册

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/Abilities/Async/AbilityAsync_WaitGameplayTagQuery.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | 58-67；9-12；10-21；219-238，443-455 |
| 问题 | `UAngelscriptAbilityAsyncLibrary` 头文件公开了 `WaitGameplayTagQueryOnActor(...)`，但函数体直接把真正的引擎调用注释掉并 `return nullptr;`。与此同时，`Bind_AngelscriptGASLibrary.cpp` 只手工注册了前四个 async helper，完全没有把 `WaitGameplayTagQueryOnActor` 加进 `AddFunctionEntry(...)`。引擎原生实现却明确存在，并会创建并返回 `UAbilityAsync_WaitGameplayTagQuery` 对象。当前 generated-function-table 测试也只断言 `WaitForAttributeChanged` 和 `WaitGameplayTagRemoveFromActor` 仍然存在，没有任何覆盖 `WaitGameplayTagQueryOnActor` 的注册或可调用性。 |
| 根因 | GAS handwritten compatibility layer在扩展 async helper 时停在“四个常用节点”，后续新增 `WaitGameplayTagQueryOnActor` 只在 wrapper 头文件里留下了占位 API，没有把真实实现和 bind registration 一起补完，形成“头文件声明、bind 表、运行时实现”三方失配。 |
| 影响 | 当前脚本或生成代码即使能看到 `WaitGameplayTagQueryOnActor` 这个公开 API，也只能得到 `nullptr` 或在 handwritten function table 上完全找不到 direct-call entry，导致基于 gameplay tag query 的 async 等待能力在 Angelscript 侧实质不可用。更糟的是，这不是显式 compile error，而是一个看起来存在、运行时却什么也不返回的 silent capability hole。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 GAS wrapper、handwritten bind 表和引擎原生 async 节点重新对齐，补回真实实现与回归测试。 |
| 具体步骤 | 1. 将 `AngelscriptAbilityAsyncLibrary.h:58-67` 的 stub 改为直接转发 `UAbilityAsync_WaitGameplayTagQuery::WaitGameplayTagQueryOnActor(TargetActor, Query, TriggerCondition, bTriggerOnce)`，删除注释掉的旧代码。 2. 在 `Bind_AngelscriptGASLibrary.cpp:9-12` 后补一条 `AddFunctionEntry(...)`：键名为 `WaitGameplayTagQueryOnActor`，签名对齐 `(AActor*, const FGameplayTagQuery, const EWaitGameplayTagQueryTriggerCondition, const bool)`，返回 `UAbilityAsync_WaitGameplayTagQuery*`。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 扩展现有 GAS 兼容测试：显式断言 `AsyncLibraryFunctionMap->Find(TEXT("WaitGameplayTagQueryOnActor"))` 非空、`FuncPtr.IsBound()` 为 true，且不会被标记成 reflective fallback。 4. 新增一条 runtime 绑定测试，例如 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASBindingsTests.cpp`，编译并执行最小脚本片段 `auto Task = UAngelscriptAbilityAsyncLibrary::WaitGameplayTagQueryOnActor(this, Query);`，断言返回对象非空且类型为 `UAbilityAsync_WaitGameplayTagQuery`。 5. 如果当前项目暂时不准备支持该节点，就反过来删掉 wrapper 头文件里的公开 `UFUNCTION`，并在 generated function table 测试中显式断言它不存在；不能继续维持“头文件声明存在但实现恒为 null”的半完成状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果团队之前故意注释掉该调用是因为某个未解决的生命周期问题，直接放开后可能会暴露更底层的 GAS/ASC 使用前提；因此第 4 步必须用真实脚本回归验证 task 对象至少能稳定创建，再决定是否继续扩展行为测试。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增/扩展后的 GAS 测试。 2. 运行 generated-function-table 测试，确认 `WaitGameplayTagQueryOnActor` 出现在 handwritten map 且 direct pointer 已绑定。 3. 运行 runtime 绑定测试，确认脚本调用后拿到非空 `UAbilityAsync_WaitGameplayTagQuery`，不再返回 `nullptr`。 |

### Issue-43：`Bind_UObject.cpp` 的 class lookup/filter 被复制成 5 份，导致 lookup 语义修一处漏四处

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | 331-398；519-553 |
| 问题 | `Bind_UObject.cpp` 当前把 class lookup/filter 逻辑散落在至少 5 个 lambda 里：namespace `UClass::FindClass()`、namespace `GetAllClasses()`、`GetAllSubclassesOf()`、`__StaticClass()`，以及全局 `FindClass()` / `GetAllClasses()`。其中 `GetAllClasses()` 两份实现甚至是逐行复制；`FindClass()` 的 namespace 版直接 `FindObject<UClass>`，全局版却改成 `TObjectIterator + GetBoundClassName()`；`GetAllSubclassesOf()`、`__StaticClass()` 又各自维护一套更弱的过滤条件。结果是同一文件内没有任何“哪些类允许暴露给脚本 lookup”统一判定，任何一次 tombstone / deprecation / naming 规则修正，都必须手工同步多份扫描循环。 |
| 根因 | 绑定层在 `UClass` 命名空间 helper 和全局 helper 之间采用了就地复制实现，而不是抽取共享 predicate / iterator helper；随着 current 增加 `FindClass`、`GetAllClasses`、`__StaticClass` 等新 surface，复制体开始各自演化，形成结构性漂移。 |
| 影响 | 这类复制已经直接放大了 lookup 相关缺陷的修复面：同一个 class 生命周期或命名规则，一旦只在某一份循环里修复，就会在其它入口继续回流旧行为。继续按现状追加 helper，只会让 `Bind_UObject.cpp` 里的 lookup 语义越来越难验证，后续 discovery 也更容易重复在不同 surface 上发现同类问题。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把 class 可见性和名字匹配规则抽成共享内部 helper，再让 namespace/global lookup surface 全部复用同一实现。 |
| 具体步骤 | 1. 在 `Bind_UObject.cpp` 的 `Bind_UClass_Base` 前后新增两个内部 helper，例如 `static bool ShouldExposeClassToLookup(UClass* Class, bool bIncludeAbstractClasses)` 和 `static UClass* FindExposedClassByName(const FString& Name, bool bAcceptBoundName)`；前者统一处理 `nullptr`、`CLASS_Deprecated`、`CLASS_NewerVersionExists`、abstract 以及 script tombstone 过滤，后者统一做名称匹配与继续搜索。 2. 将 `331-398` 与 `519-553` 这几份 lambda 全部改成调用 helper：namespace/global `FindClass()` 只保留“是否接受 bound name”的差异，`GetAllClasses()` 两份实现直接复用同一 `EnumerateExposedClasses(...)`，`GetAllSubclassesOf()` 和 `__StaticClass()` 也不再手写扫描循环。 3. 让 `GetAllClasses()` 的 namespace/global 版本在实现上只保留一个实体，另一边变成薄 wrapper，避免后续继续出现“修了一份忘了另一份”。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 增加 lookup consistency 回归：同一组类名分别走 namespace `UClass::FindClass`、全局 `FindClass`、`__StaticClass`、`GetAllClasses` / `GetAllSubclassesOf`，断言它们对 visible / hidden / removed / native class 的可见性判定一致。 5. 在实现说明里把“class lookup 统一走 helper，不再允许手写 `TObjectIterator<UClass>` 过滤”记成 bind 约束，后续新增 class helper 时强制复用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 重构会同时触碰多个 lookup surface，短期内可能让现有依赖某个入口“特例行为”的脚本或测试暴露差异；因此第 4 步必须把 namespace/global 行为一起锁住，避免修复过程中引入新的语义分叉。 |
| 前置依赖 | 建议与已有 class lookup/tombstone 系列缺陷一起实施，同一窗口内统一收口。 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 class binding 测试。 2. 运行 lookup consistency 回归，确认 namespace/global `FindClass`、`__StaticClass`、`GetAllClasses`、`GetAllSubclassesOf` 对同一类集合返回一致结果。 3. 复查 `Bind_UObject.cpp`，确认不再存在第二份复制的 `GetAllClasses()` 扫描循环。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-41 | Defect | 立即修复，先把 `System::SetTimer` 的 silent failure 收口成脚本错误 |
| P1 | Issue-42 | Defect | 紧随其后，补回 `WaitGameplayTagQueryOnActor` 的真实实现与 bind 注册 |
| P2 | Issue-43 | Refactoring | 在前两项落地后执行，统一 `Bind_UObject.cpp` 的 class lookup/filter helper |

---

## 发现与方案 (2026-04-08 18:04)

### Issue-44：`GetCurrentWorld()` 公开返回 `null`，但同文件 `UWorld` helper 仍把接收者当成必非空对象

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 行号 | 38-42，54-85；180 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 75。`GetCurrentWorld()` 明确调用 `GetWorldFromContextObject(..., EGetWorldErrorMode::ReturnNull)`，把“当前没有有效 world”公开成可空契约；但同文件后面的 `GetGameState()`、`IsStartingUp()`、`IsTearingDown()`、`SetGameInstance()`、`GetGameInstance()`、`GetLevelScriptActor()`、`GetPersistentLevel()` 全部直接解引用 `World`。现有测试只覆盖了 `GetWorld().GetPersistentLevel().GetActors().Num()` 的 happy path，没有任何无 world-context 的链式调用回归。 |
| 根因 | bind 层把 world 获取 helper 和 world 实例 helper 分开注册，却没有为“nullable world handle 继续链式调用”定义统一的接收者防线。 |
| 影响 | 脚本一旦写出 `GetCurrentWorld().GetPersistentLevel()`、`GetCurrentWorld().GetGameState()` 这类自然链式调用，在 world-context 缺失时就不会停留在 `null` 语义，而会直接跌进 method lambda 的裸解引用。问题表面上来自业务脚本，真正根因却埋在 bind 层的接收者契约不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 保持 `GetCurrentWorld()` 的 nullable accessor 语义，但让后续 `UWorld` helper 对空接收者执行统一错误收口，而不是直接崩溃。 |
| 具体步骤 | 1. 在 `Bind_UWorld.cpp` 中提取一个内部 helper，例如 `static bool ResolveWorldReceiverOrThrow(UWorld* World, const ANSICHAR* FunctionName)`，当 `World == nullptr` 时统一 `FAngelscriptEngine::Throw("World was null.");` 并返回 `false`。 2. 让 `GetGameState()`、`IsStartingUp()`、`IsTearingDown()`、`GetGameInstance()`、`GetLevelScriptActor()`、`GetPersistentLevel()` 改用显式 lambda 包装；receiver 为空时分别返回 `nullptr` / `false` / `nullptr`，而不是继续裸解引用。 3. 由于 `SetGameInstance()` 已在 Issue-17 中建议从公开 surface 移除，这里不再单独给它补 null-guard；若短期内还不能移除，就先把它也接到同一 helper，保证空 world 至少报脚本错误。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` 增加 negative smoke：构造一个没有 world-context 的执行窗口，调用 `UWorld Current = GetCurrentWorld();` 后继续访问 `Current.GetPersistentLevel()` / `Current.GetGameState()`，断言记录 `"World was null."` 且返回稳定默认值。 5. 保留现有 `AngelscriptSubsystemScenarioTests.cpp:180` 的 happy path，并追加一条 `GetCurrentWorld()` 正向 smoke，确认 guard 落地后正常 world 场景不回退。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 预估工作量 | M |
| 风险 | 主要风险是把原先“直接崩溃”的路径收口成脚本错误后，现有依赖崩溃栈定位问题的调试脚本会看到新的错误文本；需要在测试里锁定返回默认值与错误文案，避免后续再次漂移。 |
| 前置依赖 | 建议与 Issue-5、Issue-17 一起排期，统一收口 world accessor 的生命周期与所有权语义。 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 world binding 测试。 2. 运行无 world-context 的 negative smoke，确认 `GetPersistentLevel()` / `GetGameState()` 不再崩溃，而是记录脚本错误并返回默认值。 3. 回归 subsystem/native world happy-path 测试，确认正常 world 上的查询行为不变。 |

### Issue-45：`__StaticClass` 无条件公开，直接绕过 `StaticClassDeprecation` 的迁移/禁用策略

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 382-392；687-694；94-98；438-470 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 14。current 已经用 `StaticClassDeprecation` 配置控制 namespace `StaticClass()` 是否注册以及是否打 deprecated 提示，但 `Bind_UObject.cpp` 仍无条件注册 `UClass __StaticClass(const FString& Name)`，并且它直接按名字扫描 `UClass` 返回结果，不经过任何配置判断。现有 class binding 测试只验证 namespace `StaticClass()` 和 `__StaticType_AActor`，没有覆盖 `__StaticClass` 与配置项之间的关系。 |
| 根因 | `StaticClass()` 的弃用/禁用策略被做在 `Bind_BlueprintType.cpp`，而字符串 lookup helper 被单独塞进 `Bind_UObject.cpp`，两条“取 class 的旧式入口”没有共享同一个 policy gate。 |
| 影响 | 即使项目把 `StaticClassDeprecation` 设成 `Disallowed`，脚本仍可通过 `__StaticClass("AActor")` 拿到同一信息，等价于给旧调用风格保留了一条永久旁路。结果是配置面无法真正推动迁移，团队会看到“设置已禁用，但脚本照样能写”的策略失真。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `__StaticClass` 与 namespace `StaticClass()` 走同一套配置策略；若它只服务兼容桥接，就把它内化为内部 helper，不再作为公开脚本 API 常驻。 |
| 具体步骤 | 1. 在 `Bind_UObject.cpp` 中把 `__StaticClass` 的实际查找逻辑提取成内部 helper，例如 `static UClass* FindStaticClassByLegacyName(const FString& Name)`，避免后续 policy 与查找实现继续耦在同一个 lambda。 2. 注册公开 `__StaticClass` 前读取 `FAngelscriptEngine::Get().ConfigSettings->StaticClassDeprecation`：当模式为 `Disallowed` 时直接不注册；当模式为 `Deprecated` 时注册后立即 `FAngelscriptBinds::DeprecatePreviousBind("Types can now be used as values directly")`，与 `Bind_BlueprintType.cpp:687-694` 使用同一迁移文案。 3. 若预处理器或旧脚本兼容层仍需要这个 helper，把公开名字改成 `__Internal_StaticClass`，只让内部重写/兼容路径调用；普通脚本 surface 不再看到它。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 增加配置回归：分别在 `Allowed`、`Deprecated`、`Disallowed` 三种模式下重建测试 engine，断言 `GetGlobalFunctionByDecl("UClass __StaticClass(const FString& Name)")` 的存在性/弃用行为与 `StaticClass()` 一致。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 补一条使用 `__StaticClass("AActor")` 的 compile smoke，仅在 `Allowed` 模式下保留；防止未来修配置时把 helper 完全弄坏却没人发现。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果仓内已有脚本或生成代码依赖公开 `__StaticClass`，在 `Disallowed` 模式下它们会开始编译失败；这是预期中的迁移暴露，但需要先确认是否存在内部兼容链依赖。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增配置测试。 2. 在 `Allowed/Deprecated/Disallowed` 三种配置下运行 bind config 回归，确认 `__StaticClass` 的可见性与 `StaticClass()` 同步。 3. 回归现有 `StaticClass()` / `__StaticType_*` 测试，确认正常 class lookup 不受影响。 |

### Issue-46：actor/component 查询 surface 丢掉 `const` 与 `no_discard`，只读查询契约已弱于 UEAS2

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Actor.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds/Bind_Actor.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UActorComponent.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 8-19，32-35；127-184，347-357，385-387；8-19，32-35；127-184，332-338，366-367；132-145 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 32。current 把 `FAngelscriptActorBinds::GetComponent`、`GetAllComponents`、`GetComponentFromMeta`、`GetComponentGeneric`、`GetAllComponentsGeneric` 的 actor 参数都改成了 `AActor*`；对应脚本声明也一起回退成 `AActor.GetComponent(...)` 非 `const` 且不再 `no_discard`、`AActor.GetAllComponents(...)` 非 `const`，component namespace 的 `ClassName::Get(const AActor Actor, ...)` 也去掉了 `no_discard`。UEAS2 同一组声明仍然是 `const AActor*`、`const` method，并对 `GetComponent` 与 `ClassName::Get` 标了 `no_discard`。现有 native engine 绑定测试只覆盖了 `GetOwner().GetComponent(...)` 的可变 actor happy path，没有任何 const-context 或返回值契约回归。 |
| 根因 | 组件查询 helper 在 current 中沿着“与创建/变更组件共享同一绑定结构”演化，导致只读查询路径也被一起拖成可变签名，并顺手丢掉了原本用于保护误用的 `no_discard`。 |
| 影响 | UEAS2 下合法的 `const AActor` 只读查询代码在 current 会直接遇到签名不匹配；同时脚本忽略 `GetComponent()` / `USceneComponent::Get(...)` 的返回值时不再有编译期提示。结果是 component query surface 同时失去 const-correctness 和结果消费契约，语义明显弱于参考实现。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把纯查询 helper 的 native 签名和脚本声明恢复到 UEAS2 的 `const` / `no_discard` 语义，只保留创建类 API 为可变入口。 |
| 具体步骤 | 1. 在 `Bind_Actor.h` 中将 `GetComponent`、`GetAllComponents`、`GetComponentFromMeta`、`GetComponentGeneric`、`GetAllComponentsGeneric` 的 actor 参数改回 `const AActor*`；`CreateComponent` / `GetOrCreateComponent` 继续保持可变签名。 2. 在 `Bind_UActorComponent.cpp` 中同步恢复对应实现签名：`GetComponent(const AActor* ...)`、`GetAllComponents(const AActor* ...)`、`GetComponentFromMeta(..., const AActor* ...)`；保证内部仍只做只读查询，不触发任何 actor 变更。 3. 把脚本声明改回 UEAS2 语义：`AActor_.Method("UActorComponent GetComponent(...) const no_discard", ...)`、`AActor_.Method("void GetAllComponents(...) const", ...)`，以及 component namespace 的 `ClassName::Get(const AActor Actor, ...) no_discard`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` 增加 const-context smoke：`const AActor Owner = GetOwner(); UActorComponent Found = Owner.GetComponent(USceneComponent::StaticClass());` 与 `USceneComponent::Get(Owner)` 都必须编译并返回有效组件。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 增加 trait/introspection 回归：通过 `AActor` 类型信息与 `USceneComponent` namespace 函数声明拿到绑定后的 `asCScriptFunction`，断言 `traits.GetTrait(asTRAIT_NODISCARD)` 为 true，防止后续再次悄悄掉回普通返回值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Actor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | M |
| 风险 | 主要风险是少量 current 脚本如果已经依赖“只有可变 actor 才能调用 GetComponent”这一错误现状，修复后会放宽而不是收紧；真正需要注意的是不要把 `GetOrCreateComponent` / `CreateComponent` 一并误改成 `const`。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 native engine/bind config 测试。 2. 运行 const-context smoke，确认 `const AActor` 仍可查询组件。 3. 运行 trait/introspection 回归，确认 `GetComponent` 与 namespace `ClassName::Get` 都重新带回 `asTRAIT_NODISCARD`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-45 | Architecture | 优先处理，先堵上 `StaticClassDeprecation` 被 `__StaticClass` 架空的策略旁路 |
| P1 | Issue-44 | Defect | 紧随其后，统一收口 nullable `UWorld` accessor 的空接收者崩溃 |
| P2 | Issue-46 | Defect | 在前两项完成后修复，恢复 actor/component 查询的 `const` / `no_discard` 契约 |

---

## 发现与方案 (2026-04-08 18:20)

### Issue-47：`GetAllActorsOfClass*` 在无效 world-context 时只退化成 UE warning + 空数组，和同文件 `SpawnActor` 的脚本异常语义分叉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/GameplayStatics.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 317-446；1097-1183；93-105 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 34。`Bind_AActor.cpp` 的三个公开查询入口 `GetAllActorsOfClass(?&)`、`GetAllActorsOfClass(UClass, ?&)`、`GetAllActorsOfClassWithTag(FName, ?&)` 在完成数组类型校验后，直接把 `FAngelscriptEngine::TryGetCurrentWorldContextObject()` 传给 `UGameplayStatics::GetAllActorsOfClass*`。而引擎实现会在 `GEngine->GetWorldFromContextObject(..., EGetWorldErrorMode::LogAndReturnNull)` 失败时仅写 warning 并返回空数组。对照同文件 166-253 的 `SpawnActorFromMeta()` / `SpawnActor()` / `SpawnPersistentActor()`，后者都会先收口 world-context 并 `Throw("Invalid World Context")`。结果是 actor bind 在同一文件内同时暴露“spawn 失败显式抛错”和“query 失败静默空结果”两套契约。 |
| 根因 | actor 查询路径直接复用了 `UGameplayStatics` 的默认 world-context 行为，没有复用绑定层已经在 spawn helper 上建立的 Angelscript 级错误收口。 |
| 影响 | 脚本在 constructor、工具上下文或错误的 engine scope 中调用 `GetAllActorsOfClass*` 时，调用方只能看到空数组，无法区分“世界里确实没有 actor”和“当前根本没有有效 world”。这会把真实上下文错误伪装成正常业务结果，而且仓库里现有测试也没有任何负向 world-context 回归来锁住该语义。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 actor 查询入口与同文件 spawn 入口共用 world-context 解析/抛错逻辑，失败时先清空输出数组再返回。 |
| 具体步骤 | 1. 复用 `Issue-5` 计划中的 `Bind_WorldContextHelpers.h`，提供可直接给 actor 查询使用的 `ResolveCurrentWorldOrThrow(UObject*& OutWorldContext, UWorld*& OutWorld)`；如果 `Issue-5` 尚未落地，就在本 issue 内先引入同名 helper。 2. 修改 `Bind_AActor.cpp:317-446` 的三个公开查询 lambda，在所有 `TypeId` / `UClass` 校验通过后先解析 world；解析失败时统一执行 `OutActors.Reset(); return;`，并依赖 helper 已经写出的 `"Engine was null."` / `"Invalid World Context"` 脚本错误。 3. 保留 `UGameplayStatics::GetAllActorsOfClass*` 作为真正查询实现，但禁止再把 ambient world-context 直接裸传进去；`__Actor_GetAllByClass` 若在 `Issue-16` 落地前仍保留，也必须接到同一 helper，避免继续留下 silent path。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` 新增 actor-query 负向回归，并复用 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h:93-105` 的 `FScopedTestWorldContextScope`：在无 world-context 的对象作用域里调用三条查询 API，断言都记录 `"Invalid World Context"`，且输出数组保持 `Num() == 0`。 5. 保留现有 happy-path 查询行为不变，新增一条正向 smoke，确认修复后有效 world 下的结果集仍与当前一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldContextHelpers.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 预估工作量 | M |
| 风险 | 兼容性风险在于部分脚本可能把“无效 world 返回空数组”当成现有行为；修复后这些调用会显式报错。需要在测试里同时锁住错误文本和 `OutActors` 的清空语义，避免后续再次回退成 warning-only。 |
| 前置依赖 | 优先复用 `Issue-5` 的 world-context helper；若该 helper 尚未实现，则在本 issue 中先落地它。 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 actor binding 测试。 2. 在无 world-context 场景下运行三条查询 API，确认日志出现 `"Invalid World Context"`，且输出数组被清空。 3. 在正常 world 下复跑现有 actor/component happy-path 绑定测试，确认查询结果没有功能回退。 |

### Issue-48：`GetSourceFilePath()` 只返回模块首文件，`Bind_UObject` 暴露的 source metadata 在多文件模块下会稳定指向错误脚本

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_typeinfo.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 行号 | 280-285，405-410；1497-1545；176-179；404-407；4342-4345；204-243；69-76 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 47。`Bind_UObject.cpp` 暴露给脚本的 `UClass::GetSourceFilePath()` / `UFunction::GetSourceFilePath()` 最终都落到 `ASClass.cpp`，而当前 `UASClass::GetSourceFilePath()` 与 `UASFunction::GetSourceFilePath()` 都直接返回 `Module->Code[0].AbsoluteFilename`。但 AngelScript 类型与函数本身都保留了声明节索引：`asCTypeInfo::scriptSectionIdx`、`asCScriptFunction::ScriptFunctionData::scriptSectionIdx`。同时 `AngelscriptEngine.cpp:4342-4345` 又明确按 `Module->Code` 顺序把每个 code section 送进 `AddScriptSection(...)`。这说明 current 已经持有足够的“声明实际来自哪一个脚本文件”的元数据，却完全没有使用。现有两个 source metadata 测试只覆盖单文件模块，因此这条错误路径一直没有暴露。 |
| 根因 | source metadata getter 只做了“模块级”定位，没有把 AngelScript 已经保存的 section-index 映射回 `FAngelscriptModuleDesc::Code`。 |
| 影响 | 只要一个 module 包含多个 `.as` 文件，后续文件里声明的 class/function 也会被导航、调试 UI 和脚本反射统一指向第一个文件。结果是 `GetSourceFilePath()` 表面可用，但在真实多文件项目里会系统性打开错文件。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `UASClass` / `UASFunction` 的 source-path getter 按声明 section 精确回查 `Module->Code`，不再硬编码 `Code[0]`。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` 提取共享 helper，例如 `static const FAngelscriptModuleDesc::FCodeSection* ResolveSourceSection(const TSharedPtr<FAngelscriptModuleDesc>& Module, int32 SectionIndex)`，统一做 `Module.IsValid()`、`SectionIndex >= 0`、`SectionIndex < Module->Code.Num()` 校验；越界时返回 `nullptr` 并带一条 `ensureMsgf`，避免继续悄悄掉回首文件。 2. 修改 `UASClass::GetSourceFilePath()` / `GetRelativeSourceFilePath()`：从 `((asCTypeInfo*)ScriptTypePtr)->scriptSectionIdx` 取声明 section，再通过 helper 返回对应 `AbsoluteFilename` / `RelativeFilename`。 3. 修改 `UASFunction::GetSourceFilePath()`：从 `((asCScriptFunction*)ScriptFunction)->scriptData->scriptSectionIdx` 取声明 section，再映射到正确的 code section；`ScriptFunction == nullptr` 时继续返回空串，把 hot-reload fallback 留给 `Issue-49` 单独处理。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h/.cpp` 新增一个最小 helper，例如 `CompileAnnotatedModuleFromFiles(...)`，支持把两个脚本文件装进同一个 module；不要继续让 source metadata 测试只能覆盖单文件 happy path。 5. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`：构造一个双文件 module，把 class/function 放在第二个文件里，断言 `Type.GetSourceFilePath()` 与 `Func.GetSourceFilePath()` 都命中第二个脚本路径，而不是模块首文件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 预估工作量 | M |
| 风险 | 主要风险是 section-index 与 `Module->Code` 顺序若出现未来变更，会让 helper 越界；因此必须把索引校验和多文件测试一起落地，不能只改 getter。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 source metadata/navigation 测试。 2. 运行双文件 module 回归，确认 class/function 的 `GetSourceFilePath()` 都指向实际声明文件。 3. 复跑现有单文件 metadata 测试，确认现有 happy-path 行为不回退。 |

### Issue-49：`GeneratedSourceLineNumber` 已被写入 `UASFunction`，但 `GetSourceLineNumber()` 在 `ScriptFunction` 失效后仍直接退化成 `-1`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 行号 | 413-418；123-124；1548-1558；3411-3417，4990-5016；241-243；118-181，254-272；75-76 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 48。`Bind_UObject.cpp` 的 `UFunction::GetSourceLineNumber()` 最终调用 `UASFunction::GetSourceLineNumber()`，而当前实现只要 `ScriptFunction == nullptr` 就立刻返回 `-1`。但 `UASFunction` 自己已经持有 `GeneratedSourceLineNumber` 字段，并在 class generator 生成 `UASFunction` 时明确写入 `FunctionDesc->LineNumber + 1`。同时 `CleanupRemovedClass()` 在热重载/删除时只清空 `Function->ScriptFunction`，并不会清掉这份缓存。这意味着 current 已经保存了“最后一次生成时的源码行号”，却完全没有读取路径。 |
| 根因 | 元数据写入链路和读取链路脱节：generator 负责缓存 `GeneratedSourceLineNumber`，getter 却仍然只信任 live `asCScriptFunction`。 |
| 影响 | 只要函数进入“`UASFunction` 仍在，但 `ScriptFunction` 已被 cleanup 清空”的状态，脚本反射、编辑器 source navigation 和测试都会无条件拿到 `-1`，即使对象里仍保留着最近一次有效编译的源码行号。这会把 hot-reload 之后最有价值的定位信息直接丢掉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `GeneratedSourceLineNumber` 从“只写不读”改成 `GetSourceLineNumber()` 的稳定 fallback，并用 hot-reload 回归锁住。 |
| 具体步骤 | 1. 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1548-1558`：`ScriptFunction != nullptr` 时继续优先从 `scriptData->declaredAt` 读取 live 行号；若 `ScriptFunction == nullptr`、`scriptData == nullptr` 或解析失败，则在 `GeneratedSourceLineNumber > 0` 时返回该缓存值，只有两者都不可用时才返回 `-1`。 2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:123-124` 为 `GeneratedSourceLineNumber` 增加注释，明确它既服务生成期记录，也服务 hot-reload / cleanup 后的 source metadata fallback，避免后续再次被当成冗余字段删掉。 3. 保持 `AngelscriptClassGenerator.cpp:3411-3417` 的写入逻辑不变，但在 `CleanupRemovedClass()` 所在实现附近补一条注释，说明 cleanup 只应清 `ScriptFunction`，不应清掉 `GeneratedSourceLineNumber`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` 新增回归：先编译并保存一个 `UASFunction*`，再触发 `DiscardModule` 或 rename/recompile，让旧函数进入 `ScriptFunction == nullptr` 状态；随后断言旧句柄的 `GetSourceLineNumber()` 仍返回最后一次生成行号，而不是 `-1`。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` 保留现有 live-function 行号断言，确保 fallback 修复不会破坏正常场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是把原先丢失的信息恢复出来；需要注意不要让 fallback 覆盖 live `ScriptFunction` 的真实行号，避免正常调试场景反而变旧。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 hot-reload/source metadata 测试。 2. 运行 stale-function 回归，确认 cleanup 后的旧 `UASFunction` 仍能返回最后一次生成行号。 3. 复跑现有 live-function source navigation 测试，确认正常场景仍返回即时行号而不是缓存值。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-47 | Defect | 先处理，统一 actor 查询与 spawn 的 world-context 错误语义 |
| P2 | Issue-48 | Defect | 其次处理，修正多文件 module 的 source file 定位 |
| P2 | Issue-49 | Defect | 最后处理，补回 hot-reload 后的 source line fallback |

---

## 发现与方案 (2026-04-08 18:30)

### Issue-50：`FDataTableCategoryHandle::GetRows` 按整表行数扩容，却只初始化命中项，返回数组会混入未初始化 struct 槽位

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp` |
| 行号 | 204-235 |
| 问题 | `FDataTableCategoryHandle::GetRows(?& OutArray) const` 先用 `Matches` 收集满足 category 过滤的行，但真正给 `OutArray` 扩容时写的是 `OutArray.Insert(Index, RowMap.Num(), ...)`，而不是 `Matches.Num()`。后面的初始化/拷贝循环只遍历 `Matches`。这意味着只要过滤结果小于整表行数，脚本数组长度就会按整表大小增长，但只有前 `Matches.Num()` 个槽位被 `InitializeStruct/CopyScriptStruct` 写入，剩余槽位保持未初始化状态。额外扫描 `Plugins/Angelscript/Source/AngelscriptTest` 对 `FDataTableCategoryHandle`、`GetRows(`、`GetAllRows(` 均未命中，当前没有回归覆盖这条过滤路径。 |
| 根因 | 绑定实现把 `GetAllRows` 的“整表复制”容量公式直接复制到了 category-filter 版本，却没有改成按匹配集大小分配。 |
| 影响 | 脚本侧 `CategoryHandle.GetRows(Result)` 在部分命中场景下会得到错误的 `Num()`，并把未初始化 struct 内容暴露给后续遍历、比较和序列化逻辑；若 struct 带非平凡析构，后续容器清理还可能对未初始化槽位执行销毁路径。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 category-filter 版本的数组扩容与初始化数量统一切到 `Matches.Num()`，并新增“部分命中”回归测试锁住正确的 `Num()` 与内容。 |
| 具体步骤 | 1. 将 `Bind_UDataTable.cpp:225` 的 `OutArray.Insert(Index, RowMap.Num(), ...)` 改为 `OutArray.Insert(Index, Matches.Num(), ...)`。 2. 在 `Matches.Num() == 0` 时直接返回，避免继续走无意义的 `Insert` 与数据指针计算。 3. 保留 `GetAllRows()` 现有实现不动，只修 category-filter 分支，防止把整表复制路径一并改坏。 4. 新增 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDataTableBindingsTests.cpp`，构造至少 3 行数据、仅 1 行命中 category 的表，断言 `GetRows(Result)` 后 `Result.Num() == 1` 且唯一元素内容正确。 5. 再补一条“0 命中”回归，断言输出数组不会平白增长，也不会留下默认垃圾元素。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDataTableBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险主要在于若现有脚本错误地把当前多出来的未初始化槽位当成“默认值行数”使用，修复后 `Num()` 会收紧到真实匹配数；这属于修正错误契约。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增的 DataTable 绑定测试；运行部分命中和零命中两条回归，确认 `GetRows()` 返回数组长度严格等于匹配行数，且不会出现额外空槽位。 |

### Issue-51：`UDataTable::AddRow` 在输入 struct 类型不匹配时直接静默跳过，调用方拿不到任何错误信号

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp` |
| 行号 | 14-25，122-129 |
| 问题 | `AddRow(FName RowName, const ?&in InRow)` 的绑定只在 `GetStructType(DataTable, InRowTypeId) != nullptr` 时调用 `DataTable->AddRow(...)`。但 `GetStructType()` 对类型不匹配只返回 `nullptr`，不会抛错；结果就是脚本把错误 row struct 传进来时，`AddRow` 直接无声失败。与同文件 `GetArraySubClass()` 在数组元素类型不匹配时显式 `Throw("OutArray must be a TArray of structs.")` 的处理相比，`AddRow` 当前缺少同等级别的错误收口。 |
| 根因 | 绑定层把“判断 row struct 是否匹配”的辅助函数拆成了只返回指针的 `GetStructType()`，但调用 `AddRow` 时没有补上失败分支的脚本异常。 |
| 影响 | 脚本会误以为行已经成功写入 DataTable，后续 `FindRow`/`GetRowNames`/`GetAllRows` 才暴露“不存在这行”的二次症状；真正的根因已经在 `AddRow` 时被吞掉，定位成本高。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 DataTable row-type 校验补上显式错误路径，禁止 `AddRow` 在错误 struct 上静默 no-op。 |
| 具体步骤 | 1. 将 `Bind_UDataTable.cpp` 的 `GetStructType()` 提炼成 `GetStructTypeOrThrow(const UDataTable* DataTable, int TypeId, const TCHAR* Context)`，当 `Usage.GetUnrealStruct()` 为空或与 `DataTable->GetRowStruct()` 不一致时统一 `FAngelscriptEngine::Throw("<Context> requires the table row struct type.")` 并返回 `nullptr`。 2. 让 `AddRow()` 改用这个新 helper；只有校验通过后才调用 `DataTable->AddRow(...)`。 3. 视兼容性接受度决定是否顺手让 `FindRow()` / `FDataTableRowHandle::GetRow()` / `FDataTableCategoryHandle::GetRow()` 也复用同一 helper；如果暂时不改返回值型 API，至少保证 `AddRow` 这条 `void` 接口不再静默失败。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDataTableBindingsTests.cpp` 增加负向回归：向 `RowStructA` 的表传入 `RowStructB`，断言脚本错误被记录，且 `GetRowNames()` 不会新增该行。 5. 再保留一条正确类型的 happy path，确认修复不会破坏正常写表。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDataTableBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 若现有脚本已经依赖“错误类型写入会被悄悄忽略”，修复后会变成显式脚本错误；这是预期中的契约收紧。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增的 DataTable 测试；运行错误类型写入回归，确认 `AddRow` 会报错且表内容不变，再复跑正确类型 happy path。 |

### Issue-52：多处手写 `#if WITH_EDITOR` 绑定没有写入 `asTRAIT_EDITOR_ONLY`，编译器的 editor-only 误用检查被整体绕过

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AssetRegistry.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 7-12；148-190；43-55；345-350；6938-6950；521-530 |
| 问题 | `Bind_FunctionLibraryMixins.cpp` 在 `#if WITH_EDITOR` 下直接注册 `ULevelStreaming.GetShouldBeVisibleInEditor()`，`Bind_AssetRegistry.cpp` 直接注册 `GetWidgetBlueprintCDOsByParentClass(...)`，`Bind_Subsystems.cpp` 直接注册 `UEditorSubsystem` 的 `ClassName::Get()`，但这些注册后都没有调用 `FAngelscriptBinds::SetPreviousBindIsEditorOnly(true)`。与此同时，bind core 明确提供了这条 trait 写入 API，而编译器 `asCBuilder::CheckEditorOnlyFunction()` 只会根据 `asTRAIT_EDITOR_ONLY` 阻止“在非 `EDITOR` block 中调用 editor-only function”。当前唯一相关测试 `AngelscriptEngineParityTests.cpp:521-530` 只断言 `GetShouldBeVisibleInEditor()` 存在，没有校验 trait。也就是说，这批手写 bind 目前只有“editor 构建里会注册”这一个条件，没有把 editor-only 语义继续传给脚本类型系统。 |
| 根因 | BindSystem 对手写 `#if WITH_EDITOR` shard 缺少统一的 editor-only 注册 helper，导致维护者只能记得“包一层预处理”，却经常漏掉随后的 `SetPreviousBindIsEditorOnly(true)` 元数据步骤。 |
| 影响 | 在 editor 构建里，普通 runtime 脚本可以直接看到并调用这些 API，而编译器不会报 “Cannot use editor-only function ... outside of an EDITOR block”。一旦脚本随后被拿去做非 editor 运行或打包，这些绑定会直接消失，形成跨构建配置的不一致 surface，而且错误出现得比本应的编译期检查更晚。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为手写 editor-only bind 提供统一注册封装，并先修复已确认的三处代表性遗漏，把 editor-only trait 重新接回编译器检查链。 |
| 具体步骤 | 1. 在 `AngelscriptBinds.h/.cpp` 新增轻量封装，例如 `BindEditorOnlyGlobalFunction(...)` 与 `BindEditorOnlyMethod(...)`，内部固定顺序为“注册函数/方法 -> `SetPreviousBindIsEditorOnly(true)`”。 2. 将 `Bind_FunctionLibraryMixins.cpp:10-12` 的 `GetShouldBeVisibleInEditor()`、`Bind_AssetRegistry.cpp:148-190` 的 `GetWidgetBlueprintCDOsByParentClass(...)`、`Bind_Subsystems.cpp:43-55` 的 editor subsystem `Get()` 改用该封装，避免继续手写漏 trait。 3. 追加一次 `rg "#if WITH_EDITOR"` 审计，把 `Bind_AssetRegistry.cpp`、`Bind_Subsystems.cpp`、`Bind_FMath.cpp`、`Bind_FString.cpp`、`Bind_UActorComponent.cpp` 等手写 shard 逐个复核；对所有只在 editor 构建暴露的方法统一补 `SetPreviousBindIsEditorOnly(true)`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 新增 trait 回归：取 `ULevelStreaming.GetShouldBeVisibleInEditor()`、`AssetRegistry::GetWidgetBlueprintCDOsByParentClass(...)`、一个 editor subsystem `Get()` 的 `asCScriptFunction`，断言 `traits.GetTrait(asTRAIT_EDITOR_ONLY)` 为 true。 5. 若测试框架允许，再补一条脚本编译负例：在非 `EDITOR` block 里直接调用其中一个函数，确认报错文本命中 `Cannot use editor-only function`，真正验证 builder 侧护栏重新生效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AssetRegistry.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | M |
| 风险 | 补 trait 后，当前 editor 构建中那些原本“能直接调用 editor-only API”的 runtime 脚本可能开始在编译期报错；这不是功能回退，而是把错误提前暴露。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增的 bind config 测试；确认代表性手写 editor-only bind 都带 `asTRAIT_EDITOR_ONLY`，并在负向脚本样例中触发预期的 editor-only 编译错误。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-52 | Architecture | 优先处理，先把 hand-written editor-only bind 的 trait 链补齐，避免跨构建 surface 继续漂移 |
| P2 | Issue-50 | Defect | 紧随其后，修正 `FDataTableCategoryHandle::GetRows()` 的错误扩容与未初始化槽位 |
| P2 | Issue-51 | Defect | 最后处理，补上 `UDataTable::AddRow()` 的类型不匹配显式报错 |

---

## 发现与方案 (2026-04-08 18:38)

### Issue-53：`Bind_ConfigEnums.cpp` 错把 collision profile 当成 channel 定义来源，三组查询枚举的名字与语义整体漂移

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_ConfigEnums.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/CollisionProfile.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 6-46；7-58；166-173，223-227；170-223 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 68。current 版 `Bind_ConfigEnums.cpp` 保留了 `//for (FCustomChannelSetup& Profile : Collision->DefaultChannelResponses)` 的注释，但实际实现已经改成 `GetNumOfProfiles()` + `GetProfileByIndex()`，并把 `FCollisionResponseTemplate::Name` / `temp->ObjectType` 写进 `ECollisionChannel`、`ETraceTypeQuery`、`EObjectTypeQuery`。引擎头文件明确表明 `GetProfileByIndex()` 访问的是 `Profiles`，真正的 channel 定义存放在 `DefaultChannelResponses`。UEAS2 对应实现仍然使用 `FCustomChannelSetup::Name / Channel / bTraceType` 构造三组枚举，并额外回填 `Pawn`、`Vehicle`、`Destructible` 等标准 channel。当前唯一现有回归 `AngelscriptEngineParityTests.cpp:170-223` 只验证 `CollisionProfile::<Name>` 常量，完全没有覆盖这三组查询枚举。 |
| 根因 | 引擎版本演进后 `DefaultChannelResponses` 变成 private UPROPERTY，current 分支为了避免直接字段访问，临时改用公开的 profile accessor 取数，但 `FCollisionResponseTemplate` 表示的是 profile 模板，不是 channel 定义，导致替代方案在语义层面完全不等价。 |
| 影响 | 脚本侧 `ECollisionChannel` 可能暴露 profile 名而不是 channel 名，`ETraceTypeQuery` / `EObjectTypeQuery` 也会按 `CollisionEnabled == QueryOnly` 和 `ObjectType` 误分类；标准 channel `Pawn`、`Vehicle`、`Destructible` 还会从枚举面消失。结果不是单个 helper 缺失，而是整组碰撞查询枚举的名字和值都可能偏离项目配置与 UEAS2。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用反射重新读取 `DefaultChannelResponses`，恢复按 `FCustomChannelSetup` 生成 collision query 枚举的正确语义，而不是继续复用 profile 模板。 |
| 具体步骤 | 1. 在 `Bind_ConfigEnums.cpp` 内新增一个局部 helper，使用 `FindFProperty<FArrayProperty>(UCollisionProfile::StaticClass(), TEXT("DefaultChannelResponses"))`、`FScriptArrayHelper` 和 `FCustomChannelSetup` 反射读取 channel 数组；在注释里说明这里不能继续使用 `GetProfileByIndex()`，因为 profile template 与 channel definition 语义不同。 2. 将 15-38 行的 profile 循环替换为 channel 循环：`BPName` 取 `Profile.Name`，`bTraceType` 为 true 时写 `ETraceTypeQuery_[BPName] = ConvertToTraceType(Profile.Channel)`，否则写 `EObjectTypeQuery_[BPName] = ConvertToObjectType(Profile.Channel)`，并始终写入 `ECollisionChannel_[BPName] = (int32)Profile.Channel`。 3. 补回 UEAS2 已有的标准枚举回填：`ECollisionChannel` 至少恢复 `Visibility`、`Camera`、`WorldStatic`、`WorldDynamic`、`Pawn`、`PhysicsBody`、`Vehicle`、`Destructible`，`EObjectTypeQuery` 恢复 `WorldStatic`、`WorldDynamic`、`Pawn`、`PhysicsBody`、`Vehicle`、`Destructible`。 4. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`：新增 collision channel parity 测试，同样从 `DefaultChannelResponses` 反射拿一个 trace channel 和一个 object channel，分别编译 `ETraceTypeQuery::<Name>` / `EObjectTypeQuery::<Name>` / `ECollisionChannel::<Name>` 的脚本片段；同时增加标准 channel `Pawn` 与 `Vehicle` 的 compile smoke，锁住回填逻辑。 5. 把现有 `CollisionProfile` parity 测试保留不动，避免把“profile 名常量”与“channel 枚举常量”混成一个测试，后续回归时能精确定位是哪一层配置枚举失真。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 预估工作量 | M |
| 风险 | 主要风险是对 private UPROPERTY 的反射读取依赖属性名 `DefaultChannelResponses`；如果未来引擎更名，这段 helper 会失效。因此必须把反射 helper 与 parity 测试一起落地，避免再次静默回退到错误数据源。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与扩展后的 parity 测试；确认 `ECollisionChannel` / `ETraceTypeQuery` / `EObjectTypeQuery` 都能按项目 channel 名编译通过，且 `Pawn`、`Vehicle` 等标准标识重新可见。 |

### Issue-54：`EPhysicalSurface` 绑定整段缺失，脚本已经无法按项目 surface 名访问物理材质配置

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_ConfigEnums.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/PhysicsEngine/PhysicsSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 1-10，46；5，52-57；26-43；`rg -n "EPhysicalSurface::|EPhysicalSurface " Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 69。current `Bind_ConfigEnums.cpp` 只创建了 `ETraceTypeQuery`、`ECollisionChannel`、`EObjectTypeQuery` 三个枚举，连 `PhysicsSettings.h` 都没有包含；UEAS2 同文件则会额外创建 `EPhysicalSurface`，并把 `UPhysicsSettings::Get()->PhysicalSurfaces` 中的每个 `FPhysicalSurfaceName` 暴露成脚本枚举项。引擎头文件明确说明项目级 surface 名就存放在 `FPhysicalSurfaceName { Type, Name }` 列表里。测试树对 `EPhysicalSurface` 完全零命中，说明这个 API 面当前没有任何自动化守护。 |
| 根因 | 配置枚举 shard 在迁移时被收口成“只处理 collision query enums”，把同文件里另一组依赖项目配置的 `EPhysicalSurface` 注册逻辑整段删掉了。 |
| 影响 | 依赖 `EPhysicalSurface` 名字驱动脚本逻辑的系统，如 footstep、bullet impact、surface-based FX / SFX、物理材质分流，都无法再沿用 UEAS2 的标准常量访问路径，只能手写字符串或额外桥接层，增加了运行时分叉与配置不同步风险。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 `Bind_ConfigEnums.cpp` 恢复 `EPhysicalSurface` 注册，让物理 surface 与 collision enums 一样从项目配置自动生成。 |
| 具体步骤 | 1. 在 `Bind_ConfigEnums.cpp` 恢复 `#include "PhysicsEngine/PhysicsSettings.h"`。 2. 在 collision 枚举注册逻辑之后追加 UEAS2 同款 `EPhysicalSurface` 绑定：`auto EPhysicalSurface_ = FAngelscriptBinds::Enum("EPhysicalSurface"); for (const FPhysicalSurfaceName& Surface : UPhysicsSettings::Get()->PhysicalSurfaces) { const FString BPName = Surface.Name.ToString().Replace(TEXT(" "), TEXT("_")); EPhysicalSurface_[BPName] = (int32)Surface.Type; }`。 3. 若团队担心 sanitize 后重名，顺手在循环里加一条重复名检测日志或 `ensureMsgf`，避免两个配置名在替换空格后碰撞却无人发现。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` 新增 physical surface parity 测试：先断言脚本引擎里存在 `EPhysicalSurface` 类型；若 `PhysicalSurfaces.Num() > 0`，再取第一个配置名编译 `EPhysicalSurface::<Name>` 的脚本片段，确保项目配置名可直接被脚本解析。 5. 把这条测试与 Issue-53 的 collision enum parity 分开，避免后续只修复 collision enums 时误以为整份 config enum shard 已经完全恢复。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是把已经存在于项目配置里的 surface 名重新暴露给脚本；若配置里存在 sanitize 后重名，修复会首次显式暴露这一冲突。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 parity 测试；确认脚本类型系统里重新出现 `EPhysicalSurface`，并且至少一个项目 surface 名能够被脚本成功编译引用。 |

### Issue-55：`Bind_FGeometry.cpp` 丢失 render-space 查询与 transformed-child 构造入口，UMG 几何 surface 已明显弱于 UEAS2

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FGeometry.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 5-35；6-58；`rg -n "GetRenderTransformScale|GetRenderTransformTranslation|GetAbsolutePosition|MakeTransformedChild" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 70 和 71。current `Bind_FGeometry.cpp` 只保留了 `GetLocalSize()`、`GetAbsoluteSize()`、`AbsoluteToLocal()`、`LocalToAbsolute()`、`MakeChild()` 五个 layout-space helper；UEAS2 同文件额外提供 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()` 三个查询，以及 `MakeTransformedChild()` 这个 render-space child geometry 构造入口。测试树对这四个 API 全部零命中，说明当前缺口还没有被任何 parity 或 compile smoke 固定下来。 |
| 根因 | `FGeometry` shard 在迁移时被收口成“最小可用的 layout-space API”，render transform 相关查询和 child 构造分支没有一起迁回。 |
| 影响 | 依赖 widget render transform、absolute position、render-space child geometry 的脚本都无法沿用 UEAS2 写法，拖拽偏移、tooltip 定位、命中区域换算和动画调试逻辑只能各自重算 transform 或额外桥接，UI 绑定 surface 明显缩窄。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 直接按 UEAS2 恢复 `FGeometry` 的四个缺失方法，并用类型反射 + compile smoke 锁住 UI 几何 surface。 |
| 具体步骤 | 1. 在 `Bind_FGeometry.cpp` 恢复 `#include "Layout/Geometry.h"`，并按 UEAS2 迁回四条方法：`GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()`、`MakeTransformedChild()`。 2. 保持 current 现有的 `FVector2D` / `FVector2f` 转换风格不变：前三个 getter 继续返回 `FVector2D(...)`，`MakeTransformedChild()` 直接走 `Geometry->MakeChild(FSlateRenderTransform(FScale2f(Scale), FVector2f(Translation)))`，避免另起一套 UI 向量转换习惯。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` 新增 `FGeometry` API parity 测试：直接从 script engine 取 `FGeometry` type info，断言这四条方法声明存在；再追加一个 compile smoke，验证脚本片段能够调用 `Geo.GetAbsolutePosition()` 与 `Geo.MakeTransformedChild(...)`。 4. 若现有测试基建不方便构造真实 `FGeometry` 实例，就先把验证收口为“类型反射 + 编译通过”；不要为了补一个 surface 测试引入新的 widget harness，把问题复杂化。 5. 完成后把这四个符号同步登记到 Issue-9 已建议的 `BindSystem_Coverage_Diff.md`，让后续 coverage 审计不再把它们当成隐式口头缺口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`，`Documents/AutoPlans/BindSystem_Coverage_Diff.md` |
| 预估工作量 | S |
| 风险 | 运行时风险低，主要是恢复 UEAS2 已有 surface；需要注意测试若只做反射存在性断言，仍要再加一条 compile smoke，防止 declaration 存在但签名写错。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与扩展后的 parity 测试；确认 `FGeometry` type info 中存在四条方法声明，并且引用这些 API 的脚本片段能够成功编译。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-53 | Defect | 立即修复，先纠正 collision query 枚举的数据源和标准 channel 回填 |
| P2 | Issue-54 | Architecture | 紧随其后，补回 `EPhysicalSurface` 项目配置绑定，恢复物理材质枚举面 |
| P2 | Issue-55 | Architecture | 最后补齐 `FGeometry` 的 render-space helper，并用 parity 测试锁住 UI surface |

---

## 发现与方案 (2026-04-08 23:39)

### Issue-56：`FMemoryReader::Seek/Skip` 只防上界不防负偏移，脚本可把 reader 定位到 `0` 之前

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Serialization/MemoryArchive.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Serialization/MemoryReader.h` |
| 行号 | 38-56；25-28；35-48 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 53。current `Seek(int InPos)` 只拒绝 `InPos > TotalSize()`，`Skip(int Count)` 只拒绝 `Tell() + Count > TotalSize()`，因此 `Seek(-1)`、`Skip(-1)` 会直接通过。底层 `FMemoryArchive::Seek()` 只是裸写 `Offset = InPos`，而 `FMemoryReader::Serialize()` 只要 `Offset + Num <= TotalSize()` 就继续 `Memcpy(Data, &Bytes[(int32)Offset], Num)`。结果是脚本先做一次负偏移，再读 1 byte，就可能从 `Bytes[-1]` 开始拷贝。 |
| 根因 | bind 层把“越过尾部”当成唯一非法定位条件，没有给底层可任意取值的 `Offset` 增加 `0` 下界防线。 |
| 影响 | 任意脚本都可以把 `FMemoryReader` 推到越界读路径，后续 `ReadInt*`、`ReadBytes`、`ReadAnsiString` 会落到随机数据、崩溃或更远处的序列化异常，而不是稳定的脚本错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `FMemoryReader` 引入统一的位置合法性 helper，同时在 `Seek` 和 `Skip` 两条入口上收紧到 `0..TotalSize()` 闭区间。 |
| 具体步骤 | 1. 在 `Bind_FMemoryReader.cpp` 顶部提取内部 helper，例如 `static bool ValidateReaderPositionOrThrow(const FMemoryReader* Reader, int64 Position, const TCHAR* Operation)`；当 `Position < 0` 或 `Position > Reader->TotalSize()` 时统一 `FAngelscriptEngine::Throw(...)` 并返回 `false`。 2. 让 `Seek(int InPos)` 改用该 helper，只有验证通过后才执行 `reader->Seek(InPos)`。 3. 让 `Skip(int Count)` 先按 `int64 NewPos = Reader->Tell() + Count` 计算目标位置，再复用同一 helper；不要继续只检查上界。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp` 新增负向回归：`Seek(-1)`、`Skip(-1)`、`Seek(TotalSize()+1)`、`Skip(TotalSize()+1)` 都必须记录脚本错误，且 `Tell()` 不会被推进到非法位置。 5. 将这条 helper 与 `Issue-24` 已规划的短读/EOF 防线放在同一文件内复用，避免后续再次出现“定位校验”和“读取校验”各写一套边界判断。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是把原本会跌到底层未定义行为的负偏移收紧成显式脚本错误；若现有脚本误用了负偏移技巧，修复后会在测试或运行时直接暴露。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增的 memory reader 测试；确认负偏移与越界定位都会抛脚本错误，且合法 `Seek/Skip` 行为不回退。 |

### Issue-57：`FString::RemoveAt` 越界路径缺少脚本级边界检查，错误会直接跌回底层 `RangeCheck`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FString.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Containers/Array.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 行号 | 231-233；236-245；2109-2112；303-335 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 57。current `Bind_FString.cpp` 把 `RemoveAt(int Index, int Count)` 直接转发到 `String.RemoveAt(Index, Count)`，没有任何 `Index` 合法性检查；UEAS2 对应实现则先用 `String.IsValidIndex(Index)` 拦截越界并 `Throw("String index out of bounds.")`。底层 `TArray::RemoveAt()` 会先走 `RangeCheck(Index, Count)`，因此脚本传入非法 index 时，首个失败点不再是脚本错误，而是容器层检查。现有测试也只覆盖 `"ABCDE"` 上的两个合法 `RemoveAt` happy path，没有负向回归。 |
| 根因 | `FString` 绑定迁移时保留了最小包装，却漏掉了 UEAS2 已补上的越界保护和与 `opIndex` 对齐的错误处理契约。 |
| 影响 | `FString` 在同一文件内已经出现不一致的边界行为：读索引越界会稳定抛脚本错误，删索引越界却可能直接掉进底层 `RangeCheck`。这会把普通脚本参数错误升级成更底层的断言/异常，定位和恢复都更差。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 UEAS2 的显式越界检查，让 `RemoveAt` 与 `opIndex` 共用同一套字符串边界错误语义。 |
| 具体步骤 | 1. 将 `Bind_FString.cpp:231-233` 的 `RemoveAt` lambda 改成显式校验版本：先判断 `String.IsValidIndex(Index)`，失败时 `FAngelscriptEngine::Throw("String index out of bounds."); return;`，通过后再执行 `String.RemoveAt(Index, Count)`。 2. 若团队希望一并收紧 `Count <= 0` 的行为，可在同一 lambda 内追加 `Count < 0` 拦截，并给出明确错误文本；不要继续把负 `Count` 留给底层容器。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` 的现有 `ASStringRemoveAtCompat` 基础上补两条负向脚本：`Value.RemoveAt(99, 1);` 与 `Value.RemoveAt(-1, 1);`，断言执行失败且错误文本命中 `"String index out of bounds."`。 4. 保留当前两个合法 `RemoveAt` happy-path 断言，确认修复只收紧错误路径，不影响正常字符串编辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是把原先由底层容器兜底的错误路径前移到脚本边界；若外部脚本依赖当前底层断言文本，修复后会统一成 Angelscript 错误文本。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与扩展后的 utility 绑定测试；确认合法 `RemoveAt` 保持通过，非法 index 会稳定抛出 `"String index out of bounds."`。 |

### Issue-58：`GFrameNumber` 被作为可写全局变量暴露给脚本，绑定层越权开放了引擎级帧计数器

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/CoreGlobals.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/HAL/UnrealMemory.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/Misc/MemStack.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 87-89；622-625；558-559；424-425；120-133；86-96；`rg -n "GFrameNumber" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 64。current `Bind_UWorld.cpp` 直接注册了 `uint GFrameNumber`，而 bind core 的 `BindGlobalVariable()` 只是把签名和地址原样交给 `RegisterGlobalProperty(...)`，不会自动加只读限制。引擎源码明确说明 `GFrameNumber` 是“每帧递增一次”的核心全局计数器；`UnrealMemory.cpp` 的 purgatory 轮换和 `MemStack.cpp` 的 frame-based 清理都直接依赖它。也就是说，脚本现在拿到的不是只读统计值，而是一个可写的全局时间基准。 |
| 根因 | world bind 把运行时调试状态按普通全局 property 暴露，没有区分“可观察”与“可写入”的引擎核心全局变量。 |
| 影响 | 任何脚本只要写入 `GFrameNumber`，就可能跨出 Angelscript 边界，直接污染按帧推进的内存回收和临时分配清理逻辑，导致跨系统、跨帧的异常；而当前测试树对这条全局绑定完全零覆盖。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 保留现有读取语法，但把 `GFrameNumber` 收紧成脚本只读全局变量，阻断脚本写入引擎核心计数器。 |
| 具体步骤 | 1. 将 `Bind_UWorld.cpp:89` 的注册改成 `FAngelscriptBinds::BindGlobalVariable("const uint GFrameNumber", &GFrameNumber);`，利用脚本签名层面的 `const` 禁止赋值，同时继续映射到同一原生地址，保证读取语法和实时值都不变。 2. 在 `AngelscriptBinds.h/.cpp` 附近补一条简短注释，明确 `BindGlobalVariable()` 不会自动推断只读语义，绑定引擎核心全局状态时必须在签名中显式写 `const`。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 增加 introspection 回归：取回 `GFrameNumber` 的 global property，断言其脚本声明带 `const`。 4. 再在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` 或 compile-summary 测试中增加负向脚本：尝试执行 `GFrameNumber = 123;`，确认编译失败；同时保留一条 `uint Local = GFrameNumber;` 的读取 smoke，确保读路径不回退。 5. 顺手审计其它通过 `BindGlobalVariable()` 暴露的引擎全局量，至少把“脚本不应写入”的运行时计数器、状态位和 singleton 指针列成一张小清单，避免同类越权入口继续增长。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险主要在于若已有脚本错误地依赖修改 `GFrameNumber`，修复后会在编译期暴露这类越权用法；但这类依赖本身就不应被视为稳定契约。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增测试；确认 `GFrameNumber` 仍可读取但无法赋值，且脚本侧读取到的值会随引擎帧推进变化。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-56 | Defect | 立即修复，先封住 `FMemoryReader` 的负偏移入口，避免继续触发越界读 |
| P1 | Issue-57 | Defect | 紧随其后，统一 `FString` 删除索引的脚本边界错误语义 |
| P1 | Issue-58 | Architecture | 最后收紧 `GFrameNumber` 为只读，阻断脚本越权改写引擎帧计数器 |

---

## 发现与方案 (2026-04-08 23:54)

### Issue-59：`FGameplayEffectSpec` 构造绑定对空 `UGameplayEffect` 没有前置校验，脚本会直接撞上引擎 `check(Def)`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayEffectSpec.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/GameplayEffect.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 8-13；1527-1536，1665-1669；`rg -n "FGameplayEffectSpec|GameplayEffectSpec\\(" Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 问题 | current `Bind_FGameplayEffectSpec.cpp` 的第一个构造绑定直接执行 `new(Address) FGameplayEffectSpec(InDef, InEffectContext, Level)`，对 `InDef == nullptr` 没有任何防护。引擎实现里 `FGameplayEffectSpec::FGameplayEffectSpec(const UGameplayEffect* InDef, ...)` 会立刻调用 `Initialize(InDef, ...)`，而 `Initialize()` 第 1667-1668 行先 `Def = InDef;`，紧接着就是 `check(Def);`。这意味着脚本只要传入空 `UGameplayEffect`，失败点就不是脚本错误，而是引擎断言。测试树对该构造路径零命中。 |
| 根因 | current-only 的 GAS value bind 直接复用了 native placement-new 构造，但没有像 `SpawnActor`、`GetComponentsByClass` 那样在脚本边界先校验必填对象参数。 |
| 影响 | 任意脚本只要从 data table、软引用解析、配置查找或条件分支里得到空 `UGameplayEffect`，再执行 `FGameplayEffectSpec(...)`，就可能把普通参数错误升级成 editor/runtime 断言中断，尤其容易在工具脚本和热重载场景里形成“偶发崩溃”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 binding constructor 层先拦截空 `UGameplayEffect`，并在错误路径上构造一个默认 spec，避免脚本对象落到未初始化内存。 |
| 具体步骤 | 1. 将 `Bind_FGameplayEffectSpec.cpp:8-13` 的 lambda 改成显式校验版：先判断 `InDef != nullptr`，为空时 `FAngelscriptEngine::Throw("GameplayEffect was null.");`，随后 `new(Address) FGameplayEffectSpec(); return;`；只有非空时才执行当前三参构造。 2. 保持第二个 copy constructor 绑定不变，它不经过 `check(Def)`，不需要引入额外分支。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 `AngelscriptGASValueBindingsTests.cpp`，补一条负向脚本构造：向 `FGameplayEffectSpec` 传 `null` effect，断言记录 `"GameplayEffect was null."` 且不会触发进程断言。 4. 同文件补一条 happy-path：使用有效 `UGameplayEffect` 构造 spec，并断言 `Spec.Def` 与传入 effect 一致，避免修复时回退正常 GAS 绑定能力。 5. 顺手把该测试登记到 current-only shard coverage 台账，避免 `Bind_FGameplayEffectSpec.cpp` 继续处于“已绑定但零自动化”的状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayEffectSpec.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASValueBindingsTests.cpp`，`Documents/AutoPlans/BindSystem_Coverage_Diff.md` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是把原先会进入引擎断言的空输入收紧成脚本错误；若现有脚本依赖“空 effect 也能先构造、后续再补值”的非法用法，修复后会立即暴露。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 GAS value 测试；确认空 `UGameplayEffect` 只产生脚本错误且不会触发 `check(Def)`，有效 `UGameplayEffect` 构造路径保持通过。 |

### Issue-60：`FGameplayTagBlueprintPropertyMap.Initialize` 对空 `Owner/ASC` 只写 GAS 日志，失败重绑前不会先清理旧注册

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTagBlueprintPropertyMap.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/GameplayEffectTypes.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 5-8；967-991，1086-1099；`rg -n "FGameplayTagBlueprintPropertyMap|ApplyCurrentTags\\(|Initialize\\(.*AbilitySystemComponent" Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 问题 | current 直接把 `Initialize(UObject Owner, UAbilitySystemComponent ASC)` 绑成 `METHODPR_TRIVIAL`，没有任何脚本边界校验。引擎实现里 `FGameplayTagBlueprintPropertyMap::Initialize()` 对空 `Owner` 或空 `ASC` 只做 `ABILITY_LOG(Error, ...)` 然后 `return`；更关键的是这些判定发生在 `if (CachedOwner.IsValid()) { Unregister(); }` 之前，所以一次失败的重绑不会先清理旧 delegate 注册。`ApplyCurrentTags()` 遇到失效 `Owner/ASC` 也只写 warning 后返回。测试树对整个 type 零命中。 |
| 根因 | current-only 的 GAS property-map bind 直接暴露了 native “日志诊断型”接口，但没有像其它高风险 bind 一样，把无效输入转换成脚本可见错误或显式重绑契约。 |
| 影响 | 脚本把空 `Owner` / `ASC` 传进 `Initialize()` 时，只会在引擎日志里留下一条 GAS error，脚本层仍然像“调用成功”一样继续执行；若这是一次重绑失败，旧 `CachedOwner/CachedASC` 注册还会继续存活，后续 tag 变化可能仍然回写旧对象属性，形成极难定位的 stale callback。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用 binding wrapper 把 `Initialize()` 的必填参数错误前移成脚本异常，并用专项测试锁住“失败必须可见”的契约。 |
| 具体步骤 | 1. 将 `Bind_FGameplayTagBlueprintPropertyMap.cpp:7` 从 `METHODPR_TRIVIAL` 改成显式 lambda：先检查 `Owner != nullptr`，为空时 `FAngelscriptEngine::Throw("GameplayTagBlueprintPropertyMap.Initialize received a null Owner."); return;`；再检查 `ASC != nullptr`，为空时抛 `"GameplayTagBlueprintPropertyMap.Initialize received a null AbilitySystemComponent."`；仅在两者都有效时调用 `Map.Initialize(Owner, ASC)`。 2. 保留 `ApplyCurrentTags()` 现有签名，但在同文件旁边补一条注释，明确它依赖先成功执行 `Initialize()`；后续若团队愿意继续收紧，可以再为该类型新增显式 `IsInitialized()` helper。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASValueBindingsTests.cpp` 增加两条负向回归：`Map.Initialize(null, ASC)` 与 `Map.Initialize(this, null)` 都必须记录脚本错误，而不是只留下引擎日志。 4. 同测试文件补一条正向 smoke：构造一个带 `FGameplayTagBlueprintPropertyMap` 的测试对象，在有效 `Owner + ASC` 下调用 `Initialize()` 与 `ApplyCurrentTags()`，确认 bool/int/float 属性仍能按 tag count 被正确刷新。 5. 若后续再次扩展这个 type 的 bind surface，统一要求所有会改变注册状态的方法先在 binding 层完成参数校验，不再把 `ABILITY_LOG` 当作脚本层唯一诊断出口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTagBlueprintPropertyMap.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASValueBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险主要是把原先只写日志的失败路径收紧成显式脚本错误；如果现有脚本曾经依赖“失败也继续跑”的宽松行为，修复后会更早暴露问题，但这是期望中的契约收紧。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增 GAS value 测试；确认空 `Owner/ASC` 会产生脚本错误而不再只写 GAS 日志，且有效初始化后 `ApplyCurrentTags()` 的正常行为不回退。 |

### Issue-61：`FGenericPlatformMisc::RequestExit(bool)` 被作为普通脚本 API 暴露，绑定层直接开放了进程级退出开关

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGenericPlatformMisc.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMisc.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/GenericPlatform/GenericPlatformMisc.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 5-10；1055-1059；978-993；`rg -n "FGenericPlatformMisc|RequestExit\\(" Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 问题 | current 的 `Bind_FGenericPlatformMisc.cpp` 整个 shard 只做了一件事：把 `FGenericPlatformMisc::RequestExit(bool Force)` 直接注册给脚本。引擎头文件明确把 `Force=true` 标记为危险路径，因为它会执行“immediate exit”；实现文件进一步显示 `Force` 为 true 时直接 `abort()`，为 false 时也会触发 `RequestEngineExit(...)`。当前没有任何 editor-only、commandlet-only、配置开关或测试护栏。也就是说，任意脚本现在都能把“请求优雅退出”甚至“直接中止进程”当成普通全局 API 调用。 |
| 根因 | current-only 的平台杂项 shard 直接照搬了底层 platform API，却没有区分“脚本可观察/可控制的 gameplay 能力”和“只应由宿主进程、命令行工具或崩溃恢复路径调用的进程级控制开关”。 |
| 影响 | 一段普通脚本、调试脚本或误写的示例代码就足以让 editor/game 退出；更糟的是 `Force=true` 会直接绕过正常 flush/destructor 路径，可能造成配置未写回、测试流程被硬中断、自动化结果失真和排障信息丢失。由于测试树零覆盖，这条 kill switch 当前没有任何回归保护。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 从默认脚本 surface 移除原始 `RequestExit(bool)`，若业务确有需要，只暴露受限的安全 wrapper。 |
| 具体步骤 | 1. 删除 `Bind_FGenericPlatformMisc.cpp:9` 对 `RequestExit(bool Force)` 的直接绑定，不再让脚本看到原始 platform-level kill switch。 2. 如果项目确实需要脚本触发退出，在同文件改成插件自有 wrapper，例如 `System::RequestCleanExit()` 或 `FGenericPlatformMisc::RequestCleanExit()`，内部固定调用 `FGenericPlatformMisc::RequestExit(false)`，并额外加 `IsRunningCommandlet()` / 项目配置开关保护；禁止脚本传入 `Force=true`。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 新增 introspection 回归：断言旧声明 `void RequestExit(bool Force)` 不再注册；若保留安全 wrapper，则只断言新 wrapper 存在。 4. 若团队保留 wrapper，再增加一个可测试的 indirection（例如本地 static delegate 或 function pointer），让测试能够验证“调用了退出请求”而不真正结束进程；不要把真实退出行为留给自动化测试进程承担。 5. 将这条限制记录进 `BindSystem_Coverage_Diff.md` 或 bind 约束说明，明确 current-only shard 允许新增能力，但不能把宿主进程控制 API 直接下放给脚本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGenericPlatformMisc.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Documents/AutoPlans/BindSystem_Coverage_Diff.md` |
| 预估工作量 | S-M |
| 风险 | 若仓内已有脚本依赖当前 `RequestExit(false)` 行为，移除或收紧后会在编译期暴露；但这类脚本本质上依赖的是宿主进程控制权，不应默认视为稳定绑定契约。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与 bind-config 测试；确认脚本侧不再能解析原始 `RequestExit(bool)`，若保留安全 wrapper，则只允许可控的 `clean exit` 路径，并可在测试中通过 indirection 验证调用而不真正终止进程。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-59 | Defect | 立即修复，先封住空 `UGameplayEffect` 触发 `check(Def)` 的崩溃入口 |
| P1 | Issue-60 | Defect | 紧随其后，把 `GameplayTagBlueprintPropertyMap.Initialize` 的失败路径收口成脚本错误 |
| P1 | Issue-61 | Architecture | 最后收紧 `RequestExit` 暴露面，移除脚本级进程退出开关 |

---

## 发现与方案 (2026-04-09 00:07)

### Issue-62：`AssetManager_LoadPrimaryAssets` 用 `ensureMsgf` + `return` 吞掉无效 `FPrimaryAssetId`，脚本侧既拿不到异常也拿不到回调

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 49-105；`rg -n "LoadPrimaryAsset\\(|LoadPrimaryAssets\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | `AssetManager_LoadPrimaryAssets()` 在遍历 `AssetsToLoad` 时，发现任一 `FPrimaryAssetId` 无效就只触发 `ensureMsgf(Asset.IsValid(), TEXT("Tried to load invalid asset!"))`，随后把 `bShouldLoad` 置为 `false` 并直接 `return`。`LoadPrimaryAsset(...)` 与 `LoadPrimaryAssets(...)` 两个脚本入口都无条件转发到这条 helper，因此脚本传入无效 asset id 时，不会得到 `FAngelscriptEngine::Throw(...)`，也不会进入完成/取消回调，只会变成一次静默 no-op。 |
| 根因 | 绑定层直接沿用了 native 侧的 `ensure` 诊断模式，没有把“脚本参数非法”升级成脚本可见错误，也没有给调用方留下可检测的失败信号。 |
| 影响 | 任何由字符串解析、配置查找或外部数据拼装出来的无效 `FPrimaryAssetId`，都会在脚本层退化成“调用成功但什么都没发生”。这会让资源预热、异步加载链路和依赖回调推进的脚本逻辑停在半路，排查时只能从日志里的 `ensure` 反推，而不是直接在调用点看到明确错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 primary-asset 输入校验前移成绑定层显式异常，并用可单测的校验 helper 固定错误契约。 |
| 具体步骤 | 1. 在 `Bind_UAssetManager.cpp` 提取内部 helper，例如 `static bool ValidatePrimaryAssetIdsOrThrow(const TArray<FPrimaryAssetId>& AssetsToLoad, const TCHAR* OperationName)`；逐个检查 `Asset.IsValid()`，命中无效项时执行 `FAngelscriptEngine::Throw(...)`，错误文本直接带上操作名和索引，例如 `"LoadPrimaryAssets received invalid PrimaryAssetId at index 0."`。 2. 让 `AssetManager_LoadPrimaryAssets()` 改用该 helper，校验失败时立即返回，不再依赖 `ensureMsgf` + `bShouldLoad` 这套只写日志的控制流。 3. 保留现有 delegate 绑定顺序，但仅在校验通过后再构造 `CompleteDelegate` / `CancelDelegate`，避免无效输入继续伪装成一次已发起的异步加载。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 `AngelscriptAssetManagerBindingsTests.cpp`，为校验 helper 增加两条自动化回归：一条传默认构造的无效 `FPrimaryAssetId` 并断言返回 `false` + 记录预期错误，一条传 `FPrimaryAssetId(FPrimaryAssetType(TEXT("TestType")), FName(TEXT("TestName")))` 并断言返回 `true`。 5. 在同一测试文件补一条脚本入口 smoke：编译一段调用 `LoadPrimaryAsset(...)` 的脚本函数，并通过 `asIScriptContext` 传入 `UAssetManager::GetIfInitialized()`；断言无效 id 时执行结果不再是静默完成，而会命中新错误文本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 风险主要在于把历史上“只打一条 ensure 日志”的宽松路径收紧成显式脚本错误；若已有脚本误把无效 `FPrimaryAssetId` 当成可忽略输入，修复后会更早暴露这些问题。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增的 asset manager 绑定测试；确认无效 `FPrimaryAssetId` 会产生稳定脚本错误，合法 `FPrimaryAssetId` 仍能通过校验 helper。 |

### Issue-63：`AActor::GetAllComponents` 成功路径不重置输出数组，复用查询会把旧组件混进新结果

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 184-204；337-340；151-165；`rg -n "GetAllComponents\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | `FAngelscriptActorBinds::GetAllComponents(...)` 在完成 `Actor` / `ComponentClass` 校验后，直接遍历 `OnActor->GetComponents()` 并对命中项执行 `OutComponents.Add(Comp)`，整个成功路径没有任何 `Reset()` / `Empty()`。同仓库里 `Bind_UObject.cpp` 的 `GetAllClasses()` 在填充输出数组前会显式 `OutClasses.Reset()`，说明 bind 层本来已经存在“out array 由被调用方重新填充”的稳定约定。当前测试只覆盖 `GetComponentsByClass(...)` 的 happy path，完全没有 `GetAllComponents(...)` 的复用数组回归。 |
| 根因 | 这是另一处手写查询 helper 漏掉输出数组生命周期管理的实现错误；和 `Issue-20` 属于同类问题，但发生在 `Bind_UActorComponent.cpp` 的独立入口上。 |
| 影响 | 脚本只要复用同一个 `TArray<UActorComponent>` 多次调用 `GetAllComponents(...)`，就会把旧结果、重复结果和当前结果混在一起。actor 组件树发生变化后，这还会把已经不该出现的旧句柄继续留在数组里，结果既偏离 API 名称语义，也会让后续组件遍历逻辑产生重复处理。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 `GetAllComponents(...)` 对齐到现有 out-array 查询契约，在成功路径开始处统一重置输出数组，并追加复用场景回归。 |
| 具体步骤 | 1. 在 `Bind_UActorComponent.cpp:184-204` 中，把 `OutComponents.Reset();` 放到 `OnActor` 与 `ComponentClass` 校验全部通过之后、组件遍历开始之前，确保错误路径不破坏调用方原数组，而成功路径总是返回“本次查询结果”。 2. 若团队后续要和 `Issue-20` 一起收敛重复逻辑，可顺手提取一个小 helper，例如 `static void ResetAndCollectActorComponents(const AActor* OnActor, UClass* ComponentClass, TArray<UActorComponent*>& OutComponents)`，避免 `GetAllComponents` 和 `GetComponentsByClass` 再次各写一套“Reset + 遍历 + Add”模板。 3. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` 的现有组件读取脚本：新增 `TArray<UActorComponent> ReusedComponents; ReusedComponents.Add(GetOwner().GetComponent(USceneComponent::StaticClass())); GetOwner().GetAllComponents(USceneComponent::StaticClass(), ReusedComponents);`，断言 `Num() == 1` 且元素仍是 `ScriptScene`。 4. 在同一脚本里再连续调用第二次 `GetAllComponents(...)`，确认重复查询不会把数组累积成 2 个元素，避免未来只修第一次复用、漏掉连续调用场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是把“调用方自带旧元素也会继续保留”的宽松行为收紧成标准 out-parameter 语义；若个别脚本错误地依赖当前累加行为，修复后会直接暴露。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与扩展后的 native engine 绑定测试；确认 `GetAllComponents(...)` 在预填充数组和连续调用场景下都稳定返回当前组件集，而不会累积旧元素。 |

### Issue-64：承接 `Analysis` 发现 37，`FPrimaryAssetId` 构造 surface 迁移半截，现存字符串构造还残留错误的地址参数类型

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 29-37；30-44；`rg -n "FPrimaryAssetId|PrimaryAssetType" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | current `Bind_UAssetManager.cpp` 里，`FPrimaryAssetId` 只保留了 `void f(const FString& InString)` 这一条构造器；而 UEAS2 同位置还公开 `void f(const FPrimaryAssetType& InType, const FName& InName)`。更直接的问题是，current 现存这条字符串构造器的 lambda 首参数仍写成了 `FPrimaryAssetType* Address`，而不是 `FPrimaryAssetId* Address`。这不是单纯的 API 缺口，而是迁移过程中直接留下的目标类型错位。 |
| 根因 | `PrimaryAsset` 绑定迁移只搬回了最小可用的字符串构造路径，同时直接复制了上一段 `FPrimaryAssetType` 绑定模板，没有把目标类型名和强类型构造 surface 一起整理完成。 |
| 影响 | 脚本无法继续使用 `(FPrimaryAssetType, FName)` 这种强类型组合构造 `FPrimaryAssetId`，只能退回字符串拼装；同时，现存实现还把 placement-new 地址参数写成了错误的类型名，后续维护者很难确认这段代码到底是“偶然可工作”还是“真实已对齐”，资产加载入口的可读性和可靠性都被拉低。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 直接把 `FPrimaryAssetId` 构造绑定对齐回 UEAS2：修正地址参数类型，补回强类型 overload，并用反射 + 编译 smoke 锁住这组 surface。 |
| 具体步骤 | 1. 将 `Bind_UAssetManager.cpp:33-36` 的字符串构造 lambda 首参数从 `FPrimaryAssetType* Address` 修正为 `FPrimaryAssetId* Address`，消除明显的目标类型错位。 2. 参照 UEAS2 `Bind_UAssetManager.cpp:39-43`，补回 `FPrimaryAssetId_.Constructor("void f(const FPrimaryAssetType& InType, const FName& InName)", ...)`，内部直接 `new(Address) FPrimaryAssetId(InType, InName);`。 3. 在两条 `FPrimaryAssetId` 构造器后都恢复 `FAngelscriptBinds::SetPreviousBindNoDiscard(true)`，与 UEAS2 当前契约保持一致，避免把纯 value-factory 再次退化成可静默忽略的调用。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp` 新增 parity 回归：通过脚本类型反射断言 `FPrimaryAssetId` 同时存在 `void f(const FString& InString)` 与 `void f(const FPrimaryAssetType& InType, const FName& InName)` 两条构造声明。 5. 在同一测试文件增加编译 smoke：编译脚本片段 `FPrimaryAssetId BuildId() { return FPrimaryAssetId(FPrimaryAssetType(n"TestType"), n"TestName"); }`，并执行 `BuildId().IsValid()` 断言返回 `true`；同时保留字符串构造 smoke，确认修正地址参数类型不会回退现有路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险较低，主要是恢复 UEAS2 已有构造 surface 并修正明显的迁移错位；若仓内已有脚本依赖字符串拼装，修复后仍可继续工作，只是多了一条更稳的强类型写法。 |
| 前置依赖 | 无 |
| 验证方式 | 编译 `AngelscriptRuntime` 与新增的 asset manager 绑定测试；确认 `FPrimaryAssetId` 两条构造器都出现在脚本类型系统里，且 `(FPrimaryAssetType, FName)` 构造路径可编译、可执行并返回有效 id。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-62 | Defect | 立即修复，先把无效 `FPrimaryAssetId` 的 silent failure 收口成脚本错误 |
| P2 | Issue-64 | Defect | 紧接着继续留在 `Bind_UAssetManager.cpp`，补回 `FPrimaryAssetId` 强类型构造并修正地址参数类型 |
| P2 | Issue-63 | Defect | 最后修正 `GetAllComponents(...)` 的 out-array 复用语义，并补连续调用回归 |

---

## 发现与方案 (2026-04-09 00:14)

### Issue-65：`__WorldContext` 的公开脚本 contract 从变量漂移成函数，兼容层只修了 hidden-arg/default-value 路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UWorld.cpp` |
| 行号 | 33-41；230-233；224-231，421-426；275，456；85，268-275，682-689；34-38 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 7。current `Bind_UWorld.cpp` 把公开入口注册成 `UObject __WorldContext()` 函数；UEAS2 同位置暴露的是全局变量 `UObject __WorldContext`。额外扫描 current 源码，只有 `Bind_BlueprintType.cpp:230-233` 与 `Helper_FunctionSignature.h:224-231,421-426` 会把文本 `__WorldContext` 归一化成 `__WorldContext()`，说明兼容工作只覆盖了 blueprint 默认值和 hidden world-context 参数生成链路，没有覆盖普通脚本表达式。与此同时，runtime core 仍然维护稳定的当前 world-context 存储：`FAngelscriptEngine` 持有 `WorldContextObject`，并通过 `TryGetCurrentWorldContextObject()` / `GAmbientWorldContext` 对外解析当前值。 |
| 根因 | world-context 访问从“公开变量”切换到“按调用时解析函数”后，绑定层只补了生成器内部字符串重写，没有同步恢复脚本侧公开 contract。 |
| 影响 | 与 UEAS2 对齐的显式脚本写法 `UObject Context = __WorldContext;` 已不再等价；当前只有 hidden-arg/default-value 这种受控代码生成路径还能自动吃到兼容转换。结果是公开 API surface 与内部生成器协议分叉，旧脚本迁移和对外文档都必须额外记忆“这里不是变量而是函数”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 `__WorldContext` 的公开变量形态，同时保留 current 的动态解析实现作为兼容后端，不再让内部生成器协议替代公开 API。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.h/.cpp` 增加一个专门给 bind 使用的稳定 world-context slot，例如 `static UObject*& GetBoundWorldContextStorage()`；该 slot 由 `SetAmbientWorldContext()` 和 engine scope 恢复路径统一同步，保证它始终反映 `TryGetCurrentWorldContextObject()` 当前可见的对象。 2. 在 `Bind_UWorld.cpp` 中恢复 `FAngelscriptBinds::BindGlobalVariable("const UObject __WorldContext", &FAngelscriptEngine::GetBoundWorldContextStorage())`，把它重新作为公开脚本 surface；现有 `__WorldContext()` 继续保留一轮兼容，但在注册后立即 `FAngelscriptBinds::DeprecatePreviousBind("Use __WorldContext variable instead")`，避免两个拼写长期并存。 3. 将 `Bind_BlueprintType.cpp:230-233` 和 `Helper_FunctionSignature.h:224-231,421-426` 的 canonical 输出改回 `__WorldContext`，同时继续接受 `__WorldContext()` 作为输入，保证旧生成结果和新生成结果都能被 current runtime 吃掉。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` 新增兼容回归：同一段脚本分别读取 `__WorldContext` 与 `__WorldContext()`，断言两者都等于当前测试 scope 提供的 context object；再补一条 compile smoke，确认 hidden world-context 默认值最终落成的脚本声明使用 canonical `__WorldContext`。 5. 在 `Documents/Guides/Test.md` 或相关绑定测试说明中补一条 world-context contract 说明：公开脚本入口是变量 `__WorldContext`，函数拼写只作为临时兼容别名存在。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，`Documents/Guides/Test.md` |
| 预估工作量 | M |
| 风险 | 恢复变量公开名后，短期内会同时支持变量和函数两种拼写；若不加弃用提示，双轨 surface 会固化。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 world binding 测试。 2. 运行兼容回归，确认 `__WorldContext` 和 `__WorldContext()` 在同一测试 scope 下返回同一个对象。 3. 检查新生成的 hidden world-context 默认值，确认 canonical 字符串回到 `__WorldContext`，而不是继续输出 `__WorldContext()`。 |

### Issue-66：`UObject::opCast` 把测试场景字符串和 `Display` 级调试日志留在运行时绑定路径里

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 138-173，192-201；37-77，123-139；123-153 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 17 和 24。current `UObject::opCast` 一进入就根据 `ScenarioInterfaceCastSuccess` 和 `DamageableCast` 这两个字符串决定是否打印 `UE_LOG(Angelscript, Display, ...)`；随后只要目标类型是 interface，还会再打印一次 `target/objectClass/isA/implements` 日志。测试文件里恰好定义了 `ScenarioInterfaceCastSuccess` 模块与 `UIDamageableCastOk/Fail` 接口名，这说明运行时代码已经直接依赖自动化场景命名。对照 UEAS2，同一段 `opCast` 只有最小的 type 检查和结果写回，没有任何场景名匹配或 `Display` 级日志。 |
| 根因 | 某次 interface cast 排查时把临时诊断逻辑直接写进了共享 `opCast` binder，清理测试时没有把这些硬编码探针一并移除。 |
| 影响 | 当前任何名字命中这些子串的真实业务类或接口都会额外产生日志；更广义地，所有 interface cast 都会在运行时刷 `Display` 输出。结果是核心 binder 既承担 cast 语义，又承担一次性排查职责，日志噪音、行为耦合和后续维护成本一起上升。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `opCast` 收回成纯粹的 cast primitive，所有场景化诊断移出 runtime bind，改由测试侧显式捕获和断言。 |
| 具体步骤 | 1. 删除 `Bind_UObject.cpp:140-173` 中的 `bScenarioCastProbe`、`bLogDamageableCast` 以及对应 `UE_LOG(Angelscript, Display, ...)` 调试分支。 2. 删除 `Bind_UObject.cpp:192-201` 的 interface cast `Display` 日志，让 `opCast` 只保留类型判断和 `OutAddress` 写回。 3. 如果团队仍需要临时排查 interface cast，新增一个仅在测试模块里使用的轻量日志捕获器，例如放在 `AngelscriptInterfaceCastTests.cpp` 局部 `namespace` 内的 `FBufferedOutputDevice`，由测试显式订阅/取消订阅 `GLog`，不要再让 binder 通过字符串命中决定是否输出。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` 补一条无噪音回归：执行 success/fail 两个 cast 场景时，断言 `Angelscript` category 下不再出现 `UObject::opCast` 的 `Display` 文本；同时保留原有功能断言，确保去日志不会影响 cast 成功/失败语义。 5. 追加一条注释到 `Bind_UObject.cpp` 的 `opCast` 前，明确“运行时 binder 不允许按测试名或业务类型名做诊断分支”，避免类似探针再次回流。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` |
| 预估工作量 | S |
| 风险 | 若当前有人依赖这些 `Display` 日志做手工排查，移除后会失去现成 breadcrumb；需要用测试侧捕获器替代这种一次性诊断方式。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与 interface cast 测试。 2. 运行 `Angelscript.TestModule.Interface.CastSuccess` 和 `Angelscript.TestModule.Interface.CastFail`，确认功能断言仍通过。 3. 使用新增日志捕获回归确认 `UObject::opCast` 不再向 `Angelscript` category 输出 `Display` 级调试文本。 |

### Issue-67：`__CreateLiteralAsset` 作为公开 helper 缺少 `AssetClass == nullptr` 防线，错误会直接跌回底层对象创建路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 606-682；4109-4119；`rg -n "__CreateLiteralAsset" Plugins/Angelscript/Source/AngelscriptTest Script -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | 承接 `Documents/AutoPlans/BindSystem_Analysis.md` 的发现 26。`__CreateLiteralAsset(UClass AssetClass, const FString& Name)` 当前作为公开 global bind 暴露，但实现里没有任何 `AssetClass == nullptr` 检查。后续路径会在 `ExistingObject->IsA(AssetClass)` 的错误分支里访问 `AssetClass->GetName()`，在创建新对象时把 `AssetClass` 直接传给 `NewObject<UObject>`，软重载时还会调用 `AssetClass->GetDefaultObject()`。同文件公开的 `NewObject(...)` 则在 560-563 行先统一 `Throw("Class was nullptr.")`，说明 bind 层本来有明确的 class-null 契约，只是这条内部 helper 没有跟上。 |
| 根因 | literal asset 创建 helper 依赖预处理器生成的受控调用路径默认总会传入合法类型，因此实现者把参数校验交给了调用方，而不是固化在 helper 自身。 |
| 影响 | 当前唯一已知调用方 `AngelscriptPreprocessor.cpp:4116` 确实传入 `{Type}`，但 helper 作为公开名字仍可被脚本或后续 codegen 直接调用；一旦传入空 class，失败模式不会是脚本错误，而是底层对象创建/类型访问路径上的崩溃或不透明错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `__CreateLiteralAsset` 和 `NewObject` 共享同一条 class-null 契约，并把它与 `__PostLiteralAssetSetup` 一起收紧到内部 helper surface。 |
| 具体步骤 | 1. 在 `Bind_UObject.cpp:608-609` 的 lambda 一进入时就加 `if (AssetClass == nullptr) { FAngelscriptEngine::Throw("Asset class was null."); return nullptr; }`，禁止后续再进入 `IsA()` / `GetName()` / `GetDefaultObject()` 路径。 2. 将错误分支文案与 `NewObject(...)` 的 class-null 诊断统一到同一套措辞，避免 literal asset helper 成为第二套错误语言体系。 3. 参照 `Issue-35` 的处理，把公开名字迁成 `__Internal_CreateLiteralAsset`，并在 `AngelscriptPreprocessor.cpp:4116` 同步替换调用点；如需兼容旧脚本，保留旧名字作薄 wrapper，但 wrapper 也必须先做空类校验。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` 增加负向回归：直接调用空 `AssetClass` 的 helper，断言得到脚本错误且返回 `null`；再保留一条 literal asset happy-path smoke，确认改名和加 guard 不会影响正常资产创建。 5. 结合 `Issue-35` 一并复核 literal asset 两段 helper 的公开性，确保 `__CreateLiteralAsset` / `__PostLiteralAssetSetup` 要么都走内部命名面，要么都具有完整参数校验，不再一半内部、一半公开。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 若仓内已有脚本直接调用 `__CreateLiteralAsset`，内部命名面切换会带来兼容性变化；因此需要和 `Issue-35` 一样保留薄 wrapper 过渡一轮。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与对象绑定测试。 2. 运行新增负向回归，确认空 `AssetClass` 路径返回 `null` 并记录脚本错误，而不是崩溃。 3. 运行 literal asset 正向 smoke，确认生成器调用内部 helper 后，资产创建、初始化和广播行为保持不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-65 | Architecture | 先修公开 `__WorldContext` contract，恢复 UEAS2 兼容入口并统一生成器输出 |
| P2 | Issue-66 | Defect | 紧接着清理 `opCast` 中残留的测试探针和 `Display` 日志副作用 |
| P2 | Issue-67 | Defect | 最后与 `Issue-35` 合并处理 literal asset helper 的空参防线和内部命名面 |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-68：`TArray` 缺失 UEAS2 的 self-alias 防护，拿同一容器元素引用做增删会读到失效地址

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray_Functions.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds/Bind_TArray_Functions.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | `Bind_TArray_Functions.h:17-42`; `Bind_TArray.cpp:654-675, 796-825, 1090-1119, 1157-1186`; `Bind_TArray_Functions.h (UEAS2): 14-45`; `Bind_TArray.cpp (UEAS2): 627, 778, 1077, 1147`; `rg -n "CheckAddress|Add\\(.*\\[|Insert\\(.*\\[|RemoveSwap\\(.*\\[|Remove\\(.*\\[" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 仅命中无关 `Add(...)` 调用，未命中任何 self-alias 回归 |
| 问题 | current `FArrayOperations` 头文件已经完全没有 UEAS2 的 `CheckAddress()` helper；`Add()`、`Insert()`、`Remove()`、`RemoveSwap()` 也都直接把外部 `Value` 指针继续传给 `CopyValue()` / `Memcpy()` / `IsValueEqual()`。UEAS2 对应四条 mutator 在进入真正修改前都会调用 `CheckAddress(Arr, Meta, Value)`，若 `Value` 落在当前容器内存范围内就抛出 `"Attempting to use a container element which already comes from the container being modified."`。 |
| 根因 | `Bind_TArray` 迁移时保留了 iterator/reference invalidation 的调试层，但把专门防御“容器内部地址再次传回正在修改的容器”这一条自别名检查整段删掉了。 |
| 影响 | 脚本写出 `Values.Add(Values[0])`、`Values.Insert(Values[0], 0)`、`Values.Remove(Values[0])`、`Values.RemoveSwap(Values[0])` 这类调用时，`Arr.Add/Insert/Remove` 可能先重分配或移动底层内存，随后绑定层仍继续读取旧 `Value` 地址。结果不是稳定的脚本错误，而是悬空读取、错误比较，甚至按垃圾值继续复制/删除。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 直接恢复 UEAS2 的 `CheckAddress()` 防线，并把所有“会在比较后继续修改容器”的 mutator 统一接回这条 helper。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray_Functions.h` 里补回 `#if DO_CHECK` 包裹的 `CheckAddress(FScriptArray&, asCObjectType*, void*)`，实现与 UEAS2 一致：按 `Arr.GetData()` 到 `Arr.ArrayMax * NumBytesPerElement` 判断 `Addr` 是否来自当前容器，并在命中时 `FAngelscriptEngine::Throw(...)`。 2. 在 `Bind_TArray.cpp` 的 `Add()`、`Insert()`、`Remove()`、`RemoveSwap()` 中，在 `InvalidateReferencesToArray()` 之后、真正 `Arr.Add/Insert/Remove` 之前统一调用 `CheckAddress()`；保持 `RemoveSingle()` / `RemoveSingleSwap()` 现状不动，避免把不需要继续迭代的路径一并扩大修改面。 3. 抽一个最小共享注释到 `CheckAddress()` 上，明确“只用于会在持有 `Value` 指针期间修改 `Arr` 的 mutator”，防止后续又在别处各写一份 ad-hoc 地址校验。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` 新增四条负向回归：分别覆盖 `Add(Values[0])`、`Insert(Values[0], 0)`、`Remove(Values[0])`、`RemoveSwap(Values[0])`，断言脚本得到固定错误文本，而不是继续执行。 5. 再补一条正向对照：`Values.Add(OtherValue)` 和 `Values.Remove(OtherValue)` 仍保持原行为，防止恢复 guard 时误伤正常值传递路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray_Functions.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 恢复 guard 后，历史上依赖“同数组元素引用也能拿来增删”的脚本会从不稳定行为变成稳定报错；这是必要的兼容性收紧，但需要用第 5 步的正向对照证明正常非 alias 用法没有被一起拦截。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与容器绑定测试。 2. 运行新增 self-alias 负向回归，确认四条路径都抛出固定错误文案。 3. 运行现有/新增正向 `TArray` smoke，确认普通 `Add/Insert/Remove/RemoveSwap` 不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-68 | Defect | 立即修复，先恢复 `TArray` mutator 的 self-alias 防线 |

---

## 发现与方案 (2026-04-09 00:29)

### Issue-69：`GetAllSubclassesOf()` 没有过滤已删除的 `UASClass` tombstone，热重载后脚本还能枚举到失效类

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | `Bind_UObject.cpp:356-379`; `AngelscriptClassGenerator.cpp:4990-5002`; `Bind_UObject.cpp (UEAS2): 202-213, 242-269`; `AngelscriptClassBindingsTests.cpp:50, 386-387` |
| 问题 | current `GetAllSubclassesOf()` 只过滤 `CLASS_Deprecated`、`CLASS_NewerVersionExists` 和 abstract class，然后直接把命中的 `UClass` 放进结果。与此同时，`CleanupRemovedClass()` 会把被删除的 `UASClass` 置成 `ScriptTypePtr = nullptr`、`ConstructFunction = nullptr`、`DefaultsFunction = nullptr`，并打上 `CLASS_NotPlaceable | CLASS_HideDropDown | CLASS_Hidden`。UEAS2 在同一绑定点额外定义了 `IsDeletedAngelscriptClass()`，并在 `GetAllSubclassesOf()` 枚举时显式跳过这些 tombstone；current 这一步已经完全缺失。现有 `AngelscriptClassBindingsTests.cpp` 只覆盖 `FindClass("AActor")` 和 `FindClass("ABindingStaticClassActor")` 的 happy path，没有任何 reload/remove 后的 subclass 枚举回归。 |
| 根因 | class lookup / enumeration 逻辑迁移时，只保留了通用 class flag 过滤，没有把 `CleanupRemovedClass()` 对脚本类 tombstone 的专用判定一并迁回 `Bind_UObject.cpp`。 |
| 影响 | 热重载或脚本类删除后，脚本调用 `GetAllSubclassesOf()` 仍可能拿到 `ScriptTypePtr == nullptr` 的失效壳类。后续一旦继续取 `GetDefaultObject()`、反射函数，或把枚举结果回灌到 UI/工具列表，就会把“已删除的类”重新暴露给脚本层和编辑器层。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 UEAS2 的 deleted-class predicate 恢复到 current，并与现有 class lookup helper 收口到同一条“脚本可见类”判定链路。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` 中恢复一个 current 版本的 `IsDeletedAngelscriptClass(UClass*)`，逻辑直接对齐 `CleanupRemovedClass()` 的 tombstone 状态：`UASClass` 且 `ScriptTypePtr/ConstructFunction/DefaultsFunction` 全为空，并同时具备 `CLASS_Hidden | CLASS_HideDropDown | CLASS_NotPlaceable`。 2. 将 `GetAllSubclassesOf()` 的枚举循环补上 `#if WITH_EDITOR` 防线，在 `Class->IsChildOf(ParentClass)` 之后、`Subclasses.Add(Class)` 之前统一 `if (IsDeletedAngelscriptClass(Class)) continue;`。 3. 如果后续执行 `Issue-43` 的 lookup 重构，就把这个 predicate 上提为 `ShouldExposeClassToLookup()` 的一部分，避免 subclass 枚举与 `FindClass/__StaticClass/GetAllClasses` 再次分叉。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 增加一个 focused reload/remove 回归：构造或复用现有 hot-reload 测试夹具，先生成一个脚本子类，再触发删除/替换，最后断言 `GetAllSubclassesOf(ParentClass)` 结果里不再包含 tombstone class。 5. 追加一条对照断言，确认正常存活的 script subclass 与 native subclass 仍然能被枚举到，避免把过滤条件误扩大成“隐藏所有 script class”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | tombstone 判定如果只看 class flags、不看 `ScriptTypePtr/ConstructFunction/DefaultsFunction` 三元状态，容易把合法但隐藏的类误过滤掉；因此第 1 步必须严格对齐 `CleanupRemovedClass()` 的实际写法。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与 class binding 测试。 2. 运行新增 reload/remove 回归，确认 `GetAllSubclassesOf()` 不再返回 tombstone `UASClass`。 3. 运行现有 `FindClass` / `GetDefaultObject` happy path，确认正常类枚举不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-69 | Defect | 在 class lookup 系列修复前优先落地，先阻断 tombstone 子类继续外泄 |

---

## 发现与方案 (2026-04-09 00:30)

### Issue-70：`Bind_InputEvents.cpp` 丢掉 `FKey::GetVirtualKey()` 与 `Virtual_Gamepad_*` surface，脚本无法按引擎推荐路径做平台键归一化

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_InputEvents.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/InputCore/Classes/InputCoreTypes.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/InputCore/Private/InputCoreTypes.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/` |
| 行号 | `Bind_InputEvents.cpp:16-48, 518-522`; `Bind_InputEvents.cpp (UEAS2): 31-39, 522-532`; `InputCoreTypes.h:95, 685-691`; `InputCoreTypes.cpp:725-726, 1439-1457`; `rg -n "GetVirtualKey|GetVirtual_Accept|GetVirtual_Back|Virtual_Accept|Virtual_Back|Virtual_Gamepad" Plugins/Angelscript/Source/AngelscriptTest/Bindings -g "*.cpp"` 未命中 |
| 问题 | current `Bind_InputEvents.cpp` 为 `FKey` 只绑定到 `GetKeyName()`，没有 `FKey::GetVirtualKey()`；在 `EKeys` 命名空间里也没有把 `Virtual_Gamepad_Accept` / `Virtual_Gamepad_Back` 公开给脚本，而是仅额外塞了两个预解析好的 `const FKey Virtual_Accept` / `Virtual_Back` 变量。UEAS2 同一文件既绑定了 `FKey GetVirtualKey() const`，也保留了 `Virtual_Gamepad_Accept` / `Virtual_Gamepad_Back` 常量，并额外提供 `GetVirtual_Accept()` / `GetVirtual_Back()` 兼容 property。更关键的是，引擎头文件已经把旧别名标记为 `UE_DEPRECATED(5.7, "Use Virtual_Gamepad_Accept.GetVirtualKey() instead")` / `...Back...`，说明 current 现在缺失的恰好是引擎推荐的新 canonical 路径。 |
| 根因 | 输入绑定迁移时只保留了旧式“预先算好两个虚拟键别名”的最小兼容层，没有把 `FKey` 自身的 virtual-key 归一化 API 与对应 `EKeys` 常量一起迁回。 |
| 影响 | 脚本现在只能消费两个固定别名 `Virtual_Accept/Back`，无法对任意 `FKey` 调 `GetVirtualKey()`，也无法按引擎/UEAS2 文档写 `EKeys::Virtual_Gamepad_Accept.GetVirtualKey()`。平台输入归一化逻辑被压成两个硬编码常量，后续如果引擎继续增加新的 virtual key，current bind 将完全没有扩展入口。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 `FKey` 的 virtual-key canonical surface，同时保留现有 `Virtual_Accept/Back` 作为过渡兼容别名。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp` 的 `FKey_` 绑定段补回 `FKey_.Method("FKey GetVirtualKey() const", &FKey::GetVirtualKey);`，与 `GetKeyName()` 同级公开。 2. 在 `EKeys` 命名空间恢复 `BIND_EKEYS(Virtual_Gamepad_Accept);` 和 `BIND_EKEYS(Virtual_Gamepad_Back);`，让脚本可以直接拿到原始 virtual-key 常量。 3. 保留 current 的 `Virtual_Accept` / `Virtual_Back` 兼容入口，但改成和 UEAS2 一致的 `BindGlobalFunction("const FKey GetVirtual_Accept() property no_discard", ...)` / `...Back...`，内部直接返回 `EKeys::Virtual_Gamepad_Accept.GetVirtualKey()` / `...Back...`；不要继续把它们做成只读静态变量，避免与 canonical surface 语义分叉。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 `AngelscriptInputBindingsTests.cpp`，至少覆盖三类回归：`EKeys::Virtual_Gamepad_Accept.GetVirtualKey()` 可编译执行；`GetVirtual_Accept` / `GetVirtual_Back` 仍返回与 canonical 路径一致的 key；任意普通键如 `EKeys::SpaceBar.GetVirtualKey()` 也可调用并返回稳定结果。 5. 在该测试里补一条 API surface 断言，确认 `FKey` 同时存在 `GetKeyName()` 与 `GetVirtualKey()`，防止后续再把 virtual-key 归一化能力删回成纯别名。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 恢复 `Virtual_Gamepad_*` 常量后，脚本 surface 会同时存在 canonical 名称与兼容别名；如果不把别名收口成 property wrapper，后续维护仍会面对两套“看起来相同、实际来源不同”的常量。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增输入绑定测试。 2. 运行回归，确认 `FKey::GetVirtualKey()`、`EKeys::Virtual_Gamepad_*`、`GetVirtual_Accept/Back` 三条路径都可用且结果一致。 3. 复查生成的脚本 API surface，确认不再只有 `Virtual_Accept/Back` 两个硬编码别名。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-70 | Defect | 在高优先级稳定性修复后执行，优先补齐输入 virtual-key 的 canonical coverage |

---

## 发现与方案 (2026-04-09 00:33)

### Issue-71：`TArray::Reserve()` 被错误实现成 `Reset()`，脚本调用后会直接清空已有元素

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Containers/ScriptArray.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptLearningTrace.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 行号 | 910-928；882-891；154-165；63；178 |
| 问题 | 当前 `FArrayOperations::Reserve()` 在完成容量下限计算后调用的是 `Arr.Reset(ReservedSize, ...)`。引擎 `FScriptArray::Reset()` 明确会在 `NewSize <= ArrayMax` 时把 `ArrayNum = 0`，否则走 `Empty(...)`，它不是“保留元素只扩容”的语义。UEAS2 对应实现同一位置调用的是 `Arr.ResizeTo(ReservedSize, ...)`。额外扫描测试树，命中 `Reserve(` 的仅是 C++ 原生 `TArray::Reserve()` 用法，没有任何脚本绑定回归覆盖 `FArrayOperations::Reserve()`。 |
| 根因 | `Bind_TArray` 迁移时把 `Reserve` 和 `Reset` 混用，脚本 API 名称保持为 `Reserve()`，底层却退化成“清空数组并重置 slack”的实现。 |
| 影响 | 脚本只要对非空数组调用 `Reserve()`，后续 `Num()`、遍历和索引都会看到空数组；当 `ReservedSize <= ArrayMax` 时还会直接丢失逻辑长度，形成静默数据丢失，问题比普通性能型 reserve 退化更严重。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `Reserve()` 恢复为纯 capacity 调整，不再改写 `ArrayNum`，并补一条脚本级回归锁住“保留元素”语义。 |
| 具体步骤 | 1. 将 `Bind_TArray.cpp:926-927` 的 `Arr.Reset(...)` 改回 `Arr.ResizeTo(ReservedSize, Ops->NumBytesPerElement, Ops->Alignment)`，与 UEAS2 行为对齐。 2. 保留 `if (Arr.Num() > ReservedSize) ReservedSize = Arr.Num();` 这条下限保护，避免 `Reserve(Smaller)` 误触 shrink。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` 新增脚本回归：先创建包含多个元素的 `array<int>`，调用 `Reserve(64)` 后断言 `Num()` 和各索引值保持不变。 4. 再补一条对照用例验证 `Reserve(1)` 也不会删除已有元素，确保“保留最少容量”路径不会因为下限修正而回退。 5. 在测试命名中显式写出 `ReserveKeepsExistingElements`，防止后续把它误当成性能测试而删掉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险主要是已有脚本若无意中依赖了当前“`Reserve()` 会清空数组”的错误行为，修复后结果会改变；不过这属于对错误语义的纠偏，应由新增回归明确锁定新行为。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与容器绑定测试。 2. 运行新增 `ReserveKeepsExistingElements` 脚本回归，确认 `Reserve()` 后元素内容与数量完全保持。 3. 补跑现有容器 smoke，确认 `Empty()`/`Reset()` 相关 API 没有被误影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-71 | Defect | 立即修复，先消除 `Reserve()` 导致的静默数据丢失 |

---

## 发现与方案 (2026-04-09 00:40)

### 去重补充

| 项目 | 内容 |
|------|------|
| 说明 | `Issue-71` 与既有 `Issue-7` / `Issue-16` 指向同一 `TArray::Reserve()` 根因。后续执行优先级仍以既有条目为准，`Issue-71` 不再单独新增排期。 |

### Issue-72：`USceneComponent` 旧式 child-query 已脱离 `DeprecateOldActorGenericMethods` 策略链，且只读查询签名退化成非 `const`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_USceneComponent.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 23-110；23-113；`rg -n "GetChildrenComponentsByClass|GetChildComponentByClass|DeprecateOldActorGenericMethods" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | current `Bind_USceneComponent.cpp` 无条件公开 `GetChildrenComponentsByClass(...)`，文件内既不读取 `DeprecateOldActorGenericMethods`，也没有 `DeprecatePreviousBind(...)`。同文件 `GetChildComponentByClass(...)` 还把 UEAS2 的 `const` method / `const USceneComponent*` 签名退化成了可变版本。UEAS2 对应实现则会在 `DeprecateOldActorGenericMethods == Disallowed` 时整段不注册旧 child-query，并在 `Deprecated` 模式下显式打弃用标记，同时保持 `GetChildComponentByClass(...) const`。 |
| 根因 | actor 旧泛型方法的迁移策略只修到了 `Bind_AActor.cpp`，没有把同一家族的 scene-component child-query 一并纳入同一个配置与 const-correctness 约束。 |
| 影响 | 插件内部现在出现同类 legacy query API 的双重标准：`AActor` 旧泛型查询可受配置迁移，`USceneComponent` 旧 child-query 却永久裸暴露；同时脚本在只读 `USceneComponent` 句柄上无法继续调用 `GetChildComponentByClass(...)`。这会让 API surface 的迁移规则和只读契约都变得不可预测。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `Bind_USceneComponent.cpp` 接回与 `Bind_AActor.cpp` 相同的旧 API 配置链，并恢复 child-query 的只读签名。 |
| 具体步骤 | 1. 在 `Bind_USceneComponent.cpp` 头部补回 `#include "AngelscriptSettings.h"`，并复用 current 已在 `Issue-31` 里规划的 `DeprecateOldActorGenericMethods` 配置，而不是再造第二套开关。 2. 将 `GetChildComponentByClass(...)` 改回 UEAS2 语义：脚本声明使用 `const` method，native lambda 接收 `const USceneComponent* ParentComp`。 3. 把 `GetChildrenComponentsByClass(...)` 放进与 UEAS2 相同的配置分支：`Disallowed` 时不注册，`Deprecated` 时注册后立刻 `DeprecatePreviousBind("Use GetChildrenComponentsByClass that returns an array instead")`，`Allowed` 时保持公开。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 新增配置回归，验证 `DeprecateOldActorGenericMethods` 三种模式会正确影响 `USceneComponent::GetChildrenComponentsByClass(...)` 的存在性与 trait。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` 或新增 scene-component 绑定测试里补一条 const-context smoke：`const USceneComponent Root = ...; USceneComponent Child = Root.GetChildComponentByClass(USceneComponent::StaticClass());` 必须成功编译；同时补一条 deprecated/disallowed 模式下旧 child-query 的编译诊断回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 现有脚本如果依赖旧 child-query 永远可用，切到 `Deprecated` / `Disallowed` 后会在编译期暴露；同时恢复 `const` 签名会收紧一部分以前靠可变句柄通过的调用点。 |
| 前置依赖 | `Issue-31` 中的 `DeprecateOldActorGenericMethods` 配置面需先落地或一并实现 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与相关绑定/配置测试。 2. 运行新加的 bind-config 回归，确认 `USceneComponent` 旧 child-query 随配置切换为 Allowed / Deprecated / Disallowed。 3. 运行 const-context smoke，确认 `GetChildComponentByClass(...) const` 在只读句柄上恢复可用。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-72 | Architecture | 在 `Issue-31` 配置面落地后立即收拢，避免 legacy actor/component 查询继续分叉 |

---

## 发现与方案 (2026-04-09 00:42)

### Issue-73：`ULevel::GetActors()` 把底层 `Actors` backing array 原样暴露给脚本，结果集中可能长期混入 `nullptr` 槽位

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/Level.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/Level.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 行号 | 107-110；431-432；1794-1803；180 |
| 问题 | current `ULevel::GetActors()` 直接返回 `Level->Actors` 的引用。引擎头文件把它明确定义为“供 `FActorIteratorBase` 使用的所有 actor 数组”，而 `Level.cpp` 又专门存在“先把 null 排到尾部，再把这些 null 条目裁掉”的整理逻辑，说明这个底层容器在正常运行周期中会出现 `nullptr` 空洞。现有脚本测试只验证 `GetWorld().GetPersistentLevel().GetActors().Num()` 能调用，没有任何遍历结果集或处理空槽位的回归。 |
| 根因 | bind 层把 `ULevel` 的内部存储数组直接当成脚本友好的稳定结果集暴露，没有在 API 表面区分“引擎内部 backing store”和“已过滤的 actor 列表”。 |
| 影响 | 脚本若把 `GetActors()` 当作 `GetAllActorsOfClass()` 那样的稳定列表直接遍历，就必须自己知道 Unreal 底层 `Actors` 数组可能含空槽位；否则很容易在关卡切换、actor 删除、热重载或排序整理前后撞上额外的 null 分支，甚至把空句柄继续传给后续绑定方法。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 保留 raw access 的能力，但把公开脚本 surface 改成返回已过滤结果集，必要时另开一个显式 `__Internal_*` 入口承载底层数组。 |
| 具体步骤 | 1. 将 `Bind_UWorld.cpp:107-110` 的公开 `GetActors()` 改为返回一个新的 `TArray<AActor>` 值，而不是 `const TArray<AActor>&` 直接引用 `Level->Actors`。 2. 在实现里显式遍历 `Level->Actors`，只把非空 `AActor*` 追加到返回数组，保证脚本拿到的是稳定结果集；如果团队仍需要原始 backing array 诊断入口，就单独增加 `const TArray<AActor>& __Internal_GetActorsRaw() const`，并在命名上明确“内部/raw only”。 3. 若担心分配开销，可在实现里 `Result.Reserve(Level->Actors.Num())`，但不要再把稀疏语义泄露给公开 surface。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` 新增一条 focused 回归：构造或复用一个存在空 actor 槽位的 level 场景，断言 `GetActors()` 返回结果里没有 `null`，且只包含有效 actor。 5. 保留现有 `AngelscriptSubsystemScenarioTests.cpp:180` 的 `Num()` happy path，并补一条显式遍历 smoke，确认脚本侧无需再为 `GetActors()` 额外写 `if (Actor is null)` 过滤。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 预估工作量 | M |
| 风险 | 公开 surface 从“返回引用”改成“返回过滤后的数组副本”后，少数脚本若依赖 backing array 的稀疏布局或引用语义，会看到行为变化；因此需要第 2 步的 `__Internal_*` 逃生口，避免诊断类用法被一起切断。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 world 绑定测试。 2. 运行新的 `GetActorsFiltersNullSlots` 回归，确认返回数组不含 `null`。 3. 复跑现有 `GetActors().Num()` happy path，确认公开查询语义收紧后不会影响正常计数。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-73 | Defect | 在 world helper 空值收口后执行，先把 `ULevel::GetActors()` 从 backing array 改成稳定结果集 |

---

## 发现与方案 (2026-04-09 00:45)

### Issue-74：`Bind_Subsystems.cpp` 的 `Class::Get()` accessor 在生命周期边界直接解引用 `GEngine` / `GEditor`，失败形态是崩溃而不是脚本错误

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Editor/UnrealEd/Private/Commandlets/GatherTextCommandletBase.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 43-95；427，3764；119；`rg -n "Subsystem.*Get\\(|GetEngineSubsystemBase|GetWorldFromContextObject\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | current `Bind_Subsystems.cpp` 中 `UEngineSubsystem::Get()` 直接执行 `GEngine->GetEngineSubsystemBase(...)`，`UGameInstanceSubsystem::Get()` 与 `UWorldSubsystem::Get()` 也都先走 `GEngine->GetWorldFromContextObject(...)`，editor 分支则直接 `GEditor->GetEditorSubsystemBase(...)`。引擎源码明确把 `GEngine` 初始化为 `NULL` 并在 teardown 时再次写回 `NULL`；editor commandlet 路径还存在 `GEngine = GEditor = NULL` 的显式语句。也就是说，这些 accessor 当前首个失败点不是“返回 `null` 子系统”或脚本错误，而是全局单例还没就绪时的裸解引用。 |
| 根因 | subsystem bind 复制了“直接从全局 engine/editor 单例取 subsystem”的最短路径，但没有沿用 `Bind_UWorld.cpp` / `Bind_AActor.cpp` 已经暴露出的生命周期防护需求。 |
| 影响 | 在 commandlet、引擎初始化早期、退出阶段，或测试框架创建隔离 engine/editor 环境的窗口里，脚本只要调用任一 `Class::Get()` subsystem accessor，就可能直接崩在 bind 层。当前测试树对这些 accessor 没有任何脚本回归，生命周期边界完全处于无守护状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 subsystem accessor 收口统一的 engine/editor 生命周期 helper，失败时稳定返回 `null` 并记录脚本错误，而不是直接解引用全局单例。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 新增或复用一个内部 helper（可并入 `Issue-5` 计划中的 `Bind_WorldContextHelpers.h`），提供 `ResolveEngineOrThrow()`, `ResolveEditorOrThrow()`, `ResolveWorldForSubsystemOrThrow()` 三个入口：先检查 `GEngine` / `GEditor` 是否为空，再解析 world-context；失败时统一 `FAngelscriptEngine::Throw("Engine was null.")`、`FAngelscriptEngine::Throw("Editor was null.")` 或 `FAngelscriptEngine::Throw("Invalid World Context")`。 2. 修改 `Bind_Subsystems.cpp:48-94`：editor subsystem `Get()` 先走 `ResolveEditorOrThrow()`；engine subsystem `Get()` 先走 `ResolveEngineOrThrow()`；game instance/world subsystem `Get()` 先走 `ResolveWorldForSubsystemOrThrow()`，拿到 world 后再继续 `GetGameInstance()` / `GetSubsystemBase()`。 3. 失败路径统一返回 `nullptr`，不要继续让底层 `GetWorldFromContextObject(..., ReturnNull)` 或全局空指针决定结果；同时把 `GetGameInstance() == nullptr` 的情况也收口成显式脚本错误，例如 `"GameInstance was null."`，避免继续 silent null。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemBindingsTests.cpp` 新增生命周期回归：至少覆盖 engine 为 null、editor 为 null、world-context 无效三条负向路径，断言都返回 `null` 且记录对应错误文本。若测试框架不方便直接清空全局单例，就通过 helper 注入或隔离 world-context scope 模拟。 5. 顺手复核同文件签名与 UEAS2 的差异，确保 `Class::Get()` 这批 accessor 在修复生命周期 guard 后也补回 `no_discard`，避免“虽然不崩了，但返回值还能被静默忽略”继续成为下一个回归面。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldContextHelpers.h` 或同类 helper 文件，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | subsystem accessor 在失败路径上会从“返回不透明 null / 直接崩溃”收紧成显式脚本错误，可能暴露历史上被当成普通空结果处理的错误调用；如果 helper 设计得太重，还可能与 `Issue-5` 的 world helper 产生重复实现。 |
| 前置依赖 | 可复用 `Issue-5` 中的 world-context helper；若其尚未实现，需要在本 issue 中先抽出最小共享 helper |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 subsystem 绑定测试。 2. 运行负向生命周期回归，确认 engine/editor/world-context 不可用时不再崩溃，而是返回 `null` 并记录精确错误。 3. 保留一条正常 `Class::Get()` happy path，确认合法环境下的 subsystem 获取行为不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-74 | Defect | 与 `Issue-5` 合并批量处理，优先收口所有 `GEngine` / `GEditor` 生命周期解引用点 |

---

## 发现与方案 (2026-04-09 00:51)

### Issue-75：`TSoftObjectPtr::LoadAsync` 对空 soft reference 缺少前置校验，空路径会被直接送进 `LoadPackageAsync`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TSoftObjectPtr.cpp` |
| 行号 | 483-527；486-525 |
| 问题 | current `TSoftObjectPtr_.Method("void LoadAsync(FOnSoftObjectLoaded OnLoaded) const", ...)` 在 `Bind_TSoftObjectPtr.cpp:483-527` 里只检查 subtype 是否为 `AActor` / `UActorComponent`，随后在对象未加载时直接执行 `LoadPackageAsync(*FPackageName::ObjectPathToPackageName(ObjectCopy.ToString()), Delegate, 100)`。整个入口没有任何 `Self->IsNull()` 防线。UEAS2 对应实现则在 `486-495` 先执行 `if (Self->IsNull()) { Throw(...); return; }`，明确阻断空 soft reference。也就是说，current 会把默认构造或 `Reset()` 后的空引用直接转换成空 package path 并交给 async loader。 |
| 根因 | `Bind_TSoftObjectPtr.cpp` 在迁移 `LoadAsync` 时保留了 actor/component 限制与异步回调逻辑，但把 UEAS2 已有的空引用 guard 整段落掉了。 |
| 影响 | 脚本对空 `TSoftObjectPtr` 调用 `LoadAsync` 时，失败形态不再是稳定的脚本错误，而是一次空 package name 的底层异步加载请求。调用方无法区分“引用为空”和“资源加载失败”，后续只剩 loader 日志或回调里的 `null` 可观察，错误处理语义明显变弱。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 `LoadAsync` 入口恢复 `Self->IsNull()` 前置校验，并把空引用失败收口成确定的脚本错误。 |
| 具体步骤 | 1. 在 `Bind_TSoftObjectPtr.cpp:483-499` 的 lambda 入口增加 `if (Self == nullptr || Self->IsNull())` 分支，命中时立即 `FAngelscriptEngine::Throw("Soft object reference was null."); return;`，不要继续落到 `ObjectPathToPackageName()`。 2. 保留现有 actor/component subtype 限制，但把它放在空引用检查之后，避免空引用先被误归类成“禁止加载 actor/component”。 3. 在 `LoadPackageAsync` 调用前补一层 `const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectCopy.ToString());`；若 `PackageName.IsEmpty()`，同样抛脚本错误并返回，防止未来再通过其他异常 soft path 形成空包名请求。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 `AngelscriptSoftReferenceBindingsTests.cpp`，覆盖三条回归：空 `TSoftObjectPtr<UObject>` 调 `LoadAsync` 必须记录 `"Soft object reference was null."`；空 `TSoftObjectPtr<AActor>` 仍优先走空引用错误而不是 actor 限制；合法已加载 soft 引用继续立即回调对象。 5. 如团队希望与 UEAS2 的错误文本保持兼容，可在测试里锁定关键语义而不是逐字全文，但必须保证“空引用”被单独诊断。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 现有脚本若把空 soft reference `LoadAsync` 当成“静默失败并等待回调”的容错路径，修复后会在运行时显式报错；这是把原本模糊的失败语义收紧成可诊断错误。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 soft reference 绑定测试。 2. 运行空引用回归，确认不会再发起空 package name 的异步加载。 3. 复测合法 `LoadAsync` 路径，确认已加载对象和正常 soft path 的回调行为不回退。 |

### Issue-76：`Bind_Json.cpp` 的 JSON builder/probe surface 被裁成半套，`FJsonArray` 与 `FJsonObject` 的错误处理契约已和 UEAS2 分叉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Json.cpp` |
| 行号 | 155-165，638-665；155-165，650-656，691-709 |
| 问题 | current `Bind_Json.cpp` 里，`FJsonValueArrayContainer` 明明已经实现了 `AddBoolean(bool)`、`AddArray(const FJsonValueArrayContainer&)`、`AddObject(const FJsonObjectContainer&)`，但 `FJsonArray` 绑定块只公开了 `Empty()`、`AddString()`、两个 `AddNumber()`、`Num()`、`GetValueAt()`。同一个文件的 `FJsonObject` 绑定也只保留了会抛错的 `GetStringField()` / `GetNumberField()` / `GetBoolField()`，以及 `TryGetObjectField()` / `TryGetArrayField()` 两个 probe-style API，完全没有 `TryGetStringField()` / `TryGetNumberField()` / `TryGetBoolField()`。UEAS2 对应位置则同时暴露 `FJsonArray::AddBoolean/AddArray`，以及 `FJsonObject` 的三条 primitive `TryGet*Field`。这说明 current 并不是缺底层能力，而是把 JSON surface 裁成了不对称的一半。 |
| 根因 | JSON bind 迁移时只保留了最小可用的 string/number/object/array happy path，没有把已存在的 container helper 和 UEAS2 的 probe-style primitive API 一起接回脚本 surface。 |
| 影响 | 当前脚本构造 JSON 时，bool / nested array / nested object 需要绕路到更低层 `FJsonValue` 组装；读取 primitive 字段时，又只能在“直接抛错 getter”与“自己先取 `FJsonValue` 再手工 `TryGet*`”之间二选一。结果是同一个 JSON API 家族内部的 builder/probe 契约不一致，UEAS2 兼容脚本也无法直接复用。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `Bind_Json.cpp` 恢复到“builder 完整可用、probe 对 primitive/object/array 对称”的 surface，并用运行时测试锁住不抛错的 `TryGet*` 契约。 |
| 具体步骤 | 1. 在 `Bind_Json.cpp` 的 `FJsonArray` 注册块中补回三条方法：`void AddBoolean(bool B)`、`void AddArray(FJsonArray Array)`、`void AddObject(FJsonObject Object)`；优先直接复用当前 `FJsonValueArrayContainer` 已有实现，不要再新造第二套 wrapper。 2. 在 `FJsonObjectContainer` 内新增 `TryGetStringField`、`TryGetNumberField`、`TryGetBoolField` 三个成员函数，按 UEAS2 的小函数写法实现：`CheckValidObject()` 失败时返回 `false`，成功时通过 `GetField<EJson::None>(FieldName)->TryGet*` 填充 out 参数，不调用 `TypeErrorMessage()`、不抛脚本异常。 3. 在 `Bind_Json.cpp:638-665` 附近把这三条 primitive `TryGet*Field` 注册到 `FJsonObject`，并与现有 `TryGetObjectField` / `TryGetArrayField` 排在同一段，恢复完整 probe-style surface。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonBindingsTests.cpp` 新增两组回归：一组用脚本构造 `FJsonArray`，验证 bool / nested array / nested object 都能直接追加并被读回；另一组对 `FJsonObject` 运行 `TryGetStringField` / `TryGetNumberField` / `TryGetBoolField`，断言字段不存在或类型不匹配时只返回 `false`，不会记录脚本异常。 5. 保留现有 throwing getter 行为不变，并在测试中加一条对照：`GetNumberField("Name")` 仍走 `Issue-23` 已规划的报错+默认值路径，确保“getter 抛错 / try-get 不抛错”两套契约边界清晰。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦补回 primitive `TryGet*Field`，部分脚本可能会从当前“必须走 throwing getter”迁移到 probe-style 逻辑；需要用测试明确区分两套契约，避免后续维护时又把 `TryGet*` 改回报错路径。 |
| 前置依赖 | 无；但建议与 `Issue-23` 同一批次验证，避免 JSON getter/try-get 语义再次分叉 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 JSON 绑定测试。 2. 运行 array builder 回归，确认 `AddBoolean` / `AddArray` / `AddObject` 可直接在脚本中使用。 3. 运行 primitive `TryGet*Field` 回归，确认失败时返回 `false` 且不抛异常，而现有 throwing getter 行为保持不变。 |

### Issue-77：`UAssetManager` 丢失 `LoadPrimaryAssetsWithType(...)`，按 `FPrimaryAssetType` 的批量异步加载入口已和 UEAS2 脱节

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp` |
| 行号 | 49-111；90-140 |
| 问题 | current `Bind_UAssetManager.cpp` 只保留了 `LoadPrimaryAsset(...)` 和 `LoadPrimaryAssets(...)` 两条按 `FPrimaryAssetId` 驱动的异步入口，并在 `49-81` 里只实现了 `AssetManager_LoadPrimaryAssets(...)` 这一套 helper。UEAS2 对应文件则额外提供 `AssetManager_LoadPrimaryAssetWithType(...)`，并在 `132-136` 公开 `void LoadPrimaryAssetsWithType(FPrimaryAssetType PrimaryAssetType, ...)`。也就是说，current 已经失去了“直接按资产类型批量预加载”的标准 bind surface，脚本必须先自己枚举并拼装 `TArray<FPrimaryAssetId>`。 |
| 根因 | `Bind_UAssetManager.cpp` 迁移时只搬回了按 id 加载的最小闭环，没有把 UEAS2 已有的按 type 批量加载 helper 和方法声明一并带回。 |
| 影响 | 依赖 `FPrimaryAssetType` 做分组预加载、按类型切换 content bundle 或复用现有资产清单规则的脚本，现在都要额外写一层 “type -> ids” 桥接。结果不仅和 UEAS2 surface 分叉，也把原本应该留在 `UAssetManager` bind 层的异步回调/取消逻辑拆散到业务脚本里。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 UEAS2 的 `LoadPrimaryAssetsWithType(...)` helper 和脚本声明迁回 current，并复用现有 id-based 异步回调/取消模式。 |
| 具体步骤 | 1. 在 `Bind_UAssetManager.cpp` 参照 UEAS2 新增 `static` helper `AssetManager_LoadPrimaryAssetWithType(UAssetManager* AssetManager, FPrimaryAssetType PrimaryAssetType, const TArray<FName>& LoadBundles, int32 Priority, UObject* OptionalCallbackObject, FName OptionalFinishedCallbackFunctionName, FName OptionalCanceledCallbackName)`，内部直接调用 `AssetManager->LoadPrimaryAssetsWithType(...)`，并沿用当前 `AssetManager_LoadPrimaryAssets(...)` 的 `CompleteDelegate` / `CancelDelegate` 绑定模式。 2. 在 `Bind_AssetManager` 注册块中补回 `UAssetManager_.Method("void LoadPrimaryAssetsWithType(FPrimaryAssetType PrimaryAssetType, const TArray<FName>& LoadBundles = TArray<FName>(), int32 Priority = 0, UObject OptionalCallbackObject = nullptr, FName OptionalFinishedCallbackFunctionName = NAME_None, FName OptionalCanceledCallbackName = NAME_None)", ...)`，默认参数与 UEAS2 保持一致。 3. 不要把这条新入口降级成“先查 type 再转 ids”的脚本 wrapper；应直接让 bind 调到原生 `LoadPrimaryAssetsWithType(...)`，避免重复维护回调和取消逻辑。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp` 新增 bind-surface 回归：通过编译脚本片段 `void Queue(UAssetManager Manager, FPrimaryAssetType Type) { Manager.LoadPrimaryAssetsWithType(Type); }` 或类型反射断言，确认该方法声明存在并可解析。 5. 若测试环境能拿到已初始化的 `UAssetManager`，再补一条最小 runtime smoke：传一个默认 `FPrimaryAssetType` 和空 bundle/callback 参数，确认调用路径可执行且不会破坏现有 `LoadPrimaryAsset(s)` 行为；若环境受限，则至少保留 compile-surface 断言，锁住 API 存在性。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 主要风险是测试环境里 `UAssetManager` 初始化时机不稳定，runtime 回归可能需要做环境判定或降级成 compile-surface 检查；功能本身只是补回原生入口，不应影响现有 id-based 加载语义。 |
| 前置依赖 | 无；若 `Issue-61` 的 `UAssetManager::Get()` / `IsInitialized()` 也在并行修复，可复用同一测试基建 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 asset manager 绑定测试。 2. 运行 compile-surface 回归，确认 `UAssetManager` 类型上重新出现 `LoadPrimaryAssetsWithType(...)`。 3. 若 runtime 环境允许，再执行最小调用 smoke，确认 type-based 加载入口可调用且不会回退现有 `LoadPrimaryAsset(s)`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-75 | Defect | 立即修复，先阻断空 soft reference 进入 `LoadPackageAsync` 的错误路径 |
| P2 | Issue-77 | Architecture | 紧随其后，补回 `UAssetManager` 的 type-based 批量加载入口，恢复资产加载 surface parity |
| P2 | Issue-76 | Architecture | 在 JSON getter 语义修复窗口一并处理，恢复 builder/probe surface 对称性 |

---

## 发现与方案 (2026-04-09 01:07)

### Issue-78：`TOptional` 绑定移除了 Unreal intrusive optional state，`TOptional<FName>` 这类 native interop 布局已经与引擎真实内存模型分叉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TOptional.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Misc/Optional.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/UObject/PropertyOptional.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/UObject/NameTypes.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 行号 | 34-45，59-80，147-173，317-329，354-378，494-587；17-55，57-172；31-49，60-94，135-175，373-465，591-699；52-55，87-89，139-140；28-73，139-176；70，619，1029；93-96；54-88 |
| 问题 | current `Bind_TOptional.cpp/.h` 已把 UEAS2 中的 `HasIntrusiveUnsetOptionalState()`、`InitializeIntrusiveUnsetOptionalValue()`、`ClearIntrusiveOptionalValue()`、template size calculator 与 `bIsIntrusive` 分支整组删除，统一退化成“value payload + 尾部 bool”模型：`GetIsSetPtr()` 固定返回 `&Optional + TypeSize`，`ConstructValue()` / `Reset()` / `Set()` 也只维护外置 bool。对照 Unreal 原生，`Optional.h` 明确说明 intrusive optional 不额外占 `bIsSet` 空间，`PropertyOptional.h` 也明确按 `ValueProperty->HasIntrusiveUnsetOptionalState()` 走两套完全不同的 set/unset/size 逻辑；`FName` 甚至直接声明了 `bHasIntrusiveUnsetOptionalState = true`。与此同时，本仓库自己又暴露了 native `UPROPERTY TOptional<FName> OptionalName`。这意味着 current 脚本绑定和引擎原生 `TOptional<T>` / `FOptionalProperty` 已经不再共享同一布局和状态机。 |
| 根因 | `TOptional` 绑定在 current 分支被过度简化成单一外置 bool 模型，迁移时没有保留 UEAS2 已接好的 intrusive optional 分支，也没有继续把模板 size 计算器注册到脚本引擎。 |
| 影响 | 纯脚本内 `TOptional<int>` happy path 仍可运行，但涉及 native `TOptional<FName>`、`FOptionalProperty`、属性复制、序列化、GC schema 和默认值互操作时，脚本侧可见状态与引擎真实内存状态可能错位。现有测试只覆盖纯脚本 `TOptional<int>` / `TOptional<FName>` 行为，没有任何 native property round-trip，因此这条互操作回归当前完全无自动化守护。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 按 UEAS2 恢复 intrusive optional 分支，让脚本 `TOptional<T>` 与 Unreal 原生 `TOptional<T>` / `FOptionalProperty` 重新共享同一布局与 unset 语义。 |
| 具体步骤 | 1. 在 `Bind_TOptional.h` 与 `Bind_TOptional.cpp` 恢复 `bIsIntrusive`、`HasIntrusiveUnsetOptionalState()`、`InitializeIntrusiveUnsetOptionalValue()`、`IsIntrusiveOptionalValueSet()`、`ClearIntrusiveOptionalValue()` 相关分支，覆盖 `EmitReferenceInfo()`、`CopyValue()`、`ConstructValue()`、`DestructValue()`、`Reset()`、`Set()`、`Construct()`、`CopyConstruct()` 等全部状态读写路径。 2. 恢复 UEAS2 的 `CalculateOptionalSize(asCObjectType*)`，并在 `Bind_TOptional` 注册 `TOptional<class T>` 后重新调用 `FAngelscriptEngine::Get().Engine->TemplateSizeCalculatorFunctions.Add(...)`；对 intrusive inner type 令模板实例大小等于 inner type 实际大小，而不是固定 `Align(InnerSize + 1, Alignment)`。 3. 保留 current 纯脚本 happy path 行为，但把 `FOptionalOperations::IsSet()`、`GetIsSetPtr()`、`GetValuePtr()` 改成按 `bIsIntrusive` 分流，避免继续把 `TOptional<FName>` 当成尾部带 bool 的对象。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` 增加 native interop 回归：脚本读写 `UAngelscriptNativeScriptTestObject.OptionalName` 与 `OptionalCount`，断言 `OptionalName.Reset()` / `Set(FName("Alpha"))` 能被 native 侧按 `TOptional<FName>` 正确观察；同时保留纯脚本 `TOptional<int>` / `TOptional<FName>` 行为断言，确保恢复 intrusive 分支不会破坏当前脚本内语义。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` 补一条 `TOptional<FName>` 大小与 compare 回归，确保 `OptionalUsage.GetValueSize()` 与引擎 `FOptionalPropertyLayout::CalcSize()` 的 intrusive/non-intrusive 结果一致，而不是继续一律走外置 bool 大小。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.h`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h` |
| 预估工作量 | L |
| 风险 | `TOptional` 是模板基础设施，改动会波及 GC schema、属性匹配、容器比较和 debugger 路径；如果 intrusive/non-intrusive 分支恢复不完整，容易出现“纯脚本好了、native property 仍错”的半修状态。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 container binding 测试。 2. 运行新的 native `OptionalName` round-trip 回归，确认脚本与 native `TOptional<FName>` 状态一致。 3. 复跑现有 `TOptional<int>` / `TOptional<FName>` happy path，确认纯脚本行为不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-78 | Defect | 立即排到 native interop 队列前面，先修复 `TOptional` 与 `FOptionalProperty` 的布局/状态机分叉 |

---

## 发现与方案 (2026-04-09 01:11)

### Issue-79：`Bind_Subsystems.cpp` 没有恢复 `NoBlueprintsOfChildren` metadata，编辑器仍暴露一条已知不受支持的 subsystem authoring 路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Subsystems.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 行号 | 24-122；125-128；6-7；6-7；7-8；102-107，192-197，239-244 |
| 问题 | current `Bind_Subsystems.cpp` 在 `24-122` 只注册各类 `ClassName::Get()` accessor，文件结尾没有 UEAS2 在 `125-128` 已恢复的 `USubsystem::StaticClass()->SetMetaData(TEXT("NoBlueprintsOfChildren"), TEXT(""))`。与此同时，当前三个脚本 subsystem 基类仍然全部声明为 `UCLASS(Blueprintable, Abstract)`，而现有测试又在 `AngelscriptSubsystemScenarioTests.cpp:102-107`、`192-197`、`239-244` 明确断言 world/game-instance subsystem 的 script generation 目前仍应编译失败。结果是编辑器 authoring surface 继续向用户暴露“可以尝试创建 subsystem child”的入口，但插件自己的测试基线已经说明这条能力在当前分支并不成立。 |
| 根因 | `Bind_Subsystems.cpp` 在迁移 subsystem bind 时只保留了 `Get()` 注册逻辑，没有把 UEAS2 已经补上的 editor metadata 护栏一并带回；同时 base class 仍维持 `Blueprintable`，进一步放大了 authoring surface 与真实能力边界的错位。 |
| 影响 | 用户可以在编辑器里继续看到并尝试走 subsystem blueprint/script authoring 流程，随后只在编译阶段得到“当前分支不支持”的失败结果。这个失败不是由 bind 层提前阻断，而是把无效入口一直暴露到内容生产阶段，增加无效资产、误导 onboarding 和回归噪音。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 subsystem authoring 的 editor-side metadata 护栏，让编辑器入口与当前分支真实支持矩阵重新一致。 |
| 具体步骤 | 1. 在 `Bind_Subsystems.cpp` 参照 UEAS2 于 bind 注册结束后补回 `#if WITH_EDITOR` 块，执行 `USubsystem::StaticClass()->SetMetaData(TEXT("NoBlueprintsOfChildren"), TEXT(""))`，不要把这条逻辑拆到各个 subsystem shard，避免未来再次遗漏。 2. 保留当前 `UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem`、`UScriptWorldSubsystem` 的 `Blueprintable` 声明不动，先用 metadata 在 editor 入口层面收口，而不是直接改类声明破坏已有反射与脚本生成路径。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/` 新增或扩展一条 editor regression，直接断言 `USubsystem::StaticClass()->HasMetaData(TEXT("NoBlueprintsOfChildren"))` 为 true；若测试基建允许进一步访问 class viewer / blueprint factory，再补一条 smoke，确认 subsystem child 不再出现在可创建蓝图类型列表中。 4. 在 `AngelscriptSubsystemScenarioTests.cpp` 现有“subsystem script generation remains unsupported”断言旁增加注释或 companion test，明确这是“能力暂未开放且 editor 入口已被屏蔽”的基线，而不是用户应继续尝试的正常 workflow。 5. 若后续计划真正开放 subsystem script generation，再单独开新 issue 撤销 metadata 并补完整能力闭环，不要把“恢复支持”与“修复当前错误入口”混在同一提交里。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` 或新的 subsystem editor 回归文件 |
| 预估工作量 | S |
| 风险 | 恢复 metadata 后，依赖当前编辑器入口手工尝试 subsystem 资产的内部调试流程会被收紧；但这正是把“已知不支持”的路径显式关掉，而不是功能回退。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与相关 subsystem 测试。 2. 运行 editor regression，确认 `USubsystem` 基类带有 `NoBlueprintsOfChildren` metadata。 3. 复跑 `AngelscriptSubsystemScenarioTests`，确认现有“不支持 subsystem script generation”的失败基线保持不变，同时编辑器侧不再继续暴露该入口。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-79 | Architecture | 在 subsystem 生命周期 guard 之后处理，先把无效 editor authoring 入口收口 |

---

## 发现与方案 (2026-04-09 01:13)

### Issue-80：补充 `BindSystem_Analysis.md` 发现 55 的实施方案，恢复 `FText` locale-aware compare/case surface 与测试闭环

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FText.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FText.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/`<br>`Documents/AutoPlans/BindSystem_Analysis.md` |
| 行号 | 61-113；59-111；`rg -n "ETextComparisonLevel|CompareTo\\(|CompareToCaseIgnored\\(|EqualTo\\(|EqualToCaseIgnored\\(|ToLower\\(|ToUpper\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中；707-717 |
| 问题 | 这里不重复 Analysis 55 已记录的缺口事实。当前需要解决的是：`Bind_FText.cpp` 仍未把 `ETextComparisonLevel`、`CompareTo(...)`、`CompareToCaseIgnored(...)`、`EqualTo(...)`、`EqualToCaseIgnored(...)`、`ToLower()`、`ToUpper()` 接回脚本 surface，而测试目录对这整组 API 也完全没有守护。 |
| 根因 | `FText` bind 只保留了 identity/basic state surface，但没有继续把 Analysis 55 对应的 locale-aware compare/case API 转化为可执行修复任务和回归测试。 |
| 影响 | 这组 API 缺口会持续停留在“分析文档已知、代码仍未修、测试也不会报警”的状态；后续即使有人继续修改 `Bind_FText.cpp`，也没有自动化能阻止 compare/case surface 进一步漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 直接按 UEAS2 恢复 `FText` compare/case 绑定面，并新增最小而完整的 locale-aware 行为回归。 |
| 具体步骤 | 1. 在 `Bind_FText.cpp:61-72` 后补回 `ETextComparisonLevel` 枚举注册，至少覆盖 `Default`、`Primary`、`Secondary`、`Tertiary`、`Quaternary`、`Quinary` 六个枚举值，命名与 UEAS2 保持一致。 2. 在 `FText_` 的 method 注册段补回六个成员方法：`CompareTo(const FText& Other, const ETextComparisonLevel ComparisonLevel = ETextComparisonLevel::Default)`、`CompareToCaseIgnored(const FText& Other)`、`EqualTo(const FText& Other, const ETextComparisonLevel ComparisonLevel = ETextComparisonLevel::Default)`、`EqualToCaseIgnored(const FText& Other)`、`ToLower()`、`ToUpper()`；优先继续使用 `METHOD_TRIVIAL`，不要额外引入 wrapper 层。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新建 `AngelscriptTextBindingsTests.cpp`，增加 compile-surface 断言，确保脚本可解析 `ETextComparisonLevel` 及上述六个方法签名。 4. 在同一测试文件增加 runtime 回归：使用 `FText::FromString("Alpha")` 与 `FText::FromString("alpha")`，断言 `EqualToCaseIgnored()` 为 true、`CompareToCaseIgnored()` 返回 0、`ToLower().ToString()` 为 `"alpha"`、`ToUpper().ToString()` 为 `"ALPHA"`；再补一条 `EqualTo(..., ETextComparisonLevel::Primary)` 的 smoke，确保枚举参数通路可执行。 5. 保留当前 `IdenticalTo()` 路径不变，并在测试里增加一条对照，明确 `IdenticalTo()` 与 locale-aware compare/case API 是两套不同语义，避免后续维护时把其中一套删回去。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FText.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTextBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | `FText` 大小写和比较结果受 locale 影响，测试若依赖特定文化环境可能出现脆弱断言；应优先选择大小写明显且在默认英文环境稳定的样本，并把重点放在 API 可用性与基本语义，而不是过度锁死所有地区化排序细节。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增 text binding 测试。 2. 运行 compile-surface 回归，确认脚本能解析 `ETextComparisonLevel` 和六个 `FText` compare/case 方法。 3. 运行 runtime 回归，确认 `EqualToCaseIgnored`、`CompareToCaseIgnored`、`ToLower`、`ToUpper` 与枚举比较通路都返回稳定结果。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-80 | Architecture | 在 `FText` 相关 bind 改动窗口内处理，先补 API 再补回归测试 |

---

## 发现与方案 (2026-04-09 01:14)

### Issue-81：补充 `BindSystem_Analysis.md` 发现 44 的实施方案，恢复 `FCollisionObjectQueryParams(TArray<EObjectTypeQuery>)` 构造器

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCollisionQueryParams.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FCollisionQueryParams.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`<br>`Documents/AutoPlans/BindSystem_Analysis.md` |
| 行号 | 357-386；354-395；278-299；563-573 |
| 问题 | 这里承接 Analysis 44，不重复展开全部背景。当前 `Bind_FCollisionQueryParams.cpp` 只公开默认构造、`ECollisionChannel`、`ECollisionObjectQueryInitType`、`int32` bitfield 构造器；UEAS2 在 `358-368` 还额外提供了 `void f(const TArray<EObjectTypeQuery>& ObjectTypes)`，并把脚本数组转换成 `TArray<TEnumAsByte<EObjectTypeQuery>>` 后构造 native `FCollisionObjectQueryParams`。现有 parity 测试 `AngelscriptEngineParityTests.cpp:278-299` 只验证默认构造出的 `ObjectQueryParams` 能参与 `SweepSingleByObjectType` / `ComponentOverlapMulti`，没有任何用例覆盖对象类型数组构造路径。 |
| 根因 | 发现 44 已确认缺口存在，但 `Plan` 尚未把它转化成具体修复与验证步骤；因此 object-type array 构造器仍停留在“已知缺失、无人执行”的状态。 |
| 影响 | 从 Blueprint 或 UEAS2 迁移过来的 object-type trace 代码，仍然必须退回 bitfield 或逐个 `AddObjectTypesToQuery(ECollisionChannel)` 的绕路写法；而自动化也无法在该构造器再次缺失时报警。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 按 UEAS2 原样补回数组构造器，并用 compile/runtime 双层回归锁住 object-type query 的高层输入形态。 |
| 具体步骤 | 1. 在 `Bind_FCollisionQueryParams.cpp:357-371` 之前补回 `FCollisionObjectQueryParams_.Constructor("void f(const TArray<EObjectTypeQuery>& ObjectTypes)", ...)`，lambda 内先构造 `TArray<TEnumAsByte<EObjectTypeQuery>> EnumAsByteObjectTypes`，逐项复制 `ObjectTypes`，再执行 `new(Address) FCollisionObjectQueryParams(EnumAsByteObjectTypes);`。 2. 保持新构造器位于 `ECollisionObjectQueryInitType` 和 `int32 InObjectTypesToQuery` 之前，和 UEAS2 顺序一致，降低未来 diff/merge 噪音。 3. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` 的 world collision parity 片段：新增 `TArray<EObjectTypeQuery> ObjectTypes; ObjectTypes.Add(EObjectTypeQuery::WorldStatic); ObjectTypes.Add(EObjectTypeQuery::WorldDynamic); FCollisionObjectQueryParams ObjectQueryParamsFromArray(ObjectTypes);`，并用它参与至少一条 `System::SweepSingleByObjectType(...)` 或 `System::ComponentOverlapMulti(...)` 调用，确认脚本 surface 可解析该构造器。 4. 再补一条 focused runtime/compile 对照：脚本里同时构造 `FCollisionObjectQueryParams ManualParams; ManualParams.AddObjectTypesToQuery(ECollisionChannel::WorldStatic); ManualParams.AddObjectTypesToQuery(ECollisionChannel::WorldDynamic);` 与 `FCollisionObjectQueryParams ArrayParams(ObjectTypes);`，断言两者 `GetQueryBitfield()` 一致，确保数组构造器不是“只是能编译”，而是语义上等价于现有逐个添加路径。 5. 如测试框架对 `EObjectTypeQuery` 枚举名存在项目差异风险，优先选择 `WorldStatic` / `WorldDynamic` 这类 current 与 UEAS2 都稳定暴露的标准对象类型，避免把回归建立在自定义 channel 配置上。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCollisionQueryParams.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果后续 `EObjectTypeQuery` 枚举源仍存在 `Issue-68` 的配置映射问题，数组构造器修复后可能会暴露出“构造器正确、枚举本身错误”的第二层问题；因此验证时应把构造器回归和枚举 parity 回归分开看。 |
| 前置依赖 | 无；但若 `Issue-68` 的 collision enum 修复同时进行，可共用同一批 world-collision parity 测试数据 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 `AngelscriptEngineParityTests`。 2. 运行新增 parity 片段，确认 `FCollisionObjectQueryParams(ObjectTypes)` 可在脚本中成功编译。 3. 运行 bitfield 对照断言，确认数组构造器与逐个 `AddObjectTypesToQuery(...)` 的结果一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-81 | Architecture | 在 world collision / collision enum 修复窗口一并处理，先补构造器再补 parity |

---

## 发现与方案 (2026-04-09 11:27)

### Issue-82：`Math` 命名空间缺少 `int64/uint64` 基础 overload，current 测试源码与实际 bind surface 已直接脱节

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FMath.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp`<br>`Documents/AutoPlans/BindSystem_Analysis.md` |
| 行号 | 369-393；386-418；119-127；447-457 |
| 问题 | 这里承接 `BindSystem_Analysis.md` 的发现 35。current `Bind_FMath.cpp` 在 `369-393` 只公开了 `Abs/Sign/Min/Max/Square` 的 `float64`、`float32`、`int32`，以及 `Min/Max/Square` 的 `uint32` 版本；UEAS2 在同一区段还额外保留了 `Abs(int64)`、`Sign(int64)`、`Min/Max(int64, int64)`、`Min/Max(uint64, uint64)`、`Square(int64)`、`Square(uint64)`。更直接的是，current 自己的 `AngelscriptMathAndPlatformBindingsTests.cpp:119-127` 仍在脚本里调用 `Math::Abs(int64(-7))`、`Math::Sign(int64(-7))`、`Math::Min/Max(int64(...))`、`Math::Square(int64(9))`。 |
| 根因 | `Bind_FMath.cpp` 迁移时只保留了 32-bit 与浮点的基础 overload，遗漏了 UEAS2 已补齐的宽整数 surface；测试源码仍按 UEAS2 契约书写，但没有把这组签名存在性单独锁住。 |
| 影响 | 宽整数数学在 current 上出现最基础的 API 缺口，导致 UEAS2 脚本无法平移，现有测试源码与真实 bind surface 失配；后续若有人只看测试代码或文档，会误判 `Math` 命名空间已经完整支持 64-bit 基础算子。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 按 UEAS2 直接补回 `int64/uint64` 基础 overload，并把现有 smoke 扩成显式的 64-bit parity 回归。 |
| 具体步骤 | 1. 在 `Bind_FMath.cpp:369-393` 对齐 UEAS2 `386-418`，补回 `int64 Abs(int64 Value) no_discard`、`int64 Sign(int64 Value) no_discard`、`int64 Min(int64 A, int64 B) no_discard`、`uint64 Min(uint64 A, uint64 B) no_discard`、`int64 Max(int64 A, int64 B) no_discard`、`uint64 Max(uint64 A, uint64 B) no_discard`、`int64 Square(int64 Value) no_discard`、`uint64 Square(uint64 Value) no_discard`。 2. 继续使用与 UEAS2 一致的 `FUNC_TRIVIAL` / `FUNCPR_TRIVIAL` 模式，不要为这组纯 surface parity 额外引入 wrapper lambda。 3. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` 现有脚本片段：保留 `int64` 断言，同时新增 `Math::Min(uint64(7), uint64(3))`、`Math::Max(uint64(7), uint64(3))`、`Math::Square(uint64(9))` 回归，确认 `uint64` 入口也真正可编译执行。 4. 在同一测试里追加一条 focused compile-surface 检查，例如通过脚本 `auto WideMin = Math::Min(uint64(2), uint64(1));` 和 `auto WideSquare = Math::Square(int64(9));`，确保不是“某个现有脚本恰好未覆盖到”的偶然通过。 5. 完成后在 coverage 对比说明里把这组 64-bit overload 标记为已恢复，避免后续 discovery 再把同一缺口重复登记。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 风险很低，主要是 `Math`/`FMath` 命名空间切换配置下需要确认新 overload 两种 namespace 都可见；测试应在 current 默认配置下先锁住基础行为。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 `AngelscriptMathAndPlatformBindingsTests`。 2. 运行 64-bit math 回归，确认 `int64` 与 `uint64` 的 `Abs/Sign/Min/Max/Square` 都可解析并返回正确值。 3. 复跑现有 `Math` compat smoke，确认新增 overload 不会影响 32-bit / 浮点已有路径。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-82 | Defect | 立即修复，先把 `Math` 64-bit 基础 overload 与现有测试源码重新对齐 |

---

## 发现与方案 (2026-04-09 01:26)

### Issue-83：`UStruct` 哈希能力判定过早依赖 `ICppStructOps`，带 `Hash()` 的 script struct 仍会被 `TSet` / `TMap` 错误拒绝

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UStruct.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`<br>`Documents/AutoPlans/BindSystem_Analysis.md` |
| 行号 | 227-230；91-96；108-113；68-72，108-111；224-236；157-160，249-254；499-507 |
| 问题 | 这里承接 `BindSystem_Analysis.md` 的发现 39。current `FUStructType::CanHashValue()` 在 `Bind_UStruct.cpp:227-230` 只接受 `GetOps(Usage) != nullptr && Ops->HasGetTypeHash()`；而 `Bind_TSet.cpp:91-96` 与 `Bind_TMap.cpp:108-113` 会在建类型时直接要求 key subtype 的 `CanHashValue()` 为真。问题在于 `ASStruct.cpp:68-72,108-111` 仍会在脚本 struct 存在 `uint32 Hash() const` 时把 `HasGetTypeHash` 写回 fake vtable，说明 runtime hash 支撑没被删掉；删掉的是“在 ops 尚未就绪时先承认 script struct 可哈希”的早期 fallback。UEAS2 `Bind_UStruct.cpp:224-236` 仍保留了这层 `Usage.ScriptClass->GetMethodByDecl("uint32 Hash() const")` 检查。现有 `AngelscriptContainerBindingsTests.cpp:157-160,249-254` 只覆盖 `FName` key，没有任何 script struct key 回归。 |
| 根因 | current 把“最终是否具备 `GetTypeHash`”与“在模板/property gate 阶段能否识别脚本 struct 拥有 `Hash()`”混成了一条只看 `ICppStructOps` 的判断，导致 type gate 比 runtime 初始化更早失败。 |
| 影响 | 带 `Hash()` 的 script struct 仍无法通过 `TSet` / `TMap` key 门禁，脚本作者会看到“容器类型不支持”而不是正常编译；这会直接阻断自定义值类型作为集合 key 的常见用法，也让 current 的 type-system 能力落后于 UEAS2。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 UEAS2 的早期 fallback，让 `CanHashValue()` 在 `ICppStructOps` 还没就绪时也能识别 `Hash()` 方法。 |
| 具体步骤 | 1. 在 `Bind_UStruct.cpp` 为 `FUStructType` 提取一个共享 helper，例如 `static bool HasHashMethodOrOps(const FAngelscriptTypeUsage& Usage)`：优先检查 `GetOps(Usage)`，若存在则继续按 `HasGetTypeHash()` 返回；若 `Ops == nullptr`，再回退到 `Usage.ScriptClass->GetMethodByDecl("uint32 Hash() const") != nullptr`。 2. 让 `CanHashValue()` 改用该 helper，并保留当前 `GetHash()` 继续走 `Ops->GetStructTypeHash(Address)` 的最终运行时路径，不要把真正的 hash 计算重新挪回脚本反射层。 3. 在 helper 上加一条短注释，明确它存在的原因是 `TSet/TMap` 的 gate 发生在 fake vtable 完成前，避免后续“清理重复逻辑”时再次删掉这层 fallback。 4. 扩展 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`：新增一个最小脚本 struct，例如 `struct FHashableKey { int32 Value; uint32 Hash() const { return uint32(Value); } bool opEquals(const FHashableKey& Other) const { return Value == Other.Value; } }`，并至少在一条脚本类成员或函数中声明 `TSet<FHashableKey>` 与 `TMap<FHashableKey, int32>`，确保真正经过当前 container key gate。 5. 在同一回归里验证 `Keys.Add(FHashableKey(1))`、`Counts.Add(FHashableKey(1), 7)`、`Contains/Find` 都可正常工作，锁住“能编译”与“运行时哈希确实生效”两层语义。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 若 fallback 只检查方法名而不确认签名，可能把错误签名的 `Hash` 误判成可用；因此 helper 必须严格用 `uint32 Hash() const` 完整声明匹配。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 container binding 测试。 2. 运行新的 script-struct-key 回归，确认 `TSet<FHashableKey>` / `TMap<FHashableKey, int32>` 可成功编译并在运行时正确 `Contains/Find`。 3. 复跑现有 `FName` key 测试，确认恢复 fallback 不会回退 native/hashable struct 的既有路径。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-83 | Defect | 紧随 `Math` 修复之后处理，先恢复 script struct 作为 `TSet` / `TMap` key 的基本能力 |

---

## 发现与方案 (2026-04-09 01:26)

### Issue-84：`struct __StaticType_*` / `TStructType` 链路被拆到半残状态，preprocessor、class generator 与编译器对 struct static-type 的约定已经脱节

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Helpers.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Preprocessor/AngelscriptPreprocessor.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UStruct.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/ClassGenerator/AngelscriptClassGenerator.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`<br>`Documents/AutoPlans/BindSystem_Analysis.md` |
| 行号 | 863-899；1422-1432；150-186；2619-2640，4042-4060，4960-4988；1129-1130；2573-2574；6858-6866；828-841；1540-1595；232-233；4794-4820；453-472；511-519 |
| 问题 | 这里承接 `BindSystem_Analysis.md` 的发现 40。current preprocessor 只在 `Class` / `Interface` chunk 上生成 `__StaticType_*`，`AngelscriptPreprocessor.cpp:863-899` 已经没有 UEAS2 `828-841` 的 `Struct` 分支。与此同时，current `Bind_UStruct.cpp` 在 `1432` 行结束，UEAS2 `1540-1595` 中的 `Bind_TStructType_Declaration`、`Bind_TStructType`、`Bind_StaticStructs` 整组缺席；但 current `Bind_Helpers.h:150-186` 又仍然保留了 `FScriptStructType` 与 `FAngelscriptStructTypeHelpers`。class generator 也只剩 `SetScriptStaticClass()` 与 `TSubclassOf<UObject>` 写回路径（`4960-4988`），没有 UEAS2 头/实现中的 `SetScriptStaticStruct()`（`232-233`，`4794-4820`）。更矛盾的是，第三方编译器代码还在把 `__StaticType_` 当特殊全局处理：`as_builder.cpp:2573-2574` 会把这类变量收进 `allScriptGlobalVariables`，`as_compiler.cpp:6858-6866` 继续沿用相同的 static-type 解析路径。现有测试 `AngelscriptClassBindingsTests.cpp:453-472` 只锁住 `__StaticType_AActor` 的 class happy path，没有任何 struct 对应回归。 |
| 根因 | struct static-type 能力在 current 里被部分裁撤，只删了 preprocessor/bind/class-generator 的产出与回填环节，却没有同步删掉已经存在的 helper 类型与编译器特殊分支，导致系统内部对“struct 是否支持 `__StaticType_*`”没有统一答案。 |
| 影响 | 这条链路现在处于“编译器记得、运行时忘了、helper 还留着”的半残状态。后续一旦重新启用 struct static-type 用法，问题不会表现为单点缺函数，而会同时牵扯生成代码、全局变量回填和模板类型绑定；继续让这种不一致长期存在，也会提高未来 AS 2.38 selective migration 时的结构风险。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 按 UEAS2 恢复 struct static-type 的端到端链路，优先复用 current 已保留的 `FScriptStructType` helper，而不是再造第三套类型。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp` 恢复 UEAS2 的 `else if (Chunk.Type == EChunkType::Struct)` 分支，生成 `const TStructType<FScriptStructWildcard> __StaticType_<StructName>;`，并保留 namespaced 版本的输出形态。 2. 在 `Bind_UStruct.cpp` 重新引入 `Bind_TStructType_Declaration`、`Bind_TStructType` 与 `Bind_StaticStructs`；实现上直接复用 current `Bind_Helpers.h:150-186` 已存在的 `FScriptStructType` / `FAngelscriptStructTypeHelpers`，避免重复定义底层 helper。 3. 在 `AngelscriptClassGenerator.h/.cpp` 补回 `SetScriptStaticStruct(TSharedPtr<FAngelscriptClassDesc>, UScriptStruct*)`，实现直接对齐 UEAS2 `4794-4820` 的“找到 `StaticClassGlobalVariableName` 对应脚本全局并写回 `UScriptStruct*`”流程。 4. 在 class generator 里补回 struct 回填调用点：参考 UEAS2 `2622` 与 `3921`，让 script/native struct 生成完成后都执行 `SetScriptStaticStruct(...)`，而不是只有 `SetScriptStaticClass(...)`。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 或新的 struct-binding 测试文件中新增两类回归：一类验证 native struct，如 `__StaticType_FVector.IsValid()` 且 `__StaticType_FVector.Get()` 返回非空 `UScriptStruct`；另一类编译一个最小 script struct `FStaticTypeProbe`，断言 `__StaticType_FStaticTypeProbe.IsValid()` 为真，确保 preprocessor + class generator 的写回链路都真正生效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Helpers.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 或新的 struct-binding 测试文件 |
| 预估工作量 | L |
| 风险 | 这是一条跨 preprocessor / bind / generator / compiler 约定的链路修复；若只恢复其中一段，容易得到“变量能生成但永远不回填”或“helper 类型存在但 script 看不到变量”的半修状态，因此必须把测试同时覆盖 native struct 与 script struct。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 struct/class binding 测试。 2. 运行 native struct 与 script struct 两条 `__StaticType_*` 回归，确认 `TStructType` 可见、`IsValid()` 为真、`Get()` 返回有效 `UScriptStruct`。 3. 复跑现有 `__StaticType_AActor` 测试，确认恢复 struct 链路不会回退 class static-type 现有行为。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-84 | Architecture | 在 type-system 窗口内尽早处理，避免 struct static-type 继续处于“编译器记得、运行时忘了”的半残状态 |

---

## 发现与方案 (2026-04-09 01:33)

### Issue-85：`GetAllClasses()` 没有过滤已删除的 `UASClass` tombstone，类浏览结果会继续泄露失效脚本类

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 337-354，537-554；4990-5024 |
| 问题 | `Bind_UObject.cpp` 在 `UClass` 命名空间和全局各注册了一份 `GetAllClasses(TArray<UClass>& OutClasses)`，两处实现都只过滤 `CLASS_Deprecated | CLASS_NewerVersionExists`。但 `CleanupRemovedClass()` 会把被移除的 `UASClass` 置成 `ScriptTypePtr = nullptr`、`ConstructFunction = nullptr`、`DefaultsFunction = nullptr`，并仅打上 `CLASS_NotPlaceable | CLASS_HideDropDown | CLASS_Hidden`，不会补 `CLASS_NewerVersionExists`。这意味着删除后的 script class 会从 `FindClass` / `GetAllSubclassesOf` 之外的另一条公开枚举入口重新漏回脚本层。 |
| 根因 | `GetAllClasses()` 复制了最小的 class-flag 过滤逻辑，却没有复用 class generator 已经定义的“失效 script class”判定规则，导致枚举入口和删除生命周期各自维护不同标准。 |
| 影响 | 任何依赖 `GetAllClasses()` 做类浏览、下拉菜单、批量反射或脚本工具索引的逻辑，都会继续看到已经被清理成 tombstone 的 `UASClass`。后续若脚本再对这些类取 `GetDefaultObject()`、枚举函数或实例化，故障点会晚于真正的根因，形成“列表里看得到、实际已经不可用”的静默失配。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 提取统一的 `IsDeletedAngelscriptClass` helper，并让两份 `GetAllClasses()` 与现有 class lookup/filter 共用同一套 tombstone 过滤规则。 |
| 具体步骤 | 1. 在 `Bind_UObject.cpp` 恢复一个文件内私有 helper，例如 `static bool IsDeletedAngelscriptClass(UClass* Class)`，判定条件直接对齐 `CleanupRemovedClass()` 写入的四个信号：`ScriptTypePtr == nullptr`、`ConstructFunction == nullptr`、`DefaultsFunction == nullptr`、且带 `CLASS_Hidden | CLASS_HideDropDown | CLASS_NotPlaceable`。 2. 让 `UClass` 命名空间版本和全局版本的 `GetAllClasses()` 在现有 `CLASS_Deprecated | CLASS_NewerVersionExists` 过滤之后，都额外跳过 `IsDeletedAngelscriptClass(Class)`。 3. 顺手把 `GetAllSubclassesOf()`、两份 `FindClass()`、`__StaticClass()` 也切到同一个 helper，收拢当前分散在多个 lambda 里的 class-lifecycle 过滤逻辑，避免后续再出现“修了子类枚举，漏了全量枚举”的分叉。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` 新增 hot-reload / removed-class 场景：先编译并加载一个最小 script class，记录其名字；再模拟移除该类后调用 `GetAllClasses()` 与 `GetAllSubclassesOf()`，断言两条入口都不再返回 tombstone。 5. 若现有测试框架不便直接走完整删除流程，就在测试夹具里借用 `CleanupRemovedClass()` 生成的状态条件构造一个临时 `UASClass`，明确锁定过滤 predicate，而不是继续依赖人工观察列表结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 把过滤规则统一后，部分工具脚本若曾依赖 `GetAllClasses()` 返回 tombstone 做诊断，会在行为上收紧；需要在测试里同时覆盖“正常类仍可见、删除类被隐藏”两侧，避免误伤有效 class 枚举。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与扩展后的 class binding 测试。 2. 运行 removed-class 回归，确认 `GetAllClasses()`、`GetAllSubclassesOf()`、`FindClass()` 与 `__StaticClass()` 对同一 tombstone 都返回一致的“不可见/不可解析”结果。 3. 复跑现有正常 class lookup 测试，确认 native class 与仍然有效的 script class 不受影响。 |

### Issue-86：`UAssetManager` 命名空间把初始化探测与安全获取入口整段注释掉，脚本无法在 load 前做标准生命周期分支

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 107-110；137-140；`rg -n "UAssetManager::IsInitialized|UAssetManager::Get\\(|IsInitialized\\(|GetIfValid\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | current `Bind_UAssetManager.cpp` 在 `UAssetManager` 命名空间里只留下两行被注释掉的绑定：`// BindGlobalFunction("bool IsInitialized()", FUNC_TRIVIAL(UAssetManager::IsInitialized));` 和 `// BindGlobalFunction("UAssetManager Get()", FUNC_TRIVIAL(UAssetManager::GetIfInitialized));`。UEAS2 对应位置实际公开的是 `bool IsInitialized() no_discard` 与 `UAssetManager Get() no_discard`。也就是说，当前脚本侧已经失去“先判断 asset manager 是否初始化，再安全拿实例”的标准入口，只剩对象实例方法和更脆弱的直接调用路径。 |
| 根因 | `UAssetManager` 绑定迁移时把生命周期探测入口直接注释掉，没有替换成新的 wrapper，也没有在其它命名空间补一组等价 API。 |
| 影响 | 任何需要在引擎初始化早期、commandlet、自动化测试或插件装载边界里访问 asset manager 的脚本，都无法沿用 UEAS2 的安全写法 `if (UAssetManager::IsInitialized()) { auto Manager = UAssetManager::Get(); ... }`。调用方只能假定实例存在，或各自实现一套生命周期判断，导致 bind 层和业务脚本重复承担初始化判断逻辑。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 `UAssetManager` 命名空间的初始化探测入口，并用测试锁住“未初始化返回 null/false、已初始化可安全取实例”的统一契约。 |
| 具体步骤 | 1. 在 `Bind_UAssetManager.cpp:107-110` 取消注释并恢复两条绑定，签名对齐 UEAS2：`bool IsInitialized() no_discard` 绑定 `UAssetManager::IsInitialized`，`UAssetManager Get() no_discard` 绑定 `UAssetManager::GetIfInitialized`。 2. 若 current 分支担心静态初始化顺序，可增加一个薄 wrapper lambda，把 `GetIfInitialized()` 的 `nullptr` 返回原样传给脚本，不要改成 `Get()` 那种潜在 hard failure 语义。 3. 在同文件为这两条入口重新补上 `no_discard`，保证脚本不能静默丢弃初始化判断和返回实例。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增 asset-manager binding 测试，至少覆盖两条分支：未初始化窗口下 `UAssetManager::IsInitialized()` 返回 `false` 且 `UAssetManager::Get()` 返回 `null`；已初始化窗口下两者返回 `true` / 有效实例，并能继续调用现有 `GetPrimaryAssetIdForPath(...)` 这类实例方法。 5. 让新测试复用现有 `FAngelscriptEngine::IsInitialized()` 或测试夹具对编辑器运行态的判断，避免在测试里硬编码引擎生命周期时序。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 若 current 某些脚本已经绕过命名空间 helper 直接缓存 `UAssetManager*`，恢复官方入口本身不会破坏它们，但测试需要明确 `Get()` 的 `null` 行为，避免调用方误以为这是强保证实例存在的 API。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增的 asset-manager binding 测试。 2. 在未初始化与已初始化两种测试窗口下执行 `UAssetManager::IsInitialized()` / `UAssetManager::Get()`，确认返回值与实例可用性一致。 3. 追加一条 smoke，验证 `UAssetManager::Get()` 返回的实例可继续调用 `GetPrimaryAssetIdForPath(...)`，而不是只恢复了表面符号。 |

### Issue-87：`UInputSettings` 的唯一命名/存在性查询 helper 丢掉 `no_discard`，返回值契约已经弱于 UEAS2

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UInputSettings.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 8-20；8-15；`rg -n "GetUniqueActionName|GetUniqueAxisName|DoesActionExist|DoesAxisExist|DoesSpeechExist" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 问题 | UEAS2 把 `GetUniqueActionName(...)`、`GetUniqueAxisName(...)`、`DoesActionExist(...)`、`DoesAxisExist(...)` 都声明成 `no_discard`，明确这些 helper 的唯一意义就是返回一个必须被消费的结果。current 版本把这四条约束全部去掉了；同时 current 新增的 `DoesSpeechExist(...)` 也没有任何返回值契约。 |
| 根因 | `Bind_UInputSettings.cpp` 在迁移/扩展输入设置 surface 时保留了函数本体，却没有同步保留 UEAS2 已建立的 `no_discard` 约束，也没有为新增 speech 查询补上等价契约。 |
| 影响 | 脚本现在可以无声写出 `Settings.GetUniqueActionName("Jump");`、`Settings.DoesActionExist("Sprint");` 这类被丢弃的调用，而编译器不会提示任何问题。对唯一命名 helper 来说，这会把本应接收并继续使用的新名字直接吞掉；对存在性查询来说，则会把配置探测写成无副作用的空语句，增加输入映射误判和死条件分支的排查成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 `UInputSettings` 查询 helper 的 `no_discard` 契约，并给 current 新增的 speech 查询补齐同级约束。 |
| 具体步骤 | 1. 将 `Bind_UInputSettings.cpp:8-9` 的 `GetUniqueActionName(...)`、`GetUniqueAxisName(...)` 恢复为 `... no_discard`。 2. 将 `Bind_UInputSettings.cpp:17-20` 的 `DoesActionExist(...)`、`DoesAxisExist(...)`、`DoesSpeechExist(...)` 全部改成 `... no_discard`，保持同一组“返回 bool 表示是否存在”的 helper 契约一致。 3. 若团队决定 `GetSpeechMappings()` / `DoesSpeechExist()` 作为 current 新增 surface 继续保留，就在同文件注释里说明它们虽然不在 UEAS2 对照集里，但仍遵循同一返回值规则，避免下次同步时再次遗漏。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 新增一个 focused compile test，分别写出“丢弃返回值”的脚本片段和“正确消费返回值”的脚本片段，断言前者编译失败、后者通过。 5. 用同一测试顺带锁住 `DoesSpeechExist(...)`，确保 current 自己新增的 speech 查询不会继续成为契约例外。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputSettingsBindingsTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果现有脚本已经大量丢弃这些 helper 的返回值，恢复 `no_discard` 后会在编译期集中暴露误用；这是预期中的契约收紧，需要通过专项 compile test 把变化锁成有意行为。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 编译 `AngelscriptRuntime` 与新增的 input-settings binding 测试。 2. 运行 compile-only 回归，确认丢弃 `GetUniqueActionName` / `DoesActionExist` / `DoesSpeechExist` 返回值会报编译错误。 3. 再运行消费返回值的正例脚本，确认恢复 `no_discard` 不会影响正常输入配置查询。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-85 | Defect | 与现有 class lookup / tombstone 修复同窗执行，先统一 `GetAllClasses()` 的失效类过滤 |
| P2 | Issue-86 | Architecture | 在 asset loading 接口整理时尽快补回，先恢复 `UAssetManager` 生命周期探测入口 |
| P2 | Issue-87 | Defect | 紧随输入配置 surface 清理窗口处理，用 compile test 锁住 `no_discard` 契约 |
