#include "AngelscriptTArrayBindingCoverage.h"

#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptTest_Bindings_TArrayCoverage;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptArraySyntaxCompatBindingsTest,
	"Angelscript.TestModule.Bindings.ArraySyntaxCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptArraySyntaxCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		ResetSharedCloneEngine(Engine);
	};

	bPassed &= RunTArrayBindingCoverageSections(
		*this,
		Engine,
		EArraySyntaxCoverage::ShorthandArray);
	ASTEST_END_SHARE_CLEAN

	return bPassed;
}

#endif
