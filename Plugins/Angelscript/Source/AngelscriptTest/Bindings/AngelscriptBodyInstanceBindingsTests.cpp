#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "Templates/Function.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBodyInstanceBindingsTest,
	"Angelscript.TestModule.Bindings.BodyInstanceCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_BodyInstanceBindingsTests_Private
{
	static constexpr ANSICHAR BodyInstanceCompatModuleName[] = "ASBodyInstanceCompat";

	bool SetArgAddressChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Address,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind address argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgAddress(ArgumentIndex, Address),
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

using namespace AngelscriptTest_Bindings_BodyInstanceBindingsTests_Private;

bool FAngelscriptBodyInstanceBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(BodyInstanceCompatModuleName));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		BodyInstanceCompatModuleName,
		TEXT(R"(
int VerifyBodyInstance(FBodyInstance& BodyInstance, UBodySetup ExpectedBodySetup, uint ExpectedUseCCD)
{
	if (BodyInstance.GetBodySetup() != ExpectedBodySetup)
		return 10;

	BodyInstance.SetUseCCD(ExpectedUseCCD != 0);

	if (BodyInstance.GetBodySetup() != ExpectedBodySetup)
		return 20;

	return 1;
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	UBodySetup* BodySetup = NewObject<UBodySetup>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("BodyInstanceCompat should create a transient UBodySetup fixture"), BodySetup))
	{
		return false;
	}

	FBodyInstance BodyInstance;
	BodyInstance.BodySetup = BodySetup;
	BodyInstance.bUseCCD = false;

	int32 EnableResult = 0;
	if (!ExecuteIntFunction(
		*this,
		Engine,
		*Module,
		TEXT("int VerifyBodyInstance(FBodyInstance&, UBodySetup, uint)"),
		[this, &BodyInstance, BodySetup](asIScriptContext& Context)
		{
			return SetArgAddressChecked(*this, Context, 0, &BodyInstance, TEXT("VerifyBodyInstance.EnableCCD"))
				&& SetArgObjectChecked(*this, Context, 1, BodySetup, TEXT("VerifyBodyInstance.EnableCCD"))
				&& SetArgDWordChecked(*this, Context, 2, 1u, TEXT("VerifyBodyInstance.EnableCCD"));
		},
		TEXT("VerifyBodyInstance.EnableCCD"),
		EnableResult))
	{
		return false;
	}

	TestEqual(TEXT("BodyInstanceCompat should keep GetBodySetup parity while enabling CCD"), EnableResult, 1);
	TestTrue(TEXT("BodyInstanceCompat should write bUseCCD=true through SetUseCCD(true)"), BodyInstance.bUseCCD);

	int32 DisableResult = 0;
	if (!ExecuteIntFunction(
		*this,
		Engine,
		*Module,
		TEXT("int VerifyBodyInstance(FBodyInstance&, UBodySetup, uint)"),
		[this, &BodyInstance, BodySetup](asIScriptContext& Context)
		{
			return SetArgAddressChecked(*this, Context, 0, &BodyInstance, TEXT("VerifyBodyInstance.DisableCCD"))
				&& SetArgObjectChecked(*this, Context, 1, BodySetup, TEXT("VerifyBodyInstance.DisableCCD"))
				&& SetArgDWordChecked(*this, Context, 2, 0u, TEXT("VerifyBodyInstance.DisableCCD"));
		},
		TEXT("VerifyBodyInstance.DisableCCD"),
		DisableResult))
	{
		return false;
	}

	TestEqual(TEXT("BodyInstanceCompat should keep GetBodySetup parity while disabling CCD"), DisableResult, 1);
	TestFalse(TEXT("BodyInstanceCompat should write bUseCCD=false through SetUseCCD(false)"), BodyInstance.bUseCCD);
	TestEqual(TEXT("BodyInstanceCompat should leave the native BodySetup pointer unchanged"), BodyInstance.GetBodySetup(), BodySetup);

	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
