#include "AngelscriptRuntimeModule.h"
#include "AngelscriptEngine.h"
#include "AngelscriptGameInstanceSubsystem.h"

IMPLEMENT_MODULE(FAngelscriptRuntimeModule, AngelscriptRuntime);

bool FAngelscriptRuntimeModule::bInitializeAngelscriptCalled = false;
TUniquePtr<FAngelscriptEngine> FAngelscriptRuntimeModule::OwnedPrimaryEngine;
#if WITH_DEV_AUTOMATION_TESTS
TOptional<bool> FAngelscriptRuntimeModule::StartupIsEditorOverrideForTesting;
TOptional<bool> FAngelscriptRuntimeModule::StartupIsRunningCommandletOverrideForTesting;
TFunction<FAngelscriptEngine*()> FAngelscriptRuntimeModule::InitializeOverrideForTesting;
FAngelscriptEngine* FAngelscriptRuntimeModule::InitializedOverrideEngineForTesting = nullptr;
#endif

void FAngelscriptRuntimeModule::StartupModule()
{
	bool bIsEditor = GIsEditor;
	bool bIsRunningCommandlet = IsRunningCommandlet();
	#if WITH_DEV_AUTOMATION_TESTS
	if (StartupIsEditorOverrideForTesting.IsSet())
	{
		bIsEditor = StartupIsEditorOverrideForTesting.GetValue();
	}
	if (StartupIsRunningCommandletOverrideForTesting.IsSet())
	{
		bIsRunningCommandlet = StartupIsRunningCommandletOverrideForTesting.GetValue();
	}
	#endif

	if (bIsEditor || bIsRunningCommandlet)
	{
		InitializeAngelscript();
	}

	if (bIsEditor)
	{
		FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
	}
}

void FAngelscriptRuntimeModule::ShutdownModule()
{
	if (FallbackTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FallbackTickHandle);
		FallbackTickHandle.Reset();
	}

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
		return;

	bInitializeAngelscriptCalled = true;
	#if WITH_DEV_AUTOMATION_TESTS
	if (InitializeOverrideForTesting)
	{
		InitializedOverrideEngineForTesting = nullptr;
		if (FAngelscriptEngine* OverrideEngine = InitializeOverrideForTesting())
		{
			FAngelscriptEngineContextStack::Push(OverrideEngine);
			InitializedOverrideEngineForTesting = OverrideEngine;
		}
		return;
	}
	#endif

	FModuleManager::Get().LoadModuleChecked(TEXT("AngelscriptRuntime"));
	if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		// Adopt an already-initialized ambient engine instead of re-running full initialization.
		if (CurrentEngine->GetScriptEngine() == nullptr)
		{
			CurrentEngine->Initialize();
		}
	}
	else
	{
		OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
		FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine->Initialize();
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void FAngelscriptRuntimeModule::SetStartupEnvironmentOverrideForTesting(const TOptional<bool>& bIsEditorOverride, const TOptional<bool>& bIsRunningCommandletOverride)
{
	StartupIsEditorOverrideForTesting = bIsEditorOverride;
	StartupIsRunningCommandletOverrideForTesting = bIsRunningCommandletOverride;
}

void FAngelscriptRuntimeModule::ClearStartupEnvironmentOverrideForTesting()
{
	StartupIsEditorOverrideForTesting.Reset();
	StartupIsRunningCommandletOverrideForTesting.Reset();
}

void FAngelscriptRuntimeModule::SetInitializeOverrideForTesting(TFunction<FAngelscriptEngine*()> InOverride)
{
	InitializeOverrideForTesting = MoveTemp(InOverride);
}

void FAngelscriptRuntimeModule::ResetInitializeStateForTesting()
{
	ClearStartupEnvironmentOverrideForTesting();

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

bool FAngelscriptRuntimeModule::TickFallbackPrimaryEngine(float DeltaTime)
{
	if (!UAngelscriptGameInstanceSubsystem::HasAnyTickOwner())
	{
		if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
		{
			if (CurrentEngine->ShouldTick())
			{
				CurrentEngine->Tick(DeltaTime);
			}
		}
	}

	return true;
}
