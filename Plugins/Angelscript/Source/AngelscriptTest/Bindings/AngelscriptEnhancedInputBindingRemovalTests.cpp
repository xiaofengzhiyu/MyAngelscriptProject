#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptEnhancedInputBindingRemovalTests_Private
{
	static constexpr ANSICHAR EnhancedInputBindingRemovalModuleName[] = "ASEnhancedInputBindingRemovalCompat";

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	bool ExecuteIntFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		int32& OutResult)
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

		if (!BindArguments(*Context))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("%s saw a script exception: %s"),
					ContextLabel,
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}

	struct FEnhancedInputBindingFixture
	{
		UEnhancedInputComponent* NativeComponent = nullptr;
		UEnhancedInputComponent* ScriptComponent = nullptr;
		UInputAction* Action = nullptr;
	};

	FEnhancedInputBindingFixture CreateFixture(FAutomationTestBase& Test)
	{
		FEnhancedInputBindingFixture Fixture;
		Fixture.NativeComponent = NewObject<UEnhancedInputComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Fixture.ScriptComponent = NewObject<UEnhancedInputComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Fixture.Action = NewObject<UInputAction>(GetTransientPackage(), NAME_None, RF_Transient);

		if (!Test.TestNotNull(TEXT("EnhancedInput fixture should create a native baseline component"), Fixture.NativeComponent)
			|| !Test.TestNotNull(TEXT("EnhancedInput fixture should create a script verification component"), Fixture.ScriptComponent)
			|| !Test.TestNotNull(TEXT("EnhancedInput fixture should create a transient input action"), Fixture.Action))
		{
			return Fixture;
		}

		Fixture.Action->ValueType = EInputActionValueType::Axis2D;
		return Fixture;
	}

	bool VerifyNativeBaseline(
		FAutomationTestBase& Test,
		const FEnhancedInputBindingFixture& Fixture)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(TEXT("EnhancedInput native baseline should keep the transient component alive"), Fixture.NativeComponent);
		bPassed &= Test.TestNotNull(TEXT("EnhancedInput native baseline should keep the transient input action alive"), Fixture.Action);
		if (!bPassed)
		{
			return false;
		}

		bPassed &= Test.TestFalse(
			TEXT("EnhancedInput native baseline should start without bindings"),
			Fixture.NativeComponent->HasBindings());
		bPassed &= Test.TestEqual(
			TEXT("EnhancedInput native baseline should start with no action-value bindings"),
			Fixture.NativeComponent->GetActionValueBindings().Num(),
			0);
		if (!bPassed)
		{
			return false;
		}

		FEnhancedInputActionValueBinding& NativeBinding = Fixture.NativeComponent->BindActionValue(Fixture.Action);
		const uint32 FirstHandle = NativeBinding.GetHandle();
		const FVector2D InitialBindingValue = NativeBinding.GetValue().Get<FVector2D>();
		const FVector2D InitialComponentValue = Fixture.NativeComponent->GetBoundActionValue(Fixture.Action).Get<FVector2D>();

		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should allocate a non-zero binding handle"),
			FirstHandle != 0);
		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should expose the bound action through the value binding"),
			NativeBinding.GetAction() == Fixture.Action);
		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should surface a zero Axis2D value from the new binding"),
			InitialBindingValue.IsNearlyZero());
		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should surface a zero Axis2D value from GetBoundActionValue"),
			InitialComponentValue.IsNearlyZero());
		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should report bindings after BindActionValue"),
			Fixture.NativeComponent->HasBindings());
		bPassed &= Test.TestEqual(
			TEXT("EnhancedInput native baseline should append one action-value binding"),
			Fixture.NativeComponent->GetActionValueBindings().Num(),
			1);
		if (!bPassed)
		{
			return false;
		}

		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should remove the binding by handle"),
			Fixture.NativeComponent->RemoveBindingByHandle(FirstHandle));
		bPassed &= Test.TestFalse(
			TEXT("EnhancedInput native baseline should reject removing the same handle twice"),
			Fixture.NativeComponent->RemoveBindingByHandle(FirstHandle));
		bPassed &= Test.TestFalse(
			TEXT("EnhancedInput native baseline should not keep bindings after handle removal"),
			Fixture.NativeComponent->HasBindings());
		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should return the Action value type's zero vector after handle removal"),
			Fixture.NativeComponent->GetBoundActionValue(Fixture.Action).Get<FVector2D>().IsNearlyZero());
		if (!bPassed)
		{
			return false;
		}

		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should remove a fresh action-value binding through the typed RemoveBinding overload"),
			Fixture.NativeComponent->RemoveBinding(Fixture.NativeComponent->BindActionValue(Fixture.Action)));
		bPassed &= Test.TestFalse(
			TEXT("EnhancedInput native baseline should not keep bindings after the typed RemoveBinding overload"),
			Fixture.NativeComponent->HasBindings());
		bPassed &= Test.TestTrue(
			TEXT("EnhancedInput native baseline should return the Action value type's zero vector after typed removal"),
			Fixture.NativeComponent->GetBoundActionValue(Fixture.Action).Get<FVector2D>().IsNearlyZero());
		bPassed &= Test.TestFalse(
			TEXT("EnhancedInput native baseline should reject RemoveActionValueBinding when the array is already empty"),
			Fixture.NativeComponent->RemoveActionValueBinding(0));
		return bPassed;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptEnhancedInputBindingRemovalTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptEnhancedInputBindingRemovalCompatTest,
	"Angelscript.TestModule.Bindings.EnhancedInputBindingRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptEnhancedInputBindingRemovalCompatTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASEnhancedInputBindingRemovalCompat"));
		ResetSharedCloneEngine(Engine);
	};

	const FEnhancedInputBindingFixture Fixture = CreateFixture(*this);
	if (!VerifyNativeBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		EnhancedInputBindingRemovalModuleName,
		TEXT(R"AS(
int VerifyEnhancedInputBindingRemoval(UEnhancedInputComponent Comp, UInputAction Action)
{
	if (Comp == null || Action == null)
		return 1;

	if (Comp.HasBindings())
		return 2;

	if (!Comp.GetBoundActionValue(Action).GetAxis2D().IsNearlyZero())
		return 4;

	const uint FirstHandle = Comp.BindActionValue(Action).GetHandle();
	if (FirstHandle == 0)
		return 8;

	if (!Comp.HasBindings())
		return 16;

	if (!Comp.GetBoundActionValue(Action).GetAxis2D().IsNearlyZero())
		return 32;

	if (!Comp.RemoveBindingByHandle(FirstHandle))
		return 64;

	if (Comp.RemoveBindingByHandle(FirstHandle))
		return 128;

	if (Comp.HasBindings())
		return 256;

	if (!Comp.GetBoundActionValue(Action).GetAxis2D().IsNearlyZero())
		return 512;

	if (!Comp.RemoveBinding(Comp.BindActionValue(Action)))
		return 1024;

	if (Comp.HasBindings())
		return 2048;

	if (!Comp.GetBoundActionValue(Action).GetAxis2D().IsNearlyZero())
		return 4096;

	if (Comp.RemoveActionValueBinding(0))
		return 8192;

	return 0;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	int32 ResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyEnhancedInputBindingRemoval(UEnhancedInputComponent, UInputAction)"),
			[this, &Fixture](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, Fixture.ScriptComponent, TEXT("VerifyEnhancedInputBindingRemoval"))
					&& SetArgObjectChecked(*this, Context, 1, Fixture.Action, TEXT("VerifyEnhancedInputBindingRemoval"));
			},
			TEXT("VerifyEnhancedInputBindingRemoval"),
			ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("UEnhancedInputComponent action-value binding helpers should preserve bind, zero-value readback, and removal semantics in script"),
		ResultMask,
		0);
	bPassed &= TestFalse(
		TEXT("EnhancedInput binding removal script should leave the transient component without residual bindings"),
		Fixture.ScriptComponent->HasBindings());
	bPassed &= TestEqual(
		TEXT("EnhancedInput binding removal script should leave the action-value binding array empty"),
		Fixture.ScriptComponent->GetActionValueBindings().Num(),
		0);
	bPassed &= TestTrue(
		TEXT("EnhancedInput binding removal script should fall back to an Axis2D zero value after every binding is removed"),
		Fixture.ScriptComponent->GetBoundActionValue(Fixture.Action).Get<FVector2D>().IsNearlyZero());

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
