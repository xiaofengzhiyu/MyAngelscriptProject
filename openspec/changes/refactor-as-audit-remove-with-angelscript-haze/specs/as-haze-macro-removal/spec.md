# as-haze-macro-removal Specification

## Purpose

Specifies the post-removal state of the `WITH_ANGELSCRIPT_HAZE` compile-time fork. After this change applies, the macro does not exist, the Hazelight-only RPC machinery is gone, the debugger protocol no longer carries a Haze flag, and the active-path bindings that were renamed only to dodge the auto-property-accessor system are restored to their UE-original names.

## ADDED Requirements

### Requirement: Macro is fully absent

The `WITH_ANGELSCRIPT_HAZE` preprocessor symbol SHALL NOT be defined anywhere in the repository, and no source file SHALL test for or guard code on its value.

#### Scenario: Macro definition is absent from the runtime header

- **WHEN** `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` is read
- **THEN** the file SHALL NOT contain `#ifndef WITH_ANGELSCRIPT_HAZE`, `#define WITH_ANGELSCRIPT_HAZE`, or any reference to the macro

#### Scenario: Repository search returns zero matches

- **WHEN** `rg -n "WITH_ANGELSCRIPT_HAZE"` runs from the repository root
- **THEN** zero matches SHALL be returned

### Requirement: AActor instigator accessors use UE-original names

The AngelScript `AActor` binding SHALL expose the instigator helpers under the UE-canonical names `GetInstigator()` and `GetInstigatorController()` rather than the previous `GetActorInstigator` / `GetActorInstigatorController` deviations.

#### Scenario: Script calls GetInstigator on an actor

- **WHEN** a script invokes `actor.GetInstigator()` on an `AActor` reference
- **THEN** the call SHALL compile and SHALL return the actor's `Instigator` `APawn` (or `null` when none is set)

#### Scenario: Script calls GetInstigatorController on an actor

- **WHEN** a script invokes `actor.GetInstigatorController()` on an `AActor` reference
- **THEN** the call SHALL compile and SHALL return the actor's instigator-derived `AController` (or `null` when the instigator is absent or unpossessed)

#### Scenario: Old names no longer compile

- **WHEN** a script invokes `actor.GetActorInstigator()` or `actor.GetActorInstigatorController()`
- **THEN** compilation SHALL fail with the standard "no matching method" diagnostic

### Requirement: Hazelight-only RPC specifiers are unrecognised

The fork SHALL NOT recognise the Hazelight-only UFUNCTION specifiers `NetFunction`, `CrumbFunction`, or `DevFunction`.

#### Scenario: NetFunction specifier on a UFUNCTION

- **WHEN** an AngelScript source declares `UFUNCTION(NetFunction)`
- **THEN** the AngelScript preprocessor SHALL emit an "unknown UFUNCTION specifier" error

#### Scenario: CrumbFunction or DevFunction specifier on a UFUNCTION

- **WHEN** an AngelScript source declares `UFUNCTION(CrumbFunction)` or `UFUNCTION(DevFunction)`
- **THEN** the AngelScript preprocessor SHALL emit an "unknown UFUNCTION specifier" error

#### Scenario: HazeFunctionFlags is no longer set on generated UFunctions

- **WHEN** `AngelscriptClassGenerator` finalizes a script-defined UFunction
- **THEN** the resulting `UFunction` SHALL NOT carry `FUNC_NetFunction`, `FUNC_DevFunction`, or any `HazeFunctionFlags` bit

### Requirement: AsyncTrace globals live in the System namespace

Asynchronous trace global helpers (`AsyncLineTraceByChannel`, `AsyncLineTraceByObjectType`, etc.) SHALL be exposed only under the `System` namespace.

#### Scenario: Calling async trace from script

- **WHEN** a script invokes asynchronous tracing helpers
- **THEN** the script SHALL write `System::AsyncLineTraceByChannel(...)` and similar
- **AND** the alternative `AsyncTrace::` namespace SHALL NOT be registered

### Requirement: AS_ENSURE is permanently `ensureMsgf`

The `AS_ENSURE` macro defined in `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` SHALL expand to `ensureMsgf` unconditionally.

#### Scenario: ASClass.cpp defines AS_ENSURE without conditional

- **WHEN** `ASClass.cpp` is read
- **THEN** the file SHALL contain `#define AS_ENSURE ensureMsgf` without any `#if` guard
- **AND** the alternate `devEnsure` mapping SHALL NOT be present

### Requirement: Debugger protocol no longer carries the haze flag

`FAngelscriptDebugDatabaseSettings` SHALL NOT contain a `bUseAngelscriptHaze` field, and the AngelScript debug server SHALL NOT emit a value for that field.

#### Scenario: Protocol struct is missing the field

- **WHEN** the `FAngelscriptDebugDatabaseSettings` definition is searched
- **THEN** no `bUseAngelscriptHaze` member SHALL exist

#### Scenario: Debug server does not assign the field

- **WHEN** `AngelscriptDebugServer.cpp` is compiled
- **THEN** no statement SHALL assign to `DebugSettings.bUseAngelscriptHaze`

#### Scenario: Older IDE clients tolerate the missing field

- **WHEN** an older VSCode AngelScript extension reads the new `DebugDatabaseSettings` JSON payload
- **THEN** the missing `bUseAngelscriptHaze` field SHALL deserialize as `false`
- **AND** the extension's behaviour SHALL match its prior behaviour against the macro-disabled (default) runtime

### Requirement: Test asserting Haze-mirror is removed

The automation test `AngelscriptDebuggerDatabaseTests` SHALL NOT include the assertion that mirrored `bUseAngelscriptHaze` against `!!WITH_ANGELSCRIPT_HAZE`.

#### Scenario: Searching the test for the mirror assertion

- **WHEN** `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDatabaseTests.cpp` is searched for `bUseAngelscriptHaze`
- **THEN** zero matches SHALL be returned

### Requirement: Functional baseline holds after macro removal

The catalogued automation baseline SHALL pass after the change is applied.

#### Scenario: Functional suite passes end-to-end

- **WHEN** `Tools\RunTests.ps1 -Suite Functional` runs after this change is applied
- **THEN** the catalogued 275/275 baseline SHALL pass without newly disabled tests
