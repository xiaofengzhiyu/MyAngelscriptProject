#include "Angelscript/AngelscriptTestSupport.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Internals_AngelscriptContextPushPopStateTests_Private
{
	struct FContextPushPopProbe
	{
		void Reset()
		{
			InvocationCount = 0;
			StateBeforePush = asEXECUTION_UNINITIALIZED;
			NestCountBeforePush = 0;
			bWasNestedBeforePush = false;
			PushResult = asERROR;
			StateAfterPush = asEXECUTION_UNINITIALIZED;
			NestCountAfterPush = 0;
			bNestedAfterPush = false;
			PrepareResult = asERROR;
			SetArgResult = asERROR;
			ExecuteResult = asERROR;
			StateAfterNestedExecute = asEXECUTION_UNINITIALIZED;
			NestedReturnValue = INDEX_NONE;
			PopResult = asERROR;
			StateAfterPop = asEXECUTION_UNINITIALIZED;
			NestCountAfterPop = 0;
			bNestedAfterPop = false;
		}

		int32 InvocationCount = 0;
		asEContextState StateBeforePush = asEXECUTION_UNINITIALIZED;
		asUINT NestCountBeforePush = 0;
		bool bWasNestedBeforePush = false;
		int32 PushResult = asERROR;
		asEContextState StateAfterPush = asEXECUTION_UNINITIALIZED;
		asUINT NestCountAfterPush = 0;
		bool bNestedAfterPush = false;
		int32 PrepareResult = asERROR;
		int32 SetArgResult = asERROR;
		int32 ExecuteResult = asERROR;
		asEContextState StateAfterNestedExecute = asEXECUTION_UNINITIALIZED;
		int32 NestedReturnValue = INDEX_NONE;
		int32 PopResult = asERROR;
		asEContextState StateAfterPop = asEXECUTION_UNINITIALIZED;
		asUINT NestCountAfterPop = 0;
		bool bNestedAfterPop = false;
	};

	thread_local FContextPushPopProbe* GActivePushPopProbe = nullptr;
	thread_local asIScriptFunction* GActiveNestedFunction = nullptr;

	struct FScopedPushPopProbeBinding
	{
		FScopedPushPopProbeBinding(FContextPushPopProbe& InProbe, asIScriptFunction& InNestedFunction)
			: PreviousProbe(GActivePushPopProbe)
			, PreviousNestedFunction(GActiveNestedFunction)
		{
			GActivePushPopProbe = &InProbe;
			GActiveNestedFunction = &InNestedFunction;
			GActivePushPopProbe->Reset();
		}

		~FScopedPushPopProbeBinding()
		{
			GActivePushPopProbe = PreviousProbe;
			GActiveNestedFunction = PreviousNestedFunction;
		}

		FContextPushPopProbe* PreviousProbe = nullptr;
		asIScriptFunction* PreviousNestedFunction = nullptr;
	};

	int32 CallNestedThroughPushState(int32 Value)
	{
		FContextPushPopProbe* Probe = GActivePushPopProbe;
		asIScriptFunction* NestedFunction = GActiveNestedFunction;
		asCContext* Context = static_cast<asCContext*>(asGetActiveContext());
		if (Probe == nullptr || NestedFunction == nullptr || Context == nullptr)
		{
			return -9000;
		}

		++Probe->InvocationCount;
		Probe->StateBeforePush = Context->GetState();
		Probe->bWasNestedBeforePush = Context->IsNested(&Probe->NestCountBeforePush);

		Probe->PushResult = Context->PushState();
		Probe->StateAfterPush = Context->GetState();
		Probe->bNestedAfterPush = Context->IsNested(&Probe->NestCountAfterPush);
		if (Probe->PushResult != asSUCCESS)
		{
			return -9010;
		}

		Probe->PrepareResult = Context->Prepare(NestedFunction);
		if (Probe->PrepareResult != asSUCCESS)
		{
			Probe->PopResult = Context->PopState();
			Probe->StateAfterPop = Context->GetState();
			Probe->bNestedAfterPop = Context->IsNested(&Probe->NestCountAfterPop);
			return -9020;
		}

		Probe->SetArgResult = Context->SetArgDWord(0, Value);
		if (Probe->SetArgResult != asSUCCESS)
		{
			Probe->PopResult = Context->PopState();
			Probe->StateAfterPop = Context->GetState();
			Probe->bNestedAfterPop = Context->IsNested(&Probe->NestCountAfterPop);
			return -9030;
		}

		Probe->ExecuteResult = Context->Execute();
		Probe->StateAfterNestedExecute = Context->GetState();
		if (Probe->ExecuteResult == asEXECUTION_FINISHED)
		{
			Probe->NestedReturnValue = static_cast<int32>(Context->GetReturnDWord());
		}

		Probe->PopResult = Context->PopState();
		Probe->StateAfterPop = Context->GetState();
		Probe->bNestedAfterPop = Context->IsNested(&Probe->NestCountAfterPop);

		if (Probe->ExecuteResult != asEXECUTION_FINISHED)
		{
			return -9040;
		}

		if (Probe->PopResult != asSUCCESS)
		{
			return -9050;
		}

		return Probe->NestedReturnValue;
	}
}

