#include "AngelscriptPerformanceStats.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPerformanceInstrumentationScopeCatalogTest,
	"Angelscript.TestModule.Core.Performance.InstrumentationScopeCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPerformanceInstrumentationScopeCatalogTest::RunTest(const FString& Parameters)
{
	const TArray<FName> ScopeNames = FAngelscriptPerformanceStats::GetKnownScopeNamesForTesting();
	const TSet<FName> UniqueScopeNames(ScopeNames);
	TestEqual(TEXT("Performance instrumentation scope catalog should not contain duplicates"), ScopeNames.Num(), UniqueScopeNames.Num());

	const FName ExpectedScopes[] = {
		TEXT("Angelscript.Startup.BindDatabase"),
		TEXT("Angelscript.Startup.BindScriptTypes"),
		TEXT("Angelscript.Binds.CallBinds"),
		TEXT("Angelscript.Compile.Initial"),
		TEXT("Angelscript.Compile.Modules"),
		TEXT("Angelscript.Reload.HotReload"),
		TEXT("Angelscript.ClassGenerator.Setup"),
		TEXT("Angelscript.ClassGenerator.Reload"),
		TEXT("Angelscript.RuntimeCall.BPVM.JIT"),
		TEXT("Angelscript.RuntimeCall.Parms.Context"),
		TEXT("Angelscript.StaticJIT.PrecompiledData"),
		TEXT("Angelscript.DebugServer.Tick"),
		TEXT("Angelscript.Dump.All"),
		TEXT("Angelscript.Commandlet.BlueprintImpact")
	};

	bool bAllScopesPresent = true;
	for (const FName& ExpectedScope : ExpectedScopes)
	{
		const bool bContainsScope = UniqueScopeNames.Contains(ExpectedScope);
		TestTrue(*FString::Printf(TEXT("Performance instrumentation should register scope %s"), *ExpectedScope.ToString()), bContainsScope);
		bAllScopesPresent &= bContainsScope;
	}

	return bAllScopesPresent;
}

#endif
