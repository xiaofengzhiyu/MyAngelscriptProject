#include "../Shared/AngelscriptNativeScriptTestObject.h"
#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptDelegateSignatureBindingsTests_Private
{
	static constexpr ANSICHAR DelegateSignatureCompatModuleName[] = "ASScriptDelegateSignatureCompat";
	static constexpr ANSICHAR DelegateSignatureMismatchModuleName[] = "ASScriptDelegateSignatureMismatchCompat";

	bool ExecuteFunctionExpectingException(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		const TCHAR* ContextLabel,
		FString& OutExceptionString,
		int32& OutExceptionLine)
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

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should fail with a script exception"), ContextLabel),
				ExecuteResult,
				static_cast<int32>(asEXECUTION_EXCEPTION)))
		{
			return false;
		}

		OutExceptionString = Context->GetExceptionString() != nullptr ? UTF8_TO_TCHAR(Context->GetExceptionString()) : TEXT("");
		OutExceptionLine = Context->GetExceptionLineNumber();
		return Test.TestFalse(
				*FString::Printf(TEXT("%s should report a non-empty exception string"), ContextLabel),
				OutExceptionString.IsEmpty())
			&& Test.TestTrue(
				*FString::Printf(TEXT("%s should report a positive exception line"), ContextLabel),
				OutExceptionLine > 0);
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptDelegateSignatureBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDelegateSignatureHelperBindingsTest,
	"Angelscript.TestModule.Bindings.ScriptDelegateSignatureCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDelegateSignatureMismatchBindingsTest,
	"Angelscript.TestModule.Bindings.ScriptDelegateSignatureMismatchCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDelegateSignatureHelperBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASScriptDelegateSignatureCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		DelegateSignatureCompatModuleName,
		TEXT(R"(
delegate int FNativeCallback(int Value, const FString& Label);
event void FNativeEvent(int Value, const FString& Label);

int Entry()
{
	UObject TestObject = FindClass("UAngelscriptNativeScriptTestObject").GetDefaultObject();
	if (TestObject == null)
		return 10;

	FNativeCallback Single;
	UDelegateFunction SingleSignature = __DelegateSignature(Single);
	if (SingleSignature == null)
		return 20;
	if (!(SingleSignature.GetName() == "FNativeCallback__DelegateSignature"))
		return 25;

	_FScriptDelegate Constructed(TestObject, n"NativeIntStringEvent", SingleSignature);
	Single._Inner = Constructed;
	if (!Single.IsBound())
		return 30;
	if (Single.Execute(7, "Alpha") != 12)
		return 40;

	Single.Clear();
	Single._Inner.BindUFunction(TestObject, n"NativeIntStringEvent", SingleSignature);
	if (!Single.IsBound())
		return 50;
	if (Single.GetUObject() != TestObject)
		return 55;
	if (!Single.GetFunctionName().IsEqual(n"NativeIntStringEvent"))
		return 57;
	if (Single.Execute(8, "Beta") != 12)
		return 60;

	FNativeEvent Multi;
	UDelegateFunction MultiSignature = __DelegateSignature(Multi);
	if (MultiSignature == null)
		return 70;
	if (!(MultiSignature.GetName() == "FNativeEvent__DelegateSignature"))
		return 75;

	Multi._Inner.AddUFunction(TestObject, n"SetIntStringFromDelegate", MultiSignature);
	if (!Multi.IsBound())
		return 80;

	Multi.Broadcast(11, "Beta");
	Multi.Unbind(TestObject, n"SetIntStringFromDelegate");
	if (Multi.IsBound())
		return 90;

	return 1;
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	UAngelscriptNativeScriptTestObject* NativeTestObject = GetMutableDefault<UAngelscriptNativeScriptTestObject>();
	if (!TestNotNull(TEXT("Script delegate signature compat test should resolve the native test object default instance"), NativeTestObject))
	{
		return false;
	}

	NativeTestObject->NameCounts.Reset();
	ON_SCOPE_EXIT
	{
		NativeTestObject->NameCounts.Reset();
	};

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	const int32* BetaCount = NativeTestObject->NameCounts.Find(TEXT("Beta"));
	bPassed = TestEqual(TEXT("Script delegate signature compat operations should behave as expected"), Result, 1)
		&& TestNotNull(TEXT("Signature-based multicast binding should forward the expected label key"), BetaCount)
		&& BetaCount != nullptr
		&& TestEqual(TEXT("Signature-based multicast binding should forward the expected value"), *BetaCount, 11);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptDelegateSignatureMismatchBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	AddExpectedError(TEXT("Specified function is not compatible with delegate function."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("ASScriptDelegateSignatureMismatchCompat"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("void TriggerUnicastSignatureMismatch()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerMulticastSignatureMismatch()"), EAutomationExpectedErrorFlags::Contains, 0, false);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASScriptDelegateSignatureMismatchCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		DelegateSignatureMismatchModuleName,
		TEXT(R"(
delegate int FNativeCallback(int Value, const FString& Label);
event void FNativeEvent(int Value, const FString& Label);
delegate void FMismatchedCallback(float Value);

void TriggerUnicastSignatureMismatch()
{
	UObject TestObject = FindClass("UAngelscriptNativeScriptTestObject").GetDefaultObject();
	FNativeCallback Single;
	FMismatchedCallback Wrong;
	Single._Inner.BindUFunction(TestObject, n"NativeIntStringEvent", __DelegateSignature(Wrong));
}

void TriggerMulticastSignatureMismatch()
{
	UObject TestObject = FindClass("UAngelscriptNativeScriptTestObject").GetDefaultObject();
	FNativeEvent Multi;
	FMismatchedCallback Wrong;
	Multi._Inner.AddUFunction(TestObject, n"SetIntStringFromDelegate", __DelegateSignature(Wrong));
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	FString UnicastException;
	int32 UnicastLine = 0;
	const bool bUnicastMismatch = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerUnicastSignatureMismatch()"),
			TEXT("Delegate signature mismatch unicast overload"),
			UnicastException,
			UnicastLine)
		&& TestTrue(TEXT("Delegate signature mismatch unicast overload should mention the compatibility contract"), UnicastException.Contains(TEXT("Specified function is not compatible with delegate function.")))
		&& TestTrue(TEXT("Delegate signature mismatch unicast overload should report a positive exception line"), UnicastLine > 0);

	FString MulticastException;
	int32 MulticastLine = 0;
	const bool bMulticastMismatch = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerMulticastSignatureMismatch()"),
			TEXT("Delegate signature mismatch multicast overload"),
			MulticastException,
			MulticastLine)
		&& TestTrue(TEXT("Delegate signature mismatch multicast overload should mention the compatibility contract"), MulticastException.Contains(TEXT("Specified function is not compatible with delegate function.")))
		&& TestTrue(TEXT("Delegate signature mismatch multicast overload should report a positive exception line"), MulticastLine > 0);

	bPassed = bUnicastMismatch && bMulticastMismatch;
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
