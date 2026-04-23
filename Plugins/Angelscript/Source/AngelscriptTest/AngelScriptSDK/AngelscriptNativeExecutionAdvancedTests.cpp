#include "AngelscriptNativeTestSupport.h"

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptNativeTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNativeExecuteFloatReturnTest,
	"Angelscript.TestModule.AngelScriptSDK.Execute.FloatReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNativeExecuteNegativeValueTest,
	"Angelscript.TestModule.AngelScriptSDK.Execute.NegativeValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNativeExecuteMultipleReturnPathsTest,
	"Angelscript.TestModule.AngelScriptSDK.Execute.MultipleReturnPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptNativeExecuteFloatReturnTest::RunTest(const FString& Parameters)
{
	FNativeMessageCollector Messages;
	asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages);
	if (!TestNotNull(TEXT("Native float-return execution test should create a standalone engine"), ScriptEngine))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		DestroyNativeEngine(ScriptEngine);
	};

	const bool bFloatUsesFloat64 = ScriptEngine->GetEngineProperty(asEP_FLOAT_IS_FLOAT64) != 0;
	const char* Source = bFloatUsesFloat64
		? "double Test() { return 42.5; }"
		: "float Test() { return 42.5f; }";
	const char* Declaration = bFloatUsesFloat64
		? "double Test()"
		: "float Test()";

	asIScriptModule* Module = BuildNativeModule(ScriptEngine, "NativeExecuteFloatReturn", Source);
	if (!TestNotNull(TEXT("Native float-return execution test should compile the module"), Module))
	{
		AddInfo(CollectMessages(Messages));
		return false;
	}

	asIScriptFunction* Function = GetNativeFunctionByDecl(Module, Declaration);
	if (!TestNotNull(TEXT("Native float-return execution test should resolve the entry function"), Function))
	{
		return false;
	}

	asIScriptContext* Context = ScriptEngine->CreateContext();
	if (!TestNotNull(TEXT("Native float-return execution test should create a context"), Context))
	{
		return false;
	}

	const int ExecuteResult = PrepareAndExecute(Context, Function);
	if (!TestEqual(TEXT("Native float-return execution test should finish successfully"), ExecuteResult, static_cast<int32>(asEXECUTION_FINISHED)))
	{
		Context->Release();
		return false;
	}

	if (bFloatUsesFloat64)
	{
		const double ReturnValue = Context->GetReturnDouble();
		TestTrue(TEXT("Native float-return execution test should preserve double results"), FMath::IsNearlyEqual(ReturnValue, 42.5, 0.0001));
	}
	else
	{
		TestEqual(TEXT("Native float-return execution test should preserve float results"), Context->GetReturnFloat(), 42.5f, 0.0001f);
	}

	Context->Release();
	return true;
}

bool FAngelscriptNativeExecuteNegativeValueTest::RunTest(const FString& Parameters)
{
	FNativeMessageCollector Messages;
	asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages);
	if (!TestNotNull(TEXT("Native negative-value execution test should create a standalone engine"), ScriptEngine))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		DestroyNativeEngine(ScriptEngine);
	};

	asIScriptModule* Module = BuildNativeModule(ScriptEngine, "NativeExecuteNegativeValue", "int Test(int Start, int Delta) { return Start + Delta; }");
	if (!TestNotNull(TEXT("Native negative-value execution test should compile the module"), Module))
	{
		AddInfo(CollectMessages(Messages));
		return false;
	}

	asIScriptFunction* Function = GetNativeFunctionByDecl(Module, "int Test(int, int)");
	if (!TestNotNull(TEXT("Native negative-value execution test should resolve the entry function"), Function))
	{
		return false;
	}

	asIScriptContext* Context = ScriptEngine->CreateContext();
	if (!TestNotNull(TEXT("Native negative-value execution test should create a context"), Context))
	{
		return false;
	}

	const int PrepareResult = Context->Prepare(Function);
	if (!TestEqual(TEXT("Native negative-value execution test should prepare the function"), PrepareResult, static_cast<int32>(asSUCCESS)))
	{
		Context->Release();
		return false;
	}

	Context->SetArgDWord(0, static_cast<asDWORD>(10));
	Context->SetArgDWord(1, static_cast<asDWORD>(-52));
	const int ExecuteResult = Context->Execute();
	TestEqual(TEXT("Native negative-value execution test should finish successfully"), ExecuteResult, static_cast<int32>(asEXECUTION_FINISHED));
	TestEqual(TEXT("Native negative-value execution test should preserve signed integer arguments"), static_cast<int32>(Context->GetReturnDWord()), -42);
	Context->Release();
	return ExecuteResult == asEXECUTION_FINISHED;
}

