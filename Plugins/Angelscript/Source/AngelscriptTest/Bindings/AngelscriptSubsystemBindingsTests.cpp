#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ViewportStatsSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ReplaySubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptSubsystemBindingsTests_Private
{
	static constexpr ANSICHAR SubsystemGetAccessorsModuleName[] = "ASSubsystemGetAccessors";
	static constexpr int32 LocalPlayerControllerId = 31;
	static constexpr TCHAR EnhancedInputLocalPlayerSubsystemClassPath[] = TEXT("/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");

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

	struct FSubsystemAccessorFixture
	{
		~FSubsystemAccessorFixture()
		{
			Shutdown();
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Subsystem accessor bindings test should have a live GEngine"), GEngine))
			{
				return false;
			}

			const FName PackageName = MakeUniqueObjectName(
				nullptr,
				UPackage::StaticClass(),
				FName(TEXT("/Angelscript_Test_SubsystemBindings")));
			Package = NewObject<UPackage>(GetTransientPackage(), PackageName, RF_Transient);
			if (!Test.TestNotNull(TEXT("Subsystem accessor bindings test should create a transient world package"), Package))
			{
				return false;
			}

			GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			if (!Test.TestNotNull(TEXT("Subsystem accessor bindings test should create a standalone game instance"), GameInstance))
			{
				return false;
			}

			GameInstance->InitializeStandalone(TEXT("AngelscriptSubsystemBindingsWorld"), Package);
			World = GameInstance->GetWorld();
			WorldContext = GameInstance->GetWorldContext();
			if (!Test.TestNotNull(TEXT("Subsystem accessor bindings test should initialize a standalone world"), World)
				|| !Test.TestNotNull(TEXT("Subsystem accessor bindings test should expose a world context"), WorldContext))
			{
				return false;
			}

			UClass* ViewportClass = GEngine->GameViewportClientClass != nullptr
				? GEngine->GameViewportClientClass.Get()
				: UGameViewportClient::StaticClass();
			GameViewport = NewObject<UGameViewportClient>(GEngine, ViewportClass);
			if (!Test.TestNotNull(TEXT("Subsystem accessor bindings test should create a viewport client"), GameViewport))
			{
				return false;
			}

			GameViewport->Init(*WorldContext, GameInstance, false);
			WorldContext->GameViewport = GameViewport;

			FString CreateError;
			LocalPlayer = GameInstance->CreateLocalPlayer(LocalPlayerControllerId, CreateError, false);
			if (!Test.TestNotNull(TEXT("Subsystem accessor bindings test should create a local player"), LocalPlayer))
			{
				return false;
			}

			if (!Test.TestTrue(TEXT("Subsystem accessor bindings test should create the local player without errors"), CreateError.IsEmpty()))
			{
				return false;
			}

			PlayerController = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("Subsystem accessor bindings test should spawn a transient player controller"), PlayerController))
			{
				return false;
			}

			PlayerController->SetPlayer(LocalPlayer);
			return Test.TestTrue(
				TEXT("Subsystem accessor bindings test should assign the local player to the controller"),
				PlayerController->GetLocalPlayer() == LocalPlayer);
		}

		void Shutdown()
		{
			if (GameInstance == nullptr && World == nullptr)
			{
				return;
			}

			if (PlayerController != nullptr)
			{
				PlayerController->Destroy();
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

			PlayerController = nullptr;
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
		APlayerController* PlayerController = nullptr;
	};
}

