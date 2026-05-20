## ADDED Requirements

### Requirement: Cross-module direct-bind shards are emitted into the target module's intermediate directory

The Angelscript UHT tool SHALL emit per-module cross-module direct-bind shard files (`AS_FunctionTable_<Module>_CrossModule_<NNN>.cpp`) into the OutputDirectory of the target module — not the AngelscriptRuntime module — for every `BlueprintCallable`/`BlueprintPure` UFunction that is in scope of the supported module set, not skipped (`NotInAngelscript`, `BlueprintInternalUseOnly` without `UsableInAngelscript`, `CustomThunk`, `Private/` headers, interface classes, **RPC/Net functions**), and previously classified as `unexported-symbol` because its C++ implementation lives in a module other than `AngelscriptRuntime` without an `<MODULE>_API` / `UE_API` / inline / FORCEINLINE / constexpr decoration.

#### Scenario: Source-build target with a previously unexported-symbol function

- **WHEN** the project is built as a Source-build target and a `BlueprintCallable` UFunction whose C++ implementation lives in `Engine` module (no `ENGINE_API`, no inline, not in a `Private/` header, not RPC/Net) is in scope
- **THEN** UHT emits one entry for that function into a file matching `AS_FunctionTable_Engine_CrossModule_*.cpp` placed under `Engine`'s OutputDirectory (not under `AngelscriptRuntime`'s OutputDirectory)
- **AND** the entry exposes a raw `void(*)(UObject* Self, void** Args, void* Ret)` thunk that takes the address of the target function from inside the `Engine` module's translation unit
- **AND** UBT compiles and links the new file as part of the `Engine` module without requiring any change to `Engine`'s `Build.cs`

#### Scenario: Function with a Private/ header is not emitted as cross-module

- **WHEN** a candidate function's class header path contains `/Private/`
- **THEN** UHT does NOT emit a cross-module entry for it
- **AND** the entry remains accounted for in `AS_FunctionTable_SkippedReasonSummary.csv` under the `private-header` (or pre-existing) reason

#### Scenario: Output file naming does not collide with engine CodeGen filters

- **WHEN** the cross-module shard files exist alongside the engine `CodeGen` exporter's outputs in the same module's OutputDirectory
- **THEN** the cross-module shard filenames do NOT match any of `*.generated.cpp`, `*.generated.*.cpp`, `*.gen.cpp`, `*.gen.*.cpp`
- **AND** the engine `CodeGen` exporter's `CullOutput` does NOT delete them on subsequent UHT runs

### Requirement: RPC and Net-replicated functions are excluded from cross-module direct bind

UFunctions whose `FunctionFlags` include any of `FUNC_Net`, `FUNC_NetServer`, `FUNC_NetClient`, `FUNC_NetMulticast` SHALL NOT be emitted as cross-module direct-bind entries. They MUST continue to dispatch through `BlueprintCallableReflectiveFallback` so that `UFunction::Invoke` / `UObject::ProcessEvent`'s built-in RPC routing (server-only / client-only / multicast / `WithValidation`) is preserved. Bypassing this routing would break network replication semantics — Server-only functions executing on clients, NetMulticast not multicasting, WithValidation not validating, etc. — which is a correctness defect, not a performance defect.

#### Scenario: Server-only function is not direct-bound

- **WHEN** UHT processes a `BlueprintCallable, Server, Reliable` UFunction in a public engine header
- **THEN** no cross-module shard entry is emitted for it
- **AND** `AS_FunctionTable_SkippedReasonSummary.csv` records the entry under reason `rpc-net-function`
- **AND** at script call time the function still routes through `BlueprintCallableReflectiveFallback`, dispatching via `UFunction::Invoke` so the engine's RPC layer correctly marshals the call to the server

#### Scenario: NetMulticast function preserves multicast behavior

- **WHEN** an Angelscript script calls a `NetMulticast` UFunction on a replicated actor
- **THEN** the dispatch goes through reflection fallback (NOT cross-module direct bind)
- **AND** the multicast is observed on every connected client, identical to a C++ caller

#### Scenario: WithValidation function still runs validation

- **WHEN** an Angelscript script calls a `Server, WithValidation` UFunction
- **THEN** the dispatch goes through reflection fallback
- **AND** the `_Validate` companion is invoked by the engine prior to executing `_Implementation`, identical to pre-change behavior

### Requirement: Cross-module shard ABI uses IModularFeatures-registered POD payload with raw thunks

