#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Math/Plane.h"
#include "Math/Sphere.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathRayAndConeBindingsTests_Private
{
	static constexpr ANSICHAR MathRayLineSphereModuleName[] = "ASMathRayLineSphereCompat";
	static constexpr ANSICHAR MathConeBoundsModuleName[] = "ASMathConeBoundsCompat";

	FString FormatScriptFloatLiteral(const double Value)
	{
		FString Literal = FString::Printf(TEXT("%.17g"), Value);
		if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e")) && !Literal.Contains(TEXT("E")))
		{
			Literal += TEXT(".0");
		}

		return Literal;
	}

	FString FormatScriptBoolLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString FormatScriptVectorLiteral(const FVector& Value)
	{
		return FString::Printf(
			TEXT("FVector(%s, %s, %s)"),
			*FormatScriptFloatLiteral(Value.X),
			*FormatScriptFloatLiteral(Value.Y),
			*FormatScriptFloatLiteral(Value.Z));
	}

	FString FormatScriptVector3fLiteral(const FVector3f& Value)
	{
		return FString::Printf(
			TEXT("FVector3f(%s, %s, %s)"),
			*FormatScriptFloatLiteral(Value.X),
			*FormatScriptFloatLiteral(Value.Y),
			*FormatScriptFloatLiteral(Value.Z));
	}

	FString FormatScriptPlaneLiteral(const FPlane& Value)
	{
		return FString::Printf(
			TEXT("FPlane(%s, %s)"),
			*FormatScriptVectorLiteral(Value.GetOrigin()),
			*FormatScriptVectorLiteral(Value.GetNormal()));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathRayAndConeBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathRayLineSphereBindingsTest,
	"Angelscript.TestModule.Bindings.MathRayAndCone.RayLineSphereCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathConeBoundsBindingsTest,
	"Angelscript.TestModule.Bindings.MathRayAndCone.ConeBoundsCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathRayLineSphereBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathRayLineSphereCompat"));
	};

	constexpr double VectorTolerance = 0.001;

	const FPlane TestPlane(FVector(0.0, 0.0, 5.0), FVector(0.0, 0.0, 1.0));
	const FVector RayOrigin(0.0, 0.0, 1.0);
	const FVector RayDirection(0.0, 0.0, 1.0);
	const FVector ExpectedRayPlaneIntersection = FMath::RayPlaneIntersection(RayOrigin, RayDirection, TestPlane);

	const FVector3f LinePoint1(-4.0f, 1.5f, 0.5f);
	const FVector3f LinePoint2(6.0f, 1.5f, 0.5f);
	const FVector3f PlaneOrigin3f(1.0f, 0.0f, 0.0f);
	const FVector3f PlaneNormal3f(1.0f, 0.0f, 0.0f);
	const FVector3f ExpectedLinePlaneIntersection3f = FMath::LinePlaneIntersection(LinePoint1, LinePoint2, PlaneOrigin3f, PlaneNormal3f);

	const FVector SphereStart64(-3.0, 0.0, 0.0);
	const FVector SphereDirection64(1.0, 0.0, 0.0);
	const double SphereLength64 = 12.0;
	const FVector SphereOrigin64Hit(5.0, 0.0, 0.0);
	const FVector SphereOrigin64Miss(5.0, 3.5, 0.0);
	const double SphereRadius64 = 1.5;
	const bool bExpectedLineSphere64Hit = FMath::LineSphereIntersection(SphereStart64, SphereDirection64, SphereLength64, SphereOrigin64Hit, SphereRadius64);
	const bool bExpectedLineSphere64Miss = FMath::LineSphereIntersection(SphereStart64, SphereDirection64, SphereLength64, SphereOrigin64Miss, SphereRadius64);

	const FVector3f SphereStart3f(-2.0f, 1.0f, 0.0f);
	const FVector3f SphereDirection3f(0.0f, 1.0f, 0.0f);
	const float SphereLength3f = 8.0f;
	const FVector3f SphereOrigin3fHit(-2.0f, 5.0f, 0.0f);
	const FVector3f SphereOrigin3fMiss(-2.0f, 5.0f, 3.0f);
	const float SphereRadius3f = 1.25f;
	const bool bExpectedLineSphere3fHit = FMath::LineSphereIntersection(SphereStart3f, SphereDirection3f, SphereLength3f, SphereOrigin3fHit, SphereRadius3f);
	const bool bExpectedLineSphere3fMiss = FMath::LineSphereIntersection(SphereStart3f, SphereDirection3f, SphereLength3f, SphereOrigin3fMiss, SphereRadius3f);

	bPassed &= TestTrue(
		TEXT("Native RayPlaneIntersection baseline should land on the normalized plane at Z=5 for this upward ray"),
		ExpectedRayPlaneIntersection.Equals(FVector(0.0, 0.0, 5.0), 0.0));
	bPassed &= TestTrue(
		TEXT("Native FVector3f LinePlaneIntersection baseline should hit the X=1 plane"),
		FVector(ExpectedLinePlaneIntersection3f).Equals(FVector(1.0, 1.5, 0.5), 0.0));
	bPassed &= TestTrue(
		TEXT("Native LineSphereIntersection double baseline should report a hit for the through-sphere segment"),
		bExpectedLineSphere64Hit);
	bPassed &= TestFalse(
		TEXT("Native LineSphereIntersection double baseline should report a miss for the offset sphere"),
		bExpectedLineSphere64Miss);
	bPassed &= TestTrue(
		TEXT("Native LineSphereIntersection float baseline should report a hit for the through-sphere segment"),
		bExpectedLineSphere3fHit);
	bPassed &= TestFalse(
		TEXT("Native LineSphereIntersection float baseline should report a miss for the offset sphere"),
		bExpectedLineSphere3fMiss);
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
int Entry()
{
	const FVector RayPlaneHit = Math::RayPlaneIntersection($RAY_ORIGIN$, $RAY_DIRECTION$, $PLANE$);
	if (!RayPlaneHit.Equals($EXPECTED_RAY_HIT$, $VECTOR_TOLERANCE$))
		return 10;

	const FVector3f LinePlaneHit3f = Math::LinePlaneIntersection($LINE_POINT1_3F$, $LINE_POINT2_3F$, $PLANE_ORIGIN_3F$, $PLANE_NORMAL_3F$);
	if (Math::Abs(LinePlaneHit3f.X - $EXPECTED_LINE_HIT_3F_X$) > $VECTOR_TOLERANCE$)
		return 20;
	if (Math::Abs(LinePlaneHit3f.Y - $EXPECTED_LINE_HIT_3F_Y$) > $VECTOR_TOLERANCE$)
		return 30;
	if (Math::Abs(LinePlaneHit3f.Z - $EXPECTED_LINE_HIT_3F_Z$) > $VECTOR_TOLERANCE$)
		return 40;

	if (Math::LineSphereIntersection($SPHERE_START_64$, $SPHERE_DIRECTION_64$, $SPHERE_LENGTH_64$, $SPHERE_ORIGIN_64_HIT$, $SPHERE_RADIUS_64$) != $EXPECTED_LINE_SPHERE64_HIT$)
		return 50;
	if (Math::LineSphereIntersection($SPHERE_START_64$, $SPHERE_DIRECTION_64$, $SPHERE_LENGTH_64$, $SPHERE_ORIGIN_64_MISS$, $SPHERE_RADIUS_64$) != $EXPECTED_LINE_SPHERE64_MISS$)
		return 60;

	if (Math::LineSphereIntersection($SPHERE_START_3F$, $SPHERE_DIRECTION_3F$, $SPHERE_LENGTH_3F$, $SPHERE_ORIGIN_3F_HIT$, $SPHERE_RADIUS_3F$) != $EXPECTED_LINE_SPHERE3F_HIT$)
		return 70;
	if (Math::LineSphereIntersection($SPHERE_START_3F$, $SPHERE_DIRECTION_3F$, $SPHERE_LENGTH_3F$, $SPHERE_ORIGIN_3F_MISS$, $SPHERE_RADIUS_3F$) != $EXPECTED_LINE_SPHERE3F_MISS$)
		return 80;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("$RAY_ORIGIN$"), *FormatScriptVectorLiteral(RayOrigin));
	Script.ReplaceInline(TEXT("$RAY_DIRECTION$"), *FormatScriptVectorLiteral(RayDirection));
	Script.ReplaceInline(TEXT("$PLANE$"), *FormatScriptPlaneLiteral(TestPlane));
	Script.ReplaceInline(TEXT("$EXPECTED_RAY_HIT$"), *FormatScriptVectorLiteral(ExpectedRayPlaneIntersection));
	Script.ReplaceInline(TEXT("$LINE_POINT1_3F$"), *FormatScriptVector3fLiteral(LinePoint1));
	Script.ReplaceInline(TEXT("$LINE_POINT2_3F$"), *FormatScriptVector3fLiteral(LinePoint2));
	Script.ReplaceInline(TEXT("$PLANE_ORIGIN_3F$"), *FormatScriptVector3fLiteral(PlaneOrigin3f));
	Script.ReplaceInline(TEXT("$PLANE_NORMAL_3F$"), *FormatScriptVector3fLiteral(PlaneNormal3f));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_HIT_3F_X$"), *FormatScriptFloatLiteral(ExpectedLinePlaneIntersection3f.X));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_HIT_3F_Y$"), *FormatScriptFloatLiteral(ExpectedLinePlaneIntersection3f.Y));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_HIT_3F_Z$"), *FormatScriptFloatLiteral(ExpectedLinePlaneIntersection3f.Z));
	Script.ReplaceInline(TEXT("$SPHERE_START_64$"), *FormatScriptVectorLiteral(SphereStart64));
	Script.ReplaceInline(TEXT("$SPHERE_DIRECTION_64$"), *FormatScriptVectorLiteral(SphereDirection64));
	Script.ReplaceInline(TEXT("$SPHERE_LENGTH_64$"), *FormatScriptFloatLiteral(SphereLength64));
	Script.ReplaceInline(TEXT("$SPHERE_ORIGIN_64_HIT$"), *FormatScriptVectorLiteral(SphereOrigin64Hit));
	Script.ReplaceInline(TEXT("$SPHERE_ORIGIN_64_MISS$"), *FormatScriptVectorLiteral(SphereOrigin64Miss));
	Script.ReplaceInline(TEXT("$SPHERE_RADIUS_64$"), *FormatScriptFloatLiteral(SphereRadius64));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_SPHERE64_HIT$"), *FormatScriptBoolLiteral(bExpectedLineSphere64Hit));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_SPHERE64_MISS$"), *FormatScriptBoolLiteral(bExpectedLineSphere64Miss));
	Script.ReplaceInline(TEXT("$SPHERE_START_3F$"), *FormatScriptVector3fLiteral(SphereStart3f));
	Script.ReplaceInline(TEXT("$SPHERE_DIRECTION_3F$"), *FormatScriptVector3fLiteral(SphereDirection3f));
	Script.ReplaceInline(TEXT("$SPHERE_LENGTH_3F$"), *FormatScriptFloatLiteral(SphereLength3f));
	Script.ReplaceInline(TEXT("$SPHERE_ORIGIN_3F_HIT$"), *FormatScriptVector3fLiteral(SphereOrigin3fHit));
	Script.ReplaceInline(TEXT("$SPHERE_ORIGIN_3F_MISS$"), *FormatScriptVector3fLiteral(SphereOrigin3fMiss));
	Script.ReplaceInline(TEXT("$SPHERE_RADIUS_3F$"), *FormatScriptFloatLiteral(SphereRadius3f));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_SPHERE3F_HIT$"), *FormatScriptBoolLiteral(bExpectedLineSphere3fHit));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_SPHERE3F_MISS$"), *FormatScriptBoolLiteral(bExpectedLineSphere3fMiss));
	Script.ReplaceInline(TEXT("$VECTOR_TOLERANCE$"), *FormatScriptFloatLiteral(VectorTolerance));

	asIScriptModule* Module = BuildModule(*this, Engine, MathRayLineSphereModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Ray/plane, FVector3f line/plane, and line/sphere math bindings should match the native baseline"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptMathConeBoundsBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathConeBoundsCompat"));
	};

	constexpr double VectorTolerance = 0.001;
	constexpr double RadiusTolerance = 0.0001;

	const FVector ConeOrigin(2.0, -1.0, 0.5);
	const FVector ConeDirection(0.0, 0.0, 1.0);
	const double ConeRadius = 8.0;
	const double CosConeAngle = 0.5;
	const double SinConeAngle = 0.8660254037844386;
	const FSphere ExpectedConeBounds = FMath::ComputeBoundingSphereForCone(
		ConeOrigin,
		ConeDirection,
		ConeRadius,
		CosConeAngle,
		SinConeAngle);

	const FVector3f ConeOrigin3f(-3.0f, 2.5f, 1.0f);
	const FVector3f ConeDirection3f(0.0f, 1.0f, 0.0f);
	const float ConeRadius3f = 5.0f;
	const float CosConeAngle3f = 0.70710677f;
	const float SinConeAngle3f = 0.70710677f;
	const FSphere3f ExpectedConeBounds3f = FMath::ComputeBoundingSphereForCone(
		ConeOrigin3f,
		ConeDirection3f,
		ConeRadius3f,
		CosConeAngle3f,
		SinConeAngle3f);

	bPassed &= TestTrue(
		TEXT("Native ComputeBoundingSphereForCone double baseline should produce a positive radius"),
		ExpectedConeBounds.W > 0.0);
	bPassed &= TestTrue(
		TEXT("Native ComputeBoundingSphereForCone float baseline should produce a positive radius"),
		ExpectedConeBounds3f.W > 0.0f);
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
int Entry()
{
	const FSphere ConeBounds = Math::ComputeBoundingSphereForCone($CONE_ORIGIN$, $CONE_DIRECTION$, $CONE_RADIUS$, $COS_CONE_ANGLE$, $SIN_CONE_ANGLE$);
	if (!ConeBounds.Center.Equals($EXPECTED_CONE_CENTER$, $VECTOR_TOLERANCE$))
		return 10;
	if (!Math::IsNearlyEqual(ConeBounds.W, $EXPECTED_CONE_RADIUS$, $RADIUS_TOLERANCE$))
		return 20;

	const FSphere3f ConeBounds3f = Math::ComputeBoundingSphereForCone($CONE_ORIGIN_3F$, $CONE_DIRECTION_3F$, $CONE_RADIUS_3F$, $COS_CONE_ANGLE_3F$, $SIN_CONE_ANGLE_3F$);
	if (Math::Abs(ConeBounds3f.Center.X - $EXPECTED_CONE_CENTER_3F_X$) > $VECTOR_TOLERANCE$)
		return 30;
	if (Math::Abs(ConeBounds3f.Center.Y - $EXPECTED_CONE_CENTER_3F_Y$) > $VECTOR_TOLERANCE$)
		return 40;
	if (Math::Abs(ConeBounds3f.Center.Z - $EXPECTED_CONE_CENTER_3F_Z$) > $VECTOR_TOLERANCE$)
		return 50;
	if (!Math::IsNearlyEqual(ConeBounds3f.W, $EXPECTED_CONE_RADIUS_3F$, $RADIUS_TOLERANCE$))
		return 60;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("$CONE_ORIGIN$"), *FormatScriptVectorLiteral(ConeOrigin));
	Script.ReplaceInline(TEXT("$CONE_DIRECTION$"), *FormatScriptVectorLiteral(ConeDirection));
	Script.ReplaceInline(TEXT("$CONE_RADIUS$"), *FormatScriptFloatLiteral(ConeRadius));
	Script.ReplaceInline(TEXT("$COS_CONE_ANGLE$"), *FormatScriptFloatLiteral(CosConeAngle));
	Script.ReplaceInline(TEXT("$SIN_CONE_ANGLE$"), *FormatScriptFloatLiteral(SinConeAngle));
	Script.ReplaceInline(TEXT("$EXPECTED_CONE_CENTER$"), *FormatScriptVectorLiteral(ExpectedConeBounds.Center));
	Script.ReplaceInline(TEXT("$EXPECTED_CONE_RADIUS$"), *FormatScriptFloatLiteral(ExpectedConeBounds.W));
	Script.ReplaceInline(TEXT("$CONE_ORIGIN_3F$"), *FormatScriptVector3fLiteral(ConeOrigin3f));
	Script.ReplaceInline(TEXT("$CONE_DIRECTION_3F$"), *FormatScriptVector3fLiteral(ConeDirection3f));
	Script.ReplaceInline(TEXT("$CONE_RADIUS_3F$"), *FormatScriptFloatLiteral(ConeRadius3f));
	Script.ReplaceInline(TEXT("$COS_CONE_ANGLE_3F$"), *FormatScriptFloatLiteral(CosConeAngle3f));
	Script.ReplaceInline(TEXT("$SIN_CONE_ANGLE_3F$"), *FormatScriptFloatLiteral(SinConeAngle3f));
	Script.ReplaceInline(TEXT("$EXPECTED_CONE_CENTER_3F_X$"), *FormatScriptFloatLiteral(ExpectedConeBounds3f.Center.X));
	Script.ReplaceInline(TEXT("$EXPECTED_CONE_CENTER_3F_Y$"), *FormatScriptFloatLiteral(ExpectedConeBounds3f.Center.Y));
	Script.ReplaceInline(TEXT("$EXPECTED_CONE_CENTER_3F_Z$"), *FormatScriptFloatLiteral(ExpectedConeBounds3f.Center.Z));
	Script.ReplaceInline(TEXT("$EXPECTED_CONE_RADIUS_3F$"), *FormatScriptFloatLiteral(ExpectedConeBounds3f.W));
	Script.ReplaceInline(TEXT("$VECTOR_TOLERANCE$"), *FormatScriptFloatLiteral(VectorTolerance));
	Script.ReplaceInline(TEXT("$RADIUS_TOLERANCE$"), *FormatScriptFloatLiteral(RadiusTolerance));

	asIScriptModule* Module = BuildModule(*this, Engine, MathConeBoundsModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Cone bounding-sphere math bindings should match the native baseline across double and float paths"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
