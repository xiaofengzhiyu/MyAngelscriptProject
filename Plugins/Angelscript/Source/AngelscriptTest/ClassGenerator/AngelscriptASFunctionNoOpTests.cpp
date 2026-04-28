#include "Shared/AngelscriptScenarioTestUtils.h"
#include "Shared/AngelscriptTestMacros.h"

#include "ClassGenerator/ASClass.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace ASFunctionNoOpTests
{
	static const FName ModuleName(TEXT("ASFunctionNoOpSoftReload"));
	static const FString ScriptFilename(TEXT("ASFunctionNoOpSoftReload.as"));
	static const FName GeneratedClassName(TEXT("ANoopCarrierActor"));
	static const FName CounterPropertyName(TEXT("Counter"));
	static const FName BeginPlayFunctionName(TEXT("BeginPlay"));
	static const FName ReceiveBeginPlayFunctionName(TEXT("ReceiveBeginPlay"));
	static const FName DoWorkFunctionName(TEXT("DoWork"));
	static constexpr int32 ExpectedCounterAfterBaselineBeginPlay = 0;
	static constexpr int32 ExpectedCounterAfterBaselineDoWork = 1;
	static constexpr int32 ExpectedCounterAfterReloadedBeginPlay = 2;
	static constexpr int32 ExpectedCounterAfterReloadedDoWork = 3;

	const FString ScriptV1 = TEXT(R"AS(
UCLASS()
class ANoopCarrierActor : AActor
{
	UPROPERTY()
	int Counter = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
	}

	UFUNCTION()
	void DoWork()
	{
		Counter += 1;
	}
}
)AS");

	const FString ScriptV2 = TEXT(R"AS(
UCLASS()
class ANoopCarrierActor : AActor
{
	UPROPERTY()
	int Counter = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		Counter += 2;
	}

	UFUNCTION()
	void DoWork()
	{
		Counter += 1;
	}
}
)AS");

	bool IsHandledSoftReloadResult(const ECompileResult ReloadResult)
	{
		return ReloadResult == ECompileResult::FullyHandled || ReloadResult == ECompileResult::PartiallyHandled;
	}

	void InitializeSpawner(FActorTestSpawner& Spawner)
	{
		Spawner.InitializeGameSubsystems();
	}

	UASClass* CompileNoOpCarrier(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine)
	{
		UClass* GeneratedClass = CompileScriptModule(
			Test,
			Engine,
			ModuleName,
			ScriptFilename,
			ScriptV1,
			GeneratedClassName);
		if (GeneratedClass == nullptr)
		{
			return nullptr;
		}

		UASClass* ScriptClass = Cast<UASClass>(GeneratedClass);
		if (!Test.TestNotNull(
				TEXT("ASFunction no-op soft reload scenario should compile the carrier into a UASClass"),
				ScriptClass))
		{
			return nullptr;
		}

		Test.TestNotNull(
			TEXT("ASFunction no-op soft reload scenario should keep a live script type pointer after the baseline compile"),
			ScriptClass->ScriptTypePtr);
		return ScriptClass;
	}

	bool ReadCounter(
		FAutomationTestBase& Test,
		UObject* Object,
		int32& OutCounter)
	{
		return ReadPropertyValue<FIntProperty>(Test, Object, CounterPropertyName, OutCounter);
	}

	bool VerifyNoOpState(
		FAutomationTestBase& Test,
		const FString& ScopeLabel,
		UFunction* Function,
		const bool bExpectedNoOp,
		const bool bExpectedMetadata)
	{
		UASFunction* ASFunction = Cast<UASFunction>(Function);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should expose a generated UASFunction"), *ScopeLabel),
				ASFunction))
		{
			return false;
		}

		const bool bObservedMetadata = Function->HasMetaData(TEXT("ScriptNoOp"));
		const bool bNoOpMatches = Test.TestEqual(
			*FString::Printf(TEXT("%s should observe the expected bIsNoOp state"), *ScopeLabel),
			ASFunction->bIsNoOp,
			bExpectedNoOp);
		const bool bMetadataMatches = Test.TestEqual(
			*FString::Printf(TEXT("%s should observe the expected ScriptNoOp metadata state"), *ScopeLabel),
			bObservedMetadata,
			bExpectedMetadata);

		return bNoOpMatches && bMetadataMatches;
	}

	bool InvokeNoArgumentVoidFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		const FString& ScopeLabel)
	{
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should keep a live runtime object"), *ScopeLabel),
				Object)
			|| !Test.TestNotNull(
				*FString::Printf(TEXT("%s should keep a live generated function"), *ScopeLabel),
				Function))
		{
			return false;
		}

		FAngelscriptEngineScope FunctionScope(Engine, Object);
		Object->ProcessEvent(Function, nullptr);
		return true;
	}

	UFunction* FindGeneratedBeginPlayOverrideFunction(UClass* ScriptClass)
	{
		if (ScriptClass == nullptr)
		{
			return nullptr;
		}

		UFunction* BeginPlayFunction = FindGeneratedFunction(ScriptClass, BeginPlayFunctionName);
		if (BeginPlayFunction != nullptr && BeginPlayFunction->GetOwnerClass() == ScriptClass)
		{
			return BeginPlayFunction;
		}

		UFunction* ReceiveBeginPlayFunction = FindGeneratedFunction(ScriptClass, ReceiveBeginPlayFunctionName);
		if (ReceiveBeginPlayFunction != nullptr && ReceiveBeginPlayFunction->GetOwnerClass() == ScriptClass)
		{
			return ReceiveBeginPlayFunction;
		}

		return nullptr;
	}
}

