# Syntax_AccessSpecifiers — `access` 自定义访问修饰符

> **所属前缀**: Syntax_（语法机制与实现原理族）
> **关注层面**: AS Fork 自定义关键字 `access` 的解析、登记、编译期检查与跨模块语义；不深入 UE 反射注册（那是 `Type_ClassGeneration.md`），不写"怎么用"（那是 `Guide_*`）。
> **关键源码**:
> `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_tokendef.h` — `ttAccess` token 与 `READONLY/EDITDEFAULTS/INHERITED_TOKEN` 字符串
> · `ThirdParty/angelscript/source/as_parser.cpp` — `IsAccessDecl` / `ParseAccessDecl`（~2758-2931 行），方法/属性前缀分支（~3340-3370 / ~4150-4175）
> · `ThirdParty/angelscript/source/as_builder.cpp` — `RegisterAccessSpecifier`（~5483 行），属性/虚拟属性/方法挂接（~3362 / ~3954 / ~4608 / ~5341）
> · `ThirdParty/angelscript/source/as_datatype.{h,cpp}` — `asSAccessSpecifier` / `asSAccessPermission` / `GetAllowedAccess`（~803 行）
> · `ThirdParty/angelscript/source/as_compiler.cpp` — 三处编译期检查（~11151 / ~15007 / ~18155）以及 `allowEditPropertyAccess` 开关（~1004）
> · `ThirdParty/angelscript/source/as_module.cpp` — 热重载结构变更检测（~2148 / ~2336 / ~2367）
> · `ThirdParty/angelscript/source/as_objecttype.{h,cpp}` — `accessSpecifiers` 数组与 `GetAccessSpecifier`（~833）
> · `Script/Examples/Core/Example_AccessSpecifiers.as` — 官方用例（92 行）
> **关联文档**:
> `Documents/Knowledges/ZH/Syntax_PropertyAccessor.md` — `property` 修饰符与本文 `access` 修饰符的边界对照
> · `Documents/Knowledges/ZH/AS_Parser.md` — 递归下降解析器框架
> · `Documents/Knowledges/ZH/AS_Compiler.md` — 编译器如何消费访问检查
> · `Documents/Knowledges/ZH/AS_LanguageSyntax.md` — 完整 Token/AST 表
> · `Documents/Knowledges/ZH/Type_Core.md` — 类型系统与 `asCObjectType` 的成员组织
> · `Documents/Knowledges/ZH/RT_HotReload.md` — `accessSpecifiers` 结构变更如何影响热重载

---

## 概览

本文聚焦一个核心问题：**AngelScript 这个 fork 引入的 `access` 关键字到底比 C++ 三档可见性 `public/protected/private` 强在哪里？它的实现是怎么从一个 token 一路走到字节码生成期的访问检查的？**

`access` **不是**对 `public/protected/private` 的换皮，而是一个**类内可命名、按"调用方类型/函数/命名空间"白名单授权**的自定义可见性轴。换言之：传统 OOP 可见性回答"谁能看到"，`access` 回答"哪一组特定调用方能看到、能读、能写、能在 default 块里编辑"。它由 fork 仓库自己加在 `ThirdParty/angelscript/source/` 内核中——插件桥接层（`Core/`、`Preprocessor/`）**没有**对 `access` 做任何额外扩展或拦截，所以这是一个完全在 AS 内核完成的语言特性。

```text
.as 源码
  ┌──────────────────────────────────────────────────────────────┐
  │ class UFoo {                                                  │
  │   access Internal = private;                          (decl)  │
  │   access ReadOnlyForCmp = private, UCmp(readonly);    (decl)  │
  │                                                               │
  │   access:Internal      float Value;                   (use)   │
  │   access:ReadOnlyForCmp void DoIt() { ... }           (use)   │
  │ }                                                             │
  └──────────────────────────────────────────────────────────────┘
                                │
                                ▼
  ┌──────────────────────────────────────────────────────────────┐
  │ Tokenizer:  "access" → ttAccess                              │
  │             "readonly" / "editdefaults" / "inherited" 仍是    │
  │              ttIdentifier，靠 IdentifierIs(...) 字符串识别     │
  └──────────────────────────────────────────────────────────────┘
                                │
                                ▼
  ┌──────────────────────────────────────────────────────────────┐
  │ Parser:                                                      │
  │   声明形式 `access X = private, A, B(readonly);`             │
  │     → IsAccessDecl / ParseAccessDecl → snAccessDeclaration   │
  │   使用形式 `access:X` 前缀                                    │
  │     → IsVarDecl / IsFuncDecl / ParseDeclaration / Function   │
  │       挂一段 `ttAccess + 子节点(标识符)` 在声明节点首位       │
  └──────────────────────────────────────────────────────────────┘
                                │
                                ▼
  ┌──────────────────────────────────────────────────────────────┐
  │ Builder:                                                     │
  │   RegisterAccessSpecifier(snAccessDeclaration, ot)           │
  │     → asSAccessSpecifier { name; bIsPrivate/Protected;       │
  │       bAnyReadOnly/EditDefaults; permissions[]; } 入 ot      │
  │   绑定属性/方法时：node->tokenType==ttAccess 走分支 →         │
  │     prop->accessSpecifier / func->accessSpecifier ← spec     │
  └──────────────────────────────────────────────────────────────┘
                                │
                                ▼
  ┌──────────────────────────────────────────────────────────────┐
  │ Compiler (编译期，零字节码开销):                               │
  │   每次产生属性读 / 属性写 / 函数调用字节码前：                 │
  │     spec->GetAllowedAccess(bDerived, outFunc, R/W/E)         │
  │     基于 outFunc->objectType 的名字 / 命名空间 / 父类匹配 →   │
  │     不通过则 Error("... does not allow access from here.")   │
  │   allowEditPropertyAccess（仅在 __InitDefaults /              │
  │     ConstructionScript 等少数函数内为 true）控制 editdefaults │
  └──────────────────────────────────────────────────────────────┘
                                │
                                ▼
  ┌──────────────────────────────────────────────────────────────┐
  │ Module (热重载):                                              │
  │   asCModule::* 检查 OldClass->accessSpecifiers vs NewClass    │
  │   字段值不一致 → bHadStructuralChanges=true → 走 FullReload   │
  └──────────────────────────────────────────────────────────────┘
```

