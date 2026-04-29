#include "AngelscriptConsoleBindingsSections.h"

#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptTestBindings;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptConsoleCommandLifecycleBindingsTest,
	"Angelscript.TestModule.Bindings.ConsoleCommandLifecycleOriginalReplacementUnload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptConsoleCommandLifecycleBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	bool bPassed = true;
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT { ResetSharedCloneEngine(Engine); };
	bPassed &= RunConsoleCommandLifecycleSection(*this, Engine, GetConsoleBindingsProfile());
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
