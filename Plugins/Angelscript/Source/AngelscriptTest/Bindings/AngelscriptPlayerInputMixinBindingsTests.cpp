#include "../Shared/AngelscriptTestMacros.h"

#include "../../AngelscriptRuntime/Core/AngelscriptBinds.h"
#include "../../AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h"

#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptTest_Bindings_AngelscriptPlayerInputMixinBindingsTests_Private
{
	constexpr const TCHAR* PlayerInputHelperTypeName = TEXT("UPlayerInputScriptMixinLibrary");
	constexpr const TCHAR* PlayerControllerHelperTypeName = TEXT("UPlayerControllerInputScriptMixinLibrary");

	bool ValidateDirectEntry(
		FAutomationTestBase& Test,
		const TMap<FString, FFuncEntry>& FunctionMap,
		const TCHAR* FunctionName,
		const TCHAR* ContextLabel)
	{
		const FFuncEntry* Entry = FunctionMap.Find(FunctionName);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should register a ClassFuncMaps entry for %s"), ContextLabel, FunctionName),
				Entry))
		{
			return false;
		}

		FGenericFuncPtr FunctionPointer = Entry->FuncPtr;
		const bool bDirectBound = Test.TestTrue(
			*FString::Printf(TEXT("%s should keep %s on a direct-call path"), ContextLabel, FunctionName),
			FunctionPointer.IsBound());
		const bool bNotReflectiveFallback = Test.TestFalse(
			*FString::Printf(TEXT("%s should not classify %s as reflective fallback"), ContextLabel, FunctionName),
			Entry->bReflectiveFallbackBound);
		return bDirectBound && bNotReflectiveFallback;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptPlayerInputMixinBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPlayerInputMappingMutatorsTest,
	"Angelscript.TestModule.FunctionLibraries.PlayerInputMappingMutators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPlayerInputMappingMutatorsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
	if (!TestNotNull(TEXT("PlayerInput mixin test should expose a script engine"), ScriptEngine))
	{
		return false;
	}

	asITypeInfo* PlayerInputHelperType = ScriptEngine->GetTypeInfoByName(TCHAR_TO_ANSI(PlayerInputHelperTypeName));
	asITypeInfo* PlayerControllerHelperType = ScriptEngine->GetTypeInfoByName(TCHAR_TO_ANSI(PlayerControllerHelperTypeName));
	if (!TestNotNull(TEXT("PlayerInput mixin test should expose UPlayerInputScriptMixinLibrary in the script type system"), PlayerInputHelperType) ||
		!TestNotNull(TEXT("PlayerInput mixin test should expose UPlayerControllerInputScriptMixinLibrary in the script type system"), PlayerControllerHelperType))
	{
		return false;
	}

	const TMap<UClass*, TMap<FString, FFuncEntry>>& ClassFuncMaps = FAngelscriptBinds::GetClassFuncMaps();
	const TMap<FString, FFuncEntry>* PlayerInputFunctionMap = ClassFuncMaps.Find(UPlayerInputScriptMixinLibrary::StaticClass());
	const TMap<FString, FFuncEntry>* PlayerControllerFunctionMap = ClassFuncMaps.Find(UPlayerControllerInputScriptMixinLibrary::StaticClass());
	if (!TestNotNull(TEXT("PlayerInput mixin test should expose UPlayerInputScriptMixinLibrary in ClassFuncMaps"), PlayerInputFunctionMap) ||
		!TestNotNull(TEXT("PlayerInput mixin test should expose UPlayerControllerInputScriptMixinLibrary in ClassFuncMaps"), PlayerControllerFunctionMap))
	{
		return false;
	}

	bPassed &= ValidateDirectEntry(*this, *PlayerInputFunctionMap, TEXT("AddActionMapping"), TEXT("PlayerInput helper"));
	bPassed &= ValidateDirectEntry(*this, *PlayerInputFunctionMap, TEXT("RemoveActionMapping"), TEXT("PlayerInput helper"));
	bPassed &= ValidateDirectEntry(*this, *PlayerInputFunctionMap, TEXT("AddAxisMapping"), TEXT("PlayerInput helper"));
	bPassed &= ValidateDirectEntry(*this, *PlayerInputFunctionMap, TEXT("RemoveAxisMapping"), TEXT("PlayerInput helper"));
	bPassed &= ValidateDirectEntry(*this, *PlayerControllerFunctionMap, TEXT("GetPlayerInput"), TEXT("PlayerController helper"));

	UPlayerInput* PlayerInput = NewObject<UPlayerInput>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("PlayerInput mixin test should create a transient player input"), PlayerInput))
	{
		return false;
	}

	const FName ActionName(*FString::Printf(TEXT("PlayerInputAction_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FName AxisName(*FString::Printf(TEXT("PlayerInputAxis_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));

	FInputActionKeyMapping ActionMapping;
	ActionMapping.ActionName = ActionName;
	ActionMapping.Key = EKeys::SpaceBar;
	ActionMapping.bShift = true;

	FInputAxisKeyMapping AxisMapping;
	AxisMapping.AxisName = AxisName;
	AxisMapping.Key = EKeys::MouseX;
	AxisMapping.Scale = -1.0f;

	bPassed &= TestEqual(
		TEXT("Fresh player input should start without scripted action mappings"),
		UPlayerInputScriptMixinLibrary::GetKeysForAction(PlayerInput, ActionName).Num(),
		0);
	bPassed &= TestEqual(
		TEXT("Fresh player input should start without scripted axis mappings"),
		UPlayerInputScriptMixinLibrary::GetKeysForAxis(PlayerInput, AxisName).Num(),
		0);

	UPlayerInputScriptMixinLibrary::AddActionMapping(PlayerInput, ActionMapping);
	UPlayerInputScriptMixinLibrary::AddActionMapping(PlayerInput, ActionMapping);
	UPlayerInputScriptMixinLibrary::AddAxisMapping(PlayerInput, AxisMapping);
	UPlayerInputScriptMixinLibrary::AddAxisMapping(PlayerInput, AxisMapping);
	UPlayerInputScriptMixinLibrary::ForceRebuildingKeyMaps(PlayerInput, false);

	const TArray<FInputActionKeyMapping>& AddedActionMappings = UPlayerInputScriptMixinLibrary::GetKeysForAction(PlayerInput, ActionName);
	const TArray<FInputAxisKeyMapping>& AddedAxisMappings = UPlayerInputScriptMixinLibrary::GetKeysForAxis(PlayerInput, AxisName);
	bPassed &= TestEqual(
		TEXT("PlayerInput helper should preserve AddUnique semantics for action mappings"),
		AddedActionMappings.Num(),
		1);
	bPassed &= TestEqual(
		TEXT("PlayerInput helper should preserve AddUnique semantics for axis mappings"),
		AddedAxisMappings.Num(),
		1);
	if (AddedActionMappings.IsValidIndex(0))
	{
		bPassed &= TestEqual(
			TEXT("Added action mapping should preserve the scripted action name"),
			AddedActionMappings[0].ActionName,
			ActionName);
		bPassed &= TestTrue(
			TEXT("Added action mapping should preserve the scripted key and modifier flags"),
			AddedActionMappings[0].Key == EKeys::SpaceBar
				&& AddedActionMappings[0].bShift
				&& !AddedActionMappings[0].bCtrl
				&& !AddedActionMappings[0].bAlt
				&& !AddedActionMappings[0].bCmd);
	}
	if (AddedAxisMappings.IsValidIndex(0))
	{
		bPassed &= TestEqual(
			TEXT("Added axis mapping should preserve the scripted axis name"),
			AddedAxisMappings[0].AxisName,
			AxisName);
		bPassed &= TestTrue(
			TEXT("Added axis mapping should preserve the scripted key"),
			AddedAxisMappings[0].Key == EKeys::MouseX);
		bPassed &= TestEqual(
			TEXT("Added axis mapping should preserve the scripted scale"),
			AddedAxisMappings[0].Scale,
			-1.0f);
	}

	UPlayerInputScriptMixinLibrary::RemoveActionMapping(PlayerInput, ActionMapping);
	UPlayerInputScriptMixinLibrary::RemoveActionMapping(PlayerInput, ActionMapping);
	UPlayerInputScriptMixinLibrary::RemoveAxisMapping(PlayerInput, AxisMapping);
	UPlayerInputScriptMixinLibrary::RemoveAxisMapping(PlayerInput, AxisMapping);
	UPlayerInputScriptMixinLibrary::ForceRebuildingKeyMaps(PlayerInput, false);

	bPassed &= TestEqual(
		TEXT("PlayerInput helper should remove the scripted action mapping"),
		UPlayerInputScriptMixinLibrary::GetKeysForAction(PlayerInput, ActionName).Num(),
		0);
	bPassed &= TestEqual(
		TEXT("PlayerInput helper should remove the scripted axis mapping"),
		UPlayerInputScriptMixinLibrary::GetKeysForAxis(PlayerInput, AxisName).Num(),
		0);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
