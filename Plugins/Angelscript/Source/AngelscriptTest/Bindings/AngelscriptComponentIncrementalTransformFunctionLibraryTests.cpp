#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "../../AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h"

#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptComponentIncrementalTransformFunctionLibraryTests_Private
{
	static constexpr ANSICHAR ComponentIncrementalTransformModuleName[] = "ASComponentIncrementalOffsetAndRotatorOverloads";
	static constexpr double VectorTolerance = 0.05;
	static constexpr double RotationToleranceDegrees = 0.1;

	static const FTransform ParentWorldTransform(
		FRotator(5.0, 90.0, 0.0),
		FVector(100.0, 50.0, 0.0),
		FVector::OneVector);
	static const FTransform InitialRelativeTransform(
		FRotator(-15.0, 20.0, 5.0),
		FVector(5.0, -10.0, 15.0),
		FVector(1.1, 0.9, 1.0));
	static const FVector AddRelativeLocationDelta(0.0, 20.0, 0.0);
	static const FRotator SetRelativeRotationTarget(10.0, -30.0, 25.0);
	static const FRotator AddRelativeRotationDelta(-5.0, 15.0, 0.0);
	static const FVector AddWorldOffsetDelta(10.0, 0.0, 0.0);
	static const FRotator AddWorldRotationDelta(0.0, 20.0, -10.0);

	struct FSceneComponentTransformFixture
	{
		AActor* HostActor = nullptr;
		USceneComponent* RootComponent = nullptr;
		USceneComponent* ChildComponent = nullptr;
		FString ChildComponentPath;
	};

	bool QuatMatches(const FQuat& Actual, const FQuat& Expected, double ToleranceDegrees = RotationToleranceDegrees)
	{
		FQuat ActualQuat = Actual;
		FQuat ExpectedQuat = Expected;
		ActualQuat.Normalize();
		ExpectedQuat.Normalize();

		if ((ActualQuat | ExpectedQuat) < 0.0)
		{
			ExpectedQuat = FQuat(-ExpectedQuat.X, -ExpectedQuat.Y, -ExpectedQuat.Z, -ExpectedQuat.W);
		}

		return FMath::RadiansToDegrees(ActualQuat.AngularDistance(ExpectedQuat)) <= ToleranceDegrees;
	}

	bool VerifyTransform(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FTransform& Actual,
		const FTransform& Expected,
		double Tolerance = VectorTolerance)
	{
		const bool bLocationMatches = Actual.GetLocation().Equals(Expected.GetLocation(), Tolerance);
		const bool bScaleMatches = Actual.GetScale3D().Equals(Expected.GetScale3D(), Tolerance);
		const bool bRotationMatches = QuatMatches(Actual.GetRotation(), Expected.GetRotation());
		return Test.TestTrue(What, bLocationMatches && bScaleMatches && bRotationMatches);
	}

	bool VerifyComponentStateMatchesMirror(
		FAutomationTestBase& Test,
		const TCHAR* StepLabel,
		const USceneComponent& ScriptComponent,
		const USceneComponent& MirrorComponent)
	{
		bool bPassed = true;
		bPassed &= VerifyTransform(
			Test,
			*FString::Printf(TEXT("%s should preserve the same relative transform as the native mirror"), StepLabel),
			ScriptComponent.GetRelativeTransform(),
			MirrorComponent.GetRelativeTransform());
		bPassed &= VerifyTransform(
			Test,
			*FString::Printf(TEXT("%s should preserve the same world transform as the native mirror"), StepLabel),
			ScriptComponent.GetComponentTransform(),
			MirrorComponent.GetComponentTransform());
		return bPassed;
	}

	FString EscapeScriptString(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FSceneComponentTransformFixture CreateFixture(
		FAutomationTestBase& Test,
		FActorTestSpawner& Spawner,
		const TCHAR* RootLabel,
		const TCHAR* ChildLabel)
	{
		FSceneComponentTransformFixture Fixture;
		Fixture.HostActor = &Spawner.SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("Incremental component transform fixture should spawn a host actor"), Fixture.HostActor))
		{
			return Fixture;
		}

		Fixture.RootComponent = NewObject<USceneComponent>(Fixture.HostActor, FName(RootLabel));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should create a root scene component"), RootLabel),
				Fixture.RootComponent))
		{
			return Fixture;
		}

		Fixture.HostActor->AddInstanceComponent(Fixture.RootComponent);
		Fixture.HostActor->SetRootComponent(Fixture.RootComponent);
		Fixture.RootComponent->SetMobility(EComponentMobility::Movable);
		Fixture.RootComponent->RegisterComponent();
		Fixture.RootComponent->SetWorldTransform(ParentWorldTransform);

		Fixture.ChildComponent = NewObject<USceneComponent>(Fixture.HostActor, FName(ChildLabel));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should create a child scene component"), ChildLabel),
				Fixture.ChildComponent))
		{
			return Fixture;
		}

		Fixture.HostActor->AddInstanceComponent(Fixture.ChildComponent);
		Fixture.ChildComponent->SetupAttachment(Fixture.RootComponent);
		Fixture.ChildComponent->SetMobility(EComponentMobility::Movable);
		Fixture.ChildComponent->RegisterComponent();
		Fixture.ChildComponent->SetRelativeTransform(InitialRelativeTransform);
		Fixture.ChildComponentPath = Fixture.ChildComponent->GetPathName();
		return Fixture;
	}

	FString BuildScenarioScript(const FString& ComponentPath)
	{
		FString Script = TEXT(R"AS(
USceneComponent ResolveComponent()
{
	return Cast<USceneComponent>(FindObject("__COMPONENT_PATH__"));
}

int AddRelativeLocationStep()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return 2;

	Component.AddRelativeLocation(FVector(0.0, 20.0, 0.0));
	return 1;
}

int SetRelativeRotationStep()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return 2;

	Component.SetRelativeRotation(FRotator(10.0, -30.0, 25.0));
	return 1;
}