后续章节按 **概念 / Token / 解析 / 登记 / 编译期检查 / 跨模块与热重载 / UE 反射 / 实例与速查** 的顺序展开。

---

## 一、概念区分：可见性轴 vs 自定义授权轴

### 1.1 `public/protected/private` —— 单一可见性轴

经典 OOP 的可见性是**全有或全无 + 一档继承衰减**：
- `public`（AS 默认）—— 任意调用方可见。
- `protected` —— 仅本类与派生类可见。
- `private` —— 仅本类可见。

AS 在 `ThirdParty/angelscript/source/as_compiler.cpp` 里把这套写死成两段判断（行 14994-15005，节选）：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_compiler.cpp
// 节选自: 属性访问 expression 编译路径
// ============================================================================
if( prop->isPrivate && (!outFunc || outFunc->objectType != ctx->type.dataType.GetTypeInfo()) )
{
    Error(TXT_PRIVATE_PROP_ACCESS_s, name); // ★ private 仅同类
}
else if( prop->isProtected
         && (!outFunc || !outFunc->objectType
             || !outFunc->objectType->DerivesOrShadows(ctx->type.dataType.GetTypeInfo())) )
{
    Error(TXT_PROTECTED_PROP_ACCESS_s, name); // ★ protected 仅自身或派生
}
```

判定的输入只有"调用方所在 `outFunc->objectType` 是不是被访问类型本身/派生类型"这一个维度。

### 1.2 `access NAME = private, ...` —— 命名 + 白名单 + 权限粒度

`access` 在前者基础上叠了三层维度（详见后文 §四）：

| 维度 | 含义 | 表达式 |
|------|------|--------|
| **基础底盘** | `private` 还是 `protected` 起步 | `access X = private` / `access X = protected` |
| **授权列表** | 在 base 基础上额外放行哪些类型/函数 | `, UCmp, APawn, ExampleCallRestricted` |
| **通配符** | `*` 表示对所有类型放行（必须搭配权限） | `, * (editdefaults, readonly)` |
| **权限修饰** | 单条授权可独立控制读/写/编辑 | `UCmp (readonly)` / `* (editdefaults, readonly)` |
| **继承传播** | 单条授权是否对派生类型也生效 | `USceneComponent (inherited, readonly)` |
| **类级 `*` 总闸** | 整个 access 内对所有未列出方设默认权限 | `* (readonly)` 单独写为列表项 |

也就是说，`access X = private, UCmp(readonly), * (editdefaults)` 一次性表达了：
- 默认 `private`（除本类外谁都不能访问）；
- `UCmp` 类（**仅这一种**）能读；
- 任何类（`*`）只能在 `default {}` / `ConstructionScript` 这类"初始化期上下文"中写入。

这是 `public/protected/private` 三档无法在不污染类层级、不引入 friend 关系的前提下表达的。

### 1.3 与 `property` / `mixin` 修饰符的边界

`Syntax_PropertyAccessor.md` 描述的 `property` 是**函数 → 属性访问器**的语法糖（已在 `refactor-as-remove-autoaccessor` 移除），它影响**调用形态**（写 `obj.X` 还是 `obj.GetX()`）。`access` 与它正交：

- `property` 修饰**单个函数**，决定它能否被以属性形式书写。
- `access` 修饰**整个类内一组成员**，决定外部调用方能否触达。
- 一个函数可以同时是 `access:X` + `property`（旧 fork 阶段），两者在 builder 中走不同的 trait 位。

---

## 二、Token 与词法识别

### 2.1 `ttAccess` 是真关键字

`ThirdParty/angelscript/source/as_tokendef.h:185 / :255`：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_tokendef.h
// 节选自: eTokenType 枚举 + tokenWords 列表
// ============================================================================
enum eTokenType {
    // ...
    ttAccess,                       // ★ 注册为内核 token
    ttUnresolvedObject,
};
sTokenWord const tokenWords[] = {
    // ...
    asTokenDef("access"    , ttAccess),
    asTokenDef("auto"      , ttAuto),
    // ...
};
```

这与 `Syntax_PropertyAccessor.md` §1.1 描述的 `property` 形成对比：`property` 是 `ttIdentifier` + `IdentifierIs(token, PROPERTY_TOKEN)` 字符串比较，**不是**真 token；`access` 走的是真正 `ttAccess`，因此可以在 `t.type == ttAccess` 这种快速比较里直接命中，而不必触发字符串比对。

### 2.2 `readonly` / `editdefaults` / `inherited` —— 仍是字符串

修饰符不占用 token，写在 `as_tokendef.h:336-338`：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_tokendef.h
// 节选自: 字符串常量定义（与 PROPERTY_TOKEN / FINAL_TOKEN 同一族）
// ============================================================================
const char * const READONLY_TOKEN     = "readonly";
const char * const EDITDEFAULTS_TOKEN = "editdefaults";
const char * const INHERITED_TOKEN    = "inherited";
```

它们在 parser 与 builder 两侧都用 `IdentifierIs(node, READONLY_TOKEN)` / `file->TokenEquals(...)` 识别。这意味着用户**仍可**用 `readonly` 作为变量名（与 `final` / `override` / `mixin` 同一处理），不会被强制保留。

### 2.3 `*` 在 access 列表中的特殊性

`*` 在 access 子句里复用了 `ttStar`（乘法 token），由 parser 在上下文里特判（详见 §三），不需要单独 token。这是 fork 选择的 trade-off：节省 token 槽 vs. parser 多一段判别。

---

## 三、Parser 解析路径

`access` 在 parser 里有**两类完全独立**的入口，它们对应"声明 access 命名"和"使用 access 命名"两种语法形态。

### 3.1 形态 A —— 声明：`access X = private, ...;`

入口：`as_parser.cpp:2758` `IsAccessDecl()` + `:2789` `ParseAccessDecl()`，由类内成员主循环 `ParseClass()` 在 `:3709` 调度（见下文）。

`IsAccessDecl()` 先做 LL(2) 前瞻：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_parser.cpp
// 函数: asCParser::IsAccessDecl
// ============================================================================
bool asCParser::IsAccessDecl()
{
    sToken t;  GetToken(&t);
    if (t.type != ttAccess)        { RewindTo(&t); return false; } // ① 必须以 access 起手

    sToken t1; GetToken(&t1);
    if (t1.type != ttIdentifier)   { RewindTo(&t); return false; } // ② access 后必须紧跟 ID
    GetToken(&t1);
    if (t1.type != ttAssignment)   { RewindTo(&t); return false; } // ★ 第三个 token 必须是 '='

    RewindTo(&t);                                                  // ③ 鉴别成功，rewind 由调用方再消耗
    return true;
}
```

