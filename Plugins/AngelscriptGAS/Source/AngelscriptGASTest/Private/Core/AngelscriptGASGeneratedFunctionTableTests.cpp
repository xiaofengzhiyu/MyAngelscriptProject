#include "AngelscriptBinds.h"
#include "AngelscriptEngine.h"
#include "CQTest.h"

#if WITH_DEV_AUTOMATION_TESTS

TEST_CLASS_WITH_FLAGS(FAngelscriptGASGeneratedFunctionTableTests,
	"Angelscript.GAS.Engine.GeneratedFunctionTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
	TEST_METHOD(PreservesHandwrittenEntries)
	{
		if (!FAngelscriptEngine::IsInitialized()) { TestRunner->AddInfo(TEXT("Production engine not initialized in headless mode, skipping")); return; }

		UClass* AbilityAsyncLibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/AngelscriptGAS.AngelscriptAbilityAsyncLibrary"));
		if (!TestRunner->TestNotNull(TEXT("Generated GAS compatibility test should locate UAngelscriptAbilityAsyncLibrary"), AbilityAsyncLibraryClass))
		{
			return;
		}

		const TMap<UClass*, TMap<FString, FFuncEntry>>& ClassFuncMaps = FAngelscriptBinds::GetClassFuncMaps();
		const TMap<FString, FFuncEntry>* AsyncLibraryFunctionMap = ClassFuncMaps.Find(AbilityAsyncLibraryClass);
		if (!TestRunner->TestNotNull(TEXT("Generated GAS compatibility test should expose the async ability helper class in ClassFuncMaps"), AsyncLibraryFunctionMap))
		{
			return;
		}

		const FFuncEntry* WaitForAttributeChangedEntry = AsyncLibraryFunctionMap->Find(TEXT("WaitForAttributeChanged"));
		if (!TestRunner->TestNotNull(TEXT("Generated GAS compatibility test should keep the handwritten WaitForAttributeChanged function entry"), WaitForAttributeChangedEntry))
		{
			return;
		}

		FGenericFuncPtr WaitForAttributeChangedPointer = WaitForAttributeChangedEntry->FuncPtr;
		if (!TestRunner->TestTrue(TEXT("Generated GAS compatibility test should preserve the handwritten direct-call pointer for WaitForAttributeChanged"), WaitForAttributeChangedPointer.IsBound()))
		{
			return;
		}

		const FFuncEntry* WaitGameplayTagRemoveEntry = AsyncLibraryFunctionMap->Find(TEXT("WaitGameplayTagRemoveFromActor"));
		if (!TestRunner->TestNotNull(TEXT("Generated GAS compatibility test should expose WaitGameplayTagRemoveFromActor under its own key"), WaitGameplayTagRemoveEntry))
		{
			return;
		}

		FGenericFuncPtr WaitGameplayTagRemovePointer = WaitGameplayTagRemoveEntry->FuncPtr;
		if (!TestRunner->TestTrue(TEXT("Generated GAS compatibility test should keep WaitGameplayTagRemoveFromActor bound after handwritten registration"), WaitGameplayTagRemovePointer.IsBound()))
		{
			return;
		}

		TestRunner->TestFalse(TEXT("Generated GAS compatibility test should not reclassify handwritten GAS entries as reflective fallback"), WaitForAttributeChangedEntry->bReflectiveFallbackBound);
	}
};

#endif
