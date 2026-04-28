#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorComponent.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/TimelineComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace AngelscriptTestSupport;
namespace AngelscriptTest_Bindings_AngelscriptActorComponentQueryBindingsTests_Private
{
	static constexpr ANSICHAR ActorComponentQueryCompatModuleName[] = "ASActorComponentQueryCompat";
	static constexpr ANSICHAR ActorComponentQueryErrorModuleName[] = "ASActorComponentQueryErrors";
	static constexpr TCHAR RootComponentName[] = TEXT("ActorComponentQueryRoot");
	static constexpr TCHAR SphereComponentName[] = TEXT("ActorComponentQuerySphere");
	static constexpr TCHAR PlainComponentName[] = TEXT("ActorComponentQueryPlain");
	struct FActorComponentQueryFixture
	{
		AActor* OwnerActor = nullptr;
		USceneComponent* RootComponent = nullptr;
		USphereComponent* SphereComponent = nullptr;
		UActorComponent* PlainComponent = nullptr;
		FString ActorPath;
		FString RootComponentPath;
		FString SphereComponentPath;
		FString PlainComponentPath;
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
				*FString::Printf(TEXT("ActorComponentQuery fixture should create scene component '%s'"), ComponentName),
				Component))
		{
			return nullptr;
		}
		Owner.AddOwnedComponent(Component);
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

	UActorComponent* CreatePlainComponent(
		FAutomationTestBase& Test,
		AActor& Owner,
		const TCHAR* ComponentName)
	{
		UActorComponent* Component = NewObject<UTimelineComponent>(&Owner, UTimelineComponent::StaticClass(), FName(ComponentName));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("ActorComponentQuery fixture should create actor component '%s'"), ComponentName),
				Component))
		{
			return nullptr;
		}
		Owner.AddOwnedComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	FActorComponentQueryFixture CreateFixture(FAutomationTestBase& Test, AActor& OwnerActor)
	{
		FActorComponentQueryFixture Fixture;
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
		Fixture.SphereComponent = Cast<USphereComponent>(CreateSceneComponent(
			Test,
			OwnerActor,
			USphereComponent::StaticClass(),
			SphereComponentName,
			Fixture.RootComponent));
		if (!Test.TestNotNull(
				TEXT("ActorComponentQuery fixture should cast the sphere component to USphereComponent"),
				Fixture.SphereComponent))
		{
			return Fixture;
		}
		Fixture.PlainComponent = CreatePlainComponent(Test, OwnerActor, PlainComponentName);
		if (Fixture.PlainComponent == nullptr)
		{
			return Fixture;
		}

		Fixture.SphereComponent->SetSphereRadius(32.0f, false);
		Fixture.ActorPath = OwnerActor.GetPathName();
		Fixture.RootComponentPath = Fixture.RootComponent->GetPathName();
		Fixture.SphereComponentPath = Fixture.SphereComponent->GetPathName();
		Fixture.PlainComponentPath = Fixture.PlainComponent->GetPathName();
		return Fixture;
	}

	bool VerifyNativeFixtureBaseline(
		FAutomationTestBase& Test,
		const FActorComponentQueryFixture& Fixture)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			TEXT("ActorComponentQuery native baseline should keep the owner actor alive"),
			Fixture.OwnerActor);
		bPassed &= Test.TestNotNull(
			TEXT("ActorComponentQuery native baseline should keep the root scene component alive"),
			Fixture.RootComponent);
		bPassed &= Test.TestNotNull(
			TEXT("ActorComponentQuery native baseline should keep the sphere component alive"),
			Fixture.SphereComponent);
		bPassed &= Test.TestNotNull(
			TEXT("ActorComponentQuery native baseline should keep the plain actor component alive"),
			Fixture.PlainComponent);
		bPassed &= Test.TestTrue(
			TEXT("ActorComponentQuery fixture should expose non-empty object paths"),
			!Fixture.ActorPath.IsEmpty()
				&& !Fixture.RootComponentPath.IsEmpty()
				&& !Fixture.SphereComponentPath.IsEmpty()
				&& !Fixture.PlainComponentPath.IsEmpty());
		bPassed &= Test.TestEqual(TEXT("ActorComponentQuery fixture should keep the spawned actor rooted at the dedicated root scene component"), Fixture.OwnerActor != nullptr ? Fixture.OwnerActor->GetRootComponent() : nullptr, Fixture.RootComponent);
		bPassed &= Test.TestEqual(TEXT("ActorComponentQuery fixture should attach the sphere component under the root scene component"), Fixture.SphereComponent != nullptr ? Fixture.SphereComponent->GetAttachParent() : nullptr, Fixture.RootComponent);

		if (Fixture.OwnerActor != nullptr)
		{
			const TSet<UActorComponent*>& NativeComponents = Fixture.OwnerActor->GetComponents();
			bPassed &= Test.TestEqual(
				TEXT("ActorComponentQuery native baseline should expose exactly three owned components"),
				NativeComponents.Num(),
				3);
			bPassed &= Test.TestTrue(
				TEXT("ActorComponentQuery native baseline should contain the root scene component"),
				NativeComponents.Contains(Fixture.RootComponent));
			bPassed &= Test.TestTrue(
				TEXT("ActorComponentQuery native baseline should contain the sphere component"),
				NativeComponents.Contains(Fixture.SphereComponent));
			bPassed &= Test.TestTrue(
				TEXT("ActorComponentQuery native baseline should contain the plain actor component"),
				NativeComponents.Contains(Fixture.PlainComponent));
		}

		return bPassed;
	}

	FString BuildCompatScript(const FActorComponentQueryFixture& Fixture)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	AActor Host = Cast<AActor>(FindObject("__ACTOR_PATH__"));
	UActorComponent RootActorComponent = Cast<UActorComponent>(FindObject("__ROOT_COMPONENT_PATH__"));
	UActorComponent SphereActorComponent = Cast<UActorComponent>(FindObject("__SPHERE_COMPONENT_PATH__"));
	UActorComponent PlainActorComponent = Cast<UActorComponent>(FindObject("__PLAIN_COMPONENT_PATH__"));
	USceneComponent RootSceneComponent = Cast<USceneComponent>(FindObject("__ROOT_COMPONENT_PATH__"));
	USceneComponent SphereSceneComponent = Cast<USceneComponent>(FindObject("__SPHERE_COMPONENT_PATH__"));
	USphereComponent SphereComponent = Cast<USphereComponent>(FindObject("__SPHERE_COMPONENT_PATH__"));
	if (Host == null
		|| RootActorComponent == null
		|| SphereActorComponent == null
		|| PlainActorComponent == null
		|| RootSceneComponent == null
		|| SphereSceneComponent == null
		|| SphereComponent == null)
	{
		return 2;
	}

	TArray<UActorComponent> AllComponents;
	Host.GetComponentsByClass(AllComponents);
	if (AllComponents.Num() != 3)
		return 4;

	bool bSawRootActorComponent = false;
	bool bSawSphereActorComponent = false;
	bool bSawPlainActorComponent = false;
	for (int32 Index = 0; Index < AllComponents.Num(); ++Index)
	{
		if (AllComponents[Index] == RootActorComponent)
			bSawRootActorComponent = true;
		else if (AllComponents[Index] == SphereActorComponent)
			bSawSphereActorComponent = true;
		else if (AllComponents[Index] == PlainActorComponent)
			bSawPlainActorComponent = true;
		else
			return 6;
	}
	if (!bSawRootActorComponent || !bSawSphereActorComponent || !bSawPlainActorComponent)
		return 8;

	TArray<USceneComponent> SceneComponents;
	Host.GetComponentsByClass(SceneComponents);
	if (SceneComponents.Num() != 2)
		return 10;

	bool bSawRootSceneComponent = false;
	bool bSawSphereSceneComponent = false;
	for (int32 Index = 0; Index < SceneComponents.Num(); ++Index)
	{
		if (SceneComponents[Index] == RootSceneComponent)
			bSawRootSceneComponent = true;
		else if (SceneComponents[Index] == SphereSceneComponent)
			bSawSphereSceneComponent = true;
		else
			return 12;
	}
	if (!bSawRootSceneComponent || !bSawSphereSceneComponent)
		return 14;

	TArray<UActorComponent> SceneFilteredAsActorComponents;
	Host.GetComponentsByClass(USceneComponent::StaticClass(), SceneFilteredAsActorComponents);
	if (SceneFilteredAsActorComponents.Num() != 2)
		return 16;

	bool bSawFilteredRoot = false;
	bool bSawFilteredSphere = false;
	for (int32 Index = 0; Index < SceneFilteredAsActorComponents.Num(); ++Index)
	{
		if (SceneFilteredAsActorComponents[Index] == RootActorComponent)
			bSawFilteredRoot = true;
		else if (SceneFilteredAsActorComponents[Index] == SphereActorComponent)
			bSawFilteredSphere = true;
		else
			return 18;
	}
	if (!bSawFilteredRoot || !bSawFilteredSphere)
		return 20;

	TArray<USphereComponent> SphereFilteredComponents;
	Host.GetComponentsByClass(USphereComponent::StaticClass(), SphereFilteredComponents);
	if (SphereFilteredComponents.Num() != 1)
		return 22;
	if (SphereFilteredComponents[0] != SphereComponent)
		return 24;

	TArray<USceneComponent> SphereFilteredAsSceneComponents;
	Host.GetComponentsByClass(USphereComponent::StaticClass(), SphereFilteredAsSceneComponents);
	if (SphereFilteredAsSceneComponents.Num() != 1)
		return 26;
	if (SphereFilteredAsSceneComponents[0] != SphereSceneComponent)
		return 28;

	return 1;
}
)AS");

		ReplaceToken(Script, TEXT("__ACTOR_PATH__"), EscapeScriptString(Fixture.ActorPath));
		ReplaceToken(Script, TEXT("__ROOT_COMPONENT_PATH__"), EscapeScriptString(Fixture.RootComponentPath));
		ReplaceToken(Script, TEXT("__SPHERE_COMPONENT_PATH__"), EscapeScriptString(Fixture.SphereComponentPath));
		ReplaceToken(Script, TEXT("__PLAIN_COMPONENT_PATH__"), EscapeScriptString(Fixture.PlainComponentPath));
		return Script;
	}

	FString BuildErrorScript(const FActorComponentQueryFixture& Fixture)
	{
		FString Script = TEXT(R"AS(
void TriggerWrongArrayElementType()
{
	AActor Host = Cast<AActor>(FindObject("__ACTOR_PATH__"));
	if (Host == null)
		return;

	TArray<AActor> WrongComponents;
	Host.GetComponentsByClass(WrongComponents);
}

void TriggerMismatchedQueryClass()
{
	AActor Host = Cast<AActor>(FindObject("__ACTOR_PATH__"));
	if (Host == null)
		return;

	TArray<USphereComponent> WrongComponents;
	Host.GetComponentsByClass(USceneComponent::StaticClass(), WrongComponents);
}
)AS");

		ReplaceToken(Script, TEXT("__ACTOR_PATH__"), EscapeScriptString(Fixture.ActorPath));
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

using namespace AngelscriptTest_Bindings_AngelscriptActorComponentQueryBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptActorComponentQueryCompatBindingsTest,
	"Angelscript.TestModule.Bindings.ActorComponentQueryCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptActorComponentQueryErrorBindingsTest,
	"Angelscript.TestModule.Bindings.ActorComponentQueryErrorPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptActorComponentQueryCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASActorComponentQueryCompat"));
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& HostActor = Spawner.SpawnActor<AActor>();
	FActorComponentQueryFixture Fixture = CreateFixture(*this, HostActor);
	if (!VerifyNativeFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ActorComponentQueryCompatModuleName,
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
		TEXT("AActor::GetComponentsByClass bindings should preserve typed array collection and explicit class filtering semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptActorComponentQueryErrorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	AddExpectedError(TEXT("ASActorComponentQueryErrors"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("void TriggerWrongArrayElementType()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerMismatchedQueryClass()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("GetComponentsByClass must take a TArray of components as its out argument."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Class specified to GetComponentsByClass is not a child of array element class."), EAutomationExpectedErrorFlags::Contains, 1);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASActorComponentQueryErrors"));
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& HostActor = Spawner.SpawnActor<AActor>();
	FActorComponentQueryFixture Fixture = CreateFixture(*this, HostActor);
	if (!VerifyNativeFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ActorComponentQueryErrorModuleName,
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
			TEXT("ActorComponentQueryBindings.WrongArrayElementType"),
			WrongArrayException))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GetComponentsByClass should reject out arrays whose element type is not a component"),
		WrongArrayException,
		FString(TEXT("GetComponentsByClass must take a TArray of components as its out argument.")));

	FString MismatchedClassException;
	if (!ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerMismatchedQueryClass()"),
			TEXT("ActorComponentQueryBindings.MismatchedQueryClass"),
			MismatchedClassException))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GetComponentsByClass should reject query classes that are not children of the requested out-array element type"),
		MismatchedClassException,
		FString(TEXT("Class specified to GetComponentsByClass is not a child of array element class.")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
