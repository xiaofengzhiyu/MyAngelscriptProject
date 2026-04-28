#include "Shared/AngelscriptScenarioTestUtils.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Components/ActorTestSpawner.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace AngelscriptTest_Bindings_AngelscriptSystemTimersBindingsTests_Private
{
	static constexpr float TimerIntervalSeconds = 0.05f;
	static constexpr float TimerTickDeltaSeconds = 0.02f;
	static constexpr int32 TicksWhilePaused = 8;
	static constexpr int32 TicksAfterResume = 8;

	FString BuildSystemTimerBindingScript()
	{
		return FString::Printf(TEXT(R"AS(
UCLASS()
class ASystemTimerBindingActor : AActor
{
	UPROPERTY()
	int32 CallbackCount = 0;

	UPROPERTY()
	FTimerHandle ManagedTimer;

	UFUNCTION()
	void OnManagedTimer()
	{
		CallbackCount++;
	}

	UFUNCTION()
	int StartPausedOneShotTimer()
	{
		ManagedTimer = System::SetTimer(this, n"OnManagedTimer", %0.2ff, false);
		if (System::IsTimerPausedHandle(ManagedTimer))
			return 10;

		System::PauseTimerHandle(ManagedTimer);
		if (!System::IsTimerPausedHandle(ManagedTimer))
			return 20;

		return 1;
	}

	UFUNCTION()
	int ResumePausedOneShotTimer()
	{
		if (!System::IsTimerPausedHandle(ManagedTimer))
			return 10;

		System::UnPauseTimerHandle(ManagedTimer);
		if (System::IsTimerPausedHandle(ManagedTimer))
			return 20;

		return 1;
	}

	UFUNCTION()
	int StartActiveLoopingTimer()
	{
		ManagedTimer = System::SetTimer(this, n"OnManagedTimer", %0.2ff, true);
		if (System::IsTimerPausedHandle(ManagedTimer))
			return 10;

		return 1;
	}

	UFUNCTION()
	int ClearManagedTimer()
	{
		System::ClearAndInvalidateTimerHandle(ManagedTimer);
		if (System::IsTimerPausedHandle(ManagedTimer))
			return 10;

		return 1;
	}
}
)AS"), TimerIntervalSeconds, TimerIntervalSeconds);
	}

	UClass* CompileSystemTimerActorClass(FAutomationTestBase& Test, FAngelscriptEngine& Engine, FName ModuleName)
	{
		return CompileScriptModule(
			Test,
			Engine,
			ModuleName,
			FString::Printf(TEXT("%s.as"), *ModuleName.ToString()),
			BuildSystemTimerBindingScript(),
			TEXT("ASystemTimerBindingActor"));
	}

	bool ReadTimerHandleProperty(FAutomationTestBase& Test, UObject* Object, FName PropertyName, FTimerHandle& OutHandle)
	{
		if (!Test.TestNotNull(TEXT("System timer bindings scenario object should exist for timer-handle reads"), Object))
		{
			return false;
		}

		FStructProperty* Property = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName);
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("System timer bindings scenario property '%s' should exist"), *PropertyName.ToString()),
			Property))
		{
			return false;
		}

		const FTimerHandle* HandleValue = Property->ContainerPtrToValuePtr<FTimerHandle>(Object);
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("System timer bindings scenario property '%s' should expose a timer handle value"), *PropertyName.ToString()),
			HandleValue))
		{
			return false;
		}

		OutHandle = *HandleValue;
		return true;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptSystemTimersBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSystemTimersPauseResumeBindingsTest,
	"Angelscript.TestModule.Bindings.SystemTimers.PauseResumeOneShotHandle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSystemTimersClearInvalidateBindingsTest,
	"Angelscript.TestModule.Bindings.SystemTimers.ClearInvalidateLoopingHandle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptSystemTimersPauseResumeBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	static const FName ModuleName(TEXT("BindingsSystemTimersPauseResume"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileSystemTimerActorClass(*this, Engine, ModuleName);
	if (ScriptClass == nullptr)
	{
		return false;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor* Actor = SpawnScriptActor(*this, Spawner, ScriptClass);
	if (Actor == nullptr)
	{
		return false;
	}

	BeginPlayActor(Engine, *Actor);

	UWorld* World = Actor->GetWorld();
	if (!TestNotNull(TEXT("System timer pause/resume bindings test should resolve the spawned actor world"), World))
	{
		return false;
	}

	UFunction* StartPausedFunction = FindGeneratedFunction(ScriptClass, TEXT("StartPausedOneShotTimer"));
	UFunction* ResumeFunction = FindGeneratedFunction(ScriptClass, TEXT("ResumePausedOneShotTimer"));
	if (!TestNotNull(TEXT("System timer pause/resume bindings test should expose StartPausedOneShotTimer"), StartPausedFunction)
		|| !TestNotNull(TEXT("System timer pause/resume bindings test should expose ResumePausedOneShotTimer"), ResumeFunction))
	{
		return false;
	}

	int32 StartResult = INDEX_NONE;
	if (!TestTrue(
		TEXT("System timer pause/resume bindings test should execute StartPausedLoopingTimer on the game thread"),
		ExecuteGeneratedIntEventOnGameThread(&Engine, Actor, StartPausedFunction, StartResult)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("System::SetTimer plus PauseTimerHandle should succeed for a one-shot actor timer"),
		StartResult,
		1);

	FTimerHandle ManagedHandle;
	if (!ReadTimerHandleProperty(*this, Actor, TEXT("ManagedTimer"), ManagedHandle))
	{
		return false;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	bPassed &= TestTrue(
		TEXT("System timer pause/resume bindings test should store a valid native timer handle"),
		ManagedHandle.IsValid());
	bPassed &= TestTrue(
		TEXT("System timer pause/resume bindings test should register the timer with the native timer manager"),
		TimerManager.TimerExists(ManagedHandle));
	bPassed &= TestTrue(
		TEXT("PauseTimerHandle should leave the native timer manager in a paused state"),
		TimerManager.IsTimerPaused(ManagedHandle));
	const float RemainingBeforePausedTicks = TimerManager.GetTimerRemaining(ManagedHandle);
	bPassed &= TestTrue(
		TEXT("Paused one-shot timer should report remaining time before the world advances"),
		RemainingBeforePausedTicks > 0.0f);

	TickWorld(Engine, *World, TimerTickDeltaSeconds, TicksWhilePaused);
	const float RemainingAfterPausedTicks = TimerManager.GetTimerRemaining(ManagedHandle);
	bPassed &= TestTrue(
		TEXT("Paused one-shot timer should still exist after the world advances"),
		TimerManager.TimerExists(ManagedHandle));
	bPassed &= TestTrue(
		TEXT("Paused one-shot timer should preserve its remaining time while paused"),
		FMath::IsNearlyEqual(RemainingAfterPausedTicks, RemainingBeforePausedTicks, 0.005f));

	int32 ResumeResult = INDEX_NONE;
	if (!TestTrue(
		TEXT("System timer pause/resume bindings test should execute ResumePausedOneShotTimer on the game thread"),
		ExecuteGeneratedIntEventOnGameThread(&Engine, Actor, ResumeFunction, ResumeResult)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("UnPauseTimerHandle should resume the previously paused one-shot timer"),
		ResumeResult,
		1);
	bPassed &= TestFalse(
		TEXT("UnPauseTimerHandle should clear the native paused state"),
		TimerManager.IsTimerPaused(ManagedHandle));
	bPassed &= TestTrue(
		TEXT("UnPauseTimerHandle should restore the native active state for the one-shot timer"),
		TimerManager.IsTimerActive(ManagedHandle));
	bPassed &= TestTrue(
		TEXT("UnPauseTimerHandle should keep the native timer handle registered after resuming"),
		TimerManager.TimerExists(ManagedHandle));
	bPassed &= TestTrue(
		TEXT("UnPauseTimerHandle should keep a readable positive remaining time for the resumed one-shot timer"),
		TimerManager.GetTimerRemaining(ManagedHandle) > 0.0f);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptSystemTimersClearInvalidateBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	static const FName ModuleName(TEXT("BindingsSystemTimersClearInvalidate"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileSystemTimerActorClass(*this, Engine, ModuleName);
	if (ScriptClass == nullptr)
	{
		return false;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor* Actor = SpawnScriptActor(*this, Spawner, ScriptClass);
	if (Actor == nullptr)
	{
		return false;
	}

	BeginPlayActor(Engine, *Actor);

	UWorld* World = Actor->GetWorld();
	if (!TestNotNull(TEXT("System timer clear/invalidate bindings test should resolve the spawned actor world"), World))
	{
		return false;
	}

	UFunction* StartActiveFunction = FindGeneratedFunction(ScriptClass, TEXT("StartActiveLoopingTimer"));
	UFunction* ClearFunction = FindGeneratedFunction(ScriptClass, TEXT("ClearManagedTimer"));
	if (!TestNotNull(TEXT("System timer clear/invalidate bindings test should expose StartActiveLoopingTimer"), StartActiveFunction)
		|| !TestNotNull(TEXT("System timer clear/invalidate bindings test should expose ClearManagedTimer"), ClearFunction))
	{
		return false;
	}

	int32 StartResult = INDEX_NONE;
	if (!TestTrue(
		TEXT("System timer clear/invalidate bindings test should execute StartActiveLoopingTimer on the game thread"),
		ExecuteGeneratedIntEventOnGameThread(&Engine, Actor, StartActiveFunction, StartResult)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("System::SetTimer should create an active looping timer before clear/invalidate"),
		StartResult,
		1);

	FTimerHandle OriginalHandle;
	if (!ReadTimerHandleProperty(*this, Actor, TEXT("ManagedTimer"), OriginalHandle))
	{
		return false;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	bPassed &= TestTrue(
		TEXT("System timer clear/invalidate bindings test should register the active timer"),
		TimerManager.TimerExists(OriginalHandle));
	bPassed &= TestTrue(
		TEXT("System timer clear/invalidate bindings test should start the timer in the active state"),
		TimerManager.IsTimerActive(OriginalHandle));

	int32 ClearResult = INDEX_NONE;
	if (!TestTrue(
		TEXT("System timer clear/invalidate bindings test should execute ClearManagedTimer on the game thread"),
		ExecuteGeneratedIntEventOnGameThread(&Engine, Actor, ClearFunction, ClearResult)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("ClearAndInvalidateTimerHandle should clear the active timer without error"),
		ClearResult,
		1);

	FTimerHandle ClearedHandle;
	if (!ReadTimerHandleProperty(*this, Actor, TEXT("ManagedTimer"), ClearedHandle))
	{
		return false;
	}

	bPassed &= TestFalse(
		TEXT("ClearAndInvalidateTimerHandle should invalidate the script-visible timer handle property"),
		ClearedHandle.IsValid());
	bPassed &= TestFalse(
		TEXT("ClearAndInvalidateTimerHandle should remove the original handle from the native timer manager"),
		TimerManager.TimerExists(OriginalHandle));
	bPassed &= TestFalse(
		TEXT("Cleared looping timer should no longer be active in the native timer manager"),
		TimerManager.IsTimerActive(OriginalHandle));

	int32 CallbackCountAfterClear = INDEX_NONE;
	TickWorld(Engine, *World, TimerTickDeltaSeconds, TicksAfterResume);
	if (!ReadPropertyValue<FIntProperty>(*this, Actor, TEXT("CallbackCount"), CallbackCountAfterClear))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Cleared looping timer should never invoke the callback when the world advances"),
		CallbackCountAfterClear,
		0);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
