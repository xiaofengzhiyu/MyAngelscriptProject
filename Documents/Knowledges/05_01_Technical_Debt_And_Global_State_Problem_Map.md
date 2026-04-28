# 技术债与全局状态问题地图

> **所属模块**: 工程治理与演进路线 → Technical Debt / Global State Map
> **关键源码**: `Documents/Guides/TechnicalDebtInventory.md`, `Documents/Guides/GlobalStateContainmentMatrix.md`, `Documents/Plans/Plan_TechnicalDebtRefresh.md`

这一节要做的，不是再抄一遍 debt inventory，而是把技术债条目重新解释成“和哪些子系统相连、哪些值得写成架构文章、哪些只该保留在治理台账里”。也就是说，**技术债清单要被重新投影成一张子系统问题地图。**

## 5.1.1 技术债条目如何映射回具体子系统

- `TechnicalDebtInventory.md` 已经按文件和主题记录热点，但这些热点需要反向映射回 `ClassGenerator`、`Preprocessor`、`Debugger`、`Bind`、`Coverage` 等真实子系统
- 当某个问题横跨多个子系统时，它更接近架构议题；当问题只属于具体实现细节时，它更适合留在治理清单中
- 因此“问题在哪个文件里”不是终点，“它属于哪条子系统主线”才是更重要的治理视角

## 5.1.2 全局状态问题的主题聚类

- 当前全局状态问题至少可以聚成：编译/类生成路径、世界上下文路径、测试包装器路径、调试/工具路径
- `GlobalStateContainmentMatrix.md` 已经给出了 containment 分级，这相当于问题聚类后的风险分层图
- 这种聚类方式比单纯统计 `Get()` 次数更有用，因为它直接决定后续治理策略

## 5.1.3 哪些问题适合做架构文章，哪些只保留在 debt inventory

- 适合做架构文章的：会影响多个子系统边界、调用流或长期设计判断的问题，例如全局状态治理、ThirdParty fork 边界
- 适合留在 debt inventory 的：局部错误信息、低复杂度修补、纯实现细节问题
- 换句话说，架构文章负责解释“为什么这是一条主线问题”，debt inventory 负责追踪“这件事还没完全收敛”

## 当前问题地图最值得记住的点

- debt inventory 是治理数据库，不是架构主线全文
- 问题聚类和子系统映射能帮助识别哪些技术债值得升级为架构专题
- 全局状态问题是连接实现债务与架构治理的最明显桥点

## 小结

- 当前技术债条目需要通过子系统视角重新组织，才能服务后续架构文档
- 全局状态问题是最典型的跨子系统治理主题
- 架构文章与 debt inventory 应该互相引用，但不应互相替代
