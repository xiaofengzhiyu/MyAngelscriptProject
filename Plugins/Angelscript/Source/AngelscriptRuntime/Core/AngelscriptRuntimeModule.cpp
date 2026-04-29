#include "AngelscriptRuntimeModule.h"
#include "AngelscriptEngine.h"
#include "AngelscriptEngineSubsystem.h"
#include "AngelscriptGameInstanceSubsystem.h"

IMPLEMENT_MODULE(FAngelscriptRuntimeModule, AngelscriptRuntime);

bool FAngelscriptRuntimeModule::bInitializeAngelscriptCalled = false;
TUniquePtr<FAngelscriptEngine> FAngelscriptRuntimeModule::OwnedPrimaryEngine;
#if WITH_DEV_AUTOMATION_TESTS
TFunction<FAngelscriptEngine*()> FAngelscriptRuntimeModule::InitializeOverrideForTesting;
FAngelscriptEngine* FAngelscriptRuntimeModule::InitializedOverrideEngineForTesting = nullptr;
#endif

void FAngelscriptRuntimeModule::StartupModule()
{
	UE_LOG(Angelscript, Verbose, TEXT("[RuntimeStartup] StartupModule."));
}

void FAngelscriptRuntimeModule::ShutdownModule()
{
	UE_LOG(Angelscript, Verbose, TEXT("[RuntimeStartup] ShutdownModule ownedEngine=%s"),
		OwnedPrimaryEngine.IsValid() ? TEXT("true") : TEXT("false"));

	if (OwnedPrimaryEngine.IsValid())
	{
		FAngelscriptEngineContextStack::Pop(OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine.Reset();
	}
}

FAngelscriptGetDynamicSpawnLevel& FAngelscriptRuntimeModule::GetDynamicSpawnLevel()
{
	static FAngelscriptGetDynamicSpawnLevel Delegate;
	return Delegate;
}

FAngelscriptDebugCheckBreakOptions& FAngelscriptRuntimeModule::GetDebugCheckBreakOptions()
{
	static FAngelscriptDebugCheckBreakOptions Delegate;
	return Delegate;
}

FAngelscriptGetDebugBreakFilters& FAngelscriptRuntimeModule::GetDebugBreakFilters()
{
	static FAngelscriptGetDebugBreakFilters Delegate;
	return Delegate;
}

FAngelscriptDebugObjectSuffix& FAngelscriptRuntimeModule::GetDebugObjectSuffix()
{
	static FAngelscriptDebugObjectSuffix Delegate;
	return Delegate;
}

FAngelscriptComponentCreated& FAngelscriptRuntimeModule::GetComponentCreated()
{
	static FAngelscriptComponentCreated Delegate;
	return Delegate;
}

FAngelscriptCompilationDelegate& FAngelscriptRuntimeModule::GetPreCompile()
{
	static FAngelscriptCompilationDelegate Delegate;
	return Delegate;
}

FAngelscriptCompilationDelegate& FAngelscriptRuntimeModule::GetPostCompile()
{
	static FAngelscriptCompilationDelegate Delegate;
	return Delegate;
}

FAngelscriptCompilationDelegate& FAngelscriptRuntimeModule::GetOnInitialCompileFinished()
{
	static FAngelscriptCompilationDelegate Delegate;
	return Delegate;
}

FAngelscriptClassAnalyzeDelegate& FAngelscriptRuntimeModule::GetClassAnalyze()
{
	static FAngelscriptClassAnalyzeDelegate Delegate;
	return Delegate;
}

FAngelscriptPostCompileClassCollection& FAngelscriptRuntimeModule::GetPostCompileClassCollection()
{
	static FAngelscriptPostCompileClassCollection Delegate;
	return Delegate;
}

FAngelscriptPreGenerateClasses& FAngelscriptRuntimeModule::GetPreGenerateClasses()
{
	static FAngelscriptPreGenerateClasses Delegate;
	return Delegate;
}

FAngelscriptLiteralAssetCreated& FAngelscriptRuntimeModule::GetOnLiteralAssetCreated()
{
	static FAngelscriptLiteralAssetCreated Delegate;
	return Delegate;
}

FAngelscriptLiteralAssetCreated& FAngelscriptRuntimeModule::GetPostLiteralAssetSetup()
{
	static FAngelscriptLiteralAssetCreated Delegate;
	return Delegate;
}

FAngelscriptDebugListAssets& FAngelscriptRuntimeModule::GetDebugListAssets()
{
	static FAngelscriptDebugListAssets Delegate;
	return Delegate;
}

FAngelscriptEditorCreateBlueprint& FAngelscriptRuntimeModule::GetEditorCreateBlueprint()
{
	static FAngelscriptEditorCreateBlueprint Delegate;
	return Delegate;
}

FAngelscriptEditorGetCreateBlueprintDefaultAssetPath& FAngelscriptRuntimeModule::GetEditorGetCreateBlueprintDefaultAssetPath()
{
	static FAngelscriptEditorGetCreateBlueprintDefaultAssetPath Delegate;
	return Delegate;
}

void FAngelscriptRuntimeModule::InitializeAngelscript()
{
	if (bInitializeAngelscriptCalled)
	{
		UE_LOG(Angelscript, Verbose, TEXT("[RuntimeStartup] InitializeAngelscript skipped because initialization already ran."));
		return;
	}

	bInitializeAngelscriptCalled = true;
	UE_LOG(Angelscript, Display, TEXT("[RuntimeStartup] InitializeAngelscript begin."));
	#if WITH_DEV_AUTOMATION_TESTS
	if (InitializeOverrideForTesting)
	{
		InitializedOverrideEngineForTesting = nullptr;
		if (FAngelscriptEngine* OverrideEngine = InitializeOverrideForTesting())
		{
			FAngelscriptEngineContextStack::Push(OverrideEngine);
			InitializedOverrideEngineForTesting = OverrideEngine;
			UE_LOG(Angelscript, Verbose, TEXT("[RuntimeStartup] InitializeAngelscript used automation override engine=%p."), OverrideEngine);
		}
		return;
	}
	#endif

	FModuleManager::Get().LoadModuleChecked(TEXT("AngelscriptRuntime"));
	if (UAngelscriptEngineSubsystem* EngineSubsystem = UAngelscriptEngineSubsystem::Get())
	{
		UE_LOG(Angelscript, Verbose, TEXT("[RuntimeStartup] Routing InitializeAngelscript to EngineSubsystem=%p."), EngineSubsystem);
		EngineSubsystem->EnsurePrimaryEngineInitialized();
		return;
	}

	if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		// Adopt an already-initialized ambient engine instead of re-running full initialization.
		if (CurrentEngine->GetScriptEngine() == nullptr)
		{
			UE_LOG(Angelscript, Display, TEXT("[RuntimeStartup] Initializing existing ambient engine=%p."), CurrentEngine);
			CurrentEngine->Initialize();
		}
		else
		{
			UE_LOG(Angelscript, Verbose, TEXT("[RuntimeStartup] Adopted initialized ambient engine=%p."), CurrentEngine);
		}
	}
	else
	{
		OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
		FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
		UE_LOG(Angelscript, Display, TEXT("[RuntimeStartup] Created owned primary engine=%p."), OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine->Initialize();
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void FAngelscriptRuntimeModule::SetInitializeOverrideForTesting(TFunction<FAngelscriptEngine*()> InOverride)
{
	InitializeOverrideForTesting = MoveTemp(InOverride);
}

void FAngelscriptRuntimeModule::ResetInitializeStateForTesting()
{
	if (InitializedOverrideEngineForTesting != nullptr)
	{
		FAngelscriptEngineContextStack::Pop(InitializedOverrideEngineForTesting);
		InitializedOverrideEngineForTesting = nullptr;
	}

	if (OwnedPrimaryEngine.IsValid())
	{
		FAngelscriptEngineContextStack::Pop(OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine.Reset();
	}
	bInitializeAngelscriptCalled = false;
	InitializeOverrideForTesting = nullptr;
}
#endif
