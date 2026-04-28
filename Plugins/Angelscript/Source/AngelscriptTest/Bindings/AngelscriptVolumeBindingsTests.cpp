#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "Engine/BlockingVolume.h"
#include "GameFramework/Volume.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptVolumeBindingsTests_Private
{
	static constexpr ANSICHAR VolumeBindingsModuleName[] = "ASVolumeBindingsCompat";
	static constexpr double BoundsTolerance = 0.01;
	static constexpr double DistanceTolerance = 0.01;

	struct FVolumeBindingsBaseline
	{
		FString VolumePath;
		FBoxSphereBounds Bounds;
		FVector ProbePoint = FVector::ZeroVector;
		float DistanceSphereRadius = 0.0f;
		float DistanceToPoint = -314.0f;
		bool bEncompassesWithDefaultRadius = false;
		bool bEncompassesWithDistance = false;
		FLinearColor ScriptBrushColor;
		FColor ExpectedNativeBrushColor;
	};

	FString FormatBoolLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString FormatFloatLiteral(const double Value)
	{
		FString Literal = LexToString(Value);
		if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e")) && !Literal.Contains(TEXT("E")))
		{
			Literal += TEXT(".0");
		}

		return Literal;
	}

	FString FormatVectorLiteral(const FVector& Value)
	{
		return FString::Printf(
			TEXT("FVector(%s, %s, %s)"),
			*FormatFloatLiteral(Value.X),
			*FormatFloatLiteral(Value.Y),
			*FormatFloatLiteral(Value.Z));
	}

	FString FormatLinearColorLiteral(const FLinearColor& Value)
	{
		return FString::Printf(
			TEXT("FLinearColor(%s, %s, %s, %s)"),
			*FormatFloatLiteral(Value.R),
			*FormatFloatLiteral(Value.G),
			*FormatFloatLiteral(Value.B),
			*FormatFloatLiteral(Value.A));
	}

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	FVolumeBindingsBaseline CaptureBaseline(ABlockingVolume& Volume)
	{
		FVolumeBindingsBaseline Baseline;
		Baseline.VolumePath = Volume.GetPathName();
		Baseline.Bounds = Volume.GetBounds();
		Baseline.ProbePoint = Volume.GetActorLocation();
		Baseline.DistanceSphereRadius = 12.5f;
		Baseline.DistanceToPoint = -314.0f;
		Baseline.bEncompassesWithDefaultRadius = Volume.EncompassesPoint(Baseline.ProbePoint);
		Baseline.bEncompassesWithDistance = Volume.EncompassesPoint(
			Baseline.ProbePoint,
			Baseline.DistanceSphereRadius,
			&Baseline.DistanceToPoint);
		Baseline.ScriptBrushColor = FLinearColor(0.125f, 0.5f, 0.875f, 1.0f);
		Baseline.ExpectedNativeBrushColor = Baseline.ScriptBrushColor.ToFColor(true);
		return Baseline;
	}

	bool VerifyNativeBaseline(
		FAutomationTestBase& Test,
		const ABlockingVolume& Volume,
		const FVolumeBindingsBaseline& Baseline)
	{
		bool bPassed = true;
		bPassed &= Test.TestFalse(
			TEXT("Volume native fixture should not start with script-applied brush coloring"),
			Volume.bColored);
		bPassed &= Test.TestTrue(
			TEXT("Volume native fixture should expose a finite bounds origin"),
			Baseline.Bounds.Origin.ContainsNaN() == false);
		bPassed &= Test.TestTrue(
			TEXT("Volume native fixture should expose finite bounds extents"),
			Baseline.Bounds.BoxExtent.ContainsNaN() == false);
		bPassed &= Test.TestTrue(
			TEXT("Volume native fixture should expose a finite bounds radius"),
			FMath::IsFinite(Baseline.Bounds.SphereRadius));
		return bPassed;
	}

	FString BuildVolumeBindingsScript(const FVolumeBindingsBaseline& Baseline)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	AVolume Volume = Cast<AVolume>(FindObject("__VOLUME_PATH__"));
	if (Volume == null)
		return 1;

	FBoxSphereBounds Bounds = Volume.GetBounds();
	if (!Bounds.Origin.Equals(__EXPECTED_BOUNDS_ORIGIN__, __BOUNDS_TOLERANCE__))
		return 2;
	if (!Bounds.BoxExtent.Equals(__EXPECTED_BOUNDS_EXTENT__, __BOUNDS_TOLERANCE__))
		return 4;
	if (!Math::IsNearlyEqual(Bounds.SphereRadius, __EXPECTED_BOUNDS_RADIUS__, __BOUNDS_TOLERANCE__))
		return 8;

	FVector ProbePoint = __PROBE_POINT__;
	if (Volume.EncompassesPoint(ProbePoint) != __EXPECTED_DEFAULT_ENCOMPASSES__)
		return 16;

	float32 DistanceToPoint = __DISTANCE_SENTINEL__;
	if (Volume.EncompassesPoint(ProbePoint, __DISTANCE_RADIUS__, DistanceToPoint) != __EXPECTED_DISTANCE_ENCOMPASSES__)
		return 32;
	if (!Math::IsNearlyEqual(DistanceToPoint, __EXPECTED_DISTANCE__, __DISTANCE_TOLERANCE__))
		return 64;

	Volume.SetBrushColor(__BRUSH_COLOR__);
	return 0;
}
)AS");

		ReplaceToken(Script, TEXT("__VOLUME_PATH__"), Baseline.VolumePath.ReplaceCharWithEscapedChar());
		ReplaceToken(Script, TEXT("__EXPECTED_BOUNDS_ORIGIN__"), FormatVectorLiteral(Baseline.Bounds.Origin));
		ReplaceToken(Script, TEXT("__EXPECTED_BOUNDS_EXTENT__"), FormatVectorLiteral(Baseline.Bounds.BoxExtent));
		ReplaceToken(Script, TEXT("__EXPECTED_BOUNDS_RADIUS__"), FormatFloatLiteral(Baseline.Bounds.SphereRadius));
		ReplaceToken(Script, TEXT("__BOUNDS_TOLERANCE__"), FormatFloatLiteral(BoundsTolerance));
		ReplaceToken(Script, TEXT("__PROBE_POINT__"), FormatVectorLiteral(Baseline.ProbePoint));
		ReplaceToken(Script, TEXT("__EXPECTED_DEFAULT_ENCOMPASSES__"), FormatBoolLiteral(Baseline.bEncompassesWithDefaultRadius));
		ReplaceToken(Script, TEXT("__DISTANCE_SENTINEL__"), FormatFloatLiteral(-314.0));
		ReplaceToken(Script, TEXT("__DISTANCE_RADIUS__"), FormatFloatLiteral(Baseline.DistanceSphereRadius));
		ReplaceToken(Script, TEXT("__EXPECTED_DISTANCE_ENCOMPASSES__"), FormatBoolLiteral(Baseline.bEncompassesWithDistance));
		ReplaceToken(Script, TEXT("__EXPECTED_DISTANCE__"), FormatFloatLiteral(Baseline.DistanceToPoint));
		ReplaceToken(Script, TEXT("__DISTANCE_TOLERANCE__"), FormatFloatLiteral(DistanceTolerance));
		ReplaceToken(Script, TEXT("__BRUSH_COLOR__"), FormatLinearColorLiteral(Baseline.ScriptBrushColor));
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptVolumeBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptVolumeBindingsCompatTest,
	"Angelscript.TestModule.Bindings.VolumeCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptVolumeBindingsCompatTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASVolumeBindingsCompat"));
		ResetSharedCloneEngine(Engine);
	};

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	ABlockingVolume& Volume = Spawner.SpawnActor<ABlockingVolume>();
	const FVolumeBindingsBaseline Baseline = CaptureBaseline(Volume);
	if (!VerifyNativeBaseline(*this, Volume, Baseline))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		VolumeBindingsModuleName,
		BuildVolumeBindingsScript(Baseline));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("AVolume bindings should preserve bounds and point-enclosure parity on the spawned volume fixture"),
		Result,
		0);
	bPassed &= TestTrue(
		TEXT("AVolume SetBrushColor should mark the native volume as explicitly colored"),
		Volume.bColored);
	bPassed &= TestEqual(
		TEXT("AVolume SetBrushColor should write the gamma-corrected native brush color"),
		Volume.BrushColor,
		Baseline.ExpectedNativeBrushColor);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
