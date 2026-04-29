# AS_ByteCode — 字节码指令集

> **所属模块**: AS_（AngelScript 引擎内核族）
> **关注层面**: 指令编码格式、asCByteCode 构造器、优化管线、序列化
> **关键源码**:
> `ThirdParty/angelscript/source/as_bytecode.h` (6 KB, 210 行) / `.cpp` (90 KB, 3052 行)
> · `Core/angelscript.h` (73 KB) — asEBCInstr 枚举 (213 条指令)、asEBCType 枚举 (22 种格式)
> · `ThirdParty/angelscript/source/as_restore.h` / `.cpp` (9 KB / 170 KB) — 序列化
> **关联文档**:
> `AS_Compiler.md` — 编译器如何发射字节码
> · `AS_VirtualMachine.md` — VM 如何执行字节码

---

## 概览

AngelScript 字节码是一种**定长基底 + 变长参数**的指令格式。每条指令的第一个字节存储 opcode（0-212），第二字节始终为 0（对齐填充），后跟 0-3 个 DWORD 的参数。`asCByteCode` 类在编译期构建字节码双向链表，通过四阶段 Finalize 管线（PostProcess → Optimize → ResolveJump → ExtractLineNumbers）输出最终可执行序列。

---

## 指令编码格式

```text
指令基本结构（单位：DWORD = 4 字节）
======================================
 Byte 0: opcode (asEBCInstr, 0-255)
 Byte 1: 0 (always zero, alignment)
 Byte 2-3: 第一个 WORD 参数（或 padding）
 DWORD 1-3: 附加参数（DW/QW/PTR）

 BYTECODE_SIZE  = 4   // 基础指令大小：1 个 DWORD
 MAX_DATA_SIZE  = 8   // 最大附加数据：2 个 DWORD
 MAX_INSTR_SIZE = 12  // 单条指令最大：3 个 DWORD
```

### 22 种指令类型 (asEBCType)

| 类型 | 大小(DW) | 含义 | 参数格式 |
|------|---------|------|---------|
| `INFO` | 0 | 元信息（不输出） | — |
| `NO_ARG` | 1 | 无参数 | opcode only |
| `W_ARG` / `wW_ARG` / `rW_ARG` | 1 | 16 位 WORD | op + w16 |
| `DW_ARG` / `rW_DW_ARG` | 2 | 32 位 DWORD | op + w16 + dw32 |
| `QW_ARG` / `wW_QW_ARG` | 3 | 64 位 QWORD | op + w16 + qw64 |
| `QW_DW_ARG` | 4 | QW + DW 双参数 | op + qw64 + dw32 |
| `wW_rW_rW_ARG` | 2 | 三寄存器 | op + w0 + w1 + w2 |
| `wW_rW_ARG` / `rW_rW_ARG` | 2 | 双寄存器 | op + w0 + w1 |

前缀语义：`w` = write, `r` = read, `W` = word, `DW` = dword, `QW` = qword。

---

## 指令集概览 (asEBCInstr, 213 条)

### 按功能分类

| 类别 | 指令范围 | 示例 |
|------|---------|------|
| 栈操作 | 0-5 | `PopPtr`, `PshGPtr`, `PshC4`, `PshV4`, `PSF`, `SwapPtr` |
| 控制流 | 9-17, 57 | `CALL`, `RET`, `JMP`, `JZ/JNZ`, `JS/JNS`, `JP/JNP`, `JMPP` |
| 比较/测试 | 18-23, 50-56 | `TZ/TNZ`, `TS/TNS`, `TP/TNP`, `CMPd/u/f/i` |
| 算术(32 位) | 24-38, 115-135 | `NEGi/f/d`, `INC/DEC`, `ADD/SUB/MUL/DIV/MODi/f/d` |
| 位运算 | 39-45 | `BNOT`, `BAND`, `BOR`, `BXOR`, `BSLL`, `BSRL`, `BSRA` |
| 类型转换 | 101-114, 140-155 | `iTOf`, `fTOi`, `dTOi64`, `i64TOf` |
| 对象操作 | 64-75 | `ALLOC`, `FREE`, `LOADOBJ`, `STOREOBJ`, `REFCPY` |
| 变量操作 | 77-97 | `SetV4/V8`, `CpyVtoV4`, `CpyVtoR4`, `LDG/LDV` |
| 64 位操作 | 145-172 | 完整的 64 位整数运算指令集 |
| 幂运算 | 193-199 | `POWi/u/f/d/di/i64/u64` |
| UE 扩展 | 201-212 | `FinConstruct`, `DestructScript`, `CopyScript`, `TrackRef` 等 |

### 临时标记指令 (251-255)

仅在编译期使用，不输出到最终字节码：

| 指令 | 值 | 用途 |
|------|------|------|
| `VarDecl` | 251 | 变量声明标记 |
| `Block` | 252 | 作用域块标记 |
| `ObjInfo` | 253 | 对象变量信息 |
| `LINE` | 254 | 行号标记（→ SUSPEND 或删除） |
| `LABEL` | 255 | 跳转目标标签 |

