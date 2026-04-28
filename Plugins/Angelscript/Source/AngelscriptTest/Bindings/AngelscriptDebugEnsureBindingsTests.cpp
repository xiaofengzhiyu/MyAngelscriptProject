#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Binds/Bind_Debugging.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugEnsureDeduplicationBindingsTest,
	"Angelscript.TestModule.Bindings.Debugging.EnsureDeduplicatesAndResetCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugEnsureAlwaysBindingsTest,
	"Angelscript.TestModule.Bindings.Debugging.EnsureAlwaysLogsEveryInvocationCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptDebugEnsureBindingsTests_Private
{
	static constexpr ANSICHAR EnsureDedupModuleName[] = "ASDebugEnsureDedupCompat";
	static constexpr ANSICHAR EnsureAlwaysModuleName[] = "ASDebugEnsureAlwaysCompat";

	struct FScopedDebugBreakState
	{
		explicit FScopedDebugBreakState(const bool bEnableDebugBreaks)
			: bOriginalEnabled(AreAngelscriptDebugBreaksEnabledForTesting())
		{
			if (bEnableDebugBreaks)
			{
				AngelscriptEnableDebugBreaks();
			}
			else
			{
				AngelscriptDisableDebugBreaks();
			}
		}

		~FScopedDebugBreakState()
		{
			if (bOriginalEnabled)
			{
				AngelscriptEnableDebugBreaks();
			}
			else
			{
				AngelscriptDisableDebugBreaks();
			}
		}

		bool bOriginalEnabled = false;
	};

	struct FScopedSeenEnsureReset
	{
		FScopedSeenEnsureReset()
		{
			AngelscriptForgetSeenEnsures();
		}

		~FScopedSeenEnsureReset()
		{
			AngelscriptForgetSeenEnsures();
		}
	};

	FString BuildEnsureDedupScript()
	{
		return TEXT(R"AS(
int Entry()
{
	int Failures = 0;
	if (!ensure(true, "EnsureTruePasses"))
	{
		Failures |= 1;
	}

	for (int Index = 0; Index < 2; ++Index)
	{
		if (ensure(false, "EnsureOnceCompat"))
		{
			Failures |= (2 << Index);
		}
	}

	return Failures;
}
)AS");
	}

	FString BuildEnsureAlwaysScript()
	{
		return TEXT(R"AS(
int Entry()
{
	int Failures = 0;
	if (!ensureAlways(true, "EnsureAlwaysTruePasses"))
	{
		Failures |= 1;
	}

	for (int Index = 0; Index < 2; ++Index)
	{
		if (ensureAlways(false, "EnsureAlwaysCompat"))
		{
			Failures |= (2 << Index);
		}
	}

	return Failures;
}
)AS");
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptDebugEnsureBindingsTests_Private;

bool FAngelscriptDebugEnsureDeduplicationBindingsTest::RunTest(const FString& Parameters)
{
#if DO_CHECK && !USING_CODE_ANALYSIS
	AddExpectedErrorPlain(TEXT("Ensure condition failed: EnsureOnceCompat"), EAutomationExpectedErrorFlags::Contains, 2);
#endif

	bool bPassed = true;
	FScopedDebugBreakState DebugBreakState(false);
	FScopedSeenEnsureReset EnsureReset;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(EnsureDedupModuleName));
		ResetSharedCloneEngine(Engine);
	};

	bPassed &= TestFalse(
		TEXT("Debugging ensure deduplication test should disable native debug breaks while exercising ensure"),
		AreAngelscriptDebugBreaksEnabledForTesting());

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		EnsureDedupModuleName,
		BuildEnsureDedupScript());
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 FirstResult = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, FirstResult))
	{
		return false;
	}

	AngelscriptForgetSeenEnsures();

	int32 SecondResult = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, SecondResult))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("ensure(true) should preserve success and ensure(false) should return false while deduplicating repeated logs per source location"),
		FirstResult,
		0);
	bPassed &= TestEqual(
		TEXT("AngelscriptForgetSeenEnsures should allow the same ensure location to report again on the next execution"),
		SecondResult,
		0);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptDebugEnsureAlwaysBindingsTest::RunTest(const FString& Parameters)
{
#if DO_CHECK && !USING_CODE_ANALYSIS
	AddExpectedErrorPlain(TEXT("Ensure condition failed: EnsureAlwaysCompat"), EAutomationExpectedErrorFlags::Contains, 2);
#endif

	bool bPassed = true;
	FScopedDebugBreakState DebugBreakState(false);
	FScopedSeenEnsureReset EnsureReset;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(EnsureAlwaysModuleName));
		ResetSharedCloneEngine(Engine);
	};

	bPassed &= TestFalse(
		TEXT("Debugging ensureAlways test should disable native debug breaks while exercising repeated ensureAlways failures"),
		AreAngelscriptDebugBreaksEnabledForTesting());

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		EnsureAlwaysModuleName,
		BuildEnsureAlwaysScript());
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
		TEXT("ensureAlways(true) should preserve success and ensureAlways(false) should return false on every invocation"),
		Result,
		0);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
