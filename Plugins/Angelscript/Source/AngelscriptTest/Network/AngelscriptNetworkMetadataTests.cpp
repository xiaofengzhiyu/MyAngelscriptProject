#include "Shared/AngelscriptScenarioTestUtils.h"
#include "Shared/AngelscriptTestMacros.h"

#include "ClassGenerator/ASClass.h"
#include "Containers/Set.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace AngelscriptTest_Network_AngelscriptNetworkMetadataTests_Private
{
	static const FName ReplicationConditionModuleName(TEXT("ASNetworkReplicationConditions"));
	static const FString ReplicationConditionFilename(TEXT("ASNetworkReplicationConditions.as"));
	static const FName ReplicationConditionClassName(TEXT("ANetworkReplicationConditionProbe"));
	static const FName OwnerOnlyValueName(TEXT("OwnerOnlyValue"));
	static const FName SkipOwnerValueName(TEXT("SkipOwnerValue"));
	static const FName InitialOnlyValueName(TEXT("InitialOnlyValue"));
	static const FName SkipOwnerRepNotifyName(TEXT("OnRep_SkipOwnerValue"));

	static const FName AuthorityOnlyModuleName(TEXT("ASNetworkAuthorityOnlyFlags"));
	static const FString AuthorityOnlyFilename(TEXT("ASNetworkAuthorityOnlyFlags.as"));
	static const FName AuthorityOnlyClassName(TEXT("ANetworkAuthorityOnlyProbe"));
	static const FName AuthorityOnlyLocalName(TEXT("AuthorityOnly_Local"));
	static const FName AuthorityOnlyServerName(TEXT("AuthorityOnly_Server"));
	static const FName PlainLocalName(TEXT("PlainLocal"));

	static const FName PushModelBoundaryModuleName(TEXT("ASNetworkPushModelBoundary"));
	static const FString PushModelBoundaryFilename(TEXT("ASNetworkPushModelBoundary.as"));
	static const FName PushModelBoundaryClassName(TEXT("ANetworkPushModelBoundaryProbe"));
	static const FString PushModelBoundaryDiagnostic(TEXT("Unknown property specifier ReplicationPushModel on property ANetworkPushModelBoundaryProbe::PushValue."));

	const FLifetimeProperty* FindLifetimePropertyByName(
		const UClass* OwnerClass,
		const TArray<FLifetimeProperty>& LifetimeProperties,
		const FName PropertyName)
	{
		const FProperty* Property = FindFProperty<FProperty>(OwnerClass, PropertyName);
		if (Property == nullptr)
		{
			return nullptr;
		}

		for (const FLifetimeProperty& LifetimeProperty : LifetimeProperties)
		{
			if (LifetimeProperty.RepIndex == Property->RepIndex)
			{
				return &LifetimeProperty;
			}
		}

		return nullptr;
	}

	FString JoinDiagnostics(const TArray<FAngelscriptCompileTraceDiagnosticSummary>& Diagnostics)
	{
		TArray<FString> Lines;
		for (const FAngelscriptCompileTraceDiagnosticSummary& Diagnostic : Diagnostics)
		{
			Lines.Add(FString::Printf(
				TEXT("[%s] %s(%d:%d) %s"),
				Diagnostic.bIsError ? TEXT("Error") : (Diagnostic.bIsInfo ? TEXT("Info") : TEXT("Warning")),
				*Diagnostic.Section,
				Diagnostic.Row,
				Diagnostic.Column,
				*Diagnostic.Message));
		}

		return FString::Join(Lines, TEXT(" | "));
	}

	const FAngelscriptCompileTraceDiagnosticSummary* FindMatchingErrorDiagnostic(
		const TArray<FAngelscriptCompileTraceDiagnosticSummary>& Diagnostics,
		const FString& MessageFragment)
	{
		return Diagnostics.FindByPredicate(
			[&MessageFragment](const FAngelscriptCompileTraceDiagnosticSummary& Diagnostic)
			{
				return Diagnostic.bIsError && Diagnostic.Message.Contains(MessageFragment);
			});
	}
}

