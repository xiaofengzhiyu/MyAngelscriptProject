#include "Shared/AngelscriptDebuggerScriptFixture.h"
#include "Shared/AngelscriptDebuggerTestClient.h"
#include "Shared/AngelscriptDebuggerTestSession.h"
#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Debugger_AngelscriptDebuggerResumeProtocolTests_Private
{
	bool StartResumeProtocolDebuggerSession(FAutomationTestBase& Test, FAngelscriptDebuggerTestSession& Session)
	{
		FAngelscriptDebuggerSessionConfig SessionConfig;
		// UE 5.7: headless has no production subsystem. Let Initialize() create
		// an isolated FAngelscriptEngine with its own FAngelscriptDebugServer.
		SessionConfig.DefaultTimeoutSeconds = 45.0f;

		return Test.TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should initialize the debugger session"), Session.Initialize(SessionConfig));
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

	TSharedRef<FAsyncModuleInvocationState> CreateAsyncModuleInvocationState()
	{
		return MakeShared<FAsyncModuleInvocationState>();
	}

	void DispatchModuleInvocation(
		FAngelscriptEngine& Engine,
		const FString& Filename,
		FName ModuleName,
		const FString& Declaration,
		const TSharedRef<FAsyncModuleInvocationState>& State)
	{
		AsyncTask(ENamedThreads::GameThread, [&Engine, Filename, ModuleName, Declaration, State]()
		{
			int32 InvocationResult = 0;
			State->bSucceeded = ExecuteIntFunction(&Engine, Filename, ModuleName, Declaration, InvocationResult);
			State->Result = InvocationResult;
			State->bCompleted = true;
		});
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

	bool WaitForMonitorReady(
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

	struct FResumeProtocolTranscript
	{
		TArray<FAngelscriptDebugMessageEnvelope> ReceivedMessages;
		TOptional<FDebugServerVersionMessage> DebugServerVersion;
		TArray<FStoppedMessage> StopMessages;
		TArray<FAngelscriptCallStack> CallStacks;
		int32 HasContinuedCount = 0;
		bool bTimedOut = false;
		FString Error;
	};

	bool RecordResumeProtocolEnvelope(
		FResumeProtocolTranscript& Transcript,
		const FAngelscriptDebugMessageEnvelope& Envelope,
		const TAtomic<bool>& bInvocationCompleted,
		FString& OutError)
	{
		Transcript.ReceivedMessages.Add(Envelope);

		switch (Envelope.MessageType)
		{
		case EDebugMessageType::DebugServerVersion:
			if (!Transcript.DebugServerVersion.IsSet())
			{
				const TOptional<FDebugServerVersionMessage> Version = FAngelscriptDebuggerTestClient::DeserializeMessage<FDebugServerVersionMessage>(Envelope);
				if (!Version.IsSet())
				{
					OutError = TEXT("Resume protocol monitor failed to deserialize DebugServerVersion.");
					return false;
				}

				Transcript.DebugServerVersion = Version;
			}
			break;

		case EDebugMessageType::HasStopped:
			{
				const TOptional<FStoppedMessage> StopMessage = FAngelscriptDebuggerTestClient::DeserializeMessage<FStoppedMessage>(Envelope);
				if (!StopMessage.IsSet())
				{
					OutError = TEXT("Resume protocol monitor failed to deserialize HasStopped.");
					return false;
				}

				Transcript.StopMessages.Add(StopMessage.GetValue());
			}
			break;

		case EDebugMessageType::CallStack:
			{
				const TOptional<FAngelscriptCallStack> CallStack = FAngelscriptDebuggerTestClient::DeserializeMessage<FAngelscriptCallStack>(Envelope);
				if (!CallStack.IsSet())
				{
					OutError = TEXT("Resume protocol monitor failed to deserialize CallStack.");
					return false;
				}

				Transcript.CallStacks.Add(CallStack.GetValue());
			}
			break;

		case EDebugMessageType::HasContinued:
			++Transcript.HasContinuedCount;
			break;

		default:
			break;
		}

		return true;
	}

	bool WaitForRecordedMessageType(
		FAngelscriptDebuggerTestClient& Client,
		TAtomic<bool>& bShouldStop,
		const TAtomic<bool>& bInvocationCompleted,
		const EDebugMessageType ExpectedType,
		const float TimeoutSeconds,
		FResumeProtocolTranscript& Transcript,
		FString& OutError)
	{
		const double EndTime = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() < EndTime)
		{
			if (bShouldStop.Load())
			{
				OutError = FString::Printf(TEXT("Resume protocol monitor aborted while waiting for message type %d."), static_cast<int32>(ExpectedType));
				return false;
			}

			const TOptional<FAngelscriptDebugMessageEnvelope> Envelope = Client.ReceiveEnvelope();
			if (Envelope.IsSet())
			{
				if (!RecordResumeProtocolEnvelope(Transcript, Envelope.GetValue(), bInvocationCompleted, OutError))
				{
					return false;
				}

				if (Envelope->MessageType == ExpectedType)
				{
					return true;
				}
			}
			else if (!Client.GetLastError().IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Resume protocol monitor failed while waiting for message type %d: %s"),
					static_cast<int32>(ExpectedType),
					*Client.GetLastError());
				return false;
			}

			FPlatformProcess::Sleep(0.001f);
		}

		Transcript.bTimedOut = true;
		OutError = FString::Printf(
			TEXT("Resume protocol monitor timed out after %.3f seconds waiting for message type %d."),
			TimeoutSeconds,
			static_cast<int32>(ExpectedType));
		return false;
	}

	bool WaitForInvocationAndDrainMessages(
		FAngelscriptDebuggerTestClient& Client,
		TAtomic<bool>& bShouldStop,
		const TAtomic<bool>& bInvocationCompleted,
		const float TimeoutSeconds,
		const float PostCompletionDrainSeconds,
		FResumeProtocolTranscript& Transcript,
		FString& OutError)
	{
		const double InvocationEndTime = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() < InvocationEndTime)
		{
			if (bShouldStop.Load())
			{
				return true;
			}

			const TOptional<FAngelscriptDebugMessageEnvelope> Envelope = Client.ReceiveEnvelope();
			if (Envelope.IsSet())
			{
				if (!RecordResumeProtocolEnvelope(Transcript, Envelope.GetValue(), bInvocationCompleted, OutError))
				{
					return false;
				}
			}
			else if (!Client.GetLastError().IsEmpty())
			{
				OutError = FString::Printf(TEXT("Resume protocol monitor failed while waiting for invocation completion: %s"), *Client.GetLastError());
				return false;
			}

			if (bInvocationCompleted.Load())
			{
				break;
			}

			FPlatformProcess::Sleep(0.001f);
		}

		if (!bInvocationCompleted.Load())
		{
			Transcript.bTimedOut = true;
			OutError = FString::Printf(
				TEXT("Resume protocol monitor timed out after %.3f seconds waiting for invocation completion."),
				TimeoutSeconds);
			return false;
		}

		const double DrainEndTime = FPlatformTime::Seconds() + PostCompletionDrainSeconds;
		while (FPlatformTime::Seconds() < DrainEndTime)
		{
			if (bShouldStop.Load())
			{
				return true;
			}

			const TOptional<FAngelscriptDebugMessageEnvelope> Envelope = Client.ReceiveEnvelope();
			if (Envelope.IsSet())
			{
				if (!RecordResumeProtocolEnvelope(Transcript, Envelope.GetValue(), bInvocationCompleted, OutError))
				{
					return false;
				}
			}
			else if (!Client.GetLastError().IsEmpty())
			{
				OutError = FString::Printf(TEXT("Resume protocol monitor failed while draining post-completion messages: %s"), *Client.GetLastError());
				return false;
			}
			else
			{
				FPlatformProcess::Sleep(0.001f);
			}
		}

		return true;
	}

	TFuture<FResumeProtocolTranscript> StartResumeProtocolMonitor(
		int32 Port,
		TAtomic<bool>& bMonitorReady,
		TAtomic<bool>& bShouldStop,
		TAtomic<bool>& bInvocationCompleted,
		const FAngelscriptBreakpoint& Breakpoint,
		float TimeoutSeconds)
	{
		return Async(EAsyncExecution::ThreadPool,
			[Port, &bMonitorReady, &bShouldStop, &bInvocationCompleted, Breakpoint, TimeoutSeconds]() -> FResumeProtocolTranscript
			{
				constexpr float PostCompletionDrainSeconds = 0.05f;

				FResumeProtocolTranscript Transcript;
				FAngelscriptDebuggerTestClient Client;
				ON_SCOPE_EXIT
				{
					Client.SendStopDebugging();
					Client.SendDisconnect();
					Client.Disconnect();
				};

				bMonitorReady = false;

				if (!Client.Connect(TEXT("127.0.0.1"), Port, TimeoutSeconds))
				{
					Transcript.Error = FString::Printf(TEXT("Resume protocol monitor failed to connect: %s"), *Client.GetLastError());
					bMonitorReady = true;
					return Transcript;
				}

				if (!Client.SendStartDebugging(2))
				{
					Transcript.Error = FString::Printf(TEXT("Resume protocol monitor failed to send StartDebugging: %s"), *Client.GetLastError());
					bMonitorReady = true;
					return Transcript;
				}

				FString Error;
				if (!WaitForRecordedMessageType(
					Client,
					bShouldStop,
					bInvocationCompleted,
					EDebugMessageType::DebugServerVersion,
					TimeoutSeconds,
					Transcript,
					Error))
				{
					Transcript.Error = Error;
					bMonitorReady = true;
					return Transcript;
				}

				if (!Client.SendSetBreakpoint(Breakpoint))
				{
					Transcript.Error = FString::Printf(TEXT("Resume protocol monitor failed to send SetBreakpoint: %s"), *Client.GetLastError());
					bMonitorReady = true;
					return Transcript;
				}

				bMonitorReady = true;

				if (!WaitForRecordedMessageType(
					Client,
					bShouldStop,
					bInvocationCompleted,
					EDebugMessageType::HasStopped,
					TimeoutSeconds,
					Transcript,
					Error))
				{
					Transcript.Error = Error;
					return Transcript;
				}

				if (!Client.SendRequestCallStack())
				{
					Transcript.Error = FString::Printf(TEXT("Resume protocol monitor failed to request callstack: %s"), *Client.GetLastError());
					return Transcript;
				}

				if (!WaitForRecordedMessageType(
					Client,
					bShouldStop,
					bInvocationCompleted,
					EDebugMessageType::CallStack,
					TimeoutSeconds,
					Transcript,
					Error))
				{
					Transcript.Error = Error;
					return Transcript;
				}

				if (!Client.SendContinue())
				{
					Transcript.Error = FString::Printf(TEXT("Resume protocol monitor failed to send Continue: %s"), *Client.GetLastError());
					return Transcript;
				}

				if (!WaitForRecordedMessageType(
					Client,
					bShouldStop,
					bInvocationCompleted,
					EDebugMessageType::HasContinued,
					TimeoutSeconds,
					Transcript,
					Error))
				{
					Transcript.Error = Error;
					return Transcript;
				}

				if (!WaitForInvocationAndDrainMessages(
					Client,
					bShouldStop,
					bInvocationCompleted,
					TimeoutSeconds,
					PostCompletionDrainSeconds,
					Transcript,
					Error))
				{
					Transcript.Error = Error;
				}

				return Transcript;
			});
	}

	int32 CountMessagesByType(const FResumeProtocolTranscript& Transcript, EDebugMessageType Type)
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

	int32 FindFirstMessageIndexByType(const FResumeProtocolTranscript& Transcript, EDebugMessageType Type)
	{
		for (int32 Index = 0; Index < Transcript.ReceivedMessages.Num(); ++Index)
		{
			if (Transcript.ReceivedMessages[Index].MessageType == Type)
			{
				return Index;
			}
		}

		return INDEX_NONE;
	}

	int32 CountMessagesByTypeAfterIndex(const FResumeProtocolTranscript& Transcript, EDebugMessageType Type, int32 StartIndexExclusive)
	{
		int32 Count = 0;
		for (int32 Index = StartIndexExclusive + 1; Index < Transcript.ReceivedMessages.Num(); ++Index)
		{
			if (Transcript.ReceivedMessages[Index].MessageType == Type)
			{
				++Count;
			}
		}

		return Count;
	}
}

