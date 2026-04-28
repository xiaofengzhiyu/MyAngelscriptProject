#include "BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.h"
#include "BlueprintImpact/AngelscriptBlueprintImpactScanner.h"

#include "AngelscriptEngine.h"
#include "AngelscriptInclude.h"

#include "Core/AngelscriptRuntimeModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Preprocessor/AngelscriptPreprocessor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBlueprintImpactCommandletMergeChangedScriptsTest,
	"Angelscript.Editor.BlueprintImpact.CommandletMergesInlineAndFileChangedScripts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestModuleBlueprintImpactCommandletMergeChangedScriptsTest,
	"Angelscript.TestModule.Editor.BlueprintImpact.CommandletMergesInlineAndFileChangedScripts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBlueprintImpactCommandletFailedAssetLoadsReturnExitCode3Test,
	"Angelscript.Editor.BlueprintImpact.CommandletFailedAssetLoadsReturnExitCode3",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptEditor_Private_Tests_AngelscriptBlueprintImpactCommandletTests_Private
{
	TUniquePtr<FAngelscriptEngine> CreateBlueprintImpactCommandletTestEngine()
	{
		const FAngelscriptEngineConfig Config;
		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		return FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
	}

	struct FScopedAutomaticImportsOverride
	{
		explicit FScopedAutomaticImportsOverride(asIScriptEngine* InScriptEngine)
			: ScriptEngine(InScriptEngine)
			, PreviousValue(InScriptEngine != nullptr ? InScriptEngine->GetEngineProperty(asEP_AUTOMATIC_IMPORTS) : 0)
		{
			if (ScriptEngine != nullptr)
			{
				ScriptEngine->SetEngineProperty(asEP_AUTOMATIC_IMPORTS, 0);
			}
		}

		~FScopedAutomaticImportsOverride()
		{
			if (ScriptEngine != nullptr)
			{
				ScriptEngine->SetEngineProperty(asEP_AUTOMATIC_IMPORTS, PreviousValue);
			}
		}

		asIScriptEngine* ScriptEngine = nullptr;
		asPWORD PreviousValue = 0;
	};

	bool CompileBlueprintImpactCommandletModule(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& RelativeFilename,
		const FString& ScriptSource,
		FString& OutAbsoluteFilename)
	{
		OutAbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), RelativeFilename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutAbsoluteFilename), true);
		if (!FFileHelper::SaveStringToFile(ScriptSource, *OutAbsoluteFilename))
		{
			Test.AddError(FString::Printf(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should write script file '%s'"), *OutAbsoluteFilename));
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		FAngelscriptPreprocessor Preprocessor;
		Preprocessor.AddFile(RelativeFilename, OutAbsoluteFilename);
		if (!Preprocessor.Preprocess())
		{
			Test.AddError(FString::Printf(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts failed to preprocess '%s': %s"), *RelativeFilename, *Engine.FormatDiagnostics()));
			return false;
		}

		TArray<TSharedRef<FAngelscriptModuleDesc>> ModulesToCompile = Preprocessor.GetModulesToCompile();
		TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
		TGuardValue<bool> AutomaticImportGuard(Engine.bUseAutomaticImportMethod, false);
		FScopedAutomaticImportsOverride AutomaticImportsOverride(Engine.GetScriptEngine());
		const ECompileResult CompileResult = Engine.CompileModules(ECompileType::Initial, ModulesToCompile, CompiledModules);
		if (CompileResult == ECompileResult::Error || CompileResult == ECompileResult::ErrorNeedFullReload)
		{
			Test.AddError(FString::Printf(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts failed to compile '%s': %s"), *RelativeFilename, *Engine.FormatDiagnostics()));
			return false;
		}

		return Test.TestEqual(
			*FString::Printf(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should compile exactly one module from '%s'"), *RelativeFilename),
			CompiledModules.Num(),
			1);
	}

	bool ContainsModuleWithSection(
		const TArray<TSharedRef<FAngelscriptModuleDesc>>& Modules,
		const FString& RelativeFilename)
	{
		return Modules.ContainsByPredicate([&RelativeFilename](const TSharedRef<FAngelscriptModuleDesc>& Module)
		{
			return Module->Code.ContainsByPredicate([&RelativeFilename](const FAngelscriptModuleDesc::FCodeSection& Section)
			{
				return Section.RelativeFilename == RelativeFilename;
			});
		});
	}

	FAssetData MakeUnloadableBlueprintAssetData()
	{
		const FString AssetName = FString::Printf(
			TEXT("BP_EditorImpactMissing_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		const FString PackagePath = FString::Printf(TEXT("/Game/Automation/%s"), *AssetName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
		return FAssetData(PackagePath, ObjectPath, FTopLevelAssetPath(UBlueprint::StaticClass()));
	}

	bool RunMergeChangedScriptsScenario(FAutomationTestBase& Test)
	{
		TArray<FAngelscriptEngine*> SavedStack = FAngelscriptEngineContextStack::SnapshotAndClear();
		TUniquePtr<FAngelscriptEngine> Engine = CreateBlueprintImpactCommandletTestEngine();
		TUniquePtr<FAngelscriptEngineScope> EngineScope;

		const FString TempDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintImpact"));
		IFileManager::Get().MakeDirectory(*TempDirectory, true);

		const FString TempFilename = FPaths::Combine(
			TempDirectory,
			FString::Printf(TEXT("ChangedScripts_%s.txt"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		FString ScriptAAbsoluteFilename;
		FString ScriptBAbsoluteFilename;
		FString ScriptCAbsoluteFilename;
		ON_SCOPE_EXIT
		{
			AngelscriptEditor::BlueprintImpact::ClearBlueprintAssetsOverrideForTesting();
			EngineScope.Reset();
			IFileManager::Get().Delete(*TempFilename, false, true, true);
			if (!ScriptAAbsoluteFilename.IsEmpty())
			{
				IFileManager::Get().Delete(*ScriptAAbsoluteFilename, false, true, true);
			}
			if (!ScriptBAbsoluteFilename.IsEmpty())
			{
				IFileManager::Get().Delete(*ScriptBAbsoluteFilename, false, true, true);
			}
			if (!ScriptCAbsoluteFilename.IsEmpty())
			{
				IFileManager::Get().Delete(*ScriptCAbsoluteFilename, false, true, true);
			}
			FAngelscriptEngineContextStack::RestoreSnapshot(MoveTemp(SavedStack));
		};

		if (!Test.TestNotNull(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should create a testing Angelscript engine"), Engine.Get()))
		{
			return false;
		}

		EngineScope = MakeUnique<FAngelscriptEngineScope>(*Engine);

		if (!Test.TestTrue(
			TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should write the temporary ChangedScriptFile"),
			FFileHelper::SaveStringToFile(TEXT("\n  ./Scripts/C.as  \n\n"), *TempFilename)))
		{
			return false;
		}

		if (!CompileBlueprintImpactCommandletModule(
				Test,
				*Engine,
				TEXT("Scripts/A.as"),
				TEXT("int CommandletMergeScriptA() { return 1; }"),
				ScriptAAbsoluteFilename)
			|| !CompileBlueprintImpactCommandletModule(
				Test,
				*Engine,
				TEXT("Scripts/B.as"),
				TEXT("int CommandletMergeScriptB() { return 2; }"),
				ScriptBAbsoluteFilename)
			|| !CompileBlueprintImpactCommandletModule(
				Test,
				*Engine,
				TEXT("Scripts/C.as"),
				TEXT("int CommandletMergeScriptC() { return 3; }"),
				ScriptCAbsoluteFilename))
		{
			return false;
		}

		const TArray<TSharedRef<FAngelscriptModuleDesc>> ActiveModules = Engine->GetActiveModules();
		if (!Test.TestEqual(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should only register the three synthetic active modules"),
				ActiveModules.Num(),
				3))
		{
			return false;
		}

		const FString ParamsString = FString::Printf(
			TEXT("ChangedScript=Scripts/A.as; Scripts/B.as ChangedScriptFile=%s"),
			*TempFilename);

		AngelscriptEditor::BlueprintImpact::FBlueprintImpactRequest Request;
		FString ErrorMessage;
		if (!Test.TestTrue(
			TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should parse inline and file changed scripts"),
			UAngelscriptBlueprintImpactScanCommandlet::BuildRequestForTesting(ParamsString, Request, ErrorMessage)))
		{
			return false;
		}

		if (!Test.TestTrue(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should not produce a parse error"), ErrorMessage.IsEmpty()))
		{
			return false;
		}

		if (!Test.TestEqual(
			TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should merge three trimmed changed scripts before normalization"),
			Request.ChangedScripts.Num(),
			3))
		{
			return false;
		}

		if (!Test.TestEqual(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should keep the first inline script"), Request.ChangedScripts[0], FString(TEXT("Scripts/A.as"))))
		{
			return false;
		}

		if (!Test.TestEqual(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should trim the second inline script"), Request.ChangedScripts[1], FString(TEXT("Scripts/B.as"))))
		{
			return false;
		}

		if (!Test.TestEqual(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should trim the file-provided script and preserve its relative marker"), Request.ChangedScripts[2], FString(TEXT("./Scripts/C.as"))))
		{
			return false;
		}

		const TArray<TSharedRef<FAngelscriptModuleDesc>> MatchingModules =
			AngelscriptEditor::BlueprintImpact::FindModulesForChangedScripts(ActiveModules, Request.ChangedScripts);
		if (!Test.TestEqual(
			TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should match every module referenced by inline or file inputs"),
			MatchingModules.Num(),
			3))
		{
			return false;
		}

		if (!Test.TestTrue(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should include the inline A module"), ContainsModuleWithSection(MatchingModules, TEXT("Scripts/A.as")))
			|| !Test.TestTrue(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should include the inline B module"), ContainsModuleWithSection(MatchingModules, TEXT("Scripts/B.as")))
			|| !Test.TestTrue(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should include the file-backed C module"), ContainsModuleWithSection(MatchingModules, TEXT("Scripts/C.as"))))
		{
			return false;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AngelscriptEditor::BlueprintImpact::SetBlueprintAssetsOverrideForTesting({});
		const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult ScanResult =
			AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(*Engine, AssetRegistryModule.Get(), Request);
		if (!Test.TestEqual(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should normalize three changed scripts for the commandlet scan"),
				ScanResult.NormalizedChangedScripts.Num(),
				3)
			|| !Test.TestEqual(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should report three matching active modules for the commandlet scan"),
				ScanResult.MatchingModules.Num(),
				3)
			|| !Test.TestTrue(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts commandlet scan should include the inline A module"),
				ContainsModuleWithSection(ScanResult.MatchingModules, TEXT("Scripts/A.as")))
			|| !Test.TestTrue(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts commandlet scan should include the inline B module"),
				ContainsModuleWithSection(ScanResult.MatchingModules, TEXT("Scripts/B.as")))
			|| !Test.TestTrue(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts commandlet scan should include the file-backed C module"),
				ContainsModuleWithSection(ScanResult.MatchingModules, TEXT("Scripts/C.as")))
			|| !Test.TestEqual(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should keep the candidate asset list empty when the test injects no assets"),
				ScanResult.CandidateAssets.Num(),
				0)
			|| !Test.TestEqual(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should keep the impacted asset list empty when the test injects no assets"),
				ScanResult.Matches.Num(),
				0)
			|| !Test.TestEqual(
				TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should not report failed asset loads when the test injects no assets"),
				ScanResult.FailedAssetLoads,
				0))
		{
			return false;
		}

		Engine->bDidInitialCompileSucceed = true;
		UAngelscriptBlueprintImpactScanCommandlet* Commandlet = NewObject<UAngelscriptBlueprintImpactScanCommandlet>();
		if (!Test.TestNotNull(TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should create the commandlet object"), Commandlet))
		{
			return false;
		}

		return Test.TestEqual(
			TEXT("BlueprintImpact.CommandletMergesInlineAndFileChangedScripts should return the success exit code for valid merged inputs"),
			Commandlet->Main(ParamsString),
			0);
	}
}

using namespace AngelscriptEditor_Private_Tests_AngelscriptBlueprintImpactCommandletTests_Private;

bool FAngelscriptBlueprintImpactCommandletMergeChangedScriptsTest::RunTest(const FString& Parameters)
{
	return RunMergeChangedScriptsScenario(*this);
}

bool FAngelscriptTestModuleBlueprintImpactCommandletMergeChangedScriptsTest::RunTest(const FString& Parameters)
{
	return RunMergeChangedScriptsScenario(*this);
}

bool FAngelscriptBlueprintImpactCommandletFailedAssetLoadsReturnExitCode3Test::RunTest(const FString& Parameters)
{
	UAngelscriptBlueprintImpactScanCommandlet* Commandlet = NewObject<UAngelscriptBlueprintImpactScanCommandlet>();
	if (!TestNotNull(TEXT("BlueprintImpact.CommandletFailedAssetLoadsReturnExitCode3 should create the commandlet object"), Commandlet))
	{
		return false;
	}

	FAngelscriptRuntimeModule::InitializeAngelscript();
	FAngelscriptEngine* EnginePtr = FAngelscriptEngine::TryGetCurrentEngine();
	if (!TestNotNull(TEXT("BlueprintImpact.CommandletFailedAssetLoadsReturnExitCode3 requires a live Angelscript engine on the context stack"), EnginePtr))
	{
		return false;
	}
	FAngelscriptEngine& Engine = *EnginePtr;
	const bool bOriginalDidInitialCompileSucceed = Engine.bDidInitialCompileSucceed;
	ON_SCOPE_EXIT
	{
		Engine.bDidInitialCompileSucceed = bOriginalDidInitialCompileSucceed;
		AngelscriptEditor::BlueprintImpact::ClearBlueprintAssetsOverrideForTesting();
	};
	Engine.bDidInitialCompileSucceed = true;

	TArray<FAssetData> CandidateAssets;
	CandidateAssets.Add(MakeUnloadableBlueprintAssetData());
	AngelscriptEditor::BlueprintImpact::SetBlueprintAssetsOverrideForTesting(CandidateAssets);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AngelscriptEditor::BlueprintImpact::FBlueprintImpactRequest Request;
	const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult ScanResult =
		AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(Engine, AssetRegistryModule.Get(), Request);

	if (!TestEqual(
			TEXT("BlueprintImpact.CommandletFailedAssetLoadsReturnExitCode3 should scan exactly one injected candidate asset"),
			ScanResult.CandidateAssets.Num(),
			1))
	{
		return false;
	}

	if (!TestEqual(
			TEXT("BlueprintImpact.CommandletFailedAssetLoadsReturnExitCode3 should count the unloadable candidate as a failed asset load"),
			ScanResult.FailedAssetLoads,
			1))
	{
		return false;
	}

	if (!TestEqual(
			TEXT("BlueprintImpact.CommandletFailedAssetLoadsReturnExitCode3 should not report a match when the only candidate blueprint cannot load"),
			ScanResult.Matches.Num(),
			0))
	{
		return false;
	}

	return TestEqual(
		TEXT("BlueprintImpact.CommandletFailedAssetLoadsReturnExitCode3 should return the asset-scan-failure exit code when FailedAssetLoads is non-zero"),
		Commandlet->Main(TEXT("")),
		3);
}

#endif
