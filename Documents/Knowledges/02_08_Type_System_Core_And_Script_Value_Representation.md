# 类型系统核心与脚本值表达

> **所属模块**: 类型系统与生成链路 → `FAngelscriptType` / `FAngelscriptTypeUsage`
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PODType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_StructType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_AngelscriptArguments.h`

这一节要讲清楚的是：当前插件的类型系统不是“存一个脚本类型名”那么简单，而是在搭一层跨 UE 属性系统、AngelScript 类型系统和调试/GC/绑定场景都能复用的桥。

## 2.8.1 `FAngelscriptType` / `FAngelscriptTypeUsage` 的桥接职责

- `FAngelscriptType` 是类型能力接口：匹配属性、创建属性、构造/析构/复制值、设置参数、提取返回值都从这里出发
- `FAngelscriptTypeUsage` 是具体用法描述：记录是否引用、是否 const、有哪些子类型、底层关联的是 `asITypeInfo` 还是 `FProperty`
- 类型数据库通过 `GetByClass`、`GetByProperty`、`GetByAngelscriptTypeName` 等接口，在 UE 和脚本两边建立查找桥

因此 `Type` 和 `TypeUsage` 的分层，本质上是在拆“类型能力”和“类型在当前语境下怎么被用”。

## 2.8.2 属性绑定、GC 引用信息与调试值提取

- 类型对象负责创建 `FProperty`，因此它直接参与类生成、属性绑定和脚本值布局
- `Helper_StructType` 这类结构类型辅助器会同时暴露 debugger value / scope / member 访问能力
- GC、引用信息和调试值提取都依赖这层统一的类型桥接，否则这些能力会散落到各个绑定点里

也就是说，类型系统不是“只给编译器看”的内部设施，而是同时服务：

- 属性生成
- 参数封送
- GC 引用描述
- 调试值观察

## 2.8.3 类型系统如何服务 ClassGenerator / Debugger / Bind

- **ClassGenerator**：依赖类型系统把脚本类型翻译成 UE 属性与布局信息
- **Debugger**：依赖类型系统获取可显示值、子成员和作用域信息
- **Bind**：依赖类型系统处理参数、返回值和 POD / Struct / UObject 差异

这说明类型系统是一个横向基础层，而不是某个子系统的局部实现。

## 当前体系最值得记住的点

- `FAngelscriptType` 描述能力，`FAngelscriptTypeUsage` 描述使用方式
- 类型系统同时服务属性、参数、调试和 GC，不只是类型名映射器
- `Helper_PODType`、`Helper_StructType`、参数读写辅助模板，构成了不同值语义的具体落地实现

## 小结

- 当前类型系统是 UE 属性系统与 AngelScript 值语义之间的通用桥层
- 它横向支撑类生成、绑定、调试和 GC
- 不理解 `Type / TypeUsage` 分层，就很难读懂插件里各种脚本值为什么能以统一方式被创建、调用和观察
