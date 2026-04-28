#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "FunctionLibraries/AngelscriptMathLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathScalarFunctionLibraryTests_Private
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
}

using namespace AngelscriptTest_Bindings_AngelscriptMathScalarFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathScalarFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.MathScalarParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathScalarFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathScalarFunctionLibraryParity"));
	};

	constexpr float FloatInput = 0.75f;
	constexpr double DoubleInput = 0.75;
	float ExpectedSin32 = 0.0f;
	float ExpectedCos32 = 0.0f;
	double ExpectedSin64 = 0.0;
	double ExpectedCos64 = 0.0;
	FMath::SinCos(&ExpectedSin32, &ExpectedCos32, FloatInput);
	FMath::SinCos(&ExpectedSin64, &ExpectedCos64, DoubleInput);

	float ExpectedIntPart32 = 0.0f;
	double ExpectedIntPart64 = 0.0;
	const float ExpectedFrac32 = FMath::Modf(-5.75f, &ExpectedIntPart32);
	const double ExpectedFrac64 = FMath::Modf(-9.125, &ExpectedIntPart64);

	const double ExpectedWrapDoubleBelow = UAngelscriptMathLibrary::WrapDouble(-3.0, 0.0, 10.0);
	const double ExpectedWrapDoubleAbove = UAngelscriptMathLibrary::WrapDouble(13.0, 0.0, 10.0);
	const float ExpectedWrapFloatBelow = UAngelscriptMathLibrary::WrapFloat(-2.5f, 0.0f, 5.0f);
	const float ExpectedWrapFloatAbove = UAngelscriptMathLibrary::WrapFloat(7.25f, 0.0f, 5.0f);

	const int32 ExpectedWrapIndexNegative = UAngelscriptMathLibrary::WrapIndex(-1, 2, 5);
	const int32 ExpectedWrapIndexSwapped = UAngelscriptMathLibrary::WrapIndex(8, 5, 2);
	const uint32 ExpectedWrapIndexUInt = UAngelscriptMathLibrary::WrapIndexUInt(9u, 2u, 5u);
	const uint32 ExpectedWrapIndexUIntDegenerate = UAngelscriptMathLibrary::WrapIndexUInt(77u, 7u, 7u);

	bPassed &= TestTrue(TEXT("Native SinCos float baseline should produce a unit-circle pair"), FMath::IsNearlyEqual(ExpectedSin32 * ExpectedSin32 + ExpectedCos32 * ExpectedCos32, 1.0f, 0.0001f));
	bPassed &= TestTrue(TEXT("Native SinCos double baseline should produce a unit-circle pair"), FMath::IsNearlyEqual(ExpectedSin64 * ExpectedSin64 + ExpectedCos64 * ExpectedCos64, 1.0, 0.0000001));
	bPassed &= TestTrue(TEXT("Native Modf float baseline should split the negative value into integer and fractional parts"), FMath::IsNearlyEqual(ExpectedFrac32 + ExpectedIntPart32, -5.75f, 0.0001f));
	bPassed &= TestTrue(TEXT("Native Modf double baseline should split the negative value into integer and fractional parts"), FMath::IsNearlyEqual(ExpectedFrac64 + ExpectedIntPart64, -9.125, 0.0000001));
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
int Entry()
{
	float Sin32 = 0.0f;
	float Cos32 = 0.0f;
	Math::SinCos(Sin32, Cos32, 0.75f);
	if (!Math::IsNearlyEqual(Sin32, $EXPECTED_SIN32$, $FLOAT_TOLERANCE$))
		return 10;
	if (!Math::IsNearlyEqual(Cos32, $EXPECTED_COS32$, $FLOAT_TOLERANCE$))
		return 20;

	double Sin64 = 0.0;
	double Cos64 = 0.0;
	Math::SinCos(Sin64, Cos64, 0.75);
	if (!Math::IsNearlyEqual(Sin64, $EXPECTED_SIN64$, $DOUBLE_TOLERANCE$))
		return 30;
	if (!Math::IsNearlyEqual(Cos64, $EXPECTED_COS64$, $DOUBLE_TOLERANCE$))
		return 40;

	float IntPart32 = 0.0f;
	const float Frac32 = Math::Modf(-5.75f, IntPart32);
	if (!Math::IsNearlyEqual(Frac32, $EXPECTED_FRAC32$, $FLOAT_TOLERANCE$))
		return 50;
	if (!Math::IsNearlyEqual(IntPart32, $EXPECTED_INTPART32$, $FLOAT_TOLERANCE$))
		return 60;

	double IntPart64 = 0.0;
	const double Frac64 = Math::Modf(-9.125, IntPart64);
	if (!Math::IsNearlyEqual(Frac64, $EXPECTED_FRAC64$, $DOUBLE_TOLERANCE$))
		return 70;
	if (!Math::IsNearlyEqual(IntPart64, $EXPECTED_INTPART64$, $DOUBLE_TOLERANCE$))
		return 80;

	if (!Math::IsNearlyEqual(Math::Wrap(-3.0, 0.0, 10.0), $EXPECTED_WRAP_DOUBLE_BELOW$, $DOUBLE_TOLERANCE$))
		return 90;
	if (!Math::IsNearlyEqual(Math::Wrap(13.0, 0.0, 10.0), $EXPECTED_WRAP_DOUBLE_ABOVE$, $DOUBLE_TOLERANCE$))
		return 100;
	if (!Math::IsNearlyEqual(Math::Wrap(float(-2.5), float(0.0), float(5.0)), $EXPECTED_WRAP_FLOAT_BELOW$, $FLOAT_TOLERANCE$))
		return 110;
	if (!Math::IsNearlyEqual(Math::Wrap(float(7.25), float(0.0), float(5.0)), $EXPECTED_WRAP_FLOAT_ABOVE$, $FLOAT_TOLERANCE$))
		return 120;

	if (Math::WrapIndex(-1, 2, 5) != $EXPECTED_WRAPINDEX_NEGATIVE$)
		return 130;
	if (Math::WrapIndex(8, 5, 2) != $EXPECTED_WRAPINDEX_SWAPPED$)
		return 140;
	if (Math::WrapIndex(uint32(9), uint32(2), uint32(5)) != $EXPECTED_WRAPINDEX_UINT$)
		return 150;
	if (Math::WrapIndex(uint32(77), uint32(7), uint32(7)) != $EXPECTED_WRAPINDEX_UINT_DEGENERATE$)
		return 160;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("$EXPECTED_SIN32$"), *FormatScriptFloatLiteral(ExpectedSin32));
	Script.ReplaceInline(TEXT("$EXPECTED_COS32$"), *FormatScriptFloatLiteral(ExpectedCos32));
	Script.ReplaceInline(TEXT("$EXPECTED_SIN64$"), *FormatScriptFloatLiteral(ExpectedSin64));
	Script.ReplaceInline(TEXT("$EXPECTED_COS64$"), *FormatScriptFloatLiteral(ExpectedCos64));
	Script.ReplaceInline(TEXT("$EXPECTED_FRAC32$"), *FormatScriptFloatLiteral(ExpectedFrac32));
	Script.ReplaceInline(TEXT("$EXPECTED_INTPART32$"), *FormatScriptFloatLiteral(ExpectedIntPart32));
	Script.ReplaceInline(TEXT("$EXPECTED_FRAC64$"), *FormatScriptFloatLiteral(ExpectedFrac64));
	Script.ReplaceInline(TEXT("$EXPECTED_INTPART64$"), *FormatScriptFloatLiteral(ExpectedIntPart64));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAP_DOUBLE_BELOW$"), *FormatScriptFloatLiteral(ExpectedWrapDoubleBelow));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAP_DOUBLE_ABOVE$"), *FormatScriptFloatLiteral(ExpectedWrapDoubleAbove));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAP_FLOAT_BELOW$"), *FormatScriptFloatLiteral(ExpectedWrapFloatBelow));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAP_FLOAT_ABOVE$"), *FormatScriptFloatLiteral(ExpectedWrapFloatAbove));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAPINDEX_NEGATIVE$"), *FString::FromInt(ExpectedWrapIndexNegative));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAPINDEX_SWAPPED$"), *FString::FromInt(ExpectedWrapIndexSwapped));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAPINDEX_UINT$"), *FormatScriptUInt32Literal(ExpectedWrapIndexUInt));
	Script.ReplaceInline(TEXT("$EXPECTED_WRAPINDEX_UINT_DEGENERATE$"), *FormatScriptUInt32Literal(ExpectedWrapIndexUIntDegenerate));
	Script.ReplaceInline(TEXT("$FLOAT_TOLERANCE$"), *FormatScriptFloatLiteral(0.0001));
	Script.ReplaceInline(TEXT("$DOUBLE_TOLERANCE$"), *FormatScriptFloatLiteral(0.0000001));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASMathScalarFunctionLibraryParity", Script);
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
		TEXT("Scalar Math function library helpers should match the native baseline"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