注意 ③：如果第三个 token 是 `:`（冒号），就**不是** access 声明而是 access 使用形式（详见 §3.2），这就是消歧的关键。

`ParseAccessDecl()` 在确认形态后，先解析名字、`=`，再要求 base 必须是 `private` / `protected`，否则报错：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_parser.cpp
// 函数: asCParser::ParseAccessDecl  （节选）
// ============================================================================
asCScriptNode *node = CreateNode(snAccessDeclaration);  // ★ 专属节点类型
ParseToken(ttAccess);
auto* identifierNode = ParseIdentifier();               // 名字 X
ParseToken(ttAssignment);                               // '='
node->AddChildLast(identifierNode);

GetToken(&t1);                                          // ★ base 强制是 private 或 protected
if      (t1.type == ttPrivate)   { RewindTo(&t1); node->AddChildLast(ParseToken(ttPrivate)); }
else if (t1.type == ttProtected) { RewindTo(&t1); node->AddChildLast(ParseToken(ttProtected)); }
else                             { Error(ExpectedOneOf({"private","protected"}, 2), &t1); ... }

while (true) {                                          // 后续逗号分隔的授权列表
    GetToken(&t1);
    if (t1.type == ttListSeparator) {
        GetToken(&t1);
        if (t1.type == ttStar) { ... ParseToken(ttStar) ... }       // '*' 通配
        else                   { ... ParseIdentifier() into snUndefined accessNode ... }

        // 可选的 (readonly|editdefaults|inherited[, ...]) 修饰
        if (t1.type == ttOpenParanthesis) { while (...) {
            if      (IdentifierIs(t1, READONLY_TOKEN))     accessNode->AddChildLast(ParseIdentifier());
            else if (IdentifierIs(t1, EDITDEFAULTS_TOKEN)) accessNode->AddChildLast(ParseIdentifier());
            else if (IdentifierIs(t1, INHERITED_TOKEN))    accessNode->AddChildLast(ParseIdentifier());
            ...
        }}
    }
    else if (t1.type == ttEndStatement) break;          // ';' 结束
    else { Error(...); }
}
```

最终产出的 AST 结构形如：

```text
snAccessDeclaration
 ├─ snIdentifier         (X)              ← 名字
 ├─ ttPrivate / ttProtected               ← base
 ├─ ttStar (firstChild = snIdentifier readonly,...)   ← 可选 '*' 项
 └─ snUndefined          (firstChild = snIdentifier 类名/函数名,
                                       后跟 readonly/editdefaults/inherited 修饰子节点)
    ...                  ← 重复，每条授权一个
```

`ParseClass()` 在 `:3709` 把 `snAccessDeclaration` 与 `IsFuncDecl / IsVirtualPropertyDecl / IsVarDecl / IsClassDefaultStatement` 并列：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_parser.cpp
// 节选自: ParseClass 的成员主循环
// ============================================================================
while (t.type != ttEndStatementBlock && t.type != ttEnd) {
    if      (t.type == ttFuncDef)                 node->AddChildLast(ParseFuncDef());
    else if (IsFuncDecl(true))                    node->AddChildLast(ParseFunction(true));
    else if (IsVirtualPropertyDecl())             node->AddChildLast(ParseVirtualPropertyDecl(true,false));
    else if (IsVarDecl())                         node->AddChildLast(ParseDeclaration(true));
    else if (IsAccessDecl())                      node->AddChildLast(ParseAccessDecl());     // ★
    else if (!isStruct && IsClassDefaultStatement()) ...
    ...
}
```

声明形态**只**出现在类体内：作用域是该 `asCObjectType`，不入命名空间、不导出（详见 §六）。

### 3.2 形态 B —— 使用：`access:X` 前缀

使用形态与 `private` / `protected` 修饰符并列，挂在成员声明的最前面：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_parser.cpp
// 节选自: 成员函数声明前缀解析（约 3340-3370）
// ============================================================================
if      (t1.type == ttPrivate)   { ... node->AddChildLast(ParseToken(ttPrivate));   GetToken(&t1); }
else if (t1.type == ttProtected) { ... node->AddChildLast(ParseToken(ttProtected)); GetToken(&t1); }
else if (t1.type == ttAccess)
{
    RewindTo(&t1);
    auto* accessNode = ParseToken(ttAccess);                            // ★ 留下 ttAccess 节点

    sToken topen; GetToken(&topen);
    if (topen.type != ttColon) { Error(ExpectedToken(":")); ... }       // ★ 必须紧跟 ':'

    auto* identifier = ParseIdentifier();                               // ★ 引用的 access 名
    accessNode->AddChildLast(identifier);
    node->AddChildLast(accessNode);
    GetToken(&t1);
}
```

属性声明也走对称分支，在 `as_parser.cpp:4157`。两个分支都把 `ttAccess` 节点连同它的子标识符加在声明节点的最前位置，这样下游 builder 只要看 `firstChild->tokenType == ttAccess` 就知道这条声明绑定了哪个 access 名。

### 3.3 与 `IsVarDecl / IsFuncDecl` 的歧义消解

由于 `access:X int Value;` 与 `access X = private;` 都以 `ttAccess` 起手，前瞻函数必须区分。`IsVarDecl()` 在 `as_parser.cpp:2977` 的判断方式是看第二个 token：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_parser.cpp
// 函数: asCParser::IsVarDecl  （节选）
// ============================================================================
if (t1.type == ttAccess)
{
    sToken taccess;
    GetToken(&taccess);
    if (taccess.type != ttColon)        { RewindTo(&t1); return false; } // ★ 不是 ':' → 不是 var
    GetToken(&taccess);
    if (taccess.type != ttIdentifier)   { RewindTo(&t1); return false; }
    // 是 'access:X' 形式，继续往后认 type → IsType(t1)
}
```

