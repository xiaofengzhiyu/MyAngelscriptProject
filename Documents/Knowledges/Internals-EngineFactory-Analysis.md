# Internals 测试层引擎创建方式分析与设计讨论

> **目录**: Plugins/Angelscript/Source/AngelscriptTest/Internals  
> **分析时间**: 2026-04-23  
> **相关文件**:  
> - AngelscriptBytecodeTests.cpp  
> - AngelscriptBuilderTests.cpp  
> - AngelscriptCompilerTests.cpp  
> - 其他对比: Actor/AngelscriptActorInteractionTests.cpp, Actor/AngelscriptActorLifecycleTests.cpp  

---

## 一、问题陈述

Internals 目录下的测试理应直接操作 AngelScript SDK **内部类**（如 sCByteCode、sCBuilder、sCScriptEngine 等），以验证 SDK 底层机制的独立正确性。  

然而，当前所有 Internals 测试均采用与上层测试相同的引擎创建模式：
`cpp
FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();  // 获取 UE 封装引擎
ASTEST_BEGIN_SHARE_CLEAN
asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());  // 立刻解包
// 后续全程只使用 asCScriptEngine* 及更底层内部类
``n
**用户的直觉是正确的**: Internals 测试从概念上讲，不需要 FAngelscriptEngine 这层 UE 封装。直接使用 sCScriptEngine（甚至由工厂函数直接返回 sCScriptEngine*）才是更合理、更纯粹的设计。  

---

## 二、代码实证：Internals 测试的真实使用模式

### 2.1 典型 unwrap 模式（Bytecode 测试）
`cpp
// AngelscriptBytecodeTests.cpp
bool FAngelscriptBytecodeInstructionSequenceTest::RunTest(...)
{
    FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN(); // Step 1: Wrap
    ASTEST_BEGIN_SHARE_CLEAN
    
    asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine()); // Step 2: Unwrap
    asCModule* Module = CreateBytecodeModule(ScriptEngine, 'BytecodeInstructionSequence');
    
    asCBuilder Builder(ScriptEngine, Module);
    asCByteCode ByteCode(&Builder);
    // ... 仅使用 SDK 原生内部类
}
``n
### 2.2 典型 unwrap 模式（Builder 测试）
`cpp
// AngelscriptBuilderTests.cpp
bool FAngelscriptBuilderCompileErrorCollectionTest::RunTest(...)
{
    FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
    ASTEST_BEGIN_SHARE_CLEAN
    
    asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());
    asCModule* Module = CreateBuilderModule(ScriptEngine, 'BuilderCompileErrors');
    
    asCBuilder Builder(ScriptEngine, Module);
    Builder.silent = true;
    asCScriptFunction* Function = nullptr;
    const int32 BuildResult = Builder.CompileFunction(...);
    // ...
}
``n
### 2.3 Compiler 测试
`cpp
// AngelscriptCompilerTests.cpp (同构)
asIScriptModule* Module = AngelscriptTestSupport::BuildModule(*this, Engine, ...);
// 在 Verify 完成后解剖 bytecode
asDWORD* Bytecode = Function->GetByteCode(&BytecodeLength);
``n
**结论**: FAngelscriptEngine 在所有 Internals 测试中仅扮演 **sCScriptEngine 实例提供者** 的角色，随后被完全 bypass。  

---

## 三、对比: 其他层级如何真实使用 FAngelscriptEngine`n
与 Internals 形成鲜明对比的是，Actor/ 和 Interface/ 层级中的测试深度绑定 FAngelscriptEngine 的 UE 集成能力，**unwrap 后不再回头**。  

### 3.1 Actor 测试 — 深度依赖
`cpp
// Actor/AngelscriptActorLifecycleTests.cpp
void TickWorldThroughTickManager(FAngelscriptEngine& Engine, UWorld& World, float DeltaTime, int32 NumTicks)
{
    for (int32 TickIndex = 0; TickIndex < NumTicks; ++TickIndex)
    {
        FAngelscriptEngineScope WorldScope(Engine);  // 需要 FAngelscriptEngine 构造 Scope
        World.Tick(ELevelTick::LEVELTICK_All, DeltaTime);
    }
}

// Actor/AngelscriptActorInteractionTests.cpp
UClass* ScriptClass = CompileScriptModule(*this, Engine, ModuleName, ...);
AActor* Actor = SpawnScriptActor(*this, Spawner, ScriptClass);
VerifyByPath<FIntProperty, int32>(*this, Actor, TEXT('EventCallCount'), ...);
``n
| 能力需求 | Actor/Interface 测试 | Internals 测试 |
|---------|---------------------|-----------------|
| 生成 UClass | **必需** (CompileScriptModule) | ❌ 不需要 |
| 生成 AActor | **必需** (SpawnScriptActor) | ❌ 不需要 |
| Tick/Scope 管理 | **必需** (FAngelscriptEngineScope) | ❌ 不需要 |
| 反射属性读取 | **必需** (VerifyByPath) | ❌ 不需要 |
| 底层 bytecode 解剖 | ❌ 不需要 | **必需** (GetByteCode) |
| Builder 直接实例化 | ❌ 不需要 | **必需** (sCBuilder(...)) |

### 3.2 Interface 测试 — 同样深度依赖
`cpp
// Interface/AngelscriptInterfaceImplementTests.cpp
UClass* ScriptClass = CompileScriptModule(*this, Engine, ModuleName, ...);  // 需要 FAngelscriptEngine 编译
UClass* TargetClass = FindGeneratedClass(&Engine, TEXT('ATestInterfaceTarget'));  // 依赖引擎注册表
``n
---

