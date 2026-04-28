#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptHitResultTraceConstructorBindingsTest,
	"Angelscript.TestModule.Bindings.HitResult.TraceConstructorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptHitResultActorComponentConstructorBindingsTest,
	"Angelscript.TestModule.Bindings.HitResult.ActorComponentConstructorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptHitResultBindingsTests_Private
{
	static constexpr ANSICHAR HitResultTraceConstructorModuleName[] = "ASHitResultTraceConstructorCompat";
	static constexpr ANSICHAR HitResultActorComponentConstructorModuleName[] = "ASHitResultActorComponentConstructorCompat";
	static constexpr float HitResultFieldTolerance = 0.001f;

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	template <typename TBindArguments>
	bool ExecuteHitResultFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		TBindArguments&& BindArguments,
		const TCHAR* ContextLabel,
		FHitResult& OutHit)
	{
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

		const int PrepareResult = Context->Prepare(&Function);
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
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("%s saw a script exception: %s"),
					ContextLabel,
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			return false;
		}

		void* ReturnValueAddress = Context->GetAddressOfReturnValue();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should expose the return value storage"), ContextLabel), ReturnValueAddress))
		{
			return false;
		}

		OutHit = *static_cast<FHitResult*>(ReturnValueAddress);
		return true;
	}

	bool VerifyVector(
		FAutomationTestBase& Test,
		const FString& Label,
		const FVector& Actual,
		const FVector& Expected)
	{
		return Test.TestTrue(*Label, Actual.Equals(Expected, HitResultFieldTolerance));
	}

	bool VerifyHitResult(
		FAutomationTestBase& Test,
		const TCHAR* ContextLabel,
		const FHitResult& Actual,
		const FHitResult& Expected)
	{
		bool bPassed = true;
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve the actor handle"), ContextLabel),
			Actual.GetActor(),
			Expected.GetActor());
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve the component handle"), ContextLabel),
			Actual.GetComponent(),
			Expected.GetComponent());
		bPassed &= VerifyVector(Test, FString::Printf(TEXT("%s should preserve TraceStart"), ContextLabel), Actual.TraceStart, Expected.TraceStart);
		bPassed &= VerifyVector(Test, FString::Printf(TEXT("%s should preserve TraceEnd"), ContextLabel), Actual.TraceEnd, Expected.TraceEnd);
		bPassed &= VerifyVector(Test, FString::Printf(TEXT("%s should preserve Location"), ContextLabel), Actual.Location, Expected.Location);
		bPassed &= VerifyVector(Test, FString::Printf(TEXT("%s should preserve ImpactPoint"), ContextLabel), Actual.ImpactPoint, Expected.ImpactPoint);
		bPassed &= VerifyVector(Test, FString::Printf(TEXT("%s should preserve Normal"), ContextLabel), Actual.Normal, Expected.Normal);
		bPassed &= VerifyVector(Test, FString::Printf(TEXT("%s should preserve ImpactNormal"), ContextLabel), Actual.ImpactNormal, Expected.ImpactNormal);
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should preserve Distance"), ContextLabel),
			FMath::IsNearlyEqual(Actual.Distance, Expected.Distance, HitResultFieldTolerance));
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should preserve Time"), ContextLabel),
			FMath::IsNearlyEqual(Actual.Time, Expected.Time, HitResultFieldTolerance));
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should preserve PenetrationDepth"), ContextLabel),
			FMath::IsNearlyEqual(Actual.PenetrationDepth, Expected.PenetrationDepth, HitResultFieldTolerance));
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve BoneName"), ContextLabel),
			Actual.BoneName,
			Expected.BoneName);
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve MyBoneName"), ContextLabel),
			Actual.MyBoneName,
			Expected.MyBoneName);
		return bPassed;
	}

	FHitResult BuildTraceConstructorBaseline()
	{
		FHitResult Baseline(FVector(-25.0f, 4.0f, 1.0f), FVector(125.0f, -6.0f, 3.0f));
		Baseline.Distance = 55.5f;
		Baseline.Time = 0.375f;
		Baseline.PenetrationDepth = 1.25f;
		Baseline.Location = FVector(10.0f, 20.0f, 30.0f);
		Baseline.ImpactPoint = FVector(-5.0f, 15.0f, 35.0f);
		Baseline.Normal = FVector(0.0f, 1.0f, 0.0f);
		Baseline.ImpactNormal = FVector(0.0f, 0.0f, 1.0f);
		Baseline.BoneName = TEXT("Bone");
		Baseline.MyBoneName = TEXT("MyBone");
		return Baseline;
	}

	FHitResult BuildActorComponentConstructorBaseline(AActor& Actor, UPrimitiveComponent& Component)
	{
		FHitResult Baseline(&Actor, &Component, FVector(14.0f, -3.0f, 7.0f), FVector(0.0f, -1.0f, 0.0f));
		Baseline.Distance = 9.25f;
		Baseline.Time = 0.5f;
		Baseline.PenetrationDepth = 2.5f;
		Baseline.TraceStart = FVector(-100.0f, 0.0f, 0.0f);
		Baseline.TraceEnd = FVector(100.0f, 0.0f, 0.0f);
		Baseline.BoneName = TEXT("HitBone");
		Baseline.MyBoneName = TEXT("ReceiverBone");
		return Baseline;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptHitResultBindingsTests_Private;

bool FAngelscriptHitResultTraceConstructorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	const FHitResult Expected = BuildTraceConstructorBaseline();

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		HitResultTraceConstructorModuleName,
		TEXT(R"AS(
FHitResult MakeTraceConstructorHit()
{
	FHitResult Hit(FVector(-25.0f, 4.0f, 1.0f), FVector(125.0f, -6.0f, 3.0f));
	Hit.Distance = 55.5f;
	Hit.Time = 0.375f;
	Hit.PenetrationDepth = 1.25f;
	Hit.Location = FVector(10.0f, 20.0f, 30.0f);
	Hit.ImpactPoint = FVector(-5.0f, 15.0f, 35.0f);
	Hit.Normal = FVector(0.0f, 1.0f, 0.0f);
	Hit.ImpactNormal = FVector(0.0f, 0.0f, 1.0f);
	Hit.BoneName = n"Bone";
	Hit.MyBoneName = n"MyBone";
	return Hit;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("FHitResult MakeTraceConstructorHit()"));
	if (Function == nullptr)
	{
		return false;
	}

	FHitResult ScriptHit;
	if (!ExecuteHitResultFunction(
		*this,
		Engine,
		*Function,
		[](asIScriptContext&) { return true; },
		TEXT("MakeTraceConstructorHit"),
		ScriptHit))
	{
		return false;
	}

	bPassed = VerifyHitResult(
		*this,
		TEXT("Trace-constructor FHitResult binding"),
		ScriptHit,
		Expected);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptHitResultActorComponentConstructorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	AActor* HostActor = NewObject<AActor>(GetTransientPackage(), NAME_None, RF_Transient);
	UBoxComponent* HitComponent = HostActor != nullptr
		? NewObject<UBoxComponent>(HostActor, NAME_None, RF_Transient)
		: nullptr;
	if (!TestNotNull(TEXT("HitResult actor/component constructor test should create a transient host actor"), HostActor)
		|| !TestNotNull(TEXT("HitResult actor/component constructor test should create a transient primitive component"), HitComponent))
	{
		return false;
	}

	HostActor->AddToRoot();
	HitComponent->AddToRoot();

	ON_SCOPE_EXIT
	{
		if (HitComponent != nullptr)
		{
			if (HitComponent->IsRooted())
			{
				HitComponent->RemoveFromRoot();
			}
			HitComponent->MarkAsGarbage();
		}
		if (HostActor != nullptr)
		{
			if (HostActor->IsRooted())
			{
				HostActor->RemoveFromRoot();
			}
			HostActor->MarkAsGarbage();
		}
	};

	HostActor->AddInstanceComponent(HitComponent);
	HostActor->SetRootComponent(HitComponent);

	const FHitResult Expected = BuildActorComponentConstructorBaseline(*HostActor, *HitComponent);

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		HitResultActorComponentConstructorModuleName,
		TEXT(R"AS(
FHitResult MakeActorComponentConstructorHit(AActor ExpectedActor, UPrimitiveComponent ExpectedComponent)
{
	FHitResult Hit(ExpectedActor, ExpectedComponent, FVector(14.0f, -3.0f, 7.0f), FVector(0.0f, -1.0f, 0.0f));
	Hit.Distance = 9.25f;
	Hit.Time = 0.5f;
	Hit.PenetrationDepth = 2.5f;
	Hit.TraceStart = FVector(-100.0f, 0.0f, 0.0f);
	Hit.TraceEnd = FVector(100.0f, 0.0f, 0.0f);
	Hit.BoneName = n"HitBone";
	Hit.MyBoneName = n"ReceiverBone";
	return Hit;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(
		*this,
		*Module,
		TEXT("FHitResult MakeActorComponentConstructorHit(AActor, UPrimitiveComponent)"));
	if (Function == nullptr)
	{
		return false;
	}

	FHitResult ScriptHit;
	if (!ExecuteHitResultFunction(
		*this,
		Engine,
		*Function,
		[this, HostActor, HitComponent](asIScriptContext& Context)
		{
			return SetArgObjectChecked(*this, Context, 0, HostActor, TEXT("MakeActorComponentConstructorHit"))
				&& SetArgObjectChecked(*this, Context, 1, HitComponent, TEXT("MakeActorComponentConstructorHit"));
		},
		TEXT("MakeActorComponentConstructorHit"),
		ScriptHit))
	{
		return false;
	}

	bPassed = VerifyHitResult(
		*this,
		TEXT("Actor/component-constructor FHitResult binding"),
		ScriptHit,
		Expected);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
