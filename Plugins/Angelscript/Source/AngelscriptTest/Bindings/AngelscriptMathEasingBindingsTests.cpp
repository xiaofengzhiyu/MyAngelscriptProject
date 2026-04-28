#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptMathEasingBindingsTests_Private
{
	template <typename TValue>
	bool ExecuteReturnValue(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* Declaration,
		TValue& OutValue)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, Declaration);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(TEXT("Math easing return-value check should create an execution context"), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully"), Declaration),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), Declaration),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("%s saw a script exception: %s"),
					Declaration,
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}

			return false;
		}

		void* ReturnValueAddress = Context->GetAddressOfReturnValue();
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("%s should expose return value storage"), Declaration),
			ReturnValueAddress))
		{
			return false;
		}

		OutValue = *static_cast<TValue*>(ReturnValueAddress);
		return true;
	}

	struct FDoubleEasingCase
	{
		const TCHAR* Declaration;
		double ExpectedValue;
	};

	struct FFloatEasingCase
	{
		const TCHAR* Declaration;
		float ExpectedValue;
	};

	struct FVectorEasingCase
	{
		const TCHAR* Declaration;
		FVector ExpectedValue;
	};

	bool VerifyDoubleCase(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const FDoubleEasingCase& EasingCase,
		const double Tolerance)
	{
		double ActualValue = 0.0;
		if (!ExecuteReturnValue(Test, Engine, Module, EasingCase.Declaration, ActualValue))
		{
			return false;
		}

		return Test.TestTrue(
			*FString::Printf(
				TEXT("%s should match native float64 easing baseline within %.9f (actual %.17g, expected %.17g)"),
				EasingCase.Declaration,
				Tolerance,
				ActualValue,
				EasingCase.ExpectedValue),
			FMath::IsNearlyEqual(ActualValue, EasingCase.ExpectedValue, Tolerance));
	}

	bool VerifyFloatCase(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const FFloatEasingCase& EasingCase,
		const float Tolerance)
	{
		float ActualValue = 0.0f;
		if (!ExecuteReturnValue(Test, Engine, Module, EasingCase.Declaration, ActualValue))
		{
			return false;
		}

		return Test.TestTrue(
			*FString::Printf(
				TEXT("%s should match native float32 easing baseline within %.6f (actual %.9g, expected %.9g)"),
				EasingCase.Declaration,
				Tolerance,
				ActualValue,
				EasingCase.ExpectedValue),
			FMath::IsNearlyEqual(ActualValue, EasingCase.ExpectedValue, Tolerance));
	}

	bool VerifyVectorCase(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const FVectorEasingCase& EasingCase,
		const double Tolerance)
	{
		FVector ActualValue = FVector::ZeroVector;
		if (!ExecuteReturnValue(Test, Engine, Module, EasingCase.Declaration, ActualValue))
		{
			return false;
		}

		return Test.TestTrue(
			*FString::Printf(
				TEXT("%s should match native FVector easing baseline within %.9f (actual %s, expected %s)"),
				EasingCase.Declaration,
				Tolerance,
				*ActualValue.ToString(),
				*EasingCase.ExpectedValue.ToString()),
			ActualValue.Equals(EasingCase.ExpectedValue, Tolerance));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptMathEasingBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptMathEasingOverloadsBindingsTest,
	"Angelscript.TestModule.Bindings.MathEasingOverloadsCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptMathEasingOverloadsBindingsTest::RunTest(const FString& Parameters)
{
	constexpr double DoubleA = -2.25;
	constexpr double DoubleB = 11.75;
	constexpr double DoubleAlpha = 0.37;
	constexpr double DoubleExp = 2.4;

	constexpr float FloatA = -1.5f;
	constexpr float FloatB = 5.25f;
	constexpr float FloatAlpha = 0.42f;
	constexpr float FloatExp = 3.1f;

	const FVector VectorA(-2.0, 4.0, 8.0);
	const FVector VectorB(10.0, -6.0, 3.0);
	constexpr float VectorAlpha = 0.33f;
	constexpr float VectorExp = 2.2f;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = nullptr;
	ASTEST_BUILD_MODULE(
		Engine,
		"ASMathEasingOverloadsCompat",
		TEXT(R"AS(
float64 DoubleA()
{
	return -2.25;
}

float64 DoubleB()
{
	return 11.75;
}

float64 DoubleAlpha()
{
	return 0.37;
}

float64 DoubleExp()
{
	return 2.4;
}

float32 FloatA()
{
	return -1.5f;
}

float32 FloatB()
{
	return 5.25f;
}

float32 FloatAlpha()
{
	return 0.42f;
}

float32 FloatExp()
{
	return 3.1f;
}

FVector VectorA()
{
	return FVector(-2.0, 4.0, 8.0);
}

FVector VectorB()
{
	return FVector(10.0, -6.0, 3.0);
}

float32 VectorAlpha()
{
	return 0.33f;
}

float32 VectorExp()
{
	return 2.2f;
}

float64 GetDoubleEaseIn()
{
	return Math::EaseIn(DoubleA(), DoubleB(), DoubleAlpha(), DoubleExp());
}

float64 GetDoubleEaseOut()
{
	return Math::EaseOut(DoubleA(), DoubleB(), DoubleAlpha(), DoubleExp());
}

float64 GetDoubleEaseInOut()
{
	return Math::EaseInOut(DoubleA(), DoubleB(), DoubleAlpha(), DoubleExp());
}

float64 GetDoubleSinusoidalIn()
{
	return Math::SinusoidalIn(DoubleA(), DoubleB(), DoubleAlpha());
}

float64 GetDoubleSinusoidalOut()
{
	return Math::SinusoidalOut(DoubleA(), DoubleB(), DoubleAlpha());
}

float64 GetDoubleSinusoidalInOut()
{
	return Math::SinusoidalInOut(DoubleA(), DoubleB(), DoubleAlpha());
}

float64 GetDoubleExpoIn()
{
	return Math::ExpoIn(DoubleA(), DoubleB(), DoubleAlpha());
}

float64 GetDoubleExpoOut()
{
	return Math::ExpoOut(DoubleA(), DoubleB(), DoubleAlpha());
}

float64 GetDoubleExpoInOut()
{
	return Math::ExpoInOut(DoubleA(), DoubleB(), DoubleAlpha());
}

float64 GetDoubleCircularIn()
{
	return Math::CircularIn(DoubleA(), DoubleB(), DoubleAlpha());
}

float64 GetDoubleCircularOut()
{
	return Math::CircularOut(DoubleA(), DoubleB(), DoubleAlpha());
}

float64 GetDoubleCircularInOut()
{
	return Math::CircularInOut(DoubleA(), DoubleB(), DoubleAlpha());
}

float32 GetFloatEaseIn()
{
	return Math::EaseIn(FloatA(), FloatB(), FloatAlpha(), FloatExp());
}

float32 GetFloatEaseOut()
{
	return Math::EaseOut(FloatA(), FloatB(), FloatAlpha(), FloatExp());
}

float32 GetFloatEaseInOut()
{
	return Math::EaseInOut(FloatA(), FloatB(), FloatAlpha(), FloatExp());
}

float32 GetFloatSinusoidalIn()
{
	return Math::SinusoidalIn(FloatA(), FloatB(), FloatAlpha());
}

float32 GetFloatSinusoidalOut()
{
	return Math::SinusoidalOut(FloatA(), FloatB(), FloatAlpha());
}

float32 GetFloatSinusoidalInOut()
{
	return Math::SinusoidalInOut(FloatA(), FloatB(), FloatAlpha());
}

float32 GetFloatExpoIn()
{
	return Math::ExpoIn(FloatA(), FloatB(), FloatAlpha());
}

float32 GetFloatExpoOut()
{
	return Math::ExpoOut(FloatA(), FloatB(), FloatAlpha());
}

float32 GetFloatExpoInOut()
{
	return Math::ExpoInOut(FloatA(), FloatB(), FloatAlpha());
}

float32 GetFloatCircularIn()
{
	return Math::CircularIn(FloatA(), FloatB(), FloatAlpha());
}

float32 GetFloatCircularOut()
{
	return Math::CircularOut(FloatA(), FloatB(), FloatAlpha());
}

float32 GetFloatCircularInOut()
{
	return Math::CircularInOut(FloatA(), FloatB(), FloatAlpha());
}

FVector GetVectorEaseIn()
{
	return Math::EaseIn(VectorA(), VectorB(), VectorAlpha(), VectorExp());
}

FVector GetVectorEaseOut()
{
	return Math::EaseOut(VectorA(), VectorB(), VectorAlpha(), VectorExp());
}

FVector GetVectorEaseInOut()
{
	return Math::EaseInOut(VectorA(), VectorB(), VectorAlpha(), VectorExp());
}

FVector GetVectorSinusoidalInOut()
{
	return Math::SinusoidalInOut(VectorA(), VectorB(), VectorAlpha());
}

FVector GetVectorExpoOut()
{
	return Math::ExpoOut(VectorA(), VectorB(), VectorAlpha());
}

FVector GetVectorCircularInOut()
{
	return Math::CircularInOut(VectorA(), VectorB(), VectorAlpha());
}
)AS"),
		Module);

	const FDoubleEasingCase DoubleCases[] =
	{
		{ TEXT("float64 GetDoubleEaseIn()"), FMath::InterpEaseIn<double>(DoubleA, DoubleB, DoubleAlpha, DoubleExp) },
		{ TEXT("float64 GetDoubleEaseOut()"), FMath::InterpEaseOut<double>(DoubleA, DoubleB, DoubleAlpha, DoubleExp) },
		{ TEXT("float64 GetDoubleEaseInOut()"), FMath::InterpEaseInOut<double>(DoubleA, DoubleB, DoubleAlpha, DoubleExp) },
		{ TEXT("float64 GetDoubleSinusoidalIn()"), FMath::InterpSinIn<double>(DoubleA, DoubleB, DoubleAlpha) },
		{ TEXT("float64 GetDoubleSinusoidalOut()"), FMath::InterpSinOut<double>(DoubleA, DoubleB, DoubleAlpha) },
		{ TEXT("float64 GetDoubleSinusoidalInOut()"), FMath::InterpSinInOut<double>(DoubleA, DoubleB, DoubleAlpha) },
		{ TEXT("float64 GetDoubleExpoIn()"), FMath::InterpExpoIn<double>(DoubleA, DoubleB, DoubleAlpha) },
		{ TEXT("float64 GetDoubleExpoOut()"), FMath::InterpExpoOut<double>(DoubleA, DoubleB, DoubleAlpha) },
		{ TEXT("float64 GetDoubleExpoInOut()"), FMath::InterpExpoInOut<double>(DoubleA, DoubleB, DoubleAlpha) },
		{ TEXT("float64 GetDoubleCircularIn()"), FMath::InterpCircularIn<double>(DoubleA, DoubleB, DoubleAlpha) },
		{ TEXT("float64 GetDoubleCircularOut()"), FMath::InterpCircularOut<double>(DoubleA, DoubleB, DoubleAlpha) },
		{ TEXT("float64 GetDoubleCircularInOut()"), FMath::InterpCircularInOut<double>(DoubleA, DoubleB, DoubleAlpha) },
	};

	const FFloatEasingCase FloatCases[] =
	{
		{ TEXT("float32 GetFloatEaseIn()"), FMath::InterpEaseIn<float>(FloatA, FloatB, FloatAlpha, FloatExp) },
		{ TEXT("float32 GetFloatEaseOut()"), FMath::InterpEaseOut<float>(FloatA, FloatB, FloatAlpha, FloatExp) },
		{ TEXT("float32 GetFloatEaseInOut()"), FMath::InterpEaseInOut<float>(FloatA, FloatB, FloatAlpha, FloatExp) },
		{ TEXT("float32 GetFloatSinusoidalIn()"), FMath::InterpSinIn<float>(FloatA, FloatB, FloatAlpha) },
		{ TEXT("float32 GetFloatSinusoidalOut()"), FMath::InterpSinOut<float>(FloatA, FloatB, FloatAlpha) },
		{ TEXT("float32 GetFloatSinusoidalInOut()"), FMath::InterpSinInOut<float>(FloatA, FloatB, FloatAlpha) },
		{ TEXT("float32 GetFloatExpoIn()"), FMath::InterpExpoIn<float>(FloatA, FloatB, FloatAlpha) },
		{ TEXT("float32 GetFloatExpoOut()"), FMath::InterpExpoOut<float>(FloatA, FloatB, FloatAlpha) },
		{ TEXT("float32 GetFloatExpoInOut()"), FMath::InterpExpoInOut<float>(FloatA, FloatB, FloatAlpha) },
		{ TEXT("float32 GetFloatCircularIn()"), FMath::InterpCircularIn<float>(FloatA, FloatB, FloatAlpha) },
		{ TEXT("float32 GetFloatCircularOut()"), FMath::InterpCircularOut<float>(FloatA, FloatB, FloatAlpha) },
		{ TEXT("float32 GetFloatCircularInOut()"), FMath::InterpCircularInOut<float>(FloatA, FloatB, FloatAlpha) },
	};

	const FVectorEasingCase VectorCases[] =
	{
		{ TEXT("FVector GetVectorEaseIn()"), FMath::InterpEaseIn<FVector>(VectorA, VectorB, VectorAlpha, VectorExp) },
		{ TEXT("FVector GetVectorEaseOut()"), FMath::InterpEaseOut<FVector>(VectorA, VectorB, VectorAlpha, VectorExp) },
		{ TEXT("FVector GetVectorEaseInOut()"), FMath::InterpEaseInOut<FVector>(VectorA, VectorB, VectorAlpha, VectorExp) },
		{ TEXT("FVector GetVectorSinusoidalInOut()"), FMath::InterpSinInOut<FVector>(VectorA, VectorB, VectorAlpha) },
		{ TEXT("FVector GetVectorExpoOut()"), FMath::InterpExpoOut<FVector>(VectorA, VectorB, VectorAlpha) },
		{ TEXT("FVector GetVectorCircularInOut()"), FMath::InterpCircularInOut<FVector>(VectorA, VectorB, VectorAlpha) },
	};

	bool bAllMatched = true;
	constexpr double DoubleTolerance = 1.0e-6;
	constexpr float FloatTolerance = 1.0e-4f;
	constexpr double VectorTolerance = 1.0e-6;

	for (const FDoubleEasingCase& EasingCase : DoubleCases)
	{
		bAllMatched &= VerifyDoubleCase(*this, Engine, *Module, EasingCase, DoubleTolerance);
	}

	for (const FFloatEasingCase& EasingCase : FloatCases)
	{
		bAllMatched &= VerifyFloatCase(*this, Engine, *Module, EasingCase, FloatTolerance);
	}

	for (const FVectorEasingCase& EasingCase : VectorCases)
	{
		bAllMatched &= VerifyVectorCase(*this, Engine, *Module, EasingCase, VectorTolerance);
	}

	TestTrue(TEXT("Math easing overload cases should all match same-run native baselines"), bAllMatched);

	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
