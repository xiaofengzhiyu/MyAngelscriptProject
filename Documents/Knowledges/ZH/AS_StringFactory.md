# AS_StringFactory — 字符串工厂

> **所属模块**: AS_（AngelScript 引擎内核族）
> **关注层面**: asCString 内部实现、asIStringFactory 注册机制、字符串字面量创建链路
> **关键源码**:
> `ThirdParty/angelscript/source/as_string.h` / `.cpp` (6 KB / 12 KB) — asCString
> · `ThirdParty/angelscript/source/as_string_util.h` / `.cpp` (2 KB / 8 KB) — 工具函数
> · `Core/angelscript.h` — asIStringFactory 接口
> · `as_scriptengine.cpp` L3189-3212 — RegisterStringFactory
> **关联文档**:
> `AS_ScriptEngine.md` — stringType / stringFactory 成员
> · `AS_TypeRegistration.md` — RegisterStringFactory API

---

## 概览

AngelScript 的字符串处理分两层：**引擎内部**使用自实现的 `asCString`（不依赖 STL），**脚本用户**使用宿主通过 `RegisterStringFactory` 注册的自定义字符串类型。字符串工厂（`asIStringFactory`）是连接字符串字面量和宿主字符串类型的桥梁。

---

## asCString — 引擎内部字符串

```cpp
class asCString {
    char *buffer;       // 内部缓冲区（保证空终止）
    size_t length;      // 字符串长度（不含终止符）
    size_t capacity;    // 已分配容量
};
```

### 设计特点

- **不使用引用计数**：每次赋值/拷贝都是深拷贝
- **不使用共享内存**：避免多线程问题
- **保证空终止**：`buffer[length] == '\0'`
- **使用 `FMemory::Malloc`/`Free`**（UE Fork）

### 关键操作

| 方法 | 复杂度 | 说明 |
|------|--------|------|
| `Allocate(len, keepData)` | O(n) | 分配/重分配缓冲区 |
| `Concatenate(str, len)` | O(n) | 追加字符串 |
| `SubString(start, len)` | O(n) | 提取子串 |
| `FindLast(str)` | O(n*m) | 从后向前查找 |
| `Format(fmt, ...)` | O(n) | printf 风格格式化 |

---

## asIStringFactory — 字符串工厂接口

```cpp
class asIStringFactory {
    virtual const void *GetStringConstant(const char *data, asUINT length) = 0;
    virtual int ReleaseStringConstant(const void *str) = 0;
    virtual int GetRawStringData(const void *str, char *data, asUINT *length) const = 0;
};
```

宿主必须实现此接口并通过 `RegisterStringFactory` 注册。

### RegisterStringFactory (行 3189-3212)

```cpp
int asCScriptEngine::RegisterStringFactory(const char *datatype, asIStringFactory *factory)
{
    // 解析数据类型（如 "string"）
    asCDataType dt = bld.ParseDataType(datatype, ...);
    // 验证：不能是引用或 handle
    if (dt.IsReference() || dt.IsObjectHandle()) return asINVALID_TYPE;
    // 所有字符串字面量视为 const
    dt.MakeReadOnly(true);
    // 存储
    stringType = dt;
    stringFactory = factory;
}
```

### 字符串字面量创建链路

```text
脚本代码: "hello world"
  ↓ Tokenizer: ttStringConstant
  ↓ Parser: snConstant 节点
  ↓ Compiler: CompileExpressionValue()
    → ProcessStringConstant()     // 处理转义序列
    → engine->stringFactory->GetStringConstant(data, len)  // ★ 调用工厂
    → 生成 STR 指令引用常量
  ↓ 运行时: STR 指令加载字符串常量地址
```

---

## as_string_util — 字符串工具

提供数字解析/格式化的跨平台工具函数：

| 函数 | 用途 |
|------|------|
| `asStringScanUInt64()` | 解析整数常量（支持多进制） |
| `asStringScanDouble()` | 解析浮点常量 |
| `asStringFormat()` | 格式化输出 |
| `asStringCopy()` | 安全字符串拷贝 |

---

## 小结

- `asCString` 是引擎内部的简单字符串类，无引用计数、无 STL 依赖
- 脚本中的字符串通过 `asIStringFactory` 接口委托给宿主管理
- `RegisterStringFactory` 注册宿主字符串类型，所有字面量通过 `GetStringConstant` 创建
- 字符串常量在编译期通过工厂创建，运行时通过 `STR` 指令引用
