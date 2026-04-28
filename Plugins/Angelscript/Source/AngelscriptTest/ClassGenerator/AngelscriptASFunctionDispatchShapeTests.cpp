#include "Shared/AngelscriptScenarioTestUtils.h"
#include "Shared/AngelscriptTestMacros.h"

#include "ClassGenerator/ASClass.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace ASFunctionDispatchShapeTests
{
	static const FName ModuleName(TEXT("ASFunctionDispatchShape"));
	static const FString ScriptFilename(TEXT("ASFunctionDispatchShape.as"));
	static const FName GeneratedClassName(TEXT("UDispatchShapeCarrier"));
	static const FName PingCountPropertyName(TEXT("PingCount"));
	static const FName StoredIntPropertyName(TEXT("StoredInt"));
	static const FName StoredDoubleHundredthsPropertyName(TEXT("StoredDoubleHundredths"));
	static const FName ObservedRefValuePropertyName(TEXT("ObservedRefValue"));

	struct FStoreIntParams
	{
		int32 Value = 0;
	};

	struct FStoreDoubleParams
	{
		double Value = 0.0;
	};

	struct FBumpParams
	{
		int32 Value = 0;
	};

	struct FGetSelfParams
	{
		UObject* ReturnValue = nullptr;
	};

	UASClass* CompileDispatchShapeCarrier(FAutomationTestBase& Test, FAngelscriptEngine& Engine)
	{
		const FString ScriptSource = TEXT(R"AS(
UCLASS()
class UDispatchShapeCarrier : UObject
{
	UPROPERTY()
	int PingCount = 0;

	UPROPERTY()
	int StoredInt = 0;

	UPROPERTY()
	int StoredDoubleHundredths = 0;

	UPROPERTY()
	int ObservedRefValue = 0;

	UFUNCTION()
	void Ping()
	{
		PingCount += 1;
	}

	UFUNCTION()
	void StoreInt(int Value)
	{
		StoredInt = Value;
	}

	UFUNCTION()
	void StoreDouble(float64 Value)
	{
		StoredDoubleHundredths = int(Value * 100.0);
	}

	UFUNCTION()
	void Bump(int& Value)
	{
		Value += 5;
		ObservedRefValue = Value;
	}

	UFUNCTION()
	UObject GetSelf()
	{
		return this;
	}
}
)AS");

		UClass* GeneratedClass = CompileScriptModule(
			Test,
			Engine,
			ModuleName,
			ScriptFilename,
			ScriptSource,
			GeneratedClassName);
		if (GeneratedClass == nullptr)
		{
			return nullptr;
		}

		return Cast<UASClass>(GeneratedClass);
	}

	UASFunction* RequireGeneratedScriptFunction(
		FAutomationTestBase& Test,
		UClass* ScriptClass,
		const TCHAR* FunctionName)
	{
		UASFunction* ScriptFunction = Cast<UASFunction>(FindGeneratedFunction(ScriptClass, FunctionName));
		Test.TestNotNull(
			*FString::Printf(TEXT("Dispatch-shape scenario should generate '%s' as a UASFunction"), FunctionName),
			ScriptFunction);
		return ScriptFunction;
	}

	bool ExpectFunctionClassMatchesAnyOf(
		FAutomationTestBase& Test,
		const UFunction& Function,
		const TCHAR* FunctionName,
		UClass* ExpectedClass,
		UClass* ExpectedJitClass)
	{
		const UClass* ActualClass = Function.GetClass();
		return Test.TestTrue(
			*FString::Printf(
				TEXT("Dispatch-shape scenario should route %s through %s or %s (actual: %s)"),
				FunctionName,
				*GetNameSafe(ExpectedClass),
				*GetNameSafe(ExpectedJitClass),
				*GetNameSafe(ActualClass)),
			ActualClass == ExpectedClass || ActualClass == ExpectedJitClass);
	}

	bool ExpectSingleArgumentMetadata(
		FAutomationTestBase& Test,
		UASFunction& Function,
		const TCHAR* FunctionName,
		const TCHAR* PropertyName,
		UASFunction::EArgumentParmBehavior ExpectedParmBehavior,
		UASFunction::EArgumentVMBehavior ExpectedVMBehavior,
		int32 ExpectedValueBytes)
	{
		FProperty* ReflectedProperty = FindFProperty<FProperty>(&Function, PropertyName);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("Dispatch-shape scenario should expose '%s' on %s"), PropertyName, FunctionName),
				ReflectedProperty))
		{
			return false;
		}

		if (!Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should give %s exactly one reflected argument"), FunctionName),
				Function.Arguments.Num(),
				1))
		{
			return false;
		}

		const UASFunction::FArgument& Argument = Function.Arguments[0];
		return Test.TestTrue(
				*FString::Printf(TEXT("Dispatch-shape scenario should bind %s to the reflected %s property"), FunctionName, PropertyName),
				Argument.Property == ReflectedProperty)
			&& Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s at offset 0 in the reflected parm struct"), FunctionName),
				Argument.PosInParmStruct,
				ReflectedProperty->GetOffset_ForUFunction())
			&& Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s using the expected value byte size"), FunctionName),
				Argument.ValueBytes,
				ExpectedValueBytes)
			&& Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s using the expected parameter behavior"), FunctionName),
				static_cast<uint8>(Argument.ParmBehavior),
				static_cast<uint8>(ExpectedParmBehavior))
			&& Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s using the expected VM behavior"), FunctionName),
				static_cast<uint8>(Argument.VMBehavior),
				static_cast<uint8>(ExpectedVMBehavior))
			&& Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should not register destructible arguments for %s"), FunctionName),
				Function.DestroyArguments.Num(),
				0)
			&& Test.TestTrue(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s routed through UASFunctionNativeThunk"), FunctionName),
				Function.GetNativeFunc() == &UASFunctionNativeThunk);
	}

	bool ExpectObjectReturnMetadata(
		FAutomationTestBase& Test,
		UASFunction& Function,
		const TCHAR* FunctionName)
	{
		FProperty* ReturnProperty = FindFProperty<FProperty>(&Function, TEXT("ReturnValue"));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("Dispatch-shape scenario should expose ReturnValue on %s"), FunctionName),
				ReturnProperty))
		{
			return false;
		}

		return Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s without reflected input arguments"), FunctionName),
				Function.Arguments.Num(),
				0)
			&& Test.TestTrue(
				*FString::Printf(TEXT("Dispatch-shape scenario should bind the %s return metadata to the reflected return property"), FunctionName),
				Function.ReturnArgument.Property == ReturnProperty)
			&& Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s return offset aligned with the reflected return property"), FunctionName),
				Function.ReturnArgument.PosInParmStruct,
				ReturnProperty->GetOffset_ForUFunction())
			&& Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s using object-pointer return parameter behavior"), FunctionName),
				static_cast<uint8>(Function.ReturnArgument.ParmBehavior),
				static_cast<uint8>(UASFunction::EArgumentParmBehavior::ReturnObjectPointer))
			&& Test.TestEqual(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s using value8 VM behavior for object returns"), FunctionName),
				static_cast<uint8>(Function.ReturnArgument.VMBehavior),
				static_cast<uint8>(UASFunction::EArgumentVMBehavior::Value8Byte))
			&& Test.TestTrue(
				*FString::Printf(TEXT("Dispatch-shape scenario should keep %s routed through UASFunctionNativeThunk"), FunctionName),
				Function.GetNativeFunc() == &UASFunctionNativeThunk);
	}
}

using namespace ASFunctionDispatchShapeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptASFunctionFinalizeArgumentsSelectsExpectedThunkShapeTest,
	"Angelscript.TestModule.ClassGenerator.ASFunction.FinalizeArgumentsSelectsExpectedThunkShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptASFunctionFinalizeArgumentsSelectsExpectedThunkShapeTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ASFunctionDispatchShapeTests::ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UASClass* ScriptClass = ASFunctionDispatchShapeTests::CompileDispatchShapeCarrier(*this, Engine);
	if (!TestNotNull(TEXT("Dispatch-shape scenario should compile to a UASClass"), ScriptClass))
	{
		return false;
	}

	UASFunction* PingFunction = ASFunctionDispatchShapeTests::RequireGeneratedScriptFunction(*this, ScriptClass, TEXT("Ping"));
	UASFunction* StoreIntFunction = ASFunctionDispatchShapeTests::RequireGeneratedScriptFunction(*this, ScriptClass, TEXT("StoreInt"));
	UASFunction* StoreDoubleFunction = ASFunctionDispatchShapeTests::RequireGeneratedScriptFunction(*this, ScriptClass, TEXT("StoreDouble"));
	UASFunction* BumpFunction = ASFunctionDispatchShapeTests::RequireGeneratedScriptFunction(*this, ScriptClass, TEXT("Bump"));
	UASFunction* GetSelfFunction = ASFunctionDispatchShapeTests::RequireGeneratedScriptFunction(*this, ScriptClass, TEXT("GetSelf"));
	if (PingFunction == nullptr
		|| StoreIntFunction == nullptr
		|| StoreDoubleFunction == nullptr
		|| BumpFunction == nullptr
		|| GetSelfFunction == nullptr)
	{
		return false;
	}

	if (!ASFunctionDispatchShapeTests::ExpectFunctionClassMatchesAnyOf(
			*this,
			*PingFunction,
			TEXT("Ping"),
			UASFunction_NoParams::StaticClass(),
			UASFunction_NoParams_JIT::StaticClass())
		|| !ASFunctionDispatchShapeTests::ExpectFunctionClassMatchesAnyOf(
			*this,
			*StoreIntFunction,
			TEXT("StoreInt"),
			UASFunction_DWordArg::StaticClass(),
			UASFunction_DWordArg_JIT::StaticClass())
		|| !ASFunctionDispatchShapeTests::ExpectFunctionClassMatchesAnyOf(
			*this,
			*StoreDoubleFunction,
			TEXT("StoreDouble"),
			UASFunction_DoubleArg::StaticClass(),
			UASFunction_DoubleArg_JIT::StaticClass())
		|| !ASFunctionDispatchShapeTests::ExpectFunctionClassMatchesAnyOf(
			*this,
			*BumpFunction,
			TEXT("Bump"),
			UASFunction_ReferenceArg::StaticClass(),
			UASFunction_ReferenceArg_JIT::StaticClass())
		|| !ASFunctionDispatchShapeTests::ExpectFunctionClassMatchesAnyOf(
			*this,
			*GetSelfFunction,
			TEXT("GetSelf"),
			UASFunction_ObjectReturn::StaticClass(),
			UASFunction_ObjectReturn_JIT::StaticClass()))
	{
		return false;
	}

	if (!TestEqual(TEXT("Dispatch-shape scenario should give Ping no reflected arguments"), PingFunction->Arguments.Num(), 0)
		|| !TestEqual(TEXT("Dispatch-shape scenario should keep Ping argument stack size at zero"), PingFunction->ArgStackSize, 0)
		|| !TestEqual(TEXT("Dispatch-shape scenario should keep Ping without destructible arguments"), PingFunction->DestroyArguments.Num(), 0)
		|| !TestEqual(
			TEXT("Dispatch-shape scenario should keep Ping return VM behavior as None"),
			static_cast<uint8>(PingFunction->ReturnArgument.VMBehavior),
			static_cast<uint8>(UASFunction::EArgumentVMBehavior::None))
		|| !TestTrue(TEXT("Dispatch-shape scenario should keep Ping routed through UASFunctionNativeThunk"), PingFunction->GetNativeFunc() == &UASFunctionNativeThunk)
		|| !ASFunctionDispatchShapeTests::ExpectSingleArgumentMetadata(
			*this,
			*StoreIntFunction,
			TEXT("StoreInt"),
			TEXT("Value"),
			UASFunction::EArgumentParmBehavior::Value4Byte,
			UASFunction::EArgumentVMBehavior::Value4Byte,
			sizeof(int32))
		|| !ASFunctionDispatchShapeTests::ExpectSingleArgumentMetadata(
			*this,
			*StoreDoubleFunction,
			TEXT("StoreDouble"),
			TEXT("Value"),
			UASFunction::EArgumentParmBehavior::Value8Byte,
			UASFunction::EArgumentVMBehavior::Value8Byte,
			sizeof(double))
		|| !ASFunctionDispatchShapeTests::ExpectSingleArgumentMetadata(
			*this,
			*BumpFunction,
			TEXT("Bump"),
			TEXT("Value"),
			UASFunction::EArgumentParmBehavior::Reference,
			UASFunction::EArgumentVMBehavior::ReferencePOD,
			sizeof(int32))
		|| !ASFunctionDispatchShapeTests::ExpectObjectReturnMetadata(*this, *GetSelfFunction, TEXT("GetSelf")))
	{
		return false;
	}

	UObject* Instance = NewObject<UObject>(GetTransientPackage(), ScriptClass, TEXT("DispatchShapeCarrierInstance"));
	if (!TestNotNull(TEXT("Dispatch-shape scenario should instantiate the generated UObject"), Instance))
	{
		return false;
	}

	PingFunction->RuntimeCallEvent(Instance, nullptr);

	int32 PingCount = INDEX_NONE;
	if (!ReadPropertyValue<FIntProperty>(*this, Instance, ASFunctionDispatchShapeTests::PingCountPropertyName, PingCount)
		|| !TestEqual(TEXT("Dispatch-shape scenario should execute the no-params thunk through RuntimeCallEvent"), PingCount, 1))
	{
		return false;
	}

	FStoreIntParams StoreIntParams;
	StoreIntParams.Value = 17;
	StoreIntFunction->RuntimeCallEvent(Instance, &StoreIntParams);

	int32 StoredInt = INDEX_NONE;
	if (!ReadPropertyValue<FIntProperty>(*this, Instance, ASFunctionDispatchShapeTests::StoredIntPropertyName, StoredInt)
		|| !TestEqual(TEXT("Dispatch-shape scenario should keep the DWord argument value intact through RuntimeCallEvent"), StoredInt, 17)
		|| !TestEqual(TEXT("Dispatch-shape scenario should not mutate value arguments while dispatching StoreInt"), StoreIntParams.Value, 17))
	{
		return false;
	}

	FStoreDoubleParams StoreDoubleParams;
	StoreDoubleParams.Value = 4.25;
	StoreDoubleFunction->RuntimeCallEvent(Instance, &StoreDoubleParams);

	int32 StoredDoubleHundredths = INDEX_NONE;
	if (!ReadPropertyValue<FIntProperty>(*this, Instance, ASFunctionDispatchShapeTests::StoredDoubleHundredthsPropertyName, StoredDoubleHundredths)
		|| !TestEqual(TEXT("Dispatch-shape scenario should keep the double argument value intact through RuntimeCallEvent"), StoredDoubleHundredths, 425))
	{
		return false;
	}

	FBumpParams BumpParams;
	BumpParams.Value = 6;
	BumpFunction->RuntimeCallEvent(Instance, &BumpParams);

	int32 ObservedRefValue = INDEX_NONE;
	if (!TestEqual(TEXT("Dispatch-shape scenario should write reference arguments back through RuntimeCallEvent"), BumpParams.Value, 11)
		|| !ReadPropertyValue<FIntProperty>(*this, Instance, ASFunctionDispatchShapeTests::ObservedRefValuePropertyName, ObservedRefValue)
		|| !TestEqual(TEXT("Dispatch-shape scenario should expose the post-bump reference value inside script state"), ObservedRefValue, 11))
	{
		return false;
	}

	FGetSelfParams GetSelfParams;
	GetSelfFunction->RuntimeCallEvent(Instance, &GetSelfParams);
	if (!TestTrue(TEXT("Dispatch-shape scenario should keep object-return thunks writing the instance back to the return slot"), GetSelfParams.ReturnValue == Instance))
	{
		return false;
	}

	ASTEST_END_SHARE_FRESH
	return true;
}

#endif
