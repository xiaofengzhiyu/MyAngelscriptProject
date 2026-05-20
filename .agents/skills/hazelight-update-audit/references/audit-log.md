# Hazelight Update Audit Log

## 2026-05-13 - Recent upstream trial audit

- Repo: `Hazelight/UnrealEngine-Angelscript`
- Branch: `angelscript-master`
- Upstream HEAD reviewed: `472bb2fab5a0bc76d0a86a1dd80750a4118b8418`
- Listed window: 20 recent commits, from `472bb2fab5a0` back to `5457adbc6e5a`
- Deep patch review: 8 commits
- Compare window used for file grouping: `5457adbc6e5a...472bb2fab5a0`
- Checkpoint before audit: none (`audit-state.json` has no `lastReviewedHeadSha`)
- State update: not recorded; this was a trial audit only
- UE-following signals in this window: none
- Local Hazelight clone observed: `K:\UnrealEngine\UEAS`, branch `angelscript-master`, HEAD `fb34aaf8bf10`, remote `git@github.com:straywriter/UnrealEngine-Angelscript.git`

### Deep-reviewed commits

- `472bb2fab5a0` - Dean Marsinelli - Update `FCollisionShapeType` binding to never need GC
- `abd98481b085` - Josh Wood - Add default constructors for `FTraceDatum` and `FOverlapDatum`
- `263f762c05d7` - efokschaner - Guard test viewport setup against headless commandlet runs
- `186aa1e8982b` - Anthony Rey - macOS VSCode command-line fix
- `bc0083dbdcb7` - Paul Greveson - Fix const correctness of incoming argument to `IsChildOf`
- `b51da9095493` - Kevin Masson - Add missing include for `TObjectIterator`
- `1adca44c16c0` - Markus Stephanides - Add setting for non-UPROPERTY struct fields as `NotReplicated`
- `eae1ae79bed6` - Lucas - Reduce StaticJIT binary size by refactoring static initialization of JIT references

### Author summary for listed window

- Lucas: 11
- Dean Marsinelli: 2
- Anthony Rey: 1
- efokschaner: 1
- Josh Wood: 1
- Markus Stephanides: 1
- Kevin Masson: 1
- Paul Greveson: 1

### Adopt now

- `472bb2fab5a0`: add `NeverRequiresGC()` to local `FCollisionShapeType`; local `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCollisionShape.cpp` does not have it.
- `bc0083dbdcb7`: change local `UClass.IsChildOf` binding parameter from `UClass Other` to `const UClass Other`; local `Bind_UObject.cpp` still has the non-const signature.
- `b51da9095493`: add `UObject/UObjectIterator.h` include to local `AngelscriptEditorModule.cpp`; local editor module uses `TObjectIterator` and did not show that include.

### Already absorbed

- `abd98481b085`: local `Bind_WorldCollision.cpp` already has default constructors for `FTraceDatum` and `FOverlapDatum`, and local types also implement `NeverRequiresGC()`.
- `263f762c05d7`: local `UnitTest.cpp` already guards viewport setup with `FSlateApplication::IsInitialized()`.

### Needs OpenSpec

- `1adca44c16c0`: `bMarkNonUpropertyPropertiesAsNotReplicated` adds a new settings/replication behavior. Local code only has transient handling and explicit `NotReplicated` preprocessing.
- `eae1ae79bed6`: StaticJIT static initialization refactor changes generated code shape and JIT reference registration. Local StaticJIT still uses constructor-based `AS_FORCE_LINK` references.
- Editor menu extension commits in the listed window should be grouped into an editor UX/OpenSpec change rather than mixed into small runtime fixes.
- Script semantic/binding changes such as mixin constructors, binding flags, and bitfield accessor behavior should be evaluated as separate capabilities.

### Reference only

- `186aa1e8982b`: upstream `FAngelscriptEditorModule::OpenVsCode` macOS path does not map directly because this project has source navigation split into `AngelscriptSourceCodeNavigation.cpp`. Consider a separate editor navigation follow-up if macOS support matters.

### Out of scope

- `082479e2b651`: AngelscriptGAS compiler warning fix. GAS is out of default audit scope unless the project explicitly expands sync scope.

### Next audit notes

- If this audit is accepted as reviewed, record `lastReviewedHeadSha` as `472bb2fab5a0bc76d0a86a1dd80750a4118b8418`.
- Before recording the checkpoint, decide whether the three `Adopt now` items should be implemented first or merely marked as reviewed/deferred.
- For a broader upstream sweep, inspect the earlier UE-following range around `dbd4567bb144` and `ec8d139d111c` separately from plugin feature commits.
