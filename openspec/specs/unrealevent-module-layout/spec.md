# unrealevent-module-layout Specification

## Purpose
Defines the standalone `UnrealEvent` plugin module layout after the GMP bootstrap: a runtime module named `UnrealEvent`, plus explicit editor and test module boundaries for later feature work.

## Requirements
### Requirement: Runtime module uses the UnrealEvent name

The `UnrealEvent` plugin SHALL expose its active runtime module as `UnrealEvent` rather than `GMP`.

#### Scenario: Plugin descriptor is inspected

- **WHEN** `Plugins/UnrealEvent/UnrealEvent.uplugin` is inspected
- **THEN** its runtime module entry is named `UnrealEvent`
- **AND** it does not declare an active runtime module named `GMP`

### Requirement: Editor and test module boundaries exist

The `UnrealEvent` plugin SHALL declare separate `UnrealEventEditor` and `UnrealEventTest` modules for future editor integration and automation coverage.

#### Scenario: Module list is inspected

- **WHEN** `Plugins/UnrealEvent/UnrealEvent.uplugin` is inspected
- **THEN** it declares `UnrealEventEditor` as an editor module
- **AND** it declares `UnrealEventTest` as an editor test module
- **AND** both modules depend on the `UnrealEvent` runtime module rather than the old `GMP` module

### Requirement: GMP-derived runtime remains buildable

The module rename SHALL preserve build compatibility for the GMP-derived runtime source until a later API rename change.

#### Scenario: Host project build discovers renamed modules

- **WHEN** the host project build is run through the documented build runner
- **THEN** Unreal Build Tool discovers `UnrealEvent`, `UnrealEventEditor`, and `UnrealEventTest`
- **AND** the build completes without module-name or export-macro errors caused by the rename

#### Scenario: Reflection metadata uses the active module package

- **WHEN** active runtime metadata paths are inspected
- **THEN** generated reflection metadata for UnrealEvent runtime types uses `/Script/UnrealEvent`
- **AND** legacy `/Script/GMP` redirects remain only where they preserve compatibility with old object paths
