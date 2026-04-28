#include "../Shared/AngelscriptNativeScriptTestObject.h"
#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptDelegateSignatureGuardBindingsTests_Private
{
	static constexpr ANSICHAR DelegateSignatureGuardModuleName[] = "ASScriptDelegateSignatureGuardCompat";
	static constexpr ANSICHAR DelegateSignatureConstructorModuleName[] = "ASScriptDelegateSignatureConstructorCompat";

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

using namespace AngelscriptTest_Bindings_AngelscriptDelegateSignatureGuardBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDelegateSignatureGuardBindingsTest,
	"Angelscript.TestModule.Bindings.ScriptDelegateSignatureGuardCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDelegateSignatureConstructorBindingsTest,
	"Angelscript.TestModule.Bindings.ScriptDelegateSignatureConstructorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDelegateSignatureGuardBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	AddExpectedError(TEXT("Null object passed to BindUFunction."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Null signature passed to BindUFunction."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Null object passed to AddUFunction."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Null signature passed to AddUFunction."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("ASScriptDelegateSignatureGuardCompat"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("void TriggerBindNullObject()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerBindNullSignature()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerAddNullObject()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerAddNullSignature()"), EAutomationExpectedErrorFlags::Contains, 0, false);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASScriptDelegateSignatureGuardCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		DelegateSignatureGuardModuleName,
		TEXT(R"(
delegate int FNativeCallback(int Value, const FString& Label);
event void FNativeEvent(int Value, const FString& Label);

void TriggerBindNullObject()
{
	FNativeCallback Single;
	Single._Inner.BindUFunction(nullptr, n"NativeIntStringEvent", __DelegateSignature(Single));
}

void TriggerBindNullSignature()
{
	UObject TestObject = FindClass("UAngelscriptNativeScriptTestObject").GetDefaultObject();
	FNativeCallback Single;
	UDelegateFunction Signature = nullptr;
	Single._Inner.BindUFunction(TestObject, n"NativeIntStringEvent", Signature);
}

void TriggerAddNullObject()
{
	FNativeEvent Multi;
	Multi._Inner.AddUFunction(nullptr, n"SetIntStringFromDelegate", __DelegateSignature(Multi));
}

void TriggerAddNullSignature()
{
	UObject TestObject = FindClass("UAngelscriptNativeScriptTestObject").GetDefaultObject();
	FNativeEvent Multi;
	UDelegateFunction Signature = nullptr;
	Multi._Inner.AddUFunction(TestObject, n"SetIntStringFromDelegate", Signature);
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	FString BindNullObjectException;
	int32 BindNullObjectLine = 0;
	const bool bBindNullObject = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerBindNullObject()"),
			TEXT("Delegate signature BindUFunction explicit-signature null object"),
			BindNullObjectException,
			BindNullObjectLine)
		&& TestTrue(TEXT("Delegate signature BindUFunction explicit-signature null object should mention the null-object contract"), BindNullObjectException.Contains(TEXT("Null object passed to BindUFunction.")))
		&& TestTrue(TEXT("Delegate signature BindUFunction explicit-signature null object should report a positive exception line"), BindNullObjectLine > 0);

	FString BindNullSignatureException;
	int32 BindNullSignatureLine = 0;
	const bool bBindNullSignature = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerBindNullSignature()"),
			TEXT("Delegate signature BindUFunction explicit-signature null signature"),
			BindNullSignatureException,
			BindNullSignatureLine)
		&& TestTrue(TEXT("Delegate signature BindUFunction explicit-signature null signature should mention the null-signature contract"), BindNullSignatureException.Contains(TEXT("Null signature passed to BindUFunction.")))
		&& TestTrue(TEXT("Delegate signature BindUFunction explicit-signature null signature should report a positive exception line"), BindNullSignatureLine > 0);

	FString AddNullObjectException;
	int32 AddNullObjectLine = 0;
	const bool bAddNullObject = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerAddNullObject()"),
			TEXT("Delegate signature AddUFunction explicit-signature null object"),
			AddNullObjectException,
			AddNullObjectLine)
		&& TestTrue(TEXT("Delegate signature AddUFunction explicit-signature null object should mention the null-object contract"), AddNullObjectException.Contains(TEXT("Null object passed to AddUFunction.")))
		&& TestTrue(TEXT("Delegate signature AddUFunction explicit-signature null object should report a positive exception line"), AddNullObjectLine > 0);

	FString AddNullSignatureException;
	int32 AddNullSignatureLine = 0;
	const bool bAddNullSignature = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerAddNullSignature()"),
			TEXT("Delegate signature AddUFunction explicit-signature null signature"),
			AddNullSignatureException,
			AddNullSignatureLine)
		&& TestTrue(TEXT("Delegate signature AddUFunction explicit-signature null signature should mention the null-signature contract"), AddNullSignatureException.Contains(TEXT("Null signature passed to AddUFunction.")))
		&& TestTrue(TEXT("Delegate signature AddUFunction explicit-signature null signature should report a positive exception line"), AddNullSignatureLine > 0);

	bPassed = bBindNullObject && bBindNullSignature && bAddNullObject && bAddNullSignature;
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptDelegateSignatureConstructorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	AddExpectedError(TEXT("Null object passed to BindUFunction."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Null signature passed to BindUFunction."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("ASScriptDelegateSignatureConstructorCompat"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("void TriggerConstructorNullObject()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerConstructorNullSignature()"), EAutomationExpectedErrorFlags::Contains, 0, false);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASScriptDelegateSignatureConstructorCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		DelegateSignatureConstructorModuleName,
		TEXT(R"(
delegate int FNativeCallback(int Value, const FString& Label);
event void FNativeEvent(int Value, const FString& Label);

int Entry()
{
	UObject TestObject = FindClass("UAngelscriptNativeScriptTestObject").GetDefaultObject();
	if (TestObject == nullptr)
		return 10;

	FNativeCallback Single;
	UDelegateFunction SingleSignature = __DelegateSignature(Single);
	if (SingleSignature == nullptr)
		return 20;

	_FScriptDelegate Constructed(TestObject, n"NativeIntStringEvent", SingleSignature);
	Single._Inner = Constructed;
	if (!Single.IsBound())
		return 30;
	if (!Single.GetFunctionName().IsEqual(n"NativeIntStringEvent"))
		return 40;
	if (Single.Execute(8, "Beta") != 12)
		return 50;

	FNativeEvent Multi;
	UDelegateFunction MultiSignature = __DelegateSignature(Multi);
	if (MultiSignature == nullptr)
		return 60;

	Multi._Inner.AddUFunction(TestObject, n"SetIntStringFromDelegate", MultiSignature);
	if (!Multi.IsBound())
		return 70;

	Multi.Broadcast(17, "Beta");
	Multi.Unbind(TestObject, n"SetIntStringFromDelegate");
	if (Multi.IsBound())
		return 80;

	return 1;
}

void TriggerConstructorNullObject()
{
	FNativeCallback Single;
	_FScriptDelegate Constructed(nullptr, n"NativeIntStringEvent", __DelegateSignature(Single));
}

void TriggerConstructorNullSignature()
{
	UObject TestObject = FindClass("UAngelscriptNativeScriptTestObject").GetDefaultObject();
	UDelegateFunction Signature = nullptr;
	_FScriptDelegate Constructed(TestObject, n"NativeIntStringEvent", Signature);
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	UAngelscriptNativeScriptTestObject* NativeTestObject = GetMutableDefault<UAngelscriptNativeScriptTestObject>();
	if (!TestNotNull(TEXT("Delegate signature constructor compat test should resolve the native test object default instance"), NativeTestObject))
	{
		return false;
	}

	NativeTestObject->NameCounts.Reset();
	ON_SCOPE_EXIT
	{
		NativeTestObject->NameCounts.Reset();
	};

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 EntryResult = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, EntryResult))
	{
		return false;
	}

	const int32* BetaCount = NativeTestObject->NameCounts.Find(TEXT("Beta"));
	const bool bHappyPath = TestEqual(TEXT("Delegate signature constructor compat should execute the explicit-signature constructor happy path"), EntryResult, 1)
		&& TestNotNull(TEXT("Delegate signature constructor compat should forward the expected multicast label key"), BetaCount)
		&& BetaCount != nullptr
		&& TestEqual(TEXT("Delegate signature constructor compat should forward the expected multicast value"), *BetaCount, 17);

	FString ConstructorNullObjectException;
	int32 ConstructorNullObjectLine = 0;
	const bool bConstructorNullObject = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerConstructorNullObject()"),
			TEXT("Delegate signature constructor explicit-signature null object"),
			ConstructorNullObjectException,
			ConstructorNullObjectLine)
		&& TestTrue(TEXT("Delegate signature constructor explicit-signature null object should mention the null-object contract"), ConstructorNullObjectException.Contains(TEXT("Null object passed to BindUFunction.")))
		&& TestTrue(TEXT("Delegate signature constructor explicit-signature null object should report a positive exception line"), ConstructorNullObjectLine > 0);

	FString ConstructorNullSignatureException;
	int32 ConstructorNullSignatureLine = 0;
	const bool bConstructorNullSignature = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerConstructorNullSignature()"),
			TEXT("Delegate signature constructor explicit-signature null signature"),
			ConstructorNullSignatureException,
			ConstructorNullSignatureLine)
		&& TestTrue(TEXT("Delegate signature constructor explicit-signature null signature should mention the null-signature contract"), ConstructorNullSignatureException.Contains(TEXT("Null signature passed to BindUFunction.")))
		&& TestTrue(TEXT("Delegate signature constructor explicit-signature null signature should report a positive exception line"), ConstructorNullSignatureLine > 0);

	bPassed = bHappyPath && bConstructorNullObject && bConstructorNullSignature;
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
