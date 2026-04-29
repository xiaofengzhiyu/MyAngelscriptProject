# AS_VirtualMachine — asCContext 虚拟机

> **所属模块**: AS_（AngelScript 引擎内核族）
> **关注层面**: 字节码解释执行主循环、栈帧布局、嵌套调用、调试集成
> **关键源码**:
> `ThirdParty/angelscript/source/as_context.h` (10 KB, 321 行) / `.cpp` (163 KB — 第五大文件)
> · `ThirdParty/angelscript/source/as_generic.h` / `.cpp` (3 KB / 13 KB) — 泛型调用约定
> **关联文档**:
> `AS_ByteCode.md` — 指令集定义
> · `AS_CallingConventions.md` — 本地函数调用桥
> · `AS_ScriptEngine.md` — 上下文池管理

---

## 概览

`asCContext` 实现了 `asIScriptContext` 接口，是 AngelScript 的**字节码解释执行引擎**。每个 Context 维护独立的调用栈和寄存器，支持挂起/恢复（协程语义）、嵌套调用（`PushState`/`PopState`）、调试回调、异常处理。`as_context.cpp` 的 163 KB 主要是 `Execute()` 中的巨型 switch-case 分发循环。

---

## 核心执行模型

```text
Prepare(func)        // 设置要执行的函数
  → 分配栈帧
  → 设置程序计数器 (l_bc) 到函数字节码起始

SetArg*(n, value)    // 设置参数
Execute()            // ★ 主执行循环
  → switch(*l_bc) 分发每条指令
  → CALL/CALLSYS/CALLINTF 处理函数调用
  → SUSPEND 指令暂停执行
  → 异常/栈溢出检测

GetReturn*()         // 获取返回值
```

### 关键寄存器

| 寄存器 | 类型 | 含义 |
|--------|------|------|
| `l_bc` | `asDWORD*` | 程序计数器（字节码指针） |
| `l_sp` | `asDWORD*` | 栈指针 |
| `l_fp` | `asDWORD*` | 帧指针 |
| `register1` | `asQWORD` | 通用寄存器（比较结果、临时值） |
| `regs.objectRegister` | `void*` | 对象寄存器（引用返回值） |
| `regs.objectType` | `asCObjectType*` | 当前对象类型 |

### 嵌套调用 — PushState / PopState

```text
PushState()
===========
保存当前调用状态到 callStack:
  - l_bc, l_sp, l_fp
  - register1, objectRegister
  - currentFunction
  - 栈块引用

PopState()
==========
从 callStack 恢复:
  - 所有寄存器
  - 继续执行上层函数
```

嵌套调用用于从脚本调用本地代码时，本地代码再回调脚本的场景。

---

## UE Fork 扩展

| 方法 | 功能 |
|------|------|
| `MovedToNewThread()` | 线程迁移支持 |
| `SetReturnIntoValue()` | 将返回值直接写入指定内存 |
| `SetLoopDetectionCallback()` | 死循环检测回调 |
| `SetStackPopCallback()` | 栈帧弹出通知（调试器用） |
| `GetBlueprintCallstackFrame()` | 蓝图调用栈帧索引 |
| `CallFunctionCaller()` | FunctionCaller 路径（UE 绑定专用） |

---

## 小结

- `asCContext` 是有状态的执行上下文，支持挂起/恢复和嵌套调用
- 主循环是 213 条指令的巨型 switch-case（163 KB 文件的主体）
- 栈帧通过 `l_sp`/`l_fp` 管理，支持动态增长（`maximumContextStackSize` 限制）
- 通过 `SetLineCallback` 支持行级调试断点
- UE Fork 添加了线程迁移、蓝图调用栈、FunctionCaller 等扩展
