#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Math/Sphere.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptSphereRelationBindingsTests_Private
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

	FString FormatScriptSphereLiteral(const FSphere& Value)
	{
		return FString::Printf(TEXT("FSphere(%s, %s)"), *FormatScriptVectorLiteral(Value.Center), *FormatScriptFloatLiteral(Value.W));
	}

	FString FormatScriptSphere3fLiteral(const FSphere3f& Value)
	{
		return FString::Printf(TEXT("FSphere3f(%s, %s)"), *FormatScriptVector3fLiteral(Value.Center), *FormatScriptFloatLiteral(Value.W));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptSphereRelationBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSphereRelationBindingsTest,
	"Angelscript.TestModule.Bindings.SphereRelations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptSphereRelationBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASSphereRelationCompat"));
	};

	constexpr double DoubleTolerance = 0.001;
	constexpr float FloatTolerance = 0.001f;

	const FSphere AddLeft(FVector(-2.0, 1.0, 0.0), 3.0);
	const FSphere AddRight(FVector(6.0, -1.0, 0.0), 2.5);
	const FSphere ExpectedAddedSphere = AddLeft + AddRight;
	FSphere ExpectedAccumulatedSphere = AddLeft;
	ExpectedAccumulatedSphere += AddRight;

	const FSphere OuterSphere(FVector(0.0, 0.0, 0.0), 5.0);
	const FSphere CloseMatchSphere(FVector(0.0004, -0.0003, 0.0002), 5.0004);
	const FSphere DifferentSphere(FVector(2.0, 0.0, 0.0), 3.5);
	const FSphere InnerSphere(FVector(1.0, 0.5, 0.0), 1.0);
	const FSphere OverlapSphere(FVector(8.0, 0.0, 0.0), 4.5);
	const FSphere MissSphere(FVector(12.0, 0.0, 0.0), 1.5);
	const FVector InsidePoint(2.0, 1.0, 0.0);
	const FVector OutsidePoint(6.5, 0.0, 0.0);

	const bool bExpectedEqualsClose = OuterSphere.Equals(CloseMatchSphere, DoubleTolerance);
	const bool bExpectedEqualsDifferent = OuterSphere.Equals(DifferentSphere, DoubleTolerance);
	const bool bExpectedInnerInsideOuter = InnerSphere.IsInside(OuterSphere, DoubleTolerance);
	const bool bExpectedOuterInsideInner = OuterSphere.IsInside(InnerSphere, DoubleTolerance);
	const bool bExpectedPointInside = OuterSphere.IsInside(InsidePoint, DoubleTolerance);
	const bool bExpectedPointOutside = OuterSphere.IsInside(OutsidePoint, DoubleTolerance);
	const bool bExpectedIntersectsOverlap = OuterSphere.Intersects(OverlapSphere, DoubleTolerance);
	const bool bExpectedIntersectsMiss = OuterSphere.Intersects(MissSphere, DoubleTolerance);

	const FSphere3f AddLeft3f(FVector3f(-1.5f, 0.0f, 2.0f), 2.0f);
	const FSphere3f AddRight3f(FVector3f(4.0f, 2.0f, -1.0f), 1.5f);
	const FSphere3f ExpectedAddedSphere3f = AddLeft3f + AddRight3f;
	FSphere3f ExpectedAccumulatedSphere3f = AddLeft3f;
	ExpectedAccumulatedSphere3f += AddRight3f;

	const FSphere3f OuterSphere3f(FVector3f(0.0f, 0.0f, 0.0f), 4.0f);
	const FSphere3f CloseMatchSphere3f(FVector3f(0.0004f, -0.0002f, 0.0001f), 4.0003f);
	const FSphere3f DifferentSphere3f(FVector3f(1.5f, 0.0f, 0.0f), 2.0f);
	const FSphere3f InnerSphere3f(FVector3f(1.0f, 0.5f, 0.0f), 0.75f);
	const FSphere3f OverlapSphere3f(FVector3f(6.0f, 0.0f, 0.0f), 2.5f);
	const FSphere3f MissSphere3f(FVector3f(9.0f, 0.0f, 0.0f), 0.75f);

	const bool bExpectedEqualsClose3f = OuterSphere3f.Equals(CloseMatchSphere3f, FloatTolerance);
	const bool bExpectedEqualsDifferent3f = OuterSphere3f.Equals(DifferentSphere3f, FloatTolerance);
	const bool bExpectedInnerInsideOuter3f = InnerSphere3f.IsInside(OuterSphere3f, FloatTolerance);
	const bool bExpectedOuterInsideInner3f = OuterSphere3f.IsInside(InnerSphere3f, FloatTolerance);
	const bool bExpectedIntersectsOverlap3f = OuterSphere3f.Intersects(OverlapSphere3f, FloatTolerance);
	const bool bExpectedIntersectsMiss3f = OuterSphere3f.Intersects(MissSphere3f, FloatTolerance);

	bPassed &= TestTrue(TEXT("Native FSphere additive baseline should match operator+ and operator+="), ExpectedAddedSphere.Equals(ExpectedAccumulatedSphere, DoubleTolerance));
	bPassed &= TestTrue(TEXT("Native FSphere equality baseline should accept the near-equal sphere"), bExpectedEqualsClose);
	bPassed &= TestFalse(TEXT("Native FSphere equality baseline should reject the different sphere"), bExpectedEqualsDifferent);
	bPassed &= TestTrue(TEXT("Native FSphere nested containment baseline should distinguish inner and outer directionality"), bExpectedInnerInsideOuter != bExpectedOuterInsideInner);
	bPassed &= TestTrue(TEXT("Native FSphere point containment baseline should accept the inside point"), bExpectedPointInside);
	bPassed &= TestFalse(TEXT("Native FSphere point containment baseline should reject the outside point"), bExpectedPointOutside);
	bPassed &= TestTrue(TEXT("Native FSphere intersection baseline should accept the overlapping sphere"), bExpectedIntersectsOverlap);
	bPassed &= TestFalse(TEXT("Native FSphere intersection baseline should reject the separated sphere"), bExpectedIntersectsMiss);

	bPassed &= TestTrue(TEXT("Native FSphere3f additive baseline should match operator+ and operator+="), ExpectedAddedSphere3f.Equals(ExpectedAccumulatedSphere3f, FloatTolerance));
	bPassed &= TestTrue(TEXT("Native FSphere3f equality baseline should accept the near-equal sphere"), bExpectedEqualsClose3f);
	bPassed &= TestFalse(TEXT("Native FSphere3f equality baseline should reject the different sphere"), bExpectedEqualsDifferent3f);
	bPassed &= TestTrue(TEXT("Native FSphere3f nested containment baseline should distinguish inner and outer directionality"), bExpectedInnerInsideOuter3f != bExpectedOuterInsideInner3f);
	bPassed &= TestTrue(TEXT("Native FSphere3f intersection baseline should accept the overlapping sphere"), bExpectedIntersectsOverlap3f);
	bPassed &= TestFalse(TEXT("Native FSphere3f intersection baseline should reject the separated sphere"), bExpectedIntersectsMiss3f);
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
int Entry()
{
	const FSphere AddedSphere = $ADD_LEFT$ + $ADD_RIGHT$;
	if (!AddedSphere.Equals($EXPECTED_ADDED$, $DOUBLE_TOLERANCE$))
		return 10;

	FSphere AccumulatedSphere = $ADD_LEFT$;
	AccumulatedSphere += $ADD_RIGHT$;
	if (!AccumulatedSphere.Equals($EXPECTED_ACCUMULATED$, $DOUBLE_TOLERANCE$))
		return 20;

	if ($OUTER_SPHERE$.Equals($CLOSE_MATCH_SPHERE$, $DOUBLE_TOLERANCE$) != $EXPECTED_EQUALS_CLOSE$)
		return 30;
	if ($OUTER_SPHERE$.Equals($DIFFERENT_SPHERE$, $DOUBLE_TOLERANCE$) != $EXPECTED_EQUALS_DIFFERENT$)
		return 40;
	if ($INNER_SPHERE$.IsInside($OUTER_SPHERE$, $DOUBLE_TOLERANCE$) != $EXPECTED_INNER_INSIDE_OUTER$)
		return 50;
	if ($OUTER_SPHERE$.IsInside($INNER_SPHERE$, $DOUBLE_TOLERANCE$) != $EXPECTED_OUTER_INSIDE_INNER$)
		return 60;
	if ($OUTER_SPHERE$.IsInside($INSIDE_POINT$, $DOUBLE_TOLERANCE$) != $EXPECTED_POINT_INSIDE$)
		return 70;
	if ($OUTER_SPHERE$.IsInside($OUTSIDE_POINT$, $DOUBLE_TOLERANCE$) != $EXPECTED_POINT_OUTSIDE$)
		return 80;
	if ($OUTER_SPHERE$.Intersects($OVERLAP_SPHERE$, $DOUBLE_TOLERANCE$) != $EXPECTED_INTERSECTS_OVERLAP$)
		return 90;
	if ($OUTER_SPHERE$.Intersects($MISS_SPHERE$, $DOUBLE_TOLERANCE$) != $EXPECTED_INTERSECTS_MISS$)
		return 100;

	const FSphere3f AddedSphere3f = $ADD_LEFT_3F$ + $ADD_RIGHT_3F$;
	if (!AddedSphere3f.Equals($EXPECTED_ADDED_3F$, $FLOAT_TOLERANCE$))
		return 110;

	FSphere3f AccumulatedSphere3f = $ADD_LEFT_3F$;
	AccumulatedSphere3f += $ADD_RIGHT_3F$;
	if (!AccumulatedSphere3f.Equals($EXPECTED_ACCUMULATED_3F$, $FLOAT_TOLERANCE$))
		return 120;

	if ($OUTER_SPHERE_3F$.Equals($CLOSE_MATCH_SPHERE_3F$, $FLOAT_TOLERANCE$) != $EXPECTED_EQUALS_CLOSE_3F$)
		return 130;
	if ($OUTER_SPHERE_3F$.Equals($DIFFERENT_SPHERE_3F$, $FLOAT_TOLERANCE$) != $EXPECTED_EQUALS_DIFFERENT_3F$)
		return 140;
	if ($INNER_SPHERE_3F$.IsInside($OUTER_SPHERE_3F$, $FLOAT_TOLERANCE$) != $EXPECTED_INNER_INSIDE_OUTER_3F$)
		return 150;
	if ($OUTER_SPHERE_3F$.IsInside($INNER_SPHERE_3F$, $FLOAT_TOLERANCE$) != $EXPECTED_OUTER_INSIDE_INNER_3F$)
		return 160;
	if ($OUTER_SPHERE_3F$.Intersects($OVERLAP_SPHERE_3F$, $FLOAT_TOLERANCE$) != $EXPECTED_INTERSECTS_OVERLAP_3F$)
		return 170;
	if ($OUTER_SPHERE_3F$.Intersects($MISS_SPHERE_3F$, $FLOAT_TOLERANCE$) != $EXPECTED_INTERSECTS_MISS_3F$)
		return 180;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("$ADD_LEFT$"), *FormatScriptSphereLiteral(AddLeft));
	Script.ReplaceInline(TEXT("$ADD_RIGHT$"), *FormatScriptSphereLiteral(AddRight));
	Script.ReplaceInline(TEXT("$EXPECTED_ADDED$"), *FormatScriptSphereLiteral(ExpectedAddedSphere));
	Script.ReplaceInline(TEXT("$EXPECTED_ACCUMULATED$"), *FormatScriptSphereLiteral(ExpectedAccumulatedSphere));
	Script.ReplaceInline(TEXT("$OUTER_SPHERE$"), *FormatScriptSphereLiteral(OuterSphere));
	Script.ReplaceInline(TEXT("$CLOSE_MATCH_SPHERE$"), *FormatScriptSphereLiteral(CloseMatchSphere));
	Script.ReplaceInline(TEXT("$DIFFERENT_SPHERE$"), *FormatScriptSphereLiteral(DifferentSphere));
	Script.ReplaceInline(TEXT("$INNER_SPHERE$"), *FormatScriptSphereLiteral(InnerSphere));
	Script.ReplaceInline(TEXT("$OVERLAP_SPHERE$"), *FormatScriptSphereLiteral(OverlapSphere));
	Script.ReplaceInline(TEXT("$MISS_SPHERE$"), *FormatScriptSphereLiteral(MissSphere));
	Script.ReplaceInline(TEXT("$INSIDE_POINT$"), *FormatScriptVectorLiteral(InsidePoint));
	Script.ReplaceInline(TEXT("$OUTSIDE_POINT$"), *FormatScriptVectorLiteral(OutsidePoint));
	Script.ReplaceInline(TEXT("$EXPECTED_EQUALS_CLOSE$"), *FormatScriptBoolLiteral(bExpectedEqualsClose));
	Script.ReplaceInline(TEXT("$EXPECTED_EQUALS_DIFFERENT$"), *FormatScriptBoolLiteral(bExpectedEqualsDifferent));
	Script.ReplaceInline(TEXT("$EXPECTED_INNER_INSIDE_OUTER$"), *FormatScriptBoolLiteral(bExpectedInnerInsideOuter));
	Script.ReplaceInline(TEXT("$EXPECTED_OUTER_INSIDE_INNER$"), *FormatScriptBoolLiteral(bExpectedOuterInsideInner));
	Script.ReplaceInline(TEXT("$EXPECTED_POINT_INSIDE$"), *FormatScriptBoolLiteral(bExpectedPointInside));
	Script.ReplaceInline(TEXT("$EXPECTED_POINT_OUTSIDE$"), *FormatScriptBoolLiteral(bExpectedPointOutside));
	Script.ReplaceInline(TEXT("$EXPECTED_INTERSECTS_OVERLAP$"), *FormatScriptBoolLiteral(bExpectedIntersectsOverlap));
	Script.ReplaceInline(TEXT("$EXPECTED_INTERSECTS_MISS$"), *FormatScriptBoolLiteral(bExpectedIntersectsMiss));
	Script.ReplaceInline(TEXT("$DOUBLE_TOLERANCE$"), *FormatScriptFloatLiteral(DoubleTolerance));

	Script.ReplaceInline(TEXT("$ADD_LEFT_3F$"), *FormatScriptSphere3fLiteral(AddLeft3f));
	Script.ReplaceInline(TEXT("$ADD_RIGHT_3F$"), *FormatScriptSphere3fLiteral(AddRight3f));
	Script.ReplaceInline(TEXT("$EXPECTED_ADDED_3F$"), *FormatScriptSphere3fLiteral(ExpectedAddedSphere3f));
	Script.ReplaceInline(TEXT("$EXPECTED_ACCUMULATED_3F$"), *FormatScriptSphere3fLiteral(ExpectedAccumulatedSphere3f));
	Script.ReplaceInline(TEXT("$OUTER_SPHERE_3F$"), *FormatScriptSphere3fLiteral(OuterSphere3f));
	Script.ReplaceInline(TEXT("$CLOSE_MATCH_SPHERE_3F$"), *FormatScriptSphere3fLiteral(CloseMatchSphere3f));
	Script.ReplaceInline(TEXT("$DIFFERENT_SPHERE_3F$"), *FormatScriptSphere3fLiteral(DifferentSphere3f));
	Script.ReplaceInline(TEXT("$INNER_SPHERE_3F$"), *FormatScriptSphere3fLiteral(InnerSphere3f));
	Script.ReplaceInline(TEXT("$OVERLAP_SPHERE_3F$"), *FormatScriptSphere3fLiteral(OverlapSphere3f));
	Script.ReplaceInline(TEXT("$MISS_SPHERE_3F$"), *FormatScriptSphere3fLiteral(MissSphere3f));
	Script.ReplaceInline(TEXT("$EXPECTED_EQUALS_CLOSE_3F$"), *FormatScriptBoolLiteral(bExpectedEqualsClose3f));
	Script.ReplaceInline(TEXT("$EXPECTED_EQUALS_DIFFERENT_3F$"), *FormatScriptBoolLiteral(bExpectedEqualsDifferent3f));
	Script.ReplaceInline(TEXT("$EXPECTED_INNER_INSIDE_OUTER_3F$"), *FormatScriptBoolLiteral(bExpectedInnerInsideOuter3f));
	Script.ReplaceInline(TEXT("$EXPECTED_OUTER_INSIDE_INNER_3F$"), *FormatScriptBoolLiteral(bExpectedOuterInsideInner3f));
	Script.ReplaceInline(TEXT("$EXPECTED_INTERSECTS_OVERLAP_3F$"), *FormatScriptBoolLiteral(bExpectedIntersectsOverlap3f));
	Script.ReplaceInline(TEXT("$EXPECTED_INTERSECTS_MISS_3F$"), *FormatScriptBoolLiteral(bExpectedIntersectsMiss3f));
	Script.ReplaceInline(TEXT("$FLOAT_TOLERANCE$"), *FormatScriptFloatLiteral(FloatTolerance));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASSphereRelationCompat", Script);
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
		TEXT("Sphere relation helpers should match the native baseline for additive, containment, and intersection semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
