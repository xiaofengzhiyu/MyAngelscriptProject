#include "../Shared/AngelscriptScenarioTestUtils.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "../../AngelscriptRuntime/Core/AngelscriptGASActor.h"
#include "../../AngelscriptRuntime/Core/AngelscriptGASCharacter.h"
#include "../../AngelscriptRuntime/Core/AngelscriptGASPawn.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace AngelscriptTest_Bindings_AngelscriptGASActorBindingsTests_Private
{
	static constexpr TCHAR GASActorTypeModuleName[] = TEXT("ASGASActorTypeCompileCompat");
	static constexpr TCHAR GASActorRuntimeModuleName[] = TEXT("ASGASActorSpawnedASCCompat");
	static constexpr TCHAR GASActorClassName[] = TEXT("AGASBindingActor");
	static constexpr TCHAR GASPawnClassName[] = TEXT("AGASBindingPawn");
	static constexpr TCHAR GASCharacterClassName[] = TEXT("AGASBindingCharacter");
	static constexpr int32 ExpectedASCStateMask = 7;

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	template <typename ActorType>
	bool VerifyScriptASCState(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		ActorType* Actor)
	{
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should spawn a generated GAS actor instance"), Context),
				Actor))
		{
			return false;
		}

		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s should create an ability-system component on the spawned actor"), Context),
				Actor->AbilitySystem))
		{
			return false;
		}

		int32 ScriptASCState = INDEX_NONE;
		if (!ReadPropertyValue<FIntProperty>(Test, Actor, TEXT("BeginPlayASCState"), ScriptASCState))
		{
			return false;
		}

		return Test.TestEqual(
			*FString::Printf(TEXT("%s should let script observe a non-null AbilitySystem property with the spawned actor as owner and a begun-play component lifecycle"), Context),
			ScriptASCState,
			ExpectedASCStateMask);
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptGASActorBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGASActorTypeCompileCompatBindingsTest,
	"Angelscript.TestModule.Bindings.GASActorTypeCompileCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGASActorSpawnedASCCompatBindingsTest,
	"Angelscript.TestModule.Bindings.GASActorSpawnedASCCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGASActorTypeCompileCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(GASActorTypeModuleName);
		ResetSharedCloneEngine(Engine);
	};

	FString Script = TEXT(R"AS(
UCLASS()
class __ACTOR_CLASS__ : AAngelscriptGASActor
{
	bool HasBoundAbilitySystem() const
	{
		if (AbilitySystem == null)
			return false;

		return AbilitySystem.GetOwner() == this && (AbilitySystem.HasBegunPlay() || !AbilitySystem.HasBegunPlay());
	}
}

UCLASS()
class __PAWN_CLASS__ : AAngelscriptGASPawn
{
	bool HasBoundAbilitySystem() const
	{
		if (AbilitySystem == null)
			return false;

		return AbilitySystem.GetOwner() == this && (AbilitySystem.HasBegunPlay() || !AbilitySystem.HasBegunPlay());
	}
}

UCLASS()
class __CHARACTER_CLASS__ : AAngelscriptGASCharacter
{
	bool HasBoundAbilitySystem() const
	{
		if (AbilitySystem == null)
			return false;

		return AbilitySystem.GetOwner() == this && (AbilitySystem.HasBegunPlay() || !AbilitySystem.HasBegunPlay());
	}
}

int VerifyGASActorTypeCompileCompat()
{
	AAngelscriptGASActor ActorBase;
	AAngelscriptGASPawn PawnBase;
	AAngelscriptGASCharacter CharacterBase;
	if (ActorBase != null || PawnBase != null || CharacterBase != null)
		return 10;

	if (__ACTOR_CLASS__::StaticClass() == null)
		return 20;
	if (__PAWN_CLASS__::StaticClass() == null)
		return 30;
	if (__CHARACTER_CLASS__::StaticClass() == null)
		return 40;

	__ACTOR_CLASS__ Actor;
	__PAWN_CLASS__ Pawn;
	__CHARACTER_CLASS__ Character;
	if (Actor != null || Pawn != null || Character != null)
		return 50;

	return 1;
}
)AS");
	ReplaceToken(Script, TEXT("__ACTOR_CLASS__"), GASActorClassName);
	ReplaceToken(Script, TEXT("__PAWN_CLASS__"), GASPawnClassName);
	ReplaceToken(Script, TEXT("__CHARACTER_CLASS__"), GASCharacterClassName);

	if (!TestTrue(
			TEXT("GASActorTypeCompileCompat should compile annotated GAS actor bridge types"),
			CompileAnnotatedModuleFromMemory(
				&Engine,
				FName(GASActorTypeModuleName),
				FString(GASActorTypeModuleName) + TEXT(".as"),
				Script)))
	{
		return false;
	}

	if (!TestNotNull(TEXT("GASActorTypeCompileCompat should publish the scripted GAS actor class"), FindGeneratedClass(&Engine, FName(GASActorClassName)))
		|| !TestNotNull(TEXT("GASActorTypeCompileCompat should publish the scripted GAS pawn class"), FindGeneratedClass(&Engine, FName(GASPawnClassName)))
		|| !TestNotNull(TEXT("GASActorTypeCompileCompat should publish the scripted GAS character class"), FindGeneratedClass(&Engine, FName(GASCharacterClassName))))
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!TestTrue(
			TEXT("GASActorTypeCompileCompat should execute its verification entry point"),
			ExecuteIntFunction(
				&Engine,
				FName(GASActorTypeModuleName),
				TEXT("int VerifyGASActorTypeCompileCompat()"),
				Result)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GAS actor base classes should stay declarable in script and keep their generated subclasses compilable"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptGASActorSpawnedASCCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(GASActorRuntimeModuleName);
		ResetSharedCloneEngine(Engine);
	};

	FString Script = TEXT(R"AS(
UCLASS()
class __ACTOR_CLASS__ : AAngelscriptGASActor
{
	UPROPERTY()
	int BeginPlayASCState = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		UpdateASCState();
	}

	void UpdateASCState()
	{
		if (AbilitySystem != null)
			BeginPlayASCState |= 1;

		if (AbilitySystem != null && AbilitySystem.GetOwner() == this)
			BeginPlayASCState |= 2;
		if (AbilitySystem != null && AbilitySystem.HasBegunPlay())
			BeginPlayASCState |= 4;
	}
}

