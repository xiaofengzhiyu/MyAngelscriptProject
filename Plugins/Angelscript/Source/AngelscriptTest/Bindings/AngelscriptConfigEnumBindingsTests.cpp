#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_ConfigEnumBindingsTests_Private
{
	static constexpr ANSICHAR ConfigEnumAliasModuleName[] = "ASConfigEnumAliases";
	static constexpr ANSICHAR ConfigEnumRoundTripModuleName[] = "ASConfigEnumAliasRoundTrips";

	struct FConfigEnumBaseline
	{
		int32 VisibilityTraceType = INDEX_NONE;
		int32 CameraTraceType = INDEX_NONE;
		int32 WorldStaticObjectType = INDEX_NONE;
		int32 WorldDynamicObjectType = INDEX_NONE;
		int32 PhysicsBodyObjectType = INDEX_NONE;
	};

	bool ReadConfigEnumBaseline(
		FAutomationTestBase& Test,
		FConfigEnumBaseline& OutBaseline)
	{
		UCollisionProfile* CollisionProfile = UCollisionProfile::Get();
		if (!Test.TestNotNull(TEXT("Config enum binding tests should access the native collision profile singleton"), CollisionProfile))
		{
			return false;
		}

		OutBaseline.VisibilityTraceType = static_cast<int32>(CollisionProfile->ConvertToTraceType(ECC_Visibility));
		OutBaseline.CameraTraceType = static_cast<int32>(CollisionProfile->ConvertToTraceType(ECC_Camera));
		OutBaseline.WorldStaticObjectType = static_cast<int32>(CollisionProfile->ConvertToObjectType(ECC_WorldStatic));
		OutBaseline.WorldDynamicObjectType = static_cast<int32>(CollisionProfile->ConvertToObjectType(ECC_WorldDynamic));
		OutBaseline.PhysicsBodyObjectType = static_cast<int32>(CollisionProfile->ConvertToObjectType(ECC_PhysicsBody));

		bool bPassed = true;
		bPassed &= Test.TestNotEqual(
			TEXT("Visibility trace type baseline should resolve to a bound enum value"),
			OutBaseline.VisibilityTraceType,
			static_cast<int32>(ETraceTypeQuery::TraceTypeQuery_MAX));
		bPassed &= Test.TestNotEqual(
			TEXT("Camera trace type baseline should resolve to a bound enum value"),
			OutBaseline.CameraTraceType,
			static_cast<int32>(ETraceTypeQuery::TraceTypeQuery_MAX));
		bPassed &= Test.TestNotEqual(
			TEXT("WorldStatic object type baseline should resolve to a bound enum value"),
			OutBaseline.WorldStaticObjectType,
			static_cast<int32>(EObjectTypeQuery::ObjectTypeQuery_MAX));
		bPassed &= Test.TestNotEqual(
			TEXT("WorldDynamic object type baseline should resolve to a bound enum value"),
			OutBaseline.WorldDynamicObjectType,
			static_cast<int32>(EObjectTypeQuery::ObjectTypeQuery_MAX));
		bPassed &= Test.TestNotEqual(
			TEXT("PhysicsBody object type baseline should resolve to a bound enum value"),
			OutBaseline.PhysicsBodyObjectType,
			static_cast<int32>(EObjectTypeQuery::ObjectTypeQuery_MAX));
		return bPassed;
	}

	FString BuildConfigEnumAliasScript(const FConfigEnumBaseline& Baseline)
	{
		FString Script = TEXT(R"(
int Entry()
{
	if (int(ETraceTypeQuery::Visibility) != $VISIBILITY_TRACE_TYPE$)
		return 10;
	if (int(ETraceTypeQuery::Camera) != $CAMERA_TRACE_TYPE$)
		return 20;
	if (int(EObjectTypeQuery::WorldStatic) != $WORLD_STATIC_OBJECT_TYPE$)
		return 30;
	if (int(EObjectTypeQuery::WorldDynamic) != $WORLD_DYNAMIC_OBJECT_TYPE$)
		return 40;
	if (int(EObjectTypeQuery::PhysicsBody) != $PHYSICS_BODY_OBJECT_TYPE$)
		return 50;
	if (int(ETraceTypeQuery::Visibility) == int(ETraceTypeQuery::Camera))
		return 60;
	if (int(EObjectTypeQuery::WorldStatic) == int(EObjectTypeQuery::WorldDynamic))
		return 70;
	if (int(EObjectTypeQuery::WorldStatic) == int(EObjectTypeQuery::PhysicsBody))
		return 80;
	return 1;
}
)");

		Script.ReplaceInline(TEXT("$VISIBILITY_TRACE_TYPE$"), *LexToString(Baseline.VisibilityTraceType));
		Script.ReplaceInline(TEXT("$CAMERA_TRACE_TYPE$"), *LexToString(Baseline.CameraTraceType));
		Script.ReplaceInline(TEXT("$WORLD_STATIC_OBJECT_TYPE$"), *LexToString(Baseline.WorldStaticObjectType));
		Script.ReplaceInline(TEXT("$WORLD_DYNAMIC_OBJECT_TYPE$"), *LexToString(Baseline.WorldDynamicObjectType));
		Script.ReplaceInline(TEXT("$PHYSICS_BODY_OBJECT_TYPE$"), *LexToString(Baseline.PhysicsBodyObjectType));
		return Script;
	}

	FString BuildConfigEnumRoundTripScript()
	{
		return TEXT(R"(
int Entry()
{
	if (int(UCollisionProfile::ConvertToCollisionChannel(true, int(ETraceTypeQuery::Visibility))) != int(ECollisionChannel::ECC_Visibility))
		return 10;
	if (int(UCollisionProfile::ConvertToCollisionChannel(true, int(ETraceTypeQuery::Camera))) != int(ECollisionChannel::ECC_Camera))
		return 20;
	if (int(UCollisionProfile::ConvertToCollisionChannel(false, int(EObjectTypeQuery::WorldStatic))) != int(ECollisionChannel::ECC_WorldStatic))
		return 30;
	if (int(UCollisionProfile::ConvertToCollisionChannel(false, int(EObjectTypeQuery::WorldDynamic))) != int(ECollisionChannel::ECC_WorldDynamic))
		return 40;
	if (int(UCollisionProfile::ConvertToCollisionChannel(false, int(EObjectTypeQuery::PhysicsBody))) != int(ECollisionChannel::ECC_PhysicsBody))
		return 50;
	return 1;
}
)");
	}
}

using namespace AngelscriptTest_Bindings_ConfigEnumBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptConfigEnumAliasBindingsTest,
	"Angelscript.TestModule.Bindings.ConfigEnumAliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptConfigEnumAliasRoundTripBindingsTest,
	"Angelscript.TestModule.Bindings.ConfigEnumAliasRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptConfigEnumAliasBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FConfigEnumBaseline Baseline;
	if (!ReadConfigEnumBaseline(*this, Baseline))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ConfigEnumAliasModuleName,
		BuildConfigEnumAliasScript(Baseline));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Config enum alias bindings should expose the expected built-in trace and object type aliases"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptConfigEnumAliasRoundTripBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FConfigEnumBaseline Baseline;
	if (!ReadConfigEnumBaseline(*this, Baseline))
	{
		return false;
	}

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		ConfigEnumRoundTripModuleName,
		BuildConfigEnumRoundTripScript());
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Config enum alias bindings should remain compatible with collision-profile round-trip helpers"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
