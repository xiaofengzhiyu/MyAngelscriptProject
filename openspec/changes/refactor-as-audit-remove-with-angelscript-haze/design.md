# Design: refactor-as-audit-remove-with-angelscript-haze

## Context

`WITH_ANGELSCRIPT_HAZE` is a long-standing compile-time switch (`AngelscriptRuntime/Core/AngelscriptEngine.h:30-32`, defaults to `0`) that originally guarded the divergence between Hazelight's internal AngelScript fork and a generic Unreal-Engine path. The fork shipped from this repository has been built with the macro at `0` for the entire history available in the codebase. The macro is therefore preserving an inactive Hazelight code path at zero benefit, and adding 21 conditional-compilation sites of cognitive overhead.

A full enumeration of those sites with classification has already been performed during planning. The five categories below map cleanly onto distinct handling rules:

- **A — Renaming dodge.** A binding in the active path uses a non-UE-standard name only because the original UE name would clash with the auto-property-accessor system on a same-named UPROPERTY field. There is exactly one such site in `Bind_AActor.cpp:155-175`. This is the reason the prerequisite — `refactor-as-remove-autoaccessor` — must merge first; once that change drops the auto-accessor synthesis, restoring the UE-original name is collision-free.
- **B — Haze-only RPC machinery.** Code wrapped in `#if WITH_ANGELSCRIPT_HAZE` that registers Hazelight-specific specifiers (`NetFunction`, `CrumbFunction`, `DevFunction`) and flags (`HAZEFUNC_CrumbFunction`, `HazeFunctionFlags`). The fork no longer ships those specifiers, so the wrapped code is unreachable and is deleted.
- **C — Naming-only fork.** A single site (`Bind_WorldCollision.cpp:358`) chooses between two namespace names (`System::` vs. `AsyncTrace::`) for the same global functions. Since neither is actually a UE upstream convention, the choice is preserving "fork-internal consistency". The non-Haze namespace `System::` matches every other global utility binding in the fork, so it is kept and the alternative is dropped.
- **D — Behavioural fork.** Three sites encode different runtime behaviour between the two paths: `AS_ENSURE` macro choice (`ensureMsgf` vs. `devEnsure`), cooked-build exception dialog handling, and a `bUseAngelscriptHaze` flag that the debug server reports to IDE clients. The non-Haze defaults are kept; the Haze-side branches are deleted.
- **E — Decoration only.** Twelve sites consist of `#if !WITH_ANGELSCRIPT_HAZE { generic UE binding } #endif` and nothing else. These wrap perfectly ordinary UE bindings that have no Haze-side equivalent. The wrappers are stripped, the bindings stay.

The audit work is largely mechanical, but it must be staged so that the riskiest change (Category A renames affecting both `.as` callers and inline test scripts) goes last, after the easier wrappers are gone and the build is already green at every intermediate step.

## Goals / Non-Goals

**Goals:**

- Eliminate the `WITH_ANGELSCRIPT_HAZE` macro and every `#if WITH_ANGELSCRIPT_HAZE` / `#if !WITH_ANGELSCRIPT_HAZE` site from the repository.
- Restore UE-original method names on Category-A bindings so the fork stops carrying gratuitous deviations from upstream UE method nomenclature.
- Keep every behaviour that exists in the active (non-Haze) path intact and verifiable through the existing automation suite.
- Maintain debugger wire-format compatibility for older clients (the removed `bUseAngelscriptHaze` field defaults to `false` in absent-field cases, matching the runtime behaviour with the macro at `0`).

**Non-Goals:**

- Do not extend RPC support, add new replication specifiers, or otherwise change script-visible RPC semantics. Removing the Haze RPC path is a deletion, not a redesign.
- Do not modify `Documents/Plans/Plan_HazelightScriptFeatureParity.md` content beyond a single header annotation; the Plans tree is deprecated.
- Do not refactor `Bind_AActor.cpp` beyond the Category-A renames and the `#if !` wrapper removal. Other improvements (e.g. coverage of additional `AActor` UFunctions) belong to separate changes.
- Do not delete any test except the `bUseAngelscriptHaze` mirror assertion. Other Haze-related test scaffolding, if any, is repurposed or kept as-is.

## Decisions

- **D1 — Sequential dependency on `refactor-as-remove-autoaccessor`.** Category A renames `GetActorInstigator` to `GetInstigator`. With the auto-property-accessor system still active, `Instigator` is a UPROPERTY auto-binding generates a synthetic `GetInstigator()` accessor; introducing a hand-bound `GetInstigator()` would either collide at registration time or shadow the auto-accessor unpredictably. Removing the auto-accessor system first guarantees the rename is a clean addition. The user has confirmed strong-dependency / serial execution as the chosen order.

