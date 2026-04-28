#include "../Shared/AngelscriptTestMacros.h"

#include "FunctionLibraries/SubsystemLibrary.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ViewportStatsSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "ReplaySubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptSubsystemFunctionLibraryTests_Private
{
	static constexpr int32 LocalPlayerControllerId = 37;
	static constexpr TCHAR EnhancedInputLocalPlayerSubsystemClassPath[] = TEXT("/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");

	UInputDeviceSubsystem* ResolveInputDeviceSubsystem()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}

		UInputDeviceSubsystem* Subsystem = GEngine->GetEngineSubsystem<UInputDeviceSubsystem>();
		if (Subsystem == nullptr)
		{
			FSubsystemCollectionBase::ActivateExternalSubsystem(UInputDeviceSubsystem::StaticClass());
			Subsystem = GEngine->GetEngineSubsystem<UInputDeviceSubsystem>();
		}

		return Subsystem;
	}

	struct FSubsystemFunctionLibraryFixture
	{
		~FSubsystemFunctionLibraryFixture()
		{
			Shutdown();
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Subsystem function-library fixture should have a live GEngine"), GEngine))
			{
				return false;
			}

			const FName PackageName = MakeUniqueObjectName(
				nullptr,
				UPackage::StaticClass(),
				FName(TEXT("/Angelscript_Test_SubsystemFunctionLibrary")));
			Package = NewObject<UPackage>(GetTransientPackage(), PackageName, RF_Transient);
			if (!Test.TestNotNull(TEXT("Subsystem function-library fixture should create a transient world package"), Package))
			{
				return false;
			}

			GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			if (!Test.TestNotNull(TEXT("Subsystem function-library fixture should create a standalone game instance"), GameInstance))
			{
				return false;
			}

			GameInstance->InitializeStandalone(TEXT("AngelscriptSubsystemFunctionLibraryWorld"), Package);
			World = GameInstance->GetWorld();
			WorldContext = GameInstance->GetWorldContext();
			if (!Test.TestNotNull(TEXT("Subsystem function-library fixture should initialize a standalone world"), World)
				|| !Test.TestNotNull(TEXT("Subsystem function-library fixture should expose a world context"), WorldContext))
			{
				return false;
			}

			UClass* ViewportClass = GEngine->GameViewportClientClass != nullptr
				? GEngine->GameViewportClientClass.Get()
				: UGameViewportClient::StaticClass();
			GameViewport = NewObject<UGameViewportClient>(GEngine, ViewportClass);
			if (!Test.TestNotNull(TEXT("Subsystem function-library fixture should create a viewport client"), GameViewport))
			{
				return false;
			}

			GameViewport->Init(*WorldContext, GameInstance, false);
			WorldContext->GameViewport = GameViewport;

			FString CreateError;
			LocalPlayer = GameInstance->CreateLocalPlayer(LocalPlayerControllerId, CreateError, false);
			if (!Test.TestNotNull(TEXT("Subsystem function-library fixture should create a local player"), LocalPlayer))
			{
				return false;
			}

			if (!Test.TestTrue(TEXT("Subsystem function-library fixture should create the local player without errors"), CreateError.IsEmpty()))
			{
				return false;
			}

			BoundPlayerController = World->SpawnActor<APlayerController>();
			UnboundPlayerController = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("Subsystem function-library fixture should spawn a bound player controller"), BoundPlayerController)
				|| !Test.TestNotNull(TEXT("Subsystem function-library fixture should spawn an unbound player controller"), UnboundPlayerController))
			{
				return false;
			}

			BoundPlayerController->SetPlayer(LocalPlayer);
			return Test.TestTrue(
				TEXT("Subsystem function-library fixture should assign the local player to the bound controller"),
				BoundPlayerController->GetLocalPlayer() == LocalPlayer)
				&& Test.TestNull(
					TEXT("Subsystem function-library fixture should keep the control controller without a local player"),
					UnboundPlayerController->GetLocalPlayer());
		}

		void Shutdown()
		{
			if (GameInstance == nullptr && World == nullptr)
			{
				return;
			}

			if (BoundPlayerController != nullptr)
			{
				BoundPlayerController->Destroy();
			}

			if (UnboundPlayerController != nullptr)
			{
				UnboundPlayerController->Destroy();
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

			UnboundPlayerController = nullptr;
			BoundPlayerController = nullptr;
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
		APlayerController* BoundPlayerController = nullptr;
		APlayerController* UnboundPlayerController = nullptr;
	};
}

