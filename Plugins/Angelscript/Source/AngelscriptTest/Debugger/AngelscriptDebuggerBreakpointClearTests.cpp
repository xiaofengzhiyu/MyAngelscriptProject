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

namespace AngelscriptTest_Debugger_AngelscriptDebuggerBreakpointClearTests_Private
{
	bool StartDebuggerSession(FAutomationTestBase& Test, FAngelscriptDebuggerTestSession& Session, FAngelscriptDebuggerTestClient& Client)
	{
		FAngelscriptDebuggerSessionConfig SessionConfig;
		// UE 5.7: headless has no production subsystem. Let Initialize() create
		// an isolated FAngelscriptEngine with its own FAngelscriptDebugServer.
		SessionConfig.DefaultTimeoutSeconds = 45.0f;

		if (!Test.TestTrue(TEXT("Debugger breakpoint clear tests should initialize a debuggable session"), Session.Initialize(SessionConfig)))
		{
			return false;
		}

		if (!Test.TestTrue(TEXT("Debugger breakpoint clear tests should connect the primary client"), Client.Connect(TEXT("127.0.0.1"), Session.GetPort())))
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

		if (!Test.TestTrue(TEXT("Debugger breakpoint clear tests should send StartDebugging before running fixtures"), bStartMessageSent))
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

		if (!Test.TestTrue(TEXT("Debugger breakpoint clear tests should receive the DebugServerVersion handshake"), bReceivedVersion))
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

	bool HasBreakpointLine(const FAngelscriptDebugServer& DebugServer, const FString& ModuleKey, int32 LineNumber)
	{
		const TSharedPtr<FAngelscriptDebugServer::FFileBreakpoints>* Active = DebugServer.Breakpoints.Find(ModuleKey);
		return Active != nullptr && Active->IsValid() && (*Active)->Lines.Contains(LineNumber);
	}

	int32 GetBreakpointLineCount(const FAngelscriptDebugServer& DebugServer, const FString& ModuleKey)
	{
		const TSharedPtr<FAngelscriptDebugServer::FFileBreakpoints>* Active = DebugServer.Breakpoints.Find(ModuleKey);
		return Active != nullptr && Active->IsValid() ? (*Active)->Lines.Num() : 0;
	}

	bool WaitForBreakpointLinePresence(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		const FString& ModuleKey,
		int32 LineNumber,
		const TCHAR* Context)
	{
		const bool bFoundBreakpoint = Session.PumpUntil(
			[&Session, &ModuleKey, LineNumber]()
			{
				return HasBreakpointLine(Session.GetDebugServer(), ModuleKey, LineNumber);
			},
			Session.GetDefaultTimeoutSeconds());

		return Test.TestTrue(Context, bFoundBreakpoint);
	}

	bool WaitForBreakpointLineAbsence(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		const FString& ModuleKey,
		int32 LineNumber,
		const TCHAR* Context)
	{
		const bool bRemovedBreakpoint = Session.PumpUntil(
			[&Session, &ModuleKey, LineNumber]()
			{
				return !HasBreakpointLine(Session.GetDebugServer(), ModuleKey, LineNumber);
			},
			Session.GetDefaultTimeoutSeconds());

		return Test.TestTrue(Context, bRemovedBreakpoint);
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
		TSharedRef<FAsyncModuleInvocationState> InvocationState,
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

	struct FBreakpointMonitorResult
	{
		TArray<FAngelscriptDebugMessageEnvelope> StopEnvelopes;
		TOptional<FAngelscriptCallStack> CapturedCallstack;
		bool bTimedOut = false;
		FString Error;
	};

	struct FBreakpointMonitorConfig
	{
		bool bRequestCallstack = false;
		bool bSendContinueOnStop = true;
		bool bSendStopDebuggingOnExit = false;
		int32 MaxStopsToHandle = 1;
		float TimeoutSeconds = 45.0f;
	};

	TFuture<FBreakpointMonitorResult> StartBreakpointMonitor(
		int32 Port,
		TAtomic<bool>& bMonitorReady,
		TAtomic<bool>& bShouldStop,
		const FBreakpointMonitorConfig& Config)
	{
		return Async(EAsyncExecution::ThreadPool,
			[Port, &bMonitorReady, &bShouldStop, Config]() -> FBreakpointMonitorResult
			{
				FBreakpointMonitorResult Result;
				FAngelscriptDebuggerTestClient MonitorClient;
				ON_SCOPE_EXIT
				{
					if (MonitorClient.IsConnected())
					{
						if (Config.bSendStopDebuggingOnExit)
						{
							MonitorClient.SendStopDebugging();
						}

						MonitorClient.SendDisconnect();
					}

					MonitorClient.Disconnect();
				};

				if (!MonitorClient.Connect(TEXT("127.0.0.1"), Port))
				{
					Result.Error = FString::Printf(TEXT("Breakpoint clear monitor failed to connect: %s"), *MonitorClient.GetLastError());
					bMonitorReady = true;
					return Result;
				}

				const double HandshakeEnd = FPlatformTime::Seconds() + Config.TimeoutSeconds;
				bool bSentStart = false;
				bool bReceivedVersion = false;
				while (FPlatformTime::Seconds() < HandshakeEnd && !bShouldStop.Load())
				{
					if (!bSentStart)
					{
						bSentStart = MonitorClient.SendStartDebugging(2);
						if (!bSentStart)
						{
							Result.Error = FString::Printf(TEXT("Breakpoint clear monitor failed to send StartDebugging: %s"), *MonitorClient.GetLastError());
							bMonitorReady = true;
							return Result;
						}
					}

					TOptional<FAngelscriptDebugMessageEnvelope> Envelope = MonitorClient.ReceiveEnvelope();
					if (Envelope.IsSet())
					{
						if (Envelope->MessageType == EDebugMessageType::DebugServerVersion)
						{
							bReceivedVersion = true;
							break;
						}
					}
					else if (!MonitorClient.GetLastError().IsEmpty())
					{
						Result.Error = FString::Printf(TEXT("Breakpoint clear monitor failed during handshake: %s"), *MonitorClient.GetLastError());
						bMonitorReady = true;
						return Result;
					}

					FPlatformProcess::Sleep(0.001f);
				}

				if (!bReceivedVersion)
				{
					Result.Error = bShouldStop.Load()
						? TEXT("Breakpoint clear monitor handshake stopped before receiving DebugServerVersion.")
						: TEXT("Breakpoint clear monitor timed out waiting for DebugServerVersion.");
					bMonitorReady = true;
					return Result;
				}

				bMonitorReady = true;

				int32 StopsHandled = 0;
				const double MonitorEnd = FPlatformTime::Seconds() + Config.TimeoutSeconds;
				while (FPlatformTime::Seconds() < MonitorEnd && !bShouldStop.Load())
				{
					TOptional<FAngelscriptDebugMessageEnvelope> Envelope = MonitorClient.ReceiveEnvelope();
					if (!Envelope.IsSet())
					{
						if (!MonitorClient.GetLastError().IsEmpty())
						{
							Result.Error = FString::Printf(TEXT("Breakpoint clear monitor failed while waiting for stop events: %s"), *MonitorClient.GetLastError());
							return Result;
						}

						FPlatformProcess::Sleep(0.001f);
						continue;
					}

					if (Envelope->MessageType != EDebugMessageType::HasStopped)
					{
						continue;
					}

					Result.StopEnvelopes.Add(Envelope.GetValue());

					if (Config.bRequestCallstack)
					{
						if (!MonitorClient.SendRequestCallStack())
						{
							Result.Error = FString::Printf(TEXT("Breakpoint clear monitor failed to request a callstack: %s"), *MonitorClient.GetLastError());
							return Result;
						}

						const double CallstackEnd = FPlatformTime::Seconds() + 10.0;
						while (FPlatformTime::Seconds() < CallstackEnd && !bShouldStop.Load())
						{
							TOptional<FAngelscriptDebugMessageEnvelope> CallstackEnvelope = MonitorClient.ReceiveEnvelope();
							if (CallstackEnvelope.IsSet())
							{
								if (CallstackEnvelope->MessageType == EDebugMessageType::CallStack)
								{
									Result.CapturedCallstack = FAngelscriptDebuggerTestClient::DeserializeMessage<FAngelscriptCallStack>(CallstackEnvelope.GetValue());
									break;
								}
							}
							else if (!MonitorClient.GetLastError().IsEmpty())
							{
								Result.Error = FString::Printf(TEXT("Breakpoint clear monitor failed while waiting for the callstack response: %s"), *MonitorClient.GetLastError());
								return Result;
							}

							FPlatformProcess::Sleep(0.001f);
						}

						if (!Result.CapturedCallstack.IsSet())
						{
							Result.Error = bShouldStop.Load()
								? TEXT("Breakpoint clear monitor stopped before the requested callstack arrived.")
								: TEXT("Breakpoint clear monitor timed out waiting for the requested callstack.");
							return Result;
						}
					}

					if (Config.bSendContinueOnStop && !MonitorClient.SendContinue())
					{
						Result.Error = FString::Printf(TEXT("Breakpoint clear monitor failed to send Continue after a stop: %s"), *MonitorClient.GetLastError());
						return Result;
					}

					++StopsHandled;
					if (StopsHandled >= Config.MaxStopsToHandle)
					{
						break;
					}
				}

				if (StopsHandled == 0 && !bShouldStop.Load() && FPlatformTime::Seconds() >= MonitorEnd)
				{
					Result.bTimedOut = true;
				}

				return Result;
			});
	}

	bool StartAndWaitForMonitorReady(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		int32 Port,
		TAtomic<bool>& bMonitorReady,
		TAtomic<bool>& bShouldStop,
		const FBreakpointMonitorConfig& Config,
		TFuture<FBreakpointMonitorResult>& OutFuture)
	{
		OutFuture = StartBreakpointMonitor(Port, bMonitorReady, bShouldStop, Config);

		const bool bReady = Session.PumpUntil(
			[&bMonitorReady]()
			{
				return bMonitorReady.Load();
			},
			Session.GetDefaultTimeoutSeconds());

		if (!Test.TestTrue(TEXT("Debugger breakpoint clear monitor should become ready within the timeout"), bReady))
		{
			bShouldStop = true;
			return false;
		}

		return true;
	}
}

using namespace AngelscriptTest_Debugger_AngelscriptDebuggerBreakpointClearTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebuggerBreakpointClearTargetPreservesOtherBreakpointsTest,
	"Angelscript.TestModule.Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebuggerBreakpointClearTargetPreservesOtherBreakpointsTest::RunTest(const FString& Parameters)
{
	FAngelscriptDebuggerTestSession Session;
	FAngelscriptDebuggerTestClient Client;
	if (!StartDebuggerSession(*this, Session, Client))
	{
		return false;
	}

	FAngelscriptEngine& Engine = Session.GetEngine();
	const FAngelscriptDebuggerScriptFixture FixtureA =
		FAngelscriptDebuggerScriptFixture::CreateNamedBreakpointFixture(
			TEXT("DebuggerBreakpointFixtureA"),
			TEXT("DebuggerBreakpointFixtureA.as"),
			5);
	const FAngelscriptDebuggerScriptFixture FixtureB =
		FAngelscriptDebuggerScriptFixture::CreateNamedBreakpointFixture(
			TEXT("DebuggerBreakpointFixtureB"),
			TEXT("DebuggerBreakpointFixtureB.as"),
			9);

	TAtomic<bool> bFirstMonitorShouldStop(false);
	TFuture<FBreakpointMonitorResult> FirstMonitorFuture;
	TAtomic<bool> bSecondMonitorShouldStop(false);
	TFuture<FBreakpointMonitorResult> SecondMonitorFuture;
	ON_SCOPE_EXIT
	{
		bFirstMonitorShouldStop = true;
		if (FirstMonitorFuture.IsValid())
		{
			FirstMonitorFuture.Wait();
		}

		bSecondMonitorShouldStop = true;
		if (SecondMonitorFuture.IsValid())
		{
			SecondMonitorFuture.Wait();
		}

		if (Client.IsConnected())
		{
			Client.SendStopDebugging();
			Client.SendDisconnect();
		}

		Client.Disconnect();
		Engine.DiscardModule(*FixtureA.ModuleName.ToString());
		Engine.DiscardModule(*FixtureB.ModuleName.ToString());
		CollectGarbage(RF_NoFlags, true);
	};

	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should compile fixture A"), FixtureA.Compile(Engine))
		|| !TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should compile fixture B"), FixtureB.Compile(Engine)))
	{
		return false;
	}

	const FString ModuleKeyA = FixtureA.ModuleName.ToString();
	const FString ModuleKeyB = FixtureB.ModuleName.ToString();
	const int32 BreakpointLineA = FixtureA.GetLine(TEXT("BreakpointHelperLine"));
	const int32 BreakpointLineB = FixtureB.GetLine(TEXT("BreakpointHelperLine"));

	FAngelscriptBreakpoint BreakpointA;
	BreakpointA.Filename = FixtureA.Filename;
	BreakpointA.ModuleName = ModuleKeyA;
	BreakpointA.LineNumber = BreakpointLineA;
	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should send fixture A's breakpoint"), Client.SendSetBreakpoint(BreakpointA)))
	{
		AddError(Client.GetLastError());
		return false;
	}

	FAngelscriptBreakpoint BreakpointB;
	BreakpointB.Filename = FixtureB.Filename;
	BreakpointB.ModuleName = ModuleKeyB;
	BreakpointB.LineNumber = BreakpointLineB;
	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should send fixture B's breakpoint"), Client.SendSetBreakpoint(BreakpointB)))
	{
		AddError(Client.GetLastError());
		return false;
	}

	if (!WaitForBreakpointCount(
			*this,
			Session,
			2,
			TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should register both breakpoints before clearing fixture A")))
	{
		return false;
	}

	if (!WaitForBreakpointLinePresence(
			*this,
			Session,
			ModuleKeyA,
			BreakpointLineA,
			TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should keep fixture A's helper breakpoint registered before clearing")))
	{
		return false;
	}

	if (!WaitForBreakpointLinePresence(
			*this,
			Session,
			ModuleKeyB,
			BreakpointLineB,
			TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should keep fixture B's helper breakpoint registered before clearing fixture A")))
	{
		return false;
	}

	FAngelscriptClearBreakpoints ClearBreakpoints;
	ClearBreakpoints.Filename = FixtureA.Filename;
	ClearBreakpoints.ModuleName = ModuleKeyA;
	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should clear only fixture A's breakpoints"), Client.SendClearBreakpoints(ClearBreakpoints)))
	{
		AddError(Client.GetLastError());
		return false;
	}

	if (!WaitForBreakpointCount(
			*this,
			Session,
			1,
			TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should reduce the authoritative breakpoint count from two to one after clearing fixture A")))
	{
		return false;
	}

	if (!WaitForBreakpointLineAbsence(
			*this,
			Session,
			ModuleKeyA,
			BreakpointLineA,
			TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should remove fixture A's helper breakpoint from the server")))
	{
		return false;
	}

	if (!WaitForBreakpointLinePresence(
			*this,
			Session,
			ModuleKeyB,
			BreakpointLineB,
			TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should preserve fixture B's helper breakpoint after clearing fixture A")))
	{
		return false;
	}

	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should leave fixture A's breakpoint entry empty after clearing"), GetBreakpointLineCount(Session.GetDebugServer(), ModuleKeyA), 0);
	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should keep fixture B's breakpoint entry intact after clearing fixture A"), GetBreakpointLineCount(Session.GetDebugServer(), ModuleKeyB), 1);
	TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should stay in debugging mode while validating ClearBreakpoints"), Session.GetDebugServer().bIsDebugging);

	TAtomic<bool> bFirstMonitorReady(false);
	FBreakpointMonitorConfig FirstMonitorConfig;
	FirstMonitorConfig.bSendContinueOnStop = true;
	FirstMonitorConfig.bSendStopDebuggingOnExit = false;
	FirstMonitorConfig.TimeoutSeconds = Session.GetDefaultTimeoutSeconds();
	if (!StartAndWaitForMonitorReady(*this, Session, Session.GetPort(), bFirstMonitorReady, bFirstMonitorShouldStop, FirstMonitorConfig, FirstMonitorFuture))
	{
		return false;
	}

	const TSharedRef<FAsyncModuleInvocationState> FirstInvocation = DispatchModuleInvocation(
		Engine,
		FixtureA.Filename,
		FixtureA.ModuleName,
		FixtureA.EntryFunctionDeclaration);

	if (!WaitForInvocationCompletion(
			*this,
			Session,
			FirstInvocation,
			TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should let fixture A finish executing after its breakpoint is cleared")))
	{
		bFirstMonitorShouldStop = true;
		return false;
	}

	bFirstMonitorShouldStop = true;
	const FBreakpointMonitorResult FirstMonitorResult = FirstMonitorFuture.Get();
	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should keep the cleared fixture A run free of monitor transport errors"), FirstMonitorResult.Error.IsEmpty()))
	{
		AddError(FirstMonitorResult.Error);
		return false;
	}

	TestFalse(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should not time out while validating the cleared fixture A run"), FirstMonitorResult.bTimedOut);
	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should not emit any HasStopped events when running the cleared fixture A"), FirstMonitorResult.StopEnvelopes.Num(), 0);
	TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should still execute fixture A successfully after its breakpoint is cleared"), FirstInvocation->bSucceeded);
	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should preserve fixture A's return value after its breakpoint is cleared"), FirstInvocation->Result, 8);
	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should keep exactly one authoritative breakpoint after running the cleared fixture A"), Session.GetDebugServer().BreakpointCount, 1);

	TAtomic<bool> bSecondMonitorReady(false);
	FBreakpointMonitorConfig SecondMonitorConfig;
	SecondMonitorConfig.bRequestCallstack = true;
	SecondMonitorConfig.bSendContinueOnStop = true;
	SecondMonitorConfig.bSendStopDebuggingOnExit = false;
	SecondMonitorConfig.TimeoutSeconds = Session.GetDefaultTimeoutSeconds();
	if (!StartAndWaitForMonitorReady(*this, Session, Session.GetPort(), bSecondMonitorReady, bSecondMonitorShouldStop, SecondMonitorConfig, SecondMonitorFuture))
	{
		return false;
	}

	const TSharedRef<FAsyncModuleInvocationState> SecondInvocation = DispatchModuleInvocation(
		Engine,
		FixtureB.Filename,
		FixtureB.ModuleName,
		FixtureB.EntryFunctionDeclaration);

	if (!WaitForInvocationCompletion(
			*this,
			Session,
			SecondInvocation,
			TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should let fixture B resume and finish after its preserved breakpoint stops once")))
	{
		bSecondMonitorShouldStop = true;
		return false;
	}

	bSecondMonitorShouldStop = true;
	const FBreakpointMonitorResult SecondMonitorResult = SecondMonitorFuture.Get();
	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should keep the preserved fixture B run free of monitor transport errors"), SecondMonitorResult.Error.IsEmpty()))
	{
		AddError(SecondMonitorResult.Error);
		return false;
	}

	if (!TestFalse(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should not time out while waiting for fixture B's preserved breakpoint"), SecondMonitorResult.bTimedOut))
	{
		return false;
	}

	if (!TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should stop exactly once when running fixture B after clearing fixture A"), SecondMonitorResult.StopEnvelopes.Num(), 1))
	{
		return false;
	}

	const TOptional<FStoppedMessage> StopMessage = FAngelscriptDebuggerTestClient::DeserializeMessage<FStoppedMessage>(SecondMonitorResult.StopEnvelopes[0]);
	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should deserialize fixture B's stop payload"), StopMessage.IsSet()))
	{
		return false;
	}

	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should stop fixture B because of a breakpoint"), StopMessage->Reason, FString(TEXT("breakpoint")));

	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should capture fixture B's callstack"), SecondMonitorResult.CapturedCallstack.IsSet()))
	{
		return false;
	}

	const FAngelscriptCallStack& Callstack = SecondMonitorResult.CapturedCallstack.GetValue();
	if (!TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should receive at least one frame in fixture B's callstack"), Callstack.Frames.Num() > 0))
	{
		return false;
	}

	TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should report fixture B's filename in the top stack frame"), Callstack.Frames[0].Source.EndsWith(FixtureB.Filename));
	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should stop fixture B on its preserved helper line"), Callstack.Frames[0].LineNumber, BreakpointLineB);
	TestTrue(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should execute fixture B successfully after continuing from its preserved breakpoint"), SecondInvocation->bSucceeded);
	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should preserve fixture B's distinct return value"), SecondInvocation->Result, 12);
	TestEqual(TEXT("Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints should keep the authoritative breakpoint count at one after validating fixture B"), Session.GetDebugServer().BreakpointCount, 1);
	return !HasAnyErrors();
}

#endif
