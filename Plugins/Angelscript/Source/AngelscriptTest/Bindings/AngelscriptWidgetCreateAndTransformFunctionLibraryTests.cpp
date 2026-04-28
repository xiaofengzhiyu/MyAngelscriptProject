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
#include "Templates/Function.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptWidgetCreateAndTransformFunctionLibraryTests_Private
{
	static constexpr ANSICHAR WidgetCreateAndTransformModuleName[] = "ASWidgetCreateAndTransform";
	static constexpr TCHAR WidgetCreateAndTransformFixtureModuleName[] = TEXT("ASWidgetCreateAndTransformFixture");
	static constexpr TCHAR WidgetCreateAndTransformFixtureClassName[] = TEXT("UWidgetCreateAndTransformFixture");
	static constexpr int32 WidgetCreateAndTransformControllerId = 23;

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

	UClass* CreateFixtureWidgetClass(FAutomationTestBase& Test, FAngelscriptEngine& Engine)
	{
		const bool bCompiled = CompileAnnotatedModuleFromMemory(
			&Engine,
			FName(WidgetCreateAndTransformFixtureModuleName),
			FString(WidgetCreateAndTransformFixtureModuleName) + TEXT(".as"),
			FString::Printf(
				TEXT(R"AS(
UCLASS()
class %s : UUserWidget
{
}
)AS"),
				WidgetCreateAndTransformFixtureClassName));
		if (!Test.TestTrue(TEXT("WidgetCreateAndTransform should compile a concrete scripted widget fixture class"), bCompiled))
		{
			return nullptr;
		}

		UClass* WidgetClass = FindGeneratedClass(&Engine, FName(WidgetCreateAndTransformFixtureClassName));
		Test.TestNotNull(TEXT("WidgetCreateAndTransform should publish the generated widget fixture class"), WidgetClass);
		return WidgetClass;
	}

	struct FWidgetCreateFixture
	{
		~FWidgetCreateFixture()
		{
			Shutdown();
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("WidgetCreateAndTransform should have a live GEngine"), GEngine))
			{
				return false;
			}

			const FName PackageName = MakeUniqueObjectName(
				nullptr,
				UPackage::StaticClass(),
				FName(TEXT("/Angelscript_Test_WidgetCreateAndTransform")));
			Package = NewObject<UPackage>(GetTransientPackage(), PackageName, RF_Transient);
			if (!Test.TestNotNull(TEXT("WidgetCreateAndTransform should create a transient world package"), Package))
			{
				return false;
			}

			GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			if (!Test.TestNotNull(TEXT("WidgetCreateAndTransform should create a standalone game instance"), GameInstance))
			{
				return false;
			}

			GameInstance->InitializeStandalone(TEXT("AngelscriptWidgetCreateAndTransformWorld"), Package);
			World = GameInstance->GetWorld();
			WorldContext = GameInstance->GetWorldContext();
			if (!Test.TestNotNull(TEXT("WidgetCreateAndTransform should initialize a standalone world"), World)
				|| !Test.TestNotNull(TEXT("WidgetCreateAndTransform should expose a world context"), WorldContext))
			{
				return false;
			}

			UClass* ViewportClass = GEngine->GameViewportClientClass != nullptr
				? GEngine->GameViewportClientClass.Get()
				: UGameViewportClient::StaticClass();
			GameViewport = NewObject<UGameViewportClient>(GEngine, ViewportClass);
			if (!Test.TestNotNull(TEXT("WidgetCreateAndTransform should create a viewport client"), GameViewport))
			{
				return false;
			}

			GameViewport->Init(*WorldContext, GameInstance, false);
			WorldContext->GameViewport = GameViewport;

			FString Error;
			LocalPlayer = GameInstance->CreateLocalPlayer(WidgetCreateAndTransformControllerId, Error, false);
			if (!Test.TestNotNull(TEXT("WidgetCreateAndTransform should create a local player"), LocalPlayer))
			{
				return false;
			}

			if (!Test.TestTrue(TEXT("WidgetCreateAndTransform should create the local player without errors"), Error.IsEmpty()))
			{
				return false;
			}

			PlayerController = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("WidgetCreateAndTransform should spawn a transient player controller"), PlayerController))
			{
				return false;
			}

			PlayerController->SetPlayer(LocalPlayer);
			return Test.TestTrue(
				TEXT("WidgetCreateAndTransform should let the spawned player controller expose the created local player"),
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

using namespace AngelscriptTest_Bindings_AngelscriptWidgetCreateAndTransformFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptWidgetCreateAndTransformFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.WidgetCreateAndTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptWidgetCreateAndTransformFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASWidgetCreateAndTransform"));
		Engine.DiscardModule(TEXT("ASWidgetCreateAndTransformFixture"));
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

	const FString Script = TEXT(R"AS(
UUserWidget CreateWidgetViaFunctions(UClass WidgetClass, APlayerController OwningPlayer)
{
	TSubclassOf<UUserWidget> TypedWidgetClass = WidgetClass;
	return WidgetBlueprint::CreateWidget(TypedWidgetClass, OwningPlayer);
}

int ReadWidgetTransform(UWidget Widget)
{
	const FWidgetTransform Transform = Widget.GetRenderTransform();
	if (Transform.Translation.X != 13.5f || Transform.Translation.Y != -9.25f)
		return 10;
	if (Transform.Scale.X != 1.25f || Transform.Scale.Y != 0.75f)
		return 20;
	if (Transform.Angle != 42.0f)
		return 30;
	return 1;
}
)AS");

	asIScriptModule* Module = BuildModule(*this, Engine, WidgetCreateAndTransformModuleName, Script);
	if (Module == nullptr)
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

	if (!TestNotNull(TEXT("WidgetCreateAndTransform should produce a native widget baseline from UWidgetBlueprintLibrary::Create"), NativeWidget))
	{
		return false;
	}

	FScopedTestWorldContextScope WorldContextScope(Fixture.World);

	UObject* CreatedObject = nullptr;
	if (!ExecuteObjectFunction(
			*this,
			Engine,
			*Module,
			TEXT("UUserWidget CreateWidgetViaFunctions(UClass WidgetClass, APlayerController OwningPlayer)"),
			[this, WidgetClass, &Fixture](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, WidgetClass, TEXT("CreateWidgetViaFunctions(valid)"))
					&& SetArgObjectChecked(*this, Context, 1, Fixture.PlayerController, TEXT("CreateWidgetViaFunctions(valid)"));
			},
			TEXT("CreateWidgetViaFunctions(valid)"),
			CreatedObject))
	{
		return false;
	}

	UUserWidget* CreatedWidget = Cast<UUserWidget>(CreatedObject);
	ON_SCOPE_EXIT
	{
		if (CreatedWidget != nullptr)
		{
			CreatedWidget->MarkAsGarbage();
		}
	};

	bPassed &= TestNotNull(TEXT("WidgetCreateAndTransform should return a concrete user widget instance"), CreatedWidget);
	if (CreatedWidget == nullptr)
	{
		return false;
	}

	bPassed &= TestTrue(TEXT("WidgetCreateAndTransform should instantiate the requested scripted widget class"), CreatedWidget->GetClass() == WidgetClass);
	bPassed &= TestTrue(TEXT("WidgetCreateAndTransform should preserve native world parity for the created widget"), CreatedWidget->GetWorld() == NativeWidget->GetWorld());
	bPassed &= TestTrue(TEXT("WidgetCreateAndTransform should preserve native owning-player parity for the created widget"), CreatedWidget->GetOwningPlayer() == NativeWidget->GetOwningPlayer());

	FWidgetTransform ExpectedTransform;
	ExpectedTransform.Translation = FVector2D(13.5f, -9.25f);
	ExpectedTransform.Scale = FVector2D(1.25f, 0.75f);
	ExpectedTransform.Angle = 42.0f;
	CreatedWidget->SetRenderTransform(ExpectedTransform);

	int32 TransformResult = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int ReadWidgetTransform(UWidget Widget)"),
			[this, CreatedWidget](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, CreatedWidget, TEXT("ReadWidgetTransform(created)"));
			},
			TEXT("ReadWidgetTransform(created)"),
			TransformResult))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("WidgetCreateAndTransform should let the widget mixin read the native render transform from the script-created widget"),
		TransformResult,
		1);

	UObject* NullCreatedObject = nullptr;
	if (!ExecuteObjectFunction(
			*this,
			Engine,
			*Module,
			TEXT("UUserWidget CreateWidgetViaFunctions(UClass WidgetClass, APlayerController OwningPlayer)"),
			[this, &Fixture](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, nullptr, TEXT("CreateWidgetViaFunctions(nullclass)"))
					&& SetArgObjectChecked(*this, Context, 1, Fixture.PlayerController, TEXT("CreateWidgetViaFunctions(nullclass)"));
			},
			TEXT("CreateWidgetViaFunctions(nullclass)"),
			NullCreatedObject))
	{
		return false;
	}

	bPassed &= TestNull(
		TEXT("WidgetCreateAndTransform should fail closed and return null when the widget class input is null"),
		NullCreatedObject);

	ASTEST_END_FULL
	return bPassed;
}

#endif
