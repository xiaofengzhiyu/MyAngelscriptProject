#include "AngelscriptConsoleBindingsSections.h"

#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptTestBindings;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptConsoleCommandEmptyArgsMarkerBindingsTest,
	"Angelscript.TestModule.Bindings.ConsoleCommandArgumentMarshalling.EmptyArgsMarker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptConsoleCommandContentAndOrderBindingsTest,
	"Angelscript.TestModule.Bindings.ConsoleCommandArgumentMarshalling.ContentAndOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptConsoleCommandEmptyArgsMarkerBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	bool bPassed = true;
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT { ResetSharedCloneEngine(Engine); };
	bPassed &= RunConsoleCommandArgumentEmptySection(*this, Engine, GetConsoleBindingsProfile());
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptConsoleCommandContentAndOrderBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	bool bPassed = true;
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT { ResetSharedCloneEngine(Engine); };
	bPassed &= RunConsoleCommandArgumentContentSection(*this, Engine, GetConsoleBindingsProfile());
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