using namespace AngelscriptTest_Bindings_AngelscriptSubsystemBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSubsystemGetAccessorsBindingsTest,
	"Angelscript.TestModule.Bindings.SubsystemGetAccessors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptSubsystemGetAccessorsBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FSubsystemAccessorFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UInputDeviceSubsystem* ExpectedEngineSubsystem = ResolveInputDeviceSubsystem();
	UReplaySubsystem* ExpectedReplaySubsystem = Fixture.GameInstance->GetSubsystem<UReplaySubsystem>();
	UViewportStatsSubsystem* ExpectedViewportStatsSubsystem = Fixture.World->GetSubsystem<UViewportStatsSubsystem>();
	if (!TestNotNull(TEXT("Subsystem accessor bindings test should resolve the engine subsystem"), ExpectedEngineSubsystem)
		|| !TestNotNull(TEXT("Subsystem accessor bindings test should resolve the game-instance subsystem"), ExpectedReplaySubsystem)
		|| !TestNotNull(TEXT("Subsystem accessor bindings test should resolve the world subsystem"), ExpectedViewportStatsSubsystem))
	{
		return false;
	}

	UClass* EnhancedInputLocalPlayerSubsystemClass = LoadClass<ULocalPlayerSubsystem>(nullptr, EnhancedInputLocalPlayerSubsystemClassPath);
	if (!TestNotNull(TEXT("Subsystem accessor bindings test should load the EnhancedInput local-player subsystem class"), EnhancedInputLocalPlayerSubsystemClass))
	{
		return false;
	}
	FSubsystemCollectionBase::ActivateExternalSubsystem(EnhancedInputLocalPlayerSubsystemClass);

	ULocalPlayerSubsystem* ExpectedLocalPlayerSubsystem = Fixture.LocalPlayer->GetSubsystemBase(EnhancedInputLocalPlayerSubsystemClass);
	if (!TestNotNull(TEXT("Subsystem accessor bindings test should resolve the local-player subsystem"), ExpectedLocalPlayerSubsystem))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		SubsystemGetAccessorsModuleName,
		TEXT(R"AS(
int VerifySubsystemGetAccessors(
	UInputDeviceSubsystem ExpectedEngineSubsystem,
	UReplaySubsystem ExpectedReplaySubsystem,
	UViewportStatsSubsystem ExpectedViewportStatsSubsystem,
	ULocalPlayer ExpectedLocalPlayer,
	APlayerController ExpectedPlayerController,
	UEnhancedInputLocalPlayerSubsystem ExpectedLocalPlayerSubsystem,
	ULocalPlayer NullLocalPlayer,
	APlayerController NullPlayerController)
{
	int MismatchMask = 0;

	if (UInputDeviceSubsystem::Get() != ExpectedEngineSubsystem)
		MismatchMask |= 1;
	if (UReplaySubsystem::Get() != ExpectedReplaySubsystem)
		MismatchMask |= 2;
	if (UViewportStatsSubsystem::Get() != ExpectedViewportStatsSubsystem)
		MismatchMask |= 4;
	if (UEnhancedInputLocalPlayerSubsystem::Get(ExpectedLocalPlayer) != ExpectedLocalPlayerSubsystem)
		MismatchMask |= 8;
	if (UEnhancedInputLocalPlayerSubsystem::Get(ExpectedPlayerController) != ExpectedLocalPlayerSubsystem)
		MismatchMask |= 16;
	if (UEnhancedInputLocalPlayerSubsystem::Get(NullLocalPlayer) != null)
		MismatchMask |= 32;
	if (UEnhancedInputLocalPlayerSubsystem::Get(NullPlayerController) != null)
		MismatchMask |= 64;

	return MismatchMask;
}

int VerifySubsystemGetAccessorsWithoutWorldContext(UInputDeviceSubsystem ExpectedEngineSubsystem)
{
	int MismatchMask = 0;

	if (UInputDeviceSubsystem::Get() != ExpectedEngineSubsystem)
		MismatchMask |= 1;
	if (UReplaySubsystem::Get() != null)
		MismatchMask |= 2;
	if (UViewportStatsSubsystem::Get() != null)
		MismatchMask |= 4;

	return MismatchMask;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	if (!TestNull(TEXT("Subsystem accessor bindings test should start without an ambient world context"), Engine.GetCurrentWorldContextObject()))
	{
		return false;
	}

	{
		FScopedTestWorldContextScope WorldContextScope(Fixture.World);
		if (!TestTrue(TEXT("Subsystem accessor bindings test should install the scoped world context for positive-path verification"), Engine.GetCurrentWorldContextObject() == Fixture.World))
		{
			return false;
		}

		int32 PositiveResultMask = INDEX_NONE;
		if (!ExecuteIntFunction(
				*this,
				Engine,
				*Module,
				TEXT("int VerifySubsystemGetAccessors(UInputDeviceSubsystem, UReplaySubsystem, UViewportStatsSubsystem, ULocalPlayer, APlayerController, UEnhancedInputLocalPlayerSubsystem, ULocalPlayer, APlayerController)"),
				[this, ExpectedEngineSubsystem, ExpectedReplaySubsystem, ExpectedViewportStatsSubsystem, &Fixture, ExpectedLocalPlayerSubsystem](asIScriptContext& Context)
				{
					return SetArgObjectChecked(*this, Context, 0, ExpectedEngineSubsystem, TEXT("VerifySubsystemGetAccessors"))
						&& SetArgObjectChecked(*this, Context, 1, ExpectedReplaySubsystem, TEXT("VerifySubsystemGetAccessors"))
						&& SetArgObjectChecked(*this, Context, 2, ExpectedViewportStatsSubsystem, TEXT("VerifySubsystemGetAccessors"))
						&& SetArgObjectChecked(*this, Context, 3, Fixture.LocalPlayer, TEXT("VerifySubsystemGetAccessors"))
						&& SetArgObjectChecked(*this, Context, 4, Fixture.PlayerController, TEXT("VerifySubsystemGetAccessors"))
						&& SetArgObjectChecked(*this, Context, 5, ExpectedLocalPlayerSubsystem, TEXT("VerifySubsystemGetAccessors"))
						&& SetArgObjectChecked(*this, Context, 6, nullptr, TEXT("VerifySubsystemGetAccessors"))
						&& SetArgObjectChecked(*this, Context, 7, nullptr, TEXT("VerifySubsystemGetAccessors"));
				},
				TEXT("VerifySubsystemGetAccessors"),
				PositiveResultMask))
		{
			return false;
		}

		bPassed &= TestEqual(
			TEXT("Subsystem accessor bindings should resolve engine, game-instance, world, and local-player accessors on the positive path"),
			PositiveResultMask,
			0);
	}

	if (!TestNull(TEXT("Subsystem accessor bindings test should clear the ambient world context after the positive-path scope exits"), Engine.GetCurrentWorldContextObject()))
	{
		return false;
	}

	int32 NullContextResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifySubsystemGetAccessorsWithoutWorldContext(UInputDeviceSubsystem)"),
			[this, ExpectedEngineSubsystem](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, ExpectedEngineSubsystem, TEXT("VerifySubsystemGetAccessorsWithoutWorldContext"));
			},
			TEXT("VerifySubsystemGetAccessorsWithoutWorldContext"),
			NullContextResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Subsystem accessor bindings should return null for game-instance and world subsystem accessors when no world context is installed"),
		NullContextResultMask,
		0);

	ASTEST_END_FULL
	return bPassed;
}

#endif
