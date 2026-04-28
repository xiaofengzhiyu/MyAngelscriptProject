#include "BlueprintImpact/AngelscriptBlueprintImpactScanner.h"

#include "AngelscriptEngine.h"
#include "AngelscriptInclude.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Preprocessor/AngelscriptPreprocessor.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBlueprintImpactScanMixedCandidatesTest,
	"Angelscript.TestModule.Editor.BlueprintImpact.ScanBlueprintAssets.MixedCandidatesOnlyReturnsImpactedBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptEditor_Private_Tests_AngelscriptBlueprintImpactScanMixedCandidateTests_Private
{
	TUniquePtr<FAngelscriptEngine> CreateBlueprintImpactMixedCandidateTestEngine()
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

	bool CompileBlueprintImpactStructModule(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& RelativeFilename,
		const FString& StructName,
		const FString& ScriptSource,
		FString& OutAbsoluteFilename,
		UScriptStruct*& OutGeneratedStruct)
	{
		OutAbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), RelativeFilename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutAbsoluteFilename), true);
		if (!FFileHelper::SaveStringToFile(ScriptSource, *OutAbsoluteFilename))
		{
			Test.AddError(FString::Printf(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should write script file '%s'"), *OutAbsoluteFilename));
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		FAngelscriptPreprocessor Preprocessor;
		Preprocessor.AddFile(RelativeFilename, OutAbsoluteFilename);
		if (!Preprocessor.Preprocess())
		{
			Test.AddError(FString::Printf(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test failed to preprocess '%s': %s"), *RelativeFilename, *Engine.FormatDiagnostics()));
			return false;
		}

		TArray<TSharedRef<FAngelscriptModuleDesc>> ModulesToCompile = Preprocessor.GetModulesToCompile();
		TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
		TGuardValue<bool> AutomaticImportGuard(Engine.bUseAutomaticImportMethod, false);
		FScopedAutomaticImportsOverride AutomaticImportsOverride(Engine.GetScriptEngine());
		const ECompileResult CompileResult = Engine.CompileModules(ECompileType::Initial, ModulesToCompile, CompiledModules);
		if (CompileResult == ECompileResult::Error || CompileResult == ECompileResult::ErrorNeedFullReload)
		{
			Test.AddError(FString::Printf(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test failed to compile '%s': %s"), *RelativeFilename, *Engine.FormatDiagnostics()));
			return false;
		}

		if (!Test.TestEqual(
				*FString::Printf(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should compile exactly one module from '%s'"), *RelativeFilename),
				CompiledModules.Num(),
				1))
		{
			return false;
		}

		const TSharedPtr<FAngelscriptClassDesc> StructDesc = Engine.GetClass(StructName);
		OutGeneratedStruct = StructDesc.IsValid() ? Cast<UScriptStruct>(StructDesc->Struct) : nullptr;
		return Test.TestNotNull(
			*FString::Printf(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should generate struct '%s'"), *StructName),
			OutGeneratedStruct);
	}

	bool SaveBlueprintToDisk(
		FAutomationTestBase& Test,
		UBlueprint& Blueprint,
		const FString& PackagePath,
		FString& OutPackageFilename)
	{
		UPackage* Package = Blueprint.GetOutermost();
		if (!Test.TestNotNull(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should resolve the blueprint package before save"), Package))
		{
			return false;
		}

		Package->SetDirtyFlag(true);
		OutPackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return Test.TestTrue(
			TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should save the disk-backed blueprint package"),
			UPackage::SavePackage(Package, &Blueprint, *OutPackageFilename, SaveArgs));
	}

	UBlueprint* CreateDiskBackedBlueprintChild(
		FAutomationTestBase& Test,
		FAssetRegistryModule& AssetRegistryModule,
		UClass* ParentClass,
		FStringView Suffix,
		FString& OutPackagePath,
		FString& OutPackageFilename)
	{
		if (!Test.TestNotNull(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should receive a valid parent class"), ParentClass))
		{
			return nullptr;
		}

		const FString AssetName = FString::Printf(
			TEXT("BP_EditorImpactScanMixed_%.*s_%s"),
			Suffix.Len(),
			Suffix.GetData(),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		OutPackagePath = FString::Printf(TEXT("/Game/Automation/%s"), *AssetName);

		UPackage* Package = CreatePackage(*OutPackagePath);
		if (!Test.TestNotNull(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should create a disk-backed package"), Package))
		{
			return nullptr;
		}

		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			FName(*AssetName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			TEXT("AngelscriptBlueprintImpactScanMixedCandidateTests"));
		if (Blueprint == nullptr)
		{
			Package->MarkAsGarbage();
			return nullptr;
		}

		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		AssetRegistryModule.AssetCreated(Blueprint);
		if (!SaveBlueprintToDisk(Test, *Blueprint, OutPackagePath, OutPackageFilename))
		{
			return nullptr;
		}

		Blueprint->AddToRoot();
		return Blueprint;
	}

	void CleanupBlueprintAsset(
		FAssetRegistryModule& AssetRegistryModule,
		UBlueprint*& Blueprint,
		const FString& PackageFilename)
	{
		if (Blueprint == nullptr)
		{
			return;
		}

		AssetRegistryModule.AssetDeleted(Blueprint);
		if (Blueprint->IsRooted())
		{
			Blueprint->RemoveFromRoot();
		}

		if (!PackageFilename.IsEmpty())
		{
			IFileManager::Get().Delete(*PackageFilename, false, true, true);
		}

		if (UClass* BlueprintClass = Blueprint->GeneratedClass)
		{
			BlueprintClass->MarkAsGarbage();
		}

		if (UPackage* BlueprintPackage = Blueprint->GetOutermost())
		{
			BlueprintPackage->MarkAsGarbage();
		}

		Blueprint->MarkAsGarbage();
		Blueprint = nullptr;
	}

	UK2Node_CustomEvent* AddCustomEventUserPin(UBlueprint& Blueprint, const FEdGraphPinType& PinType)
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
		if (EventGraph == nullptr)
		{
			return nullptr;
		}

		UK2Node_CustomEvent* CustomEventNode = NewObject<UK2Node_CustomEvent>(EventGraph);
		EventGraph->AddNode(CustomEventNode, false, false);
		CustomEventNode->CustomFunctionName = TEXT("BlueprintImpactMixedCandidateEvent");
		CustomEventNode->CreateNewGuid();
		CustomEventNode->PostPlacedNewNode();
		CustomEventNode->AllocateDefaultPins();
		CustomEventNode->CreateUserDefinedPin(TEXT("ImpactedValue"), PinType, EGPD_Output);
		return CustomEventNode;
	}

	bool ContainsPackagePath(const TArray<FAssetData>& Assets, const FString& PackagePath)
	{
		return Assets.ContainsByPredicate([&PackagePath](const FAssetData& AssetData)
		{
			return AssetData.PackageName.ToString() == PackagePath;
		});
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

	const AngelscriptEditor::BlueprintImpact::FBlueprintImpactMatch* FindMatchByPackage(
		const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult& Result,
		const FString& PackagePath)
	{
		return Result.Matches.FindByPredicate([&PackagePath](const AngelscriptEditor::BlueprintImpact::FBlueprintImpactMatch& Match)
		{
			return Match.AssetData.PackageName.ToString() == PackagePath;
		});
	}
}

using namespace AngelscriptEditor_Private_Tests_AngelscriptBlueprintImpactScanMixedCandidateTests_Private;

bool FAngelscriptBlueprintImpactScanMixedCandidatesTest::RunTest(const FString& Parameters)
{
	TArray<FAngelscriptEngine*> SavedStack = FAngelscriptEngineContextStack::SnapshotAndClear();
	TUniquePtr<FAngelscriptEngine> Engine = CreateBlueprintImpactMixedCandidateTestEngine();
	TUniquePtr<FAngelscriptEngineScope> EngineScope;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	UBlueprint* ImpactedBlueprint = nullptr;
	UBlueprint* ControlBlueprint = nullptr;
	FString ImpactedBlueprintPackagePath;
	FString ImpactedBlueprintPackageFilename;
	FString ControlBlueprintPackagePath;
	FString ControlBlueprintPackageFilename;
	FString ImpactedStructAbsoluteFilename;
	ON_SCOPE_EXIT
	{
		AngelscriptEditor::BlueprintImpact::ClearBlueprintAssetsOverrideForTesting();
		EngineScope.Reset();
		CleanupBlueprintAsset(AssetRegistryModule, ImpactedBlueprint, ImpactedBlueprintPackageFilename);
		CleanupBlueprintAsset(AssetRegistryModule, ControlBlueprint, ControlBlueprintPackageFilename);
		if (!ImpactedStructAbsoluteFilename.IsEmpty())
		{
			IFileManager::Get().Delete(*ImpactedStructAbsoluteFilename, false, true, true);
		}
		CollectGarbage(RF_NoFlags, true);
		FAngelscriptEngineContextStack::RestoreSnapshot(MoveTemp(SavedStack));
	};

	if (!TestNotNull(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should create a testing engine"), Engine.Get()))
	{
		return false;
	}

	EngineScope = MakeUnique<FAngelscriptEngineScope>(*Engine);

	const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString ImpactedStructRelativeFilename = FString::Printf(TEXT("Scripts/BlueprintImpact/MixedCandidateStruct_%s.as"), *UniqueSuffix);
	const FString ImpactedStructName = FString::Printf(TEXT("FBlueprintImpactMixedStruct_%s"), *UniqueSuffix);
	const FString ImpactedStructSource = FString::Printf(TEXT(R"AS(
USTRUCT()
struct %s
{
	UPROPERTY()
	int Value = 17;
}
)AS"), *ImpactedStructName);

	UScriptStruct* ImpactedStruct = nullptr;
	if (!CompileBlueprintImpactStructModule(
			*this,
			*Engine,
			ImpactedStructRelativeFilename,
			ImpactedStructName,
			ImpactedStructSource,
			ImpactedStructAbsoluteFilename,
			ImpactedStruct))
	{
		return false;
	}

	ImpactedBlueprint = CreateDiskBackedBlueprintChild(
		*this,
		AssetRegistryModule,
		AActor::StaticClass(),
		TEXT("Impacted"),
		ImpactedBlueprintPackagePath,
		ImpactedBlueprintPackageFilename);
	ControlBlueprint = CreateDiskBackedBlueprintChild(
		*this,
		AssetRegistryModule,
		AActor::StaticClass(),
		TEXT("Control"),
		ControlBlueprintPackagePath,
		ControlBlueprintPackageFilename);
	if (!TestNotNull(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should create the impacted blueprint"), ImpactedBlueprint)
		|| !TestNotNull(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should create the control blueprint"), ControlBlueprint))
	{
		return false;
	}

	FEdGraphPinType StructPinType;
	StructPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	StructPinType.PinSubCategoryObject = ImpactedStruct;
	if (!TestNotNull(
			TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate test should add a custom event user pin using the impacted struct"),
			AddCustomEventUserPin(*ImpactedBlueprint, StructPinType)))
	{
		return false;
	}

	if (!SaveBlueprintToDisk(*this, *ImpactedBlueprint, ImpactedBlueprintPackagePath, ImpactedBlueprintPackageFilename))
	{
		return false;
	}

	AssetRegistryModule.Get().ScanModifiedAssetFiles({ ImpactedBlueprintPackageFilename, ControlBlueprintPackageFilename });
	AngelscriptEditor::BlueprintImpact::SetBlueprintAssetsOverrideForTesting({
		FAssetData(ImpactedBlueprint),
		FAssetData(ControlBlueprint)
	});

	AngelscriptEditor::BlueprintImpact::FBlueprintImpactRequest Request;
	Request.ChangedScripts = { ImpactedStructRelativeFilename };
	const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult Result =
		AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(*Engine, AssetRegistryModule.Get(), Request);

	if (!TestEqual(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should keep one normalized changed script"), Result.NormalizedChangedScripts.Num(), 1)
		|| !TestEqual(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should normalize the changed script path"), Result.NormalizedChangedScripts[0], FString(ImpactedStructRelativeFilename).ToLower())
		|| !TestEqual(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should match exactly one active module"), Result.MatchingModules.Num(), 1)
		|| !TestTrue(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should match the impacted struct module"), ContainsModuleWithSection(Result.MatchingModules, ImpactedStructRelativeFilename))
		|| !TestTrue(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should build impact symbols from the impacted struct"), Result.Symbols.Structs.Contains(ImpactedStruct)))
	{
		return false;
	}

	if (!TestEqual(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should expose both blueprints as candidates"), Result.CandidateAssets.Num(), 2)
		|| !TestTrue(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should include the impacted blueprint candidate"), ContainsPackagePath(Result.CandidateAssets, ImpactedBlueprintPackagePath))
		|| !TestTrue(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should include the control blueprint candidate"), ContainsPackagePath(Result.CandidateAssets, ControlBlueprintPackagePath)))
	{
		return false;
	}

	if (!TestEqual(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should only report one impacted blueprint"), Result.Matches.Num(), 1))
	{
		return false;
	}

	const AngelscriptEditor::BlueprintImpact::FBlueprintImpactMatch* ImpactedMatch = FindMatchByPackage(Result, ImpactedBlueprintPackagePath);
	if (!TestNotNull(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should match the impacted blueprint"), ImpactedMatch))
	{
		return false;
	}

	if (!TestFalse(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should not keep a match entry for the unaffected blueprint"), FindMatchByPackage(Result, ControlBlueprintPackagePath) != nullptr)
		|| !TestEqual(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should report exactly one reason for the impacted blueprint"), ImpactedMatch->Reasons.Num(), 1)
		|| !TestEqual(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should classify the impacted blueprint as a pin-type-only match"), ImpactedMatch->Reasons[0], AngelscriptEditor::BlueprintImpact::EBlueprintImpactReason::PinType)
		|| !TestEqual(TEXT("BlueprintImpact.ScanBlueprintAssets mixed-candidate scan should not fail to load any overridden blueprint assets"), Result.FailedAssetLoads, 0))
	{
		return false;
	}

	return true;
}

#endif