using namespace ASFunctionNoOpTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptASFunctionNoOpFlagAndMetadataTrackSoftReloadTest,
	"Angelscript.TestModule.ClassGenerator.ASFunction.NoOpFlagAndMetadataTrackSoftReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptASFunctionNoOpFlagAndMetadataTrackSoftReloadTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ASFunctionNoOpTests::ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
		CollectGarbage(RF_NoFlags, true);
	};

	UASClass* ScriptClass = ASFunctionNoOpTests::CompileNoOpCarrier(*this, Engine);
	if (ScriptClass == nullptr)
	{
		return false;
	}

	UFunction* InitialBeginPlayFunction = ASFunctionNoOpTests::FindGeneratedBeginPlayOverrideFunction(ScriptClass);
	UFunction* InitialDoWorkFunction = FindGeneratedFunction(ScriptClass, ASFunctionNoOpTests::DoWorkFunctionName);
	if (!TestNotNull(
			TEXT("ASFunction no-op soft reload scenario should expose the baseline BeginPlay override function"),
			InitialBeginPlayFunction)
		|| !TestNotNull(
			TEXT("ASFunction no-op soft reload scenario should expose the baseline DoWork function"),
			InitialDoWorkFunction))
	{
		return false;
	}

	if (!ASFunctionNoOpTests::VerifyNoOpState(
			*this,
			TEXT("ASFunction no-op soft reload scenario baseline BeginPlay override function"),
			InitialBeginPlayFunction,
			true,
			true)
		|| !ASFunctionNoOpTests::VerifyNoOpState(
			*this,
			TEXT("ASFunction no-op soft reload scenario baseline DoWork function"),
			InitialDoWorkFunction,
			false,
			false))
	{
		return false;
	}

	FActorTestSpawner Spawner;
	ASFunctionNoOpTests::InitializeSpawner(Spawner);

	AActor* ActorBeforeReload = SpawnScriptActor(*this, Spawner, ScriptClass);
	if (ActorBeforeReload == nullptr)
	{
		return false;
	}

	BeginPlayActor(Engine, *ActorBeforeReload);

	int32 CounterAfterBaselineBeginPlay = INDEX_NONE;
	if (!ASFunctionNoOpTests::ReadCounter(*this, ActorBeforeReload, CounterAfterBaselineBeginPlay)
		|| !TestEqual(
			TEXT("ASFunction no-op soft reload scenario baseline BeginPlay override should keep Counter unchanged"),
			CounterAfterBaselineBeginPlay,
			ASFunctionNoOpTests::ExpectedCounterAfterBaselineBeginPlay))
	{
		return false;
	}

	if (!TestTrue(
			TEXT("ASFunction no-op soft reload scenario should execute the baseline DoWork function"),
			ASFunctionNoOpTests::InvokeNoArgumentVoidFunction(
				*this,
				Engine,
				ActorBeforeReload,
				InitialDoWorkFunction,
				TEXT("ASFunction no-op soft reload scenario baseline DoWork invocation"))))
	{
		return false;
	}

	int32 CounterAfterBaselineDoWork = INDEX_NONE;
	if (!ASFunctionNoOpTests::ReadCounter(*this, ActorBeforeReload, CounterAfterBaselineDoWork)
		|| !TestEqual(
			TEXT("ASFunction no-op soft reload scenario baseline DoWork should increment Counter"),
			CounterAfterBaselineDoWork,
			ASFunctionNoOpTests::ExpectedCounterAfterBaselineDoWork))
	{
		return false;
	}

	ECompileResult ReloadResult = ECompileResult::Error;
	if (!TestTrue(
			TEXT("ASFunction no-op soft reload scenario should compile the non-no-op update on the soft reload path"),
			CompileModuleWithResult(
				&Engine,
				ECompileType::SoftReloadOnly,
				ASFunctionNoOpTests::ModuleName,
				ASFunctionNoOpTests::ScriptFilename,
				ASFunctionNoOpTests::ScriptV2,
				ReloadResult)))
	{
		return false;
	}

	if (!TestTrue(
			TEXT("ASFunction no-op soft reload scenario should be handled by the soft reload pipeline"),
			ASFunctionNoOpTests::IsHandledSoftReloadResult(ReloadResult)))
	{
		return false;
	}

	UClass* ReloadedClass = FindGeneratedClass(&Engine, ASFunctionNoOpTests::GeneratedClassName);
	if (!TestNotNull(
			TEXT("ASFunction no-op soft reload scenario should keep the generated class visible after reload"),
			ReloadedClass))
	{
		return false;
	}

	UFunction* ReloadedBeginPlayFunction = ASFunctionNoOpTests::FindGeneratedBeginPlayOverrideFunction(ReloadedClass);
	UFunction* ReloadedDoWorkFunction = FindGeneratedFunction(ReloadedClass, ASFunctionNoOpTests::DoWorkFunctionName);
	if (!TestNotNull(
			TEXT("ASFunction no-op soft reload scenario should expose the reloaded BeginPlay override function"),
			ReloadedBeginPlayFunction)
		|| !TestNotNull(
			TEXT("ASFunction no-op soft reload scenario should expose the reloaded DoWork function"),
			ReloadedDoWorkFunction))
	{
		return false;
	}

	TestTrue(
		TEXT("ASFunction no-op soft reload scenario should preserve the canonical generated class object across soft reload"),
		ReloadedClass == ScriptClass);
	TestTrue(
		TEXT("ASFunction no-op soft reload scenario should preserve the BeginPlay override UFunction object across soft reload"),
		ReloadedBeginPlayFunction == InitialBeginPlayFunction);
	TestTrue(
		TEXT("ASFunction no-op soft reload scenario should preserve the DoWork UFunction object across soft reload"),
		ReloadedDoWorkFunction == InitialDoWorkFunction);

	if (!ASFunctionNoOpTests::VerifyNoOpState(
			*this,
			TEXT("ASFunction no-op soft reload scenario reloaded BeginPlay override function"),
			ReloadedBeginPlayFunction,
			false,
			false)
		|| !ASFunctionNoOpTests::VerifyNoOpState(
			*this,
			TEXT("ASFunction no-op soft reload scenario reloaded DoWork function"),
			ReloadedDoWorkFunction,
			false,
			false))
	{
		return false;
	}

	AActor* ActorAfterReload = SpawnScriptActor(*this, Spawner, ReloadedClass);
	if (ActorAfterReload == nullptr)
	{
		return false;
	}

	BeginPlayActor(Engine, *ActorAfterReload);

	int32 CounterAfterReloadedBeginPlay = INDEX_NONE;
	if (!ASFunctionNoOpTests::ReadCounter(*this, ActorAfterReload, CounterAfterReloadedBeginPlay)
		|| !TestEqual(
			TEXT("ASFunction no-op soft reload scenario reloaded BeginPlay override should increment Counter by 2"),
			CounterAfterReloadedBeginPlay,
			ASFunctionNoOpTests::ExpectedCounterAfterReloadedBeginPlay))
	{
		return false;
	}

	if (!TestTrue(
			TEXT("ASFunction no-op soft reload scenario should keep DoWork callable after the BeginPlay no-op flag clears"),
			ASFunctionNoOpTests::InvokeNoArgumentVoidFunction(
				*this,
				Engine,
				ActorAfterReload,
				ReloadedDoWorkFunction,
				TEXT("ASFunction no-op soft reload scenario reloaded DoWork invocation"))))
	{
		return false;
	}

	int32 CounterAfterReloadedDoWork = INDEX_NONE;
	if (!ASFunctionNoOpTests::ReadCounter(*this, ActorAfterReload, CounterAfterReloadedDoWork)
		|| !TestEqual(
			TEXT("ASFunction no-op soft reload scenario reloaded DoWork should preserve its original increment"),
			CounterAfterReloadedDoWork,
			ASFunctionNoOpTests::ExpectedCounterAfterReloadedDoWork))
	{
		return false;
	}

	bPassed = true;

	ASTEST_END_SHARE_FRESH
	return bPassed;
}

#endif
