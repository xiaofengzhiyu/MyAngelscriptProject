#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptNameBindingsTests_Private
{
	static constexpr ANSICHAR NameValueModuleName[] = "ASNameValueCompat";
	static constexpr ANSICHAR NameStringInteropModuleName[] = "ASNameStringInteropCompat";

	struct FNameBindingsBaselines
	{
		FName PlainName;
		FName NumberedName;
		FName OtherName;
		FString PlainNameString;
		FString OtherNameString;
		FString PrefixAppendString;
		FString SuffixAppendString;
		FString NumberedAppendString;
		FString ExtraSegment;
		uint32 NumberValue = 0;
		uint32 NumberedHash = 0;
	};

	FNameBindingsBaselines BuildBaselines()
	{
		FNameBindingsBaselines Baselines;
		Baselines.PlainName = FName(TEXT("AlphaName"));
		Baselines.NumberedName = Baselines.PlainName;
		Baselines.NumberValue = 7;
		Baselines.NumberedName.SetNumber(static_cast<int32>(Baselines.NumberValue));
		Baselines.OtherName = FName(TEXT("BravoName"));
		Baselines.PlainNameString = Baselines.PlainName.ToString();
		Baselines.OtherNameString = Baselines.OtherName.ToString();
		Baselines.ExtraSegment = TEXT("_Again");
		Baselines.PrefixAppendString = FString(TEXT("Head_")) + Baselines.PlainNameString;
		Baselines.SuffixAppendString = Baselines.PlainNameString + TEXT("_Tail");
		Baselines.NumberedAppendString = Baselines.NumberedName.ToString() + Baselines.ExtraSegment;
		Baselines.NumberedHash = GetTypeHash(Baselines.NumberedName);
		return Baselines;
	}

	void ReplaceToken(FString& ScriptSource, const TCHAR* Token, const FString& Replacement)
	{
		ScriptSource.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	void ReplaceToken(FString& ScriptSource, const TCHAR* Token, const uint32 Replacement)
	{
		ReplaceToken(ScriptSource, Token, FString::Printf(TEXT("%u"), Replacement));
	}

	FString BuildValueScriptSource(const FNameBindingsBaselines& Baselines)
	{
		FString ScriptSource = TEXT(R"(
int Entry()
{
	const FName DefaultName = NAME_None;
	if (!DefaultName.IsNone())
		return 10;

	FString PlainString = "__PLAIN_NAME__";
	FString OtherString = "__OTHER_NAME__";

	FName FromString(PlainString);
	if (FromString.IsNone())
		return 20;
	if (!(FromString.GetPlainNameString() == "__PLAIN_NAME__"))
		return 30;
	if (FromString.GetNumber() != 0)
		return 40;

	FromString.SetNumber(__NUMBER_VALUE__);
	if (FromString.GetNumber() != __NUMBER_VALUE__)
		return 50;
	if (!(FromString.GetPlainNameString() == "__PLAIN_NAME__"))
		return 60;

	FName Copy(FromString);
	if (!(Copy == FromString))
		return 70;
	if (Copy.Compare(FromString) != 0)
		return 80;

	FName Assigned(OtherString);
	Assigned = Copy;
	if (!(Assigned == Copy))
		return 90;

	FName PlainName(PlainString);
	FName OtherName(OtherString);
	if (!Copy.IsEqual(PlainName, true, false))
		return 100;
	if (Copy.IsEqual(OtherName, true, false))
		return 110;

	if (Copy.GetHash() != uint(__NUMBERED_HASH__))
		return 120;

	if (!NAME_None.IsEqual(DefaultName))
		return 130;

	return 1;
}
)");

		ReplaceToken(ScriptSource, TEXT("__PLAIN_NAME__"), Baselines.PlainNameString.ReplaceCharWithEscapedChar());
		ReplaceToken(ScriptSource, TEXT("__OTHER_NAME__"), Baselines.OtherNameString.ReplaceCharWithEscapedChar());
		ReplaceToken(ScriptSource, TEXT("__NUMBER_VALUE__"), Baselines.NumberValue);
		ReplaceToken(ScriptSource, TEXT("__NUMBERED_HASH__"), Baselines.NumberedHash);
		return ScriptSource;
	}

	FString BuildStringInteropScriptSource(const FNameBindingsBaselines& Baselines)
	{
		FString ScriptSource = TEXT(R"(
int Entry()
{
	FString NameString = "__PLAIN_NAME__";
	FString OtherString = "__OTHER_NAME__";
	FName Value(NameString);

	FString Prefix = "Head_";
	Prefix += Value;
	if (!(Prefix == "__PREFIX_APPEND__"))
		return 10;

	FString Suffix = Value + "_Tail";
	if (!(Suffix == "__SUFFIX_APPEND__"))
		return 20;

	FName Numbered = Value;
	Numbered.SetNumber(__NUMBER_VALUE__);

	FString Builder = "";
	Builder.Append(Numbered);
	Builder += "__EXTRA_SEGMENT__";
	if (!(Builder == "__NUMBERED_APPEND__"))
		return 30;

	if (!(Value == NameString))
		return 40;
	if (Value == OtherString)
		return 50;

	return 1;
}
)");

		ReplaceToken(ScriptSource, TEXT("__PLAIN_NAME__"), Baselines.PlainNameString.ReplaceCharWithEscapedChar());
		ReplaceToken(ScriptSource, TEXT("__OTHER_NAME__"), Baselines.OtherNameString.ReplaceCharWithEscapedChar());
		ReplaceToken(ScriptSource, TEXT("__PREFIX_APPEND__"), Baselines.PrefixAppendString.ReplaceCharWithEscapedChar());
		ReplaceToken(ScriptSource, TEXT("__SUFFIX_APPEND__"), Baselines.SuffixAppendString.ReplaceCharWithEscapedChar());
		ReplaceToken(ScriptSource, TEXT("__NUMBER_VALUE__"), Baselines.NumberValue);
		ReplaceToken(ScriptSource, TEXT("__EXTRA_SEGMENT__"), Baselines.ExtraSegment.ReplaceCharWithEscapedChar());
		ReplaceToken(ScriptSource, TEXT("__NUMBERED_APPEND__"), Baselines.NumberedAppendString.ReplaceCharWithEscapedChar());
		return ScriptSource;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptNameBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNameValueBindingsTest,
	"Angelscript.TestModule.Bindings.NameValueCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNameStringInteropBindingsTest,
	"Angelscript.TestModule.Bindings.NameStringInteropCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptNameValueBindingsTest::RunTest(const FString& Parameters)
{
	const FNameBindingsBaselines Baselines = BuildBaselines();

	TestEqual(TEXT("Native numbered name should preserve its plain-name string"), Baselines.NumberedName.GetPlainNameString(), Baselines.PlainNameString);
	TestEqual(TEXT("Native numbered name should expose the expected number"), Baselines.NumberedName.GetNumber(), static_cast<int32>(Baselines.NumberValue));
	TestEqual(TEXT("Native numbered name hash should match the cached baseline"), GetTypeHash(Baselines.NumberedName), Baselines.NumberedHash);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		NameValueModuleName,
		BuildValueScriptSource(Baselines));
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

	TestEqual(TEXT("FName value bindings should preserve numbered-name and NAME_None parity"), Result, 1);

	ASTEST_END_SHARE_CLEAN
	return true;
}

bool FAngelscriptNameStringInteropBindingsTest::RunTest(const FString& Parameters)
{
	const FNameBindingsBaselines Baselines = BuildBaselines();

	TestEqual(TEXT("Native FName prefix append baseline should match the cached expectation"), FString(TEXT("Head_")) + Baselines.PlainNameString, Baselines.PrefixAppendString);
	TestEqual(TEXT("Native numbered-name append baseline should match the cached expectation"), Baselines.NumberedName.ToString() + Baselines.ExtraSegment, Baselines.NumberedAppendString);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		NameStringInteropModuleName,
		BuildStringInteropScriptSource(Baselines));
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

	TestEqual(TEXT("FName FString interop bindings should preserve append and equality parity"), Result, 1);

	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
