#include "AngelscriptGASTestTypes.h"
#include "Shared/AngelscriptTestUtilities.h"

#include "../../AngelscriptRuntime/Core/AngelscriptGameplayCueUtils.h"

#include "Components/ActorTestSpawner.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(
		AngelscriptGameplayCueUtilsLocalWrapperTag,
		TEXT("GameplayCue.Angelscript.Tests.LocalWrapper"));

	bool CheckRecordedCueEvent(
		FAutomationTestBase& Test,
		const UAngelscriptGASTestGameplayCueRecorder& Recorder,
		const int32 Index,
		const EGameplayCueEvent::Type ExpectedEventType,
		const AActor* ExpectedTarget,
		const AActor* ExpectedInstigator,
		const float ExpectedRawMagnitude)
	{
		const FString Prefix = FString::Printf(TEXT("Recorded cue event %d"), Index);
		if (!Test.TestTrue(
				*FString::Printf(TEXT("%s should exist"), *Prefix),
				Recorder.RecordedEventTypes.IsValidIndex(Index)
					&& Recorder.RecordedRawMagnitudes.IsValidIndex(Index)
					&& Recorder.RecordedTargets.IsValidIndex(Index)
					&& Recorder.RecordedInstigators.IsValidIndex(Index)))
		{
			return false;
		}

		bool bPassed = true;
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve the expected event type"), *Prefix),
			static_cast<int32>(Recorder.RecordedEventTypes[Index]),
			static_cast<int32>(ExpectedEventType));
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should preserve the expected target actor"), *Prefix),
			Recorder.RecordedTargets[Index].Get() == ExpectedTarget);
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should preserve the expected instigator actor"), *Prefix),
			Recorder.RecordedInstigators[Index].Get() == ExpectedInstigator);
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve the raw magnitude"), *Prefix),
			Recorder.RecordedRawMagnitudes[Index],
			ExpectedRawMagnitude);
		return bPassed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGameplayCueUtilsDispatchesExpectedCueEventsTest,
	"Angelscript.TestModule.Engine.GAS.GameplayCueUtils.DispatchesExpectedCueEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGameplayCueUtilsDispatchesExpectedCueEventsTest::RunTest(const FString& Parameters)
{
	FAngelscriptTestFixture Fixture(*this, ETestEngineMode::ProductionLike);
	if (!TestTrue(TEXT("GameplayCueUtils test should acquire a production-like engine"), Fixture.IsValid()))
	{
		return false;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor* TargetActor = &Spawner.SpawnActor<AActor>();
	if (!TestNotNull(TEXT("GameplayCueUtils test should spawn a target actor"), TargetActor))
	{
		return false;
	}

	AActor* InstigatorActor = &Spawner.SpawnActor<AActor>();
	if (!TestNotNull(TEXT("GameplayCueUtils test should spawn an instigator actor"), InstigatorActor))
	{
		return false;
	}

	UAngelscriptGASTestGameplayCueRecorder* Recorder = GetMutableDefault<UAngelscriptGASTestGameplayCueRecorder>();
	if (!TestNotNull(TEXT("GameplayCueUtils test should resolve the native cue recorder CDO"), Recorder))
	{
		return false;
	}

	const FGameplayTag OriginalCueTag = Recorder->GameplayCueTag;
	const FName OriginalCueName = Recorder->GameplayCueName;
	Recorder->ResetRecords();
	Recorder->GameplayCueTag = AngelscriptGameplayCueUtilsLocalWrapperTag;
	Recorder->GameplayCueName = AngelscriptGameplayCueUtilsLocalWrapperTag.GetTag().GetTagName();
	ON_SCOPE_EXIT
	{
		Recorder->ResetRecords();
		Recorder->GameplayCueTag = OriginalCueTag;
		Recorder->GameplayCueName = OriginalCueName;
	};

	UGameplayCueManager* GameplayCueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	if (!TestNotNull(TEXT("GameplayCueUtils test should initialize the gameplay cue manager"), GameplayCueManager))
	{
		return false;
	}

	UGameplayCueSet* RuntimeCueSet = GameplayCueManager->GetRuntimeCueSet();
	if (!TestNotNull(TEXT("GameplayCueUtils test should resolve the runtime gameplay cue set"), RuntimeCueSet))
	{
		return false;
	}

	const FSoftObjectPath CueObjectPath(Recorder->GetPathName());
	FGameplayCueNotifyData CueData;
	CueData.GameplayCueTag = AngelscriptGameplayCueUtilsLocalWrapperTag;
	CueData.GameplayCueNotifyObj = CueObjectPath;
	CueData.LoadedGameplayCueClass = UAngelscriptGASTestGameplayCueRecorder::StaticClass();
	RuntimeCueSet->GameplayCueData.Add(CueData);
#if WITH_EDITOR
	RuntimeCueSet->UpdateCueByStringRefs(CueObjectPath, Recorder->GetPathName());
#else
	RuntimeCueSet->GameplayCueDataMap.Add(CueData.GameplayCueTag, RuntimeCueSet->GameplayCueData.Num() - 1);
#endif
	ON_SCOPE_EXIT
	{
		RuntimeCueSet->RemoveCuesByStringRefs({ CueObjectPath });
	};

	FGameplayCueParameters CueParameters;
	CueParameters.RawMagnitude = 37.5f;
	CueParameters.Instigator = InstigatorActor;
	CueParameters.EffectCauser = InstigatorActor;
	CueParameters.SourceObject = InstigatorActor;

	UAngelscriptGameplayCueUtils::AddLocalGameplayCue(TargetActor, AngelscriptGameplayCueUtilsLocalWrapperTag, CueParameters);
	UAngelscriptGameplayCueUtils::ExecuteLocalGameplayCue(TargetActor, AngelscriptGameplayCueUtilsLocalWrapperTag, CueParameters);
	UAngelscriptGameplayCueUtils::RemoveLocalGameplayCue(TargetActor, AngelscriptGameplayCueUtilsLocalWrapperTag, CueParameters);

	if (!TestEqual(
			TEXT("GameplayCueUtils local wrappers should dispatch four cue events in total"),
			Recorder->RecordedEventTypes.Num(),
			4))
	{
		return false;
	}

	bool bPassed = true;
	bPassed &= CheckRecordedCueEvent(
		*this,
		*Recorder,
		0,
		EGameplayCueEvent::OnActive,
		TargetActor,
		InstigatorActor,
		CueParameters.RawMagnitude);
	bPassed &= CheckRecordedCueEvent(
		*this,
		*Recorder,
		1,
		EGameplayCueEvent::WhileActive,
		TargetActor,
		InstigatorActor,
		CueParameters.RawMagnitude);
	bPassed &= CheckRecordedCueEvent(
		*this,
		*Recorder,
		2,
		EGameplayCueEvent::Executed,
		TargetActor,
		InstigatorActor,
		CueParameters.RawMagnitude);
	bPassed &= CheckRecordedCueEvent(
		*this,
		*Recorder,
		3,
		EGameplayCueEvent::Removed,
		TargetActor,
		InstigatorActor,
		CueParameters.RawMagnitude);
	if (!bPassed)
	{
		return false;
	}

	const int32 EventCountBeforeNullTarget = Recorder->RecordedEventTypes.Num();
	UAngelscriptGameplayCueUtils::AddLocalGameplayCue(nullptr, AngelscriptGameplayCueUtilsLocalWrapperTag, CueParameters);
	UAngelscriptGameplayCueUtils::ExecuteLocalGameplayCue(nullptr, AngelscriptGameplayCueUtilsLocalWrapperTag, CueParameters);
	UAngelscriptGameplayCueUtils::RemoveLocalGameplayCue(nullptr, AngelscriptGameplayCueUtilsLocalWrapperTag, CueParameters);

	TestEqual(
		TEXT("GameplayCueUtils null-target calls should not append any recorder events"),
		Recorder->RecordedEventTypes.Num(),
		EventCountBeforeNullTarget);

	return true;
}

#endif
