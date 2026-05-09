#include "CQTest.h"
#include "Shared/AngelscriptTestMacros.h"
#include "Functional/Actor/AngelscriptActorTestHelpers.h"

#include "Components/ActorComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptFunctionalTestUtils;
using namespace AngelscriptReflectiveAccess;
using namespace AngelscriptActorTestUtils;

namespace AngelscriptTest_Functional_Actor_Component_Private
{
	int32 CountComponentsByClass(const AActor* Actor, const UClass* ComponentClass)
	{
		if (Actor == nullptr || ComponentClass == nullptr)
		{
			return 0;
		}

		int32 Count = 0;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component != nullptr && Component->IsA(ComponentClass))
			{
				++Count;
			}
		}
		return Count;
	}

	template <typename ComponentType>
	ComponentType* FindComponentByName(const AActor* Actor, const FName ComponentName)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component != nullptr && Component->GetFName() == ComponentName)
			{
				return Cast<ComponentType>(Component);
			}
		}
		return nullptr;
	}

	bool AreAllComponentsRegistered(const AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return false;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component == nullptr || !Component->IsRegistered())
			{
				return false;
			}
		}
		return true;
	}

	FString DescribeComponents(const TArray<UActorComponent*>& Components)
	{
		TArray<FString> Entries;
		Entries.Reserve(Components.Num());
		for (const UActorComponent* Component : Components)
		{
			if (Component == nullptr)
			{
				Entries.Add(TEXT("<null>"));
				continue;
			}

			Entries.Add(FString::Printf(TEXT("%s:%s"), *Component->GetName(), *Component->GetClass()->GetName()));
		}
		return FString::Join(Entries, TEXT(", "));
	}

	bool InvokeComponentArrayOut(
		FAutomationTestBase& Test,
		UObject* Target,
		FName FunctionName,
		const TArray<UActorComponent*>& SeedComponents,
		TArray<UActorComponent*>& OutComponents)
	{
		FFunctionInvoker Invoker(Test, Target, FunctionName);
		if (!Invoker.IsValid())
		{
			return false;
		}

		Invoker.AddParam<TArray<UActorComponent*>>(SeedComponents);
		return Invoker.Call() && Invoker.ReadParamAfterCall<TArray<UActorComponent*>>(0, OutComponents);
	}

	bool ReadComponentArrayProperty(
		FAutomationTestBase& Test,
		UObject* Object,
		FName PropertyName,
		TArray<UActorComponent*>& OutComponents)
	{
		if (!Test.TestNotNull(TEXT("Component array property read requires a valid object"), Object))
		{
			return false;
		}

		FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(Object->GetClass(), PropertyName);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should be a reflected TArray property"), *PropertyName.ToString()), ArrayProperty))
		{
			return false;
		}

		FObjectPropertyBase* InnerObjectProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should contain UObject references"), *PropertyName.ToString()), InnerObjectProperty))
		{
			return false;
		}

		void* ArrayAddress = ArrayProperty->ContainerPtrToValuePtr<void>(Object);
		FScriptArrayHelper Helper(ArrayProperty, ArrayAddress);
		OutComponents.Reset(Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			UObject* ElementObject = InnerObjectProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index));
			OutComponents.Add(Cast<UActorComponent>(ElementObject));
		}
		return true;
	}
}