`IsFuncDecl()` 在 `:3124` 走完全对称的分支。整个解析阶段无回溯。

---

## 四、Builder：从 AST 到 `asSAccessSpecifier`

### 4.1 数据载体

`ThirdParty/angelscript/source/as_datatype.h:187-208` 定义两个结构：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_datatype.h
// 节选自: 访问授权数据载体
// ============================================================================
struct asSAccessPermission
{
    asCString accessName;
    bool      bInherited     = false;   // (inherited)
    bool      bReadOnly      = false;   // (readonly)
    bool      bEditDefaults  = false;   // (editdefaults)
};

struct asSAccessSpecifier
{
    asCString name;                     // ★ 例如 "Internal"

    bool      bIsPrivate     = false;   // base = private
    bool      bIsProtected   = false;   // base = protected

    bool      bAnyReadOnly   = false;   // 含 '*' 项时聚合
    bool      bAnyEditDefaults = false;

    asCArray<asSAccessPermission> permissions;   // 每条 idType/funcName 授权一项

    void GetAllowedAccess(bool bAccessedFromDerivedClass,
                          class asCScriptFunction* accessFromFunction,
                          bool& Read, bool& Write, bool& Edit);
};
```

每个 `asCObjectType` 持有一组 access 描述（`as_objecttype.h:162`）：

```cpp
asCArray<asSAccessSpecifier*>   accessSpecifiers;
asSAccessSpecifier*             GetAccessSpecifier(const char* name);
```

`asCObjectProperty` 与 `asCScriptFunction` 各持有一个**指针**（不拥有），指向对应的 spec：

```cpp
// as_property.h:83   asCObjectProperty
asSAccessSpecifier*  accessSpecifier = nullptr;
// as_scriptfunction.h:345   asCScriptFunction
asSAccessSpecifier*  accessSpecifier;
```

### 4.2 登记声明：`RegisterAccessSpecifier`

builder 在 `as_builder.cpp:710-713` 主循环里识别 `snAccessDeclaration` 并调用：

```cpp
else if( node->nodeType == snAccessDeclaration )
    RegisterAccessSpecifier(node, decl->script, CastToObjectType(decl->typeInfo));
```

实现在 `as_builder.cpp:5483-5573`（节选关键步骤）：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_builder.cpp
// 函数: asCBuilder::RegisterAccessSpecifier  （节选）
// ============================================================================
asCScriptNode* nameNode = node->firstChild;
asCString name; name.Assign(&file->code[nameNode->tokenPos], nameNode->tokenLength);

// ① 同名重复检查
for (asUINT n=0; n<objType->accessSpecifiers.GetLength(); ++n) {
    if (objType->accessSpecifiers[n]->name == name) {
        WriteError("Access specifier %s is already declared.", file, node); return 0;
    }
}

asSAccessSpecifier* spec = asNEW(asSAccessSpecifier);
spec->name = name;

asCScriptNode* specNode = nameNode->next;
while (specNode != nullptr) {
    if      (specNode->tokenType == ttPrivate)   spec->bIsPrivate   = true;     // base
    else if (specNode->tokenType == ttProtected) spec->bIsProtected = true;
    else if (specNode->tokenType == ttStar) {                                   // ★ '*' 项
        for (asCScriptNode* m = specNode->firstChild; m; m = m->next) {
            if      (file->TokenEquals(m, READONLY_TOKEN))     spec->bAnyReadOnly     = true;
            else if (file->TokenEquals(m, EDITDEFAULTS_TOKEN)) spec->bAnyEditDefaults = true;
        }
    }
    else if (specNode->firstChild
             && specNode->firstChild->tokenType == ttIdentifier) {              // ★ 单条授权
        asSAccessPermission perm;
        perm.accessName.Assign(&file->code[specNode->firstChild->tokenPos],
                               specNode->firstChild->tokenLength);
        for (asCScriptNode* m = specNode->firstChild->next; m; m = m->next) {
            if      (file->TokenEquals(m, READONLY_TOKEN))     perm.bReadOnly     = true;
            else if (file->TokenEquals(m, EDITDEFAULTS_TOKEN)) perm.bEditDefaults = true;
            else if (file->TokenEquals(m, INHERITED_TOKEN))    perm.bInherited    = true;
        }
        spec->permissions.PushLast(perm);
    }
    specNode = specNode->next;
}
objType->accessSpecifiers.PushLast(spec);
```

注意 `spec->permissions` 里**只**保存了字符串名字，没有解析为 `asCObjectType*` 指针。这是有意为之：access 列表里的类型/函数**不要求已经存在或已 import**，运行时再做名字匹配就够（详见 §6.1）。这也避免了类间的循环依赖。

### 4.3 把 spec 挂到属性 / 方法

