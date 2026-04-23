#include "Shared/AngelscriptFunctionalTestUtils.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

// Test Layer: UE Scenario
//
// Tests Phase 1 of Plan_InterfaceParityWithCpp.md — structural signature
// matching in `FinalizeClass`. Before Phase 1, interface method completeness
// was checked only by name; a signature-mismatching implementation would
// compile silently and fail at runtime. These tests lock in the compile-time
// diagnostic behavior for parameter count, parameter type, return type, and
// const-ness mismatches, plus an exact-match positive regression.
#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptFunctionalTestUtils;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceSignatureArgCountMismatchTest,
	"Angelscript.TestModule.Interface.Signature.ArgCountMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceSignatureArgTypeMismatchTest,
	"Angelscript.TestModule.Interface.Signature.ArgTypeMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceSignatureReturnTypeMismatchTest,
	"Angelscript.TestModule.Interface.Signature.ReturnTypeMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceSignatureConstMismatchTest,
	"Angelscript.TestModule.Interface.Signature.ConstMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceSignatureExactMatchTest,
	"Angelscript.TestModule.Interface.Signature.ExactMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptTestInterfaceSignatureArgCountMismatchTest::RunTest(const FString& Parameters)
{
	bool bCompileSucceeded = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	static const FName ModuleName(TEXT("TestInterfaceSignatureArgCountMismatch"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	AddExpectedError(TEXT("mismatching signature"), EAutomationExpectedErrorFlags::Contains, 1);
	bCompileSucceeded = CompileAnnotatedModuleFromMemory(
		&Engine,
		ModuleName,
		TEXT("TestInterfaceSignatureArgCountMismatch.as"),
		TEXT(R"AS(
UINTERFACE()
interface UISigArgCount
{
	void DoIt(int A, int B);
}

UCLASS()
class ATestSigArgCount : AActor, UISigArgCount
{
	UFUNCTION()
	void DoIt(int A) {}
}
)AS"));

	ASTEST_END_SHARE_FRESH

	return bCompileSucceeded && !HasAnyErrors();
}

bool FAngelscriptTestInterfaceSignatureArgTypeMismatchTest::RunTest(const FString& Parameters)
{
	bool bCompileSucceeded = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	static const FName ModuleName(TEXT("TestInterfaceSignatureArgTypeMismatch"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	AddExpectedError(TEXT("mismatching signature"), EAutomationExpectedErrorFlags::Contains, 1);
	bCompileSucceeded = CompileAnnotatedModuleFromMemory(
		&Engine,
		ModuleName,
		TEXT("TestInterfaceSignatureArgTypeMismatch.as"),
		TEXT(R"AS(
UINTERFACE()
interface UISigArgType
{
	void DoIt(int Amount);
}

UCLASS()
class ATestSigArgType : AActor, UISigArgType
{
	UFUNCTION()
	void DoIt(FString Amount) {}
}
)AS"));

	ASTEST_END_SHARE_FRESH

	return bCompileSucceeded && !HasAnyErrors();
}

bool FAngelscriptTestInterfaceSignatureReturnTypeMismatchTest::RunTest(const FString& Parameters)
{
	bool bCompileSucceeded = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	static const FName ModuleName(TEXT("TestInterfaceSignatureReturnTypeMismatch"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	AddExpectedError(TEXT("mismatching signature"), EAutomationExpectedErrorFlags::Contains, 1);
	bCompileSucceeded = CompileAnnotatedModuleFromMemory(
		&Engine,
		ModuleName,
		TEXT("TestInterfaceSignatureReturnTypeMismatch.as"),
		TEXT(R"AS(
UINTERFACE()
interface UISigReturnType
{
	int GetValue();
}

UCLASS()
class ATestSigReturnType : AActor, UISigReturnType
{
	UFUNCTION()
	bool GetValue() { return false; }
}
)AS"));

	ASTEST_END_SHARE_FRESH

	return bCompileSucceeded && !HasAnyErrors();
}

bool FAngelscriptTestInterfaceSignatureConstMismatchTest::RunTest(const FString& Parameters)
{
	bool bCompileSucceeded = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	static const FName ModuleName(TEXT("TestInterfaceSignatureConstMismatch"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	AddExpectedError(TEXT("mismatching signature"), EAutomationExpectedErrorFlags::Contains, 1);
	bCompileSucceeded = CompileAnnotatedModuleFromMemory(
		&Engine,
		ModuleName,
		TEXT("TestInterfaceSignatureConstMismatch.as"),
		TEXT(R"AS(
UINTERFACE()
interface UISigConst
{
	int GetValue() const;
}

UCLASS()
class ATestSigConst : AActor, UISigConst
{
	UFUNCTION()
	int GetValue() { return 0; }
}
)AS"));

	ASTEST_END_SHARE_FRESH

	return bCompileSucceeded && !HasAnyErrors();
}

bool FAngelscriptTestInterfaceSignatureExactMatchTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	do
	{
	static const FName ModuleName(TEXT("TestInterfaceSignatureExactMatch"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileScriptModule(
		*this,
		Engine,
		ModuleName,
		TEXT("TestInterfaceSignatureExactMatch.as"),
		TEXT(R"AS(
UINTERFACE()
interface UISigExact
{
	int Compute(int A, int B) const;
	void Reset();
}

UCLASS()
class ATestSigExact : AActor, UISigExact
{
	UFUNCTION()
	int Compute(int A, int B) const { return A + B; }

	UFUNCTION()
	void Reset() {}
}
)AS"),
		TEXT("ATestSigExact"));

	TestNotNull(TEXT("ScriptClass should compile cleanly when signatures match exactly"), ScriptClass);

	UClass* InterfaceClass = FindGeneratedClass(&Engine, TEXT("UISigExact"));
	TestNotNull(TEXT("Interface class should exist"), InterfaceClass);
	if (ScriptClass != nullptr && InterfaceClass != nullptr)
	{
		TestTrue(TEXT("Implementing class should satisfy the interface"), ScriptClass->ImplementsInterface(InterfaceClass));
	}
	}
	while (false);
	ASTEST_END_SHARE_FRESH

	return !HasAnyErrors();
}

#endif
