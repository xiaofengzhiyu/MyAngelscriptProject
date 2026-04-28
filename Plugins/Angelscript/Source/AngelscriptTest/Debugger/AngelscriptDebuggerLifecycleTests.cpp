#include "Shared/AngelscriptDebuggerScriptFixture.h"
#include "Shared/AngelscriptDebuggerTestClient.h"
#include "Shared/AngelscriptDebuggerTestSession.h"
#include "Shared/AngelscriptTestEngineHelper.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Debugger_AngelscriptDebuggerLifecycleTests_Private
{
	bool StartLifecycleSession(FAutomationTestBase& Test, FAngelscriptDebuggerTestSession& Session)
	{
		FAngelscriptDebuggerSessionConfig SessionConfig;
		// UE 5.7: headless has no production subsystem. Let Initialize() create
		// an isolated FAngelscriptEngine with its own FAngelscriptDebugServer.
		SessionConfig.DefaultTimeoutSeconds = 45.0f;

		return Test.TestTrue(TEXT("Debugger lifecycle should initialize against the debuggable production engine"), Session.Initialize(SessionConfig));
	}

	bool StartLifecycleDebuggerSession(FAutomationTestBase& Test, FAngelscriptDebuggerTestSession& Session, FAngelscriptDebuggerTestClient& Client)
	{
		if (!StartLifecycleSession(Test, Session))
		{
			return false;
		}

		if (!Test.TestTrue(TEXT("Debugger lifecycle should connect the primary debugger client"), Client.Connect(TEXT("127.0.0.1"), Session.GetPort())))
		{
			Test.AddError(Client.GetLastError());
			return false;
		}

		bool bSentStartDebugging = false;
		const bool bStartMessageSent = Session.PumpUntil(
			[&Client, &bSentStartDebugging]()
			{
				if (bSentStartDebugging)
				{
					return true;
				}

				bSentStartDebugging = Client.SendStartDebugging(2);
				return bSentStartDebugging;
			},
			Session.GetDefaultTimeoutSeconds());

		if (!Test.TestTrue(TEXT("Debugger lifecycle should send StartDebugging before lifecycle validation"), bStartMessageSent))
		{
			Test.AddError(Client.GetLastError());
			return false;
		}

		TOptional<FAngelscriptDebugMessageEnvelope> VersionEnvelope;
		const bool bReceivedVersion = Session.PumpUntil(
			[&Client, &VersionEnvelope]()
			{
				TOptional<FAngelscriptDebugMessageEnvelope> Envelope = Client.ReceiveEnvelope();
				if (Envelope.IsSet() && Envelope->MessageType == EDebugMessageType::DebugServerVersion)
				{
					VersionEnvelope = MoveTemp(Envelope);
					return true;
				}

				return false;
			},
			Session.GetDefaultTimeoutSeconds());

		if (!Test.TestTrue(TEXT("Debugger lifecycle should receive the DebugServerVersion response"), bReceivedVersion))
		{
			Test.AddError(Client.GetLastError());
			return false;
		}

		return true;
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
		const bool bCompleted = Session.PumpUntil(
			[&InvocationState]()
			{
				return InvocationState->bCompleted.Load();
			},
			Session.GetDefaultTimeoutSeconds());

		return Test.TestTrue(Context, bCompleted);
	}

	void AppendNonPingMessages(
		TArray<FAngelscriptDebugMessageEnvelope>& OutMessages,
		const TArray<FAngelscriptDebugMessageEnvelope>& InMessages)
	{
		for (const FAngelscriptDebugMessageEnvelope& Envelope : InMessages)
		{
			if (Envelope.MessageType != EDebugMessageType::PingAlive)
			{
				OutMessages.Add(Envelope);
			}
		}
	}

	struct FLifecycleNoStopMonitorResult
	{
		bool bReceivedVersion = false;
		TArray<FAngelscriptDebugMessageEnvelope> ResidualMessagesAfterHandshake;
		TArray<FAngelscriptDebugMessageEnvelope> ResidualMessagesAfterInvocation;
		int32 UnexpectedStopCount = 0;
		int32 ContinuedCount = 0;
		bool bTimedOut = false;
		FString Error;
	};

	TFuture<FLifecycleNoStopMonitorResult> StartLifecycleNoStopMonitor(
		int32 Port,
		TAtomic<bool>& bMonitorReady,
		TAtomic<bool>& bShouldStop,
		TAtomic<bool>& bInvocationCompleted,
		float TimeoutSeconds)
	{
		return Async(EAsyncExecution::ThreadPool,
			[Port, &bMonitorReady, &bShouldStop, &bInvocationCompleted, TimeoutSeconds]() -> FLifecycleNoStopMonitorResult
			{
				FLifecycleNoStopMonitorResult Result;
				FAngelscriptDebuggerTestClient MonitorClient;
				ON_SCOPE_EXIT
				{
					MonitorClient.SendStopDebugging();
					MonitorClient.SendDisconnect();
					MonitorClient.Disconnect();
				};

				if (!MonitorClient.Connect(TEXT("127.0.0.1"), Port))
				{
					Result.Error = FString::Printf(TEXT("Lifecycle monitor failed to connect: %s"), *MonitorClient.GetLastError());
					bMonitorReady = true;
					return Result;
				}

				const double HandshakeEnd = FPlatformTime::Seconds() + TimeoutSeconds;
				bool bSentStart = false;
				while (FPlatformTime::Seconds() < HandshakeEnd && !bShouldStop.Load())
				{
					if (!bSentStart)
					{
						bSentStart = MonitorClient.SendStartDebugging(2);
						if (!bSentStart)
						{
							Result.Error = FString::Printf(TEXT("Lifecycle monitor failed to send StartDebugging: %s"), *MonitorClient.GetLastError());
							bMonitorReady = true;
							return Result;
						}
					}

					TOptional<FAngelscriptDebugMessageEnvelope> Envelope = MonitorClient.ReceiveEnvelope();
					if (Envelope.IsSet())
					{
						if (Envelope->MessageType == EDebugMessageType::DebugServerVersion)
						{
							Result.bReceivedVersion = true;
							break;
						}

						if (Envelope->MessageType != EDebugMessageType::PingAlive)
						{
							Result.ResidualMessagesAfterHandshake.Add(Envelope.GetValue());
						}
					}
					else if (!MonitorClient.GetLastError().IsEmpty())
					{
						Result.Error = FString::Printf(TEXT("Lifecycle monitor failed during handshake: %s"), *MonitorClient.GetLastError());
						bMonitorReady = true;
						return Result;
					}

					FPlatformProcess::Sleep(0.001f);
				}

				if (!Result.bReceivedVersion)
				{
					Result.bTimedOut = !bShouldStop.Load();
					Result.Error = Result.bTimedOut
						? TEXT("Lifecycle monitor timed out waiting for DebugServerVersion.")
						: TEXT("Lifecycle monitor handshake aborted before receiving DebugServerVersion.");
					bMonitorReady = true;
					return Result;
				}

				AppendNonPingMessages(Result.ResidualMessagesAfterHandshake, MonitorClient.DrainPendingMessages());
				bMonitorReady = true;

				const double EndTime = FPlatformTime::Seconds() + TimeoutSeconds;
				while (FPlatformTime::Seconds() < EndTime && !bShouldStop.Load())
				{
					if (bInvocationCompleted.Load())
					{
						AppendNonPingMessages(Result.ResidualMessagesAfterInvocation, MonitorClient.DrainPendingMessages());
						return Result;
					}

					TOptional<FAngelscriptDebugMessageEnvelope> Envelope = MonitorClient.ReceiveEnvelope();
					if (Envelope.IsSet())
					{
						if (Envelope->MessageType == EDebugMessageType::HasStopped)
						{
							++Result.UnexpectedStopCount;
							if (!MonitorClient.SendContinue())
							{
								Result.Error = FString::Printf(TEXT("Lifecycle monitor failed to send Continue after an unexpected stop: %s"), *MonitorClient.GetLastError());
								return Result;
							}

							TOptional<FEmptyMessage> ContinuedMessage = MonitorClient.WaitForTypedMessage<FEmptyMessage>(EDebugMessageType::HasContinued, TimeoutSeconds);
							if (!ContinuedMessage.IsSet())
							{
								Result.Error = FString::Printf(TEXT("Lifecycle monitor failed to receive HasContinued after an unexpected stop: %s"), *MonitorClient.GetLastError());
								Result.bTimedOut = !bShouldStop.Load();
								return Result;
							}

							++Result.ContinuedCount;
						}
						else if (Envelope->MessageType != EDebugMessageType::PingAlive)
						{
							Result.ResidualMessagesAfterInvocation.Add(Envelope.GetValue());
						}
					}
					else if (!MonitorClient.GetLastError().IsEmpty())
					{
						Result.Error = FString::Printf(TEXT("Lifecycle monitor failed while waiting for invocation completion: %s"), *MonitorClient.GetLastError());
						return Result;
					}

					FPlatformProcess::Sleep(0.001f);
				}

				Result.bTimedOut = !bInvocationCompleted.Load() && !bShouldStop.Load();
				if (Result.bTimedOut && Result.Error.IsEmpty())
				{
					Result.Error = TEXT("Lifecycle monitor timed out waiting for invocation completion.");
				}
				AppendNonPingMessages(Result.ResidualMessagesAfterInvocation, MonitorClient.DrainPendingMessages());
				return Result;
			});
	}

	bool WaitForLifecycleMonitorReady(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		TAtomic<bool>& bMonitorReady,
		const TCHAR* Context)
	{
		return Test.TestTrue(
			Context,
			Session.PumpUntil(
				[&bMonitorReady]()
				{
					return bMonitorReady.Load();
				},
				Session.GetDefaultTimeoutSeconds()));
	}
}

