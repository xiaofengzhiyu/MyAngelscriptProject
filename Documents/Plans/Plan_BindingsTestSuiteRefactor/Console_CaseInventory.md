# Console Case Inventory

> SubPlan：[`SubPlan_Console.md`](./SubPlan_Console.md)  
> Baseline：`Angelscript.TestModule.Bindings.Console` = 10/10 PASS  
> 覆盖状态：所有清单项均已对位到新的 Console Section，状态为 `covered`。

## AngelscriptConsoleBindingsTests.cpp

| Status | Automation ID | Old module | Old check | Registered objects | New section |
|--------|---------------|------------|-----------|--------------------|-------------|
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | int CVar default `5` | `IntName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | int CVar Set/Get final `42` | `IntName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | float CVar default around `1.5` | `FloatName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | float CVar Set/Get final around `3.25` | `FloatName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | bool CVar default `true` | `BoolName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | bool CVar Set/Get final `false` | `BoolName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | string CVar default `DefaultValue` | `StringName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | string CVar Set/Get final `UpdatedValue` | `StringName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | native `IConsoleManager` int final `42` | `IntName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | native `IConsoleManager` float final `3.25` | `FloatName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | native `IConsoleManager` bool final `false` | `BoolName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableCompat` | `ASConsoleVariableCompat` | native `IConsoleManager` string final `UpdatedValue` | `StringName` | `RunConsoleVariableTypesSection` |
| covered | `ConsoleVariableExistingCompat` | `ASConsoleVariableExistingCompat` | existing native CVar initial value `7` reused instead of script default `99` | `ExistingName` | `RunConsoleVariableExistingSection` |
| covered | `ConsoleVariableExistingCompat` | `ASConsoleVariableExistingCompat` | existing native CVar Set/Get final `21` | `ExistingName` | `RunConsoleVariableExistingSection` |
| covered | `ConsoleVariableExistingCompat` | `ASConsoleVariableExistingCompat` | native `IConsoleManager` final value `21` | `ExistingName` | `RunConsoleVariableExistingSection` |
| covered | `ConsoleCommandCompat` | `ASConsoleCommandCompat` | setup entrypoint executed | `CommandName`, `OutputName` | `RunConsoleCommandBasicSection` |
| covered | `ConsoleCommandCompat` | `ASConsoleCommandCompat` | command exists after module build | `CommandName` | `RunConsoleCommandBasicSection` |
| covered | `ConsoleCommandCompat` | `ASConsoleCommandCompat` | executing with 3 args succeeds | `CommandName` | `RunConsoleCommandBasicSection` |
| covered | `ConsoleCommandCompat` | `ASConsoleCommandCompat` | output CVar receives arg count `3` | `OutputName` | `RunConsoleCommandBasicSection` |
| covered | `ConsoleCommandCompat` | `ASConsoleCommandCompat` | command missing after module discard | `CommandName` | `RunConsoleCommandBasicSection` |
| covered | `ConsoleCommandReplacementCompat` | `ASConsoleCommandOriginalCompat` | original module setup succeeds | `CommandName`, `OutputName` | `RunConsoleCommandReplacementSection` |
| covered | `ConsoleCommandReplacementCompat` | `ASConsoleCommandReplacementCompat` | replacement module setup succeeds | `CommandName`, `OutputName` | `RunConsoleCommandReplacementSection` |
| covered | `ConsoleCommandReplacementCompat` | `ASConsoleCommandReplacementCompat` | command exists after replacement | `CommandName` | `RunConsoleCommandReplacementSection` |
| covered | `ConsoleCommandReplacementCompat` | `ASConsoleCommandReplacementCompat` | executing command observes replacement marker `22` | `OutputName` | `RunConsoleCommandReplacementSection` |
| covered | `ConsoleCommandReplacementCompat` | `ASConsoleCommandReplacementCompat` | command missing after replacement module discard | `CommandName` | `RunConsoleCommandReplacementSection` |
| covered | `ConsoleCommandSignatureCompat` | `ASConsoleCommandSignatureCompat` | expected error contains `Global function for console command must have signature` | `BadSignatureCommand` | `RunConsoleCommandWrongSignatureSection` |
| covered | `ConsoleCommandSignatureCompat` | `ASConsoleCommandSignatureCompat` | expected error contains module name | `BadSignatureCommand` | `RunConsoleCommandWrongSignatureSection` |
| covered | `ConsoleCommandSignatureCompat` | `ASConsoleCommandSignatureCompat` | Prepare succeeds and Execute fails with exception | `BadSignatureCommand` | `RunConsoleCommandWrongSignatureSection` |
| covered | `ConsoleCommandSignatureCompat` | `ASConsoleCommandSignatureCompat` | command not registered after wrong signature failure | `BadSignatureCommand` | `RunConsoleCommandWrongSignatureSection` |

