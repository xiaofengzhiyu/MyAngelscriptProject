#include "AngelscriptNativeTestSupport.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptNativeTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNativeCompileSimpleFunctionTest,
	"Angelscript.TestModule.AngelScriptSDK.Compile.SimpleFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNativeCompileMultipleFunctionsTest,
	"Angelscript.TestModule.AngelScriptSDK.Compile.MultipleFunctions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNativeCompileGlobalVariablesTest,
	"Angelscript.TestModule.AngelScriptSDK.Compile.GlobalVariables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNativeCompileSyntaxErrorTest,
	"Angelscript.TestModule.AngelScriptSDK.Compile.SyntaxError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNativeCompileErrorMessageTest,
	"Angelscript.TestModule.AngelScriptSDK.Compile.ErrorMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptNativeCompileSimpleFunctionTest::RunTest(const FString& Parameters)
{
	FNativeMessageCollector Messages;
	asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages);
	if (!TestNotNull(TEXT("Native compile simple-function test should create a standalone engine"), ScriptEngine))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		DestroyNativeEngine(ScriptEngine);
	};

	asIScriptModule* Module = BuildNativeModule(ScriptEngine, "NativeCompileSimpleFunction", "int Test() { return 42; }");
	if (!TestNotNull(TEXT("Native compile simple-function test should compile a trivial function"), Module))
	{
		AddInfo(CollectMessages(Messages));
		return false;
	}

	return TestNotNull(TEXT("Native compile simple-function test should expose the compiled function"), GetNativeFunctionByDecl(Module, "int Test()"));
}

bool FAngelscriptNativeCompileMultipleFunctionsTest::RunTest(const FString& Parameters)
{
	FNativeMessageCollector Messages;
	asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages);
	if (!TestNotNull(TEXT("Native compile multiple-functions test should create a standalone engine"), ScriptEngine))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		DestroyNativeEngine(ScriptEngine);
	};

	asIScriptModule* Module = BuildNativeModule(ScriptEngine, "NativeCompileMultipleFunctions", "void A() {} void B() {} int C() { return 42; }");
	if (!TestNotNull(TEXT("Native compile multiple-functions test should compile the module"), Module))
	{
		AddInfo(CollectMessages(Messages));
		return false;
	}

	return TestEqual(TEXT("Native compile multiple-functions test should expose every compiled function"), static_cast<int32>(Module->GetFunctionCount()), 3);
}

bool FAngelscriptNativeCompileGlobalVariablesTest::RunTest(const FString& Parameters)
{
	FNativeMessageCollector Messages;
	asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages);
	if (!TestNotNull(TEXT("Native compile global-variables test should create a standalone engine"), ScriptEngine))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		DestroyNativeEngine(ScriptEngine);
	};

	asIScriptModule* Module = BuildNativeModule(ScriptEngine, "NativeCompileGlobalVariables", "const int First = 40; const int Second = 2; int Read() { return First + Second; }");
	if (!TestNotNull(TEXT("Native compile global-variables test should compile the module"), Module))
	{
		AddInfo(CollectMessages(Messages));
		return false;
	}

	return TestEqual(TEXT("Native compile global-variables test should preserve both global declarations"), static_cast<int32>(Module->GetGlobalVarCount()), 2);
}

bool FAngelscriptNativeCompileSyntaxErrorTest::RunTest(const FString& Parameters)
{
	FNativeMessageCollector Messages;
	asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages);
	if (!TestNotNull(TEXT("Native compile syntax-error test should create a standalone engine"), ScriptEngine))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		DestroyNativeEngine(ScriptEngine);
	};

	asIScriptModule* Module = nullptr;
	const int BuildResult = CompileNativeModule(ScriptEngine, "NativeCompileSyntaxError", "int Broken( { return 1; }", Module);
	if (!TestTrue(TEXT("Native compile syntax-error test should fail with a negative build result"), BuildResult < 0))
	{
		AddInfo(CollectMessages(Messages));
		return false;
	}

	return TestNotNull(TEXT("Native compile syntax-error test should still expose a module handle for diagnostics"), Module);
}

bool FAngelscriptNativeCompileErrorMessageTest::RunTest(const FString& Parameters)
{
	FNativeMessageCollector Messages;
	asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages);
	if (!TestNotNull(TEXT("Native compile error-message test should create a standalone engine"), ScriptEngine))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		DestroyNativeEngine(ScriptEngine);
	};

	asIScriptModule* Module = nullptr;
	const int BuildResult = CompileNativeModule(ScriptEngine, "NativeCompileErrorMessage", "int Broken( { return 1; }", Module);
	if (!TestTrue(TEXT("Native compile error-message test should fail with a negative build result"), BuildResult < 0))
	{
		AddInfo(CollectMessages(Messages));
		return false;
	}

	if (!TestTrue(TEXT("Native compile error-message test should capture at least one diagnostic entry"), Messages.Entries.Num() > 0))
	{
		return false;
	}

	const FNativeMessageEntry& FirstMessage = Messages.Entries[0];
	TestTrue(TEXT("Native compile error-message test should capture a non-empty message text"), !FirstMessage.Message.IsEmpty());
	TestTrue(TEXT("Native compile error-message test should capture a valid source row"), FirstMessage.Row > 0);
	TestTrue(TEXT("Native compile error-message test should format the diagnostics for debugging"), !CollectMessages(Messages).IsEmpty());
	return true;
}

#endif
