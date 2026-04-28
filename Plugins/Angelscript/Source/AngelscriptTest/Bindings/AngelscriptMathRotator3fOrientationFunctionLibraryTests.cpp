#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Math/Quat.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathRotator3fOrientationFunctionLibraryTests_Private
{
	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, float& OutValue)
	{
		OutValue = Context.GetReturnFloat();
		return true;
	}

	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, double& OutValue)
	{
		OutValue = Context.GetReturnDouble();
		return true;
	}

	template <typename TValue>
	bool ReadReturnValue(FAutomationTestBase& Test, asIScriptContext& Context, TValue& OutValue)
	{
		void* ReturnValueAddress = Context.GetAddressOfReturnValue();
		if (!Test.TestNotNull(TEXT("Math rotator3f orientation test should expose the return value storage"), ReturnValueAddress))
		{
			return false;
		}

		OutValue = *static_cast<TValue*>(ReturnValueAddress);
		return true;
	}

	template <typename TValue>
	bool ExecuteValueFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		TValue& OutValue)
	{
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(TEXT("Math rotator3f orientation test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Math rotator3f orientation test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Math rotator3f orientation test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Math rotator3f orientation test saw a script exception: %s"),
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			Context->Release();
			return false;
		}

		const bool bReadReturnValue = ReadReturnValue(Test, *Context, OutValue);
		Context->Release();
		return bReadReturnValue;
	}

	bool RotatorMatches(const FRotator& Actual, const FRotator& Expected, double ToleranceDegrees = 0.05)
	{
		FQuat ActualQuat(Actual);
		FQuat ExpectedQuat(Expected);
		ActualQuat.Normalize();
		ExpectedQuat.Normalize();
		return FMath::RadiansToDegrees(ActualQuat.AngularDistance(ExpectedQuat)) <= ToleranceDegrees;
	}

	bool Rotator3fMatches(const FRotator3f& Actual, const FRotator3f& Expected, float ToleranceDegrees = 0.05f)
	{
		FQuat4f ActualQuat(Actual);
		FQuat4f ExpectedQuat(Expected);
		ActualQuat.Normalize();
		ExpectedQuat.Normalize();
		return FMath::RadiansToDegrees(ActualQuat.AngularDistance(ExpectedQuat)) <= ToleranceDegrees;
	}

	bool VerifyRotator(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FRotator& Actual,
		const FRotator& Expected,
		double ToleranceDegrees = 0.05)
	{
		return Test.TestTrue(What, RotatorMatches(Actual, Expected, ToleranceDegrees));
	}

	bool VerifyRotator3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FRotator3f& Actual,
		const FRotator3f& Expected,
		float ToleranceDegrees = 0.05f)
	{
		return Test.TestTrue(What, Rotator3fMatches(Actual, Expected, ToleranceDegrees));
	}

	bool VerifyVector3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FVector3f& Actual,
		const FVector3f& Expected,
		float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathRotator3fOrientationFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathRotator3fOrientationFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.MathRotator3fOrientationAndAngles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathRotator3fOrientationFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathRotator3fOrientationAndAngles"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASMathRotator3fOrientationAndAngles",
		TEXT(R"AS(
double GetRotatorAngularDistance()
{
	return FRotator::AngularDistance(FRotator(10.0f, 20.0f, 30.0f), FRotator(-20.0f, 70.0f, 10.0f));
}

FRotator3f GetAxesRotator3f()
{
	return FRotator3f::MakeFromAxes(FVector3f(1.0f, 0.0f, 0.0f), FVector3f(0.0f, 1.0f, 0.0f), FVector3f(0.0f, 0.0f, 1.0f));
}

FVector3f GetAxesForward3f()
{
	return FRotator3f::GetForwardVector(GetAxesRotator3f());
}

FVector3f GetAxesRight3f()
{
	return FRotator3f::GetRightVector(GetAxesRotator3f());
}

FVector3f GetAxesUp3f()
{
	return FRotator3f::GetUpVector(GetAxesRotator3f());
}

FRotator3f GetComposedRotator3f()
{
	const FRotator3f A = FRotator3f(0.0f, 90.0f, 0.0f);
	const FRotator3f B = FRotator3f(45.0f, 0.0f, 0.0f);
	return FRotator3f::Compose(A, B);
}

float32 GetRotator3fAngularDistance()
{
	return FRotator3f::AngularDistance(FRotator3f(10.0f, 20.0f, 30.0f), FRotator3f(-20.0f, 70.0f, 10.0f));
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	double ScriptRotatorAngularDistance = 0.0;
	FRotator3f ScriptAxesRotator3f(ForceInitToZero);
	FVector3f ScriptAxesForward3f(ForceInitToZero);
	FVector3f ScriptAxesRight3f(ForceInitToZero);
	FVector3f ScriptAxesUp3f(ForceInitToZero);
	FRotator3f ScriptComposedRotator3f(ForceInitToZero);
	float ScriptRotator3fAngularDistance = 0.0f;

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("double GetRotatorAngularDistance()")), ScriptRotatorAngularDistance) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetAxesRotator3f()")), ScriptAxesRotator3f) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetAxesForward3f()")), ScriptAxesForward3f) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetAxesRight3f()")), ScriptAxesRight3f) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetAxesUp3f()")), ScriptAxesUp3f) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetComposedRotator3f()")), ScriptComposedRotator3f) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("float32 GetRotator3fAngularDistance()")), ScriptRotator3fAngularDistance);
	if (!bExecutedAll)
	{
		return false;
	}

	const FVector3f CanonicalForward(1.0f, 0.0f, 0.0f);
	const FVector3f CanonicalRight(0.0f, 1.0f, 0.0f);
	const FVector3f CanonicalUp(0.0f, 0.0f, 1.0f);
	const FRotator RotatorA(10.0f, 20.0f, 30.0f);
	const FRotator RotatorB(-20.0f, 70.0f, 10.0f);
	const FRotator3f Rotator3fA(10.0f, 20.0f, 30.0f);
	const FRotator3f Rotator3fB(-20.0f, 70.0f, 10.0f);
	const FRotator3f ComposeA(0.0f, 90.0f, 0.0f);
	const FRotator3f ComposeB(45.0f, 0.0f, 0.0f);

	const double ExpectedRotatorAngularDistance = FMath::RadiansToDegrees(RotatorA.Quaternion().AngularDistance(RotatorB.Quaternion()));
	const FRotator3f ExpectedAxesRotator3f = FMatrix44f(
		CanonicalForward.GetSafeNormal(),
		CanonicalRight.GetSafeNormal(),
		CanonicalUp.GetSafeNormal(),
		FVector3f::ZeroVector).Rotator();
	const FRotator3f ExpectedComposedRotator3f = FRotator3f(FQuat4f(ComposeB) * FQuat4f(ComposeA));
	const float ExpectedRotator3fAngularDistance = FMath::RadiansToDegrees(FQuat4f(Rotator3fA).AngularDistance(FQuat4f(Rotator3fB)));

	bPassed =
		TestTrue(
			TEXT("FRotator::AngularDistance should match the native quaternion angular distance"),
			FMath::IsNearlyEqual(ScriptRotatorAngularDistance, ExpectedRotatorAngularDistance, KINDA_SMALL_NUMBER)) &&
		VerifyRotator3f(
			*this,
			TEXT("FRotator3f::MakeFromAxes should build the same orientation as the native matrix conversion"),
			ScriptAxesRotator3f,
			ExpectedAxesRotator3f) &&
		VerifyVector3f(
			*this,
			TEXT("FRotator3f::GetForwardVector should recover the canonical forward axis from MakeFromAxes"),
			ScriptAxesForward3f,
			CanonicalForward) &&
		VerifyVector3f(
			*this,
			TEXT("FRotator3f::GetRightVector should recover the canonical right axis from MakeFromAxes"),
			ScriptAxesRight3f,
			CanonicalRight) &&
		VerifyVector3f(
			*this,
			TEXT("FRotator3f::GetUpVector should recover the canonical up axis from MakeFromAxes"),
			ScriptAxesUp3f,
			CanonicalUp) &&
		VerifyRotator3f(
			*this,
			TEXT("FRotator3f::Compose should match the native quaternion composition"),
			ScriptComposedRotator3f,
			ExpectedComposedRotator3f) &&
		TestTrue(
			TEXT("FRotator3f::AngularDistance should match the native quaternion angular distance"),
			FMath::IsNearlyEqual(ScriptRotator3fAngularDistance, ExpectedRotator3fAngularDistance, KINDA_SMALL_NUMBER));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
