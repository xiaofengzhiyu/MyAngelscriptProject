#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathRangePredicateBindingsTests_Private
{
	static constexpr ANSICHAR MathRangePredicateModuleName[] = "ASMathRangePredicateCompat";

	FString FormatScriptFloatLiteral(const double Value)
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
}

using namespace AngelscriptTest_Bindings_AngelscriptMathRangePredicateBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathRangePredicateBindingsTest,
	"Angelscript.TestModule.Bindings.MathRangePredicateCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathRangePredicateBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathRangePredicateCompat"));
	};

	const float ExpectedSmoothStep32 = FMath::SmoothStep(0.0f, 10.0f, 7.5f);
	const double ExpectedSmoothStep64 = FMath::SmoothStep(0.0, 10.0, 2.5);
	const float ExpectedFastAsin32 = FMath::FastAsin(0.25f);
	const double ExpectedFastAsin64 = FMath::FastAsin(0.25);
	const bool bExpectedIsWithinIntInside = FMath::IsWithin<int32, int32>(5, 0, 10);
	const bool bExpectedIsWithinIntLowerBoundary = FMath::IsWithin<int32, int32>(0, 0, 10);
	const bool bExpectedIsWithinIntUpperBoundary = FMath::IsWithin<int32, int32>(10, 0, 10);
	const bool bExpectedIsWithinFloatUpperBoundary = FMath::IsWithin<float, float>(1.0f, 0.0f, 1.0f);
	const bool bExpectedIsWithinDoubleUpperBoundary = FMath::IsWithin<double, double>(1.0, 0.0, 1.0);
	const bool bExpectedIsWithinInclusiveIntLowerBoundary = FMath::IsWithinInclusive<int32, int32>(0, 0, 10);
	const bool bExpectedIsWithinInclusiveIntUpperBoundary = FMath::IsWithinInclusive<int32, int32>(10, 0, 10);
	const bool bExpectedIsWithinInclusiveFloatUpperBoundary = FMath::IsWithinInclusive<float, float>(1.0f, 0.0f, 1.0f);
	const bool bExpectedIsWithinInclusiveDoubleLowerBoundary = FMath::IsWithinInclusive<double, double>(0.0, 0.0, 1.0);

	bPassed &= TestTrue(
		TEXT("Native SmoothStep float baseline should stay inside the unit interval for an in-range input"),
		ExpectedSmoothStep32 > 0.0f && ExpectedSmoothStep32 < 1.0f);
	bPassed &= TestTrue(
		TEXT("Native SmoothStep double baseline should stay inside the unit interval for an in-range input"),
		ExpectedSmoothStep64 > 0.0 && ExpectedSmoothStep64 < 1.0);
	bPassed &= TestTrue(
		TEXT("Native IsWithin int baseline should accept an interior integer"),
		bExpectedIsWithinIntInside);
	bPassed &= TestTrue(
		TEXT("Native IsWithin int baseline should include the lower boundary"),
		bExpectedIsWithinIntLowerBoundary);
	bPassed &= TestTrue(
		TEXT("Native IsWithin int baseline should exclude the upper boundary"),
		!bExpectedIsWithinIntUpperBoundary);
	bPassed &= TestTrue(
		TEXT("Native IsWithin float baseline should exclude the upper boundary"),
		!bExpectedIsWithinFloatUpperBoundary);
	bPassed &= TestTrue(
		TEXT("Native IsWithin double baseline should exclude the upper boundary"),
		!bExpectedIsWithinDoubleUpperBoundary);
	bPassed &= TestTrue(
		TEXT("Native IsWithinInclusive int baseline should include the lower boundary"),
		bExpectedIsWithinInclusiveIntLowerBoundary);
	bPassed &= TestTrue(
		TEXT("Native IsWithinInclusive int baseline should include the upper boundary"),
		bExpectedIsWithinInclusiveIntUpperBoundary);
	bPassed &= TestTrue(
		TEXT("Native IsWithinInclusive float baseline should include the upper boundary"),
		bExpectedIsWithinInclusiveFloatUpperBoundary);
	bPassed &= TestTrue(
		TEXT("Native IsWithinInclusive double baseline should include the lower boundary"),
		bExpectedIsWithinInclusiveDoubleLowerBoundary);
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
int Entry()
{
	if (!Math::IsNearlyEqual(Math::SmoothStep(0.0f, 10.0f, 7.5f), $EXPECTED_SMOOTHSTEP32$, $FLOAT_TOLERANCE$))
		return 10;

	if (!Math::IsNearlyEqual(Math::SmoothStep(0.0, 10.0, 2.5), $EXPECTED_SMOOTHSTEP64$, $DOUBLE_TOLERANCE$))
		return 20;

	if (!Math::IsNearlyEqual(Math::FastAsin(0.25f), $EXPECTED_FASTASIN32$, $FLOAT_TOLERANCE$))
		return 30;

	if (!Math::IsNearlyEqual(Math::FastAsin(0.25), $EXPECTED_FASTASIN64$, $DOUBLE_TOLERANCE$))
		return 40;

	if (Math::IsWithin(5, 0, 10) != $EXPECTED_ISWITHIN_INT_INSIDE$)
		return 50;

	if (Math::IsWithin(0, 0, 10) != $EXPECTED_ISWITHIN_INT_LOWER_BOUNDARY$)
		return 60;

	if (Math::IsWithin(10, 0, 10) != $EXPECTED_ISWITHIN_INT_UPPER_BOUNDARY$)
		return 70;

	if (Math::IsWithin(1.0f, 0.0f, 1.0f) != $EXPECTED_ISWITHIN_FLOAT_UPPER_BOUNDARY$)
		return 80;

	if (Math::IsWithin(1.0, 0.0, 1.0) != $EXPECTED_ISWITHIN_DOUBLE_UPPER_BOUNDARY$)
		return 90;

	if (Math::IsWithinInclusive(0, 0, 10) != $EXPECTED_ISWITHIN_INCLUSIVE_INT_LOWER_BOUNDARY$)
		return 100;

	if (Math::IsWithinInclusive(10, 0, 10) != $EXPECTED_ISWITHIN_INCLUSIVE_INT_UPPER_BOUNDARY$)
		return 110;

	if (Math::IsWithinInclusive(1.0f, 0.0f, 1.0f) != $EXPECTED_ISWITHIN_INCLUSIVE_FLOAT_UPPER_BOUNDARY$)
		return 120;

	if (Math::IsWithinInclusive(0.0, 0.0, 1.0) != $EXPECTED_ISWITHIN_INCLUSIVE_DOUBLE_LOWER_BOUNDARY$)
		return 130;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("$EXPECTED_SMOOTHSTEP32$"), *FormatScriptFloatLiteral(ExpectedSmoothStep32));
	Script.ReplaceInline(TEXT("$EXPECTED_SMOOTHSTEP64$"), *FormatScriptFloatLiteral(ExpectedSmoothStep64));
	Script.ReplaceInline(TEXT("$EXPECTED_FASTASIN32$"), *FormatScriptFloatLiteral(ExpectedFastAsin32));
	Script.ReplaceInline(TEXT("$EXPECTED_FASTASIN64$"), *FormatScriptFloatLiteral(ExpectedFastAsin64));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_INT_INSIDE$"), *FormatScriptBoolLiteral(bExpectedIsWithinIntInside));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_INT_LOWER_BOUNDARY$"), *FormatScriptBoolLiteral(bExpectedIsWithinIntLowerBoundary));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_INT_UPPER_BOUNDARY$"), *FormatScriptBoolLiteral(bExpectedIsWithinIntUpperBoundary));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_FLOAT_UPPER_BOUNDARY$"), *FormatScriptBoolLiteral(bExpectedIsWithinFloatUpperBoundary));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_DOUBLE_UPPER_BOUNDARY$"), *FormatScriptBoolLiteral(bExpectedIsWithinDoubleUpperBoundary));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_INCLUSIVE_INT_LOWER_BOUNDARY$"), *FormatScriptBoolLiteral(bExpectedIsWithinInclusiveIntLowerBoundary));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_INCLUSIVE_INT_UPPER_BOUNDARY$"), *FormatScriptBoolLiteral(bExpectedIsWithinInclusiveIntUpperBoundary));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_INCLUSIVE_FLOAT_UPPER_BOUNDARY$"), *FormatScriptBoolLiteral(bExpectedIsWithinInclusiveFloatUpperBoundary));
	Script.ReplaceInline(TEXT("$EXPECTED_ISWITHIN_INCLUSIVE_DOUBLE_LOWER_BOUNDARY$"), *FormatScriptBoolLiteral(bExpectedIsWithinInclusiveDoubleLowerBoundary));
	Script.ReplaceInline(TEXT("$FLOAT_TOLERANCE$"), *FormatScriptFloatLiteral(0.0001));
	Script.ReplaceInline(TEXT("$DOUBLE_TOLERANCE$"), *FormatScriptFloatLiteral(0.0000001));

	asIScriptModule* Module = BuildModule(*this, Engine, MathRangePredicateModuleName, Script);
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
		TEXT("Math range and predicate bindings should match the native baseline"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
