#include "Shared/AngelscriptPerformanceTestUtils.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPerformanceArtifactGenerationTest,
	"Angelscript.TestModule.Core.Performance.ArtifactGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPerformanceArtifactGenerationTest::RunTest(const FString& Parameters)
{
	const FString RunId(TEXT("P3_4_PerformanceArtifactGeneration"));
	const TArray<FAngelscriptPerformanceMetric> Metrics = {
		{ TEXT("artifact.generation.seconds"), { 0.1, 0.2, 0.3 }, ComputeMedian({ 0.1, 0.2, 0.3 }), TEXT("seconds"), TEXT("RuntimeInstrumentation") }
	};
	const FString MetricsPath = WritePerformanceMetricsArtifact(RunId, TEXT("Angelscript.TestModule.Core.Performance.ArtifactGeneration"), Metrics, { TEXT("Artifact generation regression writes a minimal metrics payload.") });
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TestTrue(TEXT("Performance artifact generation test should write metrics.json"), PlatformFile.FileExists(*MetricsPath));
	TestTrue(TEXT("Performance artifact generation test should create the Metrics directory"), PlatformFile.DirectoryExists(*FPaths::GetPath(MetricsPath)));
	FString Contents;
	TestTrue(TEXT("Performance artifact generation test should read the metrics artifact"), FFileHelper::LoadFileToString(Contents, *MetricsPath));
	TestTrue(TEXT("Performance artifact generation test should persist the metric name"), Contents.Contains(TEXT("artifact.generation.seconds")));
	TestTrue(TEXT("Performance artifact generation test should persist the metric unit"), Contents.Contains(TEXT("\"unit\":\"seconds\"")));
	TestTrue(TEXT("Performance artifact generation test should persist the metric source"), Contents.Contains(TEXT("\"source\":\"RuntimeInstrumentation\"")));
	return TestTrue(TEXT("Performance artifact generation test should persist the run id"), Contents.Contains(RunId));
}

#endif
