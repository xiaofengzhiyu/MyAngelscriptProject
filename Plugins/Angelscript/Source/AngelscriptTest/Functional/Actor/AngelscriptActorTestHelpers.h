#pragma once

#include "Shared/AngelscriptFunctionalTestUtils.h"
#include "Shared/AngelscriptReflectiveAccess.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Components/ActorTestSpawner.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptActorTestUtils
{
	using namespace AngelscriptTestSupport;
	using namespace AngelscriptFunctionalTestUtils;
	using namespace AngelscriptReflectiveAccess;

	constexpr float DefaultActorTestDeltaTime = 0.016f;
	constexpr int32 DefaultActorTestTickCount = 3;

	inline void EnableActorTick(AActor& Actor)
	{
		Actor.PrimaryActorTick.bCanEverTick = true;
		Actor.SetActorTickEnabled(true);
		Actor.RegisterAllActorTickFunctions(true, false);
	}

	inline void TickWorldThroughTickManager(
		FAngelscriptEngine& Engine,
		UWorld& World,
		float DeltaTime,
		int32 NumTicks)
	{
		for (int32 TickIndex = 0; TickIndex < NumTicks; ++TickIndex)
		{
			FAngelscriptEngineScope WorldScope(Engine);
			World.Tick(ELevelTick::LEVELTICK_All, DeltaTime);
		}
	}

	inline FAngelscriptEngine& AcquireFreshActorEngine()
	{
		DestroySharedAndStrayGlobalTestEngine();
		return AcquireCleanSharedCloneEngine();
	}

	struct FScopedActorWorld
	{
		explicit FScopedActorWorld(FAutomationTestBase& InTest)
			: Test(InTest)
		{
			Spawner.InitializeGameSubsystems();
			World = &Spawner.GetWorld();
			bIsValid = Test.TestNotNull(TEXT("Actor test world should be created"), World);
		}

		bool IsValid() const { return bIsValid && World != nullptr; }

		UWorld& GetWorld() const { return *World; }
		FActorTestSpawner& GetSpawner() { return Spawner; }

		template <typename ActorType = AActor>
		ActorType* SpawnActorOfClass(
			UClass* ActorClass,
			const FActorSpawnParameters& SpawnParameters = FActorSpawnParameters(),
			const FVector& Location = FVector::ZeroVector,
			const FRotator& Rotation = FRotator::ZeroRotator)
		{
			return SpawnScriptActor<ActorType>(Test, Spawner, ActorClass, SpawnParameters, Location, Rotation);
		}

		void BeginPlay(FAngelscriptEngine& Engine, AActor& Actor) const
		{
			BeginPlayActor(Engine, Actor);
		}

		void BeginPlay(AActor& Actor) const
		{
			BeginPlayActor(Actor);
		}

		void Tick(FAngelscriptEngine& Engine, float DeltaTime, int32 NumTicks) const
		{
			if (IsValid())
			{
				TickWorld(Engine, *World, DeltaTime, NumTicks);
			}
		}

		void TickViaManager(FAngelscriptEngine& Engine, float DeltaTime, int32 NumTicks) const
		{
			if (IsValid())
			{
				TickWorldThroughTickManager(Engine, *World, DeltaTime, NumTicks);
			}
		}

	private:
		FAutomationTestBase& Test;
		FActorTestSpawner Spawner;
		UWorld* World = nullptr;
		bool bIsValid = false;
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
