#include "../Shared/AngelscriptScenarioTestUtils.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace AngelscriptTest_Bindings_AngelscriptActorGlobalQueryBindingsTests_Private
{
	static constexpr TCHAR ActorGlobalQueryCompatModuleName[] = TEXT("ASActorGlobalQueryCompat");
	static constexpr TCHAR ActorGlobalQueryErrorModuleName[] = TEXT("ASActorGlobalQueryErrors");
	static constexpr TCHAR QueryActorClassName[] = TEXT("AActorGlobalQueryProbeActor");
	static constexpr TCHAR QueryPawnClassName[] = TEXT("AActorGlobalQueryProbePawn");
	static constexpr TCHAR QueryIdPropertyName[] = TEXT("QueryId");
	static constexpr TCHAR QueryTagName[] = TEXT("QueryTag");
	static constexpr int32 TaggedActorQueryId = 11;
	static constexpr int32 UntaggedActorQueryId = 22;
	static constexpr int32 TaggedPawnQueryId = 101;
	static constexpr int32 UntaggedPawnQueryId = 202;

	struct FActorGlobalQueryFixture
	{
		AActor* ContextActor = nullptr;
		AActor* TaggedActor = nullptr;
		AActor* UntaggedActor = nullptr;
		APawn* TaggedPawn = nullptr;
		APawn* UntaggedPawn = nullptr;
		UWorld* World = nullptr;
	};

	void ReplaceToken(FString& Source, const TCHAR* Token, const TCHAR* Replacement)
	{
		Source.ReplaceInline(Token, Replacement, ESearchCase::CaseSensitive);
	}

	FString BuildQueryScriptSource()
	{
		FString Script = TEXT(R"AS(
UCLASS()
class __QUERY_ACTOR_CLASS__ : AActor
{
	UPROPERTY()
	int QueryId = 0;
}

UCLASS()
class __QUERY_PAWN_CLASS__ : APawn
{
	UPROPERTY()
	int QueryId = 0;
}

int VerifyActorGlobalQueries()
{
	TArray<__QUERY_ACTOR_CLASS__> QueryActors;
	GetAllActorsOfClass(QueryActors);
	if (QueryActors.Num() != 2)
		return 2;

	bool bSawTaggedActor = false;
	bool bSawUntaggedActor = false;
	for (int32 Index = 0; Index < QueryActors.Num(); ++Index)
	{
		if (QueryActors[Index] == null)
			return 4;
		if (QueryActors[Index].QueryId == 11)
			bSawTaggedActor = true;
		else if (QueryActors[Index].QueryId == 22)
			bSawUntaggedActor = true;
		else
			return 6;
	}
	if (!bSawTaggedActor || !bSawUntaggedActor)
		return 8;

	TArray<AActor> ExplicitActors;
	GetAllActorsOfClass(__QUERY_ACTOR_CLASS__::StaticClass(), ExplicitActors);
	if (ExplicitActors.Num() != 2)
		return 10;

	bool bSawExplicitTaggedActor = false;
	bool bSawExplicitUntaggedActor = false;
	for (int32 Index = 0; Index < ExplicitActors.Num(); ++Index)
	{
		__QUERY_ACTOR_CLASS__ QueryActor = Cast<__QUERY_ACTOR_CLASS__>(ExplicitActors[Index]);
		if (QueryActor == null)
			return 12;
		if (QueryActor.QueryId == 11)
			bSawExplicitTaggedActor = true;
		else if (QueryActor.QueryId == 22)
			bSawExplicitUntaggedActor = true;
		else
			return 14;
	}
	if (!bSawExplicitTaggedActor || !bSawExplicitUntaggedActor)
		return 16;

	TArray<AActor> ExplicitPawns;
	GetAllActorsOfClass(__QUERY_PAWN_CLASS__::StaticClass(), ExplicitPawns);
	if (ExplicitPawns.Num() != 2)
		return 18;

	bool bSawTaggedPawn = false;
	bool bSawUntaggedPawn = false;
	for (int32 Index = 0; Index < ExplicitPawns.Num(); ++Index)
	{
		__QUERY_PAWN_CLASS__ QueryPawn = Cast<__QUERY_PAWN_CLASS__>(ExplicitPawns[Index]);
		if (QueryPawn == null)
			return 20;
		if (QueryPawn.QueryId == 101)
			bSawTaggedPawn = true;
		else if (QueryPawn.QueryId == 202)
			bSawUntaggedPawn = true;
		else
			return 22;
	}
	if (!bSawTaggedPawn || !bSawUntaggedPawn)
		return 24;

	TArray<__QUERY_ACTOR_CLASS__> TaggedActors;
	GetAllActorsOfClassWithTag(n"QueryTag", TaggedActors);
	if (TaggedActors.Num() != 1)
		return 26;
	if (TaggedActors[0] == null || TaggedActors[0].QueryId != 11)
		return 28;

	TArray<__QUERY_PAWN_CLASS__> TaggedPawns;
	GetAllActorsOfClassWithTag(n"QueryTag", TaggedPawns);
	if (TaggedPawns.Num() != 1)
		return 30;
	if (TaggedPawns[0] == null || TaggedPawns[0].QueryId != 101)
		return 32;

	return 1;
}

void TriggerWrongArrayElementType()
{
	TArray<UObject> WrongActors;
	GetAllActorsOfClass(WrongActors);
}

void TriggerNullClass()
{
	TArray<AActor> Actors;
	UObject NullObject = nullptr;
	UClass NullClass = Cast<UClass>(NullObject);
	GetAllActorsOfClass(NullClass, Actors);
}

void TriggerMismatchedQueryClass()
{
	TArray<__QUERY_ACTOR_CLASS__> WrongActors;
	GetAllActorsOfClass(__QUERY_PAWN_CLASS__::StaticClass(), WrongActors);
}
)AS");

		ReplaceToken(Script, TEXT("__QUERY_ACTOR_CLASS__"), QueryActorClassName);
		ReplaceToken(Script, TEXT("__QUERY_PAWN_CLASS__"), QueryPawnClassName);
		return Script;
	}

	bool CompileQueryModule(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const TCHAR* ModuleName,
		UClass*& OutActorClass,
		UClass*& OutPawnClass)
	{
		OutActorClass = nullptr;
		OutPawnClass = nullptr;

		if (!Test.TestTrue(
				*FString::Printf(TEXT("%s should compile the actor-query probe module"), ModuleName),
				CompileAnnotatedModuleFromMemory(
					&Engine,
					FName(ModuleName),
					FString(ModuleName) + TEXT(".as"),
					BuildQueryScriptSource())))
		{
			return false;
		}

		OutActorClass = FindGeneratedClass(&Engine, FName(QueryActorClassName));
		OutPawnClass = FindGeneratedClass(&Engine, FName(QueryPawnClassName));
		return Test.TestNotNull(
				*FString::Printf(TEXT("%s should publish the scripted actor query class"), ModuleName),
				OutActorClass)
			&& Test.TestNotNull(
				*FString::Printf(TEXT("%s should publish the scripted pawn query class"), ModuleName),
				OutPawnClass);
	}

	bool SetQueryId(
		FAutomationTestBase& Test,
		UObject& Object,
		int32 QueryId)
	{
		FIntProperty* QueryIdProperty = FindFProperty<FIntProperty>(Object.GetClass(), QueryIdPropertyName);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("ActorGlobalQuery fixture should expose reflected property '%s' on '%s'"), QueryIdPropertyName, *Object.GetName()),
				QueryIdProperty))
		{
			return false;
		}

		QueryIdProperty->SetPropertyValue_InContainer(&Object, QueryId);
		return Test.TestEqual(
			*FString::Printf(TEXT("ActorGlobalQuery fixture should persist reflected QueryId %d on '%s'"), QueryId, *Object.GetName()),
			QueryIdProperty->GetPropertyValue_InContainer(&Object),
			QueryId);
	}

	template <typename ActorType>
	ActorType* SpawnQueryActor(
		FAutomationTestBase& Test,
		FActorTestSpawner& Spawner,
		UClass* ScriptClass,
		int32 QueryId,
		bool bAddQueryTag)
	{
		ActorType* Actor = SpawnScriptActor<ActorType>(Test, Spawner, ScriptClass);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("ActorGlobalQuery fixture should spawn '%s'"), *ScriptClass->GetName()),
				Actor))
		{
			return nullptr;
		}

		if (!SetQueryId(Test, *Actor, QueryId))
		{
			return nullptr;
		}

		if (bAddQueryTag)
		{
			Actor->Tags.Add(FName(QueryTagName));
		}

		return Actor;
	}

	FActorGlobalQueryFixture CreateFixture(
		FAutomationTestBase& Test,
		FActorTestSpawner& Spawner,
		UClass* ActorClass,
		UClass* PawnClass)
	{
		FActorGlobalQueryFixture Fixture;
		Fixture.TaggedActor = SpawnQueryActor<AActor>(Test, Spawner, ActorClass, TaggedActorQueryId, true);
		if (Fixture.TaggedActor == nullptr)
		{
			return Fixture;
		}

		Fixture.UntaggedActor = SpawnQueryActor<AActor>(Test, Spawner, ActorClass, UntaggedActorQueryId, false);
		if (Fixture.UntaggedActor == nullptr)
		{
			return Fixture;
		}

		Fixture.TaggedPawn = SpawnQueryActor<APawn>(Test, Spawner, PawnClass, TaggedPawnQueryId, true);
		if (Fixture.TaggedPawn == nullptr)
		{
			return Fixture;
		}

		Fixture.UntaggedPawn = SpawnQueryActor<APawn>(Test, Spawner, PawnClass, UntaggedPawnQueryId, false);
		if (Fixture.UntaggedPawn == nullptr)
		{
			return Fixture;
		}

		Fixture.ContextActor = Fixture.TaggedActor;
		Fixture.World = Fixture.ContextActor != nullptr ? Fixture.ContextActor->GetWorld() : nullptr;
		return Fixture;
	}

	bool VerifyNativeFixtureBaseline(
		FAutomationTestBase& Test,
		const FActorGlobalQueryFixture& Fixture,
		UClass* ActorClass,
		UClass* PawnClass)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(TEXT("ActorGlobalQuery native baseline should keep the context actor alive"), Fixture.ContextActor);
		bPassed &= Test.TestNotNull(TEXT("ActorGlobalQuery native baseline should keep the tagged actor alive"), Fixture.TaggedActor);
		bPassed &= Test.TestNotNull(TEXT("ActorGlobalQuery native baseline should keep the untagged actor alive"), Fixture.UntaggedActor);
		bPassed &= Test.TestNotNull(TEXT("ActorGlobalQuery native baseline should keep the tagged pawn alive"), Fixture.TaggedPawn);
		bPassed &= Test.TestNotNull(TEXT("ActorGlobalQuery native baseline should keep the untagged pawn alive"), Fixture.UntaggedPawn);
		bPassed &= Test.TestNotNull(TEXT("ActorGlobalQuery native baseline should expose a world"), Fixture.World);
		if (!bPassed)
		{
			return false;
		}

		TArray<AActor*> NativeActors;
		UGameplayStatics::GetAllActorsOfClass(Fixture.World, ActorClass, NativeActors);
		bPassed &= Test.TestEqual(
			TEXT("ActorGlobalQuery native baseline should report exactly two scripted actor instances for the actor class"),
			NativeActors.Num(),
			2);
		bPassed &= Test.TestTrue(
			TEXT("ActorGlobalQuery native baseline should include the tagged scripted actor in the actor-class query"),
			NativeActors.Contains(Fixture.TaggedActor));
		bPassed &= Test.TestTrue(
			TEXT("ActorGlobalQuery native baseline should include the untagged scripted actor in the actor-class query"),
			NativeActors.Contains(Fixture.UntaggedActor));

		TArray<AActor*> NativePawns;
		UGameplayStatics::GetAllActorsOfClass(Fixture.World, PawnClass, NativePawns);
		bPassed &= Test.TestEqual(
			TEXT("ActorGlobalQuery native baseline should report exactly two scripted pawn instances for the pawn class"),
			NativePawns.Num(),
			2);
		bPassed &= Test.TestTrue(
			TEXT("ActorGlobalQuery native baseline should include the tagged scripted pawn in the pawn-class query"),
			NativePawns.Contains(Fixture.TaggedPawn));
		bPassed &= Test.TestTrue(
			TEXT("ActorGlobalQuery native baseline should include the untagged scripted pawn in the pawn-class query"),
			NativePawns.Contains(Fixture.UntaggedPawn));

		TArray<AActor*> TaggedActors;
		UGameplayStatics::GetAllActorsOfClassWithTag(Fixture.World, ActorClass, FName(QueryTagName), TaggedActors);
		bPassed &= Test.TestEqual(
			TEXT("ActorGlobalQuery native baseline should report exactly one tagged scripted actor"),
			TaggedActors.Num(),
			1);
		bPassed &= Test.TestTrue(
			TEXT("ActorGlobalQuery native baseline should return the tagged scripted actor for the actor tag query"),
			TaggedActors.Num() == 1 && TaggedActors[0] == Fixture.TaggedActor);

		TArray<AActor*> TaggedPawns;
		UGameplayStatics::GetAllActorsOfClassWithTag(Fixture.World, PawnClass, FName(QueryTagName), TaggedPawns);
		bPassed &= Test.TestEqual(
			TEXT("ActorGlobalQuery native baseline should report exactly one tagged scripted pawn"),
			TaggedPawns.Num(),
			1);
		bPassed &= Test.TestTrue(
			TEXT("ActorGlobalQuery native baseline should return the tagged scripted pawn for the pawn tag query"),
			TaggedPawns.Num() == 1 && TaggedPawns[0] == Fixture.TaggedPawn);
		return bPassed;
	}

	asIScriptModule* FindCompiledModule(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const TCHAR* ModuleName)
	{
		TSharedPtr<FAngelscriptModuleDesc> ModuleDesc = Engine.GetModuleByModuleName(ModuleName);
		if (!Test.TestTrue(
				*FString::Printf(TEXT("%s should resolve a compiled module descriptor"), ModuleName),
				ModuleDesc.IsValid()))
		{
			return nullptr;
		}

		asIScriptModule* Module = ModuleDesc->ScriptModule;
		Test.TestNotNull(
			*FString::Printf(TEXT("%s should expose a backing asIScriptModule"), ModuleName),
			Module);
		return Module;
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

using namespace AngelscriptTest_Bindings_AngelscriptActorGlobalQueryBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptActorGlobalQueryCompatBindingsTest,
	"Angelscript.TestModule.Bindings.ActorGlobalQueryCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptActorGlobalQueryErrorBindingsTest,
	"Angelscript.TestModule.Bindings.ActorGlobalQueryErrorPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptActorGlobalQueryCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ActorGlobalQueryCompatModuleName);
		ResetSharedCloneEngine(Engine);
	};

	UClass* ActorClass = nullptr;
	UClass* PawnClass = nullptr;
	if (!CompileQueryModule(*this, Engine, ActorGlobalQueryCompatModuleName, ActorClass, PawnClass))
	{
		return false;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	FActorGlobalQueryFixture Fixture = CreateFixture(*this, Spawner, ActorClass, PawnClass);
	if (!VerifyNativeFixtureBaseline(*this, Fixture, ActorClass, PawnClass))
	{
		return false;
	}

	FScopedTestWorldContextScope WorldContextScope(Fixture.ContextActor);

	int32 Result = INDEX_NONE;
	if (!TestTrue(
			TEXT("ActorGlobalQueryCompat should execute its verification entry point"),
			ExecuteIntFunction(
				&Engine,
				FName(ActorGlobalQueryCompatModuleName),
				TEXT("int VerifyActorGlobalQueries()"),
				Result)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("AActor global-query bindings should preserve typed class inference, explicit class filtering, and tag-based query parity"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptActorGlobalQueryErrorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	AddExpectedError(TEXT("ASActorGlobalQueryErrors"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("void TriggerWrongArrayElementType()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerNullClass()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("void TriggerMismatchedQueryClass()"), EAutomationExpectedErrorFlags::Contains, 0, false);
	AddExpectedError(TEXT("GetAllActors must take a TArray of actors as its out argument."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Actor class was null."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Class specified to GetAllActorsOfClass is not a child of array element class."), EAutomationExpectedErrorFlags::Contains, 1);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ActorGlobalQueryErrorModuleName);
		ResetSharedCloneEngine(Engine);
	};

	UClass* ActorClass = nullptr;
	UClass* PawnClass = nullptr;
	if (!CompileQueryModule(*this, Engine, ActorGlobalQueryErrorModuleName, ActorClass, PawnClass))
	{
		return false;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& ContextActor = Spawner.SpawnActor<AActor>();
	FScopedTestWorldContextScope WorldContextScope(&ContextActor);

	asIScriptModule* Module = FindCompiledModule(*this, Engine, ActorGlobalQueryErrorModuleName);
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
			TEXT("ActorGlobalQueryBindings.WrongArrayElementType"),
			WrongArrayException))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GetAllActorsOfClass should reject out arrays whose element type is not an actor"),
		WrongArrayException,
		FString(TEXT("GetAllActors must take a TArray of actors as its out argument.")));

	FString NullClassException;
	if (!ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerNullClass()"),
			TEXT("ActorGlobalQueryBindings.NullClass"),
			NullClassException))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GetAllActorsOfClass should reject null class filters"),
		NullClassException,
		FString(TEXT("Actor class was null.")));

	FString MismatchedClassException;
	if (!ExecuteFunctionExpectingException(
			*this,
			Engine,
			*Module,
			TEXT("void TriggerMismatchedQueryClass()"),
			TEXT("ActorGlobalQueryBindings.MismatchedQueryClass"),
			MismatchedClassException))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GetAllActorsOfClass should reject explicit query classes that are not children of the requested out-array element type"),
		MismatchedClassException,
		FString(TEXT("Class specified to GetAllActorsOfClass is not a child of array element class.")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