using namespace AngelscriptTest_Bindings_AngelscriptSubsystemFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSubsystemFunctionLibraryLookupTest,
	"Angelscript.TestModule.FunctionLibraries.SubsystemLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptSubsystemFunctionLibraryLookupTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FSubsystemFunctionLibraryFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UInputDeviceSubsystem* ExpectedEngineSubsystem = ResolveInputDeviceSubsystem();
	UReplaySubsystem* ExpectedReplaySubsystem = Fixture.GameInstance->GetSubsystem<UReplaySubsystem>();
	UViewportStatsSubsystem* ExpectedViewportStatsSubsystem = Fixture.World->GetSubsystem<UViewportStatsSubsystem>();
	if (!TestNotNull(TEXT("Subsystem function-library test should resolve the engine subsystem"), ExpectedEngineSubsystem)
		|| !TestNotNull(TEXT("Subsystem function-library test should resolve the game-instance subsystem"), ExpectedReplaySubsystem)
		|| !TestNotNull(TEXT("Subsystem function-library test should resolve the world subsystem"), ExpectedViewportStatsSubsystem))
	{
		return false;
	}

	UClass* EnhancedInputLocalPlayerSubsystemClass = LoadClass<ULocalPlayerSubsystem>(nullptr, EnhancedInputLocalPlayerSubsystemClassPath);
	if (!TestNotNull(TEXT("Subsystem function-library test should load the EnhancedInput local-player subsystem class"), EnhancedInputLocalPlayerSubsystemClass))
	{
		return false;
	}
	FSubsystemCollectionBase::ActivateExternalSubsystem(EnhancedInputLocalPlayerSubsystemClass);

	ULocalPlayerSubsystem* ExpectedLocalPlayerSubsystem = Fixture.LocalPlayer->GetSubsystemBase(EnhancedInputLocalPlayerSubsystemClass);
	if (!TestNotNull(TEXT("Subsystem function-library test should resolve the local-player subsystem"), ExpectedLocalPlayerSubsystem))
	{
		return false;
	}

	bPassed &= TestTrue(
		TEXT("Subsystem::GetEngineSubsystem should return the native engine subsystem instance"),
		USubsystemLibrary::GetEngineSubsystem(UInputDeviceSubsystem::StaticClass()) == ExpectedEngineSubsystem);

	bPassed &= TestTrue(
		TEXT("Subsystem::GetGameInstanceSubsystem should resolve from a bound player-controller world context"),
		USubsystemLibrary::GetGameInstanceSubsystem(Fixture.BoundPlayerController, UReplaySubsystem::StaticClass()) == ExpectedReplaySubsystem);

	bPassed &= TestTrue(
		TEXT("Subsystem::GetLocalPlayerSubsystem should resolve from a bound player-controller world context"),
		USubsystemLibrary::GetLocalPlayerSubsystem(Fixture.BoundPlayerController, EnhancedInputLocalPlayerSubsystemClass) == ExpectedLocalPlayerSubsystem);

	bPassed &= TestTrue(
		TEXT("Subsystem::GetWorldSubsystem should resolve from a bound player-controller world context"),
		USubsystemLibrary::GetWorldSubsystem(Fixture.BoundPlayerController, UViewportStatsSubsystem::StaticClass()) == ExpectedViewportStatsSubsystem);

	bPassed &= TestTrue(
		TEXT("Subsystem::GetLocalPlayerSubsystemFromPlayerController should return the subsystem bound to the controller local player"),
		USubsystemLibrary::GetLocalPlayerSubsystemFromPlayerController(Fixture.BoundPlayerController, EnhancedInputLocalPlayerSubsystemClass) == ExpectedLocalPlayerSubsystem);

	bPassed &= TestTrue(
		TEXT("Subsystem::GetLocalPlayerSubsystemFromLocalPlayer should return the subsystem bound to the provided local player"),
		USubsystemLibrary::GetLocalPlayerSubsystemFromLocalPlayer(Fixture.LocalPlayer, EnhancedInputLocalPlayerSubsystemClass) == ExpectedLocalPlayerSubsystem);

	bPassed &= TestNull(
		TEXT("Subsystem::GetLocalPlayerSubsystem should safe-fail when the controller world context has no local player"),
		USubsystemLibrary::GetLocalPlayerSubsystem(Fixture.UnboundPlayerController, EnhancedInputLocalPlayerSubsystemClass));

	bPassed &= TestNull(
		TEXT("Subsystem::GetLocalPlayerSubsystemFromPlayerController should safe-fail when the controller has no local player"),
		USubsystemLibrary::GetLocalPlayerSubsystemFromPlayerController(Fixture.UnboundPlayerController, EnhancedInputLocalPlayerSubsystemClass));

	bPassed &= TestNull(
		TEXT("Subsystem::GetGameInstanceSubsystem should safe-fail when the world context is null"),
		USubsystemLibrary::GetGameInstanceSubsystem(nullptr, UReplaySubsystem::StaticClass()));

	bPassed &= TestNull(
		TEXT("Subsystem::GetLocalPlayerSubsystem should safe-fail when the world context is null"),
		USubsystemLibrary::GetLocalPlayerSubsystem(nullptr, EnhancedInputLocalPlayerSubsystemClass));

	bPassed &= TestNull(
		TEXT("Subsystem::GetWorldSubsystem should safe-fail when the world context is null"),
		USubsystemLibrary::GetWorldSubsystem(nullptr, UViewportStatsSubsystem::StaticClass()));

	bPassed &= TestNull(
		TEXT("Subsystem::GetLocalPlayerSubsystemFromPlayerController should safe-fail when the controller is null"),
		USubsystemLibrary::GetLocalPlayerSubsystemFromPlayerController(nullptr, EnhancedInputLocalPlayerSubsystemClass));

	bPassed &= TestNull(
		TEXT("Subsystem::GetLocalPlayerSubsystemFromLocalPlayer should safe-fail when the local player is null"),
		USubsystemLibrary::GetLocalPlayerSubsystemFromLocalPlayer(nullptr, EnhancedInputLocalPlayerSubsystemClass));

	ASTEST_END_FULL
	return bPassed;
}

#endif