属性 builder 在 `as_builder.cpp:3362` 识别 `ttAccess` 前缀并查表：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_builder.cpp
// 节选自: 类属性枚举（CompileClasses 内）
// ============================================================================
asSAccessSpecifier* accessSpecifier = nullptr;
if      (nd && nd->tokenType == ttPrivate)   { isPrivate = true; nd = nd->next; }
else if (nd && nd->tokenType == ttProtected) { isProtected = true; nd = nd->next; }
else if (nd && nd->tokenType == ttAccess) {
    asCString accessName;
    if (auto* specNameNode = nd->firstChild) {
        accessName.Assign(&decl->script->code[specNameNode->tokenPos], specNameNode->tokenLength);
        accessSpecifier = ot->GetAccessSpecifier(accessName.AddressOf());        // ★ 名字查表
    }
    if (accessSpecifier == nullptr)
        WriteError("Unknown access specifier '%s'", file, nd);                   // ★ 未声明则报错
    nd = nd->next;
}
// ... AddPropertyToClass(...)
auto* Property = AddPropertyToClass(decl, name, dt, isPrivate, isProtected, file, nd);
if (Property != nullptr) Property->accessSpecifier = accessSpecifier;            // ★ 写入属性
```

mixin 注入路径 `:3954`、虚拟属性路径 `:5341`（已被 `refactor-as-remove-autoaccessor` 关闭）、方法路径 `:4608` 都走完全对称的分支，最终共同特征是把 `objType->GetAccessSpecifier(name)` 返回的指针挂到对应成员的 `accessSpecifier` 字段。

方法路径的特殊性在 `:4608`：它先把名字暂存到一个外部 `asCString*`，等函数 ID 分配后才在 `:5280-5288` 真正调用 `GetAccessSpecifier` 写入：

```cpp
if (accessSpecifier != nullptr && accessSpecifier->GetLength() != 0) {
    f->accessSpecifier = objType->GetAccessSpecifier(accessSpecifier->AddressOf());
    if (f->accessSpecifier == nullptr)
        WriteError("Unknown access specifier %s on function %s", file, node);
}
```

### 4.4 依赖关系：声明必须先于使用？

由于 builder 阶段先做 access 声明登记（在 `:710-713` 阶段），再做属性/方法登记（在 `:3346` 等阶段），所以**同一个类内** access 名字的"声明" → "使用"顺序由 builder 保证：哪怕 .as 源码里 `access:X` 的属性写在 `access X = private` 之前，也能正确 resolve（前提是同处类内）。

跨类不通：A 类的 `access X` 不会被 B 类用 `access:X` 引用——`GetAccessSpecifier` 只在自身类的 `accessSpecifiers` 里查。

---

## 五、编译期检查：`GetAllowedAccess`

### 5.1 唯一裁决函数

`as_datatype.cpp:803-900` 的 `asSAccessSpecifier::GetAllowedAccess` 是唯一的检查入口，三个 bool 输出 `Read / Write / Edit`：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_datatype.cpp
// 函数: asSAccessSpecifier::GetAllowedAccess  （节选）
// ============================================================================
Read = Write = Edit = false;

if (bIsProtected && bAccessedFromDerivedType)               // ① protected 派生类完全放行
    { Read = Write = Edit = true; }

if (bAnyReadOnly)        Read = true;                        // ② 类级 '*' 总闸
if (bAnyEditDefaults)    Edit = true;

if (accessFromFunction != nullptr) {
    auto* accessFromType = accessFromFunction->objectType;
    for (auto& spec : permissions) {                         // ③ 逐条授权匹配
        bool bSpecApplies = false;
        if (accessFromType != nullptr) {                      //   3a) 调用方是类方法
            if (spec.accessName == accessFromType->name) bSpecApplies = true;
            else if (accessFromType->nameSpace->name.GetLength() != 0
                     && spec.accessName == accessFromType->nameSpace->name) bSpecApplies = true;
        } else if (spec.accessName == accessFromFunction->name) bSpecApplies = true;  // 3b) 调用方是全局函数
            else if (accessFromFunction->nameSpace->name.GetLength() != 0
                     && spec.accessName == accessFromFunction->nameSpace->name) bSpecApplies = true;

        if (spec.bInherited && !bSpecApplies && accessFromType != nullptr) { // ★ inherited 沿继承链向上扫
            for (auto* superType = accessFromType->derivedFrom;
                 superType; superType = superType->derivedFrom)
                if (spec.accessName == superType->name) { bSpecApplies = true; break; }
            if (!bSpecApplies) /* 同样扫 shadowType（cross-module 影子）*/ ;
        }

        if (bSpecApplies) {
            if (spec.bReadOnly) {
                Read = true;
                if (spec.bEditDefaults) Edit = true;
            } else {
                if (spec.bEditDefaults) Edit = true;
                else { Read = Write = Edit = true; }            // ★ 默认整放
            }
        }
    }
}
```

注意：
- 名字匹配是**字符串**层面的：spec 里只存名字，不存类型指针，所以可以在不导入相应类型的情况下提前放行。
- "命名空间名字"也算合法匹配源——因此可以写 `, MyNamespace` 然后让该命名空间下任意函数/类成员访问。
- `bInherited` 让授权对派生类生效：常见用法 `USceneComponent (inherited)` 让任何继承自 `USceneComponent` 的类型都拿到访问权。`shadowType` 分支处理跨模块"影子继承"的场景，与 `Type_ClassGeneration.md` 里描述的影子类型机制配合。

### 5.2 编译器三处调用点

`as_compiler.cpp` 一共在三处把字节码生成前的 `GetAllowedAccess` 套上去：

| 位置 | 触发场景 | 调用形态 | 失败行为 |
|------|---------|---------|---------|
| `:11151` | 派生类访问父类属性（被命中专门分支） | `GetAllowedAccess(true, outFunc, R/W/E)` | `!Read` → Error；`!Write` → makeConst |
| `:15007` | 同名属性的常规读/写 expression 路径 | `GetAllowedAccess(false, outFunc, R/W/E)` | 同上 |
| `:18155` | 函数调用 (`MakeFunctionCall`) | `GetAllowedAccess(bIsDerivedClass, outFunc, R/W/E)` | `!Read` → Error；`!Write && !descr->IsReadOnly()` → Error |

三处都共享 `allowEditPropertyAccess` 这个 boolean 短路：

```cpp
if (!allowEditPropertyAccess || !Edit) {  // ★ Edit=true 且当前函数允许 → 完全放行
    if (!Read) Error("Access specifier ... does not allow access from here.");
    if (!Write) ... // 视场景为 makeConst / Error
}
```