## 四、为什么当前设计不合理

### 4.1 语义边界被破坏

Internals 目录的核心设计意图是 **独立于 UE 集成的 SDK 底层测试**。如果测试仍然需要经过 FAngelscriptEngine 这层 UE 封装引擎，然后再手动 unwrap，那它与 Compiler/、Actor/ 等上层目录的界限就变得模糊了。  

### 4.2 引入了多余的依赖和状态污染

FAngelscriptEngine 的初始化通常远不止创建一个 sCScriptEngine:  
- 注册 UE 类型的反射绑定（TArray、FVector、AActor、UObject 等）  
- 初始化脚本模块加载系统  
- 配置为 UE 优化的垃圾回收策略  
- 设置文件系统模块解析器  

对于测试 sCByteCode.InstrDWORD() 或者 sCBuilder.CompileFunction() 来说，以上全部**是多余的干扰变量**。测试失败时需要排查的范围被不必要地扩大了。  

### 4.3 测试纯度降低

当一个字节码测试失败时，开发者需要怀疑的问题集包括：  
1. sCByteCode 本身有 bug？  
2. FAngelscriptEngine 初始化时的某个 UE 绑定影响了引擎状态？  
3. ASTEST_CREATE_ENGINE_SHARE_CLEAN 的引擎池/清理逻辑有副作用？  

**理想的底层测试应该排除 2 和 3，只聚焦 1。**  

---

## 五、为什么当前代码会这样设计？

### 5.1 历史路径依赖（最可能）

ASTEST_CREATE_ENGINE_SHARE_CLEAN() 和 FAngelscriptEngine 的测试基础设施**先为上层目录**（Actor/、Interface/、Bindings/）搭建，提供了引擎创建、模块编译、模块清理的一条龙服务。在后续编写 Internals 测试时，**复用了同一套宏和辅助函数**，没有为底层测试专门设计独立的纯净引擎工厂。  

### 5.2 初始化便利

AngelScript 引擎的初始化确实包含一些必要步骤（配置内存分配器、消息回调、配置属性等）。FAngelscriptEngine 可能已经封装了一套 标准初始化流程。但即便如此，提取出一个 CreateBareScriptEngine() 工厂函数就足以解决这个问题，而不是让底层测试继承整个 UE 绑定层。  

### 5.3 引擎共享/隔离策略

ASTEST_CREATE_ENGINE_SHARE_CLEAN 暗示了某种引擎实例共享或池化策略（SHARE + CLEAN），可能用于加速测试执行。Internals 测试搭了这个便车，但这对于追求**最大隔离性**的底层测试来说，反而是弊大于利。  

---

## 六、建议的重构方向

### 6.1 新增纯净的 Bare Engine 工厂
`cpp
// Shared/AngelscriptTestEngineHelper.h (或 Internals/ 内部头文件)
namespace AngelscriptTestSupport
{
    /**
     * Creates a bare asCScriptEngine with minimal AngelScript SDK configuration.
     * Does NOT register any UE type bindings, script class generators, or reflection hooks.
     * Intended for Internals tests that need a pure script engine sandbox.
     */
    asCScriptEngine* CreateBareScriptEngine();
}
``n
### 6.2 Internals 测试改为直接使用原生引擎
`cpp
// 重构后的 Bytecode 测试示例
bool FAngelscriptBytecodeInstructionSequenceTest::RunTest(const FString& Parameters)
{
    asCScriptEngine* ScriptEngine = AngelscriptTestSupport::CreateBareScriptEngine();
    // 或者 RAII: FAngelscriptTestEngineGuard EngineGuard(ScriptEngine);
    
    asCModule* Module = CreateBytecodeModule(ScriptEngine, 'BytecodeInstructionSequence');
    asCBuilder Builder(ScriptEngine, Module);
    asCByteCode ByteCode(&Builder);
    ByteCode.InstrDWORD(asBC_PshC4, 42);
    ByteCode.Instr(asBC_RET);
    // ... verify
    
    ScriptEngine->Release(); // 或 RAII 自动释放
    return true;
}
``n
### 6.3 保持上层测试不动

Actor/、Interface/、Compiler/ 等目录的测试**应该继续使用 ASTEST_CREATE_ENGINE_SHARE_CLEAN()**，因为它们确实需要 FAngelscriptEngine 的全套 UE 集成能力。重构应该仅聚焦于 Internals/ 目录。  