The cross-module shard contract SHALL expose its bindings to AngelscriptRuntime exclusively through the Core-provided `IModularFeatures` registry. The payload is a POD `FAngelscriptCrossModuleEntry` table embedded in an `IModularFeature`-derived feature struct that adds **only POD data members and no new virtual methods**. Because `IModularFeature` declares a virtual destructor in Core, the derived struct is NOT a C++ aggregate — instances MUST be created via a constructor, NEVER via brace-aggregate-init. AngelscriptRuntime MUST NOT link any engine module to access these payloads. Member function pointers, `FGenericFuncPtr`, `ASAutoCaller`-style template instantiations, AngelScript SDK types (`asIScriptGeneric`, `asSFuncPtr`, etc.), and any AngelscriptRuntime-internal types MUST NOT appear at the cross-module boundary.

#### Scenario: AngelscriptRuntime does not link any engine module for cross-module dispatch

- **WHEN** `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` is inspected after this change lands
- **THEN** its `PublicDependencyModuleNames` and `PrivateDependencyModuleNames` do NOT contain any engine module added solely to resolve a cross-module direct-bind symbol
- **AND** `Bind_CrossModuleDirect.cpp` accesses cross-module entries only through `IModularFeatures::Get().GetModularFeatureImplementations(FName("AngelscriptCrossModuleBindings"))` and the `OnModularFeatureRegistered` delegate, never through any `extern Get_AS_Bindings_<Module>` declaration

#### Scenario: Public header for cross-module bindings does not depend on AS runtime types or AS SDK

- **WHEN** the public header `Plugins/Angelscript/Source/AngelscriptRuntime/Public/UHT/AngelscriptCrossModuleBindings.h` is inspected
- **THEN** it declares only the POD `FAngelscriptCrossModuleEntry { const TCHAR* ClassName; const TCHAR* FunctionName; void (*Thunk)(class UObject* Self, void** Args, void* Ret); uint16 ArgCount; uint16 RetSize; uint32 Flags; }`, the layout-matching reader struct `FAngelscriptCrossModuleFeatureReader`, the `LayoutVersionExpected` constant, and double-sided `static_assert(sizeof(...))` macros
- **AND** the header does NOT pull in `Core/AngelscriptBinds.h`, `Core/FunctionCallers.h`, `FAngelscriptBinds`, `ASAutoCaller`, `FGenericFuncPtr`, `FMethodPtrHelper`, `angelscript.h`, or any AngelScript SDK header (no forward declaration of `asIScriptGeneric` either)

#### Scenario: Generator-emitted cpp does not include any AS plugin or SDK header

- **WHEN** any UHT-emitted `AS_FunctionTable_<Module>_CrossModule_<NNN>.cpp` is inspected
- **THEN** the only headers it `#include`s are `Features/IModularFeatures.h` (Core) and the minimal target-class headers required to take addresses
- **AND** it does NOT `#include` `angelscript.h`, `AngelscriptCrossModuleBindings.h`, or any other AngelscriptRuntime / AngelScript SDK header
- **AND** the cross-module ABI struct definitions (`FAngelscriptCrossModuleEntry`, `FAngelscriptCrossModuleFeature`) are emitted inline in the cpp by the generator (kept in sync with the runtime header by the generator)

#### Scenario: Feature struct uses constructor-based instantiation, never aggregate-init

- **WHEN** any UHT-emitted cpp or the AngelscriptRuntime-side example references is inspected
- **THEN** `FAngelscriptCrossModuleFeature` declares a constructor `FAngelscriptCrossModuleFeature(const FAngelscriptCrossModuleEntry*, int32, const TCHAR*, uint32)`
- **AND** the static instance is created via `static FAngelscriptCrossModuleFeature GFeature(GTable, UE_ARRAY_COUNT(GTable), TEXT("<ModuleName>"), 0xA5C0DE01u);`
- **AND** the file does NOT contain any `static FAngelscriptCrossModuleFeature ... = { ... };` brace-aggregate-init form (which would fail to compile because the parent class has a user-provided virtual destructor)

#### Scenario: Generated thunk uses raw signature; asIScriptGeneric bridging is centralized in AngelscriptRuntime

- **WHEN** a cross-module shard is generated for a non-static, non-interface, non-CustomThunk, non-RPC `BlueprintCallable` UFunction
- **THEN** the generated thunk has the signature `static void Thunk_<Class>_<Func>(UObject* Self, void** Args, void* Ret)` — no AS SDK types
- **AND** AngelscriptRuntime registers a single shared generic hook `void(asIScriptGeneric*)` to AngelScript via `BindMethodDirect(..., asCALL_GENERIC, ...)` from the Late+60 bind stage
- **AND** that shared hook unpacks `asIScriptGeneric` arguments into a raw `void**` Args array and a raw Ret buffer, calls the entry's raw `Thunk`, then writes Ret and out-params back to `asIScriptGeneric`
- **AND** no `asSFuncPtr`, member-function pointer, `FGenericFuncPtr`, or PMF binary bits cross the module boundary

