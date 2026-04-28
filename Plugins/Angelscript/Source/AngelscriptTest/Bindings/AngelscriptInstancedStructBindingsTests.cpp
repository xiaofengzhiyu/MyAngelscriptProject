#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Bindings/AngelscriptDataTableBindingTestTypes.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInstancedStructTypeMetadataBindingsTest,
	"Angelscript.TestModule.Bindings.InstancedStruct.TypeMetadataCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInstancedStructMismatchedExtractGuardBindingsTest,
	"Angelscript.TestModule.Bindings.InstancedStruct.MismatchedExtractGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInstancedStructErrorPathsBindingsTest,
	"Angelscript.TestModule.Bindings.InstancedStructErrorPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptInstancedStructBindingsTests_Private
{
	static constexpr ANSICHAR MetadataModuleName[] = "ASInstancedStructTypeMetadataCompat";
	static constexpr ANSICHAR GuardModuleName[] = "ASInstancedStructMismatchedExtractGuards";
	static constexpr ANSICHAR ErrorPathModuleName[] = "ASInstancedStructErrorPaths";

	void DiscardModule(FAngelscriptEngine& Engine, const ANSICHAR* ModuleName)
	{
		Engine.DiscardModule(UTF8_TO_TCHAR(ModuleName));
	}

	FString BuildMetadataScript()
	{
		return TEXT(R"(
int Entry()
{
	FAngelscriptBindingDataTableRow Seed;
	Seed.Category = n"Enemy";
	Seed.Count = 5;
	Seed.Label = "Seed";

	FInstancedStruct Source = FInstancedStruct::Make(Seed);
	if (!Source.IsValid())
		return 10;

	UScriptStruct RowType = Source.GetScriptStruct();
	if (RowType == null)
		return 20;
	if (!Source.Contains(RowType))
		return 30;

	FInstancedStruct Defaulted;
	if (Defaulted.IsValid())
		return 40;

	Defaulted.InitializeAs(RowType);
	if (!Defaulted.IsValid())
		return 50;
	if (!Defaulted.Contains(RowType))
		return 60;
	if (Defaulted.GetScriptStruct() != RowType)
		return 70;

	UObject NullCallbackTarget = nullptr;
	FLatentActionInfo Info(1, 2, n"Mismatch", NullCallbackTarget);
	FInstancedStruct Other = FInstancedStruct::Make(Info);
	if (!Other.IsValid())
		return 80;
	if (Source.Contains(Other.GetScriptStruct()))
		return 90;

	return 1;
}
)");
	}

	FString BuildGuardScript()
	{
		return TEXT(R"(
void TriggerMismatchedExtract()
{
	FAngelscriptBindingDataTableRow Seed;
	Seed.Category = n"Enemy";

	FInstancedStruct Source = FInstancedStruct::Make(Seed);
	if (!Source.IsValid())
		return;

	UObject NullCallbackTarget = nullptr;
	FLatentActionInfo WrongType(7, 42, n"HandleAsync", NullCallbackTarget);
	FInstancedStruct Other = FInstancedStruct::Make(WrongType);
	if (!Other.IsValid())
		return;

	FLatentActionInfo Extracted;
	Source.Get(Extracted);
}
)");
	}

	FString BuildErrorPathScript()
	{
		return TEXT(R"(
void TriggerEmptyExtract()
{
	FInstancedStruct Empty;
	FAngelscriptBindingDataTableRow Extracted;
	Extracted.Category = n"Sentinel";
	Extracted.Count = 77;
	Extracted.Label = "Sentinel";
	Empty.Get(Extracted);
}

void TriggerEmptyTypedGet()
{
	FAngelscriptBindingDataTableRow Seed;
	Seed.Category = n"Enemy";
	Seed.Count = 5;
	Seed.Label = "Seed";

	FInstancedStruct Source = FInstancedStruct::Make(Seed);
	FInstancedStruct Empty;
	FInstancedStruct Copy = FInstancedStruct::Make(Empty.Get(Source.GetScriptStruct()));
}

void TriggerMismatchedTypedGet()
{
	FAngelscriptBindingDataTableRow Seed;
	Seed.Category = n"Enemy";
	Seed.Count = 5;
	Seed.Label = "Seed";

	UObject NullCallbackTarget = nullptr;
	FLatentActionInfo Wrong(7, 42, n"HandleAsync", NullCallbackTarget);

	FInstancedStruct Source = FInstancedStruct::Make(Seed);
	FInstancedStruct Other = FInstancedStruct::Make(Wrong);
	FInstancedStruct Copy = FInstancedStruct::Make(Source.Get(Other.GetScriptStruct()));
}
)");
	}

	bool ExecuteFunctionExpectingScriptException(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const FString& FunctionDecl,
		const TCHAR* ContextLabel,
		const TCHAR* ExpectedException,
		FString* OutExceptionString = nullptr)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* ScriptContext = Engine.CreateContext();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should create an execution context"), ContextLabel), ScriptContext))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			ScriptContext->Release();
		};

		const int PrepareResult = ScriptContext->Prepare(Function);
		const int ExecuteResult = PrepareResult == asSUCCESS ? ScriptContext->Execute() : PrepareResult;
		const FString ExceptionString = UTF8_TO_TCHAR(
			ScriptContext->GetExceptionString() != nullptr ? ScriptContext->GetExceptionString() : "");
		const int32 ExceptionLine = ScriptContext->GetExceptionLineNumber();

		if (OutExceptionString != nullptr)
		{
			*OutExceptionString = ExceptionString;
		}

		const bool bPrepared = Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully before the runtime error path"), ContextLabel),
			PrepareResult,
			static_cast<int32>(asSUCCESS));
		const bool bThrew = Test.TestEqual(
			*FString::Printf(TEXT("%s should raise a script execution exception"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_EXCEPTION));
		const bool bHasMessage = Test.TestTrue(
			*FString::Printf(TEXT("%s should surface the expected exception message"), ContextLabel),
			ExceptionString.Contains(ExpectedException));
		const bool bHasLine = Test.TestTrue(
			*FString::Printf(TEXT("%s should report a positive exception line"), ContextLabel),
			ExceptionLine > 0);

		return bPrepared && bThrew && bHasMessage && bHasLine;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptInstancedStructBindingsTests_Private;

bool FAngelscriptInstancedStructTypeMetadataBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		DiscardModule(Engine, MetadataModuleName);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		MetadataModuleName,
		BuildMetadataScript());
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
		TEXT("FInstancedStruct should preserve script-visible struct metadata for GetScriptStruct, Contains, and InitializeAs(UScriptStruct)"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptInstancedStructMismatchedExtractGuardBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	AddExpectedError(TEXT("Mismatching types."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("ASInstancedStructMismatchedExtractGuards"), EAutomationExpectedErrorFlags::Exact, 1);
	AddExpectedError(TEXT("void TriggerMismatchedExtract() | Line"), EAutomationExpectedErrorFlags::Contains, 1);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		DiscardModule(Engine, GuardModuleName);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		GuardModuleName,
		BuildGuardScript());
	if (Module == nullptr)
	{
		return false;
	}

	FString ExceptionString;
	bPassed = ExecuteFunctionExpectingScriptException(
		*this,
		Engine,
		*Module,
		TEXT("void TriggerMismatchedExtract()"),
		TEXT("FInstancedStruct mismatched Get(out)"),
		TEXT("Mismatching types."),
		&ExceptionString);

	bPassed &= TestTrue(
		TEXT("FInstancedStruct mismatched Get(out) should mention the requested FLatentActionInfo type"),
		ExceptionString.Contains(TEXT("FLatentActionInfo")));
	bPassed &= TestTrue(
		TEXT("FInstancedStruct mismatched Get(out) should mention the contained FAngelscriptBindingDataTableRow type"),
		ExceptionString.Contains(TEXT("FAngelscriptBindingDataTableRow")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptInstancedStructErrorPathsBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	AddExpectedError(TEXT("Source is empty or not valid"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("Mismatching types."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("ASInstancedStructErrorPaths"), EAutomationExpectedErrorFlags::Exact, 3);
	AddExpectedError(TEXT("| Line "), EAutomationExpectedErrorFlags::Contains, 6);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		DiscardModule(Engine, ErrorPathModuleName);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ErrorPathModuleName,
		BuildErrorPathScript());
	if (Module == nullptr)
	{
		return false;
	}

	FString EmptyExtractException;
	bPassed = ExecuteFunctionExpectingScriptException(
		*this,
		Engine,
		*Module,
		TEXT("void TriggerEmptyExtract()"),
		TEXT("FInstancedStruct empty Get(out)"),
		TEXT("Source is empty or not valid"),
		&EmptyExtractException);

	bPassed &= TestTrue(
		TEXT("FInstancedStruct empty Get(out) should mention checking IsValid before extraction"),
		EmptyExtractException.Contains(TEXT("Check `IsValid()` before trying to `Get()` the underlying struct.")));

	FString EmptyTypedGetException;
	bPassed &= ExecuteFunctionExpectingScriptException(
		*this,
		Engine,
		*Module,
		TEXT("void TriggerEmptyTypedGet()"),
		TEXT("FInstancedStruct empty typed Get(UScriptStruct)"),
		TEXT("Source is empty or not valid"),
		&EmptyTypedGetException);

	bPassed &= TestTrue(
		TEXT("FInstancedStruct empty typed Get(UScriptStruct) should share the same IsValid guidance as Get(out)"),
		EmptyTypedGetException.Contains(TEXT("Check `IsValid()` before trying to `Get()` the underlying struct.")));

	FString MismatchedTypedGetException;
	bPassed &= ExecuteFunctionExpectingScriptException(
		*this,
		Engine,
		*Module,
		TEXT("void TriggerMismatchedTypedGet()"),
		TEXT("FInstancedStruct mismatched typed Get(UScriptStruct)"),
		TEXT("Mismatching types."),
		&MismatchedTypedGetException);

	bPassed &= TestTrue(
		TEXT("FInstancedStruct mismatched typed Get(UScriptStruct) should mention the contained FAngelscriptBindingDataTableRow type"),
		MismatchedTypedGetException.Contains(TEXT("FAngelscriptBindingDataTableRow")));
	bPassed &= TestTrue(
		TEXT("FInstancedStruct mismatched typed Get(UScriptStruct) should mention the requested FLatentActionInfo type"),
		MismatchedTypedGetException.Contains(TEXT("FLatentActionInfo")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
