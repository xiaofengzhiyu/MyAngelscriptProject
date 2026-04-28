#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Math/Box.h"
#include "Math/BoxSphereBounds.h"
#include "Math/Plane.h"
#include "Math/Sphere.h"
#include "Math/Transform.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGeometryBoundsDoubleBindingsTest,
	"Angelscript.TestModule.Bindings.GeometryBounds.DoublePrecisionCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGeometryBoundsFloatBindingsTest,
	"Angelscript.TestModule.Bindings.GeometryBounds.FloatPrecisionCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptGeometryBoundsBindingsTests_Private
{
	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, bool& OutValue)
	{
		OutValue = Context.GetReturnByte() != 0;
		return true;
	}

	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, float& OutValue)
	{
		OutValue = Context.GetReturnFloat();
		return true;
	}

	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, double& OutValue)
	{
		OutValue = Context.GetReturnDouble();
		return true;
	}

	template <typename TValue>
	bool ReadReturnValue(FAutomationTestBase& Test, asIScriptContext& Context, TValue& OutValue)
	{
		void* ReturnValueAddress = Context.GetAddressOfReturnValue();
		if (!Test.TestNotNull(TEXT("Geometry bounds bindings test should expose the return value storage"), ReturnValueAddress))
		{
			return false;
		}

		OutValue = *static_cast<TValue*>(ReturnValueAddress);
		return true;
	}

	template <typename TValue>
	bool ExecuteValueFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		TValue& OutValue)
	{
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(TEXT("Geometry bounds bindings test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Geometry bounds bindings test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Geometry bounds bindings test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Geometry bounds bindings test saw a script exception: %s"),
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			Context->Release();
			return false;
		}

		const bool bReadReturnValue = ReadReturnValue(Test, *Context, OutValue);
		Context->Release();
		return bReadReturnValue;
	}

	bool VerifyFloat(FAutomationTestBase& Test, const TCHAR* What, float Actual, float Expected, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, FMath::Abs(Actual - Expected) <= Tolerance);
	}

	bool VerifyDouble(FAutomationTestBase& Test, const TCHAR* What, double Actual, double Expected, double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, FMath::Abs(Actual - Expected) <= Tolerance);
	}

	bool VerifyVector(FAutomationTestBase& Test, const TCHAR* What, const FVector& Actual, const FVector& Expected, double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}

	bool VerifyVector3f(FAutomationTestBase& Test, const TCHAR* What, const FVector3f& Actual, const FVector3f& Expected, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Equals(Expected, Tolerance));
	}

	bool VerifyBox(FAutomationTestBase& Test, const TCHAR* What, const FBox& Actual, const FBox& Expected, double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Min.Equals(Expected.Min, Tolerance) && Actual.Max.Equals(Expected.Max, Tolerance));
	}

	bool VerifyBox3f(FAutomationTestBase& Test, const TCHAR* What, const FBox3f& Actual, const FBox3f& Expected, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Min.Equals(Expected.Min, Tolerance) && Actual.Max.Equals(Expected.Max, Tolerance));
	}

	bool VerifySphere(FAutomationTestBase& Test, const TCHAR* What, const FSphere& Actual, const FSphere& Expected, double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Center.Equals(Expected.Center, Tolerance) && FMath::Abs(Actual.W - Expected.W) <= Tolerance);
	}

	bool VerifySphere3f(FAutomationTestBase& Test, const TCHAR* What, const FSphere3f& Actual, const FSphere3f& Expected, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(What, Actual.Center.Equals(Expected.Center, Tolerance) && FMath::Abs(Actual.W - Expected.W) <= Tolerance);
	}

	bool VerifyBounds(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FBoxSphereBounds& Actual,
		const FBoxSphereBounds& Expected,
		double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(
			What,
			Actual.Origin.Equals(Expected.Origin, Tolerance)
				&& Actual.BoxExtent.Equals(Expected.BoxExtent, Tolerance)
				&& FMath::Abs(Actual.SphereRadius - Expected.SphereRadius) <= Tolerance);
	}

	bool VerifyBounds3f(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FBoxSphereBounds3f& Actual,
		const FBoxSphereBounds3f& Expected,
		float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(
			What,
			Actual.Origin.Equals(Expected.Origin, Tolerance)
				&& Actual.BoxExtent.Equals(Expected.BoxExtent, Tolerance)
				&& FMath::Abs(Actual.SphereRadius - Expected.SphereRadius) <= Tolerance);
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptGeometryBoundsBindingsTests_Private;

bool FAngelscriptGeometryBoundsDoubleBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASGeometryBoundsDoubleCompat",
		TEXT(R"AS(
FBox GetExpandedBox()
{
	FBox Box = FBox(FVector(-2.0, -1.0, 0.0), FVector(4.0, 5.0, 6.0));
	Box = Box.ExpandBy(FVector(1.0, 2.0, 3.0));
	Box += FVector(10.0, -4.0, 12.0);
	return Box;
}

FBox GetOverlapBox()
{
	return GetExpandedBox().Overlap(FBox::BuildAABB(FVector(3.0, 2.0, 4.0), FVector(3.0, 2.0, 4.0)));
}

FBox GetConvertedBox()
{
	return FBox(FBox3f(FVector3f(-1.5, -2.5, -3.5), FVector3f(4.5, 5.5, 6.5)));
}

FSphere GetTransformedSphere()
{
	return FSphere(FVector(1.0, 2.0, 3.0), 4.0).TransformBy(FTransform(FRotator(0.0, 90.0, 0.0), FVector(5.0, -1.0, 2.0), FVector(2.0, 2.0, 2.0)));
}

FSphere GetConvertedSphere()
{
	return FSphere(FSphere3f(FVector3f(2.0, -1.0, 0.5), 6.5));
}

float64 GetSphereVolume()
{
	return GetTransformedSphere().GetVolume();
}

FPlane GetNormalizedPlane()
{
	return FPlane(FVector(0.0, 0.0, 5.0), FVector(0.0, 0.0, 10.0));
}

FPlane GetConvertedPlane()
{
	return FPlane(FPlane4f(FVector3f(1.0, 0.0, 3.0), FVector3f(0.0, 0.0, 1.0)));
}

FVector GetPlaneRayIntersection()
{
	return GetNormalizedPlane().RayPlaneIntersection(FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, 1.0));
}

bool GetPlaneSegmentIntersects()
{
	FVector Hit;
	return GetNormalizedPlane().SegmentPlaneIntersection(FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, 10.0), Hit);
}