using namespace AngelscriptTest_Network_AngelscriptNetworkMetadataTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNetworkReplicationConditionLifetimeListTest,
	"Angelscript.TestModule.Network.Metadata.ReplicationConditionsFlowIntoLifetimeList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNetworkBlueprintAuthorityOnlyFlagsTest,
	"Angelscript.TestModule.Network.Metadata.BlueprintAuthorityOnlyFlagsMaterialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNetworkPushModelSpecifierRejectedTest,
	"Angelscript.TestModule.Network.Metadata.ReplicationPushModelSpecifierRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptNetworkReplicationConditionLifetimeListTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ReplicationConditionModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileScriptModule(
		*this,
		Engine,
		ReplicationConditionModuleName,
		ReplicationConditionFilename,
		TEXT(R"AS(
UCLASS()
class ANetworkReplicationConditionProbe : AActor
{
	default bReplicates = true;

	UPROPERTY(Replicated, ReplicationCondition=OwnerOnly)
	int OwnerOnlyValue = 11;

	UPROPERTY(ReplicatedUsing=OnRep_SkipOwnerValue, ReplicationCondition=SkipOwner)
	int SkipOwnerValue = 13;

	UPROPERTY(Replicated, ReplicationCondition=InitialOnly)
	int InitialOnlyValue = 17;

	UFUNCTION()
	void OnRep_SkipOwnerValue()
	{
	}
}
)AS"),
		ReplicationConditionClassName);
	if (ScriptClass == nullptr)
	{
		return false;
	}

	UASClass* ScriptASClass = Cast<UASClass>(ScriptClass);
	FProperty* OwnerOnlyProperty = FindFProperty<FProperty>(ScriptClass, OwnerOnlyValueName);
	FProperty* SkipOwnerProperty = FindFProperty<FProperty>(ScriptClass, SkipOwnerValueName);
	FProperty* InitialOnlyProperty = FindFProperty<FProperty>(ScriptClass, InitialOnlyValueName);
	if (!TestNotNull(TEXT("Network metadata replication-condition test should materialize the generated class as UASClass"), ScriptASClass)
		|| !TestNotNull(TEXT("Network metadata replication-condition test should materialize the OwnerOnly property"), OwnerOnlyProperty)
		|| !TestNotNull(TEXT("Network metadata replication-condition test should materialize the SkipOwner property"), SkipOwnerProperty)
		|| !TestNotNull(TEXT("Network metadata replication-condition test should materialize the InitialOnly property"), InitialOnlyProperty))
	{
		return false;
	}

	TestTrue(TEXT("Network metadata replication-condition test should keep OwnerOnlyValue replicated"), OwnerOnlyProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("Network metadata replication-condition test should keep SkipOwnerValue replicated"), SkipOwnerProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("Network metadata replication-condition test should keep InitialOnlyValue replicated"), InitialOnlyProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("Network metadata replication-condition test should keep SkipOwnerValue as a RepNotify property"), SkipOwnerProperty->HasAnyPropertyFlags(CPF_RepNotify));
	TestEqual(
		TEXT("Network metadata replication-condition test should preserve the RepNotify callback on SkipOwnerValue"),
		SkipOwnerProperty->RepNotifyFunc,
		SkipOwnerRepNotifyName);

	TestEqual(
		TEXT("Network metadata replication-condition test should preserve COND_OwnerOnly on OwnerOnlyValue"),
		OwnerOnlyProperty->GetBlueprintReplicationCondition(),
		COND_OwnerOnly);
	TestEqual(
		TEXT("Network metadata replication-condition test should preserve COND_SkipOwner on SkipOwnerValue"),
		SkipOwnerProperty->GetBlueprintReplicationCondition(),
		COND_SkipOwner);
	TestEqual(
		TEXT("Network metadata replication-condition test should preserve COND_InitialOnly on InitialOnlyValue"),
		InitialOnlyProperty->GetBlueprintReplicationCondition(),
		COND_InitialOnly);

	TArray<FLifetimeProperty> LifetimeProperties;
	ScriptASClass->GetLifetimeScriptReplicationList(LifetimeProperties);

	TSet<uint16> UniqueRepIndexes;
	for (const FLifetimeProperty& LifetimeProperty : LifetimeProperties)
	{
		UniqueRepIndexes.Add(LifetimeProperty.RepIndex);
	}

	const FLifetimeProperty* OwnerOnlyLifetimeProperty =
		FindLifetimePropertyByName(ScriptClass, LifetimeProperties, OwnerOnlyValueName);
	const FLifetimeProperty* SkipOwnerLifetimeProperty =
		FindLifetimePropertyByName(ScriptClass, LifetimeProperties, SkipOwnerValueName);
	const FLifetimeProperty* InitialOnlyLifetimeProperty =
		FindLifetimePropertyByName(ScriptClass, LifetimeProperties, InitialOnlyValueName);
	if (!TestNotNull(TEXT("Network metadata replication-condition test should publish OwnerOnlyValue into the lifetime list"), OwnerOnlyLifetimeProperty)
		|| !TestNotNull(TEXT("Network metadata replication-condition test should publish SkipOwnerValue into the lifetime list"), SkipOwnerLifetimeProperty)
		|| !TestNotNull(TEXT("Network metadata replication-condition test should publish InitialOnlyValue into the lifetime list"), InitialOnlyLifetimeProperty))
	{
		return false;
	}

	TestEqual(
		TEXT("Network metadata replication-condition test should only publish the three script-replicated properties into the lifetime list"),
		LifetimeProperties.Num(),
		3);
	TestEqual(
		TEXT("Network metadata replication-condition test should not duplicate lifetime replication entries"),
		UniqueRepIndexes.Num(),
		LifetimeProperties.Num());
	TestEqual(
		TEXT("Network metadata replication-condition test should keep the OwnerOnly lifetime condition"),
		OwnerOnlyLifetimeProperty->Condition,
		COND_OwnerOnly);
	TestEqual(
		TEXT("Network metadata replication-condition test should keep the SkipOwner lifetime condition"),
		SkipOwnerLifetimeProperty->Condition,
		COND_SkipOwner);
	TestEqual(
		TEXT("Network metadata replication-condition test should keep the InitialOnly lifetime condition"),
		InitialOnlyLifetimeProperty->Condition,
		COND_InitialOnly);
	TestEqual(
		TEXT("Network metadata replication-condition test should keep the default RepNotify lifetime policy on the RepNotify property"),
		SkipOwnerLifetimeProperty->RepNotifyCondition,
		REPNOTIFY_OnChanged);
	TestFalse(
		TEXT("Network metadata replication-condition test should not mark the lifetime entry as push-based in the current stock-plugin boundary"),
		SkipOwnerLifetimeProperty->bIsPushBased);

	ASTEST_END_SHARE_FRESH
	return true;
}

