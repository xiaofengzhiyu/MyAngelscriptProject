#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSetAppendAndCopyIsolationBindingsTest,
	"Angelscript.TestModule.Bindings.SetAppendAndCopyIsolationCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptSetAppendAndCopyIsolationBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASSetAppendAndCopyIsolationCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASSetAppendAndCopyIsolationCompat",
		TEXT(R"(
int Entry()
{
	TSet<int> SourceSet;
	SourceSet.Add(1);
	SourceSet.Add(4);

	TArray<int> SourceArray;
	SourceArray.Add(4);
	SourceArray.Add(7);
	SourceArray.Add(7);

	TSet<int> Combined;
	Combined.Append(SourceArray);
	if (Combined.Num() != 2)
		return 10;
	if (!Combined.Contains(4) || !Combined.Contains(7) || Combined.Contains(1))
		return 20;

	Combined.Append(SourceSet);
	if (Combined.Num() != 3)
		return 30;
	if (!Combined.Contains(1) || !Combined.Contains(4) || !Combined.Contains(7))
		return 40;

	TSet<int> Copy = Combined;
	Copy.Add(9);
	if (!Copy.Remove(1))
		return 50;
	if (!Copy.Contains(4) || !Copy.Contains(7) || !Copy.Contains(9) || Copy.Contains(1))
		return 60;
	if (Combined.Num() != 3)
		return 70;
	if (!Combined.Contains(1) || !Combined.Contains(4) || !Combined.Contains(7) || Combined.Contains(9))
		return 80;

	TSet<int> Assigned;
	Assigned.Add(42);
	Assigned.Add(99);
	Assigned = Combined;
	if (Assigned.Num() != 3)
		return 90;
	if (!Assigned.Contains(1) || !Assigned.Contains(4) || !Assigned.Contains(7))
		return 100;
	if (Assigned.Contains(42) || Assigned.Contains(99))
		return 110;

	Assigned.Empty(8);
	if (!Assigned.IsEmpty())
		return 120;
	if (Assigned.Contains(1) || Assigned.Contains(4) || Assigned.Contains(7))
		return 130;
	if (Combined.Num() != 3)
		return 140;
	if (!Combined.Contains(1) || !Combined.Contains(4) || !Combined.Contains(7) || Combined.Contains(42) || Combined.Contains(99))
		return 150;

	return 1;
}
)"));
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

	TestEqual(TEXT("TSet append/copy-isolation compatibility should satisfy the full regression matrix"), Result, 1);
	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
