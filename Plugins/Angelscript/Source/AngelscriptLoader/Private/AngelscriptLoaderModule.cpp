#include "AngelscriptLoaderModule.h"

#include "AngelscriptEngine.h"
#include "AngelscriptRuntimeModule.h"

IMPLEMENT_MODULE(FAngelscriptLoaderModule, AngelscriptLoader);

#if WITH_DEV_AUTOMATION_TESTS
TOptional<bool> FAngelscriptLoaderModule::StartupIsEditorOverrideForTesting;
TOptional<bool> FAngelscriptLoaderModule::StartupIsRunningCommandletOverrideForTesting;
#endif

void FAngelscriptLoaderModule::StartupModule()
{
	const bool bShouldInitialize = ShouldInitializeAngelscript();
	UE_LOG(Angelscript, Display, TEXT("[LoaderStartup] StartupModule editor=%s commandlet=%s shouldInitialize=%s"),
		GIsEditor ? TEXT("true") : TEXT("false"),
		IsRunningCommandlet() ? TEXT("true") : TEXT("false"),
		bShouldInitialize ? TEXT("true") : TEXT("false"));

	if (bShouldInitialize)
	{
		UE_LOG(Angelscript, Display, TEXT("[LoaderStartup] Calling FAngelscriptRuntimeModule::InitializeAngelscript."));
		FAngelscriptRuntimeModule::InitializeAngelscript();
	}
	else
	{
		UE_LOG(Angelscript, Verbose, TEXT("[LoaderStartup] Skipping Angelscript initialization outside editor/commandlet startup."));
	}
}

void FAngelscriptLoaderModule::ShutdownModule()
{
	UE_LOG(Angelscript, Verbose, TEXT("[LoaderStartup] ShutdownModule."));
}

bool FAngelscriptLoaderModule::ShouldInitializeAngelscript() const
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

	return bIsEditor || bIsRunningCommandlet;
}

#if WITH_DEV_AUTOMATION_TESTS
void FAngelscriptLoaderModule::SetStartupEnvironmentOverrideForTesting(const TOptional<bool>& bIsEditorOverride, const TOptional<bool>& bIsRunningCommandletOverride)
{
	StartupIsEditorOverrideForTesting = bIsEditorOverride;
	StartupIsRunningCommandletOverrideForTesting = bIsRunningCommandletOverride;
}

void FAngelscriptLoaderModule::ClearStartupEnvironmentOverrideForTesting()
{
	StartupIsEditorOverrideForTesting.Reset();
	StartupIsRunningCommandletOverrideForTesting.Reset();
}
#endif
