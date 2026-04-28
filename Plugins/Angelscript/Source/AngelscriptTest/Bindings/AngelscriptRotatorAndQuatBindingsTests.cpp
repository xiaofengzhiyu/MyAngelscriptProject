#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Math/Quat.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptRotatorBindingsTest,
	"Angelscript.TestModule.Bindings.RotatorAndQuat.RotatorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptQuat4fBindingsTest,
	"Angelscript.TestModule.Bindings.RotatorAndQuat.Quat4fCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptRotatorAndQuatBindingsTests_Private
{
	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, float& OutValue) { OutValue = Context.GetReturnFloat(); return true; }
	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, double& OutValue) { OutValue = Context.GetReturnDouble(); return true; }

	template <typename TValue>
	bool ReadReturnValue(FAutomationTestBase& Test, asIScriptContext& Context, TValue& OutValue)
	{
		void* ReturnValueAddress = Context.GetAddressOfReturnValue();
		if (!Test.TestNotNull(TEXT("Rotator/Quat bindings test should expose the return value storage"), ReturnValueAddress))
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
		if (!Test.TestNotNull(TEXT("Rotator/Quat bindings test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Rotator/Quat bindings test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Rotator/Quat bindings test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Rotator/Quat bindings test saw a script exception: %s"),
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			Context->Release();
			return false;
		}

		const bool bReadReturnValue = ReadReturnValue(Test, *Context, OutValue);
		Context->Release();
		return bReadReturnValue;
	}

	bool QuatMatches(const FQuat& Actual, const FQuat& Expected, double ToleranceDegrees = 0.05)
	{
		FQuat ActualQuat = Actual;
		FQuat ExpectedQuat = Expected;
		ActualQuat.Normalize();
		ExpectedQuat.Normalize();
		if ((ActualQuat | ExpectedQuat) < 0.0)
		{
			ExpectedQuat = FQuat(-ExpectedQuat.X, -ExpectedQuat.Y, -ExpectedQuat.Z, -ExpectedQuat.W);
		}

		return FMath::RadiansToDegrees(ActualQuat.AngularDistance(ExpectedQuat)) <= ToleranceDegrees;
	}

	bool VerifyVector(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FVector& Actual,
		const FVector& Expected,
		double Tolerance = KINDA_SMALL_NUMBER)
	{ return Test.TestTrue(What, Actual.Equals(Expected, Tolerance)); }

	bool VerifyVector3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FVector3f& Actual,
		const FVector3f& Expected,
		float Tolerance = KINDA_SMALL_NUMBER)
	{ return Test.TestTrue(What, Actual.Equals(Expected, Tolerance)); }

	bool VerifyRotator(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FRotator& Actual,
		const FRotator& Expected,
		double ToleranceDegrees = 0.05)
	{ return Test.TestTrue(What, QuatMatches(FQuat(Actual), FQuat(Expected), ToleranceDegrees)); }

	bool VerifyRotator3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FRotator3f& Actual,
		const FRotator3f& Expected,
		double ToleranceDegrees = 0.05)
	{ return Test.TestTrue(What, QuatMatches(FQuat(Actual.Quaternion()), FQuat(Expected.Quaternion()), ToleranceDegrees)); }

	bool VerifyQuat4f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FQuat4f& Actual,
		const FQuat4f& Expected,
		double ToleranceDegrees = 0.05)
	{ return Test.TestTrue(What, QuatMatches(FQuat(Actual), FQuat(Expected), ToleranceDegrees)); }

	bool VerifyNumeric(
		FAutomationTestBase& Test,
		const TCHAR* What,
		double Actual,
		double Expected,
		double Tolerance)
	{ return Test.TestTrue(What, FMath::Abs(Actual - Expected) <= Tolerance); }
}

using namespace AngelscriptTest_Bindings_AngelscriptRotatorAndQuatBindingsTests_Private;

