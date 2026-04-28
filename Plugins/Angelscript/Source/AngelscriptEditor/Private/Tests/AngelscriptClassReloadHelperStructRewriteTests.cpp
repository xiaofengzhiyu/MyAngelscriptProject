#include "ClassReloadHelper.h"

#include "AngelscriptEngine.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptClassReloadHelperPerformReinstanceStructRewriteTest,
	"Angelscript.TestModule.Editor.ClassReloadHelper.PerformReinstanceRewritesStructReferencesAcrossPinsVariablesAndMacroWildcards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptEditor_Private_Tests_AngelscriptClassReloadHelperStructRewriteTests_Private
{
	struct FPerformReinstanceStructRewriteCallLog
	{
		int32 FlushCompilationQueueAndReinstanceCalls = 0;
		TArray<TWeakObjectPtr<UBlueprint>> QueuedBlueprints;
	};

	TUniquePtr<FAngelscriptEngine> MakeClassReloadHelperStructRewriteTestEngine()
	{
		const FAngelscriptEngineConfig Config;
		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		return FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
	}

	void EnsureClassReloadHelperInitialized()
	{
		if (!FAngelscriptClassGenerator::OnClassReload.IsBound())
		{
			FClassReloadHelper::Init();
		}
	}

	void RootObject(TArray<UObject*>& RootedObjects, UObject* Object)
	{
		if (Object == nullptr || RootedObjects.Contains(Object))
		{
			return;
		}

		Object->AddToRoot();
		RootedObjects.Add(Object);
	}

	UBlueprint* CreateTransientBlueprintChild(FAutomationTestBase& Test, UClass* ParentClass, FStringView Suffix, TArray<UObject*>& RootedObjects)
	{
		const FString PackagePath = FString::Printf(
			TEXT("/Temp/AngelscriptClassReloadHelperStructRewrite_%.*s_%s"),
			Suffix.Len(),
			Suffix.GetData(),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));

		UPackage* BlueprintPackage = CreatePackage(*PackagePath);
		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should create a transient blueprint package"), BlueprintPackage))
		{
			return nullptr;
		}

		BlueprintPackage->SetFlags(RF_Transient);
		RootObject(RootedObjects, BlueprintPackage);

		const FName BlueprintName(*FPackageName::GetLongPackageAssetName(PackagePath));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			BlueprintPackage,
			BlueprintName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			TEXT("AngelscriptClassReloadHelperStructRewriteTests"));
		if (!Test.TestNotNull(*FString::Printf(TEXT("ClassReloadHelper.StructRewrite test should create blueprint %.*s"), Suffix.Len(), Suffix.GetData()), Blueprint))
		{
			return nullptr;
		}

		RootObject(RootedObjects, Blueprint);
		if (UClass* GeneratedClass = Blueprint->GeneratedClass)
		{
			RootObject(RootedObjects, GeneratedClass);
		}

		return Blueprint;
	}

	UUserDefinedStruct* CreateTransientUserDefinedStruct(FAutomationTestBase& Test, FStringView Suffix, TArray<UObject*>& RootedObjects)
	{
		const FName StructName(*FString::Printf(
			TEXT("AngelscriptClassReloadHelperStructRewrite%s_%s"),
			Suffix.GetData(),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));

		UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(GetTransientPackage(), StructName, RF_Transient);
		if (!Test.TestNotNull(*FString::Printf(TEXT("ClassReloadHelper.StructRewrite test should create struct fixture %.*s"), Suffix.Len(), Suffix.GetData()), Struct))
		{
			return nullptr;
		}

		RootObject(RootedObjects, Struct);
		FStructureEditorUtils::CompileStructure(Struct);
		return Struct;
	}

	FEdGraphPinType MakeStructPinType(UScriptStruct* Struct)
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = Struct;
		return PinType;
	}

	UK2Node_CallFunction* AddCallFunctionNode(FAutomationTestBase& Test, UBlueprint& Blueprint, UFunction* Function)
	{
		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should resolve the source function for the ordinary pin carrier"), Function))
		{
			return nullptr;
		}

		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should find the blueprint event graph"), EventGraph))
		{
			return nullptr;
		}

		UK2Node_CallFunction* CallFunctionNode = NewObject<UK2Node_CallFunction>(EventGraph);
		EventGraph->AddNode(CallFunctionNode, false, false);
		CallFunctionNode->CreateNewGuid();
		CallFunctionNode->PostPlacedNewNode();
		CallFunctionNode->SetFromFunction(Function);
		CallFunctionNode->AllocateDefaultPins();
		return CallFunctionNode;
	}

	bool FindTwoOrdinaryDataPins(
		FAutomationTestBase& Test,
		UK2Node_CallFunction& CallFunctionNode,
		UEdGraphPin*& OutImpactPin,
		UEdGraphPin*& OutControlPin)
	{
		OutImpactPin = nullptr;
		OutControlPin = nullptr;

		for (UEdGraphPin* Pin : CallFunctionNode.Pins)
		{
			if (Pin == nullptr
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
				|| Pin->PinName == UEdGraphSchema_K2::PN_Self)
			{
				continue;
			}

			if (OutImpactPin == nullptr)
			{
				OutImpactPin = Pin;
			}
			else
			{
				OutControlPin = Pin;
				break;
			}
		}

		return Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should find an ordinary data pin to rewrite"), OutImpactPin)
			&& Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should find an ordinary control pin to leave untouched"), OutControlPin);
	}

	UEdGraph* CreateWildcardMacroGraph(FAutomationTestBase& Test, UBlueprint& Blueprint)
	{
		const FName MacroGraphName(*FString::Printf(
			TEXT("StructRewriteMacro_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));

		UEdGraph* MacroGraph = FBlueprintEditorUtils::CreateNewGraph(
			&Blueprint,
			MacroGraphName,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should create a transient macro graph"), MacroGraph))
		{
			return nullptr;
		}

		FBlueprintEditorUtils::AddMacroGraph(&Blueprint, MacroGraph, true, nullptr);

		TArray<UK2Node_Tunnel*> TunnelNodes;
		MacroGraph->GetNodesOfClass(TunnelNodes);

		UK2Node_Tunnel* EntryTunnel = nullptr;
		for (UK2Node_Tunnel* TunnelNode : TunnelNodes)
		{
			if (TunnelNode != nullptr && TunnelNode->bCanHaveOutputs && !TunnelNode->bCanHaveInputs)
			{
				EntryTunnel = TunnelNode;
				break;
			}
		}

		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should find the macro entry tunnel"), EntryTunnel))
		{
			return nullptr;
		}

		FEdGraphPinType WildcardPinType;
		WildcardPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		EntryTunnel->CreateUserDefinedPin(TEXT("WildcardInput"), WildcardPinType, EGPD_Output);
		return MacroGraph;
	}

	UK2Node_CustomEvent* AddCustomEventUserPins(
		FAutomationTestBase& Test,
		UBlueprint& Blueprint,
		const FEdGraphPinType& ImpactPinType,
		const FEdGraphPinType& ControlPinType)
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should find the event graph for editable user pins"), EventGraph))
		{
			return nullptr;
		}

		UK2Node_CustomEvent* CustomEventNode = NewObject<UK2Node_CustomEvent>(EventGraph);
		EventGraph->AddNode(CustomEventNode, false, false);
		CustomEventNode->CustomFunctionName = TEXT("StructRewriteEditablePins");
		CustomEventNode->CreateNewGuid();
		CustomEventNode->PostPlacedNewNode();
		CustomEventNode->AllocateDefaultPins();
		CustomEventNode->CreateUserDefinedPin(TEXT("ImpactStruct"), ImpactPinType, EGPD_Output);
		CustomEventNode->CreateUserDefinedPin(TEXT("ControlStruct"), ControlPinType, EGPD_Output);
		return CustomEventNode;
	}

	TSharedPtr<FUserPinInfo> FindUserDefinedPinInfo(UK2Node_EditablePinBase& Node, const FName PinName)
	{
		for (const TSharedPtr<FUserPinInfo>& PinInfo : Node.UserDefinedPins)
		{
			if (PinInfo.IsValid() && PinInfo->PinName == PinName)
			{
				return PinInfo;
			}
		}

		return nullptr;
	}

	UK2Node_MacroInstance* AddMacroInstanceWithResolvedWildcard(
		FAutomationTestBase& Test,
		UBlueprint& Blueprint,
		UEdGraph& MacroGraph,
		UEdGraphPin& SourcePin)
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should find the event graph for the macro wildcard carrier"), EventGraph))
		{
			return nullptr;
		}

		UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(EventGraph);
		EventGraph->AddNode(MacroNode, false, false);
		MacroNode->CreateNewGuid();
		MacroNode->SetMacroGraph(&MacroGraph);
		MacroNode->PostPlacedNewNode();
		MacroNode->AllocateDefaultPins();

		UEdGraphPin* WildcardInputPin = MacroNode->FindPin(TEXT("WildcardInput"));
		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should materialize the macro wildcard input pin"), WildcardInputPin))
		{
			return nullptr;
		}

		const UEdGraphSchema* Schema = EventGraph->GetSchema();
		if (!Test.TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should resolve the event-graph schema for macro pin linking"), Schema))
		{
			return nullptr;
		}
		if (!Test.TestTrue(TEXT("ClassReloadHelper.StructRewrite test should connect the ordinary struct pin into the macro wildcard input"), Schema->TryCreateConnection(&SourcePin, WildcardInputPin)))
		{
			return nullptr;
		}
		MacroNode->NotifyPinConnectionListChanged(WildcardInputPin);
		MacroNode->NodeConnectionListChanged();

		// UE 5.7 smart wildcard inference updates the materialized pin type but does not
		// consistently repopulate the legacy ResolvedWildcardType field that
		// FClassReloadHelper::PerformReinstance() still rewrites explicitly.
		MacroNode->ResolvedWildcardType = SourcePin.PinType;

		return MacroNode;
	}
}

using namespace AngelscriptEditor_Private_Tests_AngelscriptClassReloadHelperStructRewriteTests_Private;

bool FAngelscriptClassReloadHelperPerformReinstanceStructRewriteTest::RunTest(const FString& Parameters)
{
	const FClassReloadHelper::FReloadState SavedState = FClassReloadHelper::ReloadState();
	TArray<FAngelscriptEngine*> SavedStack = FAngelscriptEngineContextStack::SnapshotAndClear();
	TUniquePtr<FAngelscriptEngine> Engine = MakeClassReloadHelperStructRewriteTestEngine();
	TUniquePtr<FAngelscriptEngineScope> EngineScope;
	TArray<UObject*> RootedObjects;
	IConsoleVariable* UseUnrealReloadCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("angelscript.UseUnrealReload"));
	const int32 SavedUseUnrealReload = UseUnrealReloadCVar != nullptr ? UseUnrealReloadCVar->GetInt() : 0;

	ON_SCOPE_EXIT
	{
		FClassReloadHelperTestAccess::ResetPerformReinstanceTestHooks();
		if (UseUnrealReloadCVar != nullptr)
		{
			UseUnrealReloadCVar->Set(SavedUseUnrealReload, ECVF_SetByCode);
		}

		EngineScope.Reset();
		for (UObject* Object : RootedObjects)
		{
			if (Object != nullptr)
			{
				Object->RemoveFromRoot();
				Object->MarkAsGarbage();
			}
		}

		CollectGarbage(RF_NoFlags, true);
		FAngelscriptEngineContextStack::RestoreSnapshot(MoveTemp(SavedStack));
		FClassReloadHelper::ReloadState() = SavedState;
	};

	if (!TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should create a testing engine"), Engine.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should expose GEditor"), GEditor))
	{
		return false;
	}

	EngineScope = MakeUnique<FAngelscriptEngineScope>(*Engine);
	Engine->bIsInitialCompileFinished = true;
	EnsureClassReloadHelperInitialized();

	if (UseUnrealReloadCVar != nullptr)
	{
		UseUnrealReloadCVar->Set(0, ECVF_SetByCode);
	}

	UUserDefinedStruct* OldStruct = CreateTransientUserDefinedStruct(*this, TEXT("Old"), RootedObjects);
	UUserDefinedStruct* NewStruct = CreateTransientUserDefinedStruct(*this, TEXT("New"), RootedObjects);
	UUserDefinedStruct* ControlStruct = CreateTransientUserDefinedStruct(*this, TEXT("Control"), RootedObjects);
	if (OldStruct == nullptr || NewStruct == nullptr || ControlStruct == nullptr)
	{
		return false;
	}

	UBlueprint* Blueprint = CreateTransientBlueprintChild(*this, UObject::StaticClass(), TEXT("StructRewrite"), RootedObjects);
	if (Blueprint == nullptr)
	{
		return false;
	}

	const FEdGraphPinType OldStructPinType = MakeStructPinType(OldStruct);
	const FEdGraphPinType ControlStructPinType = MakeStructPinType(ControlStruct);

	UK2Node_CallFunction* CallFunctionNode = AddCallFunctionNode(
		*this,
		*Blueprint,
		UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MakeVector")));
	if (CallFunctionNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* OrdinaryImpactPin = nullptr;
	UEdGraphPin* OrdinaryControlPin = nullptr;
	if (!FindTwoOrdinaryDataPins(*this, *CallFunctionNode, OrdinaryImpactPin, OrdinaryControlPin))
	{
		return false;
	}
	OrdinaryImpactPin = CallFunctionNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
	if (!TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should use the call-function return pin as the ordinary rewritten carrier"), OrdinaryImpactPin))
	{
		return false;
	}
	if (OrdinaryControlPin == OrdinaryImpactPin)
	{
		OrdinaryControlPin = nullptr;
		for (UEdGraphPin* Pin : CallFunctionNode->Pins)
		{
			if (Pin == nullptr
				|| Pin == OrdinaryImpactPin
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
				|| Pin->PinName == UEdGraphSchema_K2::PN_Self)
			{
				continue;
			}

			OrdinaryControlPin = Pin;
			break;
		}
		if (!TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should find a non-return control pin"), OrdinaryControlPin))
		{
			return false;
		}
	}
	OrdinaryImpactPin->PinType = OldStructPinType;
	OrdinaryControlPin->PinType = ControlStructPinType;

	UK2Node_CustomEvent* EditablePinNode = AddCustomEventUserPins(*this, *Blueprint, OldStructPinType, ControlStructPinType);
	if (!TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should add a custom event editable-pin carrier"), EditablePinNode))
	{
		return false;
	}

	UEdGraphPin* EditableImpactPin = EditablePinNode->FindPin(TEXT("ImpactStruct"));
	UEdGraphPin* EditableControlPin = EditablePinNode->FindPin(TEXT("ControlStruct"));
	if (!TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should materialize the impacted editable pin"), EditableImpactPin)
		|| !TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should materialize the control editable pin"), EditableControlPin))
	{
		return false;
	}

	TSharedPtr<FUserPinInfo> EditableImpactInfo = FindUserDefinedPinInfo(*EditablePinNode, TEXT("ImpactStruct"));
	TSharedPtr<FUserPinInfo> EditableControlInfo = FindUserDefinedPinInfo(*EditablePinNode, TEXT("ControlStruct"));
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should track the impacted editable pin descriptor"), EditableImpactInfo.IsValid())
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should track the control editable pin descriptor"), EditableControlInfo.IsValid()))
	{
		return false;
	}

	UEdGraph* MacroGraph = CreateWildcardMacroGraph(*this, *Blueprint);
	if (MacroGraph == nullptr)
	{
		return false;
	}

	UK2Node_MacroInstance* MacroNode = AddMacroInstanceWithResolvedWildcard(*this, *Blueprint, *MacroGraph, *OrdinaryImpactPin);
	if (!TestNotNull(TEXT("ClassReloadHelper.StructRewrite test should add a macro wildcard carrier"), MacroNode))
	{
		return false;
	}

	const int32 ImpactVariableIndex = Blueprint->NewVariables.Num();
	FBPVariableDescription ImpactVariable;
	ImpactVariable.VarName = TEXT("ImpactStructVariable");
	ImpactVariable.VarType = OldStructPinType;
	Blueprint->NewVariables.Add(ImpactVariable);

	const int32 ControlVariableIndex = Blueprint->NewVariables.Num();
	FBPVariableDescription ControlVariable;
	ControlVariable.VarName = TEXT("ControlStructVariable");
	ControlVariable.VarType = ControlStructPinType;
	Blueprint->NewVariables.Add(ControlVariable);

	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the ordinary impact pin pointing at the old struct"), OrdinaryImpactPin->PinType.PinSubCategoryObject.Get() == OldStruct)
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the ordinary control pin pointing at the control struct"), OrdinaryControlPin->PinType.PinSubCategoryObject.Get() == ControlStruct)
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the editable impact pin pointing at the old struct"), EditableImpactPin->PinType.PinSubCategoryObject.Get() == OldStruct)
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the editable control pin pointing at the control struct"), EditableControlPin->PinType.PinSubCategoryObject.Get() == ControlStruct)
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the editable impact descriptor pointing at the old struct"), EditableImpactInfo->PinType.PinSubCategoryObject.Get() == OldStruct)
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the editable control descriptor pointing at the control struct"), EditableControlInfo->PinType.PinSubCategoryObject.Get() == ControlStruct)
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the macro wildcard pointing at the old struct"), MacroNode->ResolvedWildcardType.PinSubCategoryObject.Get() == OldStruct)
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the impacted blueprint variable pointing at the old struct"), Blueprint->NewVariables[ImpactVariableIndex].VarType.PinSubCategoryObject.Get() == OldStruct)
		|| !TestTrue(TEXT("ClassReloadHelper.StructRewrite test should start with the control blueprint variable pointing at the control struct"), Blueprint->NewVariables[ControlVariableIndex].VarType.PinSubCategoryObject.Get() == ControlStruct))
	{
		return false;
	}

	FPerformReinstanceStructRewriteCallLog CallLog;
	FClassReloadHelper::FReloadState& ReloadState = FClassReloadHelper::ReloadState();
	ReloadState = FClassReloadHelper::FReloadState();
	ReloadState.ReloadStructs.Add(OldStruct, NewStruct);

	FClassReloadHelperPerformReinstanceTestHooks Hooks;
	Hooks.QueueBlueprintForCompilation = [&CallLog](UBlueprint* QueuedBlueprint)
	{
		CallLog.QueuedBlueprints.Add(QueuedBlueprint);
	};
	Hooks.FlushCompilationQueueAndReinstance = [&CallLog]()
	{
		++CallLog.FlushCompilationQueueAndReinstanceCalls;
	};
	FClassReloadHelperTestAccess::SetPerformReinstanceTestHooks(MoveTemp(Hooks));

	ReloadState.PerformReinstance();

	int32 DependentQueueCount = 0;
	for (const TWeakObjectPtr<UBlueprint>& QueuedBlueprint : CallLog.QueuedBlueprints)
	{
		if (QueuedBlueprint.Get() == Blueprint)
		{
			++DependentQueueCount;
		}
	}

	if (!TestEqual(TEXT("ClassReloadHelper.StructRewrite test should queue the impacted blueprint exactly once"), DependentQueueCount, 1))
	{
		return false;
	}
	if (!TestEqual(TEXT("ClassReloadHelper.StructRewrite test should flush the blueprint compilation queue once after rewriting impacted struct carriers"), CallLog.FlushCompilationQueueAndReinstanceCalls, 1))
	{
		return false;
	}
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should rewrite the ordinary pin to the replacement struct"), OrdinaryImpactPin->PinType.PinSubCategoryObject.Get() == NewStruct))
	{
		return false;
	}
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should leave the unrelated ordinary pin on the control struct"), OrdinaryControlPin->PinType.PinSubCategoryObject.Get() == ControlStruct))
	{
		return false;
	}
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should rewrite the editable pin instance to the replacement struct"), EditableImpactPin->PinType.PinSubCategoryObject.Get() == NewStruct))
	{
		return false;
	}
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should leave the unrelated editable pin instance unchanged"), EditableControlPin->PinType.PinSubCategoryObject.Get() == ControlStruct))
	{
		return false;
	}
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should rewrite the editable pin descriptor to the replacement struct"), EditableImpactInfo->PinType.PinSubCategoryObject.Get() == NewStruct))
	{
		return false;
	}
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should leave the unrelated editable pin descriptor unchanged"), EditableControlInfo->PinType.PinSubCategoryObject.Get() == ControlStruct))
	{
		return false;
	}
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should rewrite the macro wildcard pin type to the replacement struct"), MacroNode->ResolvedWildcardType.PinSubCategoryObject.Get() == NewStruct))
	{
		return false;
	}
	if (!TestEqual(TEXT("ClassReloadHelper.StructRewrite test should keep the blueprint variable count stable"), Blueprint->NewVariables.Num(), 2))
	{
		return false;
	}
	if (!TestTrue(TEXT("ClassReloadHelper.StructRewrite test should rewrite the impacted blueprint variable to the replacement struct"), Blueprint->NewVariables[ImpactVariableIndex].VarType.PinSubCategoryObject.Get() == NewStruct))
	{
		return false;
	}
	return TestTrue(TEXT("ClassReloadHelper.StructRewrite test should leave the unrelated blueprint variable unchanged"), Blueprint->NewVariables[ControlVariableIndex].VarType.PinSubCategoryObject.Get() == ControlStruct);
}

#endif
