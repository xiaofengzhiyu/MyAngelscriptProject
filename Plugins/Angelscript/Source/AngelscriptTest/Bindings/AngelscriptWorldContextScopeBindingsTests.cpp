#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptWorldContextScopeBindingsTests_Private
{
	static constexpr ANSICHAR ScopePushPopModuleName[] = "ASWorldContextScopePushPop";
	static constexpr ANSICHAR ScopeRestoreModuleName[] = "ASWorldContextScopeRestore";

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
		const FString& FunctionDecl,
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

	FString BuildScopePushPopScript()
	{
		return TEXT(R"AS(
int VerifyScopePushPop(AActor ContextActor)
{
	if (__WorldContext() != null)
		return 10;
	if (GetCurrentWorld() != null)
		return 20;

	{
		FAngelscriptGameThreadScopeWorldContext Scope(ContextActor);
		if (__WorldContext() != ContextActor)
			return 30;

		UWorld CurrentWorld = GetCurrentWorld();
		if (CurrentWorld == null)
			return 40;
		if (CurrentWorld != ContextActor.GetWorld())
			return 50;
	}

	if (__WorldContext() != null)
		return 60;
	if (GetCurrentWorld() != null)
		return 70;

	return 1;
}
)AS");
	}

	FString BuildScopeRestoreScript()
	{
		return TEXT(R"AS(
int VerifyNestedScopeRestore(AActor PreviousContextActor, AActor OuterContextActor, AActor InnerContextActor)
{
	if (__WorldContext() != PreviousContextActor)
		return 10;
	if (GetCurrentWorld() != PreviousContextActor.GetWorld())
		return 20;

	{
		FAngelscriptGameThreadScopeWorldContext OuterScope(OuterContextActor);
		if (__WorldContext() != OuterContextActor)
			return 30;
		if (GetCurrentWorld() != OuterContextActor.GetWorld())
			return 40;

		{
			FAngelscriptGameThreadScopeWorldContext InnerScope(InnerContextActor);
			if (__WorldContext() != InnerContextActor)
				return 50;
			if (GetCurrentWorld() != InnerContextActor.GetWorld())
				return 60;
		}

		if (__WorldContext() != OuterContextActor)
			return 70;
		if (GetCurrentWorld() != OuterContextActor.GetWorld())
			return 80;
	}

	if (__WorldContext() != PreviousContextActor)
		return 90;
	if (GetCurrentWorld() != PreviousContextActor.GetWorld())
		return 100;

	return 1;
}
)AS");
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptWorldContextScopeBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptWorldContextScopePushPopBindingsTest,
	"Angelscript.TestModule.Bindings.WorldContextScope.PushPop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptWorldContextScopeRestoreBindingsTest,
	"Angelscript.TestModule.Bindings.WorldContextScope.RestoreOuterAmbient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptWorldContextScopePushPopBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& ContextActor = Spawner.SpawnActor<AActor>();
	if (!TestNotNull(TEXT("World-context scope push-pop test should spawn a scenario actor"), &ContextActor)
		|| !TestNotNull(TEXT("World-context scope push-pop test should resolve the actor world"), ContextActor.GetWorld()))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(*this, Engine, ScopePushPopModuleName, BuildScopePushPopScript());
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(
		*this,
		Engine,
		*Module,
		TEXT("int VerifyScopePushPop(AActor)"),
		[this, &ContextActor](asIScriptContext& Context)
		{
			return SetArgObjectChecked(*this, Context, 0, &ContextActor, TEXT("VerifyScopePushPop"));
		},
		TEXT("VerifyScopePushPop"),
		Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Script world-context scope should install the actor world context inside scope and clear it after destruction"),
		Result,
		1);
	bPassed &= TestNull(
		TEXT("Script world-context scope push-pop test should leave the engine world context cleared after execution"),
		Engine.GetCurrentWorldContextObject());

	ASTEST_END_FULL
	return bPassed;
}

bool FAngelscriptWorldContextScopeRestoreBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AActor& PreviousContextActor = Spawner.SpawnActor<AActor>();
	AActor& OuterContextActor = Spawner.SpawnActor<AActor>();
	AActor& InnerContextActor = Spawner.SpawnActor<AActor>();
	if (!TestNotNull(TEXT("World-context scope restore test should spawn the previous ambient actor"), &PreviousContextActor)
		|| !TestNotNull(TEXT("World-context scope restore test should spawn the outer scoped actor"), &OuterContextActor)
		|| !TestNotNull(TEXT("World-context scope restore test should spawn the inner scoped actor"), &InnerContextActor))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(*this, Engine, ScopeRestoreModuleName, BuildScopeRestoreScript());
	if (Module == nullptr)
	{
		return false;
	}

	{
		FScopedTestWorldContextScope PreviousContextScope(&PreviousContextActor);
		if (!TestTrue(
			TEXT("World-context scope restore test should install the pre-existing ambient context before script execution"),
			Engine.GetCurrentWorldContextObject() == &PreviousContextActor))
		{
			return false;
		}

		int32 Result = INDEX_NONE;
		if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyNestedScopeRestore(AActor, AActor, AActor)"),
			[this, &PreviousContextActor, &OuterContextActor, &InnerContextActor](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, &PreviousContextActor, TEXT("VerifyNestedScopeRestore"))
					&& SetArgObjectChecked(*this, Context, 1, &OuterContextActor, TEXT("VerifyNestedScopeRestore"))
					&& SetArgObjectChecked(*this, Context, 2, &InnerContextActor, TEXT("VerifyNestedScopeRestore"));
			},
			TEXT("VerifyNestedScopeRestore"),
			Result))
		{
			return false;
		}

		bPassed &= TestEqual(
			TEXT("Nested script world-context scopes should restore the previous ambient context after inner and outer destruction"),
			Result,
			1);
		bPassed &= TestTrue(
			TEXT("Script world-context scope restore test should preserve the pre-existing ambient context after script returns"),
			Engine.GetCurrentWorldContextObject() == &PreviousContextActor);
	}

	bPassed &= TestNull(
		TEXT("World-context scope restore test should clear the engine world context after the outer C++ scope exits"),
		Engine.GetCurrentWorldContextObject());

	ASTEST_END_FULL
	return bPassed;
}

#endif
