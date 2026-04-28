# Hash / 元数据辅助子系统

> **所属模块**: 运行时支撑子系统 → Hash / Metadata Auxiliary
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Hash/xxhash.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Hash/xxhash.inl`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/`, `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md`

这一节最容易被低估，因为 `Hash/` 目录看上去很薄。但当前仓库里它的职责并不是“顺便放个第三方哈希头文件”，而是给脚本模块、元数据和缓存身份提供稳定的哈希基础设施。

## 3.7.1 Hash 子系统承担的定位与用途

- 当前 `Hash/` 以 xxHash 为基础，为字符串、元数据键和缓存身份提供快速哈希能力
- 这类哈希能力并不直接暴露给最终脚本用户，但会被模块、符号表和缓存判定路径依赖
- 因此它属于“低可见度、高基础性”的辅助子系统

## 3.7.2 关键数据结构与哈希边界

- 当前实现采用 xxHash 0.6.5 头文件内联模式
- 32 位和 64 位接口都存在，足以覆盖大多数模块标识和缓存键场景
- 边界上它更适合做 identity / lookup / cache key，不负责更高层的业务语义判定

## 3.7.3 与预处理、绑定、缓存的协作点

- 预处理和模块描述符需要稳定的代码/模块哈希来识别变化和缓存有效性
- 绑定数据库和部分缓存路径也需要快速且稳定的键生成
- 因此 Hash 子系统虽然薄，但在预处理、缓存、符号查找等多个子系统之间形成了公共底座

## 当前体系最值得记住的点

- `Hash/` 目录虽然小，但承担的是身份与缓存层的基础设施职责
- 它服务的是多个上层子系统之间的公共需求，而不是单一功能点
- 在架构地图里，它应该被看作辅助底座，而不是孤立第三方代码

## 小结

- Hash 子系统为模块、元数据和缓存判定提供统一哈希基础
- 当前边界是“做稳定 identity / lookup”，而不是承担高层业务逻辑
- 它和预处理、绑定、缓存协作紧密，是典型的小目录大作用子系统