## AngelscriptConsoleCommandArgumentBindingsTests.cpp

| Status | Automation ID | Old module | Old check | Registered objects | New section |
|--------|---------------|------------|-----------|--------------------|-------------|
| covered | `ConsoleCommandArgumentMarshalling.EmptyArgsMarker` | `ASConsoleCommandArgumentEmptyCompat` | setup entrypoint executes | command/output | `RunConsoleCommandArgumentEmptySection` |
| covered | `ConsoleCommandArgumentMarshalling.EmptyArgsMarker` | `ASConsoleCommandArgumentEmptyCompat` | command exists after module build | command | `RunConsoleCommandArgumentEmptySection` |
| covered | `ConsoleCommandArgumentMarshalling.EmptyArgsMarker` | `ASConsoleCommandArgumentEmptyCompat` | executing with empty args succeeds | command | `RunConsoleCommandArgumentEmptySection` |
| covered | `ConsoleCommandArgumentMarshalling.EmptyArgsMarker` | `ASConsoleCommandArgumentEmptyCompat` | output CVar receives `<empty>` | output | `RunConsoleCommandArgumentEmptySection` |
| covered | `ConsoleCommandArgumentMarshalling.EmptyArgsMarker` | `ASConsoleCommandArgumentEmptyCompat` | command missing after module discard | command | `RunConsoleCommandArgumentEmptySection` |
| covered | `ConsoleCommandArgumentMarshalling.ContentAndOrder` | `ASConsoleCommandArgumentContentCompat` | setup entrypoint executes | command/output | `RunConsoleCommandArgumentContentSection` |
| covered | `ConsoleCommandArgumentMarshalling.ContentAndOrder` | `ASConsoleCommandArgumentContentCompat` | command exists after module build | command | `RunConsoleCommandArgumentContentSection` |
| covered | `ConsoleCommandArgumentMarshalling.ContentAndOrder` | `ASConsoleCommandArgumentContentCompat` | executing with ordered args succeeds | command | `RunConsoleCommandArgumentContentSection` |
| covered | `ConsoleCommandArgumentMarshalling.ContentAndOrder` | `ASConsoleCommandArgumentContentCompat` | output CVar receives `One|Two Words|Three=Value` | output | `RunConsoleCommandArgumentContentSection` |
| covered | `ConsoleCommandArgumentMarshalling.ContentAndOrder` | `ASConsoleCommandArgumentContentCompat` | command missing after module discard | command | `RunConsoleCommandArgumentContentSection` |

## AngelscriptConsoleCommandErrorBindingsTests.cpp

| Status | Automation ID | Old module | Old check | Registered objects | New section |
|--------|---------------|------------|-----------|--------------------|-------------|
| covered | `ConsoleCommandMissingHandlerCompat` | `ASConsoleCommandMissingHandlerCompat` | expected error contains missing handler text | command | `RunConsoleCommandMissingHandlerSection` |
| covered | `ConsoleCommandMissingHandlerCompat` | `ASConsoleCommandMissingHandlerCompat` | expected error contains module name | command | `RunConsoleCommandMissingHandlerSection` |
| covered | `ConsoleCommandMissingHandlerCompat` | `ASConsoleCommandMissingHandlerCompat` | Prepare succeeds and Execute raises exception | command | `RunConsoleCommandMissingHandlerSection` |
| covered | `ConsoleCommandMissingHandlerCompat` | `ASConsoleCommandMissingHandlerCompat` | exception text contains missing handler text | command | `RunConsoleCommandMissingHandlerSection` |
| covered | `ConsoleCommandMissingHandlerCompat` | `ASConsoleCommandMissingHandlerCompat` | command lookup remains null after failure | command | `RunConsoleCommandMissingHandlerSection` |

