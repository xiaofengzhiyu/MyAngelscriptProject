#include "AngelscriptDebuggerTestSession.h"

#include "Binds/Bind_Debugging.h"
#include "ClassGenerator/ASClass.h"
#include "Debugging/AngelscriptDebugServer.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"

namespace AngelscriptTestSupport
{
	namespace
	{
		struct FGeneratedBoolInvocationParams
		{
			bool Condition = false;
			FString Message;
			bool ReturnValue = false;
		};

		struct FGeneratedVoidInvocationParams
		{
			bool Condition = false;
			FString Message;
		};

		int32 MakeUniqueDebugServerPort()
		{
			static int32 NextOffset = 0;
			constexpr int32 BasePort = 30000;
			constexpr int32 PortWindow = 10000;
			const int32 ProcessBucket = (FPlatformProcess::GetCurrentProcessId() % 500) * 10;
			const int32 Port = BasePort + ProcessBucket + NextOffset;
			NextOffset = (NextOffset + 1) % 10;
			return FMath::Clamp(Port, BasePort, BasePort + PortWindow - 1);
		}

		bool InvokeGeneratedFunction(FAngelscriptEngine& Engine, UObject* Object, UFunction* Function, void* Params)
		{
			if (!::IsValid(Object) || Function == nullptr)
			{
				return false;
			}

			FAngelscriptEngineScope FunctionScope(Engine, Object);
			if (UASFunction* ScriptFunction = Cast<UASFunction>(Function))
			{
				ScriptFunction->RuntimeCallEvent(Object, Params);
			}
			else
			{
				Object->ProcessEvent(Function, Params);
			}

			return true;
		}
	}

	FAngelscriptDebuggerTestSession::~FAngelscriptDebuggerTestSession()
	{
		Shutdown();
	}

	bool FAngelscriptDebuggerTestSession::Initialize(const FAngelscriptDebuggerSessionConfig& Config)
	{
		Shutdown();

		DefaultTimeoutSeconds = Config.DefaultTimeoutSeconds > 0.0f ? Config.DefaultTimeoutSeconds : 5.0f;
		bResetSeenEnsuresOnShutdown = Config.bResetSeenEnsuresOnShutdown;
		PreviousDebugAdapterVersion = AngelscriptDebugServer::DebugAdapterVersion;
		bHasCapturedDebugAdapterVersion = true;

		if (Config.bResetSeenEnsuresOnInitialize)
		{
			AngelscriptForgetSeenEnsures();
		}

		if (Config.bDisableDebugBreaks)
		{
			bPreviousDebugBreakStateEnabled = AreAngelscriptDebugBreaksEnabledForTesting();
			bHasCapturedDebugBreakState = true;
			AngelscriptDisableDebugBreaks();
		}

		if (Config.ExistingEngine != nullptr)
		{
			Engine = Config.ExistingEngine;
		}
		else
		{
			FAngelscriptEngineConfig EngineConfig;
			EngineConfig.DebugServerPort = Config.DebugServerPort > 0 ? Config.DebugServerPort : MakeUniqueDebugServerPort();
			FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
			OwnedEngine = FAngelscriptEngine::CreateTestingFullEngine(EngineConfig, Dependencies);
			Engine = OwnedEngine.Get();
		}

		if (Engine == nullptr)
		{
			Shutdown();
			return false;
		}

		GlobalScope = MakeUnique<FAngelscriptEngineScope>(*Engine);
		DebugServer = Engine->DebugServer;
		Port = Engine->GetRuntimeConfig().DebugServerPort;

		if (DebugServer == nullptr)
		{
			Shutdown();
			return false;
		}

		return true;
	}

	void FAngelscriptDebuggerTestSession::Shutdown()
	{
		if (bResetSeenEnsuresOnShutdown)
		{
			AngelscriptForgetSeenEnsures();
		}

		if (bHasCapturedDebugBreakState)
		{
			if (bPreviousDebugBreakStateEnabled)
			{
				AngelscriptEnableDebugBreaks();
			}
			else
			{
				AngelscriptDisableDebugBreaks();
			}
			bHasCapturedDebugBreakState = false;
		}

		bPreviousDebugBreakStateEnabled = true;

		if (bHasCapturedDebugAdapterVersion)
		{
			AngelscriptDebugServer::DebugAdapterVersion = PreviousDebugAdapterVersion;
			bHasCapturedDebugAdapterVersion = false;
		}

		PreviousDebugAdapterVersion = 0;

		DebugServer = nullptr;
		Port = 0;
		GlobalScope.Reset();
		Engine = nullptr;
		OwnedEngine.Reset();
	}

