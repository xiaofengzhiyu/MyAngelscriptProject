#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTransform3fBindingsTest,
	"Angelscript.TestModule.Bindings.Transform3fCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptTransform3fBindingsTests_Private
{
	FString FormatScriptFloatLiteral(const double Value)
	{
		FString Literal = FString::Printf(TEXT("%.9g"), Value);
		if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e")) && !Literal.Contains(TEXT("E")))
		{
			Literal += TEXT(".0");
		}

		return Literal;
	}

	FString FormatScriptVector3fLiteral(const FVector3f& Value)
	{
		return FString::Printf(
			TEXT("FVector3f(%s, %s, %s)"),
			*FormatScriptFloatLiteral(Value.X),
			*FormatScriptFloatLiteral(Value.Y),
			*FormatScriptFloatLiteral(Value.Z));
	}

	FString FormatScriptRotator3fLiteral(const FRotator3f& Value)
	{
		return FString::Printf(
			TEXT("FRotator3f(%s, %s, %s)"),
			*FormatScriptFloatLiteral(Value.Pitch),
			*FormatScriptFloatLiteral(Value.Yaw),
			*FormatScriptFloatLiteral(Value.Roll));
	}

	template <typename TValue>
	bool ReadReturnValue(FAutomationTestBase& Test, asIScriptContext& Context, TValue& OutValue)
	{
		void* ReturnValueAddress = Context.GetAddressOfReturnValue();
		if (!Test.TestNotNull(TEXT("Transform3f bindings test should expose the return value storage"), ReturnValueAddress))
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
		if (!Test.TestNotNull(TEXT("Transform3f bindings test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Transform3f bindings test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Transform3f bindings test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Transform3f bindings test saw a script exception: %s"),
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

	bool VerifyVector3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FVector3f& Actual,
		const FVector3f& Expected,
		float Tolerance = KINDA_SMALL_NUMBER)
	{
		if (!Actual.Equals(Expected, Tolerance))
		{
			Test.AddInfo(FString::Printf(
				TEXT("%s actual=(%.6f, %.6f, %.6f) expected=(%.6f, %.6f, %.6f) tolerance=%.6f"),
				What,
				Actual.X,
				Actual.Y,
				Actual.Z,
				Expected.X,
				Expected.Y,
				Expected.Z,
				Tolerance));
		}

		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}

	bool VerifyTransform3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FTransform3f& Actual,
		const FTransform3f& Expected,
		float Tolerance = 0.001f)
	{
		const double RotationToleranceDegrees = FMath::Max(0.05, static_cast<double>(Tolerance));
		const bool bRotationMatches = QuatMatches(FQuat(Actual.GetRotation()), FQuat(Expected.GetRotation()), RotationToleranceDegrees);
		const bool bTranslationMatches = Actual.GetTranslation().Equals(Expected.GetTranslation(), Tolerance);
		const bool bScaleMatches = Actual.GetScale3D().Equals(Expected.GetScale3D(), Tolerance);
		if (!(bRotationMatches && bTranslationMatches && bScaleMatches))
		{
			Test.AddInfo(FString::Printf(
				TEXT("%s actual rotation=%s expected rotation=%s actual translation=(%.6f, %.6f, %.6f) expected translation=(%.6f, %.6f, %.6f) actual scale=(%.6f, %.6f, %.6f) expected scale=(%.6f, %.6f, %.6f)"),
				What,
				*FQuat(Actual.GetRotation()).Rotator().ToCompactString(),
				*FQuat(Expected.GetRotation()).Rotator().ToCompactString(),
				Actual.GetTranslation().X,
				Actual.GetTranslation().Y,
				Actual.GetTranslation().Z,
				Expected.GetTranslation().X,
				Expected.GetTranslation().Y,
				Expected.GetTranslation().Z,
				Actual.GetScale3D().X,
				Actual.GetScale3D().Y,
				Actual.GetScale3D().Z,
				Expected.GetScale3D().X,
				Expected.GetScale3D().Y,
				Expected.GetScale3D().Z));
		}

		return Test.TestTrue(What, bRotationMatches && bTranslationMatches && bScaleMatches);
	}

	bool VerifyTransform(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FTransform& Actual,
		const FTransform& Expected,
		double Tolerance = 0.001)
	{
		const double RotationToleranceDegrees = FMath::Max(0.05, Tolerance);
		const bool bRotationMatches = QuatMatches(Actual.GetRotation(), Expected.GetRotation(), RotationToleranceDegrees);
		const bool bTranslationMatches = Actual.GetTranslation().Equals(Expected.GetTranslation(), Tolerance);
		const bool bScaleMatches = Actual.GetScale3D().Equals(Expected.GetScale3D(), Tolerance);
		return Test.TestTrue(What, bRotationMatches && bTranslationMatches && bScaleMatches);
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptTransform3fBindingsTests_Private;

bool FAngelscriptTransform3fBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	constexpr float Tolerance = 0.001f;
	constexpr float BlendAlpha = 0.35f;
	constexpr float BlendWithAlpha = 0.5f;

	const FRotator3f BaselineRotator(12.5f, 47.0f, -18.0f);
	const FVector3f BaselineTranslation(10.0f, -20.0f, 30.0f);
	const FVector3f BaselineScale(1.5f, 2.0f, 0.75f);
	const FTransform3f Baseline(BaselineRotator, BaselineTranslation, BaselineScale);

	const FRotator3f OtherRotator(-22.0f, 91.0f, 13.0f);
	const FVector3f OtherTranslation(-5.0f, 7.0f, 9.0f);
	const FVector3f OtherScale(0.5f, 1.25f, 2.0f);
	const FTransform3f Other(FQuat4f(OtherRotator), OtherTranslation, OtherScale);

	const FVector3f InputPosition(2.0f, -1.0f, 0.5f);
	const FVector3f UpdatedTranslation(-11.0f, 12.0f, 13.0f);
	const FVector3f UpdatedScale(8.0f, 9.0f, 10.0f);

	const FVector3f ExpectedTransformPosition = Baseline.TransformPosition(InputPosition);
	const FVector3f ExpectedRecoveredPosition = Baseline.InverseTransformPosition(ExpectedTransformPosition);
	const FTransform3f ExpectedInverse = Baseline.Inverse();
	const FTransform3f ExpectedComposed = Baseline * Other;
	FTransform3f ExpectedBlend;
	ExpectedBlend.Blend(Baseline, Other, BlendAlpha);
	FTransform3f ExpectedBlendWith = Baseline;
	ExpectedBlendWith.BlendWith(Other, BlendWithAlpha);
	const FTransform ExpectedConverted = FTransform(Baseline);
	const FTransform3f ExpectedRoundTripped = FTransform3f(ExpectedConverted);
	FTransform3f ExpectedUpdated = Baseline;
	ExpectedUpdated.SetTranslation(UpdatedTranslation);
	ExpectedUpdated.SetScale3D(UpdatedScale);

	bPassed &= TestTrue(
		TEXT("Native FTransform3f baseline should round-trip a transformed position through InverseTransformPosition"),
		ExpectedRecoveredPosition.Equals(InputPosition, Tolerance));
	bPassed &= TestTrue(
		TEXT("Native FTransform3f blend baseline should differ from the original transform"),
		!ExpectedBlend.Equals(Baseline, 0.0f));
	bPassed &= TestTrue(
		TEXT("Native FTransform3f round-trip through FTransform should remain stable"),
		ExpectedRoundTripped.Equals(Baseline, Tolerance));
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"AS(
FTransform3f MakeBaseline()
{
	return FTransform3f($BASELINE_ROTATOR$, $BASELINE_TRANSLATION$, $BASELINE_SCALE$);
}

FTransform3f MakeOther()
{
	return FTransform3f(FQuat4f($OTHER_ROTATOR$), $OTHER_TRANSLATION$, $OTHER_SCALE$);
}

FTransform3f GetIdentityTransform()
{
	return FTransform3f::Identity;
}

FVector3f GetTransformedPosition()
{
	return MakeBaseline().TransformPosition($INPUT_POSITION$);
}

FVector3f GetRecoveredPosition()
{
	return MakeBaseline().InverseTransformPosition(GetTransformedPosition());
}

FTransform3f GetInverseTransform()
{
	return MakeBaseline().Inverse();
}

FTransform3f GetComposedTransform()
{
	return MakeBaseline() * MakeOther();
}

FTransform3f GetBlendTransform()
{
	FTransform3f Result;
	Result.Blend(MakeBaseline(), MakeOther(), $BLEND_ALPHA$);
	return Result;
}

FTransform3f GetBlendWithTransform()
{
	FTransform3f Result = MakeBaseline();
	Result.BlendWith(MakeOther(), $BLEND_WITH_ALPHA$);
	return Result;
}

FTransform GetConvertedTransform()
{
	return FTransform(MakeBaseline());
}

FTransform3f GetRoundTrippedTransform()
{
	return FTransform3f(GetConvertedTransform());
}

FTransform3f GetUpdatedTransform()
{
	FTransform3f Result = MakeBaseline();
	Result.SetTranslation($UPDATED_TRANSLATION$);
	Result.SetScale3D($UPDATED_SCALE$);
	return Result;
}

bool IdentityMatchesStaticValue()
{
	return GetIdentityTransform().Equals(FTransform3f::Identity, $TOLERANCE$);
}

bool RoundTripPreservesBaseline()
{
	return GetRoundTrippedTransform().Equals(MakeBaseline(), $TOLERANCE$);
}

bool UpdatedTransformDiffersFromBaseline()
{
	return !GetUpdatedTransform().Equals(MakeBaseline(), $TOLERANCE$);
}
)AS");

	Script.ReplaceInline(TEXT("$BASELINE_ROTATOR$"), *FormatScriptRotator3fLiteral(BaselineRotator));
	Script.ReplaceInline(TEXT("$BASELINE_TRANSLATION$"), *FormatScriptVector3fLiteral(BaselineTranslation));
	Script.ReplaceInline(TEXT("$BASELINE_SCALE$"), *FormatScriptVector3fLiteral(BaselineScale));
	Script.ReplaceInline(TEXT("$OTHER_ROTATOR$"), *FormatScriptRotator3fLiteral(OtherRotator));
	Script.ReplaceInline(TEXT("$OTHER_TRANSLATION$"), *FormatScriptVector3fLiteral(OtherTranslation));
	Script.ReplaceInline(TEXT("$OTHER_SCALE$"), *FormatScriptVector3fLiteral(OtherScale));
	Script.ReplaceInline(TEXT("$INPUT_POSITION$"), *FormatScriptVector3fLiteral(InputPosition));
	Script.ReplaceInline(TEXT("$UPDATED_TRANSLATION$"), *FormatScriptVector3fLiteral(UpdatedTranslation));
	Script.ReplaceInline(TEXT("$UPDATED_SCALE$"), *FormatScriptVector3fLiteral(UpdatedScale));
	Script.ReplaceInline(TEXT("$BLEND_ALPHA$"), *FormatScriptFloatLiteral(BlendAlpha));
	Script.ReplaceInline(TEXT("$BLEND_WITH_ALPHA$"), *FormatScriptFloatLiteral(BlendWithAlpha));
	Script.ReplaceInline(TEXT("$TOLERANCE$"), *FormatScriptFloatLiteral(Tolerance));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASTransform3fCompat", Script);
	if (Module == nullptr)
	{
		return false;
	}

	FTransform3f ScriptIdentity = FTransform3f::Identity;
	FVector3f ScriptTransformPosition(ForceInitToZero);
	FVector3f ScriptRecoveredPosition(ForceInitToZero);
	FTransform3f ScriptInverse = FTransform3f::Identity;
	FTransform3f ScriptComposed = FTransform3f::Identity;
	FTransform3f ScriptBlend = FTransform3f::Identity;
	FTransform3f ScriptBlendWith = FTransform3f::Identity;
	FTransform ScriptConverted = FTransform::Identity;
	FTransform3f ScriptRoundTripped = FTransform3f::Identity;
	FTransform3f ScriptUpdated = FTransform3f::Identity;
	bool bIdentityMatchesStaticValue = false;
	bool bRoundTripPreservesBaseline = false;
	bool bUpdatedTransformDiffersFromBaseline = false;

	asIScriptFunction* IdentityFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetIdentityTransform()"));
	asIScriptFunction* TransformPositionFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetTransformedPosition()"));
	asIScriptFunction* RecoveredPositionFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetRecoveredPosition()"));
	asIScriptFunction* InverseFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetInverseTransform()"));
	asIScriptFunction* ComposedFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetComposedTransform()"));
	asIScriptFunction* BlendFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetBlendTransform()"));
	asIScriptFunction* BlendWithFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetBlendWithTransform()"));
	asIScriptFunction* ConvertedFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform GetConvertedTransform()"));
	asIScriptFunction* RoundTrippedFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetRoundTrippedTransform()"));
	asIScriptFunction* UpdatedFunction = GetFunctionByDecl(*this, *Module, TEXT("FTransform3f GetUpdatedTransform()"));
	asIScriptFunction* IdentityMatchesFunction = GetFunctionByDecl(*this, *Module, TEXT("bool IdentityMatchesStaticValue()"));
	asIScriptFunction* RoundTripMatchesFunction = GetFunctionByDecl(*this, *Module, TEXT("bool RoundTripPreservesBaseline()"));
	asIScriptFunction* UpdatedDiffersFunction = GetFunctionByDecl(*this, *Module, TEXT("bool UpdatedTransformDiffersFromBaseline()"));
	if (IdentityFunction == nullptr
		|| TransformPositionFunction == nullptr
		|| RecoveredPositionFunction == nullptr
		|| InverseFunction == nullptr
		|| ComposedFunction == nullptr
		|| BlendFunction == nullptr
		|| BlendWithFunction == nullptr
		|| ConvertedFunction == nullptr
		|| RoundTrippedFunction == nullptr
		|| UpdatedFunction == nullptr
		|| IdentityMatchesFunction == nullptr
		|| RoundTripMatchesFunction == nullptr
		|| UpdatedDiffersFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *IdentityFunction, ScriptIdentity) &&
		ExecuteValueFunction(*this, Engine, *TransformPositionFunction, ScriptTransformPosition) &&
		ExecuteValueFunction(*this, Engine, *RecoveredPositionFunction, ScriptRecoveredPosition) &&
		ExecuteValueFunction(*this, Engine, *InverseFunction, ScriptInverse) &&
		ExecuteValueFunction(*this, Engine, *ComposedFunction, ScriptComposed) &&
		ExecuteValueFunction(*this, Engine, *BlendFunction, ScriptBlend) &&
		ExecuteValueFunction(*this, Engine, *BlendWithFunction, ScriptBlendWith) &&
		ExecuteValueFunction(*this, Engine, *ConvertedFunction, ScriptConverted) &&
		ExecuteValueFunction(*this, Engine, *RoundTrippedFunction, ScriptRoundTripped) &&
		ExecuteValueFunction(*this, Engine, *UpdatedFunction, ScriptUpdated) &&
		ExecuteValueFunction(*this, Engine, *IdentityMatchesFunction, bIdentityMatchesStaticValue) &&
		ExecuteValueFunction(*this, Engine, *RoundTripMatchesFunction, bRoundTripPreservesBaseline) &&
		ExecuteValueFunction(*this, Engine, *UpdatedDiffersFunction, bUpdatedTransformDiffersFromBaseline);
	if (!bExecutedAll)
	{
		return false;
	}

	bPassed &= VerifyTransform3f(
		*this,
		TEXT("FTransform3f::Identity should round-trip through the static binding"),
		ScriptIdentity,
		FTransform3f::Identity,
		Tolerance);
	bPassed &= VerifyVector3f(
		*this,
		TEXT("FTransform3f::TransformPosition should match the native deterministic baseline"),
		ScriptTransformPosition,
		ExpectedTransformPosition,
		Tolerance);
	bPassed &= VerifyVector3f(
		*this,
		TEXT("FTransform3f::InverseTransformPosition should recover the original local point"),
		ScriptRecoveredPosition,
		ExpectedRecoveredPosition,
		Tolerance);
	bPassed &= VerifyTransform3f(
		*this,
		TEXT("FTransform3f::Inverse should match the native inverse transform"),
		ScriptInverse,
		ExpectedInverse,
		Tolerance);
	bPassed &= VerifyTransform3f(
		*this,
		TEXT("FTransform3f::operator* should match native transform composition"),
		ScriptComposed,
		ExpectedComposed,
		Tolerance);
	bPassed &= VerifyTransform3f(
		*this,
		TEXT("FTransform3f::Blend should match native transform blending"),
		ScriptBlend,
		ExpectedBlend,
		Tolerance);
	bPassed &= VerifyTransform3f(
		*this,
		TEXT("FTransform3f::BlendWith should match native in-place transform blending"),
		ScriptBlendWith,
		ExpectedBlendWith,
		Tolerance);
	bPassed &= VerifyTransform(
		*this,
		TEXT("FTransform(FTransform3f) should preserve the native transform components"),
		ScriptConverted,
		ExpectedConverted,
		Tolerance);
	bPassed &= VerifyTransform3f(
		*this,
		TEXT("FTransform3f(FTransform) should preserve the native transform after round-trip conversion"),
		ScriptRoundTripped,
		ExpectedRoundTripped,
		Tolerance);
	bPassed &= VerifyTransform3f(
		*this,
		TEXT("FTransform3f setters should preserve the updated translation and scale"),
		ScriptUpdated,
		ExpectedUpdated,
		Tolerance);
	bPassed &= VerifyVector3f(
		*this,
		TEXT("FTransform3f::GetLocation should return the updated translation after SetTranslation"),
		ScriptUpdated.GetLocation(),
		UpdatedTranslation,
		0.0f);
	bPassed &= VerifyVector3f(
		*this,
		TEXT("FTransform3f::GetScale3D should return the updated scale after SetScale3D"),
		ScriptUpdated.GetScale3D(),
		UpdatedScale,
		0.0f);
	bPassed &= TestTrue(
		TEXT("FTransform3f::Equals should recognize the static identity binding"),
		bIdentityMatchesStaticValue);
	bPassed &= TestTrue(
		TEXT("FTransform3f <-> FTransform conversion should preserve the deterministic baseline"),
		bRoundTripPreservesBaseline);
	bPassed &= TestTrue(
		TEXT("FTransform3f::Equals should report that the setter-mutated transform differs from the baseline"),
		bUpdatedTransformDiffersFromBaseline);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