bool FAngelscriptRotatorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASRotatorVariantCompat",
		TEXT(R"AS(
FRotator GetZeroRotator()
{
	return FRotator::ZeroRotator;
}

FRotator GetEulerRotator()
{
	return FRotator::MakeFromEuler(FVector(10.0, 20.0, 30.0));
}

FVector GetRotatedForward()
{
	const FRotator Value = FRotator(5.0, 90.0, 0.0);
	return Value.RotateVector(FVector(1.0, 0.0, 0.0));
}

FVector GetUnrotatedForward()
{
	const FRotator Value = FRotator(5.0, 90.0, 0.0);
	return Value.UnrotateVector(Value.RotateVector(FVector(1.0, 0.0, 0.0)));
}

FVector GetFacingVector()
{
	return FRotator(5.0, 90.0, 0.0).Vector();
}

FRotator GetInverseRotator()
{
	return FRotator(5.0, 90.0, 0.0).GetInverse();
}

FRotator GetNormalizedRotator()
{
	return FRotator(370.0, -725.0, 45.0).GetNormalized();
}

FRotator GetRoundTripRotator()
{
	return FRotator(FRotator(10.0, 20.0, 30.0).Quaternion());
}

float64 GetNormalizedAxis()
{
	return FRotator::NormalizeAxis(540.0);
}

float64 GetClampedAxis()
{
	return FRotator::ClampAxis(-90.0);
}

FRotator3f GetZeroRotator3f()
{
	return FRotator3f::ZeroRotator;
}

FRotator3f GetEulerRotator3f()
{
	return FRotator3f::MakeFromEuler(FVector3f(10.0, 20.0, 30.0));
}

FVector3f GetRotatedForward3f()
{
	const FRotator3f Value = FRotator3f(5.0, 90.0, 0.0);
	return Value.RotateVector(FVector3f(1.0, 0.0, 0.0));
}

FVector3f GetUnrotatedForward3f()
{
	const FRotator3f Value = FRotator3f(5.0, 90.0, 0.0);
	return Value.UnrotateVector(Value.RotateVector(FVector3f(1.0, 0.0, 0.0)));
}

FVector3f GetFacingVector3f()
{
	return FRotator3f(5.0, 90.0, 0.0).Vector();
}

FRotator3f GetRoundTripRotator3f()
{
	return FRotator3f(FQuat4f(FRotator3f(10.0, 20.0, 30.0)));
}

FRotator3f GetRotator3fFromRotator()
{
	return FRotator3f(GetRoundTripRotator());
}

float32 GetNormalizedAxis3f()
{
	return FRotator3f::NormalizeAxis(540.0);
}

float32 GetClampedAxis3f()
{
	return FRotator3f::ClampAxis(-90.0);
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	const FRotator BaseRotator(5.0, 90.0, 0.0);
	const FVector ForwardVector(1.0, 0.0, 0.0);
	const FRotator EulerRotator = FRotator::MakeFromEuler(FVector(10.0, 20.0, 30.0));
	const FVector ExpectedRotatedForward = BaseRotator.RotateVector(ForwardVector);
	const FVector ExpectedUnrotatedForward = BaseRotator.UnrotateVector(ExpectedRotatedForward);
	const FVector ExpectedFacingVector = BaseRotator.Vector();
	const FRotator ExpectedInverseRotator = BaseRotator.GetInverse();
	const FRotator ExpectedNormalizedRotator = FRotator(370.0, -725.0, 45.0).GetNormalized();
	const FRotator ExpectedRoundTripRotator = FRotator(FRotator(10.0, 20.0, 30.0).Quaternion());
	const double ExpectedNormalizedAxis = FRotator::NormalizeAxis(540.0);
	const double ExpectedClampedAxis = FRotator::ClampAxis(-90.0);

	const FRotator3f BaseRotator3f(5.0f, 90.0f, 0.0f);
	const FVector3f ForwardVector3f(1.0f, 0.0f, 0.0f);
	const FRotator3f EulerRotator3f = FRotator3f::MakeFromEuler(FVector3f(10.0f, 20.0f, 30.0f));
	const FVector3f ExpectedRotatedForward3f = BaseRotator3f.RotateVector(ForwardVector3f);
	const FVector3f ExpectedUnrotatedForward3f = BaseRotator3f.UnrotateVector(ExpectedRotatedForward3f);
	const FVector3f ExpectedFacingVector3f = BaseRotator3f.Vector();
	const FRotator3f ExpectedRoundTripRotator3f = FRotator3f(FQuat4f(FRotator3f(10.0f, 20.0f, 30.0f)));
	const FRotator3f ExpectedRotator3fFromRotator = FRotator3f(ExpectedRoundTripRotator);
	const float ExpectedNormalizedAxis3f = FRotator3f::NormalizeAxis(540.0f);
	const float ExpectedClampedAxis3f = FRotator3f::ClampAxis(-90.0f);

	FRotator ScriptZeroRotator;
	FRotator ScriptEulerRotator;
	FVector ScriptRotatedForward;
	FVector ScriptUnrotatedForward;
	FVector ScriptFacingVector;
	FRotator ScriptInverseRotator;
	FRotator ScriptNormalizedRotator;
	FRotator ScriptRoundTripRotator;
	double ScriptNormalizedAxis = 0.0;
	double ScriptClampedAxis = 0.0;
	FRotator3f ScriptZeroRotator3f;
	FRotator3f ScriptEulerRotator3f;
	FVector3f ScriptRotatedForward3f;
	FVector3f ScriptUnrotatedForward3f;
	FVector3f ScriptFacingVector3f;
	FRotator3f ScriptRoundTripRotator3f;
	FRotator3f ScriptRotator3fFromRotator;
	float ScriptNormalizedAxis3f = 0.0f;
	float ScriptClampedAxis3f = 0.0f;

	asIScriptFunction* ZeroRotatorFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator GetZeroRotator()"));
	asIScriptFunction* EulerRotatorFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator GetEulerRotator()"));
	asIScriptFunction* RotatedForwardFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetRotatedForward()"));
	asIScriptFunction* UnrotatedForwardFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetUnrotatedForward()"));
	asIScriptFunction* FacingVectorFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetFacingVector()"));
	asIScriptFunction* InverseRotatorFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator GetInverseRotator()"));
	asIScriptFunction* NormalizedRotatorFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator GetNormalizedRotator()"));
	asIScriptFunction* RoundTripRotatorFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator GetRoundTripRotator()"));
	asIScriptFunction* NormalizedAxisFunction = GetFunctionByDecl(*this, *Module, TEXT("float64 GetNormalizedAxis()"));
	asIScriptFunction* ClampedAxisFunction = GetFunctionByDecl(*this, *Module, TEXT("float64 GetClampedAxis()"));
	asIScriptFunction* ZeroRotator3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetZeroRotator3f()"));
	asIScriptFunction* EulerRotator3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetEulerRotator3f()"));
	asIScriptFunction* RotatedForward3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetRotatedForward3f()"));
	asIScriptFunction* UnrotatedForward3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetUnrotatedForward3f()"));
	asIScriptFunction* FacingVector3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetFacingVector3f()"));
	asIScriptFunction* RoundTripRotator3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetRoundTripRotator3f()"));
	asIScriptFunction* Rotator3fFromRotatorFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetRotator3fFromRotator()"));
	asIScriptFunction* NormalizedAxis3fFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetNormalizedAxis3f()"));
	asIScriptFunction* ClampedAxis3fFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetClampedAxis3f()"));
	if (ZeroRotatorFunction == nullptr
		|| EulerRotatorFunction == nullptr
		|| RotatedForwardFunction == nullptr
		|| UnrotatedForwardFunction == nullptr
		|| FacingVectorFunction == nullptr
		|| InverseRotatorFunction == nullptr
		|| NormalizedRotatorFunction == nullptr
		|| RoundTripRotatorFunction == nullptr
		|| NormalizedAxisFunction == nullptr
		|| ClampedAxisFunction == nullptr
		|| ZeroRotator3fFunction == nullptr
		|| EulerRotator3fFunction == nullptr
		|| RotatedForward3fFunction == nullptr
		|| UnrotatedForward3fFunction == nullptr
		|| FacingVector3fFunction == nullptr
		|| RoundTripRotator3fFunction == nullptr
		|| Rotator3fFromRotatorFunction == nullptr
		|| NormalizedAxis3fFunction == nullptr
		|| ClampedAxis3fFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *ZeroRotatorFunction, ScriptZeroRotator) &&
		ExecuteValueFunction(*this, Engine, *EulerRotatorFunction, ScriptEulerRotator) &&
		ExecuteValueFunction(*this, Engine, *RotatedForwardFunction, ScriptRotatedForward) &&
		ExecuteValueFunction(*this, Engine, *UnrotatedForwardFunction, ScriptUnrotatedForward) &&
		ExecuteValueFunction(*this, Engine, *FacingVectorFunction, ScriptFacingVector) &&
		ExecuteValueFunction(*this, Engine, *InverseRotatorFunction, ScriptInverseRotator) &&
		ExecuteValueFunction(*this, Engine, *NormalizedRotatorFunction, ScriptNormalizedRotator) &&
		ExecuteValueFunction(*this, Engine, *RoundTripRotatorFunction, ScriptRoundTripRotator) &&
		ExecuteValueFunction(*this, Engine, *NormalizedAxisFunction, ScriptNormalizedAxis) &&
		ExecuteValueFunction(*this, Engine, *ClampedAxisFunction, ScriptClampedAxis) &&
		ExecuteValueFunction(*this, Engine, *ZeroRotator3fFunction, ScriptZeroRotator3f) &&
		ExecuteValueFunction(*this, Engine, *EulerRotator3fFunction, ScriptEulerRotator3f) &&
		ExecuteValueFunction(*this, Engine, *RotatedForward3fFunction, ScriptRotatedForward3f) &&
		ExecuteValueFunction(*this, Engine, *UnrotatedForward3fFunction, ScriptUnrotatedForward3f) &&
		ExecuteValueFunction(*this, Engine, *FacingVector3fFunction, ScriptFacingVector3f) &&
		ExecuteValueFunction(*this, Engine, *RoundTripRotator3fFunction, ScriptRoundTripRotator3f) &&
		ExecuteValueFunction(*this, Engine, *Rotator3fFromRotatorFunction, ScriptRotator3fFromRotator) &&
		ExecuteValueFunction(*this, Engine, *NormalizedAxis3fFunction, ScriptNormalizedAxis3f) &&
		ExecuteValueFunction(*this, Engine, *ClampedAxis3fFunction, ScriptClampedAxis3f);
	if (!bExecutedAll)
	{
		return false;
	}

	bPassed =
		VerifyRotator(*this, TEXT("FRotator::ZeroRotator should round-trip through the static binding"), ScriptZeroRotator, FRotator::ZeroRotator) &&
		VerifyRotator(*this, TEXT("FRotator::MakeFromEuler should match the native Euler conversion"), ScriptEulerRotator, EulerRotator) &&
		VerifyVector(*this, TEXT("FRotator::RotateVector should match the native rotated forward vector"), ScriptRotatedForward, ExpectedRotatedForward) &&
		VerifyVector(*this, TEXT("FRotator::UnrotateVector should recover the original forward vector"), ScriptUnrotatedForward, ExpectedUnrotatedForward) &&
		VerifyVector(*this, TEXT("FRotator::Vector should report the same facing vector as the native rotator"), ScriptFacingVector, ExpectedFacingVector) &&
		VerifyRotator(*this, TEXT("FRotator::GetInverse should match the native inverse rotator"), ScriptInverseRotator, ExpectedInverseRotator) &&
		VerifyRotator(*this, TEXT("FRotator::GetNormalized should match the native normalized rotator"), ScriptNormalizedRotator, ExpectedNormalizedRotator) &&
		VerifyRotator(*this, TEXT("FRotator(FQuat) should preserve the native quaternion round-trip"), ScriptRoundTripRotator, ExpectedRoundTripRotator) &&
		VerifyNumeric(*this, TEXT("FRotator::NormalizeAxis should match the native wrapped angle"), ScriptNormalizedAxis, ExpectedNormalizedAxis, KINDA_SMALL_NUMBER) &&
		VerifyNumeric(*this, TEXT("FRotator::ClampAxis should match the native clamped angle"), ScriptClampedAxis, ExpectedClampedAxis, KINDA_SMALL_NUMBER) &&
		VerifyRotator3f(*this, TEXT("FRotator3f::ZeroRotator should round-trip through the static binding"), ScriptZeroRotator3f, FRotator3f::ZeroRotator) &&
		VerifyRotator3f(*this, TEXT("FRotator3f::MakeFromEuler should match the native float Euler conversion"), ScriptEulerRotator3f, EulerRotator3f) &&
		VerifyVector3f(*this, TEXT("FRotator3f::RotateVector should match the native float rotated forward vector"), ScriptRotatedForward3f, ExpectedRotatedForward3f) &&
		VerifyVector3f(*this, TEXT("FRotator3f::UnrotateVector should recover the original float forward vector"), ScriptUnrotatedForward3f, ExpectedUnrotatedForward3f) &&
		VerifyVector3f(*this, TEXT("FRotator3f::Vector should report the same float facing vector as the native rotator"), ScriptFacingVector3f, ExpectedFacingVector3f) &&
		VerifyRotator3f(*this, TEXT("FRotator3f(FQuat4f) should preserve the native float quaternion round-trip"), ScriptRoundTripRotator3f, ExpectedRoundTripRotator3f) &&
		VerifyRotator3f(*this, TEXT("FRotator3f(FRotator) should preserve the native double-to-float conversion"), ScriptRotator3fFromRotator, ExpectedRotator3fFromRotator) &&
		VerifyNumeric(*this, TEXT("FRotator3f::NormalizeAxis should match the native float wrapped angle"), ScriptNormalizedAxis3f, ExpectedNormalizedAxis3f, KINDA_SMALL_NUMBER) &&
		VerifyNumeric(*this, TEXT("FRotator3f::ClampAxis should match the native float clamped angle"), ScriptClampedAxis3f, ExpectedClampedAxis3f, KINDA_SMALL_NUMBER);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptQuat4fBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASQuat4fCompat",
		TEXT(R"AS(
FQuat4f GetIdentityQuat4f()
{
	return FQuat4f::Identity;
}

FQuat4f GetNormalizedQuarterTurnQuat4f()
{
	FQuat4f Value = FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637) * 3.0;
	Value.Normalize();
	return Value;
}

FVector3f GetForwardVector()
{
	return FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637).GetForwardVector();
}

FVector3f GetRightVector()
{
	return FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637).GetRightVector();
}

