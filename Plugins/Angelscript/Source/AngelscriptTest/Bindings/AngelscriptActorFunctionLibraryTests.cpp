#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "../../AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h"

#include "Components/ActorTestSpawner.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptActorFunctionLibraryTests_Private
{
	static constexpr ANSICHAR ModuleName[] = "ASActorFunctionLibraryRoundTrip";
	static constexpr double VectorTolerance = 0.05;
	static constexpr double RotationTolerance = 0.1;
	static const FVector RootBoxExtent(30.0f, 30.0f, 30.0f);
	static const FVector BlockingBoxExtent(50.0f, 50.0f, 50.0f);
	static const FVector TransformInitialLocation(-220.0f, 0.0f, 50.0f);
	static const FRotator TransformInitialRotation(-10.0f, 15.0f, 5.0f);
	static const FVector TransformSetterLocation(-120.0f, 0.0f, 50.0f);
	static const FRotator TransformSetterRotation(20.0f, 75.0f, -5.0f);
	static const FVector TransformSweepTargetLocation(220.0f, 0.0f, 50.0f);
	static const FVector BlockingLocation(0.0f, 0.0f, 50.0f);
	static const FVector ParentLocation(450.0f, 0.0f, 120.0f);
	static const FVector ActorAttachChildLocation(640.0f, 0.0f, 120.0f);
	static const FVector ComponentAttachChildLocation(760.0f, 0.0f, 120.0f);
	static const FVector ActorRelativeLocation(15.0f, 25.0f, 35.0f);
	static const FRotator ActorRelativeRotation(10.0f, 20.0f, 30.0f);
	static const FVector ComponentRelativeLocation(-12.0f, 18.0f, 24.0f);
	static const FRotator ComponentRelativeRotation(-5.0f, 45.0f, 12.5f);

	bool SetArgAddressChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		asUINT ArgumentIndex,
		void* Address,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind address argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgAddress(ArgumentIndex, Address),
			static_cast<int32>(asSUCCESS));
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

	bool ExecuteBoolFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		bool& OutResult)
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

		OutResult = Context->GetReturnByte() != 0;
		return true;
	}

	FString FormatScriptFloatLiteral(double Value)
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

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FString BuildActorFunctionLibraryScript()
	{
		FString ScriptSource = TEXT(R"AS(
int VerifyActorTransformAndAttach(AActor TransformActor, AActor ActorAttachChild, AActor ComponentAttachChild, AActor ParentActor, USceneComponent ParentComponent)
{
	if (TransformActor == null)
		return 1;

	if (!TransformActor.GetActorLocation().Equals(__INITIAL_LOCATION__, 0.1f))
		return 2;
	if (!TransformActor.GetActorRotation().Equals(__INITIAL_ROTATION__, 0.1f))
		return 4;

	FHitResult SetterLocationHit;
	if (!TransformActor.SetActorLocation(__SETTER_LOCATION__, false, SetterLocationHit, false))
		return 8;
	if (!TransformActor.GetActorLocation().Equals(__SETTER_LOCATION__, 0.1f))
		return 16;

	if (!TransformActor.SetActorRotation(__SETTER_ROTATION__, false))
		return 32;
	if (!TransformActor.GetActorRotation().Equals(__SETTER_ROTATION__, 0.1f))
		return 64;
	return 0;
}

bool RunActorSweep(AActor TransformActor, FHitResult& OutHit)
{
	if (TransformActor == null)
		return false;
	return TransformActor.SetActorLocation(__SWEEP_TARGET_LOCATION__, true, OutHit, false);
}
)AS");

		ReplaceToken(ScriptSource, TEXT("__INITIAL_LOCATION__"), FormatScriptVectorLiteral(TransformInitialLocation));
		ReplaceToken(ScriptSource, TEXT("__INITIAL_ROTATION__"), FormatScriptRotatorLiteral(TransformInitialRotation));
		ReplaceToken(ScriptSource, TEXT("__SETTER_LOCATION__"), FormatScriptVectorLiteral(TransformSetterLocation));
		ReplaceToken(ScriptSource, TEXT("__SETTER_ROTATION__"), FormatScriptRotatorLiteral(TransformSetterRotation));
		ReplaceToken(ScriptSource, TEXT("__SWEEP_TARGET_LOCATION__"), FormatScriptVectorLiteral(TransformSweepTargetLocation));
		return ScriptSource;
	}

	USceneComponent* CreateSceneRootComponent(
		FAutomationTestBase& Test,
		AActor& Owner,
		const TCHAR* ComponentName,
		const FVector& WorldLocation)
	{
		USceneComponent* RootComponent = NewObject<USceneComponent>(&Owner, FName(ComponentName));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s fixture should create a scene root"), ComponentName),
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

	UBoxComponent* CreateCollisionRootComponent(
		FAutomationTestBase& Test,
		AActor& Owner,
		const TCHAR* ComponentName,
		const FVector& BoxExtent,
		const FVector& WorldLocation)
	{
		UBoxComponent* RootComponent = NewObject<UBoxComponent>(&Owner, FName(ComponentName));
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s fixture should create a collision root"), ComponentName),
				RootComponent))
		{
			return nullptr;
		}

		Owner.AddInstanceComponent(RootComponent);
		Owner.SetRootComponent(RootComponent);
		RootComponent->SetMobility(EComponentMobility::Movable);
		RootComponent->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		RootComponent->SetGenerateOverlapEvents(true);
		RootComponent->SetBoxExtent(BoxExtent);
		RootComponent->RegisterComponent();
		RootComponent->SetWorldLocation(WorldLocation);
		return RootComponent;
	}

	bool ExpectHitResultParity(
		FAutomationTestBase& Test,
		const TCHAR* Label,
		bool bScriptReturnValue,
		bool bNativeReturnValue,
		const FHitResult& ScriptHit,
		const FHitResult& NativeHit)
	{
		bool bPassed = true;
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s should preserve the bool return value"), Label), bScriptReturnValue, bNativeReturnValue);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s should preserve the hit actor"), Label), ScriptHit.GetActor(), NativeHit.GetActor());
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s should preserve the hit component"), Label), ScriptHit.GetComponent(), NativeHit.GetComponent());
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s should preserve the blocking-hit flag"), Label), ScriptHit.bBlockingHit, NativeHit.bBlockingHit);
		bPassed &= Test.TestTrue(*FString::Printf(TEXT("%s should preserve the hit location"), Label), ScriptHit.Location.Equals(NativeHit.Location, 0.05f));
		bPassed &= Test.TestTrue(*FString::Printf(TEXT("%s should preserve the impact point"), Label), ScriptHit.ImpactPoint.Equals(NativeHit.ImpactPoint, 0.05f));
		return bPassed;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptActorFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptActorFunctionLibraryRoundTripTest,
	"Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptActorFunctionLibraryRoundTripTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	asIScriptModule* Module = nullptr;
	ASTEST_BUILD_MODULE(
		Engine,
		ModuleName,
		BuildActorFunctionLibraryScript(),
		Module);

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& TransformActor = Spawner.SpawnActor<AActor>();
	AActor& BlockingActor = Spawner.SpawnActor<AActor>();
	AActor& ParentActor = Spawner.SpawnActor<AActor>();
	AActor& ActorAttachChild = Spawner.SpawnActor<AActor>();
	AActor& ComponentAttachChild = Spawner.SpawnActor<AActor>();

	UBoxComponent* TransformRoot = CreateCollisionRootComponent(*this, TransformActor, TEXT("ActorFunctionTransformRoot"), RootBoxExtent, TransformInitialLocation);
	UBoxComponent* BlockingRoot = CreateCollisionRootComponent(*this, BlockingActor, TEXT("ActorFunctionBlockingRoot"), BlockingBoxExtent, BlockingLocation);
	USceneComponent* ParentRoot = CreateSceneRootComponent(*this, ParentActor, TEXT("ActorFunctionParentRoot"), ParentLocation);
	USceneComponent* ActorAttachRoot = CreateSceneRootComponent(*this, ActorAttachChild, TEXT("ActorFunctionActorAttachRoot"), ActorAttachChildLocation);
	USceneComponent* ComponentAttachRoot = CreateSceneRootComponent(*this, ComponentAttachChild, TEXT("ActorFunctionComponentAttachRoot"), ComponentAttachChildLocation);
	if (TransformRoot == nullptr || BlockingRoot == nullptr || ParentRoot == nullptr || ActorAttachRoot == nullptr || ComponentAttachRoot == nullptr)
	{
		return false;
	}

	TransformActor.SetActorRotation(TransformInitialRotation);
	ParentActor.SetActorRotation(FRotator::ZeroRotator);
	ActorAttachChild.SetActorRotation(FRotator::ZeroRotator);
	ComponentAttachChild.SetActorRotation(FRotator::ZeroRotator);

	FScopedTestWorldContextScope WorldContextScope(&TransformActor);

	int32 ResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyActorTransformAndAttach(AActor, AActor, AActor, AActor, USceneComponent)"),
			[this, &TransformActor, &ActorAttachChild, &ComponentAttachChild, &ParentActor, ParentRoot](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, &TransformActor, TEXT("VerifyActorTransformAndAttach"))
					&& SetArgObjectChecked(*this, Context, 1, &ActorAttachChild, TEXT("VerifyActorTransformAndAttach"))
					&& SetArgObjectChecked(*this, Context, 2, &ComponentAttachChild, TEXT("VerifyActorTransformAndAttach"))
					&& SetArgObjectChecked(*this, Context, 3, &ParentActor, TEXT("VerifyActorTransformAndAttach"))
					&& SetArgObjectChecked(*this, Context, 4, ParentRoot, TEXT("VerifyActorTransformAndAttach"));
			},
			TEXT("VerifyActorTransformAndAttach"),
			ResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Actor function library script scenario should preserve getter/setter and attach behavior without mismatch codes"),
		ResultMask,
		0);
	bPassed &= TestTrue(
		TEXT("Actor transform round-trip should leave the transform actor at the scripted setter location before sweep parity"),
		TransformActor.GetActorLocation().Equals(TransformSetterLocation, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("Actor transform round-trip should leave the transform actor at the scripted setter rotation before sweep parity"),
		TransformActor.GetActorRotation().Equals(TransformSetterRotation, RotationTolerance));

	UAngelscriptActorLibrary::AttachToActor(&ActorAttachChild, &ParentActor);
	UAngelscriptActorLibrary::SetActorRelativeLocation(&ActorAttachChild, ActorRelativeLocation);
	UAngelscriptActorLibrary::SetActorRelativeRotation(&ActorAttachChild, ActorRelativeRotation);
	bPassed &= TestEqual(
		TEXT("AttachToActor should preserve the requested parent actor"),
		ActorAttachChild.GetAttachParentActor(),
		&ParentActor);
	bPassed &= TestEqual(
		TEXT("AttachToActor should preserve the requested parent root component"),
		ActorAttachRoot->GetAttachParent(),
		ParentRoot);
	bPassed &= TestTrue(
		TEXT("AttachToActor should preserve the scripted relative location"),
		UAngelscriptActorLibrary::GetActorRelativeLocation(&ActorAttachChild).Equals(ActorRelativeLocation, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("AttachToActor should preserve the scripted relative rotation"),
		UAngelscriptActorLibrary::GetActorRelativeRotation(&ActorAttachChild).Equals(ActorRelativeRotation, RotationTolerance));

	UAngelscriptActorLibrary::AttachToComponent(&ComponentAttachChild, ParentRoot);
	UAngelscriptActorLibrary::SetActorRelativeLocation(&ComponentAttachChild, ComponentRelativeLocation);
	UAngelscriptActorLibrary::SetActorRelativeRotation(&ComponentAttachChild, ComponentRelativeRotation);
	bPassed &= TestEqual(
		TEXT("AttachToComponent should preserve the requested parent component"),
		ComponentAttachRoot->GetAttachParent(),
		ParentRoot);
	bPassed &= TestEqual(
		TEXT("AttachToComponent should preserve the requested parent actor"),
		ComponentAttachChild.GetAttachParentActor(),
		&ParentActor);
	bPassed &= TestTrue(
		TEXT("AttachToComponent should preserve the scripted relative location"),
		UAngelscriptActorLibrary::GetActorRelativeLocation(&ComponentAttachChild).Equals(ComponentRelativeLocation, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("AttachToComponent should preserve the scripted relative rotation"),
		UAngelscriptActorLibrary::GetActorRelativeRotation(&ComponentAttachChild).Equals(ComponentRelativeRotation, RotationTolerance));

	FHitResult NativeSweepHit;
	const bool bNativeSweepReturn =
		UAngelscriptActorLibrary::SetActorLocationAdvanced(&TransformActor, TransformSweepTargetLocation, true, NativeSweepHit, false);
	const FVector NativeFinalLocation = TransformActor.GetActorLocation();

	TransformActor.SetActorLocation(TransformSetterLocation);
	TransformActor.SetActorRotation(TransformSetterRotation);

	FHitResult ScriptSweepHit;
	bool bScriptSweepReturn = false;
	if (!ExecuteBoolFunction(
			*this,
			Engine,
			*Module,
			TEXT("bool RunActorSweep(AActor, FHitResult&)"),
			[this, &TransformActor, &ScriptSweepHit](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, &TransformActor, TEXT("RunActorSweep"))
					&& SetArgAddressChecked(*this, Context, 1, &ScriptSweepHit, TEXT("RunActorSweep"));
			},
			TEXT("RunActorSweep"),
			bScriptSweepReturn))
	{
		return false;
	}

	bPassed &= ExpectHitResultParity(
		*this,
		TEXT("SetActorLocationAdvanced sweep parity"),
		bScriptSweepReturn,
		bNativeSweepReturn,
		ScriptSweepHit,
		NativeSweepHit);
	bPassed &= TestTrue(
		TEXT("SetActorLocationAdvanced sweep parity should preserve the final actor location"),
		TransformActor.GetActorLocation().Equals(NativeFinalLocation, VectorTolerance));
	bPassed &= TestTrue(
		TEXT("SetActorLocationAdvanced sweep parity should preserve the post-sweep actor rotation"),
		TransformActor.GetActorRotation().Equals(TransformSetterRotation, RotationTolerance));
	bPassed &= TestEqual(
		TEXT("SetActorLocationAdvanced sweep parity should preserve the blocking actor"),
		ScriptSweepHit.GetActor(),
		&BlockingActor);
	bPassed &= TestEqual(
		TEXT("SetActorLocationAdvanced sweep parity should preserve the blocking component"),
		ScriptSweepHit.GetComponent(),
		static_cast<UPrimitiveComponent*>(BlockingRoot));

	ASTEST_END_FULL
	return bPassed;
}

#endif
