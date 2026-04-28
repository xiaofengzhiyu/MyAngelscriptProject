#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "CoreGlobals.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"
#include "UObject/Class.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptCoreGlobalsBindingsTest,
	"Angelscript.TestModule.Bindings.CoreGlobalsCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_CoreGlobalsBindingsTests_Private
{
	static constexpr ANSICHAR CoreGlobalsCompatModuleName[] = "ASCoreGlobalsCompat";

	bool SetArgDWordChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		const uint32 Value,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind dword argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgDWord(ArgumentIndex, Value),
			static_cast<int32>(asSUCCESS));
	}

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	bool ExecuteIntFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		int32& OutResult)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should create an execution context"), ContextLabel), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		if (!BindArguments(*Context))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}
}

using namespace AngelscriptTest_Bindings_CoreGlobalsBindingsTests_Private;

bool FAngelscriptCoreGlobalsBindingsTest::RunTest(const FString& Parameters)
{
	const uint32 bExpectedRunningCommandlet = IsRunningCommandlet() ? 1u : 0u;
	const uint32 bExpectedRunningCookCommandlet = IsRunningCookCommandlet() ? 1u : 0u;
	const uint32 bExpectedRunningDLCCookCommandlet = IsRunningDLCCookCommandlet() ? 1u : 0u;
	UClass* ExpectedRunningCommandletClass = GetRunningCommandletClass();

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(CoreGlobalsCompatModuleName));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		CoreGlobalsCompatModuleName,
		TEXT(R"(
int VerifyCoreGlobals(
	uint ExpectedRunningCommandlet,
	uint ExpectedRunningCookCommandlet,
	uint ExpectedRunningDLCCookCommandlet,
	UClass ExpectedRunningCommandletClass)
{
	if ((IsRunningCommandlet() ? uint(1) : uint(0)) != ExpectedRunningCommandlet)
		return 10;

	if ((IsRunningCookCommandlet() ? uint(1) : uint(0)) != ExpectedRunningCookCommandlet)
		return 20;

	if ((IsRunningDLCCookCommandlet() ? uint(1) : uint(0)) != ExpectedRunningDLCCookCommandlet)
		return 30;

	UClass RuntimeCommandletClass = GetRunningCommandletClass();
	if ((RuntimeCommandletClass == null) != (ExpectedRunningCommandletClass == null))
		return 40;

	if (RuntimeCommandletClass != ExpectedRunningCommandletClass)
		return 50;

	return 1;
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(
		*this,
		Engine,
		*Module,
		TEXT("int VerifyCoreGlobals(uint ExpectedRunningCommandlet, uint ExpectedRunningCookCommandlet, uint ExpectedRunningDLCCookCommandlet, UClass ExpectedRunningCommandletClass)"),
		[&](asIScriptContext& Context)
		{
			return SetArgDWordChecked(*this, Context, 0, bExpectedRunningCommandlet, TEXT("VerifyCoreGlobals"))
				&& SetArgDWordChecked(*this, Context, 1, bExpectedRunningCookCommandlet, TEXT("VerifyCoreGlobals"))
				&& SetArgDWordChecked(*this, Context, 2, bExpectedRunningDLCCookCommandlet, TEXT("VerifyCoreGlobals"))
				&& SetArgObjectChecked(*this, Context, 3, ExpectedRunningCommandletClass, TEXT("VerifyCoreGlobals"));
		},
		TEXT("VerifyCoreGlobals"),
		Result))
	{
		return false;
	}

	TestEqual(TEXT("Core global bindings should match the same-run native process state"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return true;
}

#endif
