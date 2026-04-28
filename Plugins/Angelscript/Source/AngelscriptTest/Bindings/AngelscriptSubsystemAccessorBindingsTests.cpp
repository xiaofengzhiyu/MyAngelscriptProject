#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptSubsystemAccessorBindingsTests_Private
{
	static constexpr ANSICHAR EngineSubsystemModuleName[] = "ASSubsystemEngineAccessorCompat";
	static constexpr ANSICHAR LocalPlayerSubsystemModuleName[] = "ASSubsystemLocalPlayerAccessorCompat";
	static constexpr int32 LocalPlayerControllerId = 29;
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

	struct FLocalPlayerSubsystemAccessorFixture
	{
		~FLocalPlayerSubsystemAccessorFixture()
		{
			Shutdown();
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Subsystem accessor fixture should have a live GEngine"), GEngine))
			{
				return false;
			}

			const FName PackageName = MakeUniqueObjectName(
				nullptr,
				UPackage::StaticClass(),
				FName(TEXT("/Angelscript_Test_SubsystemAccessorBindings")));
			Package = NewObject<UPackage>(GetTransientPackage(), PackageName, RF_Transient);
			if (!Test.TestNotNull(TEXT("Subsystem accessor fixture should create a transient world package"), Package))
			{
				return false;
			}

			GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			if (!Test.TestNotNull(TEXT("Subsystem accessor fixture should create a standalone game instance"), GameInstance))
			{
				return false;
			}

			GameInstance->InitializeStandalone(TEXT("AngelscriptSubsystemAccessorBindingsWorld"), Package);
			World = GameInstance->GetWorld();
			WorldContext = GameInstance->GetWorldContext();
			if (!Test.TestNotNull(TEXT("Subsystem accessor fixture should initialize a standalone world"), World)
				|| !Test.TestNotNull(TEXT("Subsystem accessor fixture should expose a world context"), WorldContext))
			{
				return false;
			}

			UClass* ViewportClass = GEngine->GameViewportClientClass != nullptr
				? GEngine->GameViewportClientClass.Get()
				: UGameViewportClient::StaticClass();
			GameViewport = NewObject<UGameViewportClient>(GEngine, ViewportClass);
			if (!Test.TestNotNull(TEXT("Subsystem accessor fixture should create a viewport client"), GameViewport))
			{
				return false;
			}

			GameViewport->Init(*WorldContext, GameInstance, false);
			WorldContext->GameViewport = GameViewport;

			FString CreateError;
			LocalPlayer = GameInstance->CreateLocalPlayer(LocalPlayerControllerId, CreateError, false);
			if (!Test.TestNotNull(TEXT("Subsystem accessor fixture should create a local player"), LocalPlayer))
			{
				return false;
			}

			if (!Test.TestTrue(TEXT("Subsystem accessor fixture should create the local player without errors"), CreateError.IsEmpty()))
			{
				return false;
			}

			PlayerController = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("Subsystem accessor fixture should spawn a transient player controller"), PlayerController))
			{
				return false;
			}
			PlayerController->SetPlayer(LocalPlayer);

			return Test.TestTrue(
				TEXT("Subsystem accessor fixture should assign the local player to the controller"),
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

using namespace AngelscriptTest_Bindings_AngelscriptSubsystemAccessorBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptEngineSubsystemAccessorCompatBindingsTest,
	"Angelscript.TestModule.Bindings.SubsystemAccessor.EngineSubsystemGetCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptLocalPlayerSubsystemAccessorCompatBindingsTest,
	"Angelscript.TestModule.Bindings.SubsystemAccessor.LocalPlayerControllerGetCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptEngineSubsystemAccessorCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	UInputDeviceSubsystem* ExpectedInputDeviceSubsystem = ResolveInputDeviceSubsystem();
	if (!TestNotNull(TEXT("Engine subsystem accessor test should resolve the input-device subsystem"), ExpectedInputDeviceSubsystem))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		EngineSubsystemModuleName,
		TEXT(R"AS(
int VerifyEngineSubsystemAccessor(UInputDeviceSubsystem ExpectedInputDeviceSubsystem)
{
	int MismatchMask = 0;

	UInputDeviceSubsystem DirectInputDeviceSubsystem = UInputDeviceSubsystem::Get();
	if (DirectInputDeviceSubsystem == null)
		MismatchMask |= 1;
	else if (DirectInputDeviceSubsystem != ExpectedInputDeviceSubsystem)
		MismatchMask |= 2;

	UEngineSubsystem LibraryInputDeviceSubsystem =
		Subsystem::GetEngineSubsystem(UInputDeviceSubsystem::StaticClass());
	if (LibraryInputDeviceSubsystem == null)
		MismatchMask |= 4;
	else if (LibraryInputDeviceSubsystem != DirectInputDeviceSubsystem)
		MismatchMask |= 8;

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
			TEXT("int VerifyEngineSubsystemAccessor(UInputDeviceSubsystem)"),
			[this, ExpectedInputDeviceSubsystem](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, ExpectedInputDeviceSubsystem, TEXT("VerifyEngineSubsystemAccessor"));
			},
			TEXT("VerifyEngineSubsystemAccessor"),
			ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Engine subsystem direct Type::Get and Subsystem::GetEngineSubsystem should return the native input-device subsystem"),
		ResultMask,
		0);

	ASTEST_END_FULL
	return bPassed;
}

bool FAngelscriptLocalPlayerSubsystemAccessorCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FLocalPlayerSubsystemAccessorFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UClass* EnhancedInputLocalPlayerSubsystemClass = LoadClass<ULocalPlayerSubsystem>(nullptr, EnhancedInputLocalPlayerSubsystemClassPath);
	if (!TestNotNull(TEXT("Local-player subsystem accessor test should load the EnhancedInput local-player subsystem class"), EnhancedInputLocalPlayerSubsystemClass))
	{
		return false;
	}
	FSubsystemCollectionBase::ActivateExternalSubsystem(EnhancedInputLocalPlayerSubsystemClass);

	ULocalPlayerSubsystem* ExpectedSubsystem = Fixture.LocalPlayer->GetSubsystemBase(EnhancedInputLocalPlayerSubsystemClass);
	if (!TestNotNull(TEXT("Local-player subsystem accessor test should resolve the EnhancedInput local-player subsystem"), ExpectedSubsystem))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		LocalPlayerSubsystemModuleName,
		TEXT(R"AS(
int VerifyLocalPlayerSubsystemAccessor(
	ULocalPlayer ExpectedLocalPlayer,
	APlayerController ExpectedPlayerController,
	UEnhancedInputLocalPlayerSubsystem ExpectedSubsystem)
{
	int MismatchMask = 0;

	if (UEnhancedInputLocalPlayerSubsystem::Get(ExpectedLocalPlayer) != ExpectedSubsystem)
		MismatchMask |= 1;
	if (UEnhancedInputLocalPlayerSubsystem::Get(ExpectedPlayerController) != ExpectedSubsystem)
		MismatchMask |= 2;

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
			TEXT("int VerifyLocalPlayerSubsystemAccessor(ULocalPlayer, APlayerController, UEnhancedInputLocalPlayerSubsystem)"),
			[this, &Fixture, ExpectedSubsystem](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, Fixture.LocalPlayer, TEXT("VerifyLocalPlayerSubsystemAccessor"))
					&& SetArgObjectChecked(*this, Context, 1, Fixture.PlayerController, TEXT("VerifyLocalPlayerSubsystemAccessor"))
					&& SetArgObjectChecked(*this, Context, 2, ExpectedSubsystem, TEXT("VerifyLocalPlayerSubsystemAccessor"));
			},
			TEXT("VerifyLocalPlayerSubsystemAccessor"),
			ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Local-player subsystem Type::Get overloads should resolve through both ULocalPlayer and APlayerController"),
		ResultMask,
		0);

	ASTEST_END_FULL
	return bPassed;
}

#endif