TEST_CLASS_WITH_FLAGS(FAngelscriptActorComponentTest,
	"Angelscript.TestModule.Actor.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
	BEFORE_ALL()
	{
		ASTEST_CREATE_ENGINE();
	}

	AFTER_ALL()
	{
		FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();
		ASTEST_RESET_ENGINE(Engine);
	}

	TEST_METHOD(CreateComponent)
	{
		using namespace AngelscriptTest_Functional_Actor_Component_Private;

		FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();
		FAngelscriptEngineScope Scope(Engine);
		static const FName ModuleName(TEXT("TestActorCreateComponent"));
		ON_SCOPE_EXIT { Engine.DiscardModule(*ModuleName.ToString()); };

		UClass* ScriptClass = CompileScriptModule(*TestRunner, Engine, ModuleName,
			TEXT("TestActorCreateComponent.as"),
			TEXT(R"AS(
UCLASS()
class ATestActorCreateComponent : AActor
{
	UFUNCTION()
	int RunCreateComponentTest()
	{
		USceneComponent FirstScene = Cast<USceneComponent>(CreateComponent(USceneComponent::StaticClass(), n"DynamicRoot"));
		if (FirstScene == nullptr)
			return 10;
		if (FirstScene.GetOwner() != this)
			return 11;
		if (FirstScene.GetName() != n"DynamicRoot")
			return 12;

		USceneComponent SecondScene = Cast<USceneComponent>(CreateComponent(USceneComponent::StaticClass(), n"DynamicChild"));
		if (SecondScene == nullptr)
			return 20;
		if (SecondScene.GetOwner() != this)
			return 21;
		if (SecondScene.GetName() != n"DynamicChild")
			return 22;

		UBillboardComponent Billboard = Cast<UBillboardComponent>(CreateComponent(UBillboardComponent::StaticClass(), n"DynamicBillboard"));
		if (Billboard == nullptr)
			return 30;
		if (Billboard.GetOwner() != this)
			return 31;

		TArray<USceneComponent> AllScenes;
		GetComponentsByClass(AllScenes);
		if (AllScenes.Num() != 3)
			return 40;

		if (GetComponent(USceneComponent::StaticClass(), n"DynamicRoot") != FirstScene)
			return 50;
		if (GetComponent(UBillboardComponent::StaticClass(), n"DynamicBillboard") != Billboard)
			return 51;
		if (GetComponent(UStaticMeshComponent::StaticClass(), n"DynamicBillboard") != nullptr)
			return 52;

		return 1;
	}

	UFUNCTION()
	USceneComponent CreateNamedSceneForCpp()
	{
		return Cast<USceneComponent>(CreateComponent(USceneComponent::StaticClass(), n"CppReturnedNamedScene"));
	}
}
)AS"),
			TEXT("ATestActorCreateComponent"));
		if (ScriptClass == nullptr) return;

		FAngelscriptTestWorld W(*TestRunner, Engine);
		if (!W.IsValid()) return;
		AActor* Actor = W.SpawnActorOfClass(ScriptClass);
		if (!TestRunner->TestNotNull(TEXT("Actor should spawn"), Actor)) return;
		W.BeginPlay(*Actor);

		FFunctionInvoker Invoker(*TestRunner, Actor, FName(TEXT("RunCreateComponentTest")));
		if (!Invoker.IsValid()) return;
		TestRunner->TestEqual(TEXT("CreateComponent should create and register components on the actor"),
			Invoker.CallAndReturn<int32>(INDEX_NONE), 1);

		USceneComponent* DynamicRoot = FindComponentByName<USceneComponent>(Actor, TEXT("DynamicRoot"));
		USceneComponent* DynamicChild = FindComponentByName<USceneComponent>(Actor, TEXT("DynamicChild"));
		UBillboardComponent* DynamicBillboard = FindComponentByName<UBillboardComponent>(Actor, TEXT("DynamicBillboard"));
		if (!TestRunner->TestNotNull(TEXT("CreateComponent should keep the first dynamic scene component"), DynamicRoot)
			|| !TestRunner->TestNotNull(TEXT("CreateComponent should keep the second dynamic scene component"), DynamicChild)
			|| !TestRunner->TestNotNull(TEXT("CreateComponent should keep the dynamic billboard component"), DynamicBillboard))
		{
			return;
		}

		TestRunner->TestEqual(TEXT("CreateComponent should promote the first scene component to root"), Actor->GetRootComponent(), DynamicRoot);
		TestRunner->TestEqual(TEXT("CreateComponent should attach later scene components to the root"), DynamicChild->GetAttachParent(), DynamicRoot);
		TestRunner->TestEqual(TEXT("CreateComponent should attach later scene-derived components to the root"), DynamicBillboard->GetAttachParent(), DynamicRoot);
		TestRunner->TestTrue(TEXT("CreateComponent should register every created component"), AreAllComponentsRegistered(Actor));
		TestRunner->TestEqual(TEXT("CreateComponent should leave exactly three scene components on this actor"), CountComponentsByClass(Actor, USceneComponent::StaticClass()), 3);

		FFunctionInvoker ReturnComponentInvoker(*TestRunner, Actor, FName(TEXT("CreateNamedSceneForCpp")));
		if (!ReturnComponentInvoker.IsValid()) return;
		USceneComponent* ReturnedNamedScene = ReturnComponentInvoker.CallAndReturn<USceneComponent*>(nullptr);
		if (!TestRunner->TestNotNull(TEXT("CreateComponent should return the newly created named component to C++"), ReturnedNamedScene)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("CreateComponent returned named component to C++: %s:%s"), *ReturnedNamedScene->GetName(), *ReturnedNamedScene->GetClass()->GetName()));
		TestRunner->TestEqual(TEXT("Returned named component should preserve the requested object name"), ReturnedNamedScene->GetFName(), FName(TEXT("CppReturnedNamedScene")));
		TestRunner->TestEqual(TEXT("Returned named component should be owned by the script actor"), ReturnedNamedScene->GetOwner(), Actor);
		TestRunner->TestTrue(TEXT("Returned named component should be registered"), ReturnedNamedScene->IsRegistered());
		TestRunner->TestEqual(TEXT("Returned named component should attach to the existing dynamic root"), ReturnedNamedScene->GetAttachParent(), DynamicRoot);
		TestRunner->TestEqual(TEXT("CreateComponent should expose the returned named component in the actor component list"), FindComponentByName<USceneComponent>(Actor, TEXT("CppReturnedNamedScene")), ReturnedNamedScene);
	}

	TEST_METHOD(GetComponent)
	{
		using namespace AngelscriptTest_Functional_Actor_Component_Private;

		FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();
		FAngelscriptEngineScope Scope(Engine);
		static const FName ModuleName(TEXT("TestActorGetComponent"));
		ON_SCOPE_EXIT { Engine.DiscardModule(*ModuleName.ToString()); };

		UClass* ScriptClass = CompileScriptModule(*TestRunner, Engine, ModuleName,
			TEXT("TestActorGetComponent.as"),
			TEXT(R"AS(
UCLASS()
class UTestActorGetComponentMissing : UActorComponent
{
}

UCLASS()
class ATestActorGetComponent : AActor
{
	UPROPERTY(DefaultComponent, RootComponent)
	USceneComponent RootScene;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UStaticMeshComponent Mesh;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UBillboardComponent Billboard;

	UFUNCTION()
	int RunGetComponentTest()
	{
		UActorComponent Found = GetComponent(USceneComponent::StaticClass());
		if (Found == nullptr)
			return 10;
		if (!Found.IsA(USceneComponent::StaticClass()))
			return 11;

		UActorComponent FoundMesh = GetComponent(UStaticMeshComponent::StaticClass());
		if (FoundMesh == nullptr)
			return 20;
		if (FoundMesh.GetName() != n"Mesh")
			return 21;

		UActorComponent ByName = GetComponent(USceneComponent::StaticClass(), n"RootScene");
		if (ByName == nullptr)
			return 30;
		if (ByName != RootScene)
			return 31;

		UActorComponent MeshAsScene = GetComponent(USceneComponent::StaticClass(), n"Mesh");
		if (MeshAsScene != Mesh)
			return 40;

		UActorComponent MeshWrongName = GetComponent(UStaticMeshComponent::StaticClass(), n"Billboard");
		if (MeshWrongName != nullptr)
			return 50;

		UActorComponent MissingByName = GetComponent(USceneComponent::StaticClass(), n"MissingScene");
		if (MissingByName != nullptr)
			return 60;

		UActorComponent MissingByClass = GetComponent(UTestActorGetComponentMissing::StaticClass());
		if (MissingByClass != nullptr)
			return 70;

		return 1;
	}
}
)AS"),
			TEXT("ATestActorGetComponent"));
		if (ScriptClass == nullptr) return;

		FAngelscriptTestWorld W(*TestRunner, Engine);
		if (!W.IsValid()) return;
		AActor* Actor = W.SpawnActorOfClass(ScriptClass);
		if (!TestRunner->TestNotNull(TEXT("Actor should spawn"), Actor)) return;
		W.BeginPlay(*Actor);

		FFunctionInvoker Invoker(*TestRunner, Actor, FName(TEXT("RunGetComponentTest")));
		if (!Invoker.IsValid()) return;
		TestRunner->TestEqual(TEXT("GetComponent should find existing and return null for missing"),
			Invoker.CallAndReturn<int32>(INDEX_NONE), 1);

		TestRunner->TestNotNull(TEXT("GetComponent fixture should have a root scene component"), FindComponentByName<USceneComponent>(Actor, TEXT("RootScene")));
		TestRunner->TestNotNull(TEXT("GetComponent fixture should have a static mesh component"), FindComponentByName<UStaticMeshComponent>(Actor, TEXT("Mesh")));
		TestRunner->TestNotNull(TEXT("GetComponent fixture should have a billboard component"), FindComponentByName<UBillboardComponent>(Actor, TEXT("Billboard")));
		TestRunner->TestEqual(TEXT("GetComponent fixture should not create extra components"), CountComponentsByClass(Actor, UActorComponent::StaticClass()), 3);
	}

	TEST_METHOD(GetOrCreateComponent)
	{
		using namespace AngelscriptTest_Functional_Actor_Component_Private;

		FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();
		FAngelscriptEngineScope Scope(Engine);
		static const FName ModuleName(TEXT("TestActorGetOrCreateComponent"));
		ON_SCOPE_EXIT { Engine.DiscardModule(*ModuleName.ToString()); };

		UClass* ScriptClass = CompileScriptModule(*TestRunner, Engine, ModuleName,
			TEXT("TestActorGetOrCreateComponent.as"),
			TEXT(R"AS(
UCLASS()
class ATestActorGetOrCreateComponent : AActor
{
	UPROPERTY(DefaultComponent, RootComponent)
	USceneComponent RootScene;

	UFUNCTION()
	int RunGetOrCreateComponentTest()
	{
		UActorComponent ExistingRootByName = GetOrCreateComponent(USceneComponent::StaticClass(), n"RootScene");
		if (ExistingRootByName == nullptr)
			return 10;
		if (ExistingRootByName != RootScene)
			return 11;

		UActorComponent ExistingRootByClass = GetOrCreateComponent(USceneComponent::StaticClass());
		if (ExistingRootByClass == nullptr)
			return 20;
		if (ExistingRootByClass != RootScene)
			return 21;

		USceneComponent FirstLazy = Cast<USceneComponent>(GetOrCreateComponent(USceneComponent::StaticClass(), n"LazyScene"));
		if (FirstLazy == nullptr)
			return 30;
		if (FirstLazy.GetOwner() != this)
			return 31;
		if (FirstLazy.GetName() != n"LazyScene")
			return 32;

		UActorComponent SecondLazy = GetOrCreateComponent(USceneComponent::StaticClass(), n"LazyScene");
		if (SecondLazy == nullptr)
			return 40;
		if (FirstLazy != SecondLazy)
			return 41;

		UBillboardComponent FirstBillboard = Cast<UBillboardComponent>(GetOrCreateComponent(UBillboardComponent::StaticClass(), n"LazyBillboard"));
		if (FirstBillboard == nullptr)
			return 50;
		if (FirstBillboard.GetOwner() != this)
			return 51;

		UActorComponent BillboardBySceneName = GetOrCreateComponent(USceneComponent::StaticClass(), n"LazyBillboard");
		if (BillboardBySceneName != FirstBillboard)
			return 60;

		TArray<UActorComponent> AllComponents;
		GetAllComponents(UActorComponent::StaticClass(), AllComponents);
		if (AllComponents.Num() != 3)
			return 70;

		return 1;
	}
}
)AS"),
			TEXT("ATestActorGetOrCreateComponent"));
		if (ScriptClass == nullptr) return;

		FAngelscriptTestWorld W(*TestRunner, Engine);
		if (!W.IsValid()) return;
		AActor* Actor = W.SpawnActorOfClass(ScriptClass);
		if (!TestRunner->TestNotNull(TEXT("Actor should spawn"), Actor)) return;
		W.BeginPlay(*Actor);

		FFunctionInvoker Invoker(*TestRunner, Actor, FName(TEXT("RunGetOrCreateComponentTest")));
		if (!Invoker.IsValid()) return;
		TestRunner->TestEqual(TEXT("GetOrCreateComponent should reuse existing and create on first access"),
			Invoker.CallAndReturn<int32>(INDEX_NONE), 1);

		USceneComponent* RootScene = FindComponentByName<USceneComponent>(Actor, TEXT("RootScene"));
		USceneComponent* LazyScene = FindComponentByName<USceneComponent>(Actor, TEXT("LazyScene"));
		UBillboardComponent* LazyBillboard = FindComponentByName<UBillboardComponent>(Actor, TEXT("LazyBillboard"));
		if (!TestRunner->TestNotNull(TEXT("GetOrCreateComponent should keep the original root scene"), RootScene)
			|| !TestRunner->TestNotNull(TEXT("GetOrCreateComponent should create one lazy scene component"), LazyScene)
			|| !TestRunner->TestNotNull(TEXT("GetOrCreateComponent should create one lazy billboard component"), LazyBillboard))
		{
			return;
		}

		TestRunner->TestEqual(TEXT("GetOrCreateComponent should not replace the root component"), Actor->GetRootComponent(), RootScene);
		TestRunner->TestEqual(TEXT("GetOrCreateComponent should attach created scene components to the root"), LazyScene->GetAttachParent(), RootScene);
		TestRunner->TestEqual(TEXT("GetOrCreateComponent should attach created scene-derived components to the root"), LazyBillboard->GetAttachParent(), RootScene);
		TestRunner->TestEqual(TEXT("GetOrCreateComponent should not duplicate components when called repeatedly"), CountComponentsByClass(Actor, UActorComponent::StaticClass()), 3);
	}

	TEST_METHOD(GetAllComponents)
	{
		using namespace AngelscriptTest_Functional_Actor_Component_Private;

		FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();
		FAngelscriptEngineScope Scope(Engine);
		static const FName ModuleName(TEXT("TestActorGetAllComponents"));
		ON_SCOPE_EXIT { Engine.DiscardModule(*ModuleName.ToString()); };

		UClass* ScriptClass = CompileScriptModule(*TestRunner, Engine, ModuleName,
			TEXT("TestActorGetAllComponents.as"),
			TEXT(R"AS(
UCLASS()
class UTestCompA : USceneComponent
{
}

UCLASS()
class UTestCompB : USceneComponent
{
}

UCLASS()
class UTestCompDerivedB : UTestCompB
{
}

UCLASS()
class ATestActorGetAllComponents : AActor
{
	UPROPERTY(DefaultComponent, RootComponent)
	UTestCompA CompA;

	UPROPERTY(DefaultComponent, Attach = CompA)
	UTestCompB CompB;

	UPROPERTY(DefaultComponent, Attach = CompA)
	UTestCompB CompB2;

	UPROPERTY(DefaultComponent, Attach = CompA)
	UTestCompDerivedB DerivedB;

	UPROPERTY(DefaultComponent, Attach = CompA)
	UTestCompDerivedB DerivedB2;

	UPROPERTY(DefaultComponent, Attach = CompA)
	UBillboardComponent Billboard;

	UPROPERTY(DefaultComponent, Attach = CompA)
	UBillboardComponent Billboard2;

	UPROPERTY()
	TArray<UActorComponent> LastBFamilyForCpp;

	UPROPERTY()
	TArray<UActorComponent> LastAllComponentsForCpp;

	UPROPERTY()
	TArray<UActorComponent> LastBillboardsForCpp;

	UFUNCTION()
	int RunGetAllComponentsTest()
	{
		TArray<UActorComponent> SeededComps;
		SeededComps.Add(CompA);
		GetAllComponents(UStaticMeshComponent::StaticClass(), SeededComps);
		if (SeededComps.Num() != 1)
			return 5;
		if (SeededComps[0] != CompA)
			return 6;

		TArray<UActorComponent> AllComps;
		GetAllComponents(UActorComponent::StaticClass(), AllComps);
		if (AllComps.Num() != 7)
			return 10;

		TArray<UActorComponent> AllScenes;
		GetAllComponents(USceneComponent::StaticClass(), AllScenes);
		if (AllScenes.Num() != 7)
			return 20;

		TArray<UActorComponent> BFamily;
		GetAllComponents(UTestCompB::StaticClass(), BFamily);
		if (BFamily.Num() != 4)
			return 30;
		if (!BFamily.Contains(CompB))
			return 31;
		if (!BFamily.Contains(CompB2))
			return 32;
		if (!BFamily.Contains(DerivedB))
			return 33;
		if (!BFamily.Contains(DerivedB2))
			return 34;

		TArray<UActorComponent> OnlyDerivedB;
		GetAllComponents(UTestCompDerivedB::StaticClass(), OnlyDerivedB);
		if (OnlyDerivedB.Num() != 2)
			return 40;
		if (!OnlyDerivedB.Contains(DerivedB))
			return 41;
		if (!OnlyDerivedB.Contains(DerivedB2))
			return 42;

		TArray<UActorComponent> OnlyBillboard;
		GetAllComponents(UBillboardComponent::StaticClass(), OnlyBillboard);
		if (OnlyBillboard.Num() != 2)
			return 50;
		if (!OnlyBillboard.Contains(Billboard))
			return 51;
		if (!OnlyBillboard.Contains(Billboard2))
			return 52;

		TArray<UActorComponent> NoMatch;
		GetAllComponents(UStaticMeshComponent::StaticClass(), NoMatch);
		if (NoMatch.Num() != 0)
			return 60;

		return 1;
	}

	UFUNCTION()
	UActorComponent ReturnRootForCpp()
	{
		return CompA;
	}

	UFUNCTION()
	UActorComponent ReturnDerivedForCpp()
	{
		return DerivedB;
	}

	UFUNCTION()
	void FillBFamilyForCpp(TArray<UActorComponent>& OutComponents)
	{
		GetAllComponents(UTestCompB::StaticClass(), OutComponents);
	}

	UFUNCTION()
	void FillAllActorComponentsForCpp(TArray<UActorComponent>& OutComponents)
	{
		GetAllComponents(UActorComponent::StaticClass(), OutComponents);
	}

	UFUNCTION()
	void AppendBillboardsForCpp(TArray<UActorComponent>& OutComponents)
	{
		GetAllComponents(UBillboardComponent::StaticClass(), OutComponents);
	}

	UFUNCTION()
	void StoreArraysForCpp()
	{
		LastBFamilyForCpp.Empty();
		GetAllComponents(UTestCompB::StaticClass(), LastBFamilyForCpp);

		LastAllComponentsForCpp.Empty();
		GetAllComponents(UActorComponent::StaticClass(), LastAllComponentsForCpp);

		LastBillboardsForCpp.Empty();
		GetAllComponents(UBillboardComponent::StaticClass(), LastBillboardsForCpp);
	}
}
)AS"),
			TEXT("ATestActorGetAllComponents"));
		if (ScriptClass == nullptr) return;

		FAngelscriptTestWorld W(*TestRunner, Engine);
		if (!W.IsValid()) return;
		AActor* Actor = W.SpawnActorOfClass(ScriptClass);
		if (!TestRunner->TestNotNull(TEXT("Actor should spawn"), Actor)) return;
		W.BeginPlay(*Actor);

		FFunctionInvoker Invoker(*TestRunner, Actor, FName(TEXT("RunGetAllComponentsTest")));
		if (!Invoker.IsValid()) return;
		TestRunner->TestEqual(TEXT("GetAllComponents should filter by class and return correct counts"),
			Invoker.CallAndReturn<int32>(INDEX_NONE), 1);

		TestRunner->TestEqual(TEXT("GetAllComponents fixture should contain seven actor components"), CountComponentsByClass(Actor, UActorComponent::StaticClass()), 7);
		TestRunner->TestEqual(TEXT("GetAllComponents fixture should contain seven scene components"), CountComponentsByClass(Actor, USceneComponent::StaticClass()), 7);
		TestRunner->TestEqual(TEXT("GetAllComponents fixture should contain two billboard components"), CountComponentsByClass(Actor, UBillboardComponent::StaticClass()), 2);

		UActorComponent* CompA = FindComponentByName<UActorComponent>(Actor, TEXT("CompA"));
		UActorComponent* CompB = FindComponentByName<UActorComponent>(Actor, TEXT("CompB"));
		UActorComponent* CompB2 = FindComponentByName<UActorComponent>(Actor, TEXT("CompB2"));
		UActorComponent* DerivedB = FindComponentByName<UActorComponent>(Actor, TEXT("DerivedB"));
		UActorComponent* DerivedB2 = FindComponentByName<UActorComponent>(Actor, TEXT("DerivedB2"));
		UActorComponent* Billboard = FindComponentByName<UActorComponent>(Actor, TEXT("Billboard"));
		UActorComponent* Billboard2 = FindComponentByName<UActorComponent>(Actor, TEXT("Billboard2"));
		if (!TestRunner->TestNotNull(TEXT("GetAllComponents C++ fixture should expose CompA"), CompA)
			|| !TestRunner->TestNotNull(TEXT("GetAllComponents C++ fixture should expose CompB"), CompB)
			|| !TestRunner->TestNotNull(TEXT("GetAllComponents C++ fixture should expose CompB2"), CompB2)
			|| !TestRunner->TestNotNull(TEXT("GetAllComponents C++ fixture should expose DerivedB"), DerivedB)
			|| !TestRunner->TestNotNull(TEXT("GetAllComponents C++ fixture should expose DerivedB2"), DerivedB2)
			|| !TestRunner->TestNotNull(TEXT("GetAllComponents C++ fixture should expose Billboard"), Billboard)
			|| !TestRunner->TestNotNull(TEXT("GetAllComponents C++ fixture should expose Billboard2"), Billboard2))
		{
			return;
		}

		FFunctionInvoker ReturnRootInvoker(*TestRunner, Actor, FName(TEXT("ReturnRootForCpp")));
		if (!ReturnRootInvoker.IsValid()) return;
		UActorComponent* ReturnedRoot = ReturnRootInvoker.CallAndReturn<UActorComponent*>(nullptr);
		TestRunner->TestEqual(TEXT("Script should return CompA to C++ as a component object"), ReturnedRoot, CompA);

		FFunctionInvoker ReturnDerivedInvoker(*TestRunner, Actor, FName(TEXT("ReturnDerivedForCpp")));
		if (!ReturnDerivedInvoker.IsValid()) return;
		UActorComponent* ReturnedDerived = ReturnDerivedInvoker.CallAndReturn<UActorComponent*>(nullptr);
		TestRunner->TestEqual(TEXT("Script should return DerivedB to C++ as a component object"), ReturnedDerived, DerivedB);

		TArray<UActorComponent*> BFamily;
		if (!InvokeComponentArrayOut(*TestRunner, Actor, FName(TEXT("FillBFamilyForCpp")), {}, BFamily)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("GetAllComponents B-family returned to C++: [%s]"), *DescribeComponents(BFamily)));
		TestRunner->TestEqual(TEXT("B-family array returned to C++ should contain every base and derived component"), BFamily.Num(), 4);
		TestRunner->TestTrue(TEXT("B-family array returned to C++ should include CompB"), BFamily.Contains(CompB));
		TestRunner->TestTrue(TEXT("B-family array returned to C++ should include CompB2"), BFamily.Contains(CompB2));
		TestRunner->TestTrue(TEXT("B-family array returned to C++ should include DerivedB"), BFamily.Contains(DerivedB));
		TestRunner->TestTrue(TEXT("B-family array returned to C++ should include DerivedB2"), BFamily.Contains(DerivedB2));

		TArray<UActorComponent*> AllActorComponents;
		if (!InvokeComponentArrayOut(*TestRunner, Actor, FName(TEXT("FillAllActorComponentsForCpp")), {}, AllActorComponents)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("GetAllComponents all actor components returned to C++: [%s]"), *DescribeComponents(AllActorComponents)));
		TestRunner->TestEqual(TEXT("All actor components returned to C++ should include every fixture component"), AllActorComponents.Num(), 7);
		TestRunner->TestTrue(TEXT("All actor components returned to C++ should include CompA"), AllActorComponents.Contains(CompA));
		TestRunner->TestTrue(TEXT("All actor components returned to C++ should include CompB"), AllActorComponents.Contains(CompB));
		TestRunner->TestTrue(TEXT("All actor components returned to C++ should include CompB2"), AllActorComponents.Contains(CompB2));
		TestRunner->TestTrue(TEXT("All actor components returned to C++ should include DerivedB"), AllActorComponents.Contains(DerivedB));
		TestRunner->TestTrue(TEXT("All actor components returned to C++ should include DerivedB2"), AllActorComponents.Contains(DerivedB2));
		TestRunner->TestTrue(TEXT("All actor components returned to C++ should include Billboard"), AllActorComponents.Contains(Billboard));
		TestRunner->TestTrue(TEXT("All actor components returned to C++ should include Billboard2"), AllActorComponents.Contains(Billboard2));

		TArray<UActorComponent*> SeededComponents;
		SeededComponents.Add(CompA);
		TArray<UActorComponent*> SeededBillboards;
		if (!InvokeComponentArrayOut(*TestRunner, Actor, FName(TEXT("AppendBillboardsForCpp")), SeededComponents, SeededBillboards)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("GetAllComponents seeded billboard append returned to C++: [%s]"), *DescribeComponents(SeededBillboards)));
		if (!TestRunner->TestEqual(TEXT("Seeded array returned to C++ should preserve seed and append both billboards"), SeededBillboards.Num(), 3)) return;
		TestRunner->TestEqual(TEXT("Seeded array returned to C++ should keep CompA as the seed element"), SeededBillboards[0], CompA);
		TestRunner->TestTrue(TEXT("Seeded array returned to C++ should append Billboard"), SeededBillboards.Contains(Billboard));
		TestRunner->TestTrue(TEXT("Seeded array returned to C++ should append Billboard2"), SeededBillboards.Contains(Billboard2));

		FFunctionInvoker StoreArraysInvoker(*TestRunner, Actor, FName(TEXT("StoreArraysForCpp")));
		if (!StoreArraysInvoker.IsValid() || !StoreArraysInvoker.Call()) return;

		TArray<UActorComponent*> StoredBFamily;
		if (!ReadComponentArrayProperty(*TestRunner, Actor, FName(TEXT("LastBFamilyForCpp")), StoredBFamily)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("GetAllComponents B-family stored property read by C++: [%s]"), *DescribeComponents(StoredBFamily)));
		TestRunner->TestEqual(TEXT("Stored B-family array should contain four components"), StoredBFamily.Num(), 4);
		TestRunner->TestTrue(TEXT("Stored B-family array should include CompB"), StoredBFamily.Contains(CompB));
		TestRunner->TestTrue(TEXT("Stored B-family array should include CompB2"), StoredBFamily.Contains(CompB2));
		TestRunner->TestTrue(TEXT("Stored B-family array should include DerivedB"), StoredBFamily.Contains(DerivedB));
		TestRunner->TestTrue(TEXT("Stored B-family array should include DerivedB2"), StoredBFamily.Contains(DerivedB2));

		TArray<UActorComponent*> StoredAllComponents;
		if (!ReadComponentArrayProperty(*TestRunner, Actor, FName(TEXT("LastAllComponentsForCpp")), StoredAllComponents)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("GetAllComponents all components stored property read by C++: [%s]"), *DescribeComponents(StoredAllComponents)));
		TestRunner->TestEqual(TEXT("Stored all-components array should contain seven components"), StoredAllComponents.Num(), 7);
		TestRunner->TestTrue(TEXT("Stored all-components array should include CompA"), StoredAllComponents.Contains(CompA));
		TestRunner->TestTrue(TEXT("Stored all-components array should include CompB"), StoredAllComponents.Contains(CompB));
		TestRunner->TestTrue(TEXT("Stored all-components array should include CompB2"), StoredAllComponents.Contains(CompB2));
		TestRunner->TestTrue(TEXT("Stored all-components array should include DerivedB"), StoredAllComponents.Contains(DerivedB));
		TestRunner->TestTrue(TEXT("Stored all-components array should include DerivedB2"), StoredAllComponents.Contains(DerivedB2));
		TestRunner->TestTrue(TEXT("Stored all-components array should include Billboard"), StoredAllComponents.Contains(Billboard));
		TestRunner->TestTrue(TEXT("Stored all-components array should include Billboard2"), StoredAllComponents.Contains(Billboard2));

		TArray<UActorComponent*> StoredBillboards;
		if (!ReadComponentArrayProperty(*TestRunner, Actor, FName(TEXT("LastBillboardsForCpp")), StoredBillboards)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("GetAllComponents billboards stored property read by C++: [%s]"), *DescribeComponents(StoredBillboards)));
		TestRunner->TestEqual(TEXT("Stored billboard array should contain two components"), StoredBillboards.Num(), 2);
		TestRunner->TestTrue(TEXT("Stored billboard array should include Billboard"), StoredBillboards.Contains(Billboard));
		TestRunner->TestTrue(TEXT("Stored billboard array should include Billboard2"), StoredBillboards.Contains(Billboard2));
	}

	TEST_METHOD(ReturnComponentsToCpp)
	{
		using namespace AngelscriptTest_Functional_Actor_Component_Private;

		FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();
		FAngelscriptEngineScope Scope(Engine);
		static const FName ModuleName(TEXT("TestActorReturnComponentsToCpp"));
		ON_SCOPE_EXIT { Engine.DiscardModule(*ModuleName.ToString()); };

		UClass* ScriptClass = CompileScriptModule(*TestRunner, Engine, ModuleName,
			TEXT("TestActorReturnComponentsToCpp.as"),
			TEXT(R"AS(
UCLASS()
class UReturnComponentBase : USceneComponent
{
}

UCLASS()
class UReturnComponentDerived : UReturnComponentBase
{
}

UCLASS()
class ATestActorReturnComponentsToCpp : AActor
{
	UPROPERTY(DefaultComponent, RootComponent)
	USceneComponent RootScene;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UReturnComponentBase BaseA;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UReturnComponentBase BaseB;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UReturnComponentDerived DerivedA;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UReturnComponentDerived DerivedB;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UBillboardComponent BillboardA;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UBillboardComponent BillboardB;

	UPROPERTY()
	TArray<UActorComponent> StoredBaseFamily;

	UPROPERTY()
	TArray<UActorComponent> StoredAllComponents;

	UPROPERTY()
	TArray<UActorComponent> StoredBillboards;

	UFUNCTION()
	UActorComponent ReturnBaseAForCpp()
	{
		return BaseA;
	}

	UFUNCTION()
	UActorComponent ReturnDerivedBForCpp()
	{
		return DerivedB;
	}

	UFUNCTION()
	UActorComponent ReturnComponentByNameForCpp(FName ComponentName)
	{
		return GetComponent(UActorComponent::StaticClass(), ComponentName);
	}

	UFUNCTION()
	USceneComponent ReturnCreatedNamedSceneForCpp()
	{
		return Cast<USceneComponent>(CreateComponent(USceneComponent::StaticClass(), n"CppExplicitNamedScene"));
	}

	UFUNCTION()
	void ReturnBaseFamilyArrayForCpp(TArray<UActorComponent>& OutComponents)
	{
		GetAllComponents(UReturnComponentBase::StaticClass(), OutComponents);
	}

	UFUNCTION()
	void ReturnAllComponentsArrayForCpp(TArray<UActorComponent>& OutComponents)
	{
		GetAllComponents(UActorComponent::StaticClass(), OutComponents);
	}

	UFUNCTION()
	void AppendBillboardArrayForCpp(TArray<UActorComponent>& OutComponents)
	{
		GetAllComponents(UBillboardComponent::StaticClass(), OutComponents);
	}

	UFUNCTION()
	void StoreComponentArraysForCpp()
	{
		StoredBaseFamily.Empty();
		GetAllComponents(UReturnComponentBase::StaticClass(), StoredBaseFamily);

		StoredAllComponents.Empty();
		GetAllComponents(UActorComponent::StaticClass(), StoredAllComponents);

		StoredBillboards.Empty();
		GetAllComponents(UBillboardComponent::StaticClass(), StoredBillboards);
	}
}
)AS"),
			TEXT("ATestActorReturnComponentsToCpp"));
		if (ScriptClass == nullptr) return;

		UClass* ReturnBaseClass = FindGeneratedClass(&Engine, TEXT("UReturnComponentBase"));
		UClass* ReturnDerivedClass = FindGeneratedClass(&Engine, TEXT("UReturnComponentDerived"));
		if (!TestRunner->TestNotNull(TEXT("ReturnComponentsToCpp should generate the base component class"), ReturnBaseClass)
			|| !TestRunner->TestNotNull(TEXT("ReturnComponentsToCpp should generate the derived component class"), ReturnDerivedClass))
		{
			return;
		}

		FAngelscriptTestWorld W(*TestRunner, Engine);
		if (!W.IsValid()) return;
		AActor* Actor = W.SpawnActorOfClass(ScriptClass);
		if (!TestRunner->TestNotNull(TEXT("Actor should spawn"), Actor)) return;
		W.BeginPlay(*Actor);

		UActorComponent* BaseA = FindComponentByName<UActorComponent>(Actor, TEXT("BaseA"));
		UActorComponent* BaseB = FindComponentByName<UActorComponent>(Actor, TEXT("BaseB"));
		UActorComponent* DerivedA = FindComponentByName<UActorComponent>(Actor, TEXT("DerivedA"));
		UActorComponent* DerivedB = FindComponentByName<UActorComponent>(Actor, TEXT("DerivedB"));
		UActorComponent* BillboardA = FindComponentByName<UActorComponent>(Actor, TEXT("BillboardA"));
		UActorComponent* BillboardB = FindComponentByName<UActorComponent>(Actor, TEXT("BillboardB"));
		if (!TestRunner->TestNotNull(TEXT("ReturnComponentsToCpp fixture should expose BaseA"), BaseA)
			|| !TestRunner->TestNotNull(TEXT("ReturnComponentsToCpp fixture should expose BaseB"), BaseB)
			|| !TestRunner->TestNotNull(TEXT("ReturnComponentsToCpp fixture should expose DerivedA"), DerivedA)
			|| !TestRunner->TestNotNull(TEXT("ReturnComponentsToCpp fixture should expose DerivedB"), DerivedB)
			|| !TestRunner->TestNotNull(TEXT("ReturnComponentsToCpp fixture should expose BillboardA"), BillboardA)
			|| !TestRunner->TestNotNull(TEXT("ReturnComponentsToCpp fixture should expose BillboardB"), BillboardB))
		{
			return;
		}

		TestRunner->TestTrue(TEXT("BaseA should use the generated base component class"), BaseA->IsA(ReturnBaseClass));
		TestRunner->TestTrue(TEXT("DerivedA should use the generated derived component class"), DerivedA->IsA(ReturnDerivedClass));

		FFunctionInvoker ReturnBaseInvoker(*TestRunner, Actor, FName(TEXT("ReturnBaseAForCpp")));
		if (!ReturnBaseInvoker.IsValid()) return;
		UActorComponent* ReturnedBaseA = ReturnBaseInvoker.CallAndReturn<UActorComponent*>(nullptr);
		if (!TestRunner->TestNotNull(TEXT("ReturnBaseAForCpp should return a component object"), ReturnedBaseA)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp single component returned to C++: %s:%s"), *ReturnedBaseA->GetName(), *ReturnedBaseA->GetClass()->GetName()));
		TestRunner->TestEqual(TEXT("ReturnBaseAForCpp should return BaseA to C++"), ReturnedBaseA, BaseA);

		FFunctionInvoker ReturnDerivedInvoker(*TestRunner, Actor, FName(TEXT("ReturnDerivedBForCpp")));
		if (!ReturnDerivedInvoker.IsValid()) return;
		UActorComponent* ReturnedDerivedB = ReturnDerivedInvoker.CallAndReturn<UActorComponent*>(nullptr);
		if (!TestRunner->TestNotNull(TEXT("ReturnDerivedBForCpp should return a component object"), ReturnedDerivedB)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp derived component returned to C++: %s:%s"), *ReturnedDerivedB->GetName(), *ReturnedDerivedB->GetClass()->GetName()));
		TestRunner->TestEqual(TEXT("ReturnDerivedBForCpp should return DerivedB to C++"), ReturnedDerivedB, DerivedB);

		FFunctionInvoker ReturnByNameInvoker(*TestRunner, Actor, FName(TEXT("ReturnComponentByNameForCpp")));
		if (!ReturnByNameInvoker.IsValid()) return;
		ReturnByNameInvoker.AddParam<FName>(FName(TEXT("BillboardB")));
		UActorComponent* ReturnedByName = ReturnByNameInvoker.CallAndReturn<UActorComponent*>(nullptr);
		if (!TestRunner->TestNotNull(TEXT("ReturnComponentByNameForCpp should return a component object"), ReturnedByName)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp component returned by name to C++: %s:%s"), *ReturnedByName->GetName(), *ReturnedByName->GetClass()->GetName()));
		TestRunner->TestEqual(TEXT("ReturnComponentByNameForCpp should return the named BillboardB component"), ReturnedByName, BillboardB);

		TArray<UActorComponent*> BaseFamily;
		if (!InvokeComponentArrayOut(*TestRunner, Actor, FName(TEXT("ReturnBaseFamilyArrayForCpp")), {}, BaseFamily)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp base-family array returned to C++: [%s]"), *DescribeComponents(BaseFamily)));
		TestRunner->TestEqual(TEXT("Returned base-family array should contain base and derived instances"), BaseFamily.Num(), 4);
		TestRunner->TestTrue(TEXT("Returned base-family array should include BaseA"), BaseFamily.Contains(BaseA));
		TestRunner->TestTrue(TEXT("Returned base-family array should include BaseB"), BaseFamily.Contains(BaseB));
		TestRunner->TestTrue(TEXT("Returned base-family array should include DerivedA"), BaseFamily.Contains(DerivedA));
		TestRunner->TestTrue(TEXT("Returned base-family array should include DerivedB"), BaseFamily.Contains(DerivedB));

		TArray<UActorComponent*> AllComponents;
		if (!InvokeComponentArrayOut(*TestRunner, Actor, FName(TEXT("ReturnAllComponentsArrayForCpp")), {}, AllComponents)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp all-component array returned to C++: [%s]"), *DescribeComponents(AllComponents)));
		TestRunner->TestEqual(TEXT("Returned all-component array should include every default component"), AllComponents.Num(), 7);
		TestRunner->TestTrue(TEXT("Returned all-component array should include BaseA"), AllComponents.Contains(BaseA));
		TestRunner->TestTrue(TEXT("Returned all-component array should include BaseB"), AllComponents.Contains(BaseB));
		TestRunner->TestTrue(TEXT("Returned all-component array should include DerivedA"), AllComponents.Contains(DerivedA));
		TestRunner->TestTrue(TEXT("Returned all-component array should include DerivedB"), AllComponents.Contains(DerivedB));
		TestRunner->TestTrue(TEXT("Returned all-component array should include BillboardA"), AllComponents.Contains(BillboardA));
		TestRunner->TestTrue(TEXT("Returned all-component array should include BillboardB"), AllComponents.Contains(BillboardB));

		TArray<UActorComponent*> SeededComponents;
		SeededComponents.Add(BaseA);
		TArray<UActorComponent*> SeededBillboards;
		if (!InvokeComponentArrayOut(*TestRunner, Actor, FName(TEXT("AppendBillboardArrayForCpp")), SeededComponents, SeededBillboards)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp seeded billboard array returned to C++: [%s]"), *DescribeComponents(SeededBillboards)));
		if (!TestRunner->TestEqual(TEXT("Returned seeded billboard array should preserve the seed and append both billboards"), SeededBillboards.Num(), 3)) return;
		TestRunner->TestEqual(TEXT("Returned seeded billboard array should keep BaseA as the seed element"), SeededBillboards[0], BaseA);
		TestRunner->TestTrue(TEXT("Returned seeded billboard array should include BillboardA"), SeededBillboards.Contains(BillboardA));
		TestRunner->TestTrue(TEXT("Returned seeded billboard array should include BillboardB"), SeededBillboards.Contains(BillboardB));

		FFunctionInvoker StoreArraysInvoker(*TestRunner, Actor, FName(TEXT("StoreComponentArraysForCpp")));
		if (!StoreArraysInvoker.IsValid() || !StoreArraysInvoker.Call()) return;

		TArray<UActorComponent*> StoredBaseFamily;
		if (!ReadComponentArrayProperty(*TestRunner, Actor, FName(TEXT("StoredBaseFamily")), StoredBaseFamily)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp stored base-family property read by C++: [%s]"), *DescribeComponents(StoredBaseFamily)));
		TestRunner->TestEqual(TEXT("Stored base-family property should contain four components"), StoredBaseFamily.Num(), 4);
		TestRunner->TestTrue(TEXT("Stored base-family property should include BaseA"), StoredBaseFamily.Contains(BaseA));
		TestRunner->TestTrue(TEXT("Stored base-family property should include BaseB"), StoredBaseFamily.Contains(BaseB));
		TestRunner->TestTrue(TEXT("Stored base-family property should include DerivedA"), StoredBaseFamily.Contains(DerivedA));
		TestRunner->TestTrue(TEXT("Stored base-family property should include DerivedB"), StoredBaseFamily.Contains(DerivedB));

		TArray<UActorComponent*> StoredAllComponents;
		if (!ReadComponentArrayProperty(*TestRunner, Actor, FName(TEXT("StoredAllComponents")), StoredAllComponents)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp stored all-component property read by C++: [%s]"), *DescribeComponents(StoredAllComponents)));
		TestRunner->TestEqual(TEXT("Stored all-component property should contain seven components"), StoredAllComponents.Num(), 7);
		TestRunner->TestTrue(TEXT("Stored all-component property should include BaseA"), StoredAllComponents.Contains(BaseA));
		TestRunner->TestTrue(TEXT("Stored all-component property should include BaseB"), StoredAllComponents.Contains(BaseB));
		TestRunner->TestTrue(TEXT("Stored all-component property should include DerivedA"), StoredAllComponents.Contains(DerivedA));
		TestRunner->TestTrue(TEXT("Stored all-component property should include DerivedB"), StoredAllComponents.Contains(DerivedB));
		TestRunner->TestTrue(TEXT("Stored all-component property should include BillboardA"), StoredAllComponents.Contains(BillboardA));
		TestRunner->TestTrue(TEXT("Stored all-component property should include BillboardB"), StoredAllComponents.Contains(BillboardB));

		TArray<UActorComponent*> StoredBillboards;
		if (!ReadComponentArrayProperty(*TestRunner, Actor, FName(TEXT("StoredBillboards")), StoredBillboards)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp stored billboard property read by C++: [%s]"), *DescribeComponents(StoredBillboards)));
		TestRunner->TestEqual(TEXT("Stored billboard property should contain two components"), StoredBillboards.Num(), 2);
		TestRunner->TestTrue(TEXT("Stored billboard property should include BillboardA"), StoredBillboards.Contains(BillboardA));
		TestRunner->TestTrue(TEXT("Stored billboard property should include BillboardB"), StoredBillboards.Contains(BillboardB));

		FFunctionInvoker CreateNamedInvoker(*TestRunner, Actor, FName(TEXT("ReturnCreatedNamedSceneForCpp")));
		if (!CreateNamedInvoker.IsValid()) return;
		USceneComponent* CreatedNamedScene = CreateNamedInvoker.CallAndReturn<USceneComponent*>(nullptr);
		if (!TestRunner->TestNotNull(TEXT("ReturnCreatedNamedSceneForCpp should return a dynamically created named component"), CreatedNamedScene)) return;
		TestRunner->AddInfo(FString::Printf(TEXT("ReturnComponentsToCpp dynamically created named component returned to C++: %s:%s"), *CreatedNamedScene->GetName(), *CreatedNamedScene->GetClass()->GetName()));
		TestRunner->TestEqual(TEXT("Returned dynamically created component should preserve its requested name"), CreatedNamedScene->GetFName(), FName(TEXT("CppExplicitNamedScene")));
		TestRunner->TestEqual(TEXT("Returned dynamically created component should be owned by the actor"), CreatedNamedScene->GetOwner(), Actor);
		TestRunner->TestTrue(TEXT("Returned dynamically created component should be registered"), CreatedNamedScene->IsRegistered());
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS
