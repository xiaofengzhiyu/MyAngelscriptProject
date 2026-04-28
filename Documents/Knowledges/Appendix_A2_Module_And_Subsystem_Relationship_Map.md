# 附录 A.2 模块 / 子系统关系图

> **所属模块**: 附录 → Module / Subsystem Relationship Map
> **关键源码**: `Plugins/Angelscript/Angelscript.uplugin`, `Plugins/Angelscript/Source/AngelscriptRuntime/`, `Plugins/Angelscript/Source/AngelscriptEditor/`, `Plugins/Angelscript/Source/AngelscriptTest/`, `Plugins/Angelscript/Source/AngelscriptUHTTool/`

## 总体关系图

```text
Host Project
  └─ Plugins/Angelscript
      ├─ AngelscriptRuntime
      │   ├─ Core / Engine
      │   ├─ ClassGenerator
      │   ├─ Preprocessor
      │   ├─ Binds / FunctionLibraries / FunctionCallers
      │   ├─ BaseClasses / Type System
      │   ├─ StaticJIT / Debugging / Dump / CodeCoverage
      │   └─ Hash / ThirdParty AngelScript
      ├─ AngelscriptEditor
      │   ├─ Directory Watcher
      │   ├─ Reload Helper / Blueprint Impact
      │   ├─ Content Browser / Navigation / Menus
      │   └─ Editor-only FunctionLibraries / Tools
      ├─ AngelscriptTest
      │   ├─ Shared Helpers / Fixtures
      │   ├─ Native / Learning / Validation
      │   ├─ Topic Tests (Actor, HotReload, Debugger, Dump ...)
      │   └─ Console Commands / Automation Entrypoints
      └─ AngelscriptUHTTool
          ├─ Header Signature Resolver
          ├─ Function Table Exporter
          └─ Code Generator / Direct-Bind Fallback
```

## 关系解释

- Runtime 是主能力层
- Editor 负责 editor-side 接缝，不反向吞并 Runtime 主语
- Test 负责验证地图和命令入口
- UHTTool 负责生成链，不直接承担运行时状态

## 小结

- 这张图可以作为所有架构专题的最小骨架图
- 读某个专题前，先确认它落在哪个模块和哪条子系统链上
