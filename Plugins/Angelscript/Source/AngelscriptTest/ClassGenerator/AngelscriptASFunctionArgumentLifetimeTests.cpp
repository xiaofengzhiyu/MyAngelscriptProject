#include "Shared/AngelscriptScenarioTestUtils.h"
#include "Shared/AngelscriptTestMacros.h"

#include "ClassGenerator/ASClass.h"
#include "ClassGenerator/AngelscriptASFunctionArgumentLifetimeTestTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace ASFunctionArgumentLifetimeTests
{
	static const FName ModuleName(TEXT("ASFunctionArgumentLifetime"));
	static const FString ScriptFilename(TEXT("ASFunctionArgumentLifetime.as"));
	static const FName GeneratedClassName(TEXT("UTrackedRefArgCarrier"));
	static const FName IncrementFunctionName(TEXT("Increment"));
	static const FName ValuePropertyName(TEXT("Value"));

	UASClass* CompileTrackedRefArgCarrier(FAutomationTestBase& Test, FAngelscriptEngine& Engine)
	{
		const FString ScriptSource = TEXT(R"AS(
UCLASS()
class UTrackedRefArgCarrier : UObject
{
	UFUNCTION()
	void Increment(FAngelscriptASFunctionArgumentLifetimeFixture& Arg)
	{
		Arg.Value += 1;
	}
}
)AS");

		UClass* GeneratedClass = CompileScriptModule(
			Test,
			Engine,
			ModuleName,
			ScriptFilename,
			ScriptSource,
			GeneratedClassName);
		if (GeneratedClass == nullptr)
		{
			return nullptr;
		}

		return Cast<UASClass>(GeneratedClass);
	}
}

using namespace ASFunctionArgumentLifetimeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptASFunctionRuntimeCallFunctionCleansUpDestructibleReferenceArgumentsTest,
	"Angelscript.TestModule.ClassGenerator.ASFunction.RuntimeCallFunctionCleansUpDestructibleReferenceArguments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptASFunctionRuntimeCallFunctionCleansUpDestructibleReferenceArgumentsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ASFunctionArgumentLifetimeTests::ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UASClass* ScriptClass = ASFunctionArgumentLifetimeTests::CompileTrackedRefArgCarrier(*this, Engine);
	if (!TestNotNull(TEXT("ASFunction argument lifetime should compile the carrier into a UASClass"), ScriptClass))
	{
		return false;
	}

	UScriptStruct* TrackedStruct = FAngelscriptASFunctionArgumentLifetimeFixture::StaticStruct();
	if (!TestNotNull(TEXT("ASFunction argument lifetime should expose the native reflected helper struct"), TrackedStruct))
	{
		return false;
	}

	UFunction* IncrementFunction = FindGeneratedFunction(ScriptClass, ASFunctionArgumentLifetimeTests::IncrementFunctionName);
	UASFunction* IncrementScriptFunction = Cast<UASFunction>(IncrementFunction);
	if (!TestNotNull(TEXT("ASFunction argument lifetime should generate Increment"), IncrementFunction)
		|| !TestNotNull(TEXT("ASFunction argument lifetime should expose Increment as a UASFunction"), IncrementScriptFunction))
	{
		return false;
	}

	TestTrue(
		TEXT("ASFunction argument lifetime should route Increment through the native thunk so ProcessEvent enters RuntimeCallFunction"),
		IncrementFunction->GetNativeFunc() == &UASFunctionNativeThunk);

	FStructProperty* ArgProperty = FindFProperty<FStructProperty>(IncrementFunction, TEXT("Arg"));
	if (!TestNotNull(TEXT("ASFunction argument lifetime should expose the reflected Arg property"), ArgProperty))
	{
		return false;
	}

	FIntProperty* ValueProperty = FindFProperty<FIntProperty>(TrackedStruct, ASFunctionArgumentLifetimeTests::ValuePropertyName);
	if (!TestNotNull(TEXT("ASFunction argument lifetime should expose the tracked struct Value property"), ValueProperty))
	{
		return false;
	}

	if (!TestEqual(
			TEXT("ASFunction argument lifetime should register exactly one destructible argument for the reference struct"),
			IncrementScriptFunction->DestroyArguments.Num(),
			1))
	{
		return false;
	}

	if (!TestTrue(
			TEXT("ASFunction argument lifetime should record the reflected Arg property in DestroyArguments"),
			IncrementScriptFunction->DestroyArguments[0].Property == ArgProperty))
	{
		return false;
	}

	if (!TestTrue(
			TEXT("ASFunction argument lifetime should keep the reflected argument struct bound to the native helper struct"),
			ArgProperty->Struct == TrackedStruct))
	{
		return false;
	}

	UObject* Instance = NewObject<UObject>(GetTransientPackage(), ScriptClass, TEXT("TrackedRefArgCarrierInstance"));
	if (!TestNotNull(TEXT("ASFunction argument lifetime should instantiate the generated UObject"), Instance))
	{
		return false;
	}

	FAngelscriptASFunctionArgumentLifetimeFixture::ResetCounters();

	int32 DestructorCountAfterCall = INDEX_NONE;
	{
		FStructOnScope Params(IncrementFunction);
		void* ParamsMemory = Params.GetStructMemory();
		if (!TestNotNull(TEXT("ASFunction argument lifetime should allocate a reflected parameter buffer"), ParamsMemory))
		{
			return false;
		}

		void* ArgValue = ArgProperty->ContainerPtrToValuePtr<void>(ParamsMemory);
		if (!TestNotNull(TEXT("ASFunction argument lifetime should expose the reflected reference argument memory"), ArgValue))
		{
			return false;
		}

		const int32 DestructorCountBeforeCall = FAngelscriptASFunctionArgumentLifetimeFixture::DestructorCount;
		if (!TestEqual(
				TEXT("ASFunction argument lifetime should keep the destructor counter at zero before the call"),
				DestructorCountBeforeCall,
				0))
		{
			return false;
		}

		ValueProperty->SetPropertyValue_InContainer(ArgValue, 5);
		{
			FAngelscriptEngineScope FunctionScope(Engine, Instance);
			Instance->ProcessEvent(IncrementFunction, ParamsMemory);
		}

		DestructorCountAfterCall = FAngelscriptASFunctionArgumentLifetimeFixture::DestructorCount;
		if (!TestEqual(
				TEXT("ASFunction argument lifetime should destruct exactly one temporary argument inside RuntimeCallFunction"),
				DestructorCountAfterCall - DestructorCountBeforeCall,
				1))
		{
			return false;
		}

		if (!TestEqual(
				TEXT("ASFunction argument lifetime should write the reference payload back through RuntimeCallFunction"),
				ValueProperty->GetPropertyValue_InContainer(ArgValue),
				6))
		{
			return false;
		}
	}

	const int32 DestructorCountAfterScope = FAngelscriptASFunctionArgumentLifetimeFixture::DestructorCount;

	TestEqual(
		TEXT("ASFunction argument lifetime should destroy the reflected parameter buffer exactly once after the call scope ends"),
		DestructorCountAfterScope - DestructorCountAfterCall,
		1);
	TestEqual(
		TEXT("ASFunction argument lifetime should observe two total destructor calls after the runtime temporary and reflected buffer both clean up"),
		DestructorCountAfterScope,
		2);

	ASTEST_END_SHARE_FRESH
	return true;
}

#endif
