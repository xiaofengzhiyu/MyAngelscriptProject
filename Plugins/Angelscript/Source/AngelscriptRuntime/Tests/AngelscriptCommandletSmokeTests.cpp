#include "AngelscriptAllScriptRootsCommandlet.h"
#include "AngelscriptEngine.h"
#include "AngelscriptTestCommandlet.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptRuntime_Tests_AngelscriptCommandletSmokeTests_Private
{
	struct FCommandletContextStackGuard
	{
		TArray<FAngelscriptEngine*> SavedStack;

		FCommandletContextStackGuard()
		{
			SavedStack = FAngelscriptEngineContextStack::SnapshotAndClear();
		}

		~FCommandletContextStackGuard()
		{
			FAngelscriptEngineContextStack::RestoreSnapshot(MoveTemp(SavedStack));
		}
	};

	TUniquePtr<FAngelscriptEngine> CreateIsolatedCommandletEngine(FAutomationTestBase& Test)
	{
		const FAngelscriptEngineConfig Config;
		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		TUniquePtr<FAngelscriptEngine> OwnedEngine =
			FAngelscriptEngine::CreateForTesting(Config, Dependencies, EAngelscriptEngineCreationMode::Clone);
		Test.TestNotNull(TEXT("Commandlet smoke test should create an isolated Angelscript engine"), OwnedEngine.Get());
		return OwnedEngine;
	}
}

using namespace AngelscriptRuntime_Tests_AngelscriptCommandletSmokeTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestCommandletEmptyMainUsesCompileGateTest,
	"Angelscript.CppTests.Commandlet.TestCommandlet.EmptyMainUsesCompileGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptAllScriptRootsCommandletEmptyMainReturnsSuccessTest,
	"Angelscript.CppTests.Commandlet.AllScriptRoots.EmptyMainReturnsSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptTestCommandletEmptyMainUsesCompileGateTest::RunTest(const FString& Parameters)
{
	FCommandletContextStackGuard ContextGuard;
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateIsolatedCommandletEngine(*this);
	if (!TestNotNull(TEXT("TestCommandlet smoke test should keep the isolated engine alive"), OwnedEngine.Get()))
	{
		return false;
	}

	FAngelscriptEngineScope EngineScope(*OwnedEngine);
	if (!TestEqual(
			TEXT("TestCommandlet smoke test should start from an engine without active script modules"),
			OwnedEngine->GetActiveModules().Num(),
			0))
	{
		return false;
	}

	UAngelscriptTestCommandlet* Commandlet = NewObject<UAngelscriptTestCommandlet>(GetTransientPackage());
	if (!TestNotNull(TEXT("TestCommandlet smoke test should instantiate the commandlet object"), Commandlet))
	{
		return false;
	}

	const bool bOriginalDidInitialCompileSucceed = OwnedEngine->bDidInitialCompileSucceed;
	ON_SCOPE_EXIT
	{
		OwnedEngine->bDidInitialCompileSucceed = bOriginalDidInitialCompileSucceed;
	};

	OwnedEngine->bDidInitialCompileSucceed = true;
	if (!TestEqual(
			TEXT("TestCommandlet smoke test should return success for an empty Main() call after a successful compile"),
			Commandlet->Main(TEXT("")),
			0))
	{
		return false;
	}

	OwnedEngine->bDidInitialCompileSucceed = false;
	return TestEqual(
		TEXT("TestCommandlet smoke test should short-circuit to exit code 1 when the initial compile failed"),
		Commandlet->Main(TEXT("")),
		1);
}

bool FAngelscriptAllScriptRootsCommandletEmptyMainReturnsSuccessTest::RunTest(const FString& Parameters)
{
	const TArray<FString> AllScriptRoots = FAngelscriptEngine::MakeAllScriptRoots();
	if (!TestTrue(
			TEXT("AllScriptRoots commandlet smoke test should discover at least one script root"),
			AllScriptRoots.Num() > 0))
	{
		return false;
	}

	for (const FString& Root : AllScriptRoots)
	{
		if (!TestFalse(
				TEXT("AllScriptRoots commandlet smoke test should not emit empty script root entries"),
				Root.IsEmpty()))
		{
			return false;
		}
	}

	UAngelscriptAllScriptRootsCommandlet* Commandlet =
		NewObject<UAngelscriptAllScriptRootsCommandlet>(GetTransientPackage());
	if (!TestNotNull(TEXT("AllScriptRoots commandlet smoke test should instantiate the commandlet object"), Commandlet))
	{
		return false;
	}

	return TestEqual(
		TEXT("AllScriptRoots commandlet smoke test should return success for an empty Main() call"),
		Commandlet->Main(TEXT("")),
		0);
}

#endif
