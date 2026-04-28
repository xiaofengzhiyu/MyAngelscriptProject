#include "AngelscriptEngine.h"
#include "ClassGenerator/ASClass.h"
#include "ClassGenerator/AngelscriptClassGenerator.h"
#include "EditorMenuExtensions/ScriptEditorMenuExtension.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Preprocessor/AngelscriptPreprocessor.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptEditorMenuExtensionsInitializeLifecycleTest,
	"Angelscript.Editor.MenuExtensions.InitializeExtensionsReRegistersOnlyOnFullReloadAndUnregistersOnPreExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptEditor_Private_Tests_AngelscriptEditorMenuExtensionLifecycleTests_Private
{
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

	struct FScopedMenuExtensionDelegateIsolation
	{
		FScopedMenuExtensionDelegateIsolation()
			: SavedPostReload(FAngelscriptClassGenerator::OnPostReload)
			, SavedPreExit(FCoreDelegates::OnEnginePreExit)
		{
			FAngelscriptClassGenerator::OnPostReload.Clear();
			FCoreDelegates::OnEnginePreExit.Clear();
		}

		~FScopedMenuExtensionDelegateIsolation()
		{
			FAngelscriptClassGenerator::OnPostReload = SavedPostReload;
			FCoreDelegates::OnEnginePreExit = SavedPreExit;
		}

		FOnAngelscriptPostReload SavedPostReload;
		FSimpleMulticastDelegate SavedPreExit;
	};

	TUniquePtr<FAngelscriptEngine> MakeLifecycleTestEngine()
	{
		FAngelscriptEngineConfig Config;
		Config.bIsEditor = true;
		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		return FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
	}

	bool CompileLifecycleScript(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& ModuleName,
		const FString& RelativeFilename,
		const FString& ScriptSource)
	{
		const FString AbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), RelativeFilename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteFilename), true);
		if (!FFileHelper::SaveStringToFile(ScriptSource, *AbsoluteFilename))
		{
			Test.AddError(FString::Printf(TEXT("MenuExtensions lifecycle test should write script file '%s'"), *AbsoluteFilename));
			return false;
		}

		FAngelscriptEngineScope CompileScope(Engine);
		FAngelscriptPreprocessor Preprocessor;
		Preprocessor.AddFile(RelativeFilename, AbsoluteFilename);
		if (!Preprocessor.Preprocess())
		{
			Test.AddError(FString::Printf(TEXT("MenuExtensions lifecycle test failed to preprocess the script: %s"), *Engine.FormatDiagnostics()));
			return false;
		}

		TArray<TSharedRef<FAngelscriptModuleDesc>> ModulesToCompile = Preprocessor.GetModulesToCompile();
		TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
		TGuardValue<bool> AutomaticImportGuard(Engine.bUseAutomaticImportMethod, false);
		FScopedAutomaticImportsOverride AutomaticImportsOverride(Engine.GetScriptEngine());
		const ECompileResult CompileResult = Engine.CompileModules(ECompileType::Initial, ModulesToCompile, CompiledModules);
		if (CompileResult == ECompileResult::Error || CompileResult == ECompileResult::ErrorNeedFullReload)
		{
			Test.AddError(FString::Printf(TEXT("MenuExtensions lifecycle test failed to compile module '%s': %s"), *ModuleName, *Engine.FormatDiagnostics()));
			return false;
		}

		return Test.TestTrue(TEXT("MenuExtensions lifecycle test should compile at least one module"), CompiledModules.Num() > 0);
	}

	UASClass* FindGeneratedMenuExtensionClass(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const TCHAR* ClassName)
	{
		const TSharedPtr<FAngelscriptClassDesc> ClassDesc = Engine.GetClass(ClassName);
		UASClass* ScriptClass = ClassDesc.IsValid() ? Cast<UASClass>(ClassDesc->Class) : nullptr;
		Test.TestNotNull(*FString::Printf(TEXT("MenuExtensions lifecycle test should generate script class '%s'"), ClassName), ScriptClass);
		return ScriptClass;
	}

	bool ContainsSnapshot(
		const TArray<UScriptEditorMenuExtension::FRegisteredExtensionSnapshot>& Snapshots,
		const EScriptEditorMenuExtensionLocation Location,
		const FName ExtensionPoint,
		const FName SectionName)
	{
		return Snapshots.ContainsByPredicate([Location, ExtensionPoint, SectionName](const UScriptEditorMenuExtension::FRegisteredExtensionSnapshot& Snapshot)
		{
			return Snapshot.Location == Location
				&& Snapshot.ExtensionPoint == ExtensionPoint
				&& Snapshot.SectionName == SectionName;
		});
	}

	int32 CountSnapshotMatches(
		const TArray<UScriptEditorMenuExtension::FRegisteredExtensionSnapshot>& Snapshots,
		const EScriptEditorMenuExtensionLocation Location,
		const FName ExtensionPoint,
		const FName SectionName)
	{
		int32 MatchCount = 0;
		for (const UScriptEditorMenuExtension::FRegisteredExtensionSnapshot& Snapshot : Snapshots)
		{
			if (Snapshot.Location == Location
				&& Snapshot.ExtensionPoint == ExtensionPoint
				&& Snapshot.SectionName == SectionName)
			{
				++MatchCount;
			}
		}

		return MatchCount;
	}

	TArray<FString> BuildSnapshotSignatures(const TArray<UScriptEditorMenuExtension::FRegisteredExtensionSnapshot>& Snapshots)
	{
		TArray<FString> Signatures;
		Signatures.Reserve(Snapshots.Num());
		for (const UScriptEditorMenuExtension::FRegisteredExtensionSnapshot& Snapshot : Snapshots)
		{
			Signatures.Add(FString::Printf(
				TEXT("%d|%s|%s"),
				static_cast<int32>(Snapshot.Location),
				*Snapshot.ExtensionPoint.ToString(),
				*Snapshot.SectionName.ToString()));
		}

		Signatures.Sort();
		return Signatures;
	}
}

