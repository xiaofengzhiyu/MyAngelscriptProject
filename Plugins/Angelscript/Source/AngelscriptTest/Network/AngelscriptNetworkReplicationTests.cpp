#include "Shared/AngelscriptScenarioTestUtils.h"
#include "Shared/AngelscriptTestMacros.h"

#include "ClassGenerator/ASClass.h"
#include "Containers/Set.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/CoreNet.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace AngelscriptTest_Network_AngelscriptNetworkReplicationTests_Private
{
	static const FName NetworkModuleName(TEXT("ASNetworkReplicationSurface"));
	static const FString NetworkFilename(TEXT("ASNetworkReplicationSurface.as"));
	static const FName NetworkClassName(TEXT("ANetworkReplicationProbe"));
	static const FName ReplicatedCounterName(TEXT("ReplicatedCounter"));
	static const FName LastAcceptedValueName(TEXT("LastAcceptedValue"));
	static const FName RepNotifyFunctionName(TEXT("OnRep_LastAcceptedValue"));
	static const FName ServerRecordValueName(TEXT("Server_RecordValue"));
	static const FName ServerRecordValueValidateName(TEXT("Server_RecordValue_Validate"));
	static const FName ClientNotifyCounterName(TEXT("Client_NotifyCounter"));
	static const FName MulticastBroadcastCounterName(TEXT("Multicast_BroadcastCounter"));

	FName ResolveReplicatedPropertyName(const UClass* OwnerClass, const FLifetimeProperty& LifetimeProperty)
	{
		for (TFieldIterator<FProperty> It(OwnerClass); It; ++It)
		{
			if (It->RepIndex == LifetimeProperty.RepIndex)
			{
				return It->GetFName();
			}
		}

		return NAME_None;
	}

	TArray<FName> CollectReplicatedPropertyNames(const UClass* OwnerClass, const TArray<FLifetimeProperty>& LifetimeProperties)
	{
		TArray<FName> PropertyNames;
		PropertyNames.Reserve(LifetimeProperties.Num());

		for (const FLifetimeProperty& LifetimeProperty : LifetimeProperties)
		{
			const FName PropertyName = ResolveReplicatedPropertyName(OwnerClass, LifetimeProperty);
			if (PropertyName != NAME_None)
			{
				PropertyNames.Add(PropertyName);
			}
		}

		return PropertyNames;
	}

	void CollectNonReturnParameters(UFunction& Function, TArray<FProperty*>& OutParameters)
	{
		for (TFieldIterator<FProperty> It(&Function); It; ++It)
		{
			FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				OutParameters.Add(Property);
			}
		}
	}

	bool ExpectMatchingParameterSignature(
		FAutomationTestBase& Test,
		UFunction& NetworkFunction,
		UFunction& ValidateFunction)
	{
		TArray<FProperty*> NetworkParameters;
		TArray<FProperty*> ValidateParameters;
		CollectNonReturnParameters(NetworkFunction, NetworkParameters);
		CollectNonReturnParameters(ValidateFunction, ValidateParameters);

		if (!Test.TestEqual(
				TEXT("Network replication test should keep the same parameter count on the _Validate companion"),
				ValidateParameters.Num(),
				NetworkParameters.Num()))
		{
			return false;
		}

		for (int32 Index = 0; Index < NetworkParameters.Num(); ++Index)
		{
			FProperty* NetworkParameter = NetworkParameters[Index];
			FProperty* ValidateParameter = ValidateParameters[Index];
			const FString Context = FString::Printf(TEXT("Network replication validate parameter %d"), Index);
			if (!Test.TestEqual(*(Context + TEXT(" should preserve the parameter name")), ValidateParameter->GetFName(), NetworkParameter->GetFName())
				|| !Test.TestEqual(*(Context + TEXT(" should preserve the parameter property class")), ValidateParameter->GetClass(), NetworkParameter->GetClass())
				|| !Test.TestEqual(*(Context + TEXT(" should preserve the parameter cpp type")), ValidateParameter->GetCPPType(), NetworkParameter->GetCPPType()))
			{
				return false;
			}
		}

		return true;
	}
}

