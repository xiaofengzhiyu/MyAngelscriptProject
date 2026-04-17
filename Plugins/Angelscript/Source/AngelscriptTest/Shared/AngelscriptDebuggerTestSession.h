#pragma once

#include "Binds/Bind_Debugging.h"
#include "CoreMinimal.h"
#include "Debugging/AngelscriptDebugServer.h"
#include "Templates/Function.h"

#include "AngelscriptTestUtilities.h"

class FAngelscriptDebugServer;
class UObject;
class UFunction;

namespace AngelscriptTestSupport
{
	struct FAngelscriptDebuggerSessionConfig
	{
		FAngelscriptEngine* ExistingEngine = nullptr;
		int32 DebugServerPort = 0;
		float DefaultTimeoutSeconds = 5.0f;
		bool bDisableDebugBreaks = false;
		bool bResetSeenEnsuresOnInitialize = true;
		bool bResetSeenEnsuresOnShutdown = true;
	};

	struct FScopedDebugAdapterVersionSentinel
	{
		explicit FScopedDebugAdapterVersionSentinel(int32 InSentinelValue)
			: OriginalValue(AngelscriptDebugServer::DebugAdapterVersion)
		{
			SetSentinel(InSentinelValue);
		}

		~FScopedDebugAdapterVersionSentinel()
		{
			AngelscriptDebugServer::DebugAdapterVersion = OriginalValue;
		}

		void SetSentinel(int32 InSentinelValue)
		{
			SentinelValue = InSentinelValue;
			AngelscriptDebugServer::DebugAdapterVersion = SentinelValue;
		}

		int32 GetCurrent() const
		{
			return AngelscriptDebugServer::DebugAdapterVersion;
		}

		int32 GetOriginal() const
		{
			return OriginalValue;
		}

		int32 GetSentinel() const
		{
			return SentinelValue;
		}

	private:
		int32 OriginalValue = 0;
		int32 SentinelValue = 0;
	};

	struct FScopedDebugBreakStateSentinel
	{
		FScopedDebugBreakStateSentinel()
			: bOriginalValueEnabled(AreAngelscriptDebugBreaksEnabledForTesting())
		{
		}

		~FScopedDebugBreakStateSentinel()
		{
			SetEnabled(bOriginalValueEnabled);
		}

		void SetEnabled(const bool bEnabled) const
		{
			if (bEnabled)
			{
				AngelscriptEnableDebugBreaks();
			}
			else
			{
				AngelscriptDisableDebugBreaks();
			}
		}

		bool IsEnabled() const
		{
			return AreAngelscriptDebugBreaksEnabledForTesting();
		}

		bool GetOriginalValue() const
		{
			return bOriginalValueEnabled;
		}

	private:
		bool bOriginalValueEnabled = true;
	};

	class FAngelscriptDebuggerTestSession
	{
public:
		FAngelscriptDebuggerTestSession() = default;
		~FAngelscriptDebuggerTestSession();

		bool Initialize(const FAngelscriptDebuggerSessionConfig& Config = FAngelscriptDebuggerSessionConfig());
		void Shutdown();

		bool IsInitialized() const
		{
			return Engine != nullptr && DebugServer != nullptr;
		}

		bool PumpOneTick();
		bool PumpUntil(TFunctionRef<bool()> Predicate, float TimeoutSeconds = 0.0f);

		FAngelscriptEngine& GetEngine() const;
		FAngelscriptDebugServer& GetDebugServer() const;
		int32 GetPort() const
		{
			return Port;
		}

		float GetDefaultTimeoutSeconds() const
		{
			return DefaultTimeoutSeconds;
		}

	private:
		TUniquePtr<FAngelscriptEngine> OwnedEngine;
		FAngelscriptEngine* Engine = nullptr;
		FAngelscriptDebugServer* DebugServer = nullptr;
		TUniquePtr<FAngelscriptEngineScope> GlobalScope;

		int32 Port = 0;
		float DefaultTimeoutSeconds = 5.0f;
		bool bResetSeenEnsuresOnShutdown = true;
		bool bHasCapturedDebugBreakState = false;
		bool bPreviousDebugBreakStateEnabled = true;
		bool bHasCapturedDebugAdapterVersion = false;
		int32 PreviousDebugAdapterVersion = 0;
	};

	struct FAsyncGeneratedVoidInvocationState : public TSharedFromThis<FAsyncGeneratedVoidInvocationState>
	{
		TAtomic<bool> bCompleted = false;
		bool bSucceeded = false;
	};

	struct FAsyncGeneratedBoolInvocationState : public TSharedFromThis<FAsyncGeneratedBoolInvocationState>
	{
		TAtomic<bool> bCompleted = false;
		bool bSucceeded = false;
		bool bReturnValue = false;
	};

	TSharedRef<FAsyncGeneratedVoidInvocationState> DispatchGeneratedVoidInvocation(
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function);

	TSharedRef<FAsyncGeneratedVoidInvocationState> DispatchGeneratedVoidInvocation(
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		bool Condition,
		const FString& Message);

	TSharedRef<FAsyncGeneratedBoolInvocationState> DispatchGeneratedBoolInvocation(
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		bool Condition,
		const FString& Message);

	bool WaitForDebugServerIdle(
		FAngelscriptDebuggerTestSession& Session,
		float TimeoutSeconds = 0.0f);
}
