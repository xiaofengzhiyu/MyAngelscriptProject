#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSetIteratorBindingsTest,
	"Angelscript.TestModule.Bindings.SetIteratorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMapIteratorBindingsTest,
	"Angelscript.TestModule.Bindings.MapIteratorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMapIteratorPairingBindingsTest,
	"Angelscript.TestModule.Bindings.MapIteratorPairingCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptSetIteratorBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASSetIteratorCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASSetIteratorCompat",
		TEXT(R"(
int Entry()
{
	TSet<int> Values;
	Values.Add(2);
	Values.Add(5);

	TSetIterator<int> It = Values.Iterator();
	int Sum = 0;
	while (It.CanProceed)
	{
		Sum += It.Proceed();
	}

	return Sum == 7 ? 1 : 10;
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

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	TestEqual(TEXT("TSet iterator helpers should behave as expected"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return true;
}

bool FAngelscriptMapIteratorBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMapIteratorCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASMapIteratorCompat",
		TEXT(R"(
int Entry()
{
	TMap<FName, int> Values;
	Values.Add(FName("Alpha"), 2);
	Values.Add(FName("Beta"), 5);

	TMapIterator<FName, int> It = Values.Iterator();
	int Sum = 0;
	int KeyCount = 0;
	while (It.CanProceed)
	{
		It.Proceed();
		if (It.GetKey() == FName("Alpha") || It.GetKey() == FName("Beta"))
			KeyCount += 1;
		Sum += It.GetValue();
	}

	return (Sum == 7 && KeyCount == 2) ? 1 : 10;
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

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	TestEqual(TEXT("TMap iterator helpers should behave as expected"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return true;
}

bool FAngelscriptMapIteratorPairingBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMapIteratorPairingCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASMapIteratorPairingCompat",
		TEXT(R"(
bool MatchesExpectedPair(FName Key, int Value)
{
	if (Key == FName("Alpha"))
		return Value == 2;
	if (Key == FName("Beta"))
		return Value == 9;
	if (Key == FName("Gamma"))
		return Value == 17;
	return false;
}

int Entry()
{
	TMap<FName, int> Empty;
	if (Empty.Iterator().CanProceed)
		return 10;

	TMap<FName, int> Values;
	Values.Add(FName("Alpha"), 2);
	Values.Add(FName("Beta"), 9);
	Values.Add(FName("Gamma"), 17);

	TMapIterator<FName, int> It = Values.Iterator();
	if (!It.CanProceed)
		return 11;

	It.Proceed();
	if (!MatchesExpectedPair(It.GetKey(), It.GetValue()))
		return 12;

	TMapIterator<FName, int> Copy = It;

	TMap<FName, int> OriginalRemaining;
	TMap<FName, int> CopyRemaining;

	OriginalRemaining.Add(It.GetKey(), It.GetValue());
	CopyRemaining.Add(Copy.GetKey(), Copy.GetValue());

	while (It.CanProceed)
	{
		It.Proceed();
		if (!MatchesExpectedPair(It.GetKey(), It.GetValue()))
			return 20;
		OriginalRemaining.Add(It.GetKey(), It.GetValue());
	}

	while (Copy.CanProceed)
	{
		Copy.Proceed();
		if (!MatchesExpectedPair(Copy.GetKey(), Copy.GetValue()))
			return 21;
		CopyRemaining.Add(Copy.GetKey(), Copy.GetValue());
	}

	if (It.CanProceed || Copy.CanProceed)
		return 30;

	if (OriginalRemaining.Num() != 3 || CopyRemaining.Num() != 3)
		return 31;

	int Value = 0;
	if (!OriginalRemaining.Find(FName("Alpha"), Value) || Value != 2)
		return 32;
	if (!OriginalRemaining.Find(FName("Beta"), Value) || Value != 9)
		return 33;
	if (!OriginalRemaining.Find(FName("Gamma"), Value) || Value != 17)
		return 34;

	if (!CopyRemaining.Find(FName("Alpha"), Value) || Value != 2)
		return 35;
	if (!CopyRemaining.Find(FName("Beta"), Value) || Value != 9)
		return 36;
	if (!CopyRemaining.Find(FName("Gamma"), Value) || Value != 17)
		return 37;

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

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	TestEqual(TEXT("TMap iterator pairing helpers should preserve key/value correspondence and copy remaining state"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return true;
}

#endif
