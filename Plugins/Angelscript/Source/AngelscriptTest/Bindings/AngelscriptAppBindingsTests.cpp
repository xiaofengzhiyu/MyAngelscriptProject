#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/App.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptAppBindingsTest,
	"Angelscript.TestModule.Bindings.AppCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AppBindingsTests_Private
{
	static constexpr ANSICHAR AppCompatModuleName[] = "ASAppCompat";

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
}

using namespace AngelscriptTest_Bindings_AppBindingsTests_Private;

bool FAngelscriptAppBindingsTest::RunTest(const FString& Parameters)
{
	const bool bExpectedCanEverRender = FApp::CanEverRender();
	const FString ExpectedProjectName = FApp::GetProjectName();

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(AppCompatModuleName));
	};

	FString Script = TEXT(R"(
int Entry()
{
	if (FApp::CanEverRender() != __EXPECTED_CAN_EVER_RENDER__)
		return 10;

	if (!(FApp::GetProjectName() == "__EXPECTED_PROJECT_NAME__"))
		return 20;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("__EXPECTED_CAN_EVER_RENDER__"), *ToScriptBoolLiteral(bExpectedCanEverRender), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_PROJECT_NAME__"), *EscapeScriptString(ExpectedProjectName), ESearchCase::CaseSensitive);

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		AppCompatModuleName,
		Script);
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

	TestEqual(TEXT("FApp bindings should match the same-run native app state"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return true;
}

#endif