- **D2 — Land Category E first, A last.** Category E touches 12 files with mechanical wrapper deletion and zero behaviour change. Doing it first reduces the macro's footprint to single-digit sites and gives the build pipeline early confirmation that the macro is no longer load-bearing. Category B follows (Haze-only RPC machinery deletion, isolated to four files, cleanly verified by RPC test coverage). Category C and D are settled mid-sequence. Category A goes last because it is the only one that reaches into `.as` callers and inline AS literals.

- **D3 — `System::` over `AsyncTrace::`.** UE itself does not put `AsyncLineTraceByChannel` etc. in any namespace; the fork's choice is purely a fork-internal organizing decision. `System::` is already where every other global utility (loading, FName, math entry points) lives in the fork, so the choice is "match the rest of the file" rather than "match upstream". One site only, low risk.

- **D4 — `ensureMsgf` over `devEnsure` for `AS_ENSURE`.** `devEnsure` is a Hazelight-internal stricter variant that is harder to disable in opt builds; `ensureMsgf` is the standard UE primitive and pairs better with the fork's "ship-friendly" stance. The current non-Haze default is already `ensureMsgf`; this decision just makes that permanent.

- **D5 — Remove `bUseAngelscriptHaze` from the debug protocol struct, not just its assignment.** Leaving the field with a permanent `false` value would advertise a contract the fork no longer honours. Removing the field signals to IDE-extension authors that the concept is gone. Older clients receive missing-field defaults of `false` in JSON deserialization, which matches every runtime observation prior to this change.

- **D6 — Keep the parser/preprocessor handling for `Replicated` UPROPERTY specifier (Category E site `AngelscriptPreprocessor.cpp:2627`).** That site is currently `#if !WITH_ANGELSCRIPT_HAZE` because Hazelight historically handled replication on the C++ side; the fork's active path needs the AS-side processing. Removing the wrapper preserves the working processing logic. No behaviour change.

## Risks / Trade-offs

- **Risk: Category A rename misses inline AS callers in `.cpp` test files** → Mitigation: the rename task does an `rg` sweep for `GetActorInstigator` and `GetActorInstigatorController` over the entire repository (including `Plugins/Angelscript/Source/AngelscriptTest`, `Script/`, and `Documents/`). The post-task verification asserts both names produce zero hits.

- **Risk: Removing `FAngelscriptDebugDatabaseSettings::bUseAngelscriptHaze` breaks IDE clients pinned to an older protocol shape** → Mitigation: confirmed forward-compatible because the protocol JSON deserializes missing fields as `false`. Documented in proposal Impact. If a client is found to fail, the fix is on the client side (delete the field consumer) — but no such consumer is known in the repository.

- **Risk: Category B deletion breaks compilation if a Haze-only specifier survives in some `.as` script** → Mitigation: the removal task includes an `rg` sweep for `NetFunction`, `CrumbFunction`, and `DevFunction` over `Script/` and `Plugins/Angelscript/Source/AngelscriptTest`. Any hit is flagged and resolved before the macro definition is removed.

- **Trade-off: Category C consolidation (`System::` only) makes async-trace global functions accessible only in `System` namespace.** Existing `.as` scripts that imported them via `AsyncTrace::` (extremely rare; Haze-only path) lose that name. The replacement script update is part of the task.

- **Trade-off: removing the macro at all means future re-introduction requires reverting this commit rather than flipping a flag.** Acceptable: the fork has no reason to revisit the Hazelight internal path; if it ever does, that requires a new design.

## Migration Plan

1. **Phase 1 — Category E wrapper stripping (12 sites).** Lowest risk. After this phase the macro is referenced in 9 places.
2. **Phase 2 — Category B Haze-only RPC machinery deletion (4 sites).** Verify with the existing `Networking` test prefix in the automation suite.
3. **Phase 3 — Category C namespace consolidation (1 site) and Category D behavioural decisions (3 sites).** Update the `AngelscriptDebuggerDatabaseTests.cpp:115` assertion in this phase.
4. **Phase 4 — Category A rename (1 site, multiple call-sites).** Sweep `GetActorInstigator` and `GetActorInstigatorController` across the repo and rewrite. Run Actor and Networking automation suites.
5. **Phase 5 — Macro definition removal and final cleanup.** Delete `AngelscriptEngine.h:30-32`. Verify `rg "WITH_ANGELSCRIPT_HAZE"` returns zero hits. Add the milestone entry to `AGENTS.md` / `AGENTS_ZH.md`. Annotate `Documents/Plans/Plan_HazelightScriptFeatureParity.md` header.

## Open Questions

- None — all category assignments and handling rules are settled. The single dependency (`refactor-as-remove-autoaccessor`) is locked in by D1.