using namespace AngelscriptEditor_Private_Tests_AngelscriptEditorMenuExtensionLifecycleTests_Private;

bool FAngelscriptEditorMenuExtensionsInitializeLifecycleTest::RunTest(const FString& Parameters)
{
	const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString ModuleName = FString::Printf(TEXT("ASEditorMenuExtensionLifecycle_%s"), *UniqueSuffix);
	const FString RelativeFilename = FString::Printf(TEXT("Editor/MenuExtensions/%s.as"), *ModuleName);
	const FString ClassName = FString::Printf(TEXT("UMenuExtensionLifecycle_%s"), *UniqueSuffix);
	const FName UniqueExtensionPoint(*FString::Printf(TEXT("Automation.MenuExtensions.Lifecycle.%s"), *UniqueSuffix));
	const FString ScriptSource = FString::Printf(
		TEXT(R"AS(
UCLASS()
class %s : UScriptEditorMenuExtension
{
	default ExtensionMenu = EScriptEditorMenuExtensionLocation::LevelViewport_ContextMenu;
	default ExtensionPoint = n"%s";

	UFUNCTION(CallInEditor)
	void LifecycleCommand()
	{
	}
}
)AS"),
		*ClassName,
		*UniqueExtensionPoint.ToString());

	TArray<FAngelscriptEngine*> SavedStack = FAngelscriptEngineContextStack::SnapshotAndClear();
	TUniquePtr<FAngelscriptEngine> Engine = MakeLifecycleTestEngine();
	TUniquePtr<FAngelscriptEngineScope> EngineScope;
	FScopedMenuExtensionDelegateIsolation DelegateIsolation;

	ON_SCOPE_EXIT
	{
		FAngelscriptEditorMenuExtensionTestAccess::UnregisterExtensions();
		if (Engine != nullptr)
		{
			Engine->DiscardModule(*ModuleName);
		}
		EngineScope.Reset();
		Engine.Reset();
		CollectGarbage(RF_NoFlags, true);
		FAngelscriptEditorMenuExtensionTestAccess::RegisterExtensions();
		FAngelscriptEngineContextStack::RestoreSnapshot(MoveTemp(SavedStack));
	};

	if (!TestNotNull(TEXT("MenuExtensions lifecycle test should create a testing engine"), Engine.Get())
		|| !TestNotNull(TEXT("MenuExtensions lifecycle test should expose GEditor"), GEditor))
	{
		return false;
	}

	EngineScope = MakeUnique<FAngelscriptEngineScope>(*Engine);

	FAngelscriptEditorMenuExtensionTestAccess::UnregisterExtensions();
	if (!TestEqual(TEXT("MenuExtensions lifecycle test should start from an empty registration snapshot set"), UScriptEditorMenuExtension::GetRegisteredExtensionSnapshots().Num(), 0))
	{
		return false;
	}

	if (!CompileLifecycleScript(*this, *Engine, ModuleName, RelativeFilename, ScriptSource))
	{
		return false;
	}

	UASClass* const ScriptClass = FindGeneratedMenuExtensionClass(*this, *Engine, *ClassName);
	if (ScriptClass == nullptr)
	{
		return false;
	}

	UScriptEditorMenuExtension* const ExtensionCDO = ScriptClass->GetDefaultObject<UScriptEditorMenuExtension>();
	if (!TestNotNull(TEXT("MenuExtensions lifecycle test should resolve the generated extension CDO"), ExtensionCDO))
	{
		return false;
	}

	if (!TestTrue(TEXT("MenuExtensions lifecycle test should compile under an initial-compile-finished engine"), Engine->IsInitialCompileFinished()))
	{
		return false;
	}

	const EScriptEditorMenuExtensionLocation ExpectedLocation = ExtensionCDO->ExtensionMenu;
	const FName ExpectedExtensionPoint = ExtensionCDO->ExtensionPoint;
	const FName ExpectedSectionName =
		ExpectedLocation == EScriptEditorMenuExtensionLocation::ToolMenu
			? ScriptClass->GetFName()
			: NAME_None;

	UScriptEditorMenuExtension::InitializeExtensions();

	const TArray<UScriptEditorMenuExtension::FRegisteredExtensionSnapshot> InitialSnapshots = UScriptEditorMenuExtension::GetRegisteredExtensionSnapshots();
	const TArray<FString> InitialSnapshotSignatures = BuildSnapshotSignatures(InitialSnapshots);
	if (!TestTrue(TEXT("MenuExtensions lifecycle test should register at least one snapshot after InitializeExtensions"), InitialSnapshots.Num() > 0))
	{
		return false;
	}

	bool bPassed = true;
	bPassed &= TestEqual(
		TEXT("MenuExtensions lifecycle test should register the generated extension exactly once after InitializeExtensions"),
		CountSnapshotMatches(InitialSnapshots, ExpectedLocation, ExpectedExtensionPoint, ExpectedSectionName),
		1);
	bPassed &= TestTrue(
		TEXT("MenuExtensions lifecycle test should expose the generated extension snapshot after InitializeExtensions"),
		ContainsSnapshot(InitialSnapshots, ExpectedLocation, ExpectedExtensionPoint, ExpectedSectionName));

	FAngelscriptClassGenerator::OnPostReload.Broadcast(false);
	const TArray<UScriptEditorMenuExtension::FRegisteredExtensionSnapshot> AfterSoftReloadSnapshots = UScriptEditorMenuExtension::GetRegisteredExtensionSnapshots();
	bPassed &= TestEqual(
		TEXT("MenuExtensions lifecycle test should not duplicate the generated extension on soft reload"),
		CountSnapshotMatches(AfterSoftReloadSnapshots, ExpectedLocation, ExpectedExtensionPoint, ExpectedSectionName),
		1);
	bPassed &= TestTrue(
		TEXT("MenuExtensions lifecycle test should keep the registration snapshot set unchanged on soft reload"),
		BuildSnapshotSignatures(AfterSoftReloadSnapshots) == InitialSnapshotSignatures);

	FAngelscriptClassGenerator::OnPostReload.Broadcast(true);
	const TArray<UScriptEditorMenuExtension::FRegisteredExtensionSnapshot> AfterFullReloadSnapshots = UScriptEditorMenuExtension::GetRegisteredExtensionSnapshots();
	bPassed &= TestEqual(
		TEXT("MenuExtensions lifecycle test should re-register the generated extension exactly once after full reload"),
		CountSnapshotMatches(AfterFullReloadSnapshots, ExpectedLocation, ExpectedExtensionPoint, ExpectedSectionName),
		1);
	bPassed &= TestTrue(
		TEXT("MenuExtensions lifecycle test should keep the registration snapshot set stable across full reload"),
		BuildSnapshotSignatures(AfterFullReloadSnapshots) == InitialSnapshotSignatures);

	FCoreDelegates::OnEnginePreExit.Broadcast();
	const TArray<UScriptEditorMenuExtension::FRegisteredExtensionSnapshot> AfterPreExitSnapshots = UScriptEditorMenuExtension::GetRegisteredExtensionSnapshots();
	bPassed &= TestEqual(TEXT("MenuExtensions lifecycle test should clear all registrations on pre-exit"), AfterPreExitSnapshots.Num(), 0);

	FCoreDelegates::OnEnginePreExit.Broadcast();
	return bPassed && TestEqual(TEXT("MenuExtensions lifecycle test should keep registrations cleared on repeated pre-exit"), UScriptEditorMenuExtension::GetRegisteredExtensionSnapshots().Num(), 0);
}

#endif
