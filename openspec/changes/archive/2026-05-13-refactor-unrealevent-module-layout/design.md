## Context

The standalone `UnrealEvent` plugin currently declares a single active module named `GMP`. Its source is still GMP-derived and contains many `GMP_*` symbols, `GMP_API` declarations, and includes under a `GMP/` public subfolder. A full symbol/API rename would be high risk and belongs in a later change.

Angelscript uses three UE modules for runtime, editor integration, and tests. For `UnrealEvent`, the requested naming omits a `Runtime` suffix, so the runtime module should be named exactly `UnrealEvent`, with separate `UnrealEventEditor` and `UnrealEventTest` editor modules.

## Goals / Non-Goals

**Goals:**

- Make the active runtime module name `UnrealEvent`.
- Add buildable `UnrealEventEditor` and `UnrealEventTest` module skeletons.
- Keep the GMP-derived runtime source compiling with minimal mechanical compatibility changes.
- Keep module loading phases aligned with the existing UnrealEvent bootstrap unless build evidence requires adjustment.

**Non-Goals:**

- Do not rename every `GMP` C++ type, namespace, Blueprint category, config key, or include path.
- Do not move editor features from the disabled GMP editor modules into `UnrealEventEditor`.
- Do not add behavioral tests in `UnrealEventTest` yet.
- Do not delete the `Source/GMPEditor` reference tree in this change.

## Decisions

- **Runtime module is `UnrealEvent`, not `UnrealEventRuntime`.** This follows the user request while still matching the runtime/editor/test split used by Angelscript.
- **Keep `GMP_API` as a compatibility alias.** UBT will generate `UNREALEVENT_API` for the renamed module. Defining `GMP_API=UNREALEVENT_API` in the renamed module keeps existing exported declarations compiling without editing hundreds of source references.
- **Keep the public `GMP/` include folder for now.** Existing includes such as `GMP/GMPBPLib.h` remain valid and avoid a broad include churn.
- **Add empty module implementations for editor and test.** These modules establish the build boundary now; real editor tooling and automation tests can be added under later OpenSpec changes.

## Risks / Trade-offs

- **Generated header path drift** -> Keep source file paths and UCLASS names unchanged except for the UE module folder/build name.
- **Runtime references to `/Script/GMP`** -> This change may leave redirect/config strings as legacy compatibility references; audit and migration can happen in a future API rename change.
- **Editor/test modules increase build surface** -> Keep skeletons minimal and verify with the host build runner.
- **Submodule pointer drift** -> Commit runtime/module changes inside `Plugins/UnrealEvent`, then stage only the parent gitlink and OpenSpec artifacts in the host repository.

## Migration Plan

- Rename the runtime module folder and `.Build.cs` class from `GMP` to `UnrealEvent`.
- Update `UnrealEvent.uplugin` to declare `UnrealEvent`, `UnrealEventEditor`, and `UnrealEventTest`.
- Update the runtime module implementation macro to `IMPLEMENT_MODULE(..., UnrealEvent)`.
- Update active `/Script/GMP` reflection metadata to `/Script/UnrealEvent`, retaining redirects for legacy object paths where the runtime already provided them.
- Add minimal `UnrealEventEditor` and `UnrealEventTest` module build files and implementation files.
- Run descriptor JSON validation, host build verification, and OpenSpec validation before committing.

## Open Questions

- When should the project rename public `GMP` symbols, Blueprint categories, and script paths to `UnrealEvent`?
- Should `UnrealEventTest` later use CQTest or plain UE automation tests for its first behavioral coverage?
