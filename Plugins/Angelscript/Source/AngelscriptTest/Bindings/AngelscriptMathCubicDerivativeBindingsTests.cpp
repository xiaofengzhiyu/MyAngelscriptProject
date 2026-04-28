#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Math/Rotator.h"
#include "Math/UnrealMathUtility.h"
#include "Math/Vector.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathCubicDerivativeBindingsTests_Private
{
	template <typename TValue>
	bool ExecuteValueFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		const TCHAR* ContextLabel,
		TValue& OutValue)
	{
		FAngelscriptEngineScope EngineScope(Engine);
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

	bool VerifyVector(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FVector& Actual,
		const FVector& Expected,
		double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}

	bool VerifyRotator(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FRotator& Actual,
		const FRotator& Expected,
		double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
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

	bool VerifyRotator3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FRotator3f& Actual,
		const FRotator3f& Expected,
		float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathCubicDerivativeBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathCubicDerivativeBindingsTest,
	"Angelscript.TestModule.Bindings.MathCubicDerivativeCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathCubicDerivativeBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASMathCubicDerivativeCompat",
		TEXT(R"AS(
FVector GetVectorDerivative64()
{
	const FVector Point0 = FVector(-10.0, 2.0, 5.0);
	const FVector Tangent0 = FVector(3.0, -4.0, 1.0);
	const FVector Point1 = FVector(7.0, 9.0, -2.0);
	const FVector Tangent1 = FVector(-6.0, 1.0, 8.0);
	const float64 Alpha = 0.625;
	return Math::CubicInterpDerivative(Point0, Tangent0, Point1, Tangent1, Alpha);
}

FRotator GetRotatorDerivative64()
{
	const FRotator Point0 = FRotator(-20.0, 45.0, 5.0);
	const FRotator Tangent0 = FRotator(10.0, -15.0, 20.0);
	const FRotator Point1 = FRotator(80.0, -35.0, 25.0);
	const FRotator Tangent1 = FRotator(-30.0, 18.0, 40.0);
	const float64 Alpha = 0.625;
	return Math::CubicInterpDerivative(Point0, Tangent0, Point1, Tangent1, Alpha);
}

FVector GetVectorDerivative32()
{
	const FVector Point0 = FVector(-10.0, 2.0, 5.0);
	const FVector Tangent0 = FVector(3.0, -4.0, 1.0);
	const FVector Point1 = FVector(7.0, 9.0, -2.0);
	const FVector Tangent1 = FVector(-6.0, 1.0, 8.0);
	const float32 Alpha = 0.375f;
	return Math::CubicInterpDerivative(Point0, Tangent0, Point1, Tangent1, Alpha);
}

FRotator GetRotatorDerivative32()
{
	const FRotator Point0 = FRotator(-20.0, 45.0, 5.0);
	const FRotator Tangent0 = FRotator(10.0, -15.0, 20.0);
	const FRotator Point1 = FRotator(80.0, -35.0, 25.0);
	const FRotator Tangent1 = FRotator(-30.0, 18.0, 40.0);
	const float32 Alpha = 0.375f;
	return Math::CubicInterpDerivative(Point0, Tangent0, Point1, Tangent1, Alpha);
}

FVector3f GetVector3fDerivative32()
{
	const FVector3f Point0 = FVector3f(-4.0f, 6.0f, 2.0f);
	const FVector3f Tangent0 = FVector3f(1.5f, -2.5f, 0.5f);
	const FVector3f Point1 = FVector3f(9.0f, -3.0f, 7.5f);
	const FVector3f Tangent1 = FVector3f(-4.0f, 5.0f, -1.0f);
	const float32 Alpha = 0.375f;
	return Math::CubicInterpDerivative(Point0, Tangent0, Point1, Tangent1, Alpha);
}

FRotator3f GetRotator3fDerivative32()
{
	const FRotator3f Point0 = FRotator3f(-15.0f, 30.0f, 12.0f);
	const FRotator3f Tangent0 = FRotator3f(6.0f, -8.0f, 3.0f);
	const FRotator3f Point1 = FRotator3f(40.0f, 75.0f, -18.0f);
	const FRotator3f Tangent1 = FRotator3f(-10.0f, 4.0f, 9.0f);
	const float32 Alpha = 0.375f;
	return Math::CubicInterpDerivative(Point0, Tangent0, Point1, Tangent1, Alpha);
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* VectorDerivative64Function = GetFunctionByDecl(*this, *Module, TEXT("FVector GetVectorDerivative64()"));
	asIScriptFunction* RotatorDerivative64Function = GetFunctionByDecl(*this, *Module, TEXT("FRotator GetRotatorDerivative64()"));
	asIScriptFunction* VectorDerivative32Function = GetFunctionByDecl(*this, *Module, TEXT("FVector GetVectorDerivative32()"));
	asIScriptFunction* RotatorDerivative32Function = GetFunctionByDecl(*this, *Module, TEXT("FRotator GetRotatorDerivative32()"));
	asIScriptFunction* Vector3fDerivative32Function = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetVector3fDerivative32()"));
	asIScriptFunction* Rotator3fDerivative32Function = GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetRotator3fDerivative32()"));
	if (VectorDerivative64Function == nullptr
		|| RotatorDerivative64Function == nullptr
		|| VectorDerivative32Function == nullptr
		|| RotatorDerivative32Function == nullptr
		|| Vector3fDerivative32Function == nullptr
		|| Rotator3fDerivative32Function == nullptr)
	{
		return false;
	}

	FVector ScriptVectorDerivative64 = FVector::ZeroVector;
	FRotator ScriptRotatorDerivative64 = FRotator::ZeroRotator;
	FVector ScriptVectorDerivative32 = FVector::ZeroVector;
	FRotator ScriptRotatorDerivative32 = FRotator::ZeroRotator;
	FVector3f ScriptVector3fDerivative32 = FVector3f::ZeroVector;
	FRotator3f ScriptRotator3fDerivative32 = FRotator3f::ZeroRotator;

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *VectorDerivative64Function, TEXT("GetVectorDerivative64"), ScriptVectorDerivative64) &&
		ExecuteValueFunction(*this, Engine, *RotatorDerivative64Function, TEXT("GetRotatorDerivative64"), ScriptRotatorDerivative64) &&
		ExecuteValueFunction(*this, Engine, *VectorDerivative32Function, TEXT("GetVectorDerivative32"), ScriptVectorDerivative32) &&
		ExecuteValueFunction(*this, Engine, *RotatorDerivative32Function, TEXT("GetRotatorDerivative32"), ScriptRotatorDerivative32) &&
		ExecuteValueFunction(*this, Engine, *Vector3fDerivative32Function, TEXT("GetVector3fDerivative32"), ScriptVector3fDerivative32) &&
		ExecuteValueFunction(*this, Engine, *Rotator3fDerivative32Function, TEXT("GetRotator3fDerivative32"), ScriptRotator3fDerivative32);
	if (!bExecutedAll)
	{
		return false;
	}

	const FVector VectorPoint0(-10.0, 2.0, 5.0);
	const FVector VectorTangent0(3.0, -4.0, 1.0);
	const FVector VectorPoint1(7.0, 9.0, -2.0);
	const FVector VectorTangent1(-6.0, 1.0, 8.0);
	const FRotator RotatorPoint0(-20.0, 45.0, 5.0);
	const FRotator RotatorTangent0(10.0, -15.0, 20.0);
	const FRotator RotatorPoint1(80.0, -35.0, 25.0);
	const FRotator RotatorTangent1(-30.0, 18.0, 40.0);
	const FVector3f Vector3fPoint0(-4.0f, 6.0f, 2.0f);
	const FVector3f Vector3fTangent0(1.5f, -2.5f, 0.5f);
	const FVector3f Vector3fPoint1(9.0f, -3.0f, 7.5f);
	const FVector3f Vector3fTangent1(-4.0f, 5.0f, -1.0f);
	const FRotator3f Rotator3fPoint0(-15.0f, 30.0f, 12.0f);
	const FRotator3f Rotator3fTangent0(6.0f, -8.0f, 3.0f);
	const FRotator3f Rotator3fPoint1(40.0f, 75.0f, -18.0f);
	const FRotator3f Rotator3fTangent1(-10.0f, 4.0f, 9.0f);

	const FVector ExpectedVectorDerivative64 =
		FMath::CubicInterpDerivative<FVector, double>(VectorPoint0, VectorTangent0, VectorPoint1, VectorTangent1, 0.625);
	const FRotator ExpectedRotatorDerivative64 =
		FMath::CubicInterpDerivative<FRotator, double>(RotatorPoint0, RotatorTangent0, RotatorPoint1, RotatorTangent1, 0.625);
	const FVector ExpectedVectorDerivative32 =
		FMath::CubicInterpDerivative<FVector, float>(VectorPoint0, VectorTangent0, VectorPoint1, VectorTangent1, 0.375f);
	const FRotator ExpectedRotatorDerivative32 =
		FMath::CubicInterpDerivative<FRotator, float>(RotatorPoint0, RotatorTangent0, RotatorPoint1, RotatorTangent1, 0.375f);
	const FVector3f ExpectedVector3fDerivative32 =
		FMath::CubicInterpDerivative<FVector3f, float>(Vector3fPoint0, Vector3fTangent0, Vector3fPoint1, Vector3fTangent1, 0.375f);
	const FRotator3f ExpectedRotator3fDerivative32 =
		FMath::CubicInterpDerivative<FRotator3f, float>(Rotator3fPoint0, Rotator3fTangent0, Rotator3fPoint1, Rotator3fTangent1, 0.375f);

	bPassed &= VerifyVector(
		*this,
		TEXT("Math::CubicInterpDerivative FVector float64 overload should match the native baseline"),
		ScriptVectorDerivative64,
		ExpectedVectorDerivative64,
		0.000001);
	bPassed &= VerifyRotator(
		*this,
		TEXT("Math::CubicInterpDerivative FRotator float64 overload should match the native baseline"),
		ScriptRotatorDerivative64,
		ExpectedRotatorDerivative64,
		0.000001);
	bPassed &= VerifyVector(
		*this,
		TEXT("Math::CubicInterpDerivative FVector float32 overload should match the native baseline"),
		ScriptVectorDerivative32,
		ExpectedVectorDerivative32,
		0.0001);
	bPassed &= VerifyRotator(
		*this,
		TEXT("Math::CubicInterpDerivative FRotator float32 overload should match the native baseline"),
		ScriptRotatorDerivative32,
		ExpectedRotatorDerivative32,
		0.0001);
	bPassed &= VerifyVector3f(
		*this,
		TEXT("Math::CubicInterpDerivative FVector3f float32 overload should match the native baseline"),
		ScriptVector3fDerivative32,
		ExpectedVector3fDerivative32,
		0.0001f);
	bPassed &= VerifyRotator3f(
		*this,
		TEXT("Math::CubicInterpDerivative FRotator3f float32 overload should match the native baseline"),
		ScriptRotator3fDerivative32,
		ExpectedRotator3fDerivative32,
		0.0001f);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
