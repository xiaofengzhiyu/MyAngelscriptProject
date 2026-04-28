#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathIntegerDivisionBindingsTest,
	"Angelscript.TestModule.Bindings.MathIntegerDivisionCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptMathIntegerDivisionBindingsTests_Private
{
	static constexpr ANSICHAR MathIntegerDivisionModuleName[] = "ASMathIntegerDivisionCompat";

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
		const bool bHasExceptionString = Test.TestFalse(
			*FString::Printf(TEXT("%s should report a non-empty exception string"), ContextLabel),
			OutExceptionString.IsEmpty());
		const bool bHasExceptionLine = Test.TestTrue(
			*FString::Printf(TEXT("%s should report a positive exception line"), ContextLabel),
			OutExceptionLine > 0);
		return bHasExceptionString && bHasExceptionLine;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathIntegerDivisionBindingsTests_Private;

bool FAngelscriptMathIntegerDivisionBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathIntegerDivisionCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		MathIntegerDivisionModuleName,
		TEXT(R"(
int Entry()
{
	if (Math::IntegerDivisionTrunc(7, 3) != 2)
		return 10;
	if (Math::IntegerDivisionTrunc(-7, 3) != -2)
		return 20;
	if (Math::IntegerDivisionTrunc(7, -3) != -2)
		return 30;
	if (Math::IntegerDivisionTrunc(int64(-9), int64(4)) != int64(-2))
		return 40;
	if (Math::IntegerDivisionTrunc(uint32(9), uint32(4)) != uint32(2))
		return 50;
	if (Math::IntegerDivisionTrunc(uint64(17), uint64(5)) != uint64(3))
		return 60;

	return 1;
}

void TriggerInt32DivideByZero()
{
	if (Math::IntegerDivisionTrunc(7, 0) == 123456)
	{
	}
}

void TriggerInt64DivideByZero()
{
	if (Math::IntegerDivisionTrunc(int64(-9), int64(0)) == int64(123456))
	{
	}
}

void TriggerUint32DivideByZero()
{
	if (Math::IntegerDivisionTrunc(uint32(9), uint32(0)) == uint32(123456))
	{
	}
}

void TriggerUint64DivideByZero()
{
	if (Math::IntegerDivisionTrunc(uint64(17), uint64(0)) == uint64(123456))
	{
	}
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	const bool bHappyPathPassed = TestEqual(
		TEXT("Math::IntegerDivisionTrunc should preserve signed and unsigned truncation semantics"),
		Result,
		1);

	AddExpectedError(TEXT("Division by zero"), EAutomationExpectedErrorFlags::Contains, 4);
	AddExpectedError(TEXT("ASMathIntegerDivisionCompat"), EAutomationExpectedErrorFlags::Contains, 4);
	AddExpectedError(TEXT("| Col 2"), EAutomationExpectedErrorFlags::Contains, 4);

	FString ExceptionString;
	int32 ExceptionLine = INDEX_NONE;

	const bool bInt32DivideByZero = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerInt32DivideByZero()"),
			TEXT("Math::IntegerDivisionTrunc int32 divide-by-zero"),
			ExceptionString,
			ExceptionLine)
		&& TestEqual(
			TEXT("Math::IntegerDivisionTrunc int32 divide-by-zero should report the expected exception"),
			ExceptionString,
			FString(TEXT("Division by zero")));

	const bool bInt64DivideByZero = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerInt64DivideByZero()"),
			TEXT("Math::IntegerDivisionTrunc int64 divide-by-zero"),
			ExceptionString,
			ExceptionLine)
		&& TestEqual(
			TEXT("Math::IntegerDivisionTrunc int64 divide-by-zero should report the expected exception"),
			ExceptionString,
			FString(TEXT("Division by zero")));

	const bool bUint32DivideByZero = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerUint32DivideByZero()"),
			TEXT("Math::IntegerDivisionTrunc uint32 divide-by-zero"),
			ExceptionString,
			ExceptionLine)
		&& TestEqual(
			TEXT("Math::IntegerDivisionTrunc uint32 divide-by-zero should report the expected exception"),
			ExceptionString,
			FString(TEXT("Division by zero")));

	const bool bUint64DivideByZero = ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerUint64DivideByZero()"),
			TEXT("Math::IntegerDivisionTrunc uint64 divide-by-zero"),
			ExceptionString,
			ExceptionLine)
		&& TestEqual(
			TEXT("Math::IntegerDivisionTrunc uint64 divide-by-zero should report the expected exception"),
			ExceptionString,
			FString(TEXT("Division by zero")));

	bPassed =
		bHappyPathPassed &&
		bInt32DivideByZero &&
		bInt64DivideByZero &&
		bUint32DivideByZero &&
		bUint64DivideByZero;

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