using namespace AngelscriptTest_Internals_AngelscriptContextPushPopStateTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptContextPushPopStateTest,
	"Angelscript.TestModule.Internals.Context.PushPopState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptContextPushPopStateTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
	if (!TestNotNull(TEXT("Context.PushPopState should expose the backing script engine"), ScriptEngine))
	{
		return false;
	}

	const ASAutoCaller::FunctionCaller Caller = ASAutoCaller::MakeFunctionCaller(CallNestedThroughPushState);
	const int RegisterResult = ScriptEngine->RegisterGlobalFunction(
		"int CallNestedThroughPushState(int Value)",
		asFUNCTION(CallNestedThroughPushState),
		asCALL_CDECL,
		*(asFunctionCaller*)&Caller);
	if (!TestTrue(TEXT("Context.PushPopState should register the native nested-call helper"), RegisterResult >= 0))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASContextPushPopState",
		TEXT(R"AS(
int NestedWorker(int Value)
{
	return Value + 7;
}

int Entry(int Seed)
{
	int NestedResult = CallNestedThroughPushState(Seed);
	return NestedResult + 1000;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry(int)"));
	asIScriptFunction* NestedFunction = GetFunctionByDecl(*this, *Module, TEXT("int NestedWorker(int)"));
	if (EntryFunction == nullptr || NestedFunction == nullptr)
	{
		return false;
	}

	asIScriptContext* RawContext = Engine.CreateContext();
	if (!TestNotNull(TEXT("Context.PushPopState should create a script context"), RawContext))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		RawContext->Release();
	};

	asCContext* Context = static_cast<asCContext*>(RawContext);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should start in the uninitialized state"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should reject PushState on an uninitialized context"),
		Context->PushState(),
		static_cast<int32>(asERROR));

	const int32 PrepareResult = Context->Prepare(EntryFunction);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should prepare Entry(int) successfully"),
		PrepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should enter the prepared state after Prepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_PREPARED));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should reject PushState while only prepared"),
		Context->PushState(),
		static_cast<int32>(asERROR));

	const int32 SetArgResult = PrepareResult == asSUCCESS ? Context->SetArgDWord(0, 35) : PrepareResult;
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should accept the outer Entry(int) argument"),
		SetArgResult,
		static_cast<int32>(asSUCCESS));
	if (PrepareResult != asSUCCESS || SetArgResult != asSUCCESS)
	{
		return false;
	}

	FContextPushPopProbe Probe;
	{
		FScopedPushPopProbeBinding ProbeBinding(Probe, *NestedFunction);
		const int32 ExecuteResult = Context->Execute();
		bPassed &= TestEqual(
			TEXT("Context.PushPopState should finish the outer script execution successfully"),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED));
	}

	bPassed &= TestEqual(
		TEXT("Context.PushPopState should finish the outer context in the finished state"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should preserve the nested-call return value after outer execution resumes"),
		static_cast<int32>(Context->GetReturnDWord()),
		1042);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should invoke the native nested-call helper exactly once"),
		Probe.InvocationCount,
		1);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should reach the active state before pushing"),
		static_cast<int32>(Probe.StateBeforePush),
		static_cast<int32>(asEXECUTION_ACTIVE));
	bPassed &= TestFalse(
		TEXT("Context.PushPopState should not already be nested before PushState"),
		Probe.bWasNestedBeforePush);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should report zero nested frames before PushState"),
		static_cast<int32>(Probe.NestCountBeforePush),
		0);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should push the active state successfully"),
		Probe.PushResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should move to the uninitialized state immediately after PushState"),
		static_cast<int32>(Probe.StateAfterPush),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));
	bPassed &= TestTrue(
		TEXT("Context.PushPopState should report a nested state after PushState"),
		Probe.bNestedAfterPush);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should report a single nested frame after PushState"),
		static_cast<int32>(Probe.NestCountAfterPush),
		1);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should prepare the nested function successfully after PushState"),
		Probe.PrepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should bind the nested function argument successfully"),
		Probe.SetArgResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should finish the nested function execution successfully"),
		Probe.ExecuteResult,
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should leave the nested execution in the finished state before PopState"),
		static_cast<int32>(Probe.StateAfterNestedExecute),
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should capture the nested function return value before PopState restores the outer call"),
		Probe.NestedReturnValue,
		42);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should restore the outer call with PopState successfully"),
		Probe.PopResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should return to the active state immediately after PopState restores the outer execution"),
		static_cast<int32>(Probe.StateAfterPop),
		static_cast<int32>(asEXECUTION_ACTIVE));
	bPassed &= TestFalse(
		TEXT("Context.PushPopState should clear the nested flag after PopState"),
		Probe.bNestedAfterPop);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should report zero nested frames after PopState"),
		static_cast<int32>(Probe.NestCountAfterPop),
		0);
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should reject PopState once the outer execution has already finished"),
		Context->PopState(),
		static_cast<int32>(asERROR));

	const int32 UnprepareResult = Context->Unprepare();
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should unprepare successfully after the finished outer run"),
		UnprepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should return to the uninitialized state after Unprepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));
	bPassed &= TestEqual(
		TEXT("Context.PushPopState should reject PopState on a non-nested uninitialized context"),
		Context->PopState(),
		static_cast<int32>(asERROR));

	ASTEST_END_FULL
	return bPassed;
}

#endif