### Requirement: Complex UFunction forms have well-defined thunk marshalling contracts

The generator SHALL emit thunk bodies that correctly marshal the following UFunction signature forms; the AngelscriptRuntime-side shared generic hook SHALL provide the matching `asIScriptGeneric*` ↔ raw-buffer translation for each. The `Flags` bitfield in `FAngelscriptCrossModuleEntry` carries: `bit0 Static`, `bit1 Const`, `bit2 WorldContext`, `bit3 HasOutParams`, `bit4 ReturnByRef`, remaining bits reserved.

#### Scenario: out-param functions write back through Args slots

- **WHEN** the target UFunction has a non-const reference parameter or `UPARAM(ref)` (e.g., `void GetClampedActorBounds(FVector& OutMin, FVector& OutMax)`)
- **THEN** the generated thunk receives `Args[i]` as a pointer into an AS-side temporary buffer; the body invokes the function passing `*static_cast<Type*>(Args[i])` so the function writes through the buffer
- **AND** after the thunk returns, the AngelscriptRuntime hook writes `*Args[i]` back into the corresponding out-param slot of `asIScriptGeneric`
- **AND** the entry's `Flags & bit3 HasOutParams` is set so the hook's writeback loop is enabled

#### Scenario: static functions invoke without Self

- **WHEN** the target UFunction has `FUNC_Static` flag
- **THEN** `Self == nullptr` is passed to the thunk; the body calls `T::Func(...)` rather than `static_cast<T*>(Self)->Func(...)`
- **AND** the AngelscriptRuntime registration uses `BindGlobalFunction` (in namespace `<ClassName>`) rather than `BindMethodDirect`
- **AND** `Flags & bit0 Static` is set

#### Scenario: WorldContextObject parameter is injected by the hook

- **WHEN** the target UFunction has `meta = (WorldContext = "<ParamName>")`
- **THEN** the generator emits the WorldContext parameter as an explicit Args slot in its declared position
- **AND** the AngelscriptRuntime hook detects `Flags & bit2 WorldContext` and supplies the `UWorld*` (or owning `UObject*`) from the AS calling context to fill that slot before invoking the thunk

#### Scenario: const-qualified methods preserve const

- **WHEN** the target UFunction is declared `const`
- **THEN** the thunk body invokes via `static_cast<const T*>(Self)->Func()`
- **AND** the AngelscriptRuntime registration declares the AS method with `const` modifier (driven by `Flags & bit1 Const`)

#### Scenario: non-trivial value parameters are marshalled by value

- **WHEN** the target UFunction has parameters of type `FString`, `FName`, `FText`, or `TArray<X>` / `TSet<X>` / `TMap<K,V>`
- **THEN** the generator emits the thunk body to copy the value (`FString S = *static_cast<FString*>(Args[i]);`) before passing it to the target function, avoiding aliasing into AS-side temporaries
- **AND** the AngelscriptRuntime hook allocates stack-life buffers of those types whose lifetime spans the call

#### Scenario: object/class pointer wrappers are marshalled by value

- **WHEN** the target UFunction has parameters of type `TSubclassOf<X>`, `TSoftObjectPtr<X>`, `TSoftClassPtr<X>`, `TWeakObjectPtr<X>`
- **THEN** the generated thunk treats the parameter as an opaque `sizeof(...)`-byte value at `Args[i]` and copies it into a local before invocation
- **AND** the AngelscriptRuntime registration matches the type's existing AS SDK registration shape (typically opaque or value-by-ref)

#### Scenario: SIMD-aligned struct parameters honor 16-byte alignment

