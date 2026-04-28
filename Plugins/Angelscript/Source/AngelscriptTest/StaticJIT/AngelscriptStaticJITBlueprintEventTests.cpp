#include "Misc/AutomationTest.h"

#include "Shared/AngelscriptScenarioTestUtils.h"
#include "Shared/AngelscriptTestEngineHelper.h"

#include "ClassGenerator/ASClass.h"

#include "StaticJIT/StaticJITConfig.h"

#include "Blueprint/BlueprintSupport.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS && AS_CAN_GENERATE_JIT

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace AngelscriptTest_StaticJIT_AngelscriptStaticJITBlueprintEventTests_Private
{
	constexpr TCHAR BaselineFilename[] = TEXT("StaticJITBlueprintEventVmBaseline.as");
	constexpr TCHAR JitFilename[] = TEXT("StaticJITBlueprintEventJit.as");
	const FName BaselineModuleName(TEXT("StaticJITBlueprintEventVmBaseline"));
	const FName JitModuleName(TEXT("StaticJITBlueprintEventJit"));
	const FName GeneratedClassName(TEXT("UStaticJITBlueprintEventCarrier"));
	const FName EventFunctionName(TEXT("ComputeScore"));
	const FName EntryFunctionName(TEXT("CallComputeScore"));
	constexpr int32 InputValue = 7;
	constexpr int32 BlueprintOverrideDelta = 100;
	constexpr int32 ExpectedOverrideResult = InputValue + BlueprintOverrideDelta;

	struct FIntInputAndReturnValue
	{
		int32 Input = 0;
		int32 ReturnValue = 0;
	};

	struct FScenarioExecutionResult
	{
		int32 Result = INDEX_NONE;
		bool bUsesJitDispatch = false;
		FString FunctionClassName;
		FString GeneratedSource;
	};

	FString MakeScriptSource()
	{
		return TEXT(R"AS(
UCLASS()
class UStaticJITBlueprintEventCarrier : UObject
{
	UFUNCTION(BlueprintEvent)
	int ComputeScore(int Input)
	{
		return Input + 10;
	}

	UFUNCTION()
	int CallComputeScore(int Input)
	{
		return ComputeScore(Input);
	}
}
)AS");
	}

	void CleanupBlueprint(UBlueprint*& Blueprint)
	{
		if (Blueprint == nullptr)
		{
			return;
		}

		if (UClass* BlueprintClass = Blueprint->GeneratedClass)
		{
			BlueprintClass->MarkAsGarbage();
		}

		if (UPackage* BlueprintPackage = Blueprint->GetOutermost())
		{
			BlueprintPackage->MarkAsGarbage();
		}

		Blueprint->MarkAsGarbage();
		CollectGarbage(RF_NoFlags, true);
		Blueprint = nullptr;
	}

	UBlueprint* CreateTransientBlueprintChild(
		FAutomationTestBase& Test,
		UClass* ParentClass,
		FStringView Suffix,
		const TCHAR* CallingContext)
	{
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should receive a valid script parent class before creating a transient Blueprint child"), ParentClass))
		{
			return nullptr;
		}

		const FString PackagePath = FString::Printf(
			TEXT("/Temp/StaticJITBlueprintEvent_%.*s_%s"),
			Suffix.Len(),
			Suffix.GetData(),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		UPackage* BlueprintPackage = CreatePackage(*PackagePath);
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should create a transient Blueprint package"), BlueprintPackage))
		{
			return nullptr;
		}

		BlueprintPackage->SetFlags(RF_Transient);
		const FName BlueprintName(*FPackageName::GetLongPackageAssetName(PackagePath));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			BlueprintPackage,
			BlueprintName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			CallingContext);
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should create a transient Blueprint child"), Blueprint))
		{
			return nullptr;
		}

		return Blueprint;
	}

	bool ConnectPins(
		FAutomationTestBase& Test,
		const UEdGraphSchema_K2& Schema,
		UEdGraphPin* SourcePin,
		UEdGraphPin* TargetPin,
		const TCHAR* Context)
	{
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should expose the source pin"), Context), SourcePin)
			|| !Test.TestNotNull(*FString::Printf(TEXT("%s should expose the target pin"), Context), TargetPin))
		{
			return false;
		}

		return Test.TestTrue(
			*FString::Printf(TEXT("%s should connect the Blueprint graph pins"), Context),
			Schema.TryCreateConnection(SourcePin, TargetPin));
	}

	bool CreateTransientBlueprintIntReturnOverride(
		FAutomationTestBase& Test,
		UClass& ParentClass,
		UBlueprint& Blueprint,
		FName FunctionName,
		int32 AddedValue)
	{
		UFunction* OverrideFunction = nullptr;
		UClass* const OverrideFunctionClass = FBlueprintEditorUtils::GetOverrideFunctionClass(&Blueprint, FunctionName, &OverrideFunction);
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should resolve an override class for the BlueprintEvent function"), OverrideFunctionClass)
			|| !Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should resolve the overridable BlueprintEvent function"), OverrideFunction))
		{
			return false;
		}

		UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(&Blueprint, FunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should create an override graph for the Blueprint child"), FunctionGraph))
		{
			return false;
		}

		FBlueprintEditorUtils::AddFunctionGraph(&Blueprint, FunctionGraph, /*bIsUserCreated=*/false, OverrideFunctionClass);

		TArray<UK2Node_FunctionEntry*> EntryNodes;
		FunctionGraph->GetNodesOfClass(EntryNodes);
		UK2Node_FunctionEntry* EntryNode = EntryNodes.Num() > 0 ? EntryNodes[0] : nullptr;

		TArray<UK2Node_FunctionResult*> ResultNodes;
		FunctionGraph->GetNodesOfClass(ResultNodes);
		UK2Node_FunctionResult* ResultNode = ResultNodes.Num() > 0 ? ResultNodes[0] : nullptr;

		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should create a function entry node for the Blueprint override"), EntryNode)
			|| !Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should create a function result node for the Blueprint override"), ResultNode))
		{
			return false;
		}

		UK2Node_CallFunction* AddNode = NewObject<UK2Node_CallFunction>(FunctionGraph);
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should allocate a math node for the Blueprint override graph"), AddNode))
		{
			return false;
		}

		FunctionGraph->AddNode(AddNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
		AddNode->CreateNewGuid();
		AddNode->PostPlacedNewNode();
		AddNode->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt)));
		AddNode->AllocateDefaultPins();
		AddNode->NodePosX = 320;
		AddNode->NodePosY = 0;

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should resolve the K2 schema"), Schema))
		{
			return false;
		}

		UEdGraphPin* EntryThenPin = EntryNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* EntryInputPin = EntryNode->FindPin(TEXT("Input"));
		UEdGraphPin* ResultExecutePin = ResultNode->FindPin(UEdGraphSchema_K2::PN_Execute);
		UEdGraphPin* ResultValuePin = ResultNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
		UEdGraphPin* AddAPin = AddNode->FindPin(TEXT("A"));
		UEdGraphPin* AddBPin = AddNode->FindPin(TEXT("B"));
		UEdGraphPin* AddReturnPin = AddNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue);

		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should expose the Blueprint override input pin"), EntryInputPin)
			|| !Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should expose the Blueprint override return pin"), ResultValuePin)
			|| !Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should expose the Add_IntInt input A pin"), AddAPin)
			|| !Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should expose the Add_IntInt input B pin"), AddBPin)
			|| !Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should expose the Add_IntInt return pin"), AddReturnPin))
		{
			return false;
		}

		AddBPin->DefaultValue = LexToString(AddedValue);

		return ConnectPins(Test, *Schema, EntryThenPin, ResultExecutePin, TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall Blueprint override exec flow"))
			&& ConnectPins(Test, *Schema, EntryInputPin, AddAPin, TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall Blueprint override input flow"))
			&& ConnectPins(Test, *Schema, AddReturnPin, ResultValuePin, TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall Blueprint override return flow"))
			&& [&Test, &Blueprint]()
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
				FKismetEditorUtilities::CompileBlueprint(&Blueprint);
				return Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should compile the transient Blueprint override"), Blueprint.GeneratedClass.Get());
			}();
	}

	bool ExecuteGeneratedIntMethodOnGameThread(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		int32 Input,
		int32& OutResult,
		const TCHAR* Context)
	{
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should have a valid runtime object"), Context), Object)
			|| !Test.TestNotNull(*FString::Printf(TEXT("%s should expose the generated function"), Context), Function))
		{
			return false;
		}

		auto Invoke = [&Engine, Object, Function, Input, &OutResult]()
		{
			FIntInputAndReturnValue Params;
			Params.Input = Input;

			FAngelscriptEngineScope EngineScope(Engine, Object);
			Object->ProcessEvent(Function, &Params);
			OutResult = Params.ReturnValue;
		};

		if (IsInGameThread())
		{
			Invoke();
			return true;
		}

		FEvent* CompletedEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [Invoke, CompletedEvent]() mutable
		{
			Invoke();
			CompletedEvent->Trigger();
		});

		CompletedEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CompletedEvent);
		return true;
	}

	bool UsesJitDispatchClass(const UFunction& Function)
	{
		const UASFunction* ScriptFunction = Cast<UASFunction>(&Function);
		return ScriptFunction != nullptr
			&& ScriptFunction->JitFunction_Raw != nullptr
			&& Function.GetClass()->GetName().EndsWith(TEXT("_JIT"));
	}

	bool RunBlueprintEventScenario(
		FAutomationTestBase& Test,
		bool bGeneratePrecompiledData,
		FName ModuleName,
		const FString& Filename,
		FStringView BlueprintSuffix,
		FScenarioExecutionResult& OutResult)
	{
		FAngelscriptEngineConfig Config;
		Config.bGeneratePrecompiledData = bGeneratePrecompiledData;

		FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		TUniquePtr<FAngelscriptEngine> OwnedEngine = FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should create a dedicated full engine for the scenario"), OwnedEngine.Get()))
		{
			return false;
		}

		FAngelscriptEngine& Engine = *OwnedEngine;
		ON_SCOPE_EXIT
		{
			Engine.DiscardModule(*ModuleName.ToString());
		};

		UClass* ScriptParentClass = CompileScriptModule(
			Test,
			Engine,
			ModuleName,
			Filename,
			MakeScriptSource(),
			GeneratedClassName);
		if (ScriptParentClass == nullptr)
		{
			return false;
		}

		UBlueprint* Blueprint = CreateTransientBlueprintChild(
			Test,
			ScriptParentClass,
			BlueprintSuffix,
			TEXT("AngelscriptStaticJITBlueprintEventTests"));
		ON_SCOPE_EXIT
		{
			CleanupBlueprint(Blueprint);
		};

		if (Blueprint == nullptr || !CreateTransientBlueprintIntReturnOverride(Test, *ScriptParentClass, *Blueprint, EventFunctionName, BlueprintOverrideDelta))
		{
			return false;
		}

		UClass* BlueprintClass = Blueprint->GeneratedClass.Get();
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should materialize a generated Blueprint child class"), BlueprintClass))
		{
			return false;
		}

		UFunction* CallComputeScoreFunction = FindGeneratedFunction(BlueprintClass, EntryFunctionName);
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should resolve the generated entry function on the Blueprint child"), CallComputeScoreFunction))
		{
			return false;
		}

		OutResult.FunctionClassName = GetNameSafe(CallComputeScoreFunction->GetClass());
		OutResult.bUsesJitDispatch = UsesJitDispatchClass(*CallComputeScoreFunction);

		UObject* RuntimeObject = NewObject<UObject>(GetTransientPackage(), BlueprintClass, NAME_None, RF_Transient);
		if (!Test.TestNotNull(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should instantiate the transient Blueprint child"), RuntimeObject))
		{
			return false;
		}

		if (!ExecuteGeneratedIntMethodOnGameThread(
				Test,
				Engine,
				RuntimeObject,
				CallComputeScoreFunction,
				InputValue,
				OutResult.Result,
				TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall runtime invocation")))
		{
			return false;
		}

		if (bGeneratePrecompiledData)
		{
			FString GenerateError;
			const bool bGenerated = GenerateStaticJITSourceText(
				&Engine,
				ModuleName,
				OutResult.GeneratedSource,
				/*bEmitDebugMetadata=*/false,
				&GenerateError);
			if (!Test.TestTrue(TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should generate StaticJIT source text for the JIT scenario"), bGenerated))
			{
				if (!GenerateError.IsEmpty())
				{
					Test.AddError(GenerateError);
				}
				return false;
			}
		}

		return true;
	}
}

using namespace AngelscriptTest_StaticJIT_AngelscriptStaticJITBlueprintEventTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITBlueprintEventProcessEventCustomCallTest,
	"Angelscript.TestModule.StaticJIT.BlueprintEvent.ProcessEventCustomCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptStaticJITBlueprintEventProcessEventCustomCallTest::RunTest(const FString& Parameters)
{
	FScenarioExecutionResult BaselineResult;
	if (!RunBlueprintEventScenario(
			*this,
			/*bGeneratePrecompiledData=*/false,
			BaselineModuleName,
			BaselineFilename,
			TEXT("VmBaseline"),
			BaselineResult))
	{
		return false;
	}

	if (!TestFalse(
			TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should keep the VM baseline on a non-JIT dispatch class"),
			BaselineResult.bUsesJitDispatch))
	{
		AddInfo(FString::Printf(TEXT("Observed VM baseline function class: %s"), *BaselineResult.FunctionClassName));
		return false;
	}

	if (!TestEqual(
			TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should let the Blueprint child override change the VM baseline result"),
			BaselineResult.Result,
			ExpectedOverrideResult))
	{
		return false;
	}

	FScenarioExecutionResult JitResult;
	if (!RunBlueprintEventScenario(
			*this,
			/*bGeneratePrecompiledData=*/true,
			JitModuleName,
			JitFilename,
			TEXT("Jit"),
			JitResult))
	{
		return false;
	}

#ifdef AS_SKIP_JITTED_CODE
	if (!TestFalse(
			TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should keep editor test builds on the non-JIT runtime dispatch path when AS_SKIP_JITTED_CODE is defined"),
			JitResult.bUsesJitDispatch))
	{
		AddInfo(FString::Printf(TEXT("Observed editor-build function class: %s"), *JitResult.FunctionClassName));
		return false;
	}
#else
	if (!TestTrue(
			TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should route the JIT scenario through a JIT dispatch class"),
			JitResult.bUsesJitDispatch))
	{
		AddInfo(FString::Printf(TEXT("Observed JIT scenario function class: %s"), *JitResult.FunctionClassName));
		return false;
	}
#endif

	if (!TestEqual(
			TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should preserve the Blueprint child override result under StaticJIT execution"),
			JitResult.Result,
			ExpectedOverrideResult))
	{
		return false;
	}

	if (!TestEqual(
			TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should keep VM and JIT results identical for the Blueprint override"),
			JitResult.Result,
			BaselineResult.Result))
	{
		return false;
	}

	if (!TestTrue(
			TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should emit FindFunctionChecked in the generated StaticJIT custom call"),
			JitResult.GeneratedSource.Contains(TEXT("FindFunctionChecked")))
		|| !TestTrue(
			TEXT("StaticJIT.BlueprintEvent.ProcessEventCustomCall should emit ProcessEvent(EventFunction, &l_ParmStruct[0]) in the generated StaticJIT custom call"),
			JitResult.GeneratedSource.Contains(TEXT("ProcessEvent(EventFunction, &l_ParmStruct[0]);"))))
	{
		return false;
	}

	return true;
}

#endif
