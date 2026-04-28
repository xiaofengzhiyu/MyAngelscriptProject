#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Math/Box.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathTrigDecomposeAndBoxIntersectionTests_Private
{
	FString FormatScriptNumericLiteral(const double Value)
	{
		FString Literal = FString::Printf(TEXT("%.17g"), Value);
		if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e")) && !Literal.Contains(TEXT("E")))
		{
			Literal += TEXT(".0");
		}

		return Literal;
	}

	FString FormatScriptBoolLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString FormatScriptVectorLiteral(const FVector& Value)
	{
		return FString::Printf(
			TEXT("FVector(%s, %s, %s)"),
			*FormatScriptNumericLiteral(Value.X),
			*FormatScriptNumericLiteral(Value.Y),
			*FormatScriptNumericLiteral(Value.Z));
	}

	FString FormatScriptBoxLiteral(const FBox& Value)
	{
		return FString::Printf(
			TEXT("FBox(%s, %s)"),
			*FormatScriptVectorLiteral(Value.Min),
			*FormatScriptVectorLiteral(Value.Max));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathTrigDecomposeAndBoxIntersectionTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathTrigDecomposeAndBoxIntersectionTest,
	"Angelscript.TestModule.FunctionLibraries.MathTrigDecomposeAndBoxIntersection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathTrigDecomposeAndBoxIntersectionTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathTrigDecomposeAndBoxIntersection"));
	};

	constexpr float FloatTolerance = 0.0001f;
	constexpr double DoubleTolerance = 0.0000001;
	constexpr float HalfPiFloat = 0.5f * PI;
	constexpr double HalfPiDouble = 0.5 * PI;

	float ExpectedSin32 = 0.0f;
	float ExpectedCos32 = 0.0f;
	double ExpectedSin64 = 0.0;
	double ExpectedCos64 = 0.0;
	FMath::SinCos(&ExpectedSin32, &ExpectedCos32, HalfPiFloat);
	FMath::SinCos(&ExpectedSin64, &ExpectedCos64, HalfPiDouble);

	float ExpectedNegativeIntPart32 = 0.0f;
	const float ExpectedNegativeFrac32 = FMath::Modf(-3.75f, &ExpectedNegativeIntPart32);
	double ExpectedPositiveIntPart64 = 0.0;
	const double ExpectedPositiveFrac64 = FMath::Modf(2.0, &ExpectedPositiveIntPart64);

	const FBox TestBox(FVector(-1.0, -1.0, -1.0), FVector(1.0, 1.0, 1.0));
	const FVector HitStart(-2.0, 0.0, 0.0);
	const FVector HitEnd(2.0, 0.0, 0.0);
	const FVector MissStart(-2.0, 2.0, 0.0);
	const FVector MissEnd(2.0, 2.0, 0.0);
	const FVector InsideStart(0.0, 0.0, 0.0);
	const FVector InsideEnd(2.0, 0.0, 0.0);

	const bool bExpectedLineHit = FMath::LineBoxIntersection(TestBox, HitStart, HitEnd, HitEnd - HitStart);
	const bool bExpectedLineMiss = FMath::LineBoxIntersection(TestBox, MissStart, MissEnd, MissEnd - MissStart);
	const bool bExpectedLineInside = FMath::LineBoxIntersection(TestBox, InsideStart, InsideEnd, InsideEnd - InsideStart);

	bPassed &= TestTrue(TEXT("Native SinCos float baseline should reach the positive Y-axis"), FMath::IsNearlyEqual(ExpectedSin32, 1.0f, FloatTolerance) && FMath::IsNearlyZero(ExpectedCos32, FloatTolerance));
	bPassed &= TestTrue(TEXT("Native SinCos double baseline should reach the positive Y-axis"), FMath::IsNearlyEqual(ExpectedSin64, 1.0, DoubleTolerance) && FMath::IsNearlyZero(ExpectedCos64, DoubleTolerance));
	bPassed &= TestTrue(TEXT("Native Modf float baseline should split -3.75 into -0.75 and -3.0"), FMath::IsNearlyEqual(ExpectedNegativeFrac32, -0.75f, FloatTolerance) && FMath::IsNearlyEqual(ExpectedNegativeIntPart32, -3.0f, FloatTolerance));
	bPassed &= TestTrue(TEXT("Native Modf double baseline should split 2.0 into 0.0 and 2.0"), FMath::IsNearlyZero(ExpectedPositiveFrac64, DoubleTolerance) && FMath::IsNearlyEqual(ExpectedPositiveIntPart64, 2.0, DoubleTolerance));
	bPassed &= TestTrue(TEXT("Native LineBoxIntersection baseline should report a hit for the through-box segment"), bExpectedLineHit);
	bPassed &= TestFalse(TEXT("Native LineBoxIntersection baseline should report a miss for the offset segment"), bExpectedLineMiss);
	bPassed &= TestTrue(TEXT("Native LineBoxIntersection baseline should report a hit when the segment starts inside the box"), bExpectedLineInside);
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
int Entry()
{
	float Sin32 = 0.0f;
	float Cos32 = 0.0f;
	Math::SinCos(Sin32, Cos32, float($HALF_PI_FLOAT$));
	if (!Math::IsNearlyEqual(Sin32, $EXPECTED_SIN32$, $FLOAT_TOLERANCE$))
		return 10;
	if (!Math::IsNearlyEqual(Cos32, $EXPECTED_COS32$, $FLOAT_TOLERANCE$))
		return 20;

	double Sin64 = 0.0;
	double Cos64 = 0.0;
	Math::SinCos(Sin64, Cos64, $HALF_PI_DOUBLE$);
	if (!Math::IsNearlyEqual(Sin64, $EXPECTED_SIN64$, $DOUBLE_TOLERANCE$))
		return 30;
	if (!Math::IsNearlyEqual(Cos64, $EXPECTED_COS64$, $DOUBLE_TOLERANCE$))
		return 40;

	float NegativeIntPart32 = 0.0f;
	const float NegativeFrac32 = Math::Modf(-3.75f, NegativeIntPart32);
	if (!Math::IsNearlyEqual(NegativeFrac32, $EXPECTED_NEGATIVE_FRAC32$, $FLOAT_TOLERANCE$))
		return 50;
	if (!Math::IsNearlyEqual(NegativeIntPart32, $EXPECTED_NEGATIVE_INTPART32$, $FLOAT_TOLERANCE$))
		return 60;

	double PositiveIntPart64 = 0.0;
	const double PositiveFrac64 = Math::Modf(2.0, PositiveIntPart64);
	if (!Math::IsNearlyEqual(PositiveFrac64, $EXPECTED_POSITIVE_FRAC64$, $DOUBLE_TOLERANCE$))
		return 70;
	if (!Math::IsNearlyEqual(PositiveIntPart64, $EXPECTED_POSITIVE_INTPART64$, $DOUBLE_TOLERANCE$))
		return 80;

	const FBox TestBox = $BOX$;
	if (Math::LineBoxIntersection(TestBox, $HIT_START$, $HIT_END$, $HIT_DELTA$) != $EXPECTED_LINE_HIT$)
		return 90;
	if (Math::LineBoxIntersection(TestBox, $MISS_START$, $MISS_END$, $MISS_DELTA$) != $EXPECTED_LINE_MISS$)
		return 100;
	if (Math::LineBoxIntersection(TestBox, $INSIDE_START$, $INSIDE_END$, $INSIDE_DELTA$) != $EXPECTED_LINE_INSIDE$)
		return 110;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("$HALF_PI_FLOAT$"), *FormatScriptNumericLiteral(HalfPiFloat));
	Script.ReplaceInline(TEXT("$HALF_PI_DOUBLE$"), *FormatScriptNumericLiteral(HalfPiDouble));
	Script.ReplaceInline(TEXT("$EXPECTED_SIN32$"), *FormatScriptNumericLiteral(ExpectedSin32));
	Script.ReplaceInline(TEXT("$EXPECTED_COS32$"), *FormatScriptNumericLiteral(ExpectedCos32));
	Script.ReplaceInline(TEXT("$EXPECTED_SIN64$"), *FormatScriptNumericLiteral(ExpectedSin64));
	Script.ReplaceInline(TEXT("$EXPECTED_COS64$"), *FormatScriptNumericLiteral(ExpectedCos64));
	Script.ReplaceInline(TEXT("$EXPECTED_NEGATIVE_FRAC32$"), *FormatScriptNumericLiteral(ExpectedNegativeFrac32));
	Script.ReplaceInline(TEXT("$EXPECTED_NEGATIVE_INTPART32$"), *FormatScriptNumericLiteral(ExpectedNegativeIntPart32));
	Script.ReplaceInline(TEXT("$EXPECTED_POSITIVE_FRAC64$"), *FormatScriptNumericLiteral(ExpectedPositiveFrac64));
	Script.ReplaceInline(TEXT("$EXPECTED_POSITIVE_INTPART64$"), *FormatScriptNumericLiteral(ExpectedPositiveIntPart64));
	Script.ReplaceInline(TEXT("$FLOAT_TOLERANCE$"), *FormatScriptNumericLiteral(FloatTolerance));
	Script.ReplaceInline(TEXT("$DOUBLE_TOLERANCE$"), *FormatScriptNumericLiteral(DoubleTolerance));
	Script.ReplaceInline(TEXT("$BOX$"), *FormatScriptBoxLiteral(TestBox));
	Script.ReplaceInline(TEXT("$HIT_START$"), *FormatScriptVectorLiteral(HitStart));
	Script.ReplaceInline(TEXT("$HIT_END$"), *FormatScriptVectorLiteral(HitEnd));
	Script.ReplaceInline(TEXT("$HIT_DELTA$"), *FormatScriptVectorLiteral(HitEnd - HitStart));
	Script.ReplaceInline(TEXT("$MISS_START$"), *FormatScriptVectorLiteral(MissStart));
	Script.ReplaceInline(TEXT("$MISS_END$"), *FormatScriptVectorLiteral(MissEnd));
	Script.ReplaceInline(TEXT("$MISS_DELTA$"), *FormatScriptVectorLiteral(MissEnd - MissStart));
	Script.ReplaceInline(TEXT("$INSIDE_START$"), *FormatScriptVectorLiteral(InsideStart));
	Script.ReplaceInline(TEXT("$INSIDE_END$"), *FormatScriptVectorLiteral(InsideEnd));
	Script.ReplaceInline(TEXT("$INSIDE_DELTA$"), *FormatScriptVectorLiteral(InsideEnd - InsideStart));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_HIT$"), *FormatScriptBoolLiteral(bExpectedLineHit));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_MISS$"), *FormatScriptBoolLiteral(bExpectedLineMiss));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_INSIDE$"), *FormatScriptBoolLiteral(bExpectedLineInside));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASMathTrigDecomposeAndBoxIntersection", Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Math trig/decompose and box intersection helpers should match native baselines"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