- **WHEN** the target UFunction has parameters of type `FVector`, `FRotator`, `FTransform`, or any other SIMD-aligned struct
- **THEN** the AngelscriptRuntime hook allocates the corresponding Args slot with `alignas(16)` (or the type's required alignment)
- **AND** the thunk dereferences the slot through a properly aligned cast

#### Scenario: UENUM tags are passed at the underlying width

- **WHEN** the target UFunction has parameters of `UENUM` types backed by `uint8` / `int32` / `uint64`
- **THEN** the Args slot holds the underlying-width integer; the thunk reinterprets via `static_cast<EnumType>(*static_cast<UnderlyingType*>(Args[i]))`

### Requirement: AngelscriptRuntime injects cross-module entries at Late+60 via IModularFeatures, without overriding same-module direct binds

`Bind_CrossModuleDirect.cpp` in `AngelscriptRuntime` SHALL run at `EOrder::Late + 60` (strictly after the existing Late+50 same-module shards), enumerate `IModularFeatures::Get().GetModularFeatureImplementations(FName("AngelscriptCrossModuleBindings"))`, reinterpret_cast each `IModularFeature*` to the `FAngelscriptCrossModuleFeatureReader` layout, validate `LayoutVersion` and basic invariants, then write entries into `FAngelscriptBinds::ClassFuncMaps` only when the slot is empty. Multiple feature instances may share the same `ModuleName` (large modules sharded across multiple `AS_FunctionTable_<Module>_CrossModule_<NNN>.cpp` files each register their own anonymous feature); AngelscriptRuntime MUST iterate all returned implementations and not deduplicate by `ModuleName`.

#### Scenario: Bind_CrossModuleDirect uses IModularFeatures, never extern declarations

- **WHEN** `Bind_CrossModuleDirect.cpp` is inspected
- **THEN** it discovers cross-module bindings exclusively via `IModularFeatures::Get().GetModularFeatureImplementations(FName("AngelscriptCrossModuleBindings"))`
- **AND** it contains NO `extern <MODULE>_API const FAngelscriptCrossModuleEntry* Get_AS_Bindings_<Module>(int32&)` declarations
- **AND** it subscribes to `IModularFeatures::OnModularFeatureRegistered` to handle modules registered after Late+60 has already run

#### Scenario: Multiple shards from the same module register independent feature instances

- **WHEN** module `<M>` has more than `MaxEntriesPerShard` cross-module entries and is therefore split into `AS_FunctionTable_<M>_CrossModule_000.cpp`, `_001.cpp`, ...
- **THEN** each shard file declares its own anonymous `FAngelscriptCrossModuleFeature` instance and registers it independently with `IModularFeatures`
- **AND** `IModularFeatures::Get().GetModularFeatureImplementations(FName("AngelscriptCrossModuleBindings"))` returns all of them with `ModuleName == TEXT("<M>")`
- **AND** AngelscriptRuntime iterates the returned array as-is; `ModuleName` is used only for diagnostic logging, not for deduplication

#### Scenario: Same-module direct bind has priority

- **WHEN** a function is already present in `FAngelscriptBinds::ClassFuncMaps` because the existing Late+50 same-module shard already supplied a direct PMF-based entry
- **THEN** the Late+60 cross-module pass does NOT overwrite that entry
- **AND** the resulting `Entry->FuncPtr.IsBound()` remains `true` from the same-module shard

#### Scenario: Previously-stub cross-module function becomes direct-bound

- **WHEN** in a Source-build target a function previously stub-bound (`ERASE_NO_FUNCTION()`) due to `unexported-symbol` is now emitted as a cross-module entry
- **THEN** after the Late+60 stage runs, `FAngelscriptBinds::GetClassFuncMaps()[OwningClass][FunctionName].FuncPtr.IsBound()` returns `true`
- **AND** `Bind_BlueprintCallable.cpp`'s subsequent execution takes the direct-bind branch (`bHasDirectNativePointer == true`) and does NOT invoke `BindBlueprintCallableReflectionFallback`

### Requirement: Modular and Monolithic build configurations both produce a working binding without configuration

The dispatch system SHALL automatically select between the cross-module direct-bind path and the existing reflection fallback at runtime, with no project configuration, env var, compile-time macro, or feature flag required. Both Modular (Editor, DebugGame, Development, Test) and Monolithic (Shipping) build configurations SHALL exercise the same `IModularFeatures`-based path; Shipping is NOT permitted to silently downgrade to reflection fallback when the cross-module shard exists.

#### Scenario: Modular build uses cross-module direct-bind through IModularFeatures

- **WHEN** the project is built as a Modular target (e.g., Editor) and the cross-module shard for module `<M>` was emitted
- **THEN** `<M>`'s static initializer calls `IModularFeatures::Get().RegisterModularFeature(FName("AngelscriptCrossModuleBindings"), &GFeature_<M>)` at DLL load
- **AND** `Bind_CrossModuleDirect.cpp` retrieves the feature at Late+60 and registers entries via the shared `asCALL_GENERIC` hook
- **AND** at script call time, `BlueprintCallableReflectiveFallback::InvokeReflectiveUFunctionFromGenericCall_Bridge` is NOT invoked for the corresponding UFunctions

#### Scenario: Monolithic Shipping build uses the same path

- **WHEN** the project is built as a Monolithic Shipping target and the cross-module shard for module `<M>` was emitted as part of the same build
- **THEN** the same `IModularFeatures`-based path is exercised; `<M>`'s static initializer registers the feature, AngelscriptRuntime retrieves it at Late+60, and direct-bind is in effect
- **AND** behavior is observably identical to the Modular Editor build for the same UFunctions

#### Scenario: Launcher / installed-engine target gracefully falls back

- **WHEN** the project is built against an installed/Launcher engine where engine modules are not recompiled and therefore no cross-module shard exists in their OutputDirectory
- **THEN** the build still links successfully (AngelscriptRuntime has no link-time dependency on any cross-module symbol)
- **AND** `IModularFeatures::Get().GetModularFeatureImplementations(FName("AngelscriptCrossModuleBindings"))` returns an empty array for the missing modules
- **AND** at runtime, the corresponding UFunctions take the existing `BlueprintCallableReflectionFallback` path with identical observable behavior to the pre-change baseline

### Requirement: HasLinkableExport no longer rejects cross-module unexported symbols

`AngelscriptHeaderSignatureResolver.HasLinkableExport` SHALL stop classifying "module is not AngelscriptRuntime + function lacks `<MODULE>_API` / `UE_API` / inline / FORCEINLINE / constexpr / class lacks API and is not MinimalAPI" as `unexported-symbol`. The "real-unreachable" filters (`Private/` header, `CustomThunk`, `Interface` stubs, **RPC/Net function**, target module not in the supported module list) MUST remain.

#### Scenario: Engine-module function without ENGINE_API is no longer skipped as unexported-symbol

- **WHEN** UHT processes a non-RPC `BlueprintCallable` function declared in a public Engine header without `ENGINE_API` / `UE_API` / inline / FORCEINLINE / constexpr decoration, in a class without API macro and not `MinimalAPI`
- **THEN** the function is NOT recorded as `unexported-symbol` in `AS_FunctionTable_SkippedReasonSummary.csv`
- **AND** instead it is recorded under the new `EntryKind = "CrossModule"` row in `AS_FunctionTable_Entries.csv`

#### Scenario: CustomThunk and Interface continue to bypass cross-module path

- **WHEN** UHT processes a `CustomThunk`-flagged function or a function on an `Interface`/`NativeInterface` class
- **THEN** no cross-module shard entry is emitted for it
- **AND** the existing stub / `CallInterfaceMethod` path remains unchanged

#### Scenario: RPC/Net functions continue to bypass cross-module path

- **WHEN** UHT processes a UFunction with any of `FUNC_Net | FUNC_NetServer | FUNC_NetClient | FUNC_NetMulticast`
- **THEN** no cross-module shard entry is emitted; reflection fallback handles it
- **AND** the entry is recorded in `AS_FunctionTable_SkippedReasonSummary.csv` under reason `rpc-net-function`

### Requirement: Cross-module ABI is protected by three lines of defense, with a single-source LayoutVersion

To prevent silent layout drift between the AngelscriptRuntime-side reader and the generator-side feature struct (which live in different translation units, possibly different DLLs), the cross-module ABI SHALL be guarded by (a) compile-time `static_assert` on `sizeof`, (b) a runtime `LayoutVersion` magic, and (c) runtime null/range validation. Layout drift MUST surface at cold start, never as a silent crash at script call time. The `LayoutVersion` value SHALL live in a single source-of-truth file `Plugins/Angelscript/Source/AngelscriptUHTTool/cross-module-layout-version.txt`; the generator C# and the AngelscriptRuntime public header MUST read from that file (the public header via a `LayoutVersionExpected` constant whose value is sync'd to that file by the build system or a generator emit step). Any add/remove/reorder/widen/narrow change to `FAngelscriptCrossModuleEntry` POD or the POD portion of `FAngelscriptCrossModuleFeature` MUST bump the value in that file.

#### Scenario: Single-source LayoutVersion file exists and is read by both ends

- **WHEN** the repository is inspected
- **THEN** `Plugins/Angelscript/Source/AngelscriptUHTTool/cross-module-layout-version.txt` exists with a single 32-bit hex token (e.g., `0xA5C0DE01`) and a header comment listing the bump triggers
- **AND** the generator C# reads that token at startup and emits it into every cpp's `FAngelscriptCrossModuleFeature(... , 0xA5C0DE01u)` constructor argument
- **AND** the AngelscriptRuntime public header `AngelscriptCrossModuleBindings.h` exposes `LayoutVersionExpected` whose value is identical to the file content (sync'd at build by `factory.AddExternalDependency` so the header is regenerated when the file changes)

#### Scenario: LayoutVersion mismatch is logged and skipped without crashing

- **WHEN** a registered `IModularFeature*` has a `LayoutVersion` field different from the AngelscriptRuntime-side `LayoutVersionExpected`
- **THEN** AngelscriptRuntime emits `UE_LOG(Angelscript, Warning, ...)` identifying the offending `ModuleName` and the observed vs expected magic
- **AND** the entire feature is skipped (no entries from it are written into `ClassFuncMaps`)
- **AND** the engine does NOT crash; subsequent calls to those UFunctions take the reflection fallback path

#### Scenario: Compile-time static_assert prevents silent layout drift

- **WHEN** the layout of `FAngelscriptCrossModuleEntry` or the POD portion of `FAngelscriptCrossModuleFeature` changes such that `sizeof(...)` differs between the AS Runtime header and the generator-emitted cpp
- **THEN** at least one of the two translation units fails to compile due to a `static_assert(sizeof(...) == EXPECTED_BYTES, ...)` violation
- **AND** the breakage is observed at build time, not at runtime

#### Scenario: Runtime null/range validation rejects malformed payloads

- **WHEN** a registered feature has `Count < 0`, `Table == nullptr`, or `ModuleName == nullptr`
- **THEN** AngelscriptRuntime warns and skips the feature without dereferencing the offending field

#### Scenario: Derived feature struct adds no new virtual methods, uses constructor instantiation

- **WHEN** the project's source is inspected (both AS Runtime header and any UHT-emitted cpp)
- **THEN** the `IModularFeature`-derived feature struct used for cross-module bindings declares no new virtual methods beyond `IModularFeature`'s implicit virtual destructor
- **AND** every data member is a fixed-width type (pointer, int32, uint32, int16, uint16); `bool`, `uint8`, or other variable-padding types are NOT used
- **AND** all instances are created via the explicit constructor `FAngelscriptCrossModuleFeature(...)`; brace-aggregate-init forms are NOT used (and would not compile)

### Requirement: Late-loaded modules' cross-module bindings are picked up via OnModularFeatureRegistered, with GameThread marshalling

Engine, plugin, or game modules whose static initializers run after the Late+60 bind stage (e.g., dynamically loaded plugins, deferred-load modules) SHALL still have their cross-module bindings injected. AngelscriptRuntime SHALL subscribe to `IModularFeatures::OnModularFeatureRegistered` and take the same code path as the Late+60 bulk pass. Because UE does NOT guarantee the delegate fires on the GameThread, the callback MUST marshal the actual `ClassFuncMaps` mutation and `BindMethodDirect` call to the GameThread via `AsyncTask(ENamedThreads::GameThread, ...)` (or equivalent) before touching either AS Engine or the bind registry.

#### Scenario: Module loaded after Late+60 still gets its bindings injected

- **WHEN** a module containing a registered `FAngelscriptCrossModuleFeature` is loaded after `EOrder::Late + 60` has already executed
- **THEN** the `OnModularFeatureRegistered` delegate fires with `FeatureName == "AngelscriptCrossModuleBindings"` and the new feature pointer
- **AND** AngelscriptRuntime executes the same reader-cast + LayoutVersion check + range validation + `ClassFuncMaps` injection logic as the Late+60 bulk pass
- **AND** subsequent script calls into that module's UFunctions take the direct-bind path

#### Scenario: Worker-thread feature registration marshals to GameThread

- **WHEN** `IModularFeatures::Get().RegisterModularFeature(FName("AngelscriptCrossModuleBindings"), ...)` is invoked from a non-GameThread (simulating a dynamically loaded plugin's worker-thread initialization)
- **THEN** AngelscriptRuntime's `OnModularFeatureRegistered` callback does NOT directly call `BindMethodDirect` or write to `FAngelscriptBinds::ClassFuncMaps` on the calling thread
- **AND** instead it captures the feature pointer and dispatches a GameThread task (e.g., `AsyncTask(ENamedThreads::GameThread, ...)`) that performs the actual injection
- **AND** no race conditions are observed in `ClassFuncMaps` mutation or AS Engine bind register internals

### Requirement: Stale cross-module shard files are cleaned up across all supported module directories

`AngelscriptFunctionTableCodeGenerator.DeleteStaleOutputs` SHALL enumerate every supported module's OutputDirectory (not just AngelscriptRuntime's) and remove any `AS_FunctionTable_<Module>_CrossModule_*.cpp` file that is not part of the current generated set. Otherwise, when a UFunction is deleted, renamed, marked `NotInAngelscript`, or its module is removed from the supported set, the stale shard remains in the engine module's OutputDirectory, UBT continues to compile it, and at link time produces duplicate-symbol or unresolved-reference errors.

#### Scenario: Deleted UFunction removes its stale shard

- **WHEN** a previously-generating UFunction is removed (or marked `NotInAngelscript`), and UHT runs again
- **THEN** the old `AS_FunctionTable_<Module>_CrossModule_<NNN>.cpp` containing only that entry is deleted from `<Module>`'s OutputDirectory
- **AND** no link-time duplicate-symbol or unresolved-reference error is produced on the next build

#### Scenario: Module removed from supported set has all its shards cleaned

- **WHEN** module `<M>` is removed from the supported module list
- **THEN** every `AS_FunctionTable_<M>_CrossModule_*.cpp` in `<M>`'s OutputDirectory is deleted
- **AND** the same incremental UHT run does not delete any shard belonging to a still-supported module

#### Scenario: Existing AngelscriptRuntime stale-cleanup behavior is preserved

- **WHEN** the existing `AS_FunctionTable_<Module>_<NNN>.cpp` shards in AngelscriptRuntime's OutputDirectory go stale
- **THEN** they continue to be deleted as before (this requirement does not regress that behavior)

### Requirement: Shutdown ordering across DLL unload and Core singleton destruction is safe

The cross-module binding system SHALL not crash when (a) the engine is shutting down and DLLs are being unloaded in arbitrary order, or (b) the `IModularFeatures` Meyers singleton has already been destroyed. Both the emit-cpp side `~FAutoRegister()` destructor and the AngelscriptRuntime side `OnModularFeatureRegistered` subscription MUST guard `IModularFeatures` access with availability checks or be unsubscribed before Core teardown.

#### Scenario: AngelscriptRuntime unsubscribes OnModularFeatureRegistered on engine pre-exit

- **WHEN** `FCoreDelegates::OnPreExit` fires
- **THEN** AngelscriptRuntime's `Bind_CrossModuleDirect` removes its `OnModularFeatureRegistered` subscription
- **AND** any subsequent `IModularFeatures::Get()` call from within AngelscriptRuntime is guarded by an availability check (or wrapped to no-op when the singleton is gone)

#### Scenario: Emit cpp Unregister is guarded against destroyed singleton

- **WHEN** `~FAutoRegister()` runs during DLL unload
- **THEN** it checks `IModularFeatures` availability (using whatever idiom UE 5.7 exposes; selected during Phase 0 probe) before calling `UnregisterModularFeature`
- **AND** if the singleton is already destroyed, the destructor no-ops without dereferencing

#### Scenario: Editor exit and project switch do not produce crashes

- **WHEN** an Editor session is closed cleanly, or the user switches projects causing AngelscriptRuntime DLL to be unloaded
- **THEN** no `IModularFeatures`-related crash occurs in the engine log
- **AND** subsequent fresh Editor starts resume normal binding behavior

### Requirement: Cross-module emission and registration are observable through metrics

UHT SHALL extend its existing summary outputs to surface cross-module direct-bind coverage so regressions can be measured.

#### Scenario: Summary JSON exposes cross-module counts

- **WHEN** UHT finishes a session that emits cross-module shards
- **THEN** `AS_FunctionTable_Summary.json` contains, in addition to existing fields, `crossModuleEntries` (total) and per-module `crossModuleEntries` under each module entry
- **AND** the per-module CSV `AS_FunctionTable_ModuleSummary.csv` contains a `CrossModuleEntries` column populated for every module that emitted a cross-module shard

#### Scenario: Entries CSV distinguishes Direct, CrossModule, and Stub

- **WHEN** an entry is recorded in `AS_FunctionTable_Entries.csv`
- **THEN** its `EntryKind` column takes one of exactly three values: `Direct` (PMF-based same-module shard), `CrossModule` (POD-table-based cross-module shard registered via IModularFeatures), or `Stub` (`ERASE_NO_FUNCTION()`)

#### Scenario: Skipped reasons distinguish RPC and disabled-target-module

- **WHEN** a candidate is skipped due to RPC/Net flags or due to its target module being disabled
- **THEN** `AS_FunctionTable_SkippedReasonSummary.csv` records the reason as `rpc-net-function` or `target-module-disabled` respectively (rather than the legacy `unexported-symbol` bucket)

## Testing Requirements

- **Layer**: Bindings CQTest (Layer "Bindings CQTest" from `Documents/Guides/TestConventions.md`),and a small set of "Runtime CppTests" for the resolver-only and ABI-protection rules.
- **Automation prefix**: `Angelscript.TestModule.Bindings.CrossModuleDirectBind.*` for the bindings layer; `Angelscript.CppTests.UHTToolResolver.*` for the resolver-only and public-header rules.
- **Recommended helpers**: `FAngelscriptTestWorld` for actor/component invocation; `FCoverageModuleScope` to record coverage for the new direct-bind surface; `AngelscriptNativeTestSupport.h` for headless UHT-resolver and public-header unit tests that do not need a UE world.
- **Verification entry points** (from `Documents/Guides/Test.md`):
  - Bindings group: `Tools\RunTests.ps1 -Group Bindings`
  - Native CQTest group: `Tools\RunTests.ps1 -Group Cpp` (resolver-only and ABI tests only need the runtime suite, not a world)
  - Full regression before merge: `Tools\RunTestSuite.ps1 -Suite Default`
  - Run the suite once in **Modular Editor** build and once in **Monolithic Shipping** build to satisfy the dual-config scenarios.
- **New test files** are placed under `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` (functional) and `Plugins/Angelscript/Source/AngelscriptTest/UHTTool/` (resolver / public header / ABI). All file names start with the `Angelscript` prefix per `Documents/Guides/TestConventions.md`.
- **Required scenarios** (each maps to one or more requirement scenarios above):
  - `IsBoundAfterLate60_CrossModuleFunction_PreviouslyUnexportedSymbol`
  - `BehaviorEquivalent_CrossModuleDirectBind_VsReflectionFallback`
  - `LauncherTargetSimulation_NoFeatureRegistered_FallsBackToReflection`(模拟 IModularFeatures 拉空)
  - `SameModuleShardWins_When_BothExist_AtLate50_AndLate60`
  - `MultipleShardsFromSameModule_AllRegisteredAndIterated_NoModuleNameDedup`
  - `Resolver_NoLongerEmitsUnexportedSymbol_ForCrossModuleCandidate`(headless UHT resolver 单测)
  - `CullOutput_DoesNotDelete_AS_FunctionTable_CrossModule_Files`
  - `LayoutVersionMismatch_FeatureSkipped_NoCrash`(ABI 防线 a)
  - `StaticAssert_SizeofConsistency`(ABI 防线 b — 编译期失败即测试失败)
  - `RuntimeNullRangeValidation_RejectsMalformedFeature`(ABI 防线 c)
  - `LayoutVersionFile_SingleSource_GeneratorAndHeaderInSync`(读取 `cross-module-layout-version.txt`)
  - `OnModularFeatureRegistered_LateLoadedModule_BindingsInjected`(后到模块通道)
  - `OnModularFeatureRegistered_WorkerThreadInvocation_MarshalsToGameThread`(线程安全)
  - `BuildCs_DoesNotAddEngineModuleDependency`(`AngelscriptRuntime.Build.cs` 反向依赖回归;静态扫描)
  - `EmittedCpp_DoesNotInclude_AS_SDK_Headers`(emit 内容静态扫描;断言不出现 `angelscript.h` / `AngelscriptCrossModuleBindings.h` 等)
  - `EmittedCpp_UsesConstructorInstantiation_NoBraceAggregate`(emit 内容静态扫描;断言无 `static .* = { GTable,` 形态)
  - `MonolithicShipping_DirectBind_BehaviorParity`(在 Shipping 配置下与 Editor 行为对齐)
  - `RPC_ServerOnly_NotDirectBound_ReflectionRoutesCorrectly`(Server-only RPC 走反射,正确 marshal 到对端)
  - `RPC_NetMulticast_PreservesMulticast`(NetMulticast 仍多播)
  - `RPC_WithValidation_StillValidates`(WithValidation 仍验证)
  - `OutParam_WriteBackThroughArgsSlot`
  - `StaticFunc_NoSelf_BindsAsGlobal`
  - `WorldContextObject_InjectedByHook`
  - `ConstQualifiedMethod_PreservesConst`
  - `NonTrivialValueParam_FString_TArray_MarshalledByValue`
  - `SimdAlignedStruct_FVector_FTransform_HonorAlignment`
  - `UEnumTag_PassedAtUnderlyingWidth`
  - `ObjectPtrWrapper_TSubclassOf_TWeakObjectPtr_OpaqueByValue`
  - `StaleShard_DeletedOnRebuildAfterFunctionRemoval`
  - `StaleShard_DeletedOnRebuildAfterModuleRemoval`
  - `Shutdown_OnPreExit_UnsubscribesOnModularFeatureRegistered_NoCrash`
  - `Shutdown_DllUnload_GuardedUnregister_NoCrash`
- **Performance verification**:在 `Documents/Guides/TestPerformance.md` 增加一节,通过 micro-bench 比较"反射 fallback cached" vs "raw thunk 直绑"的单次调用耗时,产出基线后写入,本 change 验收要求至少减少 30%(具体阈值 Phase 4 出数后写定,先以 30% 为门槛;若实测低于此应回到 Open Question Q3)。
