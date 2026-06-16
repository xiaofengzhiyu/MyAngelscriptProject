## 1. Define the provider boundary

- [x] 1.1 Add focused tests that describe provider-backed source discovery, memory-source bypass behavior, and legacy filename compatibility.
- [x] 1.2 Implement a narrow `IAngelscriptSourceProvider` contract that returns `FAngelscriptSource` descriptors and source-state data.
- [x] 1.3 Add a disk-backed default provider that wraps the existing UE file APIs and current engine dependency points.

## 2. Route preprocessing through the provider

- [x] 2.1 Update preprocessing so disk-backed sources load text through the provider instead of reading files directly.
- [x] 2.2 Keep `FAngelscriptSource::FromMemorySource()` and existing `AddSource()` behavior as the bypass path for inline source text.
- [x] 2.3 Add tests that prove memory-backed sources do not touch disk and that disk-backed sources still preprocess with the same metadata.

## 3. Route engine discovery through the provider

- [x] 3.1 Replace direct recursive file discovery with provider-backed source discovery in the engine.
- [x] 3.2 Keep `FindAllScriptFilenames()` and other filename-based entry points as compatibility adapters while the provider becomes the preferred path.
- [x] 3.3 Add tests that cover project roots, plugin roots, and legacy `AllRootPaths` compatibility.

## 4. Rework hot reload state

- [x] 4.1 Move hot-reload bookkeeping off legacy relative-filename keys and onto canonical source identity.
- [x] 4.2 Add content-state comparison so timestamp-only churn does not queue unnecessary reloads.
- [x] 4.3 Add regression tests for same-content reload suppression and distinct-source-key separation.

## 5. Validate the change

- [x] 5.1 Run the focused FileSystem, Preprocessor, and hot-reload test groups that cover the new provider path.
- [x] 5.2 Run `Tools\\RunBuild.ps1` to confirm the refactor compiles cleanly.
- [x] 5.3 Run `openspec validate "refactor-as-script-source-provider" --strict --json`.
