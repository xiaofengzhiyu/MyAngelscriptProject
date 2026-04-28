#include "../Shared/AngelscriptScenarioTestUtils.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace ObjectPropertyBindingsTests
{
	static const FName StoredTexturePropertyName(TEXT("StoredTexture"));
	static const FName SetterFunctionName(TEXT("SetStoredTexture"));
	static const FName GetterFunctionName(TEXT("GetStoredTexture"));

	struct FPropertyBindingScenario
	{
		FName ModuleName;
		FString ScriptFilename;
		FName GeneratedClassName;
		FString ScriptPropertyType;
		bool bExpectWeakProperty = false;
		const TCHAR* Context = TEXT("Object property binding scenario");
	};

	static const FPropertyBindingScenario ObjectPtrScenario
	{
		TEXT("ASObjectPtrPropertyReflection"),
		TEXT("ASObjectPtrPropertyReflection.as"),
		TEXT("UBindingObjectPtrPropertyCarrier"),
		TEXT("TObjectPtr<UTexture2D>"),
		false,
		TEXT("TObjectPtr property reflection scenario"),
	};

	static const FPropertyBindingScenario WeakObjectPtrScenario
	{
		TEXT("ASWeakObjectPtrPropertyReflection"),
		TEXT("ASWeakObjectPtrPropertyReflection.as"),
		TEXT("UBindingWeakObjectPtrPropertyCarrier"),
		TEXT("TWeakObjectPtr<UTexture2D>"),
		true,
		TEXT("TWeakObjectPtr property reflection scenario"),
	};

	FString BuildScriptSource(const FPropertyBindingScenario& Scenario)
	{
		return FString::Printf(TEXT(R"AS(
UCLASS()
class %s : UObject
{
	UPROPERTY()
	%s StoredTexture;

	UFUNCTION()
	void SetStoredTexture(UTexture2D InTexture)
	{
		StoredTexture = InTexture;
	}

	UFUNCTION()
	UTexture2D GetStoredTexture() const
	{
		return StoredTexture.Get();
	}
}
)AS"),
			*Scenario.GeneratedClassName.ToString(),
			*Scenario.ScriptPropertyType);
	}

	UClass* CompileCarrierClass(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FPropertyBindingScenario& Scenario)
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
		const FPropertyBindingScenario& Scenario)
	{
		UFunction* Function = FindGeneratedFunction(OwnerClass, FunctionName);
		Test.TestNotNull(
			*FString::Printf(TEXT("%s should expose generated function '%s'"), Scenario.Context, *FunctionName.ToString()),
			Function);
		return Function;
	}

	FObjectPropertyBase* ValidateReflectedProperty(
		FAutomationTestBase& Test,
		UClass* ScriptClass,
		const FPropertyBindingScenario& Scenario)
	{
		FProperty* Property = FindFProperty<FProperty>(ScriptClass, StoredTexturePropertyName);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should generate reflected property '%s'"), Scenario.Context, *StoredTexturePropertyName.ToString()),
				Property))
		{
			return nullptr;
		}

		FObjectPropertyBase* ObjectProperty = nullptr;
		if (Scenario.bExpectWeakProperty)
		{
			ObjectProperty = CastField<FWeakObjectProperty>(Property);
			if (!Test.TestNotNull(TEXT("Weak object property scenario should materialize StoredTexture as FWeakObjectProperty"), ObjectProperty))
			{
				return nullptr;
			}
		}
		else
		{
			ObjectProperty = CastField<FObjectProperty>(Property);
			if (!Test.TestNotNull(TEXT("Strong object property scenario should materialize StoredTexture as FObjectProperty"), ObjectProperty))
			{
				return nullptr;
			}
		}

		Test.TestFalse(TEXT("Object property reflection scenario should not route the reflected field through CPF_UObjectWrapper"), Property->HasAnyPropertyFlags(CPF_UObjectWrapper));
		Test.TestTrue(TEXT("Object property reflection scenario should preserve UTexture2D as the property class"), ObjectProperty->PropertyClass.Get() == UTexture2D::StaticClass());
		return ObjectProperty;
	}

	bool InvokeSetter(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* SetterFunction,
		UTexture2D* Texture,
		const FPropertyBindingScenario& Scenario)
	{
		FObjectPropertyBase* InputProperty = FindFProperty<FObjectPropertyBase>(SetterFunction, TEXT("InTexture"));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should expose reflected setter parameter 'InTexture'"), Scenario.Context),
				InputProperty))
		{
			return false;
		}

		FStructOnScope Params(SetterFunction);
		void* ParamsMemory = Params.GetStructMemory();
		if (!Test.TestNotNull(TEXT("Object property reflection scenario should allocate setter params"), ParamsMemory))
		{
			return false;
		}

		InputProperty->SetObjectPropertyValue_InContainer(ParamsMemory, Texture);

		FAngelscriptEngineScope FunctionScope(Engine, Object);
		Object->ProcessEvent(SetterFunction, ParamsMemory);
		return true;
	}

	bool InvokeGetter(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* GetterFunction,
		const FPropertyBindingScenario& Scenario,
		UTexture2D*& OutTexture)
	{
		FObjectPropertyBase* ReturnProperty = FindFProperty<FObjectPropertyBase>(GetterFunction, TEXT("ReturnValue"));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should expose reflected getter return value"), Scenario.Context),
				ReturnProperty))
		{
			return false;
		}

		FStructOnScope Params(GetterFunction);
		void* ParamsMemory = Params.GetStructMemory();
		if (!Test.TestNotNull(TEXT("Object property reflection scenario should allocate getter params"), ParamsMemory))
		{
			return false;
		}

		FAngelscriptEngineScope FunctionScope(Engine, Object);
		Object->ProcessEvent(GetterFunction, ParamsMemory);
		OutTexture = Cast<UTexture2D>(ReturnProperty->GetObjectPropertyValue_InContainer(ParamsMemory));
		return true;
	}

	bool VerifyRuntimeRoundtrip(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UClass* ScriptClass,
		FObjectPropertyBase* ReflectedProperty,
		const FPropertyBindingScenario& Scenario)
	{
		UFunction* SetterFunction = RequireGeneratedFunction(Test, ScriptClass, SetterFunctionName, Scenario);
		UFunction* GetterFunction = RequireGeneratedFunction(Test, ScriptClass, GetterFunctionName, Scenario);
		if (SetterFunction == nullptr || GetterFunction == nullptr)
		{
			return false;
		}

		UObject* Instance = NewObject<UObject>(GetTransientPackage(), ScriptClass, *FString::Printf(TEXT("%sInstance"), *Scenario.GeneratedClassName.ToString()));
		if (!Test.TestNotNull(TEXT("Object property reflection scenario should instantiate the generated UObject"), Instance))
		{
			return false;
		}

		Test.TestNull(TEXT("Object property reflection scenario should default the reflected property to null"), ReflectedProperty->GetObjectPropertyValue_InContainer(Instance));

		UTexture2D* FirstTexture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		UTexture2D* SecondTexture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!Test.TestNotNull(TEXT("Object property reflection scenario should create the first transient texture"), FirstTexture)
			|| !Test.TestNotNull(TEXT("Object property reflection scenario should create the second transient texture"), SecondTexture))
		{
			return false;
		}

		ReflectedProperty->SetObjectPropertyValue_InContainer(Instance, FirstTexture);

		UTexture2D* GetterValue = nullptr;
		if (!InvokeGetter(Test, Engine, Instance, GetterFunction, Scenario, GetterValue))
		{
			return false;
		}

		if (!Test.TestTrue(TEXT("Object property reflection scenario should let script getters read values written through reflected property access"), GetterValue == FirstTexture))
		{
			return false;
		}

		if (!InvokeSetter(Test, Engine, Instance, SetterFunction, SecondTexture, Scenario))
		{
			return false;
		}

		if (!Test.TestTrue(TEXT("Object property reflection scenario should let script setters update the reflected property storage"), ReflectedProperty->GetObjectPropertyValue_InContainer(Instance) == SecondTexture))
		{
			return false;
		}

		if (!InvokeSetter(Test, Engine, Instance, SetterFunction, nullptr, Scenario))
		{
			return false;
		}

		Test.TestNull(TEXT("Object property reflection scenario should clear the reflected property through the generated setter"), ReflectedProperty->GetObjectPropertyValue_InContainer(Instance));

		GetterValue = reinterpret_cast<UTexture2D*>(UPTRINT(1));
		if (!InvokeGetter(Test, Engine, Instance, GetterFunction, Scenario, GetterValue))
		{
			return false;
		}

		Test.TestNull(TEXT("Object property reflection scenario should report null back through the generated getter after clearing"), GetterValue);
		return true;
	}

	bool RunScenario(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FPropertyBindingScenario& Scenario)
	{
		UClass* ScriptClass = CompileCarrierClass(Test, Engine, Scenario);
		if (ScriptClass == nullptr)
		{
			return false;
		}

		FObjectPropertyBase* ReflectedProperty = ValidateReflectedProperty(Test, ScriptClass, Scenario);
		if (ReflectedProperty == nullptr)
		{
			return false;
		}

		return VerifyRuntimeRoundtrip(Test, Engine, ScriptClass, ReflectedProperty, Scenario);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptObjectPtrPropertyReflectionTest,
	"Angelscript.TestModule.Bindings.ObjectProperty.TObjectPtrReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptWeakObjectPtrPropertyReflectionTest,
	"Angelscript.TestModule.Bindings.ObjectProperty.TWeakObjectPtrReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptObjectPtrPropertyReflectionTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ObjectPropertyBindingsTests::ObjectPtrScenario.ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	if (!ObjectPropertyBindingsTests::RunScenario(*this, Engine, ObjectPropertyBindingsTests::ObjectPtrScenario))
	{
		return false;
	}

	ASTEST_END_SHARE_FRESH
	return true;
}

bool FAngelscriptWeakObjectPtrPropertyReflectionTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ObjectPropertyBindingsTests::WeakObjectPtrScenario.ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	if (!ObjectPropertyBindingsTests::RunScenario(*this, Engine, ObjectPropertyBindingsTests::WeakObjectPtrScenario))
	{
		return false;
	}

	ASTEST_END_SHARE_FRESH
	return true;
}

#endif