`allowEditPropertyAccess` 在 `as_compiler.cpp:1004` 被设置：

```cpp
allowEditPropertyAccess = m_isInitDefaults
    || in_outFunc->name == __ConstructionScript                     // "ConstructionScript_Implementation"
    || in_outFunc->name.StartsWith("__Init_")
    || in_outFunc->name == "OnActorModifiedInEditor_Implementation"
    || in_outFunc->name == "OnComponentModifiedInEditor_Implementation"
    /* + 其他几项 */;
```

也就是说，`(editdefaults)` 修饰**只在**这些"初始化期 / 编辑器期"上下文里被认为是合法的"写"，其他普通函数即便拿到 `Edit=true` 也不能写。这与 `Syntax_DefaultStatement.md` 里描述的"`default {}` 仅在构造期注入"语义保持一致。

### 5.3 关键性质：编译期检查、零运行时开销

`access` 全部在 builder + compiler 期完成：
- `asSAccessSpecifier` 不参与任何字节码指令；
- 通过检查的访问会按普通 `asBC_LDV / asBC_PSF / asBC_CALL` 等字节码生成；
- 失败的访问在生成字节码前就被 `Error()` 拦下，根本不会跑到 VM。

这与 C++ 的 `private/protected` 一致——编译时强制 + 字节码无差异。

---

## 六、跨模块、命名空间与热重载

### 6.1 命名空间内 access 名字是否可见？

**不是按命名空间可见**——access 名字的作用域被限制在它所在的 `asCObjectType`，参见 §4.2。`accessSpecifiers` 数组只挂在 `asCObjectType` 上，没有 `asSNameSpace` 维度的副本。所以：

```angelscript
namespace Foo {
class CA {
    access Internal = private;
    access:Internal int Field;
}
class CB {
    access:Internal int Other;  // ★ 编译错误：Unknown access specifier 'Internal'
                                //   即便同命名空间也不会从 CA 借
}
}
```

唯一的"跨类"通道是 §5.1 的 `inherited` 修饰符 + 派生关系：父类的 access 命名落在父类的 `accessSpecifiers` 里，但派生类的方法在 `GetAllowedAccess` 时通过 `accessFromType->derivedFrom` 链向上找名字匹配，因此父类授权可以覆盖派生类。

### 6.2 跨模块/导入语义

由于 access 名字和授权名字都是**字符串**：
- 一个类被 `import` 进另一个模块后，`accessSpecifiers` 数组与里头的 spec 仍随 `asCObjectType` 一起被引用——这部分是 AS 模块系统已有的机制，access 不需要额外处理。
- spec 里的 `permissions[].accessName` 字符串在编译被引用方时再做名字匹配，所以授权方哪怕跨模块、哪怕没被显式 import，也能命中。

副作用：拼写错误的授权名字**不会**报错——只会"不放行"。这是已知设计 trade-off，详见 §附录 B。

### 6.3 热重载结构变更检测

`as_module.cpp:2367-2409` 在比较旧 / 新模块的类时显式检查 access 元数据：

```cpp
// ============================================================================
// 文件: ThirdParty/angelscript/source/as_module.cpp
// 节选自: 类结构变更检测
// ============================================================================
for (int i = 0; i < OldClass->accessSpecifiers.GetLength(); ++i) {
    asSAccessSpecifier* Old = OldClass->accessSpecifiers[i];
    asSAccessSpecifier* New = NewClass->GetAccessSpecifier(Old->name.AddressOf());

    if (New != nullptr) {
        if (Old->bIsPrivate     != New->bIsPrivate)     OutHadStructuralChanges = true;
        if (Old->bIsProtected   != New->bIsProtected)   OutHadStructuralChanges = true;
        if (Old->bAnyReadOnly   != New->bAnyReadOnly)   OutHadStructuralChanges = true;
        if (Old->bAnyEditDefaults != New->bAnyEditDefaults) OutHadStructuralChanges = true;
        // 比对每条 permission
        if (Old->permissions.GetLength() != New->permissions.GetLength()) ...
        else for (...) {
            if (OldPerm.accessName    != NewPerm.accessName
             || OldPerm.bInherited    != NewPerm.bInherited
             || OldPerm.bReadOnly     != NewPerm.bReadOnly
             || OldPerm.bEditDefaults != NewPerm.bEditDefaults) {
                OutHadStructuralChanges = true; break;
            }
        }
    } else {
        OutHadStructuralChanges = true;
    }
}
```

属性 / 方法的 `accessSpecifier` 字段同样参与变更检测（`:2148`、`:2336`）：哪怕属性名字、类型完全一致，但 `accessSpecifier->name` 不同也算结构变更，热重载时走 FullReload，详见 `RT_HotReload.md`。

实务影响：**调一行 access 子句即触发整个类重建**。在生产中，大批量改 access 名字时建议关掉自动热重载，统一编译验证。

---

## 七、与 UE 反射 / Blueprint 的关系

### 7.1 不是 UPROPERTY meta，不会"投射"

`access` 完全不影响 UE 反射元数据。如下事实可以从代码层验证：
- `asSAccessSpecifier` 字段没有出现在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 任何 `FProperty` / `UFunction` 创建路径里。
- `Type_ClassGeneration.md` 描述的 UClass 生成完全由 `UPROPERTY(...)` / `UFUNCTION(...)` 宏的元数据驱动。
- 桥接层 `Preprocessor` 只在 `ExtractReturnType`（`AngelscriptPreprocessor.cpp:2005`）里识别 `private` / `protected` 关键字以剥离它们对宏展开的干扰，**没有**对 `access` 关键字的特殊处理（因为 `access:X` 永远在 `private` / `protected` 同一位置而非 `UPROPERTY(...)` 内部）。

也就是说：**Blueprint 看到的字段一律按 `UPROPERTY()` meta 决定**（`BlueprintReadOnly` / `EditAnywhere` 等），AS 的 `access` 不会让 BP 拿到额外权限或被额外阻断。`Example_AccessSpecifiers.as:67-69` 同时写 `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Options")` 和 `access:EditAndReadOnly`，前者面向 BP，后者面向 AS——它们各管一段。

