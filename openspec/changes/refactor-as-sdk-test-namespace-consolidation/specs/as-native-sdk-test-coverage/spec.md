# as-native-sdk-test-coverage (delta)

## ADDED Requirements

### Requirement: SDK tests SHALL invoke specific named AS functions with typed arguments instead of monolithic Entry() self-checks

SDK tests that exercise script-level behavior SHALL call specific, named AS functions with typed arguments and assert their typed return values — one behavior per call — rather than compiling a single `bool Entry()` that performs all assertions internally and returns one aggregate bool. A shared raw-engine invoker helper (operating on `asIScriptEngine`/`asIScriptContext`, since SDK tests run below the `FAngelscriptEngine` wrapper) SHALL provide resolve-by-declaration, typed argument binding, and typed return reading, mirroring the ergonomics demonstrated by `Template_GlobalFunctions.cpp`'s `FASGlobalFunctionInvoker`.

#### Scenario: A shared raw-engine invoker exists

- **WHEN** the SDK test support headers are inspected
- **THEN** a shared invoker (e.g. `FSdkFunctionInvoker` / `CallSdkFunction<R>(...)`) exists in `namespace AngelscriptNativeTestSupport` (or `AngelscriptSDKTestExecutionHelpers.h`) that resolves an AS function by declaration, binds typed arguments, executes, and returns a typed result on a raw `asIScriptContext`

#### Scenario: Behavioral SDK tests assert per-function results, not one aggregate bool

- **WHEN** a behavioral SDK test (e.g. functions, operators, types, conversions, OOP) is inspected
- **THEN** it invokes named AS functions with arguments and asserts their individual typed returns
- **AND** it does not rely on a single `bool Entry()` that aggregates all assertions into one returned bool

### Requirement: Behavioral SDK tests SHALL execute and assert, not merely compile

A test claiming to cover runtime behavior SHALL execute the script and assert observable results; asserting only that a module compiles and a function resolves is insufficient for behavioral coverage. Tests SHALL be named to match what they actually verify.

#### Scenario: No compile-only false coverage for behavioral themes

- **WHEN** the object-lifetime, OOP, auto-type, and implicit-conversion tests are inspected
- **THEN** each behavioral case executes the relevant AS function(s) and asserts an observable result (return value, mutated state, destructor call count, or a deliberately-expected execution error)
- **AND** no behavioral case asserts only "module compiled" + "function resolved" without executing

#### Scenario: Test names match behavior

- **WHEN** a test named for a specific mechanism is inspected (e.g. a "Suspend" test, an "interface method" test)
- **THEN** the test actually exercises that mechanism (true suspend/resume; a script class implementing a script interface), or the test is renamed to describe what it really does
- **AND** helper scaffolding defined in a test file is actually used by at least one test (no dead operator/return helpers)

### Requirement: SDK tests SHALL cover core language semantics that are currently untested

The SDK suite SHALL add focused runtime coverage for the high-priority language/engine semantics that currently have zero or false coverage, each as per-behavior cases (not aggregate `Entry()`):

- containers: `array<T>`/`TArray<T>` runtime — length, index read/write, insert/remove, out-of-bounds exception;
- OOP polymorphism: base-handle dispatch to overridden methods, `override`/`final`/`abstract`, script class implementing a script interface, multiple interfaces;
- property accessors (`get`/`set`);
- handles: reference-count behavior, `cast<T>()` up/down-cast, null-handle dereference exception;
- object lifetime: destructor call count and order, object as a return value;
- functions: `&in` parameters and overload resolution including ambiguity negative cases;
- runtime control: true Suspend/Resume and distinct runtime exception varieties (array out-of-bounds, null handle, abort);
- script-class operator overloads: `opAdd`/`opEquals`/`opCmp`/`opAssign`.

#### Scenario: High-priority semantic gaps have executing coverage

- **WHEN** the SDK suite is inspected after this change
- **THEN** each high-priority semantic above has at least one executing test that asserts an observable result
- **AND** semantics blocked by a known fork limitation (e.g. string factory API difference, Thiscall execution crash) are recorded as explicit expected-fail or tracked follow-up items rather than silently omitted

## MODIFIED Requirements

### Requirement: Native SDK tests SHALL share helpers via AngelscriptNativeTestSupport.h

Cross-file helper utilities SHALL be inline header-only and shared. Helpers that are duplicated across two or more SDK test files SHALL be defined once in `namespace AngelscriptNativeTestSupport` (in `AngelscriptNativeTestSupport.h`) or in the dedicated `AngelscriptSDKTestExecutionHelpers.h` execution-helper header, rather than copy-pasted per file. Truly file-local, single-use helpers MAY remain in the file's anonymous namespace. Verbose `AngelscriptTest_*_Private` named namespaces SHALL NOT be used; uniqueness needed under Unity Build SHALL be achieved by sharing common symbols and giving genuinely divergent same-named helpers file-unique names.

#### Scenario: Shared accessors live in the support header

- **WHEN** the SDK test directory is inspected
- **THEN** `FTokenizerAccessor`, `FParserAccessor` (a superset exposing every snippet helper any test needs), `CreateSdkModule`, `FBytecodeFixture`, `CountInstructions`, `ContainsOpcode`, `FindFirstNodeOfType`, and `FMemoryBinaryStream` are each defined exactly once inside `namespace AngelscriptNativeTestSupport`
- **AND** no SDK test `.cpp` redefines those types/functions locally

#### Scenario: Execution helpers are shared via a template entry point

- **WHEN** a test needs to run an AS function and read its return value
- **THEN** it uses `AngelscriptSDKTestSupport::ExecuteScriptFunction<T>()` (or `ExecuteScriptVoidFunction`) from `AngelscriptSDKTestExecutionHelpers.h`
- **AND** no SDK test `.cpp` defines its own `Execute*Entry` helper

#### Scenario: No verbose private namespaces remain

- **WHEN** the SDK test directory is grep'd for `namespace AngelscriptTest_*_Private`
- **THEN** there are zero matches
- **AND** divergent same-named free helpers (e.g. the per-file `ParseScript` variants) carry file-unique names so they do not collide under Unity Build

#### Scenario: Module compiles under Unity Build

- **WHEN** `Tools\RunBuild.ps1` builds the editor target with Unity Build enabled
- **THEN** the `AngelscriptTest` module compiles with zero errors (no `C2011`/`C2084` redefinitions from merged translation units)

### Requirement: Native SDK tests SHALL use SDK naming and a full operator matrix

The SDK test directory SHALL use the `SDK`-prefixed naming (not `ASSDK`) for the formerly `ASSDK`-named files, classes, helper types, and automation IDs, and `AngelscriptSDKOperatorTests` SHALL cover a full operator matrix.

#### Scenario: ASSDK prefix is collapsed to SDK

- **WHEN** the SDK test directory and automation IDs are inspected
- **THEN** there are no `ASSDK` file names, `FAngelscriptASSDK*` class names, `FASSDK*` helper types, or `Angelscript.TestModule.AngelScriptSDK.ASSDK.*` automation paths
- **AND** their `SDK`-named equivalents exist instead

#### Scenario: Operator tests cover the full matrix

- **WHEN** `AngelscriptSDKOperatorTests.cpp` is inspected
- **THEN** it contains test methods covering arithmetic, comparison, logical, bitwise, assignment/compound, ternary, pow, opCall, opIndex, string concatenation, and short-circuit evaluation
