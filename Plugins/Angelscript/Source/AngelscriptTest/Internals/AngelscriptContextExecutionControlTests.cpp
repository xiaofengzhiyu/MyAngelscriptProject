#include "Angelscript/AngelscriptTestSupport.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Internals_AngelscriptContextExecutionControlTests_Private
{
	struct FExecutionControlProbe
	{
		void Reset()
		{
			InvocationCount = 0;
			StateBeforeAbort = asEXECUTION_UNINITIALIZED;
			AbortResultWhileActive = asERROR;
			StateAfterAbort = asEXECUTION_UNINITIALIZED;
			SuspendResultWhileActive = asERROR;
			StateAfterSuspend = asEXECUTION_UNINITIALIZED;
		}

		int32 InvocationCount = 0;
		asEContextState StateBeforeAbort = asEXECUTION_UNINITIALIZED;
		int32 AbortResultWhileActive = asERROR;
		asEContextState StateAfterAbort = asEXECUTION_UNINITIALIZED;
		int32 SuspendResultWhileActive = asERROR;
		asEContextState StateAfterSuspend = asEXECUTION_UNINITIALIZED;
	};

	thread_local FExecutionControlProbe* GActiveExecutionControlProbe = nullptr;

	struct FScopedExecutionControlProbeBinding
	{
		explicit FScopedExecutionControlProbeBinding(FExecutionControlProbe& InProbe)
			: PreviousProbe(GActiveExecutionControlProbe)
		{
			GActiveExecutionControlProbe = &InProbe;
			GActiveExecutionControlProbe->Reset();
		}

		~FScopedExecutionControlProbeBinding()
		{
			GActiveExecutionControlProbe = PreviousProbe;
		}

		FExecutionControlProbe* PreviousProbe = nullptr;
	};

	int32 InspectExecutionControl(int32 Value)
	{
		FExecutionControlProbe* Probe = GActiveExecutionControlProbe;
		asCContext* Context = static_cast<asCContext*>(asGetActiveContext());
		if (Probe == nullptr || Context == nullptr)
		{
			return -9100;
		}

		++Probe->InvocationCount;
		Probe->StateBeforeAbort = Context->GetState();
		Probe->AbortResultWhileActive = Context->Abort();
		Probe->StateAfterAbort = Context->GetState();
		Probe->SuspendResultWhileActive = Context->Suspend();
		Probe->StateAfterSuspend = Context->GetState();
		return Value + 5;
	}
}

using namespace AngelscriptTest_Internals_AngelscriptContextExecutionControlTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptContextExecutionControlTest,
	"Angelscript.TestModule.Internals.Context.ExecutionControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptContextExecutionControlTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
	if (!TestNotNull(TEXT("Context.ExecutionControl should expose the backing script engine"), ScriptEngine))
	{
		return false;
	}

	const ASAutoCaller::FunctionCaller Caller = ASAutoCaller::MakeFunctionCaller(InspectExecutionControl);
	const int RegisterResult = ScriptEngine->RegisterGlobalFunction(
		"int InspectExecutionControl(int Value)",
		asFUNCTION(InspectExecutionControl),
		asCALL_CDECL,
		*(asFunctionCaller*)&Caller);
	if (!TestTrue(TEXT("Context.ExecutionControl should register the native execution-control helper"), RegisterResult >= 0))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASContextExecutionControl",
		TEXT(R"AS(
int Entry(int Seed)
{
	int Baseline = Seed + 2;
	int NativeResult = InspectExecutionControl(Baseline);
	return NativeResult * 2;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry(int)"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	asIScriptContext* RawContext = Engine.CreateContext();
	if (!TestNotNull(TEXT("Context.ExecutionControl should create a script context"), RawContext))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		RawContext->Release();
	};

	asCContext* Context = static_cast<asCContext*>(RawContext);
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should start in the uninitialized state"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reject Abort on an uninitialized context"),
		Context->Abort(),
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should remain uninitialized after rejecting Abort"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reject Suspend on an uninitialized context"),
		Context->Suspend(),
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should remain uninitialized after rejecting Suspend"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));

	const int32 PrepareResult = Context->Prepare(EntryFunction);
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should prepare Entry(int) successfully"),
		PrepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should enter the prepared state after Prepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_PREPARED));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reject Abort while only prepared"),
		Context->Abort(),
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should remain prepared after rejecting Abort"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_PREPARED));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reject Suspend while only prepared"),
		Context->Suspend(),
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should remain prepared after rejecting Suspend"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_PREPARED));

	const int32 SetArgResult = Context->SetArgDWord(0, 35);
	if (!TestEqual(
			TEXT("Context.ExecutionControl should accept the Entry(int) argument"),
			SetArgResult,
			static_cast<int32>(asSUCCESS)))
	{
		return false;
	}

	FExecutionControlProbe Probe;
	{
		FScopedExecutionControlProbeBinding ProbeBinding(Probe);
		const int32 ExecuteResult = Context->Execute();
		bPassed &= TestEqual(
			TEXT("Context.ExecutionControl should finish execution successfully"),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED));
	}

	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should finish in the finished state after Execute"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should preserve the native helper return value"),
		static_cast<int32>(Context->GetReturnDWord()),
		84);
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should invoke the native helper exactly once"),
		Probe.InvocationCount,
		1);
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reach the active state before the native Abort call"),
		static_cast<int32>(Probe.StateBeforeAbort),
		static_cast<int32>(asEXECUTION_ACTIVE));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reject Abort while script execution is active"),
		Probe.AbortResultWhileActive,
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should remain active after rejecting Abort during execution"),
		static_cast<int32>(Probe.StateAfterAbort),
		static_cast<int32>(asEXECUTION_ACTIVE));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reject Suspend while script execution is active"),
		Probe.SuspendResultWhileActive,
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should remain active after rejecting Suspend during execution"),
		static_cast<int32>(Probe.StateAfterSuspend),
		static_cast<int32>(asEXECUTION_ACTIVE));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reject Abort after execution has already finished"),
		Context->Abort(),
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should remain finished after rejecting Abort post-execution"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should reject Suspend after execution has already finished"),
		Context->Suspend(),
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should remain finished after rejecting Suspend post-execution"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_FINISHED));

	const int32 UnprepareResult = Context->Unprepare();
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should unprepare successfully after the finished run"),
		UnprepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should return to the uninitialized state after Unprepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should still reject Abort after Unprepare"),
		Context->Abort(),
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should stay uninitialized after rejecting Abort post-Unprepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should still reject Suspend after Unprepare"),
		Context->Suspend(),
		static_cast<int32>(asERROR));
	bPassed &= TestEqual(
		TEXT("Context.ExecutionControl should stay uninitialized after rejecting Suspend post-Unprepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));

	ASTEST_END_FULL
	return bPassed;
}

#endif
