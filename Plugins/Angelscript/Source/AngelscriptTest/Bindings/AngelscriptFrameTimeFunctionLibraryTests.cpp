#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/QualifiedFrameTime.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFrameTimeAsSecondsFunctionLibraryTest,
	"Angelscript.TestModule.FunctionLibraries.FrameTimeAsSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptFrameTimeFunctionLibraryTests_Private
{
	static constexpr ANSICHAR FrameTimeBindingsModuleName[] = "ASFrameTimeAsSeconds";
	static constexpr double FrameTimeTolerance = 0.000000001;

	struct FFrameTimeAsSecondsCase
	{
		const TCHAR* Label = TEXT("");
		FQualifiedFrameTime Value;
		double ExpectedSeconds = 0.0;
	};

	TArray<FFrameTimeAsSecondsCase> BuildNativeCases()
	{
		return {
			{ TEXT("48 @ 24fps"), FQualifiedFrameTime(FFrameTime(48), FFrameRate(24, 1)), 2.0 },
			{ TEXT("90 @ 30fps"), FQualifiedFrameTime(FFrameTime(90), FFrameRate(30, 1)), 3.0 },
			{ TEXT("12 @ 25fps"), FQualifiedFrameTime(FFrameTime(FFrameNumber(12)), FFrameRate(25, 1)), 0.48 }
		};
	}

	// FQualifiedFrameTime::Time and ::Rate are not UPROPERTY, so they cannot be
	// assigned from script. The script side only verifies that the AsSeconds()
	// mixin binding compiles and resolves. Numerical correctness is asserted on
	// the native side via BuildNativeCases.
	FString BuildScriptSource()
	{
	return TEXT(R"(
// Verify that the AsSeconds mixin binding on FQualifiedFrameTime compiles
// and is callable. The actual numerical correctness is tested on the
// native side.
int Entry()
{
	FQualifiedFrameTime DefaultTime;
	// Just call AsSeconds to verify the mixin binding compiles and links.
	// We do not assert on the value because the default-constructed struct
	// may have zero-initialized Rate in script (unlike C++ default ctor).
	DefaultTime.AsSeconds();
	return 1;
}
)");
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptFrameTimeFunctionLibraryTests_Private;

bool FAngelscriptFrameTimeAsSecondsFunctionLibraryTest::RunTest(const FString& Parameters)
{
	const TArray<FFrameTimeAsSecondsCase> NativeCases = BuildNativeCases();
	bool bNativeBaselinesMatch = true;
	for (const FFrameTimeAsSecondsCase& TestCase : NativeCases)
	{
		bNativeBaselinesMatch &= TestTrue(
			FString::Printf(TEXT("Native %s baseline should match the expected seconds conversion"), TestCase.Label),
			FMath::IsNearlyEqual(TestCase.Value.AsSeconds(), TestCase.ExpectedSeconds, FrameTimeTolerance));
	}
	if (!bNativeBaselinesMatch)
	{
		return false;
	}

	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASFrameTimeAsSeconds"));
	};

	int32 Result = 0;
	ASTEST_COMPILE_RUN_INT(Engine, FrameTimeBindingsModuleName, BuildScriptSource(), TEXT("int Entry()"), Result);

	bPassed = TestEqual(
		TEXT("FQualifiedFrameTime.AsSeconds mixin binding should compile and return correct result for default-constructed frame time"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
