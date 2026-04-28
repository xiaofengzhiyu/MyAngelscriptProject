#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Math/Box.h"
#include "Math/Sphere.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathIntersectionBoundsBindingsTests_Private
{
	FString FormatScriptFloatLiteral(const double Value)
	{
		FString Literal = FString::Printf(TEXT("%.9g"), Value);
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

	FString FormatScriptBoxLiteral(const FBox& Value)
	{
		return FString::Printf(TEXT("FBox(%s, %s)"), *FormatScriptVectorLiteral(Value.Min), *FormatScriptVectorLiteral(Value.Max));
	}

	FString FormatScriptBox3fLiteral(const FBox3f& Value)
	{
		return FString::Printf(TEXT("FBox3f(%s, %s)"), *FormatScriptVector3fLiteral(Value.Min), *FormatScriptVector3fLiteral(Value.Max));
	}

	FString FormatScriptSphereLiteral(const FSphere& Value)
	{
		return FString::Printf(TEXT("FSphere(%s, %s)"), *FormatScriptVectorLiteral(Value.Center), *FormatScriptFloatLiteral(Value.W));
	}

	FString FormatScriptSphere3fLiteral(const FSphere3f& Value)
	{
		return FString::Printf(TEXT("FSphere3f(%s, %s)"), *FormatScriptVector3fLiteral(Value.Center), *FormatScriptFloatLiteral(Value.W));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathIntersectionBoundsBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathIntersectionBoundsBindingsTest,
	"Angelscript.TestModule.Bindings.MathIntersectionBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathIntersectionBoundsBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASMathIntersectionBoundsCompat"));
	};

	constexpr double VectorTolerance = 0.001;

	const FBox TestBox(FVector(-2.0, -2.0, -2.0), FVector(2.0, 2.0, 2.0));
	const FVector HitStart(-5.0, 0.5, 0.25);
	const FVector HitEnd(5.0, 0.5, 0.25);
	const FVector MissStart(-5.0, 4.0, 0.0);
	const FVector MissEnd(5.0, 4.0, 0.0);
	const bool bExpectedLineHit = FMath::LineBoxIntersection(TestBox, HitStart, HitEnd, HitEnd - HitStart);
	const bool bExpectedLineMiss = FMath::LineBoxIntersection(TestBox, MissStart, MissEnd, MissEnd - MissStart);

	const FVector SegmentStart(0.0, 0.0, 0.0);
	const FVector SegmentEnd(2.0, 0.0, 0.0);
	const FVector QueryPoint(5.0, 4.0, 0.0);
	const FVector ExpectedClosestPointOnLine = FMath::ClosestPointOnLine(SegmentStart, SegmentEnd, QueryPoint);
	const FVector ExpectedClosestPointOnInfiniteLine = FMath::ClosestPointOnInfiniteLine(SegmentStart, SegmentEnd, QueryPoint);

	const FVector SphereCenterHit(3.5, 0.0, 0.0);
	const FVector SphereCenterMiss(5.5, 0.0, 0.0);
	const double SphereRadiusSquared = 4.0;
	const FSphere HitSphere(SphereCenterHit, 2.0);
	const FSphere MissSphere(SphereCenterMiss, 2.0);
	const bool bExpectedSphereVectorHit = FMath::SphereAABBIntersection(SphereCenterHit, SphereRadiusSquared, TestBox);
	const bool bExpectedSphereVectorMiss = FMath::SphereAABBIntersection(SphereCenterMiss, SphereRadiusSquared, TestBox);
	const bool bExpectedSphereStructHit = FMath::SphereAABBIntersection(HitSphere, TestBox);
	const bool bExpectedSphereStructMiss = FMath::SphereAABBIntersection(MissSphere, TestBox);

	const FBox3f TestBox3f(FVector3f(-1.5f, -1.0f, -1.0f), FVector3f(1.5f, 1.0f, 1.0f));
	const FVector3f SphereCenter3fHit(2.25f, 0.0f, 0.0f);
	const FVector3f SphereCenter3fMiss(3.5f, 0.0f, 0.0f);
	const float SphereRadiusSquared3f = 1.0f;
	const FSphere3f HitSphere3f(SphereCenter3fHit, 1.0f);
	const FSphere3f MissSphere3f(SphereCenter3fMiss, 1.0f);
	const bool bExpectedSphere3fVectorHit = FMath::SphereAABBIntersection(SphereCenter3fHit, SphereRadiusSquared3f, TestBox3f);
	const bool bExpectedSphere3fVectorMiss = FMath::SphereAABBIntersection(SphereCenter3fMiss, SphereRadiusSquared3f, TestBox3f);
	const bool bExpectedSphere3fStructHit = FMath::SphereAABBIntersection(HitSphere3f, TestBox3f);
	const bool bExpectedSphere3fStructMiss = FMath::SphereAABBIntersection(MissSphere3f, TestBox3f);

	bPassed &= TestTrue(TEXT("Native LineBoxIntersection baseline should report a hit for the through-box segment"), bExpectedLineHit);
	bPassed &= TestFalse(TEXT("Native LineBoxIntersection baseline should report a miss for the offset segment"), bExpectedLineMiss);
	bPassed &= TestTrue(
		TEXT("Native closest-point baselines should distinguish finite-line and infinite-line semantics for this query"),
		!ExpectedClosestPointOnLine.Equals(ExpectedClosestPointOnInfiniteLine, 0.0));
	bPassed &= TestTrue(TEXT("Native sphere/AABB vector baseline should report a hit"), bExpectedSphereVectorHit);
	bPassed &= TestFalse(TEXT("Native sphere/AABB vector baseline should report a miss"), bExpectedSphereVectorMiss);
	bPassed &= TestTrue(TEXT("Native sphere/AABB FSphere baseline should report a hit"), bExpectedSphereStructHit);
	bPassed &= TestFalse(TEXT("Native sphere/AABB FSphere baseline should report a miss"), bExpectedSphereStructMiss);
	bPassed &= TestTrue(TEXT("Native sphere/AABB FVector3f baseline should report a hit"), bExpectedSphere3fVectorHit);
	bPassed &= TestFalse(TEXT("Native sphere/AABB FVector3f baseline should report a miss"), bExpectedSphere3fVectorMiss);
	bPassed &= TestTrue(TEXT("Native sphere/AABB FSphere3f baseline should report a hit"), bExpectedSphere3fStructHit);
	bPassed &= TestFalse(TEXT("Native sphere/AABB FSphere3f baseline should report a miss"), bExpectedSphere3fStructMiss);
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
int Entry()
{
	const FBox TestBox = $BOX$;
	if (Math::LineBoxIntersection(TestBox, $HIT_START$, $HIT_END$, $HIT_DELTA$) != $EXPECTED_LINE_HIT$)
		return 10;
	if (Math::LineBoxIntersection(TestBox, $MISS_START$, $MISS_END$, $MISS_DELTA$) != $EXPECTED_LINE_MISS$)
		return 20;

	const FVector ClosestOnLine = Math::ClosestPointOnLine($SEGMENT_START$, $SEGMENT_END$, $QUERY_POINT$);
	if (!ClosestOnLine.Equals($EXPECTED_CLOSEST_ON_LINE$, $VECTOR_TOLERANCE$))
		return 30;

	const FVector ClosestOnInfiniteLine = Math::ClosestPointOnInfiniteLine($SEGMENT_START$, $SEGMENT_END$, $QUERY_POINT$);
	if (!ClosestOnInfiniteLine.Equals($EXPECTED_CLOSEST_ON_INFINITE_LINE$, $VECTOR_TOLERANCE$))
		return 40;

	if (Math::SphereAABBIntersection($SPHERE_CENTER_HIT$, $SPHERE_RADIUS_SQUARED$, TestBox) != $EXPECTED_SPHERE_VECTOR_HIT$)
		return 50;
	if (Math::SphereAABBIntersection($SPHERE_CENTER_MISS$, $SPHERE_RADIUS_SQUARED$, TestBox) != $EXPECTED_SPHERE_VECTOR_MISS$)
		return 60;
	if (Math::SphereAABBIntersection($SPHERE_HIT$, TestBox) != $EXPECTED_SPHERE_STRUCT_HIT$)
		return 70;
	if (Math::SphereAABBIntersection($SPHERE_MISS$, TestBox) != $EXPECTED_SPHERE_STRUCT_MISS$)
		return 80;

	const FBox3f TestBox3f = $BOX3F$;
	if (Math::SphereAABBIntersection($SPHERE_CENTER_3F_HIT$, $SPHERE_RADIUS_SQUARED_3F$, TestBox3f) != $EXPECTED_SPHERE3F_VECTOR_HIT$)
		return 90;
	if (Math::SphereAABBIntersection($SPHERE_CENTER_3F_MISS$, $SPHERE_RADIUS_SQUARED_3F$, TestBox3f) != $EXPECTED_SPHERE3F_VECTOR_MISS$)
		return 100;
	if (Math::SphereAABBIntersection($SPHERE_3F_HIT$, TestBox3f) != $EXPECTED_SPHERE3F_STRUCT_HIT$)
		return 110;
	if (Math::SphereAABBIntersection($SPHERE_3F_MISS$, TestBox3f) != $EXPECTED_SPHERE3F_STRUCT_MISS$)
		return 120;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("$BOX$"), *FormatScriptBoxLiteral(TestBox));
	Script.ReplaceInline(TEXT("$HIT_START$"), *FormatScriptVectorLiteral(HitStart));
	Script.ReplaceInline(TEXT("$HIT_END$"), *FormatScriptVectorLiteral(HitEnd));
	Script.ReplaceInline(TEXT("$HIT_DELTA$"), *FormatScriptVectorLiteral(HitEnd - HitStart));
	Script.ReplaceInline(TEXT("$MISS_START$"), *FormatScriptVectorLiteral(MissStart));
	Script.ReplaceInline(TEXT("$MISS_END$"), *FormatScriptVectorLiteral(MissEnd));
	Script.ReplaceInline(TEXT("$MISS_DELTA$"), *FormatScriptVectorLiteral(MissEnd - MissStart));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_HIT$"), *FormatScriptBoolLiteral(bExpectedLineHit));
	Script.ReplaceInline(TEXT("$EXPECTED_LINE_MISS$"), *FormatScriptBoolLiteral(bExpectedLineMiss));
	Script.ReplaceInline(TEXT("$SEGMENT_START$"), *FormatScriptVectorLiteral(SegmentStart));
	Script.ReplaceInline(TEXT("$SEGMENT_END$"), *FormatScriptVectorLiteral(SegmentEnd));
	Script.ReplaceInline(TEXT("$QUERY_POINT$"), *FormatScriptVectorLiteral(QueryPoint));
	Script.ReplaceInline(TEXT("$EXPECTED_CLOSEST_ON_LINE$"), *FormatScriptVectorLiteral(ExpectedClosestPointOnLine));
	Script.ReplaceInline(TEXT("$EXPECTED_CLOSEST_ON_INFINITE_LINE$"), *FormatScriptVectorLiteral(ExpectedClosestPointOnInfiniteLine));
	Script.ReplaceInline(TEXT("$VECTOR_TOLERANCE$"), *FormatScriptFloatLiteral(VectorTolerance));
	Script.ReplaceInline(TEXT("$SPHERE_CENTER_HIT$"), *FormatScriptVectorLiteral(SphereCenterHit));
	Script.ReplaceInline(TEXT("$SPHERE_CENTER_MISS$"), *FormatScriptVectorLiteral(SphereCenterMiss));
	Script.ReplaceInline(TEXT("$SPHERE_RADIUS_SQUARED$"), *FormatScriptFloatLiteral(SphereRadiusSquared));
	Script.ReplaceInline(TEXT("$SPHERE_HIT$"), *FormatScriptSphereLiteral(HitSphere));
	Script.ReplaceInline(TEXT("$SPHERE_MISS$"), *FormatScriptSphereLiteral(MissSphere));
	Script.ReplaceInline(TEXT("$EXPECTED_SPHERE_VECTOR_HIT$"), *FormatScriptBoolLiteral(bExpectedSphereVectorHit));
	Script.ReplaceInline(TEXT("$EXPECTED_SPHERE_VECTOR_MISS$"), *FormatScriptBoolLiteral(bExpectedSphereVectorMiss));
	Script.ReplaceInline(TEXT("$EXPECTED_SPHERE_STRUCT_HIT$"), *FormatScriptBoolLiteral(bExpectedSphereStructHit));
	Script.ReplaceInline(TEXT("$EXPECTED_SPHERE_STRUCT_MISS$"), *FormatScriptBoolLiteral(bExpectedSphereStructMiss));
	Script.ReplaceInline(TEXT("$BOX3F$"), *FormatScriptBox3fLiteral(TestBox3f));
	Script.ReplaceInline(TEXT("$SPHERE_CENTER_3F_HIT$"), *FormatScriptVector3fLiteral(SphereCenter3fHit));
	Script.ReplaceInline(TEXT("$SPHERE_CENTER_3F_MISS$"), *FormatScriptVector3fLiteral(SphereCenter3fMiss));
	Script.ReplaceInline(TEXT("$SPHERE_RADIUS_SQUARED_3F$"), *FormatScriptFloatLiteral(SphereRadiusSquared3f));
	Script.ReplaceInline(TEXT("$SPHERE_3F_HIT$"), *FormatScriptSphere3fLiteral(HitSphere3f));
	Script.ReplaceInline(TEXT("$SPHERE_3F_MISS$"), *FormatScriptSphere3fLiteral(MissSphere3f));
	Script.ReplaceInline(TEXT("$EXPECTED_SPHERE3F_VECTOR_HIT$"), *FormatScriptBoolLiteral(bExpectedSphere3fVectorHit));
	Script.ReplaceInline(TEXT("$EXPECTED_SPHERE3F_VECTOR_MISS$"), *FormatScriptBoolLiteral(bExpectedSphere3fVectorMiss));
	Script.ReplaceInline(TEXT("$EXPECTED_SPHERE3F_STRUCT_HIT$"), *FormatScriptBoolLiteral(bExpectedSphere3fStructHit));
	Script.ReplaceInline(TEXT("$EXPECTED_SPHERE3F_STRUCT_MISS$"), *FormatScriptBoolLiteral(bExpectedSphere3fStructMiss));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASMathIntersectionBoundsCompat", Script);
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
		TEXT("Math geometry intersection helpers should match the native baseline for box and sphere queries"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
