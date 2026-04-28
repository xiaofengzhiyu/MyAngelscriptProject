#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "FunctionLibraries/AngelscriptMathLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathBoundaryFunctionLibraryTests_Private
{
	FString FormatScriptFloatLiteral(const double Value)
	{
		FString Literal = FString::Printf(TEXT("%.17g"), Value);
		if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e")) && !Literal.Contains(TEXT("E")))
		{
			Literal += TEXT(".0");
		}

		return Literal;
	}

	FString FormatScriptUInt32Literal(const uint32 Value)
	{
		return FString::Printf(TEXT("uint32(%u)"), Value);
	}

	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, int32& OutValue)
	{
		OutValue = static_cast<int32>(Context.GetReturnDWord());
		return true;
	}

	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, uint32& OutValue)
	{
		OutValue = static_cast<uint32>(Context.GetReturnDWord());
		return true;
	}

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
		if (!Test.TestNotNull(TEXT("Math boundary test should expose the return value storage"), ReturnValueAddress))
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
		if (!Test.TestNotNull(TEXT("Math boundary test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Math boundary test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Math boundary test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Math boundary test saw a script exception: %s"),
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

using namespace AngelscriptTest_Bindings_AngelscriptMathBoundaryFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathBoundaryFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.MathBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathBoundaryFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathBoundaries"));
	};

	const int32 ExpectedWrapIntBelow = UAngelscriptMathLibrary::WrapInt(-1, 0, 4);
	const int32 ExpectedWrapIntAbove = UAngelscriptMathLibrary::WrapInt(5, 0, 4);
	const float ExpectedWrapFloatBelow = UAngelscriptMathLibrary::WrapFloat(-1.25f, 0.0f, 4.0f);
	const double ExpectedWrapDoubleAbove = UAngelscriptMathLibrary::WrapDouble(5.5, 0.0, 4.0);
	const int32 ExpectedWrapIndexNegative = UAngelscriptMathLibrary::WrapIndex(-1, 0, 4);
	const int32 ExpectedWrapIndexAbove = UAngelscriptMathLibrary::WrapIndex(5, 0, 4);
	const uint32 ExpectedWrapIndexUIntAbove = UAngelscriptMathLibrary::WrapIndexUInt(6u, 0u, 4u);

	FString Script = TEXT(R"AS(
int VerifyWrapContracts()
{
	if (Math::Wrap(-1, 0, 4) != $EXPECTED_WRAP_INT_BELOW$)
		return 10;
	if (Math::Wrap(5, 0, 4) != $EXPECTED_WRAP_INT_ABOVE$)
		return 20;
	if (!Math::IsNearlyEqual(
		Math::Wrap(float32(-1.25f), float32(0.0f), float32(4.0f)),
		float32($EXPECTED_WRAP_FLOAT_BELOW$),
		float32($FLOAT_TOLERANCE$)))
		return 30;
	if (!Math::IsNearlyEqual(
		Math::Wrap(float64(5.5), float64(0.0), float64(4.0)),
		float64($EXPECTED_WRAP_DOUBLE_ABOVE$),
		float64($DOUBLE_TOLERANCE$)))
		return 40;
	if (Math::WrapIndex(-1, 0, 4) != $EXPECTED_WRAPINDEX_NEGATIVE$)
		return 50;
	if (Math::WrapIndex(5, 0, 4) != $EXPECTED_WRAPINDEX_ABOVE$)
		return 60;
	if (Math::WrapIndex(uint32(6), uint32(0), uint32(4)) != $EXPECTED_WRAPINDEX_UINT_ABOVE$)
		return 70;

	return 1;
}

double GetZeroVectorAngularDistance() { return AngelscriptFVectorMixin::AngularDistance(FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 0.0f, 0.0f)); }
double GetNearUnitNormalAngularDistance() { return AngelscriptFVectorMixin::AngularDistanceForNormals(FVector(1.0f, 0.0f, 0.0f), FVector(1.0001f, 0.0f, 0.0f)); }
FVector GetConstrainedToPlaneNonUnit() { return AngelscriptFVectorMixin::ConstrainToPlane(FVector(1.0f, 2.0f, 3.0f), FVector(0.0f, 0.0f, 2.0f)); }
FVector GetConstrainedToDirectionNonUnit() { return AngelscriptFVectorMixin::ConstrainToDirection(FVector(1.0f, 0.0f, 0.0f), FVector(2.0f, 0.0f, 0.0f)); }

float32 GetZeroVectorAngularDistance3f() { return AngelscriptFVector3fMixin::AngularDistance(FVector3f(0.0f, 0.0f, 0.0f), FVector3f(1.0f, 0.0f, 0.0f)); }
float32 GetNearUnitNormalAngularDistance3f() { return AngelscriptFVector3fMixin::AngularDistanceForNormals(FVector3f(1.0f, 0.0f, 0.0f), FVector3f(1.0001f, 0.0f, 0.0f)); }
FVector3f GetConstrainedToPlaneNonUnit3f() { return AngelscriptFVector3fMixin::ConstrainToPlane(FVector3f(1.0f, 2.0f, 3.0f), FVector3f(0.0f, 0.0f, 2.0f)); }
FVector3f GetConstrainedToDirectionNonUnit3f() { return AngelscriptFVector3fMixin::ConstrainToDirection(FVector3f(1.0f, 0.0f, 0.0f), FVector3f(2.0f, 0.0f, 0.0f)); }
)AS");
	Script.ReplaceInline(TEXT("$EXPECTED_WRAP_INT_BELOW$"), *FString::FromInt(ExpectedWrapIntBelow));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAP_INT_ABOVE$"), *FString::FromInt(ExpectedWrapIntAbove));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAP_FLOAT_BELOW$"), *FormatScriptFloatLiteral(ExpectedWrapFloatBelow));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAP_DOUBLE_ABOVE$"), *FormatScriptFloatLiteral(ExpectedWrapDoubleAbove));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAPINDEX_NEGATIVE$"), *FString::FromInt(ExpectedWrapIndexNegative));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAPINDEX_ABOVE$"), *FString::FromInt(ExpectedWrapIndexAbove));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAPINDEX_UINT_ABOVE$"), *FormatScriptUInt32Literal(ExpectedWrapIndexUIntAbove));
	Script.ReplaceInline(TEXT("$FLOAT_TOLERANCE$"), *FormatScriptFloatLiteral(0.0001));
	Script.ReplaceInline(TEXT("$DOUBLE_TOLERANCE$"), *FormatScriptFloatLiteral(0.0000001));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASMathBoundaries", Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* VerifyWrapContractsFunction = GetFunctionByDecl(*this, *Module, TEXT("int VerifyWrapContracts()"));
	asIScriptFunction* GetZeroVectorAngularDistanceFunction = GetFunctionByDecl(*this, *Module, TEXT("double GetZeroVectorAngularDistance()"));
	asIScriptFunction* GetNearUnitNormalAngularDistanceFunction = GetFunctionByDecl(*this, *Module, TEXT("double GetNearUnitNormalAngularDistance()"));
	asIScriptFunction* GetConstrainedToPlaneNonUnitFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetConstrainedToPlaneNonUnit()"));
	asIScriptFunction* GetConstrainedToDirectionNonUnitFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetConstrainedToDirectionNonUnit()"));
	asIScriptFunction* GetZeroVectorAngularDistance3fFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetZeroVectorAngularDistance3f()"));
	asIScriptFunction* GetNearUnitNormalAngularDistance3fFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetNearUnitNormalAngularDistance3f()"));
	asIScriptFunction* GetConstrainedToPlaneNonUnit3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetConstrainedToPlaneNonUnit3f()"));
	asIScriptFunction* GetConstrainedToDirectionNonUnit3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetConstrainedToDirectionNonUnit3f()"));
	if (VerifyWrapContractsFunction == nullptr
		|| GetZeroVectorAngularDistanceFunction == nullptr
		|| GetNearUnitNormalAngularDistanceFunction == nullptr
		|| GetConstrainedToPlaneNonUnitFunction == nullptr
		|| GetConstrainedToDirectionNonUnitFunction == nullptr
		|| GetZeroVectorAngularDistance3fFunction == nullptr
		|| GetNearUnitNormalAngularDistance3fFunction == nullptr
		|| GetConstrainedToPlaneNonUnit3fFunction == nullptr
		|| GetConstrainedToDirectionNonUnit3fFunction == nullptr)
	{
		return false;
	}

	const double ExpectedZeroVectorAngularDistance = UAngelscriptFVectorMixinLibrary::AngularDistance(FVector::ZeroVector, FVector::ForwardVector);
	const double ExpectedNearUnitNormalAngularDistance = UAngelscriptFVectorMixinLibrary::AngularDistanceForNormals(FVector::ForwardVector, FVector(1.0001f, 0.0f, 0.0f));
	const FVector ExpectedConstrainToPlane = UAngelscriptFVectorMixinLibrary::ConstrainToPlane(FVector(1.0f, 2.0f, 3.0f), FVector(0.0f, 0.0f, 2.0f));
	const FVector ExpectedConstrainToDirection = UAngelscriptFVectorMixinLibrary::ConstrainToDirection(FVector(1.0f, 0.0f, 0.0f), FVector(2.0f, 0.0f, 0.0f));

	const float ExpectedZeroVectorAngularDistance3f = UAngelscriptFVector3fMixinLibrary::AngularDistance(FVector3f::ZeroVector, FVector3f(1.0f, 0.0f, 0.0f));
	const float ExpectedNearUnitNormalAngularDistance3f = UAngelscriptFVector3fMixinLibrary::AngularDistanceForNormals(FVector3f(1.0f, 0.0f, 0.0f), FVector3f(1.0001f, 0.0f, 0.0f));
	const FVector3f ExpectedConstrainToPlane3f = UAngelscriptFVector3fMixinLibrary::ConstrainToPlane(FVector3f(1.0f, 2.0f, 3.0f), FVector3f(0.0f, 0.0f, 2.0f));
	const FVector3f ExpectedConstrainToDirection3f = UAngelscriptFVector3fMixinLibrary::ConstrainToDirection(FVector3f(1.0f, 0.0f, 0.0f), FVector3f(2.0f, 0.0f, 0.0f));

	bPassed &= TestTrue(TEXT("Native zero-vector angular-distance baseline should stay finite"), FMath::IsFinite(ExpectedZeroVectorAngularDistance));
	bPassed &= TestTrue(TEXT("Native near-unit normal angular-distance baseline should stay finite"), FMath::IsFinite(ExpectedNearUnitNormalAngularDistance));
	bPassed &= TestTrue(TEXT("Native FVector3f zero-vector angular-distance baseline should stay finite"), FMath::IsFinite(ExpectedZeroVectorAngularDistance3f));
	bPassed &= TestTrue(TEXT("Native FVector3f near-unit normal angular-distance baseline should stay finite"), FMath::IsFinite(ExpectedNearUnitNormalAngularDistance3f));
	if (!bPassed)
	{
		return false;
	}

	int32 WrapVerificationResult = 0;

	double ScriptZeroVectorAngularDistance = 0.0;
	double ScriptNearUnitNormalAngularDistance = 0.0;
	FVector ScriptConstrainToPlane = FVector::ZeroVector;
	FVector ScriptConstrainToDirection = FVector::ZeroVector;

	float ScriptZeroVectorAngularDistance3f = 0.0f;
	float ScriptNearUnitNormalAngularDistance3f = 0.0f;
	FVector3f ScriptConstrainToPlane3f(ForceInitToZero);
	FVector3f ScriptConstrainToDirection3f(ForceInitToZero);

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *VerifyWrapContractsFunction, WrapVerificationResult) &&
		ExecuteValueFunction(*this, Engine, *GetZeroVectorAngularDistanceFunction, ScriptZeroVectorAngularDistance) &&
		ExecuteValueFunction(*this, Engine, *GetNearUnitNormalAngularDistanceFunction, ScriptNearUnitNormalAngularDistance) &&
		ExecuteValueFunction(*this, Engine, *GetConstrainedToPlaneNonUnitFunction, ScriptConstrainToPlane) &&
		ExecuteValueFunction(*this, Engine, *GetConstrainedToDirectionNonUnitFunction, ScriptConstrainToDirection) &&
		ExecuteValueFunction(*this, Engine, *GetZeroVectorAngularDistance3fFunction, ScriptZeroVectorAngularDistance3f) &&
		ExecuteValueFunction(*this, Engine, *GetNearUnitNormalAngularDistance3fFunction, ScriptNearUnitNormalAngularDistance3f) &&
		ExecuteValueFunction(*this, Engine, *GetConstrainedToPlaneNonUnit3fFunction, ScriptConstrainToPlane3f) &&
		ExecuteValueFunction(*this, Engine, *GetConstrainedToDirectionNonUnit3fFunction, ScriptConstrainToDirection3f);
	if (!bExecutedAll)
	{
		return false;
	}

	bPassed &= TestEqual(TEXT("Math::Wrap and Math::WrapIndex boundary helpers should match native baselines"), WrapVerificationResult, 1);

	bPassed &= TestTrue(TEXT("FVector AngularDistance should keep zero-vector input finite"), FMath::IsFinite(ScriptZeroVectorAngularDistance));
	bPassed &= TestTrue(TEXT("FVector AngularDistanceForNormals should keep near-unit input finite"), FMath::IsFinite(ScriptNearUnitNormalAngularDistance));
	bPassed &= TestTrue(TEXT("FVector3f AngularDistance should keep zero-vector input finite"), FMath::IsFinite(ScriptZeroVectorAngularDistance3f));
	bPassed &= TestTrue(TEXT("FVector3f AngularDistanceForNormals should keep near-unit input finite"), FMath::IsFinite(ScriptNearUnitNormalAngularDistance3f));
	bPassed &= TestTrue(TEXT("FVector AngularDistance should match the finite native baseline for zero-vector input"), FMath::IsNearlyEqual(ScriptZeroVectorAngularDistance, ExpectedZeroVectorAngularDistance, KINDA_SMALL_NUMBER));
	bPassed &= TestTrue(TEXT("FVector AngularDistanceForNormals should match the finite native baseline for near-unit input"), FMath::IsNearlyEqual(ScriptNearUnitNormalAngularDistance, ExpectedNearUnitNormalAngularDistance, KINDA_SMALL_NUMBER));
	bPassed &= TestTrue(TEXT("FVector3f AngularDistance should match the finite native baseline for zero-vector input"), FMath::IsNearlyEqual(ScriptZeroVectorAngularDistance3f, ExpectedZeroVectorAngularDistance3f, KINDA_SMALL_NUMBER));
	bPassed &= TestTrue(TEXT("FVector3f AngularDistanceForNormals should match the finite native baseline for near-unit input"), FMath::IsNearlyEqual(ScriptNearUnitNormalAngularDistance3f, ExpectedNearUnitNormalAngularDistance3f, KINDA_SMALL_NUMBER));
	bPassed &= VerifyVector(*this, TEXT("FVector ConstrainToPlane should treat a non-unit normal as a geometric plane normal"), ScriptConstrainToPlane, ExpectedConstrainToPlane);
	bPassed &= VerifyVector(*this, TEXT("FVector ConstrainToDirection should project onto the normalized direction"), ScriptConstrainToDirection, ExpectedConstrainToDirection);
	bPassed &= VerifyVector3f(*this, TEXT("FVector3f ConstrainToPlane should treat a non-unit normal as a geometric plane normal"), ScriptConstrainToPlane3f, ExpectedConstrainToPlane3f);
	bPassed &= VerifyVector3f(*this, TEXT("FVector3f ConstrainToDirection should project onto the normalized direction"), ScriptConstrainToDirection3f, ExpectedConstrainToDirection3f);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
