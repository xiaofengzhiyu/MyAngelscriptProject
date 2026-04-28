#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Particles/ParticleSystemComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptFXSystemBindingsTests_Private
{
	static constexpr ANSICHAR FXSystemBindingsModuleName[] = "ASFXSystemDeactivateImmediateCompat";
	static constexpr TCHAR RootComponentName[] = TEXT("FXSystemBindingsRoot");
	static constexpr TCHAR ParticleComponentName[] = TEXT("FXSystemBindingsParticle");

	struct FFXSystemFixture
	{
		AActor* HostActor = nullptr;
		USceneComponent* RootComponent = nullptr;
		UParticleSystemComponent* ParticleComponent = nullptr;
		FString ParticleComponentPath;
	};

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FString EscapeScriptString(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	FFXSystemFixture CreateFixture(FAutomationTestBase& Test, AActor& HostActor)
	{
		FFXSystemFixture Fixture;
		Fixture.HostActor = &HostActor;

		Fixture.RootComponent = NewObject<USceneComponent>(
			&HostActor,
			USceneComponent::StaticClass(),
			RootComponentName);
		if (!Test.TestNotNull(
				TEXT("FXSystemDeactivateImmediateCompat should create the root scene component"),
				Fixture.RootComponent))
		{
			return Fixture;
		}

		HostActor.AddInstanceComponent(Fixture.RootComponent);
		HostActor.SetRootComponent(Fixture.RootComponent);
		Fixture.RootComponent->RegisterComponent();

		Fixture.ParticleComponent = NewObject<UParticleSystemComponent>(
			&HostActor,
			UParticleSystemComponent::StaticClass(),
			ParticleComponentName);
		if (!Test.TestNotNull(
				TEXT("FXSystemDeactivateImmediateCompat should create the particle-system component"),
				Fixture.ParticleComponent))
		{
			return Fixture;
		}

		HostActor.AddInstanceComponent(Fixture.ParticleComponent);
		Fixture.ParticleComponent->SetupAttachment(Fixture.RootComponent);
		Fixture.ParticleComponent->RegisterComponent();
		Fixture.ParticleComponent->SetActiveFlag(true);
		Fixture.ParticleComponentPath = Fixture.ParticleComponent->GetPathName();
		return Fixture;
	}

	bool VerifyNativeFixtureBaseline(FAutomationTestBase& Test, const FFXSystemFixture& Fixture)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			TEXT("FXSystemDeactivateImmediateCompat native baseline should keep the spawned host actor alive"),
			Fixture.HostActor);
		bPassed &= Test.TestNotNull(
			TEXT("FXSystemDeactivateImmediateCompat native baseline should keep the root scene component alive"),
			Fixture.RootComponent);
		bPassed &= Test.TestNotNull(
			TEXT("FXSystemDeactivateImmediateCompat native baseline should keep the particle-system component alive"),
			Fixture.ParticleComponent);
		bPassed &= Test.TestTrue(
			TEXT("FXSystemDeactivateImmediateCompat fixture should expose a non-empty particle-component path"),
			!Fixture.ParticleComponentPath.IsEmpty());
		bPassed &= Test.TestTrue(
			TEXT("FXSystemDeactivateImmediateCompat particle-system component should be registered"),
			Fixture.ParticleComponent != nullptr && Fixture.ParticleComponent->IsRegistered());
		bPassed &= Test.TestEqual(
			TEXT("FXSystemDeactivateImmediateCompat particle-system component should attach under the dedicated root scene component"),
			Fixture.ParticleComponent != nullptr ? Fixture.ParticleComponent->GetAttachParent() : nullptr,
			Fixture.RootComponent);
		bPassed &= Test.TestTrue(
			TEXT("FXSystemDeactivateImmediateCompat native fixture should start with an active particle-system component"),
			Fixture.ParticleComponent != nullptr && Fixture.ParticleComponent->IsActive());
		return bPassed;
	}

	bool VerifyNativePostconditions(FAutomationTestBase& Test, const FFXSystemFixture& Fixture)
	{
		return Test.TestFalse(
			TEXT("UFXSystemComponent::DeactivateImmediate should clear the active state on the native particle-system component"),
			Fixture.ParticleComponent != nullptr && Fixture.ParticleComponent->IsActive());
	}

	FString BuildScript(const FFXSystemFixture& Fixture)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	UFXSystemComponent FXComponent = Cast<UFXSystemComponent>(FindObject("__PARTICLE_COMPONENT_PATH__"));
	if (FXComponent == null)
		return 2;

	FXComponent.DeactivateImmediate();
	FXComponent.DeactivateImmediate();
	return 1;
}
)AS");

		ReplaceToken(Script, TEXT("__PARTICLE_COMPONENT_PATH__"), EscapeScriptString(Fixture.ParticleComponentPath));
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptFXSystemBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFXSystemDeactivateImmediateCompatBindingsTest,
	"Angelscript.TestModule.Bindings.FXSystemDeactivateImmediateCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptFXSystemDeactivateImmediateCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASFXSystemDeactivateImmediateCompat"));
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& HostActor = Spawner.SpawnActor<AActor>();
	const FFXSystemFixture Fixture = CreateFixture(*this, HostActor);
	if (!VerifyNativeFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		FXSystemBindingsModuleName,
		BuildScript(Fixture));
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
		TEXT("UFXSystemComponent::DeactivateImmediate binding should preserve runtime parity on a live particle-system component fixture"),
		Result,
		1);
	bPassed &= VerifyNativePostconditions(*this, Fixture);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