---

## 七、总结

| 维度 | 当前状态 | 理想状态 |
|------|---------|---------|
| 引擎类型 | FAngelscriptEngine (UE 封装) | sCScriptEngine* (SDK 原生) |
| 初始化开销 | 包含全套 UE 绑定 | 最小化配置，无 UE 绑定 |
| 测试失败排查面 | 需要怀疑 UE 层和 SDK 层 | 只聚焦 SDK 内部类自身 |
| 与目录设计意图的一致性 | ❌ 不一致（Internals 却要 unwrap） | ✅ 一致（直接操作内部类） |
| 与上层测试的区别 | ❌ 模糊（都用同一套宏） | ✅ 清晰（不同层级用不同工厂） |

**结论**: Internals 目录下的测试**从语义上确实应该绕过 FAngelscriptEngine，直接使用 sCScriptEngine*（或更细粒度的内部类）**。当前使用 ASTEST_CREATE_ENGINE_SHARE_CLEAN() 是一种**实现层的妥协或历史遗留**，它让底层测试多绕了一层 UE 封装，破坏了 Internals 目录本应具备的**最小依赖、最大纯净度**的设计目标。  

---

---

## 附录 A: 目录命名讨论 — `Internals` 是否应该改名？

### A.1 问题提出

在分析 Internals 测试层的过程中，一个自然的问题是：**`Internals` 这个目录名是否合理？**

### A.2 当前命名的问题

`Internals` 这个名字存在以下问题：

1. **归属主体不明确**：`Internals` 没有回答"**谁的** Internals？"
   - 是 Angelscript **插件自身**的内部实现？
   - 还是 AngelScript **SDK** 的内部类？
   - 对后来者来说容易产生困惑。

2. **与同级目录的命名风格不一致**：
   | 目录 | 命名精确度 | 语义 |
   |------|-----------|------|
   | `Actor/` | ✅ 高 | 明确测试 AActor 场景 |
   | `Bindings/` | ✅ 高 | 明确测试 C++/脚本绑定 |
   | `Compiler/` | ⚠️ 中 | 从 UE/Angelscript 封装层视角测试编译 |
   | `Interface/` | ✅ 高 | 明确测试 UInterface |
   | `Internals/` | ❌ 低 | 宽泛且不说明归属 |

3. **与 `Compiler/` 的层级关系模糊**：
   - `Compiler/`：从 **UE-Angelscript 封装层**测试编译（`FAngelscriptEngine` 视角，模块级）
   - `Internals/`：从 **AS SDK 原生内部类**测试编译（`asCBuilder` 视角，函数级）
   
   两者测试的是**同一件事的不同层级**，但目录名完全没有体现这种关系。

### A.3 为什么 `AngelScriptSDK` 是更好的名字

| 评判维度 | `Internals` | `AngelScriptSDK` |
|---------|------------|-----------------|
| 归属主体 | ❌ 模糊 | ✅ 直接点明是 AngelScript SDK |
| 与 `Compiler/` 的层级关系 | ❌ 模糊 | ✅ 清晰（封装层 vs SDK 原生） |
| 自说明性 | ❌ 差 | ✅ 好 |
| 与插件命名一致性 | ⚠️ 还行 | ✅ 好（AngelscriptTest/AngelScriptSDK） |

#### 改名后的目录结构更清晰：

```
AngelscriptTest/
  Actor/              ← UE Actor 场景测试
  Bindings/           ← C++/脚本绑定测试
  Compiler/           ← UE-Angelscript 封装层编译测试（FAngelscriptEngine 视角）
  AngelScriptSDK/     ← AngelScript SDK 原生内部测试（asCScriptEngine/asCBuilder/asCByteCode 视角）
  Core/               ← 插件核心功能
  Interface/          ← UInterface 测试
```

对比关系一目了然：
- `Compiler/`：从 UE 用户角度验证"编译是否工作"
- `AngelScriptSDK/`：从 SDK 引擎角度验证"字节码和编译器内部机制是否正确"

### A.4 注意事项

改名为 `AngelScriptSDK` 并不意味着这个目录是"测试 AngelScript SDK 的全面回归测试"——它仍然是 UE 插件测试模块的一部分：
- 调用 UE 测试框架（`FAutomationTestBase` + `TEST_*` 宏）
- 测试断言的是**插件需求的正确性**（指令序列、异常匹配）
- 而不是 SDK 自身的全面回归测试

但即便如此，`AngelScriptSDK` 仍然比 `Internals` 好太多了——它至少**精准传达了测试对象是谁**。

### A.5 建议

```diff
- AngelscriptTest/Internals/
+ AngelscriptTest/AngelScriptSDK/
```

这个改动会让整个测试目录的语义瞬间清晰，后续维护者看到这个目录就能立刻理解：**这里面的测试直接操作 AngelScript SDK 内部类，绕过所有 UE 插件封装**。

---

*This document was generated to capture the design discussion around engine factory usage in the Internals test layer.*