### 7.2 与 `editdefaults` 的语义"巧合"

`access ... (editdefaults, ...)` 让一个类可以**仅在 default 块/构造期**写入字段，这与 UE 的 `EditDefaultsOnly`（仅在 CDO 编辑面板修改）在含义上巧合，但实现完全不同：
- UE 的 `EditDefaultsOnly` 是编辑器端的 PostEdit gate；
- AS 的 `editdefaults` 是编译期的 `allowEditPropertyAccess` 开关 + `Edit` bit；
- 二者**不联动**：你必须分别写 `UPROPERTY(EditDefaultsOnly)` 和 `access:EditAndReadOnly`。

### 7.3 UE 生命周期函数被默认放行

`allowEditPropertyAccess = true` 的函数清单（来自 `as_compiler.cpp:1004-1010` 节选）：

- `__InitDefaults`（AS 自动生成的 default 块函数）
- `ConstructionScript_Implementation`
- 任意以 `__Init_` 开头的函数（成员级 default 子句）
- `OnActorModifiedInEditor_Implementation`
- `OnComponentModifiedInEditor_Implementation`

这覆盖了 UE 编辑期能触达的所有"初始化 / 即时修改回调"路径，确保 `editdefaults` 修饰能在 UE 工作流里有意义地工作。

---

## 八、用例剖析与速查

### 8.1 官方示例逐行映射

`Script/Examples/Core/Example_AccessSpecifiers.as` 是唯一一份系统化用例，完整对照 §四 / §五 的实现：

| 行号 | 源码 | 对应解析/编译产物 |
|------|------|------------------|
| 12 | `access Internal = private;` | `snAccessDeclaration{Internal, ttPrivate}` → `spec{name="Internal", bIsPrivate=true, permissions=[]}` |
| 15-16 | `access:Internal float PrivateFloatValue=0.0;` | property 节点首子 `ttAccess(Internal)` → `prop->accessSpecifier=spec` |
| 22 | `access InternalWithCapability = private, UAccessSpecifierComponent, APlayerController;` | spec.permissions=[{UAccessSpecifierComponent}, {APlayerController}]，无修饰 |
| 29-32 | `access:InternalWithCapability void AccessibleMethod()` | `func->accessSpecifier=spec`；外部 `UCmp::*` 的方法可调用 |
| 44 | `access SpecifierCapabilityCanOnlyRead = private, UAccessSpecifierComponent (readonly);` | permissions=[{UCmp,bReadOnly=true}] |
| 54 | `access ReadableInAnySceneComponent = private, USceneComponent (inherited, readonly);` | permissions=[{USceneComponent,bInherited=true,bReadOnly=true}] → 任意 USceneComponent 派生类只读 |
| 64 | `access EditAndReadOnly = private, * (editdefaults, readonly);` | spec.bAnyReadOnly=true, bAnyEditDefaults=true |
| 67-69 | `UPROPERTY(...) access:EditAndReadOnly bool bOptionOnlyEditable=false;` | UE meta + access 双层叠加（§7.1）|
| 75 | `access RestrictedToSpecificGlobalFunction = private, ExampleCallRestricted;` | permissions=[{ExampleCallRestricted}]（**全局函数名**，不是类名）|
| 77-80 | `access:RestrictedToSpecificGlobalFunction void RestrictedFunction()` | 仅 `ExampleCallRestricted()` 可以调用 |
| 83-87 | `void ExampleCallRestricted() { ... }` | 无 `objectType`，靠 §5.1 第 3b 分支命中 `accessFromFunction->name == "ExampleCallRestricted"` |

### 8.2 EBNF（实操归纳，便于读者建立心智模型）

```text
ClassMember ::=
      AccessDecl
    | (AccessUse | "private" | "protected")? VarDecl
    | (AccessUse | "private" | "protected")? FuncDecl
    | ...

AccessDecl  ::= "access" IDENT "=" ("private" | "protected")
                ( "," PermItem )* ";"

PermItem    ::= ("*" | IDENT) ( "(" PermMod ("," PermMod)* ")" )?
PermMod     ::= "readonly" | "editdefaults" | "inherited"

AccessUse   ::= "access" ":" IDENT
```

### 8.3 决策矩阵：什么时候用 `access`，什么时候用 `private/protected`

| 场景 | 推荐 |
|------|------|
| 仅本类可见 | `private` |
| 仅本类 + 派生类可见 | `protected` |
| 仅本类 + 一个/几个特定外部类型可见 | **`access X = private, A, B;`** |
| 一组成员对所有人只读、对自己读写 | **`access X = private, * (readonly);`** |
| 仅初始化期可编辑、运行期只读 | **`access X = private, * (editdefaults, readonly);`** + `UPROPERTY(EditDefaultsOnly)` |
| 整个继承家族需要隐式访问权 | **`access X = private, BaseT (inherited);`** |
| 对 BP / Native 暴露 | 走 `UPROPERTY` / `UFUNCTION` 元数据，与 access 无关 |

---

## 附录 A：`access` 关键字速查表

### A.1 关键字 / 符号

| Token / 字符串 | 类型 | 出现位置 | 含义 |
|---------------|------|----------|------|
| `access` | `ttAccess` (真 token) | 类内任意成员位置 | 起手字 |
| `:` | `ttColon` | `access:NAME` 形态 | 引用已声明的 access 名 |
| `=` | `ttAssignment` | `access NAME = ...` 形态 | 进入声明 |
| `private` / `protected` | `ttPrivate` / `ttProtected` | base | 必须二选一 |
| `*` | `ttStar` | 授权列表项 | 通配所有调用方 |
| `,` | `ttListSeparator` | 授权之间 | 分隔 |
| `(` `)` | `ttOpen/CloseParanthesis` | 单条授权后 | 圈定权限修饰 |
| `readonly` | `ttIdentifier` (`READONLY_TOKEN`) | 修饰 | 只读放行 |
| `editdefaults` | `ttIdentifier` (`EDITDEFAULTS_TOKEN`) | 修饰 | 仅编辑器/初始化期可写 |
| `inherited` | `ttIdentifier` (`INHERITED_TOKEN`) | 修饰 | 沿派生链传播 |
| `;` | `ttEndStatement` | 声明结束 | — |

