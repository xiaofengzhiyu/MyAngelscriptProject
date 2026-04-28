#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptGenericPlatformMiscBindingsTests_Private
{
	static constexpr ANSICHAR GenericPlatformMiscModuleName[] = "ASGenericPlatformMiscCompileSurface";

	FString BuildScriptSource()
	{
		return TEXT(R"(
void TouchRequestExitCompileOnly()
{
	FGenericPlatformMisc::RequestExit(false);
}

int Entry()
{
	return 1;
}
)");
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptGenericPlatformMiscBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGenericPlatformMiscCompileSurfaceBindingsTest,
	"Angelscript.TestModule.Bindings.GenericPlatformMisc.CompileSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGenericPlatformMiscCompileSurfaceBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(GenericPlatformMiscModuleName));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		GenericPlatformMiscModuleName,
		BuildScriptSource());
	if (Module == nullptr)
	{
		return false;
	}

	bPassed &= TestNotNull(
		TEXT("FGenericPlatformMisc compile-surface smoke should expose the RequestExit compile-only helper"),
		GetFunctionByDecl(*this, *Module, TEXT("void TouchRequestExitCompileOnly()")));

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

	bPassed &= TestEqual(
		TEXT("FGenericPlatformMisc compile-surface smoke should keep the safe entry path executable without invoking RequestExit"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
