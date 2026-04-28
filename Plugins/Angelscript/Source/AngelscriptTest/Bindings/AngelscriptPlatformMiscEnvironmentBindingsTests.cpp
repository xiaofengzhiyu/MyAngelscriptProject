#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptPlatformMiscEnvironmentBindingsTests_Private
{
	static constexpr ANSICHAR PlatformMiscEnvironmentModuleName[] = "ASPlatformMiscEnvironmentCompat";

	FString EscapeScriptString(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return Value;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptPlatformMiscEnvironmentBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPlatformMiscEnvironmentCompatBindingsTest,
	"Angelscript.TestModule.Bindings.PlatformMisc.EnvironmentCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPlatformMiscEnvironmentCompatBindingsTest::RunTest(const FString& Parameters)
{
	const FString PathValue = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	const FString MissingVariableName = FString::Printf(
		TEXT("ANGELSCRIPT_TEST_MISSING_ENV_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString MissingValue = FPlatformMisc::GetEnvironmentVariable(*MissingVariableName);

	if (!TestFalse(TEXT("PlatformMisc environment fixture should choose a missing variable name"), !MissingValue.IsEmpty()))
	{
		return false;
	}

	FString Script = TEXT(R"AS(
int Entry()
{
	if (!(FPlatformMisc::GetEnvironmentVariable("PATH") == "__PATH_VALUE__"))
		return 10;
	if (!FPlatformMisc::GetEnvironmentVariable("__MISSING_VARIABLE__").IsEmpty())
		return 20;

	return 1;
}
)AS");
	Script.ReplaceInline(TEXT("__PATH_VALUE__"), *EscapeScriptString(PathValue), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__MISSING_VARIABLE__"), *EscapeScriptString(MissingVariableName), ESearchCase::CaseSensitive);

	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(*this, Engine, PlatformMiscEnvironmentModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("FPlatformMisc::GetEnvironmentVariable should match native same-run PATH and empty missing-variable baselines"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
