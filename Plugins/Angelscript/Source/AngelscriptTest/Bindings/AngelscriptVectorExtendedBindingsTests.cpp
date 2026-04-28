#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFloatVectorBindingsTest,
	"Angelscript.TestModule.Bindings.VectorExtended.FloatVectorsCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptVector4BindingsTest,
	"Angelscript.TestModule.Bindings.VectorExtended.Vector4Compat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptVectorExtendedBindingsTests_Private
{
	bool ReadReturnValue(FAutomationTestBase&, asIScriptContext& Context, float& OutValue) { OutValue = Context.GetReturnFloat(); return true; }

	template <typename TValue>
	bool ReadReturnValue(FAutomationTestBase& Test, asIScriptContext& Context, TValue& OutValue)
	{
		void* ReturnValueAddress = Context.GetAddressOfReturnValue();
		if (!Test.TestNotNull(TEXT("Vector extended bindings test should expose the return value storage"), ReturnValueAddress))
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
		if (!Test.TestNotNull(TEXT("Vector extended bindings test should create an execution context"), Context))
		{
			return false;
		}

		const int PrepareResult = Context->Prepare(&Function);
		if (!Test.TestEqual(TEXT("Vector extended bindings test should prepare the script function"), PrepareResult, asSUCCESS))
		{
			Context->Release();
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Vector extended bindings test should execute the script function"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Vector extended bindings test saw a script exception: %s"),
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			Context->Release();
			return false;
		}

		const bool bReadReturnValue = ReadReturnValue(Test, *Context, OutValue);
		Context->Release();
		return bReadReturnValue;
	}

	bool VerifyVector2f(FAutomationTestBase& Test, const TCHAR* What, const FVector2f& Actual, const FVector2f& Expected, float Tolerance = KINDA_SMALL_NUMBER)
	{ return Test.TestTrue(What, Actual.Equals(Expected, Tolerance)); }

	bool VerifyVector3f(FAutomationTestBase& Test, const TCHAR* What, const FVector3f& Actual, const FVector3f& Expected, float Tolerance = KINDA_SMALL_NUMBER)
	{ return Test.TestTrue(What, Actual.Equals(Expected, Tolerance)); }

	bool VerifyVector4(FAutomationTestBase& Test, const TCHAR* What, const FVector4& Actual, const FVector4& Expected, double Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(
			What,
			FMath::Abs(Actual.X - Expected.X) <= Tolerance
				&& FMath::Abs(Actual.Y - Expected.Y) <= Tolerance
				&& FMath::Abs(Actual.Z - Expected.Z) <= Tolerance
				&& FMath::Abs(Actual.W - Expected.W) <= Tolerance);
	}

	bool VerifyVector4f(FAutomationTestBase& Test, const TCHAR* What, const FVector4f& Actual, const FVector4f& Expected, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Test.TestTrue(
			What,
			FMath::Abs(Actual.X - Expected.X) <= Tolerance
				&& FMath::Abs(Actual.Y - Expected.Y) <= Tolerance
				&& FMath::Abs(Actual.Z - Expected.Z) <= Tolerance
				&& FMath::Abs(Actual.W - Expected.W) <= Tolerance);
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptVectorExtendedBindingsTests_Private;

bool FAngelscriptFloatVectorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASFloatVectorCompat",
		TEXT(R"AS(
FVector2f GetVector2fCombined()
{
	FVector2f Value = FVector2f(3.0, 4.0) + FVector2f(1.0, -1.0);
	Value /= 2.0;
	return Value;
}

float32 GetVector2fSize()
{
	return FVector2f(3.0, 4.0).Size();
}

float32 GetVector2fDot()
{
	return FVector2f(3.0, 4.0).DotProduct(FVector2f(1.0, -1.0));
}

float32 GetVector2fCross()
{
	return FVector2f(3.0, 4.0).CrossProduct(FVector2f(1.0, -1.0));
}

FVector2f GetVector2fFrom3f()
{
	return FVector2f(FVector3f(6.0, 8.0, 10.0));
}

FVector3f GetVector3fCombined()
{
	FVector3f Value = FVector3f(1.0, 2.0, 3.0) + FVector3f(4.0, 5.0, 6.0);
	Value *= 0.5;
	return Value;
}

float32 GetVector3fSize()
{
	return FVector3f(2.0, 3.0, 6.0).Size();
}

float32 GetVector3fDot()
{
	return FVector3f(1.0, 2.0, 3.0).DotProduct(FVector3f(4.0, 5.0, 6.0));
}

FVector3f GetVector3fCross()
{
	return FVector3f::ForwardVector.CrossProduct(FVector3f::RightVector);
}

FVector3f GetVector3fNormalized()
{
	FVector3f Value = FVector3f(3.0, 4.0, 0.0);
	Value.Normalize();
	return Value;
}

FVector3f GetVector3fFromDoubleVector()
{
	return FVector3f(FVector(7.0, 8.0, 9.0));
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	const FVector2f ExpectedVector2fCombined = (FVector2f(3.0f, 4.0f) + FVector2f(1.0f, -1.0f)) / 2.0f;
	const float ExpectedVector2fSize = FVector2f(3.0f, 4.0f).Size();
	const float ExpectedVector2fDot = FVector2f::DotProduct(FVector2f(3.0f, 4.0f), FVector2f(1.0f, -1.0f));
	const float ExpectedVector2fCross = FVector2f::CrossProduct(FVector2f(3.0f, 4.0f), FVector2f(1.0f, -1.0f));
	const FVector2f ExpectedVector2fFrom3f = FVector2f(FVector3f(6.0f, 8.0f, 10.0f));

	const FVector3f ExpectedVector3fCombined = (FVector3f(1.0f, 2.0f, 3.0f) + FVector3f(4.0f, 5.0f, 6.0f)) * 0.5f;
	const float ExpectedVector3fSize = FVector3f(2.0f, 3.0f, 6.0f).Size();
	const float ExpectedVector3fDot = FVector3f::DotProduct(FVector3f(1.0f, 2.0f, 3.0f), FVector3f(4.0f, 5.0f, 6.0f));
	const FVector3f ExpectedVector3fCross = FVector3f::CrossProduct(FVector3f::ForwardVector, FVector3f::RightVector);
	FVector3f ExpectedVector3fNormalized(3.0f, 4.0f, 0.0f);
	ExpectedVector3fNormalized.Normalize();
	const FVector3f ExpectedVector3fFromDoubleVector = FVector3f(FVector(7.0, 8.0, 9.0));

	FVector2f ScriptVector2fCombined;
	float ScriptVector2fSize = 0.0f;
	float ScriptVector2fDot = 0.0f;
	float ScriptVector2fCross = 0.0f;
	FVector2f ScriptVector2fFrom3f;
	FVector3f ScriptVector3fCombined;
	float ScriptVector3fSize = 0.0f;
	float ScriptVector3fDot = 0.0f;
	FVector3f ScriptVector3fCross;
	FVector3f ScriptVector3fNormalized;
	FVector3f ScriptVector3fFromDoubleVector;

	asIScriptFunction* Vector2fCombinedFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector2f GetVector2fCombined()"));
	asIScriptFunction* Vector2fSizeFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetVector2fSize()"));
	asIScriptFunction* Vector2fDotFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetVector2fDot()"));
	asIScriptFunction* Vector2fCrossFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetVector2fCross()"));
	asIScriptFunction* Vector2fFrom3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector2f GetVector2fFrom3f()"));
	asIScriptFunction* Vector3fCombinedFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetVector3fCombined()"));
	asIScriptFunction* Vector3fSizeFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetVector3fSize()"));
	asIScriptFunction* Vector3fDotFunction = GetFunctionByDecl(*this, *Module, TEXT("float32 GetVector3fDot()"));
	asIScriptFunction* Vector3fCrossFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetVector3fCross()"));
	asIScriptFunction* Vector3fNormalizedFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetVector3fNormalized()"));
	asIScriptFunction* Vector3fFromDoubleVectorFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector3f GetVector3fFromDoubleVector()"));
	if (Vector2fCombinedFunction == nullptr
		|| Vector2fSizeFunction == nullptr
		|| Vector2fDotFunction == nullptr
		|| Vector2fCrossFunction == nullptr
		|| Vector2fFrom3fFunction == nullptr
		|| Vector3fCombinedFunction == nullptr
		|| Vector3fSizeFunction == nullptr
		|| Vector3fDotFunction == nullptr
		|| Vector3fCrossFunction == nullptr
		|| Vector3fNormalizedFunction == nullptr
		|| Vector3fFromDoubleVectorFunction == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *Vector2fCombinedFunction, ScriptVector2fCombined) &&
		ExecuteValueFunction(*this, Engine, *Vector2fSizeFunction, ScriptVector2fSize) &&
		ExecuteValueFunction(*this, Engine, *Vector2fDotFunction, ScriptVector2fDot) &&
		ExecuteValueFunction(*this, Engine, *Vector2fCrossFunction, ScriptVector2fCross) &&
		ExecuteValueFunction(*this, Engine, *Vector2fFrom3fFunction, ScriptVector2fFrom3f) &&
		ExecuteValueFunction(*this, Engine, *Vector3fCombinedFunction, ScriptVector3fCombined) &&
		ExecuteValueFunction(*this, Engine, *Vector3fSizeFunction, ScriptVector3fSize) &&
		ExecuteValueFunction(*this, Engine, *Vector3fDotFunction, ScriptVector3fDot) &&
		ExecuteValueFunction(*this, Engine, *Vector3fCrossFunction, ScriptVector3fCross) &&
		ExecuteValueFunction(*this, Engine, *Vector3fNormalizedFunction, ScriptVector3fNormalized) &&
		ExecuteValueFunction(*this, Engine, *Vector3fFromDoubleVectorFunction, ScriptVector3fFromDoubleVector);
	if (!bExecutedAll)
	{
		return false;
	}

	bPassed =
		VerifyVector2f(*this, TEXT("FVector2f arithmetic should match the native deterministic baseline"), ScriptVector2fCombined, ExpectedVector2fCombined) &&
		TestEqual(TEXT("FVector2f::Size should match the native Euclidean length"), ScriptVector2fSize, ExpectedVector2fSize) &&
		TestEqual(TEXT("FVector2f::DotProduct should match the native scalar product"), ScriptVector2fDot, ExpectedVector2fDot) &&
		TestEqual(TEXT("FVector2f::CrossProduct should match the native 2D cross product"), ScriptVector2fCross, ExpectedVector2fCross) &&
		VerifyVector2f(*this, TEXT("FVector2f(FVector3f) should preserve the XY conversion"), ScriptVector2fFrom3f, ExpectedVector2fFrom3f) &&
		VerifyVector3f(*this, TEXT("FVector3f arithmetic should match the native deterministic baseline"), ScriptVector3fCombined, ExpectedVector3fCombined) &&
		TestEqual(TEXT("FVector3f::Size should match the native Euclidean length"), ScriptVector3fSize, ExpectedVector3fSize) &&
		TestEqual(TEXT("FVector3f::DotProduct should match the native scalar product"), ScriptVector3fDot, ExpectedVector3fDot) &&
		VerifyVector3f(*this, TEXT("FVector3f::CrossProduct should match the native basis-vector cross product"), ScriptVector3fCross, ExpectedVector3fCross) &&
		VerifyVector3f(*this, TEXT("FVector3f::Normalize should match the native normalized direction"), ScriptVector3fNormalized, ExpectedVector3fNormalized) &&
		VerifyVector3f(*this, TEXT("FVector3f(FVector) should preserve the double-to-float conversion"), ScriptVector3fFromDoubleVector, ExpectedVector3fFromDoubleVector);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptVector4BindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASVector4Compat",
		TEXT(R"AS(
FVector4 GetVector4Combined()
{
	return (FVector4(1.0, 2.0, 3.0, 4.0) + FVector4(4.0, 3.0, 2.0, 1.0)) * 0.5;
}

FVector4 GetVector4FromVector()
{
	return FVector4(FVector(1.0, 2.0, 3.0), 4.0);
}

FVector4f GetVector4fCombined()
{
	return (FVector4f(1.0, 2.0, 3.0, 4.0) - FVector4f(0.5, 0.5, 0.5, 0.5)) * 2.0;
}

FVector4f GetVector4fFromVector3f()
{
	return FVector4f(FVector3f(5.0, 6.0, 7.0), 8.0);
}

FVector4f GetVector4fFromDoubleVector4()
{
	return FVector4f(FVector4(1.0, 2.0, 3.0, 4.0));
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	const FVector4 ExpectedVector4Combined = (FVector4(1.0, 2.0, 3.0, 4.0) + FVector4(4.0, 3.0, 2.0, 1.0)) * 0.5;
	const FVector4 ExpectedVector4FromVector(FVector(1.0, 2.0, 3.0), 4.0);
	const FVector4f ExpectedVector4fCombined = (FVector4f(1.0f, 2.0f, 3.0f, 4.0f) - FVector4f(0.5f, 0.5f, 0.5f, 0.5f)) * 2.0f;
	const FVector4f ExpectedVector4fFromVector3f(FVector3f(5.0f, 6.0f, 7.0f), 8.0f);
	const FVector4f ExpectedVector4fFromDoubleVector4(FVector4(1.0, 2.0, 3.0, 4.0));

	FVector4 ScriptVector4Combined;
	FVector4 ScriptVector4FromVector;
	FVector4f ScriptVector4fCombined;
	FVector4f ScriptVector4fFromVector3f;
	FVector4f ScriptVector4fFromDoubleVector4;

	asIScriptFunction* Vector4CombinedFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector4 GetVector4Combined()"));
	asIScriptFunction* Vector4FromVectorFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector4 GetVector4FromVector()"));
	asIScriptFunction* Vector4fCombinedFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector4f GetVector4fCombined()"));
	asIScriptFunction* Vector4fFromVector3fFunction = GetFunctionByDecl(*this, *Module, TEXT("FVector4f GetVector4fFromVector3f()"));
	asIScriptFunction* Vector4fFromDoubleVector4Function = GetFunctionByDecl(*this, *Module, TEXT("FVector4f GetVector4fFromDoubleVector4()"));
	if (Vector4CombinedFunction == nullptr
		|| Vector4FromVectorFunction == nullptr
		|| Vector4fCombinedFunction == nullptr
		|| Vector4fFromVector3fFunction == nullptr
		|| Vector4fFromDoubleVector4Function == nullptr)
	{
		return false;
	}

	const bool bExecutedAll =
		ExecuteValueFunction(*this, Engine, *Vector4CombinedFunction, ScriptVector4Combined) &&
		ExecuteValueFunction(*this, Engine, *Vector4FromVectorFunction, ScriptVector4FromVector) &&
		ExecuteValueFunction(*this, Engine, *Vector4fCombinedFunction, ScriptVector4fCombined) &&
		ExecuteValueFunction(*this, Engine, *Vector4fFromVector3fFunction, ScriptVector4fFromVector3f) &&
		ExecuteValueFunction(*this, Engine, *Vector4fFromDoubleVector4Function, ScriptVector4fFromDoubleVector4);
	if (!bExecutedAll)
	{
		return false;
	}

	bPassed =
		VerifyVector4(*this, TEXT("FVector4 arithmetic should match the native deterministic baseline"), ScriptVector4Combined, ExpectedVector4Combined) &&
		VerifyVector4(*this, TEXT("FVector4(FVector, W) should preserve the native constructor conversion"), ScriptVector4FromVector, ExpectedVector4FromVector) &&
		VerifyVector4f(*this, TEXT("FVector4f arithmetic should match the native deterministic baseline"), ScriptVector4fCombined, ExpectedVector4fCombined) &&
		VerifyVector4f(*this, TEXT("FVector4f(FVector3f, W) should preserve the native constructor conversion"), ScriptVector4fFromVector3f, ExpectedVector4fFromVector3f) &&
		VerifyVector4f(*this, TEXT("FVector4f(FVector4) should preserve the double-to-float conversion"), ScriptVector4fFromDoubleVector4, ExpectedVector4fFromDoubleVector4);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