---

## asCByteCode 构造器

### 数据结构

字节码在编译期表示为**双向链表**：

```cpp
class asCByteCode {
    asCByteInstruction *first;  // 链表头
    asCByteInstruction *last;   // 链表尾
    int largestStackUsed;       // 最大栈使用量
};

class asCByteInstruction {
    asEBCInstr op;      // 操作码
    asQWORD arg;        // 最多 8 字节参数
    short wArg[3];      // 三个 WORD 参数（寄存器操作数）
    int size;           // 指令大小（DWORD 单位）
    int stackInc;       // 栈增量
    asCByteInstruction *next, *prev;  // 双向链表
    bool marked;        // PostProcess 可达标记
    int stackSize;      // 该位置的栈深度
};
```

### Finalize 四阶段管线

```cpp
void asCByteCode::Finalize(const asCArray<int> &tempVariableOffsets)
{
    temporaryVariables = &tempVariableOffsets;
    PostProcess();           // ★ 阶段 1: 验证 + 栈分析 + 删除不可达代码
    Optimize();              // ★ 阶段 2: 全局窥孔优化
    ResolveJumpAddresses();  // ★ 阶段 3: 标签 → 偏移量
    ExtractLineNumbers();    // ★ 阶段 4: LINE → SUSPEND 转换
}
```

### PostProcess — 验证与栈分析 (行 2064-2143+)

1. **标记所有指令为未访问**（`marked=false`, `stackSize=-1`）
2. **从 first 开始控制流分析**：沿执行路径标记可达指令，累加 `stackInc` 计算栈深度
3. **跟踪最大栈使用量**（`largestStackUsed`，ALLOC 特殊处理需 +2）
4. **条件跳转**（JZ/JNZ/JS/JNS/JP/JNP）将两条路径都加入检查列表
5. **JMPP**（跳转表）遍历后续 JMP 序列获取所有目标
6. **删除未标记（不可达）的指令**

### Optimize — 全局窥孔优化 (行 1148-1243)

单遍前向遍历，识别可简化的指令对/序列：

| 模式 | 优化 |
|------|------|
| `PopPtr` + `RET` | 删除 `PopPtr`（RET 自行恢复栈） |
| `SUSPEND` + `SUSPEND` | 合并为一个 |
| `LINE` + `LINE` | 合并为一个 |
| `JMP +0` | 删除（跳到下一条） |
| `SUSPEND`+`JitEntry`+`SUSPEND` | 删除前两条 |

### OptimizeLocally — 局部窥孔优化 (行 619-1147)

在每个子表达式编译后调用（非全局），逆向遍历：

1. **`RemoveUnusedValue()`**：删除写入但从未读取的临时变量指令
2. **`PostponeInitOfTemp()`**：延迟临时变量初始化以便后续合并
3. **`SwapPtr` 消除**：如果两条 push 指令可交换，删除 SwapPtr

### ExtractLineNumbers (行 1517-1562)

将 `LINE` 伪指令转换为运行时行为：

- **`ep.buildWithoutLineCues == false`**：`LINE` → `SUSPEND`（保留调试断点）
- **`ep.buildWithoutLineCues == true`**：删除 `LINE`（性能优先）

行号信息存储到 `lineNumbers[]` 和 `sectionIdxs[]` 数组。

---

## 序列化 (asCReader / asCWriter)

`as_restore.h/.cpp` 实现字节码的二进制保存/加载：

### ReadByteCode (行 2645-2700+)

```text
ReadByteCode(func)
==================
1. ReadEncodedUInt() → 指令数量
2. 预分配 byteCode 数组
3. 逐指令读取：
   - 读 1 字节 opcode
   - 查 asBCInfo[opcode].type → 确定指令大小
   - 按指令类型读取参数
   - NO_ARG: 仅 opcode
   - W_ARG/wW_ARG/rW_ARG: opcode + WORD
   - rW_DW_ARG: opcode + WORD + DWORD
   - QW_ARG: opcode + WORD + QWORD
   ...
```

### Output — 链表到数组 (行 1987-2062)

将双向链表序列化为紧凑的 `asDWORD[]` 数组：

```cpp
*(asBYTE*)ap = asBYTE(instr->op);      // 字节 0: opcode
*(((asBYTE*)ap)+1) = 0;                  // 字节 1: always zero
// 按指令类型写入参数...
```

---

## 小结

- 字节码采用 1 字节 opcode + 0-3 DWORD 参数的变长格式，共 213 条指令 + 5 条临时标记
- 编译期表示为双向链表（`asCByteInstruction`），最终 `Output()` 到紧凑数组
- Finalize 四阶段管线：栈验证 → 窥孔优化 → 跳转解析 → 行号提取
- 两级优化：`OptimizeLocally`（子表达式级别）+ `Optimize`（函数级别）
- UE Fork 添加了 12 条专用指令（201-212）：`FinConstruct`、`DestructScript`、`TrackRef` 等
