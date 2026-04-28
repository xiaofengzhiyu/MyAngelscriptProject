#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptColorBindingsTests_Private
{
	static constexpr ANSICHAR ModuleName[] = "ASColorRoundTripCompat";
}

using namespace AngelscriptTest_Bindings_AngelscriptColorBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptColorRoundTripCompatTest,
	"Angelscript.TestModule.Bindings.ColorRoundTripCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptColorRoundTripCompatTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	const FString ScriptSource = TEXT(R"AS(
int Entry()
{
	FColor Parsed = FColor::FromHex("#FF0000FF");
	if (!(Parsed == FColor::Red))
		return 10;

	if (!(Parsed.ToHex() == FColor::Red.ToHex()))
		return 20;

	if (!(FColor::Red == FColor(255, 0, 0, 255)))
		return 30;

	FLinearColor LinearFromRed = FLinearColor(FColor::Red);
	if (!(LinearFromRed.ToFColor(true) == FColor::Red))
		return 40;

	FLinearColor Value = FLinearColor(0.25, 0.5, 0.75, 1.0);
	FLinearColor ColorFactor = FLinearColor(2.0, 4.0, 0.5, 1.0);

	Value *= ColorFactor;
	if (!Value.Equals(FLinearColor(0.5, 2.0, 0.375, 1.0), 0.0001))
		return 50;

	Value /= ColorFactor;
	if (!Value.Equals(FLinearColor(0.25, 0.5, 0.75, 1.0), 0.0001))
		return 60;

	Value *= 2.0;
	if (!Value.Equals(FLinearColor(0.5, 1.0, 1.5, 2.0), 0.0001))
		return 70;

	Value /= 2.0;
	if (!Value.Equals(FLinearColor(0.25, 0.5, 0.75, 1.0), 0.0001))
		return 80;

	FLinearColor HsvRoundTrip = Value.LinearRGBToHSV().HSVToLinearRGB();
	if (!HsvRoundTrip.Equals(Value, 0.001))
		return 90;

	return 1;
}
)AS");

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(ModuleName));
	};

	asIScriptModule* Module = BuildModule(*this, Engine, ModuleName, ScriptSource);
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

	bPassed = TestEqual(
		TEXT("FColor/FLinearColor bindings should preserve color round-trip and in-place arithmetic semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
