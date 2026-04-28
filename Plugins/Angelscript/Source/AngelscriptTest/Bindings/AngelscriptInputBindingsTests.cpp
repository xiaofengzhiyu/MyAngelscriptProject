#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptInputBindingsTests_Private
{
	static constexpr ANSICHAR InputKeyCompatModuleName[] = "ASInputKeyAndValueTypeCompat";
	static constexpr ANSICHAR InputSettingsCompatModuleName[] = "ASInputActionMappingAndSettingsCompat";

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FString BoolToScriptLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	bool SetArgAddressChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Address,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind address argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgAddress(ArgumentIndex, Address),
			static_cast<int32>(asSUCCESS));
	}

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	bool ExecuteIntFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		int32& OutResult)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should create an execution context"), ContextLabel), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		if (!BindArguments(*Context))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptInputBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInputKeyAndValueTypeCompatTest,
	"Angelscript.TestModule.Bindings.InputKeyAndValueTypeCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInputActionMappingAndSettingsCompatTest,
	"Angelscript.TestModule.Bindings.InputActionMappingAndSettingsCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptInputKeyAndValueTypeCompatTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASInputKeyAndValueTypeCompat"));
	};

	const int32 ExpectedBooleanValueType = static_cast<int32>(FInputActionValue::GetValueTypeFromKey(EKeys::LeftMouseButton));
	const int32 ExpectedAxis2DValueType = static_cast<int32>(FInputActionValue::GetValueTypeFromKey(EKeys::Gamepad_Left2D));

	FString Script = TEXT(R"(
int Entry()
{
	FKey SpaceFromName = FKey(n"SpaceBar");
	FKey SpaceImplicit = n"SpaceBar";
	if (!SpaceFromName.IsValid())
		return 10;
	if (!(SpaceFromName == EKeys::SpaceBar))
		return 20;
	if (!(SpaceImplicit == EKeys::SpaceBar))
		return 30;
	if (SpaceFromName.IsMouseButton())
		return 40;
	if (SpaceFromName.IsGamepadKey())
		return 50;
	if (SpaceFromName.GetKeyName() != n"SpaceBar")
		return 60;
	if (SpaceFromName.GetDisplayName(false).ToString().IsEmpty())
		return 70;

	FKey LeftMouse = EKeys::LeftMouseButton;
	if (!LeftMouse.IsMouseButton())
		return 80;
	if (LeftMouse.IsTouch())
		return 90;
	if (LeftMouse.GetKeyName() != n"LeftMouseButton")
		return 100;

	FKey GamepadAxis = EKeys::Gamepad_Left2D;
	if (!GamepadAxis.IsValid())
		return 110;
	if (!GamepadAxis.IsGamepadKey())
		return 120;
	if (!GamepadAxis.IsAxis2D())
		return 130;
	if (GamepadAxis.IsAxis3D())
		return 140;
	if (int(FInputActionValue::GetValueTypeFromKey(GamepadAxis)) != __EXPECTED_AXIS2D_TYPE__)
		return 150;
	if (int(FInputActionValue::GetValueTypeFromKey(LeftMouse)) != __EXPECTED_BOOLEAN_TYPE__)
		return 160;
	if (!EKeys::LeftShift.IsModifierKey())
		return 170;

	FKey InvalidKey = EKeys::Invalid;
	if (InvalidKey.IsValid())
		return 180;

	FInputActionValue Value(FVector2D(3.0f, 4.0f));
	if (Value.GetAxis2D().X != 3.0f || Value.GetAxis2D().Y != 4.0f)
		return 190;

	return 1;
}
)");
	ReplaceToken(Script, TEXT("__EXPECTED_BOOLEAN_TYPE__"), FString::FromInt(ExpectedBooleanValueType));
	ReplaceToken(Script, TEXT("__EXPECTED_AXIS2D_TYPE__"), FString::FromInt(ExpectedAxis2DValueType));

	int32 Result = INDEX_NONE;
	ASTEST_COMPILE_RUN_INT(
		Engine,
		InputKeyCompatModuleName,
		Script,
		TEXT("int Entry()"),
		Result);

	bPassed &= TestEqual(
		TEXT("FKey construction, EKeys globals, and FInputActionValue::GetValueTypeFromKey should stay compatible with the native input bindings"),
		Result,
		1);
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptInputActionMappingAndSettingsCompatTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASInputActionMappingAndSettingsCompat"));
	};

	UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
	if (!TestNotNull(TEXT("Input mapping compat test should resolve UInputSettings"), InputSettings))
	{
		return false;
	}

	FInputActionKeyMapping NativeMapping;
	NativeMapping.ActionName = TEXT("Jump");
	NativeMapping.Key = EKeys::SpaceBar;
	NativeMapping.bShift = true;
	NativeMapping.bCtrl = false;
	NativeMapping.bAlt = false;
	NativeMapping.bCmd = false;

	const int32 ExpectedActionMappingCount = InputSettings->GetActionMappings().Num();
	const bool bExpectedJumpExists = InputSettings->DoesActionExist(TEXT("Jump"));
	const bool bExpectedMoveForwardExists = InputSettings->DoesAxisExist(TEXT("MoveForward"));
	const int32 ExpectedBooleanValueType = static_cast<int32>(FInputActionValue::GetValueTypeFromKey(NativeMapping.Key));

	FString Script = TEXT(R"(
uint ReadBindingHandle(const FInputBindingHandle& BindingHandle)
{
	return BindingHandle.GetHandle();
}

int VerifyInputActionMapping(const FInputActionKeyMapping& Mapping, UInputSettings Settings)
{
	if (Mapping.ActionName != n"Jump")
		return 10;
	if (!(Mapping.Key == EKeys::SpaceBar))
		return 20;
	if (!Mapping.bShift || Mapping.bCtrl || Mapping.bAlt || Mapping.bCmd)
		return 30;

	FInputActionKeyMapping Copy = Mapping;
	if (!(Copy == Mapping))
		return 40;

	Copy.Key = EKeys::Enter;
	if (Copy == Mapping)
		return 50;
	if (Copy.Key.GetKeyName() != n"Enter")
		return 60;

	if (Settings == null)
		return 70;
	if (Settings.GetActionMappings().Num() != __EXPECTED_ACTION_MAPPING_COUNT__)
		return 80;
	if (Settings.DoesActionExist(n"Jump") != __EXPECTED_JUMP_EXISTS__)
		return 90;
	if (Settings.DoesAxisExist(n"MoveForward") != __EXPECTED_MOVE_FORWARD_EXISTS__)
		return 100;
	const FName UniqueJumpNameA = Settings.GetUniqueActionName(n"Jump");
	const FName UniqueJumpNameB = Settings.GetUniqueActionName(n"Jump");
	if (UniqueJumpNameA == n"Jump" || UniqueJumpNameB == n"Jump")
		return 110;
	if (UniqueJumpNameA == UniqueJumpNameB)
		return 115;
	if (Settings.DoesActionExist(UniqueJumpNameA) || Settings.DoesActionExist(UniqueJumpNameB))
		return 118;
	if (int(FInputActionValue::GetValueTypeFromKey(Mapping.Key)) != __EXPECTED_BOOLEAN_TYPE__)
		return 120;

	return 1;
}
)");
	ReplaceToken(Script, TEXT("__EXPECTED_ACTION_MAPPING_COUNT__"), FString::FromInt(ExpectedActionMappingCount));
	ReplaceToken(Script, TEXT("__EXPECTED_JUMP_EXISTS__"), BoolToScriptLiteral(bExpectedJumpExists));
	ReplaceToken(Script, TEXT("__EXPECTED_MOVE_FORWARD_EXISTS__"), BoolToScriptLiteral(bExpectedMoveForwardExists));
	ReplaceToken(Script, TEXT("__EXPECTED_BOOLEAN_TYPE__"), FString::FromInt(ExpectedBooleanValueType));

	asIScriptModule* Module = BuildModule(*this, Engine, InputSettingsCompatModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyInputActionMapping(const FInputActionKeyMapping& Mapping, UInputSettings Settings)"),
			[this, &NativeMapping, InputSettings](asIScriptContext& Context)
			{
				return SetArgAddressChecked(*this, Context, 0, &NativeMapping, TEXT("VerifyInputActionMapping"))
					&& SetArgObjectChecked(*this, Context, 1, InputSettings, TEXT("VerifyInputActionMapping"));
			},
			TEXT("VerifyInputActionMapping"),
			Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("FInputActionKeyMapping and UInputSettings entry points should match the native input configuration baselines while FInputBindingHandle remains script-compilable"),
		Result,
		1);
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
