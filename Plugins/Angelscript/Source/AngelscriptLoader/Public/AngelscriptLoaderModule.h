#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class ANGELSCRIPTLOADER_API FAngelscriptLoaderModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	friend struct FAngelscriptLoaderModuleTestAccess;

	bool ShouldInitializeAngelscript() const;

	#if WITH_DEV_AUTOMATION_TESTS
	static void SetStartupEnvironmentOverrideForTesting(const TOptional<bool>& bIsEditorOverride, const TOptional<bool>& bIsRunningCommandletOverride);
	static void ClearStartupEnvironmentOverrideForTesting();
	static TOptional<bool> StartupIsEditorOverrideForTesting;
	static TOptional<bool> StartupIsRunningCommandletOverrideForTesting;
	#endif
};
