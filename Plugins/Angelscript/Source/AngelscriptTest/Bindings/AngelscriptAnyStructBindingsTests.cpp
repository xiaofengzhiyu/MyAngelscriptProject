#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Bindings/AngelscriptDataTableBindingTestTypes.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptAnyStructImplicitConstructorRoundTripTest,
	"Angelscript.TestModule.Bindings.AnyStructParameter.ImplicitConstructorsRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptAnyStructBindingsTests_Private
{
	static constexpr ANSICHAR AnyStructModuleName[] = "ASAnyStructImplicitConstructors";

	void DiscardModule(FAngelscriptEngine& Engine, const ANSICHAR* ModuleName)
	{
		Engine.DiscardModule(UTF8_TO_TCHAR(ModuleName));
	}

	FString BuildAnyStructScript()
	{
		return TEXT(R"(
int ValidateAnyStruct(FAngelscriptAnyStructParameter AnyParam, int ExpectedVariant)
{
	if (!AnyParam.InstancedStruct.IsValid())
		return 10;

	FInstancedStruct Snapshot = AnyParam.InstancedStruct;
	if (!Snapshot.IsValid())
		return 20;

	FAngelscriptBindingDataTableRow Extracted;
	Snapshot.Get(Extracted);

	if (ExpectedVariant == 0)
	{
		if (Extracted.Category != n"Enemy")
			return 30;
		if (Extracted.Count != 3)
			return 40;
		if (Extracted.Label != "Gamma")
			return 50;
		return 1;
	}

	if (ExpectedVariant == 1)
	{
		if (Extracted.Category != n"Item")
			return 60;
		if (Extracted.Count != 9)
			return 70;
		if (Extracted.Label != "Delta")
			return 80;
		return 1;
	}

	return 90;
}

int Entry()
{
	FAngelscriptBindingDataTableRow Enemy;
	Enemy.Category = n"Enemy";
	Enemy.Count = 3;
	Enemy.Label = "Gamma";

	const int FromStruct = ValidateAnyStruct(Enemy, 0);
	if (FromStruct != 1)
		return 100 + FromStruct;

	FInstancedStruct Wrapped = FInstancedStruct::Make(Enemy);
	if (!Wrapped.IsValid())
		return 200;

	const int FromInstancedStruct = ValidateAnyStruct(Wrapped, 0);
	if (FromInstancedStruct != 1)
		return 300 + FromInstancedStruct;

	FAngelscriptBindingDataTableRow Item;
	Item.Category = n"Item";
	Item.Count = 9;
	Item.Label = "Delta";

	Wrapped.InitializeAs(Item);
	const int FromReinitializedInstancedStruct = ValidateAnyStruct(Wrapped, 1);
	if (FromReinitializedInstancedStruct != 1)
		return 400 + FromReinitializedInstancedStruct;

	return 1;
}
)");
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptAnyStructBindingsTests_Private;

bool FAngelscriptAnyStructImplicitConstructorRoundTripTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		DiscardModule(Engine, AnyStructModuleName);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		AnyStructModuleName,
		BuildAnyStructScript());
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed = TestEqual(
		TEXT("FAngelscriptAnyStructParameter should accept both reflected USTRUCT values and FInstancedStruct inputs through its implicit constructors"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
