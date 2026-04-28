#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Bindings/AngelscriptDataTableBindingTestTypes.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMiscBindingsRuntimeSmokeTest,
	"Angelscript.TestModule.Bindings.MiscSmoke.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMiscBindingsCompileSurfaceTest,
	"Angelscript.TestModule.Bindings.MiscSmoke.CompileSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptMiscBindingsTests_Private
{
	static constexpr ANSICHAR MiscRuntimeModuleName[] = "ASMiscBindingRuntimeSmoke";
	static constexpr ANSICHAR MiscCompileSurfaceModuleName[] = "ASMiscBindingCompileSurface";

	void DiscardModule(FAngelscriptEngine& Engine, const ANSICHAR* ModuleName)
	{
		Engine.DiscardModule(UTF8_TO_TCHAR(ModuleName));
	}

	FString BuildRuntimeScript()
	{
		return TEXT(R"(
int Entry()
{
	UObject NullCallbackTarget = nullptr;
	FLatentActionInfo Info(7, 42, n"HandleAsync", NullCallbackTarget);
	if (Info.Linkage != 7)
		return 10;
	if (Info.UUID != 42)
		return 20;
	if (Info.ExecutionFunction != n"HandleAsync")
		return 30;
	if (Info.CallbackTarget != null)
		return 40;

	FAngelscriptBindingDataTableRow OriginalValue;
	OriginalValue.Category = n"Enemy";
	OriginalValue.Count = 2;
	OriginalValue.Label = "Alpha";

	FInstancedStruct Original = FInstancedStruct::Make(OriginalValue);
	if (!Original.IsValid())
		return 50;

	FInstancedStruct Copy = Original;
	if (!(Copy == Original))
		return 60;

	FAngelscriptBindingDataTableRow Extracted;
	Original.Get(Extracted);
	if (Extracted.Category != n"Enemy" || Extracted.Count != 2 || Extracted.Label != "Alpha")
		return 70;

	FAngelscriptBindingDataTableRow ReinitializedValue;
	ReinitializedValue.Category = n"Item";
	ReinitializedValue.Count = 7;
	ReinitializedValue.Label = "Beta";

	Copy.InitializeAs(ReinitializedValue);
	FAngelscriptBindingDataTableRow Reinitialized;
	Copy.Get(Reinitialized);
	if (Reinitialized.Category != n"Item" || Reinitialized.Count != 7 || Reinitialized.Label != "Beta")
		return 80;

	Copy.Reset();
	if (Copy.IsValid())
		return 90;

	return 1;
}
)");
	}

	FString BuildCompileSurfaceScript()
	{
		return TEXT(R"(
// Keep modal/UI surfaces compile-only in unattended automation.
void TouchMessageDialogOverloadsCompileOnly()
{
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("CompileOnly"), FText::FromString("MiscSmoke"));
	FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString("CompileOnly"), FText::FromString("MiscSmoke"));
}

bool TouchLandscapeCompileOnly(ALandscapeProxy Landscape)
{
	float32 Height = -1.0f;
	return Landscape != nullptr ? Landscape.GetHeightAtLocation(FVector(0.0f, 0.0f, 0.0f), Height) : false;
}

int Entry()
{
	ALandscapeProxy NullLandscape = nullptr;
	if (TouchLandscapeCompileOnly(NullLandscape))
		return 10;

	return 1;
}
)");
	}

	bool ExecuteEntry(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		int32& OutResult)
	{
		asIScriptFunction* EntryFunction = GetFunctionByDecl(Test, Module, TEXT("int Entry()"));
		if (EntryFunction == nullptr)
		{
			return false;
		}

		return ExecuteIntFunction(Test, Engine, *EntryFunction, OutResult);
	}

	bool VerifyCompileSurfaceFunctions(
		FAutomationTestBase& Test,
		asIScriptModule& Module)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			TEXT("Compile-surface misc bindings test should expose the message-dialog compile-only helper"),
			GetFunctionByDecl(Test, Module, TEXT("void TouchMessageDialogOverloadsCompileOnly()")));
		bPassed &= Test.TestNotNull(
			TEXT("Compile-surface misc bindings test should expose the landscape compile-only helper"),
			GetFunctionByDecl(Test, Module, TEXT("bool TouchLandscapeCompileOnly(ALandscapeProxy)")));
		return bPassed;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMiscBindingsTests_Private;

bool FAngelscriptMiscBindingsRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		DiscardModule(Engine, MiscRuntimeModuleName);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		MiscRuntimeModuleName,
		BuildRuntimeScript());
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteEntry(*this, Engine, *Module, Result))
	{
		return false;
	}

	bPassed = TestEqual(
		TEXT("Misc runtime smoke should preserve FLatentActionInfo field assignment and FInstancedStruct creation, copy, initialize and reset semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptMiscBindingsCompileSurfaceTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		DiscardModule(Engine, MiscCompileSurfaceModuleName);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		MiscCompileSurfaceModuleName,
		BuildCompileSurfaceScript());
	if (Module == nullptr)
	{
		return false;
	}

	bPassed &= VerifyCompileSurfaceFunctions(*this, *Module);

	int32 Result = INDEX_NONE;
	if (!ExecuteEntry(*this, Engine, *Module, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Misc compile-surface smoke should keep the landscape helper runnable with a null proxy while preserving message-dialog and landscape binding registration"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