FVector GetPlaneSegmentHit()
{
	FVector Hit = FVector::ZeroVector;
	GetNormalizedPlane().SegmentPlaneIntersection(FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, 10.0), Hit);
	return Hit;
}

FBoxSphereBounds GetExpandedBounds()
{
	return FBoxSphereBounds(FVector(1.0, 2.0, 3.0), FVector(4.0, 5.0, 6.0), 7.0).ExpandBy(1.5);
}

FBoxSphereBounds GetConvertedBounds()
{
	return FBoxSphereBounds(FBoxSphereBounds3f(
		FBox3f(FVector3f(-1.0, -2.0, -3.0), FVector3f(5.0, 6.0, 7.0)),
		FSphere3f(FVector3f(2.0, 2.0, 2.0), 3.5)));
}

float64 GetBoundsSquaredDistance()
{
	return GetExpandedBounds().ComputeSquaredDistanceFromBoxToPoint(FVector(20.0, 2.0, 3.0));
}

bool GetBoundsIntersections()
{
	FBoxSphereBounds A = GetExpandedBounds();
	FBoxSphereBounds B = FBoxSphereBounds(FBox(FVector(0.0, -1.0, -1.0), FVector(7.0, 5.0, 7.0)), FSphere(FVector(3.0, 2.0, 3.0), 4.0));
	return FBoxSphereBounds::BoxesIntersect(A, B) && FBoxSphereBounds::SpheresIntersect(A, B, 0.001);
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	FBox ExpectedExpandedBox(FVector(-2.0, -1.0, 0.0), FVector(4.0, 5.0, 6.0));
	ExpectedExpandedBox = ExpectedExpandedBox.ExpandBy(FVector(1.0, 2.0, 3.0));
	ExpectedExpandedBox += FVector(10.0, -4.0, 12.0);
	const FBox ExpectedOverlapBox = ExpectedExpandedBox.Overlap(FBox::BuildAABB(FVector(3.0, 2.0, 4.0), FVector(3.0, 2.0, 4.0)));
	const FBox ExpectedConvertedBox(FBox3f(FVector3f(-1.5f, -2.5f, -3.5f), FVector3f(4.5f, 5.5f, 6.5f)));
	const FSphere ExpectedTransformedSphere = FSphere(FVector(1.0, 2.0, 3.0), 4.0).TransformBy(
		FTransform(FRotator(0.0, 90.0, 0.0), FVector(5.0, -1.0, 2.0), FVector(2.0, 2.0, 2.0)));
	const FSphere ExpectedConvertedSphere(FSphere3f(FVector3f(2.0f, -1.0f, 0.5f), 6.5f));
	const FPlane ExpectedNormalizedPlane(FVector(0.0, 0.0, 5.0), FVector(0.0, 0.0, 1.0));
	const FPlane ExpectedConvertedPlane(FPlane4f(FVector3f(1.0f, 0.0f, 3.0f), FVector3f(0.0f, 0.0f, 1.0f)));
	const FVector ExpectedPlaneIntersection = FMath::RayPlaneIntersection(FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, 1.0), ExpectedNormalizedPlane);
	FVector ExpectedSegmentHit = FVector::ZeroVector;
	const bool bExpectedPlaneSegmentIntersects = FMath::SegmentPlaneIntersection(
		FVector(0.0, 0.0, 1.0),
		FVector(0.0, 0.0, 10.0),
		ExpectedNormalizedPlane,
		ExpectedSegmentHit);
	const FBoxSphereBounds ExpectedExpandedBounds(FVector(1.0, 2.0, 3.0), FVector(4.0, 5.0, 6.0), 7.0);
	const FBoxSphereBounds ExpectedExpandedBoundsResult = ExpectedExpandedBounds.ExpandBy(1.5);
	const FBoxSphereBounds ExpectedConvertedBounds(FBoxSphereBounds3f(
		FBox3f(FVector3f(-1.0f, -2.0f, -3.0f), FVector3f(5.0f, 6.0f, 7.0f)),
		FSphere3f(FVector3f(2.0f, 2.0f, 2.0f), 3.5f)));
	const double ExpectedBoundsSquaredDistance = ExpectedExpandedBoundsResult.ComputeSquaredDistanceFromBoxToPoint(FVector(20.0, 2.0, 3.0));
	const FBoxSphereBounds BoundsIntersectionBaseline(
		FBox(FVector(0.0, -1.0, -1.0), FVector(7.0, 5.0, 7.0)),
		FSphere(FVector(3.0, 2.0, 3.0), 4.0));
	const bool bExpectedBoundsIntersections =
		FBoxSphereBounds::BoxesIntersect(ExpectedExpandedBoundsResult, BoundsIntersectionBaseline)
		&& FBoxSphereBounds::SpheresIntersect(ExpectedExpandedBoundsResult, BoundsIntersectionBaseline, 0.001);

	FBox ScriptExpandedBox(ForceInit);
	FBox ScriptOverlapBox(ForceInit);
	FBox ScriptConvertedBox(ForceInit);
	FSphere ScriptTransformedSphere(ForceInit);
	FSphere ScriptConvertedSphere(ForceInit);
	FPlane ScriptNormalizedPlane(ForceInit);
	FPlane ScriptConvertedPlane(ForceInit);
	FVector ScriptPlaneIntersection = FVector::ZeroVector;
	bool bScriptPlaneSegmentIntersects = false;
	FVector ScriptPlaneSegmentHit = FVector::ZeroVector;
	FBoxSphereBounds ScriptExpandedBounds(ForceInit);
	FBoxSphereBounds ScriptConvertedBounds(ForceInit);
	double ScriptSphereVolume = 0.0;
	double ScriptBoundsSquaredDistance = 0.0;
	bool bScriptBoundsIntersections = false;

	asIScriptFunction* ExpandedBoxFunction = GetFunctionByDecl(*this, *Module, TEXT("FBox GetExpandedBox()"));
	asIScriptFunction* OverlapBoxFunction = GetFunctionByDecl(*this, *Module, TEXT("FBox GetOverlapBox()"));
	asIScriptFunction* ConvertedBoxFunction = GetFunctionByDecl(*this, *Module, TEXT("FBox GetConvertedBox()"));
	asIScriptFunction* TransformedSphereFunction = GetFunctionByDecl(*this, *Module, TEXT("FSphere GetTransformedSphere()"));
	asIScriptFunction* ConvertedSphereFunction = GetFunctionByDecl(*this, *Module, TEXT("FSphere GetConvertedSphere()"));
	asIScriptFunction* SphereVolumeFunction = GetFunctionByDecl(*this, *Module, TEXT("float64 GetSphereVolume()"));
	asIScriptFunction* NormalizedPlaneFunction = GetFunctionByDecl(*this, *Module, TEXT("FPlane GetNormalizedPlane()"));
	asIScriptFunction* ConvertedPlaneFunction = GetFunctionByDecl(*this, *Module, TEXT("FPlane GetConvertedPlane()"));
	asIScriptFunction* PlaneIntersectionFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetPlaneRayIntersection()"));
	asIScriptFunction* PlaneSegmentIntersectsFunction = GetFunctionByDecl(*this, *Module, TEXT("bool GetPlaneSegmentIntersects()"));
	asIScriptFunction* PlaneSegmentHitFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector GetPlaneSegmentHit()"));
	asIScriptFunction* ExpandedBoundsFunction = GetFunctionByDecl(*this, *Module, TEXT("FBoxSphereBounds GetExpandedBounds()"));
	asIScriptFunction* ConvertedBoundsFunction = GetFunctionByDecl(*this, *Module, TEXT("FBoxSphereBounds GetConvertedBounds()"));
	asIScriptFunction* BoundsSquaredDistanceFunction = GetFunctionByDecl(*this, *Module, TEXT("float64 GetBoundsSquaredDistance()"));
	asIScriptFunction* BoundsIntersectionsFunction = GetFunctionByDecl(*this, *Module, TEXT("bool GetBoundsIntersections()"));
	if (ExpandedBoxFunction == nullptr
		|| OverlapBoxFunction == nullptr
		|| ConvertedBoxFunction == nullptr
		|| TransformedSphereFunction == nullptr
		|| ConvertedSphereFunction == nullptr
		|| SphereVolumeFunction == nullptr
		|| NormalizedPlaneFunction == nullptr
		|| ConvertedPlaneFunction == nullptr
		|| PlaneIntersectionFunction == nullptr
		|| PlaneSegmentIntersectsFunction == nullptr
		|| PlaneSegmentHitFunction == nullptr
		|| ExpandedBoundsFunction == nullptr
		|| ConvertedBoundsFunction == nullptr
		|| BoundsSquaredDistanceFunction == nullptr
		|| BoundsIntersectionsFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *ExpandedBoxFunction, ScriptExpandedBox) &&
		ExecuteValueFunction(*this, Engine, *OverlapBoxFunction, ScriptOverlapBox) &&
		ExecuteValueFunction(*this, Engine, *ConvertedBoxFunction, ScriptConvertedBox) &&
		ExecuteValueFunction(*this, Engine, *TransformedSphereFunction, ScriptTransformedSphere) &&
		ExecuteValueFunction(*this, Engine, *ConvertedSphereFunction, ScriptConvertedSphere) &&
		ExecuteValueFunction(*this, Engine, *SphereVolumeFunction, ScriptSphereVolume) &&
		ExecuteValueFunction(*this, Engine, *NormalizedPlaneFunction, ScriptNormalizedPlane) &&
		ExecuteValueFunction(*this, Engine, *ConvertedPlaneFunction, ScriptConvertedPlane) &&
		ExecuteValueFunction(*this, Engine, *PlaneIntersectionFunction, ScriptPlaneIntersection) &&
		ExecuteValueFunction(*this, Engine, *PlaneSegmentIntersectsFunction, bScriptPlaneSegmentIntersects) &&
		ExecuteValueFunction(*this, Engine, *PlaneSegmentHitFunction, ScriptPlaneSegmentHit) &&
		ExecuteValueFunction(*this, Engine, *ExpandedBoundsFunction, ScriptExpandedBounds) &&
		ExecuteValueFunction(*this, Engine, *ConvertedBoundsFunction, ScriptConvertedBounds) &&
		ExecuteValueFunction(*this, Engine, *BoundsSquaredDistanceFunction, ScriptBoundsSquaredDistance) &&
		ExecuteValueFunction(*this, Engine, *BoundsIntersectionsFunction, bScriptBoundsIntersections);
	if (!bExecutedAll)
	{
		return false;
	}

	bPassed &= VerifyBox(*this, TEXT("FBox expand-plus-point semantics should match the native deterministic baseline"), ScriptExpandedBox, ExpectedExpandedBox);
	bPassed &= VerifyBox(*this, TEXT("FBox::Overlap and FBox::BuildAABB should preserve the native overlap result"), ScriptOverlapBox, ExpectedOverlapBox);
	bPassed &= VerifyBox(*this, TEXT("FBox(FBox3f) should preserve the float-to-double conversion path"), ScriptConvertedBox, ExpectedConvertedBox);
	bPassed &= VerifySphere(*this, TEXT("FSphere::TransformBy should preserve the native transformed center and radius"), ScriptTransformedSphere, ExpectedTransformedSphere, 0.001);
	bPassed &= VerifySphere(*this, TEXT("FSphere(FSphere3f) should preserve the float-to-double conversion path"), ScriptConvertedSphere, ExpectedConvertedSphere, 0.001);
	bPassed &= VerifyDouble(
		*this,
		TEXT("FSphere::GetVolume should preserve the volume of the already-verified script sphere return value"),
		ScriptSphereVolume,
		ScriptTransformedSphere.GetVolume(),
		0.01);
	bPassed &= VerifyVector(*this, TEXT("FPlane(FVector, FVector) should normalize the supplied normal vector"), ScriptNormalizedPlane.GetNormal(), ExpectedNormalizedPlane.GetNormal());
	bPassed &= VerifyVector(*this, TEXT("FPlane(FPlane4f) should preserve the converted origin"), ScriptConvertedPlane.GetOrigin(), ExpectedConvertedPlane.GetOrigin(), 0.001);
	bPassed &= VerifyVector(*this, TEXT("FPlane::RayPlaneIntersection should preserve the native intersection point"), ScriptPlaneIntersection, ExpectedPlaneIntersection, 0.001);
	bPassed &= TestEqual(TEXT("FPlane::SegmentPlaneIntersection should report the same hit boolean as the native baseline"), bScriptPlaneSegmentIntersects, bExpectedPlaneSegmentIntersects);
	bPassed &= VerifyVector(*this, TEXT("FPlane::SegmentPlaneIntersection should preserve the native segment hit point"), ScriptPlaneSegmentHit, ExpectedSegmentHit, 0.001);
	bPassed &= VerifyBounds(*this, TEXT("FBoxSphereBounds::ExpandBy should preserve the native origin, extent, and sphere radius"), ScriptExpandedBounds, ExpectedExpandedBoundsResult, 0.001);
	bPassed &= VerifyBounds(*this, TEXT("FBoxSphereBounds(FBoxSphereBounds3f) should preserve the float-to-double conversion path"), ScriptConvertedBounds, ExpectedConvertedBounds, 0.001);
	bPassed &= VerifyDouble(*this, TEXT("FBoxSphereBounds::ComputeSquaredDistanceFromBoxToPoint should preserve the native distance"), ScriptBoundsSquaredDistance, ExpectedBoundsSquaredDistance, 0.001);
	bPassed &= TestEqual(TEXT("FBoxSphereBounds::BoxesIntersect and SpheresIntersect should preserve the native boolean result"), bScriptBoundsIntersections, bExpectedBoundsIntersections);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptGeometryBoundsFloatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASGeometryBoundsFloatCompat",
		TEXT(R"AS(
FBox3f GetExpandedBox3f()
{
	FBox3f Box = FBox3f(FVector3f(-2.0, -1.0, 0.0), FVector3f(4.0, 5.0, 6.0));
	Box += FVector3f(8.0, -3.0, 5.0);
	return Box;
}

FBox3f GetConvertedBox3f()
{
	return FBox3f(FBox(FVector(-1.25, -2.25, -3.25), FVector(4.25, 5.25, 6.25)));
}

bool GetBox3fIntersection()
{
	return GetExpandedBox3f().Intersect(FBox3f::BuildAABB(FVector3f(2.0, 3.0, 3.0), FVector3f(2.0, 2.0, 2.0)));
}

FVector3f GetBox3fClosestPoint()
{
	return GetExpandedBox3f().GetClosestPointTo(FVector3f(20.0, -10.0, 3.0));
}

FSphere3f GetConvertedSphere3f()
{
	return FSphere3f(FSphere(FVector(3.0, 1.0, -2.0), 5.5));
}

float32 GetSphere3fVolume()
{
	return GetConvertedSphere3f().GetVolume();
}

FPlane4f GetConvertedPlane4f()
{
	return FPlane4f(FPlane(FVector(0.0, 0.0, 2.0), FVector(0.0, 0.0, 1.0)));
}

float32 GetPlane4fDot()
{
	return GetConvertedPlane4f().PlaneDot(FVector3f(0.0, 0.0, 4.0));
}

FBoxSphereBounds3f GetExpandedBounds3f()
{
	return FBoxSphereBounds3f(FVector3f(1.0, 2.0, 3.0), FVector3f(4.0, 5.0, 6.0), 7.0).ExpandBy(2.0);
}

FBoxSphereBounds3f GetConvertedBounds3f()
{
	return FBoxSphereBounds3f(FBoxSphereBounds(FBox(FVector(-1.0, -2.0, -3.0), FVector(5.0, 6.0, 7.0)), FSphere(FVector(2.0, 2.0, 2.0), 3.5)));
}

float32 GetBounds3fSquaredDistance()
{
	return GetExpandedBounds3f().ComputeSquaredDistanceFromBoxToPoint(FVector3f(20.0, 2.0, 3.0));
}

bool GetBounds3fIntersections()
{
	FBoxSphereBounds3f A = GetExpandedBounds3f();
	FBoxSphereBounds3f B = FBoxSphereBounds3f(FBox3f(FVector3f(0.0, -1.0, -1.0), FVector3f(7.0, 5.0, 7.0)), FSphere3f(FVector3f(3.0, 2.0, 3.0), 4.0));
	return FBoxSphereBounds3f::BoxesIntersect(A, B) && FBoxSphereBounds3f::SpheresIntersect(A, B, 0.001);
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	FBox3f ExpectedExpandedBox3f(FVector3f(-2.0f, -1.0f, 0.0f), FVector3f(4.0f, 5.0f, 6.0f));
	ExpectedExpandedBox3f += FVector3f(8.0f, -3.0f, 5.0f);
	const FBox3f ExpectedConvertedBox3f(FBox(FVector(-1.25, -2.25, -3.25), FVector(4.25, 5.25, 6.25)));
	const bool bExpectedBox3fIntersection = ExpectedExpandedBox3f.Intersect(FBox3f::BuildAABB(FVector3f(2.0f, 3.0f, 3.0f), FVector3f(2.0f, 2.0f, 2.0f)));
	const FVector3f ExpectedBox3fClosestPoint = ExpectedExpandedBox3f.GetClosestPointTo(FVector3f(20.0f, -10.0f, 3.0f));
	const FSphere3f ExpectedConvertedSphere3f(FSphere(FVector(3.0, 1.0, -2.0), 5.5));
	const float ExpectedSphere3fVolume = ExpectedConvertedSphere3f.GetVolume();
	const FPlane4f ExpectedConvertedPlane4f(FPlane(FVector(0.0, 0.0, 2.0), FVector(0.0, 0.0, 1.0)));
	const float ExpectedPlane4fDot = ExpectedConvertedPlane4f.PlaneDot(FVector3f(0.0f, 0.0f, 4.0f));
	const FBoxSphereBounds3f ExpectedExpandedBounds3f(FVector3f(1.0f, 2.0f, 3.0f), FVector3f(4.0f, 5.0f, 6.0f), 7.0f);
	const FBoxSphereBounds3f ExpectedExpandedBounds3fResult = ExpectedExpandedBounds3f.ExpandBy(2.0f);
	const FBoxSphereBounds3f ExpectedConvertedBounds3f(FBoxSphereBounds(
		FBox(FVector(-1.0, -2.0, -3.0), FVector(5.0, 6.0, 7.0)),
		FSphere(FVector(2.0, 2.0, 2.0), 3.5)));
	const float ExpectedBounds3fSquaredDistance = ExpectedExpandedBounds3fResult.ComputeSquaredDistanceFromBoxToPoint(FVector3f(20.0f, 2.0f, 3.0f));
	const FBoxSphereBounds3f Bounds3fIntersectionBaseline(
		FBox3f(FVector3f(0.0f, -1.0f, -1.0f), FVector3f(7.0f, 5.0f, 7.0f)),
		FSphere3f(FVector3f(3.0f, 2.0f, 3.0f), 4.0f));
	const bool bExpectedBounds3fIntersections =
		FBoxSphereBounds3f::BoxesIntersect(ExpectedExpandedBounds3fResult, Bounds3fIntersectionBaseline)
		&& FBoxSphereBounds3f::SpheresIntersect(ExpectedExpandedBounds3fResult, Bounds3fIntersectionBaseline, 0.001f);

	FBox3f ScriptExpandedBox3f(ForceInit);
	FBox3f ScriptConvertedBox3f(ForceInit);
	bool bScriptBox3fIntersection = false;
	FVector3f ScriptBox3fClosestPoint = FVector3f::ZeroVector;
	FSphere3f ScriptConvertedSphere3f(ForceInit);
	float ScriptSphere3fVolume = 0.0f;
	FPlane4f ScriptConvertedPlane4f(ForceInit);
	float ScriptPlane4fDot = 0.0f;
	FBoxSphereBounds3f ScriptExpandedBounds3f(ForceInit);
	FBoxSphereBounds3f ScriptConvertedBounds3f(ForceInit);
	float ScriptBounds3fSquaredDistance = 0.0f;
	bool bScriptBounds3fIntersections = false;

	asIScriptFunction* ExpandedBox3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FBox3f GetExpandedBox3f()"));
	asIScriptFunction* ConvertedBox3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FBox3f GetConvertedBox3f()"));
	asIScriptFunction* Box3fIntersectionFunction = GetFunctionByDecl(*this, *Module, TEXT("bool GetBox3fIntersection()"));
	asIScriptFunction* Box3fClosestPointFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetBox3fClosestPoint()"));
	asIScriptFunction* ConvertedSphere3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FSphere3f GetConvertedSphere3f()"));
	asIScriptFunction* Sphere3fVolumeFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetSphere3fVolume()"));
	asIScriptFunction* ConvertedPlane4fFunction = GetFunctionByDecl(*this, *Module, TEXT("FPlane4f GetConvertedPlane4f()"));
	asIScriptFunction* Plane4fDotFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetPlane4fDot()"));
	asIScriptFunction* ExpandedBounds3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FBoxSphereBounds3f GetExpandedBounds3f()"));
	asIScriptFunction* ConvertedBounds3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FBoxSphereBounds3f GetConvertedBounds3f()"));
	asIScriptFunction* Bounds3fSquaredDistanceFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetBounds3fSquaredDistance()"));
	asIScriptFunction* Bounds3fIntersectionsFunction = GetFunctionByDecl(*this, *Module, TEXT("bool GetBounds3fIntersections()"));
	if (ExpandedBox3fFunction == nullptr
		|| ConvertedBox3fFunction == nullptr
		|| Box3fIntersectionFunction == nullptr
		|| Box3fClosestPointFunction == nullptr
		|| ConvertedSphere3fFunction == nullptr
		|| Sphere3fVolumeFunction == nullptr
		|| ConvertedPlane4fFunction == nullptr
		|| Plane4fDotFunction == nullptr
		|| ExpandedBounds3fFunction == nullptr
		|| ConvertedBounds3fFunction == nullptr
		|| Bounds3fSquaredDistanceFunction == nullptr
		|| Bounds3fIntersectionsFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *ExpandedBox3fFunction, ScriptExpandedBox3f) &&
		ExecuteValueFunction(*this, Engine, *ConvertedBox3fFunction, ScriptConvertedBox3f) &&
		ExecuteValueFunction(*this, Engine, *Box3fIntersectionFunction, bScriptBox3fIntersection) &&
		ExecuteValueFunction(*this, Engine, *Box3fClosestPointFunction, ScriptBox3fClosestPoint) &&
		ExecuteValueFunction(*this, Engine, *ConvertedSphere3fFunction, ScriptConvertedSphere3f) &&
		ExecuteValueFunction(*this, Engine, *Sphere3fVolumeFunction, ScriptSphere3fVolume) &&
		ExecuteValueFunction(*this, Engine, *ConvertedPlane4fFunction, ScriptConvertedPlane4f) &&
		ExecuteValueFunction(*this, Engine, *Plane4fDotFunction, ScriptPlane4fDot) &&
		ExecuteValueFunction(*this, Engine, *ExpandedBounds3fFunction, ScriptExpandedBounds3f) &&
		ExecuteValueFunction(*this, Engine, *ConvertedBounds3fFunction, ScriptConvertedBounds3f) &&
		ExecuteValueFunction(*this, Engine, *Bounds3fSquaredDistanceFunction, ScriptBounds3fSquaredDistance) &&
		ExecuteValueFunction(*this, Engine, *Bounds3fIntersectionsFunction, bScriptBounds3fIntersections);
	if (!bExecutedAll)
	{
		return false;
	}

	bPassed &= VerifyBox3f(*this, TEXT("FBox3f point-expansion semantics should match the native deterministic baseline"), ScriptExpandedBox3f, ExpectedExpandedBox3f);
	bPassed &= VerifyBox3f(*this, TEXT("FBox3f(FBox) should preserve the double-to-float conversion path"), ScriptConvertedBox3f, ExpectedConvertedBox3f);
	bPassed &= TestEqual(TEXT("FBox3f::Intersect and FBox3f::BuildAABB should preserve the native boolean result"), bScriptBox3fIntersection, bExpectedBox3fIntersection);
	bPassed &= VerifyVector3f(*this, TEXT("FBox3f::GetClosestPointTo should preserve the native closest point result"), ScriptBox3fClosestPoint, ExpectedBox3fClosestPoint, 0.001f);
	bPassed &= VerifySphere3f(*this, TEXT("FSphere3f(FSphere) should preserve the double-to-float conversion path"), ScriptConvertedSphere3f, ExpectedConvertedSphere3f, 0.001f);
	bPassed &= VerifyFloat(*this, TEXT("FSphere3f::GetVolume should preserve the native sphere volume"), ScriptSphere3fVolume, ExpectedSphere3fVolume, 0.01f);
	bPassed &= VerifyVector3f(*this, TEXT("FPlane4f(FPlane) should preserve the converted plane origin"), ScriptConvertedPlane4f.GetOrigin(), ExpectedConvertedPlane4f.GetOrigin(), 0.001f);
	bPassed &= VerifyVector3f(*this, TEXT("FPlane4f(FPlane) should preserve the converted plane normal"), ScriptConvertedPlane4f.GetNormal(), ExpectedConvertedPlane4f.GetNormal(), 0.001f);
	bPassed &= VerifyFloat(*this, TEXT("FPlane4f::PlaneDot should preserve the native signed distance"), ScriptPlane4fDot, ExpectedPlane4fDot, 0.001f);
	bPassed &= VerifyBounds3f(*this, TEXT("FBoxSphereBounds3f::ExpandBy should preserve the native origin, extent, and sphere radius"), ScriptExpandedBounds3f, ExpectedExpandedBounds3fResult, 0.001f);
	bPassed &= VerifyBounds3f(*this, TEXT("FBoxSphereBounds3f(FBoxSphereBounds) should preserve the double-to-float conversion path"), ScriptConvertedBounds3f, ExpectedConvertedBounds3f, 0.001f);
	bPassed &= VerifyFloat(*this, TEXT("FBoxSphereBounds3f::ComputeSquaredDistanceFromBoxToPoint should preserve the native distance"), ScriptBounds3fSquaredDistance, ExpectedBounds3fSquaredDistance, 0.001f);
	bPassed &= TestEqual(TEXT("FBoxSphereBounds3f::BoxesIntersect and SpheresIntersect should preserve the native boolean result"), bScriptBounds3fIntersections, bExpectedBounds3fIntersections);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
