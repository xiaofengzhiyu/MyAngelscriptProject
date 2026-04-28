# 脚本结构体生成机制

> **所属模块**: 类型系统与生成链路 → `UASStruct` / `FASStructOps`
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`, `Documents/Hazelight/ScriptStructImplementation.md`

这一节补的是 `2.2.2` 和 `2.2.3` 两个缺口：`UASStruct` 为什么要通过 FakeVTable 接进 UE 的 `ICppStructOps`，以及它和 Hazelight 方案的差异边界。核心结论很简单：当前实现把“结构体身份”留在 `UASStruct`，把“生命周期操作”压进 `FASStructOps`，再通过 UE5 已经提供的 FakeVTable 协议完成注入，因此不需要改引擎 ABI。

## 2.2.2 FakeVTable 注入与生命周期操作

- `UASStruct` 继续扮演 `UScriptStruct` 子类，负责 `ScriptType`、`Guid`、`NewerVersion` 等长期身份信息
- `FASStructOps` 继承 `UASStruct::ICppStructOps`，缓存 `ConstructFunction`、`EqualsFunction`、`ToStrFunction`、`HashFunction`
- `FASStructOps::FASFakeVTable` 把 `Construct`、`Destruct`、`Copy`、`Identical`、`GetStructTypeHash` 几个操作函数地址挂给 UE

当前接缝是：

```text
UASStruct
  -> CreateCppStructOps()
      -> FASStructOps(this)
          -> FakeVPtr = &FakeVTable
              -> UE 在结构体生命周期里通过 ICppStructOps 回调脚本操作
```

这意味着 `UASStruct` 不直接承担构造/析构逻辑，而是把这些动作交给 `FASStructOps`：

- `Construct`：优先执行脚本侧构造函数；没有构造函数时退回 `Memzero`
- `Destruct`：对 `asCScriptObject` 调析构
- `Copy`：走脚本对象复制语义，而不是简单字节拷贝
- `Identical`：优先使用脚本 `opEquals`
- `GetStructTypeHash`：优先使用脚本 `Hash()`，同时把 UE 的 `CPF_HasGetValueTypeHash` 能力标志补齐

## 2.2.3 与 Hazelight 方案的差异边界

- Hazelight 方案在 `ICppStructOps` 协议上做了额外参数扩展，本质上是修改引擎接口以携带脚本侧上下文
- 当前实现没有改 UE 的 `ICppStructOps` 定义，而是利用 UE5 的 `FStructOpsFakeVTable` 做运行时注入
- 两者都在解决“UE 如何调用脚本结构体生命周期操作”这个问题，但我们的边界更清晰：引擎 ABI 不变，脚本桥接逻辑留在插件内部

这也解释了为什么 `UpdateScriptType()` 只需要刷新 `FASStructOps` 的方法缓存，而不需要重做整个 `UScriptStruct` 注册：

- 类型级稳定性仍然由 `UASStruct` 维护
- 操作级能力由 `FASStructOps` 重新探测
- 最终再把 `STRUCT_IdenticalNative` 等能力位写回 UE 结构体标志

## 当前设计真正划出的边界

- **`UASStruct`**：身份、版本链、类型指针、GUID、注册锚点
- **`FASStructOps`**：生命周期协议适配器、脚本方法缓存、FakeVTable 注入
- **Hazelight 文档**：适合作为“问题域对照”，不适合作为当前实现的字面镜像

因此这一节最值得记住的不是“FakeVTable 很巧”，而是：**当前结构体支持走的是插件内部协议适配，而不是引擎接口改造。**

## 小结

- FakeVTable 让脚本结构体能以 UE 原生 `ICppStructOps` 方式参与构造、析构、复制、比较和哈希
- `UASStruct` 保存长期身份，`FASStructOps` 负责运行时操作，两层边界清晰
- 与 Hazelight 相比，当前实现最大的差异是不改引擎 ABI，而是利用 UE5 现成的 FakeVTable 机制完成桥接