bool FAngelscriptNetworkBlueprintAuthorityOnlyFlagsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*AuthorityOnlyModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileScriptModule(
		*this,
		Engine,
		AuthorityOnlyModuleName,
		AuthorityOnlyFilename,
		TEXT(R"AS(
UCLASS()
class ANetworkAuthorityOnlyProbe : AActor
{
	UFUNCTION(BlueprintAuthorityOnly)
	void AuthorityOnly_Local(int Value)
	{
	}

	UFUNCTION(Server, BlueprintAuthorityOnly)
	void AuthorityOnly_Server(int Value)
	{
	}

	UFUNCTION()
	void PlainLocal(int Value)
	{
	}
}
)AS"),
		AuthorityOnlyClassName);
	if (ScriptClass == nullptr)
	{
		return false;
	}

	UFunction* AuthorityOnlyLocalFunction = FindGeneratedFunction(ScriptClass, AuthorityOnlyLocalName);
	UFunction* AuthorityOnlyServerFunction = FindGeneratedFunction(ScriptClass, AuthorityOnlyServerName);
	UFunction* PlainLocalFunction = FindGeneratedFunction(ScriptClass, PlainLocalName);
	if (!TestNotNull(TEXT("Network metadata authority-only test should materialize the local authority-only function"), AuthorityOnlyLocalFunction)
		|| !TestNotNull(TEXT("Network metadata authority-only test should materialize the server authority-only function"), AuthorityOnlyServerFunction)
		|| !TestNotNull(TEXT("Network metadata authority-only test should materialize the plain local function"), PlainLocalFunction))
	{
		return false;
	}

	TestTrue(
		TEXT("Network metadata authority-only test should flag the local function as BlueprintAuthorityOnly"),
		AuthorityOnlyLocalFunction->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly));
	TestFalse(
		TEXT("Network metadata authority-only test should keep the local authority-only function off the net path"),
		AuthorityOnlyLocalFunction->HasAnyFunctionFlags(FUNC_Net));

	TestTrue(
		TEXT("Network metadata authority-only test should flag the server RPC as BlueprintAuthorityOnly"),
		AuthorityOnlyServerFunction->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly));
	TestTrue(
		TEXT("Network metadata authority-only test should mark the server authority-only function as net"),
		AuthorityOnlyServerFunction->HasAnyFunctionFlags(FUNC_Net));
	TestTrue(
		TEXT("Network metadata authority-only test should mark the server authority-only function as server"),
		AuthorityOnlyServerFunction->HasAnyFunctionFlags(FUNC_NetServer));
	TestTrue(
		TEXT("Network metadata authority-only test should keep the server authority-only function reliable"),
		AuthorityOnlyServerFunction->HasAnyFunctionFlags(FUNC_NetReliable));
	TestFalse(
		TEXT("Network metadata authority-only test should not add validation to the server authority-only function without WithValidation"),
		AuthorityOnlyServerFunction->HasAnyFunctionFlags(FUNC_NetValidate));

	TestFalse(
		TEXT("Network metadata authority-only test should not leak BlueprintAuthorityOnly onto plain local functions"),
		PlainLocalFunction->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly));
	TestFalse(
		TEXT("Network metadata authority-only test should keep the plain local function off the net path"),
		PlainLocalFunction->HasAnyFunctionFlags(FUNC_Net));

	ASTEST_END_SHARE_FRESH
	return true;
}

