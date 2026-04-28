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

namespace AngelscriptTest_Bindings_AngelscriptComponentFunctionLibraryTests_Private
{
	static constexpr ANSICHAR ComponentQuatAndCompositeTransformModuleName[] = "ASComponentQuatAndCompositeTransform";
	static constexpr double VectorTolerance = 0.05;
	static constexpr double RotationToleranceDegrees = 0.1;

	static const FTransform ParentWorldTransform(
		FRotator(15.0, -35.0, 20.0),
		FVector(250.0, -100.0, 80.0),
		FVector::OneVector);
	static const FTransform InitialRelativeTransform(
		FRotator(-10.0, 25.0, 5.0),
		FVector(30.0, -15.0, 12.0),
		FVector(0.75, 1.5, 1.25));
	static const FQuat RelativeRotationQuat(FVector::UpVector, FMath::DegreesToRadians(30.0));
	static const FTransform RelativeTransformSnapshot(
		FRotator(20.0, 35.0, -10.0),
		FVector(15.0, -5.0, 8.0),
		FVector(1.25, 0.75, 1.5));
	static const FVector RelativeLocationRotatorTarget(10.0, -15.0, 20.0);
	static const FRotator RelativeRotationRotatorTarget(5.0, 45.0, 0.0);
	static const FVector RelativeLocationQuatTarget(4.0, 8.0, -12.0);
	static const FQuat RelativeLocationQuatRotationTarget(FVector::RightVector, FMath::DegreesToRadians(20.0));
	static const FQuat ComponentQuatTarget(FVector::UpVector, FMath::DegreesToRadians(60.0));
	static const FQuat AddRelativeRotationDelta(FVector::ForwardVector, FMath::DegreesToRadians(10.0));
	static const FVector AddLocalOffsetDelta(3.0, -2.0, 5.0);
	static const FRotator AddLocalRotationDelta(0.0, 15.0, 5.0);
	static const FQuat AddLocalRotationQuatDelta(FVector::UpVector, FMath::DegreesToRadians(-15.0));
	static const FTransform AddLocalTransformDelta(
		FRotator(-10.0, 5.0, 20.0),
		FVector(1.0, 2.0, -3.0),
		FVector(1.1, 0.9, 1.0));
	static const FQuat WorldRotationQuatTarget(FVector::UpVector, FMath::DegreesToRadians(120.0));
	static const FTransform WorldTransformSnapshot(
		FRotator(0.0, 135.0, -15.0),
		FVector(120.0, -30.0, 60.0),
		FVector(0.8, 1.2, 1.4));
	static const FVector WorldLocationRotatorTarget(-75.0, 40.0, 55.0);
	static const FRotator WorldRotationRotatorTarget(25.0, -70.0, 10.0);
	static const FVector WorldLocationQuatTarget(60.0, -90.0, 110.0);
	static const FQuat WorldLocationQuatRotationTarget(FVector::UpVector, FMath::DegreesToRadians(-70.0));
	static const FQuat AddWorldRotationQuatDelta(FVector::RightVector, FMath::DegreesToRadians(15.0));
	static const FTransform AddWorldTransformDelta(
		FRotator(0.0, 20.0, 0.0),
		FVector(5.0, -10.0, 15.0),
		FVector(1.05, 0.95, 1.1));

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

	bool VerifyQuat(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FQuat& Actual,
		const FQuat& Expected,
		double ToleranceDegrees = RotationToleranceDegrees)
	{
		return Test.TestTrue(What, QuatMatches(Actual, Expected, ToleranceDegrees));
	}

	bool VerifyVector(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FVector& Actual,
		const FVector& Expected,
		double Tolerance = VectorTolerance)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
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

