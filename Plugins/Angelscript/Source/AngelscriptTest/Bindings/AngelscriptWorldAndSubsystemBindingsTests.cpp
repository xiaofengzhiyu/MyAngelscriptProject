#include "../Shared/AngelscriptScenarioTestUtils.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ViewportStatsSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ReplaySubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptScenarioTestUtils;
using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptWorldStateCompatBindingsTest,
	"Angelscript.TestModule.Bindings.WorldAndSubsystem.WorldStateCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSubsystemGetCompatBindingsTest,
	"Angelscript.TestModule.Bindings.WorldAndSubsystem.SubsystemGetCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptWorldAndSubsystemBindingsTests_Private
{
	static constexpr ANSICHAR WorldStateModuleName[] = "ASWorldStateCompat";
	static constexpr ANSICHAR SubsystemGetModuleName[] = "ASSubsystemGetCompat";
	static constexpr float WorldTickDeltaTime = 0.125f;
	static constexpr int32 WorldTickCount = 2;
	static constexpr int32 LocalPlayerControllerId = 11;
	static constexpr TCHAR EnhancedInputLocalPlayerSubsystemClassPath[] = TEXT("/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");

	FString FormatScriptFloatLiteral(const double Value)
	{
		FString Literal = FString::Printf(TEXT("%.17g"), Value);
		if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e")) && !Literal.Contains(TEXT("E")))
		{
			Literal += TEXT(".0");
		}

		return Literal;
	}

	const TCHAR* FormatScriptBoolLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	bool ExecuteIntFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		int32& OutResult)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should create an execution context"), ContextLabel), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		if (!BindArguments(*Context))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}

	struct FGameInstanceLocalPlayerFixture
	{
		~FGameInstanceLocalPlayerFixture()
		{
			Shutdown();
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("WorldAndSubsystem fixture should have a live GEngine"), GEngine))
			{
				return false;
			}

			const FName PackageName = MakeUniqueObjectName(
				nullptr,
				UPackage::StaticClass(),
				FName(TEXT("/Angelscript_Test_WorldAndSubsystem")));
			Package = NewObject<UPackage>(GetTransientPackage(), PackageName, RF_Transient);
			if (!Test.TestNotNull(TEXT("WorldAndSubsystem fixture should create a transient world package"), Package))
			{
				return false;
			}

			GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			if (!Test.TestNotNull(TEXT("WorldAndSubsystem fixture should create a standalone game instance"), GameInstance))
			{
				return false;
			}

			GameInstance->InitializeStandalone(TEXT("AngelscriptWorldAndSubsystemWorld"), Package);
			World = GameInstance->GetWorld();
			WorldContext = GameInstance->GetWorldContext();
			if (!Test.TestNotNull(TEXT("WorldAndSubsystem fixture should initialize a standalone world"), World)
				|| !Test.TestNotNull(TEXT("WorldAndSubsystem fixture should expose a world context"), WorldContext))
			{
				return false;
			}

			UClass* ViewportClass = GEngine->GameViewportClientClass != nullptr
				? GEngine->GameViewportClientClass.Get()
				: UGameViewportClient::StaticClass();
			GameViewport = NewObject<UGameViewportClient>(GEngine, ViewportClass);
			if (!Test.TestNotNull(TEXT("WorldAndSubsystem fixture should create a viewport client"), GameViewport))
			{
				return false;
			}

			GameViewport->Init(*WorldContext, GameInstance, /*bCreateNewAudioDevice*/false);
			WorldContext->GameViewport = GameViewport;

			FString Error;
			LocalPlayer = GameInstance->CreateLocalPlayer(LocalPlayerControllerId, Error, /*bSpawnPlayerController*/false);
			if (!Test.TestNotNull(TEXT("WorldAndSubsystem fixture should create a local player"), LocalPlayer))
			{
				return false;
			}

			return Test.TestTrue(TEXT("WorldAndSubsystem fixture should create the local player without errors"), Error.IsEmpty());
		}

		void Shutdown()
		{
			if (GameInstance == nullptr && World == nullptr)
			{
				return;
			}

			if (GameInstance != nullptr && LocalPlayer != nullptr)
			{
				GameInstance->RemoveLocalPlayer(LocalPlayer);
			}

			if (World != nullptr)
			{
				World->BeginTearingDown();
			}

			if (GameInstance != nullptr)
			{
				GameInstance->Shutdown();
			}

			if (WorldContext != nullptr)
			{
				WorldContext->GameViewport = nullptr;
			}

			if (World != nullptr)
			{
				World->DestroyWorld(false);
				if (GEngine != nullptr)
				{
					GEngine->DestroyWorldContext(World);
				}
			}

			LocalPlayer = nullptr;
			GameViewport = nullptr;
			WorldContext = nullptr;
			World = nullptr;
			GameInstance = nullptr;
			Package = nullptr;
		}

		UPackage* Package = nullptr;
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		FWorldContext* WorldContext = nullptr;
		UGameViewportClient* GameViewport = nullptr;
		ULocalPlayer* LocalPlayer = nullptr;
	};
}