bool FAngelscriptNetworkPushModelSpecifierRejectedTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*PushModelBoundaryModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	Engine.ResetDiagnostics();
	AddExpectedErrorPlain(
		*PushModelBoundaryDiagnostic,
		EAutomationExpectedErrorFlags::Contains,
		1);

	FAngelscriptCompileTraceSummary Summary;
	const bool bCompiled = CompileModuleWithSummary(
		&Engine,
		ECompileType::FullReload,
		PushModelBoundaryModuleName,
		PushModelBoundaryFilename,
		TEXT(R"AS(
UCLASS()
class ANetworkPushModelBoundaryProbe : AActor
{
	default bReplicates = true;

	UPROPERTY(Replicated, ReplicationPushModel)
	int PushValue = 23;
}
)AS"),
		true,
		Summary,
		true);

	if (Summary.Diagnostics.Num() > 0)
	{
		AddInfo(FString::Printf(TEXT("Network push-model boundary diagnostics: %s"), *JoinDiagnostics(Summary.Diagnostics)));
	}

	const FAngelscriptCompileTraceDiagnosticSummary* MatchingDiagnostic =
		FindMatchingErrorDiagnostic(Summary.Diagnostics, PushModelBoundaryDiagnostic);

	bPassed &= TestFalse(
		TEXT("Network push-model boundary test should reject ReplicationPushModel on the current stock-plugin branch"),
		bCompiled);
	bPassed &= TestFalse(
		TEXT("Network push-model boundary test should keep bCompileSucceeded false"),
		Summary.bCompileSucceeded);
	bPassed &= TestTrue(
		TEXT("Network push-model boundary test should still exercise the preprocessor-backed compile path"),
		Summary.bUsedPreprocessor);
	bPassed &= TestNotNull(
		TEXT("Network push-model boundary test should emit the unsupported-specifier diagnostic"),
		MatchingDiagnostic);
	if (MatchingDiagnostic != nullptr)
	{
		bPassed &= TestEqual(
			TEXT("Network push-model boundary test should pin the diagnostic row to the UPROPERTY declaration"),
			MatchingDiagnostic->Row,
			7);
	}

	UClass* GeneratedClass = FindGeneratedClass(&Engine, PushModelBoundaryClassName);
	bPassed &= TestNull(
		TEXT("Network push-model boundary test should not leave behind a generated class when the property specifier is rejected"),
		GeneratedClass);

	ASTEST_END_SHARE_FRESH
	return bPassed;
}

#endif
