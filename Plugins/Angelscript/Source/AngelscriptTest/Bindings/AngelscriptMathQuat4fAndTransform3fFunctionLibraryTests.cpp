#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Math/Quat.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathQuat4fAndTransform3fFunctionLibraryTests_Private
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
		if (!Test.TestNotNull(TEXT("Math quat4f/transform3f function library test should expose the return value storage"), ReturnValueAddress))
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
		if (!Test.TestNotNull(TEXT("Math quat4f/transform3f function library test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Math quat4f/transform3f function library test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Math quat4f/transform3f function library test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Math quat4f/transform3f function library test saw a script exception: %s"),
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			Context->Release();
			return false;
		}

		const bool bReadReturnValue = ReadReturnValue(Test, *Context, OutValue);
		Context->Release();
		return bReadReturnValue;
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

	bool Rotator3fMatches(const FRotator3f& Actual, const FRotator3f& Expected, float ToleranceDegrees = 0.05f)
	{
		FQuat4f ActualQuat(Actual);
		FQuat4f ExpectedQuat(Expected);
		ActualQuat.Normalize();
		ExpectedQuat.Normalize();
		return FMath::RadiansToDegrees(ActualQuat.AngularDistance(ExpectedQuat)) <= ToleranceDegrees;
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

	bool VerifyTransform3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FTransform3f& Actual,
		const FTransform3f& Expected,
		float Tolerance = 0.001f)
	{
		const bool bRotationMatches = Quat4fMatches(Actual.GetRotation(), Expected.GetRotation(), FMath::Max(Tolerance, 0.05f));
		const bool bTranslationMatches = Actual.GetTranslation().Equals(Expected.GetTranslation(), Tolerance);
		const bool bScaleMatches = Actual.GetScale3D().Equals(Expected.GetScale3D(), Tolerance);
		return Test.TestTrue(What, bRotationMatches && bTranslationMatches && bScaleMatches);
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathQuat4fAndTransform3fFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathQuat4fAndTransform3fFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.MathQuat4fAndTransform3fOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathQuat4fAndTransform3fFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathQuat4fAndTransform3fOrientation"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASMathQuat4fAndTransform3fOrientation",
		TEXT(R"AS(
FQuat4f GetQuat4fFromX() { return FQuat4f::MakeFromX(FVector3f(1.0f, 1.0f, 0.0f)); }
FQuat4f GetQuat4fFromY() { return FQuat4f::MakeFromY(FVector3f(-1.0f, 1.0f, 0.0f)); }
FQuat4f GetQuat4fFromZ() { return FQuat4f::MakeFromZ(FVector3f(0.0f, 0.0f, 1.0f)); }
FQuat4f GetQuat4fFromXY() { return FQuat4f::MakeFromXY(FVector3f(1.0f, 1.0f, 0.0f), FVector3f(-1.0f, 1.0f, 0.0f)); }
FQuat4f GetQuat4fFromXZ() { return FQuat4f::MakeFromXZ(FVector3f(1.0f, 1.0f, 0.0f), FVector3f(0.0f, 0.0f, 1.0f)); }
FQuat4f GetQuat4fFromYX() { return FQuat4f::MakeFromYX(FVector3f(-1.0f, 1.0f, 0.0f), FVector3f(1.0f, 1.0f, 0.0f)); }
FQuat4f GetQuat4fFromYZ() { return FQuat4f::MakeFromYZ(FVector3f(-1.0f, 1.0f, 0.0f), FVector3f(0.0f, 0.0f, 1.0f)); }
FQuat4f GetQuat4fFromZX() { return FQuat4f::MakeFromZX(FVector3f(0.0f, 0.0f, 1.0f), FVector3f(1.0f, 1.0f, 0.0f)); }
FQuat4f GetQuat4fFromZY() { return FQuat4f::MakeFromZY(FVector3f(0.0f, 0.0f, 1.0f), FVector3f(-1.0f, 1.0f, 0.0f)); }

FTransform3f MakeBaselineTransform()
{
	return FTransform3f(FRotator3f(10.0f, 20.0f, 30.0f), FVector3f(100.0f, -50.0f, 25.0f), FVector3f(1.25f, 0.75f, 2.0f));
}

FRotator3f GetTransformRotation3f()
{
	return FTransform3f::TransformRotation(MakeBaselineTransform(), FRotator3f(-30.0f, 15.0f, 45.0f));
}

FRotator3f GetRoundTripRotation3f()
{
	return FTransform3f::InverseTransformRotation(MakeBaselineTransform(), GetTransformRotation3f());
}

FTransform3f GetSetRotationTransform3f()
{
	FTransform3f Result = MakeBaselineTransform();
	Result.SetRotation(FQuat4f(FRotator3f(-30.0f, 15.0f, 45.0f)));
	return Result;
}

FTransform3f GetTransformFromXY3f() { return FTransform3f::MakeFromXY(FVector3f(1.0f, 1.0f, 0.0f), FVector3f(-1.0f, 1.0f, 0.0f)); }
FTransform3f GetTransformFromXZ3f() { return FTransform3f::MakeFromXZ(FVector3f(1.0f, 1.0f, 0.0f), FVector3f(0.0f, 0.0f, 1.0f)); }
FTransform3f GetTransformFromYX3f() { return FTransform3f::MakeFromYX(FVector3f(-1.0f, 1.0f, 0.0f), FVector3f(1.0f, 1.0f, 0.0f)); }
FTransform3f GetTransformFromYZ3f() { return FTransform3f::MakeFromYZ(FVector3f(-1.0f, 1.0f, 0.0f), FVector3f(0.0f, 0.0f, 1.0f)); }
FTransform3f GetTransformFromZX3f() { return FTransform3f::MakeFromZX(FVector3f(0.0f, 0.0f, 1.0f), FVector3f(1.0f, 1.0f, 0.0f)); }
FTransform3f GetTransformFromZY3f() { return FTransform3f::MakeFromZY(FVector3f(0.0f, 0.0f, 1.0f), FVector3f(-1.0f, 1.0f, 0.0f)); }
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	const FVector3f FactoryX(1.0f, 1.0f, 0.0f);
	const FVector3f FactoryY(-1.0f, 1.0f, 0.0f);
	const FVector3f FactoryZ(0.0f, 0.0f, 1.0f);
	const FTransform3f BaselineTransform(
		FRotator3f(10.0f, 20.0f, 30.0f),
		FVector3f(100.0f, -50.0f, 25.0f),
		FVector3f(1.25f, 0.75f, 2.0f));
	const FRotator3f LocalRotation(-30.0f, 15.0f, 45.0f);

	FQuat4f ScriptQuat4fFromX;
	FQuat4f ScriptQuat4fFromY;
	FQuat4f ScriptQuat4fFromZ;
	FQuat4f ScriptQuat4fFromXY;
	FQuat4f ScriptQuat4fFromXZ;
	FQuat4f ScriptQuat4fFromYX;
	FQuat4f ScriptQuat4fFromYZ;
	FQuat4f ScriptQuat4fFromZX;
	FQuat4f ScriptQuat4fFromZY;
	FRotator3f ScriptTransformRotation3f(ForceInitToZero);
	FRotator3f ScriptRoundTripRotation3f(ForceInitToZero);
	FTransform3f ScriptSetRotationTransform3f = FTransform3f::Identity;
	FTransform3f ScriptTransformFromXY3f = FTransform3f::Identity;
	FTransform3f ScriptTransformFromXZ3f = FTransform3f::Identity;
	FTransform3f ScriptTransformFromYX3f = FTransform3f::Identity;
	FTransform3f ScriptTransformFromYZ3f = FTransform3f::Identity;
	FTransform3f ScriptTransformFromZX3f = FTransform3f::Identity;
	FTransform3f ScriptTransformFromZY3f = FTransform3f::Identity;

	asIScriptFunction* Quat4fFromXFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromX()"));
	asIScriptFunction* Quat4fFromYFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromY()"));
	asIScriptFunction* Quat4fFromZFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromZ()"));
	asIScriptFunction* Quat4fFromXYFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromXY()"));
	asIScriptFunction* Quat4fFromXZFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromXZ()"));
	asIScriptFunction* Quat4fFromYXFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromYX()"));
	asIScriptFunction* Quat4fFromYZFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromYZ()"));
	asIScriptFunction* Quat4fFromZXFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromZX()"));
	asIScriptFunction* Quat4fFromZYFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat4f GetQuat4fFromZY()"));
	asIScriptFunction* TransformRotation3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetTransformRotation3f()"));
	asIScriptFunction* RoundTripRotation3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FRotator3f GetRoundTripRotation3f()"));
	asIScriptFunction* SetRotationTransform3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetSetRotationTransform3f()"));
	asIScriptFunction* TransformFromXY3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetTransformFromXY3f()"));
	asIScriptFunction* TransformFromXZ3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetTransformFromXZ3f()"));
	asIScriptFunction* TransformFromYX3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetTransformFromYX3f()"));
	asIScriptFunction* TransformFromYZ3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetTransformFromYZ3f()"));
	asIScriptFunction* TransformFromZX3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetTransformFromZX3f()"));
	asIScriptFunction* TransformFromZY3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetTransformFromZY3f()"));
	if (Quat4fFromXFunction == nullptr
		|| Quat4fFromYFunction == nullptr
		|| Quat4fFromZFunction == nullptr
		|| Quat4fFromXYFunction == nullptr
		|| Quat4fFromXZFunction == nullptr
		|| Quat4fFromYXFunction == nullptr
		|| Quat4fFromYZFunction == nullptr
		|| Quat4fFromZXFunction == nullptr
		|| Quat4fFromZYFunction == nullptr
		|| TransformRotation3fFunction == nullptr
		|| RoundTripRotation3fFunction == nullptr
		|| SetRotationTransform3fFunction == nullptr
		|| TransformFromXY3fFunction == nullptr
		|| TransformFromXZ3fFunction == nullptr
		|| TransformFromYX3fFunction == nullptr
		|| TransformFromYZ3fFunction == nullptr
		|| TransformFromZX3fFunction == nullptr
		|| TransformFromZY3fFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *Quat4fFromXFunction, ScriptQuat4fFromX) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromYFunction, ScriptQuat4fFromY) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromZFunction, ScriptQuat4fFromZ) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromXYFunction, ScriptQuat4fFromXY) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromXZFunction, ScriptQuat4fFromXZ) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromYXFunction, ScriptQuat4fFromYX) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromYZFunction, ScriptQuat4fFromYZ) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromZXFunction, ScriptQuat4fFromZX) &&
		ExecuteValueFunction(*this, Engine, *Quat4fFromZYFunction, ScriptQuat4fFromZY) &&
		ExecuteValueFunction(*this, Engine, *TransformRotation3fFunction, ScriptTransformRotation3f) &&
		ExecuteValueFunction(*this, Engine, *RoundTripRotation3fFunction, ScriptRoundTripRotation3f) &&
		ExecuteValueFunction(*this, Engine, *SetRotationTransform3fFunction, ScriptSetRotationTransform3f) &&
		ExecuteValueFunction(*this, Engine, *TransformFromXY3fFunction, ScriptTransformFromXY3f) &&
		ExecuteValueFunction(*this, Engine, *TransformFromXZ3fFunction, ScriptTransformFromXZ3f) &&
		ExecuteValueFunction(*this, Engine, *TransformFromYX3fFunction, ScriptTransformFromYX3f) &&
		ExecuteValueFunction(*this, Engine, *TransformFromYZ3fFunction, ScriptTransformFromYZ3f) &&
		ExecuteValueFunction(*this, Engine, *TransformFromZX3fFunction, ScriptTransformFromZX3f) &&
		ExecuteValueFunction(*this, Engine, *TransformFromZY3fFunction, ScriptTransformFromZY3f);
	if (!bExecutedAll)
	{
		return false;
	}

	const FQuat4f ExpectedQuat4fFromX = FRotationMatrix44f::MakeFromX(FactoryX).ToQuat();
	const FQuat4f ExpectedQuat4fFromY = FRotationMatrix44f::MakeFromY(FactoryY).ToQuat();
	const FQuat4f ExpectedQuat4fFromZ = FRotationMatrix44f::MakeFromZ(FactoryZ).ToQuat();
	const FQuat4f ExpectedQuat4fFromXY = FRotationMatrix44f::MakeFromXY(FactoryX, FactoryY).ToQuat();
	const FQuat4f ExpectedQuat4fFromXZ = FRotationMatrix44f::MakeFromXZ(FactoryX, FactoryZ).ToQuat();
	const FQuat4f ExpectedQuat4fFromYX = FRotationMatrix44f::MakeFromYX(FactoryY, FactoryX).ToQuat();
	const FQuat4f ExpectedQuat4fFromYZ = FRotationMatrix44f::MakeFromYZ(FactoryY, FactoryZ).ToQuat();
	const FQuat4f ExpectedQuat4fFromZX = FRotationMatrix44f::MakeFromZX(FactoryZ, FactoryX).ToQuat();
	const FQuat4f ExpectedQuat4fFromZY = FRotationMatrix44f::MakeFromZY(FactoryZ, FactoryY).ToQuat();

	const FRotator3f ExpectedTransformRotation3f = BaselineTransform.TransformRotation(LocalRotation.Quaternion()).Rotator();
	const FRotator3f ExpectedRoundTripRotation3f = BaselineTransform.InverseTransformRotation(ExpectedTransformRotation3f.Quaternion()).Rotator();
	FTransform3f ExpectedSetRotationTransform3f = BaselineTransform;
	ExpectedSetRotationTransform3f.SetRotation(LocalRotation.Quaternion());

	const FTransform3f ExpectedTransformFromXY3f = FTransform3f(FRotationMatrix44f::MakeFromXY(FactoryX, FactoryY));
	const FTransform3f ExpectedTransformFromXZ3f = FTransform3f(FRotationMatrix44f::MakeFromXZ(FactoryX, FactoryZ));
	const FTransform3f ExpectedTransformFromYX3f = FTransform3f(FRotationMatrix44f::MakeFromYX(FactoryY, FactoryX));
	const FTransform3f ExpectedTransformFromYZ3f = FTransform3f(FRotationMatrix44f::MakeFromYZ(FactoryY, FactoryZ));
	const FTransform3f ExpectedTransformFromZX3f = FTransform3f(FRotationMatrix44f::MakeFromZX(FactoryZ, FactoryX));
	const FTransform3f ExpectedTransformFromZY3f = FTransform3f(FRotationMatrix44f::MakeFromZY(FactoryZ, FactoryY));

	bPassed =
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromX should match the native float rotation matrix factory"),
			ScriptQuat4fFromX,
			ExpectedQuat4fFromX) &&
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromY should match the native float rotation matrix factory"),
			ScriptQuat4fFromY,
			ExpectedQuat4fFromY) &&
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromZ should match the native float rotation matrix factory"),
			ScriptQuat4fFromZ,
			ExpectedQuat4fFromZ) &&
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromXY should match the native float rotation matrix factory"),
			ScriptQuat4fFromXY,
			ExpectedQuat4fFromXY) &&
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromXZ should match the native float rotation matrix factory"),
			ScriptQuat4fFromXZ,
			ExpectedQuat4fFromXZ) &&
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromYX should match the native float rotation matrix factory"),
			ScriptQuat4fFromYX,
			ExpectedQuat4fFromYX) &&
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromYZ should match the native float rotation matrix factory"),
			ScriptQuat4fFromYZ,
			ExpectedQuat4fFromYZ) &&
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromZX should match the native float rotation matrix factory"),
			ScriptQuat4fFromZX,
			ExpectedQuat4fFromZX) &&
		VerifyQuat4f(
			*this,
			TEXT("FQuat4f::MakeFromZY should match the native float rotation matrix factory"),
			ScriptQuat4fFromZY,
			ExpectedQuat4fFromZY) &&
		VerifyRotator3f(
			*this,
			TEXT("FTransform3f::TransformRotation should match the native quaternion-based rotation transform"),
			ScriptTransformRotation3f,
			ExpectedTransformRotation3f) &&
		VerifyRotator3f(
			*this,
			TEXT("FTransform3f::InverseTransformRotation should round-trip the transformed rotator"),
			ScriptRoundTripRotation3f,
			ExpectedRoundTripRotation3f) &&
		VerifyRotator3f(
			*this,
			TEXT("FTransform3f rotation round-trip should recover the original local rotator"),
			ScriptRoundTripRotation3f,
			LocalRotation) &&
		VerifyTransform3f(
			*this,
			TEXT("FTransform3f::SetRotation should update only the rotation component"),
			ScriptSetRotationTransform3f,
			ExpectedSetRotationTransform3f) &&
		VerifyVector3f(
			*this,
			TEXT("FTransform3f::SetRotation should preserve the original translation"),
			ScriptSetRotationTransform3f.GetTranslation(),
			BaselineTransform.GetTranslation()) &&
		VerifyVector3f(
			*this,
			TEXT("FTransform3f::SetRotation should preserve the original scale"),
			ScriptSetRotationTransform3f.GetScale3D(),
			BaselineTransform.GetScale3D()) &&
		VerifyTransform3f(
			*this,
			TEXT("FTransform3f::MakeFromXY should match the native float transform factory"),
			ScriptTransformFromXY3f,
			ExpectedTransformFromXY3f) &&
		VerifyTransform3f(
			*this,
			TEXT("FTransform3f::MakeFromXZ should match the native float transform factory"),
			ScriptTransformFromXZ3f,
			ExpectedTransformFromXZ3f) &&
		VerifyTransform3f(
			*this,
			TEXT("FTransform3f::MakeFromYX should match the native float transform factory"),
			ScriptTransformFromYX3f,
			ExpectedTransformFromYX3f) &&
		VerifyTransform3f(
			*this,
			TEXT("FTransform3f::MakeFromYZ should match the native float transform factory"),
			ScriptTransformFromYZ3f,
			ExpectedTransformFromYZ3f) &&
		VerifyTransform3f(
			*this,
			TEXT("FTransform3f::MakeFromZX should match the native float transform factory"),
			ScriptTransformFromZX3f,
			ExpectedTransformFromZX3f) &&
		VerifyTransform3f(
			*this,
			TEXT("FTransform3f::MakeFromZY should match the native float transform factory"),
			ScriptTransformFromZY3f,
			ExpectedTransformFromZY3f);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
