#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "HAL/PlatformApplicationMisc.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPlatformApplicationMiscClipboardCompatBindingsTest,
	"Angelscript.TestModule.Bindings.PlatformApplicationMiscClipboardCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptPlatformApplicationMiscBindingsTests_Private
{
	static constexpr ANSICHAR PlatformApplicationMiscModuleName[] = "ASPlatformApplicationMiscClipboardCompat";

	FString EscapeScriptString(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"), ESearchCase::CaseSensitive);
		return Escaped;
	}

	FString ReadClipboard()
	{
		FString ClipboardContents;
		FPlatformApplicationMisc::ClipboardPaste(ClipboardContents);
		return ClipboardContents;
	}

	FString BuildScriptSource(const FString& ExpectedNativeToScript, const FString& ExpectedScriptToNative)
	{
		FString ScriptSource = TEXT(R"(
int Entry()
{
	FString SeededValue;
	FPlatformApplicationMisc::ClipboardPaste(SeededValue);
	if (!(SeededValue == "__EXPECTED_NATIVE_TO_SCRIPT__"))
		return 10;

	FPlatformApplicationMisc::ClipboardCopy("__EXPECTED_SCRIPT_TO_NATIVE__");

	FString RoundTripValue;
	FPlatformApplicationMisc::ClipboardPaste(RoundTripValue);
	if (!(RoundTripValue == "__EXPECTED_SCRIPT_TO_NATIVE__"))
		return 20;

	return 1;
}
)");

		ScriptSource.ReplaceInline(
			TEXT("__EXPECTED_NATIVE_TO_SCRIPT__"),
			*EscapeScriptString(ExpectedNativeToScript),
			ESearchCase::CaseSensitive);
		ScriptSource.ReplaceInline(
			TEXT("__EXPECTED_SCRIPT_TO_NATIVE__"),
			*EscapeScriptString(ExpectedScriptToNative),
			ESearchCase::CaseSensitive);
		return ScriptSource;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptPlatformApplicationMiscBindingsTests_Private;

bool FAngelscriptPlatformApplicationMiscClipboardCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	const FString OriginalClipboardContents = ReadClipboard();
	const FString NativeSeedValue = FString::Printf(
		TEXT("AngelscriptClipboardNative_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ScriptSeedValue = FString::Printf(
		TEXT("AngelscriptClipboardScript_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));

	FPlatformApplicationMisc::ClipboardCopy(*NativeSeedValue);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(PlatformApplicationMiscModuleName));
		FPlatformApplicationMisc::ClipboardCopy(*OriginalClipboardContents);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		PlatformApplicationMiscModuleName,
		BuildScriptSource(NativeSeedValue, ScriptSeedValue));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	const FString NativeClipboardAfterScript = ReadClipboard();

	bPassed = TestEqual(
		TEXT("FPlatformApplicationMisc clipboard bindings should preserve native-to-script and script-to-native round-trip parity"),
		Result,
		1);
	bPassed &= TestEqual(
		TEXT("FPlatformApplicationMisc clipboard bindings should leave the native clipboard at the script-written value"),
		NativeClipboardAfterScript,
		ScriptSeedValue);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
