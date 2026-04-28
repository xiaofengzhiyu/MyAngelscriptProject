#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptCommandLineQuotedParseBindingsTest,
	"Angelscript.TestModule.Bindings.CommandLineParse.QuotedCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptParseMissingKeyGuardBindingsTest,
	"Angelscript.TestModule.Bindings.CommandLineParse.MissingKeyGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptCommandLineAndParseEdgeBindingsTests_Private
{
	static constexpr ANSICHAR CommandLineModuleName[] = "ASCommandLineQuotedParseCompat";
	static constexpr ANSICHAR ParseModuleName[] = "ASParseMissingKeyGuards";

	static constexpr int32 MissingIntSentinel = 99;
	static constexpr float MissingFloatSentinel = 7.25f;
	static constexpr TCHAR MissingNameSentinel[] = TEXT("SentinelName");
	static constexpr bool MissingBoolSentinel = true;

	struct FCommandLineParseExpectations
	{
		FString Source;
		TArray<FString> Tokens;
		TArray<FString> Switches;
	};

	struct FParseExpectations
	{
		FString Source;
		bool bHasCount = false;
		int32 Count = 0;
		bool bHasRatio = false;
		float Ratio = 0.0f;
		bool bHasName = false;
		FString Name;
		bool bHasEnabled = false;
		bool bEnabled = false;
		bool bHasDisabled = false;
		bool bDisabled = false;
		bool bHasMissingInt = false;
		int32 MissingInt = MissingIntSentinel;
		bool bHasMissingFloat = false;
		float MissingFloat = MissingFloatSentinel;
		bool bHasMissingName = false;
		FString MissingName = MissingNameSentinel;
		bool bHasMissingBool = false;
		bool bMissingBool = MissingBoolSentinel;
	};

	FString EscapeForScriptLiteral(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	FString ToScriptBoolLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString ToScriptIntLiteral(const int32 Value)
	{
		return FString::Printf(TEXT("%d"), Value);
	}

	FString ToScriptCountLiteral(const int32 Value)
	{
		return FString::Printf(TEXT("%d"), Value);
	}

	FString ToScriptFloatLiteral(const float Value)
	{
		return FString::Printf(TEXT("%sf"), *FString::SanitizeFloat(Value));
	}

	void ReplaceToken(FString& ScriptSource, const TCHAR* Token, const FString& Replacement)
	{
		ScriptSource.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	void ReplaceToken(FString& ScriptSource, const TCHAR* Token, const int32 Replacement)
	{
		ReplaceToken(ScriptSource, Token, ToScriptIntLiteral(Replacement));
	}

	void ReplaceToken(FString& ScriptSource, const TCHAR* Token, const bool Replacement)
	{
		ReplaceToken(ScriptSource, Token, ToScriptBoolLiteral(Replacement));
	}

	void ReplaceToken(FString& ScriptSource, const TCHAR* Token, const float Replacement)
	{
		ReplaceToken(ScriptSource, Token, ToScriptFloatLiteral(Replacement));
	}

	FCommandLineParseExpectations BuildCommandLineParseExpectations()
	{
		FCommandLineParseExpectations Expectations;
		Expectations.Source = TEXT("-AlphaSwitch \"Quoted Token\" BareToken -BetaSwitch \"Second Token\"");
		Expectations.Tokens.Add(TEXT("ExistingToken"));
		Expectations.Switches.Add(TEXT("ExistingSwitch"));
		FCommandLine::Parse(*Expectations.Source, Expectations.Tokens, Expectations.Switches);
		return Expectations;
	}

	void AppendArrayChecks(
		FString& ScriptSource,
		const TCHAR* ArrayName,
		const TArray<FString>& Values,
		const int32 FailureBase)
	{
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			ScriptSource += FString::Printf(
				TEXT("\tif (!(%s[%d] == \"%s\"))\n\t\treturn %d;\n"),
				ArrayName,
				Index,
				*EscapeForScriptLiteral(Values[Index]),
				FailureBase + Index);
		}
	}

	FString BuildCommandLineScriptSource(const FCommandLineParseExpectations& Expectations)
	{
		FString ScriptSource = TEXT(R"(
int Entry()
{
	TArray<FString> Tokens;
	Tokens.Add("ExistingToken");
	TArray<FString> Switches;
	Switches.Add("ExistingSwitch");

	FCommandLine::Parse("__SOURCE__", Tokens, Switches);

	if (Tokens.Num() != __TOKEN_COUNT__)
		return 10;
	if (Switches.Num() != __SWITCH_COUNT__)
		return 20;
)");

		ReplaceToken(ScriptSource, TEXT("__SOURCE__"), EscapeForScriptLiteral(Expectations.Source));
		ReplaceToken(ScriptSource, TEXT("__TOKEN_COUNT__"), ToScriptCountLiteral(Expectations.Tokens.Num()));
		ReplaceToken(ScriptSource, TEXT("__SWITCH_COUNT__"), ToScriptCountLiteral(Expectations.Switches.Num()));
		AppendArrayChecks(ScriptSource, TEXT("Tokens"), Expectations.Tokens, 30);
		AppendArrayChecks(ScriptSource, TEXT("Switches"), Expectations.Switches, 60);
		ScriptSource += TEXT("\treturn 1;\n}\n");
		return ScriptSource;
	}

	FParseExpectations BuildParseExpectations()
	{
		FParseExpectations Expectations;
		Expectations.Source = TEXT("Count=12 Ratio=-3.5 Name=Alpha Enabled=true Disabled=false");
		Expectations.Count = -1;
		Expectations.bHasCount = FParse::Value(*Expectations.Source, TEXT("Count="), Expectations.Count);
		Expectations.Ratio = 0.0f;
		Expectations.bHasRatio = FParse::Value(*Expectations.Source, TEXT("Ratio="), Expectations.Ratio);
		Expectations.Name = TEXT("UnsetName");
		Expectations.bHasName = FParse::Value(*Expectations.Source, TEXT("Name="), Expectations.Name);
		Expectations.bEnabled = false;
		Expectations.bHasEnabled = FParse::Bool(*Expectations.Source, TEXT("Enabled="), Expectations.bEnabled);
		Expectations.bDisabled = true;
		Expectations.bHasDisabled = FParse::Bool(*Expectations.Source, TEXT("Disabled="), Expectations.bDisabled);
		Expectations.bHasMissingInt = FParse::Value(*Expectations.Source, TEXT("MissingInt="), Expectations.MissingInt);
		Expectations.bHasMissingFloat = FParse::Value(*Expectations.Source, TEXT("MissingFloat="), Expectations.MissingFloat);
		Expectations.bHasMissingName = FParse::Value(*Expectations.Source, TEXT("MissingName="), Expectations.MissingName);
		Expectations.bHasMissingBool = FParse::Bool(*Expectations.Source, TEXT("MissingBool="), Expectations.bMissingBool);
		return Expectations;
	}

	FString BuildParseScriptSource(const FParseExpectations& Expectations)
	{
		FString ScriptSource = TEXT(R"(
int Entry()
{
	const FString Source = "__SOURCE__";

	int Count = -1;
	if (FParse::Value(Source, "Count=", Count) != __HAS_COUNT__)
		return 10;
	if (Count != __COUNT__)
		return 11;

	float32 Ratio = 0.0f;
	if (FParse::Value(Source, "Ratio=", Ratio) != __HAS_RATIO__)
		return 20;
	if (Ratio != __RATIO__)
		return 21;

	FString Name = "UnsetName";
	if (FParse::Value(Source, "Name=", Name) != __HAS_NAME__)
		return 30;
	if (!(Name == "__NAME__"))
		return 31;

	bool bEnabled = false;
	if (FParse::Bool(Source, "Enabled=", bEnabled) != __HAS_ENABLED__)
		return 40;
	if (bEnabled != __ENABLED__)
		return 41;

	bool bDisabled = true;
	if (FParse::Bool(Source, "Disabled=", bDisabled) != __HAS_DISABLED__)
		return 50;
	if (bDisabled != __DISABLED__)
		return 51;

	int MissingInt = __MISSING_INT_SENTINEL__;
	if (FParse::Value(Source, "MissingInt=", MissingInt) != __HAS_MISSING_INT__)
		return 60;
	if (MissingInt != __MISSING_INT__)
		return 61;

	float32 MissingFloat = __MISSING_FLOAT_SENTINEL__;
	if (FParse::Value(Source, "MissingFloat=", MissingFloat) != __HAS_MISSING_FLOAT__)
		return 70;
	if (MissingFloat != __MISSING_FLOAT__)
		return 71;

	FString MissingName = "__MISSING_NAME_SENTINEL__";
	if (FParse::Value(Source, "MissingName=", MissingName) != __HAS_MISSING_NAME__)
		return 80;
	if (!(MissingName == "__MISSING_NAME__"))
		return 81;

	bool bMissingBool = __MISSING_BOOL_SENTINEL__;
	if (FParse::Bool(Source, "MissingBool=", bMissingBool) != __HAS_MISSING_BOOL__)
		return 90;
	if (bMissingBool != __MISSING_BOOL__)
		return 91;

	return 1;
}
)");

		ReplaceToken(ScriptSource, TEXT("__SOURCE__"), EscapeForScriptLiteral(Expectations.Source));
		ReplaceToken(ScriptSource, TEXT("__HAS_COUNT__"), Expectations.bHasCount);
		ReplaceToken(ScriptSource, TEXT("__COUNT__"), Expectations.Count);
		ReplaceToken(ScriptSource, TEXT("__HAS_RATIO__"), Expectations.bHasRatio);
		ReplaceToken(ScriptSource, TEXT("__RATIO__"), Expectations.Ratio);
		ReplaceToken(ScriptSource, TEXT("__HAS_NAME__"), Expectations.bHasName);
		ReplaceToken(ScriptSource, TEXT("__NAME__"), EscapeForScriptLiteral(Expectations.Name));
		ReplaceToken(ScriptSource, TEXT("__HAS_ENABLED__"), Expectations.bHasEnabled);
		ReplaceToken(ScriptSource, TEXT("__ENABLED__"), Expectations.bEnabled);
		ReplaceToken(ScriptSource, TEXT("__HAS_DISABLED__"), Expectations.bHasDisabled);
		ReplaceToken(ScriptSource, TEXT("__DISABLED__"), Expectations.bDisabled);
		ReplaceToken(ScriptSource, TEXT("__MISSING_INT_SENTINEL__"), MissingIntSentinel);
		ReplaceToken(ScriptSource, TEXT("__HAS_MISSING_INT__"), Expectations.bHasMissingInt);
		ReplaceToken(ScriptSource, TEXT("__MISSING_INT__"), Expectations.MissingInt);
		ReplaceToken(ScriptSource, TEXT("__MISSING_FLOAT_SENTINEL__"), MissingFloatSentinel);
		ReplaceToken(ScriptSource, TEXT("__HAS_MISSING_FLOAT__"), Expectations.bHasMissingFloat);
		ReplaceToken(ScriptSource, TEXT("__MISSING_FLOAT__"), Expectations.MissingFloat);
		ReplaceToken(ScriptSource, TEXT("__MISSING_NAME_SENTINEL__"), EscapeForScriptLiteral(MissingNameSentinel));
		ReplaceToken(ScriptSource, TEXT("__HAS_MISSING_NAME__"), Expectations.bHasMissingName);
		ReplaceToken(ScriptSource, TEXT("__MISSING_NAME__"), EscapeForScriptLiteral(Expectations.MissingName));
		ReplaceToken(ScriptSource, TEXT("__MISSING_BOOL_SENTINEL__"), MissingBoolSentinel);
		ReplaceToken(ScriptSource, TEXT("__HAS_MISSING_BOOL__"), Expectations.bHasMissingBool);
		ReplaceToken(ScriptSource, TEXT("__MISSING_BOOL__"), Expectations.bMissingBool);
		return ScriptSource;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptCommandLineAndParseEdgeBindingsTests_Private;

bool FAngelscriptCommandLineQuotedParseBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	const FCommandLineParseExpectations Expectations = BuildCommandLineParseExpectations();

	bPassed &= TestTrue(TEXT("Native command-line baseline should keep at least one parsed token beyond the seeded entry"), Expectations.Tokens.Num() > 1);
	bPassed &= TestTrue(TEXT("Native command-line baseline should keep at least one parsed switch beyond the seeded entry"), Expectations.Switches.Num() > 1);
	if (!bPassed)
	{
		return false;
	}

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(CommandLineModuleName));
		ResetSharedCloneEngine(Engine);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		CommandLineModuleName,
		BuildCommandLineScriptSource(Expectations));
	if (Module == nullptr)
	{
		bPassed = false;
	}
	else
	{
		asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
		if (Function == nullptr)
		{
			bPassed = false;
		}
		else
		{
			int32 Result = 0;
			bPassed &= ExecuteIntFunction(*this, Engine, *Function, Result);
			if (bPassed)
			{
				bPassed &= TestEqual(TEXT("FCommandLine::Parse should preserve quoted token and switch parity"), Result, 1);
			}
		}
	}

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptParseMissingKeyGuardBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	const FParseExpectations Expectations = BuildParseExpectations();

	bPassed &= TestTrue(TEXT("Native FParse baseline should find Count"), Expectations.bHasCount);
	bPassed &= TestEqual(TEXT("Native FParse baseline should preserve Count"), Expectations.Count, 12);
	bPassed &= TestTrue(TEXT("Native FParse baseline should find Ratio"), Expectations.bHasRatio);
	bPassed &= TestEqual(TEXT("Native FParse baseline should preserve Ratio"), Expectations.Ratio, -3.5f);
	bPassed &= TestTrue(TEXT("Native FParse baseline should find Name"), Expectations.bHasName);
	bPassed &= TestEqual(TEXT("Native FParse baseline should preserve Name"), Expectations.Name, FString(TEXT("Alpha")));
	bPassed &= TestTrue(TEXT("Native FParse baseline should find Enabled"), Expectations.bHasEnabled);
	bPassed &= TestTrue(TEXT("Native FParse baseline should set Enabled to true"), Expectations.bEnabled);
	bPassed &= TestTrue(TEXT("Native FParse baseline should find Disabled"), Expectations.bHasDisabled);
	bPassed &= TestFalse(TEXT("Native FParse baseline should set Disabled to false"), Expectations.bDisabled);
	bPassed &= TestFalse(TEXT("Native FParse baseline should reject missing int keys"), Expectations.bHasMissingInt);
	bPassed &= TestEqual(TEXT("Native FParse missing int should preserve the sentinel"), Expectations.MissingInt, MissingIntSentinel);
	bPassed &= TestFalse(TEXT("Native FParse baseline should reject missing float keys"), Expectations.bHasMissingFloat);
	bPassed &= TestEqual(TEXT("Native FParse missing float should preserve the sentinel"), Expectations.MissingFloat, MissingFloatSentinel);
	bPassed &= TestFalse(TEXT("Native FParse baseline should reject missing name keys"), Expectations.bHasMissingName);
	bPassed &= TestEqual(TEXT("Native FParse missing name should preserve the sentinel"), Expectations.MissingName, FString(MissingNameSentinel));
	bPassed &= TestFalse(TEXT("Native FParse baseline should reject missing bool keys"), Expectations.bHasMissingBool);
	bPassed &= TestEqual(TEXT("Native FParse missing bool should preserve the sentinel"), Expectations.bMissingBool, MissingBoolSentinel);
	if (!bPassed)
	{
		return false;
	}

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(ParseModuleName));
		ResetSharedCloneEngine(Engine);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ParseModuleName,
		BuildParseScriptSource(Expectations));
	if (Module == nullptr)
	{
		bPassed = false;
	}
	else
	{
		asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
		if (Function == nullptr)
		{
			bPassed = false;
		}
		else
		{
			int32 Result = 0;
			bPassed &= ExecuteIntFunction(*this, Engine, *Function, Result);
			if (bPassed)
			{
				bPassed &= TestEqual(TEXT("FParse bindings should preserve found-value and missing-key guard parity"), Result, 1);
			}
		}
	}

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
