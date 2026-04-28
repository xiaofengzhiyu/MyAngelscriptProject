# LanguageFeatures 测试覆盖缺口分析

---

## 测试审查 (2026-04-08 13:09)

### 一、现有测试问题

#### Issue-1：`NeverVisited` 用例只验证“可编译”而没有验证控制流分析结果

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.NeverVisited` |
| 行号范围 | 98-115 |
| 问题描述 | 用例只调用 `ASTEST_BUILD_MODULE` 和 `GetFunctionByDecl`，最后执行 `TestTrue(..., true)`。这只证明脚本入口存在，没有验证 `if (bCondition) { return; } int Value = 42;` 这段“可能永远不执行”的语句块是否被 parser / builder 正确标记，也没有检查是否产生预期 warning / optimization 结果。 |
| 影响 | 该用例即使在控制流分析退化、NeverVisited 标记失效或诊断静默丢失时仍会绿灯，无法拦截语言层回归。 |
| 修复建议 | 改为显式验证诊断或执行结果：1）像同文件 `ContainsWarningDiagnostic` 一样增加 helper，检查 diagnostics 中是否存在与 unreachable / never visited 对应的 warning；2）如果该场景设计为“允许编译但保留块信息”，则进一步执行一个带 side effect 的版本，例如在未命中早退分支时返回特定值，分别断言 `bCondition=true/false` 的结果；3）删除 `TestTrue(..., true)` 这类空断言。 |

#### Issue-2：`Constructor` 用例只验证模块可编译，没有验证构造函数语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Functions.Constructor` |
| 行号范围 | 120-136 |
| 问题描述 | 用例只调用 `BuildModule` 编译含两个构造函数的 `ConstructorCarrier`，之后没有 `GetFunctionByDecl`、没有执行 `Run()`、也没有读取 `DefaultCarrier.Value`。如果默认构造函数没有被调用、字段初始化失效或重载构造分派错误，只要模块还能通过编译，该测试仍会返回 `true`。 |
| 影响 | 构造函数解析、默认构造路径和对象初始化语义发生回归时，这个用例无法报错，等于把“对象构造”降级成了单纯的编译冒烟测试。 |
| 修复建议 | 保持该文件职责不变，直接把用例改成执行型断言：1）使用 `ASTEST_COMPILE_RUN_INT` 执行 `Run()` 并断言默认构造返回 `42`；2）补一个额外入口例如 `int RunExplicit() { ConstructorCarrier ExplicitCarrier(7); return ExplicitCarrier.Value; }`，再断言重载构造也能返回 `7`；3）如果不想在同一用例内覆盖两条路径，至少把显式构造拆成同文件的独立测试。 |

#### Issue-3：`InterfaceAdvancedTests.cpp` 单文件 803 行，已经超出项目测试文件上限

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.InheritedInterface` 等 9 个用例 |
| 行号范围 | 1-803 |
| 问题描述 | 该文件同时承载 inherited interface、missing method、GC、hot reload、multiple inheritance、dispatch 等 9 个场景，总长度达到 803 行，显著超过规则要求的“单文件 300-500 行、单文件单职责”。文件内部还重复出现 `CompileScriptModule`、`FActorTestSpawner`、`ResetSharedCloneEngine` 等 setup/teardown 模式，阅读和维护成本已经很高。 |
| 影响 | 场景耦合过高会放大 merge 冲突与 review 成本，也容易让后续补测试继续堆在同一文件中，进一步恶化可读性并掩盖真正的断言问题。 |
| 修复建议 | 按职责拆分为至少 3 个文件：1）`AngelscriptInterfaceHierarchyTests.cpp` 负责 inherited/multiple inheritance；2）`AngelscriptInterfaceLifecycleTests.cpp` 负责 GC/hot reload；3）`AngelscriptInterfaceValidationTests.cpp` 负责 missing method/no property。共享的 compile/spawn/reset 逻辑抽到 `Shared/` helper，保持每个文件控制在 300-500 行。 |

#### Issue-4：`HotReload` 用例没有验证接口热重载后的行为切换

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.HotReload` |
| 行号范围 | 316-414 |
| 问题描述 | 脚本 V1 和 V2 的唯一区别是 `TakeDamage` 从 `DamageReceived = Amount` 变成 `DamageReceived = Amount * 2.0`，但测试只断言 `ClassV2->ImplementsInterface(InterfaceV2)`，没有生成实例、没有调用接口方法、也没有读取 `DamageReceived`。因此即使热重载后仍然执行旧函数体，只要接口元数据没丢，这个用例就会通过。 |
| 影响 | 该测试无法覆盖 P10 `UInterface` 主线最关键的“热重载后 dispatch 指向新实现”语义，容易漏掉 class reload 成功但 method thunk 未更新的回归。 |
| 修复建议 | 在同一用例内补行为断言：1）V1 编译后 spawn actor，调用 `TakeDamage(3.0)` 并断言属性为 `3.0`；2）FullReload 到 V2 后重新获取类并 spawn 新实例，再调用同一接口方法并断言属性变为 `6.0`；3）如果引擎支持旧实例迁移，还应追加对 reload 前实例的调用，验证 thunk/dispatch 是否也切换到新实现。 |

#### Issue-5：`StructCppOpsTests` 唯一用例没有覆盖任何 `CppOps` 行为

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptStructCppOpsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.StructCppOps.NotBlueprintTypeByDefault` |
| 行号范围 | 33-64 |
| 问题描述 | 文件名和测试模块名都指向 `StructCppOps`，但唯一断言只有 `Struct->GetBoolMetaData("BlueprintType") == false`。它没有检查 struct 的构造、复制、析构、默认值初始化、`TCppStructOps` 注册状态或任何与 C++ ops 相关的行为。 |
| 影响 | 该文件会给人“StructCppOps 已有内部单测”的错误印象，实际上真正高风险的 native layout / ctor / copy / dtor 路径完全没被覆盖。 |
| 修复建议 | 保留当前 metadata 断言的同时，把文件改成真正的 `StructCppOps` 套件：1）增加 helper 读取 `UScriptStruct::GetCppStructOps()` 并断言存在；2）通过 `InitializeStruct` / `CopyScriptStruct` / `DestroyStruct` 验证默认值、复制结果和析构路径；3）若这些断言会让文件超过 500 行，则把 metadata 检查移到单独的 `StructMetadataTests.cpp`。 |

#### Issue-6：`Memory.Construction` 是空断言测试

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptMemoryTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Memory.Construction` |
| 行号范围 | 51-56 |
| 问题描述 | 用例仅构造 `asCMemoryMgr Manager;`，随后执行 `TestTrue("Constructing the internal memory manager should succeed", true)`。这不是对内存管理器状态的验证，只是对常量 `true` 做断言。 |
| 影响 | 即使构造函数没有初始化池、默认容量异常或内部计数状态被破坏，该测试也始终绿灯，无法对 memory manager 初始化回归提供任何保护。 |
| 修复建议 | 把空断言替换成可观察状态验证：1）像同文件 `FMemoryManagerProbe` 一样读取 `scriptNodePool` / `byteInstructionPool` 长度并断言初始为 `0`；2）如果构造函数应设置额外字段，补充对这些字段的断言；3）删除 `TestTrue(..., true)`。 |

#### Issue-7：`Restore` round-trip 用例没有验证恢复后的模块可执行

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Restore.RoundTrip`、`Angelscript.TestModule.Internals.Restore.StripDebugInfoRoundTrip` |
| 行号范围 | 118-243 |
| 问题描述 | 两个 round-trip 用例都只验证 `SaveByteCode` / `LoadByteCode` 返回 `asSUCCESS`，再检查 `bWasDebugInfoStripped` 标志。`Restore.RoundTrip` 虽然在序列化前执行了源模块，但加载后的 `RestoredModule` 从未再执行；`StripDebugInfoRoundTrip` 更是连源模块执行前后的行为对比都没有。 |
| 影响 | bytecode 反序列化即使成功返回，但函数表、全局常量重建、跳转重定位或运行时执行路径损坏，这两个用例也不会失败，导致 restore 子系统最关键的“可恢复可运行”语义处于未验证状态。 |
| 修复建议 | 在两个用例里都追加恢复后执行断言：1）`LoadByteCode` 成功后调用 `ExecuteRestoreFunction`，断言 `Test()` 仍返回 `42`；2）对 strip-debug 版本额外验证“去掉 debug info 不影响执行结果”；3）如果 restore 会重建全局常量或函数元数据，可顺手验证 `GetFunctionByDecl("int Test()")` 和相关 global 仍可访问。 |

#### Issue-8：`ScriptNode.Types` 用魔法数字校验内部枚举值，价值低且脆弱

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptScriptNodeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.ScriptNode.Types` |
| 行号范围 | 49-55 |
| 问题描述 | 用例断言 `snScript == 1`，其余 `snFunction` / `snClass` / `snExpression` 仅检查“大于 0”。这类硬编码枚举值没有直接业务语义，而且在 upstream enum 重排但 parser 行为不变时会造成无意义失败；反过来，如果节点类型映射错了但值仍然大于 0，测试也抓不出来。 |
| 影响 | 该用例既脆弱又不敏感，容易把内部实现细节变化当成回归，同时漏掉真正的 AST 节点构造错误。 |
| 修复建议 | 改成基于行为的断言：1）复用 parser 产出真实 AST，验证根节点 / 子节点的 `nodeType` 与语法输入匹配；2）如果必须覆盖 enum 稳定性，只保留对公开约定常量的最小断言，并去掉 `> 0` 这种低信号检查；3）将类型枚举覆盖并入同文件的 `Traversal` / `Copy` 场景，避免单独维护魔法数字测试。 |

#### Issue-9：`Handles.Implicit` 只验证编译与符号注册，没有任何运行时断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Handles.Implicit` |
| 行号范围 | 53-82 |
| 问题描述 | 用例先编译 `HandleImplicitObject` 并确认 `int Test()` 能被找到，随后直接执行 `TestTrue(..., true)`，注释说明“由于当前分支 runtime 会 fault，只验证 compile 和 symbol registration”。这意味着真正的隐式 handle/对象参数传递语义完全没被断言。 |
| 影响 | 参数按值/按引用传递、对象写回、handle 生命周期等运行时回归都无法被该测试拦截，`Handles.Implicit` 名称会显著高估实际覆盖。 |
| 修复建议 | 最低限度应把该用例拆成两层：1）保留现有 compile-only 冒烟，但重命名为 `CompileOnly` 并在报告中降级权重；2）新增真正的运行时用例，在 runtime fault 修复后执行 `Test()` 并断言 `ValueHolder.Value == 42`；3）若当前分支必须跳过运行时路径，使用明确的 `AddExpectedError` / `return false` 机制标记 blocker，而不是 `TestTrue(..., true)`。 |

#### Issue-10：`Inheritance` 套件中 `Basic`/`VirtualMethod` 都退化成 compile-only 测试

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Inheritance.Basic`、`Angelscript.TestModule.Angelscript.Inheritance.VirtualMethod` |
| 行号范围 | 22-50, 89-117 |
| 问题描述 | 两个用例都先编译模块并解析 `int Test()`，但结尾都是 `TestTrue(..., true)`，注释分别写着“执行继承脚本类仍会 fault”与“继承 dispatch 仍会 fault”。因此它们没有验证基类字段继承、override 分派或 `Derived` 实际返回值，只是确认符号能生成。 |
| 影响 | 继承层最核心的 layout 与 virtual dispatch 回归不会被捕获，尤其 `VirtualMethod` 完全没有证明 override 结果会从 `1` 切到 `2`。 |
| 修复建议 | 1）把 compile-only 冒烟从语义测试中拆走，避免继续用 `Inheritance.Basic` / `VirtualMethod` 名字暗示行为已验证；2）runtime 路径恢复后，分别执行 `Test()` 并断言 `30` 与 `2`；3）如果短期内确实无法执行，至少补充反射级断言，例如验证派生类方法表中存在 override，并在文档中把 runtime blocker 显式列为待补覆盖。 |

#### Issue-11：`ObjectModel` 套件里多个用例没有验证对象运行时语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Objects.Basic`、`Angelscript.TestModule.Angelscript.Objects.Composition`、`Angelscript.TestModule.Angelscript.Objects.Singleton` |
| 行号范围 | 66-146 |
| 问题描述 | `Objects.Basic` 和 `Objects.Composition` 都只验证 compile + `GetFunctionByDecl`，随后以 `TestTrue(..., true)` 结束；`Objects.Singleton` 更是直接只有一句“known unsupported branch constraint”的 `TestTrue(true)`。这些用例没有执行对象方法、没有检查嵌套对象成员写入，也没有为 singleton 失败路径提供任何编译错误/诊断断言。 |
| 影响 | 对象模型测试表面上有 6 个用例，实际上 script object method dispatch、组合成员访问和 singleton 约束都缺少可执行证据，覆盖率被明显高估。 |
| 修复建议 | 1）`Basic`/`Composition` 在 runtime 可用后执行 `Run()`，分别断言 `42`；2）`Singleton` 如果当前分支确实不支持，应像其他 negative test 一样通过 `CompileModuleWithResult` 明确断言 `ECompileResult::Error` 与预期诊断，而不是空通过；3）将 compile-only blocker 独立成单独测试名，避免继续占用行为测试名。 |

#### Issue-12：`Operator` 套件中 3 个用例只做编译冒烟

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Operators.Overload`、`Angelscript.TestModule.Angelscript.Operators.GetSet`、`Angelscript.TestModule.Angelscript.Operators.Const` |
| 行号范围 | 20-100 |
| 问题描述 | `Overload` 只确认模块能编译；`GetSet` 只确认 raw module `Build()` 成功；`Const` 同样只编译一个含 `const` 方法的脚本类。三个用例都以 `TestTrue(..., true)` 收尾，没有执行 `A + B`、property accessor 读写或 `const` 方法调用。 |
| 影响 | 运算符重载、property accessor、`const` 成员函数这些语言特性最容易在 codegen / dispatch 层回归，但当前套件对行为层几乎没有保护。 |
| 修复建议 | 1）保留编译冒烟时，重命名为 `CompileOnly`；2）运行时路径恢复后分别断言 `A+B` 结果、`Instance.Value` getter/setter 效果和 `GetValue()` 返回值；3）`GetSet` 至少在 raw path 上额外断言 `Module->GetFunctionByDecl("int Test()")` 可执行，而不是停在 `Build()`。 |

#### Issue-13：`NativeScriptHotReload` 只验证“可 reload”，没有验证 reload 后行为变化

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A`、`Phase2B`、`Phase2C` |
| 行号范围 | 14-61, 80-256 |
| 问题描述 | 公共 helper `VerifyNativeScriptHotReloadInline` 的 reload 变更只是给原脚本追加一行注释 `// hot reload verification marker`，然后断言 `CompileModuleWithResult(... SoftReloadOnly ...)` 成功且结果为 handled。测试既没有改变任何 API/属性/函数体，也没有在 reload 前后实例化对象或执行方法。 |
| 影响 | 这组用例最多只能说明“编译包装器接受一次无语义变化的 soft reload”，无法覆盖 native script hot reload 最关键的类重建、属性保留、函数体切换或旧实例兼容性。 |
| 修复建议 | 对每个 phase 至少引入一个真实语义变化并验证前后差异：1）修改函数返回值或属性默认值，而不是仅追加注释；2）reload 前后都实例化对应 class 并执行方法，断言行为从旧值切换到新值；3）若关注 ABI/metadata，额外断言 `UClass`、`UFunction` 或 `UProperty` 的存在与更新结果。 |

#### Issue-14：`Int64AndTypedef` 用例标题包含 `Typedef`，但脚本完全没有覆盖 typedef

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.Int64AndTypedef` |
| 行号范围 | 82-95 |
| 问题描述 | 用例脚本只有 `int64 Value = 1; Value <<= 40; Value += 7; return Value;`，没有任何 `typedef`/type alias 声明，也没有 alias 的解析、赋值或转换。测试名宣称覆盖 `Int64AndTypedef`，实际只验证了宽整数移位。 |
| 影响 | typedef 解析如果发生回归，这个测试不会失败；测试名还会误导后续审查，认为 alias 特性已经被覆盖。 |
| 修复建议 | 二选一：1）若继续保留现有脚本，把测试重命名为 `Int64Arithmetic`；2）更推荐补足真实 typedef 场景，例如 `typedef int64 Score; Score Value = 1; ...`，然后同时断言 alias 变量可参与运算与返回。 |

#### Issue-15：`PrimitiveAndEnum` 用例把多个语言特性压缩成一个结果值，定位能力弱

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.PrimitiveAndEnum` |
| 行号范围 | 61-75 |
| 问题描述 | 脚本把 `bool`、`float` 加法、`int(Value)` 转换和 `enum` 常量一起折叠到单个表达式 `(bFlag ? 1 : 0) + int(Value) + int(EState::Running)`，最终只断言结果为 `9`。这种组合断言无法区分到底是布尔逻辑、浮点处理还是枚举值出了问题。 |
| 影响 | 当多个子特性之一回归时，失败定位非常模糊；更糟的是如果两个错误相互抵消，测试甚至可能继续通过。 |
| 修复建议 | 将该用例拆成更精确的断言或子测试：1）单独断言 `bool` 分支结果；2）单独断言浮点运算与显式转换；3）单独断言枚举常量值；4）如果保留组合脚本，也要通过多个返回位或多个 helper 函数分别验证每一项，而不是只看汇总和。 |

#### Issue-16：`DeclareInheritance` 没有验证接口继承关系本身

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceDeclareTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.DeclareInheritance` |
| 行号范围 | 58-97 |
| 问题描述 | 用例编译了 `UIKillableInh : UIDamageableInh`，但断言只有“`ChildInterface` 非空”和“带 `CLASS_Interface` 标记”。它没有检查 `ChildInterface->GetSuperClass()` / `ImplementsInterface(ParentInterface)` / 父接口方法是否出现在子接口反射信息里。 |
| 影响 | 如果接口声明语法还能通过编译，但 parent link 丢失、反射层未建立继承关系，测试仍然会绿灯，无法保护 `UInterface` 继承主线。 |
| 修复建议 | 在现有用例中补父接口级断言：1）查找 `UIDamageableInh` 并断言非空；2）断言 `UIKillableInh` 与父接口存在继承/实现关系；3）验证子接口可见父接口方法 `TakeDamage`，确保不只是“生成了一个独立接口类”。 |

### 二、需要新增的测试

#### NewTest-109：补 `Phase2B` 的 namespace 函数体切换回归，锁定 soft reload 后调用方是否真正切到新实现

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::PerformHotReload`、`FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | `AngelscriptNativeScriptHotReloadTests.cpp` 目前只验证 `CompileModuleWithResult(... SoftReloadOnly ...)` 返回 handled；即便 `UNativeHotReloadPhase2BMathCarrier::ComputeSquare()` 在 reload 后仍绑定旧的 `NativeHotReloadMath::Square` 实现，现有 `Phase2B` 也不会失败。 |
| 风险评估 | 如果 soft reload 只更新模块状态却没有把 namespace free function 的新函数体重新绑定到调用方，脚本类方法会继续执行旧代码；当前 compile-smoke 测试完全抓不到这类“reload 成功但行为没切换”的回归。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2B.NamespaceFunctionBehaviorSwitch` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadBehaviorTests.cpp` |
| 场景描述 | 用 `HotReloadPhase2BMathNamespace.as` 的最小变体做两版脚本：V1 中 `NativeHotReloadMath::Square(int X) { return X * X; }`，V2 改成 `return X * X + 1;`；`UNativeHotReloadPhase2BMathCarrier::ComputeSquare(int X)` 始终只转发到 namespace 函数。先 full reload 编译 V1，创建对象并调用 `ComputeSquare(3)`；再对同模块做 soft reload 到 V2，重新创建对象并调用同一函数，验证返回值从 `9` 切到 `10`。 |
| 输入/前置 | 使用 `RequireRunningProductionEngine(*this, ...)` 拿 production engine，并在 `ON_SCOPE_EXIT` 中 `Engine.DiscardModule(TEXT("HotReloadPhase2BMathNamespace"))`。通过 `CompileModuleWithResult(&Engine, ECompileType::FullReload/SoftReloadOnly, ...)` 分别编译 V1/V2；用 `FindGeneratedClass(&Engine, TEXT("UNativeHotReloadPhase2BMathCarrier"))` 取 `UClass*`，再用 `NewObject<UObject>(GetTransientPackage(), GeneratedClass)` 创建实例。调用可复用 [AngelscriptScriptSpawnedActorOverrideTests.cpp](/J:/UnrealEngine/AngelscriptProject/Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptScriptSpawnedActorOverrideTests.cpp) 里的 `TryInvokeGeneratedFunction` 模式，或直接 `ProcessEvent` 执行 `ComputeSquare(int)`。 |
| 期望行为 | 1）V1 full reload 成功，`ComputeSquare(3)` 返回 `9`；2）V2 soft reload 成功，`ReloadCompileResult` 为 `FullyHandled` 或 `PartiallyHandled`；3）reload 后重新取到的 `ComputeSquare(3)` 返回 `10`，证明调用方已切到新 namespace 函数体；4）若引擎支持旧实例继续使用，再额外断言 reload 前对象再次调用也返回 `10`，锁定旧对象 thunk 更新路径。 |
| 使用的 Helper | `RequireRunningProductionEngine` + `CompileModuleWithResult` + `FindGeneratedClass` + `NewObject` + `ProcessEvent` / `TryInvokeGeneratedFunction` + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-131 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
---

## 测试审查 (2026-04-10 01:01)

### 一、现有测试问题

#### Issue-132：`Execution.MixedArgs` 只跑当前引擎的浮点模式，另一条 marshalling/return 解码路径长期处于未测状态

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.MixedArgs` |
| 行号范围 | 231-316 |
| 问题描述 | 用例先读取 `ScriptEngine->GetEngineProperty(asEP_FLOAT_IS_FLOAT64)`，随后二选一生成 `double Test(int A, double B, int C)` 或 `float Test(int A, float B, int C)`，并分别走 `SetArgQWord`/`GetReturnQWord` 或 `SetArgFloat`/`GetReturnFloat`。这意味着单次执行永远只覆盖当前环境下的一条 ABI 分支；另一条参数写入和返回值解码路径即使在 `bScriptFloatIsFloat64` wiring、声明选择或 context 取值上回归，也不会在该测试里暴露。与 `Types.Float` 已经暴露出的同类问题一样，这个用例把“当前配置能跑通”误写成了“mixed float/int marshalling 已验证”。 |
| 影响 | 浮点兼容模式切换时最容易出问题的正是参数编组和返回值读取。如果某次改动只破坏了 `float32` 或 `float64` 其中一侧，当前 `Execution.MixedArgs` 会在默认配置下持续绿灯，导致 ABI 回归直到换配置或更高层脚本才被发现。 |
| 修复建议 | 1）把现有用例拆成显式双模矩阵，而不是依赖 ambient engine 配置；2）使用 `FAngelscriptEngine::CreateForTesting(...)` 分别创建 `bScriptFloatIsFloat64=false/true` 的两个 isolated engine，分别断言 `float` 路径的 `SetArgFloat/GetReturnFloat` 与 `double` 路径的 `SetArgQWord/GetReturnQWord`；3）若继续复用同一 helper，至少让测试同时验证“配置值、脚本声明、参数写入 API、返回值读取 API”四者成链一致，而不是只跑当前分支。 |

#### Issue-133：`Misc.Namespace` 只覆盖单层 namespace 下的单次函数调用，测试名明显高估命名空间解析覆盖

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Misc.Namespace` |
| 行号范围 | 9-30 |
| 问题描述 | 用例脚本只有 `namespace MyNamespace { const int Value = 42; int GetValue() { return Value; } } int Test() { return MyNamespace::GetValue(); }`，最终只断言一次返回 `42`。这只能证明最简单的“单层限定名调用命名空间函数”能工作，却没有覆盖 nested namespace、namespace 内常量的直接读取、同名全局遮蔽或 `MyNamespace::Value` 与 `Value` 的解析差异。测试名直接叫 `Misc.Namespace`，容易让后续审查误以为整个 namespace lookup 语义都已经有回归保护。 |
| 影响 | 一旦限定名查找、namespace 作用域遮蔽或 nested namespace 解析回归，当前套件仍可能全绿；脚本开始组织成多层命名空间后，问题只会在更复杂的业务脚本里才暴露。 |
| 修复建议 | 1）把当前 happy path 收窄为更准确的子场景名，或保留原名但追加多段编码断言；2）至少在同一模块内补 `namespace Outer { namespace Inner { ... } }`、全局 `Value` 与 `Outer::Value` 并存、以及直接读取 `MyNamespace::Value` 的断言；3）若代码量增长，按项目规范把 namespace lookup 矩阵拆到独立文件，例如 `AngelscriptNamespaceTests.cpp`，避免继续把更复杂的作用域规则塞进 `Misc` 大杂烩。 |

### 二、需要新增的测试

#### NewTest-110：补 `FAngelscriptTypeUsage::FromDataType` 的 qualifier 与容器映射矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptTypeUsage::FromDataType(const asCDataType& DataType)` |
| 现有测试覆盖 | `Internals.DataType.*` 只验证底层 `asCDataType` 本身的 flag/size/object-handle 语义；`TypeUsage` 方向当前已有建议集中在 `FromParam`、`FromReturn`、`FromProperty`、`FromClass`、`FromStruct` 与 `EqualsUnqualified`。目前没有任何正式测试直接命中 `FromDataType(...)` 这条把底层 AngelScript type descriptor 映射回 runtime wrapper 的入口。 |
| 风险评估 | 如果 `GetTypeIdFromDataType()` 与 wrapper qualifier 恢复之间出现错位，绑定系统、调试器和属性桥接都会落入“底层 `asCDataType` 正常，但 `FAngelscriptTypeUsage` 读出来错误”的隐蔽回归；现有 LanguageFeatures 套件无法把问题定位到这条转换边界。 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeUsage.FromDataType.QualifierAndContainerMatrix` |
| 测试类型 | Unit |
| 测试文件 | 追加到规划中的 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeUsageTests.cpp` |
| 场景描述 | 在带 `FAngelscriptEngineScope` 的 clean engine 中，直接构造一组 `asCDataType`：`const int&`、`const AActor@` 和 `array<int>`。随后对每个 `asCDataType` 调用 `FAngelscriptTypeUsage::FromDataType(...)`，验证 wrapper 能恢复 top-level `const/ref`、object handle 以及 template subtype。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `FAngelscriptEngineScope`。准备数据类型时：1）`asCDataType IntConstRef = asCDataType::CreatePrimitive(ttInt, true); IntConstRef.MakeReference(true);`；2）`asCTypeInfo* ActorType = static_cast<asCTypeInfo*>(Engine.GetScriptEngine()->GetTypeInfoByName(\"AActor\")); asCDataType ActorHandle = asCDataType::CreateObjectHandle(ActorType, false); ActorHandle.MakeHandleToConst(true);`；3）`asITypeInfo* ArrayType = Engine.GetScriptEngine()->GetTypeInfoByDecl(\"array<int>\"); asCDataType ArrayValue = asCDataType::CreateType(static_cast<asCTypeInfo*>(ArrayType), false);`。 |
| 期望行为 | 1）`IntConstRef` 的 usage 有效，`bIsConst == true`、`bIsReference == true`，且 `GetAngelscriptDeclaration() == "const int&"`；2）`ActorHandle` 的 usage 有效，声明字符串保持 handle 形态并保留 `const`，同时 `GetClass() == AActor::StaticClass()`；3）`ArrayValue` 的 usage 有效、`SubTypes.Num() == 1`，其子类型声明为 `int`，证明容器 subtype 没在 `FromDataType` 边界丢失。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `FAngelscriptEngineScope` + `asCDataType::CreatePrimitive/CreateObjectHandle/CreateType` + `FAngelscriptTypeUsage::FromDataType` |
| 优先级 | P1 |

### 本轮校正说明

`NewTest-44` 至 `NewTest-46` 和对应的 `### 本轮汇总` 已经写入本文前部，但由于本次追加时命中了前文已有的同名标题，正文被插入到了较早位置而不是当前文件尾部。

为遵守“只追加不覆盖”，这里不重复正文，也不改动前面已有内容；后续整理文档时，应将 `NewTest-44`、`NewTest-45`、`NewTest-46` 与它们后面的汇总表视为 `## 测试审查 (2026-04-08 19:21)` 这一轮的新增发现。

---

## 校正说明 (2026-04-08 19:55)

`## 测试审查 (2026-04-08 19:39)` 这一轮新增内容已经写入文档，编号范围为 `Issue-65` 至 `Issue-67`、`NewTest-47` 至 `NewTest-50`。

该段正文因为再次命中了前文同样的节锚点，被插入到了文档前部（约第 211 行）而不是当前文件尾部。本次仅在文件末尾追加定位说明和对应汇总，不重复正文，也不改动前面任何已有内容；后续整理文档时，应将第 211 行起的 `## 测试审查 (2026-04-08 19:39)` 视为本轮最新发现。

### 本轮汇总（对应 `## 测试审查 (2026-04-08 19:39)`）

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-65 |
| WeakAssertion | 2 | Issue-66 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | MissingEdgeCase: 3, MissingScenario: 1 |

---

## 校正说明 (2026-04-08 20:20)

`Issue-68` 已经写入文档，但由于补丁命中了前文重复的汇总表锚点，正文被插入到了文档前部（当前约第 234 行附近）而不是本文件真实末尾。

本次仅在 EOF 追加定位说明，不重复 `Issue-68` 正文，也不改动前面任何已有内容；后续整理文档时，应将前部的 `## 测试审查 (2026-04-08 20:18)` 视为本轮最新发现。

---

## 测试审查 (2026-04-08 20:18)

### 一、现有测试问题

#### Issue-68：`Objects.ZeroSize` 只验证“能声明一个空对象”，没有让零大小对象参与任何可观察语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Objects.ZeroSize` |
| 行号范围 | 148-166 |
| 问题描述 | 用例脚本只有 `class EmptyObject {} int Run() { EmptyObject Instance; return 1; }`，最终断言返回 `1`。这只能证明 parser/compiler 接受了空类语法，无法证明零大小对象的构造、局部槽位分配、复制传递或调用约定真的正确。即使 runtime 把空对象错误降级成“声明后从不触碰的占位符”，该测试仍会稳定绿灯。 |
| 影响 | 零大小 script object 一旦在 stack layout、parameter passing 或临时值构造路径上回归，当前对象模型套件不会报警；`Objects.ZeroSize` 这个名字也会明显高估它对 empty class runtime 语义的保护强度。 |
| 修复建议 | 把“声明后直接返回常量”升级成可观察行为：1）让 `EmptyObject` 参与一次按值传参或返回，例如 `int Accept(EmptyObject Value) { return 2; } int Run() { EmptyObject Instance; return Accept(Instance); }`，锁定 zero-size value 在调用约定里不会丢失；2）再补一个包含两个独立局部实例的路径，验证连续声明不会破坏后续局部变量布局；3）如果后续 runtime 已支持 empty object method dispatch，可进一步增加一个无字段成员函数调用，避免该用例永远停留在语法冒烟层。 |

### 二、需要新增的测试

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-68 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |

---

## 测试审查 (2026-04-08 19:39)

### 一、现有测试问题

#### Issue-65：`GC` 探针类型按固定名字复用，`RegisterGCProbeType()` 会把旧注册状态当成当前测试前置

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.GC.ManualCycleCollection`、`Angelscript.TestModule.Internals.GC.CycleDetection` |
| 行号范围 | 199-228, 343-419 |
| 问题描述 | `RegisterGCProbeType()` 先用固定名字 `GCProbeObject` 调 `ScriptEngine->GetTypeInfoByName("GCProbeObject")`，只要找到已有 type 就直接 `return true`，不再验证 `ADDREF/RELEASE/ENUMREFS/RELEASEREFS` 等 GC behaviour 是否仍然绑定到当前 helper；而 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 的 reset 语义只清模块与 detached `UASClass`，不会清引擎级 object type 注册。结果是这两个 GC 用例会依赖“之前某次测试已经把同名 type 正确注册好”的历史状态，类型名冲突、旧 behaviour 残留或跨测试污染都会被静默吞掉。 |
| 影响 | 一旦别的测试先注册了同名 `GCProbeObject`，或者当前 helper 实现变化后旧 behaviour 仍挂在共享引擎上，这两个核心 GC 用例可能在错误的 type/behaviour 组合上继续绿灯，形成顺序相关的假阳性。 |
| 修复建议 | 1）不要用固定全局类型名，改成带测试唯一后缀的 probe type 名称，例如 `GCProbeObject_ManualCycle` / `GCProbeObject_CycleDetection`；2）即便 type 已存在，也要显式校验全部 GC behaviours 和 `GGCProbeScriptEngine` 指向是否匹配当前实现，而不是直接成功返回；3）若无法避免共享名字，至少改用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` 或 isolated clone/full engine，彻底切断引擎级注册状态的测试间复用。 |

#### Issue-66：`Compiler.TypeConversions` 名称覆盖“多种转换”，实际只验证单个 `int -> float32` happy path

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Compiler.TypeConversions` |
| 行号范围 | 127-162 |
| 问题描述 | 用例脚本固定为 `float32 Entry() { int Value = 7; return float32(Value); }`，然后只检查 `Prepare/Execute` 成功且返回 `7.0f`。这只覆盖了一个最简单的 widening conversion；没有任何负数、narrowing、`float64`、`const/ref` 参与、也没有对白盒 bytecode 或 type id 做检查。测试名却使用复数 `TypeConversions`，会让人误以为编译器的主要 numeric conversion 路径都已受保护。 |
| 影响 | 编译器一旦在其他转换路径上回归，例如负数截断、`double`/`float32` 分支、显式 narrowing 或 qualifier 传递，当前套件不会发出任何信号；定位时也很容易被这个过宽的测试名误导。 |
| 修复建议 | 1）把现有用例收窄命名为 `IntToFloat32`，避免继续用单一路径代表整类转换；2）在同文件补至少两个补充用例：一个验证负数 `float -> int` / `double -> int` 的 toward-zero 结果，一个验证显式 narrowing 或 `float64` 分支；3）若保持 internals 定位，再追加对白盒 `GetByteCode()` 或返回声明的检查，确保不是只靠执行结果间接碰巧通过。 |

#### Issue-67：`DataType.Comparisons` 只覆盖 primitive `int` 的 `const/ref`，却以宽泛标题代表整个比较语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDataTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.DataType.Comparisons` |
| 行号范围 | 48-59 |
| 问题描述 | 用例只创建了 `MutableInt`、`ConstInt` 和 `RefInt` 三个 primitive `ttInt` 变体，随后断言 `IsEqualExceptConst`、`IsEqualExceptRef` 与 `operator==` 的最基础差异。它没有覆盖 `as_datatype.h` 里同类 API 的剩余高价值路径，例如 `IsEqualExceptRefAndConst()`、object handle vs handle-to-const、`null handle` 与普通 handle、以及 object/value type 的比较。 |
| 影响 | 一旦 `asCDataType` 在 handle qualifier、组合限定符或对象类型比较上回归，`DataType.Comparisons` 仍会全部通过；测试名又会误导后续审查，以为比较语义已经做过完整白盒验证。 |
| 修复建议 | 1）把当前用例改名为更准确的 `PrimitiveConstAndRefComparisons`；2）同文件新增对白盒组合限定符的断言，至少覆盖 `IsEqualExceptRefAndConst()` 和 `CreateObjectHandle(..., true)`；3）再补一组 `CreateNullHandle()` 与普通 object handle 的不等价断言，防止 handle 比较逻辑被 primitive happy path 掩盖。 |

### 二、需要新增的测试

#### NewTest-47：补 `Compiler.TypeConversions` 的负数与 `float64` 矩阵，避免复数测试名继续只覆盖单条 widening path

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute`、`asIScriptFunction::GetByteCode` |
| 现有测试覆盖 | `Compiler.TypeConversions` 只覆盖 `int -> float32` 返回 `7.0f` 的 happy path，没有任何负数、`float64` 或 toward-zero 断言 |
| 风险评估 | 编译器如果在负数截断、`float64` 路径或显式 conversion opcode 选择上回归，当前 internals 套件会继续绿灯；问题通常要等到语言层复杂脚本才暴露 |
| 建议测试名 | `Angelscript.TestModule.Internals.Compiler.TypeConversions.NegativeAndFloat64Matrix` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 场景描述 | 编译一个同时包含 `float32 -> int` 负数截断和 `float64 -> int` 正数截断的脚本入口，并把两条转换结果编码进单个返回值。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction`；脚本示例：`int Entry() { float32 A = -3.75f; float64 B = 9.25; int FromA = int(A); int FromB = int(B); return (FromA + 10) * 100 + FromB; }`。 |
| 期望行为 | `Entry()` 返回 `709`，从而同时证明 `int(-3.75f) == -3`、`int(9.25) == 9`；若需要保持 internals 定位，可再断言 `GetByteCode()` 非空且长度大于 0，确认不是解释器侧偶然行为。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` + 可选 `GetByteCode()` |
| 优先级 | P1 |

#### NewTest-48：补 `DataType.Comparisons` 的 handle qualifier 矩阵，直接锁定 `IsEqualExceptRefAndConst()` 与 `null handle` 语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptEngine::GetTypeInfoByName` |
| 现有测试覆盖 | `DataType.Comparisons` 只覆盖 primitive `ttInt` 的 `const/ref` 差异，没有覆盖 object handle、handle-to-const、`IsEqualExceptRefAndConst()` 或 `null handle` |
| 风险评估 | 一旦 `asCDataType` 在对象 handle qualifier 上回归，绑定生成、调试展示和参数匹配都可能拿到错误的比较结果；当前套件无法第一时间定位到类型比较层 |
| 建议测试名 | `Angelscript.TestModule.Internals.DataType.Comparisons.HandleQualifierMatrix` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDataTypeTests.cpp` |
| 场景描述 | 直接构造 `AActor` 的 value type、普通 object handle、handle-to-const 和 `null handle`，逐项验证 exact equality 与 “忽略 ref/const” 比较函数的差异。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `ScriptEngine->GetTypeInfoByName("AActor")` 获取 type info，构造 `asCDataType Value = asCDataType::CreateType(ActorType, false); asCDataType Handle = asCDataType::CreateObjectHandle(ActorType, false); asCDataType ConstHandle = asCDataType::CreateObjectHandle(ActorType, true); asCDataType RefConstHandle = ConstHandle; RefConstHandle.MakeReference(true); asCDataType NullHandle = asCDataType::CreateNullHandle();`。 |
| 期望行为 | `Handle != ConstHandle`；`Handle.IsEqualExceptConst(ConstHandle) == true`；`Handle.IsEqualExceptRefAndConst(RefConstHandle) == true`；`NullHandle != Handle` 且 `NullHandle.IsObjectHandle() == true`；同时 `Handle.GetTypeInfo() == ActorType`，确保比较矩阵建立在正确目标类型上。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ScriptEngine->GetTypeInfoByName` + `asCDataType` 白盒 API |
| 优先级 | P1 |

#### NewTest-49：补 `Execution.Script` 的区间边界矩阵，直接验证空区间、单元素和负数范围

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `Execution.Script` 只执行 `Calculate(1, 10)` 并断言返回 `55`，没有覆盖单元素区间、`Start > End` 的零迭代路径或负数范围 |
| 风险评估 | 一旦循环边界、比较符或参数传递在执行路径上出现 off-by-one / 反向区间回归，现有执行测试不会报警；这类问题会直接污染脚本里的区间累加与遍历逻辑 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.Script.RangeBoundaries` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionScriptRangeTests.cpp` |
| 场景描述 | 复用现有 `Calculate(int Start, int End)` 形式，在同一测试里依次执行单元素区间、反向空区间和跨零负数区间。 |
| 输入/前置 | 使用 `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()`；脚本示例：`int Calculate(int Start, int End) { int Result = 0; for (int Index = Start; Index <= End; ++Index) { Result += Index; } return Result; }`。测试内依次执行 `(1, 1)`、`(5, 4)`、`(-2, 2)` 三组参数。 |
| 期望行为 | 返回值依次为 `1`、`0`、`0`；第一组证明区间包含上界，第二组证明反向区间不会错误进入循环，第三组证明负数到正数范围的累计不会丢失跨零边界。 |
| 使用的 Helper | `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` |
| 优先级 | P1 |

#### NewTest-50：补 native child interface 走 parent `Execute_` bridge 的 setter 与 `int&` round-trip

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.h` |
| 关联函数 | `InvokeReflectiveUFunctionFromGenericCall` |
| 现有测试覆盖 | `NativeInheritedImplement` 只验证 child implementation 上的 `Execute_GetNativeValue` / `Execute_GetChildValue`；`NativeReferenceRoundTrip` 又只覆盖 direct parent implementation 的 `AdjustNativeValue(int, int&)` |
| 风险评估 | 如果 native child implementation 通过 parent interface 走 `Execute_SetNativeMarker` 或 `Execute_AdjustNativeValue` 时桥接错误，当前套件不会报警；这正是 `UInterface` 继承链最容易漏掉的 dispatch 组合 |
| 建议测试名 | `Angelscript.TestModule.Interface.NativeInheritedImplement.ParentBridgeSetterAndRef` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 场景描述 | 生成一个实现 `UAngelscriptNativeChildInterface` 的脚本 actor，既实现 parent setter，也实现 parent `AdjustNativeValue(int Delta, int& Value)`；然后从 C++ 侧只通过 parent interface 的 `Execute_` bridge 调用这两个入口。 |
| 输入/前置 | 复用 `EnsureNativeInterfaceFixturesBound()`；脚本类可沿用 `NativeInheritedImplement` 的结构，但新增 `UPROPERTY() int ParentAdjustedValue = 0;`。测试在 spawn 后先执行 `IAngelscriptNativeParentInterface::Execute_SetNativeMarker(Actor, TEXT("FromParentExecute"))`，再令 `int Value = 20; IAngelscriptNativeParentInterface::Execute_AdjustNativeValue(Actor, 9, Value);`，并将结果写回属性或直接断言局部变量。 |
| 期望行为 | `NativeMarker == FName(TEXT("FromParentExecute"))`；`Value == 29`；若脚本同时把 `ParentAdjustedValue` 记录下来，则该属性也应为 `29`。这证明 child implementation 经 parent `Execute_` bridge 仍能正确命中 setter 与 ref-param thunk。 |
| 使用的 Helper | `EnsureNativeInterfaceFixturesBound` + `CompileScriptModule` + `FActorTestSpawner` + `ReadPropertyValue` + `IAngelscriptNativeParentInterface::Execute_*` |
| 优先级 | P1 |

#### NewTest-44：补 `asEP_FOREACH_SUPPORT` 的关闭/恢复行为测试，证明 property 不只是“能存值”

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp` |
| 关联函数 | `asIScriptEngine::SetEngineProperty`、`asIScriptEngine::GetEngineProperty` |
| 现有测试覆盖 | `Upgrade.EngineProperties` 只验证 getter/setter round-trip；`NewTest-1` 虽然补 `foreach` happy path，但没有覆盖把 `asEP_FOREACH_SUPPORT` 关闭后的编译拒绝路径 |
| 风险评估 | 如果升级后 `asEP_FOREACH_SUPPORT` 被 parser/compiler 忽略，现有兼容测试仍会全绿；项目会在以为“已关闭 foreach”时继续接受语法，或在重新开启后仍然拒绝合法脚本 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Upgrade.EngineProperties.ForeachSupportGate` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 场景描述 | 在同一 isolated engine 中先关闭 `asEP_FOREACH_SUPPORT`，编译一个含 `foreach` 的最小脚本并断言失败；随后恢复为 `1`，用相同脚本重新编译并执行，证明 property 真正影响语言开关。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_FULL()` 或 `CreateIsolatedCloneEngine()`；脚本示例：`int Run() { TArray<int> Values = {1,2,3}; int Sum = 0; foreach (int Value : Values) { Sum += Value; } return Sum; }`。先 `SetEngineProperty(asEP_FOREACH_SUPPORT, 0)` 后用 `CompileModuleWithResult` 编译，记录 diagnostics；再 `SetEngineProperty(asEP_FOREACH_SUPPORT, 1)`，重新编译同脚本并执行 `Run()`。 |
| 期望行为 | 关闭时 `bCompiled == false`、`CompileResult == ECompileResult::Error`，diagnostics 命中 `foreach` 相关报错；重新开启后同脚本编译成功并返回 `6`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL` / `CreateIsolatedCloneEngine` + `CompileModuleWithResult` + `Engine.Diagnostics` 读取 helper + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-45：直接锁定 `BuildFunctionDeclaration` 的 trailing-default 规则与 `const` 后缀

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptType::BuildFunctionDeclaration` |
| 现有测试覆盖 | `Functions.DefaultArguments`、`NamedArguments` 只从脚本执行侧验证语言语法，没有任何白盒测试直达 native declaration builder，也没有锁定“默认值缺口要裁掉前置默认值”和 `const` 方法后缀输出 |
| 风险评估 | 一旦 declaration builder 生成错签名，native bind、hot reload 差异比较和反射声明输出都会出现系统性回归，但现有 LanguageFeatures 套件不会给出直接信号 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeUsage.BuildFunctionDeclaration.DefaultGapAndConstSuffix` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeUsageTests.cpp` |
| 场景描述 | 构造 `int` 返回值和 `int/int/bool` 参数的 `FAngelscriptTypeUsage`，直接调用 `BuildFunctionDeclaration()` 两次，分别验证“所有参数都有默认值”和“中间参数缺默认值时只保留 trailing default”这两种输出。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 初始化 type database；构造 `FAngelscriptTypeUsage IntUsage(FAngelscriptType::GetByAngelscriptTypeName(TEXT("int")))`、`BoolUsage(...)`。第一次调用参数名 `A/B/C`、默认值 `{"7","9","true"}`、`bConstMethod=true`；第二次默认值改成 `{"7","-","true"}`。 |
| 期望行为 | 第一次返回精确声明 `int Mix(int A = 7,int B = 9,bool C = true) const`；第二次返回 `int Mix(int A,int B,bool C = true) const`，从而锁定 default-gap 裁剪规则和 `const` 后缀格式。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `FAngelscriptType::GetByAngelscriptTypeName` + `FAngelscriptType::BuildFunctionDeclaration` |
| 优先级 | P2 |

#### NewTest-46：补 `CanCastScriptObjectToUnrealInterface` 的正向与 guard 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CanCastScriptObjectToUnrealInterface` |
| 现有测试覆盖 | `Interface.CastSuccess`、`CastFail` 只通过脚本里的 `Cast<>()` 间接验证 happy path，没有任何白盒测试直接覆盖 helper 的 `nullptr` guard、非接口目标拒绝以及正向 fast path |
| 风险评估 | 如果 fast path 在 null/object type 校验上退化成宽松通过，脚本 cast 可能出现假阳性、错误 dispatch，甚至在极端情况下因为错误 user data / target class 触发崩溃 |
| 建议测试名 | `Angelscript.TestModule.Interface.CastFastPath.GuardsAndPositivePath` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` |
| 场景描述 | 编译一个实现接口的 actor 和一个未实现接口的 actor，直接获取它们的 `asITypeInfo*` 与接口 type info，调用 `CanCastScriptObjectToUnrealInterface()` 覆盖成功、失败和 guard 分支。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileScriptModule` + `FActorTestSpawner`；脚本模块定义 `UINTERFACE() interface UIDamageableCastFastPath`、`AScenarioInterfaceCastFastPath : AActor, UIDamageableCastFastPath` 和 `AScenarioInterfaceCastPlain : AActor`。通过 `FAngelscriptType::GetBoundClassName(GeneratedClass)` + `Engine.GetScriptEngine()->GetTypeInfoByName(...)` 取得实现类、普通类和接口的 type info。随后分别调用：1）实现类对象 + 接口 type；2）普通类对象 + 接口 type；3）实现类对象 + 普通类 type 作为 target；4）接口 type + `nullptr` object。 |
| 期望行为 | 调用 1 返回 `true`；调用 2/3/4 全部返回 `false`；从而证明 fast path 只会接受“对象真实实现了接口”的组合，不会把 null 或非接口目标误判为可 cast。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH` + `CompileScriptModule` + `FActorTestSpawner` + `FAngelscriptType::GetBoundClassName` + `GetTypeInfoByName` + `FAngelscriptEngine::CanCastScriptObjectToUnrealInterface` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-63 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 1, NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |

#### NewTest-1：补齐 `foreach` / `break` / `continue` 组合控制流

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptEngine::SetEngineProperty(asEP_FOREACH_SUPPORT, ...)` |
| 现有测试覆盖 | 有测试但缺少 `foreach`、`break`、`continue` 组合场景 |
| 风险评估 | 控制流 lowering 或 iterator 语义回归时，现有 `ControlFlowTests` 无法发现，尤其 `continue`/`break` 在 `foreach` 中的行为完全未覆盖。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.ForeachBreakContinueNested` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 场景描述 | 编译一个包含 `foreach`、`continue`、`break` 的脚本入口，确保循环跳过、提前退出和累计逻辑同时成立。 |
| 输入/前置 | 使用 fresh/shared engine，脚本示例可为 `array<int> Values = {1,2,3,4,5}; int Count=0; int Sum=0; foreach(int Value : Values){ if(Value == 2) continue; ++Count; Sum += Value; if(Value == 4) break; } return Count * 10 + Sum;`。 |
| 期望行为 | 编译成功并执行返回 `38`；断言 `Count == 3`、`Sum == 8` 的编码结果准确，证明 `continue` 跳过 `2` 且 `break` 在 `4` 处提前终止。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-2：补非法 `break` / `continue` 的错误路径与诊断断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp` |
| 关联函数 | `asCBuilder::CompileFunction` |
| 现有测试覆盖 | `ControlFlowTests` 只有正常路径，没有非法控制流语句的编译失败场景 |
| 风险评估 | parser / compiler 若错误接受 `break` 或 `continue` 的非法位置，会直接放出错误 bytecode，现有用例不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.InvalidBreakContinueDiagnostics` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 场景描述 | 分别编译 `void Run(){ break; }` 和 `void Run(){ continue; }`，验证编译失败且 diagnostics 指向非法控制流语句。 |
| 输入/前置 | 使用 full engine；通过 `CompileModuleWithSummary` 或 `CompileModuleWithResult` 编译两段非法脚本。 |
| 期望行为 | `bCompileSucceeded == false`；`CompileResult == ECompileResult::Error`；diagnostics 至少包含 `break` / `continue` 非法使用的消息，并带有非零行列号。 |
| 使用的 Helper | `CompileModuleWithSummary` |
| 优先级 | P1 |

#### NewTest-3：覆盖 `UInterface` soft reload 遇到结构变化时的 `FullReloadRequired` 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | 有 `Interface.HotReload`，但只覆盖“函数体变更仍可 soft reload”的 happy path |
| 风险评估 | `UPROPERTY()` / `UFUNCTION()` 结构变化在 PIE 中需要 full reload；如果错误 swap 进新模块或未正确排队 full reload，会导致旧代码/新元数据错配。 |
| 建议测试名 | `Angelscript.TestModule.Interface.HotReload.FullReloadRequiredKeepsOldCode` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceLifecycleTests.cpp` |
| 场景描述 | 先编译并运行 V1 接口实现类；再用 `SoftReloadOnly` 编译 V2，其中新增 `UPROPERTY()` 或新增 `UFUNCTION()`；验证 soft reload 不能直接切换结构变化版本，旧行为仍保持；最后再执行一次 full reload，验证新成员可见。 |
| 输入/前置 | V1 例子：接口实现类只暴露 `GetValue()` 返回 `1`；V2 增加 `UPROPERTY() int Extra = 7;` 或新增接口方法。需要 scenario helper 负责 compile、spawn actor、读取属性。 |
| 期望行为 | soft reload 阶段返回 `ECompileResult::ErrorNeedFullReload` 或 diagnostics 明确提示 full reload required，reload 前实例仍返回 `1`；full reload 后新实例能读取到 `Extra == 7` 或新方法可见并可调用。 |
| 使用的 Helper | `CompileModuleWithSummary` + `CompileScriptModule` + `FActorTestSpawner` |
| 优先级 | P0 |

#### NewTest-4：直接覆盖 `CanCastScriptObjectToUnrealInterface` 的 guard 分支

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CanCastScriptObjectToUnrealInterface` |
| 现有测试覆盖 | 只有脚本层 cast 成功/失败场景，没有直接覆盖 `nullptr` 与“目标不是 interface”这几个 guard 分支 |
| 风险评估 | 该静态 helper 一旦在空指针或非接口目标上行为异常，会影响所有脚本接口 cast 路径，现有 scenario 测试难以精确定位。 |
| 建议测试名 | `Angelscript.TestModule.Interface.Cast.GuardsReturnFalse` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` |
| 场景描述 | 在已有 cast fixture 基础上，直接调用 `CanCastScriptObjectToUnrealInterface`，分别传入 `nullptr` runtime type、`nullptr` target type、`nullptr` object 指针，以及一个非 interface 的 target type。 |
| 输入/前置 | 需要绑定一个实现接口的 script actor 类型，拿到对应 `asITypeInfo*`；非 interface target 可使用 `AActor` 对应的 bound type。 |
| 期望行为 | 四种 guard 输入都返回 `false` 且不触发 crash / ensure；正常 interface target 仍返回 `true`，形成对照。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH` + `CompileScriptModule` + 局部 helper 获取 `asITypeInfo*` |
| 优先级 | P1 |

#### NewTest-5：真正验证 `StructCppOps` 绑定时走 `GetCppStructOps()` 与生命周期函数

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` |
| 关联函数 | `FAngelscriptBinds::ValueClass(FBindString, UScriptStruct*, FBindFlags)` |
| 现有测试覆盖 | 当前 `StructCppOpsTests` 只有 metadata 断言，完全无 source-level 覆盖 |
| 风险评估 | `GetCppStructOps()` size/alignment 或 ctor/copy/dtor 绑定错误会直接影响脚本值类型布局和对象生命周期，属于高风险盲区。 |
| 建议测试名 | `Angelscript.TestModule.Internals.StructCppOps.ValueClassUsesCppStructOps` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptStructCppOpsTests.cpp` |
| 场景描述 | 定义一个带自定义 ctor/copy/dtor 计数器的 native `UScriptStruct` fixture，经 `FAngelscriptBinds::ValueClass` 绑定后，验证 size/alignment 取自 `GetCppStructOps()`，并通过 `InitializeStruct` / `CopyScriptStruct` / `DestroyStruct` 验证生命周期。 |
| 输入/前置 | 需要一个本地测试 struct：默认构造写入哨兵值，复制时递增计数，析构时递增静态 counter。 |
| 期望行为 | 绑定后的 size 等于 `Ops->GetSize()`，默认构造值可读，复制后目标值与源值一致且 copy counter 增加，销毁后 dtor counter 增加。 |
| 使用的 Helper | `FAngelscriptEngine` + `FAngelscriptBinds` + `UScriptStruct::InitializeStruct/CopyScriptStruct/DestroyStruct` |
| 优先级 | P0 |

#### NewTest-6：补 `FromTypeId` 对 script enum / delegate / template subtype 的映射

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptTypeUsage::FromTypeId` |
| 现有测试覆盖 | `TypeTests` 和 `DataTypeTests` 只覆盖 primitive/object handle，未覆盖 `FromTypeId` 的 script enum、delegate、script struct/object、subtype 递归分支 |
| 风险评估 | TypeId 映射一旦错误，会影响 debugger、反射桥和函数签名匹配，回归波及面大但不容易通过现有高层测试暴露。 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeUsage.FromTypeIdScriptKinds` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeUsageTests.cpp` |
| 场景描述 | 编译包含 script enum、delegate、script struct、script object 与 template container 的模块，取各自 type id 后调用 `FromTypeId`，验证解析出的 `Type`、`ScriptClass` 与 `SubTypes`。 |
| 输入/前置 | 需要一个 test module，包含 `enum EMode { ... }`、`delegate void FOnDone()`、`struct FPayload { int Value; }`、`class FCarrier {}`、`array<FPayload>` 或等价 template 容器。 |
| 期望行为 | enum 映射到 `GetScriptEnum()`；delegate 映射到 `GetScriptDelegate()` 或 `GetScriptMulticastDelegate()`；script struct/object 分别映射到对应 script type；container 的 `SubTypes` 至少包含 `FPayload`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` |
| 优先级 | P1 |

#### NewTest-7：补 tokenizer 对未闭合 block comment 与转义字符串长度的边界断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp` |
| 关联函数 | `asCTokenizer::GetToken` |
| 现有测试覆盖 | 仅覆盖单行注释、多行注释、普通字符串、未闭合字符串和未知字符，未覆盖未闭合 block comment 与带转义引号的 token 长度 |
| 风险评估 | tokenizer 在 comment/string 边界上的错误常会向 parser 传播成难定位问题，现有测试对这类边界保护不足。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Tokenizer.UnterminatedBlockCommentAndEscapes` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 场景描述 | 用 `FTokenizerAccessor` 分别解析未闭合 `/* comment` 和含转义引号/反斜杠的字符串字面量，验证 token 类型与返回长度。 |
| 输入/前置 | 输入示例：`"/* broken"`、`"\"a\\\"b\""`、`"\"\\\\\""`。 |
| 期望行为 | 未闭合 block comment 返回对应错误 token 或 non-terminated token；带转义引号的字符串仍返回 `ttStringConstant`，且 `TokenLength` 精确覆盖整个字面量。 |
| 使用的 Helper | 现有 `FTokenizerAccessor` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 14 | Issue-4 |
| AntiPattern | 2 | Issue-3 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | MissingErrorPath: 1, NoTestForSource: 1 |
| P1 | 4 | MissingErrorPath: 1, MissingScenario: 1, NoTestForSource: 2 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-08 13:25)

### 一、现有测试问题

#### Issue-17：`FunctionTests` 中 3 个 negative test 只验证“编译失败”，没有锁定失败原因

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Functions.Pointer`、`Angelscript.TestModule.Angelscript.Functions.Template`、`Angelscript.TestModule.Angelscript.Functions.Factory` |
| 行号范围 | 91-112, 167-188, 196-217 |
| 问题描述 | 这三个用例都调用 `CompileModuleWithResult(...)`，但真正的断言只有 `TestFalse(..., bCompiled)`，随后直接把 `!bCompiled` 作为结果返回。用例既没有 `TestEqual(CompileResult, ECompileResult::Error)`，也没有检查 diagnostics 是否包含 `funcdef` / template / factory handle 不支持的特定报错。因此任何无关原因导致的编译失败，例如 fixture 初始化异常、脚本文件名冲突、parser 早期崩坏，都会被当成“预期通过”。 |
| 影响 | negative test 无法区分“目标特性仍不支持”和“编译器在别处坏了”，会掩盖真实回归并降低失败定位价值。 |
| 修复建议 | 对三个用例统一补强 negative assertion：1）显式断言 `CompileResult == ECompileResult::Error`；2）增加 diagnostics helper，验证错误消息包含各自的关键字，例如 `funcdef`、template parameter 或 `@` handle/factory 相关文本；3）若当前分支未来开始支持其中某项特性，应把测试切换为正向执行断言，而不是继续沿用“只要失败就算通过”的结构。 |

#### Issue-18：`NativeScriptHotReload` phase 用共享 production engine 串跑多个脚本，清理时机过晚

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A`、`Phase2B`、`Phase2C` |
| 行号范围 | 14-59, 80-256 |
| 问题描述 | 公共 helper `VerifyNativeScriptHotReloadInline` 通过 `RequireRunningProductionEngine(...)` 直接复用正在运行的 production engine，而不是 fresh/shared test engine。循环内部虽然写了 `ON_SCOPE_EXIT { Engine.DiscardModule(...); }`，但这些 cleanup 会在整个 helper 返回时才执行，不会在每个 `InlineScripts[Index]` 结束后触发。因此同一 phase 里的前一个模块会在后一个模块编译时继续留在引擎里；跨测试用例也没有显式 reset production engine 的状态。 |
| 影响 | hot reload 结果会受前序脚本残留类型、模块描述和生产引擎全局状态影响，形成隐式依赖。测试失败时很难判断是当前脚本问题还是前一个 phase/模块污染，且顺序改变可能导致结果变化。 |
| 修复建议 | 1）不要在循环里把 `ON_SCOPE_EXIT` 当作“每轮清理”；改成每轮结束后立刻调用 `Engine.DiscardModule(*ModuleName.ToString())`，或把单脚本验证提取到独立 helper，使 RAII 作用域缩到单次迭代；2）如果必须依赖 production engine，进入每个 phase 前增加显式状态快照/restore 或全量 reset；3）为三组 phase 增加“先独立运行再乱序运行”的隔离验证，确保模块间不存在顺序依赖。 |

#### Issue-19：`Handle` / `Inheritance` 里的多组 negative test 没有验证具体诊断，仍会把“任意报错”当成通过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Handles.Basic`、`Handles.Auto`、`Inheritance.Interface`、`Inheritance.CastOp`、`Inheritance.Mixin` |
| 行号范围 | `AngelscriptHandleTests.cpp` 22-46, 90-114；`AngelscriptInheritanceTests.cpp` 58-82, 125-149, 156-176 |
| 问题描述 | 这些用例都以“当前分支应不支持某语法”为前提，但断言基本停留在 `TestFalse(..., bCompiled)` 或 `CompileResult == ECompileResult::Error`。它们没有检查 diagnostics 是否真的命中了 `handle`、`interface`、`mixin` 等目标语法的拒绝原因。尤其 `Inheritance.Mixin` 与前面的 `FunctionTests` 一样，连 `CompileResult` 都没有断言。 |
| 影响 | 只要编译器因为任意别的错误提前失败，这些测试就会继续绿灯，无法证明“不支持的是该特性本身”，也不利于未来排查 upstream 合并后到底是哪条兼容约束发生了变化。 |
| 修复建议 | 为这五个 negative test 统一补 `CompileModuleWithSummary` 或 diagnostics helper：1）显式断言 `CompileResult == ECompileResult::Error`；2）校验错误消息包含 `interface` / `mixin` / `@` handle construction 等关键字；3）对 `Inheritance.Mixin` 这类已知 parser blocker，再补一条行列号断言，确保错误落在 mixin 语法位置而不是后续连锁失败。 |

#### Issue-20：`InterfaceImplementTests` 前两例只验证反射实现标记，没有验证接口调用行为

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.ImplementBasic`、`Angelscript.TestModule.Interface.ImplementMultiple` |
| 行号范围 | 33-92, 94-173 |
| 问题描述 | `ImplementBasic` 的脚本类定义了 `DamageReceived` 和 `TakeDamage(float Amount)`，但测试只断言 `Actor->GetClass()->ImplementsInterface(InterfaceClass)`；`ImplementMultiple` 也只验证 actor 同时实现两个接口，没有调用 `TakeDamage` / `Heal`，更没有读取 `Health`。这两个用例证明了元数据层“实现了接口”，却没有证明接口方法能被正确调度到脚本实现。 |
| 影响 | 如果类标记仍然存在，但 interface dispatch、参数传递或 script 函数绑定已经失效，这两例仍会绿灯，无法覆盖 `UInterface` 生命周期中的“实现后可调用”阶段。 |
| 修复建议 | 1）`ImplementBasic` 生成实例后，通过 `Execute_` bridge 或脚本内 cast 调用 `TakeDamage(12.5)`，再读取 `DamageReceived` 断言为 `12.5`；2）`ImplementMultiple` 依次走两个接口入口，断言 `Health` 从 `100` 变到 `90` 再回到 `95`；3）若项目更倾向脚本侧验证，可在 `BeginPlay` 中分别经两个 interface ref 调用，再由测试读取最终状态。 |

#### Issue-21：`Interface.MissingMethod` 把正向编译 helper 用在 negative path，上报错误却不要求编译失败

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.MissingMethod` |
| 行号范围 | 150-209 |
| 问题描述 | 用例先 `AddExpectedError("missing required method")`，说明它预期“实现类漏掉接口方法”会报错；但实际调用的是正向 helper `CompileAnnotatedModuleFromMemory(...)`，最后还把 `bCompiled` 直接作为通过条件返回。也就是说，只要模块编译成功，用例就会绿灯；即使接口约束完全失效，只要仍输出一条包含目标文本的 log，也不会失败。 |
| 影响 | 这个用例无法验证“缺少接口方法必须阻止类通过编译/生成”，会把最关键的 validation path 退化成日志存在性检查，误导 `UInterface` 约束已经被严格执行。 |
| 修复建议 | 改成真正的 negative compile 测试：1）使用 `CompileModuleWithSummary` 或 `CompileModuleWithResult`，显式断言 `bCompileSucceeded == false` 且 `CompileResult == ECompileResult::Error`；2）检查 diagnostics 中存在 `missing required method` 且定位到缺失实现类；3）补充断言 `AScenarioInterfaceMissingMethod` 类未生成或不实现 `UIDamageableMissing`，避免“报了错但仍产出可用类”的假阳性。 |

#### Issue-22：`Interface.GCSafe` 没有持有任何 interface 引用，无法证明接口引用在 GC 前后是安全的

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.GCSafe` |
| 行号范围 | 253-313 |
| 问题描述 | 脚本类只实现了空的 `TakeDamage`，测试流程是 spawn actor、`BeginPlay`、确认 `ImplementsInterface`、`Destroy()`、`CollectGarbage()`、最后断言 `WeakActor` 失效。整个过程没有创建 `UIDamageableGC` interface 引用、没有把 interface ref 跨 GC 保存下来，也没有验证 GC 之后通过 interface wrapper 访问是否被安全阻止。 |
| 影响 | 即使 `TScriptInterface` / 脚本 interface handle 在对象销毁后悬空、引用计数泄漏或再次调用会 crash，这个用例也照样通过；它实际验证的是“actor 能被 GC 回收”，不是“interface GC safe”。 |
| 修复建议 | 1）在脚本或 C++ 侧显式保存一个 interface 引用，例如 `UIDamageableGC SavedRef = Cast<UIDamageableGC>(this);` 或 `TScriptInterface`；2）销毁 actor 并 GC 后，断言该引用变为 `nullptr`/invalid，且后续调用被安全拒绝而非 crash；3）如果引擎有专门的 interface bridge helper，再补一条通过 bridge 触发的 after-GC guard 断言。 |

#### Issue-23：`Builder.CompileErrors` 与 `Parser.SyntaxErrors` 都没有验证错误集合内容，名不副实

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Builder.CompileErrors`、`Angelscript.TestModule.Internals.Parser.SyntaxErrors` |
| 行号范围 | `AngelscriptBuilderTests.cpp` 72-94；`AngelscriptParserTests.cpp` 180-204 |
| 问题描述 | 两个用例都把 malformed input 喂给底层组件，但断言只剩“返回值 < 0”。`Builder.CompileErrors` 没有检查 builder 是否真正收集到 compile error、错误文本或行列号；`Parser.SyntaxErrors` 也没有检查 `Parser.GetErrorString()`、错误位置，甚至没确认 AST 是否为空/已 reset。测试名宣称覆盖错误收集和语法错误，实际只是最薄的一层失败冒烟。 |
| 影响 | 如果 builder/parser 开始返回错误码却丢失具体诊断、定位信息错误，或者错误后仍残留半成品 AST/函数对象，这两个用例都不会失败，无法保护内部组件最关键的调试能力。 |
| 修复建议 | 1）为 builder/parser 接上可观测 diagnostics：记录错误消息、section、row、column，并断言命中预期位置；2）`Builder.CompileErrors` 额外断言 `Function == nullptr` 之外，builder/module 没有残留可执行符号；3）`Parser.SyntaxErrors` 额外断言 `Parser.GetScriptNode()` 为空或至少不含可遍历的有效根节点，确保错误路径不会留下脏状态。 |

#### Issue-24：`Bytecode.JumpResolution` 只验证返回码，不验证 jump operand 是否真的被回填

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBytecodeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Bytecode.JumpResolution` |
| 行号范围 | 94-114 |
| 问题描述 | 用例构造 `JMP -> Label(1)` 后，只断言 `ResolveJumpAddresses()` 返回 `0`。它没有读取 bytecode buffer、没有检查 `asBC_JMP` 的 dword operand 是否从占位 label 变成真实跳转偏移，也没有验证跳转目标对应的是正确的 instruction index。 |
| 影响 | 如果 `ResolveJumpAddresses()` 意外退化成 no-op 但仍返回成功，这个用例会继续绿灯，无法保护 bytecode 修补阶段最核心的地址解析逻辑。 |
| 修复建议 | 在解析前后都导出 bytecode：1）解析前确认第二个 dword 仍是 label id；2）调用 `ResolveJumpAddresses()` 后检查同一 operand 已替换为非 label 的实际跳转位移/地址；3）若 `asCByteCode` 提供 helper，可同时断言解析后的跳转落点对应 `Label(1)` 后第一条有效指令。 |

#### Issue-25：`Compiler.VariableScopes` 没有验证作用域边界，只验证“至少有两个局部变量”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Compiler.VariableScopes` |
| 行号范围 | 63-93 |
| 问题描述 | 用例脚本故意构造了 `{ int Inner = 2; }` 这种内层 block，但断言只有 `Function->GetVarCount() >= 2` 和“第一个变量名非空”。它没有确认两个变量分别是 `Outer` / `Inner`，没有验证 `Inner` 的声明范围只覆盖 block 内，也没有检查 debug info / var offsets 是否随着作用域结束而收缩。 |
| 影响 | 如果编译器把 inner 变量错误地提升到外层、调试变量表次序错乱，甚至把两个局部都登记成同一个符号，这个测试都可能继续通过，无法保护真正的 scope bookkeeping。 |
| 修复建议 | 把断言提升到可定位的 scope 级别：1）显式读取两个变量名并断言分别为 `Outer`、`Inner`；2）如果 API 可见，验证 `Inner` 的作用域结束位置晚于声明且早于函数结束；3）至少追加一个 negative snippet，例如在 block 外访问 `Inner` 必须编译失败，形成“有记录且边界正确”的闭环。 |

#### Issue-26：`Interface.CppInterface` 测试名暗示桥接验证，但实际只检查实现标记

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.CppInterface` |
| 行号范围 | 418-478 |
| 问题描述 | 脚本类 `AScenarioInterfaceCppBase` 定义了 `WorkDone` 属性和 `DoWork()`，但测试仅断言 `Actor->GetClass()->ImplementsInterface(InterfaceClass)`。它既没有通过 C++ 调用 `DoWork()`，也没有读取 `WorkDone`，甚至脚本里的 `UICppTestInterface` 本身也是脚本声明，不存在任何真正的 C++/script 桥接行为验证。 |
| 影响 | 这个用例的名称会让人误以为“接口可被 C++ 侧调用”的路径已经覆盖；实际上只要类标记还在，bridge/dispath 回归都会被漏掉。 |
| 修复建议 | 1）如果目标是验证 script interface 被 C++ 正常调用，就在测试里通过 `FindFunction` / `Execute_` 或反射调用 `DoWork()`，随后断言 `WorkDone == 1`；2）如果目标只是声明/实现反射，请重命名为更准确的 `Interface.Reflection` 类测试名；3）更推荐把真正的 C++ bridge 验证并入此用例，避免继续保留误导性命名。 |

### 二、需要新增的测试

#### NewTest-8：补 `while` + `break` + `continue` + `if/else` 嵌套控制流

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp` |
| 关联函数 | `asCParser::ParseStatement` / `asCBuilder::CompileFunction` |
| 现有测试覆盖 | `ControlFlowTests` 只有 `for`、`switch`、ternary condition，没有 `while`、`break`、`continue` 与嵌套 `if/else` 的组合覆盖 |
| 风险评估 | 控制流 lowering 在循环回跳、continue 目标块和 break 退出块上很容易回归；现有套件无法发现这些路径的跳转错误。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.WhileBreakContinueNested` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 场景描述 | 编译并执行一个包含 `while` 循环、奇偶分支、`continue` 跳过、`break` 提前退出的脚本入口。 |
| 输入/前置 | 脚本示例：`int Run() { int Index = 0; int Hits = 0; int Sum = 0; while (Index < 6) { ++Index; if ((Index % 2) == 0) { continue; } else { Sum += Index; ++Hits; } if (Index >= 5) { break; } } return Hits * 100 + Sum; }`。 |
| 期望行为 | 编译成功并执行返回 `309`，从而同时证明 `continue` 跳过 `2/4`，`break` 在 `5` 处生效，且 `if/else` 嵌套没有破坏累计逻辑。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-9：补真正的 script `typedef`/alias 语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp` |
| 关联函数 | `asCBuilder::CompileFunction` |
| 现有测试覆盖 | `Angelscript.TestModule.Angelscript.Types.Int64AndTypedef` 名称包含 `Typedef`，但脚本实际上完全没有 typedef 声明 |
| 风险评估 | type alias 解析、函数参数/返回值中的 alias 展开一旦回归，当前 `TypeTests` 不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.TypedefAliasArithmetic` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 声明 `typedef int64 Score;`，让 alias 同时出现在局部变量、函数参数和返回值里，再执行运算。 |
| 输入/前置 | 脚本示例：`typedef int64 Score; Score Add(Score A, Score B) { return A + B; } int Run() { Score Base = 1; Base <<= 9; return int(Add(Base, 5)); }`。 |
| 期望行为 | 编译成功并执行返回 `517`；若允许额外反射断言，可再验证 `GetFunctionByDecl("Score Add(Score, Score)")` 可被解析到正确符号。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` 或 `BuildModule + GetFunctionByDecl` |
| 优先级 | P1 |

#### NewTest-10：覆盖 `FullReloadSuggested` 路径，验证代码已切换但新增 metadata 仍排队等待 full reload

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | 有 `Interface.HotReload` 和上一轮记录的 `FullReloadRequired` 建议，但没有覆盖 `FullReloadSuggested` 这条中间路径 |
| 风险评估 | soft reload during PIE 可能出现“旧函数体/新元数据”或“新函数体已生效但新增 `UFUNCTION()` 丢失”的半更新状态；没有专门测试很难定位。 |
| 建议测试名 | `Angelscript.TestModule.Interface.HotReload.FullReloadSuggestedQueuesMetadata` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceLifecycleTests.cpp` |
| 场景描述 | V1 编译一个实现接口的 actor，现有方法 `GetValue()` 返回 `1`；V2 将 `GetValue()` 改成 `2`，并新增一个 `UFUNCTION() int GetBonus() { return 7; }`，在 `SoftReloadOnly` 下重新编译。 |
| 输入/前置 | 需要 scenario helper 负责编译、spawn actor、调用已存在方法并查找新增 `UFunction`。 |
| 期望行为 | soft reload 阶段编译成功且结果为 `PartiallyHandled` 或带 warning；新实例调用旧方法时返回 `2`，证明代码热替换生效；`FindGeneratedFunction(..., "GetBonus")` 在 soft reload 后仍为空；随后执行 full reload，再断言 `GetBonus` 可见并返回 `7`。 |
| 使用的 Helper | `CompileModuleWithSummary` + `CompileScriptModule` + `FActorTestSpawner` + `FindGeneratedFunction` |
| 优先级 | P1 |

#### NewTest-11：直接验证 `RegisterInterfaceMethodSignature` / `ReleaseInterfaceMethodSignature` 的生命周期

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::RegisterInterfaceMethodSignature`、`FAngelscriptEngine::ReleaseInterfaceMethodSignature` |
| 现有测试覆盖 | `InterfaceNativeTests` 只间接注册 signature，没有任何断言验证数组增长、定点释放和 `nullptr` guard |
| 风险评估 | 若签名对象泄漏、释放错删或重复释放后留下悬空 user data，会影响 native interface dispatch 的长期稳定性。 |
| 建议测试名 | `Angelscript.TestModule.Interface.Native.SignatureRegistrationRelease` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeLifecycleTests.cpp` |
| 场景描述 | 在 fresh/clean test engine 中注册两个不同 `FName` 的 interface method signature，随后只释放其中一个，再检查剩余项；最后释放第二个并验证空数组。 |
| 输入/前置 | 需要一个 test-local whitebox accessor 读取 `InterfaceMethodSignatures.Num()` 与剩余条目的 `FunctionName`；同时调用一次 `ReleaseInterfaceMethodSignature(nullptr)` 作为 guard。 |
| 期望行为 | 数量按 `0 -> 2 -> 1 -> 1 -> 0` 变化；释放第一个后第二个 signature 仍保留且 `FunctionName` 正确；传 `nullptr` 不改变数量也不崩溃。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `FAngelscriptEngineScope` + test accessor helper |
| 优先级 | P2 |

#### NewTest-12：补 `GC` 两节点环引用收集，而不是只测自环

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptEngine::GarbageCollect`、`asIScriptEngine::GetGCStatistics` |
| 现有测试覆盖 | `GCInternalTests` 只覆盖单节点自环；当前 helper 里虽然有 `CreateTwoNodeCycle`，但并没有被任何测试使用 |
| 风险评估 | GC 对多节点环的 detect/destroy 次序与自环不同；若枚举/释放顺序有 bug，只测自环会漏掉真实场景。 |
| 建议测试名 | `Angelscript.TestModule.Internals.GC.TwoNodeCycleCollection` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 场景描述 | 创建 `A <-> B` 的双向引用环，释放所有外部引用后先做 detect-only，再做 full collect。 |
| 输入/前置 | 复用现有 `RegisterGCProbeType`，但补一个能返回两个外部指针或自动平衡外部 `Release()` 的 helper，避免 `B` 的外部引用泄漏。 |
| 期望行为 | `TotalDetected` 至少递增一次；full collect 后 `TotalDestroyed` 增加且 `FGCProbeObject::LiveCount == 0`；`CurrentSize` 不再保留这两个 probe object。 |
| 使用的 Helper | 现有 `RegisterGCProbeType` + 新增 cycle helper + `RunFullGarbageCollection` |
| 优先级 | P1 |

#### NewTest-13：补 `Type` 注册表 alias / property finder 的直接单测

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptType::RegisterAlias`、`FAngelscriptType::GetByAngelscriptTypeName`、`FAngelscriptType::GetByProperty` |
| 现有测试覆盖 | 只有 `FromTypeId` 有建议覆盖，alias 注册和 property finder 分支没有任何直接测试 |
| 风险评估 | alias 或 type finder 退化会直接影响绑定系统对属性类型的解析，问题通常表现为“脚本里某些属性突然找不到类型”，定位成本很高。 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeRegistry.AliasAndPropertyFinder` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeRegistryTests.cpp` |
| 场景描述 | 取一个已注册的 baseline type，给它注册 alias，然后用 test-only property finder 把某个 fixture property 映射到该 alias。 |
| 输入/前置 | 需要一个轻量 `USTRUCT`/`UObject` fixture 暴露单个 property，以及一个 `FAngelscriptTypeDatabaseScope` 类 helper 负责测试前后 snapshot/restore type database。 |
| 期望行为 | `RegisterAlias("ScoreAlias", Type)` 后，`GetByAngelscriptTypeName("ScoreAlias")` 返回与 baseline 同一个 type；`GetByProperty(Property, true)` 命中 finder 返回 alias 对应 type；`GetByProperty(Property, false)` 不应走 finder 分支。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + 新增 `FAngelscriptTypeDatabaseScope` + property fixture helper |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 8 | Issue-20 |
| BadIsolation | 1 | Issue-18 |
| WrongHelper | 1 | Issue-21 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | MissingScenario: 3, MissingEdgeCase: 1 |
| P2 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-08 13:41)

### 一、现有测试问题

#### Issue-27：`Upgrade.MessageCallback` 在 shared engine 上注册全局回调后没有恢复

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Upgrade.MessageCallback` |
| 行号范围 | 204-245 |
| 问题描述 | 用例通过 `ASTEST_CREATE_ENGINE_SHARE()` 取得进程级共享引擎，并在 `ScriptEngine->SetMessageCallback(...)` 后直接结束测试，没有恢复之前的 callback，也没有切到 `SHARE_CLEAN` / `FULL` 隔离模式。`ASTEST_BEGIN_SHARE` 只建立 `FAngelscriptEngineScope`，不会自动回滚引擎级 callback 状态。 |
| 影响 | 后续在同一 shared engine 上运行的编译或诊断测试可能继续命中这个临时 callback，造成额外 side effect、日志路由变化或顺序依赖，属于典型测试间状态泄漏。 |
| 修复建议 | 1）最直接的修复是把该用例改为 `ASTEST_CREATE_ENGINE_FULL()` 或 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；2）若必须复用 shared engine，则在设置前保存旧 callback，并在 `ON_SCOPE_EXIT` 中恢复；3）额外把全局捕获变量 `GUpgradeMessage*` 的 reset 也放进 `ON_SCOPE_EXIT`，避免失败路径残留脏状态。 |

#### Issue-28：`Upgrade.RegisterObjectTypeFlags` 在共享引擎里注册固定类型名，没有任何清理

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Upgrade.RegisterObjectTypeFlags` |
| 行号范围 | 248-278 |
| 问题描述 | 用例同样使用 `ASTEST_CREATE_ENGINE_SHARE()`，然后以固定名字 `FUpgradeEditorOnlyRegisteredType` 调用 `RegisterObjectType(...)`。该注册写入的是引擎全局 type system，不会随着 `ASTEST_END_SHARE` 自动移除；下次运行此用例或任何同进程复用 shared engine 的相关测试时，重复注册同名类型就可能直接失败。 |
| 影响 | 测试结果依赖执行历史：第一次运行可能通过，二次运行或与其他注册同名 type 的测试交错时会出现假失败，属于可重复性很差的隔离错误。 |
| 修复建议 | 1）优先改成 `ASTEST_CREATE_ENGINE_FULL()` 或 `ASTEST_CREATE_ENGINE_NATIVE()`，让注册生命周期只存在于当前测试；2）若坚持 shared engine，至少改用唯一 type 名并在测试结束时重建 clean engine；3）补一条断言验证重复执行同一注册不会污染后续测试，而不是依赖当前进程“首次执行”。 |

#### Issue-29：`Tokenizer.ErrorRecovery` 只测错误 token 分类，没有验证 recovery

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Tokenizer.ErrorRecovery` |
| 行号范围 | 80-87 |
| 问题描述 | 用例只分别调用两次 `GetToken`，断言未终止字符串返回 `ttNonTerminatedStringConstant`、反引号返回 `ttUnrecognizedToken`。它没有检查 `TokenLength` 是否前进到正确位置，也没有在错误 token 之后继续解析后续输入，因此没有覆盖“recovery”本身。 |
| 影响 | 如果 tokenizer 遇到错误 token 时不推进游标、返回长度错误或无法继续扫描后续 token，这个用例仍会绿灯，无法保护错误恢复逻辑。 |
| 修复建议 | 把 malformed input 改成连续流式片段，例如 ``"` int Value"` `` 或 `"\"unterminated\nint Value"`：1）先断言第一个 token 类型与 `TokenLength`；2）再用剩余 buffer 调第二次 `GetToken`，断言后续 `int` / `Identifier` 仍能被正确识别；3）保留当前 token-type 断言，但补齐长度和后续 token 检查，才配得上 `ErrorRecovery` 名称。 |

#### Issue-30：`Tokenizer.CommentsAndStrings` 不验证 token 长度，无法发现吞词边界错误

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Tokenizer.CommentsAndStrings` |
| 行号范围 | 69-77 |
| 问题描述 | 用例分别断言 single-line comment、multi-line comment 和 string literal 的 token type，但三次调用都没有验证 `TokenLength`。如果 tokenizer 正确返回了 `ttOnelineComment` / `ttMultilineComment` / `ttStringConstant`，却把长度算短或算长，当前测试不会失败。 |
| 影响 | comment/string token 的边界计算直接决定 parser 从哪里继续读；长度错误会导致后续 token 错位，但这个用例会把“分类正确但跨度错误”的高风险回归放过去。 |
| 修复建议 | 1）为三类输入都补 `TokenLength` 断言，至少覆盖 `// hello\n`、`/* hi */` 和带转义字符的 string；2）对 single-line comment 额外断言长度包含换行终止符或明确符合当前 tokenizer 约定；3）把长度检查和后续 token 续扫组合起来，避免只测“看上去像哪类 token”。 |

### 二、需要新增的测试

#### NewTest-14：补 `asIScriptContext` 的 `Abort` / `Suspend` / `Unprepare` / reuse 生命周期

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Unprepare`、`asIScriptContext::Abort`、`asIScriptContext::Suspend`、`asIScriptContext::GetState` |
| 现有测试覆盖 | `AngelscriptExecutionTests.cpp` 只覆盖单次 `Prepare -> Execute -> Finished` happy path，没有覆盖 not-prepared guard、`Unprepare` 后状态恢复和同一 context 的二次复用 |
| 风险评估 | context 生命周期一旦回归，最常见的后果是脚本上下文复用后残留旧函数状态、错误码不对或第二次执行直接崩溃；当前套件无法提前发现。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.ContextLifecycleReuse` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionContextLifecycleTests.cpp` |
| 场景描述 | 编译一个同时包含 `int First()` 与 `int Second()` 的模块，手动驱动同一个 `asIScriptContext` 先走 guard path，再完成一次执行、`Unprepare`，最后复用到第二个函数。 |
| 输入/前置 | 1）创建 context 后立即调用 `Abort()` 和 `Suspend()`；2）`Prepare(First)`、`Execute()`；3）调用 `Unprepare()`；4）再次 `Prepare(Second)`、`Execute()`。脚本可为 `int First() { return 1; } int Second() { return 2; }`。 |
| 期望行为 | `Abort()` 与 `Suspend()` 在未准备状态下都返回 `asCONTEXT_NOT_PREPARED`；第一次执行后 `GetState() == asEXECUTION_FINISHED` 且返回 `1`；`Unprepare()` 返回 `asSUCCESS` 且状态回到 `asEXECUTION_UNINITIALIZED`；第二次执行成功返回 `2`。 |
| 使用的 Helper | `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` |
| 优先级 | P1 |

#### NewTest-15：补可变 global state 在多次执行间的持久化语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptModule::GetAddressOfGlobalVar`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `Angelscript.TestModule.Angelscript.Core.GlobalState` 只读 `const int g_Count`，没有覆盖可写 global 在多次函数调用间的累积状态 |
| 风险评估 | global storage 初始化、写回或二次执行复用一旦回归，脚本会表现成“每次调用都像首次启动”或写入丢失；当前核心执行测试看不到这个问题。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Core.MutableGlobalStateAcrossCalls` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 场景描述 | 编译一个带可变全局变量的模块，对同一个模块连续执行两次增量函数，再读取全局变量。 |
| 输入/前置 | 脚本示例：`int g_Count = 0; int Increment() { g_Count += 1; return g_Count; } int Read() { return g_Count; }`。测试内依次执行 `Increment()`、`Increment()`、`Read()`；若需要额外白盒验证，可读取第 0 个 global 的地址。 |
| 期望行为 | 第一次执行返回 `1`，第二次返回 `2`，`Read()` 也返回 `2`；如果读取 global 地址，其值同样应为 `2`，证明状态保存在模块而不是临时 context。 |
| 使用的 Helper | `BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-16：补 `UInterface` 持久引用在目标销毁并 GC 后的失效语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CanCastScriptObjectToUnrealInterface` |
| 现有测试覆盖 | `Interface.GCSafe` 只验证实现接口的 actor 会被 GC 回收，没有验证保存下来的 interface ref 在目标销毁后是否自动失效 |
| 风险评估 | 这是 `UInterface` 生命周期最危险的路径之一；如果保存的 interface wrapper 在目标销毁后仍保持悬空引用，后续 cast/dispatch 很容易 crash，而当前套件完全没有证据。 |
| 建议测试名 | `Angelscript.TestModule.Interface.GCSafe.SavedReferenceClearsAfterGC` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceLifecycleTests.cpp` |
| 场景描述 | 在同一脚本模块中定义 `ATarget` 实现接口、`AHolder` 保存一个 interface ref。测试先让 holder 捕获 target 的 interface 引用，再销毁 target 并执行 GC，最后由 holder 检查保存的 interface ref 是否自动变为 `nullptr`。 |
| 输入/前置 | 脚本建议包含：1）`UINTERFACE()` `UIDamageableGCRef`；2）`ATargetDamageableGCRef : AActor, UIDamageableGCRef`；3）`AHolderDamageableGCRef : AActor`，带 `UIDamageableGCRef SavedRef`、`int BeforeGCValid`、`int AfterGCNull`，以及 `UFUNCTION() void Capture(UObject Target)`、`UFUNCTION() void ProbeAfterGC()`。测试用 `ProcessEvent` 或轻量反射 helper 调 `Capture` / `ProbeAfterGC`。 |
| 期望行为 | `Capture` 后 `BeforeGCValid == 1`；销毁 target、`TickWorld`、`CollectGarbage` 后，`WeakTarget` 失效且 `ProbeAfterGC` 把 `AfterGCNull` 置为 `1`；整个过程不应崩溃，也不应还能通过 `SavedRef` 成功调用接口方法。 |
| 使用的 Helper | `CompileScriptModule` + `FActorTestSpawner` + `TickWorld` + `ReadPropertyValue` + `ProcessEvent`/反射调用 helper |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-29 |
| BadIsolation | 1 | Issue-28 |
| MissingCleanup | 1 | Issue-27 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingEdgeCase: 1, MissingScenario: 2 |

---

## 测试审查 (2026-04-08 14:00)

### 一、现有测试问题

#### Issue-31：`InterfaceNativeTests` 向持久 shared engine 注册 interface signature，但没有释放或重置

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.NativeImplement`、`NativeInheritedImplement`、`NativeReferenceRoundTrip` |
| 行号范围 | 41-84, 102-434 |
| 问题描述 | `BindNativeInterfaceMethod()` 通过 `FAngelscriptEngine::Get().RegisterInterfaceMethodSignature(...)` 为每个 native interface 方法分配 `FInterfaceMethodSignature`，但整个文件没有任何地方调用 `ReleaseInterfaceMethodSignature()`。三个用例结束时只做 `Engine.DiscardModule(...)` 和 `ResetSharedCloneEngine(Engine)`；而 `ResetSharedCloneEngine` 仅丢弃模块，不会清空 `InterfaceMethodSignatures`，真正清空发生在 `FAngelscriptEngine` 销毁路径（`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 1198-1275）。这意味着测试会把 interface signature 永久留在 shared clone engine 上。 |
| 影响 | 重复运行该文件或在同一进程内多轮执行时，会累积悬挂的 signature state，增加内存泄漏和顺序依赖风险；一旦后续 native interface 测试对白盒数量、user data 或绑定顺序做断言，这里的残留状态会直接制造假失败。 |
| 修复建议 | 1）把 `RegisterInterfaceMethodSignature` 返回的指针集中保存到 test-local 容器，并在 `ON_SCOPE_EXIT` 中逐个调用 `FAngelscriptEngine::Get().ReleaseInterfaceMethodSignature(...)`；2）若 `ReferenceClass` 绑定后的生命周期确实要求签名与 type 同步存在，则应把绑定与释放封装成专用 fixture，在 fixture teardown 中同时清理 type 和 signature；3）补一条回归断言，验证同一测试重复执行前后 `InterfaceMethodSignatures.Num()` 不增长。 |

#### Issue-32：`SwitchAndConditional` 只跑到一个 `switch case` 和一个 ternary 分支，无法证明控制流覆盖

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.SwitchAndConditional` |
| 行号范围 | 51-70 |
| 问题描述 | 用例脚本固定执行 `Pick(1)`，随后只走 `Base > 3 ? Base + 3 : Base - 1` 的 true 分支，最终断言结果为 `7`。这意味着 `switch` 的 `case 0`、`default`，以及条件表达式的 false 分支完全没有被观察；而单个汇总值 `7` 也不能区分到底是 `Pick(1)`、比较表达式还是 ternary lowering 出了问题。 |
| 影响 | `switch` case table/default block、比较表达式或 ternary false-path 发生回归时，该测试仍可能绿灯，尤其不符合本区域“每个语言特性都要有精确断言”的要求。 |
| 修复建议 | 1）把 `switch` 和 conditional 拆成两个独立用例，避免一个测试同时承载两种控制流；2）若暂不拆分，至少在同一脚本里分别执行 `Pick(0)`、`Pick(1)`、`Pick(9)`，并同时覆盖 `Base > 3` 为 true/false 的两条路径；3）断言不要只看单个汇总值，改成编码多段结果或多个 helper 函数分别验证每个分支。 |

#### Issue-33：`Compiler.BytecodeGeneration` 只检查“生成了非空 buffer”，没有验证生成的 bytecode 是否正确

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Compiler.BytecodeGeneration` |
| 行号范围 | 32-61 |
| 问题描述 | 用例编译 `int Entry() { int A = 1; int B = 2; return A + B; }` 后，只断言 `GetByteCode()` 返回非空指针且 `BytecodeLength > 0`。它既没有执行 `Entry()`，也没有检查生成的 opcode/operand 中是否真的包含局部变量初始化、加法和返回指令，因此任何“只要不是空 bytecode”的错误产物都会被视为通过。 |
| 影响 | 编译器如果把算术表达式编错、截断指令流、丢失 `return` 或退化成错误但非空的 bytecode，这个用例都拦不住，会让 `Compiler.BytecodeGeneration` 这个测试名严重高估实际保护强度。 |
| 修复建议 | 1）先执行编译出的函数并断言返回 `3`，建立最基本的语义闭环；2）再读取 bytecode buffer，验证至少存在 load/const、算术和 `RET` 相关 opcode，而不是仅验证“长度大于 0”；3）如果 opcode 细节易变，可退一步对比同类 baseline 脚本的最小长度或关键 opcode 集合，但必须保留行为断言。 |

#### Issue-34：`Core.Optimize` 只验证运行结果，没有验证优化路径本身是否生效

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Core.Optimize` |
| 行号范围 | 215-261 |
| 问题描述 | 用例分别编译 `1 + 2 + 3` 和 `return Value; Value = 2;` 两个典型优化场景，再执行并断言结果为 `6` 与 `1`。这只能证明“优化前后的语义没有被破坏”，却没有任何证据表明 constant folding 或 dead-code elimination 真正发生了；即使优化器完全停用、编译器保留全部原始指令流，该测试仍会通过。 |
| 影响 | 优化相关回归会被降级成普通执行测试，无法发现 optimizer 被意外绕过、dead code 没有修剪或常量折叠退化等问题；对于这个以 `Optimize` 命名的用例来说，信号强度明显不足。 |
| 修复建议 | 1）在保持行为断言的同时，读取 `int Test()` 的 bytecode，检查 constant case 的指令序列不再包含完整的逐步加法链；2）对 dead-code case 验证 `return` 之后没有可达 opcode，或至少不存在对应第二次赋值的指令模式；3）如果 bytecode 细节不稳定，就引入 baseline 对比脚本，断言优化版长度显著短于未优化版。 |

### 二、需要新增的测试

#### NewTest-17：补 `switch case/default` 与 ternary false-path 的分支矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Angelscript.TestModule.Angelscript.ControlFlow.SwitchAndConditional` 只执行 `Pick(1)` 和 `Base > 3` 的 true 分支 |
| 风险评估 | `switch` 的 `case 0` / `default` 或 ternary false-path 一旦回归，当前控制流套件不会报警，尤其不利于定位分支 lowering 错误。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.SwitchDefaultAndConditionalMatrix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 场景描述 | 用同一模块分别执行 `switch` 的 `case 0`、`case 1`、`default`，并同时覆盖条件表达式的 true/false 两条路径。 |
| 输入/前置 | 脚本示例：`int Pick(int Value) { switch (Value) { case 0: return 2; case 1: return 4; default: return 6; } } int Conditional(int Base) { return Base > 3 ? Base + 3 : Base - 1; } int Run() { return Pick(0) * 10000 + Pick(1) * 1000 + Pick(9) * 100 + Conditional(4) * 10 + Conditional(2); }`。 |
| 期望行为 | 编译成功并执行返回 `24671`，从而同时证明 `case 0`、`case 1`、`default`、ternary true-path 与 false-path 都命中了预期结果。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-18：补编译器 bytecode 生成的“执行结果 + 末尾 `RET`”双重断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptFunction::GetByteCode` |
| 现有测试覆盖 | `Compiler.BytecodeGeneration` 只验证 bytecode buffer 非空，没有验证执行结果和基本指令边界 |
| 风险评估 | 编译器如果输出了错误但非空的 bytecode，当前内部测试会误报绿灯；这类回归通常直到更高层运行时才暴露，定位成本高。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Compiler.BytecodeExecutionAndRetBoundary` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 场景描述 | 编译一个带参数的算术函数，先用真实 context 执行，再读取 `GetByteCode()` 检查指令流边界。 |
| 输入/前置 | 脚本示例：`int Entry(int A) { int B = 2; return A + B; }`。测试内用 `Engine.CreateContext()` 设置参数 `A = 1`，随后读取 `BytecodeLength` 与首尾 opcode。 |
| 期望行为 | `Prepare/Execute` 成功且返回 `3`；`BytecodeLength > 1`；首条 opcode 不是 `asBC_RET`，最后一条有效 opcode 为 `asBC_RET`，证明函数体不是“空壳 bytecode”。 |
| 使用的 Helper | `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` |
| 优先级 | P1 |

#### NewTest-19：补 `SetArgQWord` / `GetReturnQWord` 的 int64 高位与符号位 round-trip

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::SetArgQWord`、`asIScriptContext::GetReturnQWord` |
| 现有测试覆盖 | `Execution.MixedArgs` 只有在 `float` 走 `float64` 模式时才间接使用 `SetArgQWord`，没有任何针对 `int64` / `uint64` 的专门覆盖 |
| 风险评估 | 64-bit 参数 marshaling 一旦出现高位截断、符号位处理错误或字节序问题，现有 `ExecutionTests` 很难直接暴露，尤其在 Windows/不同 ABI 路径上风险较高。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.Int64QWordRoundTrip` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 场景描述 | 编译两个 `int64` 函数，分别验证大正数加法和负数取反，再通过 `SetArgQWord` / `GetReturnQWord` 手动驱动 context。 |
| 输入/前置 | 脚本示例：`int64 AddFive(int64 Value) { return Value + 5; } int64 Negate(int64 Value) { return -Value; }`。测试输入至少覆盖 `1099511627776` 和 `-7` 两组值，并用 `FMemory::Memcpy` 在 `asQWORD` 与 `int64` 之间转码。 |
| 期望行为 | `AddFive(1099511627776)` 返回 `1099511627781`；`Negate(-7)` 返回 `7`；两次调用都应得到 `asEXECUTION_FINISHED`，证明 high bits 与 sign bits 都被完整保留。 |
| 使用的 Helper | `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` |
| 优先级 | P1 |

#### NewTest-20：补脚本接口继承链在 C++ 反射侧的 `ProcessEvent` 调度

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CanCastScriptObjectToUnrealInterface` |
| 现有测试覆盖 | `InheritedMethodDispatch` 和 `MultipleInheritanceDispatch` 只验证脚本侧 `Cast<Interface>` + 方法调用；C++/UE reflection 侧对继承链 parent/leaf 方法的调度没有直接测试 |
| 风险评估 | 脚本内部调用即使正常，若生成的 `UFunction` 没有正确挂到继承链或 UE 侧 `ProcessEvent` 路由错误，游戏代码从 C++ 调用这些接口时仍可能失效，而当前套件看不到。 |
| 建议测试名 | `Angelscript.TestModule.Interface.Hierarchy.ProcessEventDispatch` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceLifecycleTests.cpp` |
| 场景描述 | 编译一个三层脚本接口链和实现 leaf 接口的 actor，从 C++ 侧查找 parent/leaf 生成函数并用 `ProcessEvent` 触发。 |
| 输入/前置 | 脚本建议定义 `UIBaseDispatchProcess`、`UIMidDispatchProcess`、`UILeafDispatchProcess` 三层接口，以及 actor 上的 `BaseCalled` / `LeafCalled` 属性；方法可设计成无参 `BasePing()` / `LeafPing()`，由 `ProcessEvent` 调用后分别把属性置为 `1`。 |
| 期望行为 | `FindGeneratedFunction(ScriptClass, "BasePing")` 与 `FindGeneratedFunction(ScriptClass, "LeafPing")` 都能找到函数；`ProcessEvent` 后 `BaseCalled == 1`、`LeafCalled == 1`；同时 `ScriptClass` 继续对 base/mid/leaf 三层接口都返回 `ImplementsInterface == true`。 |
| 使用的 Helper | `CompileScriptModule` + `FActorTestSpawner` + `FindGeneratedFunction` + `ProcessEvent` + `ReadPropertyValue` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-32 |
| MissingCleanup | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | MissingScenario: 3, MissingEdgeCase: 1 |
---

## 测试审查 (2026-04-08 17:30)

### 一、现有测试问题

#### Issue-35：`Interface.MethodCall` 只证明 dispatch 发生，没有验证接口参数是否正确传入

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.MethodCall` |
| 行号范围 | 197-260 |
| 问题描述 | 脚本里的 `TakeDamage(float Amount)` 完全忽略 `Amount`，只把 `MethodCalled` 置为 `1`；测试末尾也只断言 `CastSucceeded == 1` 和 `MethodCalled == 1`。这只能证明通过 interface ref 触发到了某个函数体，却不能证明 `Casted.TakeDamage(42.0)` 的实参被正确 marshaling 到实现函数。只要 thunk 路由到了目标方法，即使参数被截断、默认成 `0` 或被错误重排，该用例仍会通过。 |
| 影响 | `UInterface` 调度链里最容易回归的参数传递路径目前没有保护，特别是 float 参数 ABI / thunk 生成错误会被误报成绿灯。 |
| 修复建议 | 把脚本 actor 补成可观察参数值的形态：1）新增 `UPROPERTY() float LastDamage = -1.0f;`；2）在 `TakeDamage(float Amount)` 里同时设置 `MethodCalled = 1; LastDamage = Amount;`；3）测试末尾使用 `ReadPropertyValue<FFloatProperty>` 读取 `LastDamage` 并以容差断言它等于 `42.0f`；4）如果要进一步覆盖参数写回，可再追加一个返回值或第二次调用，验证连续 dispatch 不串值。 |

#### Issue-36：`Execution` 多参数用例使用可交换表达式，无法发现参数槽位错位

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.TwoArgs`、`Angelscript.TestModule.Angelscript.Execute.FourArgs`、`Angelscript.TestModule.Angelscript.Execute.MixedArgs` |
| 行号范围 | 129-224, 247-314 |
| 问题描述 | 这三例都把重点放在 `A + B`、`A + B + C + D`、`A + B + C` 这类可交换表达式上；其中 `FourArgs` 甚至把前三个参数都设成 `10`。结果是参数编组如果把多个 `DWord` 槽位顺序弄错，甚至把第 0/1/2 个参数互换，测试依然可能得到同样的 `42` / `42.5`。`MixedArgs` 也无法证明 `SetArgDWord(0, 10)` 与 `SetArgDWord(2, 12)` 真正落到了对应形参，因为它们最终只是参与求和。 |
| 影响 | 这组测试名会让人以为不同参数槽位、mixed calling convention 和返回值封送都已覆盖，但实际对“顺序是否正确”几乎没有保护；一旦 context 参数布局、register/stack 映射或 wrapper 代码把参数位置写错，当前用例仍可能误报绿灯。 |
| 修复建议 | 把断言改成对顺序敏感的表达式：1）`TwoArgs` 用 `return A * 100 + B;` 并断言 `2022`；2）`FourArgs` 用 `return A * 1000 + B * 100 + C * 10 + D;`，同时给四个槽位设置互不相同的值；3）`MixedArgs` 用非交换编码，例如 `return double(A * 1000 + C) + B;` 或拆成多个 helper 分别读取每个参数；4）保留现有 `Prepare/Execute` 断言，但不要再用求和型脚本做参数顺序测试。 |

#### Issue-37：`Types.FloatDebuggerFormatting` 只检查是否出现科学计数法标记，没有验证格式化后的数值内容

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.FloatDebuggerFormatting` |
| 行号范围 | 165-201 |
| 问题描述 | 用例把一个很小的浮点值喂给 `GetDebuggerValue()`，最后只断言 `DebugValue.Value.Contains("e") || Contains("E")`。这只能证明 formatter 输出里带了科学计数法字符，无法证明格式化后的数值仍然对应原始输入；即使实现把 `0.000000123456` 错写成 `1e+20`、`0e-0`，甚至输出一个只有 `e` 的畸形字符串，该测试也可能继续通过。 |
| 影响 | 浮点 debugger 展示层如果出现量级、符号或有效数字错误，当前测试无法报警；调试器里看到“看起来是科学计数法”的错误值也会被误判为正确。 |
| 修复建议 | 1）在保留“应使用科学计数法”断言的同时，增加对数值内容的验证，例如把 `DebugValue.Value` 解析回 `double` 并用容差比较原值；2）额外断言结果包含负指数特征（如 `e-`），避免把大数或错误量级误判为通过；3）若 formatter 的小数位数有平台差异，可改为验证解析值接近原始输入而不是硬编码完整字符串。 |

#### Issue-38：`Restore` 失败路径只验证“加载失败”，没有验证失败后模块保持干净状态

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Restore.EmptyStreamFails`、`Angelscript.TestModule.Internals.Restore.TruncatedStreamFails` |
| 行号范围 | 246-323 |
| 问题描述 | 两个 negative test 都只做了两件事：`AddExpectedErrorPlain("Unexpected end of file")`，然后断言 `LoadByteCode(...) != asSUCCESS`。它们没有锁定固定错误码，也没有检查失败后 `RestoredModule` 是否仍然是空模块、是否残留半恢复的 function/global/type 元数据。只要 loader 任何位置返回了一个非成功值，这两个用例就会通过。 |
| 影响 | `LoadByteCode` 一旦出现“报错了但留下半初始化模块”的回归，当前测试完全看不到；后续对同名模块再次加载或执行时才会暴露问题，定位成本高。 |
| 修复建议 | 1）在失败断言后追加模块状态检查，例如 `GetFunctionCount() == 0`、`GetGlobalVarCount() == 0`，并确认 `GetFunctionByDecl("int Test()") == nullptr`；2）如果当前实现对空流和截断流返回固定错误码，应把返回码锁死，而不是只判断“不等于 asSUCCESS”；3）可以在同一用例里追加一次“失败后重新创建/重新加载完整流成功”的回归断言，证明失败路径不会污染后续 restore。 |

#### Issue-39：`ExecutionTests.cpp` 已经超过单文件 500 行上限，执行语义与 context 生命周期耦在一起

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.*` 9 个用例 |
| 行号范围 | 1-533 |
| 问题描述 | 该文件同时承载 basic execute、单/双/四参数封送、mixed args、context state、nested call、discard、script range 求值等 9 个场景，总长度达到 533 行，已经超过规则要求的单文件 300-500 行。文件内部大量重复 `BuildModule -> GetFunctionByDecl -> CreateContext -> Prepare -> Execute -> Release` 模式，新增覆盖时很容易继续把不同责任堆进同一文件。 |
| 影响 | 文件继续增长会放大 review 成本和 merge 冲突，也让“参数封送问题”“context 生命周期问题”“模块管理问题”混在一起，后续补断言或补 negative path 时更难保持单文件单职责。 |
| 修复建议 | 按职责拆分：1）保留基础 happy-path 到 `AngelscriptExecutionBasicTests.cpp`；2）把 `OneArg/TwoArgs/FourArgs/MixedArgs` 收拢到 `AngelscriptExecutionArgumentMarshallingTests.cpp`；3）把 `Context/Nested/Discard` 收拢到 `AngelscriptExecutionLifecycleTests.cpp`；4）把重复的 prepare/execute/release 流程抽到 shared helper，确保每个文件控制在 300-500 行内。 |

### 二、需要新增的测试

#### NewTest-21：补参数槽位顺序敏感的 `Execute` 参数编组矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::SetArgDWord`、`asIScriptContext::SetArgFloat`、`asIScriptContext::SetArgQWord`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `Execution.TwoArgs`、`FourArgs`、`MixedArgs` 只覆盖 commutative 求和，不覆盖参数槽位顺序错误 |
| 风险评估 | 一旦 context 参数布局把多个参数写错槽位，现有 `42` / `42.5` 型断言可能继续绿灯，导致 ABI / wrapper 回归长期潜伏。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.ArgumentSlotOrderMatrix` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionArgumentMarshallingTests.cpp` |
| 场景描述 | 手动驱动 context，分别验证 2 参数、4 参数以及 int+float+int mixed 参数时，各槽位按顺序写入并按顺序读取。 |
| 输入/前置 | 脚本建议包含：`int Encode2(int A, int B) { return A * 100 + B; }`、`int Encode4(int A, int B, int C, int D) { return A * 1000 + B * 100 + C * 10 + D; }`，以及 `float64` 模式下 `int EncodeMixed(int A, double B, int C) { return A * 1000 + int(B * 10) + C; }` / `float32` 模式下对应 `float` 版本。测试输入使用互不相同的值，如 `20/22`、`1/2/3/4`、`7/2.5/9`。 |
| 期望行为 | `Encode2(20,22)` 返回 `2022`；`Encode4(1,2,3,4)` 返回 `1234`；`EncodeMixed(7,2.5,9)` 返回 `7034`；三条路径都必须得到 `asEXECUTION_FINISHED`。 |
| 使用的 Helper | `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` |
| 优先级 | P1 |

#### NewTest-22：补 `default arguments` 的显式覆盖与 named argument 组合路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `Functions.DefaultArguments` 只覆盖“省略最后一个参数”这一条 happy path；`NamedArguments` 也没有和 default arg 组合 |
| 风险评估 | default arg 绑定顺序、显式实参覆盖默认值、named argument 与默认参数混用一旦退化，当前套件不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Functions.DefaultArguments.OverrideAndNamedMix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 场景描述 | 编译一个带两个默认参数的函数，分别走“只传必选参数”“显式覆盖中间参数”“只命名覆盖尾参数”三条路径。 |
| 输入/前置 | 脚本示例：`int Format(int A, int B = 5, int C = 9) { return A * 100 + B * 10 + C; } int RunDefault() { return Format(1); } int RunOverride() { return Format(1, 2); } int RunNamedPartial() { return Format(A: 1, C: 3); }`。 |
| 期望行为 | `RunDefault()` 返回 `159`；`RunOverride()` 返回 `129`；`RunNamedPartial()` 返回 `153`。这三个断言共同证明默认值被填充、显式参数覆盖生效、named argument 不会错误覆盖未传的默认位。 |
| 使用的 Helper | `BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-23：补 `LoadByteCode` 失败后模块保持干净且可重试的回归测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptModule::SaveByteCode`、`asIScriptModule::LoadByteCode` |
| 现有测试覆盖 | `Restore.EmptyStreamFails`、`TruncatedStreamFails` 只覆盖“返回失败”，没有覆盖失败后的模块状态和重试能力 |
| 风险评估 | 如果 restore 失败后残留半初始化 state，后续同名模块重新加载或执行时可能崩溃；当前测试无法提前发现。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Restore.FailureLeavesModuleClean` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp` |
| 场景描述 | 先保存一份有效 bytecode，再用截断流触发一次失败加载，检查模块为空；随后用完整流重新加载并执行，验证失败路径没有污染后续 restore。 |
| 输入/前置 | 复用现有 `FMemoryBinaryStream`、`BuildRestoreModule`、`ExecuteRestoreFunction` helper。先构建 `const int GlobalValue = 41; int Test() { return GlobalValue + 1; }`，保存 bytecode；拷贝一份截断流给 `RestoredModule`，另一份完整流用于重试。 |
| 期望行为 | 截断流 `LoadByteCode` 返回失败；失败后 `RestoredModule->GetFunctionCount() == 0`、`GetGlobalVarCount() == 0`，且 `GetFunctionByDecl("int Test()") == nullptr`；随后重新创建模块并加载完整流成功，执行 `Test()` 返回 `42`。 |
| 使用的 Helper | `CreateIsolatedCloneEngine` + `FMemoryBinaryStream` + `CreateRestoreModule` + `ExecuteRestoreFunction` |
| 优先级 | P1 |

#### NewTest-24：补 `auto` 对非整数表达式的推导矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Types.Auto` 只覆盖 `auto Value = 42`，没有覆盖 enum、float/double 等非整数推导 |
| 风险评估 | `auto` 推导一旦退化成“总是 int”或在 float32/float64 分支上选错目标类型，当前类型套件完全看不到。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.AutoInferenceMatrix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 通过重载分派来验证 `auto` 推导出的真实类型，而不是只看最终数值。 |
| 输入/前置 | 在 `float64` 模式下脚本可为：`enum EKind { A, B } int Which(int) { return 1; } int Which(double) { return 2; } int Which(EKind) { return 3; } int Run() { auto IntValue = 42; auto FloatValue = 1.5; auto EnumValue = EKind::B; return Which(IntValue) * 100 + Which(FloatValue) * 10 + Which(EnumValue); }`；在 `float32` 模式下把 `double` 换成 `float` 并把字面量写成 `1.5f`。 |
| 期望行为 | `Run()` 返回 `123`，从而证明 `auto` 对 int、float/double、enum 三类表达式都推导到了正确重载，而不是退化成统一类型。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-36 |
| AntiPattern | 1 | Issue-39 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | MissingEdgeCase: 1, MissingScenario: 2, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-08 17:45)

### 一、现有测试问题

#### Issue-40：`Core.CreateEngine` 用硬编码版本号锁死上游依赖，属于脆弱断言

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Core.CreateEngine` |
| 行号范围 | 57-80 |
| 问题描述 | 用例最后直接断言 `ANGELSCRIPT_VERSION == 23300`。这把测试绑定到了一个 magic number，而不是绑定到 `CreateForTesting` 的可观察行为。只要上游 AngelScript 版本正常升级，哪怕 `FAngelscriptEngine` 创建逻辑完全正确，这个用例也会因为常量变化而失败；反过来，如果创建流程损坏但版本宏没变，该断言也提供不了额外信号。 |
| 影响 | 该用例会把“依赖版本升级”误报成运行时回归，增加无效红灯；同时占用了一个断言位却没有提升对 engine 创建语义的保护。 |
| 修复建议 | 去掉对固定字面量 `23300` 的断言，改为验证真正的创建语义：1）保留两个 wrapper/script engine 非空断言；2）若需要验证版本透传，改成比较 `ScriptEngineA->GetEngineProperty`/公开 API 返回值与 `ANGELSCRIPT_VERSION` 宏一致，而不是与 magic number 比较；3）把版本升级兼容性检查放到专门的升级兼容测试里，避免污染 `CreateEngine` 行为测试。 |
#### Issue-41：`Misc.MultiAssign` 的脚本无法区分赋值结合顺序，断言与测试名不匹配

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Misc.MultiAssign` |
| 行号范围 | 61-76 |
| 问题描述 | 用例脚本是 `A = B = C = 42; return A + B + C;`，最终只断言 `126`。这个脚本无论赋值是按右结合还是错误地按左结合、甚至把三个目标都最终写成同一个值，结果都仍然是 `126`。也就是说，测试名声称在验证“chained assignments from right to left”，但脚本本身并不能观测结合顺序。 |
| 影响 | 如果赋值表达式的求值顺序或中间返回值语义退化，该用例仍可能长期绿灯，误导后续审查认为多重赋值的语言规则已经被锁住。 |
| 修复建议 | 改成顺序敏感的脚本和断言，例如利用赋值表达式返回值：`int A = 1, B = 2, C = 3; int Result = (A = (B = (C = 4))); return A * 1000 + B * 100 + C * 10 + Result;`，期望返回 `4444`；或者显式验证 `A = (B = 4)` 的返回值再组合为多位编码，确保一旦结合方向错了就会得到不同结果。 |
#### Issue-42：`NativeScriptHotReload.Phase2C` 依赖磁盘上的真实脚本文件，存在隐式外部资源耦合

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2C` |
| 行号范围 | 239-255 |
| 问题描述 | `Phase2C` 不是像 `Phase2A/2B` 那样内联固定脚本文本，而是通过 `FFileHelper::LoadFileToString` 从 `Script/Tests/Test_ExampleActorFixture.as` 读取当前工作区文件内容，再把该内容送入 hot reload helper。这样一来，测试结果会随着外部脚本文件的日常演化而变化，而这些变化并不一定与 hot reload 行为本身有关。 |
| 影响 | 该用例会把“fixture 文件改动”“路径变更”“打包/工作目录差异”等环境问题混入 hot reload 结果，导致测试在不同机器或不同分支状态下出现非语义性失败，也让问题定位从“热重载逻辑”扩散到“某个生产脚本当前长什么样”。 |
| 修复建议 | 1）把 `Phase2C` 需要验证的最小脚本内联到测试文件，保证输入稳定；2）如果必须复用磁盘 fixture，至少在测试里锁定文件内容的关键行为断言，并把 fixture 复制到测试专用路径而不是直接消费共享脚本；3）额外断言读取到的脚本包含预期 class/function 标识，避免把“加载到别的内容”误当成 hot reload 回归。 |
#### Issue-43：`DeclareBasic` 只验证接口类存在，没有验证接口方法被生成到反射层

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceDeclareTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.DeclareBasic` |
| 行号范围 | 19-46 |
| 问题描述 | 用例编译 `UIDamageable` 后只断言 `InterfaceClass != nullptr` 和 `CLASS_Interface` flag。它没有检查 `TakeDamage(float Amount)` 对应的 `UFunction` 是否存在，也没有验证方法签名是否被正确挂到生成的接口类上。只要生成了一个空的 `UClass` 壳子，这个测试就会通过。 |
| 影响 | 如果 `UInterface` 生成流程回归成“类能建出来，但方法表丢失/签名不完整”，当前测试仍是绿灯；后续真正通过接口调用时才会暴露问题，定位会落到更后面的场景测试。 |
| 修复建议 | 在现有断言后追加反射级检查：1）`InterfaceClass->FindFunctionByName(TEXT("TakeDamage"))` 必须非空；2）进一步检查该 `UFunction` 的参数数量和 `float Amount` 参数类型；3）如果项目已有 helper，可抽成 `AssertGeneratedInterfaceMethod`，复用于 `DeclareInheritance` 和其他接口声明测试。 |
### 二、需要新增的测试

#### NewTest-25：补 `GetDebuggerValueFromFunction` 的 getter 求值与地址跟踪路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptType::GetDebuggerValueFromFunction` |
| 现有测试覆盖 | `AngelscriptTypeTests.cpp` 只直接调用 `FAngelscriptTypeUsage::GetDebuggerValue` 做 primitive float 格式化；没有任何测试直达函数求值、`bTemporaryValue`、`NonTemporaryAddress` / `AddressToMonitor` 这条调试器 getter 路径 |
| 风险评估 | 该函数是 debugger 自动求值的核心桥接层，若 getter 执行、world-context 参数注入、返回值拷贝或属性地址跟踪回归，当前 LanguageFeatures 套件完全看不到，调试器会在运行时显示错值或失去监视地址。 |
| 建议测试名 | `Angelscript.TestModule.Internals.DebuggerValue.GetterPropertyTracking` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDebuggerValueTests.cpp` |
| 场景描述 | 编译一个带 `UPROPERTY() int Health = 42;` 和 `int GetHealth() const { return Health; }` 的 script actor，直接调用 `FAngelscriptType::GetDebuggerValueFromFunction` 对 getter 求值，并要求它把调试值绑定回真实属性地址。 |
| 输入/前置 | 使用 `CompileScriptModule` + `FActorTestSpawner` 生成 actor；通过 `FAngelscriptType::GetBoundClassName(ScriptClass)` + `Engine.GetScriptEngine()->GetTypeInfoByName(...)` 拿到 script type，再用 `GetMethodByDecl("int GetHealth() const")` 取得 `asIScriptFunction*`；同时通过 `FindFProperty<FIntProperty>(ScriptClass, TEXT("Health"))` 取到 native property 地址。调用 `FAngelscriptType::GetDebuggerValueFromFunction(Function, Actor, DebugValue, ScriptType, ScriptClass, TEXT("Health"))`。 |
| 期望行为 | 调用返回 `true`；`DebugValue.Value` 能解析为 `42`；`DebugValue.bTemporaryValue == true`；`DebugValue.NonTemporaryAddress` 指向 `Health` 的真实地址，或 `AddressToMonitor` 指向同一底层值地址；随后把属性改成 `99` 再次求值，应看到新值 `99`，证明 getter 求值与地址跟踪都生效。 |
| 使用的 Helper | `CompileScriptModule` / `FActorTestSpawner` / `FindFProperty` / `FAngelscriptType::GetDebuggerValueFromFunction` |
| 优先级 | P1 |
#### NewTest-26：补顺序敏感的链式赋值表达式测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Angelscript.TestModule.Angelscript.Misc.MultiAssign` 只覆盖 `A = B = C = 42`，无法区分赋值表达式的结合顺序和返回值语义 |
| 风险评估 | 如果链式赋值的结合方向、赋值表达式返回值或中间结果传播退化，当前 Misc 套件仍会绿灯，语言层回归会潜伏到更复杂脚本里才暴露。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Misc.MultiAssign.OrderSensitive` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 场景描述 | 用一个能同时观测 `A`、`B`、`C` 和赋值表达式返回值的脚本，验证链式赋值确实按右结合执行。 |
| 输入/前置 | 脚本示例：`int Test() { int A = 1, B = 2, C = 3; int Result = (A = (B = (C = 4))); return A * 1000 + B * 100 + C * 10 + Result; }`。 |
| 期望行为 | `Test()` 返回 `4444`；若实现错误地按别的结合方向求值，至少一位会变化，从而直接暴露问题。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |
#### NewTest-27：补 `int8` 的负数与边界值符号扩展测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Angelscript.TestModule.Angelscript.Types.Int8` 只覆盖 `100 + 50` 的正数提升路径，没有覆盖负数、最小值和符号扩展 |
| 风险评估 | 如果 `int8` 在字面量绑定、寄存器传递或提升回 `int` 时发生符号位丢失，现有 TypeTests 仍可能全部通过；这类回归会直接影响脚本里的位运算、网络序列化和小整数算术。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.Int8.SignAndBounds` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 编译并执行一个同时使用 `-1`、`-128`、`127` 三个 `int8` 边界值的脚本，验证它们在比较和提升回 `int` 时保持正确符号。 |
| 输入/前置 | 脚本示例：`int Run() { int8 Negative = -1; int8 MinValue = -128; int8 MaxValue = 127; return (Negative == -1 ? 100 : 0) + (int(MinValue) == -128 ? 10 : 0) + (int(MaxValue) == 127 ? 1 : 0); }`。 |
| 期望行为 | `Run()` 返回 `111`，从而证明负数字面量绑定、最小值边界和提升回 `int` 的符号扩展都正确。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |
### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-43 |
| AntiPattern | 1 | Issue-40 |
| FlakyRisk | 1 | Issue-42 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | NoTestForSource: 1, MissingScenario: 1, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-08 18:00)

### 一、现有测试问题

#### Issue-44：`Parser.ExpressionAst` / `Parser.ControlFlow` 只检查“节点存在”，没有验证 AST 结构

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Parser.ExpressionAst`、`Angelscript.TestModule.Internals.Parser.ControlFlow` |
| 行号范围 | 120-174 |
| 问题描述 | 这两个用例都依赖递归 helper `ContainsNodeType(...)`，断言 AST 里“出现过” `snExprOperator`、`snIf`、`snFor`、`snWhile` 即算通过。对表达式用例 `1 + 2 * 3` 来说，它没有验证 `*` 是否正确嵌套在 `+` 之下，因此即使运算符优先级被错误展平，只要树里还有某个 operator 节点，测试仍会绿灯；对控制流用例也是同理，只证明三种节点类型存在，却没有证明它们真的是 `if -> for -> while` 的嵌套关系。 |
| 影响 | parser 一旦在 AST 结构、子节点顺序或优先级上回归，这两个用例很可能继续通过，无法保护后续 compiler/codegen 真正依赖的树形结构。 |
| 修复建议 | 1）给 `ExpressionAst` 增加结构断言，至少检查根表达式下的主 operator 是 `+`，且其右子树包含 `*` 子表达式；2）给 `ControlFlow` 显式验证 `snIf` 的 body 下第一层是 `snFor`，`snFor` 的 body 内再出现 `snWhile`，而不是仅做集合包含；3）如果 `asCScriptNode` 暴露 token/child helper，可抽 `AssertNodeChain` 一类 helper 复用于 parser/script-node 套件。 |

#### Issue-45：`GCInternalTests` 用全局静态探针状态驱动断言，但没有恢复且会在测试入口直接清零

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.GC.ManualCycleCollection`、`Angelscript.TestModule.Internals.GC.CycleDetection` |
| 行号范围 | 46-50, 199-228, 343-390 |
| 问题描述 | 该文件把 `GGCProbeScriptEngine` 和 `FGCProbeObject::LiveCount` 定义成进程级静态状态。`RegisterGCProbeType()` 每次把 `GGCProbeScriptEngine` 改写为当前引擎，但没有在 teardown 里清空；两个核心 GC 用例又都在入口直接执行 `FGCProbeObject::LiveCount = 0`。这意味着一旦前一个用例泄漏 probe object，后一个用例会把计数强行归零并继续运行，等于把跨测试泄漏隐藏掉。 |
| 影响 | GC 套件无法可靠发现测试间残留对象或悬空 engine 指针，失败定位会被静态全局状态污染；更糟的是，真实泄漏可能因为入口清零而被误报成“已收集完成”。 |
| 修复建议 | 1）把 `LiveCount`/engine 指针封装进 test-local fixture，并在 `ON_SCOPE_EXIT` 中恢复；2）不要在测试开头直接清零，改为先断言进入测试时 `LiveCount == 0`，把它当作隔离前置条件；3）`RegisterGCProbeType()` 返回后在 teardown 里把 `GGCProbeScriptEngine` 置回 `nullptr`，避免后续 GC 回调误用已销毁引擎。 |

### 二、需要新增的测试

#### NewTest-28：补 `ResolveDeclaredImports` 在“依赖模块后出现”场景下的自动重绑

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::ResolveDeclaredImports` |
| 现有测试覆盖 | `Builder.ImportBinding` 只覆盖手工 `BindImportedFunction(...)`，没有任何测试直达 engine 层的 declared-import 自动解析路径 |
| 风险评估 | 如果 engine 在模块热编译、延迟加载或跨模块重建后不能自动把 import 重新绑回目标函数，当前套件不会报警；这类回归会直接表现为“模块都存在，但调用仍然 unresolved”。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Builder.ResolveDeclaredImportsLateSource` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 场景描述 | 先创建带 `import int SharedValue() from "BuilderImportLateSource"` 的 consumer module，再编译 source module，最后显式调用 `Engine.ResolveDeclaredImports(ConsumerModule)` 验证 engine 能自动把 import 重新绑到后出现的 source function。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；脚本一为 `import int SharedValue() from "BuilderImportLateSource"; int Entry() { return SharedValue(); }`，脚本二为 `int SharedValue() { return 77; }`。consumer/source 都用现有 `BuildModule` 构建；为避免先前状态干扰，可在解析前先对 `ConsumerModule->UnbindImportedFunction(0)` 断言 `asSUCCESS`。 |
| 期望行为 | `ConsumerModule->GetImportedFunctionCount() == 1`；`Engine.ResolveDeclaredImports(ConsumerModule)` 后，`GetFunctionByDecl("int Entry()")` 可执行并返回 `77`；额外断言 `ConsumerModule->BindAllImportedFunctions() == asSUCCESS` 或等价执行结果，证明 import 已不再处于 unresolved 状态。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-29：补 `CheckFunctionImportsForNewModules` 的签名不匹配诊断路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CheckFunctionImportsForNewModules` |
| 现有测试覆盖 | `Builder.ImportBinding` 只验证匹配签名时可以手工绑定；没有任何测试覆盖“import 目标模块存在，但函数签名不匹配”的报错分支 |
| 风险评估 | 一旦 import 校验退化成“只看模块名不看签名”或 diagnostics 丢失，模块切换/热重载时会留下静默错误；当前 LanguageFeatures 套件无法拦截。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Builder.ImportSignatureMismatchDiagnostics` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 场景描述 | source module 提供错误签名 `int SharedValue(int Value)`，consumer module 继续 import `int SharedValue()`，然后直接走 engine 的 import 检查路径，验证它返回 invalid 并产生精确 diagnostics。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；source 脚本为 `int SharedValue(int Value) { return Value; }`，consumer 脚本为 `import int SharedValue() from "BuilderImportSourceMismatch"; int Entry() { return SharedValue(); }`。构建完成后通过 `Engine.GetModuleByModuleName(TEXT("BuilderImportConsumerMismatch"))` 取到 `FAngelscriptModuleDesc`，组装单元素数组传给 `Engine.CheckFunctionImportsForNewModules(...)`；补一个 `ContainsDiagnostic(Engine, TEXT("could not find function with this signature"))` helper 读取 diagnostics。 |
| 期望行为 | `Engine.CheckFunctionImportsForNewModules(...) == false`；diagnostics 至少包含 `could not find function with this signature in module BuilderImportSourceMismatch`；`ConsumerModule->GetImportedFunctionCount() == 1` 且 `BindAllImportedFunctions()` 失败或 `Entry()` 不可执行，证明错误没有被静默吞掉。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `Engine.GetModuleByModuleName` + 新增 `ContainsDiagnostic` helper |
| 优先级 | P1 |

#### NewTest-30：补 `CheckFunctionImportsForNewModules` 的缺模块诊断路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CheckFunctionImportsForNewModules` |
| 现有测试覆盖 | 当前 import 相关测试没有覆盖 `FromModule` 不存在时的报错分支，也没有验证 diagnostics 会指出缺失模块名 |
| 风险评估 | 如果 import 检查在缺模块场景下静默放过 consumer，后续模块加载顺序变化会直接变成运行时 unresolved call；现有套件无法在编译期阻断。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Builder.ImportMissingModuleDiagnostics` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 场景描述 | 只构建 consumer module，不提供任何 source module，然后直接调用 engine 的 import 检查逻辑，确认它会把模块判为 invalid 并给出缺模块 diagnostics。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；consumer 脚本为 `import int SharedValue() from "BuilderImportMissingSource"; int Entry() { return SharedValue(); }`。通过 `Engine.GetModuleByModuleName(TEXT("BuilderImportMissingConsumer"))` 取到 `FAngelscriptModuleDesc`，调用 `Engine.CheckFunctionImportsForNewModules({ConsumerDesc.ToSharedRef()})`；复用 `ContainsDiagnostic` helper 检查 `could not find module BuilderImportMissingSource to import from`。 |
| 期望行为 | `Engine.CheckFunctionImportsForNewModules(...) == false`；diagnostics 明确包含缺失模块名；`ConsumerModule->GetImportedFunctionCount() == 1` 且 `BindAllImportedFunctions()` 失败，证明 import 没有被错误地标记为已解析。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `Engine.GetModuleByModuleName` + 新增 `ContainsDiagnostic` helper |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-44 |
| BadIsolation | 1 | Issue-45 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | NoTestForSource: 1, MissingErrorPath: 2 |

---

## 测试审查 (2026-04-08 18:26)

### 一、现有测试问题

#### Issue-46：`NativeInterface` 绑定 helper 以“类型已存在”代替“绑定已完成”，会引入顺序依赖

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.NativeImplement`、`NativeInheritedImplement`、`NativeReferenceRoundTrip` |
| 行号范围 | 47-84, 102-359 |
| 问题描述 | `EnsureNativeInterfaceBoundForTests()` 只要发现 `ScriptEngine->GetTypeInfoByName(TCHAR_TO_ANSI(*TypeName)) != nullptr` 就直接返回，不再重新执行 `ReferenceClass(...)`、`plainUserData` 赋值和 `BindNativeInterfaceMethod(...)`。这意味着测试把“类型条目存在”错误地当成“native interface 已完整绑定”。一旦前序测试留下了半初始化 type、缺方法签名或旧的 `plainUserData`，后续用例会静默复用这份状态并跳过真正的绑定步骤，结果依赖执行顺序。 |
| 影响 | 这组三个测试即使各自单跑通过，也可能在乱序运行、重复运行或中途失败后出现伪失败/伪通过，无法满足用户要求中“检查测试间是否有隐式依赖”的隔离标准。 |
| 修复建议 | 1）把 `EnsureNativeInterfaceBoundForTests()` 改成显式验证完整绑定条件，而不是只看 type 是否存在，例如同时检查 `plainUserData == InterfaceClass` 且所需 method signature 全部可见；2）更稳妥的做法是每个测试创建专用 fixture，在 setup 中无条件重新绑定，在 teardown 中释放 type/signature；3）补一条乱序回归验证，先运行 `NativeReferenceRoundTrip` 再运行 `NativeImplement`，确认后者不会因为复用旧 type state 而跳过绑定。 |

#### Issue-47：`StructCppOps` helper 通过全局 package 按名字取 struct，会把旧类型对象当成当前编译结果

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptStructCppOpsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.StructCppOps.NotBlueprintTypeByDefault` |
| 行号范围 | 10-29, 43-60 |
| 问题描述 | `BuildScriptStruct()` 在 `BuildModule(...)` 之后不是从返回的 module/type 元数据里取出新建 struct，而是直接执行 `FindObject<UScriptStruct>(FAngelscriptEngine::GetPackage(), *UnrealName)`。再加上测试使用固定的 module 名 `StructCppOpsScopeModule` 和固定的 struct 名 `FScopeConstructStruct`，一旦前一次运行留下同名 `UScriptStruct`，本次测试就可能读到旧对象，并把旧 metadata 误判为当前编译结果。 |
| 影响 | 该用例对执行顺序和历史残留状态敏感，可能在“本次编译根本没有生成新 struct”时仍然通过，也可能把其他测试留下的同名对象当作自己的被测对象，直接违背“不要依赖前一个测试创建的类型”的审查要求。 |
| 修复建议 | 1）不要用全局 `FindObject` 兜底查找；改为从当前 module 导出的 type info 或 compile helper 返回值拿到本次编译产生的 `UScriptStruct`；2）如果短期内必须按名字查找，至少为每次测试生成唯一 module/type 名并在 teardown 中显式移除生成对象；3）追加防串跑断言，例如验证返回的 struct 所属 outer/module 标识与本次编译的 module 名一致。 |

#### Issue-48：`Bytecode.Output` 在未锁定输出长度前直接读取 `Buffer[1]`，布局变化时会变成脆弱测试

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBytecodeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Bytecode.Output` |
| 行号范围 | 116-137 |
| 问题描述 | 用例只发出一条 `InstrDWORD(asBC_PshC4, 42)`，随后用 `ByteCode.GetSize()` 分配缓冲区并直接断言 `Buffer[0]`、`Buffer[1]`。它没有先验证 `GetSize() >= 2`，也没有确认 `Output()` 实际写出的布局确实包含 opcode + payload 两个 dword。一旦 bytecode 编码尺寸回归、`GetSize()` 计算错误，或 `Output()` 只写出部分数据，这个测试会先读非法索引/垃圾值，而不是稳定地产出“输出长度错误”的失败。 |
| 影响 | 当 bytecode layout 真正回归时，这个测试可能表现成崩溃、未定义读或难以定位的随机失败，降低内部组件测试对问题的诊断价值。 |
| 修复建议 | 1）在访问 `Buffer[1]` 前先断言 `ByteCode.GetSize() >= 2`；2）把长度、opcode、payload 三个断言拆开，先锁输出尺寸再读具体槽位；3）如果 `Output()` 支持更精确的布局查询，优先用 helper 校验 emitted dword 数，而不是直接假设 `PshC4` 永远占两个槽位。 |

#### Issue-49：`Parser.Declarations` 只验证节点“出现过”，没有锁定顶层声明树结构

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Parser.Declarations` |
| 行号范围 | 84-117 |
| 问题描述 | 该用例输入是 `int GlobalValue = 7; class FSample { int Value; }`，但断言只有三条：根节点是 `snScript`、树里存在一个 `snDeclaration`、树里存在一个 `snClass`。它没有检查顶层 child 数量是否恰好为 2，也没有验证第一个 child 是 global declaration、第二个 child 是 class declaration。若 parser 把 class member declaration 误算成顶层 declaration，或者复制/错位了一个顶层 child，只要树里还能搜到这两个 node type，测试依然通过。 |
| 影响 | 声明列表顺序、顶层归属和 child 链接关系回归时，当前测试给不出有效保护；后续 builder 依赖 declaration order 的问题也会被延后到更高层才暴露。 |
| 修复建议 | 1）像 `ScriptNode.Traversal` 那样增加顶层 child 数量断言；2）显式检查 `Root->firstChild` / `firstChild->next` 的 `nodeType` 顺序，确保 global declaration 在前、class declaration 在后；3）若 parser 暴露 identifier/token 信息，再顺手断言名称为 `GlobalValue` 和 `FSample`，避免命中错误节点。 |

#### Issue-50：`ScriptNode.Copy` 只验证“复制了另一个实例”，没有验证复制树是否完整

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptScriptNodeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.ScriptNode.Copy` |
| 行号范围 | 96-135 |
| 问题描述 | 用例在 `Root->CreateCopy(...)` 之后只断言了三件事：root node type 一样、`Copy != Root`、`Copy->firstChild != Root->firstChild`。它没有检查 copied tree 的 child 数量、`lastChild` / `prev` / `next` 链接、也没有验证 child node type 或 token 内容。哪怕 `CreateCopy()` 只复制了根和第一个 child、丢失了 sibling 链，或者复制树的双向链接损坏，这个测试仍可能通过。 |
| 影响 | AST 深拷贝一旦在 sibling 链接、子树完整性或 token 元数据上回归，当前测试无法发现；后续依赖 AST copy 的 parser/builder 场景会在更复杂的输入下才暴露问题。 |
| 修复建议 | 1）在原始树里先计算 direct child 数量并对 copy 做同样断言；2）检查 `Copy->firstChild->prev == nullptr`、`Copy->lastChild` 指向最后一个 child，必要时验证 sibling `next/prev` 链完整；3）至少再比较一个 child 的 `nodeType` 与名称/token，确保不是“结构存在但内容错位”的浅复制。 |

#### Issue-51：`Types.Bool` 以单一真值分支代替整个 bool 语义，测试名明显过宽

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.Bool` |
| 行号范围 | 98-117 |
| 问题描述 | 脚本只有 `bool A = true; bool B = false; return (A && !B) ? 1 : 0;`，最终只断言结果是 `1`。这只验证了一个最简单的 true-path 组合，并没有覆盖 `false` 分支、`||`、`==/!=`、赋值后再读取，甚至连 `!true` 的结果都没被直接观察。测试名却是宽泛的 `Types.Bool`，会让人误以为 bool 语义已有完整保护。 |
| 影响 | 若 bool lowering、逻辑短路或 false-path 求值发生回归，当前用例很可能继续绿灯；TypeTests 的覆盖面会被测试名高估。 |
| 修复建议 | 1）把宽泛的 `Types.Bool` 拆成至少两个更具体的用例，分别验证 true/false 分支与 `&&/||/!` 组合；2）若保留单测试文件，可让脚本返回多位编码，例如同时编码 `true && false`、`true || false`、`!true`、`!false` 四个结果；3）测试名应与实际断言范围一致，避免继续用一个 happy path 代表整个 bool 特性。 |

#### Issue-52：`Execution.Discard` 只验证模块映射移除，没有覆盖 `DiscardModule` 的关键清理路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.Discard` |
| 行号范围 | 444-476 |
| 问题描述 | 用例编译的脚本只有一个 `void Test() {}`，随后只断言 `Engine.GetModuleByModuleName(...)` 在 discard 前存在、discard 后不存在、再次 discard 返回 false。它没有创建任何 class/enum/delegate、没有留下 diagnostics，也没有生成/缓存 context，因此完全没有触达 `FAngelscriptEngine::DiscardModule()` 中更关键的清理逻辑，例如释放 context pool、清空 `ActiveClassesByName` / `ActiveEnumsByName` / `ActiveDelegatesByName`，以及把 `UASFunction::ScriptFunction` 置空。 |
| 影响 | `DiscardModule` 一旦在类型注册表、诊断缓存或 stale script function 清理上回归，当前测试仍会通过；后续同名模块重建或热重载场景容易出现隐式依赖，而 `Execution.Discard` 给不出保护。 |
| 修复建议 | 1）把脚本换成至少包含一个 script class、一个 enum 或 delegate 的模块，并在 discard 后验证这些名字从 engine active maps 中消失；2）在 discard 前先创建并释放一个 context，随后断言 discard 不会保留可重用的旧 context/function 指针；3）补一条同名模块重建断言，确保 discard 后重新编译得到的是干净的新模块而不是残留 state。 |

### 二、需要新增的测试

#### NewTest-31：补 `DiscardModule` 对 script type/function 清理的直接回归测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::DiscardModule` |
| 现有测试覆盖 | `Execution.Discard` 只验证模块映射是否移除，没有覆盖 context pool、`UASClass`/`UASFunction` stale 指针和 active type registry 清理 |
| 风险评估 | 一旦 discard 后残留旧 `ScriptFunction`/`ScriptTypePtr` 或 active map 条目，同名模块重建、热重载和跨测试执行都会出现顺序依赖，且当前套件无法提前报警。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.Discard.CleansTypeRegistries` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionLifecycleTests.cpp` |
| 场景描述 | 编译一个同时包含 script class、enum、delegate 和可执行函数的模块，执行一次函数后调用 `Engine.DiscardModule(...)`，直接验证生成的 type/function 句柄被清空。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；脚本建议包含 `enum EDiscardState { Ready = 1 }`、`delegate void FDiscardDelegate()`、`class DiscardCarrier { int Value; }`、`int Run() { return 42; }`。测试需保留 `UASClass*`、`UASFunction*` 或等价生成对象句柄，并在 discard 前通过 `ExecuteIntFunction` 跑一次 `Run()`。 |
| 期望行为 | `Engine.GetModuleByModuleName(...)` 在 discard 后失效；生成类上的 `ScriptTypePtr`、`ConstructFunction`、`DefaultsFunction` 置空；生成函数上的 `ScriptFunction`、`ValidateFunction` 置空；重新编译同名模块后 `Run()` 仍可执行且不会复用旧指针。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` + `FindGeneratedClass` + `UASClass/UASFunction` whitebox 读取 |
| 优先级 | P1 |

#### NewTest-32：补 bool 逻辑矩阵，直接锁定 true/false 与 `&&/||/!` 组合

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Types.Bool` 只覆盖 `true && !false` 的单一路径，没有 false-path 与 `||` 组合断言 |
| 风险评估 | bool lowering、逻辑短路或取反语义一旦回归，当前 TypeTests 很可能继续绿灯，语言层问题会被推迟到业务脚本才暴露。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.Bool.LogicMatrix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 编译并执行一个同时计算 `A && B`、`A || B`、`!A`、`!B` 的脚本入口，用编码结果一次性锁住 true/false 组合语义。 |
| 输入/前置 | 脚本示例：`int Run() { bool A = true; bool B = false; return (A && B ? 1000 : 0) + (A || B ? 100 : 0) + (!A ? 10 : 0) + (!B ? 1 : 0); }`。 |
| 期望行为 | `Run()` 返回 `101`，从而直接证明 `A&&B` 为 false、`A||B` 为 true、`!A` 为 false、`!B` 为 true。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-33：补显式 enum 值与非零起始值的断言矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Types.Enum` 只验证默认递增 enum 的 `Green == 1`，没有覆盖显式赋值、非零起始值和带空洞的枚举 |
| 风险评估 | enum 常量解析或显式底层值绑定一旦回归，脚本编译仍可能成功，但运行时逻辑会悄悄拿到错误常量；当前 TypeTests 没有任何专门保护。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.Enum.ExplicitValueMatrix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 定义一个非零起始且有值空洞的 enum，验证 `Color::Green`、`Color::Blue` 的显式常量值不会被重新编号。 |
| 输入/前置 | 脚本示例：`enum Color { Red = 10, Green = 20, Blue = 21 } int Run() { return int(Color::Green) * 10 + int(Color::Blue) - int(Color::Red); }`。 |
| 期望行为 | `Run()` 返回 `211`，从而证明显式常量值 `10/20/21` 被原样保留，没有退化成 `0/1/2`。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 2 | Issue-46 |
| FlakyRisk | 1 | Issue-48 |
| WeakAssertion | 4 | Issue-52 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 2, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-08 19:02)

### 一、现有测试问题

#### Issue-57：`Builder.RebuildModule` 只证明新版本可执行，没有验证旧模块/旧函数句柄被淘汰

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Builder.RebuildModule` |
| 行号范围 | 96-145 |
| 问题描述 | 用例先保留了 `ModuleV1` 和 `FunctionV1`，随后用同名 `BuilderRebuild` 再编译出 `ModuleV2` / `FunctionV2`，但断言只剩“`FunctionV2` 执行返回 `2`”。它没有检查 `ModuleV1 != ModuleV2`、没有验证引擎当前缓存确实切到了新模块，也没有证明旧模块名称已被改写/旧函数句柄不再代表活动版本。即使 rebuild 路径把新旧模块同时留在缓存里、旧 `Entry()` 仍可被当成活动版本调用，这个测试也会绿灯。 |
| 影响 | 模块重建后残留 stale module/function handle、重复缓存条目或顺序依赖时，当前套件无法及时发现；这类问题后续很容易在热重载、import 解析或跨测试执行中放大。 |
| 修复建议 | 在第二次编译后补淘汰语义断言：1）断言 `ModuleV1 != ModuleV2` 且 `Engine.GetModuleByModuleName(TEXT("BuilderRebuild"))->ScriptModule == ModuleV2`；2）读取 `ModuleV1->GetName()`，断言它不再是原始模块名 `BuilderRebuild`；3）重新通过当前活动模块查 `int Entry()`，断言返回的是 `FunctionV2` 而不是 `FunctionV1`；4）如果旧 handle 允许安全探测，再补一条“旧函数不再代表活动版本”的失败/失效断言。 |

#### Issue-58：`Core.CompilerBasic` / `Core.Parser` 的错误路径只看失败布尔值和枚举，缺少诊断与清理断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Core.CompilerBasic`、`Angelscript.TestModule.Angelscript.Core.Parser` |
| 行号范围 | 145-159, 195-209 |
| 问题描述 | 两个用例的 invalid syntax 分支都只断言 `bCompiledInvalid == false` 和 `CompileResult == ECompileResult::Error`。它们没有检查 diagnostics 是否包含正确的文件名/错误位置，也没有验证失败后模块没有残留函数、全局变量或半注册状态。只要编译包装器因为任何原因返回一个“错误”枚举，这两个用例就会通过，即便错误原因错了、定位错了，或者失败路径把脏模块留在引擎里。 |
| 影响 | 编译管线一旦在错误上报、定位信息或失败后清理上回归，现有 `Core` 套件不会报警；同名模块的后续重编译还可能被残留 state 污染，形成隐式依赖。 |
| 修复建议 | 1）为两个 invalid 用例都补 diagnostics 断言，至少验证文件名、非零行列号或命中预期 token；2）失败后检查 `Engine.GetModuleByModuleName(...)` 不存在，或其 `ScriptModule` 的 `GetFunctionCount()/GetGlobalVarCount()` 为 `0`；3）追加一次同名合法脚本的重编译与执行断言，证明错误路径不会污染后续成功编译。 |

#### Issue-59：`MultipleInheritanceChain` 只验证 actor 满足接口标记，没有验证接口链自身的继承关系

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.MultipleInheritanceChain` |
| 行号范围 | 593-676 |
| 问题描述 | 用例定义了 `UIBaseChain -> UIMidChain -> UILeafChain` 三层接口链，但断言只检查 `Actor->GetClass()->ImplementsInterface(BaseInterface/MidInterface/LeafInterface)`。它没有验证 `LeafInterface` 与 `MidInterface`、`MidInterface` 与 `BaseInterface` 的反射链路，也没有检查叶子接口是否可见父层方法。若实现退化成“只把三个接口平铺挂到 actor 上”，而接口类彼此不再保持父子关系，这个测试依然会通过。 |
| 影响 | `UInterface` 多层继承链的元数据断裂、父接口方法不可见或 parent link 丢失时，当前套件给不出信号；这正是 P10 接口继承主线里最难靠业务脚本即时定位的回归。 |
| 修复建议 | 1）在现有 actor 断言之外，补 `LeafInterface`/`MidInterface`/`BaseInterface` 之间的继承关系断言，例如 `ImplementsInterface(...)` 或 `GetSuperClass()` 链，按项目实际反射表示选择其一；2）验证 `LeafInterface` 上可查到 `BaseMethod`、`MidMethod`、`LeafMethod`，确保父层方法没有丢；3）若引擎提供 C++ 侧 parent-interface 调度 helper，再补一条从叶子实现回看父接口元数据的反射断言。 |

### 二、需要新增的测试

#### NewTest-37：补 `CreateForTesting` 的显式模式与 clone fallback 语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CreateForTesting` |
| 现有测试覆盖 | `Core.CreateEngine` 只验证返回对象非空，且 `GetCreationMode()` 不是 magic value `255`；没有显式覆盖 `Full`、`Clone` 和“无当前引擎时 clone fallback 到 full”这三条路径 |
| 风险评估 | 如果 `CreateForTesting` 忽略调用方传入的模式、或者 clone fallback 退化成错误模式，当前套件不会报警；测试引擎隔离策略一旦出错，会直接放大为整套测试的顺序依赖。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Core.CreateEngine.RespectsRequestedMode` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 场景描述 | 分三步验证 `CreateForTesting`：无当前引擎时请求 `Clone`、有当前引擎 scope 时请求 `Clone`、以及显式请求 `Full`。 |
| 输入/前置 | 使用 `FAngelscriptEngineConfig Config; FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();`。先在无 scope 状态下调用 `CreateForTesting(Config, Dependencies, EAngelscriptEngineCreationMode::Clone)`；再创建一个 `Full` host engine 并压入 `FAngelscriptEngineScope`，在该 scope 内分别调用 `CreateForTesting(..., Clone)` 与 `CreateForTesting(..., Full)`。 |
| 期望行为 | 无当前引擎时的 clone 请求返回可用 engine，且 `GetCreationMode() == EAngelscriptEngineCreationMode::Full`；有当前引擎时的 clone 请求返回 `GetCreationMode() == Clone`；显式 full 请求始终返回 `GetCreationMode() == Full`；三个 engine wrapper 和 `asIScriptEngine*` 都非空，且 clone/full 实例彼此不同。 |
| 使用的 Helper | `FAngelscriptEngine::CreateForTesting` + `FAngelscriptEngineScope` |
| 优先级 | P1 |

#### NewTest-38：补同名模块重建后旧句柄失效与缓存切换的断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | `Builder.RebuildModule` 只验证第二次编译后的 `Entry()` 返回 `2`，没有验证旧模块被重命名/淘汰，也没有验证引擎缓存已切到新模块 |
| 风险评估 | 若重建后仍保留可误用的旧模块或旧函数句柄，热重载、declared import 与后续模块查找都会产生 stale handle 问题；现有 builder 套件不会提前发现。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Builder.RebuildModule.InvalidatesOldHandles` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 场景描述 | 先编译 `BuilderRebuild` 的 V1 并保留 `ModuleV1` / `FunctionV1`，再用同名模块编译 V2，随后对白盒验证当前活动模块、旧模块名和当前函数指针。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；V1 脚本为 `int Entry() { return 1; }`，V2 脚本为 `int Entry() { return 2; }`。第二次编译后通过 `Engine.GetModuleByModuleName(TEXT("BuilderRebuild"))` 取活动模块，再次 `GetFunctionByDecl("int Entry()")` 取得当前函数。 |
| 期望行为 | `ModuleV1 != ModuleV2`；活动模块描述存在且 `ScriptModule == ModuleV2`；`UTF8_TO_TCHAR(ModuleV1->GetName())` 不再等于 `BuilderRebuild`；当前 `Entry()` 指针与 `FunctionV2` 相同且不等于 `FunctionV1`；执行当前 `Entry()` 返回 `2`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` + `Engine.GetModuleByModuleName` |
| 优先级 | P1 |

#### NewTest-39：补 `Core.Parser` 的错误定位与失败后清理回归测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | `Core.Parser` 只验证 invalid syntax 返回失败与 `ECompileResult::Error`，没有覆盖 diagnostics 定位和失败后的模块状态 |
| 风险评估 | parser/compile 包装层一旦开始报错位置漂移、诊断文本退化或失败后保留脏模块，现有 Core 测试不会报警；同名模块的后续成功编译也可能被污染。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Core.Parser.InvalidSyntaxDiagnosticsAndCleanup` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 场景描述 | 先用一个带语法错误的脚本编译同名模块，检查 diagnostics 与模块清理状态；随后用合法脚本重编译同名模块并执行，证明失败路径不残留脏状态。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_FULL()`；先调用 `CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly, TEXT("ASCoreParserInvalidCleanup"), TEXT("ASCoreParserInvalidCleanup.as"), TEXT("void Test( { int A = 1; }"), CompileResult)`；再编译 `int Test() { return 42; }`。需要一个轻量 helper 遍历 `Engine.Diagnostics`，检查 filename、message、row/column。 |
| 期望行为 | 首次编译 `bCompiled == false` 且 `CompileResult == ECompileResult::Error`；diagnostics 包含 `ASCoreParserInvalidCleanup.as` 和非零行列号，消息命中 syntax error；失败后模块不存在，或其 `ScriptModule` 的 `GetFunctionCount()` / `GetGlobalVarCount()` 为 `0`；再次用同名合法脚本编译成功并执行 `Test()` 返回 `42`。 |
| 使用的 Helper | `CompileModuleWithResult` + `Engine.GetModuleByModuleName` + diagnostics helper + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-58 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 2, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-08 18:45)

### 一、现有测试问题

#### Issue-53：`Bytecode.Append` 只看长度增长和尾部 payload，无法证明追加顺序正确

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBytecodeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Bytecode.Append` |
| 行号范围 | 70-89 |
| 问题描述 | 用例构造两段 bytecode 后只断言 `First.GetSize() > InitialSize`，以及 `GetLastInstrValueDW() == 20`。它没有验证第一段 `PshC4 10` 仍然保留在前缀，也没有验证追加后的总长度是否等于两段序列之和。即使 `AddCode()` 错误地覆盖了第一段、重复了第二段，或者把 payload 顺序打乱，只要尾部还残留 `20` 且长度变大，测试就会通过。 |
| 影响 | `asCByteCode::AddCode` 一旦回归成“尾部值看着对、整体序列已损坏”，当前套件无法及时报警；后续 builder/compiler 依赖拼接 bytecode 的路径会在更高层才暴露问题，定位成本高。 |
| 修复建议 | 1）追加后显式断言总长度等于 `InitialSize + Second.GetSize()`；2）调用 `Output()` 或遍历指令流，验证前缀仍是第一段的 opcode/payload `10`，后缀才是第二段的 payload `20`；3）若 `asCByteCode` 暴露 first/last instruction value helper，可把“前段保留 + 后段追加 + 顺序稳定”拆成三个独立断言，避免只看尾部。 |

#### Issue-54：`Functions.Destructor` 没有任何可观察副作用，无法证明析构函数真的执行

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Functions.Destructor` |
| 行号范围 | 140-157 |
| 问题描述 | 脚本只定义了空析构 `~DestructorCarrier() {}`，`Run()` 里创建局部对象后直接 `return 1`，最终断言结果也是固定值 `1`。这只说明“声明了析构函数的脚本类能编译并执行”，没有任何状态变化能够证明离开作用域时 वास्तव际调用了析构函数。即使析构 lowering、作用域退出析构或析构注册完全失效，只要函数还能返回 `1`，测试就会绿灯。 |
| 影响 | 语言层对象生命周期回归会被这个用例静默放过，尤其是 block-scope object 清理、析构调度和未来可能引入的资源释放逻辑，都没有得到实际保护。 |
| 修复建议 | 1）把当前 compile-and-run 冒烟保留为单独的 compile smoke 时，另补一个带副作用的析构断言；2）更直接的做法是在脚本里加入可观察全局状态，例如 `int g_Destroyed = 0; class DestructorCarrier { ~DestructorCarrier() { g_Destroyed = 1; } } int Run() { { DestructorCarrier Carrier; } return g_Destroyed; }`，然后断言返回 `1`；3）如果担心单测职责混杂，可把“析构可编译”和“析构确实执行”拆成同文件两个独立测试名。 |

#### Issue-55：`ControlFlow.Condition` 以“条件控制流”命名，但实际只覆盖嵌套 ternary，不覆盖 `if/else`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.Condition` |
| 行号范围 | 72-89 |
| 问题描述 | 该用例脚本只有 `return (Value > 0) ? ((Value > 10) ? 2 : 1) : 0;` 这一组嵌套 ternary，并用编码结果 `210` 同时覆盖 `15/5/-3` 三个输入。它没有任何 statement-level `if/else` block、没有分支体内的局部变量或嵌套语句，因此只能证明条件表达式求值路径正常，不能代表整个“条件控制流”已被验证。 |
| 影响 | 如果 `if/else` 语句 lowering、block 绑定或嵌套分支作用域发生回归，而 ternary 仍然工作，这个名为 `Condition` 的用例会继续绿灯，直接高估 `ControlFlowTests` 对条件语句的保护强度。 |
| 修复建议 | 1）把当前用例重命名为更准确的 `TernaryConditionMatrix`；2）在同文件补一个真正的 `if/else` 语句测试，覆盖 true/false 两条路径、嵌套 block 和局部变量写回；3）若暂不拆名，至少把脚本扩成 `if/else` 与 ternary 并存，并分别编码断言两类控制流的结果，避免继续用 ternary 代替 `if/else`。 |

#### Issue-56：`Tokenizer.Keywords` 不验证 token 长度和关键字边界，误判前缀时仍会通过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Tokenizer.Keywords` |
| 行号范围 | 57-66 |
| 问题描述 | 用例只对 `class`、`void`、`int`、`float32` 这四个精确输入断言 token type，却没有检查 `TokenLength`，也没有覆盖 keyword boundary，例如 `className`、`intValue`、`float32x` 这类“以关键字开头但整体应当是 identifier”的输入。若 tokenizer 错误地把前缀切成关键字、或返回正确 token type 但长度错误，当前测试仍会全部通过。 |
| 影响 | tokenizer 一旦在关键字边界上回归，parser 会拿到错误切片并把后续字符留给下一轮扫描，造成难以定位的语法错误；当前 `Keywords` 用例对这种高频边界问题没有保护。 |
| 修复建议 | 1）为现有四个关键字都补 `TokenLength` 断言，锁定返回长度与完整词一致；2）在同一用例或新增子用例里追加 `className` / `intValue` / `float32x`，断言它们返回 `ttIdentifier` 且长度覆盖整个标识符；3）若希望保持单测聚焦，可把 exact-keyword 与 boundary case 拆成两个名字明确的 tokenizer 用例。 |

### 二、需要新增的测试

#### NewTest-34：补 statement-level `if/else` 的分支矩阵，避免继续用 ternary 代替条件语句

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `ControlFlow.Condition` 只覆盖嵌套 ternary；当前控制流套件没有任何直接执行 `if/else` 语句块的测试 |
| 风险评估 | `if/else` 语句的 block 绑定、局部变量作用域或 false-path lowering 一旦回归，现有 `ControlFlowTests` 仍可能全部绿灯，无法满足用户要求中的 `if/else` 覆盖 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.IfElseStatementMatrix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 场景描述 | 编译一个显式使用 `if/else` 与嵌套 block 的入口函数，分别走 true/false 两条路径，并在分支内修改不同局部变量后编码返回。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Evaluate(int Value) { int TrueHits = 0; int FalseHits = 0; if (Value > 0) { int Local = Value + 1; TrueHits = Local; } else { int Local = -Value + 2; FalseHits = Local; } return TrueHits * 100 + FalseHits; } int Run() { return Evaluate(3) * 1000 + Evaluate(-2); }`。 |
| 期望行为 | `Run()` 返回 `4004`，其中 `Evaluate(3) == 400`、`Evaluate(-2) == 4`，从而同时证明 true-path、false-path 和分支内局部变量写回都正确生效。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-35：补负数显式转换，锁定“向零截断”而不是向下取整

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Types.Conversion` 只覆盖正数 `3.7 -> 3`，没有任何负数转换断言 |
| 风险评估 | numeric conversion 一旦对负数退化成 floor/round，而不是语言约定的 truncate-toward-zero，现有套件不会报警；这类错误会直接影响脚本数学与索引计算 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.Conversion.NegativeTruncateTowardZero` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 执行一个同时把正数和负数 `float32` 显式转换成 `int` 的脚本入口，用编码结果证明正负两边都按 toward-zero 截断。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Run() { float32 Negative = -3.7f; float32 Positive = 3.7f; return int(Negative) * 10 + int(Positive); }`。 |
| 期望行为 | `Run()` 返回 `-27`，从而直接证明 `int(-3.7f) == -3` 而不是 `-4`，同时 `int(3.7f) == 3`。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-36：补脚本析构的可观察副作用，验证离开作用域时确实执行 dtor

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Functions.Destructor` 只有“声明析构后仍可编译执行”的冒烟，没有任何可观察析构语义断言 |
| 风险评估 | 若 block-scope object 的析构调度失效，当前函数套件不会报警；脚本资源释放和生命周期钩子很容易在业务层才暴露问题 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Functions.Destructor.SideEffectObserved` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 场景描述 | 定义一个在析构函数里写全局计数器的脚本类，进入局部 block 创建对象，离开 block 后再读取计数器。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int g_Destroyed = 0; class DestructorCarrier { ~DestructorCarrier() { g_Destroyed += 1; } } int Run() { { DestructorCarrier Carrier; } return g_Destroyed; }`。 |
| 期望行为 | `Run()` 返回 `1`；若再扩展为双重 block，可进一步断言计数器递增到 `2`，证明析构按作用域边界准确触发。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-54 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 2, MissingEdgeCase: 1 |

---

## 校正说明 (2026-04-08 19:06)

`2026-04-08 19:02` 这一轮新增内容已经写入文档，编号范围为 `Issue-57` 至 `Issue-59`、`NewTest-37` 至 `NewTest-39`。

该段正文因为命中了前文同样的汇总表行，被插入到了文档中部而不是当前文件尾部。本次仅追加校正说明，不重复正文，也不改动前面任何已有内容；后续整理文档时应将 `## 测试审查 (2026-04-08 19:02)` 视为本轮最新发现。

---

## 测试审查 (2026-04-08 19:11)

### 一、现有测试问题

#### Issue-60：`Types.Auto` 只验证值传播，没有验证 `auto` 实际推导出的类型

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.Auto` |
| 行号范围 | 275-286 |
| 问题描述 | 用例脚本只有 `auto Value = 42; return Value;`，最终只断言返回值 `42`。这只能证明“某种可返回到 `int` 的数值类型”能通过执行，无法区分 `auto` 究竟推导成 `int`、`int64`、`uint`，还是其他会隐式收窄回 `int` 的类型。即使 `auto` 推导规则回归，测试仍可能继续绿灯。 |
| 影响 | `auto` 类型推导一旦偏离语言约定，现有套件无法在语言层第一时间拦截；后续只有在更复杂的重载解析、符号位或溢出场景中才会暴露，定位成本明显升高。 |
| 修复建议 | 把“值断言”升级成“类型可观察断言”：1）在脚本中加入重载分派，例如 `int Pick(int) { return 1; } int Pick(float) { return 2; } int Run() { auto Value = 42; return Pick(Value); }`，明确断言 `auto` 走到 `int` 重载；2）再补一个非整数初始化场景，验证 `auto` 对 `3.5f`、`enum`、`bool` 等输入不会退化成错误类型；3）若项目允许反射检查，可额外取函数签名或 type id，把推导类型直接暴露到断言中。 |

#### Issue-61：`InheritedInterface` 只检查 actor 的实现标记，没有验证接口类自身的继承关系

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.InheritedInterface` |
| 行号范围 | 66-145 |
| 问题描述 | 用例声明了 `UIKillableChild : UIDamageableParent`，但断言只检查 `Actor->GetClass()->ImplementsInterface(ChildInterface/ParentInterface)`。它没有验证 `ChildInterface` 与 `ParentInterface` 之间的反射父子链，也没有检查父接口方法是否被继承到子接口元数据上。若实现退化成“把两个接口平铺挂到 actor 上”，当前测试仍会通过。 |
| 影响 | 单层 `UInterface` 继承链一旦在反射层断裂，当前套件只会看到 actor 仍然实现两个接口，却无法发现真正的接口层级和父方法可见性已经损坏；这会直接削弱 P10 `UInterface` 主线对继承语义的保护。 |
| 修复建议 | 1）在现有 actor 断言之外，补 `ChildInterface` 到 `ParentInterface` 的层级断言，例如检查 `GetSuperClass()` 链或项目现有的 interface-hierarchy helper；2）验证 `ChildInterface` 上可见 `TakeDamage` 之类父接口方法，避免只看实现标记；3）保留 `InheritedMethodDispatch` 负责运行时调用，而把本用例收敛为“反射继承关系 + 父方法可见性”专用检查。 |

#### Issue-62：`Bytecode.InstructionSequence` 没有验证立即数 payload 和精确长度，无法锁定真实指令序列

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBytecodeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Bytecode.InstructionSequence` |
| 行号范围 | 42-64 |
| 问题描述 | 用例生成 `InstrDWORD(asBC_PshC4, 42); Instr(asBC_RET);` 后，只断言首 opcode 是 `asBC_PshC4`、末 opcode 是 `asBC_RET`，以及 `GetSize() > 0`。它没有验证中间 dword payload 仍是 `42`，也没有锁定总长度是否正好等于这两条指令所需的字数。若编码器把立即数写错、插入额外垃圾指令，或把 `PshC4` 的 payload 宽度编码错误，只要首尾 opcode 没变，测试仍会通过。 |
| 影响 | bytecode emitter 的低层编码回归会被这个用例放过，之后只会在 compiler / execution 更高层出现“不明原因执行异常”，降低内部组件测试对定位问题的价值。 |
| 修复建议 | 1）追加对 payload 的直接断言，例如通过 `Output()` 或内部 helper 验证第二个 dword 为 `42`；2）锁定 `GetSize()` 为预期精确值，而不是仅检查“大于 0”；3）若保留当前结构，至少把“首 opcode / payload / 末 opcode / 总长度”拆成四个独立断言，避免把完整指令序列退化成首尾冒烟。 |

### 二、需要新增的测试

#### NewTest-40：补 `auto` 的可观察推导测试，直接通过重载解析锁定最终类型

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Types.Auto` 只验证 `auto Value = 42` 的返回值，无法观察真实推导类型；现有新增建议 `NewTest-24` 关注非整数表达式，但没有把“推导出的类型”直接暴露到断言 |
| 风险评估 | 一旦 `auto` 推导从 `int` 漂移到 `int64`、`uint` 或错误的浮点类型，现有套件可能依旧返回相同数值而持续绿灯，直到更复杂的重载或溢出场景才暴露回归 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.Auto.InferenceByOverload` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 用重载函数把 `auto` 的推导结果转成可观察返回值，分别覆盖整数、浮点和布尔初始化场景。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本按 `asEP_FLOAT_IS_FLOAT64` 分两版生成：float64 模式用 `int Pick(int) { return 1; } int Pick(double) { return 2; } int Pick(bool) { return 3; } int Run() { auto I = 42; auto F = 3.5; auto B = true; return Pick(I) * 100 + Pick(F) * 10 + Pick(B); }`，float32 模式把第二个重载改成 `int Pick(float)` 并把 `auto F` 初始化写成 `3.5f`。 |
| 期望行为 | `Run()` 返回 `123`；任一 `auto` 推导错型都会把对应位数改掉，从而直接暴露到失败结果里。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` + 现有 `Types.Float` 同类的 float/double 选择 helper |
| 优先级 | P1 |

#### NewTest-41：补 `ResolveDeclaredImports` 在源模块消失后的解绑与恢复路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::ResolveDeclaredImports`、`FAngelscriptEngine::DiscardModule` |
| 现有测试覆盖 | `Builder.ImportBinding` 只覆盖手工 `BindImportedFunction` 的 happy path，没有覆盖 `ResolveDeclaredImports` 在源模块被丢弃或函数消失时的 `UnbindImportedFunction` 分支 |
| 风险评估 | 如果 consumer module 在 source module 被 discard 后仍保留 stale import 绑定，后续执行可能命中悬空函数句柄；这类错误会直接污染热重载和模块重建后的行为 |
| 建议测试名 | `Angelscript.TestModule.Internals.Builder.ImportBinding.UnbindsAndRebindsStaleImports` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 场景描述 | 先建立 source/consumer 模块并成功执行导入函数，再丢弃 source module，调用 `Engine.ResolveDeclaredImports(ConsumerModule)` 验证导入被解绑；最后重建 source module 并再次调用 resolve，验证绑定可以恢复到新函数体。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；source V1 为 `int SharedValue() { return 77; }`，consumer 为 `import int SharedValue() from "BuilderImportSource"; int Entry() { return SharedValue(); }`。成功执行一次后调用 `Engine.DiscardModule(TEXT("BuilderImportSource"))`，再执行 `Engine.ResolveDeclaredImports(ConsumerModule)`；随后重建 source V2 为 `int SharedValue() { return 91; }` 并再次 resolve。 |
| 期望行为 | 初次执行 `Entry()` 返回 `77`；source 被 discard 且 resolve 后，`ExecuteIntFunction` 对 `Entry()` 不再成功完成，且 `ConsumerModule->BindAllImportedFunctions()` 返回 `asCANT_BIND_ALL_FUNCTIONS`；source V2 重建并 resolve 后，`Entry()` 再次可执行且返回 `91`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` + `FAngelscriptEngine::ResolveDeclaredImports` + `FAngelscriptEngine::DiscardModule` |
| 优先级 | P1 |

#### NewTest-42：补 `FAngelscriptTypeUsage::FromParam` / `FromReturn` 的 qualifier 映射测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptTypeUsage::FromParam`、`FAngelscriptTypeUsage::FromReturn` |
| 现有测试覆盖 | `DataType` 套件只验证原始 `asCDataType` 的 primitive / const / ref 比较，没有任何测试覆盖 runtime wrapper `FAngelscriptTypeUsage` 对 `const`、`in/out ref`、返回类型的映射 |
| 风险评估 | 如果 qualifier 在类型包装层被丢失，debugger、函数声明生成、UFunction 参数桥接和热重载签名对比都可能拿到错误的类型信息，现有套件不会报警 |
| 建议测试名 | `Angelscript.TestModule.Internals.DataType.TypeUsageQualifiers` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDataTypeTests.cpp` |
| 场景描述 | 编译一个同时包含 `const int&in`、`int&out`、普通 `bool` 参数和一个原始返回值函数的脚本模块，然后直接对白盒检查 `FAngelscriptTypeUsage` 的 `bIsConst`、`bIsReference` 和声明字符串。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `BuildModule`；脚本示例：`void Qualifiers(const int&in Input, int&out Output, bool Flag) { Output = Input; } int Produce() { return 7; }`。获取 `Qualifiers` 与 `Produce` 的 `asIScriptFunction*` 后，对参数 0/1/2 分别调用 `FAngelscriptTypeUsage::FromParam`，对返回值调用 `FAngelscriptTypeUsage::FromReturn`。 |
| 期望行为 | 参数 0 的 usage 为 `bIsConst=true`、`bIsReference=true`；参数 1 为 `bIsConst=false`、`bIsReference=true`；参数 2 两个 flag 都为 `false`；返回值 usage 同样无多余 qualifier，且 `GetAngelscriptDeclaration(FunctionArgument/FunctionReturnValue)` 输出与脚本声明一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `FAngelscriptTypeUsage::FromParam/FromReturn` |
| 优先级 | P1 |

#### NewTest-43：补位运算的移位与补码路径，避免 `Bits` 继续只覆盖 `|/&/^`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Types.Bits` 只验证 `|`、`&`、`^` 在一组正掩码上的结果，没有覆盖左移、带符号右移和 `~` 的组合行为 |
| 风险评估 | 一旦移位符号扩展、优先级或按位取反路径回归，现有 `TypeTests` 仍可能全部绿灯；这类问题会直接影响脚本中的 mask 和 flag 逻辑 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.Bits.ShiftAndComplement` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 在单个脚本入口里同时执行左移、负数右移和按位取反后再做掩码，编码成一个稳定返回值。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Run() { int Left = 1 << 4; int SignedRight = (-16) >> 2; int MaskedComplement = (~0x0F) & 0xFF; return Left * 10000 + (SignedRight + 10) * 100 + MaskedComplement; }`。 |
| 期望行为 | `Run()` 返回 `160840`，其中 `Left == 16`、`SignedRight == -4`、`MaskedComplement == 240`；任一路径出错都会改变编码结果。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-61 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 1, MissingErrorPath: 1, NoTestForSource: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-08 19:21)

### 一、现有测试问题

#### Issue-63：`Upgrade.EngineProperties` 只验证属性值 round-trip，没有验证关键 property 真正驱动语言行为

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Upgrade.EngineProperties` |
| 行号范围 | 125-201 |
| 问题描述 | 用例把 `asEP_AUTOMATIC_IMPORTS`、`asEP_FOREACH_SUPPORT`、`asEP_BOOL_CONVERSION_MODE` 等 property 写进去后，只通过 `GetEngineProperty()` 验证 getter 返回了同样的数值。它没有编译或执行任何脚本来证明这些 property 仍然会被 parser / compiler / runtime 消费。也就是说，只要 engine 内部把值存起来了，即使 `foreach` 开关、bool conversion 模式或 automatic imports 在实际编译路径上完全失效，这个测试仍会通过。 |
| 影响 | 该用例会把“property 存储正常但行为已断开”的回归误报成绿灯，尤其不利于发现升级兼容过程中最容易出问题的 property wiring；`Upgrade.EngineProperties` 这个名字也会高估实际保护范围。 |
| 修复建议 | 1）保留现有 getter/setter round-trip 断言，但补至少 2 个行为型探针：例如关闭 `asEP_FOREACH_SUPPORT` 后编译带 `foreach` 的脚本必须失败，重新打开后同脚本必须成功并执行出稳定结果；2）对 `asEP_AUTOMATIC_IMPORTS` 或 `asEP_BOOL_CONVERSION_MODE` 也补最小行为断言，避免只测存储层；3）若不想把多个 property 语义塞进同一用例，应把“数值 round-trip”与“行为回归”拆成独立测试名。 |

#### Issue-64：`DataType.ObjectHandles` 只检查 handle 标记，没有锁定底层类型和限定符

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDataTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.DataType.ObjectHandles` |
| 行号范围 | 62-80 |
| 问题描述 | 用例对白盒只断言了 `ActorValueType.IsObject()`、`SupportHandles()`、`ActorHandleType.IsObjectHandle()` 和 `CanBeInstantiable()`。它没有验证 `CreateObjectHandle()` 产物仍然指向原始 `AActor` type info，也没有覆盖 `const handle` / `handle-to-const` 这类 qualifier。若实现退化成“生成了某个 object handle，但底层类型指针错了、const 位丢了”，当前测试仍会绿灯。 |
| 影响 | `asCDataType` 一旦在 handle 包装层丢失真实目标类型或限定符，后续绑定生成、声明字符串、调试器显示和参数匹配都可能用错类型；当前 `ObjectHandles` 用例对这些关键元数据没有保护。 |
| 修复建议 | 1）在现有断言后补 `ActorHandleType.GetTypeInfo() == ActorType` 一类白盒检查，锁定 handle 仍绑定到正确的 `AActor` type；2）追加 `CreateObjectHandle(ActorType, true)` 或等价 API 的断言，验证只读限定符不会在 handle 包装时丢失；3）顺手对 `CreateNullHandle()` 与普通 object handle 做不等价比较，避免“任何 handle 都算通过”的宽松判断。 |

### 二、需要新增的测试

### 本轮校正说明

`NewTest-44` 至 `NewTest-46` 和对应的 `### 本轮汇总` 已经写入本文前部，但由于本次追加时命中了前文已有的同名标题，正文被插入到了较早位置而不是当前文件尾部。

为遵守“只追加不覆盖”，这里不重复正文，也不改动前面已有内容；后续整理文档时，应将 `NewTest-44`、`NewTest-45`、`NewTest-46` 与它们后面的汇总表视为 `## 测试审查 (2026-04-08 19:21)` 这一轮的新增发现。

---

## 校正说明 (2026-04-08 19:56)

`## 测试审查 (2026-04-08 19:39)` 这一轮新增内容已经写入文档，编号范围为 `Issue-65` 至 `Issue-67`、`NewTest-47` 至 `NewTest-50`。

该段正文此前因补丁再次命中旧锚点，被插入到了文档前部：`## 校正说明 (2026-04-08 19:55)` 位于约第 211 行，而对应的 `## 测试审查 (2026-04-08 19:39)` 位于约第 234 行。本次仅在真实 EOF 追加定位说明和对应汇总，不重复正文，也不改动前面任何已有内容；后续整理文档时，应将约第 234 行起的 `## 测试审查 (2026-04-08 19:39)` 视为本轮最新发现。

### 本轮汇总（对应 `## 测试审查 (2026-04-08 19:39)`）

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-65 |
| WeakAssertion | 2 | Issue-66 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | MissingEdgeCase: 3, MissingScenario: 1 |

---

## 校正说明 (2026-04-08 20:25)

`Issue-68` 与对应的 `## 测试审查 (2026-04-08 20:18)` 已经写入文档，但由于补丁再次命中了前文重复的汇总表锚点，正文被插入到了文档前部：`## 校正说明 (2026-04-08 20:20)` 当前位于约第 234 行，随后紧跟着 `## 测试审查 (2026-04-08 20:18)`。

本次仅在真实 EOF 追加定位说明，不重复 `Issue-68` 正文，也不改动前面任何已有内容；后续整理文档时，应将前部的 `## 测试审查 (2026-04-08 20:18)` 视为本轮最新发现。

---

## 测试审查 (2026-04-08 20:33)

### 一、现有测试问题

#### Issue-69：`Execution.Context` 只验证单次 `Prepare -> Execute -> Finished` happy path，无法代表完整 context 生命周期

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.Context` |
| 行号范围 | 321-387 |
| 问题描述 | 用例先确认能创建两个不同的 `asIScriptContext*`，随后只对白盒检查 `ContextA` 的 `UNINITIALIZED -> PREPARED -> FINISHED` 状态跃迁。它没有覆盖 `Abort()` / `Suspend()` 在未准备状态下的返回码，没有调用 `Unprepare()` 验证状态回退，也没有复用同一个 context 执行第二个函数。结果是“context 生命周期”这个标题覆盖面远大于真实断言，当前测试实际上只保护了最窄的一条 happy path。 |
| 影响 | 一旦 context 在 guard path、`Unprepare` 回退或二次 `Prepare` 复用上回归，执行套件仍会全绿；后续只有更高层的执行测试偶然命中这些状态时才会暴露问题，定位成本明显升高。 |
| 修复建议 | 1）把当前用例重命名为更准确的 `ContextHappyPath`，避免继续高估覆盖；2）更推荐直接在同文件补齐 `Abort()` / `Suspend()` / `Unprepare()` / reuse 断言，让测试名与实际范围重新对齐；3）若担心单用例过重，可把 happy path 与 lifecycle matrix 拆成两个独立用例，但都保留在 `ExecutionTests.cpp`。 |

#### Issue-70：`Core.GlobalState` 实际只覆盖只读常量访问，测试名却暗示了更广的全局状态语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Core.GlobalState` |
| 行号范围 | 31-49 |
| 问题描述 | 用例脚本只有 `const int g_Count = 3; int Step(int Value) { return Value + 4; } int Run() { return Step(g_Count); }`，最终只断言结果为 `7`。这证明了“常量全局可被读取”，但没有覆盖可写 global 的初始化、跨多次执行的持久化、通过 `GetAddressOfGlobalVar()` 读取底层存储，甚至连 global 值是否真来自模块状态都无法区分。 |
| 影响 | 模块级全局存储一旦在写回、重复执行或地址暴露路径上回归，当前核心执行套件不会报警；`Core.GlobalState` 这个名字会误导后续审查，以为 global semantics 已经被完整覆盖。 |
| 修复建议 | 1）若保留当前脚本，应将测试名收窄为 `ConstGlobalRead` 之类更准确的名称；2）更推荐把脚本改成可变 global，并对同一模块连续执行两次增量函数，再断言最终读值；3）若目标是验证 runtime module storage，还应补 `GetAddressOfGlobalVar()` 的白盒断言，把“值留在模块里”而不是“恰好返回了常量”直接暴露出来。 |

### 二、需要新增的测试

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-69 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |

---

## 测试审查 (2026-04-08 20:46)

### 一、现有测试问题

#### Issue-71：`DataType.SizeAndAlignment` 标题覆盖范围过大，实际只采样了 3 个 primitive 类型

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDataTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.DataType.SizeAndAlignment` |
| 行号范围 | 85-94 |
| 问题描述 | 用例只对白盒断言了 `int` 的 dword 大小、`float64` 的 byte 大小和 `bool` 的对齐。它没有覆盖 `float32`、reference、object handle、null handle，也没有验证绑定 UE 类型或脚本对象在 `GetSizeInMemory*()` / `GetAlignment()` 上的结果。结果是“SizeAndAlignment”这个标题暗示了更广的布局保护，但当前实际只覆盖了 3 个最基础的 primitive 样本。 |
| 影响 | 一旦类型布局在 handle、reference 或对象路径上回归，参数编组、stack slot 计算和调试展示都可能出错，而当前 `DataType` 套件仍会保持绿灯；问题会被推迟到更高层脚本执行阶段才暴露。 |
| 修复建议 | 1）若维持当前最小样本，应将测试名收窄为 `PrimitiveSizeAndAlignment`；2）更推荐扩成矩阵断言，至少覆盖 `float32`、`AActor` handle、null handle 和一个 reference type；3）对需要 engine 的对象类型，可切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 后通过 `GetTypeInfoByName()` 取 `AActor`，再对白盒验证 handle / object / ref 的尺寸与对齐结果。 |

### 二、需要新增的测试

#### NewTest-51：补 zero-size object 的按值传参与局部布局探针，避免 `Objects.ZeroSize` 继续停留在常量返回

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Objects.ZeroSize` 只声明 `EmptyObject Instance` 后返回常量 `1`，没有覆盖 zero-size value 的按值传参、多个局部实例并存，或相邻局部变量布局是否稳定 |
| 风险评估 | 如果 empty class 在 stack layout、parameter passing 或临时值构造上退化，当前对象模型套件不会报警；业务侧会在极少见的空 marker type 场景里遇到难定位的执行错误 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Objects.ZeroSize.ByValueAndLocalLayout` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 场景描述 | 定义空脚本类 `EmptyObject`，让两个独立实例夹在普通局部变量之间，并分别按值传给 helper 函数，把结果编码成一个稳定整数。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`class EmptyObject {} int Accept(EmptyObject Value) { return 2; } int Run() { int Prefix = 5; EmptyObject First; int Middle = 6; EmptyObject Second; return Prefix * 1000 + Middle * 100 + Accept(First) * 10 + Accept(Second); }`。 |
| 期望行为 | `Run()` 返回 `5622`；这同时证明 zero-size locals 不会破坏相邻整数局部的布局，并且按值传参路径可稳定执行两次。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-71 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 11:49)

### 一、现有测试问题

#### Issue-105：`Memory.ScriptNodeReuse` / `Memory.ByteInstructionReuse` 在前置分配失败后仍继续 free/reuse 路径

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptMemoryTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Memory.ScriptNodeReuse`、`Angelscript.TestModule.Internals.Memory.ByteInstructionReuse` |
| 行号范围 | 68-94 |
| 问题描述 | 两个用例都先执行 `TestNotNull(...)`，但没有根据断言结果提前返回：`FirstAllocation = Manager.AllocScriptNode(); TestNotNull(..., FirstAllocation); Manager.FreeScriptNode(FirstAllocation);` 与 `AllocByteInstruction()` 路径完全相同。如果底层分配未来回归为 `nullptr`，测试仍会继续把空指针送进 `FreeScriptNode` / `FreeByteInstruction`，再去检查 pool size 和“重用同一指针”。这会把真正的根因从“分配失败”放大成后续一串噪声断言，甚至把空指针错误地写入池里。 |
| 影响 | 一旦 memory manager 的分配路径退化，测试输出会被后续 free/reuse 断言污染，根因定位明显变差；更糟的是，具体实现如果不接受空指针，还可能把单纯的功能回归放大成崩溃或未定义行为。 |
| 修复建议 | 1）把两个用例都改成 guard 风格：`if (!TestNotNull(..., FirstAllocation)) { return false; }`；2）只有在拿到非空分配后再执行 `Free*`、pool size 与 reuse 断言；3）若后续继续扩展 Memory 套件，建议抽一个小 helper，统一“分配成功前不得进入复用路径”的模式。 |

#### Issue-106：`Bytecode.InstructionSequence` 在 `GetFirstInstr()` 为空时会直接解引用，把单一失败放大成崩溃风险

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBytecodeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Bytecode.InstructionSequence` |
| 行号范围 | 58-61 |
| 问题描述 | 用例先断言 `TestNotNull(TEXT(\"Bytecode should expose the first instruction\"), ByteCode.GetFirstInstr());`，下一行立刻再次调用 `ByteCode.GetFirstInstr()->op` 做 opcode 比较。也就是说，一旦 bytecode 回归导致 `GetFirstInstr()` 返回 `nullptr`，测试不会停在“首条指令缺失”这个明确失败，而是继续落到空指针解引用。 |
| 影响 | 原本应当是可定位的单条断言失败，会被放大成访问违规、崩溃或额外噪声错误，降低 `Bytecode.InstructionSequence` 在底层 bytecode 退化时的诊断价值。 |
| 修复建议 | 1）把 `GetFirstInstr()` 结果缓存到局部变量 `FirstInstr`；2）改成 `if (!TestNotNull(..., FirstInstr)) { return false; }` 后再读取 `FirstInstr->op`；3）顺手避免重复调用 getter，让“首条指令存在”与“opcode 正确”形成清晰的前置关系。 |

### 二、需要新增的测试

无新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 2 | Issue-106 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-08 23:42)

### 一、现有测试问题

#### Issue-72：`Handles.RefArgument` 实际没有覆盖任何 handle 语义，测试名与内容脱节

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Handles.RefArgument` |
| 行号范围 | 117-156 |
| 问题描述 | 用例脚本只有 `void Modify(int &out Value) { Value = 42; } int Test() { int Value = 0; Modify(Value); return Value; }`，整个执行路径完全没有 `@` handle、对象引用、nullable object 或任何 handle-specific API。它验证的是 primitive `out-ref` 写回，而不是 handle 参数传递、handle nullability、或 `SetArgAddress/SetArgObject` 相关的运行时语义。 |
| 影响 | 当前报告表面上看像是 `HandleTests` 已经覆盖了“引用参数”这类 handle 场景，实际上真正高风险的对象 handle 传参/写回路径仍然是空白；一旦 handle marshaling 或对象引用封送回归，这个用例不会报警。 |
| 修复建议 | 1）把现有用例移动到更准确的 suite，例如 `Execution` 或 `Functions`，并改名为 `RefArgument.OutPrimitive`; 2）在 `HandleTests.cpp` 里新增真正的对象/handle 参数测试，至少让被测脚本涉及 object handle 或 native UObject reference；3）如果当前分支暂时不支持 script-class handle，仍应选择一个已支持的 native reference type 做正向断言，避免继续用 `int &out` 冒充 handle 覆盖。 |

#### Issue-73：`Types.ImplicitCast` 只覆盖单个正数 `int -> float/double` widening，远不足以代表隐式转换语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.ImplicitCast` |
| 行号范围 | 321-359 |
| 问题描述 | 用例只在 `float32` / `float64` 两种配置下编译 `int Value = 42; float|double Converted = Value; return Converted;`，最后通过 `ReadExpectedFloatResult(..., 42.0)` 验证结果。它只能证明“正整数隐式 widening 到浮点后数值没变”，没有覆盖负数符号保留、较大整数的精度边界、`bool -> int/float` 之类常见隐式转换，也没有验证 implicit cast 发生在表达式/参数传递场景时的行为。 |
| 影响 | 一旦隐式转换在符号位、精度截断或参数匹配路径上回归，`TypeTests` 仍可能保持全绿；`Types.ImplicitCast` 这个宽泛名字会明显高估当前套件对转换规则的保护。 |
| 修复建议 | 1）若保留当前最小脚本，应将测试名收窄为 `ImplicitCast.IntToFloat.Positive`; 2）更推荐把脚本改成矩阵断言，至少同时编码 `-7 -> -7.0`、`42 -> 42.0` 和一个参数传递型 implicit cast； 3）若项目希望把“值保持”与“重载/参数匹配”拆开，可保留现用例验证 widening，再补独立测试覆盖 overload resolution 中仅允许 implicit cast 的分派结果。 |

#### Issue-74：`Functions.DefaultArguments` 只验证“省略最后一个参数”这一条 happy path，覆盖面和测试名不匹配

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Functions.DefaultArguments` |
| 行号范围 | 17-37 |
| 问题描述 | 用例脚本只有 `int Add(int A, int B = 5) { return A + B; } int Run() { return Add(7); }`，最终只断言结果为 `12`。这只能证明“调用方省略尾参数时能取到默认值”，没有覆盖显式传参覆盖默认值、多个默认参数的裁剪规则，也没有验证默认值和 named argument / overload resolution 的交互。 |
| 影响 | 默认参数展开如果只在最简单的尾参省略场景里工作，而在显式 override、多个默认值或与 named arguments 混用时回归，当前 `FunctionTests` 不会报警；测试名也会误导后续审查，以为 default-argument 语义已经被整体覆盖。 |
| 修复建议 | 1）保留当前用例时应把测试名收窄为 `DefaultArguments.TrailingOmitted`; 2）更推荐把脚本扩成顺序敏感编码，同时断言 `Add(7)` 和 `Add(7, 9)` 两条路径；3）再补一个带多个默认参数或 named argument 的 companion test，锁定“只允许从尾部裁剪默认值”和“显式传参优先于默认值”这两条规则。 |

### 二、需要新增的测试

#### NewTest-52：补真正的 native object 参数传递测试，直接覆盖 `SetArgObject` 的 null / non-null 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::SetArgObject` |
| 现有测试覆盖 | `Handles.Basic`、`Handles.Auto` 都停在 script-class handle 不支持的 negative path；`Handles.RefArgument` 又只覆盖 `int &out`，没有任何现有测试真正命中 object 参数封送 |
| 风险评估 | 如果 native object 参数在 context 调用层被写错槽位、丢失 null 语义或根本没有走到对象封送逻辑，当前 `HandleTests` 会继续给出误导性的绿灯 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Handles.NativeObjectArgument.NullAndNonNull` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp` |
| 场景描述 | 编译一个只接收 `UObject` 参数并返回 `Value != nullptr ? 1 : 0` 的最小脚本函数，分别从 C++ 用真实 `UObject*` 和 `nullptr` 调用两次，直接验证对象参数封送。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 isolated clone engine；脚本示例：`int Test(UObject Value) { return Value != nullptr ? 1 : 0; }`。C++ 侧通过 `BuildModule` + `GetFunctionByDecl("int Test(UObject)")` 拿到函数，创建 `UObject* Instance = NewObject<UObject>()`，然后对同一个 `asIScriptContext` 分两次 `Prepare`，一次 `SetArgObject(0, Instance)`，一次 `SetArgObject(0, nullptr)`。 |
| 期望行为 | 两次 `Prepare/Execute` 都返回成功；non-null 调用返回 `1`，null 调用返回 `0`。这两个断言共同证明对象参数的 non-null 与 null 两条路径都经过了真实的 `SetArgObject` 封送，而不是继续停留在 primitive `out-ref` 冒烟。 |
| 使用的 Helper | `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `SetArgObject` |
| 优先级 | P1 |

#### NewTest-53：补 `Types.ImplicitCast` 的负数与参数传递矩阵，锁定隐式 widening 的符号保持

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `Types.ImplicitCast` 只验证 `42 -> 42.0` 的赋值型 widening，没有任何负数、参数传递或多路径编码断言 |
| 风险评估 | 一旦 implicit cast 在负号保留、参数绑定或 float32/float64 分支上退化，现有 `TypeTests` 仍会全绿，直到更复杂脚本偶然命中才暴露问题 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.ImplicitCast.NegativeAndParamWidening` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 在同一脚本里同时覆盖“赋值型 implicit cast”和“函数实参 implicit cast”两条路径，并把负号保留与数值保持编码成单个稳定整数。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本按 `asEP_FLOAT_IS_FLOAT64` 分两版生成。float64 模式示例：`double Accept(double Value) { return Value; } int Run() { int Negative = -7; double Assigned = Negative; double Forwarded = Accept(Negative); return (Assigned < 0.0 ? 1 : 0) * 100 + (int(Assigned) == -7 ? 1 : 0) * 10 + (int(Forwarded) == -7 ? 1 : 0); }`；float32 模式把 `double` 全部改成 `float`。 |
| 期望行为 | `Run()` 返回 `111`；任一 implicit cast 丢失符号、值不再保持，或参数路径与赋值路径行为不一致，都会直接改变编码结果。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-54：补 `GetObjectInGC` 的非空查询回归，避免 `GC.InvalidLookup` 继续只测空 collector

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptEngine::NotifyGarbageCollectorOfNewObject`、`asIScriptEngine::GetObjectInGC` |
| 现有测试覆盖 | `GC.InvalidLookup` 只覆盖空 collector 上的越界查询；`ManualCycleCollection` / `CycleDetection` 虽然创建了真实 GC 对象，但从未读取 tracked entry 元数据 |
| 风险评估 | 如果 GC 跟踪表索引、`seqNbr` 回填或 type pointer 维护损坏，调试/诊断侧依赖的 `GetObjectInGC` 会失效，而当前内部测试不会报警 |
| 建议测试名 | `Angelscript.TestModule.Internals.GC.GetObjectInGC.TrackedCycle` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 场景描述 | 复用现有 `FGCProbeObject` helper 创建一个 self-cycle，释放外部引用让对象仅由 GC 跟踪，然后在回收前后分别调用 `GetObjectInGC(0, ...)`。 |
| 输入/前置 | 使用现有 `RegisterGCProbeType`、`CreateSelfCycle`、`GetGCStatisticsSnapshot` 和 `RunFullGarbageCollection`；创建 self-cycle 后先记录 `CurrentSize`，再调用 `Root->Release()`，在 full collect 前调用 `GetObjectInGC(0, &SeqNbr, &Object, &Type)`。 |
| 期望行为 | 回收前 `CurrentSize >= 1` 且 `GetObjectInGC(0, ...) == asSUCCESS`，返回的 `Object != nullptr`、`Type == GCProbeType`、`SeqNbr > 0`；full collect 结束后 `CurrentSize == 0`，再次 `GetObjectInGC(0, ...)` 返回 `asINVALID_ARG`。 |
| 使用的 Helper | 现有 `RegisterGCProbeType` + `CreateSelfCycle` + `GetGCStatisticsSnapshot` + `RunFullGarbageCollection` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-72 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingEdgeCase: 1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:45)

### 一、现有测试问题

无新增。

### 二、需要新增的测试

#### NewTest-84：补 `Execution.Basic` 的 void entry 可观察副作用，避免 void 路径继续停留在纯状态机冒烟

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `Execution.Basic` 虽然手动执行了 `void TestVoid()`，但函数体是空的，C++ 侧只断言 `Prepare/Execute` 返回码；当前没有任何用例让 void entry 产生可观察的脚本副作用 |
| 风险评估 | 如果 void 函数 dispatch、全局写回或 script side effect 路径退化，而返回值函数仍然工作，现有执行套件很难把问题直接定位到“void entry 真没有跑”这一层 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.Basic.VoidEntrySideEffect` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionBasicSemanticTests.cpp` |
| 场景描述 | 编译包含一个 void 入口和一个读取全局状态入口的最小模块，先从 C++ 手动执行 void 函数，再执行读值函数，证明 void path 确实改变了脚本可观察状态 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule` + `GetFunctionByDecl`。脚本建议为：`int g_Value = 0; void TestVoid() { g_Value = 7; } int ReadValue() { return g_Value; }`。先对 `void TestVoid()` 创建 context 并执行，再通过现有 `ExecuteIntFunction` 或第二个 context 调用 `int ReadValue()`。 |
| 期望行为 | `TestVoid()` 的 `Prepare/Execute` 都成功；随后 `ReadValue()` 返回 `7`。这证明当前测试覆盖的不只是“void entry 可执行”，而是“void entry 真的运行并修改了脚本状态”。 |
| 使用的 Helper | `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `ExecuteIntFunction` |
| 优先级 | P2 |

#### NewTest-85：补 `BasicTokens` 的 float/bits/punctuation 矩阵，锁住 tokenizer 最基础的 literal 与标点分类

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_tokendef.h` |
| 关联函数 | `asCTokenizer::GetToken` |
| 现有测试覆盖 | `Tokenizer.BasicTokens` 目前只覆盖 identifier、整数字面量、字符串和 `+`；没有任何用例验证 `float32/float64` literal、bits constant 与最常见的 statement punctuation |
| 风险评估 | 一旦 tokenizer 在 `1.25f`、`1.25`、`0xFF`、`(`、`;` 这些最基本 token 上分类错误，parser 层会出现大面积连锁失败，而当前 tokenizer 套件无法快速把问题归因到“literal/punctuation token 分类” |
| 建议测试名 | `Angelscript.TestModule.Internals.Tokenizer.BasicLiteralAndPunctuationMatrix` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 场景描述 | 复用现有 `FTokenizerAccessor`，补充最小但覆盖面明确的 literal/punctuation 样本，要求同时验证 token 类型和长度 |
| 输入/前置 | 使用 `FTokenizerAccessor Tokenizer; size_t TokenLength = 0;`。最小输入建议包括：`"1.25f"`、`"1.25"`、`"0xFF"`、`"("`、`")"`、`";"`、`","`。 |
| 期望行为 | `1.25f -> ttFloat32Constant` 且长度 `5`；`1.25 -> ttFloat64Constant` 且长度 `4`；`0xFF -> ttBitsConstant` 且长度 `4`；`( -> ttOpenParanthesis`、`) -> ttCloseParanthesis`、`; -> ttEndStatement`、`, -> ttListSeparator`，这些标点长度都应为 `1`。 |
| 使用的 Helper | 现有 `FTokenizerAccessor` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 01:44)

### 一、现有测试问题

本轮未新增现有测试质量问题；`NewTest-114` 的正文已记录在前文 `## 测试审查 (2026-04-10 01:42)` 小节，这里补真实 EOF 锚点并继续追加 `GetDebuggerValueFromFunction` 的继承链场景。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-114`，保留在前文 `## 测试审查 (2026-04-10 01:42)` 小节；此处不重复展开。

#### NewTest-115：补 derived getter 追踪 base property 地址的回归，直达 `GetDebuggerValueFromFunction` 的继承链搜索分支

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptType::GetDebuggerValueFromFunction` |
| 现有测试覆盖 | `NewTest-25` 只规划“getter 与 property 在同一类上”的地址跟踪 happy path，`NewTest-108` 覆盖 blacklist / 参数 guard，`NewTest-114` 覆盖 global ref-return 的 `ValueRef` 直达地址路径。当前仍没有任何正式测试要求 `GetDebuggerValueFromFunction()` 第 754-825 行在 `ContainerScriptType` / `ContainerClass` 上沿 `derivedFrom`、`shadowType` 或 UE super-field 链向上搜索 inherited property 并绑定监视地址。 |
| 风险评估 | 一旦 derived script class 的 getter 返回的是 base class property，而地址搜索没有沿继承链找到正确字段，debugger watch 虽然还能显示一次性的值，却会丢失 live monitor 地址或绑定到错误属性；这类问题在继承、热重载和调试器自动求值组合场景里会非常隐蔽。 |
| 建议测试名 | `Angelscript.TestModule.Internals.DebuggerValue.GetterTracksInheritedPropertyAddress` |
| 测试类型 | Integration |
| 测试文件 | 追加到规划中的 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDebuggerValueTests.cpp` |
| 场景描述 | 编译一个最小模块，定义 `ADebuggerValueBaseProbe` 带 `UPROPERTY() int Health = 42;`，再定义 `ADebuggerValueDerivedProbe : ADebuggerValueBaseProbe`，只在 derived class 上声明 `int GetHealth() const { return Health; }`。对 derived actor 的 `GetHealth()` 调用 `FAngelscriptType::GetDebuggerValueFromFunction(...)`，并显式传入 `PropertyAddrToSearchFor = TEXT("Health")`，验证 helper 会把监视地址绑定到 base property 的真实存储。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileScriptModule` + `FActorTestSpawner`。编译后用 `FindGeneratedClass(&Engine, TEXT("ADebuggerValueDerivedProbe"))` 拿到 derived `UClass*`，再通过 `FAngelscriptType::GetBoundClassName(DerivedClass)` + `Engine.GetScriptEngine()->GetTypeInfoByName(...)` 取得 derived `asITypeInfo*`，并用 `GetMethodByDecl("int GetHealth() const")` 获取 `asIScriptFunction*`。spawn derived actor 后，用 `FindFProperty<FIntProperty>(DerivedClass, TEXT("Health"))` 取得继承自 base 的 property 指针；第一次求值后，把该属性改成 `99`，再做第二次求值。 |
| 期望行为 | 1）两次 `GetDebuggerValueFromFunction(...)` 都返回 `true`；2）第一次求值时 `DebugValueA.Value` 能解析为 `42`，且 `DebugValueA.NonTemporaryAddress` 或 `DebugValueA.AddressToMonitor` 指向 `Health` 的真实地址；3）把继承来的 `Health` 改成 `99` 后再次求值，`DebugValueB.Value` 能解析为 `99`，且监视地址保持指向同一底层字段。这样可以直接锁住 helper 在 inherited property 上的地址搜索，而不是只验证“getter 算出来了正确数值”。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH` + `CompileScriptModule` + `FActorTestSpawner` + `FindGeneratedClass` + `FindFProperty` + `FAngelscriptType::GetBoundClassName` + `FAngelscriptType::GetDebuggerValueFromFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-10 01:42)

### 一、现有测试问题

本轮未新增现有测试质量问题；增量发现集中在 `AngelscriptType.cpp` 里尚未命中的 debugger/ref-return 白盒路径。

### 二、需要新增的测试

#### NewTest-114：补 `GetDebuggerValueFromFunction` 的 ref-return 直达地址分支，锁住 `ValueRef` 到 `NonTemporaryAddress` 的映射

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptType::GetDebuggerValueFromFunction` |
| 现有测试覆盖 | `NewTest-25` 只规划了 value-return getter 的 property-address happy path，`NewTest-108` 只覆盖 blacklist / 非法签名 guard。当前没有任何正式测试直接命中 `GetDebuggerValueFromFunction()` 第 715-746 行的 `ReturnValue.bIsReference` / `ValueRef` 分支，也没有断言 helper 在 ref-return getter 上会把 `OutValue.NonTemporaryAddress` 绑定到真实 backing storage。 |
| 风险评估 | 如果 ref-return getter 在 debugger 求值时被错误复制成临时值、丢失真实地址，或 `NonTemporaryAddress` 指向了错误内存，watch/auto-evaluate 会显示陈旧值，后续监视刷新也无法跟随真实底层变量变化；现有 LanguageFeatures 套件不会对这条调试器核心路径报警。 |
| 建议测试名 | `Angelscript.TestModule.Internals.DebuggerValue.ReferenceReturnTracksBackingAddress` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDebuggerValueTests.cpp` |
| 场景描述 | 编译一个最小模块，定义 `int GlobalValue = 42; int& GetGlobalRef() { return GlobalValue; }`。直接获取 `int& GetGlobalRef()` 的 `asIScriptFunction*`，用 `FAngelscriptType::GetDebuggerValueFromFunction(Function, nullptr, DebugValue, nullptr, nullptr, TEXT(""))` 求值，验证 helper 走 ref-return 分支后能把 debugger 值绑定回真实 global 地址，而不是只生成一次性临时副本。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `BuildModule` + `GetFunctionByDecl`。模块名建议为 `ASDebuggerReferenceReturn`。编译后通过 `Module->GetAddressOfGlobalVar(0)` 拿到 `GlobalValue` 的真实地址，并准备 `FDebuggerValue DebugValueA/DebugValueB` 两次求值。第一次求值后，把该地址解释为 `int32*` 并在 C++ 侧把值从 `42` 改成 `99`，再调用第二次 helper。 |
| 期望行为 | 1）两次调用 `GetDebuggerValueFromFunction(...)` 都返回 `true`；2）第一次返回后 `DebugValueA.bTemporaryValue == true`，且 `DebugValueA.NonTemporaryAddress == Module->GetAddressOfGlobalVar(0)`；3）`DebugValueA.Value` 能解析为 `42`；4）把底层 global 改成 `99` 后再次求值，`DebugValueB.NonTemporaryAddress` 仍指向同一地址，`DebugValueB.Value` 能解析为 `99`。这组断言直接锁住 ref-return getter 的真实地址跟踪，而不是只看字符串化结果。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `asIScriptModule::GetAddressOfGlobalVar` + `FAngelscriptType::GetDebuggerValueFromFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:31)

### 一、现有测试问题

#### Issue-104：`Tokenizer.BasicTokens` 只采样 4 类最基础 token，测试名明显高估覆盖面

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Tokenizer.BasicTokens` |
| 行号范围 | 38-54 |
| 问题描述 | 用例当前只对白盒检查了 4 个样本：`Identifier123`、`12345`、`"abc"` 和 `"+"`。这意味着 `BasicTokens` 实际既没有覆盖 `float32/float64` literal、`0xFFFF` bits constant，也没有覆盖括号、分号、逗号之类最常见的 statement token。测试名却直接使用宽泛的 `BasicTokens`，容易让人误以为 tokenizer 的基础 token 家族已有系统保护。 |
| 影响 | 一旦 tokenizer 在 literal 分类、标点 token 或 bits/float 常量识别上回归，当前内部测试可能仍全部通过；后续 parser 失败时也很难第一时间定位到是 token 分类退化而不是 AST/builder 问题。 |
| 修复建议 | 1）若保留当前最小样本，应把测试名收窄为更准确的 `BasicIdentifierIntegerStringAndPlus`; 2）更推荐把同一用例扩成 matrix，至少补 `ttFloat32Constant`/`ttFloat64Constant`、`ttBitsConstant`、`ttOpenParanthesis`、`ttEndStatement` 以及对应长度断言； 3）若担心单用例过宽，可把 literal 与 punctuation 拆成两个更聚焦的 tokenizer 用例。 |

### 二、需要新增的测试

#### NewTest-81：补 `SetArgAddress` 的 `&in` / `&out` 原生编组回写，直达当前无测试的地址参数 API

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::SetArgAddress` |
| 现有测试覆盖 | `Handles.RefArgument` 只覆盖脚本内部 `Modify(Value)` 的 `int &out` 写回；当前没有任何用例从 C++ 侧直接调用 `SetArgAddress`，也没有锁定 `&in` 读取与 `&out` 写回同时存在时的原生地址编组 |
| 风险评估 | 如果 native caller 通过地址传递 `ref/in/out` 参数时发生地址错绑、写回丢失或只读参数被意外修改，现有 LanguageFeatures 套件不会直接报警；这类问题会首先出现在反射桥接、调试器调用和低层 helper 上，定位成本高 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.RefAddressRoundTrip` |
| 测试类型 | Integration |
| 测试文件 | 追加到新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionArgumentMarshallingTests.cpp` |
| 场景描述 | 编译一个同时使用 `const int&in` 和 `int&out` 的最小函数，从 C++ 侧通过同一个 `asIScriptContext` 使用 `SetArgAddress` 传入两个本地变量地址，验证输入值被正确读取、输出值被正确写回、只读输入不被污染 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule` + `GetFunctionByDecl("int UseRefs(const int&in, int&out)")` + `Engine.CreateContext()`。脚本建议为：`int UseRefs(const int&in Input, int&out Output) { Output = Input + 5; return Output * 2; }`。C++ 侧准备 `int32 Input = 10; int32 Output = -1;`，随后 `Context->SetArgAddress(0, &Input); Context->SetArgAddress(1, &Output);`。 |
| 期望行为 | `Prepare/Execute` 都返回成功，返回值为 `30`，`Output` 被写成 `15`，`Input` 仍保持 `10`。这组断言直接证明 `SetArgAddress` 同时覆盖了 `&in` 的读取与 `&out` 的原地写回语义。 |
| 使用的 Helper | `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `Context->SetArgAddress` + `Context->GetReturnDWord` |
| 优先级 | P1 |

#### NewTest-82：补 `SetArgByte` / `SetArgWord` 与 `GetReturnByte` / `GetReturnWord` 的窄整型 round-trip

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::SetArgByte`、`asIScriptContext::SetArgWord`、`asIScriptContext::GetReturnByte`、`asIScriptContext::GetReturnWord` |
| 现有测试覆盖 | `TypeTests` 只验证脚本内的 `int8`/整数运算；`ExecutionTests` 目前只直接覆盖 `SetArgDWord`、`SetArgFloat`、`SetArgQWord` 与 `SetArgObject` 路径，没有任何用例锁住 8/16-bit 原生参数与返回值 API |
| 风险评估 | 如果窄整型的参数宽度、符号/零扩展或返回槽读取发生回归，绑定层与 native 调用封装会悄悄把 `uint8/uint16` 参数读错，而现有执行套件仍可能全绿 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.ByteAndWordRoundTrip` |
| 测试类型 | Integration |
| 测试文件 | 追加到新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionArgumentMarshallingTests.cpp` |
| 场景描述 | 编译两个最小函数，分别以 `uint8` 和 `uint16` 作为参数与返回值，从 C++ 侧手动使用 `SetArgByte` / `SetArgWord` 驱动 context，验证返回值必须通过对应的 `GetReturnByte` / `GetReturnWord` 读回且保持位宽正确 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule`。脚本建议为：`uint8 AddByte(uint8 Value) { return Value + 1; } uint16 AddWord(uint16 Value) { return Value + 2; }`。通过 `GetFunctionByDecl("uint8 AddByte(uint8)")` 与 `GetFunctionByDecl("uint16 AddWord(uint16)")` 分别准备 context；输入值建议使用 `250` 和 `4095`，避免退化成普通小整数 happy path。 |
| 期望行为 | `AddByte(250)` 返回 `251`，必须通过 `GetReturnByte()` 读到；`AddWord(4095)` 返回 `4097`，必须通过 `GetReturnWord()` 读到；两次执行都应得到 `asEXECUTION_FINISHED`。这能直接锁住 byte/word 参数与返回槽的专用 API，而不是继续被 `DWord` 路径间接覆盖。 |
| 使用的 Helper | `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `Context->SetArgByte` / `SetArgWord` + `Context->GetReturnByte` / `GetReturnWord` |
| 优先级 | P2 |

#### NewTest-83：补 tokenizer 的 longest-match 运算符矩阵，避免多字符 token 长度与类型回归继续漏检

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_tokendef.h` |
| 关联函数 | `asCTokenizer::GetToken` |
| 现有测试覆盖 | `BasicTokens` 只验证 `+` 这种单字符 token；`Keywords`、`CommentsAndStrings`、`ErrorRecovery` 也都没有覆盖 `::`、`**=`, `>>=`, `>>>=`, `&&`, `||` 这类 longest-match 多字符运算符 |
| 风险评估 | 一旦 tokenizer 在最长匹配规则上退化，例如把 `>>=` 切成 `>>` + `=`、把 `::` 错认成 `:`、把 `**=` 错认成 `**`，namespace、pow、shift-assign 等语言特性都会大面积受影响，而当前 tokenizer 套件仍可能保持绿灯 |
| 建议测试名 | `Angelscript.TestModule.Internals.Tokenizer.LongestMatchOperators` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 场景描述 | 使用现有 `FTokenizerAccessor` 逐个解析代表性的多字符运算符，要求同时验证 token 类型和返回长度，确保 tokenizer 总是走最长匹配而不是被前缀 token 提前截断 |
| 输入/前置 | 复用当前文件里的 `FTokenizerAccessor` 与 `size_t TokenLength`。最小输入建议至少覆盖：`"::"`、`"**"`、`"**="`、`">>="`、`">>>="`、`"&&"`、`"||"`。 |
| 期望行为 | `:: -> ttScope` 且长度 `2`；`** -> ttStarStar` 且长度 `2`；`**= -> ttPowAssign` 且长度 `3`；`>>= -> ttShiftRightLAssign` 且长度 `3`；`>>>= -> ttShiftRightAAssign` 且长度 `4`；`&& -> ttAnd` 且长度 `2`；`|| -> ttOr` 且长度 `2`。这些断言能直接锁死 longest-match 与 token-length 两个最容易退化的点。 |
| 使用的 Helper | 现有 `FTokenizerAccessor` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-104 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingEdgeCase: 1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:06)

### 一、现有测试问题

#### Issue-103：`TwoArgs` / `FourArgs` / `MixedArgs` 都用可交换求和断言，实际锁不住参数槽位与类型编组

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.TwoArgs`、`FourArgs`、`MixedArgs` |
| 行号范围 | 124-169, 179-226, 236-316 |
| 问题描述 | 这 3 个用例的脚本都把参数直接做加法后返回：`A + B`、`A + B + C + D`、`A + B + C`。由于加法天然可交换，测试即使通过，也无法证明参数确实按槽位顺序写入。比如 `TwoArgs` 里的 `20` 和 `22` 互换仍然得到 `42`；`FourArgs` 的 4 个 `int` 只要总和还是 `42` 就会绿灯；`MixedArgs` 也存在同样问题，`10 + 20.5 + 12` 无法区分 `SetArgDWord(0)`、`SetArgFloat/SetArgQWord(1)`、`SetArgDWord(2)` 是否真的写到了正确位置。测试名是 `Execute.*Args`，但当前断言更像“加法求和能跑通”的冒烟，而不是 ABI / marshalling 验证。 |
| 影响 | 一旦 `asIScriptContext` 的参数槽位映射、`float32/float64` 位置、`QWord`/`DWord` 写入顺序发生回归，这几条执行测试仍可能继续绿灯，导致最基础的调用约定问题只能在更复杂脚本里晚暴露。 |
| 修复建议 | 1）把脚本改成顺序敏感编码，例如 `int Test(int A, int B) { return A * 100 + B; }`、`int Test(int A, int B, int C, int D) { return A * 1000 + B * 100 + C * 10 + D; }`；2）`MixedArgs` 使用带权编码而不是简单求和，例如 `A * 1000 + int(B * 10) + C`，并为 3 个槽位设置互不相同的输入；3）保留现有 smoke case 时，应额外新增专门的 marshalling 矩阵用例，避免继续让 commutative result 代表参数传递语义。 |

---

## 测试审查 (2026-04-08 23:50)

### 一、现有测试问题

#### Issue-75：`Functions.NamedArguments` 只覆盖“全部参数都改名重排”的 happy path，测试名覆盖面过宽

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Functions.NamedArguments` |
| 行号范围 | 40-58 |
| 问题描述 | 用例脚本固定为 `Mix(C: 3, A: 1, B: 2)`，最终只断言结果 `321`。这只能证明“所有实参都显式命名且完全乱序”时，绑定结果恰好正确；它没有覆盖更常见的 mixed positional+named 调用、只给部分参数命名、重复命名或未知参数名的错误路径。测试名却直接使用宽泛的 `NamedArguments`，会让人误以为命名参数语义已经整体受保护。 |
| 影响 | 如果 named argument 绑定只在“全部命名”场景里工作，而在 partial naming、和默认参数组合、或 duplicate-name diagnostics 上回归，当前 `FunctionTests` 仍会全绿；语言前端的参数匹配错误会被明显滞后地暴露。 |
| 修复建议 | 1）若保留现用例，应把测试名收窄为 `NamedArguments.FullReorderHappyPath`；2）更推荐在同文件补成矩阵断言，同时覆盖 `Mix(1, C: 3, B: 2)` 这类 mixed call 和 `Mix(B: 2, A: 1, C: 3)` 的 partial reorder；3）再补 negative compile 用例，显式断言 duplicate / unknown named argument 会返回 `ECompileResult::Error` 并带 diagnostics。 |

#### Issue-76：`Types.Conversion` 以“转换”泛称命名，但实际只验证单个正数 `3.7 -> 3`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.Conversion` |
| 行号范围 | 291-318 |
| 问题描述 | 用例根据 `asEP_FLOAT_IS_FLOAT64` 选择 `float` 或 `double` 版本的脚本，但具体行为始终只有 `3.7 -> int(Value)`，最终断言结果为 `3`。它没有覆盖负数 `-3.7 -> -3` 的 toward-zero 语义，没有覆盖 `0.x` 截断、也没有验证较大数值或 `float32/float64` 两条路径是否都真的参与了不同的 codegen。测试名却是宽泛的 `Types.Conversion`。 |
| 影响 | 一旦 numeric conversion 在负号保留、toward-zero 规则或不同浮点模式的 lowering 上回归，当前 `TypeTests` 很可能继续绿灯；这会高估语言层对显式转换语义的保护强度。 |
| 修复建议 | 1）若维持当前最小覆盖，应将测试名收窄为 `Conversion.PositiveFloatToInt`; 2）更推荐把脚本扩成顺序敏感编码，同时断言 `3.7 -> 3` 与 `-3.7 -> -3`；3）再补一个接近零的小数或 `float64` 专用断言，证明两种 engine property 分支都命中了真实转换逻辑，而不是只复用同一 happy path。 |

#### Issue-77：`Interface.NoProperty` 没有验证“接口声明属性会被拒绝”，与测试名语义不匹配

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.NoProperty` |
| 行号范围 | 211-248 |
| 问题描述 | 用例编译的是一个完全合法、只含 `void DoSomething();` 的 interface，然后遍历 `FProperty` 断言数量为 `0`。这只能证明“这个特定 interface 没有自动生成字段”，却没有触发真正重要的规则校验：如果用户在 `UINTERFACE` 里声明 `UPROPERTY()`，编译器是否会拒绝、diagnostics 是否准确。测试名 `NoProperty` 很容易让人误解成“接口属性被禁止”已经被验证。 |
| 影响 | 一旦 class generator / validator 错误放行 interface property，现有接口套件不会报警；用户会在更靠后的反射、热重载或 GC 场景里才遇到结构性问题，定位成本明显更高。 |
| 修复建议 | 1）若保留当前用例，应将测试名收窄为 `GeneratedInterfaceHasNoProperties`; 2）更推荐新增真正的 negative compile 场景，在 interface 体内声明 `UPROPERTY() int Value;`，显式断言编译失败和 diagnostics；3）若项目想同时保留正向元数据检查，可把“合法接口无字段”和“非法字段声明被拒绝”拆成两个独立用例。 |

#### Issue-78：`Compiler.FunctionCalls` 只验证运行结果为 `12`，没有证明编译器真的生成了正确 call path

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Compiler.FunctionCalls` |
| 行号范围 | 95-123 |
| 问题描述 | 用例脚本固定为 `int Add(int A, int B) { return A + B; } int Entry() { return Add(7, 5); }`，最后只断言执行结果等于 `12`。这在 runtime integration 层是合理冒烟，但在 `Internals.Compiler` 套件里过于薄弱：它没有验证 `Entry` 的 bytecode 里确实存在 call instruction，没有确认 callee 就是 `Add(int,int)`，也无法排除“表达式被常量折叠成 12”或误绑到另一个同结果函数体的情况。 |
| 影响 | 编译器在 call-site lowering、callee 绑定或参数入栈顺序上回归时，只要最终还碰巧返回 `12`，当前内部测试就会绿灯；这让 `Compiler.FunctionCalls` 对真正的 compiler 行为保护不足。 |
| 修复建议 | 1）保留现有执行断言，但额外对白盒检查 `Entry()` 的 bytecode，确认包含 call opcode 且长度大于纯常量返回；2）把脚本改成顺序敏感编码，例如 `return Add(7, 5) * 100 + Add(1, 2);` 或引入两个同名不同签名函数，避免“任何返回 12 的路径都算通过”；3）若底层 API 允许读取 call target，直接断言它指向 `Add(int, int)`，把 compiler 行为而非 runtime 结果锁住。 |

### 二、需要新增的测试

#### NewTest-55：补 mixed positional + named 参数绑定路径，避免 `NamedArguments` 继续只测全命名重排

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptFunction::GetParam(asUINT index, int *typeId, asDWORD *flags = 0, const char **name = 0, const char **defaultArg = 0) const` |
| 现有测试覆盖 | `Functions.NamedArguments` 只执行 `Mix(C: 3, A: 1, B: 2)`，没有任何 mixed positional + named 或 partial naming 场景 |
| 风险评估 | 参数名元数据与调用绑定一旦在“前半段位置参数、后半段命名参数”这种真实用法上回归，当前函数测试不会报警；脚本调用方会在语法看似合法时拿到错位实参 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Functions.NamedArguments.MixedPartialOrder` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 场景描述 | 保留 3 参数函数，但分别覆盖“第一个参数位置传递、后两个参数命名传递”和“只重排后两个命名参数”两条路径。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Mix(int A, int B, int C) { return A * 100 + B * 10 + C; } int RunMixed() { return Mix(4, C: 6, B: 5); } int RunPartial() { return Mix(A: 7, C: 9, B: 8); } int Run() { return RunMixed() * 1000 + RunPartial(); }`。 |
| 期望行为 | `Run()` 返回 `456789`；其中 `456` 证明 mixed positional + named 绑定正确，`789` 证明 partial named reorder 仍按参数名而非声明顺序绑定。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-56：补 duplicate / unknown named argument 的编译失败与 diagnostics 断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptFunction::GetParam(asUINT index, int *typeId, asDWORD *flags = 0, const char **name = 0, const char **defaultArg = 0) const` |
| 现有测试覆盖 | 现有 `FunctionTests` 没有任何命名参数错误路径；`NamedArguments` 只有正向执行断言 |
| 风险评估 | 如果 parser / binder 错误接受重复参数名或未知参数名，当前套件会完全漏报，导致调用站点在上线后才暴露难定位的参数错配问题 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Functions.NamedArguments.InvalidNameDiagnostics` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 场景描述 | 在同一测试中分别编译 `Mix(A: 1, A: 2, C: 3)` 与 `Mix(A: 1, D: 2, C: 3)` 两个最小脚本，验证 duplicate / unknown named argument 都会被拒绝。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary`；脚本函数可复用 `int Mix(int A, int B, int C) { return 0; }`。对两个脚本分别取独立模块名和文件名，记录 `FAngelscriptCompileTraceSummary`。 |
| 期望行为 | 两次编译都应 `bCompileSucceeded == false`、`CompileResult == ECompileResult::Error`；diagnostics 分别命中 duplicate-name 与 unknown-name 关键字，且行列号非零；失败后模块中不应留下可执行 `Run()`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` |
| 优先级 | P1 |

#### NewTest-57：补 interface property 声明拒绝路径，直接锁定 `NoProperty` 真正想表达的规则

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::VerifyPropertySpecifiers(const TArray<TSharedRef<FAngelscriptModuleDesc>>& Modules)` |
| 现有测试覆盖 | `Interface.NoProperty` 只验证合法 interface 最终没有反射字段，没有任何用例真正声明 `UPROPERTY()` 并断言被拒绝 |
| 风险评估 | 一旦接口属性校验失效，illegal field 会混入 UInterface 反射数据，后续 class generation、hot reload 和 GC 都可能出现结构性故障，而当前接口套件不会提前报警 |
| 建议测试名 | `Angelscript.TestModule.Interface.NoProperty.RejectsInterfacePropertyDeclarations` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceValidationTests.cpp` |
| 场景描述 | 编译一个在 `UINTERFACE()` 体内声明 `UPROPERTY() int StoredValue = 0;` 的最小脚本接口，并验证编译失败、interface class 不生成。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileModuleWithSummary`；脚本示例：`UINTERFACE() interface UIInvalidProperty { UPROPERTY() int StoredValue = 0; void DoThing(); }`。 |
| 期望行为 | 编译返回 `bCompileSucceeded == false`、`CompileResult == ECompileResult::Error`；diagnostics 命中 interface/property 相关报错；`FindGeneratedClass(&Engine, TEXT("UIInvalidProperty")) == nullptr`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileModuleWithSummary` + `FindGeneratedClass` |
| 优先级 | P1 |

#### NewTest-58：补 `Compiler.FunctionCalls` 的 bytecode/callee 白盒断言，避免内部编译测试继续只看运行结果

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptFunction::GetByteCode(asUINT *length = 0)` |
| 现有测试覆盖 | `Compiler.FunctionCalls` 只执行 `Add(7, 5)` 并断言结果 `12`，没有任何 bytecode 或 callee 绑定检查 |
| 风险评估 | 如果 compiler 把 call site 常量折叠、误绑定到错误函数或发出错误的 call opcode，当前 internals 套件可能继续绿灯，无法反映真正的 lowering 回归 |
| 建议测试名 | `Angelscript.TestModule.Internals.Compiler.FunctionCalls.BytecodeTargetsCallee` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 场景描述 | 编译一个包含两个不同返回模式 helper 的脚本，先执行验证走到了正确 helper，再读取 `Entry()` 的 bytecode，断言其长度和指令流中存在 `asBC_CALL`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `BuildModule`；脚本示例：`int Add(int A, int B) { return A * 100 + B; } int Other(int A, int B) { return A * 10 + B; } int Entry() { return Add(7, 5); }`。获取 `Entry()` 后先执行，随后调用 `GetByteCode(&Length)` 读取 dword 缓冲区。 |
| 期望行为 | `Entry()` 返回 `705`，证明 callee 不是任何“碰巧也返回 12 的函数”；`Bytecode != nullptr`、`Length > 0`，且 dword 流中至少存在一个 opcode 值等于 `asBC_CALL`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` + `GetByteCode()` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-77 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 1, MissingErrorPath: 2 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 00:07)

### 一、现有测试问题

#### Issue-79：`Memory.FreeUnused` 只验证空池 no-op，没验证真正的释放行为

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptMemoryTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Memory.FreeUnused` |
| 行号范围 | 58-65 |
| 问题描述 | 用例直接对全新 `FMemoryManagerProbe Manager;` 调一次 `FreeUnusedMemory()`，随后只断言两个 pool 仍是 `0`。这证明的只是“空池调用不会立刻崩”，却没有验证 `FreeUnusedMemory()` 面对真实已回收的 `scriptNodePool` / `byteInstructionPool` 时会把缓存块释放干净。当前文件里真正覆盖“分配 -> 回收到池 -> 再释放”的逻辑是在另一个 `PoolLeakTracking` 用例里，而 `Memory.FreeUnused` 这个测试名本身会高估它对释放路径的保护。 |
| 影响 | 如果 `FreeUnusedMemory()` 在非空池路径上失效、只清了一个 pool、或释放后内部计数没有归零，这个用例仍然会绿灯；回归只能依赖别的间接测试偶然命中。 |
| 修复建议 | 1）若保留当前空池断言，应把测试名收窄为 `FreeUnused.EmptyPoolNoop`；2）更推荐在本用例内先 `AllocScriptNode/AllocByteInstruction`，再 `Free*` 回收到 pool，之后调用 `FreeUnusedMemory()` 并分别断言两个 pool 从 `1`/`N` 归零；3）把“空池 no-op”和“非空池释放”拆成两个独立用例，避免单个宽泛标题同时代表两类行为。 |

#### Issue-80：`GC.Statistics` 只验证零初始化，无法代表 GC 统计行为

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.GC.Statistics` |
| 行号范围 | 264-286 |
| 问题描述 | 用例手工构造一个新的 `asCGarbageCollector Collector`，只读取一次 `GetStatistics()`，然后断言所有字段都是 `0`。这只能证明“fresh collector 默认值是零”，没有验证统计字段在真实对象进入 GC、detect-only 扫描、full collect 销毁之后会如何变化，也没有覆盖 engine 侧 `GetGCStatistics()` 与 collector 内部状态的一致性。测试名却直接叫 `GC.Statistics`，范围明显大于它实际锁定的内容。 |
| 影响 | 一旦 `CurrentSize`、`TotalDetected`、`TotalDestroyed` 等计数在真实 GC 流程里不再更新，或者 engine 对外暴露的统计值和 collector 内部值脱节，这个用例仍会绿灯，无法对统计回归提供保护。 |
| 修复建议 | 1）若只想保留当前断言，应把测试名收窄为 `Statistics.InitialState`；2）更推荐复用同文件 `GCProbeObject` helper，创建一个 self-cycle，分别在 `NotifyGarbageCollectorOfNewObject`、`detect-only`、`full collect` 后取快照，断言 `CurrentSize` / `TotalDetected` / `TotalDestroyed` 的递增关系；3）最好同时通过 `asIScriptEngine::GetGCStatistics` 读取一份外部快照，确保对外 API 与内部 collector 统计一致。 |

#### Issue-81：`Misc.GlobalVar` 只读编译期常量，无法证明模块级全局存储真的工作

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Misc.GlobalVar` |
| 行号范围 | 38-53 |
| 问题描述 | 脚本只有 `const int GlobalValue = 42; int Test() { return GlobalValue; }`，测试只执行一次 `Test()` 并断言结果是 `42`。这条路径完全可能被编译器当成常量内联，甚至不需要命中真实的模块级 global storage；它没有验证可写 global 的初始值、跨多次调用的持久化，也没有通过 `GetAddressOfGlobalVar()` 或同类 API 直接观察底层存储。 |
| 影响 | 如果运行时全局变量存储、初始化或地址暴露路径回归，而常量折叠仍然正常，这个用例会继续绿灯；`Misc.GlobalVar` 这个名字会明显高估当前套件对 global state 的保护。 |
| 修复建议 | 1）若保留现脚本，应把测试名收窄为 `ConstGlobalRead`; 2）更推荐把脚本改成可写 global，例如 `int GlobalValue = 40; int Step() { GlobalValue += 1; return GlobalValue; }`，然后连续执行两次断言结果为 `41/42`；3）再补一条白盒断言，通过 `asIScriptModule::GetAddressOfGlobalVar()` 读取底层值，证明返回值来自模块存储而不是编译期常量传播。 |

#### Issue-82：`DataType.Primitives` 标题覆盖整个 primitive 家族，实际只采样了 5 个特例

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDataTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.DataType.Primitives` |
| 行号范围 | 32-45 |
| 问题描述 | 用例只对白盒检查了 `ttInt`、`ttFloat32`、`ttBool`、`ttVoid` 和 `null handle` 五个样本：`IsIntegerType()`、`IsMathType()`、`CanBeInstantiated()`、`IsNullHandle()`。它没有覆盖 `int8/int16/int64`、`uint*`、`float64`，也没有证明 unsigned 与 signed、`float32` 与 `float64`、primitive 与 enum alias 之间的分类边界。测试名却是宽泛的 `Primitives`。 |
| 影响 | 一旦 `asCDataType` 在某个 primitive family 的判定上回归，例如 `uint64` 不再被识别为 unsigned、`float64` 不再被识别为 math type，当前测试仍会全绿；这会明显高估内部类型系统的分类覆盖。 |
| 修复建议 | 1）若维持当前最小样本，应将测试名收窄为 `PrimitiveSamples`; 2）更推荐扩成矩阵断言，至少补 `ttInt8`、`ttInt64`、`ttUInt32/ttUInt64`、`ttFloat64`，分别验证 `IsIntegerType`、`IsUnsignedType`、`IsFloat64Type` 与 `IsMathType`；3）把 `void/null handle` 的“可实例化性”断言与 numeric family 分类拆开，避免一个宽标题混合两类语义。 |

#### Issue-83：`Misc.Assign` 用单个结果值串联 4 个复合赋值，存在错误互相抵消的假阳性

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Misc.Assign` |
| 行号范围 | 84-99 |
| 问题描述 | 用例把 `+=`、`-=`、`*=`、`/=` 四种复合赋值串在一条线性脚本里，最后只断言总结果是 `8`。这种单值断言无法定位是哪一种赋值出错，更糟的是某些错误还能互相抵消，例如若实现把 `*=` 和 `/=` 的执行顺序或 lowering 搞反，当前这组数字仍可能偶然得到 `8`。测试名却是宽泛的 `Assign`。 |
| 影响 | 复合赋值 lowering 或运算顺序回归时，当前用例可能继续绿灯，导致 `MiscTests` 对 assignment 语义的真实保护被高估。 |
| 修复建议 | 1）把脚本改成顺序敏感编码，例如在每一步后把中间值写入不同权位，`return Step1 * 1000 + Step2 * 100 + Step3 * 10 + Step4;`；2）或拆成多个独立用例，分别锁定 `+=`、`-=`、`*=`、`/=` 的行为；3）若目标只想验证 arithmetic compound assignment，应把测试名收窄为 `CompoundAssign.IntHappyPath`，避免继续用 `Assign` 代表整个赋值语义。 |

### 二、需要新增的测试

#### NewTest-59：补 `GC` 统计快照的真实生命周期矩阵，避免 `Statistics` 继续只测零初始化

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptEngine::GetGCStatistics`、`asIScriptEngine::NotifyGarbageCollectorOfNewObject`、`asIScriptEngine::GarbageCollect` |
| 现有测试覆盖 | `GC.Statistics` 只验证 fresh collector 全零；`ManualCycleCollection` / `CycleDetection` 虽然创建了真实 cyclic object，但没有把 5 个统计字段按阶段锁成稳定矩阵 |
| 风险评估 | 如果 GC 统计字段在对象跟踪、detect-only 和 full collect 三个阶段之一停止更新，当前套件会继续绿灯；调试和诊断层会拿到错误快照而没有回归提示 |
| 建议测试名 | `Angelscript.TestModule.Internals.GC.Statistics.LiveCycleTransitions` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 场景描述 | 复用现有 `GCProbeObject` helper，创建一个 self-cycle，对“创建后 / detect-only 后 / full collect 后”三个阶段分别读取统计快照。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，复用现有 `RegisterGCProbeType`、`CreateSelfCycle`、`GetGCStatisticsSnapshot`、`RunFullGarbageCollection`。流程建议为：记录 `BeforeCreate`；创建并 `NotifyGarbageCollectorOfNewObject` 后取 `AfterCreate`；`Release()` 外部引用并执行 `GarbageCollect(asGC_FULL_CYCLE | asGC_DETECT_GARBAGE, 1)` 后取 `AfterDetect`；最后跑 `RunFullGarbageCollection()` 再取 `AfterCollect`。 |
| 期望行为 | `AfterCreate.CurrentSize >= BeforeCreate.CurrentSize + 1`；`AfterDetect.TotalDetected >= BeforeCreate.TotalDetected + 1` 且 `AfterDetect.CurrentSize >= 1`；`AfterCollect.TotalDestroyed > AfterDetect.TotalDestroyed` 且 `AfterCollect.CurrentSize == 0`；`FGCProbeObject::LiveCount == 0`。 |
| 使用的 Helper | 现有 `RegisterGCProbeType` + `CreateSelfCycle` + `GetGCStatisticsSnapshot` + `RunFullGarbageCollection` |
| 优先级 | P2 |

#### NewTest-60：补可写 global 的持久化与地址读取，避免 `Misc.GlobalVar` 继续被常量内联掩盖

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptModule::GetAddressOfGlobalVar`、`asIScriptModule::GetGlobalVarCount` |
| 现有测试覆盖 | `Misc.GlobalVar` 只执行一次 `return GlobalValue;`，而且 `GlobalValue` 还是 `const int`；没有任何测试验证可写 global 会跨调用保留状态 |
| 风险评估 | 一旦模块级全局变量初始化、持久化或地址暴露路径回归，当前 `MiscTests` 仍可能因为常量折叠而全绿；运行时真正依赖 global storage 的脚本会延后暴露问题 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Misc.GlobalVar.MutablePersistenceAndAddress` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 场景描述 | 编译一个带可写 global 的最小脚本，连续执行两次递增函数，并通过白盒 API 直接读取 global 地址。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule` + `GetFunctionByDecl`。脚本示例：`int GlobalValue = 40; int Step() { GlobalValue += 1; return GlobalValue; }`。测试编译后先断言模块 `GetGlobalVarCount() == 1`，再执行两次 `Step()`。 |
| 期望行为 | 第一次执行返回 `41`，第二次执行返回 `42`；`Module->GetAddressOfGlobalVar(0)` 返回非空；把地址解释为 `int32*` 后读取值应为 `42`。这三条断言共同证明 global 值来自模块存储而不是编译期常量传播。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` + `GetAddressOfGlobalVar` |
| 优先级 | P1 |

#### NewTest-61：补顺序敏感的复合赋值矩阵，直接锁住 `+=/-=/\*=/=` 的中间状态

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `Misc.Assign` 只检查最终结果 `8`，无法分辨 `+=`、`-=`、`*=`、`/=` 哪一步出错，也无法防止中间错误彼此抵消 |
| 风险评估 | 复合赋值 lowering 一旦在顺序、临时变量或整除语义上回归，当前测试很可能继续绿灯；语言算术路径会被明显高估 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Misc.Assign.CompoundStepMatrix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 场景描述 | 在一个脚本入口里按顺序执行四种 compound assignment，并把每一步的中间值编码进最终结果。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Run() { int Value = 10; Value += 5; int Step1 = Value; Value -= 3; int Step2 = Value; Value *= 2; int Step3 = Value; Value /= 3; int Step4 = Value; return (((Step1 * 100) + Step2) * 100 + Step3) * 100 + Step4; }`。 |
| 期望行为 | `Run()` 返回 `15122408`；其中 `15/12/24/8` 四个中间值都被锁进结果编码，任意一步的 lowering、顺序或整除语义出错都会直接改变返回值。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 5 | Issue-81 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 23:44)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-123`、`Issue-124`、`Issue-125`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-102`、`NewTest-103`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-123 |
| BadIsolation | 1 | Issue-124 |
| WeakAssertion | 1 | Issue-125 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |

---

## 测试审查 (2026-04-09 23:40)

### 一、现有测试问题

#### Issue-123：`ControlFlow` 整个文件用 `FULL` 引擎跑纯语言脚本，helper 级别明显过重

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.ForLoop`、`SwitchAndConditional`、`Condition`、`NeverVisited`、`NotInitialized` |
| 行号范围 | 35-145 |
| 问题描述 | 这 5 个用例全部使用 `ASTEST_CREATE_ENGINE_FULL()` + `ASTEST_BEGIN_FULL`，但文件里的行为只有两类：1）编译/执行几段内存脚本并断言返回值；2）编译后读取 `Engine.Diagnostics` 查 warning。根据 `Shared/AngelscriptTestMacros.h` 的 helper 约定，`FULL` 适合 engine core self-test、bind environment、hot-reload 等真正需要完整 full engine epoch 的场景；而这里既不创建 world，也不依赖 production subsystem，更没有 full-engine 专属 bootstrap。`BuildModule` / `ASTEST_COMPILE_RUN_INT` 自身已经会生成唯一脚本文件并关闭 automatic imports，因此这些控制流用例完全可以落到 `SHARE` / `SHARE_CLEAN`。 |
| 影响 | `LanguageFeatures` 最大区域里的控制流套件会为每个小语法用例重复付出 full engine 初始化成本，把纯语言层断言绑到无关的 engine bootstrap 上，增加运行时长和噪声；一旦 full-engine 初始化链路波动，这些本应聚焦 `if/switch/for` 语义的测试会一起误报失败，降低定位效率。 |
| 修复建议 | 1）把 `ForLoop`、`SwitchAndConditional`、`Condition` 改成 `ASTEST_CREATE_ENGINE_SHARE()` 或 `FAngelscriptTestFixture(ETestEngineMode::SharedClone)`；2）`NeverVisited`、`NotInitialized` 因为要读 diagnostics，更适合改成 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，显式拿 clean shared engine 而不是 full epoch；3）保留当前断言逻辑不变，先把 helper 降级，再视需要补充 module cleanup/diagnostics helper，避免让控制流语义测试继续依赖 full-engine bootstrap。 |

#### Issue-124：`InterfaceNativeTests` 通过 ambient `current engine` 绑定 native interface，存在隐式顺序依赖

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.NativeImplement`、`NativeInheritedImplement`、`NativeReferenceRoundTrip` |
| 行号范围 | 23-83, 102-434 |
| 问题描述 | `BindNativeInterfaceMethod()` 通过 `FAngelscriptEngine::Get().RegisterInterfaceMethodSignature(...)` 注册签名，`EnsureNativeInterfaceBoundForTests()` 又直接读取 `FAngelscriptEngine::Get().Engine` 做 type lookup/bind。整个 helper 链完全没有接收 `FAngelscriptEngine& Engine` 参数，也没有断言“当前 engine 就是本测试刚创建的 share-fresh fixture”。这意味着 3 个 native interface 用例之所以能工作，只是因为它们恰好在 `ASTEST_BEGIN_SHARE_FRESH` 的 ambient scope 里调用 helper；一旦 context stack 上存在别的 engine、helper 被提取复用到 scope 外，或者后续有人把 binding 提前到 `ASTEST_BEGIN_*` 之前，签名与 type 绑定就可能落到错误引擎上。 |
| 影响 | 该文件的 native interface 绑定不是显式依附于测试 fixture，而是依附于进程内“当前解析到的 engine”，会把 context stack 顺序、全局 engine 状态和别的测试残留混进结果里。这正好放大本区域最需要排查的隐式依赖问题，尤其会让 `NativeImplement` / `NativeReferenceRoundTrip` 的失败表现成偶发错绑而不是可重复的本地缺陷。 |
| 修复建议 | 1）把 `BindNativeInterfaceMethod`、`EnsureNativeInterfaceBoundForTests`、`EnsureNativeInterfaceFixturesBound` 全部改成显式接收 `FAngelscriptEngine& Engine`；2）注册签名时调用成员方法 `Engine.RegisterInterfaceMethodSignature(...)` / `Engine.ReleaseInterfaceMethodSignature(...)`，type lookup 也改成 `Engine.GetScriptEngine()`，不要再依赖 `FAngelscriptEngine::Get()`；3）若短期内保留 ambient 方案，至少在 helper 入口加 `check(&FAngelscriptEngine::Get() == &Engine)` 风格的自校验，把隐式依赖转成显式失败。 |

#### Issue-125：`Types.Enum` 只锁住默认递增枚举的单个序号，测试名明显高估覆盖面

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.Enum` |
| 行号范围 | 249-267 |
| 问题描述 | 用例脚本只有 `enum Color { Red, Green, Blue }`，最终只断言 `int(Color::Green) == 1`。这只覆盖了“默认从 0 递增的 enum 在第二个枚举项上返回 1”这一条最窄 happy path，没有验证显式赋值、非零起始值、空洞值、负值或 alias/underlying conversion 等任何其他 enum 语义。测试名却直接叫 `Types.Enum`，容易让人误以为整个枚举语言特性已有 dedicated coverage。 |
| 影响 | 如果 enum parser/binder 在显式枚举值、非零起始值或稀疏值映射上回归，当前 `TypeTests` 仍会全部通过；后续审查也会被这个宽泛测试名误导，以为枚举路径已经有系统保护。 |
| 修复建议 | 1）若短期只保留当前默认枚举样本，把测试名收窄为 `Enum.DefaultOrdinal`; 2）更推荐直接扩成 matrix：同一文件补 `enum Status { Idle = 3, Running = 7, Finished = 9 }` 或独立 helper，分别断言每个枚举项的整数值；3）再补一条通过函数返回/比较表达式读取显式枚举值的断言，避免只验证 `int(EnumValue)` 这一种读取方式。 |

### 二、需要新增的测试

#### NewTest-102：补 `Parser` 失败后的 `Reset` / reuse 回归，锁住语法错误不会污染下一次解析

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_parser.cpp` |
| 关联函数 | `asCParser::ParseScript`、`asCParser::Reset` |
| 现有测试覆盖 | `Parser.SyntaxErrors` 只验证一次 malformed input 返回 `< 0`；当前没有任何测试证明同一个 parser 在 error path 之后可以 `Reset()` 并继续成功解析下一段合法脚本。 |
| 风险评估 | 如果 parser 在失败后残留脏 AST、错误位置或 token 状态，后续复用同一 parser/builder 的调用点会表现成“第一个错误之后后面的合法代码也全坏了”；现有套件无法把根因锁在 reset/reuse 合同。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Parser.ReuseAfterSyntaxError` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp` |
| 场景描述 | 在同一个 `asCBuilder` / parser 实例上先解析一段非法脚本，再显式 `Reset()` 后解析一段合法脚本，验证第二次解析不受第一次错误污染。 |
| 输入/前置 | 复用现有 `CreateParserModule(...)` 与 `FParserAccessor`，给 accessor 增加一个公开 `ResetForTest()` / `ParseScriptSnippet()` 包装。第一次输入建议为 `void Broken( { return; }`，第二次输入建议为 `int GlobalValue = 7; class FRecovered { int Value; }`。 |
| 期望行为 | 1）第一次 `ParseScript(...) < 0`，且 `GetScriptNode()` 为空或不可遍历；2）调用 `Reset()` 后再次 `ParseScript(...) == 0`；3）第二次 `GetScriptNode()` 非空且根节点为 `snScript`，同时能观察到 `snDeclaration` 与 `snClass`；4）若 parser 暴露错误字符串/位置，也应断言第二次成功解析后不再残留第一次错误信息。 |
| 使用的 Helper | 现有 `CreateParserModule(...)` + `FParserAccessor`（新增轻量 public wrapper 暴露 `Reset()` / `ParseScript`） |
| 优先级 | P1 |

#### NewTest-103：补 `Tokenizer` 错误 token 的“推进游标后继续扫描”回归，避免 recovery 只停留在类型分类

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_tokenizer.cpp` |
| 关联函数 | `asCTokenizer::GetToken` |
| 现有测试覆盖 | `Tokenizer.ErrorRecovery` 目前只验证 `ttNonTerminatedStringConstant` 和 `ttUnrecognizedToken` 这两个错误 token 的类型；没有任何断言证明 tokenizer 在错误 token 后会把 `TokenLength` 推进到正确位置，并且能继续识别后续合法 token。 |
| 风险评估 | 如果 tokenizer 在 recovery path 上不推进游标、长度返回 0、或把下一段合法输入吞掉，parser 会在错误 token 之后持续错位；当前套件只能看到“第一个 token 类型对了”，完全锁不住真正的恢复合同。 |
| 建议测试名 | `Angelscript.TestModule.Internals.Tokenizer.ErrorRecovery.AdvancesAndContinues` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 场景描述 | 用一个包含错误 token + 合法 token 的最小 buffer 连续调用两次 `GetToken`，第一次验证错误 token 类型与长度，第二次从 `Buffer + TokenLength` 继续扫描，验证能正确识别后续合法 token。 |
| 输入/前置 | 复用现有 `FTokenizerAccessor Tokenizer; size_t TokenLength = 0;`。最小样本建议为 `` `class ``：第一次对整段输入调用 `GetToken`，预期返回 `ttUnrecognizedToken` 且 `TokenLength == 1`；第二次对 `Input + 1`、剩余长度 `5` 调用 `GetToken`，预期返回 `ttClass` 且长度 `5`。如果希望覆盖另一路 recovery，可再加 `@Identifier` 之类样本验证非法前缀后仍能识别完整 identifier。 |
| 期望行为 | 1）第一次扫描返回错误 token，且 `TokenLength > 0`，不能卡在 `0`；2）第二次从偏移位置继续扫描时，合法 token 类型与长度都正确；3）两步组合证明 tokenizer 不会因为前一个坏 token 让后续输入永久失步。 |
| 使用的 Helper | 现有 `FTokenizerAccessor` |
| 优先级 | P1 |

---

## 测试审查 (2026-04-09 13:34)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-120`、`Issue-121`，保留在前文 `## 测试审查 (2026-04-09 13:33)` 小节（当前约第 2812 行起）；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-99`、`NewTest-100`，保留在前文 `## 测试审查 (2026-04-09 13:33)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-120 |
| WeakAssertion | 1 | Issue-121 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 23:28)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-122`，保留在前文 `## 测试审查 (2026-04-09 23:25)` 小节；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-101`，保留在前文 `## 测试审查 (2026-04-09 23:25)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-122 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 23:44)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-123`、`Issue-124`、`Issue-125`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-102`、`NewTest-103`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-123 |
| BadIsolation | 1 | Issue-124 |
| WeakAssertion | 1 | Issue-125 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |

---

## 测试审查 (2026-04-09 23:44)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-123`、`Issue-124`、`Issue-125`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-102`、`NewTest-103`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-123 |
| BadIsolation | 1 | Issue-124 |
| WeakAssertion | 1 | Issue-125 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |

---

## 测试审查 (2026-04-09 23:44)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-123`、`Issue-124`、`Issue-125`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-102`、`NewTest-103`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-123 |
| BadIsolation | 1 | Issue-124 |
| WeakAssertion | 1 | Issue-125 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |

---

## 测试审查 (2026-04-09 23:25)

### 一、现有测试问题

#### Issue-122：`Core.CreateCompileExecute` 用 shared singleton 代替 fresh-engine 创建路径，测试名与 helper 选择不匹配

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Core.CreateCompileExecute` |
| 行号范围 | 11-26 |
| 问题描述 | 用例标题声称验证 `CreateCompileExecute`，但实现直接使用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_COMPILE_RUN_INT(...)`。`ASTEST_CREATE_ENGINE_SHARE()` 明确复用进程级 shared clone engine，`ASTEST_BEGIN_SHARE` 也不会 reset module state，因此它既没有调用 `FAngelscriptEngine::CreateForTesting(...)` / `CreateTestingFullEngine(...)`，也没有证明 fresh engine 能在零 baseline 上完成初始化、编译、创建 context 并执行脚本。只要前序测试已经把 shared engine 预热好，这条用例就算创建路径回归也仍可能返回 `42`。 |
| 影响 | 该测试会把真正的 engine bootstrap 回归掩盖成 shared-engine happy path 绿灯；同时也引入隐式顺序依赖，因为 shared engine 的预热状态来自前序用例而不是当前用例显式 setup。 |
| 修复建议 | 1）若目标真是 fresh-engine bootstrap，改用 `FAngelscriptEngine::CreateForTesting(...)` 或至少 `ASTEST_CREATE_ENGINE_FULL()`，并在执行前断言 `GetActiveModules().Num() == 0`；2）若只想保留 shared-engine compile smoke，应把测试名收窄为 `CompileExecuteOnSharedEngine`；3）更完整的修复是把“fresh create + compile + execute + discard”做成独立用例，显式验证 active module 数从 `0 -> 1 -> 0` 的迁移，而不是只看一次 `42`。 |

### 二、需要新增的测试

#### NewTest-101：补 fresh-engine bootstrap 回归，直接锁住 `CreateForTesting` 后的首个 compile/execute/discard 生命周期

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CreateForTesting`、`FAngelscriptEngine::InitializeForTesting`、`FAngelscriptEngine::GetActiveModules`、`FAngelscriptEngine::DiscardModule`、`FAngelscriptEngine::CreateContext` |
| 现有测试覆盖 | `Core.CreateCompileExecute` 只在 shared singleton 上执行 `ASTEST_COMPILE_RUN_INT`；`Core.CreateEngine` 只验证 wrapper/script engine 非空与 creation mode 非 magic value。当前没有任何正式用例证明一个全新测试引擎在无预热模块、无前序 shared state 的情况下，可以完成第一次 compile、第一次 context prepare/execute，并在 discard 后回到干净 baseline。 |
| 风险评估 | 如果 `CreateForTesting` / `InitializeForTesting` 的 bootstrap 过程回归，例如 fresh engine 首次编译依赖外部 current-engine state、active module 初值不干净、第一次 `CreateContext()` 失败，现有 core 套件仍可能全绿；问题会只在独立测试进程或首个脚本加载时暴露。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Core.CreateCompileExecute.FreshEngineBootstrap` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 场景描述 | 显式创建一个 fresh test engine，验证 baseline 没有 active modules；随后在该 engine 上编译并执行最小脚本；最后 discard 同一模块并确认 active module 数回到 `0`。 |
| 输入/前置 | 使用 `FAngelscriptEngineConfig Config; FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault(); TUniquePtr<FAngelscriptEngine> LocalEngine = FAngelscriptEngine::CreateForTesting(Config, Dependencies, EAngelscriptEngineCreationMode::Full);`。进入 `FAngelscriptEngineScope` 后，先断言 `LocalEngine->GetActiveModules().Num() == 0`；再用现有 `BuildModule(*this, *LocalEngine, "ASCoreFreshBootstrap", TEXT("int DoubleValue(int Value) { return Value * 2; } int Run() { return DoubleValue(21); }"))` + `GetFunctionByDecl` + `ExecuteIntFunction` 执行 `Run()`；最后调用 `LocalEngine->DiscardModule(TEXT("ASCoreFreshBootstrap"))`。 |
| 期望行为 | 1）fresh engine 创建成功且 `GetScriptEngine()` 非空；2）编译前 `GetActiveModules().Num() == 0`；3）编译后 active module 数变为 `1`，`Run()` 返回 `42`；4）`DiscardModule("ASCoreFreshBootstrap") == true`，discard 后 `GetActiveModules().Num() == 0`；5）如需额外锁定 bootstrap，可再断言首个 `CreateContext()` / `Prepare` / `Execute` 都返回 success，不依赖任何外部 current-engine state。 |
| 使用的 Helper | `FAngelscriptEngine::CreateForTesting` + `FAngelscriptEngineScope` + `BuildModule` + `GetFunctionByDecl` + `ExecuteIntFunction` + `GetActiveModules` + `DiscardModule` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-122 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 13:33)

### 一、现有测试问题

#### Issue-120：多组 negative compile 测试直接改全局 `Angelscript` log verbosity，恢复方式不对称

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Core.CompilerBasic`、`Angelscript.TestModule.Angelscript.Core.Parser`、`Angelscript.TestModule.Angelscript.Functions.Pointer`、`Angelscript.TestModule.Angelscript.Functions.Template`、`Angelscript.TestModule.Angelscript.Functions.Factory`、`Angelscript.TestModule.Angelscript.Handles.Basic`、`Angelscript.TestModule.Angelscript.Handles.Auto`、`Angelscript.TestModule.Angelscript.Inheritance.Interface`、`Angelscript.TestModule.Angelscript.Inheritance.CastOp`、`Angelscript.TestModule.Angelscript.Inheritance.Mixin` |
| 行号范围 | 146-154、196-204；97-107、173-183、202-212；26-37、94-105；63-73、130-140、161-171 |
| 问题描述 | 这些 negative compile 用例在调用 `CompileModuleWithResult(...)` 前统一执行 `UE_SET_LOG_VERBOSITY(Angelscript, Fatal)`，结束后再固定写回 `UE_SET_LOG_VERBOSITY(Angelscript, Log)`。`Angelscript` category 的 verbosity 是进程级全局状态，但测试既没有保存进入用例前的原始级别，也没有用 `ON_SCOPE_EXIT`/RAII 保证异常或早退时恢复。因此只要前一个测试或外部环境把该 category 调成 `Warning`/`Verbose` 等非 `Log` 值，这些用例执行后就会静默改写全局日志配置；一旦 future edit 在两次宏调用之间新增早退分支，还可能把整个后续套件卡在 `Fatal`。 |
| 影响 | 这会制造隐式测试间依赖：后续依赖 diagnostics 的用例可能因为 verbosity 被改低而漏报，排查 negative compile 失败时也可能因为日志级别被前一个测试永久改写而得到不同输出。问题不直接体现在单个断言上，但会持续侵蚀 LanguageFeatures 套件的可重复性和诊断质量。 |
| 修复建议 | 抽一个统一 helper 处理“静音预期编译失败”：1）进入 helper 时保存 `Angelscript` category 的当前 verbosity，并用 `ON_SCOPE_EXIT` 保证恢复原值，而不是硬编码恢复到 `Log`；2）若只是为了吞掉预期错误，优先改成 `AddExpectedError`/`AddExpectedErrorPlain` + 正常 verbosity，避免直接改全局 category；3）把 `CompileModuleWithResult` 的 negative-path 包装成共享 RAII helper，消除 `Core/Functions/Handles/Inheritance` 里重复的全局状态修改模式。 |

#### Issue-121：`Builder.ImportBinding` 名称覆盖“导入绑定流程”，实际只验证手工 `BindImportedFunction` 的单一路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Builder.ImportBinding` |
| 行号范围 | 148-195 |
| 问题描述 | 用例先构建 source/consumer 两个模块，再直接取 `SourceFunction` 调 `ConsumerModule->BindImportedFunction(0, SourceFunction)`，最后执行 `Entry()` 返回 `77`。它没有证明 consumer 在 bind 前确实处于 unresolved 状态，也没有验证 bind 之后 imported slot 是否真的从“待解析”切换成“已绑定”；更没有覆盖 engine 层 `ResolveDeclaredImports` / `BindAllImportedFunctions` 的正常路径。换句话说，这个测试只锁住了“拿到正确函数指针后手工调用一次 `BindImportedFunction` 可以跑通”的最窄 happy path，但测试名却像是在代表整条 import-binding 流程。 |
| 影响 | 如果导入函数在 builder/engine 自动解析路径上回归，或者 `BindImportedFunction` 只把一次执行糊过去却没有真正更新 imported-function 状态，当前用例仍可能绿灯。这样会高估 Internals 套件对 declared import 生命周期的保护强度。 |
| 修复建议 | 1）把现有用例收窄命名为 `ManualBindHappyPath`，避免继续用宽泛标题代表整条流程；2）在同一用例里补“绑定前不能直接视为已解析”的白盒断言，例如验证 `GetImportedFunctionCount()==1` 后 `BindAllImportedFunctions()`/执行路径在绑定前仍是 unresolved；3）绑定后再显式断言 imported slot 状态切换成功，并把自动解析、签名不匹配、source 消失后的 rebind/unbind 场景拆到独立测试，避免继续把所有导入语义压在一个手工 bind happy path 上。 |

### 二、需要新增的测试

#### NewTest-99：补 `ClearMessageCallback` 的清除与重注册回归，直达当前无测试的 callback 生命周期 API

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `asIScriptEngine::ClearMessageCallback`、`asIScriptEngine::SetMessageCallback`、`asIScriptEngine::GetMessageCallback` |
| 现有测试覆盖 | `Angelscript.TestModule.Angelscript.Upgrade.MessageCallback` 只覆盖 set/get + `WriteMessage` 触发；`Native/AngelscriptASSDKEngineTests.cpp` 只覆盖跨 engine 复用 callback。当前仓库没有任何测试直接调用 `ClearMessageCallback()`，也没有锁住“清除后旧 callback 不再收到消息、随后可以安全重注册”的生命周期合同。 |
| 风险评估 | 一旦 callback clear 路径失效，shared/clone engine 的旧回调或旧对象指针可能继续泄漏到后续编译诊断里；这类问题既会放大测试间状态污染，也会让 editor/runtime 的 message routing 出现难以定位的串音。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Upgrade.MessageCallback.ClearAndReRegister` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 场景描述 | 在 isolated test engine 上先注册 callback A，发一条 `WriteMessage` 验证 A 收到；随后调用 `ClearMessageCallback()`，再次 `WriteMessage` 验证 A 计数不再变化；最后注册 callback B，再发一条消息验证只有 B 收到。 |
| 输入/前置 | 使用 `CreateIsolatedCloneEngine()` 或 `FAngelscriptEngine::CreateForTesting(...)`，避免污染 shared engine。测试文件内增加两个轻量 collector（例如两个静态计数器 + 两个 `CaptureUpgradeMessageA/B` free function），并在每轮 `WriteMessage` 前后清零/读取计数。可在重注册后继续用 `GetMessageCallback(...)` 断言 getter 返回的是 callback B 对应的 callconv/object 组合。 |
| 期望行为 | 1）callback A 注册后第一次 `WriteMessage` 令 `A.Count == 1`；2）`ClearMessageCallback()` 返回成功后，第二次 `WriteMessage` 不再改变 `A.Count`；3）重注册 callback B 后，第三次 `WriteMessage` 令 `B.Count == 1` 且 `A.Count` 仍保持不变；4）若调用 `GetMessageCallback(...)`，返回值必须对应 callback B 而不是清除前的 A。 |
| 使用的 Helper | `CreateIsolatedCloneEngine` / `FAngelscriptEngine::CreateForTesting` + `GetScriptEngine` + `SetMessageCallback` + `ClearMessageCallback` + `GetMessageCallback` + `WriteMessage` |
| 优先级 | P1 |

#### NewTest-100：补 `DeclareBasic` 的接口方法反射形态断言，锁住 `UINTERFACE` 声明阶段生成的 `UFunction` 面

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | script `UINTERFACE` 声明到 generated `UFunction`/parameter reflection 的生成路径（`FAngelscriptClassDesc::InterfaceMethodDeclarations` 消费链） |
| 现有测试覆盖 | `Angelscript.TestModule.Interface.DeclareBasic` 只断言 interface class 存在且带 `CLASS_Interface`；`DeclareInheritance` 只断言 child interface class 能生成。当前没有任何正式用例锁住接口方法是否真的进入反射层、参数名/参数类型/返回值形态是否正确。 |
| 风险评估 | 如果接口方法在生成 `UFunction` 时丢失、参数类型错绑或 return/property flags 退化，现有 Declare 套件仍会绿灯；后续 Implement/Cast/ProcessEvent 相关测试只会以更晚、更间接的方式暴露问题，定位成本明显更高。 |
| 建议测试名 | `Angelscript.TestModule.Interface.DeclareBasic.GeneratedUFunctionShape` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceDeclareTests.cpp` |
| 场景描述 | 编译一个最小脚本接口，声明一个 `void TakeDamage(float Amount)` 和一个 `int GetPriority() const`。编译后直接对白盒读取 interface class 的 `UFunction` 形态，而不是只看 `CLASS_Interface`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileScriptModule` + `FindGeneratedClass`/`FindGeneratedFunction`。接口脚本建议为：`UINTERFACE() interface UIDamageableReflection { void TakeDamage(float Amount); int GetPriority() const; }`。编译后获取 `TakeDamage` 与 `GetPriority` 两个 `UFunction`，并用 `TFieldIterator<FProperty>` / `CastField<FFloatProperty>` / `CastField<FIntProperty>` 遍历参数与返回值。 |
| 期望行为 | 1）`TakeDamage` 与 `GetPriority` 两个 `UFunction` 都存在；2）`TakeDamage` 恰好暴露一个名为 `Amount` 的 `float` 参数且无返回值；3）`GetPriority` 没有输入参数，并暴露一个 `int` return property；4）接口类自身仍保持 `CLASS_Interface`。这组断言把“接口类能生成”升级成“接口方法反射面正确生成”。 |
| 使用的 Helper | `CompileScriptModule` + `FindGeneratedClass` / `FindGeneratedFunction` + `TFieldIterator<FProperty>` + `CastField<FFloatProperty>` / `CastField<FIntProperty>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-120 |
| WeakAssertion | 1 | Issue-121 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
| P2 | 2 | MissingScenario: 1, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 00:23)

### 一、现有测试问题

#### Issue-84：`Execution` 参数封送用例大量使用可交换求和，抓不住参数槽位错绑

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.TwoArgs`、`Angelscript.TestModule.Angelscript.Execute.FourArgs`、`Angelscript.TestModule.Angelscript.Execute.MixedArgs` |
| 行号范围 | 124-319 |
| 问题描述 | 这 3 个用例都把参数传给一个纯加法入口：`A + B`、`A + B + C + D`、`A + B + C`，最终只断言 `42` 或 `42.5`。由于加法对参数顺序天然不敏感，这些测试即使在 `SetArgDWord` / `SetArgFloat` / `SetArgQWord` 把值写进了错误槽位、binder 把参数位置映射错了、或 mixed-arg 调用时把后两个参数重排了，仍然可能继续通过。当前断言更像“任意顺序的求和还能工作”，而不是“参数封送按声明顺序进入正确槽位”。 |
| 影响 | 参数栈布局、slot index 计算和 mixed-signature 调用约定一旦回归，`ExecutionTests` 会给出假绿灯，导致真正的参数绑定错误被推迟到更复杂的脚本场景里才暴露。 |
| 修复建议 | 1）把脚本改成顺序敏感编码，例如 `int Test(int A, int B) { return A * 100 + B; }`、`int Test(int A, int B, int C, int D) { return A * 1000 + B * 100 + C * 10 + D; }`；2）`MixedArgs` 改成 `return A * 100.0 + B * 10.0 + C;` 之类的非交换表达式，并分别断言 `float32/float64` 分支；3）保留当前 happy path 时，也应把“参数是否进入正确槽位”的断言从单纯求和里拆成独立用例，而不是继续用 commutative 输入代表参数封送完整性。 |

#### Issue-85：`ControlFlow.NotInitialized` 用全局 diagnostics 模糊匹配 warning，未绑定到当前模块

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.NotInitialized` |
| 行号范围 | 124-145 |
| 问题描述 | 用例通过 `ContainsWarningDiagnostic(Engine, TEXT("may not be initialized"))` 在整个 `Engine.Diagnostics` 表里做子串搜索，然后据此判定当前脚本 `int Run() { int Value; return Value; }` 是否产生了预期 warning。这个 helper 不筛模块名、不筛文件名、也不锁定行列号或变量名，只要同一引擎里任何 diagnostics 恰好包含相同文本，测试就会通过。对于一个 full engine 来说，这属于跨模块的隐式依赖。 |
| 影响 | 一旦别的模块或预编译脚本也产生了相同 wording 的 warning，`ControlFlow.NotInitialized` 即使没有为当前脚本发出诊断也会绿灯；反过来，如果 warning wording 变了但仍指向同一未初始化读问题，这个测试也很难给出可定位的失败信息。 |
| 修复建议 | 1）把 helper 改成只检查 `ASControlFlowNotInitialized` 对应 diagnostics 条目，或在编译前后做 diagnostics diff，仅消费本次新增消息；2）额外断言 warning 命中变量名 `Value`、文件名 `ASControlFlowNotInitialized` 和非零行列号，避免仅靠模糊子串；3）若引擎支持 compile summary，优先改用 `CompileModuleWithSummary` 一类 helper，把 warning 断言收束到当前编译结果。 |

#### Issue-86：`ControlFlow.ForLoop` 只覆盖单个递增 happy path，测试名高估了 `for` 语义保护范围

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.ForLoop` |
| 行号范围 | 35-49 |
| 问题描述 | 用例脚本固定为 `for (int Index = 0; Index < 5; ++Index) { Sum += Index; }`，最后只断言结果 `10`。这只能证明最基础的“正向递增且循环体至少执行一次”的路径可用，无法区分 initializer、condition、update 三段中哪一段出了问题，也没有覆盖 `Index` 初始即不满足条件的零迭代路径，或 `--Index` / 非 `++` 更新表达式。测试名却直接叫 `ForLoop`。 |
| 影响 | 如果 `for` lowering 在零迭代判断、update 表达式求值顺序或反向循环上回归，而最简单的 `++Index` 求和仍然能跑，当前控制流套件会明显高估自己对 `for` 语义的保护。 |
| 修复建议 | 1）把现有脚本改成多段结果编码，同时覆盖“零迭代”“正向递增”“反向递减”至少三条路径；2）若想保留最小 happy path，应把测试名收窄为 `ForLoop.IncrementingHappyPath`；3）新增 companion 用例专门验证 `for (int i = 3; i >= 0; --i)` 和 `for (...; false; ...)` 这两类边界，避免继续让单个求和结果代表整个 `for` 语义。 |

### 二、需要新增的测试

#### NewTest-62：补 `for` 循环的零迭代与递减更新矩阵，避免 `ForLoop` 继续只测 `++Index`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `ControlFlow.ForLoop` 只覆盖 `for (int Index = 0; Index < 5; ++Index)` 的正向非空循环，没有任何零迭代或递减更新路径 |
| 风险评估 | 如果 `for` lowering 在初始条件短路、update 表达式顺序或 `--Index` 路径上回归，当前控制流套件仍可能全绿；这会直接削弱对 `for` 语义的基础保护 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.ForLoop.DecrementAndZeroIteration` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 场景描述 | 在同一脚本入口里同时执行一个递减 `for` 循环和一个条件一开始即为 false 的零迭代 `for` 循环，并把两段结果编码进最终返回值。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Desc() { int Encoded = 0; for (int Index = 3; Index >= 0; --Index) { Encoded = Encoded * 10 + Index; } return Encoded; } int ZeroLoopHits() { int Hits = 0; for (int Index = 5; Index < 5; ++Index) { ++Hits; } return Hits; } int Run() { return Desc() * 10 + ZeroLoopHits(); }`。 |
| 期望行为 | `Run()` 返回 `32100`；其中 `3210` 证明递减 update 与循环条件都正常，末尾 `0` 证明零迭代路径没有错误执行循环体。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-63：补 `Types.Float` 的负数与小数矩阵，直接锁住 `float32/float64` 两条执行路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptEngine::GetEngineProperty`、`asIScriptContext::GetReturnFloat`、`asIScriptContext::GetReturnQWord` |
| 现有测试覆盖 | `Types.Float` 只验证正数乘法 `3.14 * 2.0`，没有覆盖负号、小数相加后再乘、也没有专门锁定 `asEP_FLOAT_IS_FLOAT64` 两条返回读取路径 |
| 风险评估 | 如果负数符号传播、fractional arithmetic 或 `float32/float64` 返回值解码在其中一条路径上回归，当前 `TypeTests` 仍可能因为单个正数乘法继续绿灯 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.Float.NegativeAndFractionalMatrix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 复用现有 `ReadExpectedFloatResult` helper，构造一个既包含负数又包含小数加法的脚本入口，分别在 `float32` 与 `float64` 模式下执行并断言结果。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule` + `GetFunctionByDecl` + `ReadExpectedFloatResult`。脚本可按 engine property 选择：`double Run() { double A = -1.25; double B = 0.5; double C = 2.0; return (A + B) * C; }` 或 `float Run() { float A = -1.25f; float B = 0.5f; float C = 2.0f; return (A + B) * C; }`。 |
| 期望行为 | 两条模式都应返回接近 `-1.5` 的结果；helper 需要同时验证 `Prepare == asSUCCESS`、`Execute == asEXECUTION_FINISHED`，以及解码后的返回值在容差内匹配 `-1.5`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule` + `GetFunctionByDecl` + 现有 `ReadExpectedFloatResult` |
| 优先级 | P1 |

#### NewTest-64：补 `VariableScopes` 的 out-of-scope 负路径，直接锁住 block variable 不得逃逸

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptFunction::GetVar` |
| 现有测试覆盖 | `Compiler.VariableScopes` 只验证成功脚本里 `GetVarCount() >= 2`，没有任何测试证明 inner block 变量离开作用域后会被拒绝访问 |
| 风险评估 | 一旦 compiler 把 block-scope 变量错误泄漏到外层，或 diagnostics 丢失，当前 internals 套件不会报警；作用域 bookkeeping 回归会被推迟到更高层脚本里才显现 |
| 建议测试名 | `Angelscript.TestModule.Internals.Compiler.VariableScopes.OutOfScopeUseRejected` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 场景描述 | 编译一个最小脚本，在内部 block 中声明 `Inner`，离开 block 后直接 `return Inner;`，验证编译失败并产生作用域相关 diagnostics。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary`；脚本示例：`int Entry() { { int Inner = 2; } return Inner; }`。为本次编译使用独立 module name，例如 `CompilerVariableScopesOutOfScope`。 |
| 期望行为 | 编译返回 `bCompileSucceeded == false`、`CompileResult == ECompileResult::Error`；diagnostics 命中 `Inner` 与 undeclared / out-of-scope 相关文本，且行列号非零；失败后模块中不应留下可执行 `Entry()`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `Engine.GetModuleByModuleName` |
| 优先级 | P1 |

#### NewTest-65：补 overload resolution 对“exact match 优先于 widening”的偏好断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `Functions.OverloadResolution` 只覆盖 `Convert(int)` 与 `Convert(float)` 的两个 exact match，没有覆盖“一个 exact match 对一个 implicit widening overload”的选择偏好 |
| 风险评估 | 如果 overload binder 错误偏向 widening conversion，当前函数套件仍可能漏报，特别是在未来补更多数值类型和 2.38 兼容语义时更容易出现静默分派错误 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Functions.OverloadResolution.ExactVsWideningPreference` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 场景描述 | 定义 `Pick(int)` 与 `Pick(double)` 两个 overload，分别用 `int` 实参和 `float32` 实参调用，把 exact match 与 widening path 的选择编码进最终结果。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Pick(int Value) { return 100 + Value; } int Pick(double Value) { return 200 + int(Value); } int Run() { float32 FloatValue = 4.0f; return Pick(3) * 1000 + Pick(FloatValue); }`。 |
| 期望行为 | `Run()` 返回 `103204`；前半段 `103` 证明 `Pick(3)` 走了 exact `int` overload，后半段 `204` 证明 `float32` 实参走了 `double` widening overload，而不是错误落到 `int` 路径。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-84 |
| BadIsolation | 1 | Issue-85 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingEdgeCase: 2, MissingErrorPath: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 00:47)

### 一、现有测试问题

#### Issue-87：`Pointer` / `Template` / `Factory` 负向用例只验证“编译失败”，没有锁定失败类型与诊断

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Functions.Pointer`、`Template`、`Factory` |
| 行号范围 | 97-109, 173-185, 202-214 |
| 问题描述 | 这 3 个用例都走 `CompileModuleWithResult(...)`，但最后只做 `TestFalse(..., bCompiled)`，随后直接把 `bPassed = !bCompiled` 作为结果返回。它们没有断言 `CompileResult == ECompileResult::Error`，也没有验证 diagnostics 是否真的命中了 `funcdef` / template / factory-style handle 这些目标语法。只要编译在任意位置以任意原因失败，这些测试都会通过。 |
| 影响 | 当前套件无法区分“目标特性被正确拒绝”和“同一脚本里其他路径先崩掉”这两类失败；一旦 parser/compiler 在报错位置、错误类型或诊断文本上回归，`FunctionTests` 仍可能保持假绿灯。 |
| 修复建议 | 1）把 3 个用例统一改成 `CompileModuleWithSummary` 或至少补 `CompileResult == ECompileResult::Error` 断言；2）分别验证 diagnostics 命中 `funcdef`、template type parameter `<T>`、`@CreateCarrier`/handle factory 等关键字，并带有非零行列号；3）若继续保留当前 smoke test，至少把它们重命名为 `CompileFails`，避免继续用宽泛特性名暗示诊断质量已被覆盖。 |

#### Issue-88：`Execution.Context` 在“两个 context 必须不同”断言失败时会落入 use-after-release 风险

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.Context` |
| 行号范围 | 331-345 |
| 问题描述 | 用例先创建 `ContextA` 和 `ContextB`，随后只调用 `TestNotEqual(..., ContextA, ContextB)` 记录二者应不同，但无论断言是否通过都会立刻执行 `ContextB->Release()`，之后继续使用 `ContextA`。如果 `Engine.CreateContext()` 将同一底层 context 句柄复用两次，当前代码会先把 `ContextA` 释放，再继续对已释放对象调用 `GetState()` / `Prepare()` / `Execute()`。 |
| 影响 | 这会把本应只是“断言失败”的情况升级为潜在崩溃、双重释放或误导性的后续错误，导致 `Execution.Context` 在底层 context 池回归时变成不稳定测试。 |
| 修复建议 | 1）把“两个 context 必须不同”改成带控制流的硬 guard：`if (!TestNotEqual(...)) { ContextA->Release(); return false; }`；2）只有在确认 `ContextA != ContextB` 后再释放 `ContextB`；3）若项目允许复用同一 pooled context，应把测试改成“不同生命周期句柄必须在使用前处于 clean state”，而不是默认地址必异。 |

#### Issue-89：`OverloadResolution` 把两个 overload 分派结果压成一个总和，无法精确证明各自命中

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Functions.OverloadResolution` |
| 行号范围 | 73-80 |
| 问题描述 | 用例脚本把 `Convert(4)` 与 `Convert(2.0f)` 的返回值直接相加，最后只断言总结果 `11`。这只能说明“两个调用合起来等于 11”，却不能独立证明第一个调用确实命中了 `Convert(int)`、第二个调用确实命中了 `Convert(float)`；一旦未来两个 overload 的返回体修改成别的编码方式，或者某个错误路径刚好被另一路结果抵消，当前断言的定位能力会很差。 |
| 影响 | overload binder、隐式转换优先级或函数体分派一旦退化，测试即使失败也难以判断是哪一个 call site 出错；更糟的是某些错误组合仍可能在单个总和上伪装成正确结果。 |
| 修复建议 | 1）把两个调用结果编码进不同权位，例如 `return Convert(4) * 100 + Convert(2.0f);`，显式断言 `506`；2）或拆成两个独立 helper/子测试，分别锁定 `int` exact match 与 `float` exact match；3）保留现有场景时，至少把测试说明改成“两个 overload 的组合 smoke test”，不要继续让单一总和代表完整分派正确性。 |

### 二、需要新增的测试

#### NewTest-66：补接口继承元数据可见性断言，直接锁住 parent method 是否进入 child interface 反射

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 关联函数 | `FClassData::ImplementedInterfaces` / `InterfaceMethodDeclarations` |
| 现有测试覆盖 | `DeclareInheritance` 和 `InheritedInterface` 都只验证接口类生成成功或 actor 同时满足 parent/child interface，没有任何用例直接锁定 child interface 自身的父链与父方法可见性 |
| 风险评估 | 如果接口继承退化成“运行时类上平铺多个接口标记”，而 child interface 自身丢失 parent link 或看不到父方法，当前套件仍可能全绿；这会直接削弱 P10 UInterface 继承主线的反射可信度 |
| 建议测试名 | `Angelscript.TestModule.Interface.DeclareInheritance.ParentMetadataVisibility` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceDeclareTests.cpp` |
| 场景描述 | 编译一个 parent/child 两层脚本接口模块，直接对白盒反射检查 child interface 的父链与方法可见性，而不是只看生成成功。 |
| 输入/前置 | 复用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileScriptModule` + `FindGeneratedClass`。脚本沿用 `UIDamageableInh` / `UIKillableInh : UIDamageableInh` 的结构；测试拿到 `ParentInterface` 和 `ChildInterface` 后，检查 `ChildInterface->GetSuperClass()`、`ChildInterface->FindFunctionByName(TEXT("TakeDamage"))`、`ChildInterface->FindFunctionByName(TEXT("Kill"))`。 |
| 期望行为 | `ParentInterface` 与 `ChildInterface` 都非空；`ChildInterface->GetSuperClass() == ParentInterface` 或项目等价的 interface hierarchy helper 返回 true；`TakeDamage` 与 `Kill` 两个 `UFunction` 都能从 `ChildInterface` 上找到；`ChildInterface` 继续带 `CLASS_Interface` 标记。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `CompileScriptModule` + `FindGeneratedClass` + `UClass::GetSuperClass` + `UClass::FindFunctionByName` |
| 优先级 | P1 |

#### NewTest-67：补“脚本通过 native interface 调用纯 C++ 实现对象”的反向桥接场景

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CanCastScriptObjectToUnrealInterface` |
| 现有测试覆盖 | `NativeImplement`、`NativeInheritedImplement`、`NativeReferenceRoundTrip` 全部是“script class 实现 native interface，再由脚本或 C++ 去调用 script 实现”；当前没有任何用例覆盖脚本把一个纯 C++ UObject/AActor 视为 native interface 并成功调度到 C++ 实现 |
| 风险评估 | 一旦 native interface cast fast path、reflective fallback 或 ref 参数桥接只在“script implements native interface”方向可用，当前套件不会报警；真正业务里 script 调用现有 C++ gameplay object 的 interface 路径会延后暴露问题 |
| 建议测试名 | `Angelscript.TestModule.Interface.NativeImplement.CppImplementerScriptCall` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeBridgeTests.cpp` |
| 场景描述 | 新增一个 test-only C++ fixture actor，实现 `UAngelscriptNativeParentInterface`；脚本侧接收该对象、cast 成 interface、调用 getter / setter / `int&` ref 参数方法，并把结果写回脚本 actor 属性。 |
| 输入/前置 | 在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h/.cpp` 新增 `ATestNativeParentInterfaceActor`，实现 `GetNativeValue() const`、`SetNativeMarker(FName)`、`AdjustNativeValue(int32, int32&)`。测试复用 `EnsureNativeInterfaceFixturesBound()`、`CompileScriptModule`、`FActorTestSpawner`：先 spawn C++ fixture actor，再 spawn 一个脚本 actor，给脚本 actor 注入 `UObject Target` 指向该 fixture，在 `BeginPlay` 中执行 `UAngelscriptNativeParentInterface ParentRef = Cast<UAngelscriptNativeParentInterface>(Target);`，随后调用三个接口方法并记录 `ReadValue`、`AdjustedValue`、`bCastSucceeded`。 |
| 期望行为 | 脚本侧 `bCastSucceeded == 1`；`ReadValue` 等于 C++ fixture 初始值，例如 `123`；脚本调用 `SetNativeMarker(n"FromScript")` 后，C++ fixture 上的 `NativeMarker` 变为 `FromScript`；脚本通过 `AdjustNativeValue(5, Value)` 后写回结果为 `15`；这些断言共同证明 script->native interface 的 cast、dispatch 与 ref 参数桥接都成立。 |
| 使用的 Helper | `EnsureNativeInterfaceFixturesBound` + 新增 C++ fixture actor + `CompileScriptModule` + `FActorTestSpawner` + `ReadPropertyValue` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-87 |
| FlakyRisk | 1 | Issue-88 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 00:58)

### 一、现有测试问题

#### Issue-90：`ValueTypeConstruction` / `ValueTypeCopyAndArithmetic` 都把值类型语义压成单个整数，字段级回归容易被掩盖

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Objects.ValueTypeConstruction`、`Angelscript.TestModule.Angelscript.Objects.ValueTypeCopyAndArithmetic` |
| 行号范围 | 20-55 |
| 问题描述 | `ValueTypeConstruction` 只执行 `FIntPoint Point(3, 4); return Point.X + Point.Y;` 并断言结果 `7`；`ValueTypeCopyAndArithmetic` 也只返回 `Original.X * 10 + Copy.X` 并断言 `57`。这两条断言都把多个字段和多个语义压成单个数字，无法独立证明 `X/Y` 映射是否正确、copy constructor 是否保留了 `Y`、以及 `Copy + FIntPoint(2, 0)` 是否只修改了副本的 `X`。例如字段被交换、`Y` 复制丢失或算术只更新了一半分量时，当前编码仍可能误过。 |
| 影响 | `FIntPoint` 这类值类型的构造、复制和按分量运算一旦在某个字段上回归，当前对象模型套件很难给出精准报警，甚至可能继续绿灯，导致 value-type object model 的基础语义被高估。 |
| 修复建议 | 1）把字段结果拆成可定位的编码，例如 `return Point.X * 100 + Point.Y;`，显式断言 `304`；2）对 copy/arithmetic 用例同时编码 `Original.X`、`Original.Y`、`Copy.X`、`Copy.Y`，例如 `return Original.X * 1000 + Original.Y * 100 + Copy.X * 10 + Copy.Y;` 并断言 `5676`；3）若担心单用例过宽，可拆成 `ValueTypeConstruction.Fields` 与 `ValueTypeCopyAndArithmetic.CopyPreservesSource` 两个更聚焦的测试。 |

#### Issue-91：`ImplementsInterfaceMethod` 实际测试的是 `ImplementsInterface()` API，而不是接口方法行为

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.ImplementsInterfaceMethod` |
| 行号范围 | 175-249 |
| 问题描述 | 用例脚本里唯一被观察的行为是 `BeginPlay()` 调用 `this.ImplementsInterface(UIDamageableImplCheck::StaticClass())`，然后把 `ImplementsResult` 置为 `1`；C++ 侧也只再次断言 `ScriptClass->ImplementsInterface(InterfaceClass)`。整个测试没有通过 interface reference 调用 `TakeDamage(float Amount)`，也没有读取任何由接口方法产生的状态变化。测试名中的 `Method` 会让人误以为这里覆盖了“接口方法实现/分派”，实际上它只是在重复验证元数据查询 API。 |
| 影响 | 当接口方法 thunk、参数封送或脚本实现分派发生回归时，这个用例不会报警；同时它还会和 `ImplementBasic` / `ImplementMultiple` 形成语义重叠，稀释 Interface 套件对真正方法生命周期的覆盖价值。 |
| 修复建议 | 1）若目标就是验证 `ImplementsInterface()` API，应将测试名收窄为 `ImplementsInterfaceStaticClass` 或同类语义名；2）若目标是验证接口方法，应在 `BeginPlay()` 或 C++ 侧真正调用 `TakeDamage(...)`，并新增 `UPROPERTY()` 记录传入值，再断言状态变化；3）避免继续让“方法”字样对应纯元数据断言，减少后续审查误判。 |

### 二、需要新增的测试

#### NewTest-68：补 `FIntPoint` 构造字段矩阵，避免 `ValueTypeConstruction` 继续只看字段求和

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `ValueTypeConstruction` 只执行 `FIntPoint(3, 4)` 后断言 `Point.X + Point.Y == 7`，没有独立锁定 `X/Y` 两个字段 |
| 风险评估 | 如果值类型构造时字段顺序交换、某一分量未初始化或成员访问映射错误，现有测试仍可能因为总和恰好相同而漏报 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Objects.ValueTypeConstruction.Fields` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 场景描述 | 构造一个 `FIntPoint(3, 4)`，分别读取 `X` 与 `Y`，把两个字段编码到不同权位后返回。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Run() { FIntPoint Point(3, 4); return Point.X * 100 + Point.Y; }`。 |
| 期望行为 | `Run()` 返回 `304`；这要求 `X == 3`、`Y == 4` 同时成立，避免单个求和掩盖字段映射错误。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

#### NewTest-69：补值类型 copy + arithmetic 的分量隔离断言，直接锁住“原对象不变、拷贝对象按分量更新”

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Prepare`、`asIScriptContext::Execute` |
| 现有测试覆盖 | `ValueTypeCopyAndArithmetic` 只返回 `Original.X * 10 + Copy.X`，完全没有验证 `Original.Y`、`Copy.Y`，也没有证明修改只发生在副本上 |
| 风险评估 | 如果 copy constructor 丢失某个分量、`operator+` 只更新一半字段，或者写回错误污染了原对象，当前对象模型套件仍可能误判通过 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Objects.ValueTypeCopyAndArithmetic.ComponentIsolation` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 场景描述 | 构造 `Original(5, 6)`，拷贝到 `Copy`，再执行 `Copy = Copy + FIntPoint(2, 3)`，把 `Original.X`、`Original.Y`、`Copy.X`、`Copy.Y` 分别编码到返回值里。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本示例：`int Run() { FIntPoint Original(5, 6); FIntPoint Copy(Original); Copy = Copy + FIntPoint(2, 3); return Original.X * 1000000 + Original.Y * 10000 + Copy.X * 100 + Copy.Y; }`。 |
| 期望行为 | `Run()` 返回 `5060709`；其中 `5/6` 证明原对象未被污染，`7/9` 证明拷贝对象的两个分量都按预期更新。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-90 |
| AntiPattern | 1 | Issue-91 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | MissingEdgeCase: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 01:16)

### 一、现有测试问题

#### Issue-92：`InheritedMethodDispatch` / `MultipleInheritanceDispatch` 没有证明 most-derived interface ref 继承了 parent method surface

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.InheritedMethodDispatch`、`Angelscript.TestModule.Interface.MultipleInheritanceDispatch` |
| 行号范围 | 480-552, 681-765 |
| 问题描述 | `InheritedMethodDispatch` 在 `BeginPlay()` 里先把 `Self` cast 成 `UIDamageableDispatch ParentRef`，再单独 cast 成 `UIKillableDispatch ChildRef`；它只通过 `ParentRef.GetDamageLevel()` 读取 parent method，通过 `ChildRef.GetKillCount()` 读取 child-only method。`MultipleInheritanceDispatch` 也是同样模式：`BaseRef.BaseValue()`、`MidRef.MidValue()`、`LeafRef.LeafValue()` 各走各的接口引用。两个用例都没有验证 child/leaf 这一侧的 most-derived interface ref 是否能直接调用 inherited parent methods。只要运行时允许分别 cast 到每一级接口，这两个测试就会通过，即便 child/leaf surface 自身没有合并 parent methods。 |
| 影响 | 如果 `UInterface` 继承链退化成“每一级只能访问自己声明的方法，parent method 不会上浮到 child ref”，当前 dispatch 套件仍会绿灯。这会漏掉 P10 主线里最关键的一类行为回归：最派生 interface ref 表面看起来 cast 成功，但 inherited contract 实际不完整。 |
| 修复建议 | 1）在 `InheritedMethodDispatch` 的脚本 actor 上新增 `UPROPERTY() int ChildParentResult = 0;`，并在 `ChildRef != nullptr` 分支里同时调用 `ChildRef.GetDamageLevel()`，把结果写入该属性；2）在 `MultipleInheritanceDispatch` 上新增 `LeafBaseResult`、`LeafMidResult`，通过 `LeafRef.BaseValue()` / `LeafRef.MidValue()` 直接验证 leaf ref 是否继承了 base/mid surface；3）测试末尾分别读取这些新属性，断言 child/leaf ref 既能调用本级方法，也能调用 inherited parent methods，避免继续用“分别 cast 到不同层级”代替真正的继承 surface 验证。 |

#### Issue-93：`NativeInheritedImplement` 没有验证 child native interface ref 是否继承 parent native methods

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.NativeInheritedImplement` |
| 行号范围 | 218-299 |
| 问题描述 | 脚本类 `AScenarioInterfaceNativeInheritedImplement` 实现的是 `UAngelscriptNativeChildInterface`，但 `BeginPlay()` 里仍然是把 parent surface 和 child surface 分开验证：先通过 `UAngelscriptNativeParentInterface ParentRef` 调 `GetNativeValue()` / `SetNativeMarker()`，再通过 `UAngelscriptNativeChildInterface ChildRef` 只调 `GetChildValue()`。这意味着当前用例只证明“同一个对象能分别 cast 成 parent 和 child”，并没有证明 child native interface ref 本身能直接访问 inherited parent methods。 |
| 影响 | 如果 native child interface 的绑定层只暴露 child-own methods，而没有把 parent methods 合并到 child type info 上，当前测试仍会全部通过。实际业务里脚本常常直接持有 child ref 并期待它拥有 parent surface；一旦这条继承契约断裂，会在较晚的脚本调用点才暴露。 |
| 修复建议 | 1）在脚本 actor 上新增 `UPROPERTY() int ChildParentResult = 0;` 与 `UPROPERTY() int ChildAdjustedValue = 0;`；2）在 `ChildRef != nullptr` 分支里补 `ChildParentResult = ChildRef.GetNativeValue(); int Value = 20; ChildRef.AdjustNativeValue(9, Value); ChildAdjustedValue = Value; ChildRef.SetNativeMarker(n"ChildRoute");`；3）测试末尾同时断言 child ref 读到 parent getter 结果 `7`、ref 参数回写为 `29`，以及 `NativeMarker == ChildRoute`，从而锁住 native child interface 对 parent surface 的完整继承。 |

### 二、需要新增的测试

#### NewTest-70：补 most-derived script interface ref 对 inherited parent methods 的直接调度断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 关联函数 | `FClassData::ImplementedInterfaces`、`FClassData::InterfaceMethodDeclarations` |
| 现有测试覆盖 | `InheritedMethodDispatch`、`MultipleInheritanceDispatch` 只验证“分别 cast 到各层 interface 后，各自的方法能调用”，没有任何用例证明 child/leaf ref 自身可直接调用 inherited parent methods |
| 风险评估 | 如果 interface surface 合并在 child/leaf 这一层失效，当前 dispatch 套件仍会通过；业务脚本在持有 most-derived interface ref 时会迟到暴露“看起来继承成功，实际少方法”的回归 |
| 建议测试名 | `Angelscript.TestModule.Interface.InheritedMethodDispatch.ChildSurfaceIncludesParentMethods` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 场景描述 | 复用现有 `UIDamageableDispatch -> UIKillableDispatch` 和 `UIBaseDispatchChain -> UIMidDispatchChain -> UILeafDispatchChain` 结构，但在脚本里直接通过 `ChildRef` / `LeafRef` 调用 inherited parent methods，并把结果写入独立属性。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileScriptModule` + `FActorTestSpawner` + `BeginPlayActor`。在 `AScenarioInterfaceInheritedDispatch` 上新增 `ChildParentResult`；在 `AScenarioInterfaceMultiDispatch` 上新增 `LeafBaseResult`、`LeafMidResult`。`BeginPlay()` 中追加 `ChildParentResult = ChildRef.GetDamageLevel();`、`LeafBaseResult = LeafRef.BaseValue();`、`LeafMidResult = LeafRef.MidValue();`。 |
| 期望行为 | `ChildParentResult == 3`；`LeafBaseResult == 2`；`LeafMidResult == 4`；并保留原有 `ChildResult == 5`、`LeafResult == 8`。这些断言要同时成立，才能证明 most-derived script interface ref 既保留本级方法，也继承 parent surface。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH` + `CompileScriptModule` + `FActorTestSpawner` + `BeginPlayActor` + `ReadPropertyValue` |
| 优先级 | P1 |

#### NewTest-71：补 child native interface ref 直接访问 inherited parent surface 的调度矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CanCastScriptObjectToUnrealInterface`、`FAngelscriptEngine::RegisterInterfaceMethodSignature` |
| 现有测试覆盖 | `NativeInheritedImplement` 只验证 parent-typed ref 可调用 parent methods、child-typed ref 可调用 child method；当前没有任何场景证明 child native interface ref 本身继承了 parent getter/setter/ref-parameter surface |
| 风险评估 | 如果 native interface 绑定层没有把 parent methods 合并到 child interface type info，当前套件仍会绿灯；脚本侧持有 child native interface ref 的常见调用模式会直接失守 |
| 建议测试名 | `Angelscript.TestModule.Interface.NativeInheritedImplement.ChildSurfaceIncludesParentMethods` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 场景描述 | 在现有 `AScenarioInterfaceNativeInheritedImplement` 脚本 actor 上，直接通过 `UAngelscriptNativeChildInterface ChildRef` 调用 `GetNativeValue()`、`SetNativeMarker()` 和 `AdjustNativeValue(int, int&)`，把结果写回 actor 属性。 |
| 输入/前置 | 复用 `EnsureNativeInterfaceFixturesBound()`、`CompileScriptModule`、`FActorTestSpawner`。新增脚本属性 `ChildParentResult`、`ChildAdjustedValue`、沿用 `NativeMarker`；在 `ChildRef != nullptr` 分支里执行 `ChildParentResult = ChildRef.GetNativeValue(); int Value = 20; ChildRef.AdjustNativeValue(9, Value); ChildAdjustedValue = Value; ChildRef.SetNativeMarker(n"ChildRoute");`。 |
| 期望行为 | `ChildCastWorked == 1`；`ChildParentResult == 7`；`ChildAdjustedValue == 29`；`NativeMarker == ChildRoute`。如果 child native interface ref 只能访问 child-own methods，这组断言会立即失败，从而直接锁住 inherited parent surface 是否真的被 child ref 继承。 |
| 使用的 Helper | `EnsureNativeInterfaceFixturesBound` + `ASTEST_CREATE_ENGINE_SHARE_FRESH` + `CompileScriptModule` + `FActorTestSpawner` + `BeginPlayActor` + `ReadPropertyValue` |
| 优先级 | P1 |

#### NewTest-72：补 interface method signature 生命周期测试，覆盖 `ReleaseInterfaceMethodSignature` 的无测试源码路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::RegisterInterfaceMethodSignature`、`FAngelscriptEngine::ReleaseInterfaceMethodSignature` |
| 现有测试覆盖 | 当前只有 `AngelscriptInterfaceNativeTests.cpp` 通过 `EnsureNativeInterfaceBoundForTests()` 间接调用 `RegisterInterfaceMethodSignature`；没有任何测试直接覆盖 signature 注册计数、显式 release、重复 release/null release 或 shutdown 清空行为 |
| 风险评估 | 一旦 interface signature 数组泄漏、重复保留旧签名或 release 后仍残留 stale pointer，native interface 反射调用会在后续重绑/热重载后才暴露错误，现有套件无法直接报警 |
| 建议测试名 | `Angelscript.TestModule.Interface.NativeBinding.SignatureRegistrationLifecycle` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeBindingTests.cpp` |
| 场景描述 | 为 `FAngelscriptEngine` 增加一个最小 test access seam，读取 `InterfaceMethodSignatures.Num()`；测试中注册两个 signature、按顺序 release、再验证计数恢复，并额外覆盖 `nullptr` release no-op。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；新增 test-only helper `FAngelscriptInterfaceSignatureTestAccess` 暴露 `GetSignatureCount(FAngelscriptEngine&)`。测试流程：记录 baseline count；注册 `GetNativeValue` 与 `SetNativeMarker` 两个 signature；断言 count 增加 2；release 第一个后 count 减 1；release `nullptr` 后 count 不变；release 第二个后 count 回到 baseline。 |
| 期望行为 | baseline 后 `+2 -> +1 -> +1 -> +0` 的计数迁移严格成立；这直接证明 `RegisterInterfaceMethodSignature`/`ReleaseInterfaceMethodSignature` 的增删合同正确，避免 native interface 绑定在重复 setup 或未来热重载路径上堆积 stale signature。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH` + 新增 `FAngelscriptInterfaceSignatureTestAccess` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-92 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 2, NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 01:23)

### 一、现有测试问题

#### Issue-94：`Builder.SingleModulePipeline` 用高层 `BuildModule`/`ExecuteIntFunction` 代替 builder 白盒路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Builder.SingleModulePipeline` |
| 行号范围 | 41-67 |
| 问题描述 | 用例虽然位于 `Internals.Builder` 套件，文件也已经包含 `as_builder.h` 和 `CreateBuilderModule(...)` helper，但实际完全走的是 `AngelscriptTestSupport::BuildModule(...) -> GetFunctionByDecl(...) -> ExecuteIntFunction(...)` 的高层引擎包装路径。它没有实例化 `asCBuilder`、没有检查 builder message/error collection、也没有观察 raw module 在 section 添加后的状态，因此更像一次“编译并执行 42”的集成冒烟，而不是 builder 内部单测。 |
| 影响 | 一旦 `FAngelscriptEngine` 包装层、automatic import、module cache 或执行 helper 本身发生变化，这个用例会把高层故障误记为 builder 回归；反过来，builder 内部 message 收集、section 处理或符号表阶段退化时，也可能被高层 helper 的兜底行为掩盖，降低 `Internals.Builder` 的定位价值。 |
| 修复建议 | 1）若目标真的是高层冒烟，把该用例移到 `AngelscriptCoreExecutionTests.cpp` 或至少改名为 `CompilePipelineSmoke`，避免继续冒充 builder 白盒测试；2）更推荐改成真正的 builder 路径：使用 `CreateBuilderModule(...)` 创建 raw module，显式添加 script section，并通过 `asCBuilder` 或 raw module build 触发编译，再断言函数数目、builder 结果和执行结果；3）保留执行断言时，也要补 builder 级观察，例如 message count 为 `0`、module 中只生成一个 `Entry()`。 |

#### Issue-95：`GC.EmptyCollect` / `GC.ReportUndestroyedEmpty` 只看返回码，没锁定空路径的无副作用合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.GC.EmptyCollect`、`Angelscript.TestModule.Internals.GC.ReportUndestroyedEmpty` |
| 行号范围 | 288-301, 327-340 |
| 问题描述 | 这两个用例都创建一个 fresh `asCGarbageCollector`，分别调用 `GarbageCollect(asGC_FULL_CYCLE, 1)` 和 `ReportAndReleaseUndestroyedObjects()`，然后只断言返回值是 `0` 并把 `Result == 0` 作为最终通过条件。它们没有读取 `GetStatistics()` 前后快照、没有验证对象表仍为空、也没有重复调用来证明 empty path 的幂等性。只要函数继续返回 `0`，即便内部错误地增加了统计计数、残留了 queue state 或污染了后续 collector 状态，这两个测试仍会绿灯。 |
| 影响 | GC 空路径一旦发生“返回码正确但内部状态被弄脏”的回归，当前套件无法报警；后续 `ManualCycleCollection` / `CycleDetection` 出现顺序相关失败时，也很难第一时间回溯到 empty-path cleanup 已经失真。 |
| 修复建议 | 1）在调用前后都读取 `GetStatistics()`，显式断言 `CurrentSize`、`TotalDestroyed`、`TotalDetected`、`NewObjects`、`TotalNewDestroyed` 全部保持不变；2）对两个 empty-path API 都再调用第二次，验证重复执行仍返回 `0` 且统计不变；3）可顺手追加 `GetObjectInGC(0, ...) == asINVALID_ARG` 断言，证明空 collector 没有留下可查询条目。 |

### 二、需要新增的测试

#### NewTest-73：补 `PrepareAngelscriptContextWithLog` 的成功/失败路径，直达当前执行层无测试源码

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `PrepareAngelscriptContextWithLog` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` 里的 `Basic`、`OneArg`、`TwoArgs`、`FourArgs`、`MixedArgs`、`Nested`、`Script` 全部直接调用 `Context->Prepare(...)`；当前没有任何用例直接验证这个包装函数在 prepare 成功和 engine mismatch 失败时的返回值与日志合同 |
| 风险评估 | 一旦该 wrapper 在失败时不再返回 `false`、不再记录 `Callsite` / function declaration，或者把错误 prepare 当成成功吞掉，执行层调用点会失去最关键的定位日志，而现有 `ExecutionTests` 不会报警 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.PrepareContextWithLog.SuccessAndFailure` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionDiagnosticsTests.cpp` |
| 场景描述 | 分别覆盖同引擎成功 prepare 与跨引擎失败 prepare 两条路径。先在 Engine A 中编译 `int Entry() { return 42; }`；然后用 Engine A 的 context + Engine A 的 `Entry()` 验证 wrapper 返回 `true`。再创建 Engine B 的 context，把 Engine A 的 `Entry()` 传给 `PrepareAngelscriptContextWithLog(...)`，刻意制造 `ContextEngine != FunctionEngine` 的 prepare 失败，并验证日志内容。 |
| 输入/前置 | 使用 `CreateIsolatedCloneEngine()` 创建两个 isolated engine；通过 `BuildModule` + `GetFunctionByDecl("int Entry()")` 拿到 Engine A 的函数；成功路径调用 `PrepareAngelscriptContextWithLog(ContextA, FunctionA, TEXT("PrepareSuccess"))`；失败路径调用 `PrepareAngelscriptContextWithLog(ContextB, FunctionA, TEXT("PrepareMismatch"))`，并用 `AddExpectedErrorPlain(TEXT("Failed to prepare Angelscript context for 'PrepareMismatch'"), Contains, 1)` 与 `AddExpectedErrorPlain(TEXT("int Entry()"), Contains, 1)` 锁定日志。 |
| 期望行为 | 成功路径返回 `true`，随后 `ContextA->Execute()` 成功并得到 `42`；失败路径返回 `false`，不会进入执行完成态，且日志同时包含 `PrepareMismatch` 与 `int Entry()`。这组断言直接锁住 wrapper 的成功/失败合同，而不是继续只看裸 `Prepare()`。 |
| 使用的 Helper | `CreateIsolatedCloneEngine` + `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `PrepareAngelscriptContextWithLog` + `AddExpectedErrorPlain` |
| 优先级 | P1 |

#### NewTest-74：补 `Interface.HotReload` 的既有实例重绑定场景，验证旧 actor 不会继续执行旧接口函数体

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::PerformHotReload` |
| 现有测试覆盖 | `Angelscript.TestModule.Interface.HotReload` 只验证 reload 前后类和接口元数据仍存在、`ImplementsInterface(...)` 仍为真；没有任何现有用例在 reload 前先 spawn 实例、再验证同一个实例上的接口方法是否切换到新实现 |
| 风险评估 | 如果 full reload 只更新了类元数据，但既有实例保留旧 `UFunction` thunk 或旧 script body，当前热重载套件仍会全绿；实际游戏里运行中的对象会继续执行旧逻辑，直到更晚的行为测试才暴露 |
| 建议测试名 | `Angelscript.TestModule.Interface.HotReload.ExistingInstanceDispatchRebinds` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceLifecycleTests.cpp` |
| 场景描述 | 复用 `UIDamageableHR` / `AScenarioInterfaceHotReload` 两版脚本，但在 reload 前先生成一个 actor 实例并通过 `ProcessEvent` 调用 `TakeDamage`。V1 把 `DamageReceived = Amount`，V2 改成 `DamageReceived = Amount * 2.0`。重点验证同一个 actor 在 reload 后再次收到 `TakeDamage` 时，会执行 V2 的函数体而不是旧逻辑。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileScriptModule` + `FActorTestSpawner`。先编译 V1、spawn actor、通过 `Actor->FindFunction(TEXT("TakeDamage"))` + `ProcessEvent` 传入 `3.0f`，读取 `DamageReceived == 3.0f`；随后执行 `CompileModuleWithResult(&Engine, ECompileType::FullReload, ModuleName, TEXT("ScenarioInterfaceHotReload.as"), ScriptV2, ReloadResult)`；对同一个 actor 再次 `ProcessEvent` 传入 `4.0f`，最后额外 spawn 一个新 actor 作为对照并传入 `5.0f`。 |
| 期望行为 | 初次调用后旧实例 `DamageReceived == 3.0f`；reload 成功且 `ReloadResult` 为 handled；reload 后再次调用同一实例时 `DamageReceived == 8.0f`；新实例调用后 `DamageReceived == 10.0f`。旧实例和新实例都命中新实现，才能证明接口方法 dispatch/thunk 已真正重绑定。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH` + `CompileScriptModule` + `CompileModuleWithResult` + `FActorTestSpawner` + `AActor::FindFunction`/`ProcessEvent` + `ReadPropertyValue` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-94 |
| WeakAssertion | 1 | Issue-95 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 01:45)

### 一、现有测试问题

#### Issue-96：`Operators.GetSet` / `Misc.DuplicateFunction` 在 shared engine 上创建固定名 raw module 却没有清理

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Operators.GetSet`、`Angelscript.TestModule.Angelscript.Misc.DuplicateFunction` |
| 行号范围 | `AngelscriptOperatorTests.cpp` 45-75；`AngelscriptMiscTests.cpp` 107-137 |
| 问题描述 | 这两个用例都在 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE` 下直接调用 `ScriptEngine->GetModule(..., asGM_ALWAYS_CREATE)` 创建 raw AngelScript module，模块名分别固定为 `ASOperatorGetSetRaw` 与 `ASMiscDuplicateFunctionRaw`，但测试结束时没有任何 `DiscardModule()` 或 shared-engine reset。`ASTEST_BEGIN_SHARE` 只建立 `FAngelscriptEngineScope`，不会像 `FULL/CLONE` 那样自动丢弃模块；`FScopedAutomaticImportsOverride` 也只恢复 imports，不会清理 module registry。结果是这些 raw module 会在同一进程后续测试中继续留在共享引擎里，形成顺序相关的隐式状态。 |
| 影响 | 如果后续测试复用同名 raw module，或者 shared engine 上残留的 module/diagnostics 改变了 parser builder 的初始状态，这两个用例会把跨测试污染误当成当前断言结果。用户要求重点检查的“某个测试依赖前一个测试创建的类型/状态”在这里已经具备条件，只是目前没有显式爆出来。 |
| 修复建议 | 1）把这两个用例切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；2）在创建 raw module 后立刻加 `ON_SCOPE_EXIT`，显式执行 `ScriptEngine->DiscardModule("ASOperatorGetSetRaw")` / `ScriptEngine->DiscardModule("ASMiscDuplicateFunctionRaw")`；3）模块名追加测试唯一后缀，避免 future rerun 或并行执行时继续撞固定名字；4）若保留 shared engine，至少在测试尾部补 `ResetSharedInitializedTestEngine(Engine)` 或等价 reset helper，确保 raw module 不会泄漏到下一条用例。 |

#### Issue-97：`GC.CycleDetection` 的最终统计断言过弱，无法证明 full collect 真的推进了销毁阶段

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.GC.CycleDetection` |
| 行号范围 | 396-415 |
| 问题描述 | 用例在 detect-only 阶段后记录 `AfterDetect`，然后跑 `RunFullGarbageCollection()`，最终只断言 `AfterCollect.TotalDestroyed >= AfterDetect.TotalDestroyed`。这个条件允许“destroyed 统计完全没有增长”也通过；如果 detect-only 错误地提前增加了 `TotalDestroyed`，或者 full collect 根本没有把统计往前推进，当前断言仍然是绿灯。测试虽然还有 `FGCProbeObject::LiveCount == 0`，但这只覆盖析构结果，没有锁住 GC 统计合同，也没有证明 tracked size 在 full collect 后真正回到空。 |
| 影响 | 一旦 `as_gc` 在“detect-only 只检测不销毁”与“full collect 递增 `TotalDestroyed` 并清空 tracked entries”之间的分界退化，这个用例很可能继续通过，导致 GC 统计和回收阶段错乱被延迟到更复杂的集成场景才暴露。 |
| 修复建议 | 1）把 detect-only 后的断言补强为 `AfterDetect.TotalDestroyed == BeforeRelease.TotalDestroyed`，明确锁住“检测阶段不销毁”；2）把最终断言改成 `AfterCollect.TotalDestroyed > AfterDetect.TotalDestroyed`，并额外检查 `AfterCollect.CurrentSize == 0` 或至少 `< AfterDetect.CurrentSize`；3）保留 `LiveCount == 0`，但把它降级为统计断言后的辅助验证，而不是唯一能证明 full collect 真执行过的证据。 |

### 二、需要新增的测试

#### NewTest-75：补 raw module 失败后重建的隔离回归，验证 duplicate-function 错误不会泄漏旧状态

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp` |
| 关联函数 | `asCModule::Build`、`asIScriptEngine::GetModule` |
| 现有测试覆盖 | `Misc.DuplicateFunction` 只验证 duplicate build 返回 `asERROR`；`Operators.GetSet` 只验证 raw module happy path 编译成功；当前没有任何用例锁定“失败构建后同名 raw module 被 discard/recreate，不会带着旧函数表或错误状态进入下一次 build” |
| 风险评估 | 如果 raw module 在失败 build 后残留半成品函数表、diagnostics 或 registry entry，shared-engine 里的后续测试会出现顺序依赖；当前套件很难把问题定位回“前一个 raw module 失败后没清干净” |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Misc.DuplicateFunction.RawModuleRecreateAfterFailure` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 场景描述 | 直接走 raw `asIScriptEngine` module API。第一次用同名 module 编译 duplicate function 脚本并断言失败，再显式 `DiscardModule()`，随后用同一模块名重新创建 module，编译合法脚本并执行入口函数，确认第二次 build 不受第一次失败污染。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `CreateIsolatedCloneEngine()`，配合 `FAngelscriptEngineScope`、`FScopedAutomaticImportsOverride`。失败脚本示例：`int Test() { return 1; } int Test() { return 2; }`；成功脚本示例：`int Test() { return 42; }`。第一次 `Build()` 后断言 `asERROR` 与 `GetFunctionCount() == 0`；执行 `ScriptEngine->DiscardModule("ASMiscDuplicateFunctionRawIsolation")`；第二次 `GetModule(..., asGM_ALWAYS_CREATE)` + `Build()` 后取 `int Test()` 并执行。 |
| 期望行为 | 第一次构建返回 `asERROR`，module 内没有可执行 `Test()`；显式 discard 后第二次构建返回 `asSUCCESS`，`GetFunctionCount() == 1`，执行 `Test()` 返回 `42`。这组断言直接锁住 raw module 失败路径不会向下一次 build 泄漏旧状态。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` / `CreateIsolatedCloneEngine` + `FAngelscriptEngineScope` + `FScopedAutomaticImportsOverride` + raw `asIScriptEngine::GetModule` + `GetFunctionByDecl` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-76：补 namespace 层级与遮蔽矩阵，避免 `Misc.Namespace` 继续只覆盖单层 happy path

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_parser.cpp` |
| 关联函数 | namespace-qualified symbol lookup / declaration resolution |
| 现有测试覆盖 | `Misc.Namespace` 只验证单层 `MyNamespace::GetValue()` 返回 `42`；没有任何现有用例覆盖 nested namespace、同名全局遮蔽或直接读取 namespace-scoped 常量 |
| 风险评估 | 如果 namespace 解析在嵌套层级、限定名查找或 shadowing 规则上回归，当前 `Misc` 套件仍会全绿；实际项目一旦开始组织较大的脚本命名空间，会在更复杂脚本里才暴露问题 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Misc.Namespace.NestedAndShadowedLookup` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 场景描述 | 定义全局 `Value`、`Outer::Value`、`Outer::Inner::Value` 三个同名常量，并额外提供 `Outer::Inner::GetInner()`；通过一个入口函数同时读取这三层限定名，验证 lookup 命中各自作用域而不会被前缀关键字或全局符号覆盖。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`。脚本建议为：`const int Value = 5; namespace Outer { const int Value = 7; namespace Inner { const int Value = 11; int GetInner() { return Value; } } } int Run() { return Value * 100 + Outer::Value * 10 + Outer::Inner::GetInner(); }`。 |
| 期望行为 | 编译成功并执行 `Run()` 返回 `581`；其中百位锁定全局 `Value`，十位锁定 `Outer::Value`，个位锁定 `Outer::Inner::Value`。如果 nested namespace 或 shadowing lookup 任何一层出错，这个编码结果会立即偏离。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-96 |
| WeakAssertion | 1 | Issue-97 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 01:58)

### 一、现有测试问题

#### Issue-98：`Operators.Power` 用 `int(...)` 截断幂运算结果，掩盖了浮点与优先级回归

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Operators.Power` |
| 行号范围 | 103-123 |
| 问题描述 | 用例脚本固定为 `int Test() { return int(2.0f ** 3.0f); }`，最后只断言返回 `8`。这个 `int(...)` 会把幂运算结果直接截断成整数，因此只要运行时最终落在 `8.x`，测试就会继续通过；它既没有验证 `**` 的原始浮点结果，也没有覆盖和其他表达式组合时的优先级/结合关系。换言之，当前断言只能证明“一个正整数指数 happy path 最后被截成了 8”，远不足以代表 `Operators.Power` 这个标题。 |
| 影响 | 如果 `**` 在 codegen 或运行时上回归成精度错误、浮点路径错误，甚至在和其他运算混用时优先级错乱，当前测试仍可能绿灯，导致运算符套件对幂运算语义的保护明显偏弱。 |
| 修复建议 | 1）把现有用例至少收窄为直接断言浮点结果，不要再先 `int(...)` 截断；2）复用 `Types.Float` 现有的 `ReadExpectedFloatResult` 模式，按 `asEP_FLOAT_IS_FLOAT64` 分支分别断言 `2.0 ** 3.0 == 8.0`；3）再补一个与其他表达式组合的断言，例如 `1.0 + 2.0 ** 3.0` 返回 `9.0`，避免只测单独字面量幂表达式。 |

#### Issue-99：`ScriptNode.Traversal` 实际只检查顶层双向链，未证明 AST 真能被正确遍历

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptScriptNodeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.ScriptNode.Traversal` |
| 行号范围 | 58-93 |
| 问题描述 | 用例输入虽然包含 `int GlobalValue = 1; class FNodeType { int Value; }` 两层结构，但断言只停留在根节点类型、顶层 child 数量、`firstChild/lastChild` 非空，以及 `lastChild->prev == firstChild`。它没有验证 `firstChild->nodeType` / `lastChild->nodeType` 是否分别对应 declaration 与 class，也没有检查 `firstChild->next == lastChild`、`lastChild->next == nullptr`，更没有向下走到 `FNodeType` 的成员节点去证明嵌套 child/next/prev 链可遍历。测试名叫 `Traversal`，实际却只是一个很窄的顶层链表烟雾测试。 |
| 影响 | 一旦 `asCScriptNode` 的 child 链接、顶层顺序或类内成员节点遍历出现回归，当前用例很容易继续通过；这会让 `ScriptNode` 套件高估对 AST 链接结构的保护，问题只能等到 parser/debugger 的更高层场景才暴露。 |
| 修复建议 | 1）显式断言 `Root->firstChild->nodeType == snDeclaration`、`Root->lastChild->nodeType == snClass`、`Root->firstChild->next == Root->lastChild`、`Root->lastChild->next == nullptr`；2）继续向 `snClass` 的 `firstChild` 深入，验证 class member declaration 节点存在并且 `prev/next` 关系正确；3）若后续还想保留“Traversal”这个标题，建议抽一个递归 walker helper，把“访问到的节点类型序列”编码出来，而不是只看 4 个顶层指针。 |

### 二、需要新增的测试

#### NewTest-77：补 `GetUnrealStructFromAngelscriptTypeId` 的类型过滤矩阵，直达当前无测试的 runtime 映射函数

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::GetUnrealStructFromAngelscriptTypeId` |
| 现有测试覆盖 | `TypeTests` 和 `DataTypeTests` 只在 `asCDataType` / script execution 层间接验证类型语义；当前没有任何用例直接调用 `GetUnrealStructFromAngelscriptTypeId`，也没有覆盖它对 `subtype`、`delegate`、`enum` 三个 early-return 过滤分支 |
| 风险评估 | 如果这个映射函数错误地把 enum/delegate/container 当作 `UStruct` 返回，或者对 `AActor` / `FIntPoint` 这类真实 UE 类型返回 `nullptr`，调试器、反射桥接和类型转换路径会得到错误的 UE 结构信息；这类问题现在不会被 LanguageFeatures 套件直接发现 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.GetUnrealStructFromTypeId.FiltersNonStructKinds` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 编译一个最小模块，声明 script enum、delegate 和至少一个带 subtype 的容器类型，同时复用现有已绑定的 `AActor` / `FIntPoint`。对这些 type id 逐个调用 `Engine.GetUnrealStructFromAngelscriptTypeId(...)`，验证只有真实 UE `UClass`/`UScriptStruct` 会映射成功，其余类型全部返回 `nullptr`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；脚本建议包含 `enum ELocalKind { First = 1 }`、`delegate void FLocalDelegate()`、`int Run() { return 1; }`。通过 `asIScriptModule::GetTypeIdByDecl("ELocalKind")`、`GetTypeIdByDecl("FLocalDelegate")`、`Engine.GetScriptEngine()->GetTypeIdByDecl("AActor")`、`GetTypeIdByDecl("FIntPoint")`，以及一个带 subtype 的类型如 `array<int>` / 项目当前可用的容器声明获取 type id。随后分别调用 `Engine.GetUnrealStructFromAngelscriptTypeId(...)`。 |
| 期望行为 | `AActor` 返回 `AActor::StaticClass()`；`FIntPoint` 返回对应 `UScriptStruct`；`ELocalKind`、`FLocalDelegate` 与容器 type id 全部返回 `nullptr`。这组断言直接锁住函数体中的 `GetSubTypeCount() != 0`、delegate tag、multicast delegate tag 与 enum flag 过滤分支。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `asIScriptModule::GetTypeIdByDecl` + `asIScriptEngine::GetTypeIdByDecl` + `FAngelscriptEngine::GetUnrealStructFromAngelscriptTypeId` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-98 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:39)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-130`，保留在前文 `## 测试审查 (2026-04-10 00:35)` 小节（当前约第 3833 行起）；此处仅补真实 EOF 锚点，避免再次命中前文重复汇总块。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-108`，保留在前文 `## 测试审查 (2026-04-10 00:35)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-130 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-10 00:51)

### 一、现有测试问题

#### Issue-131：`NativeScriptHotReload.Phase2B` 把 4 个互不相关的 reload 场景塞进单个用例，首个失败会掩盖后续回归

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2B` |
| 行号范围 | 14-59, 142-236 |
| 问题描述 | `Phase2B` 通过 `VerifyNativeScriptHotReloadInline(...)` 一次性串跑 `HotReloadPhase2BTagCarrier.as`、`SystemUtils.as`、`ActorLifecycle.as`、`MathNamespace.as` 4 份脚本，而 helper 在循环里任何一步 `TestTrue(...)` 失败后都会立刻 `return false`。这意味着只要第 1 个脚本失败，后 3 个脚本本轮根本不会执行；UE automation summary 里也只会留下一个总的 `Phase2B` 结果，无法从测试名直接定位到底是哪一类语言特性回归。当前用例既承担容器/namespace/actor lifecycle 多个主题，又缺少逐脚本独立结果，已经违背单测试单职责。 |
| 影响 | 一个早期失败会把同 phase 内其余脚本的回归完全遮住，降低发现率；同时 failure 定位只能依赖日志细读，review 和回归排查成本明显偏高。对于 LanguageFeatures 这种最大测试区，这种粗粒度用例会持续放大“后续问题被前序问题遮蔽”的风险。 |
| 修复建议 | 1）把 `Phase2B` 拆成至少 4 个独立 automation test，分别覆盖 `TagCarrier`、`SystemUtils`、`ActorLifecycle`、`MathNamespace`；2）如果必须复用公共 helper，就让 helper 按脚本累积 `bAllPassed &= ...`，不要首个失败即返回，并把失败消息里的 test id 细化到文件名；3）行为型 reload 断言与 compile-wrapper smoke 分离，避免一个总用例继续同时承担“输入集合”“执行器”和“报告聚合器”三重职责。 |

### 二、需要新增的测试
---

## 测试审查 (2026-04-10 00:35)

### 一、现有测试问题

#### Issue-130：`Types.Bits` 把 3 个位运算折叠成一个布尔结果，标题与断言覆盖严重不对称

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.Bits` |
| 行号范围 | 233-243 |
| 问题描述 | 用例脚本只有 `int A = 0x0F; int B = 0xF0; return ((A | B) == 0xFF && (A & B) == 0 && (A ^ B) == 0xFF) ? 1 : 0;`，最后再断言返回 `1`。这把 `|`、`&`、`^` 三个 operator 的正确性折叠成一个布尔门，只要最终布尔表达式仍为真，测试就无法指出到底是哪一个运算退化；同时标题 `Bits` 会让人误以为左移、右移、`~`、符号位传播等基础位语义也被覆盖了。 |
| 影响 | 位运算实现如果只在某一类 operator 或某一侧操作数上回归，当前用例只能给出一个模糊失败，甚至可能在错误彼此抵消时继续绿灯；这会直接削弱 `TypeTests` 对最基础 integer bit semantics 的保护。 |
| 修复建议 | 1）把当前脚本改成可定位编码，而不是单个布尔值，例如分别把 `OrResult`、`AndResult`、`XorResult` 编进不同数位后断言精确值；2）至少把 `|`、`&`、`^` 的断言拆成三个独立 `TestEqual` 或三个独立 helper，避免一个布尔门吞掉定位信息；3）若继续保留 `Bits` 这个宽标题，同文件应同步补上 shift / complement 的可观察断言，否则应把现有用例收窄成 `Bits.MaskOps` 一类更准确的命名。 |

### 二、需要新增的测试

#### NewTest-108：补 `GetDebuggerValueFromFunction` 的 blacklist 与非法签名 guard 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptType::GetDebuggerValueFromFunction` |
| 现有测试覆盖 | `Types.FloatDebuggerFormatting` 只覆盖 primitive formatting；`NewTest-25` 已规划 getter 求值 + property address happy path。当前没有任何正式测试锁定 `GetDebuggerValueFromFunction` 在 `GetParamCount() != 0`、hidden world-context 不匹配、以及 `DebuggerBlacklistAutomaticFunctionEvaluation` / `DebuggerBlacklistAutomaticFunctionEvaluationWithoutWorldContext` 命中时必须直接 `return false` 的 guard 合同。 |
| 风险评估 | 如果这些 guard 失效，debugger 会在 watch/evaluate 阶段错误执行带参数函数或黑名单 getter，轻则污染对象状态，重则在无 world context 的对象上触发隐藏副作用；现有套件只能看到零散调试异常，无法把回归定位到 `GetDebuggerValueFromFunction` 的早退路径。 |
| 建议测试名 | `Angelscript.TestModule.Internals.DebuggerValue.FunctionEvaluationGuards` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDebuggerValueTests.cpp` |
| 场景描述 | 编译一个 `UObject` 脚本类，包含 `UPROPERTY() int EvalCount = 0; UFUNCTION() int GetValue() { EvalCount += 1; return 42; } UFUNCTION() int NeedsArg(int Value) { EvalCount += 100; return Value; }`。先创建不在 world 中的对象实例，构造 `ScriptTypeName + ".GetValue"` 加入 `DebuggerBlacklistAutomaticFunctionEvaluationWithoutWorldContext`，调用 `FAngelscriptType::GetDebuggerValueFromFunction(GetValueFunc, Object, ...)` 并断言返回 `false` 且 `EvalCount` 仍为 `0`。随后清空该 blacklist，把同一路径加入 `DebuggerBlacklistAutomaticFunctionEvaluation`，再次验证 unconditional blacklist 也不会执行 getter。最后对 `NeedsArg(int)` 直接调用同一 helper，断言因参数个数不合法而返回 `false`，且 `EvalCount` 仍不变。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 isolated clone engine + `CompileScriptModule` 编译 `UDebuggerValueGuardProbe`；通过 `FindGeneratedClass(&Engine, TEXT("UDebuggerValueGuardProbe"))` 与 `Engine.GetScriptEngine()->GetTypeInfoByName(...)` 拿到 `UClass*` / `asITypeInfo*`；通过 `ScriptType->GetMethodByDecl("int GetValue()")`、`ScriptType->GetMethodByDecl("int NeedsArg(int)")` 取函数；用 `NewObject<UObject>(GetTransientPackage(), ScriptClass)` 创建无 world 的实例；通过 `FindFProperty<FIntProperty>(ScriptClass, TEXT("EvalCount"))` 读取执行计数；对 `UAngelscriptSettings::Get().DebuggerBlacklistAutomaticFunctionEvaluation*` 用 `ON_SCOPE_EXIT` 保存/恢复原始集合，避免污染全局配置。 |
| 期望行为 | 1）`WithoutWorldContext` blacklist 命中时，`GetDebuggerValueFromFunction(...) == false`，`EvalCount == 0`，`OutValue` 不产生可监控地址；2）unconditional blacklist 命中时结果同样为 `false`，不会执行 getter；3）`NeedsArg(int)` 因 `GetParamCount() != 0` 被拒绝，返回 `false` 且 `EvalCount` 仍保持前值；4）移除 blacklist 后再调用 `GetValue()`，helper 才返回 `true` 并把 `EvalCount` 增到 `1`，证明前面的失败确实来自 guard 而不是脚本本身不可执行。 |
| 使用的 Helper | `CompileScriptModule` + `FindGeneratedClass` + `FindFProperty` + `UAngelscriptSettings::Get()` + `FAngelscriptType::GetDebuggerValueFromFunction` + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-130 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-10 00:12)

### 一、现有测试问题

#### Issue-129：`Execution.Script` 只跑一组递增正区间，测试名明显高估脚本执行覆盖面

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.Script` |
| 行号范围 | 478-530 |
| 问题描述 | 用例编译 `int Calculate(int Start, int End)` 后，只执行一次 `Calculate(1, 10)` 并断言返回 `55`。这只能证明“正向递增区间求和”这一条 happy path 能工作，无法区分 `Start > End` 的零迭代、单元素区间、负数范围以及边界条件下的循环终止行为。测试名却是宽泛的 `Execute.Script`，看起来像在代表更广的脚本执行覆盖。 |
| 影响 | 一旦参数编组、循环边界或比较条件在非 happy path 上回归，当前执行层套件仍可能保持全绿；同时这个测试名会误导后续审查，认为“脚本函数执行”已被比实际更完整地验证。 |
| 修复建议 | 1）若继续保留当前单场景断言，把测试名收窄为 `Script.RangeSumHappyPath`；2）更推荐在同一用例内或拆分子测试，追加 `(1, 1)`、`(5, 4)`、`(-2, 2)` 三组输入，分别验证单元素、零迭代和负数区间；3）若想顺手提高定位能力，可把多组结果编码成不同权位，避免未来失败后只看到单个总和。 |

### 二、需要新增的测试

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-129 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无 | 0 | 无 |

---

## 测试审查 (2026-04-09 13:23)

### 一、现有测试问题

#### Issue-118：`Upgrade.MessageCallback` 没有验证 `GetMessageCallback` 返回的 callback/object 真正回环到已注册值

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Upgrade.MessageCallback` |
| 行号范围 | 204-245 |
| 问题描述 | 用例调用 `SetMessageCallback(asFUNCTION(CaptureUpgradeMessage), nullptr, asCALL_CDECL)` 后，确实会通过 `WriteMessage(...)` 观察到回调被触发，但对白盒 getter 的断言只有 `GetMessageCallback(...) == asSUCCESS` 和 `CallConv == asCALL_CDECL`。它没有验证 `CallbackPtr` 是否就是刚注册的 `CaptureUpgradeMessage`，也没有验证 `CallbackObject` 仍为 `nullptr`。如果升级兼容回归变成“内部实际注册仍可工作，但 `GetMessageCallback` 返回了错误的函数指针/对象指针”，当前测试依然会通过。 |
| 影响 | 该用例名义上在验证 stock 2.38 `GetMessageCallback` ABI 兼容，但实际上只锁住了调用约定和触发结果，没有锁住 getter 最关键的“取回原始注册值”合同。后续一旦有代码依赖 getter 返回的 callback/object 做桥接、保存或恢复，当前套件无法提前报警。 |
| 修复建议 | 1）在 `SetMessageCallback(...)` 后构造期望 `asSFuncPtr ExpectedCallback = asFUNCTION(CaptureUpgradeMessage);`，再对白盒比较 `CallbackPtr` 与 `ExpectedCallback` 的二进制内容或平台兼容字段；2）补 `TestEqual(TEXT("GetMessageCallback should preserve the registered callback object"), CallbackObject, static_cast<void*>(nullptr))`，锁住 `asCALL_CDECL` 路径不应返回对象实例；3）保留现有 `WriteMessage(...)` 行为断言，把“getter 返回值正确”和“回调仍可触发”拆成两层证据。 |

#### Issue-119：`ImplementBasic` / `ImplementMultiple` 为纯元数据断言构造完整 actor world，helper 级别明显过重

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.ImplementBasic`、`Angelscript.TestModule.Interface.ImplementMultiple` |
| 行号范围 | 33-92, 94-173 |
| 问题描述 | 这两个用例最终只断言 `Actor->GetClass()->ImplementsInterface(...)`，没有调用任何接口方法，也没有依赖 `BeginPlay`、tick 或 world state；但实现上却都创建了 `FActorTestSpawner`、初始化 game subsystems 并实际 spawn actor。换言之，它们支付了完整 scenario/world fixture 的成本，却只做了 class metadata 检查。对于当前断言目标，`CompileScriptModule(...)` 拿到 `ScriptClass` 后直接对白盒检查 `ScriptClass->ImplementsInterface(...)` 就够了。 |
| 影响 | 过重 helper 会把本应是快速、稳定的元数据测试拖成依赖 world/subsystem 的 scenario 测试，增加执行时间和额外失败面；同时它也掩盖了当前用例实际上没有验证 runtime dispatch 这一事实，降低 Interface 套件的信号密度。 |
| 修复建议 | 1）若这两个用例短期仍只验证“实现标记”，直接去掉 `FActorTestSpawner` / `SpawnScriptActor(...)`，改为编译后对白盒断言 `ScriptClass->ImplementsInterface(InterfaceClass)`；2）把真正需要 world 和实例的行为断言留给新增的接口 dispatch/lifecycle 测试，例如 issue-20 已建议的 `TakeDamage/Heal` 调用路径；3）若保留 actor spawn，则必须同步补 runtime 方法调用与状态断言，避免继续用 scenario fixture 跑纯元数据检查。 |

### 二、需要新增的测试

#### NewTest-98：补 `asEP_AUTOMATIC_IMPORTS` 的跨模块符号解析行为回归，避免 `Upgrade.EngineProperties` 继续只停留在数值 round-trip

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` |
| 关联函数 | `asCBuilder::GetFunctionDescriptions`、`asCBuilder::GetGlobalProperty`、`asIScriptEngine::SetEngineProperty(asEP_AUTOMATIC_IMPORTS, ...)` |
| 现有测试覆盖 | `Upgrade.EngineProperties` 目前只验证 `asEP_AUTOMATIC_IMPORTS` 可经 `SetEngineProperty/GetEngineProperty` round-trip；仓库里没有任何正式测试直接证明该 property 会改变跨模块函数/全局查找行为。 |
| 风险评估 | 如果 selective migration 之后 `automaticImports` 只剩“值能写进去”，但 builder 不再从 `engine->allScriptGlobalFunctions` / `allScriptGlobalVariables` 解析其他模块符号，现有升级兼容套件仍会全绿；跨模块脚本会在真实项目里以“同一工程里明明有定义，却突然找不到符号”的形式晚暴露。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Upgrade.EngineProperties.AutomaticImportsCrossModuleLookup` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 场景描述 | 在同一 isolated engine 中先编译 source module，暴露一个全局函数和一个全局常量；随后在 consumer module 里不写 `import` 声明，直接调用该函数并读取该常量。分别在 `asEP_AUTOMATIC_IMPORTS = 0/1` 两种配置下编译 consumer，验证关闭时找不到跨模块符号、开启时可直接编译并执行。 |
| 输入/前置 | 使用 `CreateIsolatedCloneEngine()` 或 `ASTEST_CREATE_ENGINE_FULL()`。source 脚本可为 `const int SharedSeed = 40; int SharedValue() { return SharedSeed + 1; }`；consumer 脚本可为 `int Run() { return SharedValue() + SharedSeed; }`。先将 `ScriptEngine->SetEngineProperty(asEP_AUTOMATIC_IMPORTS, 0)`，编译 source 后再用 `CompileModuleWithSummary(...)` 编译 consumer，并记录 diagnostics；然后丢弃 consumer module，把 property 切回 `1`，重新编译同一 consumer 并执行 `Run()`。 |
| 期望行为 | 关闭 `automaticImports` 时，consumer 编译失败，`CompileResult == ECompileResult::Error`，diagnostics 命中 `SharedValue` 或 `SharedSeed` 未解析；开启后，同一 consumer 编译成功，执行 `Run()` 返回 `81`。这组断言直接证明 property 不只是“可读可写”，而是真的参与 builder 的跨模块符号查找。 |
| 使用的 Helper | `CreateIsolatedCloneEngine` / `ASTEST_CREATE_ENGINE_FULL` + `FAngelscriptEngineScope` + `CompileModuleWithSummary` + `Engine.DiscardModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-118 |
| WrongHelper | 1 | Issue-119 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 01:05)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-132`、`Issue-133`，保留在前文 `## 测试审查 (2026-04-10 01:01)` 小节；由于追加时命中了前文相同格式段落，正文没有落在真实 EOF，这里只补末尾锚点，不重复展开。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-110`，保留在前文 `## 测试审查 (2026-04-10 01:01)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-132 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 13:12)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-116`、`Issue-117`，保留在前文 `## 测试审查 (2026-04-09 13:11)` 小节；此处仅补文档末尾锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-97`，保留在前文 `## 测试审查 (2026-04-09 13:11)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-117 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 13:24)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-118`、`Issue-119`，保留在前文 `## 测试审查 (2026-04-09 13:23)` 小节；此处仅补文档末尾锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-98`，保留在前文 `## 测试审查 (2026-04-09 13:23)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-118 |
| WrongHelper | 1 | Issue-119 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 13:24)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-118`、`Issue-119`，保留在前文 `## 测试审查 (2026-04-09 13:23)` 小节；此处仅补文档末尾锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-98`，保留在前文 `## 测试审查 (2026-04-09 13:23)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-118 |
| WrongHelper | 1 | Issue-119 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 13:11)

### 一、现有测试问题

#### Issue-116：`NativeImplement` 用初始值等于期望值的属性去验证 getter，无法证明脚本侧 native interface getter 真被调度

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.NativeImplement` |
| 行号范围 | 121-205 |
| 问题描述 | 脚本类把 `NativeValue` 初始成 `123`，`BeginPlay()` 里再执行 `NativeValue = ParentRef.GetNativeValue();`，测试随后读取同一个属性并断言它仍为 `123`。由于被观察的属性在调用前后都是同一个值，这条断言无法区分“getter 经由 native interface 正确返回了 123”与“getter 根本没走到目标实现、但属性保持初始值 123”这两种情况。后面的 `Execute_GetNativeValue(Actor) == 123` 也复用了同一个常量值，仍然没有把 getter dispatch 与对象状态变化锁开。 |
| 影响 | 一旦 native interface getter 在脚本侧或 `Execute_` bridge 上退化成返回旧缓存、错误对象或默认值，而默认值刚好仍是 `123`，当前用例会继续绿灯，导致 `NativeImplement` 高估对 getter dispatch 的保护。 |
| 修复建议 | 1）把“getter 的返回源”和“测试观察值”拆成两个属性，例如 `BackingNativeValue = 123`、`GetterObservedValue = -1`，让 `GetNativeValue()` 返回前者，而 `BeginPlay()` 把 `ParentRef.GetNativeValue()` 写入后者；2）测试断言改成读取 `GetterObservedValue == 123`，避免继续拿初始化即为期望值的字段自证；3）对 C++ `Execute_GetNativeValue` 再补一次状态变化，例如先把 `BackingNativeValue` 改成 `321`，再断言 `Execute_GetNativeValue(Actor) == 321`，这样能直接锁住 bridge 读取的是当前实现而不是初始常量。 |

#### Issue-117：`GC.ManualCycleCollection` 只要求 tracked size “不增加”，允许回收后仍残留 GC 条目

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.GC.ManualCycleCollection` |
| 行号范围 | 361-372 |
| 问题描述 | 用例在创建 self-cycle 后记录 `BeforeRelease`，full collect 后只断言 `AfterCollect.CurrentSize <= BeforeRelease.CurrentSize`。由于 `BeforeRelease.CurrentSize` 在把 probe object 注册进 GC 后本来就应当大于 `0`，这条断言允许“full collect 后 `CurrentSize` 仍然等于 1”继续通过。`FGCProbeObject::LiveCount == 0` 只能证明对象析构了，不能证明 collector 的 tracked-entry 表已经真正清空。 |
| 影响 | 如果 GC 在销毁对象后仍残留 stale tracked entry、统计表没有回到空状态，当前测试不会报警；后续扫描、统计累计或同名 probe 再次进入 GC 时可能出现幽灵条目，而 `ManualCycleCollection` 给不出保护。 |
| 修复建议 | 1）把 `CurrentSize` 断言收紧为“回收到基线”，最直接的是在 `CreateSelfCycle()` 前再拍一个 baseline，然后断言 `AfterCollect.CurrentSize == Baseline.CurrentSize`；2）如果保留现有 `BeforeRelease` 快照，至少额外断言 `BeforeRelease.CurrentSize >= 1` 且 `AfterCollect.CurrentSize == 0`，明确要求 self-cycle 被完全移出 tracked set；3）可顺手补 `AfterCollect.NewObjects <= BeforeRelease.NewObjects` 或等价统计断言，避免只看析构计数、不看 collector 表本身。 |

### 二、需要新增的测试

#### NewTest-97：补 `GetModuleByFilenameOrModuleName` 的直接回归，锁住 filename 命中与 module-name fallback 两条查找路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::GetModuleByFilenameOrModuleName`、`FAngelscriptEngine::GetModuleByFilename` |
| 现有测试覆盖 | 当前 `LanguageFeatures` 只通过 `GetModuleByModuleName(...)` 间接验证模块查询，例如 `Execution.Discard`、`Core.CreateEngine` 与若干 builder/internal 用例；没有任何正式测试直接覆盖“按 filename 命中 active module”以及“filename 未命中时回退到 module name”这两条 lookup 合同。 |
| 风险评估 | 一旦 filename lookup、大小写匹配或 fallback 逻辑回归，依赖文件名定位模块的执行 helper、热重载和诊断路径会出现“模块明明已编译却查不到”的隐蔽失败，而现有套件只会在更高层报模糊红灯，无法把问题定位到 module lookup 层。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Core.ModuleLookup.FilenameThenModuleFallback` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 场景描述 | 编译一个 relative filename 与 module name 明显不同的最小模块，然后分别验证：1）给正确 absolute filename + 错误 module name 时，lookup 仍能按 filename 找到模块；2）给不存在的 filename + 正确 module name 时，会回退到 module-name 查找；3）两者都错误时返回空。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 isolated clone engine。模块名可设为 `ASCoreModuleLookupProbe`，文件名设为 `Lookup/ModuleLookup/FilenameFallback.as`，脚本为 `int Run() { return 42; }`。编译后构造 `AbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), RelativeFilename)`；然后调用 `Engine.GetModuleByFilenameOrModuleName(AbsoluteFilename, TEXT("DefinitelyWrongName"))`、`Engine.GetModuleByFilenameOrModuleName(TEXT("Z:/Missing/Module.as"), TEXT("ASCoreModuleLookupProbe"))` 与第三个“双 miss”查询。 |
| 期望行为 | 1）filename 命中查询返回有效 `FAngelscriptModuleDesc`，且其 `ModuleName == "ASCoreModuleLookupProbe"`；2）fallback 查询同样返回有效模块，并与第一条查询指向同一个 `ScriptModule`；3）双 miss 查询返回空；4）通过解析出的模块执行 `Run()` 仍返回 `42`，证明 lookup 拿到的是活动模块而不是 stale 记录。 |
| 使用的 Helper | `CompileModuleFromMemory` + `FAngelscriptEngine::GetModuleByFilenameOrModuleName` + `GetFunctionByDecl` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-117 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 11:55)

### 一、现有测试问题

---

## 测试审查 (2026-04-09 02:06)

### 一、现有测试问题

#### Issue-100：`Upgrade.CStringHash` 把“不同内容 hash 必不相等”当成合同，测试目标偏离真实兼容语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Upgrade.CStringHash` |
| 行号范围 | 280-291 |
| 问题描述 | 用例先断言 `AlphaBeta` / `alphabeta` 的 hash 相同，这部分能说明 case-insensitive canonicalization 仍在生效；但随后又用 `TestNotEqual(MixedHash, DifferentHash)` 把“不同字符串一定产生不同 hash”当成兼容合同。hash 本身并不保证 collision-free，这个断言锁住的是一对任意样例不碰撞，而不是 `asCString` 真正需要兼容的语义；同时它也没有验证 `Equals_CaseInsensitive()` 与 hash 行为是否保持一致。换言之，这个测试既可能因为合法的哈希实现变化而脆弱失败，也不能直接证明大小写无关比较合同仍成立。 |
| 影响 | 一旦未来为了迁移或性能替换 hash 实现，只要新实现对 `AlphaBeta` / `gamma` 恰好碰撞，这个用例就会误报红灯；反过来，即使 `Equals_CaseInsensitive()` 或 canonicalization 语义退化，只要这两个样例的 hash 恰好维持现状，测试仍可能给出误导性绿灯。 |
| 修复建议 | 1）保留“仅大小写不同的字符串 hash 必须一致”的正向断言；2）把“不同内容 hash 不等”替换为语义一致性断言，例如 `MixedCase.Equals_CaseInsensitive(LowerCase)` 为 `true`、`MixedCase.Equals_CaseInsensitive(DifferentValue)` 为 `false`，并明确说明 hash 与 case-insensitive equality 需要对齐；3）如果升级兼容目标确实要求固定当前 CRC 算法，则应改成对代表性输入断言一个已知 hash 常量，而不是用“不同字符串不碰撞”这种非合同性质来间接表达。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-100 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 11:17)

### 一、现有测试问题

#### Issue-101：`Compiler.TypeConversions` 在确认执行成功前就读取返回值，回归时会放大成不稳定失败

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Compiler.TypeConversions` |
| 行号范围 | 148-161 |
| 问题描述 | 用例在 `Context->Prepare(Function)` 之后立刻执行 `const int ExecuteResult = ...; const float Result = Context->GetReturnFloat();`，直到 `Context->Release()` 之后才断言 `PrepareResult == asSUCCESS` 和 `ExecuteResult == asEXECUTION_FINISHED`。这意味着一旦 prepare/execute 因真正回归而失败，测试仍会先从未完成执行的 context 里读返回槽，拿一个未定义或陈旧值去参与后续 `IsNearlyEqual(Result, 7.0f)` 断言。 |
| 影响 | 原本应该是“prepare/execute 失败”的单一红灯，会被放大成额外的浮点断言噪声，甚至在某些构建里表现为不稳定结果，降低 `Compiler.TypeConversions` 的定位价值。 |
| 修复建议 | 把返回值读取移到成功 guard 之后：1）先断言 `PrepareResult == asSUCCESS`、`ExecuteResult == asEXECUTION_FINISHED`，失败时立即 `Context->Release(); return false;`；2）只有在执行完成后再读 `GetReturnFloat()` 并做 `7.0f` 比较；3）若后续扩到 `float64` 路径，同样保持“先校验状态，再读返回槽”的顺序。 |

#### Issue-102：`CastSuccess` / `CastFail` 只看脚本侧 `Cast<>` 结果，没有把正负元数据路径锁死

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.CastSuccess`、`Angelscript.TestModule.Interface.CastFail` |
| 行号范围 | 44-99, 116-168 |
| 问题描述 | `CastSuccess` 只通过脚本里的 `Cast<UIDamageableCastOk>(Self)` 把 `CastSucceeded` 置为 `1`，`CastFail` 只通过 `Cast<UIDamageableCastFail>(Self) == nullptr` 把 `CastReturnedNull` 置为 `1`。两个用例都没有显式获取接口类并验证 `ScriptClass->ImplementsInterface(...)` 的正/负元数据结果，因此它们实际上只观察了脚本侧 cast 表象，没有把“实现类应带接口元数据 / 非实现类绝不能带接口元数据”这两条基础合同锁住。 |
| 影响 | 如果 `UInterface` 元数据登记与脚本 cast 路径发生偏差，例如实现类漏登记接口但脚本 cast 仍偶然成功，或非实现类错误带上接口标记而脚本路径因其他原因返回空，这两个测试都难以直接指出根因，削弱了 Interface 套件对完整生命周期的定位能力。 |
| 修复建议 | 1）在 `CastSuccess` 里补 `FindGeneratedClass(&Engine, TEXT("UIDamageableCastOk"))`，同时断言 `ScriptClass->ImplementsInterface(InterfaceClass)` 和 `Actor->GetClass()->ImplementsInterface(InterfaceClass)` 都为 `true`；2）在 `CastFail` 里同样找到 `UIDamageableCastFail`，显式断言 `ScriptClass->ImplementsInterface(InterfaceClass)` 与 `Actor->GetClass()->ImplementsInterface(InterfaceClass)` 都为 `false`；3）保留现有 `CastSucceeded` / `CastReturnedNull` 断言，把“元数据正确”和“脚本 cast 结果正确”拆成两层证据。 |

### 二、需要新增的测试

#### NewTest-78：补 `FAngelscriptTypeUsage::EqualsUnqualified` 的 qualifier 矩阵，直达当前无测试的 runtime wrapper 等价逻辑

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptTypeUsage::EqualsUnqualified` |
| 现有测试覆盖 | `DataType.Comparisons` 只验证底层 `asCDataType::IsEqualExceptConst/IsEqualExceptRef`；`NewTest-42` 只建议补 `FromParam/FromReturn` 的 qualifier 映射。当前没有任何测试直接锁住 runtime wrapper `FAngelscriptTypeUsage` 在忽略 `const/ref` 时的相等语义。 |
| 风险评估 | 如果 `EqualsUnqualified` 开始错误地区分 `const int&in` 与 `int&out`，或者把不同基础类型误判为相等，依赖 runtime wrapper 比较的绑定、参数匹配和调试器逻辑会悄悄漂移，而现有 LanguageFeatures 套件不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeUsage.EqualsUnqualifiedQualifierMatrix` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeUsageTests.cpp` |
| 场景描述 | 编译一个最小模块，同时声明 `const int&in`、`int&out` 和 `float` 参数；通过 `FAngelscriptTypeUsage::FromParam` 拿到三个 wrapper usage，直接比较 `EqualsUnqualified()` 与 `operator==` 的差异。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `BuildModule`。脚本建议为：`void Compare(const int&in Input, int&out Output, float Number) { Output = Input; }`。获取 `void Compare(const int&in, int&out, float)` 的 `asIScriptFunction*` 后，对参数 0/1/2 分别调用 `FAngelscriptTypeUsage::FromParam`。 |
| 期望行为 | 参数 0 与参数 1 的 `EqualsUnqualified()` 返回 `true`，但 `operator==` 返回 `false`；参数 0 与参数 2 的 `EqualsUnqualified()` 返回 `false`。这组断言直接证明 wrapper 会忽略 top-level `const/ref`，但不会把 `int` 和 `float` 混为一类。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `FAngelscriptTypeUsage::FromParam` |
| 优先级 | P1 |

#### NewTest-79：补 `GetByClass` / `GetByData` 的反向查表回归，覆盖当前无测试的 type registry 入口

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptType::GetByClass`、`FAngelscriptType::GetByData` |
| 现有测试覆盖 | 当前只有 `Types.FloatDebuggerFormatting` 直接调用 `GetByAngelscriptTypeName("float")`；`NewTest-12` 只建议补 alias/property finder。还没有任何测试对白盒验证反向索引 `TypesByClass` / `TypesByData` 是否能稳定回到同一个注册 type。 |
| 风险评估 | 一旦 type registry 的反向查表表项丢失或指向错误对象，按 `UClass*` / `UScriptStruct*` 做调试器、属性桥接和默认值转换时会得到空 type 或错 type；名称查表仍然全绿，问题很难定位。 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeRegistry.ClassAndDataLookup` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeRegistryTests.cpp` |
| 场景描述 | 以已绑定的 `AActor` 与 `FIntPoint` 为基线，先用名称查表拿到 baseline type，再分别通过 `UClass*` 和 `UScriptStruct*` 做反向查表，确认返回的是同一个注册对象。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；先调用 `FAngelscriptType::GetByAngelscriptTypeName(TEXT("AActor"))` 与 `FAngelscriptType::GetByAngelscriptTypeName(TEXT("FIntPoint"))`，再调用 `FAngelscriptType::GetByClass(AActor::StaticClass())` 和 `FAngelscriptType::GetByData(TBaseStructure<FIntPoint>::Get())`。额外补 `GetByClass(nullptr)` 与 `GetByData(nullptr)` 的 guard 断言。 |
| 期望行为 | `AActor` 的名称查表与 `GetByClass(AActor::StaticClass())` 返回同一个 `TSharedPtr`；`FIntPoint` 的名称查表与 `GetByData(TBaseStructure<FIntPoint>::Get())` 返回同一个 `TSharedPtr`；`nullptr` 输入都返回空指针。这样可以直接锁住 type registry 的两条反向索引路径。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `FAngelscriptType::GetByAngelscriptTypeName` + `FAngelscriptType::GetByClass` + `FAngelscriptType::GetByData` + `TBaseStructure<FIntPoint>::Get()` |
| 优先级 | P2 |

#### NewTest-80：补单参数执行的负数与零值编组，避免 `Execution.OneArg` 继续只覆盖正数 happy path

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::SetArgDWord`、`asIScriptContext::GetReturnDWord` |
| 现有测试覆盖 | `Execution.OneArg` 只设置一次正数 `21` 并断言结果 `42`；当前没有任何执行层用例覆盖 `0` 或负数通过 `SetArgDWord` 进入脚本后的符号保持。 |
| 风险评估 | 如果参数编组在 `DWord -> int` 的符号解释上回归，或者 `0` 值在 wrapper/调用约定里被错误当成“未设置”，现有 `ExecutionTests` 仍会全绿；问题只会在更复杂的 gameplay 脚本里晚暴露。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.OneArg.NegativeAndZero` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionArgumentMarshallingTests.cpp` 或追加到现有 `AngelscriptExecutionTests.cpp` |
| 场景描述 | 编译最小脚本 `int Test(int Value) { return Value * 2; }`，用同一个入口分别喂入 `0` 和 `-21` 两组参数，直接验证执行层参数写入与返回读取都能保持有符号整数语义。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule` + `GetFunctionByDecl("int Test(int)")` + `Engine.CreateContext()`。第一次 `Prepare()` 后调用 `SetArgDWord(0, 0)`，第二次 `Unprepare()` / 重新 `Prepare()` 后调用 `SetArgDWord(0, static_cast<asDWORD>(-21))`。 |
| 期望行为 | 第一次执行返回 `0`；第二次执行返回 `-42`。两次都应得到 `asEXECUTION_FINISHED`，且第二次结果必须证明 `SetArgDWord` 到 `int` 形参的符号位没有在 marshalling 过程中被破坏。 |
| 使用的 Helper | `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `Context->SetArgDWord` + `Context->Unprepare` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-101 |
| WeakAssertion | 1 | Issue-102 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingEdgeCase: 1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:52)

### 一、现有测试问题

无新增。

### 二、需要新增的测试

#### NewTest-86：补 `ttMultilineStringConstant` / `ttHeredocStringConstant` 分类，避免特殊字符串字面量继续无白盒覆盖

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_tokenizer.cpp` |
| 关联函数 | `asCTokenizer::GetToken` |
| 现有测试覆盖 | `Tokenizer.CommentsAndStrings` 只验证普通字符串 `"first\\nsecond"` 与 comment token；当前没有任何用例直达 `ttMultilineStringConstant` 或 `ttHeredocStringConstant` 这两条专门分支 |
| 风险评估 | 一旦 tokenizer 在带真实换行的字符串或 heredoc `\"\"\"` 语法上回归，脚本侧的多行文本、代码生成片段和文档字符串会先出问题，而现有 tokenizer 套件仍可能保持绿灯 |
| 建议测试名 | `Angelscript.TestModule.Internals.Tokenizer.MultilineAndHeredocStrings` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTokenizerTests.cpp` |
| 场景描述 | 复用现有 `FTokenizerAccessor`，分别喂入一个带真实换行的普通字符串字面量和一个 triple-quote heredoc 字面量，显式断言 token 类型与返回长度 |
| 输入/前置 | 使用 `FTokenizerAccessor Tokenizer; size_t TokenLength = 0;`。普通 multiline 样本建议为包含实际换行的 `"first` 换行 `second"`；heredoc 样本建议为 `\"\"\"alpha` 换行 `beta\"\"\"`。 |
| 期望行为 | 带真实换行的普通字符串应返回 `ttMultilineStringConstant`，长度覆盖整个 closing quote；triple-quote 样本应返回 `ttHeredocStringConstant`，长度覆盖完整 `\"\"\" ... \"\"\"` 区间。这样可以直接锁住 tokenizer 在 2 条特殊字符串分支上的分类规则。 |
| 使用的 Helper | 现有 `FTokenizerAccessor` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 11:52)

### 一、现有测试问题

无新增。

### 二、需要新增的测试

#### NewTest-87：补 `SetArgDouble` / `GetReturnDouble` 的 direct round-trip，避免 `double` API 继续只被 `QWord` 路径间接覆盖

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::SetArgDouble`、`asIScriptContext::GetReturnDouble` |
| 现有测试覆盖 | `Execution.MixedArgs` 只在 `float64` 模式下通过 `SetArgQWord` / `GetReturnQWord` + `FMemory::Memcpy` 间接验证 `double` 参数与返回值；当前没有任何用例直接调用 `SetArgDouble` 或 `GetReturnDouble` |
| 风险评估 | 如果 dedicated `double` API 在 wrapper 层把值截成 `float`、写错参数槽位，或者返回槽读取路径与 `QWord` 路径不一致，现有执行套件仍可能全绿；这类回归会直接影响原生 C++ 侧调用 `double` 脚本函数的 ABI 稳定性 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.DoubleArg.DirectApiRoundTrip` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionArgumentMarshallingTests.cpp` 或追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 场景描述 | 编译一个最小 `double` 入口，从 C++ 侧直接用 `SetArgDouble` 写入非整数值，再用 `GetReturnDouble` 读回结果，验证 dedicated `double` 参数与返回值 API 能独立工作，不依赖 `QWord` 位拷贝绕行 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE()` + `BuildModule` + `GetFunctionByDecl("double Test(double)")` + `Engine.CreateContext()`。先确认 `Engine.GetScriptEngine()->GetEngineProperty(asEP_ALLOW_DOUBLE_TYPE) != 0`。脚本建议为 `double Test(double Value) { return Value * 1.5 + 0.25; }`，C++ 侧输入 `20.5`，然后 `Context->SetArgDouble(0, 20.5)`。 |
| 期望行为 | `Prepare/Execute` 都返回成功，`Context->GetReturnDouble()` 返回 `31.0`（允许很小容差，例如 `0.0001`）。这组断言直接锁住 dedicated `double` marshalling，而不是继续依赖 `SetArgQWord` / `GetReturnQWord` 的旁路覆盖。 |
| 使用的 Helper | `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `Context->SetArgDouble` + `Context->GetReturnDouble` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:20)

### 一、现有测试问题

#### Issue-107：`Upgrade.HeaderCompatibility` / `Upgrade.EngineProperties` 用 `&&` 串联大段断言，首个失败后剩余兼容位点会被静默跳过

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Upgrade.HeaderCompatibility`、`Angelscript.TestModule.Angelscript.Upgrade.EngineProperties` |
| 行号范围 | 56-102, 137-201 |
| 问题描述 | `HeaderCompatibility` 把 version / property id / object flag 三大矩阵都写成 `TestEqual(...) && TestEqual(...) && ...`，`EngineProperties` 也把 property 兼容断言串成一个长 `&&` 链。这样只要前一项失败，后面的 `TestEqual` 就不会执行，整组测试会退化成“只报告第一个差异”。对升级兼容这种本来就需要一次性暴露多处漂移的套件来说，这会直接吞掉后续不匹配位点，削弱定位效率。 |
| 影响 | 一旦 2.38 selective migration 或 APV2 兼容层同时漂移多个常量/属性，本文件只能报出首个不一致项；其余兼容破口会被短路隐藏，导致一次回归要靠多轮修改和重跑才能看全差异。 |
| 修复建议 | 1）不要再用 `&&` 连接大批 `TestEqual`；改成 `bool bAllPassed = true; bAllPassed &= TestEqual(...);` 的逐条累积模式，确保每个兼容位点都执行；2）把 `HeaderCompatibility` 拆成 `Version`、`PropertyIds`、`ObjectFlags` 三段局部 helper，每段内部也保持逐条累积；3）`EngineProperties` 的 round-trip matrix 同样改成逐项累积，并在最终返回统一的 `bAllPassed`，这样一次失败能完整暴露所有漂移项。 |

#### Issue-108：`RegisterGCProbeType()` 用短路式行为注册，把 GC probe 绑定失败压成单个布尔值

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.GC.ManualCycleCollection`、`Angelscript.TestModule.Internals.GC.CycleDetection` |
| 行号范围 | 214-224 |
| 问题描述 | `RegisterGCProbeType()` 把 `ADDREF/RELEASE/GETREFCOUNT/SETGCFLAG/GETGCFLAG/ENUMREFS/RELEASEREFS` 七个 `RegisterObjectBehaviour(...)` 调用串成一个 `A && B && C ...` 布尔表达式。这样首个注册失败后，后续 behaviour 根本不会尝试注册，测试输出里也只剩一句“GC probe object should register all GC behaviours”。这既隐藏了具体是哪个 behaviour 坏了，也可能把 probe type 留在半注册状态。 |
| 影响 | 当 GC 行为绑定在 selective migration、SDK 差异或注册顺序上出现多点回归时，当前 helper 只能报一个模糊失败；更糟的是，后续未执行的注册步骤不会补齐 probe type，可能让同轮调试看到的是“半配置 GC object”，进一步降低 `GC.ManualCycleCollection` / `GC.CycleDetection` 的定位质量。 |
| 修复建议 | 1）把七个 behaviour 注册拆成逐条 `TestTrue` / `TestEqual`，单独输出 declaration 名称；2）即使前一条失败，也继续尝试后续注册，收集完整缺口；3）注册结束后额外检查 probe type 是否具备 GC 所需的完整 behaviour 集，再决定是否进入 cycle test，避免在半注册状态下继续执行。 |

### 二、需要新增的测试

#### NewTest-88：补递归 nested execution，避免 `Execution.Nested` 继续只覆盖单层 `Outer -> Inner` happy path

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::Execute` |
| 现有测试覆盖 | `Execution.Nested` 只编译 `Outer(Value) { return Inner(Value) + 1; }` / `Inner(Value) { return Value * 2; }`，最终只断言一次 `41`；当前没有任何 `ExecutionTests` 用例覆盖递归、多层 frame 叠加或局部变量在深层调用链中的隔离。 |
| 风险评估 | 如果 runtime 在深层 call chain 上错误复用返回槽、污染上层局部变量，或者 nested frame 清理只在单层调用下成立，现有 `Execution.Nested` 仍会绿灯；真正的脚本递归/树遍历场景会更晚才暴露问题。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.Nested.RecursiveFrameIsolation` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionNestedCallTests.cpp` |
| 场景描述 | 编译一个最小递归入口，用每层 frame 的局部变量把递归链编码到最终结果里，直接验证多层 nested call 不会互相覆盖局部状态。 |
| 输入/前置 | 使用 `ASTEST_COMPILE_RUN_INT`；脚本建议为 `int Encode(int Value) { if (Value == 0) return 0; int Local = Value; return Local + Encode(Value - 1) * 10; } int Run() { return Encode(4); }`。 |
| 期望行为 | `Run()` 返回 `1234`。这个结果要求每层 frame 都保留自己的 `Local`，并按正确的返回顺序组合结果；任何 frame-local 污染、返回槽复用错误或深层 nested dispatch 异常都会把编码打乱。 |
| 使用的 Helper | `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-89：补 `asEP_INIT_CALL_STACK_SIZE` / `asEP_MAX_CALL_STACK_SIZE` 的执行级行为，避免 `Upgrade.EngineProperties` 继续停留在 getter/setter round-trip

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptEngine::SetEngineProperty`、`asIScriptContext::Execute`、`asIScriptContext::GetExceptionString` |
| 现有测试覆盖 | `Upgrade.EngineProperties` 只验证 property 值可 round-trip，`Execution.Nested` 也只跑默认配置下的单层 nested call；当前目标目录里没有任何测试证明 `asEP_INIT_CALL_STACK_SIZE`、`asEP_MAX_CALL_STACK_SIZE`、`asEP_MAX_NESTED_CALLS` 会真实影响执行结果。 |
| 风险评估 | 如果这些 property 只是“能读能写”但没有接上 runtime enforcement，升级兼容套件仍会全绿；递归脚本在真实项目里会以 execution exception 或 silent corruption 的形式晚暴露。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Upgrade.EngineProperties.CallStackLimitOverflow` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 场景描述 | 在 isolated engine 上把 call-stack 相关 property 压到极小值，编译一个简单递归脚本并执行，验证 runtime 会按 property 配置抛出 execution exception，而不是继续正常跑完。 |
| 输入/前置 | 使用 `AngelscriptTestSupport::CreateIsolatedCloneEngine()` + `FAngelscriptEngineScope` + `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()`。脚本建议为 `void Recursive(int Depth) { if (Depth > 0) { Recursive(Depth - 1); } } void Run() { Recursive(8); }`。执行前设置 `asEP_INIT_CALL_STACK_SIZE = 1`、`asEP_MAX_CALL_STACK_SIZE = 1`、`asEP_MAX_NESTED_CALLS = 1`。 |
| 期望行为 | `Prepare()` 成功，`Execute()` 返回 `asEXECUTION_EXCEPTION`，且 `Context->GetExceptionString()` 非空。若当前分支的异常文本稳定，可进一步断言消息包含 `stack` 或 `nested` 关键词。 |
| 使用的 Helper | `AngelscriptTestSupport::CreateIsolatedCloneEngine` + `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `SetEngineProperty` |
| 优先级 | P1 |

#### NewTest-90：补 formal automation 对 exception callstack API 的直接覆盖，避免 `GetCallstackSize()` 继续只停留在 Learning trace

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` |
| 关联函数 | `asIScriptContext::GetCallstackSize`、`asIScriptContext::GetFunction`、`asIScriptContext::GetExceptionString` |
| 现有测试覆盖 | 当前 LanguageFeatures 正式套件里没有任何用例直接断言 exception 后的 callstack introspection；仓库只在 `Plugins/Angelscript/Source/AngelscriptTest/Learning/Native/AngelscriptLearningDebuggerContextTraceTests.cpp` 里有学习型 trace 示例，不属于当前目标目录的正式回归保护。 |
| 风险评估 | 如果 exception 上下文里的 stack frame 收集、函数回溯或异常字符串暴露路径回归，`AngelscriptEngine.cpp` 里的 debugger/context 诊断逻辑会失真，而 LanguageFeatures automation 仍不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Execute.Context.ExceptionCallstackInspection` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionNestedCallTests.cpp` |
| 场景描述 | 编译一个会在深层函数里触发 runtime exception 的最小脚本，执行后直接遍历 `Context->GetCallstackSize()` 和 `Context->GetFunction(StackLevel)`，验证异常上下文里至少保留了 inner / middle / entry 三层函数帧。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `BuildModule` + `GetFunctionByDecl("int Entry()")` + `Engine.CreateContext()`。脚本可复用 learning trace 的形状：`void FailInner(int Value) { int Inner = Value * 2; int Zero = 0; int Crash = Inner / Zero; } void TriggerFailure(int Seed) { int Local = Seed + 1; FailInner(Local); } int Entry() { TriggerFailure(20); return 0; }`。 |
| 期望行为 | `Execute()` 返回 `asEXECUTION_EXCEPTION`；`Context->GetExceptionString()` 非空；`Context->GetCallstackSize() >= 2`；遍历 stack frame 时至少能找到 `FailInner`、`TriggerFailure`、`Entry` 这三个函数名中的核心链路节点。这样可以直接锁住异常回溯 API，而不是只让 learning trace 打日志。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `GetFunctionByDecl` + `Engine.CreateContext()` + `GetCallstackSize`/`GetFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 2 | Issue-107 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 3 |

---

## 测试审查 (2026-04-09 12:20)

### 一、现有测试问题

#### Issue-109：`Execution.Basic` 的 `void` 路径只有状态机断言，没有任何可观察副作用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Execute.Basic` |
| 行号范围 | 14-59 |
| 问题描述 | 用例把 `void TestVoid() {}` 和 `int TestValue() { return 42; }` 放在同一个模块里，但对 `void` 路径只断言 `Prepare()` / `Execute()` 返回成功。由于 `TestVoid()` 本身没有任何状态写回，这一半测试实际上只证明“空函数能走完状态机”，并不能证明 `void` entry 真正执行了脚本逻辑，也无法发现 `void` 调度只返回 `asEXECUTION_FINISHED` 却没有触发副作用的回归。 |
| 影响 | 如果 `void` 函数 dispatch、全局写回或 `UASFunction_NoParams::RuntimeCallEvent` 一类 no-param/no-return 路径退化，而返回值函数仍然正常，当前 `Execution.Basic` 仍会绿灯，执行层对最基础的 `void` 入口保护明显不足。 |
| 修复建议 | 1）把 `TestVoid()` 改成可观察副作用，例如写全局 `g_Value = 7` 或写模块级静态状态；2）在执行 `void` 入口后，再调用一个读取函数或直接读取可观察状态，显式断言副作用生效；3）保留现有 `Prepare/Execute` 返回码断言，但不要再让空函数承担“`void` 执行语义已验证”的职责。 |

### 二、需要新增的测试

#### NewTest-91：补脚本定义 `UInterface` 的 C++ 反射调用回归，直接锁住 `ProcessEvent -> RuntimeCallEvent` 桥接

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunction_FloatArg::RuntimeCallEvent`、`UASFunction_DWordReturn::RuntimeCallEvent` |
| 现有测试覆盖 | `Interface.CppInterface` 只验证类仍带 `ImplementsInterface(...)` 元数据；`Interface.MethodCall` 只覆盖脚本侧 `Cast<Interface>` 后的调用。当前没有任何正式回归测试直接从 C++/UE reflection 侧调用“脚本定义的 interface 方法”并验证参数与返回值都正确进入脚本实现。 |
| 风险评估 | 如果 script-defined interface 的 `ProcessEvent` 桥接、参数封送或返回值回填在 `UASFunction_*::RuntimeCallEvent` 路径上回归，脚本接口对 C++/UE 侧调用者会直接失效，而现有 Interface 套件仍可能全绿。 |
| 建议测试名 | `Angelscript.TestModule.Interface.CppInterface.ProcessEventBridgeRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCppBridgeTests.cpp` |
| 场景描述 | 编译一个脚本定义的 `UINTERFACE()`，其中同时包含 `void TakeDamage(float Amount)` 和 `int GetDamageLevel()`；实现类把 `TakeDamage` 的实参写入属性，并让 `GetDamageLevel()` 返回固定值。C++ 侧直接通过反射 `ProcessEvent` / 现有 generated-function helper 调这两个入口，验证参数写入与返回值回填都命中脚本实现。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileScriptModule` + `FActorTestSpawner`。脚本建议为：`UICppBridgeDamageable` 声明 `void TakeDamage(float Amount); int GetDamageLevel();`，`AScenarioInterfaceCppBridgeActor : AActor, UICppBridgeDamageable` 带 `UPROPERTY() float LastDamage = -1.0;`，`TakeDamage` 把 `LastDamage = Amount;`，`GetDamageLevel()` 返回 `7`。C++ 侧用 `FindGeneratedFunction(ScriptClass, TEXT("TakeDamage"))` 和 `FindGeneratedFunction(ScriptClass, TEXT("GetDamageLevel"))` 拿到 `UFunction*`；对前者构造 `float Amount = 42.5f` 的参数结构并 `Actor->ProcessEvent(...)`，对后者用 `ExecuteGeneratedIntEventOnGameThread(&Engine, Actor, Function, Result)`。 |
| 期望行为 | `TakeDamage` 反射调用后，`LastDamage` 以容差断言等于 `42.5f`；`GetDamageLevel()` 反射调用返回 `7`。这组断言共同证明脚本定义 `UInterface` 不只是“类标记存在”，而是 C++ 侧真的能通过 UE reflection 进入脚本实现并拿到正确结果。 |
| 使用的 Helper | `CompileScriptModule` + `FActorTestSpawner` + `FindGeneratedFunction` + `Actor->ProcessEvent` + `ExecuteGeneratedIntEventOnGameThread` + `ReadPropertyValue<FFloatProperty>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-109 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
---

## 测试审查 (2026-04-09 12:25)

### 一、现有测试问题

#### Issue-110：Upgrade.MessageCallback / Upgrade.RegisterObjectTypeFlags 在返回语句里继续使用 && 短路断言

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp |
| 测试名 | Angelscript.TestModule.Angelscript.Upgrade.MessageCallback、Angelscript.TestModule.Angelscript.Upgrade.RegisterObjectTypeFlags |
| 行号范围 | 204-245, 248-277 |
| 问题描述 | 两个用例在 ASTEST_END_SHARE 之后直接 eturn TestTrue(...) && TestEqual(...) ...。Upgrade.MessageCallback 先检查回调是否触发，再串联消息文本和消息类型；Upgrade.RegisterObjectTypeFlags 先检查 sOBJ_EDITOR_ONLY，再检查它没有别名到 sOBJ_APP_CLASS_MORE_CONSTRUCTORS。一旦第一个断言失败，后面的兼容位点就不会执行，报告只剩首个差异。 |
| 影响 | 这会让消息回调 ABI 与 object flag 迁移类问题退化成“单点报错”，一次回归无法完整暴露所有失配位点；排查时需要重复改动和重跑，降低升级兼容套件的诊断价值。 |
| 修复建议 | 1）改成 ool bAllPassed = true; bAllPassed &= TestTrue(...); bAllPassed &= TestEqual(...); 的逐项累积模式；2）Upgrade.MessageCallback 额外把 callback invoked、message text、message type 三项都执行完后再统一返回；3）Upgrade.RegisterObjectTypeFlags 同样保留两个 flag 检查都落地，避免高位 flag 丢失时把位别名问题一起短路掉。 |

#### Issue-110-更正：`Upgrade.MessageCallback` / `Upgrade.RegisterObjectTypeFlags` 在返回语句里继续使用 `&&` 短路断言

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Upgrade.MessageCallback`、`Angelscript.TestModule.Angelscript.Upgrade.RegisterObjectTypeFlags` |
| 行号范围 | 204-245, 248-277 |
| 问题描述 | 两个用例在 `ASTEST_END_SHARE` 之后直接 `return TestTrue(...) && TestEqual(...) ...`。`Upgrade.MessageCallback` 先检查回调是否触发，再串联消息文本和消息类型；`Upgrade.RegisterObjectTypeFlags` 先检查 `asOBJ_EDITOR_ONLY`，再检查它没有别名到 `asOBJ_APP_CLASS_MORE_CONSTRUCTORS`。一旦第一个断言失败，后面的兼容位点就不会执行，报告只剩首个差异。 |
| 影响 | 这会让消息回调 ABI 与 object flag 迁移类问题退化成“单点报错”，一次回归无法完整暴露所有失配位点；排查时需要重复改动和重跑，降低升级兼容套件的诊断价值。 |
| 修复建议 | 1）改成 `bool bAllPassed = true; bAllPassed &= TestTrue(...); bAllPassed &= TestEqual(...);` 的逐项累积模式；2）`Upgrade.MessageCallback` 额外把 callback invoked、message text、message type 三项都执行完后再统一返回；3）`Upgrade.RegisterObjectTypeFlags` 同样保留两个 flag 检查都落地，避免高位 flag 丢失时把位别名问题一起短路掉。 |
### 二、需要新增的测试

#### NewTest-92：补 `ScriptStruct` 相等性与哈希桥接回归，直接覆盖 `FASStructOps::Identical` / `GetStructTypeHash`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` |
| 关联函数 | `FASStructOps::Identical`、`FASStructOps::GetStructTypeHash`、`UASStruct::UpdateScriptType` |
| 现有测试覆盖 | `Internals.StructCppOps.NotBlueprintTypeByDefault` 只检查 `BlueprintType` metadata，当前正式套件没有任何用例验证脚本 struct 上声明的 `bool opEquals(...) const` 与 `uint GetTypeHash() const` 会真实映射到 Unreal 的 native struct ops，也没有断言 `STRUCT_IdenticalNative` / `CPF_HasGetValueTypeHash` 对外可观察。 |
| 风险评估 | 如果 `UpdateScriptType()` 没有正确挂上 `EqualsFunction` / `HashFunction`，或者 `Identical` / `GetStructTypeHash` 在 `PrepareAngelscriptContextWithLog`、参数封送、返回值读取上退化，脚本 struct 参与属性比较、容器哈希或重复值去重时会悄悄失效，而当前 `LanguageFeatures` 套件完全不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Internals.StructCppOps.EqualityAndHashBridge` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptStructCppOpsTests.cpp` |
| 场景描述 | 编译一个脚本 struct，例如 `FHashablePoint`，包含 `int X`、`int Y`、`bool opEquals(const FHashablePoint& Other) const { return X == Other.X && Y == Other.Y; }`、`uint GetTypeHash() const { return uint(X * 31 + Y); }`。随后通过 backing `UScriptStruct` 创建三份实例：A=(1,2)、B=(1,2)、C=(3,4)，分别验证 struct compare 与 value hash 行为。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + 现有 `BuildScriptStruct()` helper。建议补一个小 helper：`FindStructPropertyChecked<FIntProperty>(Struct, TEXT("X"/"Y"))` 写入字段；比较时优先调用 `UScriptStruct::CompareScriptStruct(A, B, 0)` / `CompareScriptStruct(A, C, 0)`，哈希时调用 `Struct->GetStructTypeHash(&Value)` 或等价公开入口。 |
| 期望行为 | 1）`Struct->GetCppStructOps()` 非空；2）A 与 B 比较为 true，A 与 C 比较为 false；3）A 与 B 的 hash 相同且非 0，C 的 hash 与 A/B 不同；4）如当前分支可直接观察 flags，再补断言 `Struct` 带 `STRUCT_IdenticalNative`，对应 property/ops 暴露 `HasGetTypeHash` 能力。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildScriptStruct` + `GetCppStructOps` + `UScriptStruct::CompareScriptStruct` + `GetStructTypeHash` + `FStructOnScope` 或等价栈上 struct 存储 helper |
| 优先级 | P1 |

#### NewTest-93：补脚本定义 `UInterface` 的 ref/out 参数回写回归，直接覆盖 `UASFunction_ReferenceArg::RuntimeCallEvent`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunction_ReferenceArg::RuntimeCallEvent`、`UASFunction_ReferenceArg_JIT::RuntimeCallEvent` |
| 现有测试覆盖 | `NewTest-91` 只建议覆盖脚本定义 `UInterface` 的 value arg + value return 反射桥接；当前正式套件与现有建议都没有锁住 ref/out 参数经 `ProcessEvent` 进入脚本实现后再写回 caller buffer 的路径。 |
| 风险评估 | `UASFunction_ReferenceArg` 直接把整块 `Parms` buffer 传给脚本调用层；一旦 reference/out 参数的偏移、别名、JIT/non-JIT 写回逻辑回归，C++/UE 侧通过 interface 调脚本时会出现“调用成功但参数没被改回”的静默错误，而现有 Interface 回归无法发现。 |
| 建议测试名 | `Angelscript.TestModule.Interface.CppInterface.ProcessEventReferenceArgRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCppBridgeTests.cpp` |
| 场景描述 | 编译一个脚本定义的 `UINTERFACE()`，声明 `void AdjustValue(int& Value);`，实现类把入参在脚本里做可观察修改，例如 `Value += 7; LastAdjusted = Value;`。C++ 侧通过 `FindGeneratedFunction` + `ProcessEvent` 调用该 interface 方法，验证 caller 传入的参数结构被原地写回，同时脚本实例属性也记录到修改后的值。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileScriptModule` + `FActorTestSpawner` + `FindGeneratedFunction`。脚本可定义 `UICppRefBridge` 与 `AScenarioInterfaceRefBridgeActor : AActor, UICppRefBridge`，带 `UPROPERTY() int LastAdjusted = -1;`。C++ 侧自定义最小参数结构 `struct FAdjustValueParms { int32 Value; };`，初始化为 `5` 后执行 `Actor->ProcessEvent(Function, &Parms)`。如测试环境支持 JIT 切换，建议在 helper 中复跑一次 JIT 开/关两种配置。 |
| 期望行为 | `ProcessEvent` 返回后 `Parms.Value == 12`，并且 `LastAdjusted == 12`。若能直接拿到 `FProperty`，再补充断言该参数带 `CPF_ReferenceParm` / `CPF_OutParm`，避免测试只证明脚本逻辑而没有锁住桥接形态。 |
| 使用的 Helper | `CompileScriptModule` + `FActorTestSpawner` + `FindGeneratedFunction` + `Actor->ProcessEvent` + `ReadPropertyValue<FIntProperty>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-110-更正 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-10 00:10)

### 一、现有测试问题

#### Issue-128：`Types.Int8` 只覆盖正数提升 happy path，测试名明显高估 `int8` 语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.Int8` |
| 行号范围 | 207-225 |
| 问题描述 | 用例脚本只有 `int8 A = 100; int8 B = 50; return int(A + B);`，最终只断言 `150`。这只能证明“两个正数 `int8` 相加后还能提升回 `int`”，却没有覆盖 `int8` 最容易出问题的负数字面量绑定、`-128/127` 边界值、符号扩展和比较语义。测试名却是宽泛的 `Types.Int8`，会让后续审查误以为 8-bit 整数语义已有完整保护。 |
| 影响 | 如果 `int8` 在脚本编译、寄存器传递或提升回 `int` 时丢失符号位，当前用例仍可能继续绿灯；尤其负数与边界值相关的回归会被完全漏掉。 |
| 修复建议 | 1）保留当前正数 smoke path 时，把测试名收窄为 `Int8.PositivePromotion`；2）更推荐直接把脚本改成多位编码断言，同时覆盖 `-1`、`-128`、`127` 三个值，例如返回 `111` 表示三条检查全通过；3）若担心单用例过载，可拆成 `PositivePromotion` 与 `SignAndBounds` 两个独立测试名。 |

### 二、需要新增的测试

#### NewTest-106：补 `FAngelscriptTypeUsage::FromClass` / `FromStruct` 的 native 与 script 映射回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptTypeUsage::FromClass`、`FAngelscriptTypeUsage::FromStruct`、`FAngelscriptTypeUsage::GetClass` |
| 现有测试覆盖 | 当前 `LanguageFeatures` 文档里的 `TypeUsage` 建议集中在 `FromTypeId`、`FromProperty`、`FromParam`、`FromReturn` 与 `EqualsUnqualified`。还没有任何正式测试直接覆盖 `FromClass(UClass*)` / `FromStruct(UScriptStruct*)` 这两条 runtime 映射入口，也没有锁住 script-generated `UASClass` / `UASStruct` 经 wrapper 回到 `GetScriptObject()` / `GetScriptStruct()` 的分支。 |
| 风险评估 | 如果 `FromClass` / `FromStruct` 无法把 script-generated 类型正确映射回 `FAngelscriptTypeUsage`，反射、调试器、属性桥接和动态类型比较都会出现“类已经生成但 wrapper 失效”的隐蔽回归；现有套件只能在更高层看到模糊失败。 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeUsage.FromClassStruct.NativeAndScriptRoundTrip` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeUsageTests.cpp` |
| 场景描述 | 在同一个测试里同时取一组 native 类型和一组 script-generated 类型，分别调用 `FAngelscriptTypeUsage::FromClass` / `FromStruct`。native 侧用 `AActor::StaticClass()` 与 `TBaseStructure<FIntPoint>::Get()`；script 侧编译一个 `UCLASS()` actor 和一个 script struct，拿到生成后的 `UClass*` / `UScriptStruct*`。随后验证 wrapper 的 `Type`、`ScriptClass`、`GetClass()` 与 `GetUnrealStruct()` 都映射到正确目标。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileScriptModule`。脚本建议为：`UCLASS() class ATypeUsageClassProbe : AActor {}` 与 `struct FTypeUsageStructProbe { int Value = 7; }`。编译后用 `FindGeneratedClass(&Engine, TEXT("ATypeUsageClassProbe"))` 取 script class，用 `FindObject<UScriptStruct>(FAngelscriptEngine::GetPackage(), TEXT("TypeUsageStructProbe"))` 或复用一个小 helper 取 script struct。native 对照类型直接使用 `AActor::StaticClass()` 与 `TBaseStructure<FIntPoint>::Get()`。 |
| 期望行为 | 1）`FromClass(AActor::StaticClass())` 返回有效 usage，`GetClass() == AActor::StaticClass()`，且 `ScriptClass == nullptr`；2）`FromStruct(TBaseStructure<FIntPoint>::Get())` 返回有效 usage，`GetUnrealStruct() == TBaseStructure<FIntPoint>::Get()`，且不是 `GetScriptStruct()` 分支；3）`FromClass(ScriptClass)` 返回 `Type == FAngelscriptType::GetScriptObject()`、`ScriptClass != nullptr`，且 `GetClass() == ScriptClass`；4）`FromStruct(ScriptStruct)` 返回 `Type == FAngelscriptType::GetScriptStruct()`、`ScriptClass != nullptr`，且 `GetUnrealStruct() == ScriptStruct`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `CompileScriptModule` + `FindGeneratedClass` + `FindObject<UScriptStruct>`/shared struct helper + `FAngelscriptTypeUsage::FromClass` + `FAngelscriptTypeUsage::FromStruct` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-128 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:35)

### 一、现有测试问题

#### Issue-111：`Core.CreateEngine` 没有验证底层 `asIScriptEngine` 与模块注册表真正隔离

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Core.CreateEngine` |
| 行号范围 | 57-80 |
| 问题描述 | 用例创建了 `LocalEngineA` 和 `LocalEngineB`，但断言只覆盖“wrapper 非空”“`GetScriptEngine()` 非空”和 creation mode 不是 magic value。它没有检查 `ScriptEngineA != ScriptEngineB`，也没有编译一个只存在于 `LocalEngineA` 的模块再确认 `LocalEngineB` 看不到。因此如果 `CreateForTesting(...)` 错误地复用了同一个底层 `asIScriptEngine`、或者两个测试引擎共享模块注册表，这个测试仍会通过。 |
| 影响 | 测试引擎一旦失去真正隔离，LanguageFeatures 这类大量使用 shared/full/clone fixture 的套件会更容易出现顺序依赖和隐式污染；而当前 `Core.CreateEngine` 无法在最底层把这种回归拦住。 |
| 修复建议 | 1）先补 `TestNotEqual(TEXT("Core.CreateEngine should create distinct script-engine instances"), ScriptEngineA, ScriptEngineB)`；2）再在 `LocalEngineA` 上编译一个唯一模块，例如 `CreateEngineOnlyInA`，断言 `LocalEngineA->GetModuleByModuleName(...)` 有效而 `LocalEngineB->GetModuleByModuleName(...)` 为空；3）若未来要继续覆盖 clone fallback，还可以把“requested mode 正确”和“实例/模块隔离正确”拆成两个独立测试，避免一个用例承担过多职责。 |

#### Issue-112：`Memory.PoolLeakTracking` 没有验证分配成功与地址唯一性，allocator 回归会被计数断言掩盖

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptMemoryTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.Memory.PoolLeakTracking` |
| 行号范围 | 98-115 |
| 问题描述 | 用例直接调用 `AllocScriptNode()` 两次和 `AllocByteInstruction()` 一次，然后不做任何 `TestNotNull` 或“两个 script-node 地址必须不同”的检查，就把三块地址送进 `Free*()` 并只看 pool size 是否变成 `2`/`1`。如果 allocator 回归成返回 `nullptr`、或者两次 `AllocScriptNode()` 错误地复用了同一地址，当前测试仍然可能在“池里有 2 个条目”这类计数层面通过，或者把真正的分配问题放大成后续释放/回收噪声。 |
| 影响 | memory manager 一旦在分配阶段退化，`PoolLeakTracking` 不能第一时间把根因锁在“拿到的指针就不对”，而是会让问题以错误 pool 大小、重复 free 或非确定性失败的形式出现，降低内部内存测试的诊断价值。 |
| 修复建议 | 1）对 `ScriptNodeA`、`ScriptNodeB`、`Instruction` 全部加 `TestNotNull` guard，失败立即返回；2）补 `TestNotEqual(TEXT("Two consecutive script-node allocations should produce distinct live addresses"), ScriptNodeA, ScriptNodeB)`，确保测试先证明“确实分到了两块活跃内存”；3）在通过这些 guard 后再进入 `Free*()` 和 pool-size 断言，避免把分配阶段根因掩盖到回收阶段。 |

#### Issue-113：`NativeReferenceRoundTrip` 只验证 `ref/out` 回写值，没有锁定 C++ bridge 真的执行了脚本体副作用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.NativeReferenceRoundTrip` |
| 行号范围 | 349-434 |
| 问题描述 | 用例的脚本函数 `AdjustNativeValue(int Delta, int& Value)` 只有一条 `Value += Delta;`，测试对脚本侧调用只读取 `ScriptAdjustedValue == 15`，对 C++ `Execute_AdjustNativeValue(...)` 只断言 `CppAdjustedValue == 27`。它没有给 `AdjustNativeValue` 增加任何 actor/property 副作用，也没有在 C++ 调用后读取脚本实例状态。因此只要 bridge 最终把 `ref/out` 参数写回 caller buffer，哪怕脚本实例方法体没有被完整执行、call count 没更新或对象上下文不对，这个用例仍可能通过。 |
| 影响 | 当前测试锁住的是“参数值被改了”，不是“native interface 的 C++ bridge 确实进入了脚本实现并在正确对象上执行”。一旦 `RuntimeCallEvent` 路径退化成只处理参数缓冲、不再可靠驱动脚本体副作用，`NativeReferenceRoundTrip` 仍不足以报警。 |
| 修复建议 | 1）在脚本类里补一个可观察属性，例如 `UPROPERTY() int AdjustCallCount = 0;` 或 `UPROPERTY() int LastAdjustedValue = -1;`，并在 `AdjustNativeValue()` 内同时更新它；2）保留现有 `CppAdjustedValue == 27` 断言之外，再在 C++ `Execute_` 调用后读取该属性，断言 call count 递增或 `LastAdjustedValue == 27`；3）若想同时锁住 script-side 和 C++-side 两条入口，建议把 BeginPlay 那次调用和 C++ bridge 那次调用分别编码成不同的最终状态，避免两条路径混成一个数值。 |

### 二、需要新增的测试

#### NewTest-94：补 `CreateForTesting` 的引擎隔离回归，直接锁住“不同测试引擎不共享模块注册表”

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CreateForTesting`、`FAngelscriptEngine::GetModuleByModuleName`、`FAngelscriptEngine::DiscardModule` |
| 现有测试覆盖 | `Core.CreateEngine` 只验证 wrapper / `asIScriptEngine*` 非空和 creation mode 非 magic value；当前没有任何正式用例证明两个 `CreateForTesting(...)` 产物之间的模块表、script engine 实例和编译结果彼此隔离。 |
| 风险评估 | 如果 `CreateForTesting` 未来错误地复用当前引擎、clone fallback 共享了底层 module registry，整个测试体系会出现隐式顺序依赖，而现有 `Core.CreateEngine` 不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Core.CreateEngine.IsolatedModuleRegistries` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` |
| 场景描述 | 通过 `CreateForTesting` 创建两个独立测试引擎 A/B；只在 A 上编译一个唯一模块并执行，再检查 B 完全看不到该模块和函数。 |
| 输入/前置 | 使用 `FAngelscriptEngineConfig` + `FAngelscriptEngineDependencies::CreateDefault()` + `FAngelscriptEngine::CreateForTesting(...)`。建议脚本为 `int Run() { return 42; }`，模块名用 `ASCoreCreateEngineIsolationA`。A 上用现有 `CompileModuleFromMemory(&EngineA, ...)` 或等价 helper 编译，之后读取 `EngineA->GetModuleByModuleName(TEXT("ASCoreCreateEngineIsolationA"))` 与 `EngineB->GetModuleByModuleName(TEXT("ASCoreCreateEngineIsolationA"))`。 |
| 期望行为 | 1）`EngineA->GetScriptEngine() != EngineB->GetScriptEngine()`；2）A 上模块存在且 `Run()` 执行结果为 `42`；3）B 上同名模块查询结果为空；4）A `DiscardModule(...)` 后，B 仍保持空状态，避免“丢弃一个引擎的模块影响另一个引擎”。 |
| 使用的 Helper | `FAngelscriptEngine::CreateForTesting` + `CompileModuleFromMemory` + `ExecuteIntFunction` + `GetModuleByModuleName` + `DiscardModule` |
| 优先级 | P1 |

#### NewTest-95：补 native interface `ref/out` bridge 的脚本副作用断言，锁住 `UASFunction_ReferenceArg::RuntimeCallEvent`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunction_ReferenceArg::RuntimeCallEvent`、`UASFunction_ReferenceArg_JIT::RuntimeCallEvent` |
| 现有测试覆盖 | `Interface.NativeReferenceRoundTrip` 只验证 script-side 调用后 `ScriptAdjustedValue == 15`，以及 C++ `Execute_AdjustNativeValue(...)` 后 caller 侧 `CppAdjustedValue == 27`；没有任何断言直接证明 C++ bridge 调用后脚本实例自身的属性副作用也被执行。 |
| 风险评估 | 如果 `ref/out` bridge 将参数写回 caller buffer，但脚本函数体没有在正确对象上执行，当前套件仍可能绿灯；这会把 `RuntimeCallEvent` 回归推迟到更复杂的 gameplay 交互里才暴露。 |
| 建议测试名 | `Angelscript.TestModule.Interface.NativeReferenceRoundTrip.CppBridgeMutatesActorState` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` |
| 场景描述 | 基于现有 `NativeReferenceRoundTrip` fixture 再加一个 actor 属性，例如 `AdjustCallCount` / `LastAdjustedValue`。让 `AdjustNativeValue(int Delta, int& Value)` 在修改 `Value` 的同时更新这些属性；然后分别走 BeginPlay 脚本调用与 C++ `Execute_AdjustNativeValue(...)` 调用，验证两次入口都会留下不同的对象状态。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `CompileScriptModule` + `FActorTestSpawner` + `BeginPlayActor` + `IAngelscriptNativeParentInterface::Execute_AdjustNativeValue`。建议脚本把 `AdjustNativeValue` 改成：`Value += Delta; AdjustCallCount += 1; LastAdjustedValue = Value;`，BeginPlay 里仍用 `Value = 10` 调一次 `AdjustNativeValue(5, Value)`。随后 C++ 侧以 `CppAdjustedValue = 20` 调 `Execute_AdjustNativeValue(Actor, 7, CppAdjustedValue)`。 |
| 期望行为 | 1）BeginPlay 后 `ScriptAdjustedValue == 15`、`AdjustCallCount == 1`、`LastAdjustedValue == 15`；2）C++ bridge 调用后 `CppAdjustedValue == 27`；3）再次读取 actor 属性时 `AdjustCallCount == 2` 且 `LastAdjustedValue == 27`。这些断言共同证明 bridge 不只是改了 caller buffer，而是真正进了脚本实现并落在正确对象实例上。 |
| 使用的 Helper | `CompileScriptModule` + `FActorTestSpawner` + `BeginPlayActor` + `IAngelscriptNativeParentInterface::Execute_AdjustNativeValue` + `ReadPropertyValue<FIntProperty>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-111 |
| FlakyRisk | 1 | Issue-112 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 12:55)

### 一、现有测试问题

#### Issue-114：`GC.InvalidLookup` 只覆盖空 collector 的 `index=0`，测试名明显高估了无效查询保护范围

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.GC.InvalidLookup` |
| 行号范围 | 304-324 |
| 问题描述 | 用例创建的是全新 `asCGarbageCollector`，随后只调用一次 `GetObjectInGC(0, &SeqNbr, &Object, &Type)`，验证空 collector 上的失败返回码和输出清零。它没有覆盖“collector 已经持有对象时，越界 index 仍应拒绝并清空输出”的错误路径，也没有证明 `GetObjectInGC` 在存在有效 entry 的情况下不会把越界查询误返回成上一条对象。测试名却是宽泛的 `InvalidLookup`。 |
| 影响 | 一旦 `GetObjectInGC` 在非空 collector 上的边界检查回归，例如把 `CurrentSize` 之后的 index 误当成有效对象、或者失败时不再清理输出参数，当前套件仍会绿灯；GC 查询路径的真实边界条件会被高估。 |
| 修复建议 | 1）把当前用例名收窄为 `InvalidLookup.EmptyCollector`，避免继续用一个空路径断言代表全部 invalid lookup 合同；2）在同文件补一个 populated-collector 的越界查询断言，先注册 probe object 再对 `GetObjectInGC(CurrentSize, ...)` 验证 `asINVALID_ARG`、`SeqNbr/Object/Type` 全部被重置；3）若保留单测试用例，也至少把 empty path 和 populated path 都编码进去，确保 `GetObjectInGC` 的失败清理逻辑不会只在空 collector 上被验证。 |

### 二、需要新增的测试

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-114 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无 | 0 | 无 |

---

## 测试审查 (2026-04-09 12:57)

### 一、现有测试问题

#### Issue-115：`Types.Float` 每次只执行当前引擎配置的一条分支，另一半 `float32/float64` 返回读取路径长期处于未测状态

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.Types.Float` |
| 行号范围 | 129-162 |
| 问题描述 | 用例先读 `ScriptEngine->GetEngineProperty(asEP_FLOAT_IS_FLOAT64)`，然后二选一生成 `double Run()` 或 `float Run()`，最后把断言完全交给 `ReadExpectedFloatResult(...)`。这意味着单次执行只会覆盖 `GetReturnQWord()` 或 `GetReturnFloat()` 其中一条返回解码路径；另一条分支即使在 `FAngelscriptEngine::Initialize_AnyThread()` 的 `bScriptFloatIsFloat64` wiring、声明选择或返回值读取上回归，也不会在默认测试配置里暴露。测试名却仍然是宽泛的 `Types.Float`。 |
| 影响 | 一旦脚本浮点模式配置与 runtime 返回读取逻辑发生偏移，例如 engine 配成 `float64` 但测试环境始终只跑 `float32`，LanguageFeatures 套件会对一整条执行路径失去保护；这类问题通常要等到用户切换配置或不同机器上才暴露。 |
| 修复建议 | 1）把当前用例拆成显式的双模矩阵，而不是依赖“当前环境碰巧是哪种模式”；2）通过 `FAngelscriptEngine::CreateForTesting(...)` 构造 `bScriptFloatIsFloat64=false/true` 两个独立引擎，分别执行 `float Run()` 与 `double Run()`，锁死 `GetReturnFloat()` 和 `GetReturnQWord()` 两条路径；3）保留现有 `ReadExpectedFloatResult(...)` helper 也可以，但必须让测试明确驱动两种配置，而不是让其中一半分支长期是 dead branch。 |

### 二、需要新增的测试

#### NewTest-96：补 `float` 配置双模执行回归，直接锁住 `bScriptFloatIsFloat64` 到 runtime 返回读取的整条 wiring

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CreateForTesting`、`FAngelscriptEngine::Initialize_AnyThread` |
| 现有测试覆盖 | `Types.Float` 只读取当前引擎已有的 `asEP_FLOAT_IS_FLOAT64` 值，然后执行其中一个脚本分支；当前没有任何正式回归测试显式创建 `bScriptFloatIsFloat64=false/true` 两种 engine 配置，也没有把配置值、脚本声明选择和 `GetReturnFloat`/`GetReturnQWord` 读取路径串成一条可观察断言链。 |
| 风险评估 | 如果 `Initialize_AnyThread()` 没把 `ConfigSettings->bScriptFloatIsFloat64` 正确下发到 `asEP_FLOAT_IS_FLOAT64`，或者 `Types.Float` 相关 helper 只在默认模式可用，项目会在切换浮点兼容模式时出现“配置成功但执行结果读取错位”的隐蔽回归。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.Types.Float.ConfigurationModes` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 场景描述 | 创建两个独立测试引擎：引擎 A 配置 `bScriptFloatIsFloat64=false`，引擎 B 配置 `bScriptFloatIsFloat64=true`。A 编译并执行 `float Run() { float A = -1.25f; float B = 2.5f; return A + B; }`；B 编译并执行 `double Run() { double A = -1.25; double B = 2.5; return A + B; }`。分别验证 engine property、脚本声明和返回值读取都与配置一致。 |
| 输入/前置 | 使用 `FAngelscriptEngineConfig` + `FAngelscriptEngineDependencies::CreateDefault()` + `FAngelscriptEngine::CreateForTesting(...)`。A/B 两个 config 只在 `bScriptFloatIsFloat64` 上不同。执行时可复用 `BuildModule` / `GetFunctionByDecl` / `ReadExpectedFloatResult`，但必须分别断言 `Engine->GetScriptEngine()->GetEngineProperty(asEP_FLOAT_IS_FLOAT64)` 为 `0` 和 `1`。 |
| 期望行为 | 1）引擎 A 的 `asEP_FLOAT_IS_FLOAT64 == 0`，`GetFunctionByDecl("float Run()")` 成功，返回值按 `GetReturnFloat()` 解码后接近 `1.25f`；2）引擎 B 的 `asEP_FLOAT_IS_FLOAT64 == 1`，`GetFunctionByDecl("double Run()")` 成功，返回值按 `GetReturnQWord()` 解码后接近 `1.25`；3）两个引擎互不污染，`float32` 配置不会错误接受 `double Run()` 断言，`float64` 配置也不会退回到 `float` 路径。 |
| 使用的 Helper | `FAngelscriptEngine::CreateForTesting` + `BuildModule` + `GetFunctionByDecl` + `ReadExpectedFloatResult` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-115 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 13:12)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-116`、`Issue-117`，保留在前文 `## 测试审查 (2026-04-09 13:11)` 小节；此处仅补文档末尾锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-97`，保留在前文 `## 测试审查 (2026-04-09 13:11)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-117 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
---

## 测试审查 (2026-04-09 13:24)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-118`、`Issue-119`，保留在前文 `## 测试审查 (2026-04-09 13:23)` 小节；此处仅补文档末尾锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-98`，保留在前文 `## 测试审查 (2026-04-09 13:23)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-118 |
| WrongHelper | 1 | Issue-119 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 13:35)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-120`、`Issue-121`，保留在前文 `## 测试审查 (2026-04-09 13:33)` 小节（当前约第 2843 行起）；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-99`、`NewTest-100`，保留在前文 `## 测试审查 (2026-04-09 13:33)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-120 |
| WeakAssertion | 1 | Issue-121 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 23:28)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-122`，保留在前文 `## 测试审查 (2026-04-09 23:25)` 小节；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-101`，保留在前文 `## 测试审查 (2026-04-09 23:25)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-122 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
---

## 测试审查 (2026-04-09 23:46)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-123`、`Issue-124`、`Issue-125`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处仅补真实 EOF 锚点，避免覆盖已有内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-102`、`NewTest-103`，保留在前文 `## 测试审查 (2026-04-09 23:40)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-123 |
| BadIsolation | 1 | Issue-124 |
| WeakAssertion | 1 | Issue-125 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |
---

## 测试审查 (2026-04-09 23:58)

### 一、现有测试问题

#### Issue-126：`StructCppOpsTests` 唯一用例只测 `BlueprintType` metadata，文件主题与断言对象严重错位

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptStructCppOpsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Internals.StructCppOps.NotBlueprintTypeByDefault` |
| 行号范围 | 10-63 |
| 问题描述 | 该文件名、测试分组和用户看到的 coverage 标签都是 `StructCppOps`，但唯一用例 `NotBlueprintTypeByDefault` 的断言只有 `Struct->GetBoolMetaData(TEXT("BlueprintType")) == false`。`BuildScriptStruct(...)` 也只是编译一个带默认成员值的脚本 struct 并取回 `UScriptStruct`，完全没有触达 `ICppStructOps`/copy constructor/move/dtor/identical/serialize 等任何 C++ ops 语义。换句话说，当前这 1 个用例证明的只是“脚本 struct 默认不是 BlueprintType”，却被放在 `StructCppOps` 桶里充当整个组件覆盖。 |
| 影响 | 文档和测试目录会把 `StructCppOps` 渲染成“已有 dedicated coverage”，实际却没有一条断言锁住 struct 的 native ops 行为；后续如果脚本 struct 的 copy、destruct 或 compare 钩子回归，这个文件仍会全部绿灯，造成明显的 coverage 幻觉。 |
| 修复建议 | 1）如果短期内保留当前 metadata 用例，应把它迁移到更贴切的文件/分组，例如 `Internals.Struct.Metadata.NotBlueprintTypeByDefault`；2）`AngelscriptStructCppOpsTests.cpp` 应新增真正的 C++ ops 用例，至少覆盖 `PrepareCppStructOps` 后的 `HasNoOpConstructor/HasDestructor/HasCopy/HasIdentical` 等行为；3）若暂时没有实现这些用例，至少在文件头注释明确“当前不覆盖 cpp ops，只覆盖 struct metadata”，避免误导覆盖判断。 |

#### Issue-127：`ControlFlow.NeverVisited` 以 `TestTrue(..., true)` 收尾，实际上没有锁住任何语义或诊断合同

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.NeverVisited` |
| 行号范围 | 98-115 |
| 问题描述 | 该用例编译脚本 `void Run(bool bCondition) { if (bCondition) { return; } int Value = 42; }` 后，只检查 `GetFunctionByDecl(..., "void Run(bool)") != nullptr`，最后的核心断言却是 `TestTrue(TEXT("NeverVisited should compile code with a potentially unreachable block"), true);`。这条断言与 `Module`、diagnostics、bytecode、warning/error 都没有关系，即使编译器完全不发出预期 warning、控制流分析退化、甚至未来把测试名中的“NeverVisited”语义删掉，只要函数还能编出来，这个用例就永远是绿的。 |
| 影响 | `ControlFlow` 文件里唯一试图触达“不可达/未访问代码”语义的用例实际上没有验证任何分析结果，会把控制流分析回归伪装成通过；同时它还占掉一个测试名，让审查者误以为 unreachable path 已经有 dedicated coverage。 |
| 修复建议 | 1）把这条用例改成真正的 diagnostics/assertion 测试：例如读取 `Engine.Diagnostics`，明确断言是否存在或不存在特定 warning；2）如果当前编译器合同是“允许该代码且不报错”，那就至少断言 `Engine.FormatDiagnostics()` 中没有 error，且执行 `Run(true)` / `Run(false)` 都能正常返回；3）若目标只是 smoke compile，应把测试名收窄为 `CompileUnreachableTail`，不要继续使用 `NeverVisited` 这种暗示已验证语义分析的名字。 |

### 二、需要新增的测试

#### NewTest-104：补 `context pool` 回收与按 engine 过滤测试，直达 `GetLocalPooledContextCountForTesting` 的无测试路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `AngelscriptRequestContext`、`AngelscriptReturnContext`、`FAngelscriptEngine::GetLocalPooledContextCountForTesting` |
| 现有测试覆盖 | `Angelscript.TestModule.Angelscript.Execute.Context` 只检查单个 context 的 `Prepare -> Execute -> Finished` happy path；当前没有任何测试对白盒验证 context 在 `Release()` 后是否回收到 thread-local pool、再次借出时是否被 reset、以及 pool 统计是否按 `asIScriptEngine*` 正确过滤。 |
| 风险评估 | 如果 pooled context 没有在归还时 `ResetContextForPooling()`，或者不同 engine 的 context 混入同一个池并被错误复用，现有套件只能看到偶发执行异常，却无法把问题定位到 pool 生命周期；这类问题会直接污染最基础的脚本执行路径。 |
| 建议测试名 | `Angelscript.TestModule.Internals.ContextPool.ReuseAndResetPerEngine` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptContextPoolTests.cpp` |
| 场景描述 | 创建两个 `CreateForTesting(...)` 的 isolated engine。对 EngineA 编译并执行一个最小 `void Run()`，随后释放 context，验证 EngineA 的 local pooled count 从 `0 -> 1`；再从 EngineA 重新申请 context，验证 pooled count 回到 `0` 且 context state 已被 reset 为 `asEXECUTION_UNINITIALIZED`。最后再对 EngineB 单独申请/释放一次，验证两套 pooled count 互不影响。 |
| 输入/前置 | 使用 `FAngelscriptEngine::CreateForTesting(Config, Dependencies)` + `FAngelscriptEngineScope`。在 EngineA 上编译 `void Run() {}`，取到 `void Run()` 的 `asIScriptFunction*` 后显式 `Prepare/Execute/Release`；分别在每个关键点调用 `FAngelscriptEngine::GetLocalPooledContextCountForTesting(EngineX.GetScriptEngine())` 读取计数。 |
| 期望行为 | 1）EngineA baseline pooled count 为 `0`；2）执行并释放一个 context 后，EngineA pooled count 为 `1`；3）再次 `CreateContext()` 后，EngineA pooled count 立即回到 `0`，且新借出的 context `GetState() == asEXECUTION_UNINITIALIZED`；4）EngineB 在独立借还后只影响自己的计数，不改变 EngineA 的统计。 |
| 使用的 Helper | `FAngelscriptEngine::CreateForTesting` + `FAngelscriptEngineScope` + `AngelscriptTestSupport::BuildModule` + `GetFunctionByDecl` + `FAngelscriptEngine::GetLocalPooledContextCountForTesting` |
| 优先级 | P1 |

#### NewTest-105：补 `FAngelscriptTypeUsage::FromProperty(asITypeInfo*, PropertyIndex)` 的成员类型矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptTypeUsage::FromProperty(asITypeInfo*, int32)`、`FAngelscriptTypeUsage::FromTypeId` |
| 现有测试覆盖 | `Internals.DataType.*` 只覆盖底层 `asCDataType` primitive/object-handle 语义；`TypeUsage` 方向当前已有建议集中在 `FromParam` / `FromReturn` / `EqualsUnqualified`，但没有任何测试直接覆盖“从 script type 成员表按 property index 还原 `FAngelscriptTypeUsage`”这条 runtime 反射入口。 |
| 风险评估 | 如果成员 property 的 type id 映射在 script enum、template subtype 或 script object/property table 上回归，生成类、调试器与 property bridge 可能都会读到错误类型；现有套件只能从上层现象发现问题，无法把缺陷锁定到 `FromProperty(...)`。 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeUsage.FromProperty.ScriptMemberMatrix` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeUsageTests.cpp` |
| 场景描述 | 编译一个同时包含 primitive、script enum、template container 和 script object 成员的最小脚本类型；按成员名找到 property index 后，对每个成员调用 `FAngelscriptTypeUsage::FromProperty(HolderType, Index)`，验证 wrapper 能恢复正确的 type kind、subtype 和 `ScriptClass`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `BuildModule`。脚本建议为：`enum EMode { Idle = 3, Running = 7 } class FPayload { int Value = 11; } class FHolder { int Count; array<int> Values; EMode Mode; FPayload Payload; }`。获取 `FHolder`、`EMode`、`FPayload` 的 `asITypeInfo*`，用 `GetProperty(...)` 定位 `Count/Values/Mode/Payload` 的 index。 |
| 期望行为 | 1）`Count` 的 usage 有效且 `GetAngelscriptDeclaration() == "int"`；2）`Values` 的 usage 有效、`SubTypes.Num() == 1`，且子类型声明为 `int`；3）`Mode` 的 usage 走 script-enum 分支，`Type == FAngelscriptType::GetScriptEnum()` 且 `ScriptClass == EnumTypeInfo`；4）`Payload` 的 usage 走 script-object 分支，`Type == FAngelscriptType::GetScriptObject()` 且 `ScriptClass == PayloadTypeInfo`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AngelscriptTestSupport::BuildModule` + `asIScriptEngine::GetTypeInfoByName` + `FAngelscriptTypeUsage::FromProperty` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-126 |
| WeakAssertion | 1 | Issue-127 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-10 00:13)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-128`、`Issue-129`，保留在前文 `## 测试审查 (2026-04-10 00:10)` 与 `## 测试审查 (2026-04-10 00:12)` 小节；此处仅补真实 EOF 锚点，避免重复覆盖旧内容。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-106`，保留在前文 `## 测试审查 (2026-04-10 00:10)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-129 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:25)

### 一、现有测试问题

本轮未新增现有测试质量问题；增量发现集中在 `TypeUsage` 的源码无测试入口。

### 二、需要新增的测试

#### NewTest-107：补 `FAngelscriptTypeUsage::FromProperty(FProperty*)` 的 native qualifier 矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` |
| 关联函数 | `FAngelscriptTypeUsage::FromProperty(FProperty*)` |
| 现有测试覆盖 | `TypeUsage` 方向当前只覆盖或计划覆盖 `FromParam` / `FromReturn`、`FromProperty(asITypeInfo*, PropertyIndex)`、`FromClass` / `FromStruct` 与 `EqualsUnqualified`。还没有任何测试直接命中 native `FProperty*` overload，也没有断言 `CPF_ConstParm` / `CPF_ReferenceParm` 到 `bIsConst` / `bIsReference` 的映射。 |
| 风险评估 | 如果 UFunction 参数在生成 `FProperty` 后丢失 qualifier 映射，调试器、property bridge、native function 反射与签名格式化都会在“脚本函数能编译、但 runtime wrapper 读到错误 const/ref”这一层悄悄回归；现有 LanguageFeatures 套件无法把问题定位到 `FromProperty(FProperty*)`。 |
| 建议测试名 | `Angelscript.TestModule.Internals.TypeUsage.FromProperty.NativeQualifierMatrix` |
| 测试类型 | Unit |
| 测试文件 | 追加到规划中的 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptTypeUsageTests.cpp` |
| 场景描述 | 编译一个带 `UFUNCTION()` 的 script-generated class，函数签名同时包含 `const int&in`、`int&out` 和普通值参数；拿到生成后的 `UFunction*` 参数 `FProperty*` 后，直接对每个参数调用 `FAngelscriptTypeUsage::FromProperty(FProperty*)`，验证 wrapper 对 native property flag 的恢复结果。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileScriptModule`。脚本建议为：`UCLASS() class ATypeUsageNativePropertyProbe : AActor { UFUNCTION() void Qualifiers(const int&in Input, int&out Output, bool Flag) { Output = Input; } }`。编译后用 `FindGeneratedClass(&Engine, TEXT("ATypeUsageNativePropertyProbe"))` 取 `UClass*`，再通过 `GeneratedClass->FindFunctionByName(TEXT("Qualifiers"))` 与 `FindFProperty<FProperty>(Function, TEXT("Input"))` / `FindFProperty<FProperty>(Function, TEXT("Output"))` / `FindFProperty<FProperty>(Function, TEXT("Flag"))` 拿到三个参数 property。 |
| 期望行为 | 1）`Input` 的 usage 有效，`GetAngelscriptDeclaration() == "const int&"`，`bIsConst == true`，`bIsReference == true`；2）`Output` 的 usage 有效，`GetAngelscriptDeclaration() == "int&"`，`bIsConst == false`，`bIsReference == true`；3）`Flag` 的 usage 有效，`GetAngelscriptDeclaration() == "bool"`，且两个 qualifier flag 都为 `false`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `CompileScriptModule` + `FindGeneratedClass` + `FindFProperty` + `FAngelscriptTypeUsage::FromProperty` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:41)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-130`，保留在前文 `## 测试审查 (2026-04-10 00:35)` 小节；`## 测试审查 (2026-04-10 00:39)` 为前一次 EOF 校正尝试，但仍未落在真实文件末尾。本节仅补真实 EOF 锚点，不重复正文。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-108`，保留在前文 `## 测试审查 (2026-04-10 00:35)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-130 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-10 00:51)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-131`，保留在前文 `## 测试审查 (2026-04-10 00:51)` 小节（当前约第 3892 行起）；由于本次追加时命中了前文已有锚点，正文没有落在真实 EOF，本节仅补末尾定位说明。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-109`，保留在前文较早位置（当前约第 203 行起的 `### 二、需要新增的测试` 小节）；这里不重复正文，只在真实 EOF 建立本轮锚点。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-131 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 01:06)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-132`、`Issue-133`，保留在前文 `## 测试审查 (2026-04-10 01:01)` 小节；由于追加时命中了前文相同格式段落，正文没有落在真实 EOF，这里只补末尾锚点，不重复展开。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-110`，保留在前文 `## 测试审查 (2026-04-10 01:01)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-132 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 01:19)

### 一、现有测试问题

本轮未新增现有测试质量问题；增量发现集中在 `Core/` 内部 helper/桥接路径的无测试入口。

### 二、需要新增的测试

#### NewTest-111：补 `const&` 限定 native method caller 的 type-erasure 直测，覆盖 `FunctionCallers.h` 当前完全未命中的重载

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h` |
| 关联函数 | `ASAutoCaller::MakeFunctionCaller(ReturnType(ObjectType::*)(ParamTypes...) const&)`、`ASAutoCaller::RedirectMethodCaller`、`MakeAutoMethodPtr` |
| 现有测试覆盖 | `Native/AngelscriptASSDKExecuteTests.cpp` 只覆盖 global `MakeFunctionCaller(...)` happy path；`Native/AngelscriptASSDKCallingConvTests.cpp` 的 `Thiscall` 仍是 compile-only，且仓库内没有任何生产或测试代码实例化 `FunctionCallers.h` 第 373-375 行的 `const&` method overload。当前 LanguageFeatures 目标目录里也没有 dedicated internal test 直接命中这条桥接路径。 |
| 风险评估 | 一旦后续绑定或 UE 反射生成开始暴露 `const&` 限定的 native method，type-erasure 可能在 overload 选择、method pointer 拷贝或 caller `type == 2` 路径上直接失效；现有套件只能在更高层 native bind 运行时才晚发现。 |
| 建议测试名 | `Angelscript.TestModule.Internals.FunctionCallers.ConstRefQualifiedMethodCaller` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptFunctionCallersTests.cpp` |
| 场景描述 | 在测试文件内声明一个最小 native probe，例如 `struct FConstRefQualifiedProbe { int32 Base = 11; int32 ReadPlus(int32 Delta) const& { return Base + Delta; } };`。直接调用 `ASAutoCaller::MakeFunctionCaller(&FConstRefQualifiedProbe::ReadPlus)` 与 `MakeAutoMethodPtr(&FConstRefQualifiedProbe::ReadPlus)`，把 object/arg 指针组装成 `void**` 参数数组，并通过 caller 的 method branch 直接执行一次 type-erased 调用。 |
| 输入/前置 | 使用本地 probe struct，不依赖 world/engine。准备 `FConstRefQualifiedProbe Probe; int32 Delta = 5;`；`void* DeltaPtr = &Delta; void* Args[] = { &Probe, &DeltaPtr };`。同时把 typed member pointer 通过本地 `union`/`FMemory::Memcpy` 变成 `ASAutoCaller::TMethodPtr`，供 `Caller.MethodPtr(...)` 调用。 |
| 期望行为 | 1）`Caller.IsBound()` 为 `true` 且 `Caller.type == 2`；2）`MakeAutoMethodPtr(...)` 返回的 `FGenericFuncPtr` 已绑定；3）直调后返回值为 `16`，证明 `const&` 限定 method pointer 能走通 type-erasure + method caller；4）`Probe.Base` 保持 `11`，防止 caller 错把对象或参数槽位写坏。 |
| 使用的 Helper | 直接使用 `ASAutoCaller::MakeFunctionCaller` + `MakeAutoMethodPtr` + `FMemory::Memcpy`/本地 pointer reinterpret helper |
| 优先级 | P1 |

#### NewTest-112：补 `GetReifyType<T>()` 的 debugger type map 与 fallback 矩阵，直达 `Helper_Reification.h` 当前无测试源码

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/Helper_Reification.h` |
| 关联函数 | `GetReifyType<T>()`、`EReifiedType` |
| 现有测试覆盖 | 代码库搜索显示当前没有任何测试直接包含或断言 `Helper_Reification.h`；`Types.FloatDebuggerFormatting` 只从更高层验证 debugger 字符串里是否出现科学计数法，没有锁定底层 reify enum 映射。 |
| 风险评估 | 如果 debug value reification 在 `int32` / `double` / `FName` / `UObject*` 这些基础类型上映射错 enum，或在 non-debug build 下没有安全回退到 `Unknown`，调试器和容器 debug value 展示会悄悄漂移，而现有 LanguageFeatures 套件完全不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Internals.DebugReification.TypeMapAndFallback` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDebugReificationTests.cpp` |
| 场景描述 | 直接包含 `Helper_Reification.h`，按编译配置分支验证 reify type map：在 `WITH_AS_DEBUGVALUES` 为真时，检查若干已列入 `REIFIED_TYPES` 的类型返回非 `Unknown` 且彼此可区分；在 fallback 分支下，检查同样的类型全部安全回退到 `Unknown`。同时保留一个未注册类型样本，验证无论哪种配置都不会被误判成已知 reify type。 |
| 输入/前置 | 测试样本建议最少包含 `int32`、`double`、`FName`、`UObject*` 和未注册的 `FIntPoint`。实现上可写成 `const int32 Int32Type = GetReifyType<int32>();` 等直接模板调用，并用 `#if WITH_AS_DEBUGVALUES` 区分断言集合。 |
| 期望行为 | 1）`GetReifyType<FIntPoint>()` 始终等于 `EReifiedType::Unknown`；2）若 `WITH_AS_DEBUGVALUES` 为真，则 `int32`、`double`、`FName`、`UObject*` 的 reify type 都非 `Unknown`，且 `int32 != double`、`FName != UObject*`；3）若 `WITH_AS_DEBUGVALUES` 为假，则上述类型全部返回 `Unknown`，证明 fallback 分支稳定且不会伪造 debugger type id。 |
| 使用的 Helper | 直接模板调用 `GetReifyType<T>()` + 预处理分支 `#if WITH_AS_DEBUGVALUES` |
| 优先级 | P3 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 01:28)

### 一、现有测试问题

#### Issue-134：`Interface.GCSafe` 把接口类生成当成可选前提，接口元数据丢失时仍可能绿灯

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 测试名 | `Angelscript.TestModule.Interface.GCSafe` |
| 行号范围 | 298-301 |
| 问题描述 | 用例在 `FindGeneratedClass(&Engine, TEXT("UIDamageableGC"))` 之后只写了 `if (InterfaceClass != nullptr) { TestTrue(...ImplementsInterface...) }`。也就是说，只要接口 `UClass` 生成/查找失败，测试不会报错，而是继续执行 `Destroy -> TickWorld -> CollectGarbage`，最后只靠 `!WeakActor.IsValid()` 返回通过。该测试名本来要覆盖 interface 生命周期中的 GC 安全，但当前实现实际上允许“接口类型根本没生成成功，只是 actor 被正常回收”这一假阳性。 |
| 影响 | 一旦 `UIDamageableGC` 的生成、注册或查找路径回归，当前用例仍可能以“actor 已被 GC”通过，导致接口生命周期问题被普通 actor GC 路径掩盖；这会明显削弱 `Interface/` 套件对 P10 `UInterface` 主线的信号强度。 |
| 修复建议 | 1）把 `InterfaceClass` 提升为硬前置条件：`if (!TestNotNull(..., InterfaceClass)) { return false; }`；2）在此基础上保留 `Actor->GetClass()->ImplementsInterface(InterfaceClass)` 断言，确保销毁前确实处于“有效接口实例”状态；3）若继续承担 GC 安全语义，再结合前文已记录的 `SavedReferenceClearsAfterGC` 建议，让该用例或其 companion test 至少持有一次真实 interface reference，而不是只验证 actor 回收。 |

### 二、需要新增的测试

#### NewTest-113：补分支 definite-assignment warning/no-warning 双态矩阵，避免 `NotInitialized` 继续只测裸局部变量

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | `Angelscript.TestModule.Angelscript.ControlFlow.NotInitialized` 目前只编译 `int Run() { int Value; return Value; }`，再用全局 diagnostics 模糊匹配 `may not be initialized`。它没有覆盖“只在部分分支赋值时仍应 warning”与“`if/else` 两侧都赋值时不应 warning”这两条最关键的 definite-assignment 边界。 |
| 风险评估 | 如果控制流分析在分支汇合点上漏报未初始化读取，或者对完整 `if/else` 赋值误报 warning，当前控制流套件不会及时报警；这类回归会直接污染用户对编译 warning 质量的判断，也会削弱 LanguageFeatures 最大区域里最基础的 flow analysis 覆盖。 |
| 建议测试名 | `Angelscript.TestModule.Angelscript.ControlFlow.NotInitialized.BranchDefiniteAssignmentMatrix` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` |
| 场景描述 | 用 `CompileModuleWithSummary(...)` 分两次编译两个最小模块。模块 A 只在 `if (bFlag)` 内给局部变量赋值，然后立即返回；模块 B 在 `if/else` 两侧都给同一局部变量赋值，再通过两个 wrapper 分别执行 true/false 路径。测试同时锁住“partial assignment 必须 warning”和“full assignment 不得 warning 但执行结果正确”两条合同。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`。模块 A 建议脚本：`int Unsafe(bool bFlag) { int Value; if (bFlag) { Value = 7; } return Value; }`。模块 B 建议脚本：`int Safe(bool bFlag) { int Value; if (bFlag) { Value = 7; } else { Value = 9; } return Value; } int RunSafeTrue() { return Safe(true); } int RunSafeFalse() { return Safe(false); }`。分别调用 `CompileModuleWithSummary(&Engine, ECompileType::SoftReloadOnly, ...)`，并在 B 编译成功后用 `ExecuteIntFunction(&Engine, ModuleName, TEXT("int RunSafeTrue()"), Result)` / `ExecuteIntFunction(..., TEXT("int RunSafeFalse()"), Result)` 验证行为。 |
| 期望行为 | 1）模块 A `bCompileSucceeded == true` 且 `CompileResult` 为 handled，但 `Summary.Diagnostics` 中存在至少一条 `bIsError == false`、消息包含 `may not be initialized` 和变量名 `Value` 的 warning，且 `Row/Column` 都大于 `0`；2）模块 B `bCompileSucceeded == true`，其 `Summary.Diagnostics` 不包含同类 warning；3）`RunSafeTrue()` 返回 `7`，`RunSafeFalse()` 返回 `9`，证明完整分支赋值路径既没有误报 warning，也没有破坏执行结果。 |
| 使用的 Helper | `CompileModuleWithSummary` + `ExecuteIntFunction` + 本地 diagnostics filter helper（按 `Summary.Diagnostics` 精确筛 warning） |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-134 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-10 01:46)

### 一、现有测试问题

本轮未新增现有测试质量问题；`## 测试审查 (2026-04-10 01:42)` 与 `## 测试审查 (2026-04-10 01:44)` 的正文已记录在前文，但追加时命中了中段重复汇总块。本节仅补真实 EOF 锚点，不重复正文。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-114`、`NewTest-115`，保留在前文 `## 测试审查 (2026-04-10 01:42)` 与 `## 测试审查 (2026-04-10 01:44)` 小节；此处不重复展开。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
