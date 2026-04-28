#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptCpuProfilerTraceBindingsTests_Private
{
	static constexpr ANSICHAR CpuProfilerTraceScopedModuleName[] = "ASCpuProfilerTraceScopedCompat";
}

using namespace AngelscriptTest_Bindings_AngelscriptCpuProfilerTraceBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptCpuProfilerTraceScopedBindingsTest,
	"Angelscript.TestModule.Bindings.CpuProfilerTraceScopedCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptCpuProfilerTraceScopedBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(CpuProfilerTraceScopedModuleName));
	};

	asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
	if (!TestNotNull(TEXT("CpuProfilerTraceScoped binding test should expose a script engine"), ScriptEngine))
	{
		return false;
	}

	asITypeInfo* ScopeType = ScriptEngine->GetTypeInfoByName("FCpuProfilerTraceScoped");
	if (!TestNotNull(TEXT("CpuProfilerTraceScoped binding test should expose FCpuProfilerTraceScoped in the script type system"), ScopeType))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		CpuProfilerTraceScopedModuleName,
		TEXT(R"AS(
int Entry()
{
	const FName OuterEvent = n"Angelscript.Test.CpuProfilerTraceScoped.Outer";
	const FName InnerEvent = n"Angelscript.Test.CpuProfilerTraceScoped.Inner";

	{
		FCpuProfilerTraceScoped OuterScope(OuterEvent);
		{
			FCpuProfilerTraceScoped InnerScope(InnerEvent);
		}
	}

	return 1;
}
)AS"));
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

	bPassed &= TestEqual(
		TEXT("FCpuProfilerTraceScoped binding should support script-visible FName construction and nested scope execution"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
