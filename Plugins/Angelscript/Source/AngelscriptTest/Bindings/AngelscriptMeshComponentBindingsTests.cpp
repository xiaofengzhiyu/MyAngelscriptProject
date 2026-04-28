#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMeshComponentBindingsTests_Private
{
	static constexpr ANSICHAR MeshComponentModuleName[] = "ASMeshComponentCompat";

	struct FMeshComponentFixture
	{
		AActor* HostActor = nullptr;
		USkeletalMeshComponent* SkeletalComponent = nullptr;
		UPoseableMeshComponent* PoseableComponent = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			const FName HostActorName = MakeUniqueObjectName(
				GetTransientPackage(),
				AActor::StaticClass(),
				TEXT("AngelscriptMeshComponentHost"));

			HostActor = NewObject<AActor>(
				GetTransientPackage(),
				AActor::StaticClass(),
				HostActorName,
				RF_Transient);
			if (!Test.TestNotNull(TEXT("Mesh component bindings should create a transient host actor"), HostActor))
			{
				return false;
			}
			HostActor->AddToRoot();

			SkeletalComponent = NewObject<USkeletalMeshComponent>(
				HostActor,
				USkeletalMeshComponent::StaticClass(),
				TEXT("AngelscriptSkeletalMeshComponent"),
				RF_Transient);
			PoseableComponent = NewObject<UPoseableMeshComponent>(
				HostActor,
				UPoseableMeshComponent::StaticClass(),
				TEXT("AngelscriptPoseableMeshComponent"),
				RF_Transient);

			if (!Test.TestNotNull(TEXT("Mesh component bindings should create a transient skeletal mesh component"), SkeletalComponent)
				|| !Test.TestNotNull(TEXT("Mesh component bindings should create a transient poseable mesh component"), PoseableComponent))
			{
				return false;
			}

			HostActor->AddInstanceComponent(SkeletalComponent);
			HostActor->AddInstanceComponent(PoseableComponent);
			return true;
		}

		void Cleanup()
		{
			if (HostActor != nullptr && HostActor->IsRooted())
			{
				HostActor->RemoveFromRoot();
			}

			if (PoseableComponent != nullptr)
			{
				PoseableComponent->MarkAsGarbage();
				PoseableComponent = nullptr;
			}

			if (SkeletalComponent != nullptr)
			{
				SkeletalComponent->MarkAsGarbage();
				SkeletalComponent = nullptr;
			}

			if (HostActor != nullptr)
			{
				HostActor->MarkAsGarbage();
				HostActor = nullptr;
			}
		}
	};

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement.ReplaceCharWithEscapedChar(), ESearchCase::CaseSensitive);
	}

	FString BuildMeshComponentScript(
		const FString& SkeletalComponentPath,
		const FString& PoseableComponentPath,
		const int32 ExpectedLinkedAnimInstanceCount)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	UObject SkeletalObject = FindObject("__SKELETAL_COMPONENT_PATH__");
	USkeletalMeshComponent SkeletalComponent = Cast<USkeletalMeshComponent>(SkeletalObject);
	if (SkeletalComponent == null)
		return 10;

	SkeletalComponent.UpdateLODStatus();
	SkeletalComponent.InvalidateCachedBounds();
	if (SkeletalComponent.GetLinkedAnimInstances().Num() != __EXPECTED_LINKED_ANIM_INSTANCE_COUNT__)
		return 20;

	UObject PoseableObject = FindObject("__POSEABLE_COMPONENT_PATH__");
	UPoseableMeshComponent PoseableComponent = Cast<UPoseableMeshComponent>(PoseableObject);
	if (PoseableComponent == null)
		return 30;

	PoseableComponent.AllocateTransformData();
	PoseableComponent.RefreshBoneTransforms();
	PoseableComponent.UpdateLODStatus();
	PoseableComponent.InvalidateCachedBounds();

	return 1;
}
)AS");

		ReplaceToken(Script, TEXT("__SKELETAL_COMPONENT_PATH__"), SkeletalComponentPath);
		ReplaceToken(Script, TEXT("__POSEABLE_COMPONENT_PATH__"), PoseableComponentPath);
		Script.ReplaceInline(TEXT("__EXPECTED_LINKED_ANIM_INSTANCE_COUNT__"), *LexToString(ExpectedLinkedAnimInstanceCount), ESearchCase::CaseSensitive);
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMeshComponentBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMeshComponentCompatBindingsTest,
	"Angelscript.TestModule.Bindings.MeshComponentCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMeshComponentCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FMeshComponentFixture Fixture;
	bool bModuleCompiled = false;

	ON_SCOPE_EXIT
	{
		if (bModuleCompiled)
		{
			Engine.DiscardModule(ANSI_TO_TCHAR(MeshComponentModuleName));
		}
		Fixture.Cleanup();
	};

	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	const USkeletalMeshComponent* ConstSkeletalComponent = Fixture.SkeletalComponent;
	const int32 ExpectedLinkedAnimInstanceCount = ConstSkeletalComponent->GetLinkedAnimInstances().Num();
	if (!TestEqual(TEXT("Mesh component fixture should start without linked anim instances"), ExpectedLinkedAnimInstanceCount, 0))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		MeshComponentModuleName,
		BuildMeshComponentScript(
			Fixture.SkeletalComponent->GetPathName(),
			Fixture.PoseableComponent->GetPathName(),
			ExpectedLinkedAnimInstanceCount));
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

	bPassed &= TestEqual(
		TEXT("Mesh component bindings should execute skinned, skeletal, and poseable helper methods through script"),
		Result,
		1);
	bPassed &= TestEqual(
		TEXT("GetLinkedAnimInstances should remain aligned with the native empty linked-instance baseline"),
		ConstSkeletalComponent->GetLinkedAnimInstances().Num(),
		ExpectedLinkedAnimInstanceCount);
	bPassed &= TestNull(
		TEXT("Poseable mesh fixture should remain on the safe no-asset path"),
		Fixture.PoseableComponent->GetSkinnedAsset());

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
