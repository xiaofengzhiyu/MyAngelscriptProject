#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathVectorConstraintFunctionLibraryTests_Private
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
		if (!Test.TestNotNull(TEXT("Math vector constraint test should expose the return value storage"), ReturnValueAddress))
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
		if (!Test.TestNotNull(TEXT("Math vector constraint test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Math vector constraint test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Math vector constraint test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Math vector constraint test saw a script exception: %s"),
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			Context->Release();
			return false;
		}

		const bool bReadReturnValue = ReadReturnValue(Test, *Context, OutValue);
		Context->Release();
		return bReadReturnValue;
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

using namespace AngelscriptTest_Bindings_AngelscriptMathVectorConstraintFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathVectorConstraintFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.MathVectorConstraintsAndAngles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathVectorConstraintFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathVectorConstraintsAndAngles"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASMathVectorConstraintsAndAngles",
		TEXT(R"AS(
double GetVectorAngularDistance() { return AngelscriptFVectorMixin::AngularDistance(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f)); }
double GetVectorAngularDistanceForNormals() { return AngelscriptFVectorMixin::AngularDistanceForNormals(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f)); }
FVector GetVectorConstrainedToPlane() { return AngelscriptFVectorMixin::ConstrainToPlane(FVector(3.0f, 4.0f, 12.0f), FVector(0.0f, 0.0f, 1.0f)); }
FVector GetVectorConstrainedToDirection() { return AngelscriptFVectorMixin::ConstrainToDirection(FVector(3.0f, 4.0f, 12.0f), FVector(0.0f, 0.0f, 1.0f)); }

