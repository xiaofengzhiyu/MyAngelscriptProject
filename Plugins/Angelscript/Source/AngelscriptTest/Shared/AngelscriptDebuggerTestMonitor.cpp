#include "AngelscriptDebuggerTestMonitor.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptTestSupport
{
	// =========================================================================
	// Async Module Invocation
	// =========================================================================

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

	// =========================================================================
	// Monitor Handshake Helper
	// =========================================================================

	bool HandshakeMonitorClient(
		FAngelscriptDebuggerTestClient& Client,
		TAtomic<bool>& bShouldStop,
		int32 AdapterVersion,
		float TimeoutSeconds,
		FString& OutError)
	{
		const double HandshakeEnd = FPlatformTime::Seconds() + TimeoutSeconds;
		bool bSentStart = false;
		bool bReceivedVersion = false;

		while (FPlatformTime::Seconds() < HandshakeEnd && !bShouldStop.Load())
		{
			if (!bSentStart)
			{
				bSentStart = Client.SendStartDebugging(AdapterVersion);
			}

			if (bSentStart && !bReceivedVersion)
			{
				TOptional<FAngelscriptDebugMessageEnvelope> Envelope = Client.ReceiveEnvelope();
				if (Envelope.IsSet() && Envelope->MessageType == EDebugMessageType::DebugServerVersion)
				{
					bReceivedVersion = true;
					break;
				}
			}

			FPlatformProcess::Sleep(0.001f);
		}

		if (!bReceivedVersion)
		{
			OutError = bShouldStop.Load()
				? TEXT("Monitor handshake aborted before receiving DebugServerVersion.")
				: TEXT("Monitor timed out waiting for DebugServerVersion handshake.");
			return false;
		}

		return true;
	}

	bool WaitForMonitorReady(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		TAtomic<bool>& bMonitorReady,
		const TCHAR* Context)
	{
		const bool bReady = Session.PumpUntil(
			[&bMonitorReady]()
			{
				return bMonitorReady.Load();
			},
			Session.GetDefaultTimeoutSeconds());

		return Test.TestTrue(Context, bReady);
	}

	// =========================================================================
	// Breakpoint Monitor
	// =========================================================================

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
				if (!MonitorClient.Connect(TEXT("127.0.0.1"), Port))
				{
					Result.Error = FString::Printf(TEXT("Breakpoint monitor failed to connect: %s"), *MonitorClient.GetLastError());
					bMonitorReady = true;
					return Result;
				}

				if (!HandshakeMonitorClient(MonitorClient, bShouldStop, Config.TimeoutSeconds, Result.Error))
				{
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
						FPlatformProcess::Sleep(0.001f);
						continue;
					}

					if (Envelope->MessageType == EDebugMessageType::HasStopped)
					{
						Result.StopEnvelopes.Add(Envelope.GetValue());

						if (Config.bRequestCallstack)
						{
							if (MonitorClient.SendRequestCallStack())
							{
								const double CallstackEnd = FPlatformTime::Seconds() + 10.0;
								while (FPlatformTime::Seconds() < CallstackEnd)
								{
									TOptional<FAngelscriptDebugMessageEnvelope> CsEnvelope = MonitorClient.ReceiveEnvelope();
									if (CsEnvelope.IsSet() && CsEnvelope->MessageType == EDebugMessageType::CallStack)
									{
										Result.CapturedCallstack = FAngelscriptDebuggerTestClient::DeserializeMessage<FAngelscriptCallStack>(CsEnvelope.GetValue());
										break;
									}
									FPlatformProcess::Sleep(0.001f);
								}
							}
						}

						if (Config.bSendContinueOnStop)
						{
							MonitorClient.SendContinue();
						}

						StopsHandled++;
						if (StopsHandled >= Config.MaxStopsToHandle)
						{
							break;
						}
					}
				}

				if (StopsHandled == 0 && !bShouldStop.Load() && FPlatformTime::Seconds() >= MonitorEnd)
				{
					Result.bTimedOut = true;
				}

				MonitorClient.SendStopDebugging();
				MonitorClient.SendDisconnect();
				MonitorClient.Disconnect();
				return Result;
			});
	}

	bool StartAndWaitForBreakpointMonitorReady(
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

		if (!Test.TestTrue(TEXT("Breakpoint monitor should become ready within the timeout"), bReady))
		{
			bShouldStop = true;
			return false;
		}

		return true;
	}

	// =========================================================================
	// Step Monitor
	// =========================================================================

	TFuture<FStepMonitorResult> StartStepMonitor(
		int32 Port,
		TAtomic<bool>& bMonitorReady,
		TAtomic<bool>& bShouldStop,
		TArray<FStepMonitorPhase> Phases,
		float TimeoutSeconds)
	{
		return Async(EAsyncExecution::ThreadPool,
			[Port, &bMonitorReady, &bShouldStop, Phases = MoveTemp(Phases), TimeoutSeconds]() -> FStepMonitorResult
			{
				FStepMonitorResult Result;
				FAngelscriptDebuggerTestClient MonitorClient;
				if (!MonitorClient.Connect(TEXT("127.0.0.1"), Port))
				{
					Result.Error = FString::Printf(TEXT("Step monitor failed to connect: %s"), *MonitorClient.GetLastError());
					bMonitorReady = true;
					return Result;
				}

				if (!HandshakeMonitorClient(MonitorClient, bShouldStop, TimeoutSeconds, Result.Error))
				{
					bMonitorReady = true;
					MonitorClient.SendStopDebugging();
					MonitorClient.SendDisconnect();
					MonitorClient.Disconnect();
					return Result;
				}

				bMonitorReady = true;

				int32 StopsHandled = 0;
				const double MonitorEnd = FPlatformTime::Seconds() + TimeoutSeconds;
				while (FPlatformTime::Seconds() < MonitorEnd && !bShouldStop.Load())
				{
					TOptional<FAngelscriptDebugMessageEnvelope> Envelope = MonitorClient.ReceiveEnvelope();
					if (!Envelope.IsSet())
					{
						FPlatformProcess::Sleep(0.001f);
						continue;
					}

					if (Envelope->MessageType != EDebugMessageType::HasStopped)
					{
						continue;
					}

					if (StopsHandled >= Phases.Num())
					{
						MonitorClient.SendContinue();
						Result.Error = FString::Printf(TEXT("Received unexpected stop #%d beyond configured phases."), StopsHandled + 1);
						break;
					}

					FStepMonitorStop Stop;
					Stop.StopEnvelope = Envelope.GetValue();

					const FStepMonitorPhase& Phase = Phases[StopsHandled];
					if (Phase.bRequestCallstack && MonitorClient.SendRequestCallStack())
					{
						const double CallstackEnd = FPlatformTime::Seconds() + 10.0;
						while (FPlatformTime::Seconds() < CallstackEnd)
						{
							TOptional<FAngelscriptDebugMessageEnvelope> CallstackEnvelope = MonitorClient.ReceiveEnvelope();
							if (CallstackEnvelope.IsSet() && CallstackEnvelope->MessageType == EDebugMessageType::CallStack)
							{
								Stop.Callstack = FAngelscriptDebuggerTestClient::DeserializeMessage<FAngelscriptCallStack>(CallstackEnvelope.GetValue());
								break;
							}
							FPlatformProcess::Sleep(0.001f);
						}
					}

					Result.Stops.Add(MoveTemp(Stop));

					switch (Phase.Action)
					{
					case EStepMonitorAction::Continue:
						MonitorClient.SendContinue();
						break;
					case EStepMonitorAction::StepIn:
						MonitorClient.SendStepIn();
						break;
					case EStepMonitorAction::StepOver:
						MonitorClient.SendStepOver();
						break;
					case EStepMonitorAction::StepOut:
						MonitorClient.SendStepOut();
						break;
					}

					++StopsHandled;
					if (StopsHandled >= Phases.Num())
					{
						break;
					}
				}

				if (StopsHandled == 0 && !bShouldStop.Load() && FPlatformTime::Seconds() >= MonitorEnd)
				{
					Result.bTimedOut = true;
				}

				MonitorClient.SendStopDebugging();
				MonitorClient.SendDisconnect();
				MonitorClient.Disconnect();
				return Result;
			});
	}

	bool StartAndWaitForStepMonitorReady(
		FAutomationTestBase& Test,
		FAngelscriptDebuggerTestSession& Session,
		int32 Port,
		TAtomic<bool>& bMonitorReady,
		TAtomic<bool>& bShouldStop,
		TArray<FStepMonitorPhase> Phases,
		TFuture<FStepMonitorResult>& OutFuture)
	{
		OutFuture = StartStepMonitor(Port, bMonitorReady, bShouldStop, MoveTemp(Phases), Session.GetDefaultTimeoutSeconds());

		const bool bReady = Session.PumpUntil(
			[&bMonitorReady]()
			{
				return bMonitorReady.Load();
			},
			Session.GetDefaultTimeoutSeconds());

		if (!Test.TestTrue(TEXT("Step monitor should become ready within the timeout"), bReady))
		{
			bShouldStop = true;
			return false;
		}

		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