using namespace AngelscriptTest_Bindings_AngelscriptWorldAndSubsystemBindingsTests_Private;

bool FAngelscriptWorldStateCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& ContextActor = Spawner.SpawnActor<AActor>();
	UWorld* TestWorld = ContextActor.GetWorld();
	if (!TestNotNull(TEXT("World state bindings test should access the spawned test world"), TestWorld))
	{
		return false;
	}

	TickWorld(Engine, *TestWorld, WorldTickDeltaTime, WorldTickCount);

	ULevel* PersistentLevel = TestWorld->PersistentLevel;
	ALevelScriptActor* LevelScriptActor = TestWorld->GetLevelScriptActor();
	if (!TestNotNull(TEXT("World state bindings test should expose a persistent level"), PersistentLevel))
	{
		return false;
	}

	const double ExpectedTimeSeconds = TestWorld->GetTimeSeconds();
	const double ExpectedUnpausedTimeSeconds = TestWorld->GetUnpausedTimeSeconds();
	const double ExpectedRealTimeSeconds = TestWorld->GetRealTimeSeconds();
	const double ExpectedAudioTimeSeconds = TestWorld->GetAudioTimeSeconds();
	const float ExpectedDeltaSeconds = TestWorld->GetDeltaSeconds();
	const bool bExpectedIsGameWorld = TestWorld->IsGameWorld();
	const bool bExpectedIsEditorWorld = TestWorld->IsEditorWorld();
	const bool bExpectedIsPreviewWorld = TestWorld->IsPreviewWorld();
	const bool bExpectedIsStartingUp = TestWorld->bStartup;
	const bool bExpectedIsTearingDown = TestWorld->bIsTearingDown;
	const bool bExpectedLevelVisible = PersistentLevel->bIsVisible;
	const bool bExpectedLevelBeingRemoved = PersistentLevel->bIsBeingRemoved;

	FString ScriptSource = TEXT(R"AS(
bool NearlyEqual(float64 Observed, float64 Expected, float64 Tolerance)
{
	return Observed >= Expected - Tolerance && Observed <= Expected + Tolerance;
}

int VerifyWorldState(UWorld World, ULevel ExpectedPersistentLevel, ALevelScriptActor ExpectedLevelScriptActor)
{
	int MismatchMask = 0;

	if (!NearlyEqual(World.GetTimeSeconds(), __EXPECTED_TIME_SECONDS__, __TIME_TOLERANCE__))
		MismatchMask |= 1;
	if (!NearlyEqual(World.GetUnpausedTimeSeconds(), __EXPECTED_UNPAUSED_TIME_SECONDS__, __TIME_TOLERANCE__))
		MismatchMask |= 2;
	if (!NearlyEqual(World.GetRealTimeSeconds(), __EXPECTED_REAL_TIME_SECONDS__, __TIME_TOLERANCE__))
		MismatchMask |= 4;
	if (!NearlyEqual(World.GetAudioTimeSeconds(), __EXPECTED_AUDIO_TIME_SECONDS__, __TIME_TOLERANCE__))
		MismatchMask |= 8;
	if (!NearlyEqual(float64(World.GetDeltaSeconds()), __EXPECTED_DELTA_SECONDS__, __TIME_TOLERANCE__))
		MismatchMask |= 16;
	if (World.IsGameWorld() != __EXPECTED_IS_GAME_WORLD__)
		MismatchMask |= 32;
	if (World.IsEditorWorld() != __EXPECTED_IS_EDITOR_WORLD__)
		MismatchMask |= 64;
	if (World.IsPreviewWorld() != __EXPECTED_IS_PREVIEW_WORLD__)
		MismatchMask |= 128;
	if (World.IsStartingUp() != __EXPECTED_IS_STARTING_UP__)
		MismatchMask |= 256;
	if (World.IsTearingDown() != __EXPECTED_IS_TEARING_DOWN__)
		MismatchMask |= 512;
	if (World.GetPersistentLevel() != ExpectedPersistentLevel)
		MismatchMask |= 1024;
	if (World.GetLevelScriptActor() != ExpectedLevelScriptActor)
		MismatchMask |= 2048;
	if (ExpectedPersistentLevel.GetLevelScriptActor() != ExpectedLevelScriptActor)
		MismatchMask |= 4096;
	if (ExpectedPersistentLevel.IsVisible() != __EXPECTED_LEVEL_VISIBLE__)
		MismatchMask |= 8192;
	if (ExpectedPersistentLevel.IsBeingRemoved() != __EXPECTED_LEVEL_BEING_REMOVED__)
		MismatchMask |= 16384;

	return MismatchMask;
}
)AS");

	ScriptSource.ReplaceInline(TEXT("__EXPECTED_TIME_SECONDS__"), *FormatScriptFloatLiteral(ExpectedTimeSeconds), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_UNPAUSED_TIME_SECONDS__"), *FormatScriptFloatLiteral(ExpectedUnpausedTimeSeconds), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_REAL_TIME_SECONDS__"), *FormatScriptFloatLiteral(ExpectedRealTimeSeconds), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_AUDIO_TIME_SECONDS__"), *FormatScriptFloatLiteral(ExpectedAudioTimeSeconds), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_DELTA_SECONDS__"), *FormatScriptFloatLiteral(ExpectedDeltaSeconds), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__TIME_TOLERANCE__"), TEXT("0.000001"), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_IS_GAME_WORLD__"), FormatScriptBoolLiteral(bExpectedIsGameWorld), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_IS_EDITOR_WORLD__"), FormatScriptBoolLiteral(bExpectedIsEditorWorld), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_IS_PREVIEW_WORLD__"), FormatScriptBoolLiteral(bExpectedIsPreviewWorld), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_IS_STARTING_UP__"), FormatScriptBoolLiteral(bExpectedIsStartingUp), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_IS_TEARING_DOWN__"), FormatScriptBoolLiteral(bExpectedIsTearingDown), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_LEVEL_VISIBLE__"), FormatScriptBoolLiteral(bExpectedLevelVisible), ESearchCase::CaseSensitive);
	ScriptSource.ReplaceInline(TEXT("__EXPECTED_LEVEL_BEING_REMOVED__"), FormatScriptBoolLiteral(bExpectedLevelBeingRemoved), ESearchCase::CaseSensitive);

	asIScriptModule* Module = BuildModule(*this, Engine, WorldStateModuleName, ScriptSource);
	if (Module == nullptr)
	{
		return false;
	}

	int32 ResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
		*this,
		Engine,
		*Module,
		TEXT("int VerifyWorldState(UWorld, ULevel, ALevelScriptActor)"),
		[this, TestWorld, PersistentLevel, LevelScriptActor](asIScriptContext& Context)
		{
			return SetArgObjectChecked(*this, Context, 0, TestWorld, TEXT("VerifyWorldState"))
				&& SetArgObjectChecked(*this, Context, 1, PersistentLevel, TEXT("VerifyWorldState"))
				&& SetArgObjectChecked(*this, Context, 2, LevelScriptActor, TEXT("VerifyWorldState"));
		},
		TEXT("VerifyWorldState"),
		ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("UWorld bindings should preserve timing accessors, state predicates, and level accessors"),
		ResultMask,
		0);

	ASTEST_END_FULL
	return bPassed;
}

bool FAngelscriptSubsystemGetCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FGameInstanceLocalPlayerFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UWorld* TestWorld = Fixture.World;
	UGameInstance* GameInstance = Fixture.GameInstance;
	ULocalPlayer* LocalPlayer = Fixture.LocalPlayer;
	if (!TestNotNull(TEXT("Subsystem Get bindings test should expose the standalone world"), TestWorld)
		|| !TestNotNull(TEXT("Subsystem Get bindings test should expose the standalone game instance"), GameInstance)
		|| !TestNotNull(TEXT("Subsystem Get bindings test should expose the created local player"), LocalPlayer))
	{
		return false;
	}

	UReplaySubsystem* ReplaySubsystem = GameInstance->GetSubsystem<UReplaySubsystem>();
	UViewportStatsSubsystem* ViewportStatsSubsystem = TestWorld->GetSubsystem<UViewportStatsSubsystem>();
	if (!TestNotNull(TEXT("Subsystem Get bindings test should expose the replay subsystem"), ReplaySubsystem)
		|| !TestNotNull(TEXT("Subsystem Get bindings test should expose the viewport stats subsystem"), ViewportStatsSubsystem))
	{
		return false;
	}

	UClass* EnhancedInputLocalPlayerSubsystemClass = LoadClass<ULocalPlayerSubsystem>(nullptr, EnhancedInputLocalPlayerSubsystemClassPath);
	if (!TestNotNull(TEXT("Subsystem Get bindings test should load the EnhancedInput local-player subsystem class"), EnhancedInputLocalPlayerSubsystemClass))
	{
		return false;
	}

	ULocalPlayerSubsystem* EnhancedInputLocalPlayerSubsystem = LocalPlayer->GetSubsystemBase(EnhancedInputLocalPlayerSubsystemClass);

	FScopedTestWorldContextScope WorldContextScope(TestWorld);

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		SubsystemGetModuleName,
		TEXT(R"AS(
int VerifySubsystemGet(
	UWorld ExpectedWorld,
	UGameInstance ExpectedGameInstance,
	UReplaySubsystem ExpectedReplaySubsystem,
	UViewportStatsSubsystem ExpectedViewportStatsSubsystem,
	ULocalPlayer ExpectedLocalPlayer,
	UEnhancedInputLocalPlayerSubsystem ExpectedEnhancedInputLocalPlayerSubsystem)
{
	int MismatchMask = 0;

	if (UReplaySubsystem::Get() != ExpectedReplaySubsystem)
		MismatchMask |= 1;
	if (UViewportStatsSubsystem::Get() != ExpectedViewportStatsSubsystem)
		MismatchMask |= 2;
	if (UEnhancedInputLocalPlayerSubsystem::Get(ExpectedLocalPlayer) != ExpectedEnhancedInputLocalPlayerSubsystem)
		MismatchMask |= 4;
	if (ExpectedLocalPlayer.GetGameInstance() != ExpectedGameInstance)
		MismatchMask |= 8;
	if (ExpectedLocalPlayer.GetWorld() != ExpectedWorld)
		MismatchMask |= 16;

	return MismatchMask;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	int32 ResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
		*this,
		Engine,
		*Module,
		TEXT("int VerifySubsystemGet(UWorld, UGameInstance, UReplaySubsystem, UViewportStatsSubsystem, ULocalPlayer, UEnhancedInputLocalPlayerSubsystem)"),
		[this, TestWorld, GameInstance, ReplaySubsystem, ViewportStatsSubsystem, LocalPlayer, EnhancedInputLocalPlayerSubsystem](asIScriptContext& Context)
		{
			return SetArgObjectChecked(*this, Context, 0, TestWorld, TEXT("VerifySubsystemGet"))
				&& SetArgObjectChecked(*this, Context, 1, GameInstance, TEXT("VerifySubsystemGet"))
				&& SetArgObjectChecked(*this, Context, 2, ReplaySubsystem, TEXT("VerifySubsystemGet"))
				&& SetArgObjectChecked(*this, Context, 3, ViewportStatsSubsystem, TEXT("VerifySubsystemGet"))
				&& SetArgObjectChecked(*this, Context, 4, LocalPlayer, TEXT("VerifySubsystemGet"))
				&& SetArgObjectChecked(*this, Context, 5, EnhancedInputLocalPlayerSubsystem, TEXT("VerifySubsystemGet"));
		},
		TEXT("VerifySubsystemGet"),
		ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Subsystem Get bindings should preserve world, game-instance, and local-player subsystem lookup semantics"),
		ResultMask,
		0);

	ASTEST_END_FULL
	return bPassed;
}

#endif
