# examples-functional-coverage Specification

## Purpose
TBD - created by archiving change refactor-examples-into-functional-tests. Update Purpose after archive.
## Requirements
### Requirement: ScriptExamples test layer is retired
The test suite SHALL retire the Examples-specific test layer after useful behavior has been absorbed into current functional tests.

#### Scenario: ScriptExamples automation prefix is absent
- **WHEN** the test module is scanned after this change is applied
- **THEN** no automation test is registered under `Angelscript.TestModule.ScriptExamples.*`

#### Scenario: Examples test support is removed
- **WHEN** Examples-derived behavior has moved to functional tests
- **THEN** `AngelscriptScriptExampleTestSupport.*` and its `RunScriptExampleCompileTest()` helper no longer exist in the test module

#### Scenario: Examples test directory is absent
- **WHEN** the test module source tree is inspected after cleanup
- **THEN** `Plugins/Angelscript/Source/AngelscriptTest/Examples/` no longer contains active test sources

### Requirement: Useful Examples behavior is retained in functional tests
The test suite SHALL preserve Examples-derived behavior that validates runtime UE semantics by moving it into theme-owned functional tests.

#### Scenario: Behavior-rich coverage tests have functional assertions
- **WHEN** actor defaults, component lifecycle, UObject behavior, or property metadata behavior from `AngelscriptScriptExampleCoverageTests.cpp` remains valuable
- **THEN** equivalent assertions exist under the appropriate functional theme using `Angelscript.TestModule.Functional.*` or an established theme-specific functional prefix

#### Scenario: Runtime examples become behavior tests
- **WHEN** an Examples test covers behavior such as timers, construction script execution, component overlaps, delegate invocation, widget binding, actor movement, or object lifecycle
- **THEN** the retained coverage asserts the runtime behavior rather than only compiling the original example snippet

#### Scenario: Compile-only duplicates are intentionally removed
- **WHEN** an Examples test only demonstrates syntax or API calls already covered by current syntax, bindings, runtime integration, or functional tests
- **THEN** the implementation records that classification and removes the test without creating a new low-value functional duplicate

### Requirement: Functional ownership remains theme-based
Examples-derived tests SHALL be placed in existing functional ownership areas rather than recreated as a broad Examples category.

#### Scenario: Existing theme directory can own the behavior
- **WHEN** an absorbed behavior belongs to an existing theme such as Actor, Component, Delegate, Widget, Property, Objects, Functions, Types, Execution, or Subsystem
- **THEN** the test is added to that theme's existing functional directory or adjacent established test file

#### Scenario: New functional theme is justified
- **WHEN** an absorbed behavior does not fit an existing theme
- **THEN** any new functional directory has a concrete behavior name and corresponding automation prefix, not `Examples`

### Requirement: Current test entry points and docs reflect functional ownership
The project SHALL remove Examples-specific automation entry points and document functional/theme-owned coverage as the active test surface.

#### Scenario: Examples automation group is removed
- **WHEN** `Config/DefaultEngine.ini` is inspected after cleanup
- **THEN** there is no active `AngelscriptExamples` group pointing at `Angelscript.TestModule.ScriptExamples.*`

#### Scenario: Test documentation no longer lists Examples as a current layer
- **WHEN** current test guides and catalog documents are inspected
- **THEN** they no longer present `Plugins/Angelscript/Source/AngelscriptTest/Examples/` or `Angelscript.TestModule.ScriptExamples.*` as an active testing layer

#### Scenario: Functional verification commands are documented
- **WHEN** documentation or implementation tasks describe how to verify this change
- **THEN** they use the project standard runners for targeted functional prefixes, the functional suite, and a standard build

