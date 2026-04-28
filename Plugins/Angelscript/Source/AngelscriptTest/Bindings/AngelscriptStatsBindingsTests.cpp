#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptStatsBindingsTests_Private
{
	static constexpr ANSICHAR StatsTypeExposureModuleName[] = "ASStatsTypeExposureCompat";
	static constexpr ANSICHAR StatsObjectScopeModuleName[] = "ASStatsObjectScopeCompat";
}

using namespace AngelscriptTest_Bindings_AngelscriptStatsBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStatsTypeExposureBindingsTest,
	"Angelscript.TestModule.Bindings.Stats.TypeExposure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStatsObjectScopeBindingsTest,
	"Angelscript.TestModule.Bindings.Stats.ObjectScopeCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptStatsTypeExposureBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(StatsTypeExposureModuleName));
	};

	asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
	if (!TestNotNull(TEXT("Stats bindings test should expose a script engine"), ScriptEngine))
	{
		return false;
	}

	asITypeInfo* StatIdType = ScriptEngine->GetTypeInfoByName("FStatID");
	asITypeInfo* ScopeCycleCounterType = ScriptEngine->GetTypeInfoByName("FScopeCycleCounter");
	if (!TestNotNull(TEXT("Stats bindings test should expose FStatID in the script type system"), StatIdType)
		|| !TestNotNull(TEXT("Stats bindings test should expose FScopeCycleCounter in the script type system"), ScopeCycleCounterType))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		StatsTypeExposureModuleName,
		TEXT(R"AS(
int Entry()
{
	const FName NamedStat = n"Angelscript.Test.Stats.Named";
	FStatID Stat = FStatID(NamedStat);

	{
		FScopeCycleCounter NamedScope(Stat);
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
		TEXT("Stats bindings should expose FStatID and support FScopeCycleCounter construction from a named stat"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptStatsObjectScopeBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(StatsObjectScopeModuleName));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		StatsObjectScopeModuleName,
		TEXT(R"AS(
int Entry()
{
	UObject PackageObject = GetTransientPackage();
	if (PackageObject == null)
		return 10;

	{
		FScopeCycleCounter PackageScope(PackageObject);
	}

	UObject DefaultObject = UObject::StaticClass().GetDefaultObject();
	if (DefaultObject == null)
		return 20;

	{
		FScopeCycleCounter DefaultObjectScope(DefaultObject);
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
		TEXT("Stats bindings should support FScopeCycleCounter construction from script-visible UObject values"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
