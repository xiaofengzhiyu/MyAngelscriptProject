# AS_ObjectLifecycle — 脚本对象生命周期

> **所属模块**: AS_（AngelScript 引擎内核族）
> **关注层面**: asCScriptObject 构造/拷贝/销毁、引用计数、asSTypeBehaviour 行为表
> **关键源码**:
> `ThirdParty/angelscript/source/as_scriptobject.h` / `.cpp` (3 KB / 31 KB)
> · `ThirdParty/angelscript/source/as_scriptfunction.h` / `.cpp` (16 KB / 49 KB)
> · `ThirdParty/angelscript/source/as_objecttype.h` — asSTypeBehaviour 定义
> **关联文档**:
> `AS_TypeRegistration.md` — 行为注册 API
> · `AS_GarbageCollector.md` — 循环引用处理
> · `AS_VirtualMachine.md` — ALLOC/FREE 指令

---

## 概览

AngelScript 中每个脚本声明的类实例都是 `asCScriptObject`。对象的创建、拷贝和销毁通过**行为表**（`asSTypeBehaviour`）中注册的函数 ID 驱动。引擎区分两大类对象：**值类型**（栈分配，通过 construct/destruct 管理）和**引用类型**（堆分配，通过 factory/addref/release 管理）。

---

## asSTypeBehaviour — 行为表

```cpp
struct asSTypeBehaviour {
    int factory;              // 默认工厂函数 ID (REF 类型)
    int listFactory;          // 初始化列表工厂
    int copyfactory;          // 拷贝工厂
    int construct;            // 默认构造函数 ID (VALUE 类型)
    int copyconstruct;        // 拷贝构造函数
    int destruct;             // 析构函数
    int copy;                 // 赋值运算符 (opAssign)
    int addref;               // 增加引用计数
    int release;              // 减少引用计数
    int templateCallback;     // 模板实例化回调

    // GC 行为
    int gcGetRefCount;        // 获取引用计数
    int gcSetFlag;            // 设置 GC 标记
    int gcGetFlag;            // 获取 GC 标记
    int gcEnumReferences;     // 枚举引用
    int gcReleaseAllReferences; // 释放所有引用

    int getWeakRefFlag;       // 弱引用标志

    asCArray<int> factories;      // 所有工厂函数
    asCArray<int> constructors;   // 所有构造函数
};
```

每个 `int` 存储的是 `scriptFunctions[]` 中的函数 ID。

---

## 对象生命周期阶段

### 引用类型 (asOBJ_REF)

```text
创建: ALLOC 指令
  → engine->CallAlloc(objType)     // 分配内存
  → CallSystemFunction(factory)    // 调用工厂函数
  → addref (初始 refcount = 1)

使用: REFCPY / RefCpyV 指令
  → addref on new reference
  → release on old reference

销毁: FREE 指令
  → release()
  → if refcount == 0: 析构 + 释放内存
```

### 值类型 (asOBJ_VALUE)

```text
创建: 在栈或堆上分配内存
  → CallSystemFunction(construct)  // 调用构造函数
  → 或: CallSystemFunction(copyconstruct)  // 拷贝构造

赋值: 
  → CallSystemFunction(copy)       // opAssign

销毁: 
  → CallSystemFunction(destruct)   // 调用析构函数
  → 释放内存
```

---

## asCScriptObject

```cpp
class asCScriptObject : public asIScriptObject {
    asCScriptObject(asCObjectType *objType, bool doInitialize = true);
    asCScriptObject& PerformCopy(asCScriptObject* Other, ...);
    void CopyStruct(asCScriptObject* Source, asCObjectType* Type);
};
```

- 构造函数根据 `objType` 的属性列表初始化所有成员
- `PerformCopy` 逐属性拷贝，调用子对象的拷贝行为
- `CopyStruct` 用于值类型的浅拷贝

### 弱引用

`asCLockableSharedBool` 实现弱引用标志：

```cpp
class asCLockableSharedBool : public asILockableSharedBool {
    asCAtomic refCount;
    bool value;           // true = 对象仍存活
    DECLARECRITICALSECTION(lock)
};
```

---

## asCScriptFunction

```cpp
class asCScriptFunction : public asIScriptFunction {
    asCString name;
    asCDataType returnType;
    asCArray<asCDataType> parameterTypes;
    asCArray<asCString> parameterNames;
    asCArray<asETypeModifiers> inOutFlags;
    asCArray<asCString*> defaultArgs;
    
    asCObjectType *objectType;     // 所属对象类型
    asCModule *module;             // 所属模块
    
    struct ScriptFunctionData {
        asCArray<asDWORD> byteCode;     // 字节码
        asCArray<asCTypeInfo*> objVariableTypes;
        asCArray<int> objVariablePos;
        int variableSpace;              // 局部变量空间大小
        int stackNeeded;                // 栈需求
    } *scriptData;                 // 仅脚本函数有此数据
    
    asSSystemFunctionInterface *sysFuncIntf;  // 仅系统函数
    int vfTableIdx;                // 虚函数表索引
    asSFunctionTraits traits;      // 函数特征标记
};
```

---

## UE Fork 扩展

| 位置 | 修改 |
|------|------|
| `as_scriptfunction.h` L64-66 | `onHeap` 字段、`asSTryCatchInfo` 结构体 |
| `as_scriptobject.h` L56-57 | `PerformCopy` / `CopyStruct` 方法 |
| 字节码指令 | `FinConstruct`(201)、`DestructScript`(202)、`CopyScript`(203) |

---

## 小结

- 对象生命周期由行为表 (`asSTypeBehaviour`) 中的函数 ID 驱动
- 引用类型通过 factory/addref/release 管理，值类型通过 construct/destruct 管理
- `asCScriptObject` 是脚本对象的运行时表示，支持逐属性拷贝
- `asCScriptFunction` 是函数的统一容器，通过 `scriptData` 或 `sysFuncIntf` 区分脚本/系统函数
- UE Fork 添加了专用字节码指令处理脚本对象的构造完成/析构/拷贝
