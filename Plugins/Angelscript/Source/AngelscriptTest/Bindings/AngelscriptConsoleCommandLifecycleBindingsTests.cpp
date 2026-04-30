#include "AngelscriptConsoleBindingsSections.h"

#include "CQTest.h"
#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptTestBindings;

TEST_CLASS_WITH_FLAGS(FAngelscriptConsoleCommandLifecycleBindingsTest,
	"Angelscript.TestModule.Bindings.ConsoleCommandLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
	BEFORE_ALL()
	{
		ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	}

	AFTER_ALL()
	{
		FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
		AngelscriptTestSupport::ResetSharedCloneEngine(Engine);
	}

	TEST_METHOD(OriginalReplacementUnload)
	{
		FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
		FAngelscriptEngineScope Scope(Engine);
		RunConsoleCommandLifecycleSection(*TestRunner, Engine, GetConsoleBindingsProfile());
	}
};

#endif
