#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptInputActionValueConversionBindingsTests_Private
{
	static constexpr ANSICHAR TypedReadbackModuleName[] = "ASInputActionValueTypedReadbackCompat";
	static constexpr ANSICHAR ConversionModuleName[] = "ASInputActionValueConversionCompat";

	bool ExecuteScriptEntry(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const ANSICHAR* ModuleName,
		const TCHAR* Script,
		int32& OutResult)
	{
		asIScriptModule* Module = BuildModule(Test, Engine, ModuleName, Script);
		if (Module == nullptr)
		{
			return false;
		}

		asIScriptFunction* Function = GetFunctionByDecl(Test, *Module, TEXT("int Entry()"));
		if (Function == nullptr)
		{
			return false;
		}

		return ExecuteIntFunction(Test, Engine, *Function, OutResult);
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptInputActionValueConversionBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInputActionValueTypedReadbackCompatBindingsTest,
	"Angelscript.TestModule.Bindings.InputActionValue.TypedReadbackCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInputActionValueConversionCompatBindingsTest,
	"Angelscript.TestModule.Bindings.InputActionValue.ConversionCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptInputActionValueTypedReadbackCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	int32 Result = INDEX_NONE;
	if (!ExecuteScriptEntry(
			*this,
			Engine,
			TypedReadbackModuleName,
			TEXT(R"AS(
int Entry()
{
	FInputActionValue Axis1D(7.5f);
	if (!Math::IsNearlyEqual(Axis1D.GetAxis1D(), 7.5f, 0.001f))
		return 10;
	if (!Axis1D.GetAxis2D().Equals(FVector2D(7.5, 0.0), 0.001))
		return 20;
	if (!Axis1D.GetAxis3D().Equals(FVector(7.5, 0.0, 0.0), 0.001))
		return 30;

	FInputActionValue Axis2D(FVector2D(3.0, 4.0));
	if (!Axis2D.GetAxis2D().Equals(FVector2D(3.0, 4.0), 0.001))
		return 40;
	if (!Axis2D.GetAxis3D().Equals(FVector(3.0, 4.0, 0.0), 0.001))
		return 50;

	FInputActionValue Axis3D(FVector(1.0, 2.0, 3.0));
	if (!Axis3D.GetAxis3D().Equals(FVector(1.0, 2.0, 3.0), 0.001))
		return 60;

	FInputActionValue TrueValue(FInputActionValue::GetValueTypeFromKey(EKeys::LeftMouseButton), FVector(1.0, 0.0, 0.0));
	if (!TrueValue.Get())
		return 70;

	FInputActionValue FalseValue(FInputActionValue::GetValueTypeFromKey(EKeys::LeftMouseButton), FVector::ZeroVector);
	if (FalseValue.Get())
		return 80;

	return 1;
}
)AS"),
			Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("FInputActionValue typed constructors and getter readback should preserve the script-visible native value shape"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptInputActionValueConversionCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	int32 Result = INDEX_NONE;
	if (!ExecuteScriptEntry(
			*this,
			Engine,
			ConversionModuleName,
			TEXT(R"AS(
int Entry()
{
	FInputActionValue ConvertByType(FVector(9.0, 8.0, 7.0));
	ConvertByType.ConvertToType(FInputActionValue::GetValueTypeFromKey(EKeys::Gamepad_Left2D));
	if (!ConvertByType.GetAxis2D().Equals(FVector2D(9.0, 8.0), 0.001))
		return 10;
	if (!ConvertByType.GetAxis3D().Equals(FVector(9.0, 8.0, 0.0), 0.001))
		return 20;

	FInputActionValue ConvertByOther(FVector(6.0, 5.0, 4.0));
	FInputActionValue Axis1DTemplate(1.0f);
	ConvertByOther.ConvertToType(Axis1DTemplate);
	if (!Math::IsNearlyEqual(ConvertByOther.GetAxis1D(), 6.0f, 0.001f))
		return 30;
	if (!ConvertByOther.GetAxis3D().Equals(FVector(6.0, 0.0, 0.0), 0.001))
		return 40;

	FInputActionValue Accumulated(FVector2D(1.0, 2.0));
	Accumulated += FInputActionValue(FVector2D(3.0, 4.0));
	if (!Accumulated.GetAxis2D().Equals(FVector2D(4.0, 6.0), 0.001))
		return 50;

	Accumulated.ConvertToType(FInputActionValue::GetValueTypeFromKey(EKeys::LeftMouseButton));
	if (!Accumulated.Get())
		return 60;
	if (!Accumulated.GetAxis3D().Equals(FVector(4.0, 0.0, 0.0), 0.001))
		return 70;

	return 1;
}
)AS"),
			Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("FInputActionValue ConvertToType overloads should preserve native axis truncation and boolean conversion semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
