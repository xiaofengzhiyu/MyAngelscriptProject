#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptSceneComponentTransformStateBindingsTests_Private
{
	static constexpr ANSICHAR SceneComponentTransformCompatModuleName[] = "ASSceneComponentTransformStateCompat";
	static constexpr ANSICHAR SceneComponentScopedMovementCompatModuleName[] = "ASSceneComponentScopedMovementCompat";
	static constexpr TCHAR UnregisteredHostActorName[] = TEXT("SceneComponentTransformStateHost");
	static constexpr TCHAR UnregisteredParentComponentName[] = TEXT("SceneComponentTransformStateParent");
	static constexpr TCHAR UnregisteredChildComponentName[] = TEXT("SceneComponentTransformStateChild");
	static constexpr TCHAR RegisteredRootComponentName[] = TEXT("SceneComponentScopedMovementRoot");
	static constexpr TCHAR RegisteredChildComponentName[] = TEXT("SceneComponentScopedMovementChild");
	static constexpr double TransformTolerance = 0.01;

	static const FVector UnregisteredParentLocation(100.0f, -25.0f, 40.0f);
	static const FVector UnregisteredScriptRelativeLocation(-30.0f, 15.0f, 5.0f);
	static const FVector UnregisteredExpectedWorldLocation(
		UnregisteredParentLocation.X + UnregisteredScriptRelativeLocation.X,
		UnregisteredParentLocation.Y + UnregisteredScriptRelativeLocation.Y,
		UnregisteredParentLocation.Z + UnregisteredScriptRelativeLocation.Z);
	static const FVector UnregisteredExpectedVelocity(11.0f, -2.0f, 7.0f);

	static const FVector ScopedMovementRootLocation(25.0f, -5.0f, 10.0f);
	static const FVector ScopedMovementExpectedRelativeLocation(80.0f, 10.0f, -5.0f);
	static const FVector ScopedMovementExpectedVelocity(-4.0f, 6.0f, 2.5f);

	struct FUnregisteredSceneComponentFixture
	{
		AActor* HostActor = nullptr;
		USceneComponent* ParentComponent = nullptr;
		USceneComponent* ChildComponent = nullptr;
		FString ParentComponentPath;
		FString ChildComponentPath;
	};

	struct FRegisteredSceneComponentFixture
	{
		AActor* HostActor = nullptr;
		USceneComponent* RootComponent = nullptr;
		USceneComponent* ChildComponent = nullptr;
		FString ChildComponentPath;
		FVector ExpectedWorldLocation = FVector::ZeroVector;
	};

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FString EscapeScriptString(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	FString FormatDoubleLiteral(const double Value)
	{
		return LexToString(Value);
	}

	FString FormatVectorLiteral(const FVector& Value)
	{
		return FString::Printf(
			TEXT("FVector(%s, %s, %s)"),
			*LexToString(Value.X),
			*LexToString(Value.Y),
			*LexToString(Value.Z));
	}

	FUnregisteredSceneComponentFixture CreateUnregisteredFixture(FAutomationTestBase& Test)
	{
		FUnregisteredSceneComponentFixture Fixture;
		Fixture.HostActor = NewObject<AActor>(
			GetTransientPackage(),
			AActor::StaticClass(),
			UnregisteredHostActorName,
			RF_Transient);
		if (!Test.TestNotNull(
				TEXT("SceneComponentTransformStateCompat should create the transient host actor"),
				Fixture.HostActor))
		{
			return Fixture;
		}

		Fixture.HostActor->AddToRoot();

		Fixture.ParentComponent = NewObject<USceneComponent>(
			Fixture.HostActor,
			USceneComponent::StaticClass(),
			UnregisteredParentComponentName,
			RF_Transient);
		if (!Test.TestNotNull(
				TEXT("SceneComponentTransformStateCompat should create the unregistered parent scene component"),
				Fixture.ParentComponent))
		{
			return Fixture;
		}

		Fixture.ChildComponent = NewObject<USceneComponent>(
			Fixture.HostActor,
			USceneComponent::StaticClass(),
			UnregisteredChildComponentName,
			RF_Transient);
		if (!Test.TestNotNull(
				TEXT("SceneComponentTransformStateCompat should create the unregistered child scene component"),
				Fixture.ChildComponent))
		{
			return Fixture;
		}

		Fixture.HostActor->AddOwnedComponent(Fixture.ParentComponent);
		Fixture.HostActor->SetRootComponent(Fixture.ParentComponent);
		Fixture.ParentComponent->SetRelativeLocation(UnregisteredParentLocation);

		Fixture.HostActor->AddOwnedComponent(Fixture.ChildComponent);
		Fixture.ChildComponent->SetupAttachment(Fixture.ParentComponent);
		Fixture.ChildComponent->SetRelativeLocation(FVector::ZeroVector);

		Fixture.ParentComponentPath = Fixture.ParentComponent->GetPathName();
		Fixture.ChildComponentPath = Fixture.ChildComponent->GetPathName();
		return Fixture;
	}

	bool VerifyUnregisteredFixtureBaseline(
		FAutomationTestBase& Test,
		const FUnregisteredSceneComponentFixture& Fixture)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentTransformStateCompat native baseline should keep the transient host actor alive"),
			Fixture.HostActor);
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentTransformStateCompat native baseline should keep the parent scene component alive"),
			Fixture.ParentComponent);
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentTransformStateCompat native baseline should keep the child scene component alive"),
			Fixture.ChildComponent);
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentTransformStateCompat fixture should expose non-empty object paths"),
			!Fixture.ParentComponentPath.IsEmpty() && !Fixture.ChildComponentPath.IsEmpty());
		bPassed &= Test.TestFalse(
			TEXT("SceneComponentTransformStateCompat parent fixture component should remain unregistered"),
			Fixture.ParentComponent != nullptr && Fixture.ParentComponent->IsRegistered());
		bPassed &= Test.TestFalse(
			TEXT("SceneComponentTransformStateCompat child fixture component should remain unregistered"),
			Fixture.ChildComponent != nullptr && Fixture.ChildComponent->IsRegistered());
		bPassed &= Test.TestEqual(
			TEXT("SceneComponentTransformStateCompat child fixture component should attach to the parent component"),
			Fixture.ChildComponent != nullptr ? Fixture.ChildComponent->GetAttachParent() : nullptr,
			Fixture.ParentComponent);
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentTransformStateCompat parent fixture component should preserve its native relative location"),
			Fixture.ParentComponent != nullptr && Fixture.ParentComponent->GetRelativeLocation().Equals(UnregisteredParentLocation, 0.0f));
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentTransformStateCompat child fixture component should start at zero relative location"),
			Fixture.ChildComponent != nullptr && Fixture.ChildComponent->GetRelativeLocation().Equals(FVector::ZeroVector, 0.0f));
		return bPassed;
	}

	FString BuildUnregisteredTransformScript(const FUnregisteredSceneComponentFixture& Fixture)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	USceneComponent Parent = Cast<USceneComponent>(FindObject("__PARENT_COMPONENT_PATH__"));
	USceneComponent Child = Cast<USceneComponent>(FindObject("__CHILD_COMPONENT_PATH__"));
	if (Parent == null || Child == null)
		return 2;

	Child.SetRelativeLocation(__EXPECTED_RELATIVE_LOCATION__);
	if (!Child.GetRelativeLocation().Equals(__EXPECTED_RELATIVE_LOCATION__, __TRANSFORM_TOLERANCE__))
		return 10;

	const FVector WorldTranslation = Child.GetComponentTransform().GetTranslation();
	if (!WorldTranslation.Equals(__EXPECTED_WORLD_LOCATION__, __TRANSFORM_TOLERANCE__))
		return 20;

	Child.SetComponentVelocity(__EXPECTED_VELOCITY__);
	if (!Child.GetComponentVelocity().Equals(__EXPECTED_VELOCITY__, __TRANSFORM_TOLERANCE__))
		return 30;

	return 1;
}
)AS");

		ReplaceToken(Script, TEXT("__PARENT_COMPONENT_PATH__"), EscapeScriptString(Fixture.ParentComponentPath));
		ReplaceToken(Script, TEXT("__CHILD_COMPONENT_PATH__"), EscapeScriptString(Fixture.ChildComponentPath));
		ReplaceToken(Script, TEXT("__EXPECTED_RELATIVE_LOCATION__"), FormatVectorLiteral(UnregisteredScriptRelativeLocation));
		ReplaceToken(Script, TEXT("__EXPECTED_WORLD_LOCATION__"), FormatVectorLiteral(UnregisteredExpectedWorldLocation));
		ReplaceToken(Script, TEXT("__EXPECTED_VELOCITY__"), FormatVectorLiteral(UnregisteredExpectedVelocity));
		ReplaceToken(Script, TEXT("__TRANSFORM_TOLERANCE__"), FormatDoubleLiteral(TransformTolerance));
		return Script;
	}

	FRegisteredSceneComponentFixture CreateRegisteredFixture(FAutomationTestBase& Test, AActor& HostActor)
	{
		FRegisteredSceneComponentFixture Fixture;
		Fixture.HostActor = &HostActor;

		Fixture.RootComponent = NewObject<USceneComponent>(
			&HostActor,
			USceneComponent::StaticClass(),
			RegisteredRootComponentName);
		if (!Test.TestNotNull(
				TEXT("SceneComponentScopedMovementCompat should create the registered root scene component"),
				Fixture.RootComponent))
		{
			return Fixture;
		}

		HostActor.AddInstanceComponent(Fixture.RootComponent);
		HostActor.SetRootComponent(Fixture.RootComponent);
		Fixture.RootComponent->RegisterComponent();
		Fixture.RootComponent->SetWorldLocation(ScopedMovementRootLocation);

		Fixture.ChildComponent = NewObject<USceneComponent>(
			&HostActor,
			USceneComponent::StaticClass(),
			RegisteredChildComponentName);
		if (!Test.TestNotNull(
				TEXT("SceneComponentScopedMovementCompat should create the registered child scene component"),
				Fixture.ChildComponent))
		{
			return Fixture;
		}

		HostActor.AddInstanceComponent(Fixture.ChildComponent);
		Fixture.ChildComponent->SetupAttachment(Fixture.RootComponent);
		Fixture.ChildComponent->RegisterComponent();
		Fixture.ChildComponent->SetRelativeLocation(FVector::ZeroVector);
		Fixture.ExpectedWorldLocation = Fixture.RootComponent->GetComponentTransform().TransformPosition(ScopedMovementExpectedRelativeLocation);
		Fixture.ChildComponentPath = Fixture.ChildComponent->GetPathName();
		return Fixture;
	}

	bool VerifyRegisteredFixtureBaseline(
		FAutomationTestBase& Test,
		const FRegisteredSceneComponentFixture& Fixture)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentScopedMovementCompat native baseline should keep the spawned host actor alive"),
			Fixture.HostActor);
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentScopedMovementCompat native baseline should keep the registered root scene component alive"),
			Fixture.RootComponent);
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentScopedMovementCompat native baseline should keep the registered child scene component alive"),
			Fixture.ChildComponent);
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentScopedMovementCompat fixture should expose a non-empty child component path"),
			!Fixture.ChildComponentPath.IsEmpty());
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentScopedMovementCompat root scene component should be registered"),
			Fixture.RootComponent != nullptr && Fixture.RootComponent->IsRegistered());
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentScopedMovementCompat child scene component should be registered"),
			Fixture.ChildComponent != nullptr && Fixture.ChildComponent->IsRegistered());
		bPassed &= Test.TestEqual(
			TEXT("SceneComponentScopedMovementCompat child scene component should attach to the registered root scene component"),
			Fixture.ChildComponent != nullptr ? Fixture.ChildComponent->GetAttachParent() : nullptr,
			Fixture.RootComponent);
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentScopedMovementCompat child scene component should start at zero relative location"),
			Fixture.ChildComponent != nullptr && Fixture.ChildComponent->GetRelativeLocation().Equals(FVector::ZeroVector, 0.0f));
		return bPassed;
	}

	FString BuildScopedMovementScript(const FRegisteredSceneComponentFixture& Fixture)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	USceneComponent Component = Cast<USceneComponent>(FindObject("__CHILD_COMPONENT_PATH__"));
	if (Component == null)
		return 2;

	{
		FScopedMovementUpdate ScopedMove(Component);
		Component.SetRelativeLocation(__EXPECTED_RELATIVE_LOCATION__);
		Component.SetComponentVelocity(__EXPECTED_VELOCITY__);
		if (!Component.GetRelativeLocation().Equals(__EXPECTED_RELATIVE_LOCATION__, __TRANSFORM_TOLERANCE__))
			return 10;
		if (!Component.GetComponentVelocity().Equals(__EXPECTED_VELOCITY__, __TRANSFORM_TOLERANCE__))
			return 20;
	}

	const FVector WorldTranslation = Component.GetComponentTransform().GetTranslation();
	if (!WorldTranslation.Equals(__EXPECTED_WORLD_LOCATION__, __TRANSFORM_TOLERANCE__))
		return 30;

	return 1;
}
)AS");

		ReplaceToken(Script, TEXT("__CHILD_COMPONENT_PATH__"), EscapeScriptString(Fixture.ChildComponentPath));
		ReplaceToken(Script, TEXT("__EXPECTED_RELATIVE_LOCATION__"), FormatVectorLiteral(ScopedMovementExpectedRelativeLocation));
		ReplaceToken(Script, TEXT("__EXPECTED_VELOCITY__"), FormatVectorLiteral(ScopedMovementExpectedVelocity));
		ReplaceToken(Script, TEXT("__EXPECTED_WORLD_LOCATION__"), FormatVectorLiteral(Fixture.ExpectedWorldLocation));
		ReplaceToken(Script, TEXT("__TRANSFORM_TOLERANCE__"), FormatDoubleLiteral(TransformTolerance));
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptSceneComponentTransformStateBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSceneComponentTransformStateCompatBindingsTest,
	"Angelscript.TestModule.Bindings.SceneComponentTransformStateCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSceneComponentScopedMovementCompatBindingsTest,
	"Angelscript.TestModule.Bindings.SceneComponentScopedMovementCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptSceneComponentTransformStateCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	bool bModuleCompiled = false;
	FUnregisteredSceneComponentFixture Fixture = CreateUnregisteredFixture(*this);
	ON_SCOPE_EXIT
	{
		if (bModuleCompiled)
		{
			Engine.DiscardModule(TEXT("ASSceneComponentTransformStateCompat"));
		}

		if (Fixture.HostActor != nullptr)
		{
			Fixture.HostActor->RemoveFromRoot();
			Fixture.HostActor->MarkAsGarbage();
		}
	};

	if (!VerifyUnregisteredFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		SceneComponentTransformCompatModuleName,
		BuildUnregisteredTransformScript(Fixture));
	if (Module == nullptr)
	{
		return false;
	}
	bModuleCompiled = true;

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("USceneComponent transform-state bindings should preserve unregistered parent transform composition and velocity roundtrip"),
		Result,
		1);
	bPassed &= TestTrue(
		TEXT("USceneComponent SetRelativeLocation binding should update the native relative location on unregistered child components"),
		Fixture.ChildComponent != nullptr && Fixture.ChildComponent->GetRelativeLocation().Equals(UnregisteredScriptRelativeLocation, 0.0f));
	bPassed &= TestTrue(
		TEXT("USceneComponent SetComponentVelocity binding should update the native component velocity on unregistered child components"),
		Fixture.ChildComponent != nullptr && Fixture.ChildComponent->GetComponentVelocity().Equals(UnregisteredExpectedVelocity, 0.0f));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptSceneComponentScopedMovementCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASSceneComponentScopedMovementCompat"));
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& HostActor = Spawner.SpawnActor<AActor>();
	FRegisteredSceneComponentFixture Fixture = CreateRegisteredFixture(*this, HostActor);
	if (!VerifyRegisteredFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		SceneComponentScopedMovementCompatModuleName,
		BuildScopedMovementScript(Fixture));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("USceneComponent scoped-movement bindings should construct and tear down FScopedMovementUpdate while preserving scripted movement changes"),
		Result,
		1);
	bPassed &= TestTrue(
		TEXT("FScopedMovementUpdate binding should preserve the scripted relative location on registered child components"),
		Fixture.ChildComponent != nullptr && Fixture.ChildComponent->GetRelativeLocation().Equals(ScopedMovementExpectedRelativeLocation, TransformTolerance));
	bPassed &= TestTrue(
		TEXT("FScopedMovementUpdate binding should preserve the scripted component velocity on registered child components"),
		Fixture.ChildComponent != nullptr && Fixture.ChildComponent->GetComponentVelocity().Equals(ScopedMovementExpectedVelocity, TransformTolerance));
	bPassed &= TestTrue(
		TEXT("FScopedMovementUpdate binding should preserve the expected world transform after the scoped movement completes"),
		Fixture.ChildComponent != nullptr && Fixture.ChildComponent->GetComponentTransform().GetTranslation().Equals(Fixture.ExpectedWorldLocation, TransformTolerance));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