FVector3f GetUpVector()
{
	return FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637).GetUpVector();
}

FVector3f GetRotatedForward()
{
	const FQuat4f QuarterTurn = FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637);
	return QuarterTurn.RotateVector(FVector3f(1.0, 0.0, 0.0));
}

FVector3f GetUnrotatedForward()
{
	const FQuat4f QuarterTurn = FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637);
	return QuarterTurn.UnrotateVector(QuarterTurn.RotateVector(FVector3f(1.0, 0.0, 0.0)));
}

FQuat4f GetInverseQuarterTurn()
{
	return FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637).Inverse();
}

FRotator3f GetQuarterTurnRotator()
{
	return FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637).Rotator();
}

FQuat4f GetMakeFromEulerQuat4f()
{
	return FQuat4f::MakeFromEuler(FVector3f(10.0, 20.0, 30.0));
}

FQuat4f GetHalfSlerpQuat4f()
{
	return FQuat4f::Slerp(FQuat4f::Identity, FQuat4f(FVector3f(0.0, 0.0, 1.0), 1.57079637), 0.5);
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	const FQuat4f QuarterTurn(FVector3f(0.0f, 0.0f, 1.0f), UE_HALF_PI);
	FQuat4f ExpectedNormalizedQuarterTurn = QuarterTurn * 3.0f;
	ExpectedNormalizedQuarterTurn.Normalize();
	const FVector3f ExpectedForwardVector = QuarterTurn.GetForwardVector();
	const FVector3f ExpectedRightVector = QuarterTurn.GetRightVector();
	const FVector3f ExpectedUpVector = QuarterTurn.GetUpVector();
	const FVector3f ExpectedRotatedForward = QuarterTurn.RotateVector(FVector3f(1.0f, 0.0f, 0.0f));
	const FVector3f ExpectedUnrotatedForward = QuarterTurn.UnrotateVector(ExpectedRotatedForward);
	const FQuat4f ExpectedInverseQuarterTurn = QuarterTurn.Inverse();
	const FRotator3f ExpectedQuarterTurnRotator = QuarterTurn.Rotator();
	const FQuat4f ExpectedMakeFromEuler = FQuat4f::MakeFromEuler(FVector3f(10.0f, 20.0f, 30.0f));
	FQuat4f ExpectedHalfSlerp = FQuat4f::Slerp(FQuat4f::Identity, QuarterTurn, 0.5f);
	ExpectedHalfSlerp.Normalize();

	FQuat4f ScriptIdentityQuat4f;
	FQuat4f ScriptNormalizedQuarterTurn;
	FVector3f ScriptForwardVector;
	FVector3f ScriptRightVector;
	FVector3f ScriptUpVector;
	FVector3f ScriptRotatedForward;
	FVector3f ScriptUnrotatedForward;
	FQuat4f ScriptInverseQuarterTurn;
	FRotator3f ScriptQuarterTurnRotator;
	FQuat4f ScriptMakeFromEuler;
	FQuat4f ScriptHalfSlerp;

	asIScriptFunction* IdentityQuat4fFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetIdentityQuat4f()"));
	asIScriptFunction* NormalizedQuarterTurnQuat4fFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetNormalizedQuarterTurnQuat4f()"));
	asIScriptFunction* ForwardVectorFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetForwardVector()"));
	asIScriptFunction* RightVectorFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetRightVector()"));
	asIScriptFunction* UpVectorFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetUpVector()"));
	asIScriptFunction* RotatedForwardFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetRotatedForward()"));
	asIScriptFunction* UnrotatedForwardFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetUnrotatedForward()"));
	asIScriptFunction* InverseQuarterTurnFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetInverseQuarterTurn()"));
	asIScriptFunction* QuarterTurnRotatorFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetQuarterTurnRotator()"));
	asIScriptFunction* MakeFromEulerQuat4fFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetMakeFromEulerQuat4f()"));
	asIScriptFunction* HalfSlerpQuat4fFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetHalfSlerpQuat4f()"));
	if (IdentityQuat4fFunction == nullptr
		|| NormalizedQuarterTurnQuat4fFunction == nullptr
		|| ForwardVectorFunction == nullptr
		|| RightVectorFunction == nullptr
		|| UpVectorFunction == nullptr
		|| RotatedForwardFunction == nullptr
		|| UnrotatedForwardFunction == nullptr
		|| InverseQuarterTurnFunction == nullptr
		|| QuarterTurnRotatorFunction == nullptr
		|| MakeFromEulerQuat4fFunction == nullptr
		|| HalfSlerpQuat4fFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *IdentityQuat4fFunction, ScriptIdentityQuat4f) &&
		ExecuteValueFunction(*this, Engine, *NormalizedQuarterTurnQuat4fFunction, ScriptNormalizedQuarterTurn) &&
		ExecuteValueFunction(*this, Engine, *ForwardVectorFunction, ScriptForwardVector) &&
		ExecuteValueFunction(*this, Engine, *RightVectorFunction, ScriptRightVector) &&
		ExecuteValueFunction(*this, Engine, *UpVectorFunction, ScriptUpVector) &&
		ExecuteValueFunction(*this, Engine, *RotatedForwardFunction, ScriptRotatedForward) &&
		ExecuteValueFunction(*this, Engine, *UnrotatedForwardFunction, ScriptUnrotatedForward) &&
		ExecuteValueFunction(*this, Engine, *InverseQuarterTurnFunction, ScriptInverseQuarterTurn) &&
		ExecuteValueFunction(*this, Engine, *QuarterTurnRotatorFunction, ScriptQuarterTurnRotator) &&
		ExecuteValueFunction(*this, Engine, *MakeFromEulerQuat4fFunction, ScriptMakeFromEuler) &&
		ExecuteValueFunction(*this, Engine, *HalfSlerpQuat4fFunction, ScriptHalfSlerp);
	if (!bExecutedAll)
	{
		return false;
	}

	bPassed =
		VerifyQuat4f(*this, TEXT("FQuat4f::Identity should round-trip through the static binding"), ScriptIdentityQuat4f, FQuat4f::Identity) &&
		VerifyQuat4f(*this, TEXT("FQuat4f::Normalize should recover the original quarter-turn rotation"), ScriptNormalizedQuarterTurn, QuarterTurn) &&
		TestTrue(TEXT("FQuat4f::Normalize should leave the quaternion normalized"), ScriptNormalizedQuarterTurn.IsNormalized()) &&
		VerifyVector3f(*this, TEXT("FQuat4f::GetForwardVector should match the native forward axis"), ScriptForwardVector, ExpectedForwardVector) &&
		VerifyVector3f(*this, TEXT("FQuat4f::GetRightVector should match the native right axis"), ScriptRightVector, ExpectedRightVector) &&
		VerifyVector3f(*this, TEXT("FQuat4f::GetUpVector should match the native up axis"), ScriptUpVector, ExpectedUpVector) &&
		VerifyVector3f(*this, TEXT("FQuat4f::RotateVector should match the native rotated forward vector"), ScriptRotatedForward, ExpectedRotatedForward) &&
		VerifyVector3f(*this, TEXT("FQuat4f::UnrotateVector should recover the original forward vector"), ScriptUnrotatedForward, ExpectedUnrotatedForward) &&
		VerifyQuat4f(*this, TEXT("FQuat4f::Inverse should match the native inverse quaternion"), ScriptInverseQuarterTurn, ExpectedInverseQuarterTurn) &&
		VerifyRotator3f(*this, TEXT("FQuat4f::Rotator should match the native float axis-angle conversion"), ScriptQuarterTurnRotator, ExpectedQuarterTurnRotator) &&
		VerifyQuat4f(*this, TEXT("FQuat4f::MakeFromEuler should match the native float Euler conversion"), ScriptMakeFromEuler, ExpectedMakeFromEuler) &&
		VerifyQuat4f(*this, TEXT("FQuat4f::Slerp should match the native half-way interpolation result"), ScriptHalfSlerp, ExpectedHalfSlerp) &&
		TestTrue(TEXT("FQuat4f::Slerp should return a normalized quaternion for this deterministic case"), ScriptHalfSlerp.IsNormalized());

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
