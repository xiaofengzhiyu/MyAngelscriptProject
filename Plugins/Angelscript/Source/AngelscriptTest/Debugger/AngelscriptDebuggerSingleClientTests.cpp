#include "Shared/AngelscriptDebuggerScriptFixture.h"
#include "Shared/AngelscriptDebuggerTestClient.h"
#include "Shared/AngelscriptDebuggerTestSession.h"
#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Debugger_AngelscriptDebuggerSingleClientTests_Private
{
	bool StartSingleClientDebuggerSession(FAutomationTestBase& Test, FAngelscriptDebuggerTestSession& Session)
	{
		FAngelscriptDebuggerSessionConfig SessionConfig;
		// UE 5.7: headless has no production subsystem. Let Initialize() create
		// an isolated FAngelscriptEngine with its own FAngelscriptDebugServer.
		SessionConfig.DefaultTimeoutSeconds = 45.0f;

		return Test.TestTrue(TEXT("Single-client debugger test should initialize the debugger session"), Session.Initialize(SessionConfig));
	}

	bool WaitForBreakpointCount(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		int32 ExpectedCount,
		const TCHAR* Context)
	{
		const bool bReachedCount = Session.PumpUntil(
			[&Session, ExpectedCount]()
			{
				return Session.GetDebugServer().BreakpointCount == ExpectedCount;
			},
			Session.GetDefaultTimeoutSeconds());

		if (!bReachedCount)
		{
			Test.AddError(FString::Printf(TEXT("%s (actual breakpoint count: %d)."), Context, Session.GetDebugServer().BreakpointCount));
		}

		return bReachedCount;
	}

	struct FAsyncModuleInvocationState : public TSharedFromThis<FAsyncModuleInvocationState>
	{
		TAtomic<bool> bCompleted = false;
		bool bSucceeded = false;
		int32 Result = 0;
	};

	TSharedRef<FAsyncModuleInvocationState> DispatchModuleInvocation(
		FAngelscriptEngine& Engine,
		const FString& Filename,
		FName ModuleName,
		const FString& Declaration)
	{
		TSharedRef<FAsyncModuleInvocationState> State = MakeShared<FAsyncModuleInvocationState>();

		AsyncTask(ENamedThreads::GameThread, [&Engine, Filename, ModuleName, Declaration, State]()
		{
			int32 InvocationResult = 0;
			State->bSucceeded = ExecuteIntFunction(&Engine, Filename, ModuleName, Declaration, InvocationResult);
			State->Result = InvocationResult;
			State->bCompleted = true;
		});

		return State;
	}

	bool WaitForInvocationCompletion(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		const TSharedRef<FAsyncModuleInvocationState>& InvocationState,
		const TCHAR* Context)
	{
		return Test.TestTrue(
			Context,
			Session.PumpUntil(
				[&InvocationState]()
				{
					return InvocationState->bCompleted.Load();
				},
				Session.GetDefaultTimeoutSeconds()));
	}

	bool WaitForWorkerReady(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		TAtomic<bool>& bWorkerReady,
		const TCHAR* Context)
	{
		return Test.TestTrue(
			Context,
			Session.PumpUntil(
				[&bWorkerReady]()
				{
					return bWorkerReady.Load();
				},
				Session.GetDefaultTimeoutSeconds()));
	}

	int32 CountMessagesByType(const FSingleClientDebuggerTranscript& Transcript, EDebugMessageType Type)
	{
		int32 Count = 0;
		for (const FAngelscriptDebugMessageEnvelope& Envelope : Transcript.ReceivedMessages)
		{
			if (Envelope.MessageType == Type)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 FindNthMessageIndexByType(const FSingleClientDebuggerTranscript& Transcript, EDebugMessageType Type, int32 Occurrence)
	{
		int32 SeenCount = 0;
		for (int32 Index = 0; Index < Transcript.ReceivedMessages.Num(); ++Index)
		{
			if (Transcript.ReceivedMessages[Index].MessageType != Type)
			{
				continue;
			}

			if (SeenCount == Occurrence)
			{
				return Index;
			}

			++SeenCount;
		}

		return INDEX_NONE;
	}

}

using namespace AngelscriptTest_Debugger_AngelscriptDebuggerSingleClientTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebuggerSingleClientBreakpointRoundtripTest,
	"Angelscript.TestModule.Debugger.SingleClient.BreakpointRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebuggerSingleClientStepInRoundtripTest,
	"Angelscript.TestModule.Debugger.SingleClient.StepInRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebuggerSingleClientBreakpointRoundtripTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptDebuggerTestSession Session;
	if (!StartSingleClientDebuggerSession(*this, Session))
	{
		return false;
	}

	FAngelscriptEngine& Engine = Session.GetEngine();
	ASTEST_BEGIN_SHARE
		const FAngelscriptDebuggerScriptFixture Fixture = FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture();
		TAtomic<bool> bWorkerReady{false};
		TAtomic<bool> bAbortWorker{false};
		bool bWorkerStarted = false;
		bool bWorkerJoined = false;
		TFuture<FSingleClientDebuggerTranscript> WorkerFuture;

		ON_SCOPE_EXIT
		{
			bAbortWorker = true;
			if (bWorkerStarted && !bWorkerJoined)
			{
				WorkerFuture.Wait();
			}

			Engine.DiscardModule(*Fixture.ModuleName.ToString());
			CollectGarbage(RF_NoFlags, true);
		};

		if (!TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should compile the breakpoint fixture"), Fixture.Compile(Engine)))
		{
			return false;
		}

		TSharedPtr<FAngelscriptModuleDesc> ModuleDesc = Engine.GetModuleByFilenameOrModuleName(Fixture.Filename, Fixture.ModuleName.ToString());
		if (!TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should resolve the compiled module immediately after compilation"), ModuleDesc.IsValid() && ModuleDesc->ScriptModule != nullptr))
		{
			return false;
		}

		FAngelscriptBreakpoint Breakpoint;
		Breakpoint.Filename = Fixture.Filename;
		Breakpoint.ModuleName = Fixture.ModuleName.ToString();
		Breakpoint.LineNumber = Fixture.GetLine(TEXT("BreakpointHelperLine"));

		FSingleClientDebuggerWorkerConfig WorkerConfig;
		WorkerConfig.TimeoutSeconds = Session.GetDefaultTimeoutSeconds();
		WorkerConfig.InitialBreakpoints.Add(Breakpoint);
		WorkerConfig.StopActions.AddDefaulted();

		WorkerFuture = RunSingleClientDebuggerWorker(Session.GetPort(), bWorkerReady, bAbortWorker, WorkerConfig);
		bWorkerStarted = true;

		if (!WaitForWorkerReady(*this, Session, bWorkerReady, TEXT("Debugger.SingleClient.BreakpointRoundtrip should finish handshake and breakpoint registration before execution")))
		{
			return false;
		}

		if (!WaitForBreakpointCount(*this, Session, 1, TEXT("Debugger.SingleClient.BreakpointRoundtrip should observe the breakpoint registration before running the script")))
		{
			return false;
		}

		const TSharedRef<FAsyncModuleInvocationState> InvocationState = DispatchModuleInvocation(
			Engine,
			Fixture.Filename,
			Fixture.ModuleName,
			Fixture.EntryFunctionDeclaration);

		if (!WaitForInvocationCompletion(*this, Session, InvocationState, TEXT("Debugger.SingleClient.BreakpointRoundtrip should complete script invocation after the same client sends Continue")))
		{
			return false;
		}

		FSingleClientDebuggerTranscript Transcript = WorkerFuture.Get();
		bWorkerJoined = true;

		if (!Transcript.Error.IsEmpty())
		{
			AddError(Transcript.Error);
			return false;
		}

		TestFalse(TEXT("Debugger.SingleClient.BreakpointRoundtrip should not time out"), Transcript.bTimedOut);
		if (!TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should receive DebugServerVersion on the same client"), Transcript.DebugServerVersion.IsSet()))
		{
			return false;
		}

		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should keep the adapter handshake on the same socket"), Transcript.DebugServerVersion->DebugServerVersion, DEBUG_SERVER_VERSION);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should receive exactly one DebugServerVersion envelope"), CountMessagesByType(Transcript, EDebugMessageType::DebugServerVersion), 1);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should receive exactly one HasStopped envelope"), CountMessagesByType(Transcript, EDebugMessageType::HasStopped), 1);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should receive exactly one CallStack envelope"), CountMessagesByType(Transcript, EDebugMessageType::CallStack), 1);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should receive exactly one HasContinued envelope"), CountMessagesByType(Transcript, EDebugMessageType::HasContinued), 1);
		const int32 PingAliveCount = CountMessagesByType(Transcript, EDebugMessageType::PingAlive);
		TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should observe at most one PingAlive heartbeat during the roundtrip"), PingAliveCount <= 1);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should only observe handshake, stop, callstack, continue and optional PingAlive"), Transcript.ReceivedMessages.Num(), 4 + PingAliveCount);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should deserialize exactly one stop message"), Transcript.StopMessages.Num(), 1);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should deserialize exactly one callstack"), Transcript.CallStacks.Num(), 1);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should count exactly one HasContinued notification"), Transcript.HasContinuedCount, 1);
		TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should finish the script invocation successfully"), InvocationState->bSucceeded);
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should preserve the expected script result"), InvocationState->Result, 8);

		if (!TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should deserialize the stop reason"), Transcript.StopMessages.Num() == 1))
		{
			return false;
		}
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should stop because of a breakpoint"), Transcript.StopMessages[0].Reason, FString(TEXT("breakpoint")));

		if (!TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should capture exactly one callstack"), Transcript.CallStacks.Num() == 1))
		{
			return false;
		}

		const FAngelscriptCallStack& CallStack = Transcript.CallStacks[0];
		if (!TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should return at least one frame"), CallStack.Frames.Num() > 0))
		{
			return false;
		}

		TestTrue(TEXT("Debugger.SingleClient.BreakpointRoundtrip should report the fixture filename in the top stack frame"), CallStack.Frames[0].Source.EndsWith(Fixture.Filename));
		TestEqual(TEXT("Debugger.SingleClient.BreakpointRoundtrip should stop at the requested helper line"), CallStack.Frames[0].LineNumber, Fixture.GetLine(TEXT("BreakpointHelperLine")));

		bPassed = true;
	ASTEST_END_SHARE

	return bPassed;
}

bool FAngelscriptDebuggerSingleClientStepInRoundtripTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptDebuggerTestSession Session;
	if (!StartSingleClientDebuggerSession(*this, Session))
	{
		return false;
	}

	FAngelscriptEngine& Engine = Session.GetEngine();
	ASTEST_BEGIN_SHARE
		const FAngelscriptDebuggerScriptFixture Fixture = FAngelscriptDebuggerScriptFixture::CreateSteppingFixture();
		TAtomic<bool> bWorkerReady{false};
		TAtomic<bool> bAbortWorker{false};
		bool bWorkerStarted = false;
		bool bWorkerJoined = false;
		TFuture<FSingleClientDebuggerTranscript> WorkerFuture;

		ON_SCOPE_EXIT
		{
			bAbortWorker = true;
			if (bWorkerStarted && !bWorkerJoined)
			{
				WorkerFuture.Wait();
			}

			Engine.DiscardModule(*Fixture.ModuleName.ToString());
			CollectGarbage(RF_NoFlags, true);
		};

		if (!TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should compile the stepping fixture"), Fixture.Compile(Engine)))
		{
			return false;
		}

		TSharedPtr<FAngelscriptModuleDesc> ModuleDesc = Engine.GetModuleByFilenameOrModuleName(Fixture.Filename, Fixture.ModuleName.ToString());
		if (!TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should resolve the compiled module immediately after compilation"), ModuleDesc.IsValid() && ModuleDesc->ScriptModule != nullptr))
		{
			return false;
		}

		FAngelscriptBreakpoint Breakpoint;
		Breakpoint.Filename = Fixture.Filename;
		Breakpoint.ModuleName = Fixture.ModuleName.ToString();
		Breakpoint.LineNumber = Fixture.GetLine(TEXT("StepCallLine"));

		FSingleClientDebuggerWorkerConfig WorkerConfig;
		WorkerConfig.TimeoutSeconds = Session.GetDefaultTimeoutSeconds();
		WorkerConfig.InitialBreakpoints.Add(Breakpoint);
		WorkerConfig.StopActions.Add({ true, ESingleClientDebuggerCommand::StepIn });
		WorkerConfig.StopActions.Add({ true, ESingleClientDebuggerCommand::Continue });

		WorkerFuture = RunSingleClientDebuggerWorker(Session.GetPort(), bWorkerReady, bAbortWorker, WorkerConfig);
		bWorkerStarted = true;

		if (!WaitForWorkerReady(*this, Session, bWorkerReady, TEXT("Debugger.SingleClient.StepInRoundtrip should finish handshake and breakpoint registration before execution")))
		{
			return false;
		}

		if (!WaitForBreakpointCount(*this, Session, 1, TEXT("Debugger.SingleClient.StepInRoundtrip should observe the breakpoint registration before running the script")))
		{
			return false;
		}

		const TSharedRef<FAsyncModuleInvocationState> InvocationState = DispatchModuleInvocation(
			Engine,
			Fixture.Filename,
			Fixture.ModuleName,
			Fixture.EntryFunctionDeclaration);

		if (!WaitForInvocationCompletion(*this, Session, InvocationState, TEXT("Debugger.SingleClient.StepInRoundtrip should complete script invocation after the same client steps in and continues")))
		{
			return false;
		}

		FSingleClientDebuggerTranscript Transcript = WorkerFuture.Get();
		bWorkerJoined = true;

		if (!Transcript.Error.IsEmpty())
		{
			AddError(Transcript.Error);
			return false;
		}

		TestFalse(TEXT("Debugger.SingleClient.StepInRoundtrip should not time out"), Transcript.bTimedOut);
		if (!TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should receive DebugServerVersion on the same client"), Transcript.DebugServerVersion.IsSet()))
		{
			return false;
		}

		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should keep the adapter handshake on the same socket"), Transcript.DebugServerVersion->DebugServerVersion, DEBUG_SERVER_VERSION);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should receive exactly one DebugServerVersion envelope"), CountMessagesByType(Transcript, EDebugMessageType::DebugServerVersion), 1);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should receive exactly two HasStopped envelopes"), CountMessagesByType(Transcript, EDebugMessageType::HasStopped), 2);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should receive exactly two CallStack envelopes"), CountMessagesByType(Transcript, EDebugMessageType::CallStack), 2);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should receive exactly two HasContinued envelopes"), CountMessagesByType(Transcript, EDebugMessageType::HasContinued), 2);
		const int32 PingAliveCount = CountMessagesByType(Transcript, EDebugMessageType::PingAlive);
		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should observe at most one PingAlive heartbeat during the roundtrip"), PingAliveCount <= 1);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should only observe handshake, two stops, two callstacks, two continue notifications and optional PingAlive"), Transcript.ReceivedMessages.Num(), 7 + PingAliveCount);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should deserialize exactly two stop messages"), Transcript.StopMessages.Num(), 2);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should deserialize exactly two callstacks"), Transcript.CallStacks.Num(), 2);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should count exactly two HasContinued notifications"), Transcript.HasContinuedCount, 2);
		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should finish the script invocation successfully"), InvocationState->bSucceeded);
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should preserve the stepping fixture result"), InvocationState->Result, 14);

		if (!TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should deserialize exactly two stop reasons"), Transcript.StopMessages.Num() == 2))
		{
			return false;
		}

		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should stop on the breakpoint before stepping"), Transcript.StopMessages[0].Reason, FString(TEXT("breakpoint")));
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should report a step stop after sending StepIn"), Transcript.StopMessages[1].Reason, FString(TEXT("step")));

		if (!TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should capture exactly two callstacks"), Transcript.CallStacks.Num() == 2))
		{
			return false;
		}

		const FAngelscriptCallStack& BreakpointCallStack = Transcript.CallStacks[0];
		if (!TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should return at least one frame for the breakpoint stop"), BreakpointCallStack.Frames.Num() > 0))
		{
			return false;
		}

		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should report the stepping fixture filename in the first stop"), BreakpointCallStack.Frames[0].Source.EndsWith(Fixture.Filename));
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should first stop at the call line"), BreakpointCallStack.Frames[0].LineNumber, Fixture.GetLine(TEXT("StepCallLine")));

		const FAngelscriptCallStack& StepCallStack = Transcript.CallStacks[1];
		if (!TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should return at least two frames after StepIn"), StepCallStack.Frames.Num() >= 2))
		{
			return false;
		}

		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should stay inside the stepping fixture after StepIn"), StepCallStack.Frames[0].Source.EndsWith(Fixture.Filename));
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should enter Inner() on the second stop"), StepCallStack.Frames[0].LineNumber, Fixture.GetLine(TEXT("StepInnerEntryLine")));
		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should preserve the caller frame on the second stop"), StepCallStack.Frames[1].Source.EndsWith(Fixture.Filename));
		TestEqual(TEXT("Debugger.SingleClient.StepInRoundtrip should keep the caller frame at the call line"), StepCallStack.Frames[1].LineNumber, Fixture.GetLine(TEXT("StepCallLine")));

		const int32 VersionIndex = FindNthMessageIndexByType(Transcript, EDebugMessageType::DebugServerVersion, 0);
		const int32 FirstStopIndex = FindNthMessageIndexByType(Transcript, EDebugMessageType::HasStopped, 0);
		const int32 FirstCallstackIndex = FindNthMessageIndexByType(Transcript, EDebugMessageType::CallStack, 0);
		const int32 FirstContinuedIndex = FindNthMessageIndexByType(Transcript, EDebugMessageType::HasContinued, 0);
		const int32 SecondStopIndex = FindNthMessageIndexByType(Transcript, EDebugMessageType::HasStopped, 1);
		const int32 SecondCallstackIndex = FindNthMessageIndexByType(Transcript, EDebugMessageType::CallStack, 1);
		const int32 SecondContinuedIndex = FindNthMessageIndexByType(Transcript, EDebugMessageType::HasContinued, 1);
		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should observe the initial handshake before the first stop"), VersionIndex != INDEX_NONE && FirstStopIndex != INDEX_NONE && VersionIndex < FirstStopIndex);
		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should request the first callstack before the StepIn continue notification"), FirstStopIndex != INDEX_NONE && FirstCallstackIndex != INDEX_NONE && FirstContinuedIndex != INDEX_NONE && FirstStopIndex < FirstCallstackIndex && FirstCallstackIndex < FirstContinuedIndex);
		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should receive the second stop after the StepIn continue notification"), FirstContinuedIndex != INDEX_NONE && SecondStopIndex != INDEX_NONE && FirstContinuedIndex < SecondStopIndex);
		TestTrue(TEXT("Debugger.SingleClient.StepInRoundtrip should request the second callstack before the final continue notification"), SecondStopIndex != INDEX_NONE && SecondCallstackIndex != INDEX_NONE && SecondContinuedIndex != INDEX_NONE && SecondStopIndex < SecondCallstackIndex && SecondCallstackIndex < SecondContinuedIndex);

		bPassed = true;
	ASTEST_END_SHARE

	return bPassed;
}

#endif

