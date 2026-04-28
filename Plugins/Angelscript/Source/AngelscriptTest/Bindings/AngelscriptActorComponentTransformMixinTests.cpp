#include "../Shared/AngelscriptTestMacros.h"

#include "../../AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h"
#include "../../AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h"

#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptTest_Bindings_AngelscriptActorComponentTransformMixinTests_Private
{
	static constexpr double VectorTolerance = 0.05;
	static constexpr double RotationToleranceDegrees = 0.1;

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
		const bool bTranslationMatches = Actual.GetTranslation().Equals(Expected.GetTranslation(), Tolerance);
		const bool bScaleMatches = Actual.GetScale3D().Equals(Expected.GetScale3D(), Tolerance);
		const bool bRotationMatches = QuatMatches(Actual.GetRotation(), Expected.GetRotation());
		return Test.TestTrue(What, bTranslationMatches && bScaleMatches && bRotationMatches);
	}

	USceneComponent* CreateRootSceneComponent(
		FAutomationTestBase& Test,
		AActor& Owner,
		const TCHAR* ComponentName,
		const FVector& WorldLocation)
	{
		USceneComponent* RootComponent = NewObject<USceneComponent>(&Owner, FName(ComponentName));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s fixture should create a root component"), ComponentName),
				RootComponent))
		{
			return nullptr;
		}

		Owner.AddInstanceComponent(RootComponent);
		Owner.SetRootComponent(RootComponent);
		RootComponent->SetMobility(EComponentMobility::Movable);
		RootComponent->RegisterComponent();
		RootComponent->SetWorldLocation(WorldLocation);
		return RootComponent;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptActorComponentTransformMixinTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptActorTransformMixinsTest,
	"Angelscript.TestModule.FunctionLibraries.ActorComponentTransformMixins.Actor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSceneComponentTransformMixinsTest,
	"Angelscript.TestModule.FunctionLibraries.ActorComponentTransformMixins.SceneComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptActorTransformMixinsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& ParentActor = Spawner.SpawnActor<AActor>();
	AActor& ChildActor = Spawner.SpawnActor<AActor>();

	USceneComponent* ParentRoot = CreateRootSceneComponent(*this, ParentActor, TEXT("ActorMixinParentRoot"), FVector::ZeroVector);
	USceneComponent* ChildRoot = CreateRootSceneComponent(*this, ChildActor, TEXT("ActorMixinChildRoot"), FVector(300.0, 0.0, 0.0));
	if (ParentRoot == nullptr || ChildRoot == nullptr)
	{
		return false;
	}

	bPassed &= TestNotNull(
		TEXT("Actor mixin wrapper class should expose AttachToComponent as a reflected UFunction"),
		UAngelscriptActorLibrary::StaticClass()->FindFunctionByName(TEXT("AttachToComponent")));
	bPassed &= TestNotNull(
		TEXT("Actor mixin wrapper class should expose SetActorRelativeRotationQuat as a reflected UFunction"),
		UAngelscriptActorLibrary::StaticClass()->FindFunctionByName(TEXT("SetActorRelativeRotationQuat")));
	bPassed &= TestNotNull(
		TEXT("Actor mixin wrapper class should expose SetActorLocationAndRotationQuat as a reflected UFunction"),
		UAngelscriptActorLibrary::StaticClass()->FindFunctionByName(TEXT("SetActorLocationAndRotationQuat")));
	if (!bPassed)
	{
		return false;
	}

	UAngelscriptActorLibrary::AttachToComponent(&ChildActor, ParentRoot);
	bPassed &= TestEqual(
		TEXT("Actor AttachToComponent wrapper should attach the child root to the requested parent root"),
		ChildRoot->GetAttachParent(),
		ParentRoot);

	const FVector ExpectedRelativeLocation(11.0, 22.0, 33.0);
	UAngelscriptActorLibrary::SetActorRelativeLocation(&ChildActor, ExpectedRelativeLocation);
	bPassed &= TestTrue(
		TEXT("Actor SetActorRelativeLocation wrapper should preserve the requested relative location"),
		UAngelscriptActorLibrary::GetActorRelativeLocation(&ChildActor).Equals(ExpectedRelativeLocation, VectorTolerance));

	const FQuat ExpectedRelativeQuat = FRotator(10.0, 40.0, 0.0).Quaternion();
	UAngelscriptActorLibrary::SetActorRelativeRotationQuat(&ChildActor, ExpectedRelativeQuat);
	bPassed &= TestTrue(
		TEXT("Actor SetActorRelativeRotation(FQuat) wrapper should preserve the requested relative rotation"),
		UAngelscriptActorLibrary::GetActorRelativeRotation(&ChildActor).Equals(ExpectedRelativeQuat.Rotator(), RotationToleranceDegrees));

	const FTransform ExpectedRelativeTransform(
		FRotator(-5.0, 20.0, 35.0).Quaternion(),
		FVector(4.0, 5.0, 6.0),
		FVector(1.5, 2.0, 2.5));
	UAngelscriptActorLibrary::SetActorRelativeTransform(&ChildActor, ExpectedRelativeTransform);
	bPassed &= VerifyTransform(
		*this,
		TEXT("Actor SetActorRelativeTransform wrapper should preserve the requested relative transform"),
		UAngelscriptActorLibrary::GetActorRelativeTransform(&ChildActor),
		ExpectedRelativeTransform);

	const FVector ExpectedWorldLocation(100.0, -50.0, 25.0);
	const FQuat ExpectedWorldQuat = FRotator(0.0, 90.0, 15.0).Quaternion();
	UAngelscriptActorLibrary::SetActorLocationAndRotationQuat(&ChildActor, ExpectedWorldLocation, ExpectedWorldQuat, false);
	bPassed &= TestTrue(
		TEXT("Actor SetActorLocationAndRotation(FQuat) wrapper should preserve the requested world location"),
		ChildActor.GetActorLocation().Equals(ExpectedWorldLocation, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("Actor GetActorQuat wrapper should preserve the requested world rotation"),
		QuatMatches(UAngelscriptActorLibrary::GetActorQuat(&ChildActor), ExpectedWorldQuat));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptSceneComponentTransformMixinsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& ParentActor = Spawner.SpawnActor<AActor>();
	AActor& ChildActor = Spawner.SpawnActor<AActor>();

	USceneComponent* ParentRoot = CreateRootSceneComponent(*this, ParentActor, TEXT("SceneMixinParentRoot"), FVector::ZeroVector);
	USceneComponent* ChildRoot = CreateRootSceneComponent(*this, ChildActor, TEXT("SceneMixinChildRoot"), FVector(200.0, 50.0, 0.0));
	if (ParentRoot == nullptr || ChildRoot == nullptr)
	{
		return false;
	}

	bPassed &= TestNotNull(
		TEXT("SceneComponent mixin wrapper class should expose AttachToComponent as a reflected UFunction"),
		UAngelscriptComponentLibrary::StaticClass()->FindFunctionByName(TEXT("AttachToComponent")));
	bPassed &= TestNotNull(
		TEXT("SceneComponent mixin wrapper class should expose IsAttachedTo_Actor as a reflected UFunction"),
		UAngelscriptComponentLibrary::StaticClass()->FindFunctionByName(TEXT("IsAttachedTo_Actor")));
	bPassed &= TestNotNull(
		TEXT("SceneComponent mixin wrapper class should expose SetWorldLocationAndRotationQuat as a reflected UFunction"),
		UAngelscriptComponentLibrary::StaticClass()->FindFunctionByName(TEXT("SetWorldLocationAndRotationQuat")));
	if (!bPassed)
	{
		return false;
	}

	UAngelscriptComponentLibrary::AttachToComponent(ChildRoot, ParentRoot);
	bPassed &= TestEqual(
		TEXT("SceneComponent AttachToComponent wrapper should attach the child root to the requested parent root"),
		ChildRoot->GetAttachParent(),
		ParentRoot);
	bPassed &= TestTrue(
		TEXT("SceneComponent IsAttachedTo(USceneComponent) wrapper should recognize the requested parent component"),
		UAngelscriptComponentLibrary::IsAttachedTo(ChildRoot, ParentRoot));
	bPassed &= TestTrue(
		TEXT("SceneComponent IsAttachedTo(AActor) wrapper should recognize the requested parent actor"),
		UAngelscriptComponentLibrary::IsAttachedTo_Actor(ChildRoot, &ParentActor));
	bPassed &= TestEqual(
		TEXT("SceneComponent GetNumChildrenComponents wrapper should report the attached child"),
		UAngelscriptComponentLibrary::GetNumChildrenComponents(ParentRoot),
		1);

	const FVector ExpectedRelativeLocation(7.0, 8.0, 9.0);
	const FQuat ExpectedRelativeQuat = FRotator(0.0, 30.0, 10.0).Quaternion();
	UAngelscriptComponentLibrary::SetRelativeLocationAndRotationQuat(ChildRoot, ExpectedRelativeLocation, ExpectedRelativeQuat);
	bPassed &= TestTrue(
		TEXT("SceneComponent SetRelativeLocationAndRotation(FQuat) wrapper should preserve the requested relative location"),
		UAngelscriptComponentLibrary::GetRelativeLocation(ChildRoot).Equals(ExpectedRelativeLocation, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("SceneComponent SetRelativeLocationAndRotation(FQuat) wrapper should preserve the requested relative rotation"),
		UAngelscriptComponentLibrary::GetRelativeRotation(ChildRoot).Equals(ExpectedRelativeQuat.Rotator(), RotationToleranceDegrees));

	const FVector ExpectedWorldLocation(-40.0, 20.0, 80.0);
	const FQuat ExpectedWorldQuat = FRotator(-15.0, 110.0, 5.0).Quaternion();
	UAngelscriptComponentLibrary::SetWorldLocationAndRotationQuat(ChildRoot, ExpectedWorldLocation, ExpectedWorldQuat);
	bPassed &= TestTrue(
		TEXT("SceneComponent SetWorldLocationAndRotation(FQuat) wrapper should preserve the requested world location"),
		ChildRoot->GetComponentLocation().Equals(ExpectedWorldLocation, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("SceneComponent GetComponentQuat wrapper should preserve the requested world rotation"),
		QuatMatches(UAngelscriptComponentLibrary::GetComponentQuat(ChildRoot), ExpectedWorldQuat));

	const FBoxSphereBounds Bounds = UAngelscriptComponentLibrary::GetBounds(ChildRoot);
	bPassed &= TestTrue(
		TEXT("SceneComponent GetBounds wrapper should preserve the component bounds origin"),
		Bounds.Origin.Equals(ChildRoot->Bounds.Origin, VectorTolerance));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
