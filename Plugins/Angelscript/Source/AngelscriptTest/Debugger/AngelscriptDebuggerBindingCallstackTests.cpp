#include "Shared/AngelscriptDebuggerScriptFixture.h"
#include "Shared/AngelscriptDebuggerTestSession.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Debugger_AngelscriptDebuggerBindingCallstackTests_Private
{
	struct FBindingCallstackFixtureRuntime
	{
		UClass* GeneratedClass = nullptr;
		UFunction* TriggerFormattedCallstackFunction = nullptr;
		UObject* Object = nullptr;
	};

	bool ResolveBindingCallstackFixtureRuntime(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FAngelscriptDebuggerScriptFixture& Fixture,
		FBindingCallstackFixtureRuntime& OutRuntime)
	{
		OutRuntime.GeneratedClass = Fixture.FindGeneratedClass(Engine);
		if (!Test.TestNotNull(TEXT("Debugger.Binding.FormatCallstackString should resolve the generated binding fixture class"), OutRuntime.GeneratedClass))
		{
			return false;
		}

		OutRuntime.TriggerFormattedCallstackFunction = Fixture.FindGeneratedFunction(Engine, TEXT("TriggerFormattedCallstack"));
		if (!Test.TestNotNull(TEXT("Debugger.Binding.FormatCallstackString should resolve TriggerFormattedCallstack on the generated binding fixture"), OutRuntime.TriggerFormattedCallstackFunction))
		{
			return false;
		}

		OutRuntime.Object = NewObject<UObject>(GetTransientPackage(), OutRuntime.GeneratedClass);
		return Test.TestNotNull(TEXT("Debugger.Binding.FormatCallstackString should create a runtime UObject from the generated binding fixture class"), OutRuntime.Object);
	}

	bool WaitForStringInvocationCompletion(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		const TSharedRef<FAsyncGeneratedStringInvocationState>& InvocationState,
		const TCHAR* Context)
	{
		const bool bCompleted = Session.PumpUntil(
			[&InvocationState]()
			{
				return InvocationState->bCompleted.Load();
			},
			Session.GetDefaultTimeoutSeconds());

		return Test.TestTrue(Context, bCompleted);
	}
}

using namespace AngelscriptTest_Debugger_AngelscriptDebuggerBindingCallstackTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebuggerBindingFormatCallstackStringTest,
	"Angelscript.TestModule.Debugger.Binding.FormatCallstackString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebuggerBindingFormatCallstackStringTest::RunTest(const FString& Parameters)
{
	FAngelscriptDebuggerSessionConfig SessionConfig;
	SessionConfig.DefaultTimeoutSeconds = 45.0f;

	FAngelscriptDebuggerTestSession Session;
	if (!TestTrue(TEXT("Debugger.Binding.FormatCallstackString should initialize a debugger test session"), Session.Initialize(SessionConfig)))
	{
		return false;
	}

	const FAngelscriptDebuggerScriptFixture Fixture = FAngelscriptDebuggerScriptFixture::CreateBindingFixture();
	FAngelscriptEngine& Engine = Session.GetEngine();

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*Fixture.ModuleName.ToString());
		CollectGarbage(RF_NoFlags, true);
	};

	if (!TestTrue(TEXT("Debugger.Binding.FormatCallstackString should compile the binding fixture"), Fixture.Compile(Engine)))
	{
		return false;
	}

	FBindingCallstackFixtureRuntime Runtime;
	if (!ResolveBindingCallstackFixtureRuntime(*this, Engine, Fixture, Runtime))
	{
		return false;
	}

	const TSharedRef<FAsyncGeneratedStringInvocationState> InvocationState = DispatchGeneratedStringInvocation(
		Engine,
		Runtime.Object,
		Runtime.TriggerFormattedCallstackFunction);
	if (!WaitForStringInvocationCompletion(
			*this,
			Session,
			InvocationState,
			TEXT("Debugger.Binding.FormatCallstackString should finish TriggerFormattedCallstack on the game thread")))
	{
		return false;
	}

	if (!TestTrue(TEXT("Debugger.Binding.FormatCallstackString should invoke TriggerFormattedCallstack successfully"), InvocationState->bSucceeded))
	{
		return false;
	}

	const FString& FormattedCallstack = InvocationState->ReturnValue;
	TestFalse(TEXT("Debugger.Binding.FormatCallstackString should return a non-empty formatted callstack"), FormattedCallstack.IsEmpty());
	TestTrue(TEXT("Debugger.Binding.FormatCallstackString should include TriggerFormattedCallstack in the formatted stack"), FormattedCallstack.Contains(TEXT("TriggerFormattedCallstack")));
	TestTrue(TEXT("Debugger.Binding.FormatCallstackString should include FormatCurrentCallstack in the formatted stack"), FormattedCallstack.Contains(TEXT("FormatCurrentCallstack")));
	TestTrue(TEXT("Debugger.Binding.FormatCallstackString should include the binding fixture filename in the formatted stack"), FormattedCallstack.Contains(Fixture.Filename));

	return !HasAnyErrors();
}

#endif
