#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptSceneComponentChildBindingsTests_Private
{
	static constexpr ANSICHAR SceneComponentChildCompatModuleName[] = "ASSceneComponentChildQueryCompat";
	static constexpr ANSICHAR SceneComponentChildErrorModuleName[] = "ASSceneComponentChildQueryErrors";
	static constexpr TCHAR RootComponentName[] = TEXT("SceneComponentChildBindingsRoot");
	static constexpr TCHAR DirectSceneChildName[] = TEXT("SceneComponentChildBindingsDirectSceneChild");
	static constexpr TCHAR DirectSphereChildName[] = TEXT("SceneComponentChildBindingsDirectSphereChild");
	static constexpr TCHAR NestedSphereChildName[] = TEXT("SceneComponentChildBindingsNestedSphereChild");

	struct FSceneComponentChildFixture
	{
		AActor* OwnerActor = nullptr;
		USceneComponent* RootComponent = nullptr;
		USceneComponent* DirectSceneChild = nullptr;
		USphereComponent* DirectSphereChild = nullptr;
		USphereComponent* NestedSphereChild = nullptr;
		FString RootComponentPath;
		FString DirectSceneChildPath;
		FString DirectSphereChildPath;
		FString NestedSphereChildPath;
	};

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FString EscapeScriptString(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	USceneComponent* CreateSceneComponent(
		FAutomationTestBase& Test,
		AActor& Owner,
		UClass* ComponentClass,
		const TCHAR* ComponentName,
		USceneComponent* ParentComponent)
	{
		USceneComponent* Component = NewObject<USceneComponent>(&Owner, ComponentClass, FName(ComponentName));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s fixture should create component '%s'"), TEXT("SceneComponentChildBindings"), ComponentName),
				Component))
		{
			return nullptr;
		}

		Owner.AddInstanceComponent(Component);
		if (ParentComponent == nullptr)
		{
			Owner.SetRootComponent(Component);
		}
		else
		{
			Component->SetupAttachment(ParentComponent);
		}

		Component->RegisterComponent();
		return Component;
	}

	FSceneComponentChildFixture CreateFixture(FAutomationTestBase& Test, AActor& OwnerActor)
	{
		FSceneComponentChildFixture Fixture;
		Fixture.OwnerActor = &OwnerActor;

		Fixture.RootComponent = CreateSceneComponent(
			Test,
			OwnerActor,
			USceneComponent::StaticClass(),
			RootComponentName,
			nullptr);
		if (Fixture.RootComponent == nullptr)
		{
			return Fixture;
		}

		Fixture.DirectSceneChild = CreateSceneComponent(
			Test,
			OwnerActor,
			USceneComponent::StaticClass(),
			DirectSceneChildName,
			Fixture.RootComponent);
		if (Fixture.DirectSceneChild == nullptr)
		{
			return Fixture;
		}

		Fixture.DirectSphereChild = Cast<USphereComponent>(CreateSceneComponent(
			Test,
			OwnerActor,
			USphereComponent::StaticClass(),
			DirectSphereChildName,
			Fixture.RootComponent));
		if (!Test.TestNotNull(
				TEXT("SceneComponentChildBindings fixture should cast the direct sphere child to USphereComponent"),
				Fixture.DirectSphereChild))
		{
			return Fixture;
		}

		Fixture.NestedSphereChild = Cast<USphereComponent>(CreateSceneComponent(
			Test,
			OwnerActor,
			USphereComponent::StaticClass(),
			NestedSphereChildName,
			Fixture.DirectSceneChild));
		if (!Test.TestNotNull(
				TEXT("SceneComponentChildBindings fixture should cast the nested sphere child to USphereComponent"),
				Fixture.NestedSphereChild))
		{
			return Fixture;
		}

		Fixture.DirectSphereChild->SetSphereRadius(24.0f, false);
		Fixture.NestedSphereChild->SetSphereRadius(12.0f, false);
		Fixture.RootComponentPath = Fixture.RootComponent->GetPathName();
		Fixture.DirectSceneChildPath = Fixture.DirectSceneChild->GetPathName();
		Fixture.DirectSphereChildPath = Fixture.DirectSphereChild->GetPathName();
		Fixture.NestedSphereChildPath = Fixture.NestedSphereChild->GetPathName();
		return Fixture;
	}

	bool VerifyNativeFixtureBaseline(
		FAutomationTestBase& Test,
		const FSceneComponentChildFixture& Fixture)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentChildBindings native baseline should keep the root component alive"),
			Fixture.RootComponent);
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentChildBindings native baseline should keep the direct scene child alive"),
			Fixture.DirectSceneChild);
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentChildBindings native baseline should keep the direct sphere child alive"),
			Fixture.DirectSphereChild);
		bPassed &= Test.TestNotNull(
			TEXT("SceneComponentChildBindings native baseline should keep the nested sphere child alive"),
			Fixture.NestedSphereChild);
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentChildBindings fixture should expose a non-empty root path"),
			!Fixture.RootComponentPath.IsEmpty());
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentChildBindings fixture should expose a non-empty direct-child path"),
			!Fixture.DirectSceneChildPath.IsEmpty());
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentChildBindings fixture should expose a non-empty direct-sphere path"),
			!Fixture.DirectSphereChildPath.IsEmpty());
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentChildBindings fixture should expose a non-empty nested-sphere path"),
			!Fixture.NestedSphereChildPath.IsEmpty());
		bPassed &= Test.TestEqual(
			TEXT("SceneComponentChildBindings root should have exactly two direct children in the native baseline"),
			Fixture.RootComponent != nullptr ? Fixture.RootComponent->GetNumChildrenComponents() : INDEX_NONE,
			2);
		bPassed &= Test.TestEqual(
			TEXT("SceneComponentChildBindings direct scene child should have exactly one direct child in the native baseline"),
			Fixture.DirectSceneChild != nullptr ? Fixture.DirectSceneChild->GetNumChildrenComponents() : INDEX_NONE,
			1);

		TArray<USceneComponent*> DirectChildren;
		if (Fixture.RootComponent != nullptr)
		{
			Fixture.RootComponent->GetChildrenComponents(false, DirectChildren);
		}

		bPassed &= Test.TestEqual(
			TEXT("SceneComponentChildBindings native baseline should report exactly two direct descendants"),
			DirectChildren.Num(),
			2);
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentChildBindings native baseline should include the direct scene child in direct descendants"),
			DirectChildren.Contains(Fixture.DirectSceneChild));
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentChildBindings native baseline should include the direct sphere child in direct descendants"),
			DirectChildren.Contains(Fixture.DirectSphereChild));
		bPassed &= Test.TestFalse(
			TEXT("SceneComponentChildBindings native baseline should exclude nested descendants from the non-recursive query"),
			DirectChildren.Contains(Fixture.NestedSphereChild));

		TArray<USceneComponent*> AllChildren;
		if (Fixture.RootComponent != nullptr)
		{
			Fixture.RootComponent->GetChildrenComponents(true, AllChildren);
		}

		bPassed &= Test.TestEqual(
			TEXT("SceneComponentChildBindings native baseline should report exactly three recursive descendants"),
			AllChildren.Num(),
			3);
		bPassed &= Test.TestTrue(
			TEXT("SceneComponentChildBindings native baseline should include the nested sphere child in the recursive query"),
			AllChildren.Contains(Fixture.NestedSphereChild));
		return bPassed;
	}

	FString BuildCompatScript(const FSceneComponentChildFixture& Fixture)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	UObject RootObject = FindObject("__ROOT_COMPONENT_PATH__");
	UObject DirectChildObject = FindObject("__DIRECT_SCENE_CHILD_PATH__");
	UObject DirectSphereObject = FindObject("__DIRECT_SPHERE_CHILD_PATH__");
	UObject NestedSphereObject = FindObject("__NESTED_SPHERE_CHILD_PATH__");

	USceneComponent Root = Cast<USceneComponent>(RootObject);
	USceneComponent DirectChild = Cast<USceneComponent>(DirectChildObject);
	USceneComponent DirectSphereScene = Cast<USceneComponent>(DirectSphereObject);
	USceneComponent NestedSphereScene = Cast<USceneComponent>(NestedSphereObject);
	USphereComponent DirectSphere = Cast<USphereComponent>(DirectSphereObject);
	USphereComponent NestedSphere = Cast<USphereComponent>(NestedSphereObject);
	if (Root == null || DirectChild == null || DirectSphereScene == null || NestedSphereScene == null || DirectSphere == null || NestedSphere == null)
		return 2;

	if (Root.GetNumChildrenComponents() != 2)
		return 4;
	if (DirectChild.GetNumChildrenComponents() != 1)
		return 6;

	if (Root.GetChildComponentByClass(USphereComponent::StaticClass()) != DirectSphereScene)
		return 8;
	if (DirectChild.GetChildComponentByClass(USceneComponent::StaticClass()) != NestedSphereScene)
		return 10;

	TArray<USceneComponent> DirectChildren;
	Root.GetChildrenComponentsByClass(USceneComponent::StaticClass(), false, DirectChildren);
	if (DirectChildren.Num() != 2)
		return 12;

	bool bSawDirectChild = false;
	bool bSawDirectSphere = false;
	for (int32 Index = 0; Index < DirectChildren.Num(); ++Index)
	{
		if (DirectChildren[Index] == DirectChild)
			bSawDirectChild = true;
		else if (DirectChildren[Index] == DirectSphereScene)
			bSawDirectSphere = true;
		else
			return 14;
	}
	if (!bSawDirectChild || !bSawDirectSphere)
		return 16;

	TArray<USphereComponent> DirectSpheres;
	Root.GetChildrenComponentsByClass(USphereComponent::StaticClass(), false, DirectSpheres);
	if (DirectSpheres.Num() != 1)
		return 18;
	if (DirectSpheres[0] != DirectSphere)
		return 20;

	TArray<USphereComponent> AllSpheres;
	Root.GetChildrenComponentsByClass(USphereComponent::StaticClass(), true, AllSpheres);
	if (AllSpheres.Num() != 2)
		return 22;

	bool bSawRecursiveDirectSphere = false;
	bool bSawRecursiveNestedSphere = false;
	for (int32 Index = 0; Index < AllSpheres.Num(); ++Index)
	{
		if (AllSpheres[Index] == DirectSphere)
			bSawRecursiveDirectSphere = true;
		else if (AllSpheres[Index] == NestedSphere)
			bSawRecursiveNestedSphere = true;
		else
			return 24;
	}
	if (!bSawRecursiveDirectSphere || !bSawRecursiveNestedSphere)
		return 26;

	return 1;
}
)AS");

		ReplaceToken(Script, TEXT("__ROOT_COMPONENT_PATH__"), EscapeScriptString(Fixture.RootComponentPath));
		ReplaceToken(Script, TEXT("__DIRECT_SCENE_CHILD_PATH__"), EscapeScriptString(Fixture.DirectSceneChildPath));
		ReplaceToken(Script, TEXT("__DIRECT_SPHERE_CHILD_PATH__"), EscapeScriptString(Fixture.DirectSphereChildPath));
		ReplaceToken(Script, TEXT("__NESTED_SPHERE_CHILD_PATH__"), EscapeScriptString(Fixture.NestedSphereChildPath));
		return Script;
	}

	FString BuildErrorScript(const FSceneComponentChildFixture& Fixture)
	{
		FString Script = TEXT(R"AS(
void TriggerWrongArrayElementType()
{
	USceneComponent Root = Cast<USceneComponent>(FindObject("__ROOT_COMPONENT_PATH__"));
	if (Root == null)
		return;

	TArray<UActorComponent> WrongChildren;
	Root.GetChildrenComponentsByClass(USceneComponent::StaticClass(), true, WrongChildren);
}

void TriggerMismatchedQueryClass()
{
	USceneComponent Root = Cast<USceneComponent>(FindObject("__ROOT_COMPONENT_PATH__"));
	if (Root == null)
		return;

	TArray<USphereComponent> WrongChildren;
	Root.GetChildrenComponentsByClass(USceneComponent::StaticClass(), true, WrongChildren);
}
)AS");

		ReplaceToken(Script, TEXT("__ROOT_COMPONENT_PATH__"), EscapeScriptString(Fixture.RootComponentPath));
		return Script;
	}

	bool ExecuteFunctionExpectingException(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		const TCHAR* ContextLabel,
		FString& OutExceptionString)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

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

		const int32 PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
				PrepareResult,
				static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		const int32 ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should fail with a script exception"), ContextLabel),
				ExecuteResult,
				static_cast<int32>(asEXECUTION_EXCEPTION)))
		{
			return false;
		}

		OutExceptionString = Context->GetExceptionString() != nullptr ? UTF8_TO_TCHAR(Context->GetExceptionString()) : TEXT("");
		return true;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptSceneComponentChildBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSceneComponentChildQueryCompatBindingsTest,
	"Angelscript.TestModule.Bindings.SceneComponentChildQueryCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSceneComponentChildQueryErrorBindingsTest,
	"Angelscript.TestModule.Bindings.SceneComponentChildQueryErrorPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptSceneComponentChildQueryCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASSceneComponentChildQueryCompat"));
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& HostActor = Spawner.SpawnActor<AActor>();
	FSceneComponentChildFixture Fixture = CreateFixture(*this, HostActor);
	if (!VerifyNativeFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		SceneComponentChildCompatModuleName,
		BuildCompatScript(Fixture));
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
		TEXT("Scene component child-query bindings should preserve direct-child lookup, recursive filtering, and typed out-array semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptSceneComponentChildQueryErrorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	AddExpectedError(TEXT("ASSceneComponentChildQueryErrors"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("void TriggerWrongArrayElementType()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerMismatchedQueryClass()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("GetChildrenComponentsByClass must take a TArray of scene components as its out argument."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Class specified to GetChildrenComponentsByClass is not a child of array element class."), EAutomationExpectedErrorFlags::Contains, 1);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASSceneComponentChildQueryErrors"));
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& HostActor = Spawner.SpawnActor<AActor>();
	FSceneComponentChildFixture Fixture = CreateFixture(*this, HostActor);
	if (!VerifyNativeFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		SceneComponentChildErrorModuleName,
		BuildErrorScript(Fixture));
	if (Module == nullptr)
	{
		return false;
	}

	FString WrongArrayException;
	if (!ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerWrongArrayElementType()"),
			TEXT("SceneComponentChildBindings.WrongArrayElementType"),
			WrongArrayException))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GetChildrenComponentsByClass should reject out arrays whose element type is not a scene component"),
		WrongArrayException,
		FString(TEXT("GetChildrenComponentsByClass must take a TArray of scene components as its out argument.")));

	FString MismatchedClassException;
	if (!ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerMismatchedQueryClass()"),
			TEXT("SceneComponentChildBindings.MismatchedQueryClass"),
			MismatchedClassException))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GetChildrenComponentsByClass should reject query classes that are not children of the requested out-array element type"),
		MismatchedClassException,
		FString(TEXT("Class specified to GetChildrenComponentsByClass is not a child of array element class.")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