	FString EscapeScriptString(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	template <typename TValue>
	bool ExecuteValueFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		TValue& OutValue,
		const TCHAR* ContextLabel)
	{
		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should create an execution context"), ContextLabel),
				Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
				PrepareResult,
				static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
				ExecuteResult,
				static_cast<int32>(asEXECUTION_FINISHED)))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("%s saw a script exception: %s"),
					ContextLabel,
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			return false;
		}

		void* ReturnValueAddress = Context->GetAddressOfReturnValue();
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should expose return value storage"), ContextLabel),
				ReturnValueAddress))
		{
			return false;
		}

		OutValue = *static_cast<TValue*>(ReturnValueAddress);
		return true;
	}

	FSceneComponentTransformFixture CreateFixture(
		FAutomationTestBase& Test,
		FActorTestSpawner& Spawner,
		const TCHAR* RootLabel,
		const TCHAR* ChildLabel)
	{
		FSceneComponentTransformFixture Fixture;
		Fixture.HostActor = &Spawner.SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("Component quat/composite transform fixture should spawn a host actor"), Fixture.HostActor))
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
#if WITH_EDITORONLY_DATA
		Fixture.ChildComponent->bVisualizeComponent = false;
#endif

		Fixture.ChildComponentPath = Fixture.ChildComponent->GetPathName();
		return Fixture;
	}

	void ApplyNativeScenario(USceneComponent& Component)
	{
		Component.SetRelativeRotation(RelativeRotationQuat);
		Component.SetRelativeTransform(RelativeTransformSnapshot);
		Component.SetRelativeLocationAndRotation(RelativeLocationRotatorTarget, RelativeRotationRotatorTarget);
		Component.SetRelativeLocationAndRotation(RelativeLocationQuatTarget, RelativeLocationQuatRotationTarget);
		Component.SetWorldRotation(ComponentQuatTarget);
		Component.AddRelativeRotation(AddRelativeRotationDelta);
		Component.AddLocalOffset(AddLocalOffsetDelta);
		Component.AddLocalRotation(AddLocalRotationDelta);
		Component.AddLocalRotation(AddLocalRotationQuatDelta);
		Component.AddLocalTransform(AddLocalTransformDelta);
		Component.SetWorldRotation(WorldRotationQuatTarget);
		Component.SetWorldTransform(WorldTransformSnapshot);
		Component.SetWorldLocationAndRotation(WorldLocationRotatorTarget, WorldRotationRotatorTarget);
		Component.SetWorldLocationAndRotation(WorldLocationQuatTarget, WorldLocationQuatRotationTarget);
		Component.AddWorldRotation(AddWorldRotationQuatDelta);
		Component.AddWorldTransform(AddWorldTransformDelta);
#if WITH_EDITORONLY_DATA
		Component.bVisualizeComponent = true;
		Component.bVisualizeComponent = false;
#endif
	}

	FString BuildScenarioScript(const FString& ComponentPath)
	{
		FString Script = TEXT(R"AS(
USceneComponent ResolveComponent()
{
	return Cast<USceneComponent>(FindObject("__COMPONENT_PATH__"));
}

int ExecuteScenario()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return 2;

	Component.SetRelativeRotation(FQuat(FVector(0.0, 0.0, 1.0), 0.52359877559829882));
	Component.SetRelativeTransform(FTransform(FRotator(20.0, 35.0, -10.0), FVector(15.0, -5.0, 8.0), FVector(1.25, 0.75, 1.5)));
	Component.SetRelativeLocationAndRotation(FVector(10.0, -15.0, 20.0), FRotator(5.0, 45.0, 0.0));
	Component.SetRelativeLocationAndRotation(FVector(4.0, 8.0, -12.0), FQuat(FVector(0.0, 1.0, 0.0), 0.3490658503988659));
	Component.SetComponentQuat(FQuat(FVector(0.0, 0.0, 1.0), 1.0471975511965976));
	Component.AddRelativeRotation(FQuat(FVector(1.0, 0.0, 0.0), 0.17453292519943295));
	Component.AddLocalOffset(FVector(3.0, -2.0, 5.0));
	Component.AddLocalRotation(FRotator(0.0, 15.0, 5.0));
	Component.AddLocalRotation(FQuat(FVector(0.0, 0.0, 1.0), -0.26179938779914941));
	Component.AddLocalTransform(FTransform(FRotator(-10.0, 5.0, 20.0), FVector(1.0, 2.0, -3.0), FVector(1.1, 0.9, 1.0)));
	Component.SetWorldRotation(FQuat(FVector(0.0, 0.0, 1.0), 2.0943951023931953));
	Component.SetWorldTransform(FTransform(FRotator(0.0, 135.0, -15.0), FVector(120.0, -30.0, 60.0), FVector(0.8, 1.2, 1.4)));
	Component.SetWorldLocationAndRotation(FVector(-75.0, 40.0, 55.0), FRotator(25.0, -70.0, 10.0));
	Component.SetWorldLocationAndRotation(FVector(60.0, -90.0, 110.0), FQuat(FVector(0.0, 0.0, 1.0), -1.2217304763960306));
	Component.AddWorldRotation(FQuat(FVector(0.0, 1.0, 0.0), 0.26179938779914941));
	Component.AddWorldTransform(FTransform(FRotator(0.0, 20.0, 0.0), FVector(5.0, -10.0, 15.0), FVector(1.05, 0.95, 1.1)));
	Component.SetbVisualizeComponent(true);
	Component.SetbVisualizeComponent(false);
	return 1;
}

FQuat GetComponentQuatSnapshot()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return FQuat::Identity;
	return Component.GetComponentQuat();
}