float32 GetVector3fAngularDistance() { return AngelscriptFVector3fMixin::AngularDistance(FVector3f(1.0f, 0.0f, 0.0f), FVector3f(0.0f, 1.0f, 0.0f)); }
float32 GetVector3fAngularDistanceForNormals() { return AngelscriptFVector3fMixin::AngularDistanceForNormals(FVector3f(1.0f, 0.0f, 0.0f), FVector3f(0.0f, 1.0f, 0.0f)); }
FVector3f GetVector3fConstrainedToPlane() { return AngelscriptFVector3fMixin::ConstrainToPlane(FVector3f(3.0f, 4.0f, 12.0f), FVector3f(0.0f, 0.0f, 1.0f)); }
FVector3f GetVector3fConstrainedToDirection() { return AngelscriptFVector3fMixin::ConstrainToDirection(FVector3f(3.0f, 4.0f, 12.0f), FVector3f(0.0f, 0.0f, 1.0f)); }
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	const FVector Vector(3.0f, 4.0f, 12.0f);
	const FVector PlaneUp = FVector::UpVector;
	const FVector Direction = FVector::UpVector;
	const FVector A(1.0f, 0.0f, 0.0f);
	const FVector B(0.0f, 1.0f, 0.0f);

	const FVector3f Vector3f(3.0f, 4.0f, 12.0f);
	const FVector3f PlaneUp3f(0.0f, 0.0f, 1.0f);
	const FVector3f Direction3f(0.0f, 0.0f, 1.0f);
	const FVector3f A3f(1.0f, 0.0f, 0.0f);
	const FVector3f B3f(0.0f, 1.0f, 0.0f);

	double ScriptVectorAngularDistance = 0.0;
	double ScriptVectorAngularDistanceForNormals = 0.0;
	FVector ScriptVectorConstrainedToPlane = FVector::ZeroVector;
	FVector ScriptVectorConstrainedToDirection = FVector::ZeroVector;

	float ScriptVector3fAngularDistance = 0.0f;
	float ScriptVector3fAngularDistanceForNormals = 0.0f;
	FVector3f ScriptVector3fConstrainedToPlane(ForceInitToZero);
	FVector3f ScriptVector3fConstrainedToDirection(ForceInitToZero);

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("double GetVectorAngularDistance()")), ScriptVectorAngularDistance) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("double GetVectorAngularDistanceForNormals()")), ScriptVectorAngularDistanceForNormals) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FVector GetVectorConstrainedToPlane()")), ScriptVectorConstrainedToPlane) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FVector GetVectorConstrainedToDirection()")), ScriptVectorConstrainedToDirection) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("float32 GetVector3fAngularDistance()")), ScriptVector3fAngularDistance) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("float32 GetVector3fAngularDistanceForNormals()")), ScriptVector3fAngularDistanceForNormals) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetVector3fConstrainedToPlane()")), ScriptVector3fConstrainedToPlane) &&
		ExecuteValueFunction(*this, Engine, *GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetVector3fConstrainedToDirection()")), ScriptVector3fConstrainedToDirection);
	if (!bExecutedAll)
	{
		return false;
	}

	const double ExpectedVectorAngularDistance = FMath::Acos(FVector::DotProduct(A, B) / FMath::Sqrt(A.SizeSquared() * B.SizeSquared()));
	const double ExpectedVectorAngularDistanceForNormals = FMath::Acos(FVector::DotProduct(A, B));
	const FVector ExpectedVectorConstrainedToPlane = FVector::VectorPlaneProject(Vector, PlaneUp);
	const FVector ExpectedVectorConstrainedToDirection = Direction * FVector::DotProduct(Vector, Direction);

	const float ExpectedVector3fAngularDistance = FMath::Acos(FVector3f::DotProduct(A3f, B3f) / FMath::Sqrt(A3f.SizeSquared() * B3f.SizeSquared()));
	const float ExpectedVector3fAngularDistanceForNormals = FMath::Acos(FVector3f::DotProduct(A3f, B3f));
	const FVector3f ExpectedVector3fConstrainedToPlane = FVector3f::VectorPlaneProject(Vector3f, PlaneUp3f);
	const FVector3f ExpectedVector3fConstrainedToDirection = Direction3f * FVector3f::DotProduct(Vector3f, Direction3f);

	bPassed =
		TestTrue(
			TEXT("FVector AngularDistance should match the native helper result"),
			FMath::IsNearlyEqual(ScriptVectorAngularDistance, ExpectedVectorAngularDistance, KINDA_SMALL_NUMBER)) &&
		TestTrue(
			TEXT("FVector AngularDistanceForNormals should match the native helper result"),
			FMath::IsNearlyEqual(ScriptVectorAngularDistanceForNormals, ExpectedVectorAngularDistanceForNormals, KINDA_SMALL_NUMBER)) &&
		VerifyVector(
			*this,
			TEXT("FVector ConstrainToPlane should match the native helper result"),
			ScriptVectorConstrainedToPlane,
			ExpectedVectorConstrainedToPlane) &&
		VerifyVector(
			*this,
			TEXT("FVector ConstrainToDirection should match the native helper result"),
			ScriptVectorConstrainedToDirection,
			ExpectedVectorConstrainedToDirection) &&
		TestTrue(
			TEXT("FVector3f AngularDistance should match the native helper result"),
			FMath::IsNearlyEqual(ScriptVector3fAngularDistance, ExpectedVector3fAngularDistance, KINDA_SMALL_NUMBER)) &&
		TestTrue(
			TEXT("FVector3f AngularDistanceForNormals should match the native helper result"),
			FMath::IsNearlyEqual(ScriptVector3fAngularDistanceForNormals, ExpectedVector3fAngularDistanceForNormals, KINDA_SMALL_NUMBER)) &&
		VerifyVector3f(
			*this,
			TEXT("FVector3f ConstrainToPlane should match the native helper result"),
			ScriptVector3fConstrainedToPlane,
			ExpectedVector3fConstrainedToPlane) &&
		VerifyVector3f(
			*this,
			TEXT("FVector3f ConstrainToDirection should match the native helper result"),
			ScriptVector3fConstrainedToDirection,
			ExpectedVector3fConstrainedToDirection);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
