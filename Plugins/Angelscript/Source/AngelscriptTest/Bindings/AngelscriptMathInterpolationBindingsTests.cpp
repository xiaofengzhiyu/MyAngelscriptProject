#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Math/Color.h"
#include "Math/Quat.h"
#include "Math/Vector2D.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathInterpolationBindingsTest,
	"Angelscript.TestModule.Bindings.MathInterpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptMathInterpolationBindingsTests_Private
{
	template <typename TValue>
	bool ExecuteValueFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		const TCHAR* ContextLabel,
		TValue& OutValue)
	{
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should create an execution context"), ContextLabel), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("%s saw a script exception: %s"),
					ContextLabel,
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			Context->Release();
			return false;
		}

		void* ReturnValueAddress = Context->GetAddressOfReturnValue();
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("%s should expose return value storage"), ContextLabel),
			ReturnValueAddress))
		{
			Context->Release();
			return false;
		}

		OutValue = *static_cast<TValue*>(ReturnValueAddress);
		Context->Release();
		return true;
	}

	bool QuatMatches(const FQuat& ActualValue, const FQuat& ExpectedValue, double ToleranceDegrees = 0.05)
	{
		FQuat Actual = ActualValue;
		FQuat Expected = ExpectedValue;
		Actual.Normalize();
		Expected.Normalize();
		if ((Actual | Expected) < 0.0)
		{
			Expected = FQuat(-Expected.X, -Expected.Y, -Expected.Z, -Expected.W);
		}
		return FMath::RadiansToDegrees(Actual.AngularDistance(Expected)) <= ToleranceDegrees;
	}

	bool QuatMatches(const FQuat4f& ActualValue, const FQuat4f& ExpectedValue, double ToleranceDegrees = 0.05)
	{
		FQuat4f Actual = ActualValue;
		FQuat4f Expected = ExpectedValue;
		Actual.Normalize();
		Expected.Normalize();
		if ((Actual | Expected) < 0.0f)
		{
			Expected = FQuat4f(-Expected.X, -Expected.Y, -Expected.Z, -Expected.W);
		}
		return FMath::RadiansToDegrees(static_cast<double>(Actual.AngularDistance(Expected))) <= ToleranceDegrees;
	}

	template <typename TQuat>
	bool VerifyQuat(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const TQuat& Actual,
		const TQuat& Expected,
		double ToleranceDegrees = 0.05)
	{
		return Test.TestTrue(What, QuatMatches(Actual, Expected, ToleranceDegrees));
	}

	bool VerifyVector(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FVector& Actual,
		const FVector& Expected,
		double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}

	bool VerifyVector2D(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FVector2D& Actual,
		const FVector2D& Expected,
		double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}

	bool VerifyColor(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FLinearColor& Actual,
		const FLinearColor& Expected,
		float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathInterpolationBindingsTests_Private;

bool FAngelscriptMathInterpolationBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASMathInterpolationCompat",
		TEXT(R"(
FQuat GetQuatInterp()
{
	const FQuat Current = FQuat(FRotator(0.0f, 20.0f, -10.0f));
	const FQuat Target = FQuat(FRotator(45.0f, 110.0f, 15.0f));
	return Math::QInterpTo(Current, Target, 0.25f, 3.0f);
}

FQuat GetQuatConstantInterp()
{
	const FQuat Current = FQuat(FRotator(0.0f, 20.0f, -10.0f));
	const FQuat Target = FQuat(FRotator(45.0f, 110.0f, 15.0f));
	return Math::QInterpConstantTo(Current, Target, 0.25f, 35.0f);
}

FQuat4f GetQuat4fInterp()
{
	const FQuat4f Current = FQuat4f(FRotator3f(5.0f, 15.0f, -25.0f));
	const FQuat4f Target = FQuat4f(FRotator3f(35.0f, 95.0f, 20.0f));
	return Math::QInterpTo(Current, Target, 0.25f, 3.0f);
}

FQuat4f GetQuat4fConstantInterp()
{
	const FQuat4f Current = FQuat4f(FRotator3f(5.0f, 15.0f, -25.0f));
	const FQuat4f Target = FQuat4f(FRotator3f(35.0f, 95.0f, 20.0f));
	return Math::QInterpConstantTo(Current, Target, 0.25f, 35.0f);
}

FVector GetNormalRotationInterp()
{
	return Math::VInterpNormalRotationTo(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), 0.25f, 90.0f);
}

FVector2D GetVector2DInterp()
{
	return Math::Vector2DInterpTo(FVector2D(-2.0f, 5.0f), FVector2D(8.0f, -3.0f), 0.25f, 3.0f);
}

FVector2D GetVector2DConstantInterp()
{
	return Math::Vector2DInterpConstantTo(FVector2D(-2.0f, 5.0f), FVector2D(8.0f, -3.0f), 0.25f, 4.0f);
}

FLinearColor GetColorInterp()
{
	return Math::CInterpTo(
		FLinearColor(0.1f, 0.25f, 0.5f, 1.0f),
		FLinearColor(0.9f, 0.75f, 0.2f, 0.35f),
		0.25f,
		2.0f);
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	const FQuat CurrentQuat(FRotator(0.0f, 20.0f, -10.0f));
	const FQuat TargetQuat(FRotator(45.0f, 110.0f, 15.0f));
	const FQuat4f CurrentQuat4f(FRotator3f(5.0f, 15.0f, -25.0f));
	const FQuat4f TargetQuat4f(FRotator3f(35.0f, 95.0f, 20.0f));
	const FVector CurrentNormal(1.0f, 0.0f, 0.0f);
	const FVector TargetNormal(0.0f, 1.0f, 0.0f);
	const FVector2D CurrentVector2D(-2.0f, 5.0f);
	const FVector2D TargetVector2D(8.0f, -3.0f);
	const FLinearColor CurrentColor(0.1f, 0.25f, 0.5f, 1.0f);
	const FLinearColor TargetColor(0.9f, 0.75f, 0.2f, 0.35f);

	FQuat ScriptQuatInterp;
	FQuat ScriptQuatConstantInterp;
	FQuat4f ScriptQuat4fInterp;
	FQuat4f ScriptQuat4fConstantInterp;
	FVector ScriptNormalRotationInterp;
	FVector2D ScriptVector2DInterp;
	FVector2D ScriptVector2DConstantInterp;
	FLinearColor ScriptColorInterp;

	asIScriptFunction* QuatInterpFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat GetQuatInterp()"));
	asIScriptFunction* QuatConstantInterpFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat GetQuatConstantInterp()"));
	asIScriptFunction* Quat4fInterpFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fInterp()"));
	asIScriptFunction* Quat4fConstantInterpFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fConstantInterp()"));
	asIScriptFunction* NormalRotationInterpFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetNormalRotationInterp()"));
	asIScriptFunction* Vector2DInterpFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector2D GetVector2DInterp()"));
	asIScriptFunction* Vector2DConstantInterpFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector2D GetVector2DConstantInterp()"));
	asIScriptFunction* ColorInterpFunction = GetFunctionByDecl(*this, *Module, TEXT("FLinearColor GetColorInterp()"));
	if (QuatInterpFunction == nullptr
		|| QuatConstantInterpFunction == nullptr
		|| Quat4fInterpFunction == nullptr
		|| Quat4fConstantInterpFunction == nullptr
		|| NormalRotationInterpFunction == nullptr
		|| Vector2DInterpFunction == nullptr
		|| Vector2DConstantInterpFunction == nullptr
		|| ColorInterpFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *QuatInterpFunction, TEXT("GetQuatInterp"), ScriptQuatInterp) &&
		ExecuteValueFunction(*this, Engine, *QuatConstantInterpFunction, TEXT("GetQuatConstantInterp"), ScriptQuatConstantInterp) &&
		ExecuteValueFunction(*this, Engine, *Quat4fInterpFunction, TEXT("GetQuat4fInterp"), ScriptQuat4fInterp) &&
		ExecuteValueFunction(*this, Engine, *Quat4fConstantInterpFunction, TEXT("GetQuat4fConstantInterp"), ScriptQuat4fConstantInterp) &&
		ExecuteValueFunction(*this, Engine, *NormalRotationInterpFunction, TEXT("GetNormalRotationInterp"), ScriptNormalRotationInterp) &&
		ExecuteValueFunction(*this, Engine, *Vector2DInterpFunction, TEXT("GetVector2DInterp"), ScriptVector2DInterp) &&
		ExecuteValueFunction(*this, Engine, *Vector2DConstantInterpFunction, TEXT("GetVector2DConstantInterp"), ScriptVector2DConstantInterp) &&
		ExecuteValueFunction(*this, Engine, *ColorInterpFunction, TEXT("GetColorInterp"), ScriptColorInterp);
	if (!bExecutedAll)
	{
		return false;
	}

	const FQuat ExpectedQuatInterp = FMath::QInterpTo<double>(CurrentQuat, TargetQuat, 0.25f, 3.0f);
	const FQuat ExpectedQuatConstantInterp = FMath::QInterpConstantTo<double>(CurrentQuat, TargetQuat, 0.25f, 35.0f);
	const FQuat4f ExpectedQuat4fInterp = FMath::QInterpTo<float>(CurrentQuat4f, TargetQuat4f, 0.25f, 3.0f);
	const FQuat4f ExpectedQuat4fConstantInterp = FMath::QInterpConstantTo<float>(CurrentQuat4f, TargetQuat4f, 0.25f, 35.0f);
	const FVector ExpectedNormalRotationInterp = FMath::VInterpNormalRotationTo(CurrentNormal, TargetNormal, 0.25f, 90.0f);
	const FVector2D ExpectedVector2DInterp = FMath::Vector2DInterpTo(CurrentVector2D, TargetVector2D, 0.25f, 3.0f);
	const FVector2D ExpectedVector2DConstantInterp = FMath::Vector2DInterpConstantTo(CurrentVector2D, TargetVector2D, 0.25f, 4.0f);
	const FLinearColor ExpectedColorInterp = FMath::CInterpTo(CurrentColor, TargetColor, 0.25f, 2.0f);

	bPassed =
		VerifyQuat(
			*this,
			TEXT("Math::QInterpTo should match the native FQuat interpolation baseline"),
			ScriptQuatInterp,
			ExpectedQuatInterp) &&
		VerifyQuat(
			*this,
			TEXT("Math::QInterpConstantTo should match the native FQuat constant-speed interpolation baseline"),
			ScriptQuatConstantInterp,
			ExpectedQuatConstantInterp) &&
		VerifyQuat(
			*this,
			TEXT("Math::QInterpTo should match the native FQuat4f interpolation baseline"),
			ScriptQuat4fInterp,
			ExpectedQuat4fInterp) &&
		VerifyQuat(
			*this,
			TEXT("Math::QInterpConstantTo should match the native FQuat4f constant-speed interpolation baseline"),
			ScriptQuat4fConstantInterp,
			ExpectedQuat4fConstantInterp) &&
		VerifyVector(
			*this,
			TEXT("Math::VInterpNormalRotationTo should match the native unit-vector rotation interpolation baseline"),
			ScriptNormalRotationInterp,
			ExpectedNormalRotationInterp,
			0.001) &&
		VerifyVector2D(
			*this,
			TEXT("Math::Vector2DInterpTo should match the native eased FVector2D interpolation baseline"),
			ScriptVector2DInterp,
			ExpectedVector2DInterp,
			0.001) &&
		VerifyVector2D(
			*this,
			TEXT("Math::Vector2DInterpConstantTo should match the native constant-speed FVector2D interpolation baseline"),
			ScriptVector2DConstantInterp,
			ExpectedVector2DConstantInterp,
			0.001) &&
		VerifyColor(
			*this,
			TEXT("Math::CInterpTo should match the native FLinearColor interpolation baseline"),
			ScriptColorInterp,
			ExpectedColorInterp,
			0.001f);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
