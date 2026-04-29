# AS_CallingConventions — 调用约定

> **所属模块**: AS_（AngelScript 引擎内核族）
> **关注层面**: 16 种内部调用约定、平台汇编桥接、asSSystemFunctionInterface
> **关键源码**:
> `ThirdParty/angelscript/source/as_callfunc.h` (4 KB) / `.cpp` (25 KB)
> · `as_callfunc_x64_msvc.cpp` / `as_callfunc_arm64.cpp` 等 20+ 平台文件
> **关联文档**:
> `AS_VirtualMachine.md` — VM 通过 `CallSystemFunction` 调用本地函数
> · `AS_TypeRegistration.md` — `DetectCallingConvention` 在注册时调用

---

## 概览

AngelScript 需要在运行时从字节码调用宿主 C++ 函数。由于不同平台的 ABI（Application Binary Interface）差异巨大，引擎定义了 16 种内部调用约定（`internalCallConv`），并为每种约定在每个目标平台提供汇编桥接代码。核心流程：注册时 `DetectCallingConvention()` → 执行时 `CallSystemFunction()`。

---

## 16 种内部调用约定

```cpp
enum internalCallConv {
    ICC_GENERIC_FUNC,                    // asCALL_GENERIC 全局函数
    ICC_GENERIC_FUNC_RETURNINMEM,        // (未使用)
    ICC_CDECL,                           // __cdecl 全局函数
    ICC_CDECL_RETURNINMEM,               // __cdecl 返回大对象
    ICC_STDCALL,                         // __stdcall
    ICC_STDCALL_RETURNINMEM,
    ICC_THISCALL,                        // C++ 成员函数
    ICC_THISCALL_RETURNINMEM,
    ICC_VIRTUAL_THISCALL,                // 虚函数
    ICC_VIRTUAL_THISCALL_RETURNINMEM,
    ICC_CDECL_OBJLAST,                   // 第一个参数在最后传 obj
    ICC_CDECL_OBJLAST_RETURNINMEM,
    ICC_CDECL_OBJFIRST,                  // 第一个参数在最前传 obj
    ICC_CDECL_OBJFIRST_RETURNINMEM,
    ICC_GENERIC_METHOD,                  // asCALL_GENERIC 方法
    ICC_GENERIC_METHOD_RETURNINMEM,      // (未使用)
    ICC_THISCALL_OBJLAST,               // thiscall + 额外 obj 在最后
    ICC_THISCALL_OBJLAST_RETURNINMEM,
    ICC_VIRTUAL_THISCALL_OBJLAST,
    ICC_VIRTUAL_THISCALL_OBJLAST_RETURNINMEM,
    ICC_THISCALL_OBJFIRST,              // thiscall + 额外 obj 在最前
    ICC_THISCALL_OBJFIRST_RETURNINMEM,
    ICC_VIRTUAL_THISCALL_OBJFIRST,
    ICC_VIRTUAL_THISCALL_OBJFIRST_RETURNINMEM,
};
```

### asSSystemFunctionInterface

每个已注册的系统函数都有一个 `asSSystemFunctionInterface` 描述其调用方式：

```cpp
struct asSSystemFunctionInterface {
    asFUNCTION_t         func;          // 函数指针
    asMETHOD_t           method;        // 方法指针（联合体）
    int                  baseOffset;    // 虚函数表偏移
    internalCallConv     callConv;      // 内部调用约定
    int                  scriptReturnSize; // 脚本返回值大小
    int                  paramSize;     // 参数总大小
    asEFirstParamMetaData passFirstParamMetaData; // 首参元数据
    asFunctionCaller     caller;        // UE FunctionCaller
    asCArray<SClean>     cleanOps;      // 参数清理操作
};
```

---

## 核心函数

### DetectCallingConvention (注册时)

将应用层的 `asCALL_CDECL`/`asCALL_THISCALL` 等公共约定映射到内部约定：

```text
DetectCallingConvention(isMethod, funcPtr, callConv, auxiliary, caller, internal)
================================================================================
1. 提取函数指针 (union trick: asFUNCTION_t → asPWORD)
2. 按 callConv 分发:
   asCALL_CDECL       → ICC_CDECL
   asCALL_STDCALL     → ICC_STDCALL
   asCALL_THISCALL    → ICC_THISCALL 或 ICC_VIRTUAL_THISCALL
   asCALL_GENERIC     → ICC_GENERIC_FUNC 或 ICC_GENERIC_METHOD
   asCALL_CDECL_OBJLAST → ICC_CDECL_OBJLAST
   ...
3. FunctionCaller 路径: 如果 caller != nullptr，优先使用 caller
```

### CallSystemFunction (执行时)

VM 的 `CALLSYS` 指令最终调用此函数，按调用约定分发到平台特定实现。

### PrepareSystemFunction (引擎准备时)

在 `PrepareEngine()` 中调用，为每个系统函数预计算参数布局（大小、偏移、清理操作）。

---

## 平台汇编桥

| 平台 | 文件 | 大小 |
|------|------|------|
| x64 MSVC | `as_callfunc_x64_msvc.cpp` + `.asm` | 12 KB |
| x64 GCC | `as_callfunc_x64_gcc.cpp` | 16 KB |
| x86 | `as_callfunc_x86.cpp` | 46 KB |
| ARM64 | `as_callfunc_arm64.cpp` + `.S` (GCC/MSVC/Xcode) | 32 KB |
| ARM32 | `as_callfunc_arm.cpp` + `.S` | 55 KB |
| RISC-V 64 | `as_callfunc_riscv64.cpp` + `.S` | 18 KB |

每个平台实现的核心任务：按 ABI 规则将参数从 AngelScript 栈布局转换为本地调用栈布局，调用函数，再将返回值转回 AS 格式。

---

## 泛型调用约定 (asCGeneric)

`asCALL_GENERIC` 是跨平台的兜底方案——所有参数通过 `asCGeneric` 适配器传递：

```cpp
class asCGeneric : public asIScriptGeneric {
    void   *currentObject;    // this 指针
    asCScriptFunction *sysFunction;
    asCScriptEngine *engine;
    // 参数通过 GetArgByte/Word/DWord/QWord/Float/Object 访问
    // 返回值通过 SetReturnByte/DWord/Float/Object 设置
};
```

性能低于本地调用约定（额外间接层），但保证可移植。

---

## 小结

- AngelScript 定义 16+ 种内部调用约定，覆盖 cdecl/stdcall/thiscall + OBJFIRST/OBJLAST 组合
- 注册时通过 `DetectCallingConvention` 映射，执行时通过平台汇编桥转发
- 每个平台需要独立的汇编桥实现（x64/ARM64/x86/RISC-V 等 20+ 文件）
- `asCALL_GENERIC` 是跨平台兜底方案，通过 `asCGeneric` 适配器传参
- UE Fork 增加了 `asFunctionCaller` 路径，绕过标准调用约定直接使用 UE 绑定
