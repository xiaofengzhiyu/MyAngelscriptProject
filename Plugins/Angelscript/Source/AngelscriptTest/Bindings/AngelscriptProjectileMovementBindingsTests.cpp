#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptProjectileMovementHomingTargetCompatBindingsTest,
	"Angelscript.TestModule.Bindings.ProjectileMovementHomingTargetCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptProjectileMovementBindingsTests_Private
{
	static constexpr ANSICHAR ProjectileMovementBindingsModuleName[] = "ASProjectileMovementHomingTargetCompat";
	static constexpr TCHAR MovementActorName[] = TEXT("ProjectileMovementBindingsActor");
	static constexpr TCHAR MovementRootName[] = TEXT("ProjectileMovementBindingsRoot");
	static constexpr TCHAR MovementComponentName[] = TEXT("ProjectileMovementBindingsComponent");
	static constexpr TCHAR FirstTargetActorName[] = TEXT("ProjectileMovementBindingsFirstTargetActor");
	static constexpr TCHAR FirstTargetRootName[] = TEXT("ProjectileMovementBindingsFirstTargetRoot");
	static constexpr TCHAR SecondTargetActorName[] = TEXT("ProjectileMovementBindingsSecondTargetActor");
	static constexpr TCHAR SecondTargetRootName[] = TEXT("ProjectileMovementBindingsSecondTargetRoot");

	struct FProjectileMovementBindingFixture
	{
		AActor* MovementActor = nullptr;
		USceneComponent* MovementRoot = nullptr;
		UProjectileMovementComponent* MovementComponent = nullptr;
		AActor* FirstTargetActor = nullptr;
		USceneComponent* FirstTargetComponent = nullptr;
		AActor* SecondTargetActor = nullptr;
		USceneComponent* SecondTargetComponent = nullptr;
		FString MovementComponentPath;
		FString FirstTargetComponentPath;
		FString SecondTargetComponentPath;
	};

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FString EscapeScriptString(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	FProjectileMovementBindingFixture CreateFixture(FAutomationTestBase& Test)
	{
		FProjectileMovementBindingFixture Fixture;

		Fixture.MovementActor = NewObject<AActor>(
			GetTransientPackage(),
			AActor::StaticClass(),
			MovementActorName,
			RF_Transient);
		if (!Test.TestNotNull(TEXT("ProjectileMovementHomingTargetCompat should create the transient movement actor"), Fixture.MovementActor))
		{
			return Fixture;
		}

		Fixture.MovementRoot = NewObject<USceneComponent>(
			Fixture.MovementActor,
			USceneComponent::StaticClass(),
			MovementRootName,
			RF_Transient);
		if (!Test.TestNotNull(TEXT("ProjectileMovementHomingTargetCompat should create the movement root scene component"), Fixture.MovementRoot))
		{
			return Fixture;
		}

		Fixture.MovementActor->AddInstanceComponent(Fixture.MovementRoot);
		Fixture.MovementActor->SetRootComponent(Fixture.MovementRoot);

		Fixture.MovementComponent = NewObject<UProjectileMovementComponent>(
			Fixture.MovementActor,
			UProjectileMovementComponent::StaticClass(),
			MovementComponentName,
			RF_Transient);
		if (!Test.TestNotNull(TEXT("ProjectileMovementHomingTargetCompat should create the projectile movement component"), Fixture.MovementComponent))
		{
			return Fixture;
		}

		Fixture.MovementActor->AddInstanceComponent(Fixture.MovementComponent);
		Fixture.MovementComponent->SetUpdatedComponent(Fixture.MovementRoot);

		Fixture.FirstTargetActor = NewObject<AActor>(
			GetTransientPackage(),
			AActor::StaticClass(),
			FirstTargetActorName,
			RF_Transient);
		if (!Test.TestNotNull(TEXT("ProjectileMovementHomingTargetCompat should create the first target actor"), Fixture.FirstTargetActor))
		{
			return Fixture;
		}

		Fixture.FirstTargetComponent = NewObject<USceneComponent>(
			Fixture.FirstTargetActor,
			USceneComponent::StaticClass(),
			FirstTargetRootName,
			RF_Transient);
		if (!Test.TestNotNull(TEXT("ProjectileMovementHomingTargetCompat should create the first target scene component"), Fixture.FirstTargetComponent))
		{
			return Fixture;
		}

		Fixture.FirstTargetActor->AddInstanceComponent(Fixture.FirstTargetComponent);
		Fixture.FirstTargetActor->SetRootComponent(Fixture.FirstTargetComponent);

		Fixture.SecondTargetActor = NewObject<AActor>(
			GetTransientPackage(),
			AActor::StaticClass(),
			SecondTargetActorName,
			RF_Transient);
		if (!Test.TestNotNull(TEXT("ProjectileMovementHomingTargetCompat should create the second target actor"), Fixture.SecondTargetActor))
		{
			return Fixture;
		}

		Fixture.SecondTargetComponent = NewObject<USceneComponent>(
			Fixture.SecondTargetActor,
			USceneComponent::StaticClass(),
			SecondTargetRootName,
			RF_Transient);
		if (!Test.TestNotNull(TEXT("ProjectileMovementHomingTargetCompat should create the second target scene component"), Fixture.SecondTargetComponent))
		{
			return Fixture;
		}

		Fixture.SecondTargetActor->AddInstanceComponent(Fixture.SecondTargetComponent);
		Fixture.SecondTargetActor->SetRootComponent(Fixture.SecondTargetComponent);

		Fixture.MovementComponent->HomingTargetComponent = Fixture.FirstTargetComponent;
		Fixture.MovementComponentPath = Fixture.MovementComponent->GetPathName();
		Fixture.FirstTargetComponentPath = Fixture.FirstTargetComponent->GetPathName();
		Fixture.SecondTargetComponentPath = Fixture.SecondTargetComponent->GetPathName();
		return Fixture;
	}

	bool VerifyNativeFixtureBaseline(
		FAutomationTestBase& Test,
		const FProjectileMovementBindingFixture& Fixture)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			TEXT("ProjectileMovementHomingTargetCompat native baseline should keep the projectile movement component"),
			Fixture.MovementComponent);
		bPassed &= Test.TestNotNull(
			TEXT("ProjectileMovementHomingTargetCompat native baseline should keep the first target component"),
			Fixture.FirstTargetComponent);
		bPassed &= Test.TestNotNull(
			TEXT("ProjectileMovementHomingTargetCompat native baseline should keep the second target component"),
			Fixture.SecondTargetComponent);
		bPassed &= Test.TestTrue(
			TEXT("ProjectileMovementHomingTargetCompat fixture should expose a non-empty movement-component path"),
			!Fixture.MovementComponentPath.IsEmpty());
		bPassed &= Test.TestTrue(
			TEXT("ProjectileMovementHomingTargetCompat fixture should expose a non-empty first-target path"),
			!Fixture.FirstTargetComponentPath.IsEmpty());
		bPassed &= Test.TestTrue(
			TEXT("ProjectileMovementHomingTargetCompat fixture should expose a non-empty second-target path"),
			!Fixture.SecondTargetComponentPath.IsEmpty());
		bPassed &= Test.TestTrue(
			TEXT("ProjectileMovementHomingTargetCompat native baseline should start on the first target component"),
			Fixture.MovementComponent != nullptr
				&& Fixture.MovementComponent->HomingTargetComponent.Get() == Fixture.FirstTargetComponent);
		bPassed &= Test.TestTrue(
			TEXT("ProjectileMovementHomingTargetCompat native fixture should use distinct first and second target components"),
			Fixture.FirstTargetComponent != Fixture.SecondTargetComponent);
		return bPassed;
	}

	bool VerifyNativePostconditions(
		FAutomationTestBase& Test,
		const FProjectileMovementBindingFixture& Fixture)
	{
		return Test.TestTrue(
			TEXT("ProjectileMovement SetHomingTargetComponent should update the native weak-pointer target immediately"),
			Fixture.MovementComponent != nullptr
				&& Fixture.MovementComponent->HomingTargetComponent.Get() == Fixture.SecondTargetComponent);
	}

	FString BuildProjectileMovementScript(const FProjectileMovementBindingFixture& Fixture)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	UObject MovementObject = FindObject("__MOVEMENT_COMPONENT_PATH__");
	UObject FirstTargetObject = FindObject("__FIRST_TARGET_COMPONENT_PATH__");
	UObject SecondTargetObject = FindObject("__SECOND_TARGET_COMPONENT_PATH__");

	UProjectileMovementComponent Movement = Cast<UProjectileMovementComponent>(MovementObject);
	USceneComponent FirstTarget = Cast<USceneComponent>(FirstTargetObject);
	USceneComponent SecondTarget = Cast<USceneComponent>(SecondTargetObject);
	if (Movement == null || FirstTarget == null || SecondTarget == null)
		return 2;

	if (Movement.GetHomingTargetComponent() != FirstTarget)
		return 4;

	Movement.SetHomingTargetComponent(SecondTarget);
	if (Movement.GetHomingTargetComponent() != SecondTarget)
		return 8;

	return 1;
}
)AS");

		ReplaceToken(Script, TEXT("__MOVEMENT_COMPONENT_PATH__"), EscapeScriptString(Fixture.MovementComponentPath));
		ReplaceToken(Script, TEXT("__FIRST_TARGET_COMPONENT_PATH__"), EscapeScriptString(Fixture.FirstTargetComponentPath));
		ReplaceToken(Script, TEXT("__SECOND_TARGET_COMPONENT_PATH__"), EscapeScriptString(Fixture.SecondTargetComponentPath));
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptProjectileMovementBindingsTests_Private;

bool FAngelscriptProjectileMovementHomingTargetCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	bool bModuleCompiled = false;
	FProjectileMovementBindingFixture Fixture = CreateFixture(*this);

	ON_SCOPE_EXIT
	{
		if (bModuleCompiled)
		{
			Engine.DiscardModule(TEXT("ASProjectileMovementHomingTargetCompat"));
		}

		if (Fixture.MovementActor != nullptr)
		{
			Fixture.MovementActor->MarkAsGarbage();
		}

		if (Fixture.FirstTargetActor != nullptr)
		{
			Fixture.FirstTargetActor->MarkAsGarbage();
		}

		if (Fixture.SecondTargetActor != nullptr)
		{
			Fixture.SecondTargetActor->MarkAsGarbage();
		}
	};

	if (!VerifyNativeFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ProjectileMovementBindingsModuleName,
		BuildProjectileMovementScript(Fixture));
	if (Module == nullptr)
	{
		return false;
	}
	bModuleCompiled = true;

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed = TestEqual(
		TEXT("ProjectileMovement homing-target bindings should preserve native getter/setter object identity parity"),
		Result,
		1);
	bPassed &= VerifyNativePostconditions(*this, Fixture);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
