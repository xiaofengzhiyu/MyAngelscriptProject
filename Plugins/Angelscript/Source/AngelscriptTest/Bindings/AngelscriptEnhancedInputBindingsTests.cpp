#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInputActionValueMulAssignCompatTest,
	"Angelscript.TestModule.Bindings.InputActionValueMulAssignCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptInputActionValueMulAssignCompatTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

		asIScriptModule* Module = BuildModule(
			*this,
			Engine,
			"ASInputActionValueMulAssignCompat",
			TEXT(R"(
int Entry()
{
	FInputActionValue Value(2.0f);
	FInputActionValue Delta(1.0f);

	Value.opMulAssign(0.5f).opMulAssign(0.5f);
	if (Value.GetAxis1D() < 0.49f || Value.GetAxis1D() > 0.51f)
		return 10;

	Value += Delta;
	if (Value.GetAxis1D() < 1.49f || Value.GetAxis1D() > 1.51f)
		return 20;

	if (!Value.IsNonZero())
		return 30;

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

		TestEqual(TEXT("FInputActionValue *= should keep chaining on the original value and preserve later += updates"), Result, 1);

	ASTEST_END_SHARE_CLEAN

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptEnhancedInputComponentConstCompatTest,
	"Angelscript.TestModule.Bindings.EnhancedInputComponentConstCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInputDebugKeyBindingExecuteCompatTest,
	"Angelscript.TestModule.Bindings.InputDebugKeyBindingExecuteCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptEnhancedInputComponentConstCompatTest::RunTest(const FString& Parameters)
{
	struct FConstClearMethodExpectation
	{
		const TCHAR* MethodName = nullptr;
		const TCHAR* ScriptModuleSuffix = nullptr;
		const TCHAR* CallExpression = nullptr;
	};

	static const FConstClearMethodExpectation ConstClearMethods[] =
	{
		{ TEXT("ClearActionEventBindings"), TEXT("ActionEvent"), TEXT("Comp.ClearActionEventBindings();") },
		{ TEXT("ClearActionValueBindings"), TEXT("ActionValue"), TEXT("Comp.ClearActionValueBindings();") },
		{ TEXT("ClearDebugKeyBindings"), TEXT("DebugKey"), TEXT("Comp.ClearDebugKeyBindings();") },
		{ TEXT("ClearActionBindings"), TEXT("ActionBindings"), TEXT("Comp.ClearActionBindings();") },
	};

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

		for (const FConstClearMethodExpectation& Expectation : ConstClearMethods)
		{
			const FName ModuleName(*FString::Printf(TEXT("ASEnhancedInputComponentConstCompat_%s"), Expectation.ScriptModuleSuffix));
			const FString ScriptFilename = FString::Printf(TEXT("ASEnhancedInputComponentConstCompat_%s.as"), Expectation.ScriptModuleSuffix);
			const FString Script = FString::Printf(TEXT(R"(
bool ReadConst(const UEnhancedInputComponent Comp)
{
	return Comp.HasBindings();
}

void MutateConst(const UEnhancedInputComponent Comp)
{
	%s
}

int Entry()
{
	return 1;
}
)"), Expectation.CallExpression);

			FAngelscriptCompileTraceSummary CompileSummary;
			const bool bCompiled = CompileModuleWithSummary(
				&Engine,
				ECompileType::SoftReloadOnly,
				ModuleName,
				ScriptFilename,
				Script,
				false,
				CompileSummary,
				true);
			if (!TestFalse(
				FString::Printf(TEXT("const UEnhancedInputComponent should reject %s"), Expectation.MethodName),
				bCompiled))
			{
				return false;
			}

			TestEqual(
				FString::Printf(TEXT("Rejecting const %s should produce a regular compile error"), Expectation.MethodName),
				CompileSummary.CompileResult,
				ECompileResult::Error);
		}

		asIScriptModule* MutableModule = nullptr;
		ASTEST_BUILD_MODULE(
			Engine,
			"ASEnhancedInputComponentMutableCompat",
			TEXT(R"(
bool ReadConst(const UEnhancedInputComponent Comp)
{
	return Comp.HasBindings();
}

bool MutateMutable(UEnhancedInputComponent Comp)
{
	Comp.ClearActionEventBindings();
	Comp.ClearActionValueBindings();
	Comp.ClearDebugKeyBindings();
	Comp.ClearActionBindings();
	return Comp.HasBindings();
}

int Entry()
{
	return 1;
}
)"),
			MutableModule);

		asIScriptFunction* Function = GetFunctionByDecl(*this, *MutableModule, TEXT("int Entry()"));
		if (Function == nullptr)
		{
			return false;
		}

		int32 Result = 0;
		if (!ExecuteIntFunction(*this, Engine, *Function, Result))
		{
			return false;
		}

		TestEqual(TEXT("Mutable UEnhancedInputComponent should still compile and execute a no-op entry"), Result, 1);

	ASTEST_END_SHARE_CLEAN

	return true;
}

bool FAngelscriptInputDebugKeyBindingExecuteCompatTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

		asIScriptModule* Module = BuildModule(
			*this,
			Engine,
			"ASInputDebugKeyBindingExecuteCompat",
			TEXT(R"(
int VerifyBindingCompat(
	FEnhancedInputActionEventBinding& EventBinding,
	FEnhancedInputActionValueBinding& ValueBinding,
	FInputDebugKeyBinding& DebugBinding,
	const FInputActionInstance& ActionInstance,
	const FInputActionValue& ActionValue)
{
	const uint EventHandle = EventBinding.GetHandle();
	EventBinding.Execute(ActionInstance);

	const uint ValueHandle = ValueBinding.GetHandle();
	const FInputActionValue CurrentValue = ValueBinding.GetValue();

	const uint DebugHandle = DebugBinding.GetHandle();
	DebugBinding.Execute(ActionValue);

	if (CurrentValue.IsNonZero() && EventHandle == ValueHandle && ValueHandle == DebugHandle)
		return 2;

	return 1;
}

int Entry()
{
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

		TestEqual(
			TEXT("FInputDebugKeyBinding.Execute signature should coexist with enhanced-input binding handle helpers and keep the module executable"),
			Result,
			1);

	ASTEST_END_SHARE_CLEAN

	return true;
}

#endif
