#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ViewportStatsSubsystem.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ReplaySubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Templates/Function.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptFunctionLibraryBindingsTests_Private
{
	static constexpr ANSICHAR SubsystemWorldContextModuleName[] = "ASFunctionLibrarySubsystemWorldContextCompat";
	static constexpr ANSICHAR SubsystemLocalPlayerModuleName[] = "ASFunctionLibrarySubsystemLocalPlayerCompileCompat";
	static constexpr int32 LocalPlayerControllerId = 23;
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

		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
				Context->Prepare(Function),
				static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		if (!BindArguments(*Context))
		{
			return false;
		}

		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
				Context->Execute(),
				static_cast<int32>(asEXECUTION_FINISHED)))
		{
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}

	struct FStandaloneGameInstanceFixture
	{
		~FStandaloneGameInstanceFixture()
		{
			Shutdown();
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Function-library subsystem fixture should have a live GEngine"), GEngine))
			{
				return false;
			}

			const FName PackageName = MakeUniqueObjectName(
				nullptr,
				UPackage::StaticClass(),
				FName(TEXT("/Angelscript_Test_FunctionLibrarySubsystems")));
			Package = NewObject<UPackage>(GetTransientPackage(), PackageName, RF_Transient);
			if (!Test.TestNotNull(TEXT("Function-library subsystem fixture should create a transient world package"), Package))
			{
				return false;
			}

			GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			if (!Test.TestNotNull(TEXT("Function-library subsystem fixture should create a standalone game instance"), GameInstance))
			{
				return false;
			}

			GameInstance->InitializeStandalone(TEXT("AngelscriptFunctionLibrarySubsystemWorld"), Package);
			World = GameInstance->GetWorld();
			WorldContext = GameInstance->GetWorldContext();
			if (!Test.TestNotNull(TEXT("Function-library subsystem fixture should initialize a standalone world"), World)
				|| !Test.TestNotNull(TEXT("Function-library subsystem fixture should expose a world context"), WorldContext))
			{
				return false;
			}

			UClass* ViewportClass = GEngine->GameViewportClientClass != nullptr
				? GEngine->GameViewportClientClass.Get()
				: UGameViewportClient::StaticClass();
			GameViewport = NewObject<UGameViewportClient>(GEngine, ViewportClass);
			if (!Test.TestNotNull(TEXT("Function-library subsystem fixture should create a viewport client"), GameViewport))
			{
				return false;
			}

			GameViewport->Init(*WorldContext, GameInstance, false);
			WorldContext->GameViewport = GameViewport;

			FString Error;
			LocalPlayer = GameInstance->CreateLocalPlayer(LocalPlayerControllerId, Error, false);
			if (!Test.TestNotNull(TEXT("Function-library subsystem fixture should create a local player"), LocalPlayer))
			{
				return false;
			}

			return Test.TestTrue(TEXT("Function-library subsystem fixture should create the local player without errors"), Error.IsEmpty());
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

using namespace AngelscriptTest_Bindings_AngelscriptFunctionLibraryBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFunctionLibrarySubsystemWorldContextCompileCompatTest,
	"Angelscript.TestModule.Bindings.FunctionLibrary.SubsystemWorldContextCompileCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFunctionLibrarySubsystemLocalPlayerCompileCompatTest,
	"Angelscript.TestModule.Bindings.FunctionLibrary.SubsystemLocalPlayerCompileCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptFunctionLibrarySubsystemWorldContextCompileCompatTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		SubsystemWorldContextModuleName,
		TEXT(R"AS(
UGameInstanceSubsystem ProbeGameInstanceSubsystem()
{
	return Subsystem::GetGameInstanceSubsystem(UReplaySubsystem::StaticClass());
}

UWorldSubsystem ProbeWorldSubsystem()
{
	return Subsystem::GetWorldSubsystem(UViewportStatsSubsystem::StaticClass());
}

int Entry()
{
	return 1;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int Entry()"),
			[](asIScriptContext& Context)
			{
				(void)Context;
				return true;
			},
			TEXT("Entry"),
			Result))
	{
		return false;
	}

	TestEqual(
		TEXT("Subsystem:: hidden world-context helper signatures should remain script-compilable"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return true;
}

bool FAngelscriptFunctionLibrarySubsystemLocalPlayerCompileCompatTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		SubsystemLocalPlayerModuleName,
		TEXT(R"AS(
ULocalPlayerSubsystem ProbeImplicitLocalPlayerLookup()
{
	return Subsystem::GetLocalPlayerSubsystem(UEnhancedInputLocalPlayerSubsystem::StaticClass());
}

ULocalPlayerSubsystem ProbeExplicitLocalPlayerLookup(ULocalPlayer ExpectedLocalPlayer)
{
	return Subsystem::GetLocalPlayerSubsystemFromLocalPlayer(ExpectedLocalPlayer, UEnhancedInputLocalPlayerSubsystem::StaticClass());
}

ULocalPlayerSubsystem ProbePlayerControllerLookup(APlayerController ExpectedPlayerController)
{
	return Subsystem::GetLocalPlayerSubsystemFromPlayerController(ExpectedPlayerController, UEnhancedInputLocalPlayerSubsystem::StaticClass());
}

int Entry()
{
	return 1;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int Entry()"),
			[](asIScriptContext&)
			{
				return true;
			},
			TEXT("Entry"),
			Result))
	{
		return false;
	}

	TestEqual(
		TEXT("Subsystem:: local-player helper signatures should remain script-compilable"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
