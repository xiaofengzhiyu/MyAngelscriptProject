#include "Shared/AngelscriptTestEnginePool.h"

#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"
#include "Testing/AngelscriptBindExecutionObservation.h"

#include "ClassGenerator/ASClass.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectIterator.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestEnginePoolPrewarmCachesBindDatabaseTest,
	"Angelscript.TestModule.Shared.TestEnginePool.PrewarmCachesBindDatabase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestEnginePoolModuleCleanDiscardsOnlyDeltaTest,
	"Angelscript.TestModule.Shared.TestEnginePool.ModuleCleanDiscardsOnlyDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestEnginePoolGeneratedClassCleanupIsBoundedTest,
	"Angelscript.TestModule.Shared.TestEnginePool.GeneratedClassCleanupIsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Shared_AngelscriptTestEnginePoolTests_Private
{
	int32 CountRootedDetachedASClasses()
	{
		int32 Count = 0;
		for (TObjectIterator<UASClass> It; It; ++It)
		{
			if (It->ScriptTypePtr == nullptr && It->IsRooted())
			{
				++Count;
			}
		}
		return Count;
	}
}

using namespace AngelscriptTest_Shared_AngelscriptTestEnginePoolTests_Private;

bool FAngelscriptTestEnginePoolPrewarmCachesBindDatabaseTest::RunTest(const FString& Parameters)
{
	AngelscriptTestSupport::ShutdownTestEnginePool();
	FAngelscriptBindExecutionObservation::Reset();

	FAngelscriptEngine& SourceEngine = AngelscriptTestSupport::PrewarmTestEnginePool();
	const FAngelscriptBindExecutionSnapshot PrewarmSnapshot = FAngelscriptBindExecutionObservation::GetLastSnapshot();
	if (!TestTrue(TEXT("Prewarm should create a fully bound source engine"), PrewarmSnapshot.BindScriptTypesDurationSeconds > 0.0))
	{
		return false;
	}

	FAngelscriptBindExecutionObservation::Reset();
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_MODULE_CLEAN();
	ASTEST_BEGIN_MODULE_CLEAN
		TestTrue(TEXT("Module-clean acquire should reuse the prewarmed source engine"), &Engine == &SourceEngine);
	ASTEST_END_MODULE_CLEAN

	TestEqual(TEXT("Module-clean acquire should not replay BindScriptTypes"), FAngelscriptBindExecutionObservation::GetInvocationCount(), 0);
	AngelscriptTestSupport::ShutdownTestEnginePool();
	return true;
}

bool FAngelscriptTestEnginePoolModuleCleanDiscardsOnlyDeltaTest::RunTest(const FString& Parameters)
{
	AngelscriptTestSupport::ShutdownTestEnginePool();
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_MODULE_CLEAN();

	ASTEST_BEGIN_MODULE_CLEAN
		asIScriptModule* Module = AngelscriptTestSupport::BuildModule(
			*this,
			Engine,
			"PoolDeltaModule",
			TEXT("int Entry() { return 12; }"));
		if (!TestNotNull(TEXT("Module-clean test should compile its delta module"), Module))
		{
			return false;
		}
		TestEqual(TEXT("Delta module should be active inside the scoped lease"), Engine.GetActiveModules().Num(), 1);
	ASTEST_END_MODULE_CLEAN

	TestEqual(TEXT("Module-clean scope should discard its active module delta"), Engine.GetActiveModules().Num(), 0);
	const AngelscriptTestSupport::FAngelscriptTestEnginePoolMetrics Metrics = AngelscriptTestSupport::GetTestEnginePoolMetrics();
	TestTrue(TEXT("Module-clean scope should record at least one cleanup"), Metrics.ModuleCleanCount >= 1);
	TestTrue(TEXT("Module-clean scope should not force GC for a plain module"), Metrics.GarbageCollectCount == 0);

	AngelscriptTestSupport::ShutdownTestEnginePool();
	return true;
}

bool FAngelscriptTestEnginePoolGeneratedClassCleanupIsBoundedTest::RunTest(const FString& Parameters)
{
	AngelscriptTestSupport::ShutdownTestEnginePool();
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_MODULE_CLEAN();

	ASTEST_BEGIN_MODULE_CLEAN
		const bool bCompiled = AngelscriptTestSupport::CompileAnnotatedModuleFromMemory(
			&Engine,
			TEXT("PoolGeneratedClassModule"),
			TEXT("PoolGeneratedClassModule.as"),
			TEXT(R"(
UCLASS()
class UPoolGeneratedClassObject : UObject
{
	UFUNCTION()
	int GetValue()
	{
		return 9;
	}
}
)"));
		if (!TestTrue(TEXT("Generated-class pool fixture should compile"), bCompiled))
		{
			return false;
		}

		UClass* GeneratedClass = AngelscriptTestSupport::FindGeneratedClass(&Engine, TEXT("UPoolGeneratedClassObject"));
		if (!TestNotNull(TEXT("Generated class should be visible inside the scoped lease"), GeneratedClass))
		{
			return false;
		}
	ASTEST_END_MODULE_CLEAN

	const AngelscriptTestSupport::FAngelscriptTestEnginePoolMetrics Metrics = AngelscriptTestSupport::GetTestEnginePoolMetrics();
	TestEqual(TEXT("Generated-class cleanup should leave no rooted detached UASClass objects"), CountRootedDetachedASClasses(), 0);
	TestTrue(TEXT("Generated-class cleanup should discard the generated class module"), Metrics.LastActiveModuleDiscardCount >= 1);
	TestTrue(TEXT("Generated-class cleanup should inspect detached generated classes"), Metrics.LastDetachedClassCount >= 1);

	AngelscriptTestSupport::ShutdownTestEnginePool();
	return true;
}

#endif
