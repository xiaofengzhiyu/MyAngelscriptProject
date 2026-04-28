#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Containers/UnrealString.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptStringFormattingBindingsTests_Private
{
	static constexpr ANSICHAR FormatHelpersModuleName[] = "ASStringFormatHelpersCompat";
	static constexpr ANSICHAR ParseIntoArrayModuleName[] = "ASStringParseIntoArrayCompat";
	static constexpr ANSICHAR ParseIntoArrayDelimiterLimitModuleName[] = "ASStringParseIntoArrayDelimiterLimit";

	struct FStringFormattingExpectations
	{
		FString JoinExpected;
		FString SingleFormatExpected;
		FString FiveArgumentFormatExpected;
	};

	FString EscapeScriptString(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *EscapeScriptString(Replacement), ESearchCase::CaseSensitive);
	}

	FStringFormattingExpectations BuildFormattingExpectations()
	{
		FStringFormattingExpectations Expectations;

		TArray<FString> JoinParts;
		JoinParts.Add(TEXT("Alpha"));
		JoinParts.Add(TEXT("Beta"));
		JoinParts.Add(TEXT("Gamma"));
		Expectations.JoinExpected = FString::Join(JoinParts, TEXT("|"));

		FStringFormatOrderedArguments SingleArguments;
		SingleArguments.Add(FStringFormatArg(FString(TEXT("Solo"))));
		Expectations.SingleFormatExpected = FString::Format(TEXT("{0}"), SingleArguments);

		FStringFormatOrderedArguments FiveArguments;
		FiveArguments.Add(FStringFormatArg(int32(-7)));
		FiveArguments.Add(FStringFormatArg(uint32(42)));
		FiveArguments.Add(FStringFormatArg(int64(9000000000ll)));
		FiveArguments.Add(FStringFormatArg(uint64(15)));
		FiveArguments.Add(FStringFormatArg(FString(TEXT("Tail"))));
		Expectations.FiveArgumentFormatExpected = FString::Format(TEXT("{0}|{1}|{2}|{3}|{4}"), FiveArguments);

		return Expectations;
	}

	bool ExecuteFunctionExpectingException(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		FString& OutExceptionString)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(TEXT("String delimiter limit test should create an execution context"), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int32 PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(TEXT("String delimiter limit function should prepare"), PrepareResult, static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		const int32 ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("String delimiter limit function should raise a script exception"), ExecuteResult, static_cast<int32>(asEXECUTION_EXCEPTION)))
		{
			return false;
		}

		OutExceptionString = Context->GetExceptionString() != nullptr ? UTF8_TO_TCHAR(Context->GetExceptionString()) : TEXT("");
		return true;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptStringFormattingBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStringFormatHelpersBindingsTest,
	"Angelscript.TestModule.Bindings.String.FormatHelpersCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStringParseIntoArrayBindingsTest,
	"Angelscript.TestModule.Bindings.String.ParseIntoArrayCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStringParseIntoArrayDelimiterLimitBindingsTest,
	"Angelscript.TestModule.Bindings.String.ParseIntoArrayDelimiterLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptStringFormatHelpersBindingsTest::RunTest(const FString& Parameters)
{
	const FStringFormattingExpectations Expectations = BuildFormattingExpectations();

	FString ScriptSource = TEXT(R"AS(
int Entry()
{
	TArray<FString> Parts;
	Parts.Add("Alpha");
	Parts.Add("Beta");
	Parts.Add("Gamma");
	if (!(FString::Join(Parts, "|") == "__EXPECTED_JOIN__"))
		return 10;

	if (!(FString::Format("{0}", "Solo") == "__EXPECTED_SINGLE_FORMAT__"))
		return 20;
	if (!(FString::Format("{0}|{1}|{2}|{3}|{4}", int32(-7), uint32(42), int64(9000000000), uint64(15), "Tail") == "__EXPECTED_FIVE_ARGUMENT_FORMAT__"))
		return 30;

	if (!(FString::ApplyFormat(int32(-42), "d") == "-42"))
		return 40;
	if (!(FString::ApplyFormat(uint32(255), "#x") == "0xff"))
		return 50;
	if (!(FString::ApplyFormat(true, ">6") == "  true"))
		return 60;
	if (!(FString::ApplyFormat(float64(12.5), ".1f") == "12.5"))
		return 70;

	FString PadValue = "Pad";
	if (!(FString::ApplyFormat(PadValue, ">5") == "  Pad"))
		return 80;

	return 1;
}
)AS");
	ReplaceToken(ScriptSource, TEXT("__EXPECTED_JOIN__"), Expectations.JoinExpected);
	ReplaceToken(ScriptSource, TEXT("__EXPECTED_SINGLE_FORMAT__"), Expectations.SingleFormatExpected);
	ReplaceToken(ScriptSource, TEXT("__EXPECTED_FIVE_ARGUMENT_FORMAT__"), Expectations.FiveArgumentFormatExpected);

	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(FormatHelpersModuleName));
	};

	int32 Result = INDEX_NONE;
	ASTEST_COMPILE_RUN_INT(
		Engine,
		FormatHelpersModuleName,
		ScriptSource,
		TEXT("int Entry()"),
		Result);

	bPassed &= TestEqual(
		TEXT("FString Join, Format, and ApplyFormat bindings should preserve script-visible formatting semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptStringParseIntoArrayBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(ParseIntoArrayModuleName));
	};

	int32 Result = INDEX_NONE;
	ASTEST_COMPILE_RUN_INT(
		Engine,
		ParseIntoArrayModuleName,
		TEXT(R"AS(
int Entry()
{
	FString SingleSource = "Alpha,,Beta";

	TArray<FString> SingleParts;
	int SingleCount = SingleSource.ParseIntoArray(SingleParts, ",", false);
	if (SingleCount != 3 || SingleParts.Num() != 3)
		return 10;
	if (!(SingleParts[0] == "Alpha") || !(SingleParts[1] == "") || !(SingleParts[2] == "Beta"))
		return 20;

	TArray<FString> CulledParts;
	int CulledCount = SingleSource.ParseIntoArray(CulledParts, ",", true);
	if (CulledCount != 2 || CulledParts.Num() != 2)
		return 30;
	if (!(CulledParts[0] == "Alpha") || !(CulledParts[1] == "Beta"))
		return 40;

	FString MultiSource = "Alpha,Beta|Gamma";
	TArray<FString> Delimiters;
	Delimiters.Add(",");
	Delimiters.Add("|");

	TArray<FString> MultiParts;
	int MultiCount = MultiSource.ParseIntoArray(MultiParts, Delimiters, false);
	if (MultiCount != 3 || MultiParts.Num() != 3)
		return 50;
	if (!(MultiParts[0] == "Alpha") || !(MultiParts[1] == "Beta") || !(MultiParts[2] == "Gamma"))
		return 60;

	return 1;
}
)AS"),
		TEXT("int Entry()"),
		Result);

	bPassed &= TestEqual(
		TEXT("FString ParseIntoArray overloads should preserve single-delimiter, cull-empty, and multi-delimiter semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptStringParseIntoArrayDelimiterLimitBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	AddExpectedError(TEXT("More than 16 delimiters is not supported by ParseIntoArray."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("ASStringParseIntoArrayDelimiterLimit"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("void TriggerDelimiterLimit()"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(ParseIntoArrayDelimiterLimitModuleName));
		ResetSharedCloneEngine(Engine);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ParseIntoArrayDelimiterLimitModuleName,
		TEXT(R"AS(
void TriggerDelimiterLimit()
{
	TArray<FString> Delimiters;
	for (int32 Index = 0; Index < 17; ++Index)
	{
		Delimiters.Add("|");
	}

	TArray<FString> OutParts;
	FString Source = "Alpha";
	Source.ParseIntoArray(OutParts, Delimiters, true);
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	FString ExceptionString;
	if (!ExecuteFunctionExpectingException(
		*this,
		Engine,
		*Module,
		TEXT("void TriggerDelimiterLimit()"),
		ExceptionString))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("FString ParseIntoArray delimiter-limit guard should surface the expected runtime exception"),
		ExceptionString,
		FString(TEXT("More than 16 delimiters is not supported by ParseIntoArray.")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