	bool FAngelscriptDebuggerTestSession::PumpOneTick()
	{
		if (!IsInitialized())
		{
			return false;
		}

		if (IsInGameThread())
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread_Local);
		}

		DebugServer->Tick();
		FPlatformProcess::Sleep(0.0f);
		return true;
	}

	bool FAngelscriptDebuggerTestSession::PumpUntil(TFunctionRef<bool()> Predicate, float TimeoutSeconds)
	{
		if (!IsInitialized())
		{
			return false;
		}

		if (Predicate())
		{
			return true;
		}

		const double Timeout = TimeoutSeconds > 0.0f ? TimeoutSeconds : DefaultTimeoutSeconds;
		const double EndTime = FPlatformTime::Seconds() + Timeout;

		while (FPlatformTime::Seconds() < EndTime)
		{
			if (!PumpOneTick())
			{
				return false;
			}

			if (Predicate())
			{
				return true;
			}
		}

		return Predicate();
	}

	FAngelscriptEngine& FAngelscriptDebuggerTestSession::GetEngine() const
	{
		check(Engine != nullptr);
		return *Engine;
	}

	FAngelscriptDebugServer& FAngelscriptDebuggerTestSession::GetDebugServer() const
	{
		check(DebugServer != nullptr);
		return *DebugServer;
	}

	bool WaitForDebugServerIdle(FAngelscriptDebuggerTestSession& Session, float TimeoutSeconds)
	{
		if (!Session.IsInitialized())
		{
			return false;
		}

		return Session.PumpUntil(
			[&Session]()
			{
				const FAngelscriptDebugServer& DebugServer = Session.GetDebugServer();
				return !DebugServer.bIsDebugging && DebugServer.BreakpointCount == 0;
			},
			TimeoutSeconds);
	}

	TSharedRef<FAsyncGeneratedVoidInvocationState> DispatchGeneratedVoidInvocation(
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function)
	{
		TSharedRef<FAsyncGeneratedVoidInvocationState> State = MakeShared<FAsyncGeneratedVoidInvocationState>();

		AsyncTask(ENamedThreads::GameThread, [&Engine, Object, Function, State]()
		{
			State->bSucceeded = InvokeGeneratedFunction(Engine, Object, Function, nullptr);
			State->bCompleted = true;
		});

		return State;
	}

	TSharedRef<FAsyncGeneratedVoidInvocationState> DispatchGeneratedVoidInvocation(
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		bool Condition,
		const FString& Message)
	{
		TSharedRef<FAsyncGeneratedVoidInvocationState> State = MakeShared<FAsyncGeneratedVoidInvocationState>();

		AsyncTask(ENamedThreads::GameThread, [&Engine, Object, Function, Condition, Message, State]()
		{
			FGeneratedVoidInvocationParams Params;
			Params.Condition = Condition;
			Params.Message = Message;
			State->bSucceeded = InvokeGeneratedFunction(Engine, Object, Function, &Params);
			State->bCompleted = true;
		});

		return State;
	}

	TSharedRef<FAsyncGeneratedBoolInvocationState> DispatchGeneratedBoolInvocation(
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		bool Condition,
		const FString& Message)
	{
		TSharedRef<FAsyncGeneratedBoolInvocationState> State = MakeShared<FAsyncGeneratedBoolInvocationState>();

		AsyncTask(ENamedThreads::GameThread, [&Engine, Object, Function, Condition, Message, State]()
		{
			FGeneratedBoolInvocationParams Params;
			Params.Condition = Condition;
			Params.Message = Message;
			State->bSucceeded = InvokeGeneratedFunction(Engine, Object, Function, &Params);
			State->bReturnValue = Params.ReturnValue;
			State->bCompleted = true;
		});

		return State;
	}
}