using namespace AngelscriptTest_Network_AngelscriptNetworkReplicationTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNetworkReplicationActorSurfaceTest,
	"Angelscript.TestModule.Network.Replication.ActorSurfaceBuildsReplicatedPropsAndRpcFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptNetworkReplicationActorSurfaceTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*NetworkModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileScriptModule(
		*this,
		Engine,
		NetworkModuleName,
		NetworkFilename,
		TEXT(R"AS(
UCLASS()
class ANetworkReplicationProbe : AActor
{
	default bReplicates = true;

	UPROPERTY(Replicated)
	int ReplicatedCounter = 5;

	UPROPERTY(ReplicatedUsing=OnRep_LastAcceptedValue)
	int LastAcceptedValue = -1;

	UPROPERTY()
	int ValidationCalls = 0;

	UFUNCTION()
	void OnRep_LastAcceptedValue()
	{
	}

	UFUNCTION(Server, WithValidation)
	void Server_RecordValue(int Value)
	{
		LastAcceptedValue = Value;
	}

	UFUNCTION()
	bool Server_RecordValue_Validate(int Value)
	{
		ValidationCalls += 1;
		return Value >= 0;
	}

	UFUNCTION(Client, Unreliable)
	void Client_NotifyCounter(int Value)
	{
	}

	UFUNCTION(NetMulticast)
	void Multicast_BroadcastCounter(int Value)
	{
	}
}
)AS"),
		NetworkClassName);
	if (ScriptClass == nullptr)
	{
		return false;
	}

	UASClass* ScriptASClass = Cast<UASClass>(ScriptClass);
	UFunction* ServerFunction = FindGeneratedFunction(ScriptClass, ServerRecordValueName);
	UFunction* ValidateFunction = FindGeneratedFunction(ScriptClass, ServerRecordValueValidateName);
	UFunction* ClientFunction = FindGeneratedFunction(ScriptClass, ClientNotifyCounterName);
	UFunction* MulticastFunction = FindGeneratedFunction(ScriptClass, MulticastBroadcastCounterName);
	UASFunction* GeneratedServerFunction = Cast<UASFunction>(ServerFunction);
	if (!TestNotNull(TEXT("Network replication test should generate the replicated actor as a UASClass"), ScriptASClass)
		|| !TestNotNull(TEXT("Network replication test should generate the server RPC"), ServerFunction)
		|| !TestNotNull(TEXT("Network replication test should generate the _Validate companion"), ValidateFunction)
		|| !TestNotNull(TEXT("Network replication test should generate the client RPC"), ClientFunction)
		|| !TestNotNull(TEXT("Network replication test should generate the multicast RPC"), MulticastFunction)
		|| !TestNotNull(TEXT("Network replication test should expose the server RPC as UASFunction"), GeneratedServerFunction))
	{
		return false;
	}

	FProperty* ReplicatedCounterProperty = FindFProperty<FProperty>(ScriptClass, ReplicatedCounterName);
	FProperty* LastAcceptedValueProperty = FindFProperty<FProperty>(ScriptClass, LastAcceptedValueName);
	if (!TestNotNull(TEXT("Network replication test should expose the plain replicated property"), ReplicatedCounterProperty)
		|| !TestNotNull(TEXT("Network replication test should expose the RepNotify property"), LastAcceptedValueProperty))
	{
		return false;
	}

	TestTrue(TEXT("Network replication test should flag ReplicatedCounter as CPF_Net"), ReplicatedCounterProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("Network replication test should flag LastAcceptedValue as CPF_Net"), LastAcceptedValueProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("Network replication test should flag LastAcceptedValue as CPF_RepNotify"), LastAcceptedValueProperty->HasAnyPropertyFlags(CPF_RepNotify));
	TestEqual(
		TEXT("Network replication test should preserve the RepNotify callback name on the generated property"),
		LastAcceptedValueProperty->RepNotifyFunc,
		RepNotifyFunctionName);

	TArray<FLifetimeProperty> LifetimeProperties;
	ScriptASClass->GetLifetimeScriptReplicationList(LifetimeProperties);
	const TArray<FName> ReplicatedPropertyNames = CollectReplicatedPropertyNames(ScriptClass, LifetimeProperties);

	TSet<FName> UniqueReplicatedPropertyNames;
	for (const FName PropertyName : ReplicatedPropertyNames)
	{
		UniqueReplicatedPropertyNames.Add(PropertyName);
	}

	TestEqual(
		TEXT("Network replication test should publish exactly the two script replicated properties into the lifetime list"),
		LifetimeProperties.Num(),
		2);
	TestEqual(
		TEXT("Network replication test should resolve every lifetime replication entry back to a concrete property"),
		ReplicatedPropertyNames.Num(),
		LifetimeProperties.Num());
	TestEqual(
		TEXT("Network replication test should not duplicate replicated properties in the lifetime list"),
		UniqueReplicatedPropertyNames.Num(),
		ReplicatedPropertyNames.Num());
	TestTrue(
		TEXT("Network replication test should keep the plain replicated property in the lifetime list"),
		ReplicatedPropertyNames.Contains(ReplicatedCounterName));
	TestTrue(
		TEXT("Network replication test should keep the RepNotify property in the lifetime list"),
		ReplicatedPropertyNames.Contains(LastAcceptedValueName));

	TestTrue(TEXT("Network replication test should mark the server RPC as net"), ServerFunction->HasAnyFunctionFlags(FUNC_Net));
	TestTrue(TEXT("Network replication test should mark the server RPC as server"), ServerFunction->HasAnyFunctionFlags(FUNC_NetServer));
	TestTrue(TEXT("Network replication test should mark the server RPC as reliable"), ServerFunction->HasAnyFunctionFlags(FUNC_NetReliable));
	TestTrue(TEXT("Network replication test should mark the server RPC as requiring validation"), ServerFunction->HasAnyFunctionFlags(FUNC_NetValidate));

	TestTrue(TEXT("Network replication test should mark the client RPC as net"), ClientFunction->HasAnyFunctionFlags(FUNC_Net));
	TestTrue(TEXT("Network replication test should mark the client RPC as client"), ClientFunction->HasAnyFunctionFlags(FUNC_NetClient));
	TestFalse(TEXT("Network replication test should keep the client RPC unreliable"), ClientFunction->HasAnyFunctionFlags(FUNC_NetReliable));
	TestFalse(TEXT("Network replication test should not add validation to the client RPC"), ClientFunction->HasAnyFunctionFlags(FUNC_NetValidate));

	TestTrue(TEXT("Network replication test should mark the multicast RPC as net"), MulticastFunction->HasAnyFunctionFlags(FUNC_Net));
	TestTrue(TEXT("Network replication test should mark the multicast RPC as multicast"), MulticastFunction->HasAnyFunctionFlags(FUNC_NetMulticast));
	TestTrue(TEXT("Network replication test should keep the multicast RPC reliable"), MulticastFunction->HasAnyFunctionFlags(FUNC_NetReliable));
	TestFalse(TEXT("Network replication test should not add validation to the multicast RPC"), MulticastFunction->HasAnyFunctionFlags(FUNC_NetValidate));

	UFunction* CachedValidateFunction = GeneratedServerFunction->GetRuntimeValidateFunction();
	if (!TestNotNull(TEXT("Network replication test should cache the _Validate companion on the generated server RPC"), CachedValidateFunction))
	{
		return false;
	}

	TestTrue(
		TEXT("Network replication test should return the reflected _Validate companion through the runtime cache"),
		CachedValidateFunction == ValidateFunction);

	FProperty* ValidateReturnProperty = nullptr;
	for (TFieldIterator<FProperty> It(ValidateFunction); It; ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			ValidateReturnProperty = *It;
			break;
		}
	}

	if (!TestNotNull(TEXT("Network replication test should expose a return property on the _Validate companion"), ValidateReturnProperty))
	{
		return false;
	}

	TestTrue(TEXT("Network replication test should keep the _Validate return type as bool"), ValidateReturnProperty->IsA<FBoolProperty>());
	if (!ExpectMatchingParameterSignature(*this, *ServerFunction, *ValidateFunction))
	{
		return false;
	}

	ASTEST_END_SHARE_FRESH
	return true;
}

#endif