UCLASS()
class __PAWN_CLASS__ : AAngelscriptGASPawn
{
	UPROPERTY()
	int BeginPlayASCState = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		UpdateASCState();
	}

	void UpdateASCState()
	{
		if (AbilitySystem != null)
			BeginPlayASCState |= 1;

		if (AbilitySystem != null && AbilitySystem.GetOwner() == this)
			BeginPlayASCState |= 2;
		if (AbilitySystem != null && AbilitySystem.HasBegunPlay())
			BeginPlayASCState |= 4;
	}
}

UCLASS()
class __CHARACTER_CLASS__ : AAngelscriptGASCharacter
{
	UPROPERTY()
	int BeginPlayASCState = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		UpdateASCState();
	}

	void UpdateASCState()
	{
		if (AbilitySystem != null)
			BeginPlayASCState |= 1;

		if (AbilitySystem != null && AbilitySystem.GetOwner() == this)
			BeginPlayASCState |= 2;
		if (AbilitySystem != null && AbilitySystem.HasBegunPlay())
			BeginPlayASCState |= 4;
	}
}
)AS");
	ReplaceToken(Script, TEXT("__ACTOR_CLASS__"), GASActorClassName);
	ReplaceToken(Script, TEXT("__PAWN_CLASS__"), GASPawnClassName);
	ReplaceToken(Script, TEXT("__CHARACTER_CLASS__"), GASCharacterClassName);

	if (!TestTrue(
			TEXT("GASActorSpawnedASCCompat should compile annotated GAS actor runtime fixtures"),
			CompileAnnotatedModuleFromMemory(
				&Engine,
				FName(GASActorRuntimeModuleName),
				FString(GASActorRuntimeModuleName) + TEXT(".as"),
				Script)))
	{
		return false;
	}

	UClass* ActorClass = FindGeneratedClass(&Engine, FName(GASActorClassName));
	UClass* PawnClass = FindGeneratedClass(&Engine, FName(GASPawnClassName));
	UClass* CharacterClass = FindGeneratedClass(&Engine, FName(GASCharacterClassName));
	if (!TestNotNull(TEXT("GASActorSpawnedASCCompat should publish the scripted GAS actor class"), ActorClass)
		|| !TestNotNull(TEXT("GASActorSpawnedASCCompat should publish the scripted GAS pawn class"), PawnClass)
		|| !TestNotNull(TEXT("GASActorSpawnedASCCompat should publish the scripted GAS character class"), CharacterClass))
	{
		return false;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AAngelscriptGASActor* Actor = SpawnScriptActor<AAngelscriptGASActor>(*this, Spawner, ActorClass);
	AAngelscriptGASPawn* Pawn = SpawnScriptActor<AAngelscriptGASPawn>(*this, Spawner, PawnClass);
	AAngelscriptGASCharacter* Character = SpawnScriptActor<AAngelscriptGASCharacter>(*this, Spawner, CharacterClass);
	if (Actor == nullptr || Pawn == nullptr || Character == nullptr)
	{
		return false;
	}

	BeginPlayActor(*Actor);
	BeginPlayActor(*Pawn);
	BeginPlayActor(*Character);

	bPassed &= VerifyScriptASCState(*this, TEXT("GAS actor runtime binding scenario"), Actor);
	bPassed &= VerifyScriptASCState(*this, TEXT("GAS pawn runtime binding scenario"), Pawn);
	bPassed &= VerifyScriptASCState(*this, TEXT("GAS character runtime binding scenario"), Character);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
