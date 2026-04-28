#include "../Shared/AngelscriptScenarioTestUtils.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptScenarioTestUtils;
using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptActorStateBindingsTests_Private
{
	static constexpr ANSICHAR ActorStateModuleName[] = "ASActorStateCompat";
	static constexpr double VectorTolerance = 0.001;
	static constexpr double RotationTolerance = 0.001;
	static constexpr float InitialTickInterval = 0.125f;
	static constexpr float ScriptTickInterval = 0.625f;

	const FVector InitialActorLocation(125.25f, -37.5f, 512.0f);
	const FRotator InitialActorRotation(-15.0f, 123.0f, 47.5f);
	const FVector InitialActorScale(1.25f, 0.5f, 2.0f);
	const FVector ScriptActorScale(2.5f, 3.0f, 0.75f);

	struct FActorStateBaseline
	{
		AActor* Actor = nullptr;
		USceneComponent* RootComponent = nullptr;
		UGameInstance* GameInstance = nullptr;
		FVector ExpectedLocation = FVector::ZeroVector;
		FRotator ExpectedRotation = FRotator::ZeroRotator;
		FVector InitialScale = FVector::OneVector;
		FVector ScriptScale = FVector::OneVector;
		float InitialTickInterval = 0.0f;
		float ScriptTickInterval = 0.0f;
		bool bExpectedInitialized = false;
		bool bExpectedBegunPlay = false;
		bool bExpectedHidden = false;
		FString ExpectedNameOrLabel;
	};

	FString FormatScriptFloatLiteral(const double Value)
	{
		FString Literal = FString::Printf(TEXT("%.9g"), Value);
		if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e")) && !Literal.Contains(TEXT("E")))
		{
			Literal += TEXT(".0");
		}

		return Literal;
	}

	FString FormatScriptVectorLiteral(const FVector& Value)
	{
		return FString::Printf(
			TEXT("FVector(%s, %s, %s)"),
			*FormatScriptFloatLiteral(Value.X),
			*FormatScriptFloatLiteral(Value.Y),
			*FormatScriptFloatLiteral(Value.Z));
	}

	FString FormatScriptRotatorLiteral(const FRotator& Value)
	{
		return FString::Printf(
			TEXT("FRotator(%s, %s, %s)"),
			*FormatScriptFloatLiteral(Value.Pitch),
			*FormatScriptFloatLiteral(Value.Yaw),
			*FormatScriptFloatLiteral(Value.Roll));
	}

	FString FormatScriptBoolLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString EscapeScriptString(const FString& Value)
	{
		return Value.ReplaceCharWithEscapedChar();
	}

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

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

	USceneComponent* CreateRootComponent(FAutomationTestBase& Test, AActor& OwnerActor)
	{
		USceneComponent* RootComponent = NewObject<USceneComponent>(&OwnerActor, USceneComponent::StaticClass(), TEXT("ActorStateRoot"));
		if (!Test.TestNotNull(TEXT("ActorState fixture should create a transient scene root"), RootComponent))
		{
			return nullptr;
		}

		OwnerActor.AddOwnedComponent(RootComponent);
		OwnerActor.SetRootComponent(RootComponent);
		RootComponent->RegisterComponent();
		return RootComponent;
	}

	FActorStateBaseline CreateBaseline(FAutomationTestBase& Test, FActorTestSpawner& Spawner)
	{
		FActorStateBaseline Baseline;
		Baseline.Actor = &Spawner.SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("ActorState fixture should spawn a transient actor"), Baseline.Actor))
		{
			return Baseline;
		}

		Baseline.RootComponent = CreateRootComponent(Test, *Baseline.Actor);
		if (Baseline.RootComponent == nullptr)
		{
			return Baseline;
		}

		Baseline.Actor->SetActorLocation(InitialActorLocation);
		Baseline.Actor->SetActorRotation(InitialActorRotation);
		Baseline.Actor->SetActorScale3D(InitialActorScale);
		Baseline.Actor->SetActorTickInterval(InitialTickInterval);
		Baseline.Actor->SetActorHiddenInGame(true);
		BeginPlayActor(*Baseline.Actor);

		Baseline.GameInstance = Baseline.Actor->GetGameInstance();
		Baseline.ExpectedLocation = Baseline.Actor->GetActorLocation();
		Baseline.ExpectedRotation = Baseline.Actor->GetActorRotation();
		Baseline.InitialScale = Baseline.Actor->GetActorScale3D();
		Baseline.ScriptScale = ScriptActorScale;
		Baseline.InitialTickInterval = Baseline.Actor->GetActorTickInterval();
		Baseline.ScriptTickInterval = ScriptTickInterval;
		Baseline.bExpectedInitialized = Baseline.Actor->IsActorInitialized();
		Baseline.bExpectedBegunPlay = Baseline.Actor->HasActorBegunPlay();
		Baseline.bExpectedHidden = Baseline.Actor->IsHidden();
		Baseline.ExpectedNameOrLabel = Baseline.Actor->GetActorNameOrLabel();
		return Baseline;
	}

	bool VerifyNativeBaseline(
		FAutomationTestBase& Test,
		const FActorStateBaseline& Baseline)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			TEXT("ActorState native baseline should keep the spawned actor alive"),
			Baseline.Actor);
		bPassed &= Test.TestNotNull(
			TEXT("ActorState native baseline should keep the transient scene root alive"),
			Baseline.RootComponent);
		bPassed &= Test.TestNotNull(
			TEXT("ActorState native baseline should expose a non-null game instance"),
			Baseline.GameInstance);
		if (!bPassed)
		{
			return false;
		}

		bPassed &= Test.TestTrue(
			TEXT("ActorState native baseline should preserve the configured location"),
			Baseline.Actor->GetActorLocation().Equals(Baseline.ExpectedLocation, VectorTolerance));
		bPassed &= Test.TestTrue(
			TEXT("ActorState native baseline should preserve the configured rotation"),
			Baseline.Actor->GetActorRotation().Equals(Baseline.ExpectedRotation, RotationTolerance));
		bPassed &= Test.TestTrue(
			TEXT("ActorState native baseline should preserve the configured scale before the script mutation"),
			Baseline.Actor->GetActorScale3D().Equals(Baseline.InitialScale, VectorTolerance));
		bPassed &= Test.TestTrue(
			TEXT("ActorState native baseline should preserve the configured tick interval before the script mutation"),
			FMath::IsNearlyEqual(Baseline.Actor->GetActorTickInterval(), Baseline.InitialTickInterval));
		bPassed &= Test.TestTrue(
			TEXT("ActorState native baseline should keep the actor hidden after the setup mutation"),
			Baseline.bExpectedHidden);
		bPassed &= Test.TestTrue(
			TEXT("ActorState native baseline should keep the actor in begun-play state after setup"),
			Baseline.bExpectedBegunPlay);
		bPassed &= Test.TestEqual(
			TEXT("ActorState native baseline should keep GetGameInstance aligned with the actor world"),
			Baseline.Actor->GetGameInstance(),
			Baseline.GameInstance);
		bPassed &= Test.TestEqual(
			TEXT("ActorState native baseline should keep the actor rooted at the transient scene component"),
			Baseline.Actor->GetRootComponent(),
			Baseline.RootComponent);
		bPassed &= Test.TestTrue(
			TEXT("ActorState native baseline should expose a non-empty actor label-or-name string"),
			!Baseline.ExpectedNameOrLabel.IsEmpty());
		return bPassed;
	}

	FString BuildCompatScript(const FActorStateBaseline& Baseline)
	{
		FString Script = TEXT(R"AS(
int VerifyActorStateCompat(AActor Actor, UGameInstance ExpectedGameInstance)
{
	if (Actor == null || ExpectedGameInstance == null)
		return 1;

	if (Actor.IsActorInitialized() != __EXPECTED_INITIALIZED__)
		return 2;
	if (Actor.HasActorBegunPlay() != __EXPECTED_BEGUN_PLAY__)
		return 4;
	if (Actor.IsHidden() != __EXPECTED_HIDDEN__)
		return 8;
	if (!Actor.GetActorLocation().Equals(__EXPECTED_LOCATION__, __VECTOR_TOLERANCE__))
		return 16;
	if (!Actor.GetActorRotation().Equals(__EXPECTED_ROTATION__, __ROTATION_TOLERANCE__))
		return 32;
	if (Actor.GetActorNameOrLabel() != "__EXPECTED_NAME__")
		return 64;
	if (Actor.GetGameInstance() != ExpectedGameInstance)
		return 128;

	Actor.SetActorScale3D(__SCRIPT_SCALE__);
	Actor.SetActorTickInterval(__SCRIPT_TICK_INTERVAL__);
	return 0;
}
)AS");

		ReplaceToken(Script, TEXT("__EXPECTED_INITIALIZED__"), FormatScriptBoolLiteral(Baseline.bExpectedInitialized));
		ReplaceToken(Script, TEXT("__EXPECTED_BEGUN_PLAY__"), FormatScriptBoolLiteral(Baseline.bExpectedBegunPlay));
		ReplaceToken(Script, TEXT("__EXPECTED_HIDDEN__"), FormatScriptBoolLiteral(Baseline.bExpectedHidden));
		ReplaceToken(Script, TEXT("__EXPECTED_LOCATION__"), FormatScriptVectorLiteral(Baseline.ExpectedLocation));
		ReplaceToken(Script, TEXT("__EXPECTED_ROTATION__"), FormatScriptRotatorLiteral(Baseline.ExpectedRotation));
		ReplaceToken(Script, TEXT("__VECTOR_TOLERANCE__"), FormatScriptFloatLiteral(VectorTolerance));
		ReplaceToken(Script, TEXT("__ROTATION_TOLERANCE__"), FormatScriptFloatLiteral(RotationTolerance));
		ReplaceToken(Script, TEXT("__EXPECTED_NAME__"), EscapeScriptString(Baseline.ExpectedNameOrLabel));
		ReplaceToken(Script, TEXT("__SCRIPT_SCALE__"), FormatScriptVectorLiteral(Baseline.ScriptScale));
		ReplaceToken(Script, TEXT("__SCRIPT_TICK_INTERVAL__"), FormatScriptFloatLiteral(Baseline.ScriptTickInterval));
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptActorStateBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptActorStateCompatBindingsTest,
	"Angelscript.TestModule.Bindings.ActorStateCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptActorStateCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASActorStateCompat"));
		ResetSharedCloneEngine(Engine);
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	const FActorStateBaseline Baseline = CreateBaseline(*this, Spawner);
	if (!VerifyNativeBaseline(*this, Baseline))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ActorStateModuleName,
		BuildCompatScript(Baseline));
	if (Module == nullptr)
	{
		return false;
	}

	int32 ResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyActorStateCompat(AActor, UGameInstance)"),
			[this, &Baseline](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, Baseline.Actor, TEXT("VerifyActorStateCompat"))
					&& SetArgObjectChecked(*this, Context, 1, Baseline.GameInstance, TEXT("VerifyActorStateCompat"));
			},
			TEXT("VerifyActorStateCompat"),
			ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("AActor state bindings should preserve initialization, visibility, transform, naming and game-instance parity on the spawned actor fixture"),
		ResultMask,
		0);
	bPassed &= TestTrue(
		TEXT("SetActorScale3D should update the native actor scale to the script-provided value"),
		Baseline.Actor->GetActorScale3D().Equals(Baseline.ScriptScale, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("SetActorTickInterval should update the native actor tick interval to the script-provided value"),
		FMath::IsNearlyEqual(Baseline.Actor->GetActorTickInterval(), Baseline.ScriptTickInterval));
	bPassed &= TestTrue(
		TEXT("GetActorLocation should leave the native actor location unchanged after script execution"),
		Baseline.Actor->GetActorLocation().Equals(Baseline.ExpectedLocation, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("GetActorRotation should leave the native actor rotation unchanged after script execution"),
		Baseline.Actor->GetActorRotation().Equals(Baseline.ExpectedRotation, RotationTolerance));
	bPassed &= TestEqual(
		TEXT("GetActorNameOrLabel should leave the native label-or-name string unchanged after script execution"),
		Baseline.Actor->GetActorNameOrLabel(),
		Baseline.ExpectedNameOrLabel);
	bPassed &= TestEqual(
		TEXT("GetGameInstance should leave the native game-instance pointer unchanged after script execution"),
		Baseline.Actor->GetGameInstance(),
		Baseline.GameInstance);
	bPassed &= TestEqual(
		TEXT("IsActorInitialized should leave the native initialized state unchanged after script execution"),
		Baseline.Actor->IsActorInitialized(),
		Baseline.bExpectedInitialized);
	bPassed &= TestEqual(
		TEXT("HasActorBegunPlay should leave the native begun-play state unchanged after script execution"),
		Baseline.Actor->HasActorBegunPlay(),
		Baseline.bExpectedBegunPlay);
	bPassed &= TestEqual(
		TEXT("IsHidden should leave the native hidden state unchanged after script execution"),
		Baseline.Actor->IsHidden(),
		Baseline.bExpectedHidden);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
