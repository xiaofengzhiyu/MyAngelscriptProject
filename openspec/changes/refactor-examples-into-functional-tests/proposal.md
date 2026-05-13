## Why

`Plugins/Angelscript/Source/AngelscriptTest/Examples/` currently duplicates the role of `Script/Examples/` while most of its tests only compile inline snippets through a dedicated `ScriptExamples.*` layer. That layer has low standalone value now that the project needs durable behavior coverage in the functional test suite.

This change retires the obsolete Examples test layer and absorbs only the still-useful behavior into existing UE functional test themes.

## What Changes

- **BREAKING**: Remove the `Angelscript.TestModule.ScriptExamples.*` automation prefix and the `AngelscriptExamples` automation group as active test entry points.
- Remove `Plugins/Angelscript/Source/AngelscriptTest/Examples/` and `AngelscriptScriptExampleTestSupport.*` after useful behavior coverage has moved.
- Convert behavior-rich Examples coverage, especially actor, component, UObject, property metadata, timer, construction script, overlap, delegate, widget, and movement cases, into focused `Functional/**` tests.
- Delete pure compile-only or duplicative Examples tests when the behavior is already covered by runtime, bindings, syntax, or functional tests.
- Keep `Script/Examples/**` as the example asset surface; this change does not rewrite or remove those script examples.
- Update automation group configuration and test documentation so current testing guidance no longer presents Examples as an active layer.

## Capabilities

### New Capabilities

- `examples-functional-coverage`: Defines how Examples-derived behavior is retained through functional tests while the obsolete ScriptExamples test layer is retired.

### Modified Capabilities

- None.

## Impact

- Test code under `Plugins/Angelscript/Source/AngelscriptTest/Examples/` and `Plugins/Angelscript/Source/AngelscriptTest/Functional/**`.
- Existing neighboring test themes under `Plugins/Angelscript/Source/AngelscriptTest/Actor/`, `Component/`, `Delegate/`, `Widget/`, `Property/`, `Objects/`, `Functions/`, or their `Functional/<Theme>/` equivalents if they are the better local fit.
- Automation group configuration in `Config/DefaultEngine.ini`.
- Test documentation under `Documents/Guides/Test.md`, `Documents/Guides/TestConventions.md`, `Documents/Guides/TestCatalog.md`, and any current technical-debt notes that still list `ScriptExamples.*` as an active target.
- Verification uses the project standard build and test runners: `Tools\RunBuild.ps1`, `Tools\RunTests.ps1`, and `Tools\RunTestSuite.ps1`.