bool FAngelscriptNativeExecuteMultipleReturnPathsTest::RunTest(const FString& Parameters)
{
	FNativeMessageCollector Messages;
	asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages);
	if (!TestNotNull(TEXT("Native multiple-return-paths execution test should create a standalone engine"), ScriptEngine))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		DestroyNativeEngine(ScriptEngine);
	};

	asIScriptModule* Module = BuildNativeModule(ScriptEngine, "NativeExecuteMultipleReturnPaths", "int Test(int Value) { if (Value > 0) { return 40; } return 2; }");
	if (!TestNotNull(TEXT("Native multiple-return-paths execution test should compile the module"), Module))
	{
		AddInfo(CollectMessages(Messages));
		return false;
	}

	asIScriptFunction* Function = GetNativeFunctionByDecl(Module, "int Test(int)");
	if (!TestNotNull(TEXT("Native multiple-return-paths execution test should resolve the entry function"), Function))
	{
		return false;
	}

	asIScriptContext* PositiveContext = ScriptEngine->CreateContext();
	if (!TestNotNull(TEXT("Native multiple-return-paths execution test should create the positive-path context"), PositiveContext))
	{
		return false;
	}

	const int PositivePrepareResult = PositiveContext->Prepare(Function);
	if (!TestEqual(TEXT("Native multiple-return-paths execution test should prepare the positive path"), PositivePrepareResult, static_cast<int32>(asSUCCESS)))
	{
		PositiveContext->Release();
		return false;
	}

	PositiveContext->SetArgDWord(0, 1);
	const int PositiveExecuteResult = PositiveContext->Execute();
	if (!TestEqual(TEXT("Native multiple-return-paths execution test should finish the positive path"), PositiveExecuteResult, static_cast<int32>(asEXECUTION_FINISHED)))
	{
		PositiveContext->Release();
		return false;
	}

	const int32 PositiveResult = static_cast<int32>(PositiveContext->GetReturnDWord());
	PositiveContext->Release();

	asIScriptContext* FallbackContext = ScriptEngine->CreateContext();
	if (!TestNotNull(TEXT("Native multiple-return-paths execution test should create the fallback-path context"), FallbackContext))
	{
		return false;
	}

	const int FallbackPrepareResult = FallbackContext->Prepare(Function);
	if (!TestEqual(TEXT("Native multiple-return-paths execution test should prepare the fallback path"), FallbackPrepareResult, static_cast<int32>(asSUCCESS)))
	{
		FallbackContext->Release();
		return false;
	}

	FallbackContext->SetArgDWord(0, 0);
	const int FallbackExecuteResult = FallbackContext->Execute();
	if (!TestEqual(TEXT("Native multiple-return-paths execution test should finish the fallback path"), FallbackExecuteResult, static_cast<int32>(asEXECUTION_FINISHED)))
	{
		FallbackContext->Release();
		return false;
	}

	const int32 FallbackResult = static_cast<int32>(FallbackContext->GetReturnDWord());
	FallbackContext->Release();

	TestEqual(TEXT("Native multiple-return-paths execution test should take the positive branch when Value > 0"), PositiveResult, 40);
	TestEqual(TEXT("Native multiple-return-paths execution test should take the fallback branch when Value <= 0"), FallbackResult, 2);
	return true;
}

#endif
