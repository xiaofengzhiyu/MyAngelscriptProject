#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptLinearColorBindingsTest,
	"Angelscript.TestModule.Bindings.ColorAndText.LinearColorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptColorBindingsTest,
	"Angelscript.TestModule.Bindings.ColorAndText.ColorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTextBindingsTest,
	"Angelscript.TestModule.Bindings.ColorAndText.TextCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptColorAndTextBindingsTests_Private
{
	static constexpr ANSICHAR LinearColorModuleName[] = "ASLinearColorBindings";
	static constexpr ANSICHAR ColorModuleName[] = "ASColorBindings";
	static constexpr ANSICHAR TextModuleName[] = "ASTextBindings";

	struct FTextBindingExpectations
	{
		FString JoinedText;
		FString JoinedArguments;
		FString Parameter0;
		FString Parameter1;
	};

	FString FormatScriptFloatLiteral(const double Value)
	{
		FString Literal = FString::Printf(TEXT("%.9g"), Value);
		if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e")) && !Literal.Contains(TEXT("E")))
		{
			Literal += TEXT(".0");
		}

		return Literal;
	}

	FString FormatScriptLinearColorLiteral(const FLinearColor& Value)
	{
		return FString::Printf(
			TEXT("FLinearColor(%s, %s, %s, %s)"),
			*FormatScriptFloatLiteral(Value.R),
			*FormatScriptFloatLiteral(Value.G),
			*FormatScriptFloatLiteral(Value.B),
			*FormatScriptFloatLiteral(Value.A));
	}

	FString FormatScriptColorLiteral(const FColor& Value)
	{
		return FString::Printf(
			TEXT("FColor(%u, %u, %u, %u)"),
			static_cast<uint32>(Value.R),
			static_cast<uint32>(Value.G),
			static_cast<uint32>(Value.B),
			static_cast<uint32>(Value.A));
	}

	FTextBindingExpectations BuildTextBindingExpectations()
	{
		FTextBindingExpectations Expectations;

		TArray<FText> TextParts;
		TextParts.Add(FText::FromString(TEXT("One")));
		TextParts.Add(FText::AsCultureInvariant(TEXT("Two")));
		Expectations.JoinedText = FText::Join(FText::FromString(TEXT("|")), TextParts).ToString().ReplaceCharWithEscapedChar();

		TArray<FFormatArgumentValue> FormatParts;
		FormatParts.Add(FFormatArgumentValue(FText::FromString(TEXT("Alpha"))));
		FormatParts.Add(FFormatArgumentValue(int32(7)));
		Expectations.JoinedArguments = FText::Join(FText::FromString(TEXT(",")), FormatParts).ToString().ReplaceCharWithEscapedChar();

		TArray<FString> ParameterNames;
		FText::GetFormatPatternParameters(FText::FromString(TEXT("{Count}:{Name}:{Count}")), ParameterNames);
		if (ParameterNames.Num() >= 2)
		{
			Expectations.Parameter0 = ParameterNames[0].ReplaceCharWithEscapedChar();
			Expectations.Parameter1 = ParameterNames[1].ReplaceCharWithEscapedChar();
		}

		return Expectations;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptColorAndTextBindingsTests_Private;

bool FAngelscriptLinearColorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	const FLinearColor Base(0.25f, 0.5f, 0.75f, 1.0f);
	const FLinearColor Offset(0.5f, 0.25f, 0.0f, 0.0f);
	const FLinearColor ExpectedAdded = Base + Offset;
	const FLinearColor ExpectedScaled = ExpectedAdded * 0.5f;

	FString ScriptSource = TEXT(R"AS(
int Entry()
{
	FLinearColor Base = __BASE__;
	FLinearColor Offset = __OFFSET__;
	FLinearColor Added = Base + Offset;
	if (!Added.Equals(__EXPECTED_ADDED__, 0.0001))
		return 10;

	FLinearColor Scaled = Added * 0.5;
	if (!Scaled.Equals(__EXPECTED_SCALED__, 0.0001))
		return 20;

	FLinearColor Unclamped = FLinearColor(1.5, -0.5, 0.25, 2.0);
	FLinearColor Clamped = Unclamped.GetClamped();
	if (!Clamped.Equals(FLinearColor(1.0, 0.0, 0.25, 1.0), 0.0001))
		return 30;

	FLinearColor VectorConstructed = FLinearColor(FVector(0.25, 0.5, 0.75), 0.6);
	if (!VectorConstructed.Equals(FLinearColor(0.25, 0.5, 0.75, 0.6), 0.0001))
		return 40;

	FLinearColor HsvRoundTrip = Base.LinearRGBToHSV().HSVToLinearRGB();
	if (!HsvRoundTrip.Equals(Base, 0.001))
		return 50;

	if (!FLinearColor::Teal.Equals(FLinearColor(0.0, 0.5019, 0.5019, 1.0), 0.0001))
		return 60;

	if (!(FLinearColor::Red.ToFColor(true) == FColor::Red))
		return 70;

	return 1;
}
)AS");

	ScriptSource.ReplaceInline(TEXT("__BASE__"), *FormatScriptLinearColorLiteral(Base), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__OFFSET__"), *FormatScriptLinearColorLiteral(Offset), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_ADDED__"), *FormatScriptLinearColorLiteral(ExpectedAdded), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_SCALED__"), *FormatScriptLinearColorLiteral(ExpectedScaled), ESearchCase::CaseSensitive);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(LinearColorModuleName));
	};

	asIScriptModule* Module = BuildModule(*this, Engine, LinearColorModuleName, ScriptSource);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed = TestEqual(
		TEXT("FLinearColor bindings should preserve arithmetic, clamping, conversion, and static constants"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptColorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	const FColor Base(51, 102, 153, 204);
	const FColor Increment(4, 5, 6, 7);
	FColor ExpectedAccumulated = Base;
	ExpectedAccumulated += Increment;
	const FString ExpectedHex = Base.ToHex();
	const FString ExpectedOrangeHex = FColor::Orange.ToHex();

	FString ScriptSource = TEXT(R"AS(
int Entry()
{
	FColor Base = __BASE__;
	uint32 Packed = Base.DWColor;
	FColor RoundTrip = FColor(Packed);
	if (!(RoundTrip == Base))
		return 10;

	FString Hex = Base.ToHex();
	if (!(Hex == "__EXPECTED_HEX__"))
		return 20;

	FColor Parsed = FColor::FromHex(Hex);
	if (!(Parsed == Base))
		return 30;

	FColor Accumulated = Base;
	Accumulated += __INCREMENT__;
	if (!(Accumulated == __EXPECTED_ACCUMULATED__))
		return 40;

	if (!(FColor::Orange.ToHex() == "__EXPECTED_ORANGE_HEX__"))
		return 50;

	if (!(FColor::Transparent == FColor(0, 0, 0, 0)))
		return 60;

	FLinearColor LinearRoundTrip = Base.ReinterpretAsLinear();
	if (!(LinearRoundTrip.ToFColor(false) == Base))
		return 70;

	return 1;
}
)AS");

	ScriptSource.ReplaceInline(TEXT("__BASE__"), *FormatScriptColorLiteral(Base), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__INCREMENT__"), *FormatScriptColorLiteral(Increment), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_ACCUMULATED__"), *FormatScriptColorLiteral(ExpectedAccumulated), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_HEX__"), *ExpectedHex, ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_ORANGE_HEX__"), *ExpectedOrangeHex, ESearchCase::CaseSensitive);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(ColorModuleName));
	};

	asIScriptModule* Module = BuildModule(*this, Engine, ColorModuleName, ScriptSource);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed = TestEqual(
		TEXT("FColor bindings should preserve packed-value round-trip, hex conversion, constants, and linear reinterpretation"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptTextBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	const FTextBindingExpectations Expectations = BuildTextBindingExpectations();

	FString ScriptSource = TEXT(R"AS(
int Entry()
{
	FText EmptyText;
	if (!EmptyText.IsEmpty())
		return 10;

	if (!FText::FromString("   ").IsEmptyOrWhitespace())
		return 20;

	FText Invariant = FText::AsCultureInvariant("Alpha");
	if (!Invariant.IsCultureInvariant())
		return 30;
	if (!(Invariant.ToString() == "Alpha"))
		return 40;

	FText FromName = FText::FromName(n"Bravo");
	if (!(FromName.ToString() == "Bravo"))
		return 50;

	TArray<FText> TextParts;
	TextParts.Add(FText::FromString("One"));
	TextParts.Add(FText::AsCultureInvariant("Two"));
	FText JoinedText = FText::Join(FText::FromString("|"), TextParts);
	if (!(JoinedText.ToString() == "__JOINED_TEXT__"))
		return 60;

	TArray<FFormatArgumentValue> FormatParts;
	FormatParts.Add(FFormatArgumentValue(FText::FromString("Alpha")));
	FormatParts.Add(FFormatArgumentValue(int32(7)));
	FText JoinedArguments = FText::Join(FText::FromString(","), FormatParts);
	if (!(JoinedArguments.ToString() == "__JOINED_ARGUMENTS__"))
		return 70;

	TArray<FString> ParameterNames;
	FText::GetFormatPatternParameters(FText::FromString("{Count}:{Name}:{Count}"), ParameterNames);
	if (ParameterNames.Num() != 2)
		return 80;
	if (!(ParameterNames[0] == "__PARAMETER0__"))
		return 90;
	if (!(ParameterNames[1] == "__PARAMETER1__"))
		return 100;

	FText Source = FText::FromString("Gamma");
	FText Copy = Source;
	if (!Copy.IdenticalTo(Source, ETextIdenticalModeFlags::None))
		return 110;
	if (!Copy.IsInitializedFromString())
		return 120;

	return 1;
}
)AS");

	ScriptSource.ReplaceInline(TEXT("__JOINED_TEXT__"), *Expectations.JoinedText, ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__JOINED_ARGUMENTS__"), *Expectations.JoinedArguments, ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__PARAMETER0__"), *Expectations.Parameter0, ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__PARAMETER1__"), *Expectations.Parameter1, ESearchCase::CaseSensitive);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(TextModuleName));
	};

	asIScriptModule* Module = BuildModule(*this, Engine, TextModuleName, ScriptSource);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed = TestEqual(
		TEXT("FText bindings should preserve culture-invariant creation, joining, format-parameter discovery, and identity semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