### A.2 关键源码位置一览

| 阶段 | 文件 / 函数 | 行 |
|------|------------|----|
| Token 注册 | `as_tokendef.h` `tokenWords[]` | ~255 |
| Parser 声明前瞻 | `as_parser.cpp` `IsAccessDecl` | 2758 |
| Parser 声明解析 | `as_parser.cpp` `ParseAccessDecl` | 2789 |
| Parser 使用前瞻（var）| `as_parser.cpp` `IsVarDecl` | ~2977 |
| Parser 使用前瞻（func）| `as_parser.cpp` `IsFuncDecl` | ~3124 |
| Parser 函数前缀解析 | `as_parser.cpp` ParseFunction body | ~3340 |
| Parser 属性前缀解析 | `as_parser.cpp` ParseDeclaration body | ~4150 |
| Builder 声明落入 | `as_builder.cpp` `RegisterAccessSpecifier` | 5483 |
| Builder 属性挂接 | `as_builder.cpp` 类属性循环 | ~3362 |
| Builder mixin 属性挂接 | `as_builder.cpp` mixin 注入循环 | ~3954 |
| Builder 方法挂接 | `as_builder.cpp` 函数解析 | ~4608 / ~5280 |
| Builder 虚拟属性挂接（已禁用）| `as_builder.cpp` `RegisterVirtualProperty` | ~5341 |
| Compiler 属性派生类访问 | `as_compiler.cpp` | ~11151 |
| Compiler 属性常规访问 | `as_compiler.cpp` | ~15007 |
| Compiler 函数调用检查 | `as_compiler.cpp` `MakeFunctionCall` | ~18155 |
| Module 热重载结构检查 | `as_module.cpp` | ~2148 / ~2336 / ~2367 |
| 数据载体定义 | `as_datatype.h` | 187-208 |
| 决策核心 | `as_datatype.cpp` `GetAllowedAccess` | 803 |
| 类型容器 | `as_objecttype.{h,cpp}` `accessSpecifiers` / `GetAccessSpecifier` | ~162 / ~833 |
| 属性指针字段 | `as_property.h` | 83 |
| 函数指针字段 | `as_scriptfunction.h` | 345 |

---

## 附录 B：避坑清单

1. **base 必须显式写 `private` 或 `protected`**——直接 `access X = , UCmp;` 会报 `Expected one of: private, protected`。设计上是为了避免出现"访问比 public 还宽"的歧义。
2. **`access:X` 必须放在成员行的最前面**，且与 `private` / `protected` 互斥（同一位置只能挑一个）。
3. **拼错授权名不会报错**。`access X = private, UCmpp;` 在编译被授权方时悄悄不放行——不会有 `Unknown ...` 提示。建议结合 IDE 跳转或单元测试覆盖。
4. **未导入的类型也能写在授权列表里**。这是 §6.2 的优势也是陷阱：可以用来打破循环依赖，但如果重命名了被授权类型而忘了同步 access 列表，会静默丢失访问权。
5. **`*` 项不接 `inherited`**，因为 `inherited` 仅对单一具名类型在派生链上扩展才有意义。源码不会拒绝你写，但 builder 不会把它落到 `bAnyEditDefaults / bAnyReadOnly` 之外的字段——等于无效。
6. **`access X` 在不同类内不共享**。即便用一样的名字、同样的 base 与列表，仍是两份 `asSAccessSpecifier`。
7. **改 access 子句会触发 FullReload**。详见 §6.3。如果你在调试 access 行为，建议关掉 `bHotReloadEnabled` 后手动重启，避免反复 reinstance。
8. **不要把 access 当作 UE BlueprintReadOnly 的替代**。BP 完全看不到 access；它只能控制 AS 内部调用。
9. **构造器 / `default {}` 子句享受 `editdefaults` 放行**——把"只在编辑器/初始化时可改"的字段标成 `access X = private, * (editdefaults, readonly);` 是已经被官方示例 (`Example_AccessSpecifiers.as:67-69`) 采纳的模式。
10. **调试小技巧**：当不确定为什么某次访问失败时，可以临时往 `as_compiler.cpp:18155` 附近的 `Error(...)` 处加一行打印 `descr->accessSpecifier->name`，并把 `outFunc->objectType ? outFunc->objectType->name : outFunc->name` 一并打出来——能立即看到 `GetAllowedAccess` 的输入。

---

## 小结

- `access` 是 fork 仓库自家加在 `ThirdParty/angelscript/source/` 的一等 token，与 `Syntax_PropertyAccessor.md` 的"伪关键字 + 字符串识别"路径不同。
- 它在**声明侧**（`access X = private, A, B(readonly);`）走 `IsAccessDecl / ParseAccessDecl` 产出 `snAccessDeclaration` 节点，在**使用侧**（`access:X`）作为成员前缀复用 `private / protected` 的位置。
- 数据落在 `asCObjectType::accessSpecifiers` 上，属性 / 方法各持一个 `asSAccessSpecifier*` 指针；查表与权限计算分别在 builder（`GetAccessSpecifier`）和 compiler（`GetAllowedAccess`）期。
- 三个权限轴 `Read / Write / Edit` 与全局 `allowEditPropertyAccess` 开关协作，把"仅初始化期可编辑"等需求落到编译期检查、运行期零开销。
- 与 UE 反射完全正交：BP 看不到 access，access 也不会改 BP 元数据；`UPROPERTY(EditDefaultsOnly)` 和 `access ... (editdefaults)` 是同名不同源的两套机制，要叠加使用。
- 热重载会把 access 子句当作结构变更——这是触发 FullReload 的隐藏来源之一，调试时需留意。
