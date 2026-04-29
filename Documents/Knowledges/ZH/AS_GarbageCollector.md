# AS_GarbageCollector — GC 策略

> **所属模块**: AS_（AngelScript 引擎内核族）
> **关注层面**: 增量式循环引用检测、新旧对象双池、状态机驱动的 GC 算法
> **关键源码**:
> `ThirdParty/angelscript/source/as_gc.h` (5 KB, 150 行) / `.cpp` (30 KB)
> **关联文档**:
> `AS_ScriptEngine.md` — GC 入口 (`engine.gc`)
> · `AS_ObjectLifecycle.md` — GC 行为注册（gcEnumReferences 等）
> · `AS_TypeRegistration.md` — asOBJ_GC 标志位

---

## 概览

AngelScript 的 GC 专门解决**循环引用**问题——普通引用计数无法处理的场景。它采用**增量式标记-清除**算法，通过**新旧对象双池**和**两阶段状态机**（销毁 + 检测）实现分帧执行，避免长时间停顿。

---

## 核心数据结构

```cpp
class asCGarbageCollector {
    // 双池
    asCArray<asSObjTypePair> gcNewObjects;   // 新对象池
    asCArray<asSObjTypePair> gcOldObjects;   // 旧对象池
    
    // 引用关系图（检测阶段临时构建）
    asCMap<void*, asSIntTypePair> gcMap;
    
    // 活跃对象列表
    asCArray<void*> liveObjects;
    
    // 状态机状态
    egcDestroyState destroyNewState, destroyOldState;
    egcDetectState detectState;
    
    // 统计
    asUINT numDestroyed, numNewDestroyed, numDetected, numAdded;
};

struct asSObjTypePair {
    void *obj;
    asCObjectType *type;
    asUINT seqNbr;       // 序列号（用于新→旧迁移判断）
};
```

---

## 两阶段状态机

### 阶段 1: 销毁 (DestroyGarbage)

```text
egcDestroyState:
  destroyGarbage_init → destroyGarbage_loop → destroyGarbage_haveMore
```

遍历新对象池/旧对象池，释放引用计数为 0 的对象。分 `DestroyNewGarbage()` 和 `DestroyOldGarbage()` 两个独立状态机。

### 阶段 2: 检测循环引用 (IdentifyGarbageWithCyclicRefs)

```text
egcDetectState:
  clearCounters_init                 // 初始化
    → clearCounters_loop             // 清零内部计数器
  → buildMap_init                    // 构建引用图
    → buildMap_loop                  // 遍历旧对象，gcEnumReferences 填充 gcMap
  → countReferences_init             // 计数引用
    → countReferences_loop           // 统计 gcMap 中每个对象的内部引用数
  → detectGarbage_init               // 检测垃圾
    → detectGarbage_loop1            // 标记内部引用数 == 总引用数的对象为疑似垃圾
    → detectGarbage_loop2            // 从非垃圾对象出发，取消所有可达对象的垃圾标记
  → verifyUnmarked_init              // 验证
    → verifyUnmarked_loop            // 确认标记为垃圾的对象确实不可达
  → breakCircles_init                // 打破循环
    → breakCircles_loop              // 对确认的垃圾调用 gcReleaseAllReferences
    → breakCircles_haveGarbage       // 有垃圾被释放，需要重新检测
```

---

## GC 算法核心思路

```text
循环引用检测原理
================
对象 A 和 B 互相引用：
  A.refCount = 1 (来自 B)
  B.refCount = 1 (来自 A)

Step 1: buildMap — 枚举每个对象的引用目标
  gcMap[A] = {count: 0}
  gcMap[B] = {count: 0}
  枚举 A 的引用 → B 在 gcMap 中 → gcMap[B].count++
  枚举 B 的引用 → A 在 gcMap 中 → gcMap[A].count++

Step 2: countReferences — 比较内部引用数与总引用数
  A: gcMap.count(1) == refCount(1) → 疑似垃圾 ★
  B: gcMap.count(1) == refCount(1) → 疑似垃圾 ★

Step 3: detectGarbage — 从非垃圾对象传播可达性
  (如果有外部引用持有 A 或 B，会取消垃圾标记)
  没有外部引用 → A 和 B 确认为循环垃圾

Step 4: breakCircles — 打破循环
  对 A 调用 gcReleaseAllReferences → A 释放对 B 的引用 → B.refCount = 0 → B 被销毁
```

---

## 新旧对象双池

- **新对象池** (`gcNewObjects`): 刚添加到 GC 的对象
- **旧对象池** (`gcOldObjects`): 经过一轮扫描后存活的对象

对象从新池迁移到旧池（`MoveObjectToOldList`）的时机：
- 当新池中的对象在一轮销毁后仍存活
- `MoveAllObjectsToOldList()` 在全量 GC 时调用

**设计动机**：新创建的对象可能很快被释放（代际假设），先在新池中检查可以避免不必要的引用图构建。

---

## GarbageCollect 入口

```cpp
int asCGarbageCollector::GarbageCollect(asDWORD flags, asUINT iterations)
```

| flags | 行为 |
|-------|------|
| `asGC_FULL_CYCLE` | 执行完整的销毁+检测周期 |
| `asGC_ONE_STEP` | 执行一个增量步骤 |
| `asGC_DESTROY_GARBAGE` | 仅执行销毁阶段 |
| `asGC_DETECT_GARBAGE` | 仅执行检测阶段 |

`iterations` 控制增量步骤的迭代次数，实现分帧执行。

---

## 与 UE 的交互

在 UE 插件层，GC 通过 `FAngelscriptEngine::Tick()` 中的增量调用驱动：

```text
FAngelscriptEngine::Tick()
  → asEngine->GarbageCollect(asGC_ONE_STEP, N)  // 每帧 N 步
```

`ep.autoGarbageCollect` 控制是否在编译后自动触发 GC。`ep.disableScriptClassGC` 可禁用脚本类的 GC 跟踪。

---

## 小结

- GC 仅处理循环引用，普通引用通过引用计数管理
- 采用增量式标记-清除，通过 13 状态的状态机分帧执行
- 新旧对象双池实现代际优化
- 核心算法：枚举内部引用 → 比较内部引用数与总引用数 → 标记疑似垃圾 → 传播可达性 → 打破循环
- GC 行为（gcEnumReferences 等）由宿主在 RegisterObjectBehaviour 时注册
