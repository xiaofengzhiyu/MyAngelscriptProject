#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptActorInstigatorBindingsTests_Private
{
	static constexpr ANSICHAR ActorInstigatorModuleName[] = "ASActorInstigatorCompat";

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	bool ExecuteIntFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		int32& OutResult)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should create an execution context"), ContextLabel), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		if (!BindArguments(*Context))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}

	struct FActorInstigatorFixture
	{
		APlayerController* InstigatorController = nullptr;
		APawn* InstigatorPawn = nullptr;
		AActor* InstigatedActor = nullptr;
		AActor* PlainActor = nullptr;
	};

	FActorInstigatorFixture CreateFixture(FAutomationTestBase& Test, FActorTestSpawner& Spawner)
	{
		FActorInstigatorFixture Fixture;
		Fixture.InstigatorController = Spawner.GetWorld().SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("ActorInstigator fixture should spawn a transient player controller"), Fixture.InstigatorController))
		{
			return Fixture;
		}

		Fixture.InstigatorPawn = Spawner.GetWorld().SpawnActor<APawn>();
		if (!Test.TestNotNull(TEXT("ActorInstigator fixture should spawn a transient pawn"), Fixture.InstigatorPawn))
		{
			return Fixture;
		}

		Fixture.InstigatedActor = &Spawner.SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("ActorInstigator fixture should spawn the actor that carries an instigator"), Fixture.InstigatedActor))
		{
			return Fixture;
		}

		Fixture.PlainActor = &Spawner.SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("ActorInstigator fixture should spawn the actor without an instigator"), Fixture.PlainActor))
		{
			return Fixture;
		}

		Fixture.InstigatorController->Possess(Fixture.InstigatorPawn);
		Fixture.InstigatedActor->SetInstigator(Fixture.InstigatorPawn);
		return Fixture;
	}

	bool VerifyNativeFixtureBaseline(
		FAutomationTestBase& Test,
		const FActorInstigatorFixture& Fixture)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(TEXT("ActorInstigator native baseline should keep the player controller alive"), Fixture.InstigatorController);
		bPassed &= Test.TestNotNull(TEXT("ActorInstigator native baseline should keep the instigator pawn alive"), Fixture.InstigatorPawn);
		bPassed &= Test.TestNotNull(TEXT("ActorInstigator native baseline should keep the instigated actor alive"), Fixture.InstigatedActor);
		bPassed &= Test.TestNotNull(TEXT("ActorInstigator native baseline should keep the plain actor alive"), Fixture.PlainActor);
		if (!bPassed)
		{
			return false;
		}

		bPassed &= Test.TestTrue(
			TEXT("ActorInstigator native baseline should possess the pawn with the spawned player controller"),
			Fixture.InstigatorPawn->GetController() == Fixture.InstigatorController);
		bPassed &= Test.TestTrue(
			TEXT("ActorInstigator native baseline should attach the pawn as the instigator on the target actor"),
			Fixture.InstigatedActor->GetInstigator() == Fixture.InstigatorPawn);
		bPassed &= Test.TestTrue(
			TEXT("ActorInstigator native baseline should resolve the instigator controller through the pawn"),
			Fixture.InstigatedActor->GetInstigatorController() == Fixture.InstigatorController);
		bPassed &= Test.TestTrue(
			TEXT("ActorInstigator native baseline should keep the plain actor without an instigator"),
			Fixture.PlainActor->GetInstigator() == nullptr);
		bPassed &= Test.TestTrue(
			TEXT("ActorInstigator native baseline should keep the plain actor without an instigator controller"),
			Fixture.PlainActor->GetInstigatorController() == nullptr);
		return bPassed;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptActorInstigatorBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptActorInstigatorCompatBindingsTest,
	"Angelscript.TestModule.Bindings.ActorInstigatorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptActorInstigatorCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASActorInstigatorCompat"));
		ResetSharedCloneEngine(Engine);
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	const FActorInstigatorFixture Fixture = CreateFixture(*this, Spawner);
	if (!VerifyNativeFixtureBaseline(*this, Fixture))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ActorInstigatorModuleName,
		TEXT(R"AS(
int VerifyActorInstigatorCompat(AActor TargetActor, APawn ExpectedInstigator, AController ExpectedController, AActor PlainActor)
{
	if (TargetActor == null || ExpectedInstigator == null || ExpectedController == null || PlainActor == null)
		return 1;

	APawn CurrentInstigator = TargetActor.GetActorInstigator();
	if (CurrentInstigator == null)
		return 2;
	if (CurrentInstigator != ExpectedInstigator)
		return 4;

	AController CurrentController = TargetActor.GetActorInstigatorController();
	if (CurrentController == null)
		return 8;
	if (CurrentController != ExpectedController)
		return 16;

	if (PlainActor.GetActorInstigator() != null)
		return 32;
	if (PlainActor.GetActorInstigatorController() != null)
		return 64;

	return 0;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	int32 ResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyActorInstigatorCompat(AActor, APawn, AController, AActor)"),
			[this, &Fixture](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, Fixture.InstigatedActor, TEXT("VerifyActorInstigatorCompat"))
					&& SetArgObjectChecked(*this, Context, 1, Fixture.InstigatorPawn, TEXT("VerifyActorInstigatorCompat"))
					&& SetArgObjectChecked(*this, Context, 2, Fixture.InstigatorController, TEXT("VerifyActorInstigatorCompat"))
					&& SetArgObjectChecked(*this, Context, 3, Fixture.PlainActor, TEXT("VerifyActorInstigatorCompat"));
			},
			TEXT("VerifyActorInstigatorCompat"),
			ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("AActor instigator bindings should preserve possessed-instigator and null-instigator runtime parity"),
		ResultMask,
		0);
	bPassed &= TestTrue(
		TEXT("GetActorInstigator should leave the native instigator pointer unchanged after the script read"),
		Fixture.InstigatedActor->GetInstigator() == Fixture.InstigatorPawn);
	bPassed &= TestTrue(
		TEXT("GetActorInstigatorController should leave the native instigator controller pointer unchanged after the script read"),
		Fixture.InstigatedActor->GetInstigatorController() == Fixture.InstigatorController);
	bPassed &= TestTrue(
		TEXT("GetActorInstigator should keep the plain actor on the native null-instigator boundary"),
		Fixture.PlainActor->GetInstigator() == nullptr);
	bPassed &= TestTrue(
		TEXT("GetActorInstigatorController should keep the plain actor on the native null-controller boundary"),
		Fixture.PlainActor->GetInstigatorController() == nullptr);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
