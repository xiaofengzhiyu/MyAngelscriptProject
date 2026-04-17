#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Components/InputComponent.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptObjectCastCompatBindingsTest,
	"Angelscript.TestModule.Bindings.ObjectCastCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptObjectCastNullAndInterfaceCompatBindingsTest,
	"Angelscript.TestModule.Bindings.ObjectCastNullAndInterfaceCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptObjectEditorOnlyBindingsTest,
	"Angelscript.TestModule.Bindings.ObjectEditorOnlyCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptObjectEditorOnlyParityBindingsTest,
	"Angelscript.TestModule.Bindings.ObjectEditorOnlyParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTimespanBindingsTest,
	"Angelscript.TestModule.Bindings.TimespanCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDateTimeBindingsTest,
	"Angelscript.TestModule.Bindings.DateTimeCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptObjectCastCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
	ASTEST_BEGIN_SHARE
	asIScriptModule* PlainModule = BuildModule(
		*this,
		Engine,
		"ASCastCompat",
		TEXT(R"(
int Entry()
{
	UObject Object = FindObject(GetTransientPackage().GetPathName());
	UPackage Package = Cast<UPackage>(Object);
	FName LiteralName = n"Compat_Name";

	if (!IsValid(Package))
		return 0;
	if (!(LiteralName == FName("Compat_Name")))
		return 0;

	return 1;
}
)"));
	if (PlainModule == nullptr)
	{
		return false;
	}

	asIScriptFunction* PlainFunction = GetFunctionByDecl(*this, *PlainModule, TEXT("int Entry()"));
	if (PlainFunction == nullptr)
	{
		return false;
	}

	int32 PlainResult = 0;
	if (!ExecuteIntFunction(*this, Engine, *PlainFunction, PlainResult))
	{
		return false;
	}
	if (!TestEqual(TEXT("Plain module Cast<T> and n\"\" compat syntax should behave as expected"), PlainResult, 1))
	{
		return false;
	}
	const bool bAnnotatedCompiled = CompileAnnotatedModuleFromMemory(
		&Engine,
		TEXT("ASAnnotatedCastCompat"),
		TEXT("ASAnnotatedCastCompat.as"),
		TEXT(R"(
UCLASS()
class ABindingCastActor : AActor
{
}

UCLASS()
class UBindingCastComponent : UActorComponent
{
	UFUNCTION()
	int ReadCastCompat()
	{
		ABindingCastActor OwnerActor = Cast<ABindingCastActor>(GetOwner());
		FName ExpectedName = n"BindingCastOwner";

		if (OwnerActor == null)
			return 0;
		if (!(ExpectedName == FName("BindingCastOwner")))
			return 0;

		return 1;
	}
}

)"));
	if (!TestTrue(TEXT("Compile annotated module using Cast<T> compat syntax should succeed"), bAnnotatedCompiled))
	{
		return false;
	}

	UClass* RuntimeActorClass = FindGeneratedClass(&Engine, TEXT("ABindingCastActor"));
	UClass* RuntimeComponentClass = FindGeneratedClass(&Engine, TEXT("UBindingCastComponent"));
	if (!TestNotNull(TEXT("Generated actor class for compat cast should exist"), RuntimeActorClass) ||
		!TestNotNull(TEXT("Generated component class for compat cast should exist"), RuntimeComponentClass))
	{
		return false;
	}

	UFunction* ReadCastCompatFunction = FindGeneratedFunction(RuntimeComponentClass, TEXT("ReadCastCompat"));
	if (!TestNotNull(TEXT("Compat cast function should exist"), ReadCastCompatFunction))
	{
		return false;
	}
	AActor* RuntimeActor = NewObject<AActor>(GetTransientPackage(), RuntimeActorClass);
	if (!TestNotNull(TEXT("Generated compat actor instance should be created"), RuntimeActor))
	{
		return false;
	}

	UActorComponent* RuntimeComponent = NewObject<UActorComponent>(RuntimeActor, RuntimeComponentClass);
	if (!TestNotNull(TEXT("Generated compat component instance should be created"), RuntimeComponent))
	{
		return false;
	}

	int32 AnnotatedResult = 0;
	if (!TestTrue(TEXT("Compat cast reflected call should execute on the game thread"), ExecuteGeneratedIntEventOnGameThread(&Engine, RuntimeComponent, ReadCastCompatFunction, AnnotatedResult)))
	{
		return false;
	}
	TestEqual(TEXT("Annotated module Cast<T> should cast native return values to generated script classes"), AnnotatedResult, 1);
	bPassed = AnnotatedResult == 1;
	ASTEST_END_SHARE

	return bPassed;
}

bool FAngelscriptObjectCastNullAndInterfaceCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	asIScriptModule* NullModule = BuildModule(
		*this,
		Engine,
		"ASObjectCastNullCompat",
		TEXT(R"(
int Entry()
{
	UObject NullObject = nullptr;
	UPackage NullPackage = Cast<UPackage>(NullObject);

	if (NullPackage != nullptr)
		return 0;

	return 1;
}
)"));
	if (NullModule == nullptr)
	{
		return false;
	}

	asIScriptFunction* NullFunction = GetFunctionByDecl(*this, *NullModule, TEXT("int Entry()"));
	if (NullFunction == nullptr)
	{
		return false;
	}

	int32 NullResult = 0;
	if (!ExecuteIntFunction(*this, Engine, *NullFunction, NullResult))
	{
		return false;
	}
	if (!TestEqual(TEXT("Cast<T> should return null when the source object handle is null"), NullResult, 1))
	{
		return false;
	}

	const bool bAnnotatedCompiled = CompileAnnotatedModuleFromMemory(
		&Engine,
		TEXT("ASObjectCastInterfaceCompat"),
		TEXT("ASObjectCastInterfaceCompat.as"),
		TEXT(R"(
UINTERFACE()
interface UIDamageableCastCompat
{
	void Touch();
}

UCLASS()
class UBindingObjectCastInterfaceImpl : UObject, UIDamageableCastCompat
{
	UFUNCTION()
	void Touch() {}

	UFUNCTION()
	int ProbeSelfCast()
	{
		UObject Self = this;
		UIDamageableCastCompat Casted = Cast<UIDamageableCastCompat>(Self);
		return Casted != nullptr ? 1 : 0;
	}
}

UCLASS()
class UBindingObjectCastInterfaceMiss : UObject
{
	UFUNCTION()
	int ProbeSelfCast()
	{
		UObject Self = this;
		UIDamageableCastCompat Casted = Cast<UIDamageableCastCompat>(Self);
		return Casted == nullptr ? 1 : 0;
	}
}

)"));
	if (!TestTrue(TEXT("Compile annotated object/interface cast module should succeed"), bAnnotatedCompiled))
	{
		return false;
	}

	UClass* InterfaceClass = FindGeneratedClass(&Engine, TEXT("UIDamageableCastCompat"));
	UClass* ImplementerClass = FindGeneratedClass(&Engine, TEXT("UBindingObjectCastInterfaceImpl"));
	UClass* MissClass = FindGeneratedClass(&Engine, TEXT("UBindingObjectCastInterfaceMiss"));
	if (!TestNotNull(TEXT("Generated interface class should exist"), InterfaceClass) ||
		!TestNotNull(TEXT("Generated implementing object class should exist"), ImplementerClass) ||
		!TestNotNull(TEXT("Generated non-implementing object class should exist"), MissClass))
	{
		return false;
	}
	if (!TestTrue(TEXT("Implementing class should report the generated interface"), ImplementerClass->ImplementsInterface(InterfaceClass)))
	{
		return false;
	}
	if (!TestFalse(TEXT("Non-implementing class should not report the generated interface"), MissClass->ImplementsInterface(InterfaceClass)))
	{
		return false;
	}

	UFunction* ImplementerFunction = FindGeneratedFunction(ImplementerClass, TEXT("ProbeSelfCast"));
	UFunction* MissFunction = FindGeneratedFunction(MissClass, TEXT("ProbeSelfCast"));
	if (!TestNotNull(TEXT("Implementing object probe function should exist"), ImplementerFunction) ||
		!TestNotNull(TEXT("Non-implementing object probe function should exist"), MissFunction))
	{
		return false;
	}

	UObject* ImplementerObject = NewObject<UObject>(GetTransientPackage(), ImplementerClass);
	UObject* MissObject = NewObject<UObject>(GetTransientPackage(), MissClass);
	if (!TestNotNull(TEXT("Implementing object instance should be created"), ImplementerObject) ||
		!TestNotNull(TEXT("Non-implementing object instance should be created"), MissObject))
	{
		return false;
	}

	int32 ImplementerResult = 0;
	if (!TestTrue(
		TEXT("Implementing object cast probe should execute on the game thread"),
		ExecuteGeneratedIntEventOnGameThread(&Engine, ImplementerObject, ImplementerFunction, ImplementerResult)))
	{
		return false;
	}
	if (!TestEqual(TEXT("Cast<T> should return the same object when it implements the requested interface"), ImplementerResult, 1))
	{
		return false;
	}

	int32 MissResult = 0;
	if (!TestTrue(
		TEXT("Non-implementing object cast probe should execute on the game thread"),
		ExecuteGeneratedIntEventOnGameThread(&Engine, MissObject, MissFunction, MissResult)))
	{
		return false;
	}
	if (!TestEqual(TEXT("Cast<T> should return null when the object does not implement the requested interface"), MissResult, 1))
	{
		return false;
	}

	bPassed = true;
	ASTEST_END_SHARE_CLEAN

	return bPassed;
}

bool FAngelscriptObjectEditorOnlyBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
	ASTEST_BEGIN_SHARE
	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASObjectEditorOnlyCompat",
		TEXT(R"(
int Entry()
{
	UPackage Package = GetTransientPackage();
	if (Package.IsEditorOnly())
		return 10;

	return 1;
}
)"));
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

	TestEqual(TEXT("UObject editor-only binding should behave as expected"), Result, 1);
	ASTEST_END_SHARE

	return true;
}

bool FAngelscriptObjectEditorOnlyParityBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	const FName NonEditorOnlyName(*FString::Printf(
		TEXT("ASObjectEditorOnlyFalse_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FName EditorOnlyName(*FString::Printf(
		TEXT("ASObjectEditorOnlyTrue_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));

	UInputComponent* NonEditorOnlyComponent = NewObject<UInputComponent>(GetTransientPackage(), NonEditorOnlyName, RF_Transient);
	UInputComponent* EditorOnlyComponent = NewObject<UInputComponent>(GetTransientPackage(), EditorOnlyName, RF_Transient);
	if (!TestNotNull(TEXT("Non-editor-only input component should be created"), NonEditorOnlyComponent) ||
		!TestNotNull(TEXT("Editor-only input component should be created"), EditorOnlyComponent))
	{
		return false;
	}

	EditorOnlyComponent->bIsEditorOnly = true;

	ON_SCOPE_EXIT
	{
		if (EditorOnlyComponent != nullptr)
		{
			EditorOnlyComponent->bIsEditorOnly = false;
			EditorOnlyComponent->MarkAsGarbage();
		}

		if (NonEditorOnlyComponent != nullptr)
		{
			NonEditorOnlyComponent->MarkAsGarbage();
		}
	};

	const bool bNativeNonEditorOnly = NonEditorOnlyComponent->IsEditorOnly();
	const bool bNativeEditorOnly = EditorOnlyComponent->IsEditorOnly();
	if (!TestFalse(TEXT("Default transient input component should remain non-editor-only"), bNativeNonEditorOnly) ||
		!TestTrue(TEXT("Input component with bIsEditorOnly should report editor-only natively"), bNativeEditorOnly))
	{
		return false;
	}

	const FString ScriptSource = FString::Printf(
		TEXT(R"(
int Entry()
{
	UObject NonEditorOnly = FindObject("%s");
	UObject EditorOnly = FindObject("%s");

	if (NonEditorOnly == null)
		return 100;
	if (EditorOnly == null)
		return 200;

	int Result = 0;
	if (NonEditorOnly.IsEditorOnly())
		Result += 2;
	if (EditorOnly.IsEditorOnly())
		Result += 1;

	return Result;
}
)"),
		*NonEditorOnlyComponent->GetPathName().ReplaceCharWithEscapedChar(),
		*EditorOnlyComponent->GetPathName().ReplaceCharWithEscapedChar());

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASObjectEditorOnlyParity",
		ScriptSource);
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

	const int32 ExpectedResult = (bNativeNonEditorOnly ? 2 : 0) + (bNativeEditorOnly ? 1 : 0);
	if (!TestEqual(TEXT("Script IsEditorOnly() should match native results for both transient components"), Result, ExpectedResult))
	{
		return false;
	}

	ASTEST_END_SHARE_CLEAN
	return true;
}

bool FAngelscriptTimespanBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
	ASTEST_BEGIN_SHARE
	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASTimespanCompat",
		TEXT(R"(
int Entry()
{
	FTimespan Zero = FTimespan::Zero();
	if (!Zero.IsZero())
		return 10;

	FTimespan NinetySeconds = FTimespan::FromSeconds(90.0);
	if (NinetySeconds.GetMinutes() != 1)
		return 20;
	if (NinetySeconds.GetSeconds() != 30)
		return 30;
	if (NinetySeconds.GetTotalSeconds() != 90.0)
		return 40;

	FTimespan TwoHours = FTimespan::FromHours(2.0);
	if (TwoHours.GetHours() != 2)
		return 50;
	if (TwoHours.GetTotalMinutes() != 120.0)
		return 60;

	FTimespan Constructed(1, 2, 3);
	if (Constructed.GetHours() != 1)
		return 70;
	if (Constructed.GetMinutes() != 2)
		return 80;
	if (Constructed.GetSeconds() != 3)
		return 90;

	FTimespan Copy = Constructed;
	if (!(Copy == Constructed))
		return 100;
	if (Copy.opCmp(Constructed) != 0)
		return 110;

	FTimespan Longer = FTimespan::FromHours(2.0);
	if (!(Longer.opCmp(Constructed) > 0))
		return 120;
	if (Longer.GetTotalDays() <= 0.0)
		return 125;
	if (Longer.ToString().IsEmpty())
		return 130;

	FTimespan Sum = Constructed + FTimespan::FromMinutes(30.0);
	if (Sum.GetTotalMinutes() < Constructed.GetTotalMinutes() + 29.99 || Sum.GetTotalMinutes() > Constructed.GetTotalMinutes() + 30.01)
		return 140;

	FTimespan Difference = Sum - Constructed;
	if (Difference.GetTotalMinutes() < 29.99 || Difference.GetTotalMinutes() > 30.01)
		return 150;

	FTimespan Doubled = Difference * 2.0;
	if (Doubled.GetTotalMinutes() < 59.99 || Doubled.GetTotalMinutes() > 60.01)
		return 160;

	FTimespan Halved = Doubled / 2.0;
	if (Halved.GetTotalMinutes() < 29.99 || Halved.GetTotalMinutes() > 30.01)
		return 170;

	return 1;
}
)"));
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

	TestEqual(TEXT("Timespan compat operations should behave as expected"), Result, 1);
	ASTEST_END_SHARE

	return true;
}

bool FAngelscriptDateTimeBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
	ASTEST_BEGIN_SHARE
	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASDateTimeCompat",
		TEXT(R"(
int Entry()
{
	FDateTime Epoch = FDateTime::FromUnixTimestamp(0);
	if (Epoch.GetYear() != 1970)
		return 10;
	if (Epoch.GetMonth() != 1)
		return 20;
	if (Epoch.GetDay() != 1)
		return 30;
	if (Epoch.ToUnixTimestamp() != 0)
		return 40;

	FDateTime Constructed(2024, 12, 25, 14, 30, 15, 0);
	if (Constructed.GetYear() != 2024)
		return 50;
	if (Constructed.GetMonth() != 12)
		return 60;
	if (Constructed.GetDay() != 25)
		return 70;
	if (Constructed.GetHour() != 14)
		return 80;
	if (Constructed.GetMinute() != 30)
		return 90;
	if (Constructed.GetSecond() != 15)
		return 100;
	if (!Constructed.IsAfternoon())
		return 110;
	if (Constructed.IsMorning())
		return 120;

	FDateTime Copy = Constructed;
	if (!(Copy == Constructed))
		return 130;
	if (Copy.opCmp(Constructed) != 0)
		return 140;

	FTimespan OneDay = FTimespan::FromDays(1.0);
	FDateTime NextDay = Constructed + OneDay;
	if (!(NextDay.opCmp(Constructed) > 0))
		return 145;
	NextDay -= OneDay;
	if (!(NextDay == Constructed))
		return 147;

	if (FDateTime::DaysInMonth(2024, 2) != 29)
		return 148;
	if (!FDateTime::IsLeapYear(2024))
		return 149;
	if (FDateTime::DaysInYear(2024) != 366)
		return 150;

	if (Epoch.opCmp(Constructed) >= 0)
		return 160;
	if (Constructed.ToIso8601().IsEmpty())
		return 170;
	if (Constructed.ToString().IsEmpty())
		return 180;
	if (Constructed.ToString("%%Y-%%m-%%d").IsEmpty())
		return 190;

	FDateTime Today = FDateTime::Today();
	if (Today.GetHour() != 0)
		return 200;

	FDateTime Now = FDateTime::Now();
	if (Now.GetYear() < 2020)
		return 210;

	FDateTime UtcNow = FDateTime::UtcNow();
	if (UtcNow.GetYear() < 2020)
		return 220;

	return 1;
}
)"));
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

	TestEqual(TEXT("DateTime compat operations should behave as expected"), Result, 1);
	ASTEST_END_SHARE

	return true;
}

#endif
