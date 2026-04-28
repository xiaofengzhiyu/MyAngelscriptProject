#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Math/Quat.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathQuatAndTransformFactoryFunctionLibraryTests_Private
{
	template <typename TValue>
	bool ExecuteValueFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		TValue& OutValue)
	{
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(TEXT("Math quat/transform factory test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Math quat/transform factory test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Math quat/transform factory test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Math quat/transform factory test saw a script exception: %s"),
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			Context->Release();
			return false;
		}

		void* ReturnValueAddress = Context->GetAddressOfReturnValue();
		if (!Test.TestNotNull(TEXT("Math quat/transform factory test should expose the return value storage"), ReturnValueAddress))
		{
			Context->Release();
			return false;
		}

		OutValue = *static_cast<TValue*>(ReturnValueAddress);
		Context->Release();
		return true;
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

	bool VerifyQuat(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FQuat& Actual,
		const FQuat& Expected,
		double ToleranceDegrees = 0.05)
	{
		return Test.TestTrue(What, QuatMatches(Actual, Expected, ToleranceDegrees));
	}

	bool Quat4fMatches(const FQuat4f& Actual, const FQuat4f& Expected, float ToleranceDegrees = 0.05f)
	{
		FQuat4f ActualQuat = Actual;
		FQuat4f ExpectedQuat = Expected;
		ActualQuat.Normalize();
		ExpectedQuat.Normalize();
		if ((ActualQuat | ExpectedQuat) < 0.0f)
		{
			ExpectedQuat = FQuat4f(-ExpectedQuat.X, -ExpectedQuat.Y, -ExpectedQuat.Z, -ExpectedQuat.W);
		}

		return FMath::RadiansToDegrees(ActualQuat.AngularDistance(ExpectedQuat)) <= ToleranceDegrees;
	}

	bool VerifyQuat4f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FQuat4f& Actual,
		const FQuat4f& Expected,
		float ToleranceDegrees = 0.05f)
	{
		return Test.TestTrue(What, Quat4fMatches(Actual, Expected, ToleranceDegrees));
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

	bool VerifyTransform(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FTransform& Actual,
		const FTransform& Expected,
		double Tolerance = 0.01)
	{
		const bool bRotationMatches = QuatMatches(Actual.GetRotation(), Expected.GetRotation(), FMath::Max(Tolerance, 0.05));
		const bool bTranslationMatches = Actual.GetLocation().Equals(Expected.GetLocation(), Tolerance);
		const bool bScaleMatches = Actual.GetScale3D().Equals(Expected.GetScale3D(), Tolerance);
		if (!(bRotationMatches && bTranslationMatches && bScaleMatches))
		{
			Test.AddInfo(FString::Printf(
				TEXT("%s actual rotation=%s expected rotation=%s actual translation=%s expected translation=%s actual scale=%s expected scale=%s"),
				What,
				*Actual.Rotator().ToCompactString(),
				*Expected.Rotator().ToCompactString(),
				*Actual.GetLocation().ToCompactString(),
				*Expected.GetLocation().ToCompactString(),
				*Actual.GetScale3D().ToCompactString(),
				*Expected.GetScale3D().ToCompactString()));
		}

		return Test.TestTrue(What, bRotationMatches && bTranslationMatches && bScaleMatches);
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathQuatAndTransformFactoryFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathQuatAndTransformFactoryFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.MathQuatAndTransformFactories",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathQuatAndTransformFactoryFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathQuatAndTransformFactories"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASMathQuatAndTransformFactories",
		TEXT(R"AS(
FQuat GetQuatFromAxes()
{
	return FQuat::MakeFromAxes(FVector(2.0f, 2.0f, 0.0f), FVector(-3.0f, 3.0f, 0.0f), FVector(0.0f, 0.0f, 5.0f));
}

FQuat4f GetQuat4fFromAxes()
{
	return FQuat4f::MakeFromAxes(FVector3f(2.0f, 2.0f, 0.0f), FVector3f(-3.0f, 3.0f, 0.0f), FVector3f(0.0f, 0.0f, 5.0f));
}

FTransform GetTransformFromXY()
{
	return FTransform::MakeFromXY(FVector(2.0f, 2.0f, 0.0f), FVector(-3.0f, 3.0f, 0.0f));
}

FTransform GetTransformFromXZ()
{
	return FTransform::MakeFromXZ(FVector(2.0f, 2.0f, 0.0f), FVector(0.0f, 0.0f, 5.0f));
}

FTransform GetTransformFromYX()
{
	return FTransform::MakeFromYX(FVector(-3.0f, 3.0f, 0.0f), FVector(2.0f, 2.0f, 0.0f));
}

FTransform GetTransformFromYZ()
{
	return FTransform::MakeFromYZ(FVector(-3.0f, 3.0f, 0.0f), FVector(0.0f, 0.0f, 5.0f));
}

FTransform GetTransformFromZX()
{
	return FTransform::MakeFromZX(FVector(0.0f, 0.0f, 5.0f), FVector(2.0f, 2.0f, 0.0f));
}

FTransform GetTransformFromZY()
{
	return FTransform::MakeFromZY(FVector(0.0f, 0.0f, 5.0f), FVector(-3.0f, 3.0f, 0.0f));
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	const FVector Forward(2.0f, 2.0f, 0.0f);
	const FVector Right(-3.0f, 3.0f, 0.0f);
	const FVector Up(0.0f, 0.0f, 5.0f);

	const FVector3f Forward3f(2.0f, 2.0f, 0.0f);
	const FVector3f Right3f(-3.0f, 3.0f, 0.0f);
	const FVector3f Up3f(0.0f, 0.0f, 5.0f);

	FQuat ScriptQuatFromAxes = FQuat::Identity;
	FQuat4f ScriptQuat4fFromAxes = FQuat4f::Identity;
	FTransform ScriptTransformFromXY = FTransform::Identity;
	FTransform ScriptTransformFromXZ = FTransform::Identity;
	FTransform ScriptTransformFromYX = FTransform::Identity;
	FTransform ScriptTransformFromYZ = FTransform::Identity;
	FTransform ScriptTransformFromZX = FTransform::Identity;
	FTransform ScriptTransformFromZY = FTransform::Identity;

	asIScriptFunction* QuatFromAxesFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat GetQuatFromAxes()"));
	asIScriptFunction* Quat4fFromAxesFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromAxes()"));
	asIScriptFunction* TransformFromXYFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform GetTransformFromXY()"));
	asIScriptFunction* TransformFromXZFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform GetTransformFromXZ()"));
	asIScriptFunction* TransformFromYXFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform GetTransformFromYX()"));
	asIScriptFunction* TransformFromYZFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform GetTransformFromYZ()"));
	asIScriptFunction* TransformFromZXFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform GetTransformFromZX()"));
	asIScriptFunction* TransformFromZYFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform GetTransformFromZY()"));
	if (QuatFromAxesFunction == nullptr
		|| Quat4fFromAxesFunction == nullptr
		|| TransformFromXYFunction == nullptr
		|| TransformFromXZFunction == nullptr
		|| TransformFromYXFunction == nullptr
		|| TransformFromYZFunction == nullptr
		|| TransformFromZXFunction == nullptr
		|| TransformFromZYFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *QuatFromAxesFunction, ScriptQuatFromAxes) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromAxesFunction, ScriptQuat4fFromAxes) &&
		ExecuteValueFunction(*this, Engine, *TransformFromXYFunction, ScriptTransformFromXY) &&
		ExecuteValueFunction(*this, Engine, *TransformFromXZFunction, ScriptTransformFromXZ) &&
		ExecuteValueFunction(*this, Engine, *TransformFromYXFunction, ScriptTransformFromYX) &&
		ExecuteValueFunction(*this, Engine, *TransformFromYZFunction, ScriptTransformFromYZ) &&
		ExecuteValueFunction(*this, Engine, *TransformFromZXFunction, ScriptTransformFromZX) &&
		ExecuteValueFunction(*this, Engine, *TransformFromZYFunction, ScriptTransformFromZY);
	if (!bExecutedAll)
	{
		return false;
	}

	const FQuat ExpectedQuatFromAxes = FMatrix(
		Forward.GetSafeNormal(),
		Right.GetSafeNormal(),
		Up.GetSafeNormal(),
		FVector::ZeroVector).ToQuat();
	const FQuat4f ExpectedQuat4fFromAxes = FMatrix44f(
		Forward3f.GetSafeNormal(),
		Right3f.GetSafeNormal(),
		Up3f.GetSafeNormal(),
		FVector3f::ZeroVector).ToQuat();

	const FTransform ExpectedTransformFromXY = FTransform(FRotationMatrix::MakeFromXY(Forward, Right));
	const FTransform ExpectedTransformFromXZ = FTransform(FRotationMatrix::MakeFromXZ(Forward, Up));
	const FTransform ExpectedTransformFromYX = FTransform(FRotationMatrix::MakeFromYX(Right, Forward));
	const FTransform ExpectedTransformFromYZ = FTransform(FRotationMatrix::MakeFromYZ(Right, Up));
	const FTransform ExpectedTransformFromZX = FTransform(FRotationMatrix::MakeFromZX(Up, Forward));
	const FTransform ExpectedTransformFromZY = FTransform(FRotationMatrix::MakeFromZY(Up, Right));

	const bool bQuatFromAxesMatches = VerifyQuat(
		*this,
		TEXT("FQuat::MakeFromAxes should normalize the provided axes and match the native matrix conversion"),
		ScriptQuatFromAxes,
		ExpectedQuatFromAxes);
	const bool bQuat4fFromAxesMatches = VerifyQuat4f(
		*this,
		TEXT("FQuat4f::MakeFromAxes should normalize the provided axes and match the native float matrix conversion"),
		ScriptQuat4fFromAxes,
		ExpectedQuat4fFromAxes);
	const bool bTransformFromXYMatches = VerifyTransform(
		*this,
		TEXT("FTransform::MakeFromXY should match the native transform factory"),
		ScriptTransformFromXY,
		ExpectedTransformFromXY);
	const bool bTransformFromXZMatches = VerifyTransform(
		*this,
		TEXT("FTransform::MakeFromXZ should match the native transform factory"),
		ScriptTransformFromXZ,
		ExpectedTransformFromXZ);
	const bool bTransformFromYXMatches = VerifyTransform(
		*this,
		TEXT("FTransform::MakeFromYX should match the native transform factory"),
		ScriptTransformFromYX,
		ExpectedTransformFromYX);
	const bool bTransformFromYZMatches = VerifyTransform(
		*this,
		TEXT("FTransform::MakeFromYZ should match the native transform factory"),
		ScriptTransformFromYZ,
		ExpectedTransformFromYZ);
	const bool bTransformFromZXMatches = VerifyTransform(
		*this,
		TEXT("FTransform::MakeFromZX should match the native transform factory"),
		ScriptTransformFromZX,
		ExpectedTransformFromZX);
	const bool bTransformFromZYMatches = VerifyTransform(
		*this,
		TEXT("FTransform::MakeFromZY should match the native transform factory"),
		ScriptTransformFromZY,
		ExpectedTransformFromZY);
	const bool bTransformFactoriesStayOriginAligned = VerifyVector(
		*this,
		TEXT("FTransform axis factories should keep translation at the origin"),
		ScriptTransformFromXY.GetLocation(),
		FVector::ZeroVector);
	const bool bTransformFactoriesStayUnitScale = VerifyVector(
		*this,
		TEXT("FTransform axis factories should keep unit scale"),
		ScriptTransformFromXY.GetScale3D(),
		FVector::OneVector);

	bPassed =
		bQuatFromAxesMatches &&
		bQuat4fFromAxesMatches &&
		bTransformFromXYMatches &&
		bTransformFromXZMatches &&
		bTransformFromYXMatches &&
		bTransformFromYZMatches &&
		bTransformFromZXMatches &&
		bTransformFromZYMatches &&
		bTransformFactoriesStayOriginAligned &&
		bTransformFactoriesStayUnitScale;

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