## AngelscriptConsoleCommandLifecycleBindingsTests.cpp

| Status | Automation ID | Old module | Old check | Registered objects | New section |
|--------|---------------|------------|-----------|--------------------|-------------|
| covered | `ConsoleCommandLifecycleOriginalReplacementUnload` | `ASConsoleCommandOriginalLifecycle` | original module compiles and registers command | command/output | `RunConsoleCommandLifecycleSection` |
| covered | `ConsoleCommandLifecycleOriginalReplacementUnload` | `ASConsoleCommandOriginalLifecycle` | original execution writes marker `11` | command/output | `RunConsoleCommandLifecycleSection` |
| covered | `ConsoleCommandLifecycleOriginalReplacementUnload` | `ASConsoleCommandReplacementLifecycle` | replacement module compiles and keeps command registered | command/output | `RunConsoleCommandLifecycleSection` |
| covered | `ConsoleCommandLifecycleOriginalReplacementUnload` | `ASConsoleCommandReplacementLifecycle` | replacement execution writes marker `22` | command/output | `RunConsoleCommandLifecycleSection` |
| covered | `ConsoleCommandLifecycleOriginalReplacementUnload` | `ASConsoleCommandOriginalLifecycle` | original module discard leaves replacement command registered | command | `RunConsoleCommandLifecycleSection` |
| covered | `ConsoleCommandLifecycleOriginalReplacementUnload` | `ASConsoleCommandReplacementLifecycle` | replacement still executes after original unload | command/output | `RunConsoleCommandLifecycleSection` |
| covered | `ConsoleCommandLifecycleOriginalReplacementUnload` | `ASConsoleCommandReplacementLifecycle` | replacement module discard removes command | command | `RunConsoleCommandLifecycleSection` |

## AngelscriptConsoleVariableIdentityTests.cpp

| Status | Automation ID | Old module | Old check | Registered objects | New section |
|--------|---------------|------------|-----------|--------------------|-------------|
| covered | `ConsoleVariableExistingIdentityCompat` | `ASConsoleVariableExistingIdentityCompat` | existing CVar initial value `7` reused instead of script default `99` | existing CVar | `RunConsoleVariableIdentitySection` |
| covered | `ConsoleVariableExistingIdentityCompat` | `ASConsoleVariableExistingIdentityCompat` | existing CVar Set/Get final `21` | existing CVar | `RunConsoleVariableIdentitySection` |
| covered | `ConsoleVariableExistingIdentityCompat` | `ASConsoleVariableExistingIdentityCompat` | native final value is `21` | existing CVar | `RunConsoleVariableIdentitySection` |
| covered | `ConsoleVariableExistingIdentityCompat` | `ASConsoleVariableExistingIdentityCompat` | native pointer identity is preserved | existing CVar | `RunConsoleVariableIdentitySection` |
| covered | `ConsoleVariableExistingIdentityCompat` | `ASConsoleVariableExistingIdentityCompat` | native help text is preserved | existing CVar | `RunConsoleVariableIdentitySection` |
| covered | `ConsoleVariableExistingIdentityCompat` | `ASConsoleVariableExistingIdentityCompat` | persistent flags outside `ECVF_SetByMask` are preserved | existing CVar | `RunConsoleVariableIdentitySection` |
| covered | `ConsoleVariableExistingIdentityCompat` | `ASConsoleVariableExistingIdentityCompat` | `ECVF_Cheat` remains set | existing CVar | `RunConsoleVariableIdentitySection` |

## New leak self-check

| Status | Automation ID | Check | New section |
|--------|---------------|-------|-------------|
| covered | `Console.LeakSelfCheck` | `IConsoleManager::ForEachConsoleObjectThatStartsWith(TEXT("as.test.console"), ...)` returns zero matches | `RunConsoleLeakSelfCheckSection` |
