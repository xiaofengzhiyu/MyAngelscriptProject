#include "../Shared/AngelscriptScenarioTestUtils.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "ClassGenerator/ASClass.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/PropertyOptional.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace OptionalPropertyBindingsTests
{
	static const FName ObserveStateFunctionName(TEXT("ObserveState"));
	static const FName SetValueFromScriptFunctionName(TEXT("SetValueFromScript"));
	static const FName ResetValueFromScriptFunctionName(TEXT("ResetValueFromScript"));
	static constexpr int32 UnsetStateCode = -1;
	static constexpr int32 UnexpectedStateCode = -2;

	enum class EOptionalValueKind : uint8
	{
		Int,
		Name,
	};

	struct FOptionalPropertyBindingScenario
	{
		FName ModuleName;
		FString ScriptFilename;
		FName GeneratedClassName;
		FName StoredPropertyName;
		FString ScriptPropertyType;
		EOptionalValueKind ValueKind = EOptionalValueKind::Int;
		bool bExpectScriptReadbackAfterNativeReflectedWrite = true;
		int32 NativeObservedState = 0;
		int32 ScriptObservedState = 0;
		int32 NativeIntValue = 0;
		int32 ScriptIntValue = 0;
		FName NativeNameValue;
		FName ScriptNameValue;
		const TCHAR* Context = TEXT("Optional property binding scenario");
	};

	static const FOptionalPropertyBindingScenario IntScenario
	{
		TEXT("ASOptionalIntPropertyReflection"),
		TEXT("ASOptionalIntPropertyReflection.as"),
		TEXT("UBindingOptionalIntPropertyCarrier"),
		TEXT("StoredValue"),
		TEXT("TOptional<int>"),
		EOptionalValueKind::Int,
		true,
		17,
		42,
		17,
		42,
		NAME_None,
		NAME_None,
		TEXT("TOptional<int> property reflection scenario"),
	};

	static const FOptionalPropertyBindingScenario NameScenario
	{
		TEXT("ASOptionalNamePropertyReflection"),
		TEXT("ASOptionalNamePropertyReflection.as"),
		TEXT("UBindingOptionalNamePropertyCarrier"),
		TEXT("StoredName"),
		TEXT("TOptional<FName>"),
		EOptionalValueKind::Name,
		false,
		100,
		200,
		0,
		0,
		TEXT("Alpha"),
		TEXT("Beta"),
		TEXT("TOptional<FName> property reflection scenario"),
	};

	FString BuildScriptSource(const FOptionalPropertyBindingScenario& Scenario)
	{
		if (Scenario.ValueKind == EOptionalValueKind::Int)
		{
			return FString::Printf(TEXT(R"AS(
UCLASS()
class %s : UObject
{
	UPROPERTY()
	%s %s;

	UFUNCTION()
	int ObserveState() const
	{
		if (!%s.IsSet())
			return %d;
		return %s.GetValue();
	}

	UFUNCTION()
	void SetValueFromScript()
	{
		%s = %d;
	}

	UFUNCTION()
	void ResetValueFromScript()
	{
		%s.Reset();
	}
}
)AS"),
				*Scenario.GeneratedClassName.ToString(),
				*Scenario.ScriptPropertyType,
				*Scenario.StoredPropertyName.ToString(),
				*Scenario.StoredPropertyName.ToString(),
				UnsetStateCode,
				*Scenario.StoredPropertyName.ToString(),
				*Scenario.StoredPropertyName.ToString(),
				Scenario.ScriptIntValue,
				*Scenario.StoredPropertyName.ToString());
		}

		return FString::Printf(TEXT(R"AS(
UCLASS()
class %s : UObject
{
	UPROPERTY()
	%s %s;

	UFUNCTION()
	int ObserveState() const
	{
		if (!%s.IsSet())
			return %d;
		if (%s.GetValue() == FName("%s"))
			return %d;
		if (%s.GetValue() == FName("%s"))
			return %d;
		return %d;
	}

	UFUNCTION()
	void SetValueFromScript()
	{
		%s = FName("%s");
	}

	UFUNCTION()
	void ResetValueFromScript()
	{
		%s.Reset();
	}
}
)AS"),
			*Scenario.GeneratedClassName.ToString(),
			*Scenario.ScriptPropertyType,
			*Scenario.StoredPropertyName.ToString(),
			*Scenario.StoredPropertyName.ToString(),
			UnsetStateCode,
			*Scenario.StoredPropertyName.ToString(),
			*Scenario.NativeNameValue.ToString(),
			Scenario.NativeObservedState,
			*Scenario.StoredPropertyName.ToString(),
			*Scenario.ScriptNameValue.ToString(),
			Scenario.ScriptObservedState,
			UnexpectedStateCode,
			*Scenario.StoredPropertyName.ToString(),
			*Scenario.ScriptNameValue.ToString(),
			*Scenario.StoredPropertyName.ToString());
	}

	UClass* CompileCarrierClass(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FOptionalPropertyBindingScenario& Scenario)
	{
		return CompileScriptModule(
			Test,
			Engine,
			Scenario.ModuleName,
			Scenario.ScriptFilename,
			BuildScriptSource(Scenario),
			Scenario.GeneratedClassName);
	}

	UFunction* RequireGeneratedFunction(
		FAutomationTestBase& Test,
		UClass* OwnerClass,
		FName FunctionName,
		const FOptionalPropertyBindingScenario& Scenario)
	{
		UFunction* Function = FindGeneratedFunction(OwnerClass, FunctionName);
		Test.TestNotNull(
			*FString::Printf(TEXT("%s should expose generated function '%s'"), Scenario.Context, *FunctionName.ToString()),
			Function);
		return Function;
	}

	bool InvokeGeneratedFunction(
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		void* Params)
	{
		if (!::IsValid(Object) || Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope FunctionScope(Engine, Object);
		if (UASFunction* ScriptFunction = Cast<UASFunction>(Function))
		{
			ScriptFunction->RuntimeCallEvent(Object, Params);
		}
		else
		{
			Object->ProcessEvent(Function, Params);
		}

		return true;
	}

	FOptionalProperty* ValidateReflectedProperty(
		FAutomationTestBase& Test,
		UClass* ScriptClass,
		const FOptionalPropertyBindingScenario& Scenario)
	{
		FProperty* Property = FindFProperty<FProperty>(ScriptClass, Scenario.StoredPropertyName);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should generate reflected property '%s'"), Scenario.Context, *Scenario.StoredPropertyName.ToString()),
				Property))
		{
			return nullptr;
		}

		FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(Property);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should materialize '%s' as FOptionalProperty"), Scenario.Context, *Scenario.StoredPropertyName.ToString()),
				OptionalProperty))
		{
			return nullptr;
		}

		FProperty* InnerProperty = OptionalProperty->GetValueProperty();
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should create an inner value property"), Scenario.Context),
				InnerProperty))
		{
			return nullptr;
		}

		if (Scenario.ValueKind == EOptionalValueKind::Int)
		{
			Test.TestNotNull(TEXT("TOptional<int> property reflection scenario should preserve FIntProperty as the inner property"), CastField<FIntProperty>(InnerProperty));
		}
		else
		{
			Test.TestNotNull(TEXT("TOptional<FName> property reflection scenario should preserve FNameProperty as the inner property"), CastField<FNameProperty>(InnerProperty));
		}

		return OptionalProperty;
	}

	bool InvokeObserveState(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* ObserveStateFunction,
		const FOptionalPropertyBindingScenario& Scenario,
		int32& OutState)
	{
		FIntProperty* ReturnProperty = FindFProperty<FIntProperty>(ObserveStateFunction, TEXT("ReturnValue"));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should expose reflected ObserveState return value"), Scenario.Context),
				ReturnProperty))
		{
			return false;
		}

		FStructOnScope Params(ObserveStateFunction);
		void* ParamsMemory = Params.GetStructMemory();
		if (!Test.TestNotNull(TEXT("Optional property reflection scenario should allocate ObserveState params"), ParamsMemory))
		{
			return false;
		}

		if (!InvokeGeneratedFunction(Engine, Object, ObserveStateFunction, ParamsMemory))
		{
			return false;
		}

		OutState = ReturnProperty->GetPropertyValue_InContainer(ParamsMemory);
		return true;
	}

	bool InvokeVoidFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		const FOptionalPropertyBindingScenario& Scenario,
		const TCHAR* FunctionLabel)
	{
		FStructOnScope Params(Function);
		void* ParamsMemory = Params.GetStructMemory();
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should allocate '%s' params"), Scenario.Context, FunctionLabel),
				ParamsMemory))
		{
			return false;
		}

		return InvokeGeneratedFunction(Engine, Object, Function, ParamsMemory);
	}

	bool VerifyValueWrittenFromScript(
		FAutomationTestBase& Test,
		FOptionalProperty& OptionalProperty,
		void* PropertyStorage,
		const FOptionalPropertyBindingScenario& Scenario)
	{
		const void* ValuePointer = OptionalProperty.GetValuePointerForReadIfSet(PropertyStorage);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should expose a readable value after script writes the property"), Scenario.Context),
				ValuePointer))
		{
			return false;
		}

		if (Scenario.ValueKind == EOptionalValueKind::Int)
		{
			const int32* StoredIntValue = static_cast<const int32*>(ValuePointer);
			if (!Test.TestNotNull(TEXT("TOptional<int> property reflection scenario should expose typed inner int storage after script writes"), StoredIntValue))
			{
				return false;
			}

			Test.TestEqual(TEXT("TOptional<int> property reflection scenario should let script writes update reflected native storage"), *StoredIntValue, Scenario.ScriptIntValue);
			return true;
		}

		const FName* StoredNameValue = static_cast<const FName*>(ValuePointer);
		if (!Test.TestNotNull(TEXT("TOptional<FName> property reflection scenario should expose typed inner name storage after script writes"), StoredNameValue))
		{
			return false;
		}

		Test.TestEqual(TEXT("TOptional<FName> property reflection scenario should let script writes update reflected native storage"), *StoredNameValue, Scenario.ScriptNameValue);
		return true;
	}

	bool VerifyRuntimeRoundtrip(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UClass* ScriptClass,
		FOptionalProperty& OptionalProperty,
		const FOptionalPropertyBindingScenario& Scenario)
	{
		UFunction* ObserveStateFunction = RequireGeneratedFunction(Test, ScriptClass, ObserveStateFunctionName, Scenario);
		UFunction* SetValueFromScriptFunction = RequireGeneratedFunction(Test, ScriptClass, SetValueFromScriptFunctionName, Scenario);
		UFunction* ResetValueFromScriptFunction = RequireGeneratedFunction(Test, ScriptClass, ResetValueFromScriptFunctionName, Scenario);
		if (ObserveStateFunction == nullptr || SetValueFromScriptFunction == nullptr || ResetValueFromScriptFunction == nullptr)
		{
			return false;
		}

		UObject* Instance = NewObject<UObject>(GetTransientPackage(), ScriptClass, *FString::Printf(TEXT("%sInstance"), *Scenario.GeneratedClassName.ToString()));
		if (!Test.TestNotNull(TEXT("Optional property reflection scenario should instantiate the generated UObject"), Instance))
		{
			return false;
		}

		void* PropertyStorage = OptionalProperty.ContainerPtrToValuePtr<void>(Instance);
		if (!Test.TestNotNull(TEXT("Optional property reflection scenario should expose reflected property storage"), PropertyStorage))
		{
			return false;
		}

		Test.TestFalse(TEXT("Optional property reflection scenario should default the reflected property to unset"), OptionalProperty.IsSet(PropertyStorage));
		Test.TestNull(TEXT("Optional property reflection scenario should report null value storage for an unset optional"), OptionalProperty.GetValuePointerForReadIfSet(PropertyStorage));

		int32 ObservedState = UnexpectedStateCode;
		if (!InvokeObserveState(Test, Engine, Instance, ObserveStateFunction, Scenario, ObservedState))
		{
			return false;
		}

		if (!Test.TestEqual(TEXT("Optional property reflection scenario should report an unset state before any writes"), ObservedState, UnsetStateCode))
		{
			return false;
		}

		void* NativeValuePointer = OptionalProperty.MarkSetAndGetInitializedValuePointerToReplace(PropertyStorage);
		if (!Test.TestNotNull(TEXT("Optional property reflection scenario should provide writable value storage when set from native reflection"), NativeValuePointer))
		{
			return false;
		}

		if (Scenario.ValueKind == EOptionalValueKind::Int)
		{
			int32* InnerIntValue = static_cast<int32*>(NativeValuePointer);
			if (!Test.TestNotNull(TEXT("TOptional<int> property reflection scenario should expose typed writable int storage for native reflection writes"), InnerIntValue))
			{
				return false;
			}

			*InnerIntValue = Scenario.NativeIntValue;
		}
		else
		{
			FName* InnerNameValue = static_cast<FName*>(NativeValuePointer);
			if (!Test.TestNotNull(TEXT("TOptional<FName> property reflection scenario should expose typed writable name storage for native reflection writes"), InnerNameValue))
			{
				return false;
			}

			*InnerNameValue = Scenario.NativeNameValue;
		}

		Test.TestTrue(TEXT("Optional property reflection scenario should mark the reflected property as set after native reflected writes"), OptionalProperty.IsSet(PropertyStorage));

		if (Scenario.bExpectScriptReadbackAfterNativeReflectedWrite)
		{
			if (!InvokeObserveState(Test, Engine, Instance, ObserveStateFunction, Scenario, ObservedState))
			{
				return false;
			}

			if (!Test.TestEqual(TEXT("Optional property reflection scenario should let script observe values written through reflected property access"), ObservedState, Scenario.NativeObservedState))
			{
				return false;
			}
		}

		if (!InvokeVoidFunction(Test, Engine, Instance, SetValueFromScriptFunction, Scenario, TEXT("SetValueFromScript")))
		{
			return false;
		}

		if (!Test.TestTrue(TEXT("Optional property reflection scenario should stay set after script writes the property"), OptionalProperty.IsSet(PropertyStorage)))
		{
			return false;
		}

		if (!VerifyValueWrittenFromScript(Test, OptionalProperty, PropertyStorage, Scenario))
		{
			return false;
		}

		if (!InvokeObserveState(Test, Engine, Instance, ObserveStateFunction, Scenario, ObservedState))
		{
			return false;
		}

		if (!Test.TestEqual(TEXT("Optional property reflection scenario should let script observe the updated script-authored value"), ObservedState, Scenario.ScriptObservedState))
		{
			return false;
		}

		if (!InvokeVoidFunction(Test, Engine, Instance, ResetValueFromScriptFunction, Scenario, TEXT("ResetValueFromScript")))
		{
			return false;
		}

		Test.TestFalse(TEXT("Optional property reflection scenario should let script Reset() clear the reflected native storage"), OptionalProperty.IsSet(PropertyStorage));
		Test.TestNull(TEXT("Optional property reflection scenario should report null value storage after script Reset()"), OptionalProperty.GetValuePointerForReadIfSet(PropertyStorage));

		if (!InvokeObserveState(Test, Engine, Instance, ObserveStateFunction, Scenario, ObservedState))
		{
			return false;
		}

		Test.TestEqual(TEXT("Optional property reflection scenario should report an unset state after script Reset()"), ObservedState, UnsetStateCode);
		return true;
	}

	bool RunScenario(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FOptionalPropertyBindingScenario& Scenario)
	{
		UClass* ScriptClass = CompileCarrierClass(Test, Engine, Scenario);
		if (ScriptClass == nullptr)
		{
			return false;
		}

		FOptionalProperty* OptionalProperty = ValidateReflectedProperty(Test, ScriptClass, Scenario);
		if (OptionalProperty == nullptr)
		{
			return false;
		}

		return VerifyRuntimeRoundtrip(Test, Engine, ScriptClass, *OptionalProperty, Scenario);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptOptionalIntPropertyReflectionTest,
	"Angelscript.TestModule.Bindings.OptionalProperty.IntReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptOptionalIntPropertyReflectionTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*OptionalPropertyBindingsTests::IntScenario.ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	if (!OptionalPropertyBindingsTests::RunScenario(*this, Engine, OptionalPropertyBindingsTests::IntScenario))
	{
		return false;
	}

	ASTEST_END_SHARE_FRESH
	return true;
}

#endif