int AddRelativeRotationStep()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return 2;

	Component.AddRelativeRotation(FRotator(-5.0, 15.0, 0.0));
	return 1;
}

int AddWorldOffsetStep()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return 2;

	Component.AddWorldOffset(FVector(10.0, 0.0, 0.0));
	return 1;
}

int AddWorldRotationStep()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return 2;

	Component.AddWorldRotation(FRotator(0.0, 20.0, -10.0));
	return 1;
}
)AS");

		ReplaceToken(Script, TEXT("__COMPONENT_PATH__"), EscapeScriptString(ComponentPath));
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptComponentIncrementalTransformFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptComponentIncrementalOffsetAndRotatorOverloadsFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.ComponentIncrementalOffsetAndRotatorOverloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptComponentIncrementalOffsetAndRotatorOverloadsFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	FSceneComponentTransformFixture ScriptFixture = CreateFixture(
		*this,
		Spawner,
		TEXT("ComponentIncrementalScriptRoot"),
		TEXT("ComponentIncrementalScriptChild"));
	FSceneComponentTransformFixture MirrorFixture = CreateFixture(
		*this,
		Spawner,
		TEXT("ComponentIncrementalMirrorRoot"),
		TEXT("ComponentIncrementalMirrorChild"));
	if (ScriptFixture.HostActor == nullptr
		|| ScriptFixture.RootComponent == nullptr
		|| ScriptFixture.ChildComponent == nullptr
		|| MirrorFixture.HostActor == nullptr
		|| MirrorFixture.RootComponent == nullptr
		|| MirrorFixture.ChildComponent == nullptr)
	{
		return false;
	}

	FScopedTestWorldContextScope WorldContextScope(ScriptFixture.HostActor);

	const TCHAR* RequiredFunctions[] = {
		TEXT("AddRelativeLocation"),
		TEXT("SetRelativeRotation"),
		TEXT("AddRelativeRotation"),
		TEXT("AddWorldOffset"),
		TEXT("AddWorldRotation")
	};
	for (const TCHAR* FunctionName : RequiredFunctions)
	{
		bPassed &= TestNotNull(
			*FString::Printf(TEXT("Incremental component transform test should expose %s"), FunctionName),
			UAngelscriptComponentLibrary::StaticClass()->FindFunctionByName(FunctionName));
	}
	if (!bPassed)
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ComponentIncrementalTransformModuleName,
		BuildScenarioScript(ScriptFixture.ChildComponentPath));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* AddRelativeLocationFunction = GetFunctionByDecl(*this, *Module, TEXT("int AddRelativeLocationStep()"));
	asIScriptFunction* SetRelativeRotationFunction = GetFunctionByDecl(*this, *Module, TEXT("int SetRelativeRotationStep()"));
	asIScriptFunction* AddRelativeRotationFunction = GetFunctionByDecl(*this, *Module, TEXT("int AddRelativeRotationStep()"));
	asIScriptFunction* AddWorldOffsetFunction = GetFunctionByDecl(*this, *Module, TEXT("int AddWorldOffsetStep()"));
	asIScriptFunction* AddWorldRotationFunction = GetFunctionByDecl(*this, *Module, TEXT("int AddWorldRotationStep()"));
	if (AddRelativeLocationFunction == nullptr
		|| SetRelativeRotationFunction == nullptr
		|| AddRelativeRotationFunction == nullptr
		|| AddWorldOffsetFunction == nullptr
		|| AddWorldRotationFunction == nullptr)
	{
		return false;
	}

	int32 StepResult = INDEX_NONE;

	if (!ExecuteIntFunction(*this, Engine, *AddRelativeLocationFunction, StepResult))
	{
		return false;
	}
	bPassed &= TestEqual(TEXT("AddRelativeLocation step should execute without lookup failures"), StepResult, 1);
	MirrorFixture.ChildComponent->AddRelativeLocation(AddRelativeLocationDelta);
	bPassed &= VerifyComponentStateMatchesMirror(
		*this,
		TEXT("AddRelativeLocation"),
		*ScriptFixture.ChildComponent,
		*MirrorFixture.ChildComponent);

	if (!ExecuteIntFunction(*this, Engine, *SetRelativeRotationFunction, StepResult))
	{
		return false;
	}
	bPassed &= TestEqual(TEXT("SetRelativeRotation step should execute without lookup failures"), StepResult, 1);
	MirrorFixture.ChildComponent->SetRelativeRotation(SetRelativeRotationTarget);
	bPassed &= VerifyComponentStateMatchesMirror(
		*this,
		TEXT("SetRelativeRotation"),
		*ScriptFixture.ChildComponent,
		*MirrorFixture.ChildComponent);

	if (!ExecuteIntFunction(*this, Engine, *AddRelativeRotationFunction, StepResult))
	{
		return false;
	}
	bPassed &= TestEqual(TEXT("AddRelativeRotation step should execute without lookup failures"), StepResult, 1);
	MirrorFixture.ChildComponent->AddRelativeRotation(AddRelativeRotationDelta);
	bPassed &= VerifyComponentStateMatchesMirror(
		*this,
		TEXT("AddRelativeRotation"),
		*ScriptFixture.ChildComponent,
		*MirrorFixture.ChildComponent);

	if (!ExecuteIntFunction(*this, Engine, *AddWorldOffsetFunction, StepResult))
	{
		return false;
	}
	bPassed &= TestEqual(TEXT("AddWorldOffset step should execute without lookup failures"), StepResult, 1);
	MirrorFixture.ChildComponent->AddWorldOffset(AddWorldOffsetDelta);
	bPassed &= VerifyComponentStateMatchesMirror(
		*this,
		TEXT("AddWorldOffset"),
		*ScriptFixture.ChildComponent,
		*MirrorFixture.ChildComponent);

	if (!ExecuteIntFunction(*this, Engine, *AddWorldRotationFunction, StepResult))
	{
		return false;
	}
	bPassed &= TestEqual(TEXT("AddWorldRotation step should execute without lookup failures"), StepResult, 1);
	MirrorFixture.ChildComponent->AddWorldRotation(AddWorldRotationDelta);
	bPassed &= VerifyComponentStateMatchesMirror(
		*this,
		TEXT("AddWorldRotation"),
		*ScriptFixture.ChildComponent,
		*MirrorFixture.ChildComponent);

	ASTEST_END_FULL
	return bPassed;
}

#endif
