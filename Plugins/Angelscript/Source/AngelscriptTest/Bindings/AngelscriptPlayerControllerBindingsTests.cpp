#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptPlayerControllerBindingsTests_Private
{
	static constexpr ANSICHAR PlayerControllerModuleName[] = "ASPlayerControllerLocalPlayerCompat";
	static constexpr int32 LocalPlayerControllerId = 17;

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

	struct FPlayerControllerBindingFixture
	{
		~FPlayerControllerBindingFixture()
		{
			Shutdown();
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("PlayerController binding test should have a live GEngine"), GEngine))
			{
				return false;
			}

			const FName PackageName = MakeUniqueObjectName(
				nullptr,
				UPackage::StaticClass(),
				FName(TEXT("/Angelscript_Test_PlayerControllerBindings")));
			Package = NewObject<UPackage>(GetTransientPackage(), PackageName, RF_Transient);
			if (!Test.TestNotNull(TEXT("PlayerController binding test should create a transient world package"), Package))
			{
				return false;
			}

			GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			if (!Test.TestNotNull(TEXT("PlayerController binding test should create a standalone game instance"), GameInstance))
			{
				return false;
			}

			GameInstance->InitializeStandalone(TEXT("AngelscriptPlayerControllerBindingsWorld"), Package);
			World = GameInstance->GetWorld();
			WorldContext = GameInstance->GetWorldContext();
			if (!Test.TestNotNull(TEXT("PlayerController binding test should initialize a standalone world"), World)
				|| !Test.TestNotNull(TEXT("PlayerController binding test should expose a world context"), WorldContext))
			{
				return false;
			}

			UClass* ViewportClass = GEngine->GameViewportClientClass != nullptr
				? GEngine->GameViewportClientClass.Get()
				: UGameViewportClient::StaticClass();
			GameViewport = NewObject<UGameViewportClient>(GEngine, ViewportClass);
			if (!Test.TestNotNull(TEXT("PlayerController binding test should create a viewport client"), GameViewport))
			{
				return false;
			}

			GameViewport->Init(*WorldContext, GameInstance, false);
			WorldContext->GameViewport = GameViewport;

			FString CreateError;
			LocalPlayer = GameInstance->CreateLocalPlayer(LocalPlayerControllerId, CreateError, false);
			if (!Test.TestNotNull(TEXT("PlayerController binding test should create a local player"), LocalPlayer))
			{
				return false;
			}

			if (!Test.TestTrue(TEXT("PlayerController binding test should create the local player without errors"), CreateError.IsEmpty()))
			{
				return false;
			}

			PlayerController = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("PlayerController binding test should spawn a transient player controller"), PlayerController))
			{
				return false;
			}

			return true;
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

using namespace AngelscriptTest_Bindings_AngelscriptPlayerControllerBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPlayerControllerLocalPlayerCompatBindingsTest,
	"Angelscript.TestModule.Bindings.PlayerControllerLocalPlayerCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPlayerControllerLocalPlayerCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FPlayerControllerBindingFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	if (!TestTrue(
			TEXT("PlayerController binding test should start from an unassigned local-player slot"),
			Fixture.PlayerController->GetLocalPlayer() == nullptr))
	{
		return false;
	}

const FString Script = FString::Printf(TEXT(R"AS(
int VerifyPlayerControllerLocalPlayerCompat(APlayerController Controller, ULocalPlayer ExpectedPlayer)
{
	if (Controller == null || ExpectedPlayer == null)
		return 1;

	if (Controller.GetLocalPlayer() != null)
		return 2;

	Controller.SetPlayer(ExpectedPlayer);

	ULocalPlayer Current = Controller.GetLocalPlayer();
	if (Current == null)
		return 4;
	if (Current != ExpectedPlayer)
		return 8;
	if (Current.GetControllerId() != %d)
		return 16;
	if (Current.GetGameInstance() == null)
		return 32;

	return 0;
}
)AS"), LocalPlayerControllerId);

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		PlayerControllerModuleName,
		Script);
	if (Module == nullptr)
	{
		return false;
	}

	int32 ResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyPlayerControllerLocalPlayerCompat(APlayerController, ULocalPlayer)"),
			[this, &Fixture](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, Fixture.PlayerController, TEXT("VerifyPlayerControllerLocalPlayerCompat"))
					&& SetArgObjectChecked(*this, Context, 1, Fixture.LocalPlayer, TEXT("VerifyPlayerControllerLocalPlayerCompat"));
			},
			TEXT("VerifyPlayerControllerLocalPlayerCompat"),
			ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("PlayerController.SetPlayer and GetLocalPlayer should preserve the expected local-player runtime parity"),
		ResultMask,
		0);
	bPassed &= TestTrue(
		TEXT("PlayerController.SetPlayer should update the native local-player pointer immediately"),
		Fixture.PlayerController->GetLocalPlayer() == Fixture.LocalPlayer);
	bPassed &= TestEqual(
		TEXT("PlayerController.GetLocalPlayer should preserve the native controller id after script assignment"),
		Fixture.PlayerController->GetLocalPlayer()->GetControllerId(),
		LocalPlayerControllerId);

	ASTEST_END_FULL
	return bPassed;
}

#endif