using namespace AngelscriptTest_Debugger_AngelscriptDebuggerResumeProtocolTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebuggerContinueEmitsHasContinuedTest,
	"Angelscript.TestModule.Debugger.Protocol.ContinueEmitsHasContinued",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebuggerContinueEmitsHasContinuedTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptDebuggerTestSession Session;
	if (!StartResumeProtocolDebuggerSession(*this, Session))
	{
		return false;
	}

	FAngelscriptEngine& Engine = Session.GetEngine();
	ASTEST_BEGIN_SHARE
		const FAngelscriptDebuggerScriptFixture Fixture = FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture();
		TAtomic<bool> bMonitorReady{false};
		TAtomic<bool> bAbortMonitor{false};
		bool bMonitorStarted = false;
		bool bMonitorJoined = false;
		TFuture<FResumeProtocolTranscript> MonitorFuture;

		ON_SCOPE_EXIT
		{
			bAbortMonitor = true;
			if (bMonitorStarted && !bMonitorJoined)
			{
				MonitorFuture.Wait();
			}

			Engine.DiscardModule(*Fixture.ModuleName.ToString());
			CollectGarbage(RF_NoFlags, true);
		};

		if (!TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should compile the breakpoint fixture"), Fixture.Compile(Engine)))
		{
			return false;
		}

		TSharedPtr<FAngelscriptModuleDesc> ModuleDesc = Engine.GetModuleByFilenameOrModuleName(Fixture.Filename, Fixture.ModuleName.ToString());
		if (!TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should resolve the compiled module immediately after compilation"), ModuleDesc.IsValid() && ModuleDesc->ScriptModule != nullptr))
		{
			return false;
		}

		FAngelscriptBreakpoint Breakpoint;
		Breakpoint.Filename = Fixture.Filename;
		Breakpoint.ModuleName = Fixture.ModuleName.ToString();
		Breakpoint.LineNumber = Fixture.GetLine(TEXT("BreakpointHelperLine"));

		const TSharedRef<FAsyncModuleInvocationState> InvocationState = CreateAsyncModuleInvocationState();

		MonitorFuture = StartResumeProtocolMonitor(
			Session.GetPort(),
			bMonitorReady,
			bAbortMonitor,
			InvocationState->bCompleted,
			Breakpoint,
			Session.GetDefaultTimeoutSeconds());
		bMonitorStarted = true;

		if (!WaitForMonitorReady(*this, Session, bMonitorReady, TEXT("Debugger.Protocol.ContinueEmitsHasContinued should finish handshake and breakpoint registration before execution")))
		{
			return false;
		}

		if (!WaitForBreakpointCount(*this, Session, 1, TEXT("Debugger.Protocol.ContinueEmitsHasContinued should observe the breakpoint registration before running the script")))
		{
			return false;
		}

		DispatchModuleInvocation(
			Engine,
			Fixture.Filename,
			Fixture.ModuleName,
			Fixture.EntryFunctionDeclaration,
			InvocationState);

		if (!WaitForInvocationCompletion(*this, Session, InvocationState, TEXT("Debugger.Protocol.ContinueEmitsHasContinued should complete script invocation after Continue")))
		{
			return false;
		}

		FResumeProtocolTranscript Transcript = MonitorFuture.Get();
		bMonitorJoined = true;

		if (!Transcript.Error.IsEmpty())
		{
			AddError(Transcript.Error);
			return false;
		}

		TestFalse(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should not time out"), Transcript.bTimedOut);
		if (!TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should receive DebugServerVersion on the same client"), Transcript.DebugServerVersion.IsSet()))
		{
			return false;
		}

		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should keep the adapter handshake on the same socket"), Transcript.DebugServerVersion->DebugServerVersion, DEBUG_SERVER_VERSION);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should receive exactly one DebugServerVersion envelope"), CountMessagesByType(Transcript, EDebugMessageType::DebugServerVersion), 1);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should receive exactly one HasStopped envelope"), CountMessagesByType(Transcript, EDebugMessageType::HasStopped), 1);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should receive exactly one CallStack envelope"), CountMessagesByType(Transcript, EDebugMessageType::CallStack), 1);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should receive exactly one HasContinued envelope"), CountMessagesByType(Transcript, EDebugMessageType::HasContinued), 1);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should deserialize exactly one stop message"), Transcript.StopMessages.Num(), 1);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should deserialize exactly one callstack"), Transcript.CallStacks.Num(), 1);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should count exactly one HasContinued notification"), Transcript.HasContinuedCount, 1);
		TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should finish the script invocation successfully"), InvocationState->bSucceeded);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should preserve the expected script result"), InvocationState->Result, 8);

		if (!TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should deserialize the stop reason"), Transcript.StopMessages.Num() == 1))
		{
			return false;
		}

		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should stop because of a breakpoint"), Transcript.StopMessages[0].Reason, FString(TEXT("breakpoint")));

		if (!TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should capture exactly one callstack"), Transcript.CallStacks.Num() == 1))
		{
			return false;
		}

		const FAngelscriptCallStack& CallStack = Transcript.CallStacks[0];
		if (!TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should return at least one frame"), CallStack.Frames.Num() > 0))
		{
			return false;
		}

		TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should report the fixture filename in the top stack frame"), CallStack.Frames[0].Source.EndsWith(Fixture.Filename));
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should stop at the requested helper line"), CallStack.Frames[0].LineNumber, Fixture.GetLine(TEXT("BreakpointHelperLine")));

		const int32 VersionIndex = FindFirstMessageIndexByType(Transcript, EDebugMessageType::DebugServerVersion);
		const int32 StopIndex = FindFirstMessageIndexByType(Transcript, EDebugMessageType::HasStopped);
		const int32 CallStackIndex = FindFirstMessageIndexByType(Transcript, EDebugMessageType::CallStack);
		const int32 ContinuedIndex = FindFirstMessageIndexByType(Transcript, EDebugMessageType::HasContinued);
		const int32 StopCountAfterContinued = ContinuedIndex != INDEX_NONE
			? CountMessagesByTypeAfterIndex(Transcript, EDebugMessageType::HasStopped, ContinuedIndex)
			: CountMessagesByType(Transcript, EDebugMessageType::HasStopped);
		TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should observe DebugServerVersion before the breakpoint stop"), VersionIndex != INDEX_NONE && StopIndex != INDEX_NONE && VersionIndex < StopIndex);
		TestTrue(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should observe CallStack after the stop and before the continue notification"), StopIndex != INDEX_NONE && CallStackIndex != INDEX_NONE && ContinuedIndex != INDEX_NONE && StopIndex < CallStackIndex && CallStackIndex < ContinuedIndex);
		TestEqual(TEXT("Debugger.Protocol.ContinueEmitsHasContinued should not emit a second HasStopped after HasContinued"), StopCountAfterContinued, 0);

		bPassed = true;
	ASTEST_END_SHARE

	return bPassed;
}

#endif