using namespace AngelscriptTest_Debugger_AngelscriptDebuggerLifecycleTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebuggerSessionDisconnectClearsDebugStateTest,
	"Angelscript.TestModule.Debugger.Session.DisconnectClearsDebugState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebuggerSessionDisconnectClearsDebugStateTest::RunTest(const FString& Parameters)
{
	FAngelscriptDebuggerTestSession Session;
	FAngelscriptDebuggerTestClient Client;
	if (!StartLifecycleDebuggerSession(*this, Session, Client))
	{
		return false;
	}

	FAngelscriptEngine& Engine = Session.GetEngine();
	const FAngelscriptDebuggerScriptFixture Fixture = FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture();
	ON_SCOPE_EXIT
	{
		if (Client.IsConnected())
		{
			Client.SendStopDebugging();
			Client.SendDisconnect();
		}

		Client.Disconnect();
		Engine.DiscardModule(*Fixture.ModuleName.ToString());
		CollectGarbage(RF_NoFlags, true);
	};

	if (!TestTrue(TEXT("Debugger.Session.DisconnectClearsDebugState should compile the breakpoint fixture"), Fixture.Compile(Engine)))
	{
		return false;
	}

	FAngelscriptBreakpoint Breakpoint;
	Breakpoint.Filename = Fixture.Filename;
	Breakpoint.ModuleName = Fixture.ModuleName.ToString();
	Breakpoint.LineNumber = Fixture.GetLine(TEXT("BreakpointHelperLine"));
	Breakpoint.Id = 2201;
	if (!TestTrue(TEXT("Debugger.Session.DisconnectClearsDebugState should send the target breakpoint"), Client.SendSetBreakpoint(Breakpoint)))
	{
		AddError(Client.GetLastError());
		return false;
	}

	if (!WaitForBreakpointCount(*this, Session, 1, TEXT("Debugger.Session.DisconnectClearsDebugState should observe the breakpoint registration before disconnecting the last client")))
	{
		return false;
	}

	TestTrue(TEXT("Debugger.Session.DisconnectClearsDebugState should enter debugging before the disconnect"), Session.GetDebugServer().bIsDebugging);
	TestEqual(TEXT("Debugger.Session.DisconnectClearsDebugState should hold exactly one authoritative breakpoint before disconnect"), Session.GetDebugServer().BreakpointCount, 1);

	if (!TestTrue(TEXT("Debugger.Session.DisconnectClearsDebugState should send Disconnect from the primary client"), Client.SendDisconnect()))
	{
		AddError(Client.GetLastError());
		return false;
	}

	if (!TestTrue(TEXT("Debugger.Session.DisconnectClearsDebugState should let the server return to an idle state after the last client disconnects"), WaitForDebugServerIdle(Session, Session.GetDefaultTimeoutSeconds())))
	{
		AddError(FString::Printf(
			TEXT("Observed debug server state after disconnect: bIsDebugging=%d, bIsPaused=%d, bPauseRequested=%d, BreakpointCount=%d, HasAnyClients=%d."),
			Session.GetDebugServer().bIsDebugging ? 1 : 0,
			Session.GetDebugServer().bIsPaused ? 1 : 0,
			Session.GetDebugServer().bPauseRequested ? 1 : 0,
			Session.GetDebugServer().BreakpointCount,
			Session.GetDebugServer().HasAnyClients() ? 1 : 0));
		return false;
	}

	Client.Disconnect();

	TestFalse(TEXT("Debugger.Session.DisconnectClearsDebugState should clear bIsDebugging after the last client disconnects"), Session.GetDebugServer().bIsDebugging);
	TestFalse(TEXT("Debugger.Session.DisconnectClearsDebugState should clear bIsPaused after the last client disconnects"), Session.GetDebugServer().bIsPaused);
	TestFalse(TEXT("Debugger.Session.DisconnectClearsDebugState should clear bPauseRequested after the last client disconnects"), Session.GetDebugServer().bPauseRequested);
	TestFalse(TEXT("Debugger.Session.DisconnectClearsDebugState should remove the disconnected socket from the client list"), Session.GetDebugServer().HasAnyClients());
	TestEqual(TEXT("Debugger.Session.DisconnectClearsDebugState should clear all breakpoints after the last client disconnects"), Session.GetDebugServer().BreakpointCount, 0);

	TAtomic<bool> bMonitorReady(false);
	TAtomic<bool> bMonitorShouldStop(false);
	TAtomic<bool> bInvocationCompleted(false);
	TFuture<FLifecycleNoStopMonitorResult> MonitorFuture = StartLifecycleNoStopMonitor(
		Session.GetPort(),
		bMonitorReady,
		bMonitorShouldStop,
		bInvocationCompleted,
		Session.GetDefaultTimeoutSeconds());
	ON_SCOPE_EXIT
	{
		bMonitorShouldStop = true;
		if (MonitorFuture.IsValid())
		{
			MonitorFuture.Wait();
		}
	};

	if (!WaitForLifecycleMonitorReady(*this, Session, bMonitorReady, TEXT("Debugger.Session.DisconnectClearsDebugState should bring the reconnect monitor up before re-running the scenario")))
	{
		return false;
	}

	const TSharedRef<FAsyncModuleInvocationState> InvocationState = DispatchModuleInvocation(
		Engine,
		Fixture.Filename,
		Fixture.ModuleName,
		Fixture.EntryFunctionDeclaration);

	if (!WaitForInvocationCompletion(*this, Session, InvocationState, TEXT("Debugger.Session.DisconnectClearsDebugState should complete the follow-up invocation without a lingering breakpoint stop")))
	{
		bMonitorShouldStop = true;
		return false;
	}

	bInvocationCompleted = true;
	bMonitorShouldStop = true;
	const FLifecycleNoStopMonitorResult MonitorResult = MonitorFuture.Get();

	if (!TestTrue(TEXT("Debugger.Session.DisconnectClearsDebugState should keep the reconnect monitor error-free"), MonitorResult.Error.IsEmpty()))
	{
		AddError(MonitorResult.Error);
		return false;
	}

	if (!TestFalse(TEXT("Debugger.Session.DisconnectClearsDebugState should not time out while monitoring the reconnect run"), MonitorResult.bTimedOut))
	{
		return false;
	}

	TestTrue(TEXT("Debugger.Session.DisconnectClearsDebugState should let the second client receive DebugServerVersion during reconnect"), MonitorResult.bReceivedVersion);
	TestEqual(TEXT("Debugger.Session.DisconnectClearsDebugState should not leave residual messages queued after the reconnect handshake"), MonitorResult.ResidualMessagesAfterHandshake.Num(), 0);
	TestEqual(TEXT("Debugger.Session.DisconnectClearsDebugState should not emit any HasStopped during the reconnect run without re-registering breakpoints"), MonitorResult.UnexpectedStopCount, 0);
	TestEqual(TEXT("Debugger.Session.DisconnectClearsDebugState should not need any HasContinued messages during the reconnect run"), MonitorResult.ContinuedCount, 0);
	TestEqual(TEXT("Debugger.Session.DisconnectClearsDebugState should not leave residual debugger messages queued after the reconnect invocation"), MonitorResult.ResidualMessagesAfterInvocation.Num(), 0);
	TestTrue(TEXT("Debugger.Session.DisconnectClearsDebugState should complete the reconnect invocation successfully"), InvocationState->bSucceeded);
	TestEqual(TEXT("Debugger.Session.DisconnectClearsDebugState should preserve the reconnect invocation return value"), InvocationState->Result, 8);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebuggerSessionStopDebuggingClearsPausedStateTest,
	"Angelscript.TestModule.Debugger.Session.StopDebuggingClearsPausedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebuggerSessionStopDebuggingClearsPausedStateTest::RunTest(const FString& Parameters)
{
	FAngelscriptDebuggerTestSession Session;
	if (!StartLifecycleSession(*this, Session))
	{
		return false;
	}

	FAngelscriptEngine& Engine = Session.GetEngine();
	const FAngelscriptDebuggerScriptFixture Fixture = FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture();
	TAtomic<bool> bWorkerReady(false);
	TAtomic<bool> bAbortWorker(false);
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

	if (!TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should compile the breakpoint fixture"), Fixture.Compile(Engine)))
	{
		return false;
	}

	FAngelscriptBreakpoint Breakpoint;
	Breakpoint.Filename = Fixture.Filename;
	Breakpoint.ModuleName = Fixture.ModuleName.ToString();
	Breakpoint.LineNumber = Fixture.GetLine(TEXT("BreakpointHelperLine"));
	Breakpoint.Id = 2401;

	FSingleClientDebuggerWorkerConfig WorkerConfig;
	WorkerConfig.TimeoutSeconds = Session.GetDefaultTimeoutSeconds();
	WorkerConfig.InitialBreakpoints.Add(Breakpoint);
	FSingleClientDebuggerStopAction StopAction;
	StopAction.bRequestCallStack = true;
	StopAction.Command = ESingleClientDebuggerCommand::StopDebugging;
	WorkerConfig.StopActions.Add(StopAction);
	WorkerFuture = RunSingleClientDebuggerWorker(Session.GetPort(), bWorkerReady, bAbortWorker, WorkerConfig);
	bWorkerStarted = true;

	if (!WaitForLifecycleMonitorReady(*this, Session, bWorkerReady, TEXT("Debugger.Session.StopDebuggingClearsPausedState should finish the single-client handshake before the paused run")))
	{
		return false;
	}

	if (!WaitForBreakpointCount(*this, Session, 1, TEXT("Debugger.Session.StopDebuggingClearsPausedState should observe the breakpoint registration before the paused run")))
	{
		return false;
	}

	const TSharedRef<FAsyncModuleInvocationState> InvocationState = DispatchModuleInvocation(
		Engine,
		Fixture.Filename,
		Fixture.ModuleName,
		Fixture.EntryFunctionDeclaration);

	if (!WaitForInvocationCompletion(*this, Session, InvocationState, TEXT("Debugger.Session.StopDebuggingClearsPausedState should let the paused invocation complete after StopDebugging")))
	{
		return false;
	}

	FSingleClientDebuggerTranscript Transcript = WorkerFuture.Get();
	bWorkerJoined = true;

	if (!TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should keep the single-client worker error-free"), Transcript.Error.IsEmpty()))
	{
		AddError(Transcript.Error);
		return false;
	}

	if (!TestFalse(TEXT("Debugger.Session.StopDebuggingClearsPausedState should not time out on the paused stop"), Transcript.bTimedOut))
	{
		return false;
	}

	if (!TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should receive DebugServerVersion on the same client"), Transcript.DebugServerVersion.IsSet()))
	{
		return false;
	}

	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should deserialize exactly one paused stop"), Transcript.StopMessages.Num(), 1);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should deserialize exactly one paused callstack"), Transcript.CallStacks.Num(), 1);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should emit a single HasContinued after StopDebugging releases the pause"), Transcript.HasContinuedCount, 1);

	if (!TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should capture the paused stop"), Transcript.StopMessages.Num() == 1))
	{
		return false;
	}

	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should stop because of the configured breakpoint"), Transcript.StopMessages[0].Reason, FString(TEXT("breakpoint")));

	if (!TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should capture a top frame while paused"), Transcript.CallStacks.Num() == 1 && Transcript.CallStacks[0].Frames.Num() > 0))
	{
		return false;
	}

	const FAngelscriptCallStack& PausedCallstack = Transcript.CallStacks[0];
	TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should stop in the breakpoint fixture source"), PausedCallstack.Frames[0].Source.EndsWith(Fixture.Filename));
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should stop at BreakpointHelperLine"), PausedCallstack.Frames[0].LineNumber, Fixture.GetLine(TEXT("BreakpointHelperLine")));

	if (!TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should let StopDebugging return the debug server to idle"), WaitForDebugServerIdle(Session, Session.GetDefaultTimeoutSeconds())))
	{
		AddError(FString::Printf(
			TEXT("Observed debug server state after StopDebugging: bIsDebugging=%d, bIsPaused=%d, bPauseRequested=%d, BreakpointCount=%d."),
			Session.GetDebugServer().bIsDebugging ? 1 : 0,
			Session.GetDebugServer().bIsPaused ? 1 : 0,
			Session.GetDebugServer().bPauseRequested ? 1 : 0,
			Session.GetDebugServer().BreakpointCount));
		return false;
	}

	TestFalse(TEXT("Debugger.Session.StopDebuggingClearsPausedState should clear bIsDebugging after StopDebugging"), Session.GetDebugServer().bIsDebugging);
	TestFalse(TEXT("Debugger.Session.StopDebuggingClearsPausedState should clear bIsPaused after StopDebugging"), Session.GetDebugServer().bIsPaused);
	TestFalse(TEXT("Debugger.Session.StopDebuggingClearsPausedState should clear bPauseRequested after StopDebugging"), Session.GetDebugServer().bPauseRequested);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should clear all breakpoints after StopDebugging"), Session.GetDebugServer().BreakpointCount, 0);
	TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should let the paused invocation complete successfully"), InvocationState->bSucceeded);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should preserve the paused invocation result"), InvocationState->Result, 8);

	TAtomic<bool> bMonitorReady(false);
	TAtomic<bool> bMonitorShouldStop(false);
	TAtomic<bool> bInvocationCompleted(false);
	TFuture<FLifecycleNoStopMonitorResult> MonitorFuture = StartLifecycleNoStopMonitor(
		Session.GetPort(),
		bMonitorReady,
		bMonitorShouldStop,
		bInvocationCompleted,
		Session.GetDefaultTimeoutSeconds());
	ON_SCOPE_EXIT
	{
		bMonitorShouldStop = true;
		if (MonitorFuture.IsValid())
		{
			MonitorFuture.Wait();
		}
	};

	if (!WaitForLifecycleMonitorReady(*this, Session, bMonitorReady, TEXT("Debugger.Session.StopDebuggingClearsPausedState should bring the no-stop monitor up before the follow-up run")))
	{
		return false;
	}

	const TSharedRef<FAsyncModuleInvocationState> SecondInvocationState = DispatchModuleInvocation(
		Engine,
		Fixture.Filename,
		Fixture.ModuleName,
		Fixture.EntryFunctionDeclaration);

	if (!WaitForInvocationCompletion(*this, Session, SecondInvocationState, TEXT("Debugger.Session.StopDebuggingClearsPausedState should complete the follow-up invocation without a lingering stop")))
	{
		bMonitorShouldStop = true;
		return false;
	}

	bInvocationCompleted = true;
	bMonitorShouldStop = true;
	const FLifecycleNoStopMonitorResult MonitorResult = MonitorFuture.Get();

	if (!TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should keep the no-stop monitor error-free"), MonitorResult.Error.IsEmpty()))
	{
		AddError(MonitorResult.Error);
		return false;
	}

	if (!TestFalse(TEXT("Debugger.Session.StopDebuggingClearsPausedState should not time out during the follow-up run"), MonitorResult.bTimedOut))
	{
		return false;
	}

	TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should let the follow-up monitor receive DebugServerVersion"), MonitorResult.bReceivedVersion);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should not emit any HasStopped during the follow-up run without re-registering breakpoints"), MonitorResult.UnexpectedStopCount, 0);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should not need any HasContinued messages during the follow-up run"), MonitorResult.ContinuedCount, 0);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should not leave residual debugger messages queued after the follow-up handshake"), MonitorResult.ResidualMessagesAfterHandshake.Num(), 0);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should not leave residual debugger messages queued after the follow-up invocation"), MonitorResult.ResidualMessagesAfterInvocation.Num(), 0);
	TestTrue(TEXT("Debugger.Session.StopDebuggingClearsPausedState should complete the follow-up invocation successfully"), SecondInvocationState->bSucceeded);
	TestEqual(TEXT("Debugger.Session.StopDebuggingClearsPausedState should preserve the follow-up invocation return value"), SecondInvocationState->Result, 8);
	return !HasAnyErrors();
}

#endif