FQuat GetSocketQuatSnapshot()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return FQuat::Identity;
	return Component.GetSocketQuaternion(FName(""));
}

FVector GetRelativeScaleSnapshot()
{
	USceneComponent Component = ResolveComponent();
	if (Component == null)
		return FVector::ZeroVector;
	return Component.GetRelativeScale3D();
}
)AS");

		ReplaceToken(Script, TEXT("__COMPONENT_PATH__"), EscapeScriptString(ComponentPath));
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptComponentFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptComponentQuatAndCompositeTransformFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.ComponentQuatAndCompositeTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptComponentQuatAndCompositeTransformFunctionLibraryTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	FSceneComponentTransformFixture ScriptFixture = CreateFixture(
		*this,
		Spawner,
		TEXT("ComponentQuatScriptRoot"),
		TEXT("ComponentQuatScriptChild"));
	FSceneComponentTransformFixture MirrorFixture = CreateFixture(
		*this,
		Spawner,
		TEXT("ComponentQuatMirrorRoot"),
		TEXT("ComponentQuatMirrorChild"));
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
		TEXT("SetRelativeRotationQuat"),
		TEXT("SetRelativeLocationAndRotationQuat"),
		TEXT("SetComponentQuat"),
		TEXT("GetComponentQuat"),
		TEXT("GetSocketQuaternion"),
		TEXT("SetWorldRotationQuat"),
		TEXT("SetWorldLocationAndRotationQuat"),
		TEXT("AddRelativeRotationQuat"),
		TEXT("AddLocalRotationQuat"),
		TEXT("AddWorldRotationQuat"),
		TEXT("SetbVisualizeComponent")
	};
	for (const TCHAR* FunctionName : RequiredFunctions)
	{
		bPassed &= TestNotNull(
			*FString::Printf(TEXT("Component function library test should expose %s"), FunctionName),
			UAngelscriptComponentLibrary::StaticClass()->FindFunctionByName(FunctionName));
	}
	if (!bPassed)
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ComponentQuatAndCompositeTransformModuleName,
		BuildScenarioScript(ScriptFixture.ChildComponentPath));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* ExecuteScenarioFunction = GetFunctionByDecl(*this, *Module, TEXT("int ExecuteScenario()"));
	asIScriptFunction* GetComponentQuatFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat GetComponentQuatSnapshot()"));
	asIScriptFunction* GetSocketQuatFunction = GetFunctionByDecl(*this, *Module, TEXT("FQuat GetSocketQuatSnapshot()"));
	asIScriptFunction* GetRelativeScaleFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetRelativeScaleSnapshot()"));
	if (ExecuteScenarioFunction == nullptr
		|| GetComponentQuatFunction == nullptr
		|| GetSocketQuatFunction == nullptr
		|| GetRelativeScaleFunction == nullptr)
	{
		return false;
	}

	int32 ScriptResult = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *ExecuteScenarioFunction, ScriptResult))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("SceneComponent quat/composite transform scenario should complete without lookup or wrapper failures"),
		ScriptResult,
		1);

	ApplyNativeScenario(*MirrorFixture.ChildComponent);

	FQuat ScriptComponentQuat = FQuat::Identity;
	FQuat ScriptSocketQuat = FQuat::Identity;
	FVector ScriptRelativeScale = FVector::ZeroVector;
	if (!ExecuteValueFunction(
			*this,
			Engine,
			*GetComponentQuatFunction,
			ScriptComponentQuat,
			TEXT("GetComponentQuatSnapshot"))
		|| !ExecuteValueFunction(
			*this,
			Engine,
			*GetSocketQuatFunction,
			ScriptSocketQuat,
			TEXT("GetSocketQuatSnapshot"))
		|| !ExecuteValueFunction(
			*this,
			Engine,
			*GetRelativeScaleFunction,
			ScriptRelativeScale,
			TEXT("GetRelativeScaleSnapshot")))
	{
		return false;
	}

	bPassed &= VerifyTransform(
		*this,
		TEXT("SceneComponent quat/composite transform wrappers should preserve the full relative transform against the native mirror"),
		ScriptFixture.ChildComponent->GetRelativeTransform(),
		MirrorFixture.ChildComponent->GetRelativeTransform());
	bPassed &= VerifyTransform(
		*this,
		TEXT("SceneComponent quat/composite transform wrappers should preserve the full world transform against the native mirror"),
		ScriptFixture.ChildComponent->GetComponentTransform(),
		MirrorFixture.ChildComponent->GetComponentTransform());
	bPassed &= VerifyQuat(
		*this,
		TEXT("SceneComponent GetComponentQuat script helper should report the same quaternion as the native mirror"),
		ScriptComponentQuat,
		MirrorFixture.ChildComponent->GetComponentQuat());
	bPassed &= VerifyQuat(
		*this,
		TEXT("SceneComponent GetSocketQuaternion(NAME_None) script helper should report the same quaternion as the native mirror"),
		ScriptSocketQuat,
		MirrorFixture.ChildComponent->GetSocketQuaternion(NAME_None));
	bPassed &= VerifyQuat(
		*this,
		TEXT("SceneComponent GetSocketQuaternion(NAME_None) should match the script component world quaternion after composite operations"),
		ScriptSocketQuat,
		ScriptFixture.ChildComponent->GetComponentQuat());
	bPassed &= VerifyVector(
		*this,
		TEXT("SceneComponent GetRelativeScale3D script helper should report the same scale as the native mirror"),
		ScriptRelativeScale,
		MirrorFixture.ChildComponent->GetRelativeScale3D());
#if WITH_EDITORONLY_DATA
	bPassed &= TestFalse(
		TEXT("SceneComponent SetbVisualizeComponent script helper should round-trip back to false"),
		ScriptFixture.ChildComponent->bVisualizeComponent);
	bPassed &= TestEqual(
		TEXT("SceneComponent SetbVisualizeComponent script helper should keep the editor visualize flag in sync with the native mirror"),
		ScriptFixture.ChildComponent->bVisualizeComponent,
		MirrorFixture.ChildComponent->bVisualizeComponent);
#endif

	ASTEST_END_FULL
	return bPassed;
}

#endif
