#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "HAL/PlatformProcess.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPlatformProcessExactBindingsTest,
	"Angelscript.TestModule.Bindings.PlatformProcessExactCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptPlatformProcessExactBindingsTests_Private
{
	static constexpr ANSICHAR PlatformProcessExactModuleName[] = "ASPlatformProcessExactCompat";
	static constexpr TCHAR LaunchUrlLiteral[] = TEXT("https://example.com");

	struct FPlatformProcessBaseline
	{
		FString UserDir;
		FString UserSettingsDir;
		FString UserTempDir;
		FString ApplicationSettingsDir;
		FString ExecutablePath;
		FString ExecutableName;
		FString CurrentWorkingDirectory;
		FString ComputerName;
		FString UserName;
		FString GameBundleId;
		bool bCanLaunchUrl = false;
	};

	FString EscapeScriptString(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"), ESearchCase::CaseSensitive);
		return Escaped;
	}

	FString ToScriptBoolLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	void ReplaceToken(FString& ScriptSource, const TCHAR* Token, const FString& Replacement)
	{
		ScriptSource.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FPlatformProcessBaseline CaptureBaseline()
	{
		FPlatformProcessBaseline Baseline;
		Baseline.UserDir = FPlatformProcess::UserDir();
		Baseline.UserSettingsDir = FPlatformProcess::UserSettingsDir();
		Baseline.UserTempDir = FPlatformProcess::UserTempDir();
		Baseline.ApplicationSettingsDir = FPlatformProcess::ApplicationSettingsDir();
		Baseline.ExecutablePath = FPlatformProcess::ExecutablePath();
		Baseline.ExecutableName = FPlatformProcess::ExecutableName();
		Baseline.CurrentWorkingDirectory = FPlatformProcess::GetCurrentWorkingDirectory();
		Baseline.ComputerName = FPlatformProcess::ComputerName();
		Baseline.UserName = FPlatformProcess::UserName();
		Baseline.GameBundleId = FPlatformProcess::GetGameBundleId();
		Baseline.bCanLaunchUrl = FPlatformProcess::CanLaunchURL(LaunchUrlLiteral);
		return Baseline;
	}

	FString BuildScriptSource(const FPlatformProcessBaseline& Baseline)
	{
		FString ScriptSource = TEXT(R"(
int Entry()
{
	if (!(FPlatformProcess::UserDir() == "__USER_DIR__"))
		return 10;
	if (!(FPlatformProcess::UserSettingsDir() == "__USER_SETTINGS_DIR__"))
		return 20;
	if (!(FPlatformProcess::UserTempDir() == "__USER_TEMP_DIR__"))
		return 30;
	if (!(FPlatformProcess::ApplicationSettingsDir() == "__APPLICATION_SETTINGS_DIR__"))
		return 40;
	if (!(FPlatformProcess::ExecutablePath() == "__EXECUTABLE_PATH__"))
		return 50;
	if (!(FPlatformProcess::ExecutableName() == "__EXECUTABLE_NAME__"))
		return 60;
	if (!(FPlatformProcess::CurrentWorkingDirectory() == "__CURRENT_WORKING_DIRECTORY__"))
		return 70;
	if (!(FPlatformProcess::ComputerName() == "__COMPUTER_NAME__"))
		return 80;
	if (!(FPlatformProcess::UserName() == "__USER_NAME__"))
		return 90;
	if (FPlatformProcess::CanLaunchURL("__LAUNCH_URL__") != __CAN_LAUNCH_URL__)
		return 100;
	if (!(FPlatformProcess::GameBundleId() == "__GAME_BUNDLE_ID__"))
		return 110;

	return 1;
}
)");

		ReplaceToken(ScriptSource, TEXT("__USER_DIR__"), EscapeScriptString(Baseline.UserDir));
		ReplaceToken(ScriptSource, TEXT("__USER_SETTINGS_DIR__"), EscapeScriptString(Baseline.UserSettingsDir));
		ReplaceToken(ScriptSource, TEXT("__USER_TEMP_DIR__"), EscapeScriptString(Baseline.UserTempDir));
		ReplaceToken(ScriptSource, TEXT("__APPLICATION_SETTINGS_DIR__"), EscapeScriptString(Baseline.ApplicationSettingsDir));
		ReplaceToken(ScriptSource, TEXT("__EXECUTABLE_PATH__"), EscapeScriptString(Baseline.ExecutablePath));
		ReplaceToken(ScriptSource, TEXT("__EXECUTABLE_NAME__"), EscapeScriptString(Baseline.ExecutableName));
		ReplaceToken(ScriptSource, TEXT("__CURRENT_WORKING_DIRECTORY__"), EscapeScriptString(Baseline.CurrentWorkingDirectory));
		ReplaceToken(ScriptSource, TEXT("__COMPUTER_NAME__"), EscapeScriptString(Baseline.ComputerName));
		ReplaceToken(ScriptSource, TEXT("__USER_NAME__"), EscapeScriptString(Baseline.UserName));
		ReplaceToken(ScriptSource, TEXT("__LAUNCH_URL__"), EscapeScriptString(LaunchUrlLiteral));
		ReplaceToken(ScriptSource, TEXT("__CAN_LAUNCH_URL__"), ToScriptBoolLiteral(Baseline.bCanLaunchUrl));
		ReplaceToken(ScriptSource, TEXT("__GAME_BUNDLE_ID__"), EscapeScriptString(Baseline.GameBundleId));
		return ScriptSource;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptPlatformProcessExactBindingsTests_Private;

bool FAngelscriptPlatformProcessExactBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	const FPlatformProcessBaseline Baseline = CaptureBaseline();

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(PlatformProcessExactModuleName));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		PlatformProcessExactModuleName,
		BuildScriptSource(Baseline));
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

	bPassed = TestEqual(
		TEXT("FPlatformProcess exact bindings should match the same-run native getter and URL-launch baselines"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
