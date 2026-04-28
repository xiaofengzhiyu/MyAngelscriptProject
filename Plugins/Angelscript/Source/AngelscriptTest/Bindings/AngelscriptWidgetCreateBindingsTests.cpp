#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptWidgetCreateBindingsTests_Private
{
	static constexpr ANSICHAR WidgetCreateModuleName[] = "ASWidgetCreateCompat";
	static constexpr TCHAR WidgetFixtureModuleName[] = TEXT("ASWidgetCreateCompatFixture");
	static constexpr TCHAR WidgetFixtureClassName[] = TEXT("UWidgetCreateCompatFixture");
	static constexpr int32 WidgetCreateControllerId = 17;

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	bool ExecuteObjectFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		UObject*& OutResult)
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

		OutResult = static_cast<UObject*>(Context->GetReturnObject());
		return true;
	}

	UClass* CreateFixtureWidgetClass(FAutomationTestBase& Test, FAngelscriptEngine& Engine)
	{
		const bool bCompiled = CompileAnnotatedModuleFromMemory(
			&Engine,
			FName(WidgetFixtureModuleName),
			FString(WidgetFixtureModuleName) + TEXT(".as"),
			FString::Printf(
				TEXT(R"AS(
UCLASS()
class %s : UUserWidget
{
}
)AS"),
				WidgetFixtureClassName));
		if (!Test.TestTrue(TEXT("WidgetCreateCompat should compile a concrete scripted widget fixture class"), bCompiled))
		{
			return nullptr;
		}

		UClass* WidgetClass = FindGeneratedClass(&Engine, FName(WidgetFixtureClassName));
		Test.TestNotNull(TEXT("WidgetCreateCompat should publish the generated widget fixture class"), WidgetClass);
		return WidgetClass;
	}

	// WidgetBlueprint::CreateWidget needs both a live ambient world-context object
	// and an owning player surface, so keep the fixture minimal and fully standalone.
	struct FWidgetCreateFixture
	{
		~FWidgetCreateFixture()
		{
			Shutdown();
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("WidgetCreateCompat should have a live GEngine"), GEngine))
			{
				return false;
			}

			const FName PackageName = MakeUniqueObjectName(
				nullptr,
				UPackage::StaticClass(),
				FName(TEXT("/Angelscript_Test_WidgetCreateCompat")));
			Package = NewObject<UPackage>(GetTransientPackage(), PackageName, RF_Transient);
			if (!Test.TestNotNull(TEXT("WidgetCreateCompat should create a transient world package"), Package))
			{
				return false;
			}

			GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			if (!Test.TestNotNull(TEXT("WidgetCreateCompat should create a standalone game instance"), GameInstance))
			{
				return false;
			}

			GameInstance->InitializeStandalone(TEXT("AngelscriptWidgetCreateCompatWorld"), Package);
			World = GameInstance->GetWorld();
			WorldContext = GameInstance->GetWorldContext();
			if (!Test.TestNotNull(TEXT("WidgetCreateCompat should initialize a standalone world"), World)
				|| !Test.TestNotNull(TEXT("WidgetCreateCompat should expose a world context"), WorldContext))
			{
				return false;
			}

			UClass* ViewportClass = GEngine->GameViewportClientClass != nullptr
				? GEngine->GameViewportClientClass.Get()
				: UGameViewportClient::StaticClass();
			GameViewport = NewObject<UGameViewportClient>(GEngine, ViewportClass);
			if (!Test.TestNotNull(TEXT("WidgetCreateCompat should create a viewport client"), GameViewport))
			{
				return false;
			}

			GameViewport->Init(*WorldContext, GameInstance, false);
			WorldContext->GameViewport = GameViewport;

			FString Error;
			LocalPlayer = GameInstance->CreateLocalPlayer(WidgetCreateControllerId, Error, false);
			if (!Test.TestNotNull(TEXT("WidgetCreateCompat should create a local player"), LocalPlayer))
			{
				return false;
			}

			if (!Test.TestTrue(TEXT("WidgetCreateCompat should create the local player without errors"), Error.IsEmpty()))
			{
				return false;
			}

			PlayerController = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("WidgetCreateCompat should spawn a transient player controller"), PlayerController))
			{
				return false;
			}

			PlayerController->SetPlayer(LocalPlayer);
			return Test.TestTrue(
				TEXT("WidgetCreateCompat should let the spawned player controller expose the created local player"),
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

using namespace AngelscriptTest_Bindings_AngelscriptWidgetCreateBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptWidgetCreateCompatBindingsTest,
	"Angelscript.TestModule.Bindings.WidgetCreateCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptWidgetCreateCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASWidgetCreateCompat"));
		Engine.DiscardModule(TEXT("ASWidgetCreateCompatFixture"));
	};

	FWidgetCreateFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UClass* WidgetClass = CreateFixtureWidgetClass(*this, Engine);
	if (WidgetClass == nullptr)
	{
		return false;
	}

	UUserWidget* NativeWidget = UWidgetBlueprintLibrary::Create(Fixture.World, WidgetClass, Fixture.PlayerController);
	ON_SCOPE_EXIT
	{
		if (NativeWidget != nullptr)
		{
			NativeWidget->MarkAsGarbage();
		}
	};

	if (!TestNotNull(TEXT("WidgetCreateCompat should produce a native widget baseline from UWidgetBlueprintLibrary::Create"), NativeWidget))
	{
		return false;
	}

	const FString Script = FString::Printf(
		TEXT(R"AS(
UUserWidget CreateWidgetViaBindings(UClass WidgetClass, APlayerController OwningPlayer)
{
	TSubclassOf<UUserWidget> TypedWidgetClass = WidgetClass;
	return WidgetBlueprint::CreateWidget(TypedWidgetClass, OwningPlayer);
}
)AS"));

	asIScriptModule* Module = BuildModule(*this, Engine, WidgetCreateModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	FScopedTestWorldContextScope WorldContextScope(Fixture.World);

	UObject* ReturnedObject = nullptr;
	if (!ExecuteObjectFunction(
			*this,
			Engine,
			*Module,
			TEXT("UUserWidget CreateWidgetViaBindings(UClass WidgetClass, APlayerController OwningPlayer)"),
			[this, WidgetClass, &Fixture](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, WidgetClass, TEXT("CreateWidgetViaBindings"))
					&& SetArgObjectChecked(*this, Context, 1, Fixture.PlayerController, TEXT("CreateWidgetViaBindings"));
			},
			TEXT("CreateWidgetViaBindings"),
			ReturnedObject))
	{
		return false;
	}

	UUserWidget* CreatedWidget = Cast<UUserWidget>(ReturnedObject);
	ON_SCOPE_EXIT
	{
		if (CreatedWidget != nullptr)
		{
			CreatedWidget->MarkAsGarbage();
		}
	};

	bPassed &= TestNotNull(
		TEXT("Widget Create binding should return a concrete user widget instance"),
		CreatedWidget);
	if (CreatedWidget == nullptr)
	{
		return false;
	}

	bPassed &= TestTrue(
		TEXT("Widget Create binding should instantiate the requested scripted widget class"),
		CreatedWidget->GetClass() == WidgetClass);
	bPassed &= TestTrue(
		TEXT("Widget Create binding should resolve the ambient world-context object to the standalone test world"),
		CreatedWidget->GetWorld() == Fixture.World);
	bPassed &= TestTrue(
		TEXT("Widget Create binding should preserve native class parity for the created widget"),
		CreatedWidget->GetClass() == NativeWidget->GetClass());
	bPassed &= TestTrue(
		TEXT("Widget Create binding should preserve native world parity for the created widget"),
		CreatedWidget->GetWorld() == NativeWidget->GetWorld());
	bPassed &= TestTrue(
		TEXT("Widget Create binding should preserve native owning-player parity for the created widget"),
		CreatedWidget->GetOwningPlayer() == NativeWidget->GetOwningPlayer());

	ASTEST_END_FULL
	return bPassed;
}

#endif
